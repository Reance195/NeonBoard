#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;
class TaskRunStore;
class TaskRunner;
class AgentProfile;
class TaskExec;
class TaskModel;

// 执行队列调度器（R7-01）。
//
// 职责边界：
//   1. 只做「从 FIFO 队列取队头 → 交给 TaskRunner 拉起」这一件事；
//      进程生命周期 / 超时 / 日志 / 心跳仍然只归 TaskRunner（R6-04），
//      调度器不碰 QProcess，也不重复实现门锁。
//   2. 队列条目与 run 是同一行（TaskRunStore::enqueue 落 status=queued /
//      queue_status=waiting）。派发时先 beginDispatch（waiting → dispatching），
//      再调 TaskRunner::start(taskId) —— 后者经 acquire 把那行原地提升成 running。
//      这样「同卡唯一活跃 run」的部分唯一索引与队列语义同时成立。
//   3. 背压：全局在跑的 run 数达 concurrency()（默认 1，最高 2）时不再派发；
//      队列暂停（exec.queuePaused）时也不派发。正在跑的 run 不受暂停影响。
//   4. 派发失败（缺 agent / 缺 profile / 启动失败）要把该行结算成 blocked，
//      否则它会永远停在 dispatching，把并发槽位和队头一起卡死。
//
// 不做的（r7-dispatcher-plan §8 非目标）：
//   - 队列不跨会话恢复：TaskRunStore 构造时把残留 waiting / dispatching 结算成 cancelled；
//   - 不自动改看板列、不自动 push。
class Scheduler : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int queueLength READ queueLength NOTIFY changed)
    Q_PROPERTY(int running READ running NOTIFY changed)
    Q_PROPERTY(int concurrency READ concurrency NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)

public:
    Scheduler(Database *database, TaskRunStore *runs, TaskRunner *runner,
              AgentProfile *profiles, TaskExec *exec, TaskModel *tasks = nullptr,
              QObject *parent = nullptr);

    int revision() const;
    int queueLength() const;
    int running() const;
    int concurrency() const;
    bool paused() const;

    // 入队（不做派发决策）：返回 TaskRunStore::enqueue 的结果，随后 triggerDispatch。
    Q_INVOKABLE QVariantMap enqueue(const QString &taskId, const QVariantMap &fields = {});
    // 勾选多卡一次入队（R7-05 夜间批量的底层能力）：按传入顺序排队。
    // 返回 {ok:true, queued:[runId...], rejected:[{taskId, code, message}...]}。
    Q_INVOKABLE QVariantMap enqueueMany(const QStringList &taskIds,
                                        const QVariantMap &fields = {});

    // 队列操作（全部委托 TaskRunStore 落库，这里只补一次派发尝试）
    Q_INVOKABLE bool pauseQueue();
    Q_INVOKABLE bool resumeQueue();
    Q_INVOKABLE bool reorder(const QString &runId, int delta);
    Q_INVOKABLE bool skip(const QString &runId);
    Q_INVOKABLE bool setConcurrency(int value);

    // 队列快照（waiting / dispatching / running）+ 每条的卡标题，供 UI 直接渲染。
    Q_INVOKABLE QVariantList queueView(int limit = 100) const;
    // 预计剩余时长（秒）：按历史 run 的平均 cost_ms 估，没有历史时给 fallbackSec/条。
    Q_INVOKABLE int estimateRemainingSec(int fallbackSec = 300) const;

    // 显式触发一次派发（入队 / 运行结束 / 恢复 / 调序后调用；幂等，多余的调用无副作用）。
    // 用一个 0 延迟的定时器合并同一轮事件里的多次触发，避免递归启动。
    Q_INVOKABLE void triggerDispatch();

signals:
    void changed();
    void dispatched(const QString &runId, const QString &taskId);
    void dispatchFailed(const QString &runId, const QString &taskId, const QString &message);
    void queueDrained();

private:
    // 真正的派发循环：按 concurrency 与 paused 尽量多取队头。
    void pump();
    void schedulePump();
    // 派发前的执行定义解析：缺 agent / 缺 profile 时返回非空错误串。
    QString precheckQueued(const QString &taskId) const;
    void failDispatch(const QString &runId, const QString &taskId, const QString &message);
    int historyAvgCostMs() const;
    void bump();

    Database *m_database = nullptr;
    TaskRunStore *m_runs = nullptr;
    TaskRunner *m_runner = nullptr;
    AgentProfile *m_profiles = nullptr;
    TaskExec *m_exec = nullptr;
    TaskModel *m_tasks = nullptr;
    int m_revision = 0;
    bool m_pumpScheduled = false;
    bool m_pumping = false;
};
