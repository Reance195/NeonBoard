#include "PluginRuntime.h"

#include "Database.h"
#include "PluginContext.h"
#include "PluginEventBus.h"
#include "PluginHost.h"
#include "PluginLoader.h"
#include "TaskModel.h"
#include "TrayController.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <utility>

namespace {
constexpr const char *kGrantsMetaKey = "plugins.grants";
constexpr int kWatchDebounceMs = 400;

QHash<QString, QStringList> parseGrants(const QString &payload)
{
    QHash<QString, QStringList> grants;
    if (payload.isEmpty())
        return grants;

    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject())
        return grants;

    const QJsonObject object = document.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        QStringList permissions;
        for (const QJsonValue &value : it.value().toArray()) {
            const QString permission = value.toString().trimmed();
            if (!permission.isEmpty() && !permissions.contains(permission))
                permissions.append(permission);
        }
        grants.insert(it.key(), permissions);
    }
    return grants;
}

QString grantsToJson(const QHash<QString, QStringList> &grants)
{
    QJsonObject object;
    QStringList ids = grants.keys();
    ids.sort();
    for (const QString &id : ids) {
        QStringList permissions = grants.value(id);
        permissions.sort();
        QJsonArray array;
        for (const QString &permission : permissions)
            array.append(permission);
        object.insert(id, array);
    }
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString sourceLabel(const QString &source)
{
    if (source == QLatin1String("appdata"))
        return QStringLiteral("用户安装");
    if (source == QLatin1String("appdir"))
        return QStringLiteral("随包目录");
    if (source == QLatin1String("official"))
        return QStringLiteral("官方源");
    if (source == QLatin1String("sample"))
        return QStringLiteral("示例插件");
    return source;
}

// 规范化插件声明的快捷键；非法或缺少修饰键时返回空串（该命令不带全局快捷键）
QString normalizeShortcut(const QString &shortcut)
{
    const QString trimmed = shortcut.trimmed();
    if (trimmed.isEmpty())
        return {};

    const QStringList parts = trimmed.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return {};

    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool meta = false;
    for (int i = 0; i < parts.size() - 1; ++i) {
        const QString part = parts.at(i).trimmed().toLower();
        if (part == QLatin1String("ctrl") || part == QLatin1String("control"))
            ctrl = true;
        else if (part == QLatin1String("alt"))
            alt = true;
        else if (part == QLatin1String("shift"))
            shift = true;
        else if (part == QLatin1String("meta") || part == QLatin1String("win")
                 || part == QLatin1String("super"))
            meta = true;
        else
            return {};
    }
    if (!ctrl && !alt && !shift && !meta)
        return {};

    const QString rawKey = parts.last().trimmed();
    if (rawKey.isEmpty())
        return {};

    QString key;
    if (rawKey.size() == 1 && rawKey.at(0).isLetterOrNumber()) {
        key = rawKey.toUpper();
    } else if (QRegularExpression(QStringLiteral("^[Ff]([1-9]|1[0-9]|2[0-4])$"))
                   .match(rawKey)
                   .hasMatch()) {
        key = rawKey.toUpper();
    } else {
        static const QHash<QString, QString> named{
            {QStringLiteral("esc"), QStringLiteral("Escape")},
            {QStringLiteral("escape"), QStringLiteral("Escape")},
            {QStringLiteral("enter"), QStringLiteral("Return")},
            {QStringLiteral("return"), QStringLiteral("Return")},
            {QStringLiteral("space"), QStringLiteral("Space")},
            {QStringLiteral("tab"), QStringLiteral("Tab")},
            {QStringLiteral("comma"), QStringLiteral(",")},
            {QStringLiteral("backspace"), QStringLiteral("Backspace")},
            {QStringLiteral("delete"), QStringLiteral("Delete")},
            {QStringLiteral("insert"), QStringLiteral("Insert")},
            {QStringLiteral("home"), QStringLiteral("Home")},
            {QStringLiteral("end"), QStringLiteral("End")},
            {QStringLiteral("pageup"), QStringLiteral("PageUp")},
            {QStringLiteral("pagedown"), QStringLiteral("PageDown")},
            {QStringLiteral("up"), QStringLiteral("Up")},
            {QStringLiteral("down"), QStringLiteral("Down")},
            {QStringLiteral("left"), QStringLiteral("Left")},
            {QStringLiteral("right"), QStringLiteral("Right")}};
        const QString lowered = rawKey.toLower();
        if (!named.contains(lowered))
            return {};
        key = named.value(lowered);
    }

    QStringList tokens;
    if (ctrl)
        tokens.append(QStringLiteral("Ctrl"));
    if (alt)
        tokens.append(QStringLiteral("Alt"));
    if (shift)
        tokens.append(QStringLiteral("Shift"));
    if (meta)
        tokens.append(QStringLiteral("Meta"));
    tokens.append(key);
    return tokens.join(QLatin1Char('+'));
}
}

