#include "BsdiffWrapper.h"

extern "C" {
#include "thirdparty/bsdiff/bsdiff.h"
#include "thirdparty/bsdiff/bspatch.h"
}
#include "thirdparty/bzip2/bzlib.h"

#include <QByteArray>
#include <QFile>

#include <cstdlib>
#include <cstring>

namespace {

constexpr char kPatchMagic[16] = {'E', 'N', 'D', 'S', 'L', 'E', 'Y', '/',
                                  'B', 'S', 'D', 'I', 'F', 'F', '4', '3'};

// 8 字节大端 int64（与 bsdiff 的 offtout / bspatch 的 offtin 一致）
void offtout(int64_t x, uint8_t *buf)
{
    int64_t y = x < 0 ? -x : x;
    for (int i = 0; i < 7; ++i) {
        buf[i] = static_cast<uint8_t>(y % 256);
        y /= 256;
    }
    buf[7] = static_cast<uint8_t>(y % 256);
    if (x < 0)
        buf[7] |= 0x80;
}

int64_t offtin(const uint8_t *buf)
{
    int64_t y = buf[7] & 0x7F;
    for (int i = 6; i >= 0; --i) {
        y *= 256;
        y += buf[i];
    }
    if (buf[7] & 0x80)
        y = -y;
    return y;
}

bool readFile(const QString &path, QByteArray *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取：%1（%2）").arg(path, file.errorString());
        return false;
    }
    *out = file.readAll();
    return true;
}

struct BzWriter {
    struct bsdiff_stream stream;
    BZFILE *bz = nullptr;
    int bzerr = BZ_OK;
};

int bzWriteCb(struct bsdiff_stream *s, const void *buffer, int size)
{
    auto *w = reinterpret_cast<BzWriter *>(s);
    BZ2_bzWrite(&w->bzerr, w->bz, const_cast<void *>(buffer), size);
    return (w->bzerr == BZ_OK || w->bzerr == BZ_STREAM_END) ? 0 : -1;
}

struct BzReader {
    struct bspatch_stream stream;
    BZFILE *bz = nullptr;
    int bzerr = BZ_OK;
};

int bzReadCb(const struct bspatch_stream *s, void *buffer, int length)
{
    const auto *r = reinterpret_cast<const BzReader *>(s);
    const int n = BZ2_bzRead(const_cast<int *>(&r->bzerr), r->bz, buffer, length);
    return n == length ? 0 : -1;
}

} // namespace

bool Bsdiff::createPatch(const QString &oldFile, const QString &newFile,
                         const QString &patchFile, QString *error)
{
    QByteArray oldData, newData;
    if (!readFile(oldFile, &oldData, error) || !readFile(newFile, &newData, error))
        return false;

    // patch 文件是纯二进制（头 + bz2 流），直接用 C stdio，避免 QFile 句柄类型问题
    FILE *pf = fopen(patchFile.toLocal8Bit().constData(), "wb");
    if (!pf) {
        if (error)
            *error = QStringLiteral("无法写出 patch：%1").arg(patchFile);
        return false;
    }

    // 24 字节头：魔数 + newsize（大端）
    uint8_t header[24];
    std::memcpy(header, kPatchMagic, 16);
    offtout(newData.size(), header + 16);
    if (fwrite(header, 1, 24, pf) != 24) {
        if (error)
            *error = QStringLiteral("写 patch 头失败：%1").arg(patchFile);
        fclose(pf);
        return false;
    }

    BzWriter writer;
    writer.stream.malloc = &::malloc;
    writer.stream.free = &::free;
    writer.stream.write = &bzWriteCb;
    writer.bz = BZ2_bzWriteOpen(&writer.bzerr, pf, 9, 0, 0);
    if (!writer.bz || writer.bzerr != BZ_OK) {
        if (error)
            *error = QStringLiteral("bzip2 初始化失败（bz2err=%1）").arg(writer.bzerr);
        fclose(pf);
        return false;
    }

    const bool ok = bsdiff(reinterpret_cast<const uint8_t *>(oldData.constData()),
                           oldData.size(),
                           reinterpret_cast<const uint8_t *>(newData.constData()),
                           newData.size(),
                           &writer.stream) == 0;
    BZ2_bzWriteClose(&writer.bzerr, writer.bz, 0, nullptr, nullptr);
    fclose(pf);

    if (!ok || writer.bzerr != BZ_OK) {
        if (error)
            *error = QStringLiteral("bsdiff 生成失败（bz2err=%1）").arg(writer.bzerr);
        QFile::remove(patchFile);
        return false;
    }
    return true;
}

bool Bsdiff::applyPatch(const QString &oldFile, const QString &patchFile,
                        const QString &outFile, QString *error)
{
    QByteArray oldData;
    if (!readFile(oldFile, &oldData, error))
        return false;

    FILE *pf = fopen(patchFile.toLocal8Bit().constData(), "rb");
    if (!pf) {
        if (error)
            *error = QStringLiteral("无法读取 patch：%1").arg(patchFile);
        return false;
    }

    uint8_t header[24];
    if (fread(header, 1, 24, pf) != 24 || std::memcmp(header, kPatchMagic, 16) != 0) {
        if (error)
            *error = QStringLiteral("patch 头无效（魔数不符）：%1").arg(patchFile);
        fclose(pf);
        return false;
    }
    const int64_t newsize = offtin(header + 16);
    if (newsize < 0 || newsize > (1LL << 32)) {
        if (error)
            *error = QStringLiteral("patch 头 newsize 异常：%1").arg(newsize);
        fclose(pf);
        return false;
    }

    BzReader reader;
    reader.stream.opaque = &reader;
    reader.stream.read = &bzReadCb;
    reader.bz = BZ2_bzReadOpen(&reader.bzerr, pf, 0, 0, nullptr, 0);
    if (!reader.bz || reader.bzerr != BZ_OK) {
        if (error)
            *error = QStringLiteral("bzip2 初始化失败（bz2err=%1）").arg(reader.bzerr);
        fclose(pf);
        return false;
    }

    uint8_t *outBuf = static_cast<uint8_t *>(std::malloc(static_cast<size_t>(newsize) + 1));
    if (!outBuf) {
        if (error)
            *error = QStringLiteral("内存分配失败（newsize=%1）").arg(newsize);
        BZ2_bzReadClose(&reader.bzerr, reader.bz);
        return false;
    }

    const bool ok = bspatch(reinterpret_cast<const uint8_t *>(oldData.constData()),
                            oldData.size(), outBuf, newsize, &reader.stream) == 0;
    BZ2_bzReadClose(&reader.bzerr, reader.bz);
    fclose(pf);

    if (!ok || reader.bzerr != BZ_OK) {
        if (error)
            *error = QStringLiteral("bspatch 合并失败（bz2err=%1）").arg(reader.bzerr);
        std::free(outBuf);
        return false;
    }

    // 全部合并成功才写目标文件
    QFile out(outFile);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || out.write(reinterpret_cast<const char *>(outBuf), newsize) != newsize) {
        if (error)
            *error = QStringLiteral("写出合并结果失败：%1").arg(outFile);
        std::free(outBuf);
        return false;
    }
    out.close();
    std::free(outBuf);
    return true;
}
