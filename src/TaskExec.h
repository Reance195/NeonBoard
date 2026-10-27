#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;
class TaskModel;

// 任务执行定义门面（M0）。
//
// 设计要点（沿用 task_link / task_extra 的侧表先例）：
//   1. 执行定义存在独立侧表 task_exec(task_id, goal, acceptance, scope, agent, cwd,
//      workflow, timeout_min, max_attempts)，**不往 task 表加列、不升 user_version**，
//      老版本程序开同一个库不受影响（认不出这张表而已）。
//   2. 定义与运行状态分离：这里只放「人定的执行参数与验收」，运行态（run_id /
//      心跳 / lastError）属于 executor，不进本表。
//   3. acceptance / scope 以 JSON 数组文本存储，QML 侧拿到的是 QVariantList。
//   4. 任务删除时同步清行（订阅 TaskModel::taskRemoved）。
//
// 字段语义见 docs/product-design.md §3、docs/task-card.md。
class TaskExec : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    TaskExec(Database *database, TaskModel *tasks, QObject *parent = nullptr);

    int revision() const;

    // {goal, acceptance[], scope[], agent, cwd, workflow, timeoutMin, maxAttempts}
    // 无行时返回默认值（空串 / 空列表 / 1 / 1）。
    Q_INVOKABLE QVariantMap execOf(const QString &taskId) const;

    // 局部更新：只写 fields 里出现的键；未知键忽略。
    // 校验失败返回 false（goal 超长、acceptance/scope 非字符串数组、超时越界等）。
    Q_INVOKABLE bool setExec(const QString &taskId, const QVariantMap &fields);

    // 清空整行（回到「未定义执行参数」）。
    Q_INVOKABLE bool clearExec(const QString &taskId);

signals:
    void changed();

private:
    QVariantMap emptyMap() const;
    void removeRowFor(const QString &taskId);
    void bump();

    Database *m_database = nullptr;
    TaskModel *m_tasks = nullptr;
    int m_revision = 0;
};