PluginRuntime::PluginRuntime(PluginLoader *loader, PluginEventBus *bus, Database *database,
                             TaskModel *tasks, TrayController *tray, QObject *parent)
    : QObject(parent)
    , m_loader(loader)
    , m_bus(bus)
    , m_database(database)
    , m_tasks(tasks)
    , m_tray(tray)
{
    if (m_loader) {
        connect(m_loader, &PluginLoader::pluginsChanged, this, [this]() {
            emit commandsChanged();
            emit surfacesChanged();
        });
    }
}

void PluginRuntime::attachEngine(QQmlApplicationEngine *engine)
{
    m_engine = engine;
}

void PluginRuntime::ensureWatcher()
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
                &PluginRuntime::onWatchTriggered);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
                &PluginRuntime::onWatchTriggered);
    }
    if (!m_reloadDebounce) {
        m_reloadDebounce = new QTimer(this);
        m_reloadDebounce->setSingleShot(true);
        m_reloadDebounce->setInterval(kWatchDebounceMs);
        connect(m_reloadDebounce, &QTimer::timeout, this, &PluginRuntime::reloadFromDisk);
    }
}

void PluginRuntime::start()
{
    ensureGrantsBootstrapped();
    connectTaskEvents();
    loadBackgroundPlugins();
    ensureWatcher();
    refreshWatchPaths();
    m_fingerprint = computeFingerprint();
    emit commandsChanged();
    emit surfacesChanged();
}

void PluginRuntime::reload()
{
    unloadBackgroundPlugins();
    if (m_loader)
        m_loader->scan();
    refreshContexts();
    if (m_engine)
        m_engine->clearComponentCache();
    loadBackgroundPlugins();
    refreshWatchPaths();
    m_fingerprint = computeFingerprint();
}

void PluginRuntime::refreshWatchPaths()
{
    if (!m_watcher || !m_loader)
        return;

    QStringList wanted;
    const QStringList roots = m_loader->sourceDirs();
    for (const QString &rootPath : roots) {
        const QDir root(rootPath);
        if (!root.exists())
            continue;
        wanted.append(root.absolutePath());
        const QFileInfoList pluginDirs =
            root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &pluginDir : pluginDirs) {
            wanted.append(pluginDir.absoluteFilePath());
            const QFileInfoList files =
                QDir(pluginDir.absoluteFilePath()).entryInfoList(QDir::Files, QDir::Name);
            for (const QFileInfo &file : files)
                wanted.append(file.absoluteFilePath());
        }
    }

    const QStringList current = m_watcher->directories() + m_watcher->files();
    QStringList added = wanted;
    for (const QString &path : current)
        added.removeAll(path);
    QStringList removed = current;
    for (const QString &path : wanted)
        removed.removeAll(path);

    if (!added.isEmpty())
        m_watcher->addPaths(added);
    if (!removed.isEmpty())
        m_watcher->removePaths(removed);
}

void PluginRuntime::onWatchTriggered()
{
    if (m_reloadDebounce)
        m_reloadDebounce->start();
}

void PluginRuntime::reloadFromDisk()
{
    if (!m_loader)
        return;

    m_loader->scan();
    const QString fingerprint = computeFingerprint();
    if (fingerprint == m_fingerprint) {
        refreshWatchPaths();
        return;
    }

    const QVariantList plugins = m_loader->plugins();
    qInfo() << "PluginRuntime: plugin directory changed, reloading"
            << plugins.size() << "plugin(s)";
    unloadBackgroundPlugins();
    refreshContexts();
    if (m_engine)
        m_engine->clearComponentCache();
    loadBackgroundPlugins();
    m_fingerprint = fingerprint;
    refreshWatchPaths();
}

