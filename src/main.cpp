#include "AppPaths.h"
#include "Database.h"
#include "HookEventQueue.h"
#include "Hotkey.h"
#include "Metrics.h"
#include "PluginEventBus.h"
#include "PluginHost.h"
#include "PluginLoader.h"
#include "PluginRuntime.h"
#include "RoadmapSync.h"
#include "SingleInstance.h"
#include "SplashWindow.h"
#include "TaskFilterModel.h"
#include "TaskModel.h"
#include "TrayController.h"
#include "UiSettings.h"
#include "WindowController.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
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
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
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
    // 诊断模式（自检 / 冒烟 / 路线图同步 / 钩子投递）不受单实例限制，
    // 方便应用已在运行时做性能采样或由 Git 钩子投递事件
    const bool utilityMode = arguments.contains(QStringLiteral("--selftest")) || smokeMode || syncMode
        || arguments.contains(QStringLiteral("--emit-hook"))
        || arguments.contains(QStringLiteral("--emit-hook-file"));
    std::unique_ptr<SingleInstance> singleInstance;
    if (!utilityMode) {
        singleInstance = std::make_unique<SingleInstance>(AppPaths::singleInstanceKey());
        if (!singleInstance->isPrimary())
            return 0;
    } else if (smokeMode) {
        qInfo() << "BOOT smoke bypass single instance";
    }

    const bool silentStart = arguments.contains(QStringLiteral("--autostart"));
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
    qInfo() << "BOOT db" << boot.elapsed();
    splashStage(0.36, QStringLiteral("OPENING LOCAL STORE"), 70);

    TaskModel taskStore(&database);
    taskStore.load();

    // 路线图快照同步：仓库 progress.json ↔ 本地看板
    RoadmapSync roadmap(&database, &taskStore);

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

    TaskFilterModel todoModel;
    TaskFilterModel doingModel;
    TaskFilterModel doneModel;
    todoModel.setSourceModel(&taskStore);
    doingModel.setSourceModel(&taskStore);
    doneModel.setSourceModel(&taskStore);
    todoModel.setStatus(QStringLiteral("todo"));
    doingModel.setStatus(QStringLiteral("doing"));
    doneModel.setStatus(QStringLiteral("done"));

    // 钩子事件投递：--emit-hook <text> / --emit-hook-file <path>
    // 只把提交信息写进事件队列，不在这里执行规则 —— 规则由主进程里的插件执行。
    // 走 utilityMode：应用正在运行时也能投递。
    const int emitHookIndex = arguments.indexOf(QStringLiteral("--emit-hook"));
    const int emitHookFileIndex = arguments.indexOf(QStringLiteral("--emit-hook-file"));
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
            && emitQueue.enqueue(QStringLiteral("hook.commit"),
                                  {{QStringLiteral("text"), payloadText},
                                   {QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch()}},
                                  &emitError);
        qInfo().noquote() << "EMIT-HOOK" << (emitted ? "OK" : "FAIL") << emitError;
        return emitted ? 0 : 2;
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
        const QVariantMap removed = taskStore.removeTask(id);
        ok = !removed.isEmpty() && ok;
        const QString restoredId = taskStore.restoreTask(removed);
        qInfo() << "SELFTEST restore" << restoredId << taskStore.taskById(id).value("status").toString();
        ok = restoredId == id
             && taskStore.taskById(id).value("status").toString() == QStringLiteral("doing") && ok;
        ok = taskStore.deleteTask(id) && ok;
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

    Metrics metrics;
    QObject::connect(&metrics, &Metrics::startupMeasured, [&boot]() {
        qInfo() << "BOOT firstframe" << boot.elapsed();
    });
    UiSettings uiSettings;
    WindowController windowController;
    Hotkey hotkey;
    TrayController tray;
    PluginLoader pluginLoader(&database);
    PluginEventBus pluginEventBus;
    PluginRuntime pluginRuntime(&pluginLoader, &pluginEventBus, &database, &taskStore, &tray);
    PluginHost pluginHost(&pluginRuntime);

    // 钩子事件队列：与看板库物理隔离，主进程轮询消费后转成插件事件
    HookEventQueue hookQueue(AppPaths::hookDbPath());
    QString hookQueueError;
    if (!hookQueue.open(&hookQueueError))
        qWarning().noquote() << "HookEventQueue: open failed" << hookQueueError;
    else
        hookQueue.setEventBus(&pluginEventBus);

    if (singleInstance) {
        QObject::connect(singleInstance.get(), &SingleInstance::showRequested,
                         &windowController, &WindowController::showAndActivate);
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
    engine.rootContext()->setContextProperty(QStringLiteral("database"), &database);
    engine.rootContext()->setContextProperty(QStringLiteral("taskStore"), &taskStore);
    engine.rootContext()->setContextProperty(QStringLiteral("todoModel"), &todoModel);
    engine.rootContext()->setContextProperty(QStringLiteral("doingModel"), &doingModel);
    engine.rootContext()->setContextProperty(QStringLiteral("doneModel"), &doneModel);
    engine.rootContext()->setContextProperty(QStringLiteral("uiSettings"), &uiSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("roadmap"), &roadmap);
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
    hotkey.install(window);
    qInfo() << "BOOT attached" << boot.elapsed();
    splashStage(0.95, QStringLiteral("FINALIZING"), 40);

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
        QMetaObject::invokeMethod(window, "focusNewTask");
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

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &windowController, [&]() {
        windowController.saveGeometry();
        hotkey.uninstall();
    });

    const bool autostart = app.arguments().contains(QStringLiteral("--autostart"));
    if (autostart) {
        QTimer::singleShot(0, window, [window]() { window->hide(); });
        tray.showMessage(QStringLiteral("NeonBoard"),
                         QStringLiteral("正在后台运行，按 Ctrl+Shift+Space 唤起"));
    } else {
        if (splash) {
            splash->finish();
            splash.reset();
        }
        windowController.showAndActivate();
        qInfo() << "BOOT shown" << boot.elapsed();
    }

    if (app.arguments().contains(QStringLiteral("--settings"))) {
        QTimer::singleShot(300, window, [window]() {
            QMetaObject::invokeMethod(window, "openSettings");
        });
    }
    if (app.arguments().contains(QStringLiteral("--newtask"))) {
        QTimer::singleShot(300, window, [window]() {
            QMetaObject::invokeMethod(window, "focusNewTask");
        });
    }
    if (app.arguments().contains(QStringLiteral("--plugins"))) {
        QTimer::singleShot(300, window, [window]() {
            QMetaObject::invokeMethod(window, "openPlugins");
        });
    }
    if (app.arguments().contains(QStringLiteral("--appearance"))) {
        QTimer::singleShot(300, window, [window]() {
            QMetaObject::invokeMethod(window, "openAppearance");
        });
    }

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
