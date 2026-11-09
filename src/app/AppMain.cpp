#include "AppMain.h"
#include "AppObjects.h"
#include "AppWiring.h"
#include "CliHandlers.h"

#include "AppContext.h"
#include "CliCommand.h"
#include "SelfTestRunner.h"
#include "AppPaths.h"
#include "AppActions.h"
#include "AgentProfile.h"
#include "Database.h"
#include "HookEventQueue.h"
#include "Hotkey.h"
#include "Metrics.h"
#include "PluginEventBus.h"
#include "PluginHost.h"
#include "PluginLibrary.h"
#include "PluginLoader.h"
#include "PluginMarket.h"
#include "PluginPackage.h"
#include "PluginRuntime.h"
#include "PreviewHub.h"
#include "BoardSync.h"
#include "RoadmapSync.h"
#include "ProjectMap.h"
#include "ProjectStore.h"
#include "DataPort.h"
#include "SingleInstance.h"
#include "SplashWindow.h"
#include "WindowsIntegration.h"
#include "BoardColumns.h"
#include "DragFeel.h"
#include "KeyMove.h"
#include "TaskFilterModel.h"
#include "TaskModel.h"
#include "TaskTree.h"
#include "TaskStats.h"
#include "TaskAudit.h"
#include "TaskExtra.h"
#include "ThemeMarket.h"
#include "ThemePack.h"
#include "TaskExec.h"
#include "TaskRunner.h"
#include "TaskRunStore.h"
#include "Scheduler.h"
#include "WorkflowRunner.h"
#include "WorkflowPresets.h"
#include "AcceptanceRunner.h"
#include "ScopeMatcher.h"
#include "TriageRunner.h"
#include "LedgerImport.h"
#include "TaskUndo.h"
#include "CardDetail.h"
#include "ReminderScheduler.h"
#include "TrayController.h"
#include "UiSettings.h"
#include "UpdateManager.h"
#include "WindowController.h"

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <memory>
#include <mutex>
#include "StartupDefer.h"

static void neonboardLogHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    // 闪屏的渲染线程也会打日志（DX-39 起），而 QFile 不是线程安全的：
    // 两个线程同时写同一个文件会撕行，甚至踩坏 QTextStream 的内部缓冲。
    // 所以这里上把锁 —— 锁的范围只包住「写一行」，不影响任何业务路径。
    static std::mutex logMutex;
    static QFile file;
    static bool opened = false;
    std::lock_guard<std::mutex> lock(logMutex);
    if (!opened) {
        opened = true;
        file.setFileName(AppPaths::logFilePath());
        file.open(QIODevice::Append | QIODevice::Text);
    }
    if (file.isOpen()) {
        QTextStream stream(&file);
        stream << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz "))
               << message << '\n';
        stream.flush();
    }
}

// 命令行结果直接写 stdout：exe 是 GUI 子系统，qInfo 只进日志文件，
// 脚本要读结果必须走标准输出

