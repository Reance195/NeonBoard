#include "TaskStats.h"

#include "Database.h"
#include "TaskModel.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimer>

namespace {
constexpr int kHeatmapWeeks = 13;
constexpr int kWeeklyWeeks = 8;

int levelFor(int count)
{
    if (count <= 0)
        return 0;
    if (count == 1)
        return 1;
    if (count == 2)
        return 2;
    if (count <= 4)
        return 3;
    return 4;
}
} // namespace

TaskStats::TaskStats(Database *database, TaskModel *model, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_model(model)
{
    if (model)
        connect(model, &TaskModel::countsChanged, this, &TaskStats::refresh);
    refresh();

    // 逾期数会随时间自己变：到点的任务就算逾期，哪怕任务集一个字没动。
    // 只靠 countsChanged 刷新的话，这个数字会一直停在打开应用那一刻的值。
    // 每分钟重算一次（一条 COUNT，代价可忽略）。
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &TaskStats::refresh);
    timer->start(60 * 1000);
}

void TaskStats::refresh()
{
    m_byDay.clear();
    m_totalDone = 0;
    m_bestDay = 0;
    m_overdueCount = 0;

    if (m_database) {
        QSqlDatabase db = QSqlDatabase::database(m_database->connectionName());
        if (db.isOpen()) {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral(
                    "SELECT completed_at FROM task WHERE status = 'done' AND completed_at > 0"))) {
                while (query.next()) {
                    const qint64 secs = query.value(0).toLongLong();
                    if (secs <= 0)
                        continue;
                    const QDate day = QDateTime::fromSecsSinceEpoch(secs).date();
                    ++m_byDay[day];
                    ++m_totalDone;
                }
            }

            // 逾期数（R11-03）：未完成 + 设了到期时间 + 已经过期。
            // 单独一条查询，不复用上面的游标 —— 上面那个还在迭代。
            QSqlQuery overdue(db);
            overdue.prepare(QStringLiteral(
                "SELECT COUNT(*) FROM task WHERE status != 'done' AND due_at > 0 AND due_at < ?"));
            overdue.addBindValue(QDateTime::currentSecsSinceEpoch());
            if (overdue.exec() && overdue.next())
                m_overdueCount = overdue.value(0).toInt();
        }
    }

    for (auto it = m_byDay.constBegin(); it != m_byDay.constEnd(); ++it)
        m_bestDay = qMax(m_bestDay, it.value());

    rebuild();
    emit statsChanged();
}

void TaskStats::rebuild()
{
    const QDate today = QDate::currentDate();

    // 连续天数：今天还没完成则从昨天回推（GitHub 口径），停在第一个空日
    QDate cursor = m_byDay.value(today, 0) > 0 ? today : today.addDays(-1);
    int streak = 0;
    while (m_byDay.value(cursor, 0) > 0) {
        ++streak;
        cursor = cursor.addDays(-1);
    }
    m_streakDays = streak;

    // 热力图：13 列 × 7 行，列=周（周一起），最后一列是本周
    const QDate thisMonday = today.addDays(1 - today.dayOfWeek());
    const QDate firstMonday = thisMonday.addDays(-7 * (kHeatmapWeeks - 1));

    QVariantList cells;
    cells.reserve(kHeatmapWeeks * 7);
    for (int week = 0; week < kHeatmapWeeks; ++week) {
        for (int dow = 0; dow < 7; ++dow) {
            const QDate day = firstMonday.addDays(week * 7 + dow);
            const int count = m_byDay.value(day, 0);
            const bool future = day > today;
            QVariantMap cell;
            cell.insert(QStringLiteral("date"), day.toString(QStringLiteral("yyyy-MM-dd")));
            cell.insert(QStringLiteral("count"), future ? 0 : count);
            cell.insert(QStringLiteral("level"), future ? -1 : levelFor(count));
            cells.append(cell);
        }
    }
    m_heatmap = cells;

    // 周吞吐：近 8 周（含本周），周一起算
    QVariantList weeks;
    weeks.reserve(kWeeklyWeeks);
    for (int i = kWeeklyWeeks - 1; i >= 0; --i) {
        const QDate monday = thisMonday.addDays(-7 * i);
        int count = 0;
        for (int d = 0; d < 7; ++d)
            count += m_byDay.value(monday.addDays(d), 0);
        QVariantMap item;
        item.insert(QStringLiteral("label"), monday.toString(QStringLiteral("MM/dd")));
        item.insert(QStringLiteral("count"), count);
        weeks.append(item);
    }
    m_weekly = weeks;
}
