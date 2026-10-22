#include "PluginMarket.h"

#include "AppPaths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int kIndexTimeoutMs = 10000;
constexpr int kDownloadTimeoutMs = 30000;
constexpr auto kCacheVersion = 2;

bool versionGreaterThan(const QString &left, const QString &right)
{
    const QStringList a = left.split(QLatin1Char('.'));
    const QStringList b = right.split(QLatin1Char('.'));
    const int count = qMax(a.size(), b.size());
    for (int i = 0; i < count; ++i) {
        const int x = i < a.size() ? a.at(i).toInt() : 0;
        const int y = i < b.size() ? b.at(i).toInt() : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

QVariantMap sourceRecord(const QString &spec, const QString &name, bool enabled)
{
    QVariantMap record;
    record.insert(QStringLiteral("spec"), spec);
    record.insert(QStringLiteral("name"), name.isEmpty() ? PluginMarket::sourceDisplayName(spec) : name);
    record.insert(QStringLiteral("enabled"), enabled);
    return record;
}

QVariantMap sourceStatus(const QString &spec, const QString &name, bool ok, bool cached,
                          const QString &message)
{
    QVariantMap status;
    status.insert(QStringLiteral("spec"), spec);
    status.insert(QStringLiteral("name"), name);
    status.insert(QStringLiteral("enabled"), true);
    status.insert(QStringLiteral("ok"), ok);
    status.insert(QStringLiteral("cached"), cached);
    status.insert(QStringLiteral("message"), message);
    return status;
}

QVariantList annotateEntries(const QVariantList &list, const QString &name, const QString &spec,
                              const QString &url)
{
    QVariantList result;
    result.reserve(list.size());
    for (const QVariant &value : list) {
        QVariantMap entry = value.toMap();
        entry.insert(QStringLiteral("marketSource"), name);
        entry.insert(QStringLiteral("marketSpec"), spec);
        entry.insert(QStringLiteral("marketUrl"), url);
        result.append(entry);
    }
    return result;
}

} // namespace

PluginMarket::PluginMarket(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_sources.append(sourceRecord(defaultSource(), QString(), true));
}

PluginMarket::~PluginMarket() = default;

QString PluginMarket::defaultSource()
{
    // 官方源：仓库简写，客户端按 Gitee → GitHub 顺序尝试
    return QStringLiteral("Reance195/neonboard-market");
}

QString PluginMarket::cachePath()
{
    return AppPaths::dataDir() + QStringLiteral("/market-cache.json");
}

QStringList PluginMarket::resolveCandidates(const QString &spec)
{
    const QString text = spec.trimmed();
    if (text.isEmpty())
        return {};

    static const QRegularExpression repoPattern(
        QStringLiteral(R"(^([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)$)"));
    const QRegularExpressionMatch match = repoPattern.match(text);
    if (match.hasMatch()) {
        const QString owner = match.captured(1);
        const QString repo = match.captured(2);
        return {QStringLiteral("https://gitee.com/%1/%2/raw/main/market.json").arg(owner, repo),
                QStringLiteral("https://raw.githubusercontent.com/%1/%2/main/market.json").arg(owner, repo)};
    }

    if (text.startsWith(QStringLiteral("http://")) || text.startsWith(QStringLiteral("https://"))
        || text.startsWith(QStringLiteral("file://")))
        return {text};

    QFileInfo info(text);
    if (info.isDir())
        return {QUrl::fromLocalFile(info.filePath() + QStringLiteral("/market.json")).toString()};
    return {QUrl::fromLocalFile(info.filePath()).toString()};
}

QString PluginMarket::sourceDisplayName(const QString &spec)
{
    const QString text = spec.trimmed();
    if (text.isEmpty())
        return QString();

    static const QRegularExpression repoPattern(
        QStringLiteral(R"(^([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)$)"));
    const QRegularExpressionMatch match = repoPattern.match(text);
    if (match.hasMatch())
        return match.captured(2);

    if (text.startsWith(QStringLiteral("http://")) || text.startsWith(QStringLiteral("https://"))) {
        const QUrl url(text);
        QString last = url.path();
        const int slash = last.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0)
            last = last.mid(slash + 1);
        if (last.endsWith(QStringLiteral(".json")))
            last.chop(5);
        const QString host = url.host();
        return host.isEmpty() ? last : QStringLiteral("%1/%2").arg(host, last.isEmpty() ? host : last);
    }

    if (text.startsWith(QStringLiteral("file://"))) {
        const QString path = QUrl(text).toLocalFile();
        const QString dir = QFileInfo(path).absolutePath();
        return QFileInfo(dir).fileName();
    }

    const QFileInfo info(text);
    if (info.isDir())
        return info.fileName();
    return info.fileName();
}

