#include "Scheduler.h"

#include "AgentProfile.h"
#include "Database.h"
#include "TaskExec.h"
#include "TaskModel.h"
#include "TaskRunner.h"
#include "TaskRunStore.h"

#include <QDebug>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimer>
#include <QVariant>

namespace {

// 队列条目的展示字段：run 行 + 卡标题。标题从 task 表现取，避免 UI 再查一遍。
QString taskTitle(Database *database, const QString &taskId)
{
    if (!database || taskId.isEmpty())
        return {};
    const QSqlDatabase db = QSqlDatabase::database(database->connectionName());
    if (!db.isOpen())
        return {};
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT title FROM task WHERE id = ?"));
    query.addBindValue(taskId);
    if (!query.exec() || !query.next())
        return {};
    return query.value(0).toString();
}

} // namespace

Scheduler::Scheduler(Database *database, TaskRunStore *runs, TaskRunner *runner,
                     AgentProfile *profiles, TaskExec *exec, TaskModel *tasks,
                     QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_runs(runs)
    , m_runner(runner)
    , m_profiles(profiles)
    , m_exec(exec)
    , m_tasks(tasks)
{
    // 运行结束 → 让出并发槽位 → 立刻补派队头。queueDrained 只在「队列空了」时发，
    // 供 R7-05 夜间批量做完成通知。
    if (m_runner) {
        connect(m_runner, &TaskRunner::runFinished, this,
                [this](const QString &, const QVariantMap &) {
                    triggerDispatch();
                    if (m_runs && m_runs->waitingRuns().isEmpty())
                        emit queueDrained();
                });
        connect(m_runner, &TaskRunner::changed, this, [this]() { bump(); });
    }
    if (m_runs) {
        connect(m_runs, &TaskRunStore::changed, this, [this]() { bump(); });
    }
}

int Scheduler::revision() const
{
    return m_revision;
}

int Scheduler::queueLength() const
{
    return m_runs ? m_runs->waitingRuns().size() : 0;
}

int Scheduler::running() const
{
    return m_runs ? m_runs->runningCount() : 0;
}

int Scheduler::concurrency() const
{
    return m_runs ? m_runs->concurrency() : 1;
}

bool Scheduler::paused() const
{
    return m_runs ? m_runs->queuePaused() : false;
}

void Scheduler::bump()
{
    ++m_revision;
    emit changed();
}

QVariantMap Scheduler::enqueue(const QString &taskId, const QVariantMap &fields)
{
    if (!m_runs) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("code"), QStringLiteral("no-store")},
                {QStringLiteral("message"), QStringLiteral("缺少运行记录门面")}};
    }
    QVariantMap result = m_runs->enqueue(taskId, fields);
    if (result.value(QStringLiteral("ok")).toBool())
        triggerDispatch();
    return result;
}

QVariantMap Scheduler::enqueueMany(const QStringList &taskIds, const QVariantMap &fields)
{
    QVariantList queued;
    QVariantList rejected;
    for (const QString &taskId : taskIds) {
        const QString id = taskId.trimmed();
        if (id.isEmpty())
            continue;
        const QVariantMap one = m_runs ? m_runs->enqueue(id, fields)
                                       : QVariantMap{{QStringLiteral("ok"), false},
                                                     {QStringLiteral("code"),
                                                      QStringLiteral("no-store")},
                                                     {QStringLiteral("message"),
                                                      QStringLiteral("缺少运行记录门面")}};
        if (one.value(QStringLiteral("ok")).toBool()) {
            queued.append(one.value(QStringLiteral("runId")));
        } else {
            rejected.append(QVariantMap{{QStringLiteral("taskId"), id},
                                        {QStringLiteral("code"),
                                         one.value(QStringLiteral("code"))},
                                        {QStringLiteral("message"),
                                         one.value(QStringLiteral("message"))}});
        }
    }
    if (!queued.isEmpty())
        triggerDispatch();
    return {{QStringLiteral("ok"), rejected.isEmpty() && !queued.isEmpty()},
            {QStringLiteral("queued"), queued},
            {QStringLiteral("rejected"), rejected}};
}

bool Scheduler::pauseQueue()
{
    if (!m_runs)
        return false;
    const bool ok = m_runs->setQueuePaused(true);
    if (ok)
        bump();
    return ok;
}

