#include "TaskExtra.h"

#include "Database.h"
#include "TaskModel.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

TaskExtra::TaskExtra(Database *database, TaskModel *tasks, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_tasks(tasks)
{
    if (m_tasks) {
        connect(m_tasks, &TaskModel::taskRemoved, this, [this](const QString &id) {
            removeRowFor(id);
            bump();
        });
    }
}

int TaskExtra::revision() const
{
    return m_revision;
}

void TaskExtra::bump()
{
    ++m_revision;
    emit changed();
}

QVariantMap TaskExtra::rowOf(const QString &taskId) const
{
    QVariantMap empty;
    empty.insert(QStringLiteral("remindAt"), 0);
    empty.insert(QStringLiteral("notifiedAt"), 0);
    empty.insert(QStringLiteral("rrule"), QString());
    if (!m_database || taskId.isEmpty())
        return empty;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return empty;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT remind_at, notified_at, rrule FROM task_extra WHERE task_id = ?"));
    query.addBindValue(taskId);
    if (!query.exec() || !query.next())
        return empty;

    return {
        {QStringLiteral("remindAt"), query.value(0).toLongLong()},
        {QStringLiteral("notifiedAt"), query.value(1).toLongLong()},
        {QStringLiteral("rrule"), query.value(2).toString()}
    };
}

bool TaskExtra::setReminder(const QString &taskId, qint64 remindAt)
{
    if (!m_database || taskId.isEmpty())
        return false;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task_extra (task_id, remind_at, notified_at, rrule, created_at) "
        "VALUES (?, ?, 0, '', ?) "
        "ON CONFLICT(task_id) DO UPDATE SET "
        "remind_at = excluded.remind_at," // 改成未来时间才重置通知标记；清除提醒（0）不动 notified_at
        "notified_at = CASE WHEN excluded.remind_at > ? THEN 0 ELSE notified_at END"));
    query.addBindValue(taskId);
    query.addBindValue(remindAt);
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        qWarning() << "TaskExtra: setReminder failed:" << query.lastError().text();
        return false;
    }
    bump();
    return true;
}

bool TaskExtra::setRrule(const QString &taskId, const QString &rrule)
{
    if (!m_database || taskId.isEmpty())
        return false;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task_extra (task_id, remind_at, notified_at, rrule, created_at) "
        "VALUES (?, 0, 0, ?, ?) "
        "ON CONFLICT(task_id) DO UPDATE SET rrule = excluded.rrule"));
    query.addBindValue(taskId);
    query.addBindValue(rrule);
    query.addBindValue(now);
    if (!query.exec()) {
        qWarning() << "TaskExtra: setRrule failed:" << query.lastError().text();
        return false;
    }
    bump();
    return true;
}

bool TaskExtra::markNotified(const QString &taskId)
{
    if (!m_database || taskId.isEmpty())
        return false;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task_extra (task_id, remind_at, notified_at, rrule, created_at) "
        "VALUES (?, 0, ?, '', ?) "
        "ON CONFLICT(task_id) DO UPDATE SET notified_at = excluded.notified_at"));
    query.addBindValue(taskId);
    query.addBindValue(now);
    query.addBindValue(now);
    if (!query.exec()) {
        qWarning() << "TaskExtra: markNotified failed:" << query.lastError().text();
        return false;
    }
    bump();
    return true;
}

QVariantMap TaskExtra::extraOf(const QString &taskId) const
{
    return rowOf(taskId);
}

QVariantList TaskExtra::dueReminders(qint64 now) const
{
    QVariantList out;
    if (!m_tasks)
        return out;

    const QVariantList tasks = m_tasks->allTasks();
    for (const QVariant &value : tasks) {
        const QVariantMap task = value.toMap();
        const QString id = task.value(QStringLiteral("id")).toString();
        if (task.value(QStringLiteral("status")).toString() == QLatin1String("done"))
            continue;

        const QVariantMap extra = rowOf(id);
        if (extra.value(QStringLiteral("notifiedAt")).toLongLong() > 0)
            continue;

        const qint64 remindAt = extra.value(QStringLiteral("remindAt")).toLongLong();
        const qint64 dueAt = task.value(QStringLiteral("dueAt")).toLongLong();
        const qint64 effective = remindAt > 0 ? remindAt : dueAt;
        if (effective > 0 && effective <= now) {
            QVariantMap item = task;
            item.insert(QStringLiteral("effectiveRemindAt"), effective);
            out.append(item);
        }
    }
    return out;
}

void TaskExtra::removeRowFor(const QString &taskId)
{
    if (!m_database || taskId.isEmpty())
        return;

    const QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
    if (!db.isOpen())
        return;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM task_extra WHERE task_id = ?"));
    query.addBindValue(taskId);
    if (!query.exec())
        qWarning() << "TaskExtra: removeRowFor failed:" << query.lastError().text();
}