QString PluginMarket::source() const
{
    for (const QVariant &value : m_sources) {
        const QVariantMap record = value.toMap();
        if (record.value(QStringLiteral("enabled"), true).toBool())
            return record.value(QStringLiteral("spec")).toString();
    }
    if (!m_sources.isEmpty())
        return m_sources.first().toMap().value(QStringLiteral("spec")).toString();
    return QString();
}

void PluginMarket::setSource(const QString &spec)
{
    const QString text = spec.trimmed();
    if (text.isEmpty())
        return;
    if (m_sources.isEmpty()) {
        m_sources.append(sourceRecord(text, QString(), true));
    } else {
        QVariantMap first = m_sources.first().toMap();
        first.insert(QStringLiteral("spec"), text);
        first.insert(QStringLiteral("name"), PluginMarket::sourceDisplayName(text));
        m_sources[0] = first;
    }
    emit sourcesChanged();
}

void PluginMarket::setSources(const QVariantList &sources)
{
    QVariantList normalized;
    for (const QVariant &value : sources) {
        const QVariantMap record = value.toMap();
        const QString spec = record.value(QStringLiteral("spec")).toString().trimmed();
        if (spec.isEmpty())
            continue;
        normalized.append(sourceRecord(spec, record.value(QStringLiteral("name")).toString(),
                                       record.value(QStringLiteral("enabled"), true).toBool()));
    }
    if (normalized.isEmpty())
        return;
    m_sources = normalized;
    emit sourcesChanged();
}

void PluginMarket::setCacheTtlSeconds(int seconds)
{
    m_cacheTtlSeconds = qMax(0, seconds);
    emit cacheTtlSecondsChanged();
}

bool PluginMarket::addSource(const QString &spec, const QString &name)
{
    const QString text = spec.trimmed();
    if (text.isEmpty()) {
        setStatusText(QStringLiteral("市场源不能为空"));
        return false;
    }
    for (const QVariant &value : m_sources) {
        if (value.toMap().value(QStringLiteral("spec")).toString() == text) {
            setStatusText(QStringLiteral("该市场源已存在"));
            return false;
        }
    }
    m_sources.append(sourceRecord(text, name.trimmed(), true));
    emit sourcesChanged();
    setStatusText(QStringLiteral("已添加市场源：%1").arg(text));
    return true;
}

bool PluginMarket::removeSource(int index)
{
    if (index < 0 || index >= m_sources.size())
        return false;
    m_sources.removeAt(index);
    emit sourcesChanged();
    return true;
}

bool PluginMarket::setSourceEnabled(int index, bool enabled)
{
    if (index < 0 || index >= m_sources.size())
        return false;
    QVariantMap record = m_sources.at(index).toMap();
    record.insert(QStringLiteral("enabled"), enabled);
    m_sources[index] = record;
    emit sourcesChanged();
    return true;
}

bool PluginMarket::moveSource(int from, int to)
{
    if (from < 0 || from >= m_sources.size() || to < 0 || to >= m_sources.size() || from == to)
        return false;
    m_sources.move(from, to);
    emit sourcesChanged();
    return true;
}

void PluginMarket::refresh(bool force)
{
    if (m_refreshing)
        return;
    if (m_sources.isEmpty()) {
        setStatusText(QStringLiteral("未配置市场地址"));
        return;
    }
    setRefreshing(true);
    buildJobs(force);
    processNextSource();
}

void PluginMarket::buildJobs(bool force)
{
    m_jobs.clear();
    m_jobIndex = 0;
    m_sourceResults.clear();
    m_sourceStatuses.clear();

    for (const QVariant &value : m_sources) {
        const QVariantMap record = value.toMap();
        if (!record.value(QStringLiteral("enabled"), true).toBool())
            continue;
        SourceJob job;
        job.spec = record.value(QStringLiteral("spec")).toString();
        job.name = record.value(QStringLiteral("name")).toString();
        job.candidates = resolveCandidates(job.spec);
        if (job.candidates.isEmpty())
            continue;
        if (!force && cacheFresh(cachedSource(job.spec))) {
            // TTL 内且缓存新鲜 → 直接用缓存，不发请求
            m_sourceResults.insert(job.spec, cachedSource(job.spec));
            QVariantMap status;
            status.insert(QStringLiteral("spec"), job.spec);
            status.insert(QStringLiteral("name"), job.name);
            status.insert(QStringLiteral("enabled"), true);
            status.insert(QStringLiteral("ok"), true);
            status.insert(QStringLiteral("cached"), true);
            status.insert(QStringLiteral("message"), QStringLiteral("使用本地缓存"));
            m_sourceStatuses.append(status);
            continue;
        }
        m_jobs.append(job);
    }
}

