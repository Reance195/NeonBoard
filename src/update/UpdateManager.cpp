#include "UpdateManager.h"

#include "AppPaths.h"
#include "DeltaUpdate.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

namespace {

// Inno Setup 的 AppId（installer/neonboard.iss），卸载项键名为 {AppId}_is1。
// 更新功能必须能区分「安装版」与「便携版」：前者走静默安装包，后者要自替换文件，
// 判据只能是注册表里有没有这个卸载项 —— 看 exe 旁边有没有 uninstall 是不靠谱的。
constexpr auto kInnoAppId = "{8E3B6A2C-9F4D-4C1A-9B7E-2D5F6A8C1E30}_is1";
constexpr auto kUninstallRoot =
    "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
constexpr auto kUninstallRootUser =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

// 单个候选源的超时。清单不到 1 KB，6 秒足够。
// 关键不是「够不够快」，而是不能让某个挂住的源吃光整个检查预算：
// 国内 raw.githubusercontent.com 常年只挂不答，实测会一直占着连接直到系统 TCP 超时，
// 后面的 GitHub Releases API 兜底源就永远轮不到（v0.1.0 发布后踩的实际故障）。
constexpr int kSourceTimeoutMs = 6000;
// 产物下载不设总时长（几十 MB 在慢网下要几分钟），但必须有一条「多久没数据就判死」的线：
// 实测过 GitHub 资产地址在本机代理下连上就再没动静，界面永远停在「正在下载 0%」，
// 既没进度也没错误 —— 用户唯一能做的就是杀掉重来。60 秒没有任何字节即中止并报错。
constexpr int kDownloadStallMs = 60000;
// 同步下载（自检 / 命令行辅助路径）用的一次性总闸
constexpr int kDownloadTimeoutMs = 120000;
constexpr int kCacheTtlSeconds = 6 * 60 * 60;
// 启动自动检查的最小间隔：开一次程序打一次网络请求没有必要，
// 也不该让用户在飞机上被弹窗骚扰。
constexpr qint64 kStartupCheckIntervalSeconds = 20 * 60 * 60;

QString trimVersionPrefix(QString value)
{
    value = value.trimmed();
    if (!value.isEmpty() && (value.at(0) == QLatin1Char('v') || value.at(0) == QLatin1Char('V')))
        value.remove(0, 1);
    return value;
}

/** 拆成「主版本号」与「预发布标识」两段：0.2.0-beta.1 → ("0.2.0", "beta.1") */
QPair<QString, QString> splitVersion(const QString &raw)
{
    const QString value = trimVersionPrefix(raw);
    int cut = value.size();
    for (int i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == QLatin1Char('-') || c == QLatin1Char('+')) {
            cut = i;
            break;
        }
    }
    return {value.left(cut), cut < value.size() ? value.mid(cut + 1) : QString()};
}

/** 预发布段逐点比较：纯数字段按数值，其余按字典序（beta < rc 只是巧合，够用） */
int comparePrerelease(const QString &left, const QString &right)
{
    const QStringList a = left.split(QLatin1Char('.'));
    const QStringList b = right.split(QLatin1Char('.'));
    const int count = qMax(a.size(), b.size());
    for (int i = 0; i < count; ++i) {
        const QString x = i < a.size() ? a.at(i) : QString();
        const QString y = i < b.size() ? b.at(i) : QString();
        bool okA = false;
        bool okB = false;
        const int nx = x.toInt(&okA);
        const int ny = y.toInt(&okB);
        if (okA && okB) {
            if (nx != ny)
                return nx < ny ? -1 : 1;
        } else if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

QString sha256OfFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取文件：%1").arg(path);
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    // 安装包动辄几十 MB，分片喂进去，别一次性 readAll
    char buffer[65536];
    qint64 read = 0;
    while ((read = file.read(buffer, sizeof(buffer))) > 0)
        hash.addData(buffer, read);
    file.close();
    if (read < 0) {
        if (error)
            *error = QStringLiteral("读取文件出错：%1").arg(path);
        return QString();
    }
    return QString::fromLatin1(hash.result().toHex());
}

/** 从 GitHub Release 说明里抓发布脚本写下的校验值：
 *  形如 `setup-sha256: <64 位十六进制>`。update.json 里直接有字段，
 *  只有回退到 Releases API 时才需要这条路 —— GitHub 的 assets 不给 SHA-256。 */
QString sha256FromNotes(const QString &notes, const QString &kind)
{
    const QString pattern =
        QStringLiteral("%1-sha256\\s*[:=]\\s*([0-9a-fA-F]{64})").arg(kind);
    const QRegularExpression re(pattern);
    const QRegularExpressionMatch match = re.match(notes);
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

QString isoDateOnly(const QString &value)
{
    const int at = value.indexOf(QLatin1Char('T'));
    return at > 0 ? value.left(at) : value;
}

} // namespace

UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent)
{
    m_network = new QNetworkAccessManager(this);
    // 产物下载的「无数据看门狗」：每次收到字节就重新计时，超时即 abort，
    // 由 onReplyFinished 按网络错误收尾。单发定时器复用同一个实例。
    m_stallTimer = new QTimer(this);
    m_stallTimer->setSingleShot(true);
    m_stallTimer->setInterval(kDownloadStallMs);
    connect(m_stallTimer, &QTimer::timeout, this, [this]() {
        if (m_reply && m_reply->isRunning())
            m_reply->abort();
    });
    loadSettings();
    m_installKindCache = QString();
}

UpdateManager::~UpdateManager() = default;

QString UpdateManager::currentVersion() const
{
    const QString v = QCoreApplication::applicationVersion();
    return v.isEmpty() ? QStringLiteral("0.0.0") : v;
}

QString UpdateManager::uninstallKeyPath()
{
    return QStringLiteral("%1\\%2").arg(QLatin1String(kUninstallRootUser),
                                        QLatin1String(kInnoAppId));
}

