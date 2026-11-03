#include "TaskRunStore.h"

#include "Database.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QUuid>
#include <QVariant>

namespace {

const char kSelectColumns[] =
    "id, task_id, agent, cwd, workflow, status, attempt, started_at, finished_at, "
    "heartbeat_at, exit_code, last_error, log_path, scope_violation, "
    "queue_status, position, parent_run_id, step_index, role, handoff_path, cost_ms";

bool isActiveStatus(const QString &status)
{
    return status == QLatin1String("queued") || status == QLatin1String("running");
}

bool isTerminalStatus(const QString &status)
{
    return status == QLatin1String("passed") || status == QLatin1String("blocked")
        || status == QLatin1String("cancelled");
}

qint64 now()
{
    return QDateTime::currentSecsSinceEpoch();
}

QVariantMap rowToMap(const QSqlQuery &query)
{
    QVariantMap map;
    map.insert(QStringLiteral("id"), query.value(0).toString());
    map.insert(QStringLiteral("taskId"), query.value(1).toString());
    map.insert(QStringLiteral("agent"), query.value(2).toString());
    map.insert(QStringLiteral("cwd"), query.value(3).toString());
    map.insert(QStringLiteral("workflow"), query.value(4).toString());
    map.insert(QStringLiteral("status"), query.value(5).toString());
    map.insert(QStringLiteral("attempt"), query.value(6).toInt());
    map.insert(QStringLiteral("startedAt"), query.value(7));
    map.insert(QStringLiteral("finishedAt"), query.value(8));
    map.insert(QStringLiteral("heartbeatAt"), query.value(9));
    map.insert(QStringLiteral("exitCode"), query.value(10));
    map.insert(QStringLiteral("lastError"), query.value(11).toString());
    map.insert(QStringLiteral("logPath"), query.value(12).toString());
    map.insert(QStringLiteral("scopeViolation"), query.value(13).toString());
    map.insert(QStringLiteral("queueStatus"), query.value(14).toString());
    map.insert(QStringLiteral("position"), query.value(15).toInt());
    map.insert(QStringLiteral("parentRunId"), query.value(16).toString());
    map.insert(QStringLiteral("stepIndex"), query.value(17).toInt());
    map.insert(QStringLiteral("role"), query.value(18).toString());
    map.insert(QStringLiteral("handoffPath"), query.value(19).toString());
    map.insert(QStringLiteral("costMs"), query.value(20).toLongLong());
    return map;
}

} // namespace

