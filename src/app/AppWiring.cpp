#include "AppWiring.h"

#include "AppActions.h"
#include "SingleInstance.h"
#include "StartupDefer.h"
#include "WindowsIntegration.h"

#include <QDebug>
#include <QQuickWindow>

// 成员引用绑定 main 栈对象；接线内容逐字照搬拆分前的装配函数（MOD-9）。
BackgroundWiring::BackgroundWiring(RoadmapSync &roadmap_, TaskModel &taskStore_,
                                   BoardSync &boardSync_, DataPort &dataPort_,
                                   Database &database_, Metrics &metrics_,
                                   QElapsedTimer &boot_, QCoreApplication &app_,
                                   bool deferredSchema_)
    : roadmap(roadmap_),
      taskStore(taskStore_),
      boardSync(boardSync_),
      dataPort(dataPort_),
      database(database_),
      metrics(metrics_),
      boot(boot_),
      app(app_),
      deferredSchema(deferredSchema_)
{
    // 路线图同步接线：先装好「文件侧轮询 + 看板侧写回」，再做首次导入，
    // 否则启动导入触发的 countsChanged 会先于连接建立，导致看板侧改动不写回。
    roadmap.setPath(RoadmapSync::resolvePath());

    roadmapPoll.setInterval(15000);
    QObject::connect(&roadmapPoll, &QTimer::timeout, &roadmap,
                     [this]() { roadmap.poll(); });
    roadmapPoll.start();

    roadmapExport.setSingleShot(true);
    roadmapExport.setInterval(1500);
    QObject::connect(&taskStore, &TaskModel::countsChanged, &roadmapExport, [this]() {
        roadmapExport.start();
    });
    QObject::connect(&roadmapExport, &QTimer::timeout, &roadmap, [this]() {
        if (roadmap.enabled() && !roadmap.isApplying() && RoadmapSync::autoExportEnabled())
            roadmap.exportAll();
    });

    if (!deferredSchema && roadmap.importAll(false))
        qInfo().noquote() << "Roadmap: synced" << roadmap.path();

    // 领用同步：启动即扫一次，之后每 5 秒轮询各 worktree 台账
    claimPoll.setInterval(5000);
    QObject::connect(&claimPoll, &QTimer::timeout, &boardSync,
                     [this]() { boardSync.refresh(); });
    claimPoll.start();
    boardSync.refresh();

    // 每日首次启动自动备份数据库（先落 WAL 再复制，保证备份完整）
    if (!deferredSchema) {
        const QString m = dataPort.backupIfNeeded();
        if (m.isEmpty())
            qInfo().noquote() << "Backup: daily snapshot ready" << dataPort.backupDir();
        else
            qWarning().noquote() << "Backup failed" << m;
    }

    QObject::connect(&metrics, &Metrics::startupMeasured, [this]() {
        // 同时打墙钟与 CPU：墙钟随机器负载乱飘，CPU 才是改动有没有用的判据
        qInfo() << "BOOT firstframe" << boot.elapsed() << "cpu" << metrics.startupCpuMs();
    });

    nbDeferUntilFirstFrame(&metrics, &app, [this]() {
        if (!deferredSchema) return;
        if (QString e; !database.finalizeSchema(&e))
            qWarning().noquote() << "Database: deferred schema failed" << e;
        else
            qInfo() << "BOOT schema finalized" << boot.elapsed();
        taskStore.load();
        if (roadmap.importAll(false)) qInfo().noquote() << "Roadmap: synced" << roadmap.path();
        if (const QString m = dataPort.backupIfNeeded(); !m.isEmpty())
            qWarning().noquote() << "Backup failed" << m;
    });
}

void wirePluginAnnouncements(Database &database, bool deferredSchema,
                             TaskTree &taskTree, TaskRunStore &taskRuns,
                             AgentProfile &agentProfile, TrayController &tray,
                             PreviewHub &previewHub, PluginLoader &pluginLoader,
                             PluginRuntime &pluginRuntime,
                             UpdateManager &updateManager, QCoreApplication &app)
{
    pluginLoader.scanAsync();
    nbConnectStartupReplay(deferredSchema, &database, &Database::schemaFinalized,
                           &taskTree, &taskRuns, &pluginRuntime, &app);

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
    tray.setInstanceLabel(previewHub.instanceLabel());
    qInfo().noquote() << "Update: version" << updateManager.currentVersion()
                      << "kind" << updateManager.installKind();
}

std::function<void(const QString &, int)> wireShellActions(
    QQuickWindow *window, bool hosted, SingleInstance *singleInstance,
    Hotkey &hotkey, TrayController &tray, WindowController &windowController,
    TaskModel &taskStore, Scheduler &scheduler, UpdateManager &updateManager,
    ReminderScheduler &reminderScheduler, QElapsedTimer &boot,
    QCoreApplication &app,
    const std::function<void(qreal, const QString &, int)> &splashStage)
{
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
        QObject::connect(singleInstance, &SingleInstance::actionRequested, window,
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
    QObject::connect(&taskStore, &TaskModel::countsChanged, &tray,
                     [&taskStore, &tray]() {
        tray.updateStats(taskStore.todoCount(), taskStore.doingCount(),
                         taskStore.doneTodayCount());
    });
    tray.updateStats(taskStore.todoCount(), taskStore.doingCount(),
                     taskStore.doneTodayCount());

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

    return dispatch;
}