QString UpdateManager::installKind() const
{
    // 注册表查询不便宜，且结果不会在进程生命周期内变化 —— 缓存一份。
    if (!m_installKindCache.isEmpty())
        return m_installKindCache;

    for (const char *root : {kUninstallRootUser, kUninstallRoot}) {
        QSettings entry(QLatin1String(root) + QLatin1String("\\") + QLatin1String(kInnoAppId),
                        QSettings::NativeFormat);
        if (!entry.value(QStringLiteral("DisplayName")).toString().isEmpty()
            || !entry.value(QStringLiteral("InstallLocation")).toString().isEmpty()) {
            m_installKindCache = QStringLiteral("installed");
            return m_installKindCache;
        }
    }
    m_installKindCache = QStringLiteral("portable");
    return m_installKindCache;
}

int UpdateManager::compareVersion(const QString &a, const QString &b)
{
    const auto left = splitVersion(a);
    const auto right = splitVersion(b);

    const QStringList av = left.first.split(QLatin1Char('.'));
    const QStringList bv = right.first.split(QLatin1Char('.'));
    const int count = qMax(av.size(), bv.size());
    for (int i = 0; i < count; ++i) {
        const int x = i < av.size() ? av.at(i).toInt() : 0;
        const int y = i < bv.size() ? bv.at(i).toInt() : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }

    // 主版本号相同：有预发布标识的更小（0.2.0-beta < 0.2.0）
    if (left.second.isEmpty() != right.second.isEmpty())
        return left.second.isEmpty() ? 1 : -1;
    if (!left.second.isEmpty())
        return comparePrerelease(left.second, right.second);
    return 0;
}

UpdateManager::ReleaseInfo UpdateManager::parseManifest(const QByteArray &json, QString *error)
{
    ReleaseInfo info;
    if (json.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("清单内容为空");
        return info;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("清单不是合法 JSON：%1").arg(parseError.errorString());
        return info;
    }

    QJsonObject root = doc.object();
    if (root.isEmpty() && doc.isArray()) {
        // /releases（数组形式）时取第一个非草稿、非预发布的条目
        for (const QJsonValue &value : doc.array()) {
            const QJsonObject item = value.toObject();
            if (item.value(QStringLiteral("draft")).toBool()
                || item.value(QStringLiteral("prerelease")).toBool())
                continue;
            root = item;
            break;
        }
    }
    if (root.isEmpty()) {
        if (error)
            *error = QStringLiteral("清单里没有版本信息");
        return info;
    }

    // 两种格式靠特征字段区分：Release API 有 tag_name，update.json 有 latest/artifacts
    if (root.contains(QLatin1String("tag_name"))) {
        info.version = trimVersionPrefix(root.value(QStringLiteral("tag_name")).toString());
        info.notes = root.value(QStringLiteral("body")).toString().trimmed();
        info.publishedAt = isoDateOnly(root.value(QStringLiteral("published_at")).toString());
        info.htmlUrl = root.value(QStringLiteral("html_url")).toString();
        for (const QJsonValue &value : root.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            const QString url = asset.value(QStringLiteral("browser_download_url")).toString();
            const qint64 size = static_cast<qint64>(asset.value(QStringLiteral("size")).toDouble());
            const QString lowered = name.toLower();
            if (lowered.contains(QLatin1String("setup"))) {
                info.setupUrl = url;
                info.setupSize = size;
                info.setupSha256 = sha256FromNotes(info.notes, QStringLiteral("setup"));
            } else if (lowered.endsWith(QLatin1String(".zip"))) {
                info.portableUrl = url;
                info.portableSize = size;
                info.portableSha256 = sha256FromNotes(info.notes, QStringLiteral("portable"));
            }
        }
    } else {
        info.version = trimVersionPrefix(root.value(QStringLiteral("latest")).toString());
        info.notes = root.value(QStringLiteral("notes")).toString().trimmed();
        info.publishedAt = isoDateOnly(root.value(QStringLiteral("publishedAt")).toString());
        info.htmlUrl = root.value(QStringLiteral("htmlUrl")).toString();
        for (const QJsonValue &value : root.value(QStringLiteral("artifacts")).toArray()) {
            const QJsonObject item = value.toObject();
            const QString kind = item.value(QStringLiteral("kind")).toString().toLower();
            const QString url = item.value(QStringLiteral("url")).toString();
            const QString sha = item.value(QStringLiteral("sha256")).toString().trimmed().toLower();
            const qint64 size = static_cast<qint64>(item.value(QStringLiteral("size")).toDouble());
            if (kind == QLatin1String("setup")) {
                info.setupUrl = url;
                info.setupSha256 = sha;
                info.setupSize = size;
            } else if (kind == QLatin1String("portable")) {
                info.portableUrl = url;
                info.portableSha256 = sha;
                info.portableSize = size;
            } else if (kind == QLatin1String("delta")) {
                info.deltaUrl = url;
                info.deltaSha256 = sha;
                info.deltaSize = size;
                info.deltaFromVersion = trimVersionPrefix(
                    item.value(QStringLiteral("fromVersion")).toString());
            }
        }
    }

    if (!info.valid()) {
        if (error)
            *error = QStringLiteral("清单里没有版本号");
    }
    return info;
}

