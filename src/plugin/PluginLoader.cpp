#include "PluginLoader.h"

#include "AppPaths.h"
#include "Database.h"

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
#include <QUrl>

namespace {
constexpr const char *kDisabledMetaKey = "plugins.disabled";

QStringList idsFromJsonArray(const QString &payload)
{
    QStringList ids;
    if (payload.isEmpty())
        return ids;

    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isArray())
        return ids;

    for (const QJsonValue &value : document.array()) {
        const QString id = value.toString().trimmed();
        if (!id.isEmpty() && !ids.contains(id))
            ids.append(id);
    }
    return ids;
}

QString jsonArrayFromIds(const QStringList &ids)
{
    QJsonArray array;
    for (const QString &id : ids)
        array.append(id);
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
}

PluginLoader::PluginLoader(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{
    m_pluginDir = AppPaths::pluginDir();
    QDir().mkpath(m_pluginDir);
    scan();
}

QStringList PluginLoader::sourceDirs() const
{
    QStringList dirs{
        m_pluginDir,
        QCoreApplication::applicationDirPath() + QStringLiteral("/plugins"),
    };
    // 源码树的两个目录是「有源码才有」的可选形态：安装版并没有这份路径，
    // 以前靠把开发机路径编进 exe、再在运行时试探目录是否存在来兜底。
    // 改成编译期条件后，「没有源码目录」变成合法状态，也让只链 PluginLoader.cpp
    // 的单元测试不必伪造这个宏（R14-01）。
#ifdef NEONBOARD_SOURCE_DIR
    dirs.append(QStringLiteral(NEONBOARD_SOURCE_DIR) + QStringLiteral("/plugins"));
    dirs.append(QStringLiteral(NEONBOARD_SOURCE_DIR) + QStringLiteral("/plugins-samples"));
#endif
    return dirs;
}

bool PluginLoader::isOfficialPluginSource(const QString &source)
{
    // 与 sourceDirs() 的四个返回值一一对应。appdata 是用户数据目录（用户装的市场插件）、
    // sample 是示例插件，这两类都不算「官方来源」；官方来源是 appdir（安装目录，随分发走）
    // 与 official（源码树 plugins/，仅开发机存在，用来让开发态与安装态判据一致）。
    return source == QLatin1String("appdir") || source == QLatin1String("official");
}

QHash<QString, QStringList> PluginLoader::officialAutoGrants(
    const QVariantList &plugins, const QHash<QString, QStringList> &grants,
    const QHash<QString, QStringList> &revoked)
{
    QHash<QString, QStringList> patched;

    for (const QVariant &value : plugins) {
        const QVariantMap plugin = value.toMap();
        if (plugin.value(QStringLiteral("status")).toString() != QLatin1String("ok"))
            continue;
        if (!isOfficialPluginSource(plugin.value(QStringLiteral("source")).toString()))
            continue;

        const QString id = plugin.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;

        QStringList granted = grants.value(id);
        const QStringList revokedForPlugin = revoked.value(id);
        bool changed = false;
        for (const QVariant &permissionValue : plugin.value(QStringLiteral("permissions")).toList()) {
            const QString permission = permissionValue.toString();
            if (permission.isEmpty())
                continue;
            if (granted.contains(permission))
                continue;
            // 用户显式撤销过的权限不自动补回，否则「撤销」在下次启动就失效。
            if (revokedForPlugin.contains(permission))
                continue;
            granted.append(permission);
            changed = true;
        }
        if (changed)
            patched.insert(id, granted);
    }

    return patched;
}

QStringList PluginLoader::disabledPlugins() const
{
    return readDisabledList();
}

QStringList PluginLoader::readDisabledList() const
{
    if (!m_database)
        return {};
    return idsFromJsonArray(m_database->metaValue(QString::fromLatin1(kDisabledMetaKey)));
}

bool PluginLoader::writeDisabledList(const QStringList &ids) const
{
    if (!m_database)
        return false;
    return m_database->setMetaValue(QString::fromLatin1(kDisabledMetaKey),
                                    jsonArrayFromIds(ids));
}

void PluginLoader::scan()
{
    QElapsedTimer scanTimer;
    scanTimer.start();
    m_plugins.clear();
    m_candidates.clear();
    QSet<QString> seenIds;

    const QStringList disabledIds = readDisabledList();

    const auto scanDirectory = [this, &seenIds, &disabledIds](const QString &rootPath,
                                                              const QString &source) {
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
            plugin.insert(QStringLiteral("backgroundUrl"), QString());
            plugin.insert(QStringLiteral("settings"), QString());
            plugin.insert(QStringLiteral("settingsUrl"), QString());
            plugin.insert(QStringLiteral("slots"), QVariantMap());
            plugin.insert(QStringLiteral("slotUrls"), QVariantMap());
            plugin.insert(QStringLiteral("source"), source);
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
            const QString background =
                object.value(QStringLiteral("background")).toString();

            plugin[QStringLiteral("id")] = id;
            plugin[QStringLiteral("name")] = name;
            plugin[QStringLiteral("version")] = version;
            plugin[QStringLiteral("apiVersion")] = apiVersion;
            plugin[QStringLiteral("main")] = main;
            plugin[QStringLiteral("background")] = background;

            // R15-02：manifest 的类别与依赖说明原样搬运，插件库面板据此展示；
            // 缺省为空，面板不显示徽章。
            plugin[QStringLiteral("category")] = object.value(QStringLiteral("category")).toString();
            QVariantList manifestDependencies;
            for (const QJsonValue &value : object.value(QStringLiteral("dependencies")).toArray())
                manifestDependencies.append(value.toString());
            plugin[QStringLiteral("dependencies")] = manifestDependencies;

            QVariantList permissions;
            const QJsonArray permissionArray = object.value(QStringLiteral("permissions")).toArray();
            for (const QJsonValue &value : permissionArray)
                permissions.append(value.toString());
            plugin[QStringLiteral("permissions")] = permissions;

            const QJsonObject contributes = object.value(QStringLiteral("contributes")).toObject();
            QStringList contributedEvents;
            for (const QJsonValue &value : contributes.value(QStringLiteral("events")).toArray()) {
                const QString event = value.toString().trimmed();
                if (!event.isEmpty() && !contributedEvents.contains(event))
                    contributedEvents.append(event);
            }
            QVariantList contributedCommands;
            for (const QJsonValue &value : contributes.value(QStringLiteral("commands")).toArray()) {
                const QJsonObject command = value.toObject();
                if (command.isEmpty())
                    continue;
                const QString commandId = command.value(QStringLiteral("id")).toString().trimmed();
                if (commandId.isEmpty())
                    continue;
                contributedCommands.append(QVariantMap{
                    {QStringLiteral("id"), commandId},
                    {QStringLiteral("title"), command.value(QStringLiteral("title")).toString()},
                    {QStringLiteral("category"), command.value(QStringLiteral("category")).toString()},
                    {QStringLiteral("description"), command.value(QStringLiteral("description")).toString()},
                    {QStringLiteral("shortcut"), command.value(QStringLiteral("shortcut")).toString()}
                });
            }
            plugin[QStringLiteral("events")] = contributedEvents;
            plugin[QStringLiteral("commands")] = contributedCommands;

            // R9-01：contributes.agentProfiles —— 插件声明一个可被派活的命令行 agent。
            // 这里只做「原样搬运 + 记来源插件 id」，合法性校验在 AgentProfile::normalizeProfile：
            // 判断 profile 是否合法的知识只有那一处，放两份必然漂移。
            QVariantList contributedAgentProfiles;
            for (const QJsonValue &value :
                 contributes.value(QStringLiteral("agentProfiles")).toArray()) {
                if (!value.isObject())
                    continue;
                QVariantMap entry = value.toObject().toVariantMap();
                entry.insert(QStringLiteral("pluginId"), id);
                contributedAgentProfiles.append(entry);
            }
            plugin[QStringLiteral("agentProfiles")] = contributedAgentProfiles;

            // 设置页：顶层 settings 或 contributes.settings，两者取先声明者
            QString settingsPath = object.value(QStringLiteral("settings")).toString().trimmed();
            if (settingsPath.isEmpty())
                settingsPath = contributes.value(QStringLiteral("settings")).toString().trimmed();
            plugin[QStringLiteral("settings")] = settingsPath;

            // 多槽位：contributes.slots = { sidebar|bottom|card: 相对路径 }
            QVariantMap slotPaths;
            const QJsonObject slotObject = contributes.value(QStringLiteral("slots")).toObject();
            const QStringList knownSlots{QStringLiteral("sidebar"), QStringLiteral("bottom"),
                                         QStringLiteral("card")};
            for (const QString &slotName : knownSlots) {
                const QString slotPath = slotObject.value(slotName).toString().trimmed();
                if (!slotPath.isEmpty())
                    slotPaths.insert(slotName, slotPath);
            }

            if (id.isEmpty()) {
                plugin[QStringLiteral("status")] = QStringLiteral("error");
                plugin[QStringLiteral("error")] = QStringLiteral("缺少 id");
            } else if (apiVersion < MinimumApiVersion || apiVersion > MaximumApiVersion) {
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

                if (!background.isEmpty() && plugin[QStringLiteral("status")] == QStringLiteral("ok")) {
                    const QFileInfo backgroundInfo(pluginDir + QLatin1Char('/') + background);
                    if (!backgroundInfo.exists() || !backgroundInfo.isFile()) {
                        plugin[QStringLiteral("status")] = QStringLiteral("error");
                        plugin[QStringLiteral("error")] =
                            QStringLiteral("后台入口文件不存在: %1").arg(background);
                    } else {
                        plugin[QStringLiteral("backgroundUrl")] =
                            QUrl::fromLocalFile(backgroundInfo.absoluteFilePath()).toString();
                    }
                }

                // 设置页与槽位文件必须落盘存在，缺失视同入口缺失（整插件异常）
                if (plugin[QStringLiteral("status")] == QStringLiteral("ok")) {
                    const auto surfaceUrl = [&pluginDir](const QString &relative) -> QString {
                        const QFileInfo info(pluginDir + QLatin1Char('/') + relative);
                        if (!info.exists() || !info.isFile())
                            return QString();
                        return QUrl::fromLocalFile(info.absoluteFilePath()).toString();
                    };

                    if (!settingsPath.isEmpty()) {
                        const QString url = surfaceUrl(settingsPath);
                        if (url.isEmpty()) {
                            plugin[QStringLiteral("status")] = QStringLiteral("error");
                            plugin[QStringLiteral("error")] =
                                QStringLiteral("设置页文件不存在: %1").arg(settingsPath);
                        } else {
                            plugin[QStringLiteral("settingsUrl")] = url;
                        }
                    }

                    if (plugin[QStringLiteral("status")] == QStringLiteral("ok")
                        && !slotPaths.isEmpty()) {
                        QVariantMap slotUrls;
                        for (auto it = slotPaths.constBegin(); it != slotPaths.constEnd(); ++it) {
                            const QString url = surfaceUrl(it.value().toString());
                            if (url.isEmpty()) {
                                plugin[QStringLiteral("status")] = QStringLiteral("error");
                                plugin[QStringLiteral("error")] =
                                    QStringLiteral("槽位文件不存在: %1 (%2)")
                                        .arg(it.value().toString(), it.key());
                                break;
                            }
                            slotUrls.insert(it.key(), url);
                        }
                        if (plugin[QStringLiteral("status")] == QStringLiteral("ok"))
                            plugin[QStringLiteral("slotUrls")] = slotUrls;
                    }
                }
            }

            if (!id.isEmpty() && disabledIds.contains(id)
                && plugin[QStringLiteral("status")] == QStringLiteral("ok")) {
                plugin[QStringLiteral("status")] = QStringLiteral("disabled");
                plugin[QStringLiteral("error")] = QStringLiteral("插件已禁用");
            }

            const bool shadowed = !id.isEmpty() && seenIds.contains(id);
            plugin[QStringLiteral("shadowed")] = shadowed;
            if (shadowed) {
                // 同 id 被前面的源覆盖：仍记进候选清单，插件库据此判断「可安装」
                m_candidates.append(plugin);
                continue;
            }
            if (!id.isEmpty())
                seenIds.insert(id);

            m_plugins.append(plugin);
            m_candidates.append(plugin);
        }
    };

    const QStringList roots = sourceDirs();
    const char *sources[] = {"appdata", "appdir", "official", "sample"};
    for (int i = 0; i < roots.size(); ++i)
        scanDirectory(roots.at(i), QString::fromLatin1(sources[i]));

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

bool PluginLoader::isPluginEnabled(const QString &id) const
{
    if (id.isEmpty())
        return false;
    return !readDisabledList().contains(id);
}

bool PluginLoader::setPluginEnabled(const QString &id, bool enabled)
{
    if (id.isEmpty())
        return false;

    QStringList ids = readDisabledList();
    const bool wasDisabled = ids.contains(id);

    if (enabled) {
        if (!wasDisabled)
            return true;
        ids.removeAll(id);
    } else {
        if (wasDisabled)
            return true;
        ids.append(id);
    }

    if (!writeDisabledList(ids))
        return false;

    scan();
    return true;
}
