---
state: closed
card: MOD-18
stage: MOD
tier: P4
title: MOD SettingsPanel 拆分首批：内联控件三件套提取 components（DataButton / NeonComboBox / NeonField）
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 实测（基线 origin/main，SettingsPanel.qml 2072 行，LINE_LIMITS 2072）：内联控件三件套位于 71-188 共 118 行（DataButton 72-100 / NeonComboBox 102-173 / NeonField 175-188），仅依赖 Theme 单例与 QtQuick.Controls/Layouts，完全自包含；用点全在 SettingsPanel 内（DataButton 15 处 / NeonComboBox 3 处 / NeonField 8 处，跨文件仅 CardDetailPanel.qml:325 注释提及），同名类型经 qt_add_qml_module 注册后调用点零改动。本卡为 SettingsPanel 治理系列首批（MOD-11~16 批次模式沿用）：先提取公共控件，解锁后续分页拆分；后续批次盘点——六个分页（数据与备份 406 / 插件 327 / 关于 208 / 执行 204 / 热键 116 / 外观 80）与 profileEditor（222，耦合 execSettingsHint 跨页提示）/ repoBindDialog（118）两弹窗。维护者 2026-11-10 会话指令「领下一卡：SettingsPanel.qml 独立迁移」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/components/SettingsPanel.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/DataButton.qml、qml/components/NeonComboBox.qml、qml/components/NeonField.qml（三件套块体逐字照搬，仅类型来源由 SettingsPanel 内联 component 改为模块文件）且 SettingsPanel.qml 中内联声明段（71-188）删除、26 处用点零改动;SettingsPanel.qml 行数 <= 1960（基线 2072）且 LINE_LIMITS 中 qml/components/SettingsPanel.qml 上限同步调低到实际行数;CMakeLists 同时登记三个新文件的 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名（MOD-11 教训前置）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：设置面板六页（热键 / 数据 / 外观 / 执行 / 插件 / 关于）与三个弹窗的字段渲染与交互与迁移前一致
depends:
createdAt: 2026-11-10T11:40:00+00:00
---

## 目标

把 `qml/components/SettingsPanel.qml` 的三个内联控件（`component DataButton` / `component NeonComboBox` / `component NeonField`，71-188，118 行）提取为三个模块文件 `qml/components/DataButton.qml` / `NeonComboBox.qml` / `NeonField.qml`。类型名不变，SettingsPanel 内 26 处用点零改动；为后续六个分页与两个弹窗的拆分批次解锁公共控件依赖。

## 验收

见 frontmatter `acceptance`（6 条）。核心是前 4 条：全量 verify 绿（--smoke QML 致命告警 0）、块体逐字照搬仅类型来源变化、SettingsPanel 行数与 LINE_LIMITS 同步调低、CMake 三文件双登记。第 6 条诚实边界：面板交互请维护者人工复核（--smoke 4 秒窗口测不到弹层展开）。

## 边界

见 frontmatter `scope`，四条路径（MOD-11 教训，CMakeLists.txt 立卡即入 scope）：

- `qml/components/**`：新增三个控件文件；
- `qml/components/SettingsPanel.qml`：仅删内联声明段；
- `CMakeLists.txt`：三条源条目 + 三条 QT_RESOURCE_ALIAS（六行都要）；
- `scripts/check_module_deps.py`：LINE_LIMITS SettingsPanel 同步调低。

**不动**：六个分页、三个弹窗、navRail、十个手工 alias、任何 C++。不碰台账契约。

## 输入

基线 `origin/main`。MOD-11~17 已结卡（模块文件提取模式、CMake 双登记坑、FolderDialog 无 parent 属性坑均已验证）。

## 产出

三个控件文件 + SettingsPanel 瘦身 + CMake 六行登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明**单独一条 `docs(board):` 提交**，`note` 写「已完成 X / 待做 Y」与 nextStep（六个分页与两弹窗的批次清单）。
