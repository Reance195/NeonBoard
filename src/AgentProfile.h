#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Database;

// Agent profile：把不同 CLI 的差异收敛成一条配置（R6-03）。
//
// 设计要点：
//   1. 内置 opencode / claude / cursor-agent 三条（docs/agent-profile.md 实测过）；
//      codex 未实测，不内置，避免把「想当然」的参数带进执行路径。
//   2. 用户增删改后的整份列表存 meta 表 exec.agentProfiles（JSON 数组），
//      删掉该键即恢复内置；不升 user_version、不动 task 表。
//   3. 命令行只以 argv 拼装（exe + args + readonlyArgs + prompt），**不经过 shell**；
//      commandLine() 仅用于界面展示，执行侧必须用 argv()。
//   4. 执行器总开关默认关；开启前看板行为与未引入执行器时一致。
class AgentProfile : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(bool executorEnabled READ executorEnabled NOTIFY changed)
    Q_PROPERTY(QString defaultAgent READ defaultAgent NOTIFY changed)
    Q_PROPERTY(QStringList trustedCwds READ trustedCwds NOTIFY changed)

public:
    explicit AgentProfile(Database *database, QObject *parent = nullptr);

    int revision() const;

    // 当前生效的 profile 列表；用户改过用 meta，否则内置。
    QVariantList profiles() const;
    // 未知 id 返回空 map。
    Q_INVOKABLE QVariantMap profileOf(const QString &id) const;

    // 按 id 覆盖或追加；id / exe / promptMode / output / 超时非法返回 false。
    Q_INVOKABLE bool saveProfile(const QVariantMap &profile);
    Q_INVOKABLE bool removeProfile(const QString &id);
    // 清掉用户改动，恢复内置三条。
    Q_INVOKABLE bool resetProfiles();

    // 执行用 argv：exe + args (+ readonlyArgs) (+ prompt，当 promptMode=arg)。
    // promptMode=stdin 时 prompt 不出现在 argv 里，由执行器写标准输入。
    Q_INVOKABLE QStringList argv(const QString &id, bool readonly = false,
                                 const QString &prompt = QString()) const;
    // 展示用命令行（只用于确认框，不经过 shell）。
    Q_INVOKABLE QString commandLine(const QString &id, bool readonly = false,
                                    const QString &prompt = QString()) const;

    // 执行器总开关（meta exec.enabled，默认关）。
    bool executorEnabled() const;
    Q_INVOKABLE bool setExecutorEnabled(bool enabled);

    // 默认 agent（meta exec.defaultAgent，空表示未指定）。
    QString defaultAgent() const;
    Q_INVOKABLE bool setDefaultAgent(const QString &id);

    // ---- 角色 → agent 映射（R7-04）----
    // 角色链每步缺 agent 时按角色取默认 agent（meta exec.roleAgents，JSON 对象）。
    // 不校验角色名是否「已知」—— 角色白名单归 TaskExec，本类只当键值对存取，
    // 免得为一次校验把 TaskExec 依赖拉进 AgentProfile 的测试目标。
    QVariantMap roleAgents() const;
    // 某角色对应的 agent：映射命中且该 profile 仍存在才返回，否则回落 defaultAgent()。
    Q_INVOKABLE QString agentForRole(const QString &role) const;
    // 设某角色的 agent；agentId 为空表示清除该角色映射。未知 profile 拒绝。
    Q_INVOKABLE bool setRoleAgent(const QString &role, const QString &agentId);
    // 清空整张角色映射表。
    Q_INVOKABLE bool clearRoleAgents();

    // cwd 白名单（meta exec.trustedCwds）：首次执行前需在确认框勾选「信任本目录」。
    QStringList trustedCwds() const;
    Q_INVOKABLE bool isTrustedCwd(const QString &cwd) const;
    Q_INVOKABLE bool trustCwd(const QString &cwd);
    Q_INVOKABLE bool forgetCwd(const QString &cwd);

    static QVariantList builtinProfiles();

signals:
    void changed();

private:
    bool writeProfiles(const QVariantList &profiles);
    void bump();

    Database *m_database = nullptr;
    int m_revision = 0;
};
