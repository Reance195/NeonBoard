#include "PluginLibrary.h"

#include "AppPaths.h"
#include "Database.h"
#include "PluginLoader.h"
#include "PluginMarket.h"
#include "PluginPackage.h"
#include "PluginRuntime.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantList>

namespace {

constexpr auto kMarketSourceMetaKey = "plugin.market.source";
constexpr auto kMarketSourcesMetaKey = "plugin.market.sources";

QString dirNameForId(const QString &id)
{
    return PluginPackage::dirNameForId(id);
}

} // namespace

PluginLibrary::PluginLibrary(PluginLoader *loader,
                             PluginMarket *market,
                             PluginRuntime *runtime,
                             Database *database,
                             QObject *parent)
    : QObject(parent)
    , m_loader(loader)
    , m_market(market)
    , m_runtime(runtime)
    , m_database(database)
{
    if (m_market) {
        connect(m_market, &PluginMarket::entriesChanged, this, &PluginLibrary::rebuild);
        connect(m_market, &PluginMarket::sourcesChanged, this, &PluginLibrary::onMarketSourcesChanged);
        loadMarketSources();
    }
    if (m_loader)
        connect(m_loader, &PluginLoader::pluginsChanged, this, &PluginLibrary::rebuild);
    rebuild();
}

QString PluginLibrary::marketSource() const
{
    return m_market ? m_market->source() : QString();
}

QVariantList PluginLibrary::marketSources() const
{
    return m_market ? m_market->sources() : QVariantList();
}

QString PluginLibrary::marketStatus() const
{
    return m_market ? m_market->statusText() : QString();
}

void PluginLibrary::setMarketSource(const QString &url)
{
    if (!m_market)
        return;
    const QString trimmed = url.trimmed();
    m_market->setSource(trimmed);
    saveMarketSources();
    emit marketSourceChanged();
    rebuild();
}

void PluginLibrary::loadMarketSources()
{
    if (!m_market)
        return;
    QVariantList sources;
    if (m_database) {
        const QString raw = m_database->metaValue(QString::fromLatin1(kMarketSourcesMetaKey));
        if (!raw.trimmed().isEmpty()) {
            const QJsonDocument document = QJsonDocument::fromJson(raw.toUtf8());
            if (document.isArray()) {
                for (const QJsonValue &value : document.array()) {
                    const QJsonObject item = value.toObject();
                    const QString spec = item.value(QStringLiteral("spec")).toString().trimmed();
                    if (spec.isEmpty())
                        continue;
                    QVariantMap record;
                    record.insert(QStringLiteral("spec"), spec);
                    record.insert(QStringLiteral("name"), item.value(QStringLiteral("name")).toString());
                    record.insert(QStringLiteral("enabled"),
                                  item.value(QStringLiteral("enabled")).toBool(true));
                    sources.append(record);
                }
            }
        }
        // 旧版单源配置迁移成源列表
        if (sources.isEmpty()) {
            const QString legacy = m_database->metaValue(QString::fromLatin1(kMarketSourceMetaKey));
            if (!legacy.trimmed().isEmpty()) {
                QVariantMap record;
                record.insert(QStringLiteral("spec"), legacy.trimmed());
                record.insert(QStringLiteral("name"), QString());
                record.insert(QStringLiteral("enabled"), true);
                sources.append(record);
            }
        }
    }
    if (sources.isEmpty())
        return; // 没有配置就用内置默认源
    m_market->setSources(sources);
}

