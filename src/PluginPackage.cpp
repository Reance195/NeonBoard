#include "PluginPackage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryDir>

namespace {

/** 防目录穿越：文件名只允许纯文件名 + 单层子目录 */
bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty())
        return false;
    if (path.contains(QLatin1Char('\\')))
        return false;
    if (path.startsWith(QLatin1Char('/')))
        return false;
    if (path.contains(QLatin1String("..")))
        return false;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    return true;
}

bool writeFile(const QString &path, const QByteArray &content, QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("无法创建目录：%1").arg(info.absolutePath());
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入文件：%1").arg(path);
        return false;
    }
    if (file.write(content) < 0) {
        if (error)
            *error = QStringLiteral("写入失败：%1").arg(path);
        return false;
    }
    return true;
}

bool copyDirectory(const QString &source, const QString &target, QString *error)
{
    QDir sourceDir(source);
    if (!sourceDir.exists()) {
        if (error)
            *error = QStringLiteral("源目录不存在：%1").arg(source);
        return false;
    }
    if (!QDir().mkpath(target)) {
        if (error)
            *error = QStringLiteral("无法创建目录：%1").arg(target);
        return false;
    }
    const QFileInfoList entries =
        sourceDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        const QString destination = QDir(target).filePath(name);
        if (entry.isDir()) {
            if (!copyDirectory(entry.absoluteFilePath(), destination, error))
                return false;
        } else {
            if (!writeFile(destination, [&] {
                    QFile file(entry.absoluteFilePath());
                    if (!file.open(QIODevice::ReadOnly))
                        return QByteArray();
                    return file.readAll();
                }(),
                error))
                return false;
        }
    }
    return true;
}

bool removeDirectory(const QString &path)
{
    QDir dir(path);
    if (!dir.exists())
        return true;
    return dir.removeRecursively();
}

} // namespace

bool PluginPackage::validateNeonFile(const QString &neonPath, QString *error)
{
    QFile file(neonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取插件包：%1").arg(neonPath);
        return false;
    }
    const QByteArray raw = file.readAll();
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("插件包不是合法的 JSON 对象：%1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("manifest")).isObject()) {
        if (error)
            *error = QStringLiteral("插件包缺少 manifest 段");
        return false;
    }
    if (!root.value(QStringLiteral("files")).isObject()) {
        if (error)
            *error = QStringLiteral("插件包缺少 files 段");
        return false;
    }
    const QString id = root.value(QStringLiteral("manifest")).toObject().value(QStringLiteral("id")).toString();
    if (id.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("插件包的 manifest.id 为空");
        return false;
    }
    return true;
}

QString PluginPackage::neonPluginId(const QString &neonPath)
{
    QFile file(neonPath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return QString();
    return document.object()
        .value(QStringLiteral("manifest"))
        .toObject()
        .value(QStringLiteral("id"))
        .toString();
}

QString PluginPackage::dirNameForId(const QString &pluginId)
{
    const int dot = pluginId.lastIndexOf(QLatin1Char('.'));
    if (dot >= 0 && dot + 1 < pluginId.size())
        return pluginId.mid(dot + 1);
    return pluginId;
}

bool PluginPackage::installNeonFile(const QString &neonPath, const QString &targetDir, QString *error)
{
    if (!validateNeonFile(neonPath, error))
        return false;

    QFile file(neonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取插件包：%1").arg(neonPath);
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = document.object();
    const QJsonObject manifest = root.value(QStringLiteral("manifest")).toObject();
    const QJsonObject files = root.value(QStringLiteral("files")).toObject();

    // 先落到同盘临时目录，成功后再原子替换目标目录
    const QString parent = QFileInfo(targetDir).absolutePath();
    if (!QDir().mkpath(parent)) {
        if (error)
            *error = QStringLiteral("无法创建插件目录：%1").arg(parent);
        return false;
    }
    QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".neon-stage-XXXXXX")));
    if (!staging.isValid()) {
        if (error)
            *error = QStringLiteral("无法创建临时目录");
        return false;
    }
    const QString stagingPath = staging.path();

    if (!writeFile(QDir(stagingPath).filePath(QStringLiteral("plugin.json")),
                   QJsonDocument(manifest).toJson(QJsonDocument::Indented), error))
        return false;

    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString relative = it.key();
        if (!isSafeRelativePath(relative)) {
            if (error)
                *error = QStringLiteral("插件包包含非法路径：%1").arg(relative);
            return false;
        }
        const QString value = it.value().toString();
        if (!writeFile(QDir(stagingPath).filePath(relative), value.toUtf8(), error))
            return false;
    }

    const QString backup = targetDir + QStringLiteral(".backup");
    removeDirectory(backup);
    const bool hadTarget = QDir(targetDir).exists();
    if (hadTarget) {
        if (!QDir().rename(targetDir, backup)) {
            if (error)
                *error = QStringLiteral("无法备份原插件目录");
            return false;
        }
    }
    if (!QDir().rename(stagingPath, targetDir)) {
        if (hadTarget)
            QDir().rename(backup, targetDir);
        if (error)
            *error = QStringLiteral("无法安装到目标目录：%1").arg(targetDir);
        return false;
    }
    removeDirectory(backup);
    staging.setAutoRemove(false);
    return true;
}

bool PluginPackage::installDirectory(const QString &sourceDir, const QString &targetDir, QString *error)
{
    if (!QDir(sourceDir).exists()) {
        if (error)
            *error = QStringLiteral("源目录不存在：%1").arg(sourceDir);
        return false;
    }
    const QString parent = QFileInfo(targetDir).absolutePath();
    if (!QDir().mkpath(parent)) {
        if (error)
            *error = QStringLiteral("无法创建插件目录：%1").arg(parent);
        return false;
    }
    QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".copy-stage-XXXXXX")));
    if (!staging.isValid()) {
        if (error)
            *error = QStringLiteral("无法创建临时目录");
        return false;
    }
    const QString stagingPath = staging.path();
    if (!copyDirectory(sourceDir, stagingPath, error))
        return false;

    const QString backup = targetDir + QStringLiteral(".backup");
    removeDirectory(backup);
    const bool hadTarget = QDir(targetDir).exists();
    if (hadTarget) {
        if (!QDir().rename(targetDir, backup)) {
            if (error)
                *error = QStringLiteral("无法备份原插件目录");
            return false;
        }
    }
    if (!QDir().rename(stagingPath, targetDir)) {
        if (hadTarget)
            QDir().rename(backup, targetDir);
        if (error)
            *error = QStringLiteral("无法安装到目标目录：%1").arg(targetDir);
        return false;
    }
    removeDirectory(backup);
    staging.setAutoRemove(false);
    return true;
}