TaskRunStore::TaskRunStore(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{
    // 队列不跨会话恢复（r7-dispatcher-plan §8 非目标）：应用重启后残留的 waiting /
    // dispatching 行不会再有进程认领，留着只会让「排队中」永远卡住，也会让重启自动
    // 拉起一堆 agent 进程。先把它们结算成 cancelled，再处理心跳超时的活跃 run。
    if (m_database) {
        const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
        if (db.isOpen()) {
            QSqlQuery stale(db);
            stale.prepare(QStringLiteral(
                "UPDATE task_run SET status = 'cancelled', queue_status = 'done', "
                "  finished_at = ?, "
                "  last_error = CASE WHEN last_error IS NULL OR last_error = '' "
                "                    THEN ? ELSE last_error END "
                "WHERE queue_status IN ('waiting','dispatching')"));
            stale.addBindValue(now());
            stale.addBindValue(QStringLiteral("应用重启：队列不跨会话恢复，已取消排队"));
            if (!stale.exec())
                qWarning() << "TaskRunStore: cancel stale queue failed:"
                           << stale.lastError().text();
        }
    }
    // 应用启动 = 上次会话已结束；残留的活跃 run 不可能再有进程在写心跳
    recoverOrphans();
}

int TaskRunStore::revision() const
{
    return m_revision;
}

void TaskRunStore::bump()
{
    ++m_revision;
    emit changed();
}

QVariantMap TaskRunStore::acquire(const QString &taskId, const QVariantMap &fields)
{
    QVariantMap result;
    const QString id = taskId.trimmed();
    if (id.isEmpty()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("code"), QStringLiteral("invalid"));
        result.insert(QStringLiteral("message"), QStringLiteral("任务 id 为空"));
        return result;
    }

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("code"), QStringLiteral("db"));
        result.insert(QStringLiteral("message"), QStringLiteral("数据库未打开"));
        return result;
    }

    const QString status = fields.value(QStringLiteral("status"), QStringLiteral("running"))
                               .toString().trimmed();
    if (!isActiveStatus(status)) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("code"), QStringLiteral("invalid"));
        result.insert(QStringLiteral("message"), QStringLiteral("acquire 只接受 queued / running"));
        return result;
    }

    const QVariantMap existing = activeRun(id);
    if (!existing.isEmpty()) {
        // R7-01：队列条目与 run 是同一行。调度器派发时调 acquire，把 waiting /
        // dispatching 那一行**原地提升**成 running，而不是新建一行。
        const QString queueStatus = existing.value(QStringLiteral("queueStatus")).toString();
        if (queueStatus == QStringLiteral("waiting")
            || queueStatus == QStringLiteral("dispatching")) {
            const QString existingId = existing.value(QStringLiteral("id")).toString();
            const qint64 ts = now();
            QSqlQuery promote(db);
            promote.prepare(QStringLiteral(
                "UPDATE task_run SET status = 'running', queue_status = 'running', "
                "  started_at = ?, heartbeat_at = ? "
                "WHERE id = ? AND queue_status IN ('waiting','dispatching')"));
            promote.addBindValue(ts);
            promote.addBindValue(ts);
            promote.addBindValue(existingId);
            if (!promote.exec()) {
                qWarning() << "TaskRunStore: promote queued run failed:"
                           << promote.lastError().text();
                result.insert(QStringLiteral("ok"), false);
                result.insert(QStringLiteral("code"), QStringLiteral("db"));
                result.insert(QStringLiteral("message"), promote.lastError().text());
                return result;
            }
            if (promote.numRowsAffected() <= 0) {
                // 有别的东西先一步派发了它（并发窗口）：按 busy 报，不要吞掉
                result.insert(QStringLiteral("ok"), false);
                result.insert(QStringLiteral("code"), QStringLiteral("busy"));
                result.insert(QStringLiteral("runId"), existingId);
                result.insert(QStringLiteral("message"), QStringLiteral("该队列条目已被派发"));
                return result;
            }
            bump();
            result.insert(QStringLiteral("ok"), true);
            result.insert(QStringLiteral("runId"), existingId);
            result.insert(QStringLiteral("status"), QStringLiteral("running"));
            return result;
        }
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("code"), QStringLiteral("busy"));
        result.insert(QStringLiteral("runId"), existing.value(QStringLiteral("id")));
        result.insert(QStringLiteral("message"), QStringLiteral("同一张卡已有执行中的 run"));
        return result;
    }

    const QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString agent = fields.value(QStringLiteral("agent")).toString().trimmed();
    const QString cwd = fields.value(QStringLiteral("cwd")).toString().trimmed();
    const QString workflow = fields.value(QStringLiteral("workflow")).toString().trimmed();
    // attempt 缺省 = 最近一次 run 的 attempt + 1（R6-06 重试语义：新 run 递增轮次，
    // 历史不覆盖）；显式给了就用给定值。
    int attempt;
    if (fields.contains(QStringLiteral("attempt"))) {
        attempt = qMax(1, fields.value(QStringLiteral("attempt")).toInt());
    } else {
        const QVariantMap last = latestRun(id);
        attempt = last.isEmpty() ? 1 : qMax(1, last.value(QStringLiteral("attempt")).toInt()) + 1;
    }
    const qint64 ts = now();
    const bool started = status == QLatin1String("running");

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task_run (id, task_id, agent, cwd, workflow, status, attempt, "
        "                      started_at, heartbeat_at, queue_status, position) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(runId);
    query.addBindValue(id);
    query.addBindValue(agent.isNull() ? QStringLiteral("") : agent);
    query.addBindValue(cwd.isNull() ? QStringLiteral("") : cwd);
    query.addBindValue(workflow.isNull() ? QStringLiteral("") : workflow);
    query.addBindValue(status);
    query.addBindValue(attempt);
    query.addBindValue(started ? QVariant(ts) : QVariant());
    query.addBindValue(started ? QVariant(ts) : QVariant());
    // 直跑（不经队列）的 run 也标 running，队列视图据此区分「排队」与「在跑」
    query.addBindValue(started ? QStringLiteral("running") : QStringLiteral("waiting"));
    query.addBindValue(0);
    if (!query.exec()) {
        // 并发窗口里唯一索引是硬门：命中后仍然返回 busy，而不是把 SQL 错误抛给上层
        const QVariantMap active = activeRun(id);
        if (!active.isEmpty()) {
            result.insert(QStringLiteral("ok"), false);
            result.insert(QStringLiteral("code"), QStringLiteral("busy"));
            result.insert(QStringLiteral("runId"), active.value(QStringLiteral("id")));
            result.insert(QStringLiteral("message"), QStringLiteral("同一张卡已有执行中的 run"));
            return result;
        }
        qWarning() << "TaskRunStore: acquire failed:" << query.lastError().text();
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("code"), QStringLiteral("db"));
        result.insert(QStringLiteral("message"), query.lastError().text());
        return result;
    }

    bump();
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("runId"), runId);
    result.insert(QStringLiteral("status"), status);
    return result;
}