QString UpdateManager::buildPortableUpdateScript(const QString &sourceDir, const QString &targetDir)
{
    // 脚本内容恒为 ASCII。cmd.exe 按本地代码页解析 bat 文件，
    // 中文注释或中文路径在 GBK 机器上会变成语法错误 —— 这是踩过的坑，
    // 所以这里连注释都写英文，路径一律走双引号包裹。
    QString waitName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    if (waitName.isEmpty())
        waitName = QStringLiteral("NeonBoard.exe"); // 无应用实例时（单测）退回主程序名
    const QString hostName = QStringLiteral("NeonBoardHost.exe");
    const QString exeName = QStringLiteral("NeonBoard.exe");

    QStringList lines;
    lines << QStringLiteral("@echo off");
    lines << QStringLiteral("setlocal");
    lines << QStringLiteral("rem NeonBoard portable self-update (generated, ASCII only)");
    lines << QStringLiteral("set \"SRC=%1\"").arg(QDir::toNativeSeparators(sourceDir));
    lines << QStringLiteral("set \"DST=%1\"").arg(QDir::toNativeSeparators(targetDir));
    lines << QStringLiteral("set \"LOG=%1\\neonboard-update.log\"")
                 .arg(QDir::toNativeSeparators(QStandardPaths::writableLocation(
                     QStandardPaths::TempLocation)));
    lines << QStringLiteral("echo update start %DATE% %TIME% > \"%LOG%\"");
    // 托盘宿主常驻但不持有数据，强制结束没有风险
    lines << QStringLiteral("taskkill /F /IM %1 >>\"%LOG%\" 2>&1").arg(hostName);
    // 等主进程自己退出（它启动本脚本后就 quit），不要反过来强杀它
    lines << QStringLiteral(":waitloop");
    lines << QStringLiteral("tasklist /FI \"IMAGENAME eq %1\" 2>NUL | find /I \"%1\" >NUL")
                 .arg(waitName);
    lines << QStringLiteral("if not errorlevel 1 (");
    lines << QStringLiteral("  ping -n 2 127.0.0.1 >NUL");
    lines << QStringLiteral("  goto waitloop");
    lines << QStringLiteral(")");
    lines << QStringLiteral("robocopy \"%SRC%\" \"%DST%\" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >>\"%LOG%\" 2>&1");
    // robocopy 的 0-7 都是成功，>=8 才是失败
    lines << QStringLiteral("if errorlevel 8 (");
    lines << QStringLiteral("  echo robocopy failed >>\"%LOG%\"");
    lines << QStringLiteral("  exit /b 1");
    lines << QStringLiteral(")");
    lines << QStringLiteral("if exist \"%DST%\\%1\" (").arg(hostName);
    lines << QStringLiteral("  start \"\" \"%DST%\\%1\" --open").arg(hostName);
    lines << QStringLiteral(") else (");
    lines << QStringLiteral("  start \"\" \"%DST%\\%1\"").arg(exeName);
    lines << QStringLiteral(")");
    lines << QStringLiteral("rmdir /S /Q \"%SRC%\" >>\"%LOG%\" 2>&1");
    lines << QStringLiteral("del /F /Q \"%~f0\"");
    lines << QString();
    return lines.join(QStringLiteral("\r\n"));
}

QString UpdateManager::locatePayloadRoot(const QString &extractDir)
{
    QDir dir(extractDir);
    if (dir.exists(QStringLiteral("NeonBoard.exe")) || dir.exists(QStringLiteral("NeonBoardHost.exe")))
        return QDir::toNativeSeparators(extractDir);

    // zip 里多包了一层目录（NeonBoard-0.2.0/NeonBoard.exe）：逐个子目录找一层
    for (const QFileInfo &entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QDir sub(entry.absoluteFilePath());
        if (sub.exists(QStringLiteral("NeonBoard.exe"))
            || sub.exists(QStringLiteral("NeonBoardHost.exe")))
            return QDir::toNativeSeparators(entry.absoluteFilePath());
    }
    return QDir::toNativeSeparators(extractDir);
}

QString UpdateManager::resolveUrl(const QString &url, const QString &baseUrl)
{
    const QString text = url.trimmed();
    if (text.isEmpty() || text.contains(QLatin1String("://")))
        return text;
    const QUrl base(baseUrl);
    if (!base.isValid() || base.path().isEmpty())
        return text;
    return base.resolved(QUrl(text)).toString();
}

QString UpdateManager::defaultSource()
{
    // 与插件市场同一套取舍：Gitee raw 国内可达，GitHub raw 兜底。
    return QStringLiteral("gitee+github://Reance195/NeonBoard");
}

QString UpdateManager::cachePath()
{
    return AppPaths::dataDir() + QStringLiteral("/update-cache.json");
}

void UpdateManager::loadSettings()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("updates"));
    m_autoCheck = settings.value(QStringLiteral("autoCheck"), true).toBool();
    m_skippedVersion = settings.value(QStringLiteral("skipVersion")).toString();
    m_lastCheckAt = settings.value(QStringLiteral("lastCheckAt"), 0).toLongLong();
    const QStringList stored = settings.value(QStringLiteral("sources")).toStringList();
    settings.endGroup();

    m_sources = stored.isEmpty() ? QStringList{defaultSource()} : stored;
}

void UpdateManager::saveSources()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("updates"));
    settings.setValue(QStringLiteral("sources"), m_sources);
    settings.endGroup();
    emit sourcesChanged();
}

void UpdateManager::setAutoCheck(bool enabled)
{
    if (enabled == m_autoCheck)
        return;
    m_autoCheck = enabled;
    QSettings settings;
    settings.beginGroup(QStringLiteral("updates"));
    settings.setValue(QStringLiteral("autoCheck"), enabled);
    settings.endGroup();
    emit autoCheckChanged();
}

void UpdateManager::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