void PluginMarket::processNextSource()
{
    if (m_jobIndex >= m_jobs.size()) {
        finishRefresh();
        return;
    }
    SourceJob &job = m_jobs[m_jobIndex];
    if (job.candidateIndex >= job.candidates.size()) {
        // 该源所有候选都失败 → 降级到缓存
        useCachedSource(m_jobIndex, true);
        ++m_jobIndex;
        processNextSource();
        return;
    }

    const QString url = job.candidates.at(job.candidateIndex);
    if (url.startsWith(QStringLiteral("file://"))) {
        QFile file(QUrl(url).toLocalFile());
        if (file.open(QIODevice::ReadOnly)) {
            const QString json = QString::fromUtf8(file.readAll());
            file.close();
            recordSource(m_jobIndex, url, json, QString(), QString(), true);
            ++m_jobIndex;
            processNextSource();
            return;
        }
        ++job.candidateIndex;
        processNextSource();
        return;
    }

    const QString spec = job.spec;
    const int jobIndex = m_jobIndex;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NeonBoard-PluginMarket"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    const QVariantMap cached = cachedSource(spec);
    const QString etag = cached.value(QStringLiteral("etag")).toString();
    const QString lastModified = cached.value(QStringLiteral("lastModified")).toString();
    if (!etag.isEmpty())
        request.setRawHeader(QByteArrayLiteral("If-None-Match"), etag.toUtf8());
    if (!lastModified.isEmpty())
        request.setRawHeader(QByteArrayLiteral("If-Modified-Since"), lastModified.toUtf8());

    QNetworkReply *reply = m_network->get(request);
    QTimer *guard = new QTimer(reply);
    guard->setSingleShot(true);
    guard->setInterval(kIndexTimeoutMs);
    connect(guard, &QTimer::timeout, reply, &QNetworkReply::abort);
    guard->start();
    connect(reply, &QNetworkReply::finished, this, [this, reply, jobIndex]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray etagHeader = reply->rawHeader("ETag");
        const QByteArray modifiedHeader = reply->rawHeader("Last-Modified");

        if (status == 304) {
            useCachedSource(jobIndex, false);
            ++m_jobIndex;
            processNextSource();
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            SourceJob &failed = m_jobs[jobIndex];
            ++failed.candidateIndex;
            processNextSource();
            return;
        }
        const QString json = QString::fromUtf8(reply->readAll());
        if (json.trimmed().isEmpty()) {
            SourceJob &failed = m_jobs[jobIndex];
            ++failed.candidateIndex;
            processNextSource();
            return;
        }
        recordSource(jobIndex, reply->url().toString(), json,
                     QString::fromUtf8(etagHeader), QString::fromUtf8(modifiedHeader), true);
        ++m_jobIndex;
        processNextSource();
    });
}

void PluginMarket::recordSource(int jobIndex, const QString &url, const QString &json,
                                const QString &etag, const QString &lastModified, bool fromNetwork)
{
    if (jobIndex < 0 || jobIndex >= m_jobs.size())
        return;
    const SourceJob &job = m_jobs.at(jobIndex);
    QVariantMap record;
    record.insert(QStringLiteral("url"), url);
    record.insert(QStringLiteral("index"), json);
    record.insert(QStringLiteral("etag"), etag);
    record.insert(QStringLiteral("lastModified"), lastModified);
    record.insert(QStringLiteral("fetchedAt"),
                  QDateTime::currentSecsSinceEpoch());
    record.insert(QStringLiteral("fromNetwork"), fromNetwork);
    m_sourceResults.insert(job.spec, record);
    storeCachedSource(job.spec, url, json, etag, lastModified);
}

void PluginMarket::useCachedSource(int jobIndex, bool stale)
{
    if (jobIndex < 0 || jobIndex >= m_jobs.size())
        return;
    const SourceJob &job = m_jobs.at(jobIndex);
    const QVariantMap cached = cachedSource(job.spec);
    if (cached.isEmpty()) {
        markSourceFailed(jobIndex, QStringLiteral("拉取失败且无本地缓存"));
        return;
    }
    m_sourceResults.insert(job.spec, cached);
    QVariantMap status;
    status.insert(QStringLiteral("spec"), job.spec);
    status.insert(QStringLiteral("name"), job.name);
    status.insert(QStringLiteral("enabled"), true);
    status.insert(QStringLiteral("ok"), true);
    status.insert(QStringLiteral("cached"), true);
    status.insert(QStringLiteral("message"),
                  stale ? QStringLiteral("拉取失败，使用本地缓存") : QStringLiteral("索引未变化（304）"));
    m_sourceStatuses.append(status);
}

