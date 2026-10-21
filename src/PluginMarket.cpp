#include "PluginMarket.h"

#include "AppPaths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int kIndexTimeoutMs = 10000;
constexpr int kDownloadTimeoutMs = 30000;
}

PluginMarket::PluginMarket(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_source(defaultSource())
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(kIndexTimeoutMs);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        if (m_refreshing) {
            m_refreshing = false;
            emit refreshingChanged();
            setStatusText(QStringLiteral("市场请求超时"));
        }
    });
}

PluginMarket::~PluginMarket() = default;

QString PluginMarket::defaultSource()
{
    // 内置默认市场地址；用户可在设置面板改成自己的仓库
    return QStringLiteral("https://raw.githubusercontent.com/lin-schenglin/neonboard-market/main/market.json");
}

QString PluginMarket::cachePath()
{
    return AppPaths::dataDir() + QStringLiteral("/market-cache.json");
}

void PluginMarket::setSource(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty() || trimmed == m_source)
        return;
    m_source = trimmed;
    emit sourceChanged();
    setStatusText(QStringLiteral("市场地址已更新"));
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

void PluginMarket::refresh()
{
    if (m_source.trimmed().isEmpty()) {
        setStatusText(QStringLiteral("未配置市场地址"));
        return;
    }
    setRefreshing(true);
    m_timer->start();

    QNetworkRequest request{QUrl(m_source)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NeonBoard-PluginMarket"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_timer->stop();
        reply->deleteLater();
        setRefreshing(false);

        if (reply->error() != QNetworkReply::NoError) {
            setOnline(false);
            const QString cached = readCache();
            if (!cached.isEmpty()) {
                applyIndex(cached, false);
                setStatusText(QStringLiteral("离线模式，使用缓存索引"));
            } else {
                setStatusText(QStringLiteral("拉取市场索引失败：%1").arg(reply->errorString()));
                emit errorOccurred(reply->errorString());
            }
            return;
        }

        const QString payload = QString::fromUtf8(reply->readAll());
        if (payload.trimmed().isEmpty()) {
            setStatusText(QStringLiteral("市场索引为空"));
            return;
        }
        writeCache(payload);
        setOnline(true);
        applyIndex(payload, true);
    });
}

void PluginMarket::applyIndex(const QString &json, bool fromNetwork)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        setStatusText(QStringLiteral("市场索引解析失败：%1").arg(error.errorString()));
        return;
    }
    const QJsonArray plugins = document.object().value(QStringLiteral("plugins")).toArray();
    QVariantList list;
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
        entry.insert(QStringLiteral("name"),
                     item.value(QStringLiteral("name")).toString(id));
        entry.insert(QStringLiteral("version"),
                     item.value(QStringLiteral("version")).toString(QStringLiteral("0.0.0")));
        entry.insert(QStringLiteral("description"),
                     item.value(QStringLiteral("description")).toString());
        entry.insert(QStringLiteral("author"),
                     item.value(QStringLiteral("author")).toString());
        entry.insert(QStringLiteral("homepage"),
                     item.value(QStringLiteral("homepage")).toString());
        entry.insert(QStringLiteral("download"),
                     item.value(QStringLiteral("download")).toString());
        entry.insert(QStringLiteral("sha256"),
                     item.value(QStringLiteral("sha256")).toString().toLower());
        entry.insert(QStringLiteral("size"),
                     item.value(QStringLiteral("size")).toVariant());
        list.append(entry);
    }
    m_entries = list;
    emit entriesChanged();
    setStatusText(fromNetwork
                      ? QStringLiteral("市场索引已更新，共 %1 个插件").arg(m_entries.size())
                      : QStringLiteral("缓存索引，共 %1 个插件").arg(m_entries.size()));
}

bool PluginMarket::writeCache(const QString &json) const
{
    QFile file(cachePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(json.toUtf8()) >= 0;
}

QString PluginMarket::readCache() const
{
    QFile file(cachePath());
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
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

bool PluginMarket::fetchBlocking(const QString &url, QByteArray *payload, int timeoutMs, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (url.trimmed().isEmpty())
        return fail(QStringLiteral("地址为空"));

    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NeonBoard-PluginMarket"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

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
        return fail(QStringLiteral("请求超时"));
    }
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        const QString message = reply->errorString();
        reply->deleteLater();
        return fail(QStringLiteral("下载失败：%1").arg(message));
    }
    reply->deleteLater();
    if (payload)
        *payload = body;
    if (error)
        error->clear();
    return true;
}

bool PluginMarket::refreshBlocking(int timeoutMs, QString *error)
{
    if (m_source.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("未配置市场地址");
        return false;
    }
    QByteArray payload;
    if (fetchBlocking(m_source, &payload, timeoutMs, error)) {
        const QString json = QString::fromUtf8(payload);
        writeCache(json);
        setOnline(true);
        applyIndex(json, true);
        return true;
    }
    // 离线降级：读缓存索引
    const QString cached = readCache();
    if (!cached.isEmpty()) {
        setOnline(false);
        applyIndex(cached, false);
        setStatusText(QStringLiteral("离线模式，使用缓存索引"));
        if (error)
            error->clear();
        return true;
    }
    return false;
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
