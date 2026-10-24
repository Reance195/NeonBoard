#pragma once

#include <QDate>
#include <QHash>
#include <QObject>
#include <QVariantList>

class Database;
class TaskModel;

// 任务完成统计门面：热力图 / 连续天数 / 周吞吐。
// 数据只来源于 task.completed_at，不额外建表、不改 schema。
class TaskStats : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList heatmap READ heatmap NOTIFY statsChanged)
    Q_PROPERTY(QVariantList weekly READ weekly NOTIFY statsChanged)
    Q_PROPERTY(int streakDays READ streakDays NOTIFY statsChanged)
    Q_PROPERTY(int totalDone READ totalDone NOTIFY statsChanged)
    Q_PROPERTY(int bestDay READ bestDay NOTIFY statsChanged)

public:
    explicit TaskStats(Database *database, TaskModel *model, QObject *parent = nullptr);

    QVariantList heatmap() const { return m_heatmap; }
    QVariantList weekly() const { return m_weekly; }
    int streakDays() const { return m_streakDays; }
    int totalDone() const { return m_totalDone; }
    int bestDay() const { return m_bestDay; }

    Q_INVOKABLE void refresh();

signals:
    void statsChanged();

private:
    void rebuild();

    Database *m_database = nullptr;
    QHash<QDate, int> m_byDay;
    QVariantList m_heatmap;
    QVariantList m_weekly;
    int m_streakDays = 0;
    int m_totalDone = 0;
    int m_bestDay = 0;
};
