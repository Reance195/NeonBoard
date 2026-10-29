#pragma once

#include <QString>

/**
 * bsdiff/bspatch 封装（R5-06 增量升级）。
 *
 * 底层是 thirdparty/bsdiff（Colin Percival 原始算法，mendsley 库化，BSD 许可），
 * 压缩走 thirdparty/bzip2（1.0.8）。patch 格式为 ENDSLEY/BSDIFF43：
 * 24 字节头（16 字节魔数 + 8 字节大端 newsize）+ bzip2 压缩的 ctrl/diff/extra 流。
 *
 * 生成与合并都在内存里完成，不落中间文件；失败时不写出目标文件。
 */
namespace Bsdiff {
    /**
     * 生成二进制 patch：oldFile → newFile 的差异写入 patchFile。
     * 成功返回 true；失败时 patchFile 内容不可用（可能已部分写入，调用方应删除）。
     */
    bool createPatch(const QString &oldFile, const QString &newFile,
                     const QString &patchFile, QString *error = nullptr);

    /**
     * 应用 patch：oldFile + patchFile 合并出 outFile。
     * 成功返回 true；失败时 outFile 不会被写出。
     */
    bool applyPatch(const QString &oldFile, const QString &patchFile,
                    const QString &outFile, QString *error = nullptr);
}