void PluginLibrary::saveMarketSources()
{
    if (!m_market || !m_database)
        return;
    QJsonArray array;
    for (const QVariant &value : m_market->sources()) {
        const QVariantMap record = value.toMap();
        QJsonObject item;
        item.insert(QStringLiteral("spec"), record.value(QStringLiteral("spec")).toString());
        item.insert(QStringLiteral("name"), record.value(QStringLiteral("name")).toString());
        item.insert(QStringLiteral("enabled"), record.value(QStringLiteral("enabled")).toBool());
        array.append(item);
    }
    m_database->setMetaValue(QString::fromLatin1(kMarketSourcesMetaKey),
                             QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void PluginLibrary::onMarketSourcesChanged()
{
    saveMarketSources();
    emit marketSourceChanged();
    emit marketSourcesChanged();
    rebuild();
}

bool PluginLibrary::addMarketSource(const QString &spec, const QString &name)
{
    if (!m_market)
        return false;
    if (!m_market->addSource(spec, name)) {
        setStatusText(m_market->statusText());
        return false;
    }
    setStatusText(QStringLiteral("已添加市场源"));
    return true;
}

bool PluginLibrary::removeMarketSource(int index)
{
    if (!m_market)
        return false;
    if (!m_market->removeSource(index))
        return false;
    setStatusText(QStringLiteral("已移除市场源"));
    return true;
}

bool PluginLibrary::setMarketSourceEnabled(int index, bool enabled)
{
    if (!m_market)
        return false;
    if (!m_market->setSourceEnabled(index, enabled))
        return false;
    return true;
}

bool PluginLibrary::moveMarketSource(int from, int to)
{
    if (!m_market)
        return false;
    if (!m_market->moveSource(from, to))
        return false;
    setStatusText(QStringLiteral("已调整市场源优先级"));
    return true;
}

bool PluginLibrary::installFromUrl(const QString &url)
{
    if (!m_market)
        return false;
    QString target = url.trimmed();
    if (target.isEmpty()) {
        setStatusText(QStringLiteral("链接为空"));
        return false;
    }
    QString sha256;
    const int hashAt = target.indexOf(QStringLiteral("#sha256="));
    if (hashAt >= 0) {
        sha256 = target.mid(hashAt + 8).trimmed().toLower();
        target = target.left(hashAt);
    }
    if (target.isEmpty()) {
        setStatusText(QStringLiteral("链接为空"));
        return false;
    }

    const QString tempFile = QDir(QDir::tempPath()).filePath(QStringLiteral("neonboard-direct.neon"));
    m_market->downloadPackage(target, sha256, tempFile);
    connect(m_market, &PluginMarket::packageReady, this,
            [this, tempFile](const QString &file, bool ok, const QString &message) {
                Q_UNUSED(file)
                if (!ok) {
                    setStatusText(message);
                    emit operationFinished(QString(), QStringLiteral("install"), false, message);
                    QFile::remove(tempFile);
                    return;
                }
                const QString pluginId = PluginPackage::neonPluginId(tempFile);
                if (pluginId.isEmpty()) {
                    const QString text = QStringLiteral("包格式非法：缺少 manifest.id");
                    setStatusText(text);
                    emit operationFinished(QString(), QStringLiteral("install"), false, text);
                    QFile::remove(tempFile);
                    return;
                }
                const QString targetDir =
                    AppPaths::pluginDir() + QLatin1Char('/') + dirNameForId(pluginId);
                QString installError;
                if (!PluginPackage::installNeonFile(tempFile, targetDir, &installError)) {
                    setStatusText(installError);
                    emit operationFinished(pluginId, QStringLiteral("install"), false, installError);
                    QFile::remove(tempFile);
                    return;
                }
                QFile::remove(tempFile);
                setStatusText(QStringLiteral("已安装 %1").arg(pluginId));
                if (m_runtime)
                    m_runtime->reload();
                rebuild();
                emit operationFinished(pluginId, QStringLiteral("install"), true, QStringLiteral("安装成功"));
            },
            Qt::SingleShotConnection);
    return true;
}

bool PluginLibrary::installFromUrlBlocking(const QString &url, QString *error)
{
    if (!m_market) {
        if (error)
            *error = QStringLiteral("市场模块不可用");
        return false;
    }
    QString target = url.trimmed();
    if (target.isEmpty()) {
        if (error)
            *error = QStringLiteral("链接为空");
        return false;
    }
    QString sha256;
    const int hashAt = target.indexOf(QStringLiteral("#sha256="));
    if (hashAt >= 0) {
        sha256 = target.mid(hashAt + 8).trimmed().toLower();
        target = target.left(hashAt);
    }
    const QString tempFile = QDir(QDir::tempPath()).filePath(QStringLiteral("neonboard-direct.neon"));
    if (!m_market->downloadPackageBlocking(target, sha256, tempFile, error)) {
        QFile::remove(tempFile);
        return false;
    }
    const QString pluginId = PluginPackage::neonPluginId(tempFile);
    if (pluginId.isEmpty()) {
        QFile::remove(tempFile);
        if (error)
            *error = QStringLiteral("包格式非法：缺少 manifest.id");
        return false;
    }
    const QString targetDir = AppPaths::pluginDir() + QLatin1Char('/') + dirNameForId(pluginId);
    if (!PluginPackage::installNeonFile(tempFile, targetDir, error)) {
        QFile::remove(tempFile);
        return false;
    }
    QFile::remove(tempFile);
    setStatusText(QStringLiteral("已安装 %1").arg(pluginId));
    if (m_runtime)
        m_runtime->reload();
    rebuild();
    emit operationFinished(pluginId, QStringLiteral("install"), true, QStringLiteral("安装成功"));
    return true;
}

void PluginLibrary::setStatusText(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void PluginLibrary::refresh()
{
    if (m_market)
        m_market->refresh();
    rebuild();
}

void PluginLibrary::rebuild()
{
    QVariantList entries;
    QHash<QString, int> indexOfId;
    if (!m_loader)
        return;

    // 1) 本地已装 / 本地可安装
    const QVariantList candidates = m_loader->candidates();
    QVariantList marketEntries;
    if (m_market)
        marketEntries = m_market->entries();

    for (const QVariant &value : candidates) {
        const QVariantMap candidate = value.toMap();
        const QString id = candidate.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        const QString source = candidate.value(QStringLiteral("source")).toString();
        const bool installed = source == QLatin1String("appdata");
        const bool shadowed = candidate.value(QStringLiteral("shadowed")).toBool();
        if (!installed && shadowed)
            continue; // 同 id 已有更高优先级的来源，这条只是备选
        if (indexOfId.contains(id))
            continue; // 同 id 只保留一条（appdata 优先，扫描顺序已保证）

        QVariantMap entry;
        entry.insert(QStringLiteral("id"), id);
        entry.insert(QStringLiteral("name"), candidate.value(QStringLiteral("name")));
        entry.insert(QStringLiteral("version"), candidate.value(QStringLiteral("version")));
        entry.insert(QStringLiteral("description"), QString());
        entry.insert(QStringLiteral("status"), candidate.value(QStringLiteral("status")));
        entry.insert(QStringLiteral("error"), candidate.value(QStringLiteral("error")));
        entry.insert(QStringLiteral("installed"), installed);
        entry.insert(QStringLiteral("enabled"),
                     m_loader->isPluginEnabled(id));
        entry.insert(QStringLiteral("source"), source);
        entry.insert(QStringLiteral("dir"), candidate.value(QStringLiteral("dir")));
        entry.insert(QStringLiteral("origin"), installed ? QStringLiteral("local")
                                                         : QStringLiteral("bundled"));
        entry.insert(QStringLiteral("hasMarket"), false);
        entry.insert(QStringLiteral("marketVersion"), QString());
        indexOfId.insert(id, entries.size());
        entries.append(entry);
    }

    // 2) 市场里的插件（未安装的才列出来）
    for (const QVariant &value : marketEntries) {
        const QVariantMap item = value.toMap();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        bool known = false;
        for (const QVariant &existing : entries) {
            if (existing.toMap().value(QStringLiteral("id")).toString() == id) {
                known = true;
                break;
            }
        }
        if (known) {
            // 本地已有同一个 id：把市场版本挂上去，界面据此显示「更新」
            const int at = indexOfId.value(id);
            QVariantMap merged = entries.at(at).toMap();
            merged.insert(QStringLiteral("hasMarket"), true);
            merged.insert(QStringLiteral("marketVersion"), item.value(QStringLiteral("version")));
            merged.insert(QStringLiteral("marketDescription"),
                          item.value(QStringLiteral("description")));
            merged.insert(QStringLiteral("marketSource"), item.value(QStringLiteral("marketSource")));
            if (merged.value(QStringLiteral("description")).toString().isEmpty())
                merged.insert(QStringLiteral("description"), item.value(QStringLiteral("description")));
            entries[at] = merged;
            continue;
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), id);
        entry.insert(QStringLiteral("name"), item.value(QStringLiteral("name")));
        entry.insert(QStringLiteral("version"), item.value(QStringLiteral("version")));
        entry.insert(QStringLiteral("description"), item.value(QStringLiteral("description")));
        entry.insert(QStringLiteral("status"), QStringLiteral("ok"));
        entry.insert(QStringLiteral("error"), QString());
        entry.insert(QStringLiteral("installed"), false);
        entry.insert(QStringLiteral("enabled"), true);
        entry.insert(QStringLiteral("source"), QStringLiteral("market"));
        entry.insert(QStringLiteral("dir"), QString());
        entry.insert(QStringLiteral("origin"), QStringLiteral("market"));
        entry.insert(QStringLiteral("hasMarket"), true);
        entry.insert(QStringLiteral("marketVersion"), item.value(QStringLiteral("version")));
        entry.insert(QStringLiteral("marketSource"), item.value(QStringLiteral("marketSource")));
        entries.append(entry);
    }

    m_entries = entries;
    qInfo().noquote() << "PluginLibrary: rebuilt" << entries.size() << "entrie(s)";
    emit entriesChanged();
}

QString PluginLibrary::localSourceDir(const QString &pluginId) const
{
    if (!m_loader)
        return QString();
    for (const QVariant &value : m_loader->candidates()) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("id")).toString() != pluginId)
            continue;
        const QString source = candidate.value(QStringLiteral("source")).toString();
        if (source == QLatin1String("appdata"))
            continue; // 已安装的那份不算「来源」
        return candidate.value(QStringLiteral("dir")).toString();
    }
    return QString();
}

