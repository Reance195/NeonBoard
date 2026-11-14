---
state: closed
card: MOD-8
stage: MOD
tier: P4
title: MOD M1d 对象构造工厂：AppMain.cpp 构造区收敛为三个构造单元
author: codebuddy-session-2026-11-09
evidence: 2026-11-09 实物复测（基线 origin/main）：MOD-7 已把 main.cpp 降到 5 行薄入口，但 src/app/AppMain.cpp 仍有 1302 行，其中两段逐行对象声明区（253-327 共 22 个核心对象、566-606 共 15 个界面/插件对象）直接嵌在装配函数里，中间穿插 open 逻辑、deferredSchema 分支、menu-preview return 点与 5 处接线调用，构造单元无法独立审视与测试。方案 docs/module-split-plan.md v4 §5 M1d 明确此项为「对象构造工厂」，MOD-7 交付说明把它列为 nextStep 第一项。穿插安全性已核实：CardDetail 构造只存指针不读 taskUndo.taskExtra()（CardDetail.cpp:54-60），TaskUndo::setTaskExtra 与 WorkflowRunner::setPresets 均为运行时才消费的指针注入，移入工厂构造体末尾不改变任何可观察行为。维护者 2026-11-09 会话指令「继续做 M1d 构造工厂拆分」（承接 MOD-7 交付说明 nextStep ①），授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: src/app/**;CMakeLists.txt
acceptance: pwsh -File scripts/verify.ps1 全量通过（编译 → ctest → --selftest → --smoke → 模块守卫 → 看板一致性）;--selftest 退出码 0 且沙箱 neonboard.log 的 SELFTEST 标签序列与基线一致（末行 result OK remaining 0，无 FAIL）;src/app/AppObjects.h 定义 AppObjectsEarly 与 AppObjectsUi 与 AppObjectsPlugins 三个构造单元（成员声明顺序与原逐行声明逐字对应），AppMain.cpp 中原两段逐行声明区改由三处工厂构造接管，原穿插的 setter 与接线调用集中到工厂构造之后且次序保持;AppMain.cpp 行数 <= 1250（基线 1302）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0
depends: 
createdAt: 2026-11-09T02:30:00+00:00
---

## 目标

把 `src/app/AppMain.cpp` 里两段逐行对象声明区收敛为 `src/app/AppObjects.h/.cpp` 中的三个构造单元（M1d 对象构造工厂）：`AppObjectsEarly`（taskStore 起的 22 个核心对象）、`AppObjectsUi`（uiSettings 到 tray 的 6 个对象）、`AppObjectsPlugins`（reminderScheduler 到 updateManager 的 9 个对象）。成员声明顺序与原代码逐字对应，构造次序 100% 不变，装配函数退化为「三处工厂构造 + 引用别名 + 接线块」。

## 验收

见 frontmatter `acceptance`（5 条）。核心是第 1、2、3 条：全量 verify 绿、`--selftest` 行为与基线一致、构造单元按原序接管。第 4 条行数棘轮防止回弹。

## 边界

见 frontmatter `scope`，两条路径：

- `src/app/**`：新增 `AppObjects.h` / `AppObjects.cpp`，改写 `AppMain.cpp` 两段声明区；
- `CMakeLists.txt`：按 §7.4 约定只在 `qt_add_executable` 源文件列表尾部追加 `src/app/AppObjects.cpp`。

**不动**：`Database database` 的 open 逻辑（依赖 utilityMode 与 splash，留在 AppMain.cpp）、`--selftest` 调用点、`menu-preview` return 点（保持它在 tray 构造后 / reminderScheduler 构造前的原语义）、CLI 派发块、QML 注入区。不碰 `src/` 八个模块目录、不碰台账契约。

**两处 setter 移位的安全论证**（已核实源码）：`CardDetail::CardDetail` 只存指针（CardDetail.cpp:54-60），不读 `taskUndo.taskExtra()`；`TaskUndo::setTaskExtra` 与 `WorkflowRunner::setPresets` 是运行时才消费的指针注入。两者移入工厂构造体末尾，首次消费仍发生在运行期，无可观察行为变化。`AppObjectsPlugins` 段原穿插的 `scanAsync` / `nbConnectStartupReplay` / `refreshDeclaredAgents` / `setInstanceLabel` / 版本日志移到构造之后集中执行：各被调对象构造期间不读这些接线结果（信号连接只要先于 first-frame 信号发出即等效；`scanAsync` 是异步启动，晚于 9 个无依赖对象构造无语义影响），论证记入交付说明。

## 输入

基线 `origin/main`。前置 MOD-7 已结卡（`src/app/` 四类文件在位，`AppMain.cpp` 1302 行）。`--selftest` 基线判据沿用 MOD-7 结论：exe 是 GUI 子系统、stdout 恒空，比对沙箱 `neonboard.log` 的 SELFTEST 标签序列与 `result OK`。

## 产出

`src/app/AppObjects.h/.cpp`（三个构造单元）+ `AppMain.cpp` 声明区接管 + `CMakeLists.txt` 尾部追加；合并提交 body 四段；交付说明单独提交，`note` 写「已完成 X / 待做 Y」与残留（UI 段穿插接线为何集中而非删除、QML 注入区与 CLI 派发块仍留 AppMain 属后续批次）。
