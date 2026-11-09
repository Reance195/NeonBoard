#pragma once

// M1d 对象构造工厂（MOD-8）：装配函数里的逐行对象声明收敛为三个构造单元。
// 成员声明顺序 = 构造顺序，与拆分前的装配函数逐字对应；构造参数照搬原表达式。
//
// 仍留在 AppMain.cpp 的（有意为之，见各处注释）：
//   - Database 声明与 open：依赖 utilityMode 与 splash 失败对话框；
//   - TaskModel taskStore 与紧随的条件 load：load 必须保持在 taskAudit 信号
//     挂接之前（原次序如此），收进工厂会让 load 的信号被 audit 收到；
//   - Metrics 及其 first-frame 接线：lambda 捕获 main 局部的 boot；
//   - boardColumns / dragFeel / keyMove / projectStore 四个散点声明。
//
// 两处 setter（taskUndo.setTaskExtra / workflowRunner.setPresets）在
// AppObjectsEarly 构造体末尾执行：CardDetail / TaskRunner 构造期间不读它们
// （CardDetail.cpp:54-60 只存指针），首次消费发生在运行期，行为不变。

#include "AcceptanceRunner.h"
#include "AgentProfile.h"
#include "BoardSync.h"
#include "CardDetail.h"
#include "DataPort.h"
#include "Hotkey.h"
#include "LedgerImport.h"
#include "PluginEventBus.h"
#include "PluginHost.h"
#include "PluginLibrary.h"
#include "PluginLoader.h"
#include "PluginMarket.h"
#include "PluginRuntime.h"
#include "PreviewHub.h"
#include "ProjectMap.h"
#include "ReminderScheduler.h"
#include "RoadmapSync.h"
#include "Scheduler.h"
#include "ScopeMatcher.h"
#include "TaskAudit.h"
#include "TaskExec.h"
#include "TaskExtra.h"
#include "TaskRunStore.h"
#include "TaskRunner.h"
#include "TaskStats.h"
#include "TaskTree.h"
#include "TaskUndo.h"
#include "ThemeMarket.h"
#include "ThemePack.h"
#include "TriageRunner.h"
#include "TrayController.h"
#include "UiSettings.h"
#include "UpdateManager.h"
#include "WindowController.h"
#include "WorkflowPresets.h"
#include "WorkflowRunner.h"

// 核心对象构造单元：声明顺序与构造次序见各成员注释（照搬自装配函数）。
struct AppObjectsEarly {
    explicit AppObjectsEarly(Database &database, TaskModel &taskStore);

