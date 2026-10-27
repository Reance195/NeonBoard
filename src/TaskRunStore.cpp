#include "TaskRunStore.h"

#include "Database.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace {

const char kSelectColumns[] =
    "id, task_id, agent, cwd, workflow, status, attempt, started_at, finished_at, "
    "heartbeat_at, exit_code, last_error, log_path, scope_violation";

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
    return map;
}

} // namespace

TaskRunStore::TaskRunStore(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
{
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
    const int attempt = qMax(1, fields.value(QStringLiteral("attempt"), 1).toInt());
    const qint64 ts = now();
    const bool started = status == QLatin1String("running");

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task_run (id, task_id, agent, cwd, workflow, status, attempt, "
        "                      started_at, heartbeat_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(runId);
    query.addBindValue(id);
    query.addBindValue(agent.isNull() ? QStringLiteral("") : agent);
    query.addBindValue(cwd.isNull() ? QStringLiteral("") : cwd);
    query.addBindValue(workflow.isNull() ? QStringLiteral("") : workflow);
    query.addBindValue(status);
    query.addBindValue(attempt);
    query.addBindValue(started ? QVariant(ts) : QVariant());
    query.addBindValue(started ? QVariant(ts) : QVariant());
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

    QStringList sets{QStringLiteral("status = ?"), QStringLiteral("finished_at = ?")};
    QVariantList values{status, now()};
    const struct {
        const char *key;
        const char *column;
    } optional[] = {
        {"exitCode", "exit_code"},
        {"lastError", "last_error"},
        {"logPath", "log_path"},
        {"scopeViolation", "scope_violation"},
    };
    for (const auto &field : optional) {
        const QString key = QLatin1String(field.key);
        if (!detail.contains(key))
            continue;
        sets << QStringLiteral("%1 = ?").arg(QLatin1String(field.column));
        values << detail.value(key);
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

int TaskRunStore::recoverOrphans(int staleSeconds)
{
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return 0;

    const int stale = qMax(1, staleSeconds);
    const qint64 ts = now();
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE task_run SET status = 'blocked', finished_at = ?, "
        "  last_error = CASE WHEN last_error IS NULL OR last_error = '' THEN ? ELSE last_error END "
        "WHERE status IN ('queued','running') "
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