void PluginMarket::markSourceFailed(int jobIndex, const QString &message)
{
    if (jobIndex < 0 || jobIndex >= m_jobs.size())
        return;
    const SourceJob &job = m_jobs.at(jobIndex);
    QVariantMap status;
    status.insert(QStringLiteral("spec"), job.spec);
    status.insert(QStringLiteral("name"), job.name);
    status.insert(QStringLiteral("enabled"), true);
    status.insert(QStringLiteral("ok"), false);
    status.insert(QStringLiteral("cached"), false);
    status.insert(QStringLiteral("message"), message);
    m_sourceStatuses.append(status);
}

void PluginMarket::finishRefresh()
{
    QVariantList merged;
    int okCount = 0;
    int totalCount = m_sources.size();
    for (const QVariant &value : m_sources) {
        const QVariantMap record = value.toMap();
        if (!record.value(QStringLiteral("enabled"), true).toBool())
            continue;
        const QString spec = record.value(QStringLiteral("spec")).toString();
        const QVariantMap result = m_sourceResults.value(spec).toMap();
        if (result.isEmpty())
            continue;
        const QString json = result.value(QStringLiteral("index")).toString();
        QVariantList list = parseIndex(json);
        const QString sourceName = record.value(QStringLiteral("name")).toString();
        for (QVariant &item : list) {
            QVariantMap entry = item.toMap();
            entry.insert(QStringLiteral("marketSource"), sourceName);
            entry.insert(QStringLiteral("marketSpec"), spec);
            entry.insert(QStringLiteral("marketUrl"), result.value(QStringLiteral("url")).toString());
            item = entry;
        }
        merged = mergeEntries(merged + list);
        ++okCount;
    }

    // 每个成功源补一条状态
    for (const QVariant &value : m_sources) {
        const QVariantMap record = value.toMap();
        if (!record.value(QStringLiteral("enabled"), true).toBool())
            continue;
        const QString spec = record.value(QStringLiteral("spec")).toString();
        bool recorded = false;
        for (const QVariant &statusValue : m_sourceStatuses) {
            if (statusValue.toMap().value(QStringLiteral("spec")).toString() == spec) {
                recorded = true;
                break;
            }
        }
        if (recorded)
            continue;
        const QVariantMap result = m_sourceResults.value(spec).toMap();
        QVariantMap status;
        status.insert(QStringLiteral("spec"), spec);
        status.insert(QStringLiteral("name"), record.value(QStringLiteral("name")).toString());
        status.insert(QStringLiteral("enabled"), true);
        status.insert(QStringLiteral("ok"), !result.isEmpty());
        status.insert(QStringLiteral("cached"), false);
        status.insert(QStringLiteral("message"),
                      result.isEmpty() ? QStringLiteral("拉取失败且无本地缓存")
                                       : QStringLiteral("已更新"));
        m_sourceStatuses.append(status);
    }

    m_entries = merged;
    writeCache();
    setOnline(okCount > 0);
    emit entriesChanged();
    emit sourcesChanged();

    if (okCount == 0 && totalCount > 0) {
        setStatusText(QStringLiteral("市场源全部拉取失败"));
        return;
    }
    if (okCount < totalCount) {
        setStatusText(QStringLiteral("已更新 %1/%2 个市场源，共 %3 个插件")
                           .arg(okCount)
                           .arg(totalCount)
                           .arg(m_entries.size()));
        return;
    }
    setStatusText(QStringLiteral("市场索引已更新，共 %1 个插件").arg(m_entries.size()));
}

