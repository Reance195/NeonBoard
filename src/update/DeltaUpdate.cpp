#include "DeltaUpdate.h"

#include "BsdiffWrapper.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

using DeltaInfo = DeltaUpdate::DeltaInfo;
using FileChange = DeltaUpdate::FileChange;
using ChangeType = DeltaUpdate::ChangeType;

namespace {

QString toNative(const QString &relPath)
{
    return QDir::toNativeSeparators(relPath);
}

QString toSlash(const QString &path)
{
    return QDir::fromNativeSeparators(path);
}

// 递归列出目录下所有文件（相对路径，正斜杠）
QStringList listFilesRecursive(const QString &dir)
{
    QStringList files;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = it.next();
        files.append(toSlash(QDir(dir).relativeFilePath(abs)));
    }
    return files;
}

bool copyFileVerified(const QString &src, const QString &dst,
                      const QString &expectedSha, QString *error)
{
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (QFile::exists(dst) && !QFile::remove(dst)) {
        if (error)
            *error = QStringLiteral("无法移除旧文件：%1").arg(dst);
        return false;
    }
    if (!QFile::copy(src, dst)) {
        if (error)
            *error = QStringLiteral("复制失败：%1 → %2").arg(src, dst);
        return false;
    }
    if (!expectedSha.isEmpty()) {
        const QString actual = DeltaUpdate::sha256OfFile(dst);
        if (actual != expectedSha) {
            if (error)
                *error = QStringLiteral("校验失败：%1（期望 %2，实际 %3）")
                             .arg(dst, expectedSha.left(12), actual.left(12));
            return false;
        }
    }
    return true;
}

} // namespace

bool DeltaUpdate::isBinaryFile(const QString &path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".exe")) || lower.endsWith(QLatin1String(".dll")) ||
           lower.endsWith(QLatin1String(".so")) || lower.endsWith(QLatin1String(".dylib"));
}

