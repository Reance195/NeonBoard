---
state: closed
card: MOD-12
stage: MOD
tier: P4
title: MOD QML 批次二批：执行前确认弹窗整迁 components/ExecConfirmDialog
author: codebuddy-session-2026-11-09
evidence: 2026-11-09 实测（基线 origin/main，Main.qml 3675 行）：execConfirmDialog（R6-03 执行前确认弹窗）块 2777-3012 共 236 行，括号平衡；外部引用仅三处——hostWindow: window（壳头）、taskEditorLoader.item.execNotice（content 内 cwd 可信联动）、window.openExecPanel（确认后跳面板）；另有一处 background: null 是属性赋值误报非 id 引用。接口面 = 两个 property（hostWin / editorLoader），Main 实例化绑定两行即可，调用点零改动。MOD-11 已验证整 Dialog 迁移模式与其两个坑（QT_RESOURCE_ALIAS 裸名登记、body 起点勿含 id 行），本卡直接沿用。维护者 2026-11-09 会话指令「下一张自包含弹窗卡：MOD-12 迁 execConfirmDialog」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/Main.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/ExecConfirmDialog.qml（壳与 content 逐字照搬，仅 hostWindow: window 改 hostWin、taskEditorLoader 改 editorLoader 两处接口化）且 Main.qml 中 execConfirmDialog 段替换为带 hostWin 与 editorLoader 绑定的一处实例化（id 保留原名）;Main.qml 行数 <= 3450（基线 3675）且 LINE_LIMITS 中 qml/Main.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名（MOD-11 教训前置）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：执行前确认弹窗（对任一卡点执行）字段渲染与确认/取消交互与迁移前一致
depends: 
createdAt: 2026-11-09T14:10:00+00:00
---

## 目标

把 `qml/Main.qml` 的 `execConfirmDialog`（2777-3012，236 行，R6-03 执行前确认弹窗）整块迁入新建的 `qml/components/ExecConfirmDialog.qml`。接口面仅两个 property：`hostWin`（原 window：壳的 hostWindow 与确认后跳转）、`editorLoader`（原 taskEditorLoader：cwd 联动写 execNotice）。

## 验收

见 frontmatter `acceptance`（6 条）。核心是第 1、2 条：全量 verify 绿（--smoke QML 致命告警 0）、壳与 content 逐字照搬仅两处接口化。第 6 条诚实边界：弹窗打开态请维护者人工复核（--smoke 4 秒窗口测不到）。

## 边界

见 frontmatter `scope`，四条路径（**吸取 MOD-11 教训，CMakeLists.txt 立卡即入 scope**）：

- `qml/components/**`：新增 ExecConfirmDialog.qml；
- `qml/Main.qml`：execConfirmDialog 段替换为带绑定的实例化；
- `CMakeLists.txt`：qt_add_qml_module 源条目 + QT_RESOURCE_ALIAS 裸名（两条都要）；
- `scripts/check_module_deps.py`：LINE_LIMITS 中 qml/Main.qml 同步调低。

**不动**：execQueueDialog（已迁）、execPanelDialog / execBatchDialog（后续批次）、主内容层、十个手工 alias、任何 C++。不碰台账契约。

## 输入

基线 `origin/main`。MOD-11 已结卡（迁移模式与 CMake 双登记坑已验证）。

## 产出

`qml/components/ExecConfirmDialog.qml` + Main.qml 接管 + CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明**单独一条 `docs(board):` 提交**（MOD-11 的混提交瑕疵不再重复），`note` 写「已完成 X / 待做 Y」与 nextStep（execPanelDialog / execBatchDialog / 主内容层）。
