#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * 软件更新：清单拉取 → 版本比较 → 下载校验 → 安装 / 自替换。
 *
 * 清单来源两路，前者优先、后者兜底：
 *   1. update.json（可配地址，推荐托管在 Gitee raw，国内可达）
 *   2. GitHub Releases API（/releases/latest，无需额外维护）
 *
 * update.json 格式：
 * {
 *   "version": 1,
 *   "channel": "stable",
 *   "latest": "0.2.0",
 *   "publishedAt": "2026-10-26",
 *   "notes": "……",
 *   "htmlUrl": "https://github.com/Reance195/NeonBoard/releases/latest",
 *   "artifacts": [
 *     { "kind": "setup",    "url": "…", "sha256": "…", "size": 123 },
 *     { "kind": "portable", "url": "…", "sha256": "…", "size": 456 }
 *   ]
 * }
 *
 * 安装形态（installKind）：
 *   installed —— 注册表里有 Inno Setup 卸载项，下载 Setup 后走 /VERYSILENT 静默升级；
 *   portable  —— 便携目录，下载 zip 后由自更新脚本在主程序退出后覆盖文件再拉起。
 *
 * 纯逻辑（版本比较 / 清单解析 / 脚本生成）一律做成静态函数：不依赖网络与注册表，
 * 既方便离线单测，也让「清单换了格式」这类问题定位在解析层而不是网络层。
 */
class UpdateManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString installKind READ installKind CONSTANT)
    // idle / checking / uptodate / available / downloading / verifying / installing / error
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY infoChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY infoChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY infoChanged)
    Q_PROPERTY(QString publishedAt READ publishedAt NOTIFY infoChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY infoChanged)
    Q_PROPERTY(qint64 releaseSize READ releaseSize NOTIFY infoChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QStringList updateSources READ updateSources NOTIFY sourcesChanged)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)
    Q_PROPERTY(QString skippedVersion READ skippedVersion NOTIFY skippedVersionChanged)
    Q_PROPERTY(qint64 lastCheckAt READ lastCheckAt NOTIFY lastCheckChanged)

public:
    explicit UpdateManager(QObject *parent = nullptr);
    ~UpdateManager() override;

    /**
     * 一次更新检查的总预算（毫秒）。
     *
     * 候选源是串行的，单源还有自己的 6 秒看门狗（kSourceTimeoutMs），
     * 所以最坏情况 = 3 个源 × 6 秒 ≈ 18 秒，留到 25 秒。
     * 这个值必须大于「源个数 × 单源超时」，否则兜底源永远排不上队 ——
     * 之前 15 秒配 10 秒单源超时，raw.githubusercontent 一挂住，
     * 后面的 GitHub Releases API 就再也轮不到，检查恒定报超时。
     */
    static constexpr int kDefaultCheckTimeoutMs = 25000;

    /** 一次清单解析的结果 */
    struct ReleaseInfo {
        QString version;
        QString notes;
        QString publishedAt;
        QString htmlUrl;
        QString setupUrl;
        QString setupSha256;
        QString portableUrl;
        QString portableSha256;
        qint64 setupSize = 0;
        qint64 portableSize = 0;

        bool valid() const { return !version.isEmpty(); }
    };

    // ---- 纯逻辑（可离线单测）----

    /** 语义化版本比较：a > b 返回 1，相等返回 0，a < b 返回 -1。
     *  容忍 v 前缀与预发布后缀（0.2.0-beta.1 < 0.2.0）。 */
    static int compareVersion(const QString &a, const QString &b);
    /** 同时兼容 update.json 与 GitHub Releases API 两种响应体 */
    static ReleaseInfo parseManifest(const QByteArray &json, QString *error = nullptr);
    /** 便携版自更新脚本内容。恒为 ASCII：cmd 按本地代码页解析 bat，
     *  中文注释或中文路径会让脚本在某些机器上直接语法错误。 */
    static QString buildPortableUpdateScript(const QString &sourceDir, const QString &targetDir);
    /** zip 可能多包一层目录，这里定位真正含 NeonBoard.exe 的根 */
    static QString locatePayloadRoot(const QString &extractDir);
    /** Inno Setup 卸载项的注册表路径（安装形态判定的依据） */
    static QString uninstallKeyPath();
    /** 清单里只写文件名时，按清单所在地址解析成完整下载链接（与插件市场同约定） */
    static QString resolveUrl(const QString &url, const QString &baseUrl);

    QString currentVersion() const;
    QString installKind() const;

    QString state() const { return m_state; }
    bool updateAvailable() const { return m_updateAvailable; }
    QString latestVersion() const { return m_latest.version; }
    QString releaseNotes() const { return m_latest.notes; }
    QString publishedAt() const { return m_latest.publishedAt; }
    QString releaseUrl() const { return m_latest.htmlUrl; }
    qint64 releaseSize() const;
    QString statusText() const { return m_statusText; }
    int progress() const { return m_progress; }

    QStringList updateSources() const { return m_sources; }
    bool autoCheck() const { return m_autoCheck; }
    void setAutoCheck(bool enabled);
    QString skippedVersion() const { return m_skippedVersion; }
    qint64 lastCheckAt() const { return m_lastCheckAt; }

    /** 内置默认清单地址（Gitee raw 优先，GitHub raw 兜底） */
    static QString defaultSource();
    /** 清单缓存文件路径 */
    static QString cachePath();

    Q_INVOKABLE void checkForUpdates(bool silent = true);
    Q_INVOKABLE void downloadAndInstall();
    Q_INVOKABLE void openDownloadPage();
    Q_INVOKABLE void skipVersion(const QString &version);
    Q_INVOKABLE void setUpdateSource(const QString &url);
    Q_INVOKABLE bool addUpdateSource(const QString &url);
    Q_INVOKABLE bool removeUpdateSource(int index);

    /** 启动后的自动检查：到点才真的发请求，避免每次启动都打网络 */
    Q_INVOKABLE void startupCheck();

    // ---- 同步接口（命令行 / 测试用）----
    /** 同步检查一次，返回是否有更新可用 */
    bool checkBlocking(int timeoutMs, QString *error);
    /** 同步下载当前形态对应的产物到 destFile（含 SHA-256 校验） */
    bool downloadBlocking(const QString &destFile, QString *error);