QString DeltaUpdate::sha256OfFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromLatin1(
        QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

DeltaInfo DeltaUpdate::parseDeltaInfo(const QJsonObject &json)
{
    DeltaInfo info;
    info.fromVersion = json.value(QLatin1String("fromVersion")).toString();
    info.toVersion = json.value(QLatin1String("toVersion")).toString();

    const QJsonArray changes = json.value(QLatin1String("changes")).toArray();
    for (const QJsonValue &val : changes) {
        const QJsonObject obj = val.toObject();
        FileChange change;
        change.path = obj.value(QLatin1String("path")).toString();
        change.type = static_cast<ChangeType>(obj.value(QLatin1String("type")).toInt());
        change.oldSha256 = obj.value(QLatin1String("oldSha256")).toString();
        change.newSha256 = obj.value(QLatin1String("newSha256")).toString();
        change.oldSize = obj.value(QLatin1String("oldSize")).toVariant().toLongLong();
        change.newSize = obj.value(QLatin1String("newSize")).toVariant().toLongLong();
        change.patchPath = obj.value(QLatin1String("patchPath")).toString();
        if (!change.path.isEmpty())
            info.changes.append(change);
    }
    return info;
}

QJsonObject DeltaUpdate::serializeDeltaInfo(const DeltaInfo &info)
{
    QJsonObject json;
    json[QLatin1String("fromVersion")] = info.fromVersion;
    json[QLatin1String("toVersion")] = info.toVersion;

    QJsonArray changes;
    for (const FileChange &change : info.changes) {
        QJsonObject obj;
        obj[QLatin1String("path")] = change.path;
        obj[QLatin1String("type")] = static_cast<int>(change.type);
        if (!change.oldSha256.isEmpty())
            obj[QLatin1String("oldSha256")] = change.oldSha256;
        if (!change.newSha256.isEmpty())
            obj[QLatin1String("newSha256")] = change.newSha256;
        if (change.oldSize > 0)
            obj[QLatin1String("oldSize")] = change.oldSize;
        if (change.newSize > 0)
            obj[QLatin1String("newSize")] = change.newSize;
        if (!change.patchPath.isEmpty())
            obj[QLatin1String("patchPath")] = change.patchPath;
        changes.append(obj);
    }
    json[QLatin1String("changes")] = changes;
    return json;
}

bool DeltaUpdate::buildDelta(const QString &oldDir, const QString &newDir,
                             const QString &fromVersion, const QString &toVersion,
                             const QString &outputDir, DeltaInfo *info, QString *error)
{
    if (!info) {
        if (error)
            *error = QStringLiteral("info 参数为空");
        return false;
    }

    const QDir oldQDir(oldDir);
    const QDir newQDir(newDir);
    if (!oldQDir.exists()) {
        if (error)
            *error = QStringLiteral("旧版本目录不存在：%1").arg(oldDir);
        return false;
    }
    if (!newQDir.exists()) {
        if (error)
            *error = QStringLiteral("新版本目录不存在：%1").arg(newDir);
        return false;
    }

    const QDir outQDir(outputDir);
    if (!outQDir.mkpath(QLatin1String("files")) || !outQDir.mkpath(QLatin1String("patches"))) {
        if (error)
            *error = QStringLiteral("无法创建输出目录：%1").arg(outputDir);
        return false;
    }

    const QStringList oldFiles = listFilesRecursive(oldDir);
    const QStringList newFiles = listFilesRecursive(newDir);
    QSet<QString> oldSet(oldFiles.begin(), oldFiles.end());
    QSet<QString> newSet(newFiles.begin(), newFiles.end());

    DeltaInfo result;
    result.fromVersion = fromVersion;
    result.toVersion = toVersion;

    // 新增文件
    for (const QString &path : newFiles) {
        if (oldSet.contains(path))
            continue;
        FileChange change;
        change.path = path;
        change.type = ChangeType::Added;
        change.newSha256 = sha256OfFile(newQDir.absoluteFilePath(path));
        change.newSize = QFileInfo(newQDir.absoluteFilePath(path)).size();
        if (change.newSha256.isEmpty()) {
            if (error)
                *error = QStringLiteral("无法读取新增文件：%1").arg(path);
            return false;
        }
        if (!copyFileVerified(newQDir.absoluteFilePath(path),
                              outQDir.absoluteFilePath(QLatin1String("files/") + toNative(path)),
                              change.newSha256, error))
            return false;
        result.changes.append(change);
    }

    // 删除文件
    for (const QString &path : oldFiles) {
        if (newSet.contains(path))
            continue;
        FileChange change;
        change.path = path;
        change.type = ChangeType::Deleted;
        change.oldSha256 = sha256OfFile(oldQDir.absoluteFilePath(path));
        change.oldSize = QFileInfo(oldQDir.absoluteFilePath(path)).size();
        result.changes.append(change);
    }

    // 修改文件
    for (const QString &path : newFiles) {
        if (!oldSet.contains(path))
            continue;
        const QString oldPath = oldQDir.absoluteFilePath(path);
        const QString newPath = newQDir.absoluteFilePath(path);
        const QString oldSha = sha256OfFile(oldPath);
        const QString newSha = sha256OfFile(newPath);
        if (oldSha == newSha)
            continue;

        FileChange change;
        change.path = path;
        change.type = ChangeType::Modified;
        change.oldSha256 = oldSha;
        change.newSha256 = newSha;
        change.oldSize = QFileInfo(oldPath).size();
        change.newSize = QFileInfo(newPath).size();

        if (isBinaryFile(path)) {
            // 二进制：bsdiff patch
            const QString patchRel = QLatin1String("patches/") + toSlash(path) + QLatin1String(".patch");
            if (!Bsdiff::createPatch(oldPath, newPath,
                                      outQDir.absoluteFilePath(toNative(patchRel)), error))
                return false;
            change.patchPath = patchRel;
        } else {
            // 资源：直接拷入 files/
            if (!copyFileVerified(newPath,
                                  outQDir.absoluteFilePath(QLatin1String("files/") + toNative(path)),
                                  change.newSha256, error))
                return false;
        }
        result.changes.append(change);
    }

    // 写 delta.json
    const QJsonObject json = serializeDeltaInfo(result);
    QFile deltaJson(outQDir.absoluteFilePath(QLatin1String("delta.json")));
    if (!deltaJson.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入 delta.json：%1").arg(deltaJson.errorString());
        return false;
    }
    deltaJson.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    deltaJson.close();

    *info = result;
    return true;
}

bool DeltaUpdate::applyDelta(const QString &deltaDir, const QString &installDir,
                             const QString &stagedDir, QString *error)
{
    const QDir deltaQDir(deltaDir);
    const QDir installQDir(installDir);
    if (!deltaQDir.exists()) {
        if (error)
            *error = QStringLiteral("增量包目录不存在：%1").arg(deltaDir);
        return false;
    }
    if (!installQDir.exists()) {
        if (error)
            *error = QStringLiteral("安装目录不存在：%1").arg(installDir);
        return false;
    }

    // 解析 delta.json
    QFile jsonFile(deltaQDir.absoluteFilePath(QLatin1String("delta.json")));
    if (!jsonFile.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 delta.json：%1").arg(deltaDir);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll(), &parseError);
    jsonFile.close();
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("delta.json 格式错误：%1").arg(parseError.errorString());
        return false;
    }

    const DeltaInfo info = parseDeltaInfo(doc.object());
    if (info.changes.isEmpty()) {
        if (error)
            *error = QStringLiteral("delta.json 没有任何变更条目");
        return false;
    }

    // 暂存目录（二进制合并落点）与备份目录
    const QString backupDir = stagedDir + QLatin1String("/.backup");
    QDir stagedQDir(stagedDir);
    if (!stagedQDir.mkpath(QLatin1String("."))) {
        if (error)
            *error = QStringLiteral("无法创建暂存目录：%1").arg(stagedDir);
        return false;
    }

    // ---- 预校验：Modified / Deleted 的旧文件必须与清单一致，否则 patch 无法应用 ----
    for (const FileChange &change : info.changes) {
        if (change.type != ChangeType::Modified && change.type != ChangeType::Deleted)
            continue;
        const QString abs = installQDir.absoluteFilePath(toNative(change.path));
        if (!QFile::exists(abs)) {
            // Deleted 的旧文件本就该不在：清理只要有一条落在增量清单里（或上一次
            // 升级半途失败），硬失败会把这台机器从此所有增量都挡死。缺席即已清理。
            if (change.type == ChangeType::Deleted)
                continue;
            if (error)
                *error = QStringLiteral("旧文件不存在，无法应用增量：%1").arg(change.path);
            return false;
        }
        const QString actual = sha256OfFile(abs);
        if (actual != change.oldSha256) {
            if (error)
                *error = QStringLiteral("旧文件与增量包不匹配（版本不符？）：%1")
                             .arg(change.path);
            return false;
        }
    }

    // ---- 备份将被修改 / 删除的文件 ----
    for (const FileChange &change : info.changes) {
        if (change.type != ChangeType::Modified && change.type != ChangeType::Deleted)
            continue;
        const QString src = installQDir.absoluteFilePath(toNative(change.path));
        if (!QFile::exists(src))
            continue; // 上面已放行的缺席条目：没有旧文件可备份
        const QString dst = backupDir + QLatin1String("/") + toNative(change.path);
        QDir().mkpath(QFileInfo(dst).absolutePath());
        if (!QFile::copy(src, dst)) {
            if (error)
                *error = QStringLiteral("备份失败：%1").arg(change.path);
            return false;
        }
    }

    // ---- 应用变更 ----
    bool success = true;
    for (const FileChange &change : info.changes) {
        const QString dst = installQDir.absoluteFilePath(toNative(change.path));

        switch (change.type) {
        case ChangeType::Added:
        case ChangeType::Modified: {
            if (change.patchPath.isEmpty()) {
                // 资源文件：直接覆盖
                const QString src = deltaQDir.absoluteFilePath(
                    QLatin1String("files/") + toNative(change.path));
                if (!copyFileVerified(src, dst, change.newSha256, error))
                    success = false;
            } else {
                // 二进制：bsdiff 合并到暂存目录（进程退出后由自更新脚本拷入）
                const QString srcOld = dst;
                const QString srcPatch = deltaQDir.absoluteFilePath(toNative(change.patchPath));
                const QString dstStaged = stagedDir + QLatin1String("/") + toNative(change.path);
                QDir().mkpath(QFileInfo(dstStaged).absolutePath());
                if (!Bsdiff::applyPatch(srcOld, srcPatch, dstStaged, error))
                    success = false;
                else if (sha256OfFile(dstStaged) != change.newSha256) {
                    if (error)
                        *error = QStringLiteral("二进制合并结果校验失败：%1").arg(change.path);
                    success = false;
                }
            }
            break;
        }
        case ChangeType::Deleted: {
            if (QFile::exists(dst) && !QFile::remove(dst)) {
                if (error)
                    *error = QStringLiteral("删除失败：%1").arg(change.path);
                success = false;
            }
            break;
        }
        }

        if (!success)
            break;
    }

    // ---- 失败回滚 ----
    if (!success) {
        for (const FileChange &change : info.changes) {
            const QString dst = installQDir.absoluteFilePath(toNative(change.path));
            if (change.type == ChangeType::Added) {
                QFile::remove(dst);
            } else {
                const QString src = backupDir + QLatin1String("/") + toNative(change.path);
                QDir().mkpath(QFileInfo(dst).absolutePath());
                QFile::remove(dst);
                QFile::copy(src, dst);
            }
        }
        QDir(backupDir).removeRecursively();
        return false;
    }

    QDir(backupDir).removeRecursively();
    return true;
}

QStringList DeltaUpdate::loadObsoleteList(const QString &listPath)
{
    QFile file(listPath);
    if (!file.open(QIODevice::ReadOnly))
        return {}; // 清单缺席是正常状态（老增量包没带它），不是错误
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    QStringList paths;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char(';')))
            continue;
        // 清单只允许安装目录内的相对路径：绝对路径与 .. 一律丢掉，
        // 免得一份被改坏的清单把清理变成任意删除。
        const QString normalized = toSlash(line);
        if (QDir::isAbsolutePath(normalized) || normalized.contains(QLatin1String("..")))
            continue;
        paths.append(normalized);
    }
    return paths;
}