void UpdateManager::setStatusText(const QString &text)
{
    if (text == m_statusText)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void UpdateManager::setProgress(int value)
{
    if (value == m_progress)
        return;
    m_progress = value;
    emit progressChanged();
}

QStringList UpdateManager::candidateUrls() const
{
    QStringList urls;
    for (const QString &spec : m_sources) {
        const QString value = spec.trimmed();
        if (value.isEmpty())
            continue;
        if (value.startsWith(QLatin1String("gitee+github://"))) {
            const QString repo = value.mid(QStringLiteral("gitee+github://").size());
            urls << QStringLiteral("https://gitee.com/%1/raw/main/update.json").arg(repo);
            urls << QStringLiteral("https://raw.githubusercontent.com/%1/main/update.json").arg(repo);
        } else if (QFileInfo::exists(value)) {
            // 本地 update.json：自建源与离线验证都靠这条路
            urls << QUrl::fromLocalFile(value).toString();
        } else if (!value.contains(QLatin1String("://"))) {
            // 简写 owner/repo → 两个 raw 源
            urls << QStringLiteral("https://gitee.com/%1/raw/main/update.json").arg(value);
            urls << QStringLiteral("https://raw.githubusercontent.com/%1/main/update.json").arg(value);
        } else {
            urls << value;
        }
    }
    // 兜底：仓库本身的 Release API，任何托管方式都能拿到最新版本
    urls << QStringLiteral("https://api.github.com/repos/Reance195/NeonBoard/releases/latest");
    return urls;
}

void UpdateManager::checkForUpdates(bool silent)
{
    if (m_state == QLatin1String("checking") || m_state == QLatin1String("downloading")
        || m_state == QLatin1String("installing"))
        return;

    m_silent = silent;
    m_candidates = candidateUrls();
    m_candidateIndex = 0;
    setState(QStringLiteral("checking"));
    setProgress(0);
    setStatusText(QStringLiteral("正在检查更新…"));
    
    // 并行竞速：所有候选源同时发起请求，首个成功者胜出
    m_activeManifestReplies.clear();
    for (const QString &url : m_candidates) {
        if (url.isEmpty())
            continue;
        startManifestRequest(url);
    }
    
    // 所有源都为空（理论上不会发生）
    if (m_activeManifestReplies.isEmpty()) {
        if (useCachedManifest())
            return;
        setStatusText(QStringLiteral("检查更新失败：无法连接更新服务器"));
        setState(QStringLiteral("error"));
        finishCheck(false);
    }
}

void UpdateManager::startManifestRequest(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NeonBoard-UpdateManager"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    m_activeManifestReplies.append(reply);

    // 单源看门狗：6 秒无响应就 abort，按网络错误处理
    QTimer::singleShot(kSourceTimeoutMs, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply] { 
        onManifestReplyFinished(reply); 
    });
}

void UpdateManager::startRequest(const QString &url, Purpose purpose)
{
    if (url.isEmpty()) {
        if (purpose == Purpose::Manifest) {
            if (useCachedManifest())
                return;
            setStatusText(QStringLiteral("检查更新失败：无法连接更新服务器"));
            setState(QStringLiteral("error"));
            finishCheck(false);
        }
        return;
    }

    m_purpose = purpose;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NeonBoard-UpdateManager"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;

    if (purpose == Purpose::Manifest) {
        // 单源看门狗：到点主动 abort，onReplyFinished 里按「网络错误」处理并换下一个候选源。
        // 没有它的话，异步检查（设置面板、启动静默检查）会一直挂在那个源上，
        // 既拿不到失败回调，也走不到兜底源。产物下载走另一套「无数据」看门狗（见 m_stallTimer）。
        QTimer::singleShot(kSourceTimeoutMs, reply, [reply]() {
            if (reply->isRunning())
                reply->abort();
        });
        m_stallTimer->stop();
    } else {
        m_stallTimer->start();
    }

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (m_purpose != Purpose::Artifact)
                    return;
                m_stallTimer->start();  // 还在收数据就续命
                if (total <= 0)
                    return;
                setProgress(static_cast<int>(received * 100 / total));
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onReplyFinished(reply); });
}

void UpdateManager::onManifestReplyFinished(QNetworkReply *reply)
{
    if (!reply || !m_activeManifestReplies.contains(reply))
        return;
    
    reply->deleteLater();
    m_activeManifestReplies.removeOne(reply);
    
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool networkError = reply->error() != QNetworkReply::NoError;
    const QByteArray payload = reply->readAll();
    
    // 成功：取消其他请求，处理清单
    if (!networkError && status < 400 && !payload.isEmpty()) {
        // 取消其他活动的请求
        for (QNetworkReply *other : m_activeManifestReplies) {
            if (other->isRunning())
                other->abort();
        }
        m_activeManifestReplies.clear();
        handleManifest(payload, reply->url().toString());
        return;
    }
    
    // 失败：检查是否所有请求都失败了
    if (m_activeManifestReplies.isEmpty()) {
        // 所有源都失败了，降级到缓存
        if (useCachedManifest())
            return;
        
        // abort() 触发的取消要说人话
        const QString reason = reply->error() == QNetworkReply::OperationCanceledError
            ? QStringLiteral("连接超时（%1 秒无响应）").arg(kSourceTimeoutMs / 1000)
            : (networkError ? reply->errorString()
                            : QStringLiteral("服务器返回 %1").arg(status));
        setStatusText(QStringLiteral("检查更新失败：%1").arg(reason));
        setState(QStringLiteral("error"));
        finishCheck(false);
    }
    // 还有其他请求在跑，等它们
}