QString PluginRuntime::computeFingerprint() const
{
    if (!m_loader)
        return {};

    QStringList parts;
    const QVariantList plugins = m_loader->plugins();
    for (const QVariant &value : plugins) {
        const QVariantMap plugin = value.toMap();
        const QString dir = plugin.value(QStringLiteral("dir")).toString();
        qint64 newest = 0;
        const QFileInfoList files =
            QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &file : files)
            newest = qMax(newest, file.lastModified().toMSecsSinceEpoch());
        parts.append(QStringLiteral("%1|%2|%3|%4|%5")
                         .arg(plugin.value(QStringLiteral("id")).toString(),
                              plugin.value(QStringLiteral("version")).toString(),
                              plugin.value(QStringLiteral("status")).toString(),
                              plugin.value(QStringLiteral("backgroundUrl")).toString(),
                              QString::number(newest)));
    }
    parts.sort();
    return parts.join(QLatin1Char('\n'));
}

PluginContext *PluginRuntime::contextFor(const QString &pluginId)
{
    if (!m_loader || pluginId.isEmpty())
        return nullptr;

    const QVariantMap plugin = m_loader->pluginById(pluginId);
    if (plugin.isEmpty())
        return nullptr;
    if (plugin.value(QStringLiteral("status")).toString() != QStringLiteral("ok"))
        return nullptr;

    const QString id = plugin.value(QStringLiteral("id")).toString();
    const QString name = plugin.value(QStringLiteral("name"), id).toString();
    const QString version = plugin.value(QStringLiteral("version")).toString();
    const QString baseUrl = plugin.value(QStringLiteral("baseUrl")).toString();

    QStringList permissions;
    const QVariant rawPermissions = plugin.value(QStringLiteral("permissions"));
    if (rawPermissions.canConvert<QStringList>())
        permissions = rawPermissions.toStringList();
    else
        for (const QVariant &permission : rawPermissions.toList())
            permissions.append(permission.toString());

    const bool hasBackground =
        !plugin.value(QStringLiteral("backgroundUrl")).toString().isEmpty();

    auto it = m_contexts.find(id);
    if (it != m_contexts.end()) {
        it.value()->updateManifest(name, version, permissions);
        return it.value();
    }

    auto *context = new PluginContext(id, name, version, permissions, QUrl(baseUrl),
                                      hasBackground, m_database, m_tasks, m_tray, m_bus, this);
    const QString contextId = id;
    context->setPermissionHooks(
        [this, contextId](const QString &permission) {
            return isPermissionGranted(contextId, permission);
        },
        [this, contextId](const QString &permission) {
            const QString key = contextId + QLatin1Char('|') + permission;
            QMetaObject::invokeMethod(
                this,
                [this, contextId, permission, key]() {
                    if (m_promptedPermissions.contains(key))
                        return;
                    m_promptedPermissions.insert(key);
                    emit permissionRequested(contextId, permission);
                },
                Qt::QueuedConnection);
        });
    m_contexts.insert(id, context);
    return context;
}

void PluginRuntime::setActiveContext(PluginContext *context)
{
    if (m_activeContext == context)
        return;
    m_activeContext = context;
}

QStringList PluginRuntime::backgroundPlugins() const
{
    QStringList ids;
    for (const BackgroundEntry &entry : m_background)
        ids.append(entry.pluginId);
    return ids;
}

void PluginRuntime::connectTaskEvents()
{
    if (!m_tasks || !m_bus)
        return;

    connect(m_tasks, &TaskModel::taskCreated, this, [this](const QString &id, const QString &title) {
        m_bus->publish(QStringLiteral("task.created"),
                       QVariantMap{{QStringLiteral("id"), id},
                                   {QStringLiteral("title"), title}});
    });

    connect(m_tasks, &TaskModel::taskUpdated, this, [this](const QString &id) {
        m_bus->publish(QStringLiteral("task.updated"), QVariantMap{{QStringLiteral("id"), id}});
    });

    connect(m_tasks, &TaskModel::taskMoved, this,
            [this](const QString &id, const QString &fromStatus, const QString &toStatus) {
        m_bus->publish(QStringLiteral("task.moved"),
                       QVariantMap{{QStringLiteral("id"), id},
                                   {QStringLiteral("from"), fromStatus},
                                   {QStringLiteral("to"), toStatus}});
    });

    connect(m_tasks, &TaskModel::taskCompleted, this, [this](const QString &id) {
        m_bus->publish(QStringLiteral("task.completed"), QVariantMap{{QStringLiteral("id"), id}});
    });

    connect(m_tasks, &TaskModel::taskRemoved, this, [this](const QString &id) {
        m_bus->publish(QStringLiteral("task.removed"), QVariantMap{{QStringLiteral("id"), id}});
    });
}