bool Scheduler::resumeQueue()
{
    if (!m_runs)
        return false;
    const bool ok = m_runs->setQueuePaused(false);
    if (ok) {
        triggerDispatch();
        bump();
    }
    return ok;
}

bool Scheduler::reorder(const QString &runId, int delta)
{
    if (!m_runs)
        return false;
    const bool ok = m_runs->reorder(runId, delta);
    if (ok)
        bump();
    return ok;
}

bool Scheduler::skip(const QString &runId)
{
    if (!m_runs)
        return false;
    const bool ok = m_runs->skip(runId);
    // 跳过的可能是队头：让出槽位后要补派下一条
    if (ok)
        triggerDispatch();
    return ok;
}

bool Scheduler::setConcurrency(int value)
{
    if (!m_runs)
        return false;
    const bool ok = m_runs->setConcurrency(value);
    if (ok) {
        triggerDispatch();
        bump();
    }
    return ok;
}

QVariantList Scheduler::queueView(int limit) const
{
    QVariantList out;
    if (!m_runs)
        return out;
    const QVariantList rows = m_runs->queue(limit);
    out.reserve(rows.size());
    for (const QVariant &value : rows) {
        QVariantMap row = value.toMap();
        row.insert(QStringLiteral("title"),
                   taskTitle(m_database, row.value(QStringLiteral("taskId")).toString()));
        out.append(row);
    }
    return out;
}

int Scheduler::estimateRemainingSec(int fallbackSec) const
{
    if (!m_runs)
        return 0;
    const int waiting = m_runs->waitingRuns().size();
    if (waiting <= 0)
        return 0;
    const int avgMs = historyAvgCostMs();
    // 没有历史（avgMs<=0）时用 fallback；并发 >1 时按槽位数折算
    const int perRunSec = avgMs > 0 ? qMax(1, avgMs / 1000) : qMax(1, fallbackSec);
    // 注意：不能叫 slots —— Qt 的 slots 是宏（qobjectdefs.h），会被展开成空
    const int slotCount = qMax(1, concurrency());
    // 保守估计：正在跑的 run 仍占着槽位，按 ceil(waiting / slotCount) 算
    const int waves = (waiting + slotCount - 1) / slotCount;
    return perRunSec * waves;
}

int Scheduler::cardAvgCostMs(const QString &taskId) const
{
    if (!m_database || taskId.isEmpty())
        return 0;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return 0;
    // 只统计真跑完的 run（cost_ms>0 且 passed / blocked），跳过与取消不计入
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT AVG(cost_ms) FROM task_run "
        "WHERE task_id = ? AND cost_ms > 0 AND status IN ('passed','blocked')"));
    query.addBindValue(taskId);
    if (!query.exec() || !query.next())
        return 0;
    return static_cast<int>(query.value(0).toDouble());
}

QVariantMap Scheduler::nightBatchEstimate(const QStringList &taskIds, int fallbackSec) const
{
    QVariantMap out;
    const int slotCount = qMax(1, concurrency());
    const int globalAvgMs = historyAvgCostMs();
    const int perRunFallback = qMax(1, fallbackSec);

    QVariantList perCard;
    qint64 sumSec = 0;
    for (const QString &raw : taskIds) {
        const QString id = raw.trimmed();
        if (id.isEmpty())
            continue;
        int avgMs = cardAvgCostMs(id);
        if (avgMs <= 0)
            avgMs = globalAvgMs;
        const int sec = avgMs > 0 ? qMax(1, avgMs / 1000) : perRunFallback;
        sumSec += sec;
        perCard.append(QVariantMap{{QStringLiteral("taskId"), id},
                                   {QStringLiteral("sec"), sec}});
    }
    if (perCard.isEmpty()) {
        out.insert(QStringLiteral("ok"), true);
        out.insert(QStringLiteral("totalSec"), 0);
        out.insert(QStringLiteral("finishAt"), 0);
        out.insert(QStringLiteral("concurrency"), slotCount);
        out.insert(QStringLiteral("perCard"), perCard);
        return out;
    }
    // 按并发折算：N 条串行总量在 slotCount 路并行下约为 ceil(sum / slotCount)
    const int totalSec = static_cast<int>((sumSec + slotCount - 1) / slotCount);
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("totalSec"), totalSec);
    out.insert(QStringLiteral("finishAt"), QDateTime::currentSecsSinceEpoch() + totalSec);
    out.insert(QStringLiteral("concurrency"), slotCount);
    out.insert(QStringLiteral("perCard"), perCard);
    return out;
}

