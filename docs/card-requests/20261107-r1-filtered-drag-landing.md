---
state: closed
card: R1-8
stage: R1
tier: P3
title: 过滤态拖拽落点按可见顺序计算：搜索 / 阶段过滤下指示线与落库顺序一致
author: scheduled-direction-cron-2026-11-07
evidence: README「已知限制」第 2 条（2026-11-07 对现行 main 核实）：「搜索或按阶段过滤时，拖拽落点按未过滤的列内顺序计算」。成因：R1-1（moveTaskBefore + taskIdsInStatus，全列 order_key 口径）落地时未定义过滤态语义，落点索引按未过滤序列换算；R8-05 拖拽手感（涟漪 / 光轨 / 磁吸，109ef92）只做视觉层。过滤激活时霓虹指示线画在可见卡之间，落库却按全列序列换算，两者可错位——卡片实际落点偏离指示线所示的空档，这是看板核心交互的正确性缺陷。台账覆盖检查（2026-11-07）：「拖拽落点 / 过滤落点」关键词零命中，R1-1 与 R8-05 均 done 且验收不含过滤态语义，无在办卡或 claim 占用同方向。
scope: src/task/TaskFilterModel.cpp;src/task/TaskFilterModel.h;src/task/TaskModel.cpp;src/task/TaskModel.h;src/shell/DragFeel.cpp;src/shell/DragFeel.h;qml/components/BoardColumn.qml;qml/components/TaskCard.qml;tests/;README.md;docs/pitfalls.md
acceptance: 新增或扩展 ctest 用例覆盖过滤态落点映射：搜索过滤与阶段过滤激活时，drop 目标索引按可见序列计算并映射回全局 order_key（落在可见前驱之后，不因不可见卡错位），断言落库顺序与可见顺序一致，ctest 全绿;未过滤回归：拖拽落点行为与现状一致，R1-1 的 moveTaskBefore「插到目标卡之前」语义不变（Z3），既有 ctest 全绿;UI 不回归：--smoke 退出码 0 且日志无致命 QML 告警（沿用既有 check_qml_warnings 判据）;README「已知限制」中「搜索或按阶段过滤时，拖拽落点按未过滤的列内顺序计算」一条移除或改写为已修复口径;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）
depends: 
suggestedId: 
createdAt: 2026-11-07T04:55:00+00:00
---

## 目标

把「过滤态拖拽落点」从 README 已知限制变成已定义语义：过滤（搜索词或阶段）激活时，
drop 落点按**可见序列**计算再映射回全局 order_key——指示线画在哪两张可见卡之间，
卡就落在那里；未过滤时行为与现状完全一致。

## 验收

见 frontmatter `acceptance`（5 条）。核心是第 1 条的模型层映射用例（可见前驱对齐、
不可见卡不参与索引换算），第 2 条守住 R1-1 老语义不回归（Z3）。

## 边界

src/task/（TaskModel / TaskFilterModel 的落点换算）、src/shell/DragFeel.* 与
qml/components/BoardColumn.qml / TaskCard.qml（拖拽指示线与落点采集）、tests/、README.md、
docs/pitfalls.md。不改 TaskModel::moveTask 的既有语义（Z3），不动 src/board/BoardColumns.*
（列结构层，预期无需改动，确需越界即停并说明，Z11）。

## 输入

基线 origin/main（7807a7f）。R1-1 既有口径见 roadmap §4「列内精确插位」行；
R8-05 手感层见 §9.4；README「已知限制」第 2 条为待修口径。

## 产出

合并提交 body 四段（变更 / 验收 / 影响 / 卡片）；README 已知限制对应条目回收；
过程踩坑（若有）补 docs/pitfalls.md。
