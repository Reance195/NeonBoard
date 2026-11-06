---
state: proposed
stage: DX
title: 单次推送下 branch -d 恒拒：分支删除判据换锚点，收尾不再残留已合并分支
author: agent-c
evidence: DX-106 把 worktree 回收挪到推送之前（单次推送时序），回收安全判据换成了「已进本次合并」，但分支删除那半边没跟上：branch -d 的安全判据是「已合并到 upstream（refs/remotes/origin/main）」，推送前恒假，git 拒删并留「not fully merged」提示——DX-106 实测每次收尾都在本地残留一支已合并分支，需手工 branch -d 清理。
scope: scripts/git-done.ps1;docs/pitfalls.md
acceptance: 分支删除改判「分支提交已全部进合并提交」（与 worktree 回收同锚点）或推送落地后补删，正常收尾后本地不再残留已合并分支;-KeepBranch 与 -DryRun 行为不变：保留分支的路径不受影响;真实收尾端到端一次，验证收尾完成后 git branch 清单无本次分支残留;pitfalls 补现象/根因/解法条目;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: DX-106
suggestedId: 
---

## 目标
单次推送下 branch -d 恒拒：分支删除判据换锚点，收尾不再残留已合并分支

## 验收
分支删除改判「分支提交已全部进合并提交」（与 worktree 回收同锚点）或推送落地后补删，正常收尾后本地不再残留已合并分支;-KeepBranch 与 -DryRun 行为不变：保留分支的路径不受影响;真实收尾端到端一次，验证收尾完成后 git branch 清单无本次分支残留;pitfalls 补现象/根因/解法条目;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿

## 边界
scripts/git-done.ps1;docs/pitfalls.md

## 输入
基线 origin/main；依赖：DX-106

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
