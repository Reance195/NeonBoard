---
state: closed
card: MOD-19
stage: MOD
tier: P4
title: MOD SettingsPanel 拆分二批：数据与备份分页整迁 components/DataBackupPage
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 实测（基线 origin/main，SettingsPanel.qml 1953 行，LINE_LIMITS 同值）：分页 2「数据与备份」ColumnLayout 位于 369-775 共 407 行（块内无根 id）；专属逻辑枢纽与两个专属弹窗随之整迁——项目/数据函数段 97-149 共 53 行（unassignedTasks 97-101 / newProject 103-117 / bindProjectId+bindProjectRepo 119-127 / onRepoPicked 129-142 / refreshExports 145-149，块外仅 onOpened@154 调 refreshExports 一处）、repoFolderDialog 1828-1833、repoBindDialog 1835-1952（parent: panel.contentItem）。正向引用面仅 panel.page / panel.unassignedTasks / panel.newProject / panel.bindProjectRepo 四个成员；块内 8 个 id 中 exportCombo / newProjectField / projectSettingsHint 被随迁函数与弹窗引用，随迁后转同文件作用域；splitArgs / editProfile / profileEditor（profile 系）不迁。接口面 = property hostShell（原 panel：repoBindDialog.parent）+ 实例化处 visible 绑定（原块内 panel.page === "data" 一行）+ onOpened 改调实例函数 refreshExports。FolderDialog 无 parent 属性（MOD-17 已验证走父链解析），repoBindDialog 为 Controls Dialog 可携 parent。维护者 2026-11-10 会话指令「领下一卡：SettingsPanel 分页拆分之数据与备份」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/components/SettingsPanel.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/DataBackupPage.qml（分页 ColumnLayout、项目/数据函数段、repoFolderDialog、repoBindDialog 块体逐字照搬，仅 panel.bindProjectId→bindProjectId、parent: panel.contentItem→hostShell.contentItem、FolderDialog onAccepted 去面板前缀三处接口化）且 SettingsPanel 中 97-149 项目函数段、369-775 分页段、1828-1952 弹窗段删除并替换为带 id/visible/hostShell 绑定的一处实例化，onOpened 的 refreshExports 调用改指实例;SettingsPanel.qml 行数 <= 1380（基线 1953）且 LINE_LIMITS 中 qml/components/SettingsPanel.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名（MOD-11 教训前置）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：数据与备份页（导出 / 导入 / 项目新增 / 仓库绑定全流程含 FolderDialog 与绑定确认弹窗）字段渲染与交互与迁移前一致
depends:
createdAt: 2026-11-10T13:05:00+00:00
---

## 目标

把 SettingsPanel.qml 分页 2「数据与备份」（369-775，407 行）连同其专属函数段（97-149，53 行）与两个专属弹窗（repoFolderDialog 1828-1833 / repoBindDialog 1835-1952，124 行）整块迁入新建 `qml/components/DataBackupPage.qml`。SettingsPanel 侧替换为一处实例化（id / visible / hostShell 三行绑定），onOpened 的 refreshExports 改调实例。

## 验收

见 frontmatter `acceptance`（6 条）。核心是前 4 条：全量 verify 绿（--smoke QML 致命告警 0）、块体逐字照搬仅三处接口化、SettingsPanel 行数与 LINE_LIMITS 同步调低、CMake 双登记。第 6 条诚实边界：数据页全流程（导出导入 / 建项目 / 绑仓库含两个弹层）请维护者人工复核。

## 边界

见 frontmatter `scope`，四条路径（MOD-11 教训，CMakeLists.txt 立卡即入 scope）：

- `qml/components/**`：新增 DataBackupPage.qml；
- `qml/components/SettingsPanel.qml`：删三段 + 实例化接管 + onOpened 改调；
- `CMakeLists.txt`：qt_add_qml_module 源条目 + QT_RESOURCE_ALIAS 裸名（两条都要）；
- `scripts/check_module_deps.py`：LINE_LIMITS SettingsPanel 同步调低。

**不动**：其余五个分页、profileEditor（profile 系属执行页批次）、splitArgs / editProfile、navRail、任何 C++。不碰台账契约。接手现场登记单独 `chore(claim)` 提交（MOD-17/18 惯例）。

## 输入

基线 `origin/main`。MOD-11~18 已结卡（整 Dialog 迁移模式、CMake 双登记、FolderDialog 无 parent 属性走父链解析、三件套已入模块可直接用）。

## 产出

`qml/components/DataBackupPage.qml` + SettingsPanel 瘦身 + CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明**单独一条 `docs(board):` 提交**，`note` 写「已完成 X / 待做 Y」与 nextStep（插件 / 关于 / 执行 / 热键 / 外观五分页与 profileEditor / repoBindDialog 随页批次盘点）。
