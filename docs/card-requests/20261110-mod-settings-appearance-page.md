---
state: proposed
card: MOD-24
stage: MOD
tier: P4
title: MOD SettingsPanel 拆分收官批：外观分页整迁 components/AppearancePage
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 实测（基线 origin/main，SettingsPanel.qml 274 行，LINE_LIMITS 同值）：分页 3「外观与动效」ColumnLayout 位于 174-254 共 81 行；专属数据段 = currentThemeName / currentQualityName 两只读属性（52-60，9 行），块外零引用（仅分页内 204/222 使用，panel. 前缀随迁转组件内作用域）。双向引用面正则全扫：块内仅 panel.page（visible 一行）、panel.currentThemeName、panel.currentQualityName 三处；块内无 id；块内引用的块外顶层声明为零（Theme 单例 + qualityNames 等均为 Theme 成员）。接口面 = visible 绑定一行，无其它 property。本批为 SettingsPanel 拆分系列收官批：完成后 SettingsPanel 仅剩壳 + navRail + 六个分页实例化区。维护者 2026-11-10 会话指令「领下一卡：外观分页拆分」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/components/SettingsPanel.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/AppearancePage.qml（分页 ColumnLayout 与两只读属性块体逐字照搬，仅 panel.currentThemeName/currentQualityName→组件内作用域与 visible 行移实例化处）且 SettingsPanel 中 52-60 属性段与 174-254 分页段删除并替换为带 visible 绑定的一处实例化;SettingsPanel.qml 行数 <= 195（基线 274）且 LINE_LIMITS 中 qml/components/SettingsPanel.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名（MOD-11 教训前置）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：外观页（主题/画质只读展示与说明文案）渲染与迁移前一致
depends:
createdAt: 2026-11-10T17:50:00+00:00
---

## 目标

把 SettingsPanel.qml 分页 3「外观与动效」（174-254，81 行）连同专属只读属性段（52-60，9 行）整块迁入新建 `qml/components/AppearancePage.qml`。SettingsPanel 侧替换为一处实例化（visible 绑定一行）。收官批：完成后 SettingsPanel 仅剩壳 + navRail + 实例化区。

## 验收

见 frontmatter `acceptance`（6 条）。第 6 条诚实边界：外观页渲染请维护者人工复核。

## 边界

见 frontmatter `scope`：

- `qml/components/**`：新增 AppearancePage.qml；
- `qml/components/SettingsPanel.qml`：删两段 + 实例化接管；
- `CMakeLists.txt`：双登记两条；
- `scripts/check_module_deps.py`：LINE_LIMITS SettingsPanel 同步调低。

**不动**：五个已迁分页、navRail、任何 C++。接手现场单独 `chore(claim)` 提交。迁移纪律成对执行：body 切片不含根开/根闭，删除段含被迁块的根开/根闭。

## 输入

基线 `origin/main`。MOD-11~23 已结卡。

## 产出

`qml/components/AppearancePage.qml` + SettingsPanel 瘦身（预计 ~188）+ CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明单独 `docs(board):` 提交，note 写「已完成 X / 待做 Y」与系列销账小结（2072 → 预计 ~188，-91%）。
