// R14-01：官方来源插件的授权补齐策略。
//
// 背景：`ensureGrantsBootstrapped` 只在 meta 里还没有 `plugins.grants` 时做一次性
// 全授予（祖父条款）。既有库早就 bootstrap 过，之后新增的官方插件一律未授权；
// 而 `declaredAgentProfiles()` 对未授权声明是静默丢弃 —— 表现为「插件列表里看得见、
// 授权弹窗也没弹、声明却根本没进合并」。
//
// 本测试只覆盖策略函数 `PluginLoader::officialAutoGrants` 与来源判定，
// 不构造 PluginRuntime（它依赖 Database / QQmlEngine / TrayController，
// 拉进来会把一个纯逻辑用例变成集成测试）。
//
// 关键：验收场景必须是「grants 已存在」，而不是全新库。
// R9 那四张卡全绿却漏掉了这个缺陷，就是因为沙箱选的是全新库 ——
// 首次 bootstrap 会把当时发现的插件全授予，绿灯掩盖了真实库不生效。

#include "PluginLoader.h"

#include <QHash>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

namespace {

QVariantMap makePlugin(const QString &id, const QString &source, const QStringList &permissions,
                       const QString &status = QStringLiteral("ok"))
{
    QVariantMap plugin;
    plugin.insert(QStringLiteral("id"), id);
    plugin.insert(QStringLiteral("source"), source);
    plugin.insert(QStringLiteral("status"), status);
    QVariantList list;
    for (const QString &permission : permissions)
        list.append(permission);
    plugin.insert(QStringLiteral("permissions"), list);
    return plugin;
}

} // namespace

class TestPluginRuntime : public QObject
{
    Q_OBJECT

private slots:
    // 来源判定：只有随分发走的两类算官方
    void officialSourceClassification();
    // 核心用例①：既有 grants 存在时，新装的官方插件自动授权
    void autoGrantsNewOfficialPluginOnExistingGrants();
    // 核心用例②：第三方（appdata）不被顺带放行
    void autoGrantsSkipsThirdParty();
    // 示例插件也不放行：它是给人看的模板，不该自动拿到 exec 权限
    void autoGrantsSkipsSample();
    // 用户显式撤销过的权限不补回，否则撤销在下次启动就失效
    void revokedPermissionIsNotAutoRestored();
    // 幂等：补齐结果作为新 grants 再算一次，应当无变化（调用方据此跳过写库）
    void autoGrantsIsIdempotent();
    // 状态异常的插件不参与（读不到 manifest 时不该拿到权限）
    void autoGrantsSkipsBrokenPlugin();
    // 已授予的权限不重复追加
    void autoGrantsKeepsExistingGrants();
};

void TestPluginRuntime::officialSourceClassification()
{
    QVERIFY(PluginLoader::isOfficialPluginSource(QStringLiteral("appdir")));
    QVERIFY(PluginLoader::isOfficialPluginSource(QStringLiteral("official")));
    QVERIFY(!PluginLoader::isOfficialPluginSource(QStringLiteral("appdata")));
    QVERIFY(!PluginLoader::isOfficialPluginSource(QStringLiteral("sample")));
    QVERIFY(!PluginLoader::isOfficialPluginSource(QString()));
    QVERIFY(!PluginLoader::isOfficialPluginSource(QStringLiteral("OFFICIAL")));
}

void TestPluginRuntime::autoGrantsNewOfficialPluginOnExistingGrants()
{
    // 既有库：只有老插件在 grants 里（与真实库一致：agent-bridge / plan-hook / today）
    QHash<QString, QStringList> grants;
    grants.insert(QStringLiteral("com.lin.agent-bridge"),
                  {QStringLiteral("tasks.read"), QStringLiteral("tasks.write")});
    grants.insert(QStringLiteral("com.lin.plan-hook"),
                  {QStringLiteral("tasks.read"), QStringLiteral("tasks.write")});

    const QVariantList plugins{
        makePlugin(QStringLiteral("com.lin.agent-bridge"), QStringLiteral("official"),
                   {QStringLiteral("tasks.read"), QStringLiteral("tasks.write")}),
        makePlugin(QStringLiteral("com.lin.agent-claude"), QStringLiteral("official"),
                   {QStringLiteral("exec.agentProfiles")}),
        makePlugin(QStringLiteral("com.lin.agent-codex"), QStringLiteral("official"),
                   {QStringLiteral("exec.agentProfiles")}),
    };

    const QHash<QString, QStringList> patched =
        PluginLoader::officialAutoGrants(plugins, grants, {});

    QCOMPARE(patched.size(), 2);
    QVERIFY(!patched.contains(QStringLiteral("com.lin.agent-bridge"))); // 无变化不返回
    QCOMPARE(patched.value(QStringLiteral("com.lin.agent-claude")),
             QStringList{QStringLiteral("exec.agentProfiles")});
    QCOMPARE(patched.value(QStringLiteral("com.lin.agent-codex")),
             QStringList{QStringLiteral("exec.agentProfiles")});
}