QString PluginLibrary::marketDownloadUrl(const QString &pluginId) const
{
    if (!m_market)
        return QString();
    for (const QVariant &value : m_market->entries()) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("id")).toString() == pluginId)
            return item.value(QStringLiteral("download")).toString();
    }
    return QString();
}

QString PluginLibrary::marketSha256(const QString &pluginId) const
{
    if (!m_market)
        return QString();
    for (const QVariant &value : m_market->entries()) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("id")).toString() == pluginId)
            return item.value(QStringLiteral("sha256")).toString();
    }
    return QString();
}

QString PluginLibrary::marketVersion(const QString &pluginId) const
{
    if (!m_market)
        return QString();
    for (const QVariant &value : m_market->entries()) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("id")).toString() == pluginId)
            return item.value(QStringLiteral("version")).toString();
    }
    return QString();
}

bool PluginLibrary::versionGreaterThan(const QString &left, const QString &right)
{
    if (right.isEmpty())
        return !left.isEmpty();
    if (left.isEmpty())
        return false;
    const QStringList a = left.split(QLatin1Char('.'));
    const QStringList b = right.split(QLatin1Char('.'));
    const int count = qMax(a.size(), b.size());
    for (int i = 0; i < count; ++i) {
        const int leftPart = i < a.size() ? a.at(i).toInt() : 0;
        const int rightPart = i < b.size() ? b.at(i).toInt() : 0;
        if (leftPart != rightPart)
            return leftPart > rightPart;
    }
    return false;
}

