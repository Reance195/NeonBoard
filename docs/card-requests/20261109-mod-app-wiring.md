---
state: proposed
card: MOD-9
stage: MOD
tier: P4
title: MOD M1c 装配器抽取：AppMain 三段接线区收敛进 AppWiring
author: codebuddy-session-2026-11-09
evidence: 2026-11-09 实物复测（基线 origin/main）：MOD-8 结卡后 src/app/AppMain.cpp 仍有 1250 行，其中三段「连线与启动装配」直接嵌在装配函数里——后台轮询与 first-frame 接线（roadmapPoll/roadmapExport/claimPoll 三个 QTimer + 自动备份 + Metrics first-frame + nbDeferUntilFirstFrame，468-517 共 50 行）、插件宣告接线（scanAsync / 启动重放 / declaredAgents 刷新 / 实例标签 / 版本日志，540-556 共 17 行）、GUI 动作接线（dispatch lambda 与 singleInstance/hotkey/tray/windowController/taskStore/scheduler/updateManager 全部 connect 与跳转列表，1135-1219 共 85 行）。方案 docs/module-split-plan.md v4 §5 M1c 即「连线与启动装配器」，MOD-8 交付说明把它列为 nextStep ④。可行性已核实：三个 QTimer 搬进结构体成员即可保住「活到 main 退出」的生命周期；Qt 连接上的 lambda 捕获引用参数（别名指向 main 栈对象）在函数返回后依然有效；dispatch lambda 需以 std::function 返回值交还调用方（1232 行 silentStart 分支还要用）。维护者 2026-11-09 会话指令「建议后续处理 M1c 装配器抽取」（承接 MOD-8 交付说明 nextStep ④），授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: src/app/**;CMakeLists.txt
acceptance: pwsh -File scripts/verify.ps1 全量通过（编译 → ctest → --selftest → --smoke → 模块守卫 → 看板一致性）;--selftest 退出码 0 且沙箱 neonboard.log 的 SELFTEST 标签序列与基线一致（末行 result OK remaining 0，无 FAIL）;src/app/AppWiring.h 定义 BackgroundWiring 构造单元与 wirePluginAnnouncements 与 wireShellActions 装配入口，AppMain.cpp 三段接线区改由装配器接管，三个 QTimer 随结构成员活到 main 退出，dispatch 以 std::function 返回值交还调用方，接线次序逐字保持;AppMain.cpp 行数 <= 1105（基线 1250）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0
depends: 
createdAt: 2026-11-09T03:00:00+00:00
---

## 目标

把 `src/app/AppMain.cpp` 里三段连线与启动装配区收敛进新建的 `src/app/AppWiring.h/.cpp`（M1c 装配器）：`BackgroundWiring` 结构体承载三个常驻 QTimer 与后台轮询 / 防抖 / first-frame 接线，`wirePluginAnnouncements` 承载插件宣告接线，`wireShellActions` 承载 GUI 动作接线并以 `std::function` 返回 dispatch。装配函数退化为三处调用。

## 验收

见 frontmatter `acceptance`（5 条）。核心是第 1、2、3 条：全量 verify 绿、`--selftest` 行为与基线一致、三段接线按原次序接管。第 4 条行数棘轮防回弹。

## 边界

见 frontmatter `scope`。**不动**：`splashStage` / `splashPump`（main 局部 lambda，被装配序列多处穿插调用，留 AppMain）、hotkey.install 与 reminderScheduler.start（1128-1131，强依赖 window 就绪时序且夹在 splashStage 之间）、silentStart / splash 显示序列 / startupCheck / smoke 计时 / exec（1221-1249，依赖 splash 与 dispatch 的启动收尾）、CLI 派发块、QML 注入区。不碰 `src/` 八个模块目录、不碰台账契约。

**生命周期论证**（已核实）：QTimer 三个对象是常驻轮询源，搬进 `BackgroundWiring` 成员后结构体在 main 栈上存活至函数退出，与原局部声明等价；`QObject::connect` 的 lambda 捕获的引用参数是 main 栈对象的别名，函数返回后仍然有效；`QTimer::singleShot` 的 lambda 无捕获或按值捕获指针，同样安全。`dispatch` 被 1159 与 1232 两处使用，装配函数以 `std::function<void(const QString &, int)>` 返回。

## 输入

基线 `origin/main`。前置 MOD-7 / MOD-8 已结卡（`src/app/` 已有 AppMain / AppObjects / CliCommands / AppContext / SelfTestRunner 五类文件）。`--selftest` 基线判据沿用 MOD-7 结论：exe 是 GUI 子系统、stdout 恒空，比对沙箱 `neonboard.log` 的 SELFTEST 标签序列与 `result OK`。

## 产出

`src/app/AppWiring.h/.cpp`（三段装配）+ `AppMain.cpp` 接线区接管 + `CMakeLists.txt` 尾部追加；合并提交 body 四段；交付说明单独提交，`note` 写「已完成 X / 待做 Y」与残留（splashStage 与启动收尾为何留 AppMain、剩余 CLI 派发块属后续批次）。
