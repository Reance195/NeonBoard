---
state: closed
card: MOD-20
stage: MOD
tier: P4
title: MOD SettingsPanel 拆分三批：热键分页整迁 components/HotkeyPage
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 实测（基线 origin/main，SettingsPanel.qml 1374 行，LINE_LIMITS 同值）：分页 1「热键与自动化」ColumnLayout 位于 197-313 共 117 行（含 ①全局热键 与 ②开机与提醒 两小节）；专属数据 shortcuts readonly property 位于 51-59 共 9 行（块外零引用）。双向引用面全扫（MOD-19 教训：正则全扫非白名单）：块内仅 panel.page（visible 一行，移实例化处）与 panel.shortcuts（随迁后转组件内作用域）；块内唯一 id shortcutRow 无块外引用；hotkey / tray 均为 C++ 上下文属性（AppMain.cpp:393-394），组件可直接用；块内类型仅需 QtQuick / QtQuick.Controls（Switch）/ QtQuick.Layouts。接口面 = 实例化处 visible 绑定一行，无其它 property。维护者 2026-11-10 会话指令「领下一卡：SettingsPanel 分页拆分之热键」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/components/SettingsPanel.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/HotkeyPage.qml（分页 ColumnLayout 与 shortcuts 属性块体逐字照搬，仅 panel.shortcuts→shortcuts 与 visible 行移实例化处）且 SettingsPanel 中 51-59 shortcuts 段与 197-313 分页段删除并替换为带 visible 绑定的一处实例化;SettingsPanel.qml 行数 <= 1260（基线 1374）且 LINE_LIMITS 中 qml/components/SettingsPanel.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名（MOD-11 教训前置）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：热键页（全局热键状态展示 / 快捷键清单 / 开机自启开关）字段渲染与交互与迁移前一致
depends:
createdAt: 2026-11-10T14:20:00+00:00
---

## 目标

把 SettingsPanel.qml 分页 1「热键与自动化」（197-313，117 行）连同其专属数据 shortcuts（51-59，9 行）整块迁入新建 `qml/components/HotkeyPage.qml`。SettingsPanel 侧替换为一处实例化（visible 绑定一行）。本卡是 SettingsPanel 拆分系列最轻一批，模式与前两批完全一致。

## 验收

见 frontmatter `acceptance`（6 条）。第 6 条诚实边界：热键页交互请维护者人工复核。

## 边界

见 frontmatter `scope`：

- `qml/components/**`：新增 HotkeyPage.qml；
- `qml/components/SettingsPanel.qml`：删两段 + 实例化接管；
- `CMakeLists.txt`：双登记两条；
- `scripts/check_module_deps.py`：LINE_LIMITS SettingsPanel 同步调低。

**不动**：其余四个分页、DataBackupPage、profileEditor / repoBindDialog、navRail、任何 C++。接手现场单独 `chore(claim)` 提交。

## 输入

基线 `origin/main`。MOD-11~19 已结卡（分页整迁模式两轮验证、CMake 双登记、\bpanel\. 全扫勘界纪律）。

## 产出

`qml/components/HotkeyPage.qml` + SettingsPanel 瘦身 + CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明单独 `docs(board):` 提交，note 写「已完成 X / 待做 Y」与 nextStep（插件 / 执行+profileEditor / 关于 / 外观四批次盘点）。
