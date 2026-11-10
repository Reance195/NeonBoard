---
state: proposed
card: MOD-17
stage: MOD
tier: P4
title: MOD QML 批次七批：任务编辑器内联组件整迁 components/TaskEditorDialog
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 实测（基线 origin/main，Main.qml 2404 行，LINE_LIMITS 同值）：内联 component TaskEditor: Dialog 块 1293-2175 共 883 行，括号平衡，是 MOD-13~16 迁移后 Main.qml 的最大残留块。MOD-12 note 所记「主内容层 Item（933-1851，919 行）」在该四批迁移后已不复存在，本卡即该后续项在现行文件下的对应物。块内引用块外仅两类：window（6 处：parent/width/openSettings/openExecPanel/openExecConfirm/deleteTaskWithUndo）与 execCwdDialog（根层 FolderDialog 2204-2213，可随迁转内部引用）；taskStore/taskUndo/taskExtra/taskExec/agentProfile/taskRuns 均为 C++ 上下文属性（AppMain.cpp 401-409 表注入），组件文件可直接用。块外对块内引用全部走 taskEditorLoader.item（openTaskEditor 28-32、FolderDialog onAccepted 2210-2211、ExecConfirmDialog editorLoader 绑定 2218、triage Connections 2396-2399），Loader 按需加载语义保留、调用点零改动。维护者 2026-11-10 会话指令「领 MOD-12 后续：主内容层 Item 整迁」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/Main.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/TaskEditorDialog.qml（内联 component TaskEditor 块体逐字照搬，仅 window→hostWin 接口化；根层 FolderDialog execCwdDialog 随迁进组件、onAccepted 转内部赋值）且 Main.qml 中内联 component TaskEditor 块删除、taskEditorComponent 改引新组件类型、根层 FolderDialog 段删除（taskEditorLoader/execCwdDialog id 语义保留、按需加载不变）;Main.qml 行数 <= 1560（基线 2404）且 LINE_LIMITS 中 qml/Main.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名（MOD-11 教训前置）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：任务编辑器（打开 / 任务与执行两页签 / 执行确认 / 删除撤销 / 取消）字段渲染与交互与迁移前一致
depends:
createdAt: 2026-11-10T10:30:00+00:00
---

## 目标

把 `qml/Main.qml` 的内联 `component TaskEditor: Dialog`（1293-2175，883 行，R6 任务编辑器，当前最大残留块）整块迁入新建的 `qml/components/TaskEditorDialog.qml`。接口面仅一个 property：`hostWin`（原 window：parent / width / openSettings / openExecPanel / openExecConfirm / deleteTaskWithUndo 共 6 处）。根层 `FolderDialog execCwdDialog`（2204-2213）随迁进组件，onAccepted 直接内部赋值，Main 侧整段删除。

## 验收

见 frontmatter `acceptance`（6 条）。核心是前 4 条：全量 verify 绿（--smoke QML 致命告警 0）、块体逐字照搬仅 window 接口化 + FolderDialog 随迁、Main.qml 行数与 LINE_LIMITS 同步调低、CMake 双登记。第 6 条诚实边界：弹窗打开态请维护者人工复核（--smoke 4 秒窗口测不到）。

## 边界

见 frontmatter `scope`，四条路径（MOD-11 教训，CMakeLists.txt 立卡即入 scope）：

- `qml/components/**`：新增 TaskEditorDialog.qml；
- `qml/Main.qml`：内联 component 段与根层 FolderDialog 段删除，Loader 改引新组件；
- `CMakeLists.txt`：qt_add_qml_module 源条目 + QT_RESOURCE_ALIAS 裸名（两条都要）；
- `scripts/check_module_deps.py`：LINE_LIMITS qml/Main.qml 同步调低。

**不动**：已迁六个组件（ExecQueueDialog / ExecConfirmDialog / ExecPanelDialog / NightBatchDialog / BoardChrome / NeonBackgroundLayer）、十个手工 alias、任何 C++。不碰台账契约。

## 输入

基线 `origin/main`。MOD-11~16 已结卡（整 Dialog 迁移模式、CMake 双登记坑、note 单独提交纪律均已验证）。

## 产出

`qml/components/TaskEditorDialog.qml` + Main.qml 接管 + CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明**单独一条 `docs(board):` 提交**，`note` 写「已完成 X / 待做 Y」与 nextStep（剩余内联组件盘点 / 十个手工 alias 收敛）。
