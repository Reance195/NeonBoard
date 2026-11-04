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
//
// R16-01 追加：面板代理的事件转发用例。这里允许构造**最小** PluginRuntime
// （loader 传 nullptr：不 attach QML 引擎、不扫插件目录、不调 start/reload）——
// 被测对象是「活跃 context 管线 + 代理换线」，不是插件加载本身；
// Database / TaskModel / TrayController 只为满足链接符号，不实例化。
// 另有 QML 用例加载真实的 today 示例 main.qml（QtQuick 类型系统需要 QApplication，
// 故 main 宏用 QTEST_MAIN，全程不显示窗口）。

#include "Database.h"
#include "PluginContext.h"
#include "PluginEventBus.h"
#include "PluginHost.h"
#include "PluginLoader.h"
#include "PluginRuntime.h"
#include "TaskModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

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
    // R16-01 核心用例①：面板代理转发活跃 context 的已订阅事件，未订阅域不投递
    void panelProxyForwardsActiveContextEvents();
    // R16-01 核心用例②：活跃 context 切换时代理换线，旧 context 的事件不再穿透
    void panelProxyRewiresWhenActiveContextChanges();
    // R16-01 验收项②（无头替身）：加载真实 today 示例，加任务 → 事件 → 统计自动重算
    void samplePanelAutoRefreshesOnTaskEvents();
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

namespace {

// R16-01：搭一个只有事件域权限的最小 context（不落库、不弹授权，grantQuery 为空即放行）
PluginContext makeEventContext(const QString &id, PluginEventBus *bus)
{
    return PluginContext(id, QStringLiteral("测试插件"), QStringLiteral("1.0"),
                         {QStringLiteral("events")}, QUrl(), false,
                         nullptr, nullptr, nullptr, bus);
}

} // namespace

void TestPluginRuntime::panelProxyForwardsActiveContextEvents()
{
    PluginEventBus bus;
    PluginRuntime runtime(nullptr, &bus, nullptr, nullptr, nullptr);

    PluginContext contextA = makeEventContext(QStringLiteral("com.test.a"), &bus);
    runtime.setActiveContext(&contextA);

    PluginHost host(&runtime);
    QSignalSpy spy(&host, &PluginHost::eventReceived);

    QVERIFY(host.subscribe({QStringLiteral("task.updated")}));
    QCOMPARE(host.subscriptions(), QStringList{QStringLiteral("task.updated")});

    // 订阅域：总线发布 → context 投递 → 代理转发，事件名与载荷原样
    const QVariantMap payload{{QStringLiteral("id"), QStringLiteral("t1")}};
    bus.publish(QStringLiteral("task.updated"), payload);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("task.updated"));
    QCOMPARE(spy.at(0).at(1).toMap(), payload);

    // 未订阅域：publish 连 context 级投递都不发生，代理自然收不到
    bus.publish(QStringLiteral("hook.commit"), {});
    QCOMPARE(spy.count(), 1);
}

void TestPluginRuntime::panelProxyRewiresWhenActiveContextChanges()
{
    PluginEventBus bus;
    PluginRuntime runtime(nullptr, &bus, nullptr, nullptr, nullptr);

    PluginContext contextA = makeEventContext(QStringLiteral("com.test.a"), &bus);
    PluginContext contextB = makeEventContext(QStringLiteral("com.test.b"), &bus);
    runtime.setActiveContext(&contextA);

    PluginHost host(&runtime);
    QSignalSpy spy(&host, &PluginHost::eventReceived);
    QVERIFY(host.subscribe({QStringLiteral("task.updated")}));

    bus.publish(QStringLiteral("task.updated"), {});
    QCOMPARE(spy.count(), 1);

    // 切换活跃 context：代理换线。A 的订阅还挂在总线上（publish 仍向 A 投递），
    // 但代理只听当前活跃的 B —— A 的事件不许再穿透。
    runtime.setActiveContext(&contextB);
    bus.publish(QStringLiteral("task.updated"), {});
    QCOMPARE(spy.count(), 1);

    // B 订阅同一域后，这次发布 A、B 都收到 context 级投递，只有 B 穿透代理
    QVERIFY(host.subscribe({QStringLiteral("task.updated")}));
    bus.publish(QStringLiteral("task.updated"), {});
    QCOMPARE(spy.count(), 2);
}

void TestPluginRuntime::samplePanelAutoRefreshesOnTaskEvents()
{
    // 真实 Database + TaskModel（临时库）；task.created 的发布映射与
    // PluginRuntime::connectTaskEvents 同款，手动接到总线上。
    // 被测链路：真实 today 示例 main.qml → 面板代理订阅 → 门面 addTask →
    // 模型信号 → 总线 → 代理转发 → 示例自动重算（不点刷新按钮）。
    const QString path = QDir::temp().absoluteFilePath(
        QStringLiteral("nb-test-today-%1.db").arg(QCoreApplication::applicationPid()));
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));

    Database db;
    QString error;
    QVERIFY2(db.open(&error, path), qPrintable(error));

    TaskModel model(&db);
    model.load();

    PluginEventBus bus;
    PluginRuntime runtime(nullptr, &bus, &db, &model, nullptr);
    PluginContext context(
        QStringLiteral("com.lin.today"), QStringLiteral("今日概览"), QStringLiteral("1.1.0"),
        {QStringLiteral("tasks.read"), QStringLiteral("tasks.write"), QStringLiteral("events")},
        QUrl(), false, &db, &model, nullptr, &bus);
    runtime.setActiveContext(&context);

    QObject::connect(&model, &TaskModel::taskCreated, &bus,
                     [&bus](const QString &id, const QString &title) {
        bus.publish(QStringLiteral("task.created"),
                    QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("title"), title}});
    });

    PluginHost host(&runtime);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("pluginHost"), &host);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QStringLiteral(NB_TEST_SAMPLE_DIR) + QStringLiteral("/main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> panel(component.create());
    QVERIFY2(panel != nullptr, qPrintable(component.errorString()));

    // Component.onCompleted 已通过面板代理订阅任务域
    QVERIFY(host.subscriptions().contains(QStringLiteral("task.created")));

    QCOMPARE(panel->property("todoCount").toInt(), 0);

    // 走真实门面加任务，统计必须自己动 —— 这就是「无需手动刷新」
    QVERIFY(!host.addTask(QStringLiteral("R16-01 验收卡"), QStringLiteral("todo")).isEmpty());
    QCOMPARE(panel->property("todoCount").toInt(), 1);

    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
}

QTEST_MAIN(TestPluginRuntime)

#include "tst_pluginruntime.moc"
