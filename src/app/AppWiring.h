#pragma once

// M1c 装配器（MOD-9）：AppMain.cpp 的三段接线区收敛于此。
//
// 生命周期论证：
//   - BackgroundWiring 的三个 QTimer 是常驻轮询源，作成员随结构体存活到
//     main 退出；结构体声明在所引用对象之后、同栈先析构，引用不悬空。
//     接线 lambda 捕 this，经成员引用访问 main 栈对象。
//   - wirePluginAnnouncements / wireShellActions 的 lambda 捕获引用参数，
//     引用参数是 main 栈对象的别名，函数返回后依然有效。
//   - dispatch 被 singleInstance 连接与启动收尾（silentStart 分支）两处使用，
//     wireShellActions 以 std::function 返回交还调用方。
//
// 仍留在 AppMain.cpp：splashStage / splashPump（main 局部 lambda，被装配
// 序列多处穿插调用）、hotkey.install 与 reminderScheduler.start（夹在
// splashStage 之间的窗口就绪时序）、silentStart / splash 显示序列 /
// startupCheck / smoke 计时 / exec。

#include "AgentProfile.h"
#include "BoardSync.h"
#include "Database.h"
#include "DataPort.h"
#include "Hotkey.h"
#include "Metrics.h"
#include "PluginLoader.h"
#include "PluginRuntime.h"
#include "PreviewHub.h"
#include "ReminderScheduler.h"
#include "RoadmapSync.h"
#include "Scheduler.h"
#include "TaskModel.h"
#include "TaskRunStore.h"
#include "TaskTree.h"
#include "TrayController.h"
#include "UpdateManager.h"
#include "WindowController.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

class QQuickWindow;
class SingleInstance;

// 后台接线单元：roadmap 文件侧轮询 + 看板侧写回防抖、领用同步轮询、
// 每日备份、Metrics 首帧测速与 deferred schema 收尾。
struct BackgroundWiring {
    BackgroundWiring(RoadmapSync &roadmap_, TaskModel &taskStore_,
                     BoardSync &boardSync_, DataPort &dataPort_,
                     Database &database_, Metrics &metrics_, QElapsedTimer &boot_,
                     QCoreApplication &app_, bool deferredSchema_);

    RoadmapSync &roadmap;
    TaskModel &taskStore;
    BoardSync &boardSync;
    DataPort &dataPort;
    Database &database;
    Metrics &metrics;
    QElapsedTimer &boot;
    QCoreApplication &app;
    const bool deferredSchema;
    QTimer roadmapPoll;
    QTimer roadmapExport;
    QTimer claimPoll;
};

// 插件宣告接线：异步扫描启动、schema 就绪重放、declaredAgents 注入、
// 预览实例标签与版本日志。
void wirePluginAnnouncements(Database &database, bool deferredSchema,
                             TaskTree &taskTree, TaskRunStore &taskRuns,
                             AgentProfile &agentProfile, TrayController &tray,
                             PreviewHub &previewHub, PluginLoader &pluginLoader,
                             PluginRuntime &pluginRuntime,
                             UpdateManager &updateManager, QCoreApplication &app);

// 窗口就绪后的启动时序（热键安装 / 提醒调度启动）与 GUI 动作接线：
// 冷启动 / 二次启动共用的 dispatch、单实例与托盘 / 热键 / 统计 / 夜间批量 /
// 更新提示 / 退出保存 / 跳转列表，返回 dispatch。splashStage 由调用方传入
// （它是 main 局部 lambda，捕获 splash 与 pump 预算）。
std::function<void(const QString &, int)> wireShellActions(
    QQuickWindow *window, bool hosted, SingleInstance *singleInstance,
    Hotkey &hotkey, TrayController &tray, WindowController &windowController,
    TaskModel &taskStore, Scheduler &scheduler, UpdateManager &updateManager,
    ReminderScheduler &reminderScheduler, QElapsedTimer &boot,
    QCoreApplication &app,
    const std::function<void(qreal, const QString &, int)> &splashStage);
