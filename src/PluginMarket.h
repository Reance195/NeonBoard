#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * 插件市场：管理「多个插件目录源」，拉取索引、下载插件包。
 *
 * 源（source）= 一份插件目录（catalog），支持三种写法：
 *   1. 仓库简写  owner/repo              → 依次尝试 Gitee → GitHub 的 market.json
 *   2. 完整地址  https://host/path/market.json
 *   3. 本地目录  D:\market 或 file:///D:/market → 读 <目录>/market.json
 *
 * 多个源并列生效：同一个 id 的插件取版本高者，条目带上来源名（name@market）。
 * 每个源各自缓存（ETag / Last-Modified / 拉取时间），断网时各自降级到自己的缓存。
 *
 * 索引（market.json）格式：
 * {
 *   "version": 1,
 *   "updatedAt": "2026-10-21",
 *   "signature": "",            // 预留位：本轮不校验，以后接上不改协议
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
 */
class PluginMarket : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList sourceStatuses READ sourceStatuses NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(bool refreshing READ isRefreshing NOTIFY refreshingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool online READ isOnline NOTIFY onlineChanged)
    Q_PROPERTY(int cacheTtlSeconds READ cacheTtlSeconds WRITE setCacheTtlSeconds NOTIFY cacheTtlSecondsChanged)

public:
    explicit PluginMarket(QObject *parent = nullptr);
    ~PluginMarket() override;

    /** 内置默认源（仓库简写，设置里可改） */
    static QString defaultSource();
    /** 索引缓存文件路径 */
    static QString cachePath();

    /** 把源写法展开成候选地址列表（按优先级排列） */
    static QStringList resolveCandidates(const QString &spec);
    /** 源的展示名（owner/repo 取仓库名，URL 取 host + 末段文件名） */
    static QString sourceDisplayName(const QString &spec);

    /** 第一个启用的源（没有则第一个） */
    QString source() const;
    /** 替换第一个源；一个都没有时新增 */
    void setSource(const QString &spec);

    /** 源列表，元素为 {spec, name, enabled} */
    QVariantList sources() const { return m_sources; }
    void setSources(const QVariantList &sources);
    /** 每个源的刷新结果，元素为 {spec, name, enabled, ok, cached, message, pluginCount} */
    QVariantList sourceStatuses() const { return m_sourceStatuses; }

    QVariantList entries() const { return m_entries; }
    bool isRefreshing() const { return m_refreshing; }
    QString statusText() const { return m_statusText; }
    bool isOnline() const { return m_online; }
    int cacheTtlSeconds() const { return m_cacheTtlSeconds; }
    void setCacheTtlSeconds(int seconds);

    Q_INVOKABLE void refresh(bool force = false);
    Q_INVOKABLE QString defaultSourceUrl() const { return defaultSource(); }

    /** 源管理：spec 支持 owner/repo 简写、完整 URL、本地目录 */
    Q_INVOKABLE bool addSource(const QString &spec, const QString &name = QString());
    Q_INVOKABLE bool removeSource(int index);
    Q_INVOKABLE bool setSourceEnabled(int index, bool enabled);
    Q_INVOKABLE bool moveSource(int from, int to);

    /** 下载插件包到 destFile，成功时校验 sha256（sha256 为空则跳过校验） */
    Q_INVOKABLE void downloadPackage(const QString &url, const QString &sha256, const QString &destFile);

    // ---- 同步接口（命令行 / 无界面环境用）----
    /** 同步 GET，成功时把响应体写进 payload */
    bool fetchBlocking(const QString &url, QByteArray *payload, int timeoutMs, QString *error);
    /** 同步拉取并解析索引（失败时降级读缓存） */
    bool refreshBlocking(int timeoutMs, QString *error);
    /** 同上，force=true 时忽略缓存 TTL 强制回源 */
    bool refreshBlocking(int timeoutMs, QString *error, bool force);
    /** 同步下载插件包并校验 sha256 */
    bool downloadPackageBlocking(const QString &url, const QString &sha256, const QString &destFile, QString *error);
    /** 按 id 查索引条目 */
    QVariantMap entryById(const QString &id) const;

signals:
    void sourcesChanged();
    void entriesChanged();
    void refreshingChanged();
    void statusTextChanged();
    void onlineChanged();
    void cacheTtlSecondsChanged();
    void errorOccurred(const QString &message);
    void packageReady(const QString &destFile, bool ok, const QString &message);

private:
    struct SourceJob {
        QString spec;
        QString name;
        QStringList candidates;
        int candidateIndex = 0;
    };

    /** 一次 HTTP/file 拉取的结果 */
    struct FetchOutcome {
        bool ok = false;
        bool notModified = false; // 304：索引未变化
        int status = 0;
        QString etag;
        QString lastModified;
        QByteArray payload;
        QString error;
    };

    void setStatusText(const QString &text);
    void setRefreshing(bool value);
    void setOnline(bool value);

    /** 解析一份索引 JSON → 条目数组 */
    static QVariantList parseIndex(const QString &json);
    /** 合并多个源的条目：同 id 取版本高者 */
    static QVariantList mergeEntries(const QVariantList &entries);

    void buildJobs(bool force);
    void processNextSource();
    void finishRefresh();
    void recordSource(int jobIndex, const QString &url, const QString &json,
                      const QString &etag, const QString &lastModified, bool fromNetwork);
    void useCachedSource(int jobIndex, bool stale);
    void markSourceFailed(int jobIndex, const QString &message);
    QString cacheKey(const QString &spec) const;
    QVariantMap cachedSource(const QString &spec) const;
    void storeCachedSource(const QString &spec, const QString &url, const QString &json,
                           const QString &etag, const QString &lastModified);
    void writeCache() const;
    QVariantMap readCache() const;
    bool cacheFresh(const QVariantMap &record) const;

    /** 同步拉取一个地址；带条件请求头时服务端可能回 304（notModified） */
    FetchOutcome fetchUrl(const QString &url, const QString &conditionalEtag,
                          const QString &conditionalLastModified, int timeoutMs);

    QNetworkAccessManager *m_network = nullptr;
    QVariantList m_sources;
    QVariantList m_sourceStatuses;
    QVariantList m_entries;
    QString m_statusText;
    bool m_refreshing = false;
    bool m_online = false;
    int m_cacheTtlSeconds = 6 * 60 * 60;
    QList<SourceJob> m_jobs;
    int m_jobIndex = 0;
    QVariantMap m_sourceResults; // spec → {index, etag, lastModified, url, fetchedAt}
    QString m_pendingUrl;
    QString m_pendingSha;
    QString m_pendingDest;
};