bool PluginLibrary::installFromLocalSource(const QString &pluginId, QString *error)
{
    const QString sourceDir = localSourceDir(pluginId);
    if (sourceDir.isEmpty()) {
        if (error)
            *error = QStringLiteral("找不到本地插件源");
        return false;
    }
    const QString target = AppPaths::pluginDir() + QLatin1Char('/') + dirNameForId(pluginId);
    if (!PluginPackage::installDirectory(sourceDir, target, error))
        return false;
    setStatusText(QStringLiteral("已从本地源安装 %1").arg(pluginId));
    return true;
}

bool PluginLibrary::installFromMarket(const QString &pluginId, QString *error)
{
    const QString url = marketDownloadUrl(pluginId);
    if (url.isEmpty()) {
        if (error)
            *error = QStringLiteral("市场条目缺少下载地址");
        return false;
    }
    const QString tempFile =
        QDir(QDir::tempPath()).filePath(QStringLiteral("neonboard-plugin-%1.neon").arg(pluginId));
    m_market->downloadPackage(url, marketSha256(pluginId), tempFile);
    // downloadPackage 是异步的：先返回，等 packageReady 信号再落盘
    connect(m_market, &PluginMarket::packageReady, this,
            [this, pluginId, tempFile](const QString &file, bool ok, const QString &message) {
                Q_UNUSED(file)
                if (ok) {
                    const QString target =
                        AppPaths::pluginDir() + QLatin1Char('/') + dirNameForId(pluginId);
                    QString installError;
                    if (PluginPackage::installNeonFile(tempFile, target, &installError)) {
                        setStatusText(QStringLiteral("已安装 %1").arg(pluginId));
                        if (m_runtime)
                            m_runtime->reload();
                        rebuild();
                        emit operationFinished(pluginId, QStringLiteral("install"), true,
                                              QStringLiteral("安装成功"));
                    } else {
                        emit operationFinished(pluginId, QStringLiteral("install"), false, installError);
                    }
                } else {
                    emit operationFinished(pluginId, QStringLiteral("install"), false, message);
                }
                QFile::remove(tempFile);
            },
            Qt::SingleShotConnection);
    if (error)
        *error = QStringLiteral("正在下载…");
    return true;
}

