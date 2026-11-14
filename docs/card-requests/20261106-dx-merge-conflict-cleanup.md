---
state: closed
card: DX-107
stage: DX
title: 合并冲突退出不回滚现场：冲突路径清理临时分支与冲突态，dry-run 承诺的回滚真正兑现
author: agent-c
evidence: DX-106 收尾实测两次撞 progress.json 合并冲突：git-done 冲突路径直接 exit 1，现场残留 merge/<slug> 临时分支与未合并冲突态，worktree 变脏、后续命令全部撞墙；dry-run 更矛盾——头部文档承诺「结束切回原分支、演练提交全部丢弃」，冲突退出同样不兑现，本次实测留下 merge/chore-board-dx-106-zip-push 残留需手工 merge --abort + branch -D。
scope: scripts/git-done.ps1;docs/pitfalls.md
acceptance: dry-run 撞合并冲突：退出前 abort 合并、删 merge/* 临时分支、切回进入时检出，工作区恢复干净;真实模式撞合并冲突：同样不残留 merge/* 临时分支与冲突态，且不吞掉用户未提交改动;verify.ps1 -GitDoneDryRun 在冲突分支上演练不再留现场（回归用例）;pitfalls 补现象/根因/解法条目;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: DX-106
suggestedId: 
---

## 目标
合并冲突退出不回滚现场：冲突路径清理临时分支与冲突态，dry-run 承诺的回滚真正兑现

## 验收
dry-run 撞合并冲突：退出前 abort 合并、删 merge/* 临时分支、切回进入时检出，工作区恢复干净;真实模式撞合并冲突：同样不残留 merge/* 临时分支与冲突态，且不吞掉用户未提交改动;verify.ps1 -GitDoneDryRun 在冲突分支上演练不再留现场（回归用例）;pitfalls 补现象/根因/解法条目;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿

## 边界
scripts/git-done.ps1;docs/pitfalls.md

## 输入
基线 origin/main；依赖：DX-106

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
