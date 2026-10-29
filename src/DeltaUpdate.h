#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

/**
 * 混合增量升级（R5-06）：文件级 diff + bsdiff 二进制增量。
 *
 * 增量包（delta.zip）布局：
 *   delta.json              变更清单
 *   files/<相对路径>         新增 / 修改的资源文件（QML、图片、配置等，全量内容）
 *   patches/<相对路径>.patch 修改的二进制文件（exe/dll）的 bsdiff patch
 *
 * 发布侧（neonboard-delta-tool，scripts/build-delta.ps1 调用）：
 *   buildDelta() 对比新旧版本目录，产出上述布局。
 *
 * 客户端（UpdateManager::downloadAndApplyDelta）：
 *   1. 下载 delta.zip 并校验 SHA-256（校验值来自 update.json 清单）
 *   2. 解压后调 applyDelta()：预校验旧文件 → 备份 → 资源直接覆盖、
 *      二进制 bsdiff 合并到暂存目录 → 逐文件校验 → 失败回滚
 *   3. 暂存目录里的二进制由自更新脚本在进程退出后拷入安装目录并重启
 */
class DeltaUpdate
{
public:
    /** 文件变更类型（与 delta.json 的 type 字段对应） */
    enum class ChangeType {
        Added = 0,      ///< 新增文件
        Modified = 1,   ///< 修改文件（资源直接替换，二进制走 bsdiff patch）
        Deleted = 2     ///< 删除文件
    };

    /** 单个文件的变更信息 */
    struct FileChange {
        QString path;           ///< 相对路径（正斜杠）
        ChangeType type = ChangeType::Added;
        QString oldSha256;      ///< 旧版 SHA-256（Modified / Deleted）
        QString newSha256;      ///< 新版 SHA-256（Added / Modified）
        qint64 oldSize = 0;
        qint64 newSize = 0;
        QString patchPath;      ///< bsdiff patch 相对路径（仅二进制 Modified）
    };

    /** 增量包元信息（delta.json） */
    struct DeltaInfo {
        QString fromVersion;    ///< 源版本（客户端须与此版本一致才可用本增量包）
        QString toVersion;      ///< 目标版本
        QList<FileChange> changes;
    };

    /** 判断是否为二进制文件（走 bsdiff） */
    static bool isBinaryFile(const QString &path);

    /** 计算文件 SHA-256（小写十六进制）；读失败返回空串 */
    static QString sha256OfFile(const QString &path);

    /**
     * 生成增量包内容（发布侧）。
     * 对比 oldDir / newDir，在 outputDir 下写出 delta.json + files/ + patches/。
     * 二进制修改用 bsdiff 生成 patch；资源修改 / 新增直接拷入 files/。
     */
    static bool buildDelta(const QString &oldDir, const QString &newDir,
                           const QString &fromVersion, const QString &toVersion,
                           const QString &outputDir, DeltaInfo *info, QString *error);

    /**
     * 应用增量包（客户端）。deltaDir 是已解压的增量包目录（须含 delta.json）。
     *
     * 步骤：预校验（Modified/Deleted 的旧文件 sha 必须匹配）→ 备份旧文件 →
     * 应用变更（资源拷入 installDir，二进制 bsdiff 合并到 stagedDir）→ 逐文件校验。
     * 任一步失败：恢复备份、删除新增文件，返回 false（installDir 回到应用前状态）。
     *
     * @param stagedDir 合并后的二进制文件落点（保留原相对路径）；成功后由调用方
     *                  在进程退出后拷入 installDir。失败时内容不可用。
     */
    static bool applyDelta(const QString &deltaDir, const QString &installDir,
                           const QString &stagedDir, QString *error);

    /** 解析 delta.json */
    static DeltaInfo parseDeltaInfo(const QJsonObject &json);

    /** 序列化 delta.json */
    static QJsonObject serializeDeltaInfo(const DeltaInfo &info);
};
