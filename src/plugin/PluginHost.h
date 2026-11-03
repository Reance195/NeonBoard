#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class PluginContext;
class PluginLoader;
class PluginRuntime;

class PluginHost : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pluginId READ pluginId NOTIFY pluginIdChanged)
    Q_PROPERTY(QStringList permissions READ permissions NOTIFY pluginIdChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY pluginIdChanged)

public:
    explicit PluginHost(PluginRuntime *runtime, const QString &boundPluginId = QString(),
                        QObject *parent = nullptr);

    QString pluginId() const;
    QStringList permissions() const;
    bool isActive() const;
    QString boundPluginId() const;

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

signals:
    void pluginIdChanged();
    void commandInvoked(const QString &commandId);

private:
    PluginContext *active() const;

    PluginRuntime *m_runtime = nullptr;
    QString m_boundId;
};