QVariantMap Scheduler::enqueueNightBatch(const QStringList &taskIds, const QVariantMap &fields)
{
    QVariantMap result = enqueueMany(taskIds, fields);
    // 估算按入参卡列表算（queued 里是 runId，不是 taskId）
    result.insert(QStringLiteral("estimate"),
                  nightBatchEstimate(taskIds,
                                     fields.value(QStringLiteral("fallbackSec"), 300).toInt()));
    return result;
}

QString Scheduler::precheckQueued(const QString &taskId) const
{
    if (!m_profiles)
        return QStringLiteral("缺少 agent profile 门面");
    if (!m_profiles->executorEnabled())
        return QStringLiteral("执行器总开关未开启");
    if (!m_exec)
        return QStringLiteral("缺少执行定义门面");

    // 队列条目在入队时就记了 agent；没记的（UI 只给了 taskId）这里按执行定义补
    const QVariantMap exec = m_exec->execOf(taskId);
    QString agent = exec.value(QStringLiteral("agent")).toString().trimmed();
    if (agent.isEmpty())
        agent = m_profiles->defaultAgent().trimmed();
    if (agent.isEmpty())
        return QStringLiteral("这张卡没有指定 agent");
    if (m_profiles->profileOf(agent).isEmpty())
        return QStringLiteral("未知 agent：%1").arg(agent);
    return QString();
}

void Scheduler::failDispatch(const QString &runId, const QString &taskId,
                             const QString &message)
{
    if (m_runs)
        m_runs->failQueued(runId, message);
    qWarning() << "Scheduler: dispatch failed" << taskId << message;
    emit dispatchFailed(runId, taskId, message);
    bump();
}

void Scheduler::triggerDispatch()
{
    schedulePump();
}

void Scheduler::schedulePump()
{
    // 合并同一轮事件里的多次触发（入队多张卡会连发 N 次）：
    // 用 0 延迟单次定时器，天然去重，也不会在信号回调里递归启动进程。
    if (m_pumpScheduled || m_pumping)
        return;
    m_pumpScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_pumpScheduled = false;
        pump();
    });
}

void Scheduler::pump()
{
    if (!m_runs || !m_runner)
        return;
    // 重入保护：pump 里调 start() 会同步触发 acquire / 信号，别再套一层
    if (m_pumping)
        return;
    m_pumping = true;

    if (!paused()) {
        const int limit = concurrency();
        int running = m_runs->runningCount();
        // 一次最多派发到上限；start() 是同步的，每派一条都重新数一遍
        while (running < limit) {
            const QVariantList waiting = m_runs->waitingRuns();
            if (waiting.isEmpty())
                break;
            const QVariantMap head = waiting.first().toMap();
            const QString runId = head.value(QStringLiteral("id")).toString();
            const QString taskId = head.value(QStringLiteral("taskId")).toString();

            const QString problem = precheckQueued(taskId);
            if (!problem.isEmpty()) {
                // 缺 agent / 缺 profile：结算成 blocked，别把队头卡死
                failDispatch(runId, taskId, problem);
                continue;
            }
            if (!m_runs->beginDispatch(runId)) {
                // 这一行已被别的路径结算（跳过 / 取消），继续取下一个
                continue;
            }

            // 交给 TaskRunner：它内部再次 acquire，把那行从 dispatching 提升成 running
            const QVariantMap started = m_runner->start(taskId);
            if (!started.value(QStringLiteral("ok")).toBool()) {
                failDispatch(runId, taskId,
                             started.value(QStringLiteral("message")).toString());
                continue;
            }
            emit dispatched(runId, taskId);
            running = m_runs->runningCount();
        }
    }

    m_pumping = false;
    bump();
}

int Scheduler::historyAvgCostMs() const
{
    if (!m_database)
        return 0;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return 0;
    // 只统计真跑完的 run（cost_ms>0 且 passed / blocked），跳过与取消不计入
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
            "SELECT AVG(cost_ms) FROM ("
            "  SELECT cost_ms FROM task_run "
            "  WHERE cost_ms > 0 AND status IN ('passed','blocked') "
            "  ORDER BY finished_at DESC LIMIT 20)"))
        || !query.next()) {
        return 0;
    }
    return static_cast<int>(query.value(0).toDouble());
}