bool TaskRunStore::heartbeat(const QString &runId)
{
    if (runId.isEmpty())
        return false;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE task_run SET heartbeat_at = ? "
        "WHERE id = ? AND status IN ('queued','running')"));
    query.addBindValue(now());
    query.addBindValue(runId);
    if (!query.exec())
        return false;
    // 心跳每 10 秒一次，不 bump revision，避免界面跟着抖动
    return query.numRowsAffected() > 0;
}

bool TaskRunStore::release(const QString &runId, const QString &status, const QVariantMap &detail)
{
    if (runId.isEmpty() || !isTerminalStatus(status))
        return false;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;

    const qint64 ts = now();
    QStringList sets{QStringLiteral("status = ?"), QStringLiteral("finished_at = ?"),
                     QStringLiteral("queue_status = 'done'")};
    QVariantList values{status, ts};
    const struct {
        const char *key;
        const char *column;
    } optional[] = {
        {"exitCode", "exit_code"},
        {"lastError", "last_error"},
        {"logPath", "log_path"},
        {"scopeViolation", "scope_violation"},
        {"costMs", "cost_ms"},
        {"handoffPath", "handoff_path"},
        {"role", "role"},
        {"stepIndex", "step_index"},
        {"parentRunId", "parent_run_id"},
    };
    for (const auto &field : optional) {
        const QString key = QLatin1String(field.key);
        if (!detail.contains(key))
            continue;
        sets << QStringLiteral("%1 = ?").arg(QLatin1String(field.column));
        values << detail.value(key);
    }

    // R7-06 成本账：没显式给 costMs 时按 running 时长补（不含排队）
    if (!detail.contains(QStringLiteral("costMs"))) {
        sets << QStringLiteral("cost_ms = MAX(0, (? - COALESCE(started_at, ?)) * 1000)");
        values << ts << ts;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE task_run SET %1 "
        "WHERE id = ? AND status IN ('queued','running')")
                      .arg(sets.join(QStringLiteral(", "))));
    for (const QVariant &value : values)
        query.addBindValue(value);
    query.addBindValue(runId);
    if (!query.exec()) {
        qWarning() << "TaskRunStore: release failed:" << query.lastError().text();
        return false;
    }
    if (query.numRowsAffected() <= 0)
        return false;
    bump();
    return true;
}

QVariantMap TaskRunStore::activeRun(const QString &taskId) const
{
    if (taskId.isEmpty())
        return {};
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return {};

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT %1 FROM task_run "
                                 "WHERE task_id = ? AND status IN ('queued','running') "
                                 "ORDER BY started_at DESC, rowid DESC LIMIT 1")
                      .arg(QLatin1String(kSelectColumns)));
    query.addBindValue(taskId);
    if (!query.exec() || !query.next())
        return {};
    return rowToMap(query);
}

QVariantMap TaskRunStore::run(const QString &runId) const
{
    if (runId.isEmpty())
        return {};
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return {};

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT %1 FROM task_run WHERE id = ?")
                      .arg(QLatin1String(kSelectColumns)));
    query.addBindValue(runId);
    if (!query.exec() || !query.next())
        return {};
    return rowToMap(query);
}

QVariantList TaskRunStore::runsOf(const QString &taskId, int limit) const
{
    QVariantList out;
    if (taskId.isEmpty())
        return out;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return out;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT %1 FROM task_run WHERE task_id = ? "
                                 "ORDER BY started_at DESC, rowid DESC LIMIT ?")
                      .arg(QLatin1String(kSelectColumns)));
    query.addBindValue(taskId);
    query.addBindValue(qBound(1, limit, 100));
    if (!query.exec())
        return out;
    while (query.next())
        out.append(rowToMap(query));
    return out;
}

