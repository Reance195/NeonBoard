---
state: closed
card: MOD-21
stage: MOD
tier: P4
title: MOD SettingsPanel 拆分四批：插件分页整迁 components/PluginPage
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 实测（基线 origin/main，SettingsPanel.qml 1250 行，LINE_LIMITS 同值）：分页 5「插件」ColumnLayout 位于 488-815 共 328 行（含 ①已装插件 / ②市场市场源 / ③设置页签注入三节）。双向引用面正则全扫：块内仅 panel.page（visible 一行，移实例化处）与 panel.openLibraryRequested（signal，2 处 @645/710）；块内 9 个 id 均无块外引用；pluginLoader / pluginRuntime / pluginLibrary / pluginMarket 均为 C++ 上下文属性（AppMain.cpp:395-399），组件可直接用；块内类型仅需 QtQuick / QtQuick.Controls / QtQuick.Layouts。接口面 = signal openLibraryRequested（组件根声明同名信号，实例化处转发面板信号）+ visible 绑定一行。迁移脚本按 MOD-20 note 对策改模板：body 切片不含根闭行，根闭由模板统一追加（Z6：块根闭括号坑已三现）。维护者 2026-11-10 会话指令「领下一卡：插件页拆分」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/components/SettingsPanel.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/PluginPage.qml（分页 ColumnLayout 块体逐字照搬，仅 panel.openLibraryRequested→组件根同名 signal 与 visible 行移实例化处）且 SettingsPanel 中 488-815 分页段删除并替换为带 id/visible/onOpenLibraryRequested 转发的一处实例化;SettingsPanel.qml 行数 <= 935（基线 1250）且 LINE_LIMITS 中 qml/components/SettingsPanel.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名（MOD-11 教训前置）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：插件页（已装插件列表 / 市场源管理 / 插件库跳转 / 设置页签注入）字段渲染与交互与迁移前一致
depends:
createdAt: 2026-11-10T15:10:00+00:00
---

## 目标

把 SettingsPanel.qml 分页 5「插件」（488-815，328 行）整块迁入新建 `qml/components/PluginPage.qml`。SettingsPanel 侧替换为一处实例化（id / visible / onOpenLibraryRequested 转发三行）。

## 验收

见 frontmatter `acceptance`（6 条）。第 6 条诚实边界：插件页交互请维护者人工复核。

## 边界

见 frontmatter `scope`：

- `qml/components/**`：新增 PluginPage.qml；
- `qml/components/SettingsPanel.qml`：删分页段 + 实例化接管；
- `CMakeLists.txt`：双登记两条；
- `scripts/check_module_deps.py`：LINE_LIMITS SettingsPanel 同步调低。

**不动**：其余三个分页（执行 / 关于 / 外观）、DataBackupPage / HotkeyPage、profileEditor / repoBindDialog、navRail、任何 C++。接手现场单独 `chore(claim)` 提交。

## 输入

基线 `origin/main`。MOD-11~20 已结卡（分页整迁模式三轮验证、CMake 双登记、body 切片不含根闭行的新模板纪律）。

## 产出

`qml/components/PluginPage.qml` + SettingsPanel 瘦身 + CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明单独 `docs(board):` 提交，note 写「已完成 X / 待做 Y」与 nextStep（执行+profileEditor / 关于 / 外观三批次盘点）。
