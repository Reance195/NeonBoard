---
state: proposed
stage: DX
tier: P2
title: claim-card 落盘按脚本自身路径定位 store、is_anchor 却判 root：口径不一致让锚点领卡条目必写锚点
author: maintainer-session-2026-11-09
evidence: 维护者 2026-11-09 会话指令「为锚点口径不一致的根因立卡」（DX-149 机制债第四条）。实录三连：2026-11-09 MOD-10（agent-mod10）、MOD-11（agent-mod11）领卡与 DX-159 预开区，三者在锚点跑 agent-init 后锚点 docs/agent-assignments.json 均被写脏（领用条目落锚点工作树，挡锚点快进，本会话已三次手工还原）。根因：DX-158 的 is_anchor_checkout(root) 判的是 root 参数——而 agent-init 领卡语义是「在锚点建 .worktrees/<agent>」，claim-card 收到的 root = 新 worktree，is_anchor=False 不跳过；但 claim-card 内 _store 按 os.path.dirname(os.path.abspath(__file__)) 定位（脚本在锚点 scripts/），assignment-store 的 REPO_ROOT 亦按其自身路径解析 = 锚点 → 条目写进锚点。DX-158 selftest 三例全过但未覆盖「脚本在锚点检出、root 为 linked worktree」的组合（自测环境里脚本与检出同仓，暴露不了分离场景）。handler 侧已有前科族：pitfalls 1.31（.worktrees 只在锚点）、1.31/1.50（复制/镜像清单随依赖漂移）——本条补齐「脚本定位口径 ≠ 检出参数口径」一族。
scope: scripts/claim-card.py;scripts/assignment-store.py;docs/pitfalls.md
acceptance: assignment-store.py 新增 --root <检出路径> 覆盖 REPO_ROOT（缺省仍按脚本自身路径，向后兼容），selftest 覆盖该参数;claim-card 的 assignment upsert 一律显式传 --root <root>：落盘位置唯一由 root 检出决定，is_anchor(root) 判定保持不变;claim-card --selftest 新增第四例：构造「脚本在锚点检出、root 为其 linked worktree」的组合（临时仓：seed 为锚点、worktree add 派生、复制 claim-card/assignment-store 到锚点侧后以 root=worktree 调锚点侧脚本领卡），断言条目落 root 侧 docs/agent-assignments.json 且脚本侧（锚点）零写入;DX-158 既有三例（锚点 True / linked worktree False / 非检出保守 True）不回归;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
rejectReason: 
createdAt: 2026-11-09T22:30:00+00:00
---

## 目标
统一 claim-card assignment 落盘的定位口径：落盘位置唯一由 root 检出决定（领用现场随 root 检出的分支提交），消除「脚本定位口径 ≠ 检出参数口径」导致锚点领卡必脏锚点的缺陷（DX-158 只堵了 root 本身是锚点的形态，本卡堵「脚本在锚点、root 是 worktree」的第三形态）。

## 验收
见 frontmatter acceptance（7 条，分号分隔）。核心：--root 参数使落盘跟随 root；selftest 第四例以「脚本与检出分离」组合实证（这正是 DX-158 自测盲区）；既有判定不回归。

## 边界
见 frontmatter scope。硬约束：

- is_anchor_checkout(root) 的判定逻辑与语义不动（root 是锚点时仍跳过并告警）；改的是落盘定位。
- assignment-store.py 除 --root 外行为零变化；NB_ASSIGNMENTS_PATH 覆盖（selftest 既有机制）保持。
- 不改 handoff.ps1、git-done.ps1、台账结构、refs/claims 语义。
- 锚点现存状态已干净（2026-11-09 会话已三次还原），本卡无存量清理项。

## 输入
基线 origin/main。既有实现：claim-card.py 的 _store 定位（os.path.dirname(os.path.abspath(__file__))）与 is_anchor_checkout(root)（DX-158）；assignment-store.py 的 REPO_ROOT 按脚本自身路径解析与 NB_ASSIGNMENTS_PATH 覆盖。实录见 evidence（三次锚点脏改 + 手工还原记录在本会话）。同族前科：pitfalls 1.31 / 1.48 / 1.50。

## 产出
① assignment-store.py --root 参数 + selftest；② claim-card 传参改造 + selftest 第四例；③ pitfalls 条目（1.51 或并入 1.50 族）；④ 合并提交 body 四段，验收记录进卡片 note。
