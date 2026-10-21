#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Database;
class PluginLoader;
class TaskModel;
class TrayController;

class PluginHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pluginId READ pluginId NOTIFY pluginIdChanged)
    Q_PROPERTY(QStringList permissions READ permissions NOTIFY pluginIdChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY pluginIdChanged)

public:
    explicit PluginHost(PluginLoader *loader, Database *database, TaskModel *tasks,
                        TrayController *tray, QObject *parent = nullptr);

    QString pluginId() const { return m_pluginId; }
    QStringList permissions() const { return m_permissions; }
    bool isActive() const { return !m_pluginId.isEmpty(); }

    Q_INVOKABLE bool activate(const QString &pluginId);
    Q_INVOKABLE void deactivate();
    Q_INVOKABLE bool hasPermission(const QString &permission) const;

    Q_INVOKABLE QString storageGet(const QString &key) const;
    Q_INVOKABLE bool storageSet(const QString &key, const QString &value);
    Q_INVOKABLE bool storageRemove(const QString &key);

    Q_INVOKABLE QVariantList tasks() const;
    Q_INVOKABLE QString addTask(const QString &title,
                                const QString &status = QStringLiteral("todo"));
    Q_INVOKABLE bool updateTask(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool moveTask(const QString &id, const QString &status);
    Q_INVOKABLE bool deleteTask(const QString &id);

    Q_INVOKABLE void notify(const QString &title, const QString &body);
    Q_INVOKABLE void log(const QString &message);
    Q_INVOKABLE bool openUrl(const QString &url);

signals:
    void pluginIdChanged();

private:
    bool require(const QString &permission) const;

    PluginLoader *m_loader = nullptr;
    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    TrayController *m_tray = nullptr;
    QString m_pluginId;
    QStringList m_permissions;
};
