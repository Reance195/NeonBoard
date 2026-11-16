---
state: approved
card: DX-174
stage: DX
tier: P3
title: 新增插件权限管理页面，集中查看与调整所有插件授权
author: maintainer-session-2026-11-16
evidence: 维护者 2026-11-16 会话确认「有权限管理的地方吗」→ 只读核查确认当前无集中管理页：授权只能在 PluginPermissionDialog.qml 的「安装/更新后」与「能力触发时」两种临时弹窗里处理（qml/components/PluginLibraryPanel.qml:38-40、qml/Main.qml:355-361），PluginPanel.qml:147 的「已授权/待授权」徽章只读不可点；后端已有 permissionCatalog()（src/plugin/PluginRuntime.cpp:1063，含中文标题/用途/风险级别）、setPermissionGranted()（:978，granted=true 会摘除 revoked 记录）、isPermissionGranted()（:949），唯一缺口是没有暴露「已撤销」状态的全量查询接口，也没有常驻 UI。撤销权限后目前无任何界面可恢复（只能等重装或再次触发弹窗），DX-172 修复的启动误弹正是这一缺口的体现。维护者明确要求根治后追加此页，答复「开工」。
scope: src/plugin/PluginRuntime.cpp;src/plugin/PluginRuntime.h;qml/components/PluginPermissionPanel.qml;qml/components/SettingsPanel.qml;CMakeLists.txt;README.md;tests/tst_pluginruntime.cpp
acceptance: PluginRuntime 新增 Q_INVOKABLE permissionOverview() 返回 QVariantList，每项含 pluginId/pluginName/sourceLabel 与 permissions[]，每条权限含 name/title/detail/risk/granted/revoked，复用 permissionCatalog() 元数据与 m_grants/m_revoked，不新造第二份元数据事实源;setPermissionGranted() 的 granted=true/false 语义零改动（true 摘撤销记录、false 写撤销记录），页面开关直接复用该接口，切换后刷新 overview;新建 qml/components/PluginPermissionPanel.qml 作为独立组件，逐插件逐权限列出中文标题/用途/风险级别与开关，能撤销与重新授权，空态与来源标签齐全;设置面板 SettingsPanel.qml 新增「权限管理」分页（pages 数组 + 组件实例），改动后该文件净行数 ≤ 185（LINE_LIMITS 上限，靠压缩头部注释实现，不净增）;CMakeLists.txt 两处登记新 QML（QT_RESOURCE_ALIAS + qt_add_qml_module 的 QML_FILES），避免脱离模块上下文导致 Theme/Motion ReferenceError;tests/tst_pluginruntime.cpp 加 permissionOverview 单测，覆盖已授权/已撤销/未授权三态与元数据回填，最小 PluginRuntime 构造不引入新依赖;README.md 在插件与设置相关条目补一句权限管理页入口说明;pwsh -File scripts/verify.ps1 全量档退出码 0（含编译、ctest、--selftest、--smoke、模块守卫、QML 致命告警、看板一致性）;python scripts/check_scope.py --base origin/main 与 python scripts/check_module_deps.py --base origin/main 退出码 0，改动不越出 scope 且行数闸门全绿;沙箱 GUI 打开设置→权限管理分页，能看到 com.lin.scheduler 四项权限并可切换，撤销后重启不再被官方自动补授，重新授权后恢复;既有授权弹窗语义（遮罩/Esc 只关闭、拒绝写撤销）与 DX-172 启动时序修复均零改动
depends:
createdAt: 2026-11-16T00:00:00+00:00
---

## 目标

给用户一个常驻的集中入口，一处看清并调整所有插件的每项权限（含撤销后的恢复），
消除「授权只能在临时弹窗里表态、撤销后无处恢复」的缺口。

## 验收

见 frontmatter `acceptance`。判据落在「全量查询接口三态正确」「页面可撤销可恢复」
「行数闸门与 scope 零越界」「全量 verify 绿灯」「沙箱 GUI 实测」上。
scope 含 `.cpp`/`.h`/`.qml`/`CMakeLists.txt`/`README.md`/测试，按宪章 §7.7 走代码类全量档。

## 边界

- 后端只**加**一个只读全量查询 `permissionOverview()`；不新增写接口、不新造权限元数据映射，
  直接复用 `permissionCatalog()` 与 `m_grants`/`m_revoked`（Z1 / Z5 单一事实源）。
- 开关复用现有 `setPermissionGranted()`，其 granted=true/false 语义（含撤销记录摘写）零改动。
- 不改授权弹窗 `PluginPermissionDialog.qml`、不改 `permissionRequested` 信号、
  不改 DX-172 的后台插件延迟加载时序、不改官方自动补授规则。
- 新 UI 全部收进新建 `PluginPermissionPanel.qml`；`SettingsPanel.qml` 只加极简分页胶水，
  靠压缩头部注释把净行数压到 ≤185，不触碰 `Main.qml`（1504 行同样卡死）。
- 不顺手重排邻码格式、不统一无关写法（Z2）。

## 输入

基线 `origin/main`（da829c5）。权限元数据事实源 `src/plugin/PluginRuntime.cpp:1063 permissionCatalog()`；
撤销记录语义 `:978 setPermissionGranted()`；分页结构 `qml/components/SettingsPanel.qml:42 pages`。
同族先例：DX-170「新 UI 收进新组件、大文件只留胶水压净增行数」；DX-169「新 QML 要 CMakeLists 两处登记」。
依赖：无（DX-172 已合入基线）。

## 产出

① 实现提交在 scope 内完成后端接口 + 页面 + 入口 + 登记 + 测试 + README，
   subject/body 带 `#<短号>` 与 `#<短号>=done`；
② 交付说明写回卡片 `note`；
③ 合并提交 body 写全四段（变更／验收／影响／卡片），由 `git-done.ps1` 自动结卡。