QVariantMap TaskRunStore::latestRun(const QString &taskId) const
{
    if (taskId.isEmpty())
        return {};
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return {};

    // 活跃 run 优先（started_at 可能为空），其次按开始时间倒序
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT %1 FROM task_run WHERE task_id = ? "
                                  "ORDER BY (status IN ('queued','running')) DESC, "
                                  "         COALESCE(started_at, 0) DESC, rowid DESC LIMIT 1")
                       .arg(QLatin1String(kSelectColumns)));
    query.addBindValue(taskId);
    if (!query.exec() || !query.next())
        return {};
    return rowToMap(query);
}

QVariantMap TaskRunStore::summaryOf(const QString &taskId, int logTailLines) const
{
    QVariantMap out;
    const QVariantMap latest = latestRun(taskId);
    if (latest.isEmpty()) {
        out.insert(QStringLiteral("hasRun"), false);
        return out;
    }
    out = latest;
    out.insert(QStringLiteral("hasRun"), true);

    const qint64 started = latest.value(QStringLiteral("startedAt")).toLongLong();
    const QVariant finishedValue = latest.value(QStringLiteral("finishedAt"));
    const qint64 finished = finishedValue.isNull() ? 0 : finishedValue.toLongLong();
    out.insert(QStringLiteral("durationSec"),
               (started > 0 && finished >= started) ? (finished - started) : 0);

    // 日志末尾 N 行：面板不加载整个文件，失败 run 的日志可能很长
    const QString logPath = latest.value(QStringLiteral("logPath")).toString();
    if (!logPath.isEmpty() && QFile::exists(logPath)) {
        QFile f(logPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QStringList lines =
                QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            const int n = qBound(1, logTailLines, 500);
            out.insert(QStringLiteral("logTail"),
                       lines.mid(qMax(0, lines.size() - n)).join(QLatin1Char('\n')));
        }
    }
    return out;
}

QVariantMap TaskRunStore::costSummary(const QString &taskId) const
{
    QVariantMap out;
    out.insert(QStringLiteral("totalCostMs"), QVariant::fromValue<qint64>(0));
    out.insert(QStringLiteral("runs"), 0);
    if (!m_database || taskId.isEmpty())
        return out;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return out;

    // 只统计真跑完的 run（passed / blocked）：跳过与取消 cost_ms=0，不计入次数。
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(cost_ms), 0), COUNT(*) FROM task_run "
        "WHERE task_id = ? AND status IN ('passed','blocked')"));
    query.addBindValue(taskId);
    if (!query.exec() || !query.next())
        return out;
    out.insert(QStringLiteral("totalCostMs"), query.value(0).toLongLong());
    out.insert(QStringLiteral("runs"), query.value(1).toInt());
    return out;
}

qint64 TaskRunStore::totalCostAll() const
{
    if (!m_database)
        return 0;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return 0;
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
            "SELECT COALESCE(SUM(cost_ms), 0) FROM task_run "
            "WHERE status IN ('passed','blocked')"))
        || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

int TaskRunStore::recoverOrphans(int staleSeconds)
{
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return 0;

    const int stale = qMax(1, staleSeconds);
    const qint64 ts = now();
    // 只回收「在跑」的行：waiting 的队列条目没进程，等再久也不该被杀
    // （它们由构造函数按「队列不跨会话」单独处理）。
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE task_run SET status = 'blocked', queue_status = 'done', finished_at = ?, "
        "  last_error = CASE WHEN last_error IS NULL OR last_error = '' THEN ? ELSE last_error END "
        "WHERE status IN ('queued','running') "
        "  AND COALESCE(queue_status, '') NOT IN ('waiting') "
        "  AND COALESCE(heartbeat_at, started_at, 0) < ?"));
    query.addBindValue(ts);
    query.addBindValue(QStringLiteral("进程中断：心跳超时（> %1 s）").arg(stale));
    query.addBindValue(ts - stale);
    if (!query.exec()) {
        qWarning() << "TaskRunStore: recoverOrphans failed:" << query.lastError().text();
        return 0;
    }
    const int affected = query.numRowsAffected();
    if (affected > 0)
        bump();
    return affected < 0 ? 0 : affected;
}

// ---- 执行队列（R7-01）----

