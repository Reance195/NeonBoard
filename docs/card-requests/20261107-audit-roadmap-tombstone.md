---
state: proposed
stage: DX
tier: P3
title: RoadmapSync 墓碑误删：卡的「家」迁移被当成删除，看板卡片永久丢失
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计（亲验复现链路）。src/board/RoadmapSync.cpp:499-503 导出侧 scoped 过滤只把「家在本文件」的卡写进 seen；:546-560 墓碑循环把 current 里不在 seen 的条目一律写 deleted:true 且 updatedAt=now；:337-342 importEntry 见 deleted 即 deleteTask，不看该卡现在的家在哪个文件；:320-326 shouldApply 门因墓碑 updatedAt 恒新于看板而放行。触发链全自动：把卡从绑定项目解绑（或指派给绑定项目）→ 1.5s 防抖导出在项目文件写墓碑 → 15s 轮询导入 → deleteTask 把看板上的卡删掉 → 下一轮导出把全局文件也写成墓碑——卡在看板与两个快照文件中永久消失，且 taskRemoved 连带清掉父子关联边（TaskTree::removeLinkFor）。audit 同时确认：多项目绑同一路径时互相把对方的家写墓碑，同根问题。
scope: src/board/RoadmapSync.cpp;src/board/RoadmapSync.h;tests/**;docs/pitfalls.md
acceptance: tests/tst_roadmapsync.cpp 新增用例：卡归属从绑定项目文件迁到全局文件后，项目文件的墓碑条目导入不再删除看板卡（先复现原缺陷断言删卡，再断言修复后卡片仍在且计为冲突/跳过）;新增用例：真删除链路不回归——卡在看板被删后其家所在文件的墓碑导入仍正常对账（deleted 计数正确、不误伤他卡）;新增用例：两个项目绑定同一路径时，各自的墓碑互不删除对方家的卡;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）
depends:
---

## 目标

给 importEntry 应用 deleted 前补一道「家的域校验」：用与导出侧完全相同的 homeIds 判据（Z5，同一实现不复制）确认「该卡的家确实是本文件」才执行 deleteTask；家在别处（或已不在本文件域）的墓碑按冲突跳过并计数，不再吃掉正常迁移的卡。

## 验收

见 frontmatter acceptance（4 条）。口径：ctest 用例先复现原缺陷（修复前红）再断言修复；全量 verify 以退出码为准。

## 边界

- 不改 progress.json v3 快照格式、不改墓碑写盘协议（导出侧行为不动——墓碑仍写，只是导入不再误执行）。
- 不动 RoadmapSync 的防抖/轮询时序与 baseline 对账协议。
- 域判据必须与导出侧同源（同一 homeIds 计算），不得出现第二份口径。

## 输入

基线 origin/main（a63ccbc）。审计现场：上述 file:line；tst_roadmapsync.cpp 既有用例骨架（既有 scoped 导出测试可参照）。依赖：无。

## 产出

① importEntry 墓碑域校验；② tst_roadmapsync 三组用例（迁移不删/真删不回归/双项目互不误删）；③ pitfalls 沉淀「判定凭内容：作用域只在导出侧生效等于没有作用域」。
