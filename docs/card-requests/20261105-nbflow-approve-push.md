---
state: approved
card: DX-45
stage: DX
title: nbflow approve 收尾推分支撞 pre-push 规则 1，立卡提交只能手工 git-done 绕开
author: agent
evidence: 本会话 nbflow approve --cards DX-41 时：board-apply.py 落 commit 91a2a15 成功，紧接 git -C <worktree> push -u origin chore/board-a-6-replay 被 pre-push 规则 1 拒（分支尚未合并进 main，禁止推送），nbflow 退出码 2 停在 created。绕开方式：手工 git-done.ps1 -Branch <boardBranch>，杂务 diff 自动豁免 claim 闸门。修向：approve 收尾直接调 git-done，别再自己 push。
scope: scripts/nbflow.py
acceptance: scripts/nbflow.py cmd_approve 里不再有独立的 git push -u origin <branch> 步骤；收尾统一交给 scripts/git-done.ps1 -Branch <boardBranch>（remote 模式：合入 origin/main + 快进推 + 删分支）; python scripts/nbflow.py --selftest 全绿；一次真实沙箱立卡（新批次）跑到 state=created，git log origin/main 能看到 chore(board) 立卡提交合入
depends: 
suggestedId: DX-45
rejectReason: 
createdAt: 2026-11-04T20:10:16+00:00
---

## 目标
nbflow approve 收尾推分支撞 pre-push 规则 1，立卡提交只能手工 git-done 绕开

## 验收
scripts/nbflow.py cmd_approve 里不再有独立的 git push -u origin <branch> 步骤；收尾统一交给 scripts/git-done.ps1 -Branch <boardBranch>（remote 模式：合入 origin/main + 快进推 + 删分支）; python scripts/nbflow.py --selftest 全绿；一次真实沙箱立卡（新批次）跑到 state=created，git log origin/main 能看到 chore(board) 立卡提交合入

## 边界
scripts/nbflow.py

## 输入
基线 origin/main；依赖：

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