QVariantMap TaskRunStore::enqueue(const QString &taskId, const QVariantMap &fields)
{
    QVariantMap result;
    const QString id = taskId.trimmed();
    if (id.isEmpty()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("code"), QStringLiteral("invalid"));
        result.insert(QStringLiteral("message"), QStringLiteral("任务 id 为空"));
        return result;
    }
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("code"), QStringLiteral("db"));
        result.insert(QStringLiteral("message"), QStringLiteral("数据库未打开"));
        return result;
    }

    // 同卡已有活跃 run（含排队中的自己）时拒绝，避免同卡并发
    const QVariantMap existing = activeRun(id);
    if (!existing.isEmpty()) {
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("code"), QStringLiteral("busy"));
        result.insert(QStringLiteral("runId"), existing.value(QStringLiteral("id")));
        result.insert(QStringLiteral("message"), QStringLiteral("同一张卡已有执行中的 run"));
        return result;
    }

    // position 单调递增：新条目排在队尾
    const int position = [&db]() {
        QSqlQuery maxQuery(db);
        if (maxQuery.exec(QStringLiteral("SELECT COALESCE(MAX(position), 0) FROM task_run"))
            && maxQuery.next())
            return maxQuery.value(0).toInt() + 1;
        return 1;
    }();

    const QString runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString agent = fields.value(QStringLiteral("agent")).toString().trimmed();
    const QString cwd = fields.value(QStringLiteral("cwd")).toString().trimmed();
    const QString workflow = fields.value(QStringLiteral("workflow")).toString().trimmed();

    int attempt;
    if (fields.contains(QStringLiteral("attempt"))) {
        attempt = qMax(1, fields.value(QStringLiteral("attempt")).toInt());
    } else {
        const QVariantMap last = latestRun(id);
        attempt = last.isEmpty() ? 1 : qMax(1, last.value(QStringLiteral("attempt")).toInt()) + 1;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task_run (id, task_id, agent, cwd, workflow, status, attempt, "
        "                      queue_status, position) "
        "VALUES (?, ?, ?, ?, ?, 'queued', ?, 'waiting', ?)"));
    query.addBindValue(runId);
    query.addBindValue(id);
    query.addBindValue(agent.isNull() ? QStringLiteral("") : agent);
    query.addBindValue(cwd.isNull() ? QStringLiteral("") : cwd);
    query.addBindValue(workflow.isNull() ? QStringLiteral("") : workflow);
    query.addBindValue(attempt);
    query.addBindValue(position);
    if (!query.exec()) {
        qWarning() << "TaskRunStore: enqueue failed:" << query.lastError().text();
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("code"), QStringLiteral("db"));
        result.insert(QStringLiteral("message"), query.lastError().text());
        return result;
    }
    bump();
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("runId"), runId);
    result.insert(QStringLiteral("taskId"), id);
    result.insert(QStringLiteral("position"), position);
    return result;
}

QVariantList TaskRunStore::queue(int limit) const
{
    QVariantList out;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return out;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM task_run "
        "WHERE queue_status IN ('waiting','dispatching','running') "
        "ORDER BY (queue_status = 'running') DESC, position ASC, rowid ASC LIMIT ?")
                      .arg(QLatin1String(kSelectColumns)));
    query.addBindValue(qBound(1, limit, 500));
    if (!query.exec())
        return out;
    while (query.next())
        out.append(rowToMap(query));
    return out;
}

QVariantList TaskRunStore::waitingRuns() const
{
    QVariantList out;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return out;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM task_run WHERE queue_status = 'waiting' "
        "ORDER BY position ASC, rowid ASC")
                      .arg(QLatin1String(kSelectColumns)));
    if (!query.exec())
        return out;
    while (query.next())
        out.append(rowToMap(query));
    return out;
}

