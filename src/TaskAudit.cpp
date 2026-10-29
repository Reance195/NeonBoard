#include "TaskAudit.h"

#include "Database.h"
#include "TaskModel.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>

TaskAudit::TaskAudit(Database *db, TaskModel *tasks, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
    if (!tasks)
        return;

    // 只挂生命周期信号：taskUpdated 是「有字段动了」的粗粒度通知，
    // 每次移动都会跟着发一遍，记进时间线只会把真正想看的几件事淹掉。
    connect(tasks, &TaskModel::taskCreated, this,
            [this](const QString &id, const QString &title) {
                record(id, QStringLiteral("created"), title);
            });
    connect(tasks, &TaskModel::taskCompleted, this, [this](const QString &id) {
        record(id, QStringLiteral("completed"));
    });
    connect(tasks, &TaskModel::taskMoved, this,
            [this](const QString &id, const QString &from, const QString &to) {
                // 搬到「已完成」会紧接着发 taskCompleted：只留完成那一条，
                // 否则同一个动作在时间线上出现两次，读起来像是点了两下。
                if (to == QLatin1String("done"))
                    return;
                record(id, from == QLatin1String("done") ? QStringLiteral("restored")
                                                         : QStringLiteral("moved"),
                       from + QStringLiteral(" → ") + to);
            });
    connect(tasks, &TaskModel::taskRemoved, this, [this](const QString &id) {
        record(id, QStringLiteral("removed"));
    });
}

QVariantList TaskAudit::eventsOf(const QString &taskId, int limit) const
{
    QVariantList out;
    if (!m_db || taskId.isEmpty())
        return out;
    const QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    if (!db.isOpen())
        return out;

    QSqlQuery query(db);
    // 顺序用自增 id：同一秒里的两条事件（完成又恢复）at 会撞在一起，
    // 按 at 排则每次刷新顺序都可能不一样。
    QString sql = QStringLiteral(
            "SELECT id, kind, detail, at FROM task_event WHERE task_id = ? ORDER BY id DESC");
    if (limit > 0)
        sql += QStringLiteral(" LIMIT %1").arg(limit);
    query.prepare(sql);
    query.addBindValue(taskId);
    if (!query.exec())
        return out;

    while (query.next()) {
        const QString kind = query.value(1).toString();
        out.append(QVariantMap{
            { QStringLiteral("id"), query.value(0).toLongLong() },
            { QStringLiteral("kind"), kind },
            { QStringLiteral("label"), labelOf(kind) },
            { QStringLiteral("detail"), query.value(2).toString() },
            { QStringLiteral("at"), query.value(3).toLongLong() },
        });
    }
    return out;
}

int TaskAudit::countOf(const QString &taskId) const
{
    if (!m_db || taskId.isEmpty())
        return 0;
    const QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    if (!db.isOpen())
        return 0;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM task_event WHERE task_id = ?"));
    query.addBindValue(taskId);
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toInt();
}

QString TaskAudit::labelOf(const QString &kind) const
{
    if (kind == QLatin1String("created"))
        return QStringLiteral("创建");
    if (kind == QLatin1String("moved"))
        return QStringLiteral("移动");
    if (kind == QLatin1String("completed"))
        return QStringLiteral("完成");
    if (kind == QLatin1String("restored"))
        return QStringLiteral("恢复");
    if (kind == QLatin1String("removed"))
        return QStringLiteral("删除");
    return kind;
}

bool TaskAudit::record(const QString &taskId, const QString &kind, const QString &detail)
{
    if (!m_db || taskId.isEmpty() || kind.isEmpty())
        return false;
    const QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    if (!db.isOpen())
        return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
            "INSERT INTO task_event (task_id, kind, detail, at) VALUES (?, ?, ?, ?)"));
    query.addBindValue(taskId);
    query.addBindValue(kind);
    // 默认参数是「空 QString」= null，直接绑进去会变成 SQL NULL，
    // 撞上 detail NOT NULL 的约束 —— 于是「完成」这种没有 detail 的事件一条都写不进去。
    // 这里必须换成非 null 的空串（task_extra.rrule 踩过同一个坑）。
    query.addBindValue(detail.isNull() ? QString::fromLatin1("") : detail);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!query.exec()) {
        qWarning() << "TaskAudit: 事件写入失败" << query.lastError().text();
        return false;
    }

    trim(taskId);
    ++m_revision;
    emit changed();
    return true;
}

void TaskAudit::trim(const QString &taskId)
{
    // 单卡事件封顶：一张常年反复拖的卡不该把库撑大。
    // 删最旧的（id 最小），保留最近 kMaxEventsPerTask 条。
    const QSqlDatabase db = QSqlDatabase::database(m_db->connectionName());
    if (!db.isOpen())
        return;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
            "DELETE FROM task_event WHERE task_id = ? AND id NOT IN ("
            "  SELECT id FROM task_event WHERE task_id = ? ORDER BY id DESC LIMIT ?)"));
    query.addBindValue(taskId);
    query.addBindValue(taskId);
    query.addBindValue(kMaxEventsPerTask);
    query.exec();
}
