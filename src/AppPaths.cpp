#include "AppPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace {
const char *kDataDirVar = "NEONBOARD_DATA_DIR";
const char *kInstanceVar = "NEONBOARD_INSTANCE";
const char *kBaseInstanceKey = "com.lin.NeonBoard.single-instance";
}

bool AppPaths::isolated()
{
    return !qEnvironmentVariableIsEmpty(kDataDirVar);
}

QString AppPaths::dataDir()
{
    const QString override = qEnvironmentVariable(kDataDirVar);
    if (!override.isEmpty()) {
        const QString dir = QDir::fromNativeSeparators(override);
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

QString AppPaths::logFilePath()
{
    return dataDir() + QStringLiteral("/neonboard.log");
}

QString AppPaths::singleInstanceKey()
{
    QString key = QString::fromLatin1(kBaseInstanceKey);
    const QString suffix = qEnvironmentVariable(kInstanceVar);
    if (!suffix.isEmpty()) {
        key += QLatin1Char('.');
        key += suffix;
    }
    return key;
}