DeltaUpdate::ObsoleteCleanup DeltaUpdate::cleanupObsoleteFiles(const QString &installDir,
                                                               const QStringList &paths)
{
    ObsoleteCleanup result;
    QDir installQDir(installDir);
    for (const QString &rel : paths) {
        const QString abs = installQDir.absoluteFilePath(toNative(rel));
        const QFileInfo info(abs);
        if (info.isDir()) {
            if (QDir(abs).removeRecursively())
                ++result.deleted;
            else
                result.skipped.append(rel);
        } else if (info.exists()) {
            if (QFile::remove(abs))
                ++result.deleted;
            else
                result.skipped.append(rel);
        }
        // 两者皆无 = 已经清理过，静默跳过
    }
    return result;
}

bool DeltaUpdate::writeInstallReceipt(const QString &installDir, const QString &version,
                                      const QString &commit, const ObsoleteCleanup &cleanup,
                                      QString *error)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), version);
    root.insert(QStringLiteral("writtenAt"),
                QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    root.insert(QStringLiteral("commit"), commit);
    root.insert(QStringLiteral("source"), QStringLiteral("delta"));
    QJsonObject cleanupObj;
    cleanupObj.insert(QStringLiteral("deleted"), cleanup.deleted);
    cleanupObj.insert(QStringLiteral("skipped"), QJsonArray::fromStringList(cleanup.skipped));
    root.insert(QStringLiteral("cleanup"), cleanupObj);

    const QString path = QDir(installDir).absoluteFilePath(QStringLiteral("install-receipt.json"));
    QSaveFile file(path); // 原子写：回执半途失败会被读成「上次更新未完成」，那是假信号
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入安装回执：%1").arg(path);
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("写入安装回执失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

#ifdef NEONBOARD_DELTA_TOOL_MAIN
// neonboard-delta-tool：发布侧增量包生成工具（scripts/build-delta.ps1 调用）。
// 独立 console 目标，与主程序共用 DeltaUpdate / BsdiffWrapper 源码。
#include <QCoreApplication>

static bool toolBuild(int argc, char **argv)
{
    QString oldDir, newDir, fromVersion, toVersion, outDir;
    for (int i = 2; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        auto next = [&](const char *name) -> QString {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for %s\n", name);
                return QString();
            }
            return QString::fromLocal8Bit(argv[++i]);
        };
        if (arg == QLatin1String("--from"))
            oldDir = next("--from");
        else if (arg == QLatin1String("--to"))
            newDir = next("--to");
        else if (arg == QLatin1String("--from-version"))
            fromVersion = next("--from-version");
        else if (arg == QLatin1String("--to-version"))
            toVersion = next("--to-version");
        else if (arg == QLatin1String("--out"))
            outDir = next("--out");
        else {
            fprintf(stderr, "unknown arg: %s\n", arg.toLocal8Bit().constData());
            return false;
        }
    }
    if (oldDir.isEmpty() || newDir.isEmpty() || fromVersion.isEmpty()
        || toVersion.isEmpty() || outDir.isEmpty()) {
        fprintf(stderr, "usage: neonboard-delta-tool build --from <old> --to <new> --from-version X --to-version Y --out <dir>\n");
        return false;
    }

    DeltaUpdate::DeltaInfo info;
    QString error;
    if (!DeltaUpdate::buildDelta(oldDir, newDir, fromVersion, toVersion, outDir, &info, &error)) {
        fprintf(stderr, "buildDelta failed: %s\n", error.toLocal8Bit().constData());
        return false;
    }

    int added = 0, modified = 0, deleted = 0, patched = 0;
    for (const auto &c : info.changes) {
        if (c.type == DeltaUpdate::ChangeType::Added)
            ++added;
        else if (c.type == DeltaUpdate::ChangeType::Deleted)
            ++deleted;
        else {
            ++modified;
            if (!c.patchPath.isEmpty())
                ++patched;
        }
    }
    // 结果走 stdout（GUI 子系统的 qInfo 进不了管道，这里是 console 目标，直接 printf）
    printf("delta-ok added=%d modified=%d deleted=%d patched=%d out=%s\n",
           added, modified, deleted, patched,
           outDir.toLocal8Bit().constData());
    return true;
}

// cleanup 模式：与 UpdateManager::installDelta 用同一份清理实现，供发布侧与验收脚本
// 对着一个假安装树跑一遍（弃用清单是否真的删干净、被占用的条目是否只跳过不失败）。
static bool toolCleanup(int argc, char **argv)
{
    QString installDir, listPath, version, commit;
    bool wantReceipt = false;
    for (int i = 2; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        auto next = [&](const char *name) -> QString {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for %s\n", name);
                return QString();
            }
            return QString::fromLocal8Bit(argv[++i]);
        };
        if (arg == QLatin1String("--install"))
            installDir = next("--install");
        else if (arg == QLatin1String("--list"))
            listPath = next("--list");
        else if (arg == QLatin1String("--receipt"))
            wantReceipt = true;
        else if (arg == QLatin1String("--version"))
            version = next("--version");
        else if (arg == QLatin1String("--commit"))
            commit = next("--commit");
        else {
            fprintf(stderr, "unknown arg: %s\n", arg.toLocal8Bit().constData());
            return false;
        }
    }
    if (installDir.isEmpty() || listPath.isEmpty()) {
        fprintf(stderr, "usage: neonboard-delta-tool cleanup --install <dir> --list <obsolete-files.txt>"
                        " [--receipt [--version X --commit Y]]\n");
        return false;
    }

    const QStringList paths = DeltaUpdate::loadObsoleteList(listPath);
    const DeltaUpdate::ObsoleteCleanup result =
        DeltaUpdate::cleanupObsoleteFiles(installDir, paths);
    for (const QString &skipped : result.skipped)
        printf("cleanup-skip %s\n", skipped.toLocal8Bit().constData());
    printf("cleanup-ok listed=%d deleted=%d skipped=%d\n",
           static_cast<int>(paths.size()), result.deleted,
           static_cast<int>(result.skipped.size()));

    // --receipt：把清理结果连同版本落成安装回执，与 UpdateManager::downloadAndApplyDelta
    // 走的同一条写盘路径（升级验收要能对着一个假安装树把它跑一遍）
    if (wantReceipt) {
        QString error;
        if (!DeltaUpdate::writeInstallReceipt(
                installDir, version.isEmpty() ? QStringLiteral("0.0.0-test") : version,
                commit.isEmpty() ? QStringLiteral("unknown") : commit, result, &error)) {
            fprintf(stderr, "receipt failed: %s\n", error.toLocal8Bit().constData());
            return false;
        }
        printf("receipt-ok\n");
    }
    return true;
}

int main(int argc, char **argv)
{
    printf("delta-tool start argc=%d\n", argc);
    QCoreApplication app(argc, argv);
    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (mode == QLatin1String("build"))
        return toolBuild(argc, argv) ? 0 : 1;
    if (mode == QLatin1String("cleanup"))
        return toolCleanup(argc, argv) ? 0 : 1;
    fprintf(stderr, "usage: neonboard-delta-tool build --from <old> --to <new> --from-version X --to-version Y --out <dir>\n"
                    "       neonboard-delta-tool cleanup --install <dir> --list <obsolete-files.txt>\n");
    return 2;
}
#endif // NEONBOARD_DELTA_TOOL_MAIN
