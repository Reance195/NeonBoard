#pragma once

#include "PluginHost.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class Database;
class PluginContext;
class PluginEventBus;
class PluginLoader;
class QQmlApplicationEngine;
class QQmlComponent;
class QQmlContext;
class TaskModel;
class TrayController;

class PluginRuntime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int backgroundCount READ backgroundCount NOTIFY backgroundCountChanged)
    Q_PROPERTY(QStringList backgroundPlugins READ backgroundPlugins NOTIFY backgroundCountChanged)
    Q_PROPERTY(QVariantList pluginCommands READ pluginCommands NOTIFY commandsChanged)
    Q_PROPERTY(QVariantList settingsPages READ settingsPages NOTIFY surfacesChanged)
    Q_PROPERTY(QVariantList sidebarSlotItems READ sidebarSlotItems NOTIFY surfacesChanged)
    Q_PROPERTY(QVariantList bottomSlotItems READ bottomSlotItems NOTIFY surfacesChanged)
    Q_PROPERTY(QVariantList cardSlotItems READ cardSlotItems NOTIFY surfacesChanged)

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

    // ---------- R3 插件平台 2.0：表面（命令 / 设置页 / 槽位） ----------
    QVariantList pluginCommands() const;
    QVariantList settingsPages() const;
    QVariantList sidebarSlotItems() const { return slotItems(QStringLiteral("sidebar")); }
    QVariantList bottomSlotItems() const { return slotItems(QStringLiteral("bottom")); }
    QVariantList cardSlotItems() const { return slotItems(QStringLiteral("card")); }
    Q_INVOKABLE QVariantList slotItems(const QString &slot) const;

    /** 绑定到固定插件 id 的门面（设置页 / 槽位页注入用），按 id 缓存 */
    Q_INVOKABLE PluginHost *hostFor(const QString &pluginId);
    /** 调用插件命令；插件已在运行则直接投递返回 true，否则排队并等面板打开后补投递 */
    Q_INVOKABLE bool invokeCommand(const QString &pluginId, const QString &commandId);
    /** 投递并清空某插件排队中的命令（面板 / 后台加载完成后调用） */
    Q_INVOKABLE int dispatchPending(const QString &pluginId);
    Q_INVOKABLE bool isRunning(const QString &pluginId) const;

    // ---------- R3 权限授予 ----------
    Q_INVOKABLE QStringList missingPermissions(const QString &pluginId) const;
    Q_INVOKABLE bool isPermissionGranted(const QString &pluginId,
                                         const QString &permission) const;
    Q_INVOKABLE bool grantPermissions(const QString &pluginId, const QStringList &permissions);
    Q_INVOKABLE bool setPermissionGranted(const QString &pluginId, const QString &permission,
                                          bool granted);
    /** 授权弹窗数据：插件名 / 来源 / 待授权权限清单（含中文说明与风险级别） */
    Q_INVOKABLE QVariantMap permissionPrompt(const QString &pluginId) const;
    Q_INVOKABLE QVariantMap permissionCatalog() const;

signals:
    void backgroundCountChanged();
    void commandsChanged();
    void surfacesChanged();
    void permissionRequested(const QString &pluginId, const QString &permission);
    void commandPending(const QString &pluginId, const QString &commandId);
    void commandDelivered(const QString &pluginId, const QString &commandId);

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
    void ensureGrantsBootstrapped() const;
    void persistGrants() const;
    QStringList grantedPermissions(const QString &pluginId) const;
    bool contextRunning(const QString &pluginId) const;
    bool pluginDeclaresCommand(const QString &pluginId, const QString &commandId) const;

    PluginLoader *m_loader = nullptr;
    PluginEventBus *m_bus = nullptr;
    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    TrayController *m_tray = nullptr;
    QQmlApplicationEngine *m_engine = nullptr;

    QHash<QString, PluginContext *> m_contexts;
    QList<BackgroundEntry> m_background;
    PluginContext *m_activeContext = nullptr;

    QHash<QString, PluginHost *> m_hosts;
    QList<QPair<QString, QString>> m_pendingCommands;
    QSet<QString> m_promptedPermissions;
    mutable QHash<QString, QStringList> m_grants;
    mutable bool m_grantsReady = false;
};
