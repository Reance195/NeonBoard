#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

class Database;
class PluginContext;
class PluginEventBus;
class PluginLoader;
class QFileSystemWatcher;
class QQmlApplicationEngine;
class QQmlComponent;
class QQmlContext;
class QTimer;
class TaskModel;
class TrayController;

class PluginRuntime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int backgroundCount READ backgroundCount NOTIFY backgroundCountChanged)
    Q_PROPERTY(QStringList backgroundPlugins READ backgroundPlugins NOTIFY backgroundCountChanged)

public:
    PluginRuntime(PluginLoader *loader, PluginEventBus *bus, Database *database,
                  TaskModel *tasks, TrayController *tray, QObject *parent = nullptr);

    void attachEngine(QQmlApplicationEngine *engine);

    Q_INVOKABLE void start();
    Q_INVOKABLE void reload();

    PluginContext *contextFor(const QString &pluginId);
    PluginContext *activeContext() const { return m_activeContext; }
    void setActiveContext(PluginContext *context);

    int backgroundCount() const { return m_background.size(); }
    QStringList backgroundPlugins() const;

signals:
    void backgroundCountChanged();

private:
    struct BackgroundEntry {
        QString pluginId;
        QQmlContext *context = nullptr;
        QQmlComponent *component = nullptr;
        QObject *object = nullptr;
    };

    void loadBackgroundPlugins();
    void unloadBackgroundPlugins();
    void connectTaskEvents();
    void refreshContexts();

    void ensureWatcher();
    void refreshWatchPaths();
    void onWatchTriggered();
    void reloadFromDisk();
    QString computeFingerprint() const;

    PluginLoader *m_loader = nullptr;
    PluginEventBus *m_bus = nullptr;
    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    TrayController *m_tray = nullptr;
    QQmlApplicationEngine *m_engine = nullptr;

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_reloadDebounce = nullptr;
    QString m_fingerprint;

    QHash<QString, PluginContext *> m_contexts;
    QList<BackgroundEntry> m_background;
    PluginContext *m_activeContext = nullptr;
};
