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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
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
    // R16-02/03：真实 agent-bridge background.qml 走批次 —— 合并写库计数、幂等、映射清扫
    void agentBridgeBatchReconcileAndSweep();
    // R0-15：空态期 start() 的授权引导落库失败且单次闸已置位 → 就绪后重放补落库
    void grantsBootstrapReplaysAfterDeferredFinalize();
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

    // R16-05：示例 import NeonBoard（Theme 单例）—— 测试进程没有宿主编入的 QML 模块，
    // 在临时目录伪造一个最小 NeonBoard 模块（只含示例用到的颜色令牌；色值是测试夹具，
    // 不受 plugins-samples 的令牌纪律约束）。
    QTemporaryDir qmlStub;
    QVERIFY(qmlStub.isValid());
    const QString moduleDir = qmlStub.filePath(QStringLiteral("NeonBoard"));
    QVERIFY(QDir().mkpath(moduleDir));
    const auto writeStub = [&moduleDir](const QString &name, const QString &body) {
        QFile f(moduleDir + QLatin1Char('/') + name);
        QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(name));
        f.write(body.toUtf8());
    };
    writeStub(QStringLiteral("qmldir"),
              QStringLiteral("module NeonBoard\nsingleton Theme 1.0 Theme.qml\n"));
    writeStub(QStringLiteral("Theme.qml"), QStringLiteral(
        "pragma Singleton\n"
        "import QtQuick\n"
        "QtObject {\n"
        "    property color bg: \"#05060A\"\n"
        "    property color text: \"#E6F1FF\"\n"
        "    property color muted: \"#7C8BA1\"\n"
        "    property color cyan: \"#22D3EE\"\n"
        "    property color magenta: \"#F472B6\"\n"
        "    property color violet: \"#A78BFA\"\n"
        "    property color success: \"#5EEAD4\"\n"
        "    property color warning: \"#FBBF24\"\n"
        "    property color danger: \"#FB7185\"\n"
        "    property color border: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.16)\n"
        "    property color card: \"#0A1524\"\n"
        "    property color cardHover: \"#12303A\"\n"
        "    property color input: \"#12202F\"\n"
        "    property color elevated: \"#1A2433\"\n"
        "    property color panelSolid: \"#0D1420\"\n"
        "    property color accentSoft: Qt.rgba(cyan.r, cyan.g, cyan.b, 0.16)\n"
        "}\n"));

    QQmlEngine engine;
    engine.addImportPath(qmlStub.path());
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

namespace {

// R16-02/03：走真实投递链路 —— 总线 publish(agent.task) → context → 代理转发 →
// background.qml onAgentTask → applyBatch（直连信号同步完成）。
// 动作数经后台入口暴露的 lastActionCount 属性读取。
void publishBatch(PluginEventBus *bus, const QVariantList &items)
{
    bus->publish(QStringLiteral("agent.task"),
                 QVariantMap{{QStringLiteral("text"),
                     QString::fromUtf8(QJsonDocument(QJsonObject{
                         {QStringLiteral("protocol"), QStringLiteral("agent-task-event")},
                         {QStringLiteral("version"), 1},
                         {QStringLiteral("agent"), QStringLiteral("accept")},
                         {QStringLiteral("sessionId"), QStringLiteral("s1")},
                         {QStringLiteral("items"), QJsonArray::fromVariantList(items)},
                     }).toJson())}});
}

} // namespace