int neonboardMain(int argc, char *argv[])
{
    QElapsedTimer boot;
    boot.start();
    Metrics::startStartupTimer();

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    if (!qEnvironmentVariableIsSet("QSG_RHI_BACKEND"))
        qputenv("QSG_RHI_BACKEND", "d3d11");

    QApplication app(argc, argv);
    QGuiApplication::setQuitOnLastWindowClosed(false);

    QCoreApplication::setOrganizationName(QStringLiteral("com.lin"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("com.lin.neonboard"));
    QCoreApplication::setApplicationName(QStringLiteral("NeonBoard"));
    // 版本号来自 CMakeLists 的 project(VERSION)，不要在源码里写死：
    // 更新检查用的就是这里的 applicationVersion，写死即等于「永远已是最新」
    QCoreApplication::setApplicationVersion(QStringLiteral(NEONBOARD_VERSION_STRING));
    // 任务栏分组与通知归属：必须早于任何窗口创建（R4-02）
    WindowsIntegration::applyAppUserModelId();
    qInstallMessageHandler(neonboardLogHandler);

    QSettings::setDefaultFormat(QSettings::IniFormat);
    // 沙箱隔离：界面设置（主题/画质/动效）也随数据目录走，多 agent 互不干扰
    if (AppPaths::isolated())
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, AppPaths::dataDir());
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/neonboard.svg")));

    qInfo() << "BOOT app" << boot.elapsed();
    qInfo().noquote() << "AppPaths: data" << AppPaths::dataDir()
                      << "isolated" << AppPaths::isolated()
                      << "instance" << AppPaths::singleInstanceKey();

    const QStringList arguments = QCoreApplication::arguments();
    const bool smokeMode = arguments.contains(QStringLiteral("--smoke"));
    // 工具类命令集中成表（CliCommands.cpp），utilityMode 由表反推，
    // 不再在这里逐条手写 || —— 新增命令不会顶破 main.cpp 的行数棘轮。
    const bool utilityMode = isUtilityMode(arguments);
    // 冷启动请求的动作（面板开关）。二次启动走同一条分发路径，见下方 dispatch：
    // 跳转列表/托盘/命令行只是「请求已在运行的实例做某事」的不同入口，
    // 各写一套必然分叉（典型症状：托盘能开新建面板、跳转列表点了只亮窗口）。
    const QString startupAction = AppActions::fromArguments(arguments);
    const bool silentStart = arguments.contains(QStringLiteral("--autostart"));

    std::unique_ptr<SingleInstance> singleInstance;
    if (!utilityMode) {
        // 二次启动要把「用户想干什么」原样转给主实例：双击/跳转列表带具体动作；
        // 开机自启再起一份则发 noop —— 主实例已在运行，不该莫名弹窗。
        singleInstance = std::make_unique<SingleInstance>(
            AppPaths::singleInstanceKey(),
            silentStart ? QString::fromLatin1(AppActions::kNoop) : startupAction);
        if (!singleInstance->isPrimary())
            return 0;
    } else if (smokeMode) {
        qInfo() << "BOOT smoke bypass single instance";
    }

    const bool hosted = arguments.contains(QStringLiteral("--hosted"));
    std::unique_ptr<SplashWindow> splash;
    // Splash 的「呼吸」停留是纯等待：五处 pump 加起来 330 ms，全部叠加在用户等的那一刻。
    // 热启动时这 330 ms 是节奏感（进度条一格格亮起来），冷启动时就是白等 ——
    // 慢机器上用户已经盯着同一个画面两秒，再按剧本多停 330 ms 只会更像卡住。
    // 折中：给整段停留一个总预算，谁先花完谁先走完。动画仍然逐段推进，
    // 只是不再对每一段都保证最低停留时长。
    // 预算可用 NEONBOARD_SPLASH_BUDGET 覆盖（毫秒；0 = 完全不停留）。
    // 留这个口子是为了让「停留到底吃掉多少启动时间」可以被 A/B 量出来，
    // 而不是只能靠读代码估算 —— 与 NEONBOARD_NO_BG 同一套诊断约定。
    bool splashBudgetSet = false;
    const int splashBudgetEnv = qEnvironmentVariableIntValue("NEONBOARD_SPLASH_BUDGET",
                                                             &splashBudgetSet);
    int splashPumpBudgetMs = splashBudgetSet ? qMax(0, splashBudgetEnv) : 160;
    const auto splashPump = [&splash, &splashPumpBudgetMs](int holdMs) {
        if (!splash)
            return;
        // 预算用完之后仍然要 pump(0)：它至少跑一轮 processEvents + repaint，
        // 进度条才会真的画到屏幕上。跳过它的话画面会停在 36% 不动，
        // 看起来像卡死 —— 那比多等 330 ms 还糟。
        const int spend = qMin(holdMs, splashPumpBudgetMs);
        splashPumpBudgetMs -= spend;
        splash->pump(spend);
    };

    if (!utilityMode && !smokeMode && !silentStart) {
        splash = std::make_unique<SplashWindow>();
        splash->setStage(0.06, QStringLiteral("BOOTING NEON RUNTIME"));
        splash->show();
        splashPump(70);
    }

    nbPrewarmShader();

    const auto splashStage = [&splash, &splashPump](qreal progress, const QString &label, int holdMs) {
        if (!splash)
            return;
        splash->setStage(progress, label);
        splashPump(holdMs);
    };

    QFont uiFont(QStringLiteral("Microsoft YaHei UI"));
    uiFont.setPointSize(9);
    QApplication::setFont(uiFont);
    splashStage(0.18, QStringLiteral("LOADING TYPEFACES"), 40);

    Database database;
    QString databaseError;
    const bool deferredSchema = !utilityMode;
    if (!(deferredSchema ? database.openDeferred(&databaseError) : database.open(&databaseError))) {
        if (splash)
            splash->close();
        QMessageBox::critical(nullptr,
                              QStringLiteral("NeonBoard"),
                              QStringLiteral("数据库打开失败：\n%1").arg(databaseError));
        return 1;
    }
    qInfo().noquote() << "Database:" << database.path();
    if (database.recoveredFromCorruption() && !utilityMode) {
        QMessageBox::warning(nullptr,
                             QStringLiteral("NeonBoard"),
                             QStringLiteral("看板数据库文件损坏，已重建空库。\n"
                                            "原文件已备份到：\n%1\n\n原因：%2")
                                 .arg(database.recoveryBackup(), database.recoveryReason()));
    }
    qInfo() << "BOOT db" << boot.elapsed();
    splashStage(0.36, QStringLiteral("OPENING LOCAL STORE"), 70);

    TaskModel taskStore(&database);
    if (!deferredSchema) taskStore.load();

    // 核心对象构造单元（MOD-8）：次序见 AppObjects.h；taskStore 与条件 load 留原位（load 须早于 audit 挂接）。
    AppObjectsEarly early{database, taskStore};
    TaskUndo &taskUndo = early.taskUndo;
    TaskTree &taskTree = early.taskTree;
    TaskStats &taskStats = early.taskStats;
    TaskExtra &taskExtra = early.taskExtra;
    CardDetail &cardDetail = early.cardDetail;
    TaskAudit &taskAudit = early.taskAudit;
    TaskExec &taskExec = early.taskExec;
    AgentProfile &agentProfile = early.agentProfile;
    TaskRunStore &taskRuns = early.taskRuns;
    TaskRunner &taskRunner = early.taskRunner;
    Scheduler &scheduler = early.scheduler;
    AcceptanceRunner &acceptanceRunner = early.acceptanceRunner;
    ScopeMatcher &scopeMatcher = early.scopeMatcher;
    WorkflowPresets &workflowPresets = early.workflowPresets;
    WorkflowRunner &workflowRunner = early.workflowRunner;
    TriageRunner &triageRunner = early.triageRunner;
    LedgerImport &ledgerImport = early.ledgerImport;
    RoadmapSync &roadmap = early.roadmap;
    BoardSync &boardSync = early.boardSync;
    ProjectMap &projectMap = early.projectMap;
    DataPort &dataPort = early.dataPort;

    if (auto rc = runDataMode(dataPort, arguments))
        return *rc;

    if (auto rc = runSyncMode(roadmap, arguments))
        return *rc;

    // 看板列由数据库驱动（R4-03）：列清单 + 每列一个过滤模型，
    // 界面按这张表 Repeater 出列，不再写死三列。
    BoardColumns boardColumns(&database, &taskStore);
    // 拖拽落点几何（R8-05）：纯函数门面，无状态。
    // 必须声明在 QQmlApplicationEngine 之前 —— 反过来（挨着 setContextProperty 声明）
    // 会在退出时先于引擎析构，QML 侧那条 Timer 绑定重新求值就拿到 null，
    // 日志里冒出一行 "Cannot call method ... of null"。注册位置和寿命位置是两件事。
    DragFeel dragFeel;
    // 键盘跨列位移（R8-06）：只算目标列与落位，写库仍走 taskUndo。
    // 与 DragFeel 同理，声明在引擎之前 —— 否则退出时 QML 绑定重算会拿到 null。
    KeyMove keyMove;
    boardColumns.setTaskTree(&taskTree);
    // 项目维度（R10-01 数据层 / R10-02 视图）：门面在列容器之前建，范围过滤才有东西可问。
    // 只读侧表，不碰 task 表 —— 切范围是视图状态，不写库。
    ProjectStore projectStore(&database);
    boardColumns.setProjectStore(&projectStore);
    roadmap.setProjectStore(&projectStore);
    boardColumns.reload();
    // 列是库里读出来的：启动时把实际列数与列 id 打进日志，
    // 「界面上一列都没有」这类问题（列表异常 / 迁移漏了）能直接从日志定位。
    QStringList boardColumnIds;
    for (int i = 0; i < boardColumns.count(); ++i) {
        boardColumnIds.append(boardColumns.index(i, 0)
                                  .data(BoardColumns::IdRole).toString());
    }
    qInfo().noquote() << "BOARD columns" << boardColumnIds.size()
                      << boardColumnIds.join(QLatin1Char(','));

    // 钩子事件投递：--emit-hook <text> / --emit-hook-file <path>
    // 只把提交信息写进事件队列，不在这里执行规则 —— 规则由主进程里的插件执行。
    // 走 utilityMode：应用正在运行时也能投递。
    if (auto rc = runEmitHook(arguments))
        return *rc;

    // Windows 原生集成（R4-02）：.neon 文件关联的注册 / 注销 / 状态查询。
    // 只写 HKCU，不需要管理员；注册后双击插件包即走 --install-plugin-file。
    if (auto rc = runFileAssoc(arguments))
        return *rc;

    // 本地 .neon 插件包安装：--install-plugin-file <path>
    // 供脚本化安装与自检使用；包格式见 docs/plugin-api.md
    if (auto rc = runInstallPluginFile(arguments))
        return *rc;

    if (arguments.contains(QStringLiteral("--selftest"))) {
        return runSelfTest(SelfTestDeps{
            &database, &taskStore, &taskUndo, &taskTree, &taskExtra,
            &taskExec, &taskRuns, &workflowPresets, &triageRunner,
            &roadmap, &boardColumns});
    }

    Metrics metrics;
    BackgroundWiring bgw{roadmap, taskStore, boardSync, dataPort, database,
                         metrics, boot, app, deferredSchema};
    AppObjectsUi ui{hosted};  // 界面外壳构造单元（MOD-8），声明见 AppObjects.h
    UiSettings &uiSettings = ui.uiSettings;
    ThemePack &themePacks = ui.themePacks;
    ThemeMarket &themeMarket = ui.themeMarket;
    WindowController &windowController = ui.windowController;
    Hotkey &hotkey = ui.hotkey;
    TrayController &tray = ui.tray;
    // 预览出图（验收用）：托盘菜单离屏抓成 PNG，不弹到屏幕上（逻辑在 TrayController 里）。
    if (const int menuPreviewCode = tray.handleMenuPreview(arguments); menuPreviewCode >= 0)
        return menuPreviewCode;
    // 插件与更新构造单元（MOD-8）：原穿插的 scanAsync / 启动重放 / declaredAgents
    // 刷新 / 实例标签 / 版本日志集中到构造之后——被调对象构造期间不读这些结果。
    AppObjectsPlugins plugins{database, taskStore, taskExtra, tray};
    ReminderScheduler &reminderScheduler = plugins.reminderScheduler;
    PluginLoader &pluginLoader = plugins.pluginLoader;
    PluginEventBus &pluginEventBus = plugins.pluginEventBus;
    PluginRuntime &pluginRuntime = plugins.pluginRuntime;
    PluginHost &pluginHost = plugins.pluginHost;
    PluginMarket &pluginMarket = plugins.pluginMarket;
    PluginLibrary &pluginLibrary = plugins.pluginLibrary;
    PreviewHub &previewHub = plugins.previewHub;
    UpdateManager &updateManager = plugins.updateManager;
    // 插件宣告接线（MOD-9）：scanAsync / 启动重放 / declaredAgents 刷新 / 实例标签 / 版本日志。
    wirePluginAnnouncements(database, deferredSchema, taskTree, taskRuns,
                            agentProfile, tray, previewHub, pluginLoader,
                            pluginRuntime, updateManager, app);

    // --plugin-report：输出插件平台表面清单（插件 / 命令 / 槽位 / 设置页 / 授权缺口），
    // 走 stdout JSON，方便脚本与无头环境验收。
    if (auto rc = runPluginReport(pluginLoader, pluginRuntime, agentProfile, arguments))
        return *rc;

    // 钩子事件队列：与看板库物理隔离，主进程轮询消费后转成插件事件
    // 晚期命令集处理单元（MOD-10）：HookEventQueue 作成员随结构体活到 main 退出
    // （GUI 段 hookQueue.start(1000) 仍用）；open 失败日志与 setEventBus 随构造。
    LateCliRunner lateCli{pluginMarket, pluginLibrary, previewHub, updateManager,
                          taskRuns, ledgerImport, pluginEventBus};

    // 插件市场命令行：--market-source <spec>... 设源（可重复、可逗号分隔）；
    // --market-sources 列出当前源；--market-refresh 拉索引；
    // --market-install <id> 装插件；--install-plugin-url <url> 直链安装。
    // 都走同步接口，方便脚本与无头环境验证。
    if (auto rc = lateCli.runMarketCli(arguments))
        return *rc;

    // 预览中心命令行：--preview-list 列工作区；--preview-build <id> 构建；
    // --preview-start <id> 启动预览实例；--preview-stop <id> 停止实例。
    // 命令行进程跑完即退，不该把自己登记成“运行中的实例”。
    previewHub.setSelfRegisterEnabled(false);
    if (auto rc = lateCli.runPreviewAndUpdateCli(arguments))
        return *rc;

    QQmlApplicationEngine engine;
    pluginRuntime.attachEngine(&engine);
    applyContextProperties(engine.rootContext(), {
        {"metrics", &metrics},
        {"windowController", &windowController},
        {"hotkey", &hotkey},
        {"tray", &tray},
        {"pluginLoader", &pluginLoader},
        {"pluginHost", &pluginHost},
        {"pluginRuntime", &pluginRuntime},
        {"pluginMarket", &pluginMarket},
        {"pluginLibrary", &pluginLibrary},
        {"database", &database},
        {"taskStore", &taskStore},
        {"taskUndo", &taskUndo},
        {"taskTree", &taskTree},
        {"taskStats", &taskStats},
        {"taskExtra", &taskExtra},
        {"taskAudit", &taskAudit},
        {"taskExec", &taskExec},
        {"agentProfile", &agentProfile},
        {"taskRuns", &taskRuns},
        {"taskRunner", &taskRunner},
        {"scheduler", &scheduler},
        {"workflowRunner", &workflowRunner},
        {"workflowPresets", &workflowPresets},
        {"acceptanceRunner", &acceptanceRunner},
        {"scopeMatcher", &scopeMatcher},
        {"triageRunner", &triageRunner},
        {"ledgerImport", &ledgerImport},
        {"boardSync", &boardSync},
        {"projectMap", &projectMap},
    });
    // 阶段条跟随路线图（DX-08 / #11）：启动即开始监听 docs/iteration-roadmap.md，
    // 不必等全景面板首次打开才 watchFiles()。只 addPaths，不派生（不跑 git、不扫目录）。
    projectMap.startWatching();
    applyContextProperties(engine.rootContext(), {
        {"reminderScheduler", &reminderScheduler},
        {"boardColumns", &boardColumns},
    });
    // 项目清单与卡片归属（R10-02）：切换器读 projects()，卡片角标读 taskProject()
    applyContextProperties(engine.rootContext(), {
        {"projectStore", &projectStore},
        {"dragFeel", &dragFeel},
        {"keyMove", &keyMove},
        {"cardDetail", &cardDetail},
        {"uiSettings", &uiSettings},
        {"themePacks", &themePacks},
        {"themeMarket", &themeMarket},
        {"roadmap", &roadmap},
        {"dataPort", &dataPort},
        {"previewHub", &previewHub},
        {"updateManager", &updateManager},
    });
    // 启动即可按里程碑过滤看板：--stage R0
    const int stageIndex = arguments.indexOf(QStringLiteral("--stage"));
    QString initialStage;
    if (stageIndex >= 0 && stageIndex + 1 < arguments.size())
        initialStage = arguments.at(stageIndex + 1).trimmed().toUpper();
    engine.rootContext()->setContextProperty(QStringLiteral("initialStage"), initialStage);
    // 启动即打开执行队列面板：--exec-queue（截图 / 排障用，与 --map / --appearance 同类）
    const bool initialExecQueue = arguments.contains(QStringLiteral("--exec-queue"));
    engine.rootContext()->setContextProperty(QStringLiteral("initialExecQueue"), initialExecQueue);
    // 低配 / 无障碍降级：关闭动态霓虹背景（NEONBOARD_NO_BG=1 启动）
    const bool noBackground = qEnvironmentVariableIsSet("NEONBOARD_NO_BG");
    engine.rootContext()->setContextProperty(QStringLiteral("noBackground"), noBackground);
    qInfo() << "BOOT engine" << boot.elapsed();
    splashStage(0.58, QStringLiteral("COMPILING INTERFACE"), 50);

    const QUrl mainUrl(QStringLiteral("qrc:/qt/qml/NeonBoard/Main.qml"));
    qInfo().noquote() << "BOOT qml" << mainUrl.toString();
    qInfo() << "BOOT epoch" << QDateTime::currentMSecsSinceEpoch();

    // 启动耗时基准：--benchload [minimal|controls|button] 先加载一个探针 QML，
    // 用于区分「Qt Quick 固定初始化成本」与「Main.qml 树构建成本」
    QString benchVariant;
    const int benchIndex = arguments.indexOf(QStringLiteral("--benchload"));
    if (benchIndex >= 0) {
        benchVariant = QStringLiteral("minimal");
        if (benchIndex + 1 < arguments.size() && !arguments.at(benchIndex + 1).startsWith(QLatin1String("--")))
            benchVariant = arguments.at(benchIndex + 1);
        QByteArray probe = "import QtQuick\nimport QtQuick.Window\n"
                           "Window{width:64;height:64;visible:false}\n";
        if (benchVariant == QLatin1String("controls")) {
            probe = "import QtQuick\nimport QtQuick.Window\nimport QtQuick.Controls\n"
                    "Window{width:64;height:64;visible:false}\n";
        } else if (benchVariant == QLatin1String("button")) {
            probe = "import QtQuick\nimport QtQuick.Window\nimport QtQuick.Controls\n"
                    "Window{width:64;height:64;visible:false;Row{Button{text:\"x\"}}}\n";
        }
        const qint64 t0 = boot.elapsed();
        engine.loadData(probe, QUrl(QStringLiteral("qrc:/bench/probe.qml")));
        qInfo() << "BENCH probe" << benchVariant << (boot.elapsed() - t0);
    }

    const qint64 tMain = boot.elapsed();
    engine.load(mainUrl);
    qInfo() << "BOOT loaded" << boot.elapsed();

    if (benchIndex >= 0) {
        qInfo() << "BENCH main only" << (boot.elapsed() - tMain);
        return 0;
    }
    if (!pluginLoader.scanFinished()) pluginLoader.scan();
    pluginRuntime.start();
    // 后台插件就绪后再开始消费事件队列：启动时攒下的事件会被这一轮补投递
    lateCli.hookQueue.start(1000);
    splashStage(0.86, QStringLiteral("LINKING MODULES"), 60);
    if (engine.rootObjects().isEmpty()) {
        if (splash)
            splash->close();
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window) {
        qCritical() << "Root object is not a QQuickWindow";
        if (splash)
            splash->close();
        return 1;
    }

    metrics.markWindow(window);
    windowController.attach(window);
    // 窗口就绪启动时序与 GUI 动作接线（MOD-9）：dispatch 由启动收尾复用，splashStage 以参数传入。
    const auto dispatch = wireShellActions(window, hosted, singleInstance.get(),
                                           hotkey, tray, windowController,
                                           taskStore, scheduler, updateManager,
                                           reminderScheduler, boot, app,
                                           splashStage);

    if (silentStart) {
        QTimer::singleShot(0, window, [window]() { window->hide(); });
        tray.showMessage(QStringLiteral("NeonBoard"),
                         QStringLiteral("正在后台运行，按 Ctrl+Shift+Space 唤起"));
    } else {
        if (splash) {
            splash->finish();
            splash.reset();
        }
        // 显示窗口 + 按需直接打开命令行指定的面板（--newtask/--settings/...）。
        // 300ms 是留给入场动画的量，热实例（二次启动）不需要等。
        dispatch(startupAction, 300);
        qInfo() << "BOOT shown" << boot.elapsed();
    }

    // 启动后的自动更新检查：延到首帧之后，且由 UpdateManager 按 20 小时节流，
    // 不让它和启动性能抢资源（R4-08 的启动瘦身成果不能因为这功能退回去）。
    QTimer::singleShot(8000, &app, [&updateManager]() { updateManager.startupCheck(); });

    const bool smoke = app.arguments().contains(QStringLiteral("--smoke"));
    if (smoke) {
        QTimer::singleShot(4000, &app, [&]() {
            qInfo().noquote() << "METRICS" << metrics.report();
            windowController.quit();
        });
    }

    qInfo().noquote() << "NeonBoard started";
    return app.exec();
}
