---
state: approved
card: DX-172
stage: DX
tier: P4
title: 修复定时任务插件每次启动误弹权限窗（后台加载早于 schema 就绪）
author: maintainer-session-2026-11-16
evidence: 维护者 2026-11-16 会话以截图指出「定时任务」权限窗每次启动都弹，随后明确要求根治。只读核查：本机 %APPDATA%\com.lin\NeonBoard\board.db 的 plugins.grants 已含 com.lin.scheduler 四项权限、plugins.grants.revoked 为空，故不是授权漏存；neonboard.log 14:35:06.278 后台插件启动 → 06.808 PluginContext: permission not granted "storage" → 07.048 BOOT schema finalized，证明权限检查发生在两阶段建表完成之前。src/plugin/PluginRuntime.cpp:835 的 ensureGrantsBootstrapped() 被 schemaReady() 守卫跳过，start() 却在 :215 立即 loadBackgroundPlugins()；后台插件经 PluginContext::require（src/plugin/PluginContext.cpp:59-69）误判未授权并发 permissionRequested，弹窗按空授权表列出四项。schema 就绪后 :904 replayGrantsBootstrap() 才载入授权，但已打开的对话框不会刷新，于是每次启动复现。R0-15 只修了授权落库，未修这条 UI 竞态。
scope: src/plugin/PluginRuntime.cpp;src/plugin/PluginRuntime.h
acceptance: src/plugin/PluginRuntime.cpp 的 loadBackgroundPlugins() 在 m_database 存在且 schemaReady()==false 时不实例化后台插件，改为一次性挂接 schemaFinalized；回调内先 replayGrantsBootstrap() 再 loadBackgroundPlugins()，且重复 reload 不重复挂接;pwsh -File scripts/verify.ps1 全量档退出码 0，覆盖编译、ctest、--selftest、--smoke、模块守卫、QML 致命告警与看板一致性;沙箱 GUI 启动日志中 BOOT schema finalized 早于首条 PluginRuntime: background plugin started: 与任何 PluginContext: permission not granted，com.lin.scheduler 的 storage 未授权行消失，且不再出现新的权限弹窗;python scripts/check_scope.py --base origin/main 与 python scripts/check_path_refs.py --base origin/main 退出码 0，改动不越出两条 scope;只读核对 plugins.grants / plugins.grants.revoked 的读写与 PluginLoader::officialAutoGrants 语义零改动，README.md 关于第三方插件「稍后再说 / 拒绝」的授权语义不变
depends:
createdAt: 2026-11-16T00:00:00+00:00
---

## 目标

让后台插件的加载时点晚于数据库 schema 就绪，消除「授权已在库里、启动瞬间却被读成未授权」的竞态。
本卡只调整 `PluginRuntime::loadBackgroundPlugins()` 的时序，不新增授权机制、不碰弹窗 UI。

## 验收

见 frontmatter `acceptance`（5 条）。判据落在「延迟加载的一次性接线」「全量 verify 绿灯」
「沙箱启动日志的时序」「scope 零越界」「既有授权语义零改动」上。scope 含 `.cpp`/`.h`，
按宪章 §7.7 走代码类全量档。

## 边界

- 只改 `src/plugin/PluginRuntime.cpp` 与 `src/plugin/PluginRuntime.h`；不改 `Database`、
  `PluginContext`、`StartupDefer.h`、`qml/`、`tests/`、`CMakeLists.txt`、`README.md`。
- 不改 grants / revoked 的读写、官方插件自动补授规则、`permissionRequested` 信号本身，
  也不改第三方插件「稍后再说 / 拒绝」的既有语义（README.md:72 保持原样）。
- 不新造调度器：复用 `Database::schemaFinalized` 与 `replayGrantsBootstrap()` 这对既有入口；
  只加一个「已挂接」布尔标记保证重入安全（Z1 / Z13）。
- 无数据库指针时（单测等场景）保持原行为，立即加载，不为测试引入新依赖。
- 不顺手重排格式、不统一邻码写法（Z2）。

## 输入

基线 `origin/main`（5299a0f）。现场日志与文件行号见 frontmatter `evidence`；
同族先例：`src/plugin/PluginLibrary.cpp:249` 使用 `Qt::SingleShotConnection` 挂一次性回调。
依赖：无。

## 产出

① 实现提交在两条 scope 内完成时序修复，subject/body 带 `#<短号>` 与 `#<短号>=done`；
② 交付说明写回卡片 `note`；
③ 合并提交 body 写全四段（变更／验收／影响／卡片），由 `git-done.ps1` 自动结卡。
