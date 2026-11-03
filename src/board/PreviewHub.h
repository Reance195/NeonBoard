#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class QProcess;
class QTimer;

// 预览中心：发现各 agent 的 worktree，一键构建并启动隔离的预览实例。
//
// 预览实例走沙箱环境变量（数据目录 / 单实例 key / 路线图快照全部隔离），
// 不会污染锚点区的真实数据；窗口标题与托盘提示都带 agent 标签，多实例可区分。
//
// 预览实例由本对象托管：预览中心所在的主程序退出时，预览实例一并结束，
// 不会留下孤儿进程。
class PreviewHub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList agents READ agents NOTIFY agentsChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool available READ available NOTIFY agentsChanged)
    Q_PROPERTY(QString instanceLabel READ instanceLabel CONSTANT)
    Q_PROPERTY(bool hostInstance READ hostInstance CONSTANT)
    Q_PROPERTY(QVariantList packageQueue READ packageQueue NOTIFY packageQueueChanged)

public:
    explicit PreviewHub(QObject *parent = nullptr);
    ~PreviewHub() override;

    // 各 agent 的实时状态（id / 分支 / 提交 / 是否脏 / 是否已构建 / 是否在运行）
    QVariantList agents() const;
    // 面板底部状态文案
    QString statusText() const;
    // 是否定位到了仓库根目录（未定位到时预览中心不可用）
    bool available() const;
    // 当前实例身份标签（NEONBOARD_AGENT_ID），空表示主线实例
    QString instanceLabel() const;
    // 是否为主线实例（只有主线实例展示预览中心入口）
    bool hostInstance() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool build(const QString &agentId);
    Q_INVOKABLE bool start(const QString &agentId);
    Q_INVOKABLE bool stop(const QString &agentId);
    Q_INVOKABLE QString buildLog(const QString &agentId) const;
    // 打包队列：统一出包入口，串行执行，产物落 dist/packages/<agent>/
    QVariantList packageQueue() const;
    Q_INVOKABLE bool enqueuePackage(const QString &agentId);
    Q_INVOKABLE void clearFinishedPackages();

    // 命令行模式（scripts/preview.ps1 走这条路，避免两套扫描逻辑）
    QString listJson() const;
    bool buildBlocking(const QString &agentId, QString *error);
    bool packageBlocking(const QString &agentId, QString *error);
    bool startBlocking(const QString &agentId, QString *error);
    bool stopBlocking(const QString &agentId, QString *error);
    // 命令行模式：实例脱离本进程，命令退出后继续运行
    void setDetachOnStart(bool detached);

    // 命令行模式下关掉自我登记：CLI 进程跑完就退，登记进去会留下过期的“运行中”记录
    void setSelfRegisterEnabled(bool enabled);

signals:
    void agentsChanged();
    void statusChanged();
    void packageQueueChanged();

private:
    struct AgentInfo {
        QString id;
        QString path;
        QString branch;
        QString head;
        QString subject;
        bool dirty = false;
        bool detached = false;
        bool built = false;
        QString builtAt;
        QString exe;
        bool running = false;
        qint64 pid = 0;
        bool current = false;
    };

    struct BuildSettings {
        QString cmake;
        QString generator;
        QString platform;
        QString qtPrefix;
    };

    struct RuntimeEntry {
        qint64 pid = 0;
        QString startedAt;
        QString exe;
    };

    struct PackageJob {
        QString agentId;
        QString state;
        QString message;
        QString outputDir;
        QString queuedAt;
        QString finishedAt;
    };

    // —— 仓库定位 ——
    QString anchorRoot() const;
    QString commonGitDir() const;
    QString runtimeFilePath() const;
    QString agentIdFor(const QString &path) const;

    // —— 采集 ——
    void scan();
    AgentInfo makeAgent(const QString &path, const QString &id) const;
    void detectBuild(AgentInfo *info) const;
    void loadRuntime();
    void saveRuntime() const;
    bool isAlive(qint64 pid) const;

    // —— 构建 ——
    BuildSettings resolveBuildSettings() const;
    bool runConfigure(const AgentInfo &agent, const BuildSettings &settings, QString *error);
    bool runBuild(const AgentInfo &agent, const BuildSettings &settings, QString *error);
    void spawnBuildStage(const AgentInfo &agent, const BuildSettings &settings, const QString &stage);
    void onBuildFinished(const QString &agentId, int exitCode);
    void appendLog(const QString &agentId, const QString &text);

    // —— 运行实例 ——
    bool spawnInstance(const AgentInfo &agent, QString *error);

    // —— 打包队列 ——
    void startNextPackage();
    void packageStep(const QString &step);
    void finishPackage(bool ok, const QString &message);
    bool preparePackageDir(const AgentInfo &agent, QString *outDir, QString *error);
    QString packageOutputDir(const QString &agentId) const;
    QString resolveWindeployqt(const BuildSettings &settings) const;
    PackageJob *currentPackage();

    const AgentInfo *findAgent(const QString &agentId) const;
    void setStatus(const QString &status);

    QVector<AgentInfo> m_agents;
    QHash<QString, RuntimeEntry> m_runtime;
    QHash<QString, QProcess *> m_instances;
    QHash<QString, QProcess *> m_builds;
    QHash<QString, QString> m_buildStage;
    QHash<QString, QString> m_logs;
    QVector<PackageJob> m_packageQueue;
    QProcess *m_packageProc = nullptr;
    QString m_packageStep;
    QString m_status;
    QString m_instanceLabel;
    QString m_selfId;
    QString m_anchorRoot;
    QTimer *m_timer = nullptr;
    bool m_scanning = false;
    bool m_detachOnStart = false;
    bool m_selfRegisterEnabled = true;
};