QVariantList PluginMarket::parseIndex(const QString &json)
{
    QVariantList list;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return list;
    const QJsonArray plugins = document.object().value(QStringLiteral("plugins")).toArray();
    list.reserve(plugins.size());
    for (const QJsonValue &value : plugins) {
        if (!value.isObject())
            continue;
        const QJsonObject item = value.toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (id.trimmed().isEmpty())
            continue;
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), id);
        entry.insert(QStringLiteral("name"), item.value(QStringLiteral("name")).toString(id));
        entry.insert(QStringLiteral("version"),
                     item.value(QStringLiteral("version")).toString(QStringLiteral("0.0.0")));
        entry.insert(QStringLiteral("description"), item.value(QStringLiteral("description")).toString());
        entry.insert(QStringLiteral("author"), item.value(QStringLiteral("author")).toString());
        entry.insert(QStringLiteral("homepage"), item.value(QStringLiteral("homepage")).toString());
        entry.insert(QStringLiteral("download"), item.value(QStringLiteral("download")).toString());
        entry.insert(QStringLiteral("sha256"), item.value(QStringLiteral("sha256")).toString().toLower());
        entry.insert(QStringLiteral("size"), item.value(QStringLiteral("size")).toVariant());
        list.append(entry);
    }
    return list;
}

QVariantList PluginMarket::mergeEntries(const QVariantList &entries)
{
    QVariantList merged;
    QHash<QString, int> index;
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        const auto it = index.constFind(id);
        if (it == index.constEnd()) {
            index.insert(id, merged.size());
            merged.append(entry);
            continue;
        }
        const QVariantMap current = merged.at(*it).toMap();
        const QString currentVersion = current.value(QStringLiteral("version")).toString();
        const QString incomingVersion = entry.value(QStringLiteral("version")).toString();
        if (!versionGreaterThan(incomingVersion, currentVersion))
            continue;
        // 新版本胜出；新条目里为空的字段沿用旧条目的值
        QVariantMap winner = entry;
        for (auto itField = current.constBegin(); itField != current.constEnd(); ++itField) {
            if (!winner.contains(itField.key()) || winner.value(itField.key()).toString().isEmpty())
                winner.insert(itField.key(), itField.value());
        }
        merged[*it] = winner;
    }
    return merged;
}

QString PluginMarket::cacheKey(const QString &spec) const
{
    return spec.trimmed();
}

QVariantMap PluginMarket::cachedSource(const QString &spec) const
{
    const QVariantMap cache = readCache();
    const QVariantList sources = cache.value(QStringLiteral("sources")).toList();
    for (const QVariant &value : sources) {
        const QVariantMap record = value.toMap();
        if (record.value(QStringLiteral("spec")).toString() == spec)
            return record;
    }
    return QVariantMap();
}

