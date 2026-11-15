---
state: closed
card: DX-169
stage: DX
tier: P4
title: 按钮样式同源：抽 NeonButton 单一实现，收口全项目 19 处原生 Button
author: maintainer-session-2026-11-15
evidence: 维护者 2026-11-15 会话以截图指出任务编辑器底部「删除／取消／保存」按钮太丑，并在选项中明确选择「抽通用组件全项目统一」与「按上次流程完整落地」。只读核查结论：这三个是 QtQuick.Controls 原生 Button，未覆盖 background 与 contentItem，于是落到 Qt 默认浅色样式（白底深字、默认 padding 撑到约 60px），与同一对话框里手写 Theme 令牌的页签「任务／执行」、优先级「低／中／高」、重复规则小按钮、各输入框深色霓虹外观割裂。全库 qml/ 共 19 处 Button 声明，14 处完全裸用（TaskEditorDialog 11、ExecPanelDialog 2、NightBatchDialog 1），另 5 处各自手写一遍 background/contentItem（TaskEditorDialog 页签 2、CardDetailPanel 2、ThemeMarketPanel 1），圆角描边字号抄成 5 份且细节互不一致。这些裸按钮大量位于 DialogShell 的 footerExtra 里，与壳底栏已主题化的 TopToolButton 同排显示，切到「执行」页签或打开执行面板／夜间批量／主题市场都会重现白块。项目已有 DialogShell.qml:262-279 的统一底栏实践与 Theme.qml 的完整令牌，缺的是按钮这一层的单一实现。
scope: CMakeLists.txt;qml/components/NeonButton.qml;qml/components/TaskEditorDialog.qml;qml/components/CardDetailPanel.qml;qml/components/ExecPanelDialog.qml;qml/components/NightBatchDialog.qml;qml/components/ThemeMarketPanel.qml
acceptance: qml/ 下正则 (^|[^A-Za-z])Button 后紧跟左花括号的声明归零，19 处全走 NeonButton，且新增组件里不出现 Qt.rgba 与写死的 #颜色字面量，颜色只经 Theme 令牌取得;CMakeLists.txt 同时登记 QT_RESOURCE_ALIAS 与 qt_add_qml_module 的 QML_FILES 两处，--smoke 启动日志无 ReferenceError、无「NeonButton is not a type」且 QML 致命告警 0 行;pwsh -File scripts/verify.ps1 全量档退出码 0，覆盖编译、ctest、--selftest、--smoke、模块守卫、QML 致命告警与看板一致性;python scripts/check_qml_pitfalls.py 与 python scripts/check_qml_warnings.py 退出码 0，python scripts/check_scope.py --base origin/main 与 python scripts/check_path_refs.py --base origin/main 退出码 0;19 处按钮的 text、enabled、visible、onClicked 逐处保持不变，5 个存量 QML 文件的 diff 只出现类型改 NeonButton、补 tone／selected 声明、删除重复的 background／contentItem 三类变化，不含新增业务逻辑
depends: 
createdAt: 2026-11-15T00:00:00+00:00
---

## 目标

把「按钮长什么样」收敛成一处事实：新建 `qml/components/NeonButton.qml` 作为全应用按钮的单一实现，
19 处原生 `Button` 全部改走它。组件保留 QtQuick.Controls `Button` 的原生语义（`enabled`、
`hovered`、`pressed`、键盘可达、`Accessible`），只把皮肤换成 `Theme` 令牌驱动的深色霓虹外观，
并用 `tone`（neutral / primary / danger）与 `selected` 两个属性表达语义差异：
「保存」「执行」为主操作，「删除」为破坏性操作，「任务／执行」页签用选中态。
主题切换时按钮跟着走，不再靠各文件各自抄一遍颜色。

## 验收

见 frontmatter `acceptance`（5 条）。判据落在「裸 Button 归零」「CMake 两处登记」
「启动无 QML 致命告警」「全量 verify 绿灯」「diff 不含业务逻辑变化」上，
不落在「肉眼看着顺眼」这种主观判断上。本卡改 CMake 与新增 QML 文件，走 P4 全量档，
编译与冒烟由本地 `verify.ps1` 验证，CI 对 main 常规兜底。

## 边界

- 只收口「QtQuick.Controls 原生 `Button`」这一族。不迁移 `TopToolButton`（标题栏与弹窗壳底栏）、
  `DataButton`（面板内小按钮）、`WinButton`（窗口控制）、`AddColumnButton`（加列）——它们本就是
  `Rectangle` 系主题化实现，各自尺寸与交互语义不同，硬并进来反而扩大回归面。
- 不动 `DialogShell.qml` 的结构与既有 `TopToolButton` 底栏，不改 `Theme.qml` / `Motion.qml` 的令牌定义。
- 不改任何 `onClicked` 里的业务逻辑，不改 `enabled` / `visible` 的判定条件，不改布局容器与 spacing。
  19 处替换只换类型、补 `tone` / `selected`、删掉重复的 `background` / `contentItem`。
- 不动 C++、数据库、任务卡治理脚本、CI workflow，不改 README 的界面行为描述（本次是纯视觉统一，
  快捷键与功能项没有变化）。
- 不新增静态检查脚本：裸 Button 归零由验收项里的 grep 判据确认，是否沉淀成常驻闸门另卡再议。

## 输入

基线 origin/main（61974cd），本地 main 与 origin/main 一致、工作区干净，远端 HTTPS 可达。
现场：`qml/components/TaskEditorDialog.qml` 134/161（已手写样式的页签）、415/441/574/590/672/677/
781/792/829/839/844（裸按钮，829-892 即截图红框）；`qml/components/CardDetailPanel.qml` 40/62；
`qml/components/ExecPanelDialog.qml` 33/40；`qml/components/NightBatchDialog.qml` 36；
`qml/components/ThemeMarketPanel.qml` 45。`CMakeLists.txt` 216-273 是 `QT_RESOURCE_ALIAS` 登记段、
275-332 是 `qt_add_qml_module` 的 `QML_FILES` 段，两处都要加新文件（缺别名会让组件脱离模块上下文）。
样式参考：`qml/components/TopToolButton.qml`（hover / emphasis 的单一实现）、
`qml/components/DialogShell.qml:262-279`（底栏主次操作）、`qml/theme/Theme.qml`（颜色与尺寸令牌）。
依赖：无。

## 产出

① `qml/components/NeonButton.qml`：基于 `Button` 的单一皮肤实现，`tone` / `selected` 两个语义属性，
   hover / pressed / disabled 三态，颜色全部经 `Theme.alpha` 与既有令牌；
② `CMakeLists.txt` 两处登记新组件；
③ 5 个存量 QML 文件共 19 处按钮改走 `NeonButton`，删掉重复的样式代码；
④ 实现提交带 `#<短号>`，合并提交 body 写全四段（变更／验收／影响／卡片），
   交付说明写回卡片 `note`，收尾由 `git-done.ps1` 自动结卡。