void PluginRuntime::refreshContexts()
{
    if (!m_loader)
        return;

    const QVariantList plugins = m_loader->plugins();
    QStringList alive;
    for (const QVariant &value : plugins) {
        const QVariantMap plugin = value.toMap();
        const QString id = plugin.value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            alive.append(id);
    }

    const auto keys = m_contexts.keys();
    for (const QString &id : keys) {
        if (alive.contains(id))
            continue;
        PluginContext *context = m_contexts.take(id);
        if (!context)
            continue;
        if (context == m_activeContext) {
            m_activeContext = nullptr;
            continue;
        }
        if (m_bus)
            m_bus->unsubscribe(context);
        context->deleteLater();
    }
}

void PluginRuntime::loadBackgroundPlugins()
{
    if (!m_loader || !m_engine)
        return;

    const QVariantList plugins = m_loader->plugins();
    for (const QVariant &value : plugins) {
        const QVariantMap plugin = value.toMap();
        if (plugin.value(QStringLiteral("status")).toString() != QStringLiteral("ok"))
            continue;

        const QString backgroundUrl = plugin.value(QStringLiteral("backgroundUrl")).toString();
        if (backgroundUrl.isEmpty())
            continue;

        const QString pluginId = plugin.value(QStringLiteral("id")).toString();
        if (pluginId.isEmpty())
            continue;

        bool loaded = false;
        for (const BackgroundEntry &entry : m_background) {
            if (entry.pluginId == pluginId) {
                loaded = true;
                break;
            }
        }
        if (loaded)
            continue;

        PluginContext *context = contextFor(pluginId);
        if (!context)
            continue;

        QQmlContext *child = new QQmlContext(m_engine->rootContext(), m_engine);
        child->setContextProperty(QStringLiteral("pluginHost"), context);

        auto *component = new QQmlComponent(m_engine, QUrl(backgroundUrl));
        if (!component->isReady()) {
            qWarning() << "PluginRuntime: background load failed for" << pluginId
                       << component->errorString().trimmed();
            delete component;
            child->deleteLater();
            continue;
        }

        QObject *object = component->create(child);
        if (!object) {
            qWarning() << "PluginRuntime: background instance failed for" << pluginId;
            delete component;
            child->deleteLater();
            continue;
        }

        BackgroundEntry entry;
        entry.pluginId = pluginId;
        entry.context = child;
        entry.component = component;
        entry.object = object;
        m_background.append(entry);
        qInfo() << "PluginRuntime: background plugin started:" << pluginId;
        dispatchPending(pluginId);
    }

    emit backgroundCountChanged();
}

void PluginRuntime::unloadBackgroundPlugins()
{
    if (!m_background.isEmpty())
        qInfo() << "PluginRuntime: unloading" << m_background.size() << "background plugin(s)";
    for (const BackgroundEntry &entry : m_background) {
        if (m_bus) {
            if (PluginContext *context = m_contexts.value(entry.pluginId))
                m_bus->unsubscribe(context);
        }
        delete entry.object;
        delete entry.component;
        if (entry.context)
            entry.context->deleteLater();
    }
    m_background.clear();
    emit backgroundCountChanged();
}

// ---------- R3 插件平台 2.0：表面（命令 / 设置页 / 槽位） ----------