void PluginMarket::storeCachedSource(const QString &spec, const QString &url, const QString &json,
                                     const QString &etag, const QString &lastModified)
{
    QVariantMap cache = readCache();
    QVariantList sources = cache.value(QStringLiteral("sources")).toList();
    QVariantMap record;
    record.insert(QStringLiteral("spec"), spec);
    record.insert(QStringLiteral("url"), url);
    record.insert(QStringLiteral("index"), json);
    record.insert(QStringLiteral("etag"), etag);
    record.insert(QStringLiteral("lastModified"), lastModified);
    record.insert(QStringLiteral("fetchedAt"), QDateTime::currentSecsSinceEpoch());

    bool replaced = false;
    for (int i = 0; i < sources.size(); ++i) {
        if (sources.at(i).toMap().value(QStringLiteral("spec")).toString() == spec) {
            sources[i] = record;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        sources.append(record);
    cache.insert(QStringLiteral("version"), kCacheVersion);
    cache.insert(QStringLiteral("sources"), sources);

    QFile file(cachePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(QJsonObject::fromVariantMap(cache)).toJson(QJsonDocument::Indented));
    file.close();
}

void PluginMarket::writeCache() const
{
    // 与已有缓存合并（不同源各自一条记录），避免把别的源的缓存覆盖掉
    QVariantMap cache = readCache();
    QVariantList sources = cache.value(QStringLiteral("sources")).toList();
    for (auto it = m_sourceResults.constBegin(); it != m_sourceResults.constEnd(); ++it) {
        const QVariantMap record = it.value().toMap();
        QVariantMap item;
        item.insert(QStringLiteral("spec"), it.key());
        item.insert(QStringLiteral("url"), record.value(QStringLiteral("url")));
        item.insert(QStringLiteral("index"), record.value(QStringLiteral("index")));
        item.insert(QStringLiteral("etag"), record.value(QStringLiteral("etag")));
        item.insert(QStringLiteral("lastModified"), record.value(QStringLiteral("lastModified")));
        item.insert(QStringLiteral("fetchedAt"), record.value(QStringLiteral("fetchedAt")));
        bool replaced = false;
        for (int i = 0; i < sources.size(); ++i) {
            if (sources.at(i).toMap().value(QStringLiteral("spec")).toString() == it.key()) {
                sources[i] = item;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            sources.append(item);
    }
    cache.insert(QStringLiteral("version"), kCacheVersion);
    cache.insert(QStringLiteral("sources"), sources);

    QFile file(cachePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(QJsonObject::fromVariantMap(cache)).toJson(QJsonDocument::Indented));
    file.close();
}

QVariantMap PluginMarket::readCache() const
{
    QVariantMap cache;
    QFile file(cachePath());
    if (!file.open(QIODevice::ReadOnly))
        return cache;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject())
        return cache;
    const QJsonObject object = document.object();

    // 新格式：{version:2, sources:[{spec,url,index,etag,lastModified,fetchedAt}]}
    if (object.contains(QStringLiteral("sources"))) {
        cache.insert(QStringLiteral("version"), object.value(QStringLiteral("version")).toInt());
        QVariantList sources;
        const QJsonArray array = object.value(QStringLiteral("sources")).toArray();
        for (const QJsonValue &value : array) {
            if (!value.isObject())
                continue;
            const QJsonObject item = value.toObject();
            QVariantMap record;
            record.insert(QStringLiteral("spec"), item.value(QStringLiteral("spec")).toString());
            record.insert(QStringLiteral("url"), item.value(QStringLiteral("url")).toString());
            const QJsonValue indexValue = item.value(QStringLiteral("index"));
            QString indexJson;
            if (indexValue.isString())
                indexJson = indexValue.toString();
            else if (indexValue.isObject())
                indexJson = QString::fromUtf8(QJsonDocument(indexValue.toObject()).toJson(QJsonDocument::Compact));
            record.insert(QStringLiteral("index"), indexJson);
            record.insert(QStringLiteral("etag"), item.value(QStringLiteral("etag")).toString());
            record.insert(QStringLiteral("lastModified"), item.value(QStringLiteral("lastModified")).toString());
            record.insert(QStringLiteral("fetchedAt"),
                          item.value(QStringLiteral("fetchedAt")).toInt());
            sources.append(record);
        }
        cache.insert(QStringLiteral("sources"), sources);
        return cache;
    }

    // 旧格式：直接是一份索引 JSON（有 plugins 数组）
    if (object.contains(QStringLiteral("plugins"))) {
        QVariantMap record;
        record.insert(QStringLiteral("spec"), QStringLiteral("__legacy__"));
        record.insert(QStringLiteral("url"), QString());
        record.insert(QStringLiteral("index"), QString::fromUtf8(document.toJson(QJsonDocument::Compact)));
        record.insert(QStringLiteral("etag"), QString());
        record.insert(QStringLiteral("lastModified"), QString());
        record.insert(QStringLiteral("fetchedAt"), QDateTime::currentSecsSinceEpoch());
        cache.insert(QStringLiteral("version"), 1);
        cache.insert(QStringLiteral("sources"), QVariantList{record});
    }
    return cache;
}

bool PluginMarket::cacheFresh(const QVariantMap &record) const
{
    if (record.isEmpty())
        return false;
    if (m_cacheTtlSeconds <= 0)
        return false;
    const qint64 fetchedAt = record.value(QStringLiteral("fetchedAt")).toLongLong();
    if (fetchedAt <= 0)
        return false;
    const qint64 age = QDateTime::currentSecsSinceEpoch() - fetchedAt;
    return age >= 0 && age <= m_cacheTtlSeconds;
}

void PluginMarket::setStatusText(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void PluginMarket::setRefreshing(bool value)
{
    if (m_refreshing == value)
        return;
    m_refreshing = value;
    emit refreshingChanged();
}

void PluginMarket::setOnline(bool value)
{
    if (m_online == value)
        return;
    m_online = value;
    emit onlineChanged();
}

void PluginMarket::downloadPackage(const QString &url, const QString &sha256, const QString &destFile)
{
    if (url.trimmed().isEmpty()) {
        emit packageReady(destFile, false, QStringLiteral("下载地址为空"));
        return;
    }

    m_pendingUrl = url;
    m_pendingSha = sha256.trimmed().toLower();
    m_pendingDest = destFile;

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NeonBoard-PluginMarket"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit packageReady(m_pendingDest, false,
                              QStringLiteral("下载失败：%1").arg(reply->errorString()));
            return;
        }
        const QByteArray payload = reply->readAll();
        if (!m_pendingSha.isEmpty()) {
            const QString actual =
                QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
            if (actual != m_pendingSha) {
                emit packageReady(m_pendingDest, false,
                                  QStringLiteral("校验失败：SHA-256 不匹配"));
                return;
            }
        }
        QFile file(m_pendingDest);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit packageReady(m_pendingDest, false, QStringLiteral("无法写入临时文件"));
            return;
        }
        file.write(payload);
        file.close();
        emit packageReady(m_pendingDest, true, QStringLiteral("下载完成"));
    });
}

