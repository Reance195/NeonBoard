#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Database;

// Agent profile：把不同 CLI 的差异收敛成一条配置（R6-03 / R9-01）。
//
// 设计要点：
//   1. 内置 opencode / claude / cursor-agent 三条（docs/agent-profile.md 实测过）；
//      codex 未实测，不内置，避免把「想当然」的参数带进执行路径。
//      其余 agent 由插件声明（contributes.agentProfiles），不再需要改 C++。
//   2. 用户增删改存 meta 表 exec.agentProfileOverrides（id → profile，null 为墓碑），
//      **只存与下层的差异**；删掉该键即恢复 L0 ∪ L1，不升 user_version、不动 task 表。
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

    // 当前生效的 profile 列表。
    //
    // 三层合并（R9-01），同 id 后者覆盖前者：
    //   L0 内置 builtinProfiles() < L1 插件声明 setDeclaredProfiles() < L2 用户覆盖
    //      （meta exec.agentProfileOverrides，增量覆盖，**不是**整份快照）
    //
    // 为什么用户层不能用现存的 exec.agentProfiles 整份快照：那份一写就把插件后来声明的
    // agent 整个淹没 —— 插件装了却在列表里看不见。所以新写入一律走 overrides，
    // 老快照只在 migrateLegacyIfNeeded() 里被一次性翻译成 overrides 后删除。
    QVariantList profiles() const;
    // 未知 id 返回空 map。
    Q_INVOKABLE QVariantMap profileOf(const QString &id) const;

    // ---- L1 插件声明层（R9-01）----
    // 宿主扫完插件后注入（PluginRuntime::declaredAgentProfiles），本类不依赖 PluginLoader：
    // tst_agentprofile 只链 Database / AppPaths / AgentProfile，把插件栈拖进来会连带 QQml，
    // 那条测试线就不再是无头可跑的了。
    void setDeclaredProfiles(const QVariantList &profiles);
    QVariantList declaredProfiles() const { return m_declared; }

    // 按 id 覆盖或追加；id / exe / promptMode / output / 超时非法返回 false。
    // 只写"与下层的差异"，不再存整份快照。
    Q_INVOKABLE bool saveProfile(const QVariantMap &profile);
    Q_INVOKABLE bool removeProfile(const QString &id);
    // 清掉用户改动，回落 L0 ∪ L1。
    Q_INVOKABLE bool resetProfiles();

    // exe 是否真能拉起：绝对路径查文件，否则查 PATH。
    // cursor-agent「装了却不在 PATH」这种本机事实必须能在界面上看出来，
    // 而不是等执行失败才暴露。
    Q_INVOKABLE bool isAvailable(const QString &id) const;

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
    QVariantList baseProfiles() const;              // L0 内置 ∪ L1 插件
    QVariantMap baseProfile(const QString &id) const;
    QJsonObject readOverrides() const;
    bool writeOverride(const QString &id, const QJsonValue &value);
    bool clearOverride(const QString &id);
    void migrateLegacyProfiles() const;             // 整份快照 → 增量覆盖，一次性
    void bump();

    Database *m_database = nullptr;
    int m_revision = 0;
    QVariantList m_declared;
    mutable bool m_legacyChecked = false;
};
