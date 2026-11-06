#include "RepoScaffolder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

const char kProgressFile[] = "progress.json";
const char kLedgerRelPath[] = "docs/board-ledger.json";

// 最小 v3 快照：与 derive_board_status.py 派生的 progress.json 同构
// （version=3 + items 数组）。面板首次导入看到 0 张卡，什么也不建；
// 此后该仓库的卡由面板写回，格式保持同一契约。
QString scaffoldProgressJson()
{
    QJsonObject out;
    out.insert(QStringLiteral("version"), 3);
    out.insert(QStringLiteral("updated"), QDateTime::currentDateTime().toString(Qt::ISODate));
    out.insert(QStringLiteral("items"), QJsonArray());
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented));
}

// 空台账：顶层三键与仓库现有台账同形（派生器只写 claims / baseline 两段，
// 空对象是最小合法形态；cards 留给维护者按章程登记）。
QString scaffoldLedgerJson()
{
    QJsonObject out;
    out.insert(QStringLiteral("baseline"), QJsonObject());
    out.insert(QStringLiteral("cards"), QJsonObject());
    out.insert(QStringLiteral("claims"), QJsonObject());
    return QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Indented));
}

} // namespace

QString RepoScaffolder::validateRepoPath(const QString &repoPath)
{
    const QString path = repoPath.trimmed();
    if (path.isEmpty())
        return QStringLiteral("路径为空");
    if (!QDir::isAbsolutePath(path))
        return QStringLiteral("仅支持绝对路径：") + path;
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir())
        return QStringLiteral("目录不存在：") + path;
    // git 仓库判定宽松：.git 可以是目录，也可以是文件（worktree / submodule）
    if (!QDir(path).exists(QStringLiteral(".git")))
        return QStringLiteral("不是 git 仓库（找不到 .git）：") + path;
    return {};
}

bool RepoScaffolder::hasScaffold(const QString &repoPath)
{
    const QDir repo(repoPath.trimmed());
    return repo.exists(kProgressFile) && repo.exists(kLedgerRelPath);
}

bool RepoScaffolder::writeFileAtomic(const QString &filePath, const QByteArray &bytes, QString *error)
{
    // QSaveFile：写一半失败不留半文件（脚手架「全有或全无」的一半）
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 %1").arg(filePath);
        return false;
    }
    file.write(bytes);
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("落盘失败 %1").arg(filePath);
        return false;
    }
    return true;
}

QString RepoScaffolder::scaffold(const QString &repoPath)
{
    const QString invalid = validateRepoPath(repoPath);
    if (!invalid.isEmpty())
        return invalid;

    const QDir repo(repoPath.trimmed());
    // 「既有文件不覆盖」按单个文件判定：任一个存在即视为已接入过，
    // 整单拒绝 —— 不写半套脚手架
    if (repo.exists(kProgressFile) || repo.exists(kLedgerRelPath))
        return QStringLiteral("已有台账文件（既有文件不覆盖）：") + repoPath.trimmed();

    if (!repo.mkpath(QStringLiteral("docs")))
        return QStringLiteral("创建 docs/ 目录失败：") + repoPath.trimmed();

    QString error;
    const QString progressPath = repo.filePath(kProgressFile);
    if (!writeFileAtomic(progressPath, scaffoldProgressJson().toUtf8(), &error))
        return error;
    const QString ledgerPath = repo.filePath(kLedgerRelPath);
    if (!writeFileAtomic(ledgerPath, scaffoldLedgerJson().toUtf8(), &error))
        return error;
    return {};
}