bool PluginLibrary::installFromMarketBlocking(const QString &pluginId, QString *error)
{
    const QString url = marketDownloadUrl(pluginId);
    if (url.isEmpty()) {
        if (error)
            *error = QStringLiteral("市场条目缺少下载地址");
        return false;
    }
    const QString tempFile =
        QDir(QDir::tempPath()).filePath(QStringLiteral("neonboard-plugin-%1.neon").arg(pluginId));
    if (!m_market->downloadPackageBlocking(url, marketSha256(pluginId), tempFile, error)) {
        QFile::remove(tempFile);
        return false;
    }
    const QString target = AppPaths::pluginDir() + QLatin1Char('/') + dirNameForId(pluginId);
    if (!PluginPackage::installNeonFile(tempFile, target, error)) {
        QFile::remove(tempFile);
        return false;
    }
    QFile::remove(tempFile);
    setStatusText(QStringLiteral("已安装 %1").arg(pluginId));
    if (m_runtime)
        m_runtime->reload();
    rebuild();
    emit operationFinished(pluginId, QStringLiteral("install"), true, QStringLiteral("安装成功"));
    return true;
}

bool PluginLibrary::refreshMarketBlocking(int timeoutMs, QString *error)
{
    if (!m_market) {
        if (error)
            *error = QStringLiteral("市场模块不可用");
        return false;
    }
    if (!m_market->refreshBlocking(timeoutMs, error))
        return false;
    rebuild();
    return true;
}

bool PluginLibrary::installBlocking(const QString &pluginId, QString *error)
{
    if (pluginId.isEmpty()) {
        if (error)
            *error = QStringLiteral("插件 id 为空");
        return false;
    }
    // 市场索引可能还没在本进程加载过（新起的命令行进程），先同步一次
    if (marketDownloadUrl(pluginId).isEmpty() && m_market) {
        QString refreshError;
        m_market->refreshBlocking(10000, &refreshError);
    }
    if (!marketDownloadUrl(pluginId).isEmpty())
        return installFromMarketBlocking(pluginId, error);
    if (!installFromLocalSource(pluginId, error))
        return false;
    if (m_runtime)
        m_runtime->reload();
    rebuild();
    emit operationFinished(pluginId, QStringLiteral("install"), true, QStringLiteral("安装成功"));
    return true;
}

bool PluginLibrary::reportFailure(const QString &pluginId, const QString &action, const QString &message)
{
    setStatusText(message);
    emit operationFinished(pluginId, action, false, message);
    return false;
}

