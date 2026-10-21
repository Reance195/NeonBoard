#include "PluginLoader.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

PluginLoader::PluginLoader(QObject *parent)
    : QObject(parent)
{
    m_pluginDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + QStringLiteral("/plugins");
    QDir().mkpath(m_pluginDir);
    scan();
}

void PluginLoader::scan()
{
    QElapsedTimer scanTimer;
    scanTimer.start();
    m_plugins.clear();
    QSet<QString> seenIds;

    const auto scanDirectory = [this, &seenIds](const QString &rootPath) {
        QDir root(rootPath);
        if (!root.exists())
            return;

        const QFileInfoList entries =
            root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QString pluginDir = entry.absoluteFilePath();
            const QString manifestPath = pluginDir + QStringLiteral("/plugin.json");

            QVariantMap plugin;
            plugin.insert(QStringLiteral("dir"), pluginDir);
            plugin.insert(QStringLiteral("manifestPath"), manifestPath);
            plugin.insert(QStringLiteral("status"), QStringLiteral("ok"));
            plugin.insert(QStringLiteral("error"), QString());
            plugin.insert(QStringLiteral("mainUrl"), QString());
            plugin.insert(QStringLiteral("baseUrl"),
                          QUrl::fromLocalFile(pluginDir + QLatin1Char('/')).toString());

            QFile manifestFile(manifestPath);
            if (!manifestFile.open(QIODevice::ReadOnly)) {
                plugin[QStringLiteral("status")] = QStringLiteral("error");
                plugin[QStringLiteral("error")] = QStringLiteral("无法读取 plugin.json");
                m_plugins.append(plugin);
                continue;
            }

            QJsonParseError parseError{};
            const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                plugin[QStringLiteral("status")] = QStringLiteral("error");
                plugin[QStringLiteral("error")] =
                    QStringLiteral("plugin.json 解析失败: %1").arg(parseError.errorString());
                m_plugins.append(plugin);
                continue;
            }

            const QJsonObject object = document.object();
            const QString id = object.value(QStringLiteral("id")).toString();
            const QString name = object.value(QStringLiteral("name")).toString(id);
            const QString version = object.value(QStringLiteral("version")).toString();
            const int apiVersion = object.value(QStringLiteral("apiVersion")).toInt(1);
            const QString main = object.value(QStringLiteral("main")).toString(QStringLiteral("main.qml"));

            plugin[QStringLiteral("id")] = id;
            plugin[QStringLiteral("name")] = name;
            plugin[QStringLiteral("version")] = version;
            plugin[QStringLiteral("apiVersion")] = apiVersion;
            plugin[QStringLiteral("main")] = main;

            QVariantList permissions;
            const QJsonArray permissionArray = object.value(QStringLiteral("permissions")).toArray();
            for (const QJsonValue &value : permissionArray)
                permissions.append(value.toString());
            plugin[QStringLiteral("permissions")] = permissions;

            if (id.isEmpty()) {
                plugin[QStringLiteral("status")] = QStringLiteral("error");
                plugin[QStringLiteral("error")] = QStringLiteral("缺少 id");
            } else if (apiVersion != 1) {
                plugin[QStringLiteral("status")] = QStringLiteral("error");
                plugin[QStringLiteral("error")] =
                    QStringLiteral("不支持的 apiVersion: %1").arg(apiVersion);
            } else {
                const QFileInfo mainInfo(pluginDir + QLatin1Char('/') + main);
                if (!mainInfo.exists() || !mainInfo.isFile()) {
                    plugin[QStringLiteral("status")] = QStringLiteral("error");
                    plugin[QStringLiteral("error")] = QStringLiteral("入口文件不存在: %1").arg(main);
                } else {
                    plugin[QStringLiteral("mainUrl")] =
                        QUrl::fromLocalFile(mainInfo.absoluteFilePath()).toString();
                }
            }

            if (!id.isEmpty() && seenIds.contains(id))
                continue;
            if (!id.isEmpty())
                seenIds.insert(id);

            m_plugins.append(plugin);
        }
    };

    scanDirectory(m_pluginDir);
    scanDirectory(QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"));
    scanDirectory(QStringLiteral(NEONBOARD_SOURCE_DIR) + QStringLiteral("/plugins-samples"));

    qInfo() << "PluginLoader: scanned" << m_plugins.size() << "plugin(s) from" << m_pluginDir
            << "in" << scanTimer.elapsed() << "ms";
    emit pluginsChanged();
}

bool PluginLoader::openPluginDir()
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(m_pluginDir));
}

QVariantMap PluginLoader::pluginById(const QString &id) const
{
    for (const QVariant &value : m_plugins) {
        const QVariantMap plugin = value.toMap();
        if (plugin.value(QStringLiteral("id")).toString() == id)
            return plugin;
    }
    return {};
}
