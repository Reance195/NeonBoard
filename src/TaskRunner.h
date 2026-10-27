#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>

class QProcess;
class QTimer;
class Database;
class TaskRunStore;
class AgentProfile;
class TaskExec;

// 任务执行器（R6-04）：真正拉起 agent 进程并走完一个 run 的生命周期。
//
// 设计要点（docs/adr/0002-run-state-and-executor-safety.md）：
//   1. 只负责「把一个 run 跑起来到终态」，不做验收判定（R6-05）与失败回滚（R6-06），
//      那两块在依赖链上排在本卡之后。
//   2. 启动必经 TaskRunStore::acquire 拿同卡锁；遇到活跃 run 返回 busy，拒绝排队。
//   3. 命令行只走 argv（AgentProfile::argv），不经过 shell；promptMode=stdin 时把
//      prompt 写进程标准输入。
//   4. 流式日志：stdout+stderr 分别用 readyRead 增量拉取，每段追加到 run 的日志缓冲，
//      发 logAppended 供 UI 流式展示；run 结束时把完整日志落盘，logPath 塞进
//      release 的 detail。
//   5. 超时与终止：超时（exec.timeoutMin 或 profile.defaultTimeoutMin）先 terminate，
//      宽限期后 kill，recoverOrphans 兜底进程崩溃；stop() 手动终止 → cancelled。
//   6. 心跳：一个轮询 QTimer 定期 heartbeat 活跃 run，避免被 recoverOrphans 误判。
//   7. 执行器总开关：agentProfile.executorEnabled() 关着时 start 直接拒绝，
//      与「未引入执行器时行为一致」的约束对齐。
class TaskRunner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY changed)

public:
    TaskRunner(Database *database, TaskRunStore *runs, AgentProfile *profiles, TaskExec *exec,
               QObject *parent = nullptr);
    ~TaskRunner() override;

    int revision() const;
    int activeCount() const;

    // 按卡的执行定义启动：task → taskExec.execOf(taskId) 取 agent/cwd/workflow，
    // profile 取该 agent，prompt 用 goal；缺任何一环（总开关关 / 无 profile /
    // 无 agent）返回 {ok:false, message}。成功 {ok:true, runId}。
    // maxAttempts 取自执行定义（钳制 1..5）：失败且未到上限时自动重试（R6-08）。
    Q_INVOKABLE QVariantMap start(const QString &taskId);

    // 低层启动：直接用给好的 argv，不再解析执行定义。同样走 acquire/释放/心跳/日志。
    // 供测试与可嵌入场景使用；taskId 仅用于锁与记录归属。
    // maxAttempts>1 时失败自动重试（R6-08）：把失败摘要注入下一轮 prompt，
    // 新建 run 且 attempt+1；取消不重试；轮数耗尽终态 blocked 并提示人工介入。
    Q_INVOKABLE QVariantMap startRaw(const QString &taskId, const QString &exe,
                                     const QStringList &args, const QString &cwd = QString(),
                                     int timeoutSec = 600, bool promptToStdin = false,
                                     const QString &prompt = QString(), int maxAttempts = 1);

    // 手动终止运行中的 run（SIGTERM 宽限后 kill）→ cancelled。
    Q_INVOKABLE bool stop(const QString &runId);

    Q_INVOKABLE QStringList activeRuns() const;

    // R6-08：从日志与 lastError 拼「上一轮失败摘要」——末尾 200 行、
    // 8000 字符截断、单行化（注入 argv 安全）、lastError 前置。
    // 静态纯函数，供测试直接调。
    static QString failureSummary(const QByteArray &log, const QString &lastError);

signals:
    // 一段增量日志（stdout+stderr 合并），UI 可据此流式上屏。
    void logAppended(const QString &runId, const QString &text);
    // run 进入终态（passed|blocked|cancelled）后发出；payload 是 release 写回的 detail。
    void runFinished(const QString &runId, const QVariantMap &detail);
    void changed();

private:
    struct Process
    {
        QString runId;
        QString taskId;
        QProcess *proc = nullptr;
        QTimer *heartbeat = nullptr;
        QTimer *deadline = nullptr;
        QByteArray log;
        QString ending;     // 空 / timeout / cancelled：结束原因
        QString startError; // setupProcess 失败时的 QProcess errorString
        bool usedGrace = false;
        // Windows 作业对象：terminate/kill 只能杀直接子进程，agent 会再拉起子进程（
        // 编译器、git、包管理器…）。把进程挂进 Job Object 后 TerminateJobObject 能回收
        // 整棵树，否则取消/超时后子孙进程会留在后台继续跑。
        void *job = nullptr;

        // ---- R6-08 失败重试（Loop）----
        QStringList argvList; // 完整 argv（含程序），重试时在此基础上注入失败摘要
        QString cwd;
        QString prompt; // 原始 prompt（stdin 模式重试时追加失败摘要段）
        bool promptToStdin = false;
        int timeoutSec = 600;
        int maxAttempts = 1; // 钳制 1..5；默认 1 等价于不循环
        int attempt = 1;
    };

    bool setupProcess(Process &p, const QString &cwd, const QStringList &args, bool promptToStdin,
                      const QString &prompt, int timeoutSec);
    // 失败重试（R6-08）：释放当前 run（blocked + 摘要），取新锁（attempt 自增），
    // 把失败摘要注入 prompt 后重新拉起。返回 false 表示没法重试（锁被占等），
    // 调用方走正常 settle。
    bool tryRetry(Process &p, const QVariantMap &detail);
    void onProcessFinished(Process &p, int exitCode);
    void settle(Process &p, const QString &status, const QVariantMap &detail);
    void accumulate(Process &p, const QByteArray &chunk);
    // 日志脱敏：token / api key / 长十六进制串替换为占位符，避免凭据随 runs/<id>.log 外泄。
    static QString desensitize(const QString &text);
    // 把已启动的进程挂进 Job Object（仅 Windows 生效），建立失败不影响主流程。
    static void attachJob(Process &p);
    // 终止整棵进程树：先 Job Object，退化为 QProcess::kill。
    static void killTree(Process &p);
    static void closeJob(Process &p);
    QString writeLog(const Process &p) const;
    void cleanup(Process &p);
    void bump();

    Process *byRunId(const QString &runId);
    QList<Process *> m_processes;

    Database *m_database = nullptr;
    TaskRunStore *m_runs = nullptr;
    AgentProfile *m_profiles = nullptr;
    TaskExec *m_exec = nullptr;
    int m_revision = 0;
};