void UpdateManager::onReplyFinished(QNetworkReply *reply)
{
    if (!reply)
        return;
    m_stallTimer->stop();
    reply->deleteLater();
    if (m_reply == reply)
        m_reply = nullptr;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool networkError = reply->error() != QNetworkReply::NoError;
    const QByteArray payload = reply->readAll();

    if (m_purpose == Purpose::Manifest) {
        if (!networkError && status < 400 && !payload.isEmpty()) {
            handleManifest(payload, reply->url().toString());
            return;
        }
        // 换下一个候选地址；都试完再降级到缓存
        ++m_candidateIndex;
        if (m_candidateIndex < m_candidates.size()) {
            startRequest(m_candidates.value(m_candidateIndex), Purpose::Manifest);
            return;
        }
        if (useCachedManifest())
            return;
        // abort() 触发的取消要说人话：直接甩 "Operation canceled" 只会让用户更困惑
        const QString reason = reply->error() == QNetworkReply::OperationCanceledError
            ? QStringLiteral("连接超时（%1 秒无响应）").arg(kSourceTimeoutMs / 1000)
            : (networkError ? reply->errorString()
                            : QStringLiteral("服务器返回 %1").arg(status));
        setStatusText(QStringLiteral("检查更新失败：%1").arg(reason));
        setState(QStringLiteral("error"));
        finishCheck(false);
        return;
    }

    // 产物下载
    if (networkError || status >= 400 || payload.isEmpty()) {
        // abort() 触发的取消要说人话：产物下载是被「无数据看门狗」掐掉的，
        // 直甩 Operation canceled 用户只会看到一串英文
        const QString reason = reply->error() == QNetworkReply::OperationCanceledError
            ? QStringLiteral("下载超时（%1 秒没有收到数据）").arg(kDownloadStallMs / 1000)
            : (networkError ? reply->errorString()
                            : QStringLiteral("服务器返回 %1").arg(status));
        setStatusText(QStringLiteral("下载失败：%1").arg(reason));
        setState(QStringLiteral("error"));
        emit errorOccurred(m_statusText);
        return;
    }

    const QString dest = m_pendingArtifact;
    setState(QStringLiteral("verifying"));
    setStatusText(QStringLiteral("正在校验安装包…"));
    // 先落盘再校验。v0.1.0/v0.1.1 这里漏了写文件这一步：下载读进内存后直接去校验一个
    // 还不存在的路径，表现是「进度条到 100% → 无法读取文件：…」然后中止，安装永远走不到。
    // 用 QSaveFile 写入，半截文件不会留在磁盘上被当成有效产物。
    {
        QSaveFile out(dest);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString msg = QStringLiteral("无法写入安装包：%1").arg(out.errorString());
            setStatusText(msg);
            setState(QStringLiteral("error"));
            emit errorOccurred(msg);
            return;
        }
        if (out.write(payload) != payload.size() || !out.commit()) {
            const QString msg = QStringLiteral("安装包写入不完整：%1").arg(dest);
            setStatusText(msg);
            setState(QStringLiteral("error"));
            emit errorOccurred(msg);
            return;
        }
    }

    QString verifyError;
    if (!verifyFile(dest, artifactSha256(), &verifyError)) {
        QFile::remove(dest);
        setStatusText(verifyError);
        setState(QStringLiteral("error"));
        emit errorOccurred(verifyError);
        return;
    }

    setProgress(100);
    setState(QStringLiteral("installing"));
    if (installKind() == QLatin1String("installed"))
        installWithSetup(dest);
    else
        installPortable(dest);
}

void UpdateManager::handleManifest(const QByteArray &payload, const QString &fromUrl)
{
    QString parseError;
    ReleaseInfo info = parseManifest(payload, &parseError);
    if (!info.valid()) {
        // 并行竞速模式下，所有源都已尝试过，解析失败即整体失败
        setStatusText(QStringLiteral("更新清单解析失败：%1").arg(parseError));
        setState(QStringLiteral("error"));
        finishCheck(false);
        return;
    }

    // 清单里只写文件名（同一目录下分发）时补全成绝对地址
    info.setupUrl = resolveUrl(info.setupUrl, fromUrl);
    info.portableUrl = resolveUrl(info.portableUrl, fromUrl);
    info.deltaUrl = resolveUrl(info.deltaUrl, fromUrl);

    storeCachedManifest(fromUrl, payload);
    m_latest = info;
    m_lastCheckAt = QDateTime::currentSecsSinceEpoch();
    QSettings settings;
    settings.beginGroup(QStringLiteral("updates"));
    settings.setValue(QStringLiteral("lastCheckAt"), m_lastCheckAt);
    settings.endGroup();
    emit lastCheckChanged();

    const int cmp = compareVersion(info.version, currentVersion());
    // 可用 = 有当前形态的全量产物，或有恰好适用的增量包（增量失败时若全量产物
    // 存在仍可回退；只有增量没有全量时，增量失败就报错，不会装错包）
    const bool hasKindArtifact = installKind() == QLatin1String("installed")
        ? !info.setupUrl.isEmpty() : !info.portableUrl.isEmpty();
    m_updateAvailable = cmp > 0 && info.version != m_skippedVersion
        && (hasKindArtifact || info.deltaApplicable(currentVersion()));

    if (cmp <= 0) {
        setState(QStringLiteral("uptodate"));
        setStatusText(QStringLiteral("已是最新版本 %1").arg(currentVersion()));
    } else if (m_updateAvailable) {
        setState(QStringLiteral("available"));
        setStatusText(QStringLiteral("发现新版本 %1").arg(info.version));
        emit updateFound(info.version);
    } else {
        // 版本号更新了，但清单里没有对应形态的产物 —— 只提示下载页
        setState(QStringLiteral("uptodate"));
        setStatusText(QStringLiteral("新版本 %1 暂未提供当前安装方式的更新包").arg(info.version));
    }
    emit infoChanged();
    finishCheck(true);
}

bool UpdateManager::useCachedManifest()
{
    QFile file(cachePath());
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    const qint64 fetchedAt = static_cast<qint64>(root.value(QStringLiteral("fetchedAt")).toDouble());
    const QByteArray raw = root.value(QStringLiteral("manifest")).toString().toUtf8();
    if (raw.isEmpty())
        return false;

    QString parseError;
    ReleaseInfo info = parseManifest(raw, &parseError);
    if (!info.valid())
        return false;
    info.setupUrl = resolveUrl(info.setupUrl, root.value(QStringLiteral("url")).toString());
    info.portableUrl = resolveUrl(info.portableUrl, root.value(QStringLiteral("url")).toString());
    info.deltaUrl = resolveUrl(info.deltaUrl, root.value(QStringLiteral("url")).toString());

    m_latest = info;
    m_updateAvailable = compareVersion(info.version, currentVersion()) > 0
        && info.version != m_skippedVersion
        && ((installKind() == QLatin1String("installed") ? !info.setupUrl.isEmpty()
                                                          : !info.portableUrl.isEmpty())
            || info.deltaApplicable(currentVersion()));
    setStatusText(QStringLiteral("离线：使用 %1 缓存的更新信息")
                      .arg(QDateTime::fromSecsSinceEpoch(fetchedAt).toString(QStringLiteral("MM-dd hh:mm"))));
    setState(m_updateAvailable ? QStringLiteral("available") : QStringLiteral("uptodate"));
    emit infoChanged();
    finishCheck(true);
    return true;
}

