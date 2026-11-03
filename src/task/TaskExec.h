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
    // 校验失败返回 false（goal 超长、acceptance/scope 非字符串数组、超时越界、
    // cwd 非绝对路径 / 目录不存在等）。cwd 为空表示「尚未指定工作目录」。
    Q_INVOKABLE bool setExec(const QString &taskId, const QVariantMap &fields);

    // 清空整行（回到「未定义执行参数」）。
    Q_INVOKABLE bool clearExec(const QString &taskId);

    // ---- 角色链步骤数组（R7-03）----
    //
    // workflow 仍是「预设名」（fix / feature / hotfix / docs / review / delivery）；
    // 卡片可覆盖每步参数（换 agent / 补 acceptance / 补 scope），但不能跳步改变交付物契约。
    // 覆盖项存 meta 表 `exec.workflowSteps.<taskId>`（JSON），**不往 task_exec 加列**：
    // 侧表列变更要走 Database::ensureColumn，而 R7-03 的 scope 不含 Database.cpp；
    // 用 meta 同样不升 user_version。
    //
    // 每条步骤：{role, agent, prompt?, acceptance[], scope[]}
    //   role      pm / architect / engineer / qa / writer / orchestrator
    //   agent     该步用哪个 profile（空则用卡片的 agent）
    //   prompt    该步的任务描述（空则用卡片的 goal）
    //   acceptance / scope 该步独立验收与边界（复用 R6-05）
    Q_INVOKABLE QVariantList stepsOf(const QString &taskId) const;
    Q_INVOKABLE bool setSteps(const QString &taskId, const QVariantList &steps);
    Q_INVOKABLE bool clearSteps(const QString &taskId);

    // 预设步骤（纯函数，供 QML / 测试直接用）：未知 workflow 返回空表。
    static QVariantList presetSteps(const QString &workflow);

    // 校验并归一化步骤数组：{ok, steps[], error?}
    // 未知角色 / acceptance 非字符串数组 / 空步骤数组都算非法。
    static QVariantMap normalizeSteps(const QVariantList &input);

    static bool isKnownRole(const QString &role);

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
