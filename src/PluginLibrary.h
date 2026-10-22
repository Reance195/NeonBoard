#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class Database;
class PluginLoader;
class PluginMarket;
class PluginRuntime;

/**
 * 插件库：把「本地已装插件」「随包/官方/示例源里的插件」「市场上的插件」
 * 汇总成一份给界面用的清单，并提供安装、卸载、启用、禁用、更新等操作。
 *
 * 安装只允许写用户安装区（%APPDATA%\com.lin\NeonBoard\plugins），
 * 官方源与示例源一律只读，卸载也只删用户安装区里的副本。
 */
class PluginLibrary : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QString marketSource READ marketSource WRITE setMarketSource NOTIFY marketSourceChanged)
    Q_PROPERTY(QString marketStatus READ marketStatus NOTIFY entriesChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    PluginLibrary(PluginLoader *loader,
                  PluginMarket *market,
                  PluginRuntime *runtime,
                  Database *database,
                  QObject *parent = nullptr);

    QVariantList entries() const { return m_entries; }
    QString marketSource() const;
    void setMarketSource(const QString &url);
    QString marketStatus() const;
    QString statusText() const { return m_statusText; }

    Q_INVOKABLE void refresh();
    // 注意：QML 调用时不能带 QString* 出参（QML 传不了指针，少传参数会报
    // "Insufficient arguments"），失败原因统一走 statusText + operationFinished 信号。
    Q_INVOKABLE bool install(const QString &pluginId);
    Q_INVOKABLE bool uninstall(const QString &pluginId);
    Q_INVOKABLE bool setEnabled(const QString &pluginId, bool enabled);
    Q_INVOKABLE bool update(const QString &pluginId);
    Q_INVOKABLE bool openPluginDir();
    Q_INVOKABLE void requestOpen(const QString &pluginId);

    // ---- 同步接口（命令行 / 无界面环境用）----
    /** 同步拉取市场索引 */
    bool refreshMarketBlocking(int timeoutMs, QString *error);
    /** 同步安装插件（市场包或本地源） */
    bool installBlocking(const QString &pluginId, QString *error);

signals:
    void entriesChanged();
    void marketSourceChanged();
    void statusTextChanged();
    void operationFinished(const QString &pluginId, const QString &action, bool ok, const QString &message);
    void openRequested(const QString &pluginId);

private:
    void rebuild();
    void setStatusText(const QString &text);
    bool reportFailure(const QString &pluginId, const QString &action, const QString &message);
    QString localSourceDir(const QString &pluginId) const;
    QString marketDownloadUrl(const QString &pluginId) const;
    QString marketSha256(const QString &pluginId) const;
    QString marketVersion(const QString &pluginId) const;
    bool installFromMarket(const QString &pluginId, QString *error);
    bool installFromMarketBlocking(const QString &pluginId, QString *error);
    bool installFromLocalSource(const QString &pluginId, QString *error);
    static bool versionGreaterThan(const QString &left, const QString &right);

    PluginLoader *m_loader = nullptr;
    PluginMarket *m_market = nullptr;
    PluginRuntime *m_runtime = nullptr;
    Database *m_database = nullptr;
    QVariantList m_entries;
    QString m_statusText;
    bool m_refreshing = false;
};