void UpdateManager::storeCachedManifest(const QString &url, const QByteArray &payload)
{
    QJsonObject root;
    root.insert(QStringLiteral("fetchedAt"),
                static_cast<double>(QDateTime::currentSecsSinceEpoch()));
    root.insert(QStringLiteral("url"), url);
    root.insert(QStringLiteral("manifest"), QString::fromUtf8(payload));
    QFile file(cachePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

void UpdateManager::finishCheck(bool ok)
{
    emit checkCompleted(ok);
    if (!ok && !m_silent)
        emit errorOccurred(m_statusText);
}

QString UpdateManager::artifactUrl() const
{
    return installKind() == QLatin1String("installed") ? m_latest.setupUrl : m_latest.portableUrl;
}

QString UpdateManager::artifactSha256() const
{
    return installKind() == QLatin1String("installed") ? m_latest.setupSha256
                                                       : m_latest.portableSha256;
}

qint64 UpdateManager::releaseSize() const
{
    return installKind() == QLatin1String("installed") ? m_latest.setupSize : m_latest.portableSize;
}

void UpdateManager::downloadAndInstall()
{
    if (state() != QLatin1String("available"))
        return;

    // 优先增量（仅当前版本恰好等于增量源版本时）；任一步失败自动回退全量下载
    if (m_latest.deltaApplicable(currentVersion())) {
        setState(QStringLiteral("downloading"));
        setProgress(0);
        setStatusText(QStringLiteral("正在下载增量包 %1…").arg(m_latest.version));
        QString deltaError;
        if (downloadAndApplyDelta(&deltaError))
            return; // 成功：installLaunched 已发出，宿主准备退出
        qWarning() << "增量更新失败，回退全量下载：" << deltaError;
        setStatusText(QStringLiteral("增量更新失败，改用全量下载…"));
    }

    const QString url = artifactUrl();
    if (url.isEmpty()) {
        openDownloadPage();
        return;
    }

    const QString suffix = installKind() == QLatin1String("installed")
        ? QStringLiteral(".exe") : QStringLiteral(".zip");
    m_pendingArtifact = QDir::temp().absoluteFilePath(
        QStringLiteral("neonboard-update-%1%2").arg(m_latest.version, suffix));
    QFile::remove(m_pendingArtifact);

    setState(QStringLiteral("downloading"));
    setProgress(0);
    setStatusText(QStringLiteral("正在下载 %1…").arg(m_latest.version));
    startRequest(url, Purpose::Artifact);
}

void UpdateManager::installWithSetup(const QString &file)
{
    // Inno Setup 的静默参数：不弹窗、不重启、装完由 [Run] 段自己拉起宿主。
    // PrivilegesRequired=lowest，装到 %LOCALAPPDATA%，不需要管理员。
    const QStringList args{QStringLiteral("/VERYSILENT"),
                           QStringLiteral("/NORESTART"),
                           QStringLiteral("/SUPPRESSMSGBOXES"),
                           QStringLiteral("/CLOSEAPPLICATIONS")};
    setStatusText(QStringLiteral("正在安装更新…"));
    const bool started = QProcess::startDetached(file, args);
    if (!started) {
        setStatusText(QStringLiteral("启动安装程序失败"));
        setState(QStringLiteral("error"));
        emit errorOccurred(m_statusText);
        return;
    }
    emit installLaunched();
}

bool UpdateManager::downloadAndApplyDelta(QString *error)
{
    if (!m_latest.valid()) {
        if (error)
            *error = QStringLiteral("无可用更新信息");
        return false;
    }
    // 增量包只在当前版本恰好等于源版本时可用，否则回退全量
    if (!m_latest.deltaApplicable(currentVersion())) {
        if (error)
            *error = QStringLiteral("增量包不适用（当前版本与源版本不一致）");
        return false;
    }

    const QString tempRoot = QDir::temp().absoluteFilePath(
        QStringLiteral("neonboard-delta-%1").arg(m_latest.version));
    QDir(tempRoot).removeRecursively();
    if (!QDir().mkpath(tempRoot)) {
        if (error)
            *error = QStringLiteral("无法创建临时目录：%1").arg(tempRoot);
        return false;
    }
    const QString deltaFile = tempRoot + QStringLiteral("/delta.zip");
    const QString extractDir = tempRoot + QStringLiteral("/extract");
    const QString stagedDir = tempRoot + QStringLiteral("/staged");

    // 1) 下载增量包（同步；进度照常走 progress 信号，嵌套事件循环会派发）
    QNetworkRequest request(QUrl(m_latest.deltaUrl));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("NeonBoard-UpdateManager"));
    QNetworkReply *reply = m_network->get(request);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total > 0)
                    setProgress(static_cast<int>(received * 100 / total));
            });
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeoutTimer.start(120000); // 增量包通常几 MB，总超时 120 秒
    loop.exec();

    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorText = reply->errorString();
    reply->deleteLater();
    if (netError != QNetworkReply::NoError) {
        if (error)
            *error = QStringLiteral("下载增量包失败：%1").arg(netErrorText);
        QDir(tempRoot).removeRecursively();
        return false;
    }

    // 2) 校验 SHA-256（清单给了才校验）
    if (!m_latest.deltaSha256.isEmpty()) {
        const QString actual = QString::fromLatin1(
            QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        if (actual.toLower() != m_latest.deltaSha256.toLower()) {
            if (error)
                *error = QStringLiteral("增量包校验失败");
            QDir(tempRoot).removeRecursively();
            return false;
        }
    }

    QFile outFile(deltaFile);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || outFile.write(payload) != payload.size()) {
        if (error)
            *error = QStringLiteral("无法写入增量包：%1").arg(outFile.errorString());
        QDir(tempRoot).removeRecursively();
        return false;
    }
    outFile.close();

    // 3) 解压
    QString stepError;
    if (!extractZip(deltaFile, extractDir, &stepError)) {
        if (error)
            *error = QStringLiteral("解压增量包失败：%1").arg(stepError);
        QDir(tempRoot).removeRecursively();
        return false;
    }

    // 4) 应用：资源直接覆盖安装目录，二进制 bsdiff 合并到暂存目录；失败自动回滚
    const QString installDir = QCoreApplication::applicationDirPath();
    if (!DeltaUpdate::applyDelta(extractDir, installDir, stagedDir, &stepError)) {
        if (error)
            *error = QStringLiteral("应用增量失败：%1").arg(stepError);
        QDir(tempRoot).removeRecursively();
        return false;
    }

    // 5) 自更新脚本：进程退出后把暂存二进制拷入安装目录并重启
    if (!launchDeltaSelfUpdater(stagedDir, installDir, &stepError)) {
        if (error)
            *error = stepError;
        QDir(tempRoot).removeRecursively();
        return false;
    }

    // 成功：tempRoot 保留给自更新脚本（staged 拷完由脚本自行清理）
    setStatusText(QStringLiteral("更新完成，正在重启…"));
    emit installLaunched();
    return true;
}