QVariantList PluginRuntime::pluginCommands() const
{
    QVariantList result;
    if (!m_loader)
        return result;

    ensureGrantsBootstrapped();

    QSet<QString> used;
    // 宿主固定快捷键：插件声明与之冲突时禁用，避免抢键
    const QSet<QString> reserved{
        QStringLiteral("Ctrl+Z"), QStringLiteral("Ctrl+N"), QStringLiteral("Ctrl+K"),
        QStringLiteral("Ctrl+,")};

    for (const QVariant &value : m_loader->plugins()) {
        const QVariantMap plugin = value.toMap();
        if (plugin.value(QStringLiteral("status")).toString() != QStringLiteral("ok"))
            continue;

        const QString pluginId = plugin.value(QStringLiteral("id")).toString();
        const QString pluginName = plugin.value(QStringLiteral("name"), pluginId).toString();

        for (const QVariant &commandValue : plugin.value(QStringLiteral("commands")).toList()) {
            const QVariantMap command = commandValue.toMap();
            const QString commandId = command.value(QStringLiteral("id")).toString();
            if (commandId.isEmpty())
                continue;

            const QString declared =
                normalizeShortcut(command.value(QStringLiteral("shortcut")).toString());
            bool conflict = false;
            QString shortcut = declared;
            if (!declared.isEmpty()) {
                if (used.contains(declared) || reserved.contains(declared)) {
                    conflict = true;
                    shortcut.clear();
                } else {
                    used.insert(declared);
                }
            }

            result.append(QVariantMap{
                {QStringLiteral("pluginId"), pluginId},
                {QStringLiteral("pluginName"), pluginName},
                {QStringLiteral("id"), commandId},
                {QStringLiteral("title"), command.value(QStringLiteral("title")).toString()},
                {QStringLiteral("category"), command.value(QStringLiteral("category")).toString()},
                {QStringLiteral("description"),
                 command.value(QStringLiteral("description")).toString()},
                {QStringLiteral("shortcut"), shortcut},
                {QStringLiteral("declaredShortcut"), declared},
                {QStringLiteral("conflict"), conflict}});
        }
    }
    return result;
}

QVariantList PluginRuntime::settingsPages() const
{
    QVariantList result;
    if (!m_loader)
        return result;

    for (const QVariant &value : m_loader->plugins()) {
        const QVariantMap plugin = value.toMap();
        if (plugin.value(QStringLiteral("status")).toString() != QStringLiteral("ok"))
            continue;

        const QString url = plugin.value(QStringLiteral("settingsUrl")).toString();
        if (url.isEmpty())
            continue;

        const QString pluginId = plugin.value(QStringLiteral("id")).toString();
        result.append(QVariantMap{
            {QStringLiteral("pluginId"), pluginId},
            {QStringLiteral("pluginName"), plugin.value(QStringLiteral("name"), pluginId).toString()},
            {QStringLiteral("url"), url},
            {QStringLiteral("sourceLabel"),
             sourceLabel(plugin.value(QStringLiteral("source")).toString())}});
    }
    return result;
}

QVariantList PluginRuntime::slotItems(const QString &slot) const
{
    QVariantList result;
    if (!m_loader || slot.isEmpty())
        return result;

    for (const QVariant &value : m_loader->plugins()) {
        const QVariantMap plugin = value.toMap();
        if (plugin.value(QStringLiteral("status")).toString() != QStringLiteral("ok"))
            continue;

        const QVariantMap slotUrls = plugin.value(QStringLiteral("slotUrls")).toMap();
        const QString url = slotUrls.value(slot).toString();
        if (url.isEmpty())
            continue;

        const QString pluginId = plugin.value(QStringLiteral("id")).toString();
        result.append(QVariantMap{
            {QStringLiteral("pluginId"), pluginId},
            {QStringLiteral("pluginName"), plugin.value(QStringLiteral("name"), pluginId).toString()},
            {QStringLiteral("slot"), slot},
            {QStringLiteral("url"), url}});
    }
    return result;
}

PluginHost *PluginRuntime::hostFor(const QString &pluginId)
{
    if (pluginId.isEmpty())
        return nullptr;

    auto it = m_hosts.find(pluginId);
    if (it != m_hosts.end())
        return it.value();

    auto *host = new PluginHost(this, pluginId, this);
    m_hosts.insert(pluginId, host);
    return host;
}

bool PluginRuntime::pluginDeclaresCommand(const QString &pluginId, const QString &commandId) const
{
    if (!m_loader || pluginId.isEmpty() || commandId.isEmpty())
        return false;

    const QVariantMap plugin = m_loader->pluginById(pluginId);
    if (plugin.isEmpty() || plugin.value(QStringLiteral("status")).toString() != QStringLiteral("ok"))
        return false;

    for (const QVariant &value : plugin.value(QStringLiteral("commands")).toList()) {
        if (value.toMap().value(QStringLiteral("id")).toString() == commandId)
            return true;
    }
    return false;
}