void TestPluginRuntime::agentBridgeBatchReconcileAndSweep()
{
    // 真实 Database + TaskModel；加载真实的 agent-bridge 后台入口（Component.onCompleted
    // 会经面板代理订阅 agent.task），批次经总线发布走完整消费链路。
    const QString path = QDir::temp().absoluteFilePath(
        QStringLiteral("nb-test-bridge-%1.db").arg(QCoreApplication::applicationPid()));
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
        QStringLiteral("com.lin.agent-bridge"), QStringLiteral("Agent 任务桥"), QStringLiteral("1.1.0"),
        {QStringLiteral("tasks.read"), QStringLiteral("tasks.write"),
         QStringLiteral("meta"), QStringLiteral("events")},
        QUrl(), false, &db, &model, nullptr, &bus);
    runtime.setActiveContext(&context);

    PluginHost host(&runtime);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("pluginHost"), &host);
    QQmlComponent component(&engine, QUrl::fromLocalFile(
        QStringLiteral(NB_TEST_AGENTBRIDGE_DIR) + QStringLiteral("/background.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> bridge(component.create());
    QVERIFY2(bridge != nullptr, qPrintable(component.errorString()));

    const QString mapKey = QStringLiteral("agentbridge.accept.s1.t2");

    // 批次①：两条待办 → 建两张卡，映射落 meta
    publishBatch(&bus, QVariantList{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("t1")},
                    {QStringLiteral("title"), QStringLiteral("批次卡一")},
                    {QStringLiteral("status"), QStringLiteral("pending")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("t2")},
                    {QStringLiteral("title"), QStringLiteral("批次卡二")},
                    {QStringLiteral("status"), QStringLiteral("pending")}}});
    QCOMPARE(bridge->property("lastActionCount").toInt(), 2);
    const QString uuid2 = host.metaValue(mapKey);
    QVERIFY(!uuid2.isEmpty());
    QCOMPARE(host.tasks().size(), 2);

    // 批次②：同一条目重投（status 改成未知值验证不猜）→ 幂等，0 动作，
    // 卡片不被未知状态打回任何列
    publishBatch(&bus, QVariantList{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("t1")},
                    {QStringLiteral("title"), QStringLiteral("批次卡一")},
                    {QStringLiteral("status"), QStringLiteral("blocked")}}});
    QCOMPARE(bridge->property("lastActionCount").toInt(), 0);

    // 批次③：title + note + tags + status 四变 → 合并写库后动作数应为 2
    //（1 次 updateTask + 1 次 moveTaskToEnd，R16-02 之前会是 4）
    publishBatch(&bus, QVariantList{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("t1")},
                    {QStringLiteral("title"), QStringLiteral("批次卡一改")},
                    {QStringLiteral("note"), QStringLiteral("验收注")},
                    {QStringLiteral("tags"), QStringLiteral("R16")},
                    {QStringLiteral("status"), QStringLiteral("in_progress")}}});
    QCOMPARE(bridge->property("lastActionCount").toInt(), 2);
    const QVariantList after = host.tasks();
    int moved = 0;
    for (const QVariant &v : after) {
        const QVariantMap t = v.toMap();
        if (t.value(QStringLiteral("title")).toString() == QStringLiteral("批次卡一改")) {
            ++moved;
            // 看板列存的是映射后的状态：in_progress → doing
            QCOMPARE(t.value(QStringLiteral("status")).toString(), QStringLiteral("doing"));
            QCOMPARE(t.value(QStringLiteral("note")).toString(), QStringLiteral("验收注"));
            // allTasks() 的 tags 键只返回首标签；tagsFull 是 JSON 数组串（normalizeTags）
            QVERIFY(t.value(QStringLiteral("tagsFull")).toString()
                        .contains(QStringLiteral("\"R16\"")));
        }
    }
    QCOMPARE(moved, 1);

    // 删掉 t2 的卡，再投一条只含 t1 的批次：清扫必须把 t2 的残留映射拿掉，
    // 状态键（agentbridge.status / .handled，点号数 1）不受误伤
    QVERIFY(model.deleteTask(uuid2));
    QVERIFY(!host.metaValue(mapKey).isEmpty());
    publishBatch(&bus, QVariantList{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("t1")},
                    {QStringLiteral("title"), QStringLiteral("批次卡一改")},
                    {QStringLiteral("status"), QStringLiteral("in_progress")}}});
    QCOMPARE(bridge->property("lastActionCount").toInt(), 0);
    QVERIFY(host.metaValue(mapKey).isEmpty());
    QVERIFY(!host.metaValue(QStringLiteral("agentbridge.status")).isEmpty());
    QVERIFY(!host.metaValue(QStringLiteral("agentbridge.handled")).isEmpty());

    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
}

// R0-15 核心用例：空态跳过 → 就绪后重放成功（授权侧）。
// 两阶段建表下 start() 的引导被 schemaReady 守卫跳过（否则 meta 落库失败、授权只在
// 内存里等重启丢掉）；finalizeSchema 后 replayGrantsBootstrap 完成首次引导并落库，
// 已引导时重放为空操作。
void TestPluginRuntime::grantsBootstrapReplaysAfterDeferredFinalize()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    Database db;
    QString error;
    QVERIFY2(db.openDeferred(&error, tmp.filePath(QStringLiteral("board.db"))),
             qPrintable(error));

    TaskModel model(&db);
    PluginEventBus bus;
    PluginRuntime runtime(nullptr, &bus, &db, &model, nullptr);

    // 空态期 start()：引导被守卫跳过，meta 里没有 grants 键
    runtime.start();
    QVERIFY(db.metaValue(QStringLiteral("plugins.grants")).isEmpty());

    QVERIFY2(db.finalizeSchema(&error), qPrintable(error));
    runtime.replayGrantsBootstrap();
    // 补落库：loader 为 nullptr 时引导结果是空授予集，但 grants 键必须真实存在
    QVERIFY(!db.metaValue(QStringLiteral("plugins.grants")).isEmpty());

    // 幂等：已落库后重放为空操作
    const QString first = db.metaValue(QStringLiteral("plugins.grants"));
    runtime.replayGrantsBootstrap();
    QCOMPARE(db.metaValue(QStringLiteral("plugins.grants")), first);
}

QTEST_MAIN(TestPluginRuntime)

#include "tst_pluginruntime.moc"