bool UpdateManager::launchDeltaSelfUpdater(const QString &stagedDir,
                                           const QString &targetDir, QString *error)
{
    const QString scriptPath = QDir::temp().absoluteFilePath(
        QStringLiteral("neonboard-deltaupdate-%1.bat").arg(m_latest.version));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入更新脚本：%1").arg(scriptPath);
        return false;
    }
    // bat 必须是 CRLF + 纯 ASCII，否则中文路径的机器上 cmd 解析会炸
    script.write(buildDeltaUpdateScript(stagedDir, targetDir).toLatin1());
    script.close();

    const bool started = QProcess::startDetached(QStringLiteral("cmd.exe"),
                                                  {QStringLiteral("/c"), scriptPath});
    if (!started) {
        if (error)
            *error = QStringLiteral("无法启动更新脚本");
        return false;
    }
    return true;
}

QString UpdateManager::buildDeltaUpdateScript(const QString &stagedDir,
                                              const QString &targetDir)
{
    // 与便携版自更新脚本同构：恒为 ASCII，路径双引号包裹。
    // 区别只在拷贝源：这里只拷暂存目录里的二进制（资源已在应用阶段直接覆盖）。
    QString waitName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    if (waitName.isEmpty())
        waitName = QStringLiteral("NeonBoard.exe"); // 无应用实例时（单测）退回主程序名
    const QString hostName = QStringLiteral("NeonBoardHost.exe");
    const QString exeName = QStringLiteral("NeonBoard.exe");

    QStringList lines;
    lines << QStringLiteral("@echo off");
    lines << QStringLiteral("setlocal");
    lines << QStringLiteral("rem NeonBoard delta self-update (generated, ASCII only)");
    lines << QStringLiteral("set \"SRC=%1\"").arg(QDir::toNativeSeparators(stagedDir));
    lines << QStringLiteral("set \"DST=%1\"").arg(QDir::toNativeSeparators(targetDir));
    lines << QStringLiteral("set \"LOG=%1\\neonboard-delta-update.log\"")
                 .arg(QDir::toNativeSeparators(QStandardPaths::writableLocation(
                     QStandardPaths::TempLocation)));
    lines << QStringLiteral("echo delta update start %DATE% %TIME% > \"%LOG%\"");
    // 托盘宿主常驻但不持有数据，强制结束没有风险
    lines << QStringLiteral("taskkill /F /IM %1 >>\"%LOG%\" 2>&1").arg(hostName);
    // 等主进程自己退出（它启动本脚本后就 quit），不要反过来强杀它
    lines << QStringLiteral(":waitloop");
    lines << QStringLiteral("tasklist /FI \"IMAGENAME eq %1\" 2>NUL | find /I \"%1\" >NUL")
                 .arg(waitName);
    lines << QStringLiteral("if not errorlevel 1 (");
    lines << QStringLiteral("  ping -n 2 127.0.0.1 >NUL");
    lines << QStringLiteral("  goto waitloop");
    lines << QStringLiteral(")");
    lines << QStringLiteral("robocopy \"%SRC%\" \"%DST%\" /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >>\"%LOG%\" 2>&1");
    // robocopy 的 0-7 都是成功，>=8 才是失败
    lines << QStringLiteral("if errorlevel 8 (");
    lines << QStringLiteral("  echo robocopy failed >>\"%LOG%\"");
    lines << QStringLiteral("  exit /b 1");
    lines << QStringLiteral(")");
    lines << QStringLiteral("if exist \"%DST%\\%1\" (").arg(hostName);
    lines << QStringLiteral("  start \"\" \"%DST%\\%1\" --open").arg(hostName);
    lines << QStringLiteral(") else (");
    lines << QStringLiteral("  start \"\" \"%DST%\\%1\"").arg(exeName);
    lines << QStringLiteral(")");
    lines << QStringLiteral("rmdir /S /Q \"%SRC%\" >>\"%LOG%\" 2>&1");
    lines << QStringLiteral("del /F /Q \"%~f0\"");
    lines << QString();
    return lines.join(QStringLiteral("\r\n"));
}

void UpdateManager::installPortable(const QString &zipFile)
{
    QString error;
    const QString extractDir = QDir::temp().absoluteFilePath(
        QStringLiteral("neonboard-payload-%1").arg(m_latest.version));
    QDir(extractDir).removeRecursively();

    setState(QStringLiteral("installing"));
    setStatusText(QStringLiteral("正在解压更新包…"));
    if (!extractZip(zipFile, extractDir, &error)) {
        setStatusText(error);
        setState(QStringLiteral("error"));
        emit errorOccurred(error);
        return;
    }

    const QString payloadRoot = locatePayloadRoot(extractDir);
    if (!launchSelfUpdater(payloadRoot, &error)) {
        setStatusText(error);
        setState(QStringLiteral("error"));
        emit errorOccurred(error);
        return;
    }
    setStatusText(QStringLiteral("已启动更新，程序即将退出"));
    emit installLaunched();
}

