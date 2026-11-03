#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;

// 预设 workflow 门面（R7-04）。
//
// 职责边界（docs/r7-dispatcher-plan.md §R7-04）：
//   1. 预设**存在 board.db 的 meta 表**（`exec.presets`，JSON 数组），可编辑；
//      删掉该键即恢复内置六条 —— 与 AgentProfile 的做法一致，不升 user_version、
//      不动 task 表。内置定义与 TaskExec::presetSteps 是同一份事实的两处表达：
//      本类负责「有哪些预设、怎么选、能不能跳过某步」，步骤内容归 TaskExec。
//   2. **orchestrator 选 workflow**：按 triage 草案的 estimate.size（或卡片规模兜底）
//      映射到预设 —— 小 → fix，中 → feature 的裁剪版，大 → feature/delivery。
//      映射表本身也存 meta（`exec.presets.routing`），可编辑。
//   3. **跳过规则**：小任务可跳 pm / architect，**不允许跳 qa**（验收是最后防线）。
//      这条规则写死在代码里而不是配置里 —— 能配的规则一旦关掉就没人再打开。
//   4. orchestrator 步骤收口时写 summary.md（交接物目录下），给出「建议下一步」。
//
// 不做的：不自动改看板列、不自动 push、不自动重试（重试仍在 TaskRunner 的 maxAttempts 里）。
class WorkflowPresets : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY changed)

public:
    explicit WorkflowPresets(Database *database, QObject *parent = nullptr);

    int revision() const;

    // 当前生效的预设列表（用户改过用 meta，否则内置）。
    // 每条：{key, label, desc, steps:[role…], skippable:[role…], builtin:bool}
    QVariantList presets() const;
    // 单个预设；未知 key 返回空 map。
    Q_INVOKABLE QVariantMap presetOf(const QString &key) const;

    // 覆盖或追加一条预设：key / label / steps 合法才接受。
    // steps 必须是已知角色数组，且必须包含 engineer / writer / orchestrator 之一
    // （保证有产出或收口；review 的纯 orchestrator 也算合法）。
    // skippable 只能含 pm / architect —— qa / engineer / writer / orchestrator 永不入列。
    Q_INVOKABLE bool savePreset(const QVariantMap &preset);
    Q_INVOKABLE bool removePreset(const QString &key);
    // 清掉用户改动，恢复内置六条。
    Q_INVOKABLE bool resetPresets();

    // 规模 → workflow 的映射表（meta `exec.presets.routing`，可编辑）。
    // 内置：S→fix / M→feature / L→delivery / XL→delivery。
    Q_INVOKABLE QVariantMap routing() const;
    // 覆盖映射表：键取 S/M/L/XL，值必须是已登记的预设 key；非法拒绝。
    Q_INVOKABLE bool saveRouting(const QVariantMap &map);

    // 按规模选预设：size 取 "S" / "M" / "L" / "XL"（triage 草案的 estimate.size），
    // 也接受数字 1..4。未知 / 空 → 按卡片规模兜底（priority 与标题长度）。
    Q_INVOKABLE QString routeFor(const QString &size, const QVariantMap &card = {}) const;

    // 某预设在某规模下「可以跳过哪些步」：返回角色数组（不含 qa）。
    // qa 永远不在此列 —— 小任务也不许跳验收。
    Q_INVOKABLE QVariantList skippableSteps(const QString &key, const QString &size) const;

    // 把预设展开成实际步骤数组（应用规模裁剪后的结果，供 WorkflowRunner 用）：
    // 小任务按 skippableSteps 去掉 pm/architect；engineer / qa 保留。
    Q_INVOKABLE QVariantList stepsFor(const QString &key, const QString &size) const;

    // orchestrator 收口：拼 summary.md 内容（纯函数，供测试直接用）。
    // steps 每项：{role, status, costMs?, handoffPath?}
    static QString buildSummary(const QString &taskId, const QString &workflow,
                                const QVariantList &steps, const QString &cardTitle);

    static QVariantList builtinPresets();
    static QVariantMap builtinRouting();

    // 是否「小任务」规模（S / 1）：只有小任务才允许跳 pm / architect。
    static bool isSmallSize(const QString &size);
    // 规范化规模：返回 S/M/L/XL，识别不了返回空串。
    static QString normalizeSize(const QString &size);
    // 受保护角色：这些角色永远不能出现在 skippable 里（qa 是验收最后防线）。
    static bool isProtectedRole(const QString &role);
    // 校验并规范化一条预设：非法返回空 map（与 savePreset 同一口径，供测试直接调）。
    static QVariantMap normalizePreset(const QVariantMap &raw);

signals:
    void changed();

private:
    bool writePresets(const QVariantList &presets);
    void bump();

    Database *m_database = nullptr;
    int m_revision = 0;
};
