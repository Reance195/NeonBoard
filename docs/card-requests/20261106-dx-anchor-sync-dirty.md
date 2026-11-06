---
state: proposed
stage: DX
title: 锚点快进永久跳过：立卡 roadmap 行收进立卡提交，锚点同步脏文件判据与分类修正
author: agent-c
evidence: 2026-11-06 实测：锚点区 docs/iteration-roadmap.md 因 R19 行长期处于未提交修改态，git-done 锚点快进被连续跳过，本地 main 严格落后 origin/main（origin/main..main 为空但落后一串收尾提交）。根因链三条叠加：① 立卡工具对陌生 stage 强制「roadmap 有对应行」，却没有任何受控通道落这行——立卡会话（agent-r19，R19-1）把它写进锚点工作区，而锚点 pre-commit 禁 main 提交，该行永远无法入库；② Sync-AnchorWorktree 只 stash/还原 progress.json（git-done.ps1:383），遇其它已跟踪文件脏即整体放弃快进；③ 跳过原因分类（git-done.ps1:426）把「已跟踪文件本地修改、本地零提交」误报为「本地提交 / 未跟踪文件冲突」，误导排查（当日两个会话先后被带偏）。
scope: scripts/register_board_cards.py;scripts/git-done.ps1;docs/pitfalls.md;AGENTS.md
acceptance: register_board_cards.py 在 stage 有陌生取值需要 roadmap 行时，把该行并入立卡提交（docs 路径豁免闸门），不再要求立卡会话在检出工作区手工补行——新立含新 stage 的卡跑通一次端到端：立卡提交含 roadmap 行、锚点不再因它留脏;Sync-AnchorWorktree 跳过原因精确分类：本地提交 / 已跟踪文件脏（逐个点名文件）/ 未跟踪冲突三种口径分开报，不再一律报「本地提交 / 未跟踪文件冲突」;Sync-AnchorWorktree 对「仅 iteration-roadmap.md 脏」的场景按 progress.json 同款 stash/还原/主线优先口径处理（工具产物与其它文件仍跳过不碰）;pitfalls 补根因链与实录;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: DX-108
suggestedId: 
---

## 目标
锚点快进永久跳过：立卡 roadmap 行收进立卡提交，锚点同步脏文件判据与分类修正

## 验收
register_board_cards.py 在 stage 有陌生取值需要 roadmap 行时，把该行并入立卡提交（docs 路径豁免闸门），不再要求立卡会话在检出工作区手工补行——新立含新 stage 的卡跑通一次端到端：立卡提交含 roadmap 行、锚点不再因它留脏;Sync-AnchorWorktree 跳过原因精确分类：本地提交 / 已跟踪文件脏（逐个点名文件）/ 未跟踪冲突三种口径分开报，不再一律报「本地提交 / 未跟踪文件冲突」;Sync-AnchorWorktree 对「仅 iteration-roadmap.md 脏」的场景按 progress.json 同款 stash/还原/主线优先口径处理（工具产物与其它文件仍跳过不碰）;pitfalls 补根因链与实录;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿

## 边界
scripts/register_board_cards.py;scripts/git-done.ps1;docs/pitfalls.md;AGENTS.md

## 输入
基线 origin/main；依赖：DX-108（stash 泛化沿用其派生物口径与冲突善后语义）

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）；锚点现存 R19 残留行由 R19-01 实现分支收编（不在本卡 scope）