PluginMarket::FetchOutcome PluginMarket::fetchUrl(const QString &url, const QString &conditionalEtag,
                                                  const QString &conditionalLastModified, int timeoutMs)
{
    FetchOutcome outcome;
    if (url.trimmed().isEmpty()) {
        outcome.error = QStringLiteral("地址为空");
        return outcome;
    }

    if (url.startsWith(QStringLiteral("file://"))) {
        QFile file(QUrl(url).toLocalFile());
        if (!file.open(QIODevice::ReadOnly)) {
            outcome.error = QStringLiteral("无法读取本地文件");
            return outcome;
        }
        outcome.payload = file.readAll();
        file.close();
        outcome.ok = true;
        outcome.status = 200;
        return outcome;
    }

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NeonBoard-PluginMarket"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!conditionalEtag.isEmpty())
        request.setRawHeader(QByteArrayLiteral("If-None-Match"), conditionalEtag.toUtf8());
    if (!conditionalLastModified.isEmpty())
        request.setRawHeader(QByteArrayLiteral("If-Modified-Since"), conditionalLastModified.toUtf8());

    QNetworkReply *reply = m_network->get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs > 0 ? timeoutMs : kIndexTimeoutMs);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        outcome.error = QStringLiteral("请求超时");
        return outcome;
    }

    outcome.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    outcome.etag = QString::fromUtf8(reply->rawHeader("ETag"));
    outcome.lastModified = QString::fromUtf8(reply->rawHeader("Last-Modified"));

    if (outcome.status == 304) {
        // 索引未变化：不是错误，沿用缓存
        reply->deleteLater();
        outcome.ok = true;
        outcome.notModified = true;
        return outcome;
    }

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError || outcome.status >= 400) {
        outcome.error = QStringLiteral("下载失败：%1").arg(reply->errorString());
        reply->deleteLater();
        return outcome;
    }
    reply->deleteLater();
    outcome.payload = body;
    outcome.ok = true;
    return outcome;
}

bool PluginMarket::fetchBlocking(const QString &url, QByteArray *payload, int timeoutMs, QString *error)
{
    const FetchOutcome outcome = fetchUrl(url, QString(), QString(), timeoutMs);
    if (!outcome.ok) {
        if (error)
            *error = outcome.error;
        return false;
    }
    if (payload)
        *payload = outcome.payload;
    if (error)
        error->clear();
    return true;
}

bool PluginMarket::refreshBlocking(int timeoutMs, QString *error)
{
    return refreshBlocking(timeoutMs, error, true);
}