void TestPluginRuntime::autoGrantsSkipsThirdParty()
{
    const QVariantList plugins{
        makePlugin(QStringLiteral("com.lin.agent-claude"), QStringLiteral("official"),
                   {QStringLiteral("exec.agentProfiles")}),
        // 用户从市场装的第三方插件：哪怕声明了同样的权限，也必须等用户点授权
        makePlugin(QStringLiteral("com.thirdparty.evil"), QStringLiteral("appdata"),
                   {QStringLiteral("exec.agentProfiles")}),
    };

    const QHash<QString, QStringList> patched =
        PluginLoader::officialAutoGrants(plugins, {}, {});

    QCOMPARE(patched.size(), 1);
    QVERIFY(patched.contains(QStringLiteral("com.lin.agent-claude")));
    QVERIFY(!patched.contains(QStringLiteral("com.thirdparty.evil")));
}

void TestPluginRuntime::autoGrantsSkipsSample()
{
    const QVariantList plugins{
        makePlugin(QStringLiteral("com.lin.today"), QStringLiteral("sample"),
                   {QStringLiteral("tasks.read")}),
    };

    const QHash<QString, QStringList> patched =
        PluginLoader::officialAutoGrants(plugins, {}, {});

    QVERIFY(patched.isEmpty());
}

void TestPluginRuntime::revokedPermissionIsNotAutoRestored()
{
    QHash<QString, QStringList> revoked;
    revoked.insert(QStringLiteral("com.lin.agent-claude"),
                   {QStringLiteral("exec.agentProfiles")});

    const QVariantList plugins{
        makePlugin(QStringLiteral("com.lin.agent-claude"), QStringLiteral("official"),
                   {QStringLiteral("exec.agentProfiles")}),
    };

    const QHash<QString, QStringList> patched =
        PluginLoader::officialAutoGrants(plugins, {}, revoked);

    QVERIFY2(patched.isEmpty(), "用户显式撤销的权限不能被自动补齐，否则撤销无效");
}

void TestPluginRuntime::autoGrantsIsIdempotent()
{
    const QVariantList plugins{
        makePlugin(QStringLiteral("com.lin.agent-claude"), QStringLiteral("official"),
                   {QStringLiteral("exec.agentProfiles"), QStringLiteral("tasks.read")}),
    };

    QHash<QString, QStringList> grants;
    const QHash<QString, QStringList> first =
        PluginLoader::officialAutoGrants(plugins, grants, {});
    QCOMPARE(first.size(), 1);

    grants.insert(QStringLiteral("com.lin.agent-claude"),
                  first.value(QStringLiteral("com.lin.agent-claude")));
    const QHash<QString, QStringList> second =
        PluginLoader::officialAutoGrants(plugins, grants, {});
    QVERIFY2(second.isEmpty(), "补齐结果作为新 grants 再算一次应当无变化");
}

void TestPluginRuntime::autoGrantsSkipsBrokenPlugin()
{
    const QVariantList plugins{
        makePlugin(QStringLiteral("com.lin.agent-claude"), QStringLiteral("official"),
                   {QStringLiteral("exec.agentProfiles")}, QStringLiteral("error")),
    };

    const QHash<QString, QStringList> patched =
        PluginLoader::officialAutoGrants(plugins, {}, {});

    QVERIFY(patched.isEmpty());
}

void TestPluginRuntime::autoGrantsKeepsExistingGrants()
{
    QHash<QString, QStringList> grants;
    grants.insert(QStringLiteral("com.lin.agent-claude"), {QStringLiteral("tasks.read")});

    const QVariantList plugins{
        makePlugin(QStringLiteral("com.lin.agent-claude"), QStringLiteral("official"),
                   {QStringLiteral("tasks.read"), QStringLiteral("exec.agentProfiles")}),
    };

    const QHash<QString, QStringList> patched =
        PluginLoader::officialAutoGrants(plugins, grants, {});

    // 返回值是完整替换值：既有权限要保留，只追加缺的那条
    QCOMPARE(patched.value(QStringLiteral("com.lin.agent-claude")),
             (QStringList{QStringLiteral("tasks.read"), QStringLiteral("exec.agentProfiles")}));
}

QTEST_GUILESS_MAIN(TestPluginRuntime)

#include "tst_pluginruntime.moc"
