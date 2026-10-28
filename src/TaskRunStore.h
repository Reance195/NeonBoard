#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;

// 任务执行运行记录（R6-02）+ 执行队列（R7-01）。
//
// 设计要点（docs/adr/0002-run-state-and-executor-safety.md）：
//   1. 运行态独立落在侧表 task_run，不往 task 表加列、不升 user_version，
//      看板列（todo/doing/done）不会被执行结果改动；
//   2. 同一张卡同一时刻最多一个活跃 run（queued/running），由部分唯一索引兜底；
//   3. R7-01 起「忙则拒绝」升级为 FIFO 队列：队列条目与 run 是**同一行**
//      （enqueue 落一条 status=queued / queue_status=waiting 的行），调度器派发时
//      调 acquire —— acquire 发现该卡有 waiting / dispatching 的行就**原地提升**成
//      running（不新建行、不报 busy）。这样部分唯一索引与「一行一次 run」同时成立，
//      也不需要给队列另开一张表；
//   4. 启动时把心跳超时的活跃 run 判为 blocked —— 上次进程异常退出后不能永久占锁；
//      队列条目（queue_status=waiting）**不参与**孤儿回收，它们没在跑，不该被杀；
//   5. 队列**不跨会话恢复**（r7-dispatcher-plan §8 非目标）：应用启动时把残留的
//      waiting / dispatching 行结算成 cancelled，避免重启后自动拉起一堆 agent 进程。
//
// queue_status 取值：
//   ''           R6 直跑（不经队列）遗留的 run，语义等同「立即 running」
//   waiting      在队列里等派发
//   dispatching  调度器已取出、正在拉起进程
//   running      进程在跑
//   done         终态（passed / blocked / cancelled，含 skip 掉不执行的）
class TaskRunStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int concurrency READ concurrency NOTIFY changed)
    Q_PROPERTY(bool queuePaused READ queuePaused NOTIFY changed)

public:
    static constexpr int kStaleSeconds = 90;
    // 并发上限：4 核 8 线程的机器多开只会互相拖慢，设置里最多给到 2
    static constexpr int kMaxConcurrency = 2;

    explicit TaskRunStore(Database *database, QObject *parent = nullptr);

    int revision() const;

    // fields: agent / cwd / workflow / attempt / status（queued|running，默认 running）
    // attempt 缺省时自动取「该卡最近一次 run 的 attempt + 1」（重试 = 新 run 递增轮次，
    // 历史 run 不覆盖）；显式给了就用给定值。
    // 成功 {ok:true, runId, status}；同卡已有活跃 run {ok:false, code:"busy", runId, message}
    //
    // R7-01 补充：同卡已有 queue_status=waiting / dispatching 的行时，本次 acquire
    // **提升那一行**为 running（不新建行、不报 busy）——队列条目与 run 是同一行。
    Q_INVOKABLE QVariantMap acquire(const QString &taskId, const QVariantMap &fields = {});
    Q_INVOKABLE bool heartbeat(const QString &runId);
    // status 只接受终态 passed / blocked / cancelled；
    // detail 可带 exitCode / lastError / logPath / scopeViolation / costMs，未提供的列保持原值。
    // 结算时自动把 queue_status 置 done，并按 started_at/finished_at 补 cost_ms。
    Q_INVOKABLE bool release(const QString &runId, const QString &status,
                             const QVariantMap &detail = {});
    Q_INVOKABLE QVariantMap activeRun(const QString &taskId) const;
    Q_INVOKABLE QVariantMap run(const QString &runId) const;
    Q_INVOKABLE QVariantList runsOf(const QString &taskId, int limit = 20) const;

    // R6-06：最近一次 run（任意状态；没有 run 返回空 map）。卡面徽章用它。
    Q_INVOKABLE QVariantMap latestRun(const QString &taskId) const;

    // R6-06：执行面板摘要 = 最近一次 run 的行 + durationSec + logTail（末尾 N 行）
    // + canRetry（终态 blocked 且未到 maxAttempts 时由 UI 判定，这里只给事实）。
    // 没有 run 返回 {hasRun:false}。
    Q_INVOKABLE QVariantMap summaryOf(const QString &taskId, int logTailLines = 30) const;

    // 把心跳早于 now-staleSeconds 的活跃 run 判为 blocked，返回处理条数。
    // 队列里 wait 中的行不参与：它们没在跑，等久了也不该被杀。
    Q_INVOKABLE int recoverOrphans(int staleSeconds = kStaleSeconds);

    // ---- 执行队列（R7-01）----

    // 入队一条 waiting 行（position 单调递增）。同卡已有活跃 run 时返回 busy。
    // 成功 {ok:true, runId, taskId, position}；失败 {ok:false, code, message}。
    Q_INVOKABLE QVariantMap enqueue(const QString &taskId, const QVariantMap &fields = {});
    // 队列快照：waiting / dispatching / running 的行（按 position 升序）。
    Q_INVOKABLE QVariantList queue(int limit = 100) const;
    // 只取 waiting（调度器取队头用），按 position 升序。
    Q_INVOKABLE QVariantList waitingRuns() const;
    // 全局在跑的 run 数（status=running），背压判定用。
    Q_INVOKABLE int runningCount() const;

    // 暂停 / 恢复派发（meta exec.queuePaused，默认 false）。暂停只停派发，不杀在跑的 run。
    bool queuePaused() const;
    Q_INVOKABLE bool setQueuePaused(bool paused);

    // 全局并发上限（meta exec.concurrency，默认 1，钳制 1..kMaxConcurrency）。
    int concurrency() const;
    Q_INVOKABLE bool setConcurrency(int value);

    // 调序：把 waiting 行与相邻（delta=-1 上 / +1 下）的 waiting 行交换 position。
    Q_INVOKABLE bool reorder(const QString &runId, int delta);
    // 跳过：waiting 行直接结算成「不执行」——status=cancelled、queue_status=done，
    // last_error 记「已跳过」。返回 false 表示该行已不在等待态。
    Q_INVOKABLE bool skip(const QString &runId);
    // 调度器取出队头时调用：waiting → dispatching。
    Q_INVOKABLE bool beginDispatch(const QString &runId);
    // 派发失败（缺 agent / profile / 启动失败）：把该行结算成 blocked，别把队列卡死。
    Q_INVOKABLE bool failQueued(const QString &runId, const QString &message);

signals:
    void changed();

private:
    void bump();

    Database *m_database = nullptr;
    int m_revision = 0;
};
