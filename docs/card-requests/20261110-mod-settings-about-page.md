---
state: closed
card: MOD-23
stage: MOD
tier: P4
title: MOD SettingsPanel 拆分六批：关于分页整迁 components/AboutPage
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 实测（基线 origin/main，SettingsPanel.qml 480 行，LINE_LIMITS 同值）：分页 6「关于」ColumnLayout 位于 269-477 共 209 行（含版本信息 / 更新设置等小节，updateManager 等为上下文属性）。双向引用面正则全扫：块内仅 panel.page（visible 一行，移实例化处）；块内唯一 id incompleteHint 无块外引用；块内引用的块外顶层声明为零。接口面 = visible 绑定一行，无其它 property。维护者 2026-11-10 会话指令「领下一卡：关于页拆分」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/components/SettingsPanel.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/AboutPage.qml（分页 ColumnLayout 块体逐字照搬，仅 visible 行移实例化处）且 SettingsPanel 中 269-477 分页段删除并替换为带 visible 绑定的一处实例化;SettingsPanel.qml 行数 <= 285（基线 480）且 LINE_LIMITS 中 qml/components/SettingsPanel.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名（MOD-11 教训前置）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：关于页（版本信息 / 更新设置与自动检查开关）字段渲染与交互与迁移前一致
depends:
createdAt: 2026-11-10T17:00:00+00:00
---

## 目标

把 SettingsPanel.qml 分页 6「关于」（269-477，209 行）整块迁入新建 `qml/components/AboutPage.qml`。SettingsPanel 侧替换为一处实例化（visible 绑定一行）。

## 验收

见 frontmatter `acceptance`（6 条）。第 6 条诚实边界：关于页交互请维护者人工复核。

## 边界

见 frontmatter `scope`：

- `qml/components/**`：新增 AboutPage.qml；
- `qml/components/SettingsPanel.qml`：删分页段 + 实例化接管；
- `CMakeLists.txt`：双登记两条；
- `scripts/check_module_deps.py`：LINE_LIMITS SettingsPanel 同步调低。

**不动**：分页 3 外观（收官批次）、四个已迁分页、navRail、任何 C++。接手现场单独 `chore(claim)` 提交。迁移纪律成对执行：body 切片不含根开/根闭，删除段含被迁块的根开/根闭。

## 输入

基线 `origin/main`。MOD-11~22 已结卡。

## 产出

`qml/components/AboutPage.qml` + SettingsPanel 瘦身（预计 ~276）+ CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明单独 `docs(board):` 提交，note 写「已完成 X / 待做 Y」与 nextStep（外观收官批）。
