---
state: closed
card: MOD-14
stage: MOD
tier: P4
title: MOD QML 批次四批：夜间批量执行弹窗整迁 components/NightBatchDialog
author: codebuddy-session-2026-11-09
evidence: 2026-11-09 实测（基线 origin/main，Main.qml 3179 行）：夜间批量执行弹窗块 2793-3003 共 211 行（id: nightBatchDialog，R7-03 夜间批量），括号平衡；块内零 window. 引用，外部引用仅 hostWindow: window 一处接口化；batchList / rowMouse 为块内自声明误报；scheduler / Theme 均为 context property / 模块内组件。更正：此前会话盘点把它误标为 execBatchDialog，实际 id 是 nightBatchDialog，以本实测为准。MOD-11/12/13 已验证整 Dialog 迁移模式与三个坑（QT_RESOURCE_ALIAS 裸名、body 起点勿含 id 行与壳闭括号——本卡迁移脚本已按 Z6 修正切片、组件根须显式 property var 声明），本卡直接沿用。维护者 2026-11-09 会话指令「迁移 execBatchDialog 弹窗」，按实测更正对应到 nightBatchDialog，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/Main.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/NightBatchDialog.qml（壳与 content 逐字照搬，仅 hostWindow: window 改 hostWin 一处接口化）且 Main.qml 中 nightBatchDialog 段替换为带 hostWin 绑定的一处实例化（id 保留原名，Main 级 openNightBatch 函数调用不受影响）;Main.qml 行数 <= 3170（基线 3179）且 LINE_LIMITS 中 qml/Main.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：夜间批量弹窗打开后多选清单勾选、预估时长与「开始夜间执行」行为与迁移前一致
depends:
createdAt: 2026-11-09T16:10:00+00:00
---

## 目标

把 `qml/Main.qml` 的 `nightBatchDialog`（2793-3003，211 行，夜间批量执行弹窗）整块迁入新建的 `qml/components/NightBatchDialog.qml`。接口面仅一个 property：`hostWin`（原 window）。

更正说明：本卡在请求单环节实测发现该弹窗此前被会话盘点误标为「execBatchDialog」，实际 id 为 `nightBatchDialog`（R7-03 夜间批量），维护者指令「迁移 execBatchDialog 弹窗」按实测对应到本卡。

## 验收

见 frontmatter `acceptance`（6 条）。核心是第 1、2 条：全量 verify 绿（--smoke QML 致命告警 0）、壳与 content 逐字照搬仅一处接口化。第 6 条诚实边界：弹窗打开态请维护者人工复核。

## 边界

见 frontmatter `scope`，四条路径（CMakeLists.txt 立卡即入 scope，MOD-11/12 教训前置）：

- `qml/components/**`：新增 NightBatchDialog.qml；
- `qml/Main.qml`：nightBatchDialog 段替换为带 hostWin 绑定的实例化；
- `CMakeLists.txt`：qt_add_qml_module 源条目 + QT_RESOURCE_ALIAS 裸名；
- `scripts/check_module_deps.py`：LINE_LIMITS 中 qml/Main.qml 同步调低。

**不动**：execQueueDialog / execConfirmDialog / execPanelDialog（已迁）、execCwdDialog（后续批次评估）、主内容层、手工 alias、任何 C++。不碰台账契约。

## 输入

基线 `origin/main`。MOD-11 / MOD-12 / MOD-13 已结卡（迁移模式与三个坑已验证，切块脚本已按 Z6 修正）。

## 产出

`qml/components/NightBatchDialog.qml` + Main.qml 接管 + CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明**单独一条 `docs(board):` 提交**，`note` 写「已完成 X / 待做 Y」与 nextStep（主内容层 933-1851 / execCwdDialog 评估 / 手工 alias 收敛）。