int TaskRunStore::runningCount() const
{
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return 0;
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM task_run WHERE status = 'running'"))
        || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

bool TaskRunStore::queuePaused() const
{
    if (!m_database)
        return false;
    return m_database->metaValue(QStringLiteral("exec.queuePaused")).trimmed()
           == QLatin1String("1");
}

bool TaskRunStore::setQueuePaused(bool paused)
{
    if (!m_database)
        return false;
    if (!m_database->setMetaValue(QStringLiteral("exec.queuePaused"),
                                  paused ? QStringLiteral("1") : QStringLiteral("0")))
        return false;
    bump();
    return true;
}

int TaskRunStore::concurrency() const
{
    if (!m_database)
        return 1;
    const int value = m_database->metaValue(QStringLiteral("exec.concurrency")).toInt();
    if (value <= 0)
        return 1;
    return qBound(1, value, kMaxConcurrency);
}

bool TaskRunStore::setConcurrency(int value)
{
    if (!m_database)
        return false;
    const int clamped = qBound(1, value, kMaxConcurrency);
    if (!m_database->setMetaValue(QStringLiteral("exec.concurrency"), QString::number(clamped)))
        return false;
    bump();
    return true;
}

bool TaskRunStore::reorder(const QString &runId, int delta)
{
    if (runId.isEmpty() || delta == 0)
        return false;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;

    QSqlQuery current(db);
    current.prepare(QStringLiteral(
        "SELECT position FROM task_run WHERE id = ? AND queue_status = 'waiting'"));
    current.addBindValue(runId);
    if (!current.exec() || !current.next())
        return false;
    const int position = current.value(0).toInt();

    // 只与相邻的 waiting 行交换：running 的不动，避免打断正在跑的 run 的语义
    QSqlQuery neighbor(db);
    neighbor.prepare(QStringLiteral(
        "SELECT id, position FROM task_run WHERE queue_status = 'waiting' AND id <> ? "
        "  AND position %1 ? ORDER BY position %2, rowid %2 LIMIT 1")
                          .arg(delta < 0 ? QStringLiteral("<") : QStringLiteral(">"),
                               delta < 0 ? QStringLiteral("DESC") : QStringLiteral("ASC")));
    neighbor.addBindValue(runId);
    neighbor.addBindValue(position);
    if (!neighbor.exec() || !neighbor.next())
        return false;
    const QString otherId = neighbor.value(0).toString();
    const int otherPosition = neighbor.value(1).toInt();

    QSqlQuery swap(db);
    swap.prepare(QStringLiteral("UPDATE task_run SET position = ? WHERE id = ?"));
    swap.addBindValue(otherPosition);
    swap.addBindValue(runId);
    if (!swap.exec())
        return false;
    QSqlQuery swap2(db);
    swap2.prepare(QStringLiteral("UPDATE task_run SET position = ? WHERE id = ?"));
    swap2.addBindValue(position);
    swap2.addBindValue(otherId);
    if (!swap2.exec())
        return false;
    bump();
    return true;
}

bool TaskRunStore::skip(const QString &runId)
{
    if (runId.isEmpty())
        return false;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;

    // 跳过的条目也必须「结算成终态」：留 waiting 会让它一直被调度器看见。
    // 用 cancelled 而不是 passed —— 它确实没执行，别把「没跑」记成「通过了」。
    QVariantMap detail;
    detail.insert(QStringLiteral("lastError"), QStringLiteral("已跳过（未执行）"));
    detail.insert(QStringLiteral("costMs"), 0);
    return release(runId, QStringLiteral("cancelled"), detail);
}

bool TaskRunStore::beginDispatch(const QString &runId)
{
    if (runId.isEmpty())
        return false;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE task_run SET queue_status = 'dispatching' "
        "WHERE id = ? AND queue_status = 'waiting'"));
    query.addBindValue(runId);
    if (!query.exec())
        return false;
    if (query.numRowsAffected() <= 0)
        return false;
    bump();
    return true;
}

bool TaskRunStore::failQueued(const QString &runId, const QString &message)
{
    QVariantMap detail;
    detail.insert(QStringLiteral("lastError"),
                  message.trimmed().isEmpty() ? QStringLiteral("派发失败") : message);
    detail.insert(QStringLiteral("exitCode"), -1);
    detail.insert(QStringLiteral("costMs"), 0);
    return release(runId, QStringLiteral("blocked"), detail);
}

bool TaskRunStore::annotate(const QString &runId, const QVariantMap &fields)
{
    if (runId.isEmpty())
        return false;
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;

    // 只允许补这几个标注列：它们是「这一步是谁、第几步、属于哪条母 run」的事实，
    // 不参与状态机；命中别的键一律忽略，避免这条入口被用来改 status / cost_ms。
    const struct {
        const char *key;
        const char *column;
    } allowed[] = {
        {"role", "role"},
        {"stepIndex", "step_index"},
        {"parentRunId", "parent_run_id"},
        {"handoffPath", "handoff_path"},
    };
    QStringList sets;
    QVariantList values;
    for (const auto &field : allowed) {
        const QString key = QLatin1String(field.key);
        if (!fields.contains(key))
            continue;
        sets << QStringLiteral("%1 = ?").arg(QLatin1String(field.column));
        values << fields.value(key);
    }
    if (sets.isEmpty())
        return true; // 没有可写的标注，不算失败

    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE task_run SET %1 WHERE id = ?")
                      .arg(sets.join(QStringLiteral(", "))));
    for (const QVariant &value : values)
        query.addBindValue(value);
    query.addBindValue(runId);
    if (!query.exec()) {
        qWarning() << "TaskRunStore: annotate failed:" << query.lastError().text();
        return false;
    }
    if (query.numRowsAffected() <= 0)
        return false;
    bump();
    return true;
}
