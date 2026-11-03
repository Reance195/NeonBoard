#include "TaskExtra.h"

#include "Database.h"
#include "TaskModel.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>

TaskExtra::TaskExtra(Database *database, TaskModel *tasks, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_tasks(tasks)
{
    if (m_tasks) {
        connect(m_tasks, &TaskModel::taskRemoved, this, [this](const QString &id) {
            removeRowFor(id);
            m_spawned.remove(id);
            bump();
        });
        // 完成重复任务时派生下一期（撤销完成时由 TaskUndo 经 takeSpawned 连带删除）
        connect(m_tasks, &TaskModel::taskCompleted, this, &TaskExtra::onTaskCompleted);
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

QDateTime TaskExtra::nextOccurrence(const QString &rrule, const QDateTime &from)
{
    QHash<QString, QString> parts;
    for (const QString &part : rrule.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq > 0)
            parts.insert(part.left(eq).trimmed().toUpper(), part.mid(eq + 1).trimmed());
    }
    const QString freq = parts.value(QStringLiteral("FREQ")).toUpper();
    int interval = parts.value(QStringLiteral("INTERVAL"), QStringLiteral("1")).toInt();
    if (interval < 1)
        interval = 1;

    QDate d = from.date();
    if (freq == QLatin1String("DAILY")) {
        d = d.addDays(interval);
    } else if (freq == QLatin1String("WEEKLY")) {
        const QString byday = parts.value(QStringLiteral("BYDAY"));
        if (!byday.isEmpty()) {
            static const QHash<QString, int> kWeekday = {
                {QStringLiteral("MO"), 1}, {QStringLiteral("TU"), 2}, {QStringLiteral("WE"), 3},
                {QStringLiteral("TH"), 4}, {QStringLiteral("FR"), 5}, {QStringLiteral("SA"), 6},
                {QStringLiteral("SU"), 7}
            };
            QList<int> days;
            for (const QString &code : byday.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                const int wd = kWeekday.value(code.trimmed().toUpper(), 0);
                if (wd > 0)
                    days.append(wd);
            }
            if (days.isEmpty())
                return {};
            std::sort(days.begin(), days.end());
            // 严格晚于锚点日：向后 7 天内找第一个命中的星期
            QDate found;
            for (int i = 1; i <= 7; ++i) {
                const QDate candidate = d.addDays(i);
                if (days.contains(candidate.dayOfWeek())) {
                    found = candidate;
                    break;
                }
            }
            if (!found.isValid())
                return {};
            d = found;
        } else {
            d = d.addDays(7 * interval);
        }
    } else if (freq == QLatin1String("MONTHLY")) {
        // 月末钳制：1/31 + 1月 = 2/28（每月都按原始日钳制，不用上一次的钳制结果）
        const int day = d.day();
        for (int m = 1; m <= interval; ++m) {
            const QDate next = d.addMonths(1);
            d = QDate(next.year(), next.month(), qMin(day, next.daysInMonth()));
        }
    } else if (freq == QLatin1String("YEARLY")) {
        const int day = d.day();
        const QDate next = d.addYears(interval);
        d = QDate(next.year(), next.month(), qMin(day, next.daysInMonth()));
    } else {
        return {};
    }
    return QDateTime(d, from.time());
}

QString TaskExtra::takeSpawned(const QString &originalId)
{
    return m_spawned.take(originalId);
}

void TaskExtra::onTaskCompleted(const QString &taskId)
{
    if (!m_tasks)
        return;
    const QVariantMap extra = rowOf(taskId);
    const QString rrule = extra.value(QStringLiteral("rrule")).toString().trimmed();
    if (rrule.isEmpty())
        return;

    const QVariantMap task = m_tasks->taskById(taskId);
    if (task.isEmpty())
        return;
    // 只对「刚完成」派生：completedAt 在最近 10 秒内。
    // 避免撤销删除恢复已完成卡（completedAt 是旧值）时重复派生。
    const qint64 completedAt = task.value(QStringLiteral("completedAt")).toLongLong();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (completedAt <= 0 || now - completedAt > 10)
        return;

    // 锚点：有到期日按到期日推，没有按当前时刻推
    const qint64 dueAt = task.value(QStringLiteral("dueAt")).toLongLong();
    const QDateTime anchor = dueAt > 0
        ? QDateTime::fromSecsSinceEpoch(dueAt)
        : QDateTime::currentDateTime();
    const QDateTime next = nextOccurrence(rrule, anchor);
    if (!next.isValid()) {
        qWarning().noquote() << "TaskExtra: 无法解析 RRULE，跳过派生:" << rrule;
        return;
    }

    const QString newId = m_tasks->addTask(
        task.value(QStringLiteral("title")).toString(),
        QStringLiteral("todo"),
        task.value(QStringLiteral("priority")).toInt(),
        task.value(QStringLiteral("tagsFull")).toString(),
        next.toSecsSinceEpoch());
    if (newId.isEmpty())
        return;
    const QString note = task.value(QStringLiteral("note")).toString();
    if (!note.isEmpty())
        m_tasks->updateTask(newId, {{QStringLiteral("note"), note}});
    setRrule(newId, rrule);

    // 提醒偏移继承：原卡有提醒且有到期日时，下一期按相同相对偏移提醒
    const qint64 remindAt = extra.value(QStringLiteral("remindAt")).toLongLong();
    if (remindAt > 0 && dueAt > 0)
        setReminder(newId, next.toSecsSinceEpoch() + (remindAt - dueAt));

    m_spawned.insert(taskId, newId);
    qInfo().noquote() << "TaskExtra: spawned next occurrence" << newId
                      << "for" << taskId << "due" << next.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    bump();
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