bool PluginMarket::refreshBlocking(int timeoutMs, QString *error, bool force)
{
    if (m_sources.isEmpty()) {
        if (error)
            *error = QStringLiteral("未配置市场地址");
        return false;
    }

    QVariantList merged;
    int okCount = 0;
    int totalCount = 0;
    m_sourceResults.clear();
    m_sourceStatuses.clear();

    for (const QVariant &value : m_sources) {
        const QVariantMap record = value.toMap();
        if (!record.value(QStringLiteral("enabled"), true).toBool())
            continue;
        ++totalCount;
        const QString spec = record.value(QStringLiteral("spec")).toString();
        const QString name = record.value(QStringLiteral("name")).toString();
        const QStringList candidates = resolveCandidates(spec);
        if (candidates.isEmpty()) {
            m_sourceStatuses.append(sourceStatus(spec, name, false, false,
                                                QStringLiteral("源地址无效")));
            continue;
        }

        const QVariantMap cached = cachedSource(spec);
        if (!force && cacheFresh(cached)) {
            merged = mergeEntries(merged + annotateEntries(parseIndex(cached.value(QStringLiteral("index")).toString()),
                                                            name, spec, cached.value(QStringLiteral("url")).toString()));
            m_sourceResults.insert(spec, cached);
            ++okCount;
            m_sourceStatuses.append(sourceStatus(spec, name, true, true,
                                                QStringLiteral("使用本地缓存")));
            continue;
        }

        bool fetched = false;
        for (const QString &candidate : candidates) {
            const FetchOutcome outcome = fetchUrl(candidate,
                                                 cached.value(QStringLiteral("etag")).toString(),
                                                 cached.value(QStringLiteral("lastModified")).toString(),
                                                 timeoutMs);
            if (outcome.notModified) {
                // 索引未变化（304）：沿用缓存内容，只刷新时间戳
                QVariantMap kept = cached;
                kept.insert(QStringLiteral("url"), candidate);
                kept.insert(QStringLiteral("fetchedAt"), QDateTime::currentSecsSinceEpoch());
                if (kept.value(QStringLiteral("etag")).toString().isEmpty())
                    kept.insert(QStringLiteral("etag"), outcome.etag);
                if (kept.value(QStringLiteral("lastModified")).toString().isEmpty())
                    kept.insert(QStringLiteral("lastModified"), outcome.lastModified);
                m_sourceResults.insert(spec, kept);
                storeCachedSource(spec, candidate, kept.value(QStringLiteral("index")).toString(),
                                  kept.value(QStringLiteral("etag")).toString(),
                                  kept.value(QStringLiteral("lastModified")).toString());
                merged = mergeEntries(merged + annotateEntries(parseIndex(kept.value(QStringLiteral("index")).toString()),
                                                                name, spec, candidate));
                ++okCount;
                fetched = true;
                m_sourceStatuses.append(sourceStatus(spec, name, true, true,
                                                    QStringLiteral("索引未变化（304）")));
                break;
            }
            if (!outcome.ok)
                continue;
            const QString json = QString::fromUtf8(outcome.payload);
            if (json.trimmed().isEmpty())
                continue;
            QVariantMap fresh;
            fresh.insert(QStringLiteral("url"), candidate);
            fresh.insert(QStringLiteral("index"), json);
            fresh.insert(QStringLiteral("etag"), outcome.etag);
            fresh.insert(QStringLiteral("lastModified"), outcome.lastModified);
            fresh.insert(QStringLiteral("fetchedAt"), QDateTime::currentSecsSinceEpoch());
            m_sourceResults.insert(spec, fresh);
            storeCachedSource(spec, candidate, json, outcome.etag, outcome.lastModified);
            merged = mergeEntries(merged + annotateEntries(parseIndex(json), name, spec, candidate));
            ++okCount;
            fetched = true;
            m_sourceStatuses.append(sourceStatus(spec, name, true, false,
                                                QStringLiteral("已更新")));
            break;
        }
        if (fetched)
            continue;
        if (!cached.isEmpty()) {
            merged = mergeEntries(merged + annotateEntries(parseIndex(cached.value(QStringLiteral("index")).toString()),
                                                            name, spec, cached.value(QStringLiteral("url")).toString()));
            m_sourceResults.insert(spec, cached);
            ++okCount;
            m_sourceStatuses.append(sourceStatus(spec, name, true, true,
                                                QStringLiteral("拉取失败，使用本地缓存")));
        } else {
            m_sourceStatuses.append(sourceStatus(spec, name, false, false,
                                                QStringLiteral("拉取失败且无本地缓存")));
        }
    }

    m_entries = merged;
    writeCache();
    setOnline(okCount > 0);
    emit entriesChanged();
    emit sourcesChanged();
    if (okCount == 0) {
        if (error)
            *error = QStringLiteral("市场源全部拉取失败");
        setStatusText(QStringLiteral("市场源全部拉取失败"));
        return false;
    }
    if (okCount < totalCount) {
        setStatusText(QStringLiteral("已更新 %1/%2 个市场源，共 %3 个插件")
                           .arg(okCount)
                           .arg(totalCount)
                           .arg(m_entries.size()));
    } else {
        setStatusText(QStringLiteral("市场索引已更新，共 %1 个插件").arg(m_entries.size()));
    }
    if (error)
        error->clear();
    return true;
}

bool PluginMarket::downloadPackageBlocking(const QString &url, const QString &sha256,
                                           const QString &destFile, QString *error)
{
    QByteArray payload;
    if (!fetchBlocking(url, &payload, kDownloadTimeoutMs, error))
        return false;

    const QString expected = sha256.trimmed().toLower();
    if (!expected.isEmpty()) {
        const QString actual =
            QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        if (actual != expected) {
            if (error)
                *error = QStringLiteral("校验失败：SHA-256 不匹配");
            return false;
        }
    }

    QFile file(destFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入临时文件：%1").arg(destFile);
        return false;
    }
    file.write(payload);
    file.close();
    if (error)
        error->clear();
    return true;
}

QVariantMap PluginMarket::entryById(const QString &id) const
{
    for (const QVariant &value : m_entries) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("id")).toString() == id)
            return entry;
    }
    return QVariantMap();
}
