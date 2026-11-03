#include "PluginContext.h"

#include "Database.h"
#include "PluginEventBus.h"
#include "TaskModel.h"
#include "TrayController.h"

#include <QDesktopServices>
#include <QDebug>
#include <QUrl>

PluginContext::PluginContext(const QString &pluginId,
                             const QString &name,
                             const QString &version,
                             const QStringList &permissions,
                             const QUrl &baseUrl,
                             bool background,
                             Database *database,
                             TaskModel *tasks,
                             TrayController *tray,
                             PluginEventBus *bus,
                             QObject *parent)
    : QObject(parent)
    , m_pluginId(pluginId)
    , m_name(name)
    , m_version(version)
    , m_permissions(permissions)
    , m_baseUrl(baseUrl)
    , m_background(background)
    , m_database(database)
    , m_tasks(tasks)
    , m_tray(tray)
    , m_bus(bus)
{
}

bool PluginContext::hasPermission(const QString &permission) const
{
    // 语义：声明 + 已授权，才是「当前可用」
    if (permission.isEmpty() || !m_permissions.contains(permission))
        return false;
    return isGranted(permission);
}

bool PluginContext::isGranted(const QString &permission) const
{
    if (!m_grantQuery)
        return true;
    return m_grantQuery(permission);
}

void PluginContext::setPermissionHooks(std::function<bool(const QString &)> query,
                                       std::function<void(const QString &)> request)
{
    m_grantQuery = std::move(query);
    m_grantRequest = std::move(request);
}

bool PluginContext::require(const QString &permission) const
{
    if (permission.isEmpty() || !m_permissions.contains(permission)) {
        qWarning() << "PluginContext: permission not declared" << permission << "for" << m_pluginId;
        return false;
    }
    if (!isGranted(permission)) {
        qWarning() << "PluginContext: permission not granted" << permission << "for" << m_pluginId;
        if (m_grantRequest)
            m_grantRequest(permission);
        return false;
    }
    return true;
}

QString PluginContext::storageGet(const QString &key) const
{
    if (!require(QStringLiteral("storage")))
        return {};
    return m_database->pluginValue(m_pluginId, key);
}

bool PluginContext::storageSet(const QString &key, const QString &value)
{
    if (!require(QStringLiteral("storage")))
        return false;
    return m_database->setPluginValue(m_pluginId, key, value);
}

bool PluginContext::storageRemove(const QString &key)
{
    if (!require(QStringLiteral("storage")))
        return false;
    return m_database->removePluginValue(m_pluginId, key);
}

QVariantList PluginContext::tasks() const
{
    if (!require(QStringLiteral("tasks.read")))
        return {};
    return m_tasks->allTasks();
}

QString PluginContext::addTask(const QString &title, const QString &status)
{
    if (!require(QStringLiteral("tasks.write")))
        return {};
    return m_tasks->addTask(title, status);
}

bool PluginContext::updateTask(const QString &id, const QVariantMap &fields)
{
    if (!require(QStringLiteral("tasks.write")))
        return false;
    return m_tasks->updateTask(id, fields);
}

bool PluginContext::moveTask(const QString &id, const QString &status)
{
    if (!require(QStringLiteral("tasks.write")))
        return false;
    return m_tasks->moveTask(id, status, -1);
}

bool PluginContext::moveTaskToEnd(const QString &id, const QString &status)
{
    if (!require(QStringLiteral("tasks.write")))
        return false;
    return m_tasks->moveTaskToEnd(id, status);
}

bool PluginContext::deleteTask(const QString &id)
{
    if (!require(QStringLiteral("tasks.write")))
        return false;
    return m_tasks->deleteTask(id);
}

void PluginContext::notify(const QString &title, const QString &body)
{
    if (!require(QStringLiteral("notifications")))
        return;
    m_tray->showMessage(title, body);
}

void PluginContext::log(const QString &message)
{
    qInfo().noquote() << "[plugin]" << m_pluginId << message;
}

bool PluginContext::openUrl(const QString &url)
{
    if (!require(QStringLiteral("openUrl")))
        return false;
    const QUrl target(url);
    if (!target.isValid() || (target.scheme() != QStringLiteral("http")
                              && target.scheme() != QStringLiteral("https")))
        return false;
    return QDesktopServices::openUrl(target);
}

QString PluginContext::metaValue(const QString &key, const QString &defaultValue) const
{
    if (!require(QStringLiteral("meta")))
        return defaultValue;
    return m_database->metaValue(key, defaultValue);
}

bool PluginContext::setMetaValue(const QString &key, const QString &value)
{
    if (!require(QStringLiteral("meta")))
        return false;
    return m_database->setMetaValue(key, value);
}

bool PluginContext::removeMetaValue(const QString &key)
{
    if (!require(QStringLiteral("meta")))
        return false;
    return m_database->removeMetaValue(key);
}

bool PluginContext::subscribe(const QStringList &events)
{
    if (!require(QStringLiteral("events")))
        return false;
    if (!m_bus)
        return false;
    m_bus->subscribe(this, events);
    return true;
}

QStringList PluginContext::subscriptions() const
{
    if (!m_bus)
        return {};
    return m_bus->subscriptions(this);
}

void PluginContext::deliverEvent(const QString &name, const QVariantMap &payload)
{
    emit eventReceived(name, payload);
}

void PluginContext::deliverCommand(const QString &commandId)
{
    if (commandId.isEmpty())
        return;
    emit commandInvoked(commandId);
}

void PluginContext::updateManifest(const QString &name, const QString &version,
                                   const QStringList &permissions)
{
    if (m_name == name && m_version == version && m_permissions == permissions)
        return;

    m_name = name;
    m_version = version;
    m_permissions = permissions;
    emit manifestChanged();
}
