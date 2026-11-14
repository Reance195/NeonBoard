---
state: closed
card: DX-129
stage: DX
tier: P3
title: 弃单终态投影快照 deleted 墓碑：看板不再显示已弃单卡，重开自动恢复
author: maintainer-session-2026-11-07
evidence: DX-43/R0-12 台账已 abandoned（5108e69）、DX-119 已结案（4113d21），快照 progress.json 仍显示 todo——DX-111 设计「弃单终态只活台账、派生器整段跳过」防重领已收口，看板展示残留未收；快照 v3 已有 deleted:true 墓碑约定（AGENTS §11.2.5「移除的卡写 deleted 而不是删条目」），应用 RoadmapSync 导入对 deleted:true 即删看板任务、写回保留墓碑（src/board/RoadmapSync.cpp 337/556）；派生器对弃单卡整段 continue ⇒ 墓碑写入后不会被复活，pre-push 闸门（卡片数不减/code 不丢）照常满足
scope: scripts/derive_board_status.py;docs/pitfalls.md
acceptance: ①派生器 --write 对 baseline=abandoned 的卡把快照条目置 deleted:true（幂等，已是 true 不再动），status 等其余字段不改、不投影三态；②重开对称：baseline 经维护者 chore(board) 改回非终态后，--write 恢复该条目 deleted:false 并恢复正常状态投影（updatedAt 抬高）；③--check 同口径：弃单+deleted:false 报漂移、弃单+deleted:true 与重开+deleted:false 不报；④--abandoned-codes 输出行为不变；⑤derive --selftest 增弃单投影/重开恢复/--check 口径用例全过，task_gate --selftest 不回归；⑥存量 DX-43/R0-12/DX-119 经收尾 --write 后快照 deleted:true 生效，推送过 pre-push 规则 4（version≥3/卡片数不减/code 不丢），pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
suggestedId:
---

## 目标
DX-111 把弃单终态收进台账后，展示面留下残留：快照三态（todo/doing/done）装不下「弃单」，派生器对弃单卡整段跳过，DX-43/R0-12/DX-119 在看板上永远显示 todo。快照 v3 的 deleted:true 墓碑约定正是「卡从看板退役」的现成表达：把弃单终态投影为墓碑即可零应用改动收敛展示残留，并与「重开必须由维护者 chore(board) 授权改回」对称——重开时恢复 deleted:false，卡自动回看板。

## 验收
见 frontmatter `acceptance` 6 条。口径：以 `python scripts/derive_board_status.py --selftest / --check / --write` 产物、pre-push 规则 4 与 `pwsh -File scripts/verify.ps1 -ScriptOnly` 退出码断言（Z8），不写绝对性能值。

## 边界
- 只改派生器的投影判定；不动应用（RoadmapSync/TaskModel）、不动 task_gate 审计通道、不改快照 `version` 与字段结构。
- 弃单判定唯一口径仍是 `baseline=abandoned`（大小写不敏感，DX-111），不新增状态字符串。
- 台账 baseline 归维护者：本卡不改任何卡的 baseline，只改派生行为；弃单结案的 chore(board) 审计通道不变。
- 重开恢复的触发只来自 baseline 变更（维护者授权动作），派生器不引入任何自动重开。

## 输入
基线 `origin/main`。既有事实：DX-111 已落地（492ccb1）；三张存量弃单卡 DX-43/R0-12/DX-119；`derive_board_status.py` write 路径对弃单卡 `continue`（约 1310 行）。依赖：无。

## 产出
①派生器弃单投影 + 重开恢复 + selftest 用例；②docs/pitfalls.md 补条目；③`verify.ps1 -ScriptOnly` 全绿；④git-done 收尾自动把三张存量弃单卡快照刷成墓碑（Sync-BoardSnapshot 走 --write）。
