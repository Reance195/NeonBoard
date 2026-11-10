---
state: proposed
card: MOD-25
stage: MOD
tier: P4
title: MOD 手工 alias 收敛：Main 对组件内部元素的伸手改为语义接口
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 实测（基线 origin/main）：MOD-11~24 迁移批次遗留 4 个「仅为 Main 伸手」的 alias——BoardChrome.qml:14-16（searchField / columnRepeater / themePicker）与 NeonBackgroundLayer.qml:9（background）；Main.qml 消费点共 11 处：neonBgLayer.background.burst()（133/887）、boardChrome.searchField.text=""（525/580）、boardChrome.themePicker.open 读写（217/230/391/948/1044）、boardChrome.columnRepeater.count/itemAt（169-170，columnItems() 被 Main 8 处调用）。git grep 确认四 alias 消费方仅 Main。收敛方案（Z5 单一出口）：组件加语义接口——NeonBackgroundLayer 加 function burst() 并删 background alias；BoardChrome 加 function clearSearch() / function columnItems() / readonly property bool themePickerOpen / function openThemePicker() / function closeThemePicker() 并删三 alias；Main 11 处调用点改走新接口。DialogShell/DrawerShell 的 default property alias 为架构性，不动。MOD-12 note 所记「十个手工 alias」为 2026-11-09 时点口径，现行实测为 4 alias / 11 消费点。维护者 2026-11-10 会话指令「领卡：十个手工 alias 收敛」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/Main.qml;qml/components/**;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;NeonBackgroundLayer 新增 function burst() 且删 background alias；BoardChrome 新增 function clearSearch() / function columnItems() / readonly property bool themePickerOpen / function openThemePicker() / function closeThemePicker() 且删三 alias；Main.qml 11 处伸手调用点全部改走新接口，grep 断言 \bbackground\.burst|\bsearchField\.|\bthemePicker\.|\bcolumnRepeater\. 在 Main.qml 零命中;Main.qml 行数不增（基线 274，LINE_LIMITS 同步核对）且 check_module_deps 提示可下调时一并调低;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：换肤涟漪（拖卡/完成时背景爆发）、搜索清空、外观抽屉开关、键盘跨列导航行为与迁移前一致
depends:
createdAt: 2026-11-10T18:30:00+00:00
---

## 目标

把 MOD 迁移批次遗留的 4 个手工 alias（BoardChrome searchField / columnRepeater / themePicker、NeonBackgroundLayer background）收敛为组件语义接口：组件暴露「做什么」，Main 不再摸组件内部元素。11 处调用点改走新接口，4 个 alias 删除。

## 验收

见 frontmatter `acceptance`（6 条）。第 3 条含 grep 机器判据；第 6 条诚实边界：涟漪 / 抽屉 / 键盘导航请维护者人工复核。

## 边界

见 frontmatter `scope`：

- `qml/components/**`：BoardChrome.qml 加 5 个语义接口删 3 alias；NeonBackgroundLayer.qml 加 burst() 删 1 alias；
- `qml/Main.qml`：11 处调用点改写，columnItems() 变薄委托；
- `scripts/check_module_deps.py`：LINE_LIMITS Main.qml 若可下调则同步。

**不动**：DialogShell / DrawerShell 的 default property alias（架构性）、六分页组件、任何 C++。接手现场单独 `chore(claim)` 提交。

## 输入

基线 `origin/main`。MOD-11~24 已结卡（QML 治理系列已销账，BoardChrome/NeonBackgroundLayer 均为迁移批次产物）。

## 产出

组件语义接口 + alias 清零 + Main 调用点收敛；合并提交 body 四段；交付说明单独 `docs(board):` 提交，note 写「已完成 X / 待做 Y」与 QML 治理系列终盘。
