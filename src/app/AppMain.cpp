#include "AppMain.h"

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
static void emitCliLine(const QString &line)
{
    const QByteArray bytes = line.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

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

    // 通用撤销栈：删除 / 移动 / 完成 / 编辑 四类用户操作统一可撤销
    TaskUndo taskUndo(&taskStore);

    // 主任务 / 子任务关联（边表 task_link，不动 task 表结构）
    TaskTree taskTree(&database, &taskStore);

    // 完成统计（热力图 / 连续天数 / 周吞吐）：只读任务表，不建新表
    TaskStats taskStats(&database, &taskStore);

    // 任务附加属性（提醒 / 重复）：侧表 task_extra，不动 task 表结构
    TaskExtra taskExtra(&database, &taskStore);
    taskUndo.setTaskExtra(&taskExtra);

    // 卡片详情抽屉（R8-03）：补丁的脏检查与保存收口，避免「没改也写一次库」。
    // 依赖顺序：要拿 taskExtra 写提醒 / 重复（侧表），所以只能排在这里而不是 TaskUndo 之后
    CardDetail cardDetail(&taskStore, &taskExtra, &taskUndo);

    // 卡片事件审计（R8-04）：侧表 task_event，不动 task 表结构、不升 user_version。
    // 挂在 taskStore 的生命周期信号上，所以必须早于任何写入（包括自检与导入）。
    TaskAudit taskAudit(&database, &taskStore);

    // 任务执行定义（目标 / 验收 / 边界 / 执行参数）：侧表 task_exec（M0）
    TaskExec taskExec(&database, &taskStore);

    // agent profile / 总开关 / cwd 白名单：meta 表（R6-03），命令行只拼 argv
    AgentProfile agentProfile(&database);

    // 任务执行运行记录与同卡锁：侧表 task_run（R6-02），启动时回收孤儿 run
    TaskRunStore taskRuns(&database);

    // 任务执行器（R6-04）：拉起 agent 进程 / 流式日志 / 超时·终止 / 生命周期
    TaskRunner taskRunner(&database, &taskRuns, &agentProfile, &taskExec);

    // 执行队列调度器（R7-01）：FIFO + 并发≤2 + 背压 + 暂停/调序/跳过。
    // 只负责「取队头交给 TaskRunner」，进程与门锁仍归 TaskRunner / TaskRunStore。
    Scheduler scheduler(&database, &taskRuns, &taskRunner, &agentProfile, &taskExec, &taskStore);

    // 验收器与越界检查（R6-05）：角色链每步独立验收要用（R7-03 起真正接上）
    AcceptanceRunner acceptanceRunner;
    ScopeMatcher scopeMatcher;

    // 预设 workflow 与 orchestrator 选型（R7-04）：预设存 meta（可编辑），
    // 按 triage estimate.size / 卡片规模选 workflow、按规模裁剪 pm/architect（qa 永不裁）。
    WorkflowPresets workflowPresets(&database);

    // 角色链 workflow 引擎（R7-03）：按步骤数组逐步拉起 agent，每步一份 handoff。
    // 每一步仍走 TaskRunner（进程 / 超时 / 日志 / 心跳复用 R6-04）。
    // R7-04 起接上 WorkflowPresets：卡片没选 workflow 时由 orchestrator 选型。
    WorkflowRunner workflowRunner(&database, &taskExec, &taskRuns, &taskRunner, &agentProfile,
                                  &scopeMatcher, &acceptanceRunner);
    workflowRunner.setPresets(&workflowPresets);


    // Triage 只读分析（R6-07）：草案只写 meta 表，不进台账；只读靠
    // profile readonlyArgs + 分析前后 git status 一致两道硬门
    TriageRunner triageRunner(&database, &agentProfile, &taskExec, &taskStore);

    // 台账卡单向导入（R6-09，ADR-0001）：台账 → board.db 只补空值，
    // 冲突/未建卡只报告；反向写台账没有代码路径
    LedgerImport ledgerImport(&database, &taskStore, &taskExec);

    // 路线图快照同步：仓库 progress.json ↔ 本地看板
    RoadmapSync roadmap(&database, &taskStore);

    // 多工作区领用同步：扫描锚点与各 worktree 台账，把「谁在领哪张卡」反映到看板
    BoardSync boardSync(&database, &taskStore);

    // 项目全景图数据源：解析 board-ledger.json + git done 标记，供侧边浮层展示
    ProjectMap projectMap;

    // 数据导入导出与自动备份
    DataPort dataPort(&database, &taskStore);

    {
        const int exportIndex = arguments.indexOf(QStringLiteral("--export-tasks"));
        const int importIndex = arguments.indexOf(QStringLiteral("--import-tasks"));
        const bool backupMode = arguments.contains(QStringLiteral("--backup"));
        if (exportIndex >= 0 || importIndex >= 0 || backupMode) {
            QString message;
            bool ok = false;
            if (backupMode) {
                message = dataPort.backupNow();
                ok = message.isEmpty();
                if (ok)
                    qInfo().noquote() << "BACKUP OK" << dataPort.backupDir();
            } else if (exportIndex >= 0) {
                QString path;
                if (exportIndex + 1 < arguments.size())
                    path = arguments.at(exportIndex + 1);
                QString format = QStringLiteral("json");
                if (exportIndex + 2 < arguments.size())
                    format = arguments.at(exportIndex + 2);
                if (path.isEmpty())
                    path = dataPort.defaultPath(format);
                message = dataPort.exportTasks(path, format);
                ok = message.isEmpty();
                if (ok)
                    qInfo().noquote() << "EXPORT OK" << path;
            } else {
                QString path;
                if (importIndex + 1 < arguments.size())
                    path = arguments.at(importIndex + 1);
                QString format = QStringLiteral("auto");
                if (importIndex + 2 < arguments.size())
                    format = arguments.at(importIndex + 2);
                message = dataPort.importTasks(path, format);
                ok = message.isEmpty();
                if (ok)
                    qInfo().noquote() << "IMPORT OK" << path;
            }
            if (!ok)
                qWarning().noquote() << "DATA FAIL" << message;
            return ok ? 0 : 4;
        }
    }

    const int syncIndex = arguments.indexOf(QStringLiteral("--sync-roadmap"));
    const int exportIndex = arguments.indexOf(QStringLiteral("--export-roadmap"));
    if (syncIndex >= 0 || exportIndex >= 0) {
        QString target;
        if (syncIndex >= 0 && syncIndex + 1 < arguments.size())
            target = arguments.at(syncIndex + 1);
        if (exportIndex >= 0 && exportIndex + 1 < arguments.size())
            target = arguments.at(exportIndex + 1);
        if (target.isEmpty())
            target = RoadmapSync::resolvePath();
        const bool force = arguments.contains(QStringLiteral("--force"));
        const bool ok = exportIndex >= 0 ? roadmap.exportTo(target)
                                          : roadmap.importFrom(target, force);
        qInfo().noquote() << "ROADMAP" << (ok ? "OK" : "FAIL") << target << roadmap.lastReport();
        return ok ? 0 : 3;
    }

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
    const int emitHookIndex = arguments.indexOf(QStringLiteral("--emit-hook"));
    const int emitHookFileIndex = arguments.indexOf(QStringLiteral("--emit-hook-file"));
    // --hook-event <name>：可选投递事件名，默认 hook.commit。
    // 让其他来源（如 agent.task 事件域）复用同一条投递链路而不污染计划钩子的解析。
    const int eventNameIndex = arguments.indexOf(QStringLiteral("--hook-event"));
    QString eventName = QStringLiteral("hook.commit");
    if (eventNameIndex >= 0 && eventNameIndex + 1 < arguments.size()
        && !arguments.at(eventNameIndex + 1).trimmed().isEmpty())
        eventName = arguments.at(eventNameIndex + 1).trimmed();
    if (emitHookIndex >= 0 || emitHookFileIndex >= 0) {
        QString payloadText;
        if (emitHookFileIndex >= 0 && emitHookFileIndex + 1 < arguments.size()) {
            QFile payloadFile(arguments.at(emitHookFileIndex + 1));
            if (payloadFile.open(QIODevice::ReadOnly | QIODevice::Text))
                payloadText = QString::fromUtf8(payloadFile.readAll());
            else
                qWarning().noquote() << "EMIT-HOOK cannot read" << arguments.at(emitHookFileIndex + 1);
        } else if (emitHookIndex >= 0 && emitHookIndex + 1 < arguments.size()) {
            payloadText = arguments.at(emitHookIndex + 1);
        }
        if (payloadText.trimmed().isEmpty()) {
            qWarning() << "EMIT-HOOK empty payload";
            return 2;
        }
        HookEventQueue emitQueue(AppPaths::hookDbPath());
        QString emitError;
        const bool emitted = emitQueue.open(&emitError)
            && emitQueue.enqueue(eventName,
                                  {{QStringLiteral("text"), payloadText},
                                   {QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch()}},
                                  &emitError);
        qInfo().noquote() << "EMIT-HOOK" << (emitted ? "OK" : "FAIL") << emitError;
        return emitted ? 0 : 2;
    }

    // Windows 原生集成（R4-02）：.neon 文件关联的注册 / 注销 / 状态查询。
    // 只写 HKCU，不需要管理员；注册后双击插件包即走 --install-plugin-file。
    if (arguments.contains(QStringLiteral("--assoc-status"))) {
        const QVariantMap status = WindowsIntegration::associationStatus();
        emitCliLine(status.value(QStringLiteral("registered")).toBool()
                        ? QStringLiteral("OK registered")
                        : QStringLiteral("OK not-registered"));
        emitCliLine(QStringLiteral("class: %1")
                        .arg(status.value(QStringLiteral("classRoot")).toString()));
        emitCliLine(QStringLiteral("command: %1")
                        .arg(status.value(QStringLiteral("command")).toString()));
        return 0;
    }
    if (arguments.contains(QStringLiteral("--register-file-assoc"))
        || arguments.contains(QStringLiteral("--unregister-file-assoc"))) {
        const bool registering = arguments.contains(QStringLiteral("--register-file-assoc"));
        QString assocError;
        const bool assocOk = registering
            ? WindowsIntegration::registerFileAssociations(&assocError)
            : WindowsIntegration::unregisterFileAssociations(&assocError);
        if (!assocOk) {
            qWarning().noquote() << "ASSOC FAIL" << assocError;
            emitCliLine(QStringLiteral("FAIL %1").arg(assocError));
            return 2;
        }
        emitCliLine(QStringLiteral("OK"));
        return 0;
    }

    // 本地 .neon 插件包安装：--install-plugin-file <path>
    // 供脚本化安装与自检使用；包格式见 docs/plugin-api.md
    const int installPluginFileIndex = arguments.indexOf(QStringLiteral("--install-plugin-file"));
    if (installPluginFileIndex >= 0) {
        if (installPluginFileIndex + 1 >= arguments.size()) {
            qWarning() << "INSTALL-PLUGIN missing package path";
            return 2;
        }
        const QString packagePath = arguments.at(installPluginFileIndex + 1);
        const QString pluginId = PluginPackage::neonPluginId(packagePath);
        const QString dirName = PluginPackage::dirNameForId(pluginId);
        if (pluginId.isEmpty() || dirName.isEmpty()) {
            qWarning().noquote() << "INSTALL-PLUGIN invalid package" << packagePath;
            return 2;
        }
        const QString targetDir = AppPaths::pluginDir() + QLatin1Char('/') + dirName;
        QString installError;
        if (!PluginPackage::installNeonFile(packagePath, targetDir, &installError)) {
            qWarning().noquote() << "INSTALL-PLUGIN FAIL" << installError;
            return 2;
        }
        qInfo().noquote() << "INSTALL-PLUGIN OK" << pluginId << targetDir;
        return 0;
    }

    if (arguments.contains(QStringLiteral("--selftest"))) {
        return runSelfTest(SelfTestDeps{
            &database, &taskStore, &taskUndo, &taskTree, &taskExtra,
            &taskExec, &taskRuns, &workflowPresets, &triageRunner,
            &roadmap, &boardColumns});
    }

    // 路线图同步接线：先装好「文件侧轮询 + 看板侧写回」，再做首次导入，
    // 否则启动导入触发的 countsChanged 会先于连接建立，导致看板侧改动不写回。
    roadmap.setPath(RoadmapSync::resolvePath());

    QTimer roadmapPoll;
    roadmapPoll.setInterval(15000);
    QObject::connect(&roadmapPoll, &QTimer::timeout, &roadmap, [&roadmap]() { roadmap.poll(); });
    roadmapPoll.start();

    QTimer roadmapExport;
    roadmapExport.setSingleShot(true);
    roadmapExport.setInterval(1500);
    QObject::connect(&taskStore, &TaskModel::countsChanged, &roadmapExport, [&]() {
        roadmapExport.start();
    });
    QObject::connect(&roadmapExport, &QTimer::timeout, &roadmap, [&roadmap]() {
        if (roadmap.enabled() && !roadmap.isApplying() && RoadmapSync::autoExportEnabled())
            roadmap.exportAll();
    });

    if (!deferredSchema && roadmap.importAll(false)) qInfo().noquote() << "Roadmap: synced" << roadmap.path();

    // 领用同步：启动即扫一次，之后每 5 秒轮询各 worktree 台账
    QTimer claimPoll;
    claimPoll.setInterval(5000);
    QObject::connect(&claimPoll, &QTimer::timeout, &boardSync, [&boardSync]() { boardSync.refresh(); });
    claimPoll.start();
    boardSync.refresh();

    // 每日首次启动自动备份数据库（先落 WAL 再复制，保证备份完整）
    if (!deferredSchema) {
        const QString m = dataPort.backupIfNeeded();
        if (m.isEmpty()) qInfo().noquote() << "Backup: daily snapshot ready" << dataPort.backupDir();
        else qWarning().noquote() << "Backup failed" << m;
    }

    Metrics metrics;
    QObject::connect(&metrics, &Metrics::startupMeasured, [&boot, &metrics]() {
        // 同时打墙钟与 CPU：墙钟随机器负载乱飘，CPU 才是改动有没有用的判据
        qInfo() << "BOOT firstframe" << boot.elapsed() << "cpu" << metrics.startupCpuMs();
    });

    nbDeferUntilFirstFrame(&metrics, &app, [&]() {
        if (!deferredSchema) return;
        if (QString e; !database.finalizeSchema(&e)) qWarning().noquote() << "Database: deferred schema failed" << e;
        else qInfo() << "BOOT schema finalized" << boot.elapsed();
        taskStore.load();
        if (roadmap.importAll(false)) qInfo().noquote() << "Roadmap: synced" << roadmap.path();
        if (const QString m = dataPort.backupIfNeeded(); !m.isEmpty()) qWarning().noquote() << "Backup failed" << m;
    });
    UiSettings uiSettings;
    // 外部主题包（R8-01）：启动即扫描 themes/ 目录。
    // 坏包只跳过并记一行日志，绝不拦住启动 —— 主题是锦上添花，不是启动前置条件。
    ThemePack themePacks;
    // 主题包市场（R8-02）：目录内置在客户端，断网也能浏览；安装走原子替换。
    // 依赖 themePacks 才能判断「这个包装了没」，所以必须排在它后面。
    ThemeMarket themeMarket(&themePacks);
    WindowController windowController;
    Hotkey hotkey;
    TrayController tray(hosted);
    // 预览出图（验收用）：托盘菜单离屏抓成 PNG，不弹到屏幕上（逻辑在 TrayController 里）。
    if (const int menuPreviewCode = tray.handleMenuPreview(arguments); menuPreviewCode >= 0)
        return menuPreviewCode;
    // 提醒调度器：30s 轮询，托盘通知 + 窗口辉光；启动后先查一轮补发漏提醒
    ReminderScheduler reminderScheduler(&taskExtra, &tray);
    PluginLoader pluginLoader(&database);
    pluginLoader.scanAsync();
    PluginEventBus pluginEventBus;
    PluginRuntime pluginRuntime(&pluginLoader, &pluginEventBus, &database, &taskStore, &tray);
    nbConnectStartupReplay(deferredSchema, &database, &Database::schemaFinalized, &taskTree, &taskRuns, &pluginRuntime, &app);

    // R9-01：把插件声明的 agent profile 注入 AgentProfile（三层合并里的 L1）。
    // 挂信号而不是只调一次：插件热插拔会重扫（pluginsChanged），
    // 用户给插件补授权后已授权声明集合也会变（surfacesChanged）。
    const auto refreshDeclaredAgents = [&pluginRuntime, &agentProfile]() {
        agentProfile.setDeclaredProfiles(pluginRuntime.declaredAgentProfiles());
    };
    QObject::connect(&pluginLoader, &PluginLoader::pluginsChanged, &agentProfile,
                     refreshDeclaredAgents);
    QObject::connect(&pluginRuntime, &PluginRuntime::surfacesChanged, &agentProfile,
                     refreshDeclaredAgents);
    refreshDeclaredAgents();

    PluginHost pluginHost(&pluginRuntime);
    PluginMarket pluginMarket;
    PluginLibrary pluginLibrary(&pluginLoader, &pluginMarket, &pluginRuntime, &database);
    // 预览中心：发现各 agent 工作区，一键构建并启动隔离的预览实例
    PreviewHub previewHub;
    tray.setInstanceLabel(previewHub.instanceLabel());
    // 软件更新：清单拉取 → 版本比较 → 下载校验 → 安装 / 便携版自替换
    UpdateManager updateManager;
    qInfo().noquote() << "Update: version" << updateManager.currentVersion()
                      << "kind" << updateManager.installKind();

    // --plugin-report：输出插件平台表面清单（插件 / 命令 / 槽位 / 设置页 / 授权缺口），
    // 走 stdout JSON，方便脚本与无头环境验收。
    if (arguments.contains(QStringLiteral("--plugin-report"))) {
        QJsonArray plugins;
        for (const QVariant &entry : pluginLoader.plugins()) {
            const QVariantMap map = entry.toMap();
            const QString id = map.value(QStringLiteral("id")).toString();
            QJsonObject item;
            item.insert(QStringLiteral("id"), id);
            item.insert(QStringLiteral("name"), map.value(QStringLiteral("name")).toString());
            item.insert(QStringLiteral("version"), map.value(QStringLiteral("version")).toString());
            item.insert(QStringLiteral("status"), map.value(QStringLiteral("status")).toString());
            item.insert(QStringLiteral("source"), map.value(QStringLiteral("source")).toString());
            QJsonArray permissions;
            for (const QVariant &permission : map.value(QStringLiteral("permissions")).toList())
                permissions.append(permission.toString());
            item.insert(QStringLiteral("permissions"), permissions);
            QJsonArray missing;
            for (const QString &permission : pluginRuntime.missingPermissions(id))
                missing.append(permission);
            item.insert(QStringLiteral("missingPermissions"), missing);
            plugins.append(item);
        }

        QJsonObject slotMap;
        slotMap.insert(QStringLiteral("sidebar"),
                       QJsonArray::fromVariantList(pluginRuntime.sidebarSlotItems()));
        slotMap.insert(QStringLiteral("bottom"),
                       QJsonArray::fromVariantList(pluginRuntime.bottomSlotItems()));
        slotMap.insert(QStringLiteral("card"),
                       QJsonArray::fromVariantList(pluginRuntime.cardSlotItems()));

        // R9-01：agent profile 的三层合并结果 + 插件声明缺口。
        // 要能一眼看出「谁声明的、授权没有、这台机器上跑不跑得起来」——
        // cursor-agent 那种「列表里有却调不动」的坑就是靠 available 暴露的。
        QStringList pluginIds;
        for (const QVariant &value : pluginRuntime.declaredAgentProfiles())
            pluginIds.append(value.toMap().value(QStringLiteral("id")).toString());
        QStringList builtinIds;
        for (const QVariant &value : AgentProfile::builtinProfiles())
            builtinIds.append(value.toMap().value(QStringLiteral("id")).toString());

        QJsonArray agentEffective;
        for (const QVariant &value : agentProfile.profiles()) {
            const QVariantMap profile = value.toMap();
            const QString id = profile.value(QStringLiteral("id")).toString();
            QJsonObject item;
            item.insert(QStringLiteral("id"), id);
            item.insert(QStringLiteral("displayName"),
                        profile.value(QStringLiteral("displayName")).toString());
            item.insert(QStringLiteral("exe"), profile.value(QStringLiteral("exe")).toString());
            item.insert(QStringLiteral("available"), agentProfile.isAvailable(id));
            item.insert(QStringLiteral("source"),
                        pluginIds.contains(id) ? QStringLiteral("plugin")
                        : builtinIds.contains(id) ? QStringLiteral("builtin")
                                                  : QStringLiteral("user"));
            agentEffective.append(item);
        }

        QJsonArray agentDeclared;
        for (const QVariant &value : pluginRuntime.agentProfileDeclarations()) {
            const QVariantMap row = value.toMap();
            QJsonArray declaredIds;
            for (const QVariant &profile : row.value(QStringLiteral("profiles")).toList())
                declaredIds.append(profile.toMap().value(QStringLiteral("id")).toString());
            QJsonObject item;
            item.insert(QStringLiteral("pluginId"),
                        row.value(QStringLiteral("pluginId")).toString());
            item.insert(QStringLiteral("authorized"),
                        row.value(QStringLiteral("authorized")).toBool());
            item.insert(QStringLiteral("profiles"), declaredIds);
            agentDeclared.append(item);
        }

        QJsonObject agentSection;
        agentSection.insert(QStringLiteral("effective"), agentEffective);
        agentSection.insert(QStringLiteral("declared"), agentDeclared);

        QJsonObject report;
        report.insert(QStringLiteral("plugins"), plugins);
        report.insert(QStringLiteral("commands"),
                      QJsonArray::fromVariantList(pluginRuntime.pluginCommands()));
        report.insert(QStringLiteral("settingsPages"),
                      QJsonArray::fromVariantList(pluginRuntime.settingsPages()));
        report.insert(QStringLiteral("slots"), slotMap);
        report.insert(QStringLiteral("agentProfiles"), agentSection);

        QTextStream out(stdout);
        out.setEncoding(QStringConverter::Utf8);
        out << QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented));
        out.flush();
        return 0;
    }

    // 钩子事件队列：与看板库物理隔离，主进程轮询消费后转成插件事件
    HookEventQueue hookQueue(AppPaths::hookDbPath());
    QString hookQueueError;
    if (!hookQueue.open(&hookQueueError))
        qWarning().noquote() << "HookEventQueue: open failed" << hookQueueError;
    else
        hookQueue.setEventBus(&pluginEventBus);

    // 插件市场命令行：--market-source <spec>... 设源（可重复、可逗号分隔）；
    // --market-sources 列出当前源；--market-refresh 拉索引；
    // --market-install <id> 装插件；--install-plugin-url <url> 直链安装。
    // 都走同步接口，方便脚本与无头环境验证。
    QStringList marketSpecs;
    for (int i = 0; i < arguments.size(); ++i) {
        if (arguments.at(i) != QStringLiteral("--market-source"))
            continue;
        if (i + 1 >= arguments.size())
            break;
        const QStringList parts =
            arguments.at(i + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString trimmed = part.trimmed();
            if (!trimmed.isEmpty() && !marketSpecs.contains(trimmed))
                marketSpecs.append(trimmed);
        }
    }
    if (!marketSpecs.isEmpty()) {
        QVariantList sources;
        for (const QString &spec : marketSpecs) {
            QVariantMap record;
            record.insert(QStringLiteral("spec"), spec);
            record.insert(QStringLiteral("name"), QString());
            record.insert(QStringLiteral("enabled"), true);
            sources.append(record);
        }
        pluginMarket.setSources(sources);
        qInfo().noquote() << "MARKET-SOURCE OK" << sources.size() << "source(s)"
                          << marketSpecs.join(QStringLiteral(", "));
        return 0;
    }

    if (arguments.contains(QStringLiteral("--market-sources"))) {
        const QVariantList sources = pluginLibrary.marketSources();
        qInfo().noquote() << "MARKET-SOURCES" << sources.size();
        for (const QVariant &value : sources) {
            const QVariantMap record = value.toMap();
            qInfo().noquote() << "  " << record.value(QStringLiteral("name")).toString()
                              << "|" << record.value(QStringLiteral("spec")).toString()
                              << (record.value(QStringLiteral("enabled")).toBool()
                                      ? QStringLiteral("|enabled")
                                      : QStringLiteral("|disabled"));
        }
        return 0;
    }

    if (arguments.contains(QStringLiteral("--market-refresh"))) {
        QString marketError;
        const bool refreshed = pluginLibrary.refreshMarketBlocking(10000, &marketError);
        if (refreshed) {
            qInfo().noquote() << "MARKET-REFRESH OK" << pluginMarket.entries().size()
                              << pluginMarket.statusText();
            for (const QVariant &value : pluginMarket.sourceStatuses()) {
                const QVariantMap status = value.toMap();
                qInfo().noquote() << "MARKET-SOURCE" << status.value(QStringLiteral("name")).toString()
                                  << (status.value(QStringLiteral("ok")).toBool() ? QStringLiteral("OK")
                                                                                : QStringLiteral("FAIL"))
                                  << status.value(QStringLiteral("message")).toString();
            }
            return 0;
        }
        qWarning().noquote() << "MARKET-REFRESH FAIL" << marketError;
        return 2;
    }

    const int marketInstallIndex = arguments.indexOf(QStringLiteral("--market-install"));
    if (marketInstallIndex >= 0) {
        const QString pluginId = marketInstallIndex + 1 < arguments.size()
            ? arguments.at(marketInstallIndex + 1) : QString();
        if (pluginId.isEmpty()) {
            qWarning() << "MARKET-INSTALL missing plugin id";
            return 2;
        }
        QString installError;
        if (pluginLibrary.installBlocking(pluginId, &installError)) {
            qInfo().noquote() << "MARKET-INSTALL OK" << pluginId;
            return 0;
        }
        qWarning().noquote() << "MARKET-INSTALL FAIL" << installError;
        return 2;
    }

    const int directInstallIndex = arguments.indexOf(QStringLiteral("--install-plugin-url"));
    if (directInstallIndex >= 0) {
        const QString packageUrl = directInstallIndex + 1 < arguments.size()
            ? arguments.at(directInstallIndex + 1) : QString();
        if (packageUrl.trimmed().isEmpty()) {
            qWarning() << "INSTALL-PLUGIN-URL missing url";
            return 2;
        }
        QString installError;
        if (pluginLibrary.installFromUrlBlocking(packageUrl.trimmed(), &installError)) {
            qInfo().noquote() << "INSTALL-PLUGIN-URL OK" << packageUrl.trimmed();
            return 0;
        }
        qWarning().noquote() << "INSTALL-PLUGIN-URL FAIL" << installError;
        return 2;
    }

    // 预览中心命令行：--preview-list 列工作区；--preview-build <id> 构建；
    // --preview-start <id> 启动预览实例；--preview-stop <id> 停止实例。
    // 命令行进程跑完即退，不该把自己登记成“运行中的实例”。
    previewHub.setSelfRegisterEnabled(false);
    if (arguments.contains(QStringLiteral("--preview-list"))) {
        previewHub.refresh();
        qInfo().noquote() << "PREVIEW-LIST" << previewHub.listJson();
        emitCliLine(previewHub.listJson());
        return 0;
    }

    const int previewBuildIndex = arguments.indexOf(QStringLiteral("--preview-build"));
    if (previewBuildIndex >= 0) {
        const QString agentId = previewBuildIndex + 1 < arguments.size()
            ? arguments.at(previewBuildIndex + 1) : QString();
        if (agentId.isEmpty()) {
            qWarning() << "PREVIEW-BUILD missing agent id";
            emitCliLine(QStringLiteral("FAIL --preview-build missing agent id"));
            return 2;
        }
        previewHub.refresh();
        QString buildError;
        if (previewHub.buildBlocking(agentId, &buildError)) {
            qInfo().noquote() << "PREVIEW-BUILD OK" << agentId;
            emitCliLine(QStringLiteral("OK %1").arg(agentId));
            return 0;
        }
        qWarning().noquote() << "PREVIEW-BUILD FAIL" << agentId << buildError;
        emitCliLine(QStringLiteral("FAIL %1 %2").arg(agentId, buildError));
        return 2;
    }

    const int previewPackageIndex = arguments.indexOf(QStringLiteral("--preview-package"));
    if (previewPackageIndex >= 0) {
        const QString agentId = previewPackageIndex + 1 < arguments.size()
            ? arguments.at(previewPackageIndex + 1) : QString();
        if (agentId.isEmpty()) {
            qWarning() << "PREVIEW-PACKAGE missing agent id";
            emitCliLine(QStringLiteral("FAIL --preview-package missing agent id"));
            return 2;
        }
        previewHub.refresh();
        QString packageError;
        if (previewHub.packageBlocking(agentId, &packageError)) {
            qInfo().noquote() << "PREVIEW-PACKAGE OK" << agentId;
            emitCliLine(QStringLiteral("OK %1").arg(agentId));
            return 0;
        }
        qWarning().noquote() << "PREVIEW-PACKAGE FAIL" << agentId << packageError;
        emitCliLine(QStringLiteral("FAIL %1 %2").arg(agentId, packageError));
        return 2;
    }

    const int previewStartIndex = arguments.indexOf(QStringLiteral("--preview-start"));    if (previewStartIndex >= 0) {
        const QString agentId = previewStartIndex + 1 < arguments.size()
            ? arguments.at(previewStartIndex + 1) : QString();
        if (agentId.isEmpty()) {
            qWarning() << "PREVIEW-START missing agent id";
            emitCliLine(QStringLiteral("FAIL --preview-start missing agent id"));
            return 2;
        }
        previewHub.setDetachOnStart(true);
        previewHub.refresh();
        QString startError;
        if (previewHub.startBlocking(agentId, &startError)) {
            qInfo().noquote() << "PREVIEW-START OK" << agentId;
            emitCliLine(QStringLiteral("OK %1").arg(agentId));
            return 0;
        }
        qWarning().noquote() << "PREVIEW-START FAIL" << agentId << startError;
        emitCliLine(QStringLiteral("FAIL %1 %2").arg(agentId, startError));
        return 2;
    }

    const int previewStopIndex = arguments.indexOf(QStringLiteral("--preview-stop"));
    if (previewStopIndex >= 0) {
        const QString agentId = previewStopIndex + 1 < arguments.size()
            ? arguments.at(previewStopIndex + 1) : QString();
        if (agentId.isEmpty()) {
            qWarning() << "PREVIEW-STOP missing agent id";
            emitCliLine(QStringLiteral("FAIL --preview-stop missing agent id"));
            return 2;
        }
        previewHub.refresh();
        QString stopError;
        if (previewHub.stopBlocking(agentId, &stopError)) {
            qInfo().noquote() << "PREVIEW-STOP OK" << agentId;
            emitCliLine(QStringLiteral("OK %1").arg(agentId));
            return 0;
        }
        qWarning().noquote() << "PREVIEW-STOP FAIL" << agentId << stopError;
        emitCliLine(QStringLiteral("FAIL %1 %2").arg(agentId, stopError));
        return 2;
    }

    // ---- 软件更新（R5-01）：命令行检查与清单源配置 ----
    // 走 utilityMode：应用正在运行时也能查，不会撞单实例锁。
    if (arguments.contains(QStringLiteral("--update-sources"))) {
        const QStringList sources = updateManager.updateSources();
        for (const QString &source : sources)
            emitCliLine(source);
        return 0;
    }

    const int updateSourceIndex = arguments.indexOf(QStringLiteral("--update-source"));
    if (updateSourceIndex >= 0) {
        const QString spec = updateSourceIndex + 1 < arguments.size()
            ? arguments.at(updateSourceIndex + 1).trimmed() : QString();
        if (spec.isEmpty()) {
            emitCliLine(QStringLiteral("FAIL --update-source missing url"));
            return 2;
        }
        updateManager.setUpdateSource(spec);
        qInfo().noquote() << "UPDATE-SOURCE OK" << spec;
        emitCliLine(QStringLiteral("OK %1").arg(spec));
        return 0;
    }

    if (arguments.contains(QStringLiteral("--check-update"))) {
        QString checkError;
        // 用默认总预算（25 秒）：候选源串行 + 单源 6 秒看门狗，最坏 ≈18 秒。
        // 这里写死过 15 秒，配 10 秒单源超时时，挂住的 raw 源会把兜底源挤掉。
        if (!updateManager.checkBlocking(UpdateManager::kDefaultCheckTimeoutMs, &checkError)) {
            qWarning().noquote() << "CHECK-UPDATE FAIL" << checkError;
            emitCliLine(QStringLiteral("FAIL %1").arg(checkError));
            return 2;
        }
        emitCliLine(updateManager.updateAvailable()
                        ? QStringLiteral("OK available %1").arg(updateManager.latestVersion())
                        : QStringLiteral("OK uptodate %1").arg(updateManager.currentVersion()));
        // 同步打进日志：CI 会扫日志找异常，命令行结果只进 stdout 不够追溯
        qInfo().noquote() << "CHECK-UPDATE" << (updateManager.updateAvailable() ? "available" : "uptodate")
                          << "current" << updateManager.currentVersion()
                          << "latest" << updateManager.latestVersion()
                          << "kind" << updateManager.installKind();
        emitCliLine(QStringLiteral("current  %1").arg(updateManager.currentVersion()));
        emitCliLine(QStringLiteral("latest   %1").arg(updateManager.latestVersion()));
        emitCliLine(QStringLiteral("kind     %1").arg(updateManager.installKind()));
        emitCliLine(QStringLiteral("state    %1").arg(updateManager.state()));
        emitCliLine(QStringLiteral("notes    %1").arg(updateManager.releaseNotes()));
        return 0;
    }

    // ---- 软件更新（R5-03）：无人值守走完「检查 → 下载 → 校验 → 静默安装」----
    // 静默升级是整条更新链路的最后一环。只能靠人手点设置面板验证的话，这一环就永远
    // 停在「代码看起来对」；有了这个开关，升级链路可以脚本化跑通、也能用于计划内
    // 批量升级（升级动作本身仍由安装包的 /VERYSILENT 完成，不改任何安装语义）。
    if (arguments.contains(QStringLiteral("--update-apply"))) {
        QString applyError;
        if (!updateManager.checkBlocking(UpdateManager::kDefaultCheckTimeoutMs, &applyError)) {
            qWarning().noquote() << "UPDATE-APPLY FAIL check" << applyError;
            emitCliLine(QStringLiteral("FAIL check %1").arg(applyError));
            return 2;
        }
        if (!updateManager.updateAvailable()) {
            qInfo().noquote() << "UPDATE-APPLY UPTODATE" << updateManager.currentVersion();
            emitCliLine(QStringLiteral("OK uptodate %1").arg(updateManager.currentVersion()));
            return 0;
        }

        QEventLoop loop;
        int exitCode = 2;
        // 进度只按 10% 打点：几十 MB 的下载每 1% 打一行会把日志刷成噪声
        int lastDecile = -1;
        QObject::connect(&updateManager, &UpdateManager::progressChanged, &loop, [&]() {
            const int decile = updateManager.progress() / 10;
            if (decile != lastDecile) {
                lastDecile = decile;
                qInfo().noquote() << "UPDATE-APPLY progress" << updateManager.progress() << '%';
            }
        });
        QObject::connect(&updateManager, &UpdateManager::installLaunched, &loop, [&]() {
            // 安装包已启动（/VERYSILENT /NORESTART /SUPPRESSMSGBOXES，宿主已在启动前
            // 被 taskkill，安装完成由 [Run] 段拉起宿主）。本进程到此结束，不等待安装结束。
            qInfo().noquote() << "UPDATE-APPLY INSTALLING" << updateManager.latestVersion()
                              << updateManager.installKind();
            emitCliLine(QStringLiteral("OK installing %1 %2")
                            .arg(updateManager.latestVersion(), updateManager.installKind()));
            exitCode = 0;
            loop.quit();
        });
        QObject::connect(&updateManager, &UpdateManager::errorOccurred, &loop,
                         [&](const QString &message) {
            qWarning().noquote() << "UPDATE-APPLY FAIL" << message;
            emitCliLine(QStringLiteral("FAIL %1").arg(message));
            exitCode = 2;
            loop.quit();
        });
        // 总闸：下载 + 校验 10 分钟没结果就判失败退出，避免脚本调用时无限挂起。
        // 这里不 abort 请求，让 UpdateManager 自行收尾 —— 进程马上退出，无意义。
        QTimer::singleShot(10 * 60 * 1000, &loop, [&]() {
            qWarning().noquote() << "UPDATE-APPLY FAIL timeout";
            emitCliLine(QStringLiteral("FAIL timeout"));
            exitCode = 2;
            loop.quit();
        });

        qInfo().noquote() << "UPDATE-APPLY START" << updateManager.currentVersion() << "->"
                          << updateManager.latestVersion() << updateManager.installKind();
        emitCliLine(QStringLiteral("DOWNLOAD %1").arg(updateManager.latestVersion()));
        updateManager.downloadAndInstall();
        loop.exec();
        return exitCode;
    }

    // ---- 执行结果（R6-06）：--run-summary <taskId> 输出该卡最近一次 run 的 JSON ----
    // 走 utilityMode：应用正在运行时也能查，不会撞单实例锁。
    const int runSummaryIndex = arguments.indexOf(QStringLiteral("--run-summary"));
    if (runSummaryIndex >= 0) {
        const QString taskId = runSummaryIndex + 1 < arguments.size()
            ? arguments.at(runSummaryIndex + 1).trimmed() : QString();
        if (taskId.isEmpty()) {
            emitCliLine(QStringLiteral("FAIL --run-summary missing task id"));
            return 2;
        }
        const QVariantMap summary = taskRuns.summaryOf(taskId);
        const QJsonDocument doc(QJsonObject::fromVariantMap(summary));
        emitCliLine(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        return 0;
    }

    // ---- 台账单向导入（R6-09，ADR-0001）：--import-ledger [path|短号] ----
    // 参数是已存在的文件 → 按该台账全量导入；否则当短号只导一张卡（台账自动探测）。
    // 只补空值不覆盖；冲突 / 未建卡只报告。反向写台账没有代码路径。
    const int ledgerIndex = arguments.indexOf(QStringLiteral("--import-ledger"));
    if (ledgerIndex >= 0) {
        const QString arg = ledgerIndex + 1 < arguments.size()
            ? arguments.at(ledgerIndex + 1).trimmed() : QString();
        QVariantMap result;
        if (!arg.isEmpty() && QFileInfo::exists(arg))
            result = ledgerImport.importAll(arg);
        else if (arg.isEmpty())
            result = ledgerImport.importAll();
        else
            result = ledgerImport.importCard(arg);
        if (!result.value(QStringLiteral("ok")).toBool()) {
            emitCliLine(QStringLiteral("FAIL %1").arg(result.value(QStringLiteral("error")).toString()));
            return 2;
        }
        const QJsonDocument doc(QJsonObject::fromVariantMap(result));
        emitCliLine(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        return 0;
    }

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
    hookQueue.start(1000);
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
    if (!hosted)
        hotkey.install(window);
    // 窗口就绪后再启动提醒调度：首查补发漏提醒时 QML 辉光已能接住信号
    reminderScheduler.start(30000);
    qInfo() << "BOOT attached" << boot.elapsed();
    splashStage(0.95, QStringLiteral("FINALIZING"), 40);

    // 动作分发：冷启动参数与二次启动（本地管道）共用这一条路径。
    // delayMs 用于等 QML 首帧稳定；热实例窗口已就绪，不需要等。
    const auto dispatch = [window, &windowController](const QString &action, int delayMs) {
        if (action == QLatin1String(AppActions::kNoop))
            return; // 静默拉起：主实例已在运行，不打扰用户
        const AppActions::Target target = AppActions::targetFor(action);
        windowController.showAndActivate();
        if (target.method.isEmpty())
            return; // show（或只认识"亮窗口"的动作）到此为止
        QTimer::singleShot(delayMs, window, [window, target]() {
            const QByteArray method = target.method.toUtf8();
            if (target.hasArgument)
                QMetaObject::invokeMethod(window, method.constData(),
                                          Q_ARG(QVariant, target.argument));
            else
                QMetaObject::invokeMethod(window, method.constData());
        });
    };

    if (singleInstance) {
        // 二次启动：动作来自本地管道，等价于用户此刻在界面上点了一下，立即执行。
        // 注意这里必须等窗口建好再连 —— 启动瞬间（仍在 splash 里）到达的请求
        // 会丢面板动作，只保留"亮窗口"的最终效果（主实例本来就会显示自己）。
        QObject::connect(singleInstance.get(), &SingleInstance::actionRequested, window,
                         [dispatch](const QString &action) { dispatch(action, 0); });
    }

    QObject::connect(&hotkey, &Hotkey::activated, &windowController,
                     &WindowController::toggleVisibility);
    QObject::connect(&tray, &TrayController::showRequested, &windowController,
                     &WindowController::showAndActivate);
    QObject::connect(&tray, &TrayController::hideRequested, &windowController,
                     &WindowController::hideToTray);
    QObject::connect(&tray, &TrayController::quitRequested, &windowController,
                     &WindowController::quit);
    QObject::connect(&tray, &TrayController::newTaskRequested, &windowController,
                     &WindowController::showAndActivate);
    QObject::connect(&tray, &TrayController::newTaskRequested, window, [window]() {
            QMetaObject::invokeMethod(window, "openTaskComposer",
                                      Q_ARG(QVariant, QStringLiteral("todo")));
    });
    QObject::connect(&tray, &TrayController::settingsRequested, &windowController,
                     &WindowController::showAndActivate);
    QObject::connect(&tray, &TrayController::settingsRequested, window, [window]() {
        QMetaObject::invokeMethod(window, "openSettings");
    });
    QObject::connect(&taskStore, &TaskModel::countsChanged, &tray, [&taskStore, &tray]() {
        tray.updateStats(taskStore.todoCount(), taskStore.doingCount(), taskStore.doneTodayCount());
    });
    tray.updateStats(taskStore.todoCount(), taskStore.doingCount(), taskStore.doneTodayCount());

    // 夜间批量完成通知（R7-05）：队列跑空（queueDrained）时通过托盘提示一次，
    // 复用 R1 的托盘通道 —— 批量多在夜里跑，用户不一定盯着界面。
    QObject::connect(&scheduler, &Scheduler::queueDrained, &tray, [&tray]() {
        tray.showMessage(QStringLiteral("夜间批量已完成"),
                         QStringLiteral("执行队列已跑空，可到「执行队列」面板查看结果。"));
    });

    // 更新（R5-01）：发现新版本时托盘提示一次；安装动作一旦交出去
    // （安装包进程或便携版自更新脚本），本进程必须退出，否则文件被占用、覆盖失败。
    QObject::connect(&updateManager, &UpdateManager::updateFound, &tray,
                     [&tray](const QString &version) {
                         tray.showMessage(QStringLiteral("NeonBoard 有新版本"),
                                          QStringLiteral("发现 %1，可在设置里更新").arg(version));
                     });
    QObject::connect(&updateManager, &UpdateManager::installLaunched, &windowController,
                     &WindowController::quit);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &windowController, [&]() {
        windowController.saveGeometry();
        hotkey.uninstall();
    });

#ifdef Q_OS_WIN
    // 跳转列表（任务栏右键）：装完任务项后由系统持久化，与本次启动无关。
    // 延到事件循环里做，避免和首帧渲染抢 COM/注册表；失败只记日志（fail-soft）。
    QTimer::singleShot(0, &app, []() {
        QString jumpListError;
        if (WindowsIntegration::applyJumpList(&jumpListError)) {
            qInfo() << "JUMPLIST applied" << WindowsIntegration::jumpListTasks().size();
        } else {
            qWarning().noquote() << "JUMPLIST skipped:" << jumpListError;
        }
    });
#endif

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
