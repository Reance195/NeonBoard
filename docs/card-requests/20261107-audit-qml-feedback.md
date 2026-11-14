---
state: closed
card: DX-137
stage: DX
tier: P3
title: QML 反馈链三处：QString.isEmpty() 必抛 TypeError、逾期判定跨年失效、热力图永不重绘
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计（五处调用点亲验）。qml/components/SettingsPanel.qml:546、556、640、650、682 对 DataPort 返回的 QString（进 QML 后是 JS 原始字符串）调用 .isEmpty()——JS 无此方法必抛 TypeError，导出/导入/备份五处提示永远不刷新而 C++ 侧其实成功。qml/components/TaskCard.qml:98-104 逾期判定用 MM-dd 字符串比较，跨年整年失效（due "12-31" 在次年任意日期都判不逾期），TaskModel 已有 DueRole（src/task/TaskModel.h:35）可按 epoch 比较。qml/components/ActivityHeatmap.qml 网格 Canvas 的 onPaint 读 cells 但无 onCellsChanged → requestPaint，唯一重绘入口是主题 accent 变化——完成卡当天格子不变，与 OnboardingOverlay「完成时间自动进入统计热力图」的承诺矛盾（同文件吞吐曲线写法正确，可对照）。
scope: qml/components/SettingsPanel.qml;qml/components/TaskCard.qml;qml/components/ActivityHeatmap.qml;tests/qml/**;tests/CMakeLists.txt;scripts/check_qml_pitfalls.py
acceptance: scripts/check_qml_pitfalls.py 新增规则：拦截对 JS 字符串调用 .isEmpty() 的写法（QString 返回值进 QML 即 JS 字符串），qml/ 全目录扫描通过（本轮五处清零且后续回归被 CI 拦）;tests/qml 新增用例：TaskCard 以跨年 dueAt 断言 overdue 为 true、同年未来日期为 false;tests/qml 新增用例：ActivityHeatmap cells 属性变更后重绘被调度（以组件内可观测计数或 onPaint 触发为据）;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0，qml 用例 tst_taskcard_actions 全绿）
depends:
---

## 目标

修三处「界面反馈失灵」并按 Z6 把可机判的沉淀成判据：isEmpty 五处改等价 JS 语义（=== ""）；逾期判定改用模型已有的 dueAt epoch 与 now 比较（带年份，跨年正确）；热力图网格补 onCellsChanged → requestPaint（对照同文件吞吐曲线的既有正确写法）；check_qml_pitfalls.py 加 isEmpty 误用规则让这类跨语言边界错误从此由 CI 拦。

## 验收

见 frontmatter acceptance（4 条）。口径：lint 规则以全目录扫描退出码为准；QML 行为以 qmltestrunner 用例为准；全量 verify 以退出码为准。

## 边界

- 不改 TaskModel 角色/信号（DueRole 已存在，纯 QML 修复，不碰 C++）。
- 不动 DataPort 返回签名（QString 保持，修复在调用侧）。
- 主题令牌纪律不变：本卡不引入任何硬编码颜色。
- lint 规则只拦「.isEmpty() 于 JS 字符串」这一实证坑，不扩大成风格检查。

## 输入

基线 origin/main（a63ccbc）。审计现场：上述 file:line；tests/qml/tst_taskcard_actions.qml 既有用例与 qmltestrunner 接线（tests/CMakeLists.txt:556）。依赖：无。

## 产出

① 五处 isEmpty 修复 + 逾期 epoch 化 + 热力图重绘；② check_qml_pitfalls.py 新规则；③ tests/qml 两组新用例；④ pitfalls 沉淀「QString 进 QML 即 JS 字符串」判据。
