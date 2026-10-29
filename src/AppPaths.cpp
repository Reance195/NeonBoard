#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
const char *kDataDirVar = "NEONBOARD_DATA_DIR";
const char *kInstanceVar = "NEONBOARD_INSTANCE";
const char *kBaseInstanceKey = "com.lin.NeonBoard.single-instance";
const char *kWorktreeMarker = "/.worktrees/";
}

QString AppPaths::worktreeRoot()
{
    static const QString cached = [] {
        const QString appDir =
            QDir::fromNativeSeparators(QCoreApplication::applicationDirPath());
        const int at = appDir.indexOf(QLatin1String(kWorktreeMarker));
        if (at < 0)
            return QString();
        const QString rest = appDir.mid(at + QLatin1String(kWorktreeMarker).size());
        const int slash = rest.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            return QString();
        return appDir.left(at) + QLatin1String(kWorktreeMarker) + rest.left(slash);
    }();
    return cached;
}

QString AppPaths::worktreeAgentId()
{
    const QString root = worktreeRoot();
    return root.isEmpty() ? QString() : QFileInfo(root).fileName();
}

bool AppPaths::isolated()
{
    return !qEnvironmentVariableIsEmpty(kDataDirVar) || !worktreeRoot().isEmpty();
}

QString AppPaths::dataDir()
{
    const QString override = qEnvironmentVariable(kDataDirVar);
    if (!override.isEmpty()) {
        const QString dir = QDir::fromNativeSeparators(override);
        QDir().mkpath(dir);
        return dir;
    }
    // 护栏：worktree 里的可执行文件默认落自己的 .sandbox，
    // 避免忘记设 NEONBOARD_DATA_DIR 时把测试任务写进真实数据目录。
    const QString worktree = worktreeRoot();
    if (!worktree.isEmpty()) {
        const QString dir = worktree + QStringLiteral("/.sandbox");
        QDir().mkpath(dir);
        return dir;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!dir.isEmpty())
        QDir().mkpath(dir);
    return dir;
}

QString AppPaths::pluginDir()
{
    return dataDir() + QStringLiteral("/plugins");
}

// 主题包目录（R8-01）：与 plugins 同级，用户可直接丢 themepack.json 进来。
// 故意不复用 plugins/：主题是纯数据，不需要走插件那份「安装 / 权限 / 宿主」的流程。
QString AppPaths::themeDir()
{
    return dataDir() + QStringLiteral("/themes");
}

QString AppPaths::logFilePath()
{
    return dataDir() + QStringLiteral("/neonboard.log");
}

QString AppPaths::hookDbPath()
{
    return dataDir() + QStringLiteral("/hooks.db");
}

QString AppPaths::singleInstanceKey()
{
    QString key = QString::fromLatin1(kBaseInstanceKey);
    QString suffix = qEnvironmentVariable(kInstanceVar);
    if (suffix.isEmpty())
        suffix = worktreeAgentId();
    if (!suffix.isEmpty()) {
        key += QLatin1Char('.');
        key += suffix;
    }
    return key;
}