signals:
    void stateChanged();
    void infoChanged();
    void statusTextChanged();
    void progressChanged();
    void sourcesChanged();
    void autoCheckChanged();
    void skippedVersionChanged();
    void lastCheckChanged();
    /** 检查结束（成功或失败都会发），供同步等待与 QML 收尾 */
    void checkCompleted(bool ok);
    /** 发现新版本：QML 侧据此弹提示条 / 托盘通知 */
    void updateFound(const QString &version);
    void errorOccurred(const QString &message);
    /** 安装动作已启动，宿主请准备退出 */
    void installLaunched();

private:
    enum class Purpose { Manifest, Artifact };

    void setState(const QString &state);
    void setStatusText(const QString &text);
    void setProgress(int value);

    /** 候选清单地址：配置的源 + GitHub API 兜底 */
    QStringList candidateUrls() const;
    void startRequest(const QString &url, Purpose purpose);
    void onReplyFinished(QNetworkReply *reply);
    void handleManifest(const QByteArray &payload, const QString &fromUrl);
    void finishCheck(bool ok);
    bool useCachedManifest();
    void storeCachedManifest(const QString &url, const QByteArray &payload);

    /** 当前形态对应的产物：url / sha256 / size */
    QString artifactUrl() const;
    QString artifactSha256() const;

    void installWithSetup(const QString &file);
    void installPortable(const QString &zipFile);
    bool verifyFile(const QString &path, const QString &expectedSha256, QString *error);
    bool extractZip(const QString &zipFile, const QString &destDir, QString *error);
    bool launchSelfUpdater(const QString &payloadDir, QString *error);

    void loadSettings();
    void saveSources();

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    /** 产物下载的「无数据」看门狗：收到字节就续命，超时 abort 当前请求 */
    QTimer *m_stallTimer = nullptr;
    Purpose m_purpose = Purpose::Manifest;
    // 安装形态判定要查注册表，进程生命周期内不会变，缓存一次
    mutable QString m_installKindCache;

    QString m_state = QStringLiteral("idle");
    QString m_statusText;
    QString m_skippedVersion;
    int m_progress = 0;
    bool m_updateAvailable = false;
    bool m_autoCheck = true;
    bool m_silent = true;
    qint64 m_lastCheckAt = 0;

    ReleaseInfo m_latest;
    QStringList m_sources;
    QStringList m_candidates;
    int m_candidateIndex = 0;
    QString m_pendingArtifact;
};