    // 通用撤销栈：删除 / 移动 / 完成 / 编辑 四类用户操作统一可撤销
    TaskUndo taskUndo;
    // 主任务 / 子任务关联（边表 task_link，不动 task 表结构）
    TaskTree taskTree;
    // 完成统计（热力图 / 连续天数 / 周吞吐）：只读任务表，不建新表
    TaskStats taskStats;
    // 任务附加属性（提醒 / 重复）：侧表 task_extra，不动 task 表结构
    TaskExtra taskExtra;
    // 卡片详情抽屉（R8-03）：补丁的脏检查与保存收口，避免「没改也写一次库」。
    // 依赖顺序：要拿 taskExtra 写提醒 / 重复（侧表），所以只能排在这里而不是 TaskUndo 之后
    CardDetail cardDetail;
    // 卡片事件审计（R8-04）：侧表 task_event，不动 task 表结构、不升 user_version。
    // 挂在 taskStore 的生命周期信号上，所以必须早于任何写入（包括自检与导入）。
    TaskAudit taskAudit;
    // 任务执行定义（目标 / 验收 / 边界 / 执行参数）：侧表 task_exec（M0）
    TaskExec taskExec;
    // agent profile / 总开关 / cwd 白名单：meta 表（R6-03），命令行只拼 argv
    AgentProfile agentProfile;
    // 任务执行运行记录与同卡锁：侧表 task_run（R6-02），启动时回收孤儿 run
    TaskRunStore taskRuns;
    // 任务执行器（R6-04）：拉起 agent 进程 / 流式日志 / 超时·终止 / 生命周期
    TaskRunner taskRunner;
    // 执行队列调度器（R7-01）：FIFO + 并发≤2 + 背压 + 暂停/调序/跳过。
    // 只负责「取队头交给 TaskRunner」，进程与门锁仍归 TaskRunner / TaskRunStore。
    Scheduler scheduler;
    // 验收器与越界检查（R6-05）：角色链每步独立验收要用（R7-03 起真正接上）
    AcceptanceRunner acceptanceRunner;
    ScopeMatcher scopeMatcher;
    // 预设 workflow 与 orchestrator 选型（R7-04）：预设存 meta（可编辑），
    // 按 triage estimate.size / 卡片规模选 workflow、按规模裁剪 pm/architect（qa 永不裁）。
    WorkflowPresets workflowPresets;
    // 角色链 workflow 引擎（R7-03）：按步骤数组逐步拉起 agent，每步一份 handoff。
    // 每一步仍走 TaskRunner（进程 / 超时 / 日志 / 心跳复用 R6-04）。
    // R7-04 起接上 WorkflowPresets：卡片没选 workflow 时由 orchestrator 选型。
    WorkflowRunner workflowRunner;
    // Triage 只读分析（R6-07）：草案只写 meta 表，不进台账；只读靠
    // profile readonlyArgs + 分析前后 git status 一致两道硬门
    TriageRunner triageRunner;
    // 台账卡单向导入（R6-09，ADR-0001）：台账 → board.db 只补空值，
    // 冲突/未建卡只报告；反向写台账没有代码路径
    LedgerImport ledgerImport;
    // 路线图快照同步：仓库 progress.json ↔ 本地看板
    RoadmapSync roadmap;
    // 多工作区领用同步：扫描锚点与各 worktree 台账，把「谁在领哪张卡」反映到看板
    BoardSync boardSync;
    // 项目全景图数据源：解析 board-ledger.json + git done 标记，供侧边浮层展示
    ProjectMap projectMap;
    // 数据导入导出与自动备份
    DataPort dataPort;
};

// 界面外壳构造单元：uiSettings 到 tray，段内无穿插逻辑。
struct AppObjectsUi {
    explicit AppObjectsUi(bool hosted);

    UiSettings uiSettings;
    // 外部主题包（R8-01）：启动即扫描 themes/ 目录。
    // 坏包只跳过并记一行日志，绝不拦住启动 —— 主题是锦上添花，不是启动前置条件。
    ThemePack themePacks;
    // 主题包市场（R8-02）：目录内置在客户端，断网也能浏览；安装走原子替换。
    // 依赖 themePacks 才能判断「这个包装了没」，所以必须排在它后面。
    ThemeMarket themeMarket;
    WindowController windowController;
    Hotkey hotkey;
    TrayController tray;
};

// 插件与更新构造单元：reminderScheduler 到 updateManager。
// 原穿插在本段的接线（scanAsync / 启动重放 / declaredAgents 刷新 /
// 实例标签 / 版本日志）集中到 AppMain.cpp 的构造之后执行。
struct AppObjectsPlugins {
    AppObjectsPlugins(Database &database, TaskModel &taskStore,
                      TaskExtra &taskExtra, TrayController &tray);

    ReminderScheduler reminderScheduler;
    PluginLoader pluginLoader;
    PluginEventBus pluginEventBus;
    PluginRuntime pluginRuntime;
    PluginHost pluginHost;
    PluginMarket pluginMarket;
    PluginLibrary pluginLibrary;
    // 预览中心：发现各 agent 工作区，一键构建并启动隔离的预览实例
    PreviewHub previewHub;
    // 软件更新：清单拉取 → 版本比较 → 下载校验 → 安装 / 便携版自替换
    UpdateManager updateManager;
};