bool PluginLibrary::install(const QString &pluginId)
{
    if (pluginId.isEmpty())
        return reportFailure(pluginId, QStringLiteral("install"), QStringLiteral("插件 id 为空"));
    if (m_loader && m_loader->isPluginEnabled(pluginId) == false) {
        // 已安装但被禁用：直接启用即可
        if (m_loader->setPluginEnabled(pluginId, true) && m_runtime)
            m_runtime->reload();
        rebuild();
        setStatusText(QStringLiteral("已启用 %1").arg(pluginId));
        emit operationFinished(pluginId, QStringLiteral("install"), true, QStringLiteral("已启用"));
        return true;
    }
    const QString url = marketDownloadUrl(pluginId);
    if (!url.isEmpty()) {
        QString error;
        if (!installFromMarket(pluginId, &error))
            return reportFailure(pluginId, QStringLiteral("install"), error);
        setStatusText(QStringLiteral("正在下载 %1…").arg(pluginId));
        return true;
    }
    QString error;
    if (!installFromLocalSource(pluginId, &error)) {
        // 本地没有这个插件：多半是市场索引还没拉取，触发一次刷新让用户重试
        if (m_market)
            m_market->refresh();
        return reportFailure(pluginId, QStringLiteral("install"),
                             QStringLiteral("本地未找到该插件，已刷新市场索引，请重试"));
    }
    if (m_runtime)
        m_runtime->reload();
    rebuild();
    emit operationFinished(pluginId, QStringLiteral("install"), true, QStringLiteral("安装成功"));
    return true;
}

bool PluginLibrary::uninstall(const QString &pluginId)
{
    if (pluginId.isEmpty())
        return reportFailure(pluginId, QStringLiteral("uninstall"), QStringLiteral("插件 id 为空"));
    // 只允许删用户安装区里的副本，官方源/示例源是只读的
    const QString target = AppPaths::pluginDir() + QLatin1Char('/') + dirNameForId(pluginId);
    if (!QDir(target).exists()) {
        return reportFailure(pluginId, QStringLiteral("uninstall"),
                             QStringLiteral("该插件不在用户安装区，无需卸载"));
    }
    if (!QDir(target).removeRecursively()) {
        return reportFailure(pluginId, QStringLiteral("uninstall"),
                             QStringLiteral("无法删除目录：%1").arg(target));
    }
    setStatusText(QStringLiteral("已卸载 %1").arg(pluginId));
    if (m_runtime)
        m_runtime->reload();
    rebuild();
    emit operationFinished(pluginId, QStringLiteral("uninstall"), true, QStringLiteral("卸载成功"));
    return true;
}

bool PluginLibrary::setEnabled(const QString &pluginId, bool enabled)
{
    if (!m_loader)
        return reportFailure(pluginId, QStringLiteral("enable"), QStringLiteral("插件加载器不可用"));
    if (!m_loader->setPluginEnabled(pluginId, enabled))
        return reportFailure(pluginId, QStringLiteral("enable"), QStringLiteral("无法更新禁用清单"));
    if (m_runtime)
        m_runtime->reload();
    rebuild();
    emit operationFinished(pluginId, enabled ? QStringLiteral("enable") : QStringLiteral("disable"),
                          true, enabled ? QStringLiteral("已启用") : QStringLiteral("已禁用"));
    return true;
}

bool PluginLibrary::update(const QString &pluginId)
{
    const QString marketVer = marketVersion(pluginId);
    QString localVer;
    for (const QVariant &value : m_entries) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("id")).toString() == pluginId) {
            localVer = entry.value(QStringLiteral("version")).toString();
            break;
        }
    }
    if (!marketVer.isEmpty() && versionGreaterThan(marketVer, localVer))
        return install(pluginId);
    return reportFailure(pluginId, QStringLiteral("update"), QStringLiteral("已是最新版本"));
}

bool PluginLibrary::openPluginDir()
{
    return m_loader ? m_loader->openPluginDir() : false;
}

void PluginLibrary::requestOpen(const QString &pluginId)
{
    if (pluginId.isEmpty())
        return;
    emit openRequested(pluginId);
}
