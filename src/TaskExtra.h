#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;
class TaskModel;

// 任务附加属性门面（提醒时刻 / 已通知标记 / 重复规则）。
//
// 设计要点（与 task_link 边表同一先例，见 AGENTS.md）：
//   1. 属性存在独立侧表 task_extra(task_id, remind_at, notified_at, rrule)，
//      不往 task 表加列、不升 user_version，老版本程序照常能开库。
//   2. 侧表读写只走本门面；QML 侧经上下文属性 taskExtra 调用。
//   3. 任务数据只读走 TaskModel 公开接口（allTasks），不绕过模型。
//   4. 任务删除时同步清行（订阅 TaskModel::taskRemoved）。
class TaskExtra : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    TaskExtra(Database *database, TaskModel *tasks, QObject *parent = nullptr);

    int revision() const;

    // 设置提醒时刻（epoch 秒）；0 = 清除提醒。
    // 改成未来时间时重置 notified_at，允许再次触发。
    Q_INVOKABLE bool setReminder(const QString &taskId, qint64 remindAt);
    // 设置重复规则（RFC 5545 RRULE 字符串）；空串 = 清除
    Q_INVOKABLE bool setRrule(const QString &taskId, const QString &rrule);
    // 标记已通知（防重复触发）
    Q_INVOKABLE bool markNotified(const QString &taskId);

    // {remindAt, notifiedAt, rrule}；无行返回全零 / 空
    Q_INVOKABLE QVariantMap extraOf(const QString &taskId) const;

    // 应触发的提醒列表：有效时刻 = remind_at（显式设置优先），
    // 未设提醒但有 due_at 时按逾期提醒处理。
    // 条件：有效时刻 <= now 且 notified_at = 0 且任务未完成。
    // 启动补发漏提醒靠同一条查询天然覆盖（应用没跑期间到期的）。
    Q_INVOKABLE QVariantList dueReminders(qint64 now) const;

signals:
    // 侧表变化（QML 侧靠 revision 建立依赖）
    void changed();

private:
    QVariantMap rowOf(const QString &taskId) const;
    void removeRowFor(const QString &taskId);
    void bump();

    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    int m_revision = 0;
};
