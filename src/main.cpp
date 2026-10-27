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
#include "DataPort.h"
#include "SingleInstance.h"
#include "SplashWindow.h"
#include "WindowsIntegration.h"
#include "BoardColumns.h"
#include "TaskFilterModel.h"
#include "TaskModel.h"
#include "TaskTree.h"
#include "TaskStats.h"
#include "TaskExtra.h"
#include "TaskExec.h"
#include "TaskRunner.h"
#include "TaskRunStore.h"
#include "TriageRunner.h"
#include "LedgerImport.h"
#include "TaskUndo.h"
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
#include <QFontDatabase>
#include <QFontMetrics>
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

static void neonboardLogHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    static QFile file;
    static bool opened = false;
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

int main(int argc, char *argv[])
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
    const bool syncMode = arguments.contains(QStringLiteral("--sync-roadmap"))
        || arguments.contains(QStringLiteral("--export-roadmap"));
    const bool dataMode = arguments.contains(QStringLiteral("--export-tasks"))
        || arguments.contains(QStringLiteral("--import-tasks"))
        || arguments.contains(QStringLiteral("--backup"));
    // 诊断模式（自检 / 冒烟 / 路线图同步 / 钩子投递）不受单实例限制，
    // 方便应用已在运行时做性能采样或由 Git 钩子投递事件
    const bool utilityMode = arguments.contains(QStringLiteral("--selftest")) || smokeMode || syncMode
        || dataMode
        || arguments.contains(QStringLiteral("--emit-hook"))
        || arguments.contains(QStringLiteral("--emit-hook-file"))
        || arguments.contains(QStringLiteral("--install-plugin-file"))
        || arguments.contains(QStringLiteral("--market-refresh"))
        || arguments.contains(QStringLiteral("--market-source"))
        || arguments.contains(QStringLiteral("--market-sources"))
        || arguments.contains(QStringLiteral("--market-install"))
        || arguments.contains(QStringLiteral("--install-plugin-url"))
        || arguments.contains(QStringLiteral("--preview-list"))
        || arguments.contains(QStringLiteral("--preview-build"))
        || arguments.contains(QStringLiteral("--preview-package"))
        || arguments.contains(QStringLiteral("--preview-start"))
        || arguments.contains(QStringLiteral("--preview-stop"))
        || arguments.contains(QStringLiteral("--plugin-report"))
        || arguments.contains(QStringLiteral("--register-file-assoc"))
        || arguments.contains(QStringLiteral("--unregister-file-assoc"))
        || arguments.contains(QStringLiteral("--assoc-status"))
        || arguments.contains(QStringLiteral("--check-update"))
        || arguments.contains(QStringLiteral("--update-apply"))
        || arguments.contains(QStringLiteral("--update-source"))
        || arguments.contains(QStringLiteral("--update-sources"))
        || arguments.contains(QStringLiteral("--run-summary"))
        || arguments.contains(QStringLiteral("--import-ledger"));
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
    if (!utilityMode && !smokeMode && !silentStart) {
        splash = std::make_unique<SplashWindow>();
        splash->setStage(0.06, QStringLiteral("BOOTING NEON RUNTIME"));
        splash->show();
        splash->pump(70);
    }

    const auto splashStage = [&splash](qreal progress, const QString &label, int holdMs) {
        if (!splash)
            return;
        splash->setStage(progress, label);
        splash->pump(holdMs);
    };

    QElapsedTimer fontTimer;
    fontTimer.start();
    qInfo() << "BOOT font families" << QFontDatabase::families().size() << fontTimer.elapsed();
    QFont uiFont(QStringLiteral("Microsoft YaHei UI"));
    uiFont.setPointSize(9);
    QFontMetrics uiMetrics(uiFont);
    qInfo() << "BOOT font metrics"
            << uiMetrics.horizontalAdvance(QStringLiteral("工作台待办进行中已办结"))
            << fontTimer.elapsed();
    QApplication::setFont(uiFont);
    splashStage(0.18, QStringLiteral("LOADING TYPEFACES"), 40);

    Database database;
    QString databaseError;
    if (!database.open(&databaseError)) {
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
    taskStore.load();

    // 通用撤销栈：删除 / 移动 / 完成 / 编辑 四类用户操作统一可撤销
    TaskUndo taskUndo(&taskStore);

    // 主任务 / 子任务关联（边表 task_link，不动 task 表结构）
    TaskTree taskTree(&database, &taskStore);

    // 完成统计（热力图 / 连续天数 / 周吞吐）：只读任务表，不建新表
    TaskStats taskStats(&database, &taskStore);

    // 任务附加属性（提醒 / 重复）：侧表 task_extra，不动 task 表结构
    TaskExtra taskExtra(&database, &taskStore);
    taskUndo.setTaskExtra(&taskExtra);

    // 任务执行定义（目标 / 验收 / 边界 / 执行参数）：侧表 task_exec（M0）
    TaskExec taskExec(&database, &taskStore);

    // agent profile / 总开关 / cwd 白名单：meta 表（R6-03），命令行只拼 argv
    AgentProfile agentProfile(&database);

    // 任务执行运行记录与同卡锁：侧表 task_run（R6-02），启动时回收孤儿 run
    TaskRunStore taskRuns(&database);

    // 任务执行器（R6-04）：拉起 agent 进程 / 流式日志 / 超时·终止 / 生命周期
    TaskRunner taskRunner(&database, &taskRuns, &agentProfile, &taskExec);

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
    boardColumns.setTaskTree(&taskTree);
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
        if (pluginId.isEmpty()) {
            qWarning().noquote() << "INSTALL-PLUGIN invalid package" << packagePath;
            return 2;
        }
        const QString targetDir = AppPaths::pluginDir() + QLatin1Char('/')
            + PluginPackage::dirNameForId(pluginId);
        QString installError;
        if (!PluginPackage::installNeonFile(packagePath, targetDir, &installError)) {
            qWarning().noquote() << "INSTALL-PLUGIN FAIL" << installError;
            return 2;
        }
        qInfo().noquote() << "INSTALL-PLUGIN OK" << pluginId << targetDir;
        return 0;
    }

    if (arguments.contains(QStringLiteral("--selftest"))) {
        const QString id = taskStore.addTask(QStringLiteral("selftest"), QStringLiteral("todo"), 2, QStringLiteral("test"), 0);
        const QVariantMap created = taskStore.taskById(id);
        qInfo() << "SELFTEST add" << id << created.value("title").toString() << "count" << taskStore.count();
        bool ok = !id.isEmpty() && created.value("title").toString() == QStringLiteral("selftest");
        ok = taskStore.updateTask(id, {{QStringLiteral("title"), QStringLiteral("selftest-updated")}, {QStringLiteral("priority"), 3}}) && ok;
        const QVariantMap updated = taskStore.taskById(id);
        qInfo() << "SELFTEST update" << updated.value("title").toString() << updated.value("priority").toInt();
        ok = updated.value("title").toString() == QStringLiteral("selftest-updated") && updated.value("priority").toInt() == 3 && ok;
        ok = taskStore.moveTask(id, QStringLiteral("doing"), -1) && ok;
        qInfo() << "SELFTEST move" << taskStore.taskById(id).value("status").toString();
        ok = taskStore.taskById(id).value("status").toString() == QStringLiteral("doing") && ok;
        // 精确插位：moveTaskBefore 插入到指定卡片之前，且落点顺序可被 taskIdsInStatus 读回
        const QString insA = taskStore.addTask(QStringLiteral("selftest-a"), QStringLiteral("todo"), 1, QString(), 0);
        const QString insB = taskStore.addTask(QStringLiteral("selftest-b"), QStringLiteral("todo"), 1, QString(), 0);
        const QString insC = taskStore.addTask(QStringLiteral("selftest-c"), QStringLiteral("todo"), 1, QString(), 0);
        ok = taskStore.moveTaskBefore(insC, QStringLiteral("todo"), insA) && ok;
        const QStringList todoIds = taskStore.taskIdsInStatus(QStringLiteral("todo"));
        qInfo() << "SELFTEST insert" << todoIds.join(QLatin1Char(','));
        ok = todoIds.indexOf(insC) >= 0 && todoIds.indexOf(insC) < todoIds.indexOf(insA) && ok;
        ok = taskStore.moveTaskBefore(insC, QStringLiteral("todo"), QString()) && ok;
        const QStringList tailIds = taskStore.taskIdsInStatus(QStringLiteral("todo"));
        ok = tailIds.last() == insC && ok;
        // 主/子任务关联：建边 → 父卡统计 → 成环拒绝 → 删子卡清边
        const QString linkParent = taskStore.addTask(QStringLiteral("selftest-parent"), QStringLiteral("todo"), 2, QString(), 0);
        const QString linkChild = taskStore.addTask(QStringLiteral("selftest-child"), QStringLiteral("todo"), 2, QString(), 0);
        ok = taskTree.setParent(linkChild, linkParent) && ok;
        const QVariantMap linkStats = taskTree.childStats(linkParent);
        qInfo() << "SELFTEST link" << linkStats.value("total").toInt() << linkStats.value("done").toInt();
        ok = linkStats.value("total").toInt() == 1 && ok;
        ok = !taskTree.setParent(linkParent, linkChild) && ok;   // 成环必须被拒
        taskStore.deleteTask(linkChild);
        ok = taskTree.parentId(linkChild).isEmpty() && ok;       // 删子卡要清边
        taskStore.deleteTask(linkParent);
        taskStore.deleteTask(insA);
        taskStore.deleteTask(insB);
        taskStore.deleteTask(insC);
        // 快速录入解析：日期/时间/标签/优先级标记被拆出来，标题只留正文
        const QVariantMap p1 = taskStore.previewInput(QStringLiteral("明天 15:00 #工作 !3 评审方案"));
        const QDateTime due1 = QDateTime::fromSecsSinceEpoch(p1.value("dueAt").toLongLong());
        ok = p1.value("title").toString() == QStringLiteral("评审方案") && ok;
        ok = p1.value("priority").toInt() == 3 && ok;
        ok = p1.value("tags").toStringList().contains(QStringLiteral("工作")) && ok;
        ok = due1.date() == QDate::currentDate().addDays(1)
             && due1.time().hour() == 15 && due1.time().minute() == 0 && ok;
        const QVariantMap p2 = taskStore.previewInput(QStringLiteral("周五 18:30 复盘"));
        const QDateTime due2 = QDateTime::fromSecsSinceEpoch(p2.value("dueAt").toLongLong());
        ok = p2.value("title").toString() == QStringLiteral("复盘") && ok;
        ok = due2.time().hour() == 18 && due2.time().minute() == 30 && ok;
        const QVariantMap p3 = taskStore.previewInput(QStringLiteral("普通任务"));
        ok = p3.value("title").toString() == QStringLiteral("普通任务")
             && !p3.value("hasDue").toBool() && p3.value("priority").toInt() < 0 && ok;
        const QVariantMap p4 = taskStore.previewInput(QStringLiteral("下周三 上午9点 汇报"));
        const QDateTime due4 = QDateTime::fromSecsSinceEpoch(p4.value("dueAt").toLongLong());
        ok = p4.value("title").toString() == QStringLiteral("汇报") && ok;
        ok = due4.date().dayOfWeek() == 3 && due4.time().hour() == 9 && ok;
        qInfo() << "SELFTEST parse" << p1.value("title").toString() << p1.value("dueText").toString()
                << p2.value("dueText").toString() << p4.value("dueText").toString();
        // 解析结果真正落库：addTask 走同一套解析
        const QString parsedId = taskStore.addTask(QStringLiteral("解析入库 明天 10:00 #测试 !2"),
                                                   QStringLiteral("todo"), 1, QString(), 0);
        const QVariantMap parsedTask = taskStore.taskById(parsedId);
        ok = parsedTask.value("title").toString() == QStringLiteral("解析入库") && ok;
        ok = parsedTask.value("priority").toInt() == 2 && ok;
        ok = parsedTask.value("tagsFull").toString().contains(QStringLiteral("测试")) && ok;
        ok = parsedTask.value("dueAt").toLongLong() > 0 && ok;
        taskStore.deleteTask(parsedId);
        // 通用撤销：删除 / 移动 / 编辑 三类操作都能回退到操作前的状态
        const QString undoId = taskStore.addTask(QStringLiteral("selftest-undo"), QStringLiteral("todo"), 1,
                                                 QStringLiteral("undo"), 0);
        ok = taskUndo.deleteTask(undoId) && ok;
        ok = taskStore.taskById(undoId).isEmpty() && ok;
        ok = taskUndo.undo() && ok;
        ok = taskStore.taskById(undoId).value("title").toString() == QStringLiteral("selftest-undo") && ok;
        ok = taskUndo.moveTask(undoId, QStringLiteral("doing"), -1) && ok;
        ok = taskStore.taskById(undoId).value("status").toString() == QStringLiteral("doing") && ok;
        ok = taskUndo.undo() && ok;
        ok = taskStore.taskById(undoId).value("status").toString() == QStringLiteral("todo") && ok;
        ok = taskUndo.updateTask(undoId, {{QStringLiteral("title"), QStringLiteral("selftest-undo-edited")},
                                          {QStringLiteral("priority"), 3}}) && ok;
        ok = taskStore.taskById(undoId).value("title").toString() == QStringLiteral("selftest-undo-edited") && ok;
        ok = taskUndo.undo() && ok;
        ok = taskStore.taskById(undoId).value("title").toString() == QStringLiteral("selftest-undo")
             && taskStore.taskById(undoId).value("priority").toInt() == 1 && ok;
        qInfo() << "SELFTEST undo" << taskUndo.canUndo();
        taskStore.deleteTask(undoId);
        const QVariantMap removed = taskStore.removeTask(id);
        ok = !removed.isEmpty() && ok;
        const QString restoredId = taskStore.restoreTask(removed);
        qInfo() << "SELFTEST restore" << restoredId << taskStore.taskById(id).value("status").toString();
        ok = restoredId == id
             && taskStore.taskById(id).value("status").toString() == QStringLiteral("doing") && ok;
        // 任务附加属性：设提醒 → 读回 → 标记已通知 → 清提醒 → 删任务清行
        const QString extraId = taskStore.addTask(QStringLiteral("selftest-extra"), QStringLiteral("todo"), 1, QString(), 0);
        ok = taskExtra.setReminder(extraId, QDateTime::currentSecsSinceEpoch() + 3600) && ok;
        {
            const QVariantMap extra = taskExtra.extraOf(extraId);
            ok = extra.value("remindAt").toLongLong() > 0
                 && extra.value("notifiedAt").toLongLong() == 0 && ok;
        }
        ok = taskExtra.markNotified(extraId) && ok;
        ok = taskExtra.extraOf(extraId).value("notifiedAt").toLongLong() > 0 && ok;
        ok = taskExtra.setReminder(extraId, 0) && ok;
        ok = taskExtra.extraOf(extraId).value("remindAt").toLongLong() == 0 && ok;
        taskStore.deleteTask(extraId);
        ok = taskExtra.extraOf(extraId).value("remindAt").toLongLong() == 0 && ok;  // 删任务要清行
        qInfo() << "SELFTEST extra" << taskExtra.revision();

        // 执行定义侧表：写入 / 往返 / 校验 / 删任务清行（M0）
        {
            const QString execId = taskStore.addTask(QStringLiteral("selftest-exec"));
            ok = !execId.isEmpty() && ok;
            QVariantMap execFields;
            execFields.insert(QStringLiteral("goal"), QStringLiteral("自检目标"));
            execFields.insert(QStringLiteral("acceptance"),
                              QVariantList{QStringLiteral("--selftest")});
            execFields.insert(QStringLiteral("scope"),
                              QVariantList{QStringLiteral("src/**")});
            execFields.insert(QStringLiteral("agent"), QStringLiteral("opencode"));
            execFields.insert(QStringLiteral("cwd"), QDir::tempPath());
            execFields.insert(QStringLiteral("workflow"), QStringLiteral("fix"));
            execFields.insert(QStringLiteral("timeoutMin"), 30);
            execFields.insert(QStringLiteral("maxAttempts"), 2);
            ok = taskExec.setExec(execId, execFields) && ok;
            const QVariantMap read = taskExec.execOf(execId);
            ok = read.value(QStringLiteral("goal")).toString() == QStringLiteral("自检目标") && ok;
            ok = read.value(QStringLiteral("acceptance")).toList().size() == 1 && ok;
            ok = read.value(QStringLiteral("workflow")).toString() == QStringLiteral("fix") && ok;
            // 非法 workflow 必须被拒
            QVariantMap bad;
            bad.insert(QStringLiteral("workflow"), QStringLiteral("落选流程"));
            ok = !taskExec.setExec(execId, bad) && ok;
            // 删任务要清行
            taskStore.deleteTask(execId);
            ok = taskExec.execOf(execId).value(QStringLiteral("goal")).toString().isEmpty() && ok;
            qInfo() << "SELFTEST exec" << taskExec.revision();
        }

        // Triage 只读分析（R6-07）：纯逻辑校验 + 草案存取
        {
            // 带解释性文字的输出里抠出 JSON
            const QJsonDocument doc = TriageRunner::extractJson(
                QStringLiteral("分析如下：\n```json\n{\"understanding\":\"x\"}\n```\n完毕"));
            ok = !doc.isNull()
                 && doc.object().value(QStringLiteral("understanding")).toString()
                        == QStringLiteral("x")
                 && ok;
            ok = TriageRunner::extractJson(QStringLiteral("完全没有 JSON")).isNull() && ok;
            // glob 合法性：相对路径可，绝对路径 / 反斜杠 / .. 不可
            ok = TriageRunner::isValidGlob(QStringLiteral("src/**")) && ok;
            ok = !TriageRunner::isValidGlob(QStringLiteral("C:/abs/path")) && ok;
            ok = !TriageRunner::isValidGlob(QStringLiteral("src\\a.cpp")) && ok;
            ok = !TriageRunner::isValidGlob(QStringLiteral("../evil")) && ok;
            // 草案校验：缺字段报 missing；齐全时 questions 截断、非法 glob 丢弃、
            // acceptance 空补默认、confidence<0.5 不可执行
            const QVariantMap missing = TriageRunner::validateDraft(
                {{QStringLiteral("understanding"), QStringLiteral("u")}});
            ok = !missing.value(QStringLiteral("ok")).toBool()
                 && missing.value(QStringLiteral("missing")).toList().size() >= 5 && ok;
            QVariantList questions;
            for (int i = 0; i < 5; ++i)
                questions.append(QStringLiteral("问题%1").arg(i));
            const QVariantMap full = TriageRunner::validateDraft({
                {QStringLiteral("understanding"), QStringLiteral("u")},
                {QStringLiteral("affected"), QVariantMap{{QStringLiteral("risk"), QStringLiteral("低")}}},
                {QStringLiteral("scope_draft"), QVariantList{QStringLiteral("src/**"),
                                                             QStringLiteral("bad\\path")}},
                {QStringLiteral("acceptance_draft"), QVariantList{}},
                {QStringLiteral("questions"), questions},
                {QStringLiteral("estimate"), QVariantMap{{QStringLiteral("size"), QStringLiteral("S")}}},
                {QStringLiteral("confidence"), 0.3},
            });
            ok = full.value(QStringLiteral("ok")).toBool() && ok;
            const QVariantMap draft = full.value(QStringLiteral("draft")).toMap();
            ok = draft.value(QStringLiteral("questions")).toList().size() == 3 && ok;
            ok = draft.value(QStringLiteral("scope_draft")).toList().size() == 1 && ok;
            ok = draft.value(QStringLiteral("acceptance_draft")).toList().size() == 1 && ok;
            ok = !draft.value(QStringLiteral("executable")).toBool() && ok;
            // prompt 模板：骨架 + fix 专项段
            const QString prompt = TriageRunner::buildPrompt(
                QStringLiteral("修按钮"), QStringLiteral("点了没反应"), QStringLiteral("fix"));
            ok = prompt.contains(QStringLiteral("只读分析器"))
                 && prompt.contains(QStringLiteral("修按钮"))
                 && prompt.contains(QStringLiteral("复现路径")) && ok;
            // 草案存 meta 表：写 → 读回 → 清除（不升 user_version、不加表）
            const QString triageId = taskStore.addTask(QStringLiteral("selftest-triage"));
            QVariantMap record{{QStringLiteral("status"), QStringLiteral("ok")},
                               {QStringLiteral("draft"), draft}};
            ok = database.setMetaValue(QStringLiteral("exec.triage.draft.%1").arg(triageId),
                                       QStringLiteral("{\"status\":\"ok\"}"))
                 && ok;
            ok = triageRunner.draftOf(triageId).value(QStringLiteral("status")).toString()
                        == QStringLiteral("ok")
                 && ok;
            ok = triageRunner.clearDraft(triageId) && triageRunner.draftOf(triageId).isEmpty()
                 && ok;
            Q_UNUSED(record);
            taskStore.deleteTask(triageId);
            qInfo() << "SELFTEST triage" << triageRunner.revision();
        }

        // 运行记录侧表：取锁 / 二次取锁被拒 / 心跳 / 结束 / 释放后可再取（R6-02）
        {
            const QString runTaskId = taskStore.addTask(QStringLiteral("selftest-run"));
            ok = !runTaskId.isEmpty() && ok;
            const QVariantMap runFirst = taskRuns.acquire(runTaskId);
            const QString runId = runFirst.value(QStringLiteral("runId")).toString();
            ok = runFirst.value(QStringLiteral("ok")).toBool() && !runId.isEmpty() && ok;
            const QVariantMap runBusy = taskRuns.acquire(runTaskId);
            ok = !runBusy.value(QStringLiteral("ok")).toBool()
                 && runBusy.value(QStringLiteral("code")).toString() == QStringLiteral("busy")
                 && ok;
            ok = taskRuns.heartbeat(runId) && ok;
            ok = taskRuns.release(runId, QStringLiteral("passed")) && ok;
            const QVariantMap runAgain = taskRuns.acquire(runTaskId);
            ok = runAgain.value(QStringLiteral("ok")).toBool() && ok;
            ok = taskRuns.release(runAgain.value(QStringLiteral("runId")).toString(),
                                  QStringLiteral("cancelled")) && ok;
            taskStore.deleteTask(runTaskId);
            const QSqlDatabase runConn = QSqlDatabase::database(database.connectionName());
            QSqlQuery runSchema(runConn);
            ok = runSchema.exec(QStringLiteral(
                     "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='task_run'"))
                 && runSchema.next() && runSchema.value(0).toInt() == 1 && ok;
            ok = runSchema.exec(QStringLiteral(
                     "SELECT COUNT(*) FROM sqlite_master WHERE type='index' "
                     "AND name='idx_task_run_active' AND sql LIKE '%UNIQUE%'"))
                 && runSchema.next() && runSchema.value(0).toInt() == 1 && ok;
            QSqlQuery runVersion(runConn);
            ok = runVersion.exec(QStringLiteral("PRAGMA user_version")) && runVersion.next()
                 && runVersion.value(0).toInt() == Database::kSchemaVersion && ok;
            qInfo() << "SELFTEST runs" << taskRuns.revision();
        }

        // RRULE 下一期：DAILY / INTERVAL / WEEKLY+BYDAY / 月末钳制 / 非法规则
        ok = TaskExtra::nextOccurrence(QStringLiteral("FREQ=DAILY"),
                                        QDateTime(QDate(2026, 10, 23), QTime(15, 0)))
               .date() == QDate(2026, 10, 24) && ok;
        ok = TaskExtra::nextOccurrence(QStringLiteral("FREQ=DAILY;INTERVAL=7"),
                                        QDateTime(QDate(2026, 10, 23), QTime(9, 0)))
               .date() == QDate(2026, 10, 30) && ok;
        // 2026-10-23 是周五；BYDAY=MO → 下周一 10-26
        ok = TaskExtra::nextOccurrence(QStringLiteral("FREQ=WEEKLY;BYDAY=MO"),
                                        QDateTime(QDate(2026, 10, 23), QTime(9, 0)))
               .date() == QDate(2026, 10, 26) && ok;
        // 月末钳制：2027-01-31 + 1月 = 02-28（2027 非闰年）
        ok = TaskExtra::nextOccurrence(QStringLiteral("FREQ=MONTHLY"),
                                        QDateTime(QDate(2027, 1, 31), QTime(9, 0)))
               .date() == QDate(2027, 2, 28) && ok;
        ok = TaskExtra::nextOccurrence(QStringLiteral("FREQ=FOO"),
                                        QDateTime(QDate(2026, 10, 23), QTime(9, 0))).isNull() && ok;
        qInfo() << "SELFTEST rrule";
        // 完成重复任务派生下一期；撤销完成连带删除派生卡
        const QString recId = taskStore.addTask(QStringLiteral("selftest-recurring"),
                                                 QStringLiteral("todo"), 1, QString(), 0);
        ok = taskExtra.setRrule(recId, QStringLiteral("FREQ=DAILY")) && ok;
        const int beforeSpawn = taskStore.count();
        ok = taskUndo.moveTask(recId, QStringLiteral("done"), -1) && ok;
        ok = taskStore.count() == beforeSpawn + 1 && ok;
        // 找派生卡：同标题的新卡（不消费 takeSpawned 映射，留给撤销路径）
        QString spawnedId;
        for (const QVariant &value : taskStore.allTasks()) {
            const QVariantMap t = value.toMap();
            if (t.value(QStringLiteral("title")).toString() == QStringLiteral("selftest-recurring")
                && t.value(QStringLiteral("id")).toString() != recId)
                spawnedId = t.value(QStringLiteral("id")).toString();
        }
        ok = !spawnedId.isEmpty() && ok;
        if (!spawnedId.isEmpty()) {
            const QVariantMap spawned = taskStore.taskById(spawnedId);
            ok = spawned.value("status").toString() == QStringLiteral("todo")
                 && spawned.value("dueAt").toLongLong() > 0 && ok;
            ok = taskExtra.extraOf(spawnedId).value("rrule").toString() == QStringLiteral("FREQ=DAILY") && ok;
        }
        ok = taskUndo.undo() && ok;
        ok = taskStore.taskById(recId).value("status").toString() == QStringLiteral("todo") && ok;
        ok = spawnedId.isEmpty() || taskStore.taskById(spawnedId).isEmpty() && ok;  // 派生卡已删
        qInfo() << "SELFTEST recurring" << spawnedId;
        taskStore.deleteTask(recId);
        // 快照写回防降级：本程序不认识的字段（根级 / 条目级）必须原样保留。
        // 历史事故：旧版按「已知字段重建」把 v3 的 code 整片抹掉。
        {
            const QString snapPath = QDir(QDir::tempPath())
                                         .filePath(QStringLiteral("neonboard-selftest-snapshot.json"));
            const QString keepId = taskStore.addTask(QStringLiteral("selftest-snapshot"),
                                                     QStringLiteral("todo"), 1, QString(), 0);
            const QJsonObject seedItem{
                {QStringLiteral("id"), keepId},
                {QStringLiteral("title"), QStringLiteral("selftest-snapshot")},
                {QStringLiteral("status"), QStringLiteral("todo")},
                {QStringLiteral("priority"), 1},
                {QStringLiteral("unknownCustomField"), QStringLiteral("keep-me")},
            };
            const QJsonObject seedRoot{
                {QStringLiteral("version"), 3},
                {QStringLiteral("updated"), QStringLiteral("2026-01-01T00:00:00")},
                {QStringLiteral("signature"), QStringLiteral("keep-root-signature")},
                {QStringLiteral("items"), QJsonArray{seedItem}},
            };
            QFile seed(snapPath);
            if (seed.open(QIODevice::WriteOnly)) {
                seed.write(QJsonDocument(seedRoot).toJson(QJsonDocument::Indented));
                seed.close();
            }
            ok = roadmap.exportTo(snapPath) && ok;
            QJsonObject outRoot;
            QFile snapshot(snapPath);
            if (snapshot.open(QIODevice::ReadOnly)) {
                outRoot = QJsonDocument::fromJson(snapshot.readAll()).object();
                snapshot.close();
            }
            QJsonObject outItem;
            const QJsonArray outItems = outRoot.value(QStringLiteral("items")).toArray();
            for (const QJsonValue &value : outItems) {
                if (value.toObject().value(QStringLiteral("id")).toString() == keepId) {
                    outItem = value.toObject();
                    break;
                }
            }
            const QString keptRoot = outRoot.value(QStringLiteral("signature")).toString();
            const QString keptItem = outItem.value(QStringLiteral("unknownCustomField")).toString();
            ok = keptRoot == QStringLiteral("keep-root-signature") && ok;
            ok = keptItem == QStringLiteral("keep-me") && ok;
            qInfo() << "SELFTEST snapshot-preserve" << keptRoot << keptItem;
            QFile::remove(snapPath);
            taskStore.deleteTask(keepId);
        }
        // 事件队列不丢事件：没有订阅者时不得标记已投递，应保留待重投。
        {
            const QString hookPath = QDir(QDir::tempPath())
                                         .filePath(QStringLiteral("neonboard-selftest-hooks.db"));
            QFile::remove(hookPath);
            int pending = -1;
            {
                HookEventQueue probe(hookPath);
                QString hookError;
                ok = probe.open(&hookError) && ok;
                PluginEventBus emptyBus;
                probe.setEventBus(&emptyBus);
                ok = probe.enqueue(QStringLiteral("agent.task"),
                                   {{QStringLiteral("text"), QStringLiteral("selftest")}}, &hookError)
                     && ok;
                probe.start(50);
                probe.stop();
                pending = probe.pendingCount();
            }
            QFile::remove(hookPath);
            QFile::remove(hookPath + QStringLiteral("-wal"));
            QFile::remove(hookPath + QStringLiteral("-shm"));
            qInfo() << "SELFTEST hook-retry pending" << pending;
            ok = pending == 1 && ok;
        }
        ok = taskStore.deleteTask(id) && ok;
        // 数据可靠性：WAL 生效 + quick_check + 旧库迁移 + 损坏库自动备份重建
        {
            QSqlDatabase conn = QSqlDatabase::database(database.connectionName());
            QSqlQuery walQuery(conn);
            ok = walQuery.exec(QStringLiteral("PRAGMA journal_mode")) && walQuery.next()
                 && walQuery.value(0).toString().compare(QStringLiteral("wal"), Qt::CaseInsensitive) == 0
                 && ok;
            QSqlQuery checkQuery(conn);
            ok = checkQuery.exec(QStringLiteral("PRAGMA quick_check")) && checkQuery.next()
                 && checkQuery.value(0).toString().compare(QStringLiteral("ok"), Qt::CaseInsensitive) == 0
                 && ok;
            qInfo() << "SELFTEST integrity" << (ok ? "OK" : "FAIL");
        }
        {
            QTemporaryDir tmp;
            ok = tmp.isValid() && ok;
            if (tmp.isValid()) {
                // 旧库迁移：早期版本（无 task 表 / user_version=0）的库要能一路升到当前
                // schema 版本，已有 meta 数据保留，且看板/列被自动补齐
                const QString legacyPath = tmp.filePath(QStringLiteral("legacy.db"));
                {
                    QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                                    QStringLiteral("selftest_legacy"));
                    legacy.setDatabaseName(legacyPath);
                    ok = legacy.open() && ok;
                    QSqlQuery query(legacy);
                    ok = query.exec(QStringLiteral(
                             "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)")) && ok;
                    ok = query.exec(QStringLiteral(
                             "INSERT INTO meta (key, value) VALUES ('legacy', '1')")) && ok;
                    legacy.close();
                }
                QSqlDatabase::removeDatabase(QStringLiteral("selftest_legacy"));

                Database migrated;
                QString migrateError;
                ok = migrated.open(&migrateError, legacyPath) && ok;
                ok = migrated.metaValue(QStringLiteral("legacy")) == QStringLiteral("1") && ok;
                ok = migrated.setMetaValue(QStringLiteral("after"), QStringLiteral("ok"))
                     && migrated.metaValue(QStringLiteral("after")) == QStringLiteral("ok") && ok;
                // schema v2：默认板与三列必须补齐（老库没有 board 表）
                ok = migrated.defaultBoardId() == QStringLiteral("board-default") && ok;
                ok = migrated.boardColumns(migrated.defaultBoardId()).size() == 3 && ok;
                qInfo() << "SELFTEST migrate" << (ok ? "OK" : "FAIL") << migrateError;
            }
        }
        {
            QTemporaryDir tmp;
            ok = tmp.isValid() && ok;
            if (tmp.isValid()) {
                const QString badPath = tmp.filePath(QStringLiteral("bad.db"));
                QFile bad(badPath);
                ok = bad.open(QIODevice::WriteOnly) && ok;
                bad.write(QByteArray(4096, '\x5A'));
                bad.close();

                Database recovered;
                QString recoverError;
                ok = recovered.open(&recoverError, badPath) && ok;
                ok = recovered.recoveredFromCorruption() && ok;
                ok = QFileInfo::exists(recovered.recoveryBackup()) && ok;
                ok = !recovered.recoveryReason().isEmpty() && ok;
                ok = recovered.setMetaValue(QStringLiteral("probe"), QStringLiteral("1"))
                     && recovered.metaValue(QStringLiteral("probe")) == QStringLiteral("1") && ok;
                qInfo() << "SELFTEST recover" << (ok ? "OK" : "FAIL")
                        << recovered.recoveryBackup() << recoverError;
            }
        }
        qInfo() << "SELFTEST result" << (ok ? "OK" : "FAIL") << "remaining" << taskStore.count();
        return ok ? 0 : 2;
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
            roadmap.exportTo(roadmap.path());
    });

    // 启动即拉取路线图快照：仓库 progress.json ↔ 本地看板保持一致
    if (roadmap.enabled()) {
        if (roadmap.importFrom(roadmap.path(), false))
            qInfo().noquote() << "Roadmap: synced" << roadmap.path() << roadmap.lastReport();
        else
            qWarning().noquote() << "Roadmap: sync failed" << roadmap.path() << roadmap.lastReport();
    }

    // 领用同步：启动即扫一次，之后每 5 秒轮询各 worktree 台账
    QTimer claimPoll;
    claimPoll.setInterval(5000);
    QObject::connect(&claimPoll, &QTimer::timeout, &boardSync, [&boardSync]() { boardSync.refresh(); });
    claimPoll.start();
    boardSync.refresh();

    // 每日首次启动自动备份数据库（先落 WAL 再复制，保证备份完整）
    {
        const QString backupMessage = dataPort.backupIfNeeded();
        if (backupMessage.isEmpty())
            qInfo().noquote() << "Backup: daily snapshot ready" << dataPort.backupDir();
        else
            qWarning().noquote() << "Backup failed" << backupMessage;
    }

    Metrics metrics;
    QObject::connect(&metrics, &Metrics::startupMeasured, [&boot]() {
        qInfo() << "BOOT firstframe" << boot.elapsed();
    });
    UiSettings uiSettings;
    WindowController windowController;
    Hotkey hotkey;
    TrayController tray(hosted);
    // 提醒调度器：30s 轮询，托盘通知 + 窗口辉光；启动后先查一轮补发漏提醒
    ReminderScheduler reminderScheduler(&taskExtra, &tray);
    PluginLoader pluginLoader(&database);
    PluginEventBus pluginEventBus;
    PluginRuntime pluginRuntime(&pluginLoader, &pluginEventBus, &database, &taskStore, &tray);
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

        QJsonObject report;
        report.insert(QStringLiteral("plugins"), plugins);
        report.insert(QStringLiteral("commands"),
                      QJsonArray::fromVariantList(pluginRuntime.pluginCommands()));
        report.insert(QStringLiteral("settingsPages"),
                      QJsonArray::fromVariantList(pluginRuntime.settingsPages()));
        report.insert(QStringLiteral("slots"), slotMap);

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
            // 安装包已启动（/VERYSILENT /CLOSEAPPLICATIONS，安装完成由 [Run] 段拉起宿主）。
            // 本进程到此结束，不等待安装结束 —— 安装器会结束掉占用文件的进程。
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
    engine.rootContext()->setContextProperty(QStringLiteral("metrics"), &metrics);
    engine.rootContext()->setContextProperty(QStringLiteral("windowController"), &windowController);
    engine.rootContext()->setContextProperty(QStringLiteral("hotkey"), &hotkey);
    engine.rootContext()->setContextProperty(QStringLiteral("tray"), &tray);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginLoader"), &pluginLoader);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginHost"), &pluginHost);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginRuntime"), &pluginRuntime);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginMarket"), &pluginMarket);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginLibrary"), &pluginLibrary);
    engine.rootContext()->setContextProperty(QStringLiteral("database"), &database);
    engine.rootContext()->setContextProperty(QStringLiteral("taskStore"), &taskStore);
    engine.rootContext()->setContextProperty(QStringLiteral("taskUndo"), &taskUndo);
    engine.rootContext()->setContextProperty(QStringLiteral("taskTree"), &taskTree);
    engine.rootContext()->setContextProperty(QStringLiteral("taskStats"), &taskStats);
    engine.rootContext()->setContextProperty(QStringLiteral("taskExtra"), &taskExtra);
    engine.rootContext()->setContextProperty(QStringLiteral("taskExec"), &taskExec);
    engine.rootContext()->setContextProperty(QStringLiteral("agentProfile"), &agentProfile);
    engine.rootContext()->setContextProperty(QStringLiteral("taskRuns"), &taskRuns);
    engine.rootContext()->setContextProperty(QStringLiteral("taskRunner"), &taskRunner);
    engine.rootContext()->setContextProperty(QStringLiteral("triageRunner"), &triageRunner);
    engine.rootContext()->setContextProperty(QStringLiteral("ledgerImport"), &ledgerImport);
    engine.rootContext()->setContextProperty(QStringLiteral("boardSync"), &boardSync);
    engine.rootContext()->setContextProperty(QStringLiteral("projectMap"), &projectMap);
    engine.rootContext()->setContextProperty(QStringLiteral("reminderScheduler"), &reminderScheduler);
    engine.rootContext()->setContextProperty(QStringLiteral("boardColumns"), &boardColumns);
    engine.rootContext()->setContextProperty(QStringLiteral("uiSettings"), &uiSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("roadmap"), &roadmap);
engine.rootContext()->setContextProperty(QStringLiteral("dataPort"), &dataPort);
    engine.rootContext()->setContextProperty(QStringLiteral("previewHub"), &previewHub);
    engine.rootContext()->setContextProperty(QStringLiteral("updateManager"), &updateManager);
    // 启动即可按里程碑过滤看板：--stage R0
    const int stageIndex = arguments.indexOf(QStringLiteral("--stage"));
    QString initialStage;
    if (stageIndex >= 0 && stageIndex + 1 < arguments.size())
        initialStage = arguments.at(stageIndex + 1).trimmed().toUpper();
    engine.rootContext()->setContextProperty(QStringLiteral("initialStage"), initialStage);
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
