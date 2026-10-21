#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class Database;
class PluginEventBus;
class TaskModel;
class TrayController;

class PluginContext : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY manifestChanged)
    Q_PROPERTY(QString version READ version NOTIFY manifestChanged)
    Q_PROPERTY(QStringList permissions READ permissions NOTIFY manifestChanged)
    Q_PROPERTY(QUrl baseUrl READ baseUrl CONSTANT)
    Q_PROPERTY(bool background READ isBackground CONSTANT)

public:
    PluginContext(const QString &pluginId,
                  const QString &name,
                  const QString &version,
                  const QStringList &permissions,
                  const QUrl &baseUrl,
                  bool background,
                  Database *database,
                  TaskModel *tasks,
                  TrayController *tray,
                  PluginEventBus *bus,
                  QObject *parent = nullptr);

    QString pluginId() const { return m_pluginId; }
    QString name() const { return m_name; }
    QString version() const { return m_version; }
    QStringList permissions() const { return m_permissions; }
    QUrl baseUrl() const { return m_baseUrl; }
    bool isBackground() const { return m_background; }

    Q_INVOKABLE bool hasPermission(const QString &permission) const;

    Q_INVOKABLE QString storageGet(const QString &key) const;
    Q_INVOKABLE bool storageSet(const QString &key, const QString &value);
    Q_INVOKABLE bool storageRemove(const QString &key);

    Q_INVOKABLE QVariantList tasks() const;
    Q_INVOKABLE QString addTask(const QString &title,
                                const QString &status = QStringLiteral("todo"));
    Q_INVOKABLE bool updateTask(const QString &id, const QVariantMap &fields);
    Q_INVOKABLE bool moveTask(const QString &id, const QString &status);
    Q_INVOKABLE bool moveTaskToEnd(const QString &id, const QString &status);
    Q_INVOKABLE bool deleteTask(const QString &id);

    Q_INVOKABLE void notify(const QString &title, const QString &body);
    Q_INVOKABLE void log(const QString &message);
    Q_INVOKABLE bool openUrl(const QString &url);

    Q_INVOKABLE QString metaValue(const QString &key,
                                  const QString &defaultValue = QString()) const;
    Q_INVOKABLE bool setMetaValue(const QString &key, const QString &value);
    Q_INVOKABLE bool removeMetaValue(const QString &key);

    Q_INVOKABLE bool subscribe(const QStringList &events);
    Q_INVOKABLE QStringList subscriptions() const;

    void deliverEvent(const QString &name, const QVariantMap &payload);
    void updateManifest(const QString &name, const QString &version,
                        const QStringList &permissions);

signals:
    void eventReceived(const QString &name, const QVariantMap &payload);
    void manifestChanged();

private:
    bool require(const QString &permission) const;

    QString m_pluginId;
    QString m_name;
    QString m_version;
    QStringList m_permissions;
    QUrl m_baseUrl;
    bool m_background = false;

    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    TrayController *m_tray = nullptr;
    PluginEventBus *m_bus = nullptr;
};
