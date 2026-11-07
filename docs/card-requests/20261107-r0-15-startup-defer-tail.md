---
state: proposed
stage: R0
tier: P2
title: 首启空态残留：全新库建表延后期初始化失败无重试，官方插件自动授权可能未落地
author: maintainer-session-2026-11-07
evidence: R0-13 交付残留（pitfalls §7.7）：GUI 两阶段建表（openDeferred → 首帧后 finalizeSchema）在**全新库**空态期，TaskTree::loadLinks、TaskRunStore::cancelStaleQueue/recoverOrphans、Database meta 写入均失败（GUI 实测日志 no such table: task_link / Parameter count mismatch）；PluginRuntime::ensureGrantsBootstrapped() 私有且只在 start() 调用一次，首帧前写 meta 失败后无重试通道——首次运行的官方插件自动授权可能未落地。R14-01 只治「既有库默认未授权」，不含此场景。老库表已存在，无空态，完全不受影响。这些路径均在 R0-13 卡 scope 之外，由本卡承接。
scope: src/plugin/PluginRuntime.cpp;src/plugin/PluginRuntime.h;src/task/TaskTree.cpp;src/task/TaskRunStore.cpp;src/main.cpp;docs/pitfalls.md
acceptance: 全新库 GUI 启动后，官方来源（appdir/official）插件的权限补齐在 schema finalized 后完成落地（grants 落库），无需二次启动；R14-01 的 officialAutoGrants 判据不回归;空态期一次性初始化（links / 陈旧队列清理 / 孤儿 run 恢复 / meta 写入）在 schemaReady 后重放或幂等补齐，启动日志不再出现 no such table / Parameter count mismatch 失败行;老库行为不回归：tst_pluginruntime、tst_startupdefer 全过；新增用例覆盖「空态失败 → 就绪后重放成功」;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）；若 DX-131 未先行合入，允许按 pitfalls §7.7 D 口径以全量构建 + -SkipBuild 组合佐证并如实记录，DX-131 合入后补一次全量;pitfalls §7.7 残留段收敛更新
depends:
suggestedId: R0-15
---

## 目标

把 R0-13 两阶段建表留下的空态残留收掉：全新库首次运行时，「建表完成之前」失败掉的一次性初始化在 schema 就绪后有重放通道，官方插件的自动授权不再依赖「表恰好先建好」的时序运气。

## 验收

见 frontmatter `acceptance`（5 条）。口径说明：

- 以**产物与日志**断言：全新库 GUI 启动日志无 `no such table` / `Parameter count mismatch` 失败行；grants 落库可由 `--plugin-report` 或库查询证明。
- 竞态面必须有用例：空态失败 → schemaFinalized 后重放成功（沿 tst_startupdefer 的两阶段测试模式扩展）。
- 第 4 条的全量口径与 DX-131 联动：DX-131 未合入时允许组合佐证，但要如实记录、不虚报「verify 全量通过」。

## 边界

- 不改 Database 两阶段 API 形态与 schema（R0-13 已定）；重放挂在 schemaFinalized 信号之后。
- 不改插件 API 契约与 manifest 格式；officialAutoGrants 的补齐判据（R14-01）不动。
- 一次性初始化的重放必须幂等：老库（无空态）重复重放不产生副作用。

## 输入

基线 origin/main。既有实测：R0-13 的 GUI 启动日志（.worktrees/agent-r0-13 已回收，现场记录在 pitfalls §7.7 与 roadmap §3.1）、tst_startupdefer 既有用例。依赖：无（与 DX-131 并行不阻塞）。

## 产出

① 官方插件授权补齐的重放通道；② 空态一次性初始化的幂等重放；③ 竞态面用例；④ pitfalls §7.7 残留段收敛。立卡与实现分开两条分支、两段提交（宪章条款 6）。