bool PluginRuntime::contextRunning(const QString &pluginId) const
{
    if (pluginId.isEmpty())
        return false;
    if (m_activeContext && m_activeContext->pluginId() == pluginId)
        return true;
    for (const BackgroundEntry &entry : m_background) {
        if (entry.pluginId == pluginId)
            return true;
    }
    return false;
}

bool PluginRuntime::isRunning(const QString &pluginId) const
{
    return contextRunning(pluginId);
}

bool PluginRuntime::invokeCommand(const QString &pluginId, const QString &commandId)
{
    if (!pluginDeclaresCommand(pluginId, commandId))
        return false;

    if (contextRunning(pluginId)) {
        if (PluginContext *context = m_contexts.value(pluginId)) {
            context->deliverCommand(commandId);
            emit commandDelivered(pluginId, commandId);
            return true;
        }
    }

    const QPair<QString, QString> pending{pluginId, commandId};
    if (!m_pendingCommands.contains(pending)) {
        m_pendingCommands.append(pending);
        emit commandPending(pluginId, commandId);
    }
    return false;
}

int PluginRuntime::dispatchPending(const QString &pluginId)
{
    if (pluginId.isEmpty())
        return 0;

    PluginContext *context = m_contexts.value(pluginId);
    int delivered = 0;
    QList<QPair<QString, QString>> remaining;
    for (const QPair<QString, QString> &pending : m_pendingCommands) {
        if (pending.first != pluginId) {
            remaining.append(pending);
            continue;
        }
        if (context) {
            context->deliverCommand(pending.second);
            emit commandDelivered(pluginId, pending.second);
            ++delivered;
        }
    }
    m_pendingCommands = remaining;
    return delivered;
}

// ---------- R3 权限授予 ----------

void PluginRuntime::ensureGrantsBootstrapped() const
{
    if (m_grantsReady || !m_database)
        return;
    m_grantsReady = true;

    const QString payload = m_database->metaValue(QString::fromLatin1(kGrantsMetaKey));
    if (!payload.isEmpty()) {
        m_grants = parseGrants(payload);
        return;
    }

    // 首次启用授权模型：把当前已发现插件声明的权限全部预授予（祖父条款，老插件零改动）
    if (m_loader) {
        for (const QVariant &value : m_loader->plugins()) {
            const QVariantMap plugin = value.toMap();
            if (plugin.value(QStringLiteral("status")).toString() != QStringLiteral("ok"))
                continue;
            const QString id = plugin.value(QStringLiteral("id")).toString();
            if (id.isEmpty())
                continue;
            QStringList permissions;
            for (const QVariant &permission : plugin.value(QStringLiteral("permissions")).toList()) {
                const QString name = permission.toString();
                if (!name.isEmpty() && !permissions.contains(name))
                    permissions.append(name);
            }
            if (!permissions.isEmpty())
                m_grants.insert(id, permissions);
        }
    }
    persistGrants();
    qInfo() << "PluginRuntime: permission grants bootstrapped for" << m_grants.size()
            << "plugin(s)";
}

void PluginRuntime::persistGrants() const
{
    if (!m_database)
        return;
    m_database->setMetaValue(QString::fromLatin1(kGrantsMetaKey), grantsToJson(m_grants));
}

QStringList PluginRuntime::grantedPermissions(const QString &pluginId) const
{
    ensureGrantsBootstrapped();
    return m_grants.value(pluginId);
}

QStringList PluginRuntime::missingPermissions(const QString &pluginId) const
{
    if (!m_loader || pluginId.isEmpty())
        return {};

    const QVariantMap plugin = m_loader->pluginById(pluginId);
    const QStringList granted = grantedPermissions(pluginId);
    QStringList missing;
    for (const QVariant &value : plugin.value(QStringLiteral("permissions")).toList()) {
        const QString name = value.toString();
        if (!name.isEmpty() && !granted.contains(name))
            missing.append(name);
    }
    return missing;
}

bool PluginRuntime::isPermissionGranted(const QString &pluginId, const QString &permission) const
{
    if (pluginId.isEmpty() || permission.isEmpty())
        return false;
    return grantedPermissions(pluginId).contains(permission);
}

