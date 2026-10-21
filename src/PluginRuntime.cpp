#include "PluginRuntime.h"

#include "Database.h"
#include "PluginContext.h"
#include "PluginEventBus.h"
#include "PluginLoader.h"
#include "TaskModel.h"
#include "TrayController.h"

#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>

PluginRuntime::PluginRuntime(PluginLoader *loader, PluginEventBus *bus, Database *database,
                             TaskModel *tasks, TrayController *tray, QObject *parent)
    : QObject(parent)
    , m_loader(loader)
    , m_bus(bus)
    , m_database(database)
    , m_tasks(tasks)
    , m_tray(tray)
{
}

void PluginRuntime::attachEngine(QQmlApplicationEngine *engine)
{
    m_engine = engine;
}

void PluginRuntime::start()
{
    connectTaskEvents();
    loadBackgroundPlugins();
}

void PluginRuntime::reload()
{
    unloadBackgroundPlugins();
    if (m_loader)
        m_loader->scan();
    refreshContexts();
    loadBackgroundPlugins();
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
    }

    emit backgroundCountChanged();
}

void PluginRuntime::unloadBackgroundPlugins()
{
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
