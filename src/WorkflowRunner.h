#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;
class TaskExec;
class TaskRunStore;
class TaskRunner;
class AgentProfile;
class ScopeMatcher;
class AcceptanceRunner;
class WorkflowPresets;

// 角色链 workflow 引擎（R7-03）：把「一次执行」从单步升级为按序接力的角色步骤。
//
// 设计要点（docs/r7-dispatcher-plan.md §3.3 / §R7-03）：
//   1. **复用而不是重造执行器**：每一步都调 TaskRunner::startRaw 起一次真实进程，
//      超时 / 终止 / 流式日志 / 心跳 / 失败重试全部沿用 R6-04 与 R6-08；
//      本类只负责「该跑第几步、把上一步的交接物喂给下一步、逐步验收」。
//   2. **每步一条 run**：parent_run_id 指母 run、step_index 递增、role 记录角色，
//      成本按 step 累加到母 run 的 cost_ms —— 这样执行面板与队列视图不用区分层级。
//   3. **交接物**：每步产出 `.neonboard/handoff/<taskId>/<role>.md`（数据目录下，
//      不进仓库，与 runs/ 同待遇）。下一步的 prompt 注入上一步 handoff 全文。
//      路径契约固定，单测直接断言落盘与注入。
//   4. **每步独立验收与越界检查**：复用 R6-05 的 AcceptanceRunner / ScopeMatcher，
//      跑前记 git 基线、步后比对；任何一步不过即停（fail-fast），后续步骤不再启动。
//   5. **安全默认**：不自动改看板列、不自动 push；总开关关着时 start 直接拒绝，
//      与「未引入执行器时行为一致」的约束对齐。
//
// 会话态（chain）只在内存里：进程重启后不续跑（与队列一致的 v1 非目标）。
class WorkflowRunner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)

public:
    WorkflowRunner(Database *database, TaskExec *exec, TaskRunStore *runs, TaskRunner *runner,
                   AgentProfile *profiles, ScopeMatcher *scope, AcceptanceRunner *acceptance,
                   QObject *parent = nullptr);

    // R7-04：注入预设门面后，卡片未选 workflow 时由 orchestrator 按规模选型，
    // 步骤改由可编辑预设展开（按规模裁剪 pm/architect，qa 永不裁）。
    // 用 setter 而不是构造参数，避免改动签名的既有调用方（单测）。
    void setPresets(WorkflowPresets *presets);

    int revision() const;
    bool running() const;

    // 启动一条角色链：按 taskExec.stepsOf(taskId) 的顺序逐步执行。
    // {ok:true, chainId} / {ok:false, message}
    Q_INVOKABLE QVariantMap start(const QString &taskId);

    // 取消当前链（后续步骤不再启动，正在跑的那步交给 TaskRunner::stop）。
    Q_INVOKABLE bool cancel();

    // 当前链快照：{chainId, taskId, steps:[{index, role, status, runId, handoffPath, costMs}],
    //             currentIndex, done, failed}
    Q_INVOKABLE QVariantMap chain() const;

    // 交接物目录（数据目录下 .neonboard/handoff），供 UI 展示「文件在哪」
    Q_INVOKABLE QString handoffDir(const QString &taskId) const;

    // ---- 纯函数，供测试与 QML 直接用 ----

    // 交接物路径契约：<handoffRoot>/<taskId>/<role>.md
    static QString handoffPath(const QString &root, const QString &taskId, const QString &role);

    // 拼某一步的 prompt：上一步 handoff 全文 + 本步任务描述 + 交付物要求。
    // previousHandoff 为空表示第一步（没有交接物可读）。
    static QString buildStepPrompt(const QString &taskId, const QString &workflow,
                                   const QVariantMap &step, int stepIndex, int stepCount,
                                   const QString &stepGoal, const QString &previousHandoff);

    // 步骤结束时从日志里抽「交接物摘要」：超长截断、保留结尾（结论通常在末尾）。
    static QString summarizeHandoff(const QByteArray &log, int maxChars = 6000);

signals:
    void changed();
    // 某一步开始 / 结束。detail 带 role / stepIndex / status / runId / handoffPath
    void stepStarted(const QString &chainId, int stepIndex, const QVariantMap &detail);
    void stepFinished(const QString &chainId, int stepIndex, const QVariantMap &detail);
    // 整条链收口：{ok, failedStep?, message?}
    void chainFinished(const QString &chainId, const QVariantMap &result);

private:
    struct Step
    {
        QString role;
        QString agent;
        QString prompt;
        QVariantList acceptance;
        QVariantList scope;
        QString runId;
        QString handoffPath;
        QString status; // pending / running / passed / blocked / cancelled
        qint64 costMs = 0;
        qint64 startedAtMs = 0; // 本步启动时刻（成本账口径）
        QVariantList baseline; // 本步跑前的 git 基线
    };

    struct Chain
    {
        QString chainId;
        QString taskId;
        QString workflow;
        QString agent; // 卡片默认 agent
        QString cwd;
        QString goal;
        QList<Step> steps;
        int current = -1;
        bool done = false;
        bool failed = false;
        bool cancelled = false;
        QString failMessage;
    };

    void markFailed(Chain &chain, const QString &message);
    void startStep(int index);
    void finishStep(int index, int exitCode, const QVariantMap &detail);
    void bump();

    // R7-04：从 triage 草案（meta exec.triage.draft.<taskId>）读 estimate.size；
    // 没有草案时返回空串，由 WorkflowPresets 按卡片规模兜底。
    QString sizeHintFor(const QString &taskId) const;
    // R7-04：卡片规模兜底用的 {title, priority}（供 WorkflowPresets::routeFor）。
    QVariantMap cardHintFor(const QString &taskId) const;

    Database *m_database = nullptr;
    TaskExec *m_exec = nullptr;
    TaskRunStore *m_runs = nullptr;
    TaskRunner *m_runner = nullptr;
    AgentProfile *m_profiles = nullptr;
    ScopeMatcher *m_scope = nullptr;
    AcceptanceRunner *m_acceptance = nullptr;
    WorkflowPresets *m_presets = nullptr;

    Chain m_chain;
    int m_activeIndex = -1; // 正在等结果的那一步（-1 表示没有）
    int m_revision = 0;
};
