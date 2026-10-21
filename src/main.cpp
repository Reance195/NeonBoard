#include "AppPaths.h"
#include "Database.h"
#include "Hotkey.h"
#include "Metrics.h"
#include "PluginHost.h"
#include "PluginLoader.h"
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
    // 诊断模式（自检 / 冒烟）不受单实例限制，方便应用已在运行时做性能采样
    const bool utilityMode = arguments.contains(QStringLiteral("--selftest")) || smokeMode;
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

    TaskFilterModel todoModel;
    TaskFilterModel doingModel;
    TaskFilterModel doneModel;
    todoModel.setSourceModel(&taskStore);
    doingModel.setSourceModel(&taskStore);
    doneModel.setSourceModel(&taskStore);
    todoModel.setStatus(QStringLiteral("todo"));
    doingModel.setStatus(QStringLiteral("doing"));
    doneModel.setStatus(QStringLiteral("done"));

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

    Metrics metrics;
    QObject::connect(&metrics, &Metrics::startupMeasured, [&boot]() {
        qInfo() << "BOOT firstframe" << boot.elapsed();
    });
    UiSettings uiSettings;
    WindowController windowController;
    Hotkey hotkey;
    TrayController tray;
    PluginLoader pluginLoader;
    PluginHost pluginHost(&pluginLoader, &database, &taskStore, &tray);

    if (singleInstance) {
        QObject::connect(singleInstance.get(), &SingleInstance::showRequested,
                         &windowController, &WindowController::showAndActivate);
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("metrics"), &metrics);
    engine.rootContext()->setContextProperty(QStringLiteral("windowController"), &windowController);
    engine.rootContext()->setContextProperty(QStringLiteral("hotkey"), &hotkey);
    engine.rootContext()->setContextProperty(QStringLiteral("tray"), &tray);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginLoader"), &pluginLoader);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginHost"), &pluginHost);
    engine.rootContext()->setContextProperty(QStringLiteral("database"), &database);
    engine.rootContext()->setContextProperty(QStringLiteral("taskStore"), &taskStore);
    engine.rootContext()->setContextProperty(QStringLiteral("todoModel"), &todoModel);
    engine.rootContext()->setContextProperty(QStringLiteral("doingModel"), &doingModel);
    engine.rootContext()->setContextProperty(QStringLiteral("doneModel"), &doneModel);
    engine.rootContext()->setContextProperty(QStringLiteral("uiSettings"), &uiSettings);
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