bool PluginRuntime::grantPermissions(const QString &pluginId, const QStringList &permissions)
{
    if (pluginId.isEmpty() || permissions.isEmpty())
        return false;

    QStringList granted = grantedPermissions(pluginId);
    bool changed = false;
    for (const QString &permission : permissions) {
        if (permission.isEmpty() || granted.contains(permission))
            continue;
        granted.append(permission);
        changed = true;
    }
    if (!changed)
        return true;

    m_grants.insert(pluginId, granted);
    persistGrants();
    emit surfacesChanged();
    return true;
}

bool PluginRuntime::setPermissionGranted(const QString &pluginId, const QString &permission,
                                         bool granted)
{
    if (pluginId.isEmpty() || permission.isEmpty())
        return false;

    QStringList current = grantedPermissions(pluginId);
    if (granted) {
        if (current.contains(permission))
            return true;
        current.append(permission);
    } else {
        if (!current.contains(permission))
            return true;
        current.removeAll(permission);
    }

    m_grants.insert(pluginId, current);
    persistGrants();
    emit surfacesChanged();
    return true;
}

QVariantMap PluginRuntime::permissionPrompt(const QString &pluginId) const
{
    QVariantMap prompt;
    if (!m_loader || pluginId.isEmpty())
        return prompt;

    const QVariantMap plugin = m_loader->pluginById(pluginId);
    if (plugin.isEmpty())
        return prompt;

    const QVariantMap catalog = permissionCatalog();
    QVariantList pending;
    for (const QString &permission : missingPermissions(pluginId)) {
        QVariantMap info = catalog.value(permission).toMap();
        if (info.isEmpty()) {
            info.insert(QStringLiteral("title"), permission);
            info.insert(QStringLiteral("detail"), QStringLiteral("插件声明的自定义权限"));
            info.insert(QStringLiteral("risk"), QStringLiteral("中"));
        }
        info.insert(QStringLiteral("name"), permission);
        pending.append(info);
    }

    prompt.insert(QStringLiteral("pluginId"), pluginId);
    prompt.insert(QStringLiteral("pluginName"),
                  plugin.value(QStringLiteral("name"), pluginId).toString());
    prompt.insert(QStringLiteral("source"), plugin.value(QStringLiteral("source")).toString());
    prompt.insert(QStringLiteral("sourceLabel"),
                  sourceLabel(plugin.value(QStringLiteral("source")).toString()));
    prompt.insert(QStringLiteral("dir"), plugin.value(QStringLiteral("dir")).toString());
    prompt.insert(QStringLiteral("permissions"), pending);
    return prompt;
}

QVariantMap PluginRuntime::permissionCatalog() const
{
    const auto entry = [](const QString &title, const QString &detail, const QString &risk) {
        return QVariantMap{{QStringLiteral("title"), title},
                           {QStringLiteral("detail"), detail},
                           {QStringLiteral("risk"), risk}};
    };

    QVariantMap catalog;
    catalog.insert(QStringLiteral("storage"),
                   entry(QStringLiteral("插件存储"),
                         QStringLiteral("读写插件自己的键值数据"), QStringLiteral("低")));
    catalog.insert(QStringLiteral("tasks.read"),
                   entry(QStringLiteral("读取任务"),
                         QStringLiteral("读取全部任务内容与状态"), QStringLiteral("低")));
    catalog.insert(QStringLiteral("tasks.write"),
                   entry(QStringLiteral("修改任务"),
                         QStringLiteral("新增、更新、移动、删除任务"), QStringLiteral("高")));
    catalog.insert(QStringLiteral("notifications"),
                   entry(QStringLiteral("系统通知"), QStringLiteral("弹出托盘通知"),
                         QStringLiteral("低")));
    catalog.insert(QStringLiteral("openUrl"),
                   entry(QStringLiteral("打开链接"),
                         QStringLiteral("用系统浏览器打开 http/https 链接"),
                         QStringLiteral("中")));
    catalog.insert(QStringLiteral("meta"),
                   entry(QStringLiteral("全局元数据"),
                         QStringLiteral("读写 board.db 的 meta 表（含其它插件数据）"),
                         QStringLiteral("高")));
    catalog.insert(QStringLiteral("events"),
                   entry(QStringLiteral("事件订阅"),
                         QStringLiteral("订阅看板任务事件（创建/更新/移动/完成/删除）"),
                         QStringLiteral("低")));
    return catalog;
}