bool UpdateManager::extractZip(const QString &zipFile, const QString &destDir, QString *error)
{
    if (!QDir().mkpath(destDir)) {
        if (error)
            *error = QStringLiteral("无法创建解压目录：%1").arg(destDir);
        return false;
    }

    // 优先用系统自带 tar（Win10 17063+），比拉起 PowerShell 快一个数量级；
    // 老机器上没有 tar 时退回 Expand-Archive。
    QProcess tar;
    tar.start(QStringLiteral("tar"),
              {QStringLiteral("-xf"), QDir::toNativeSeparators(zipFile),
               QStringLiteral("-C"), QDir::toNativeSeparators(destDir)});
    if (tar.waitForFinished(60000) && tar.exitCode() == 0)
        return true;

    QProcess ps;
    ps.start(QStringLiteral("powershell"),
             {QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
              QStringLiteral("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                  .arg(zipFile, destDir)});
    if (!ps.waitForFinished(120000) || ps.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("解压失败：%1")
                         .arg(QString::fromUtf8(ps.readAllStandardError()).trimmed());
        return false;
    }
    return true;
}

bool UpdateManager::launchSelfUpdater(const QString &payloadDir, QString *error)
{
    const QString targetDir = QCoreApplication::applicationDirPath();
    const QString scriptPath = QDir::temp().absoluteFilePath(
        QStringLiteral("neonboard-selfupdate-%1.bat").arg(m_latest.version));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入更新脚本：%1").arg(scriptPath);
        return false;
    }
    // bat 必须是 CRLF + 纯 ASCII，否则中文路径的机器上 cmd 解析会炸
    script.write(buildPortableUpdateScript(payloadDir, targetDir).toLatin1());
    script.close();

    const bool started = QProcess::startDetached(QStringLiteral("cmd.exe"),
                                                {QStringLiteral("/c"), scriptPath});
    if (!started) {
        if (error)
            *error = QStringLiteral("无法启动更新脚本");
        return false;
    }
    return true;
}

bool UpdateManager::verifyFile(const QString &path, const QString &expectedSha256, QString *error)
{
    if (expectedSha256.trimmed().isEmpty()) {
        // 清单没给校验值（例如回退到 Releases API 且 Release 说明里也没写）：
        // 不静默放行 —— 明确告知，让用户自己决定要不要装。
        if (error)
            *error = QStringLiteral("更新清单未提供 SHA-256 校验值，已中止安装");
        return false;
    }
    QString hashError;
    const QString actual = sha256OfFile(path, &hashError);
    if (actual.isEmpty()) {
        if (error)
            *error = hashError;
        return false;
    }
    if (actual != expectedSha256.trimmed().toLower()) {
        if (error)
            *error = QStringLiteral("校验失败：文件 SHA-256 与清单不一致");
        return false;
    }
    return true;
}

void UpdateManager::openDownloadPage()
{
    const QString url = m_latest.htmlUrl.isEmpty()
        ? QStringLiteral("https://github.com/Reance195/NeonBoard/releases/latest")
        : m_latest.htmlUrl;
    QDesktopServices::openUrl(QUrl(url));
}

void UpdateManager::skipVersion(const QString &version)
{
    const QString value = version.isEmpty() ? m_latest.version : version;
    if (value.isEmpty())
        return;
    m_skippedVersion = value;
    QSettings settings;
    settings.beginGroup(QStringLiteral("updates"));
    settings.setValue(QStringLiteral("skipVersion"), value);
    settings.endGroup();
    m_updateAvailable = false;
    setState(QStringLiteral("idle"));
    emit infoChanged();
    emit skippedVersionChanged();
}

void UpdateManager::setUpdateSource(const QString &url)
{
    if (url.trimmed().isEmpty())
        return;
    if (m_sources.isEmpty())
        m_sources.append(url.trimmed());
    else
        m_sources[0] = url.trimmed();
    saveSources();
}

bool UpdateManager::addUpdateSource(const QString &url)
{
    const QString value = url.trimmed();
    if (value.isEmpty() || m_sources.contains(value))
        return false;
    m_sources.append(value);
    saveSources();
    return true;
}

bool UpdateManager::removeUpdateSource(int index)
{
    if (index < 0 || index >= m_sources.size())
        return false;
    m_sources.removeAt(index);
    saveSources();
    return true;
}

void UpdateManager::startupCheck()
{
    if (!m_autoCheck)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_lastCheckAt != 0 && now - m_lastCheckAt < kStartupCheckIntervalSeconds)
        return;
    checkForUpdates(true);
}

bool UpdateManager::checkBlocking(int timeoutMs, QString *error)
{
    QEventLoop loop;
    bool finished = false;
    bool ok = false;
    // 无论成功失败都要退出等待：网络不可达时 checkCompleted 同样会发
    const auto connection = connect(this, &UpdateManager::checkCompleted, this,
                                    [&](bool result) {
                                        ok = result;
                                        finished = true;
                                        loop.quit();
                                    });
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs > 0 ? timeoutMs : kDefaultCheckTimeoutMs);

    checkForUpdates(true);
    if (!finished)
        loop.exec();
    disconnect(connection);

    if (!finished) {
        if (error)
            *error = QStringLiteral("检查超时");
        return false;
    }
    if (!ok) {
        if (error)
            *error = m_statusText;
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool UpdateManager::downloadBlocking(const QString &destFile, QString *error)
{
    if (state() != QLatin1String("available")) {
        if (error)
            *error = QStringLiteral("当前没有可用更新");
        return false;
    }
    const QString url = artifactUrl();
    if (url.isEmpty()) {
        if (error)
            *error = QStringLiteral("清单缺少当前安装方式的更新包");
        return false;
    }

    QEventLoop loop;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NeonBoard-UpdateManager"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(kDownloadTimeoutMs);
    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        if (error)
            *error = QStringLiteral("下载超时");
        return false;
    }
    const QByteArray payload = reply->readAll();
    const bool failed = reply->error() != QNetworkReply::NoError;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString message = reply->errorString();
    reply->deleteLater();
    if (failed || status >= 400 || payload.isEmpty()) {
        if (error)
            *error = QStringLiteral("下载失败：%1").arg(message);
        return false;
    }

    QFile file(destFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入文件：%1").arg(destFile);
        return false;
    }
    file.write(payload);
    file.close();

    return verifyFile(destFile, artifactSha256(), error);
}
