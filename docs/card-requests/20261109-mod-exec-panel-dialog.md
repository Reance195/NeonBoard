---
state: closed
card: MOD-13
stage: MOD
tier: P4
title: MOD QML 批次三批：执行结果面板弹窗整迁 components/ExecPanelDialog
author: codebuddy-session-2026-11-09
evidence: 2026-11-09 实测（基线 origin/main，Main.qml 3675 行）：execPanelDialog（R6-06 执行结果面板弹窗）块 2784-3045 共 262 行，括号平衡；外部引用仅一处——window.openExecQueue（确认后跳队列），另有一处 background: null 经核实是属性赋值误报非 id 引用；content 引用的 taskRuns / costSummary 等均为 context property。接口面 = hostWin 一个 property。MOD-11 / MOD-12 已验证整 Dialog 迁移模式与三个坑（QT_RESOURCE_ALIAS 裸名、body 起点勿含 id 行与壳闭括号、组件根须显式 property var 声明），本卡直接沿用。维护者 2026-11-09 会话指令「下一张卡：迁移 execPanelDialog 弹窗」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/Main.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/ExecPanelDialog.qml（壳与 content 逐字照搬，仅 hostWindow: window 改 hostWin 一处接口化）且 Main.qml 中 execPanelDialog 段替换为带 hostWin 绑定的一处实例化（id 保留原名）;Main.qml 行数 <= 3420（基线 3675）且 LINE_LIMITS 中 qml/Main.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：执行结果面板弹窗打开后 run 状态、耗时与成本合计渲染与迁移前一致
depends: 
createdAt: 2026-11-09T14:50:00+00:00
---

## 目标

把 `qml/Main.qml` 的 `execPanelDialog`（2784-3045，262 行，R6-06 执行结果面板弹窗）整块迁入新建的 `qml/components/ExecPanelDialog.qml`。接口面仅一个 property：`hostWin`（原 window）。

## 验收

见 frontmatter `acceptance`（6 条）。核心是第 1、2 条：全量 verify 绿（--smoke QML 致命告警 0）、壳与 content 逐字照搬仅一处接口化。第 6 条诚实边界：弹窗打开态请维护者人工复核。

## 边界

见 frontmatter `scope`，四条路径（CMakeLists.txt 立卡即入 scope，MOD-11/12 教训前置）：

- `qml/components/**`：新增 ExecPanelDialog.qml；
- `qml/Main.qml`：execPanelDialog 段替换为带 hostWin 绑定的实例化；
- `CMakeLists.txt`：qt_add_qml_module 源条目 + QT_RESOURCE_ALIAS 裸名；
- `scripts/check_module_deps.py`：LINE_LIMITS 中 qml/Main.qml 同步调低。

**不动**：execQueueDialog / execConfirmDialog（已迁）、execBatchDialog（下一批）、主内容层、手工 alias、任何 C++。不碰台账契约。

## 输入

基线 `origin/main`。MOD-11 / MOD-12 已结卡（迁移模式与三个坑已验证）。

## 产出

`qml/components/ExecPanelDialog.qml` + Main.qml 接管 + CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明**单独一条 `docs(board):` 提交**，`note` 写「已完成 X / 待做 Y」与 nextStep（execBatchDialog / 主内容层 933-1851）。
