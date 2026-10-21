#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * 插件市场：负责「联网」这一件事 —— 拉取索引、下载插件包。
 *
 * 索引（market.json）格式：
 * {
 *   "version": 1,
 *   "updatedAt": "2026-10-21",
 *   "plugins": [
 *     {
 *       "id": "com.lin.plan-hook",
 *       "name": "计划钩子",
 *       "version": "1.0.0",
 *       "description": "提交信息里的 #短号 自动流转任务状态",
 *       "author": "NeonBoard",
 *       "homepage": "https://github.com/...",
 *       "download": "https://raw.githubusercontent.com/.../plan-hook.neon",
 *       "sha256": "....",
 *       "size": 12345
 *     }
 *   ]
 * }
 *
 * 拉取失败时降级使用本地缓存索引（离线可用）。
 */
class PluginMarket : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(bool refreshing READ isRefreshing NOTIFY refreshingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool online READ isOnline NOTIFY onlineChanged)

public:
    explicit PluginMarket(QObject *parent = nullptr);
    ~PluginMarket() override;

    /** 内置默认市场地址（设置里可改） */
    static QString defaultSource();
    /** 索引缓存文件路径 */
    static QString cachePath();

    QString source() const { return m_source; }
    void setSource(const QString &url);

    QVariantList entries() const { return m_entries; }
    bool isRefreshing() const { return m_refreshing; }
    QString statusText() const { return m_statusText; }
    bool isOnline() const { return m_online; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString defaultSourceUrl() const { return defaultSource(); }

    /** 下载插件包到 destFile，成功时校验 sha256（sha256 为空则跳过校验） */
    Q_INVOKABLE void downloadPackage(const QString &url, const QString &sha256, const QString &destFile);

    // ---- 同步接口（命令行 / 无界面环境用）----
    /** 同步 GET，成功时把响应体写进 payload */
    bool fetchBlocking(const QString &url, QByteArray *payload, int timeoutMs, QString *error);
    /** 同步拉取并解析索引（失败时降级读缓存） */
    bool refreshBlocking(int timeoutMs, QString *error);
    /** 同步下载插件包并校验 sha256 */
    bool downloadPackageBlocking(const QString &url, const QString &sha256, const QString &destFile, QString *error);
    /** 按 id 查索引条目 */
    QVariantMap entryById(const QString &id) const;

signals:
    void sourceChanged();
    void entriesChanged();
    void refreshingChanged();
    void statusTextChanged();
    void onlineChanged();
    void errorOccurred(const QString &message);
    void packageReady(const QString &destFile, bool ok, const QString &message);

private:
    void setStatusText(const QString &text);
    void setRefreshing(bool value);
    void setOnline(bool value);
    void applyIndex(const QString &json, bool fromNetwork);
    bool writeCache(const QString &json) const;
    QString readCache() const;

    QNetworkAccessManager *m_network = nullptr;
    QTimer *m_timer = nullptr;
    QString m_source;
    QVariantList m_entries;
    QString m_statusText;
    bool m_refreshing = false;
    bool m_online = false;
    QString m_pendingUrl;
    QString m_pendingSha;
    QString m_pendingDest;
};
