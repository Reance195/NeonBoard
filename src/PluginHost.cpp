#include "PluginHost.h"

#include "Database.h"
#include "PluginLoader.h"
#include "TaskModel.h"
#include "TrayController.h"

#include <QDesktopServices>
#include <QDebug>
#include <QUrl>

PluginHost::PluginHost(PluginLoader *loader, Database *database, TaskModel *tasks,
                       TrayController *tray, QObject *parent)
    : QObject(parent)
    , m_loader(loader)
    , m_database(database)
    , m_tasks(tasks)
    , m_tray(tray)
{
}

bool PluginHost::activate(const QString &pluginId)
{
    if (pluginId.isEmpty()) {
        deactivate();
        return true;
    }

    const QVariantMap plugin = m_loader->pluginById(pluginId);
    if (plugin.isEmpty() || plugin.value(QStringLiteral("status")).toString() != QStringLiteral("ok"))
        return false;

    m_pluginId = pluginId;
    m_permissions.clear();
    const QVariant permissions = plugin.value(QStringLiteral("permissions"));
    if (permissions.canConvert<QStringList>())
        m_permissions = permissions.toStringList();
    else
        for (const QVariant &permission : permissions.toList())
            m_permissions.append(permission.toString());
    emit pluginIdChanged();
    return true;
}

void PluginHost::deactivate()
{
    if (m_pluginId.isEmpty())
        return;
    m_pluginId.clear();
    m_permissions.clear();
    emit pluginIdChanged();
}

bool PluginHost::hasPermission(const QString &permission) const
{
    return !permission.isEmpty() && m_permissions.contains(permission);
}

bool PluginHost::require(const QString &permission) const
{
    if (hasPermission(permission))
        return true;
    qWarning() << "PluginHost: permission denied" << permission << "for" << m_pluginId;
    return false;
}

QString PluginHost::storageGet(const QString &key) const
{
    if (m_pluginId.isEmpty() || !require(QStringLiteral("storage")))
        return {};
    return m_database->pluginValue(m_pluginId, key);
}

bool PluginHost::storageSet(const QString &key, const QString &value)
{
    if (m_pluginId.isEmpty() || !require(QStringLiteral("storage")))
        return false;
    return m_database->setPluginValue(m_pluginId, key, value);
}

bool PluginHost::storageRemove(const QString &key)
{
    if (m_pluginId.isEmpty() || !require(QStringLiteral("storage")))
        return false;
    return m_database->removePluginValue(m_pluginId, key);
}

QVariantList PluginHost::tasks() const
{
    if (!require(QStringLiteral("tasks.read")))
        return {};
    return m_tasks->allTasks();
}

QString PluginHost::addTask(const QString &title, const QString &status)
{
    if (!require(QStringLiteral("tasks.write")))
        return {};
    return m_tasks->addTask(title, status);
}

bool PluginHost::updateTask(const QString &id, const QVariantMap &fields)
{
    if (!require(QStringLiteral("tasks.write")))
        return false;
    return m_tasks->updateTask(id, fields);
}

bool PluginHost::moveTask(const QString &id, const QString &status)
{
    if (!require(QStringLiteral("tasks.write")))
        return false;
    return m_tasks->moveTask(id, status, -1);
}

bool PluginHost::deleteTask(const QString &id)
{
    if (!require(QStringLiteral("tasks.write")))
        return false;
    return m_tasks->deleteTask(id);
}

void PluginHost::notify(const QString &title, const QString &body)
{
    if (!require(QStringLiteral("notifications")))
        return;
    m_tray->showMessage(title, body);
}

void PluginHost::log(const QString &message)
{
    qInfo().noquote() << "[plugin]" << m_pluginId << message;
}

bool PluginHost::openUrl(const QString &url)
{
    if (!require(QStringLiteral("openUrl")))
        return false;
    const QUrl target(url);
    if (!target.isValid() || (target.scheme() != QStringLiteral("http")
                              && target.scheme() != QStringLiteral("https")))
        return false;
    return QDesktopServices::openUrl(target);
}
