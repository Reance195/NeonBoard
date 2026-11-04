---
state: approved
card: DX-47
stage: DX
title: nbflow 请求单与 handoff.json 落工作树，挡住 git-done §11.2 未提交只允许是快照的 pre-check
author: agent
evidence: 本会话合并 DX-41 前：锚点工作树出现未跟踪 docs/card-requests/20261105-board-tip-only-check.md（另一会话留下），git-done 报「当前工作区有未提交改动（progress.json 之外）」退出码 1。我把文件 park 到 .git/neonboard-flow/parked/ 才跑通，合完再放回原位。同类：agent-dx41 worktree 根留下未跟踪 handoff.json，git-done 报「跳过清理 ... 1 项未提交改动」，branch 删不掉。AGENTS.md §7.6 明确「会话产物只往 NB_SESSION_ARTIFACT_DIR 写」，§11.2 第 6 条只放行 progress.json。修向：请求单改落 .git/neonboard-flow/requests/*.md，或在 approve 里把当批请求单合入 chore(board) 提交；handoff.json 改落 <worktree>/.sandbox/artifacts/handoff.json。
scope: scripts/nbflow.py
acceptance: scripts/nbflow.py REQUEST_DIR 与 handoff_path 都不再指向工作树；handoff 落 <worktree>/.sandbox/artifacts/handoff.json，请求单落 .git/neonboard-flow/requests/*.md（approve 时把当批请求单一次性合入 chore(board) 提交的分支也算通过，二选一由实施者决定）; python scripts/nbflow.py --selftest 补一条「产物路径不越出 .git / .sandbox」用例并全绿；完整跑一次 authorize→request→approve→dispatch→close，锚点 git status --porcelain 只剩 progress.json，agent worktree 只剩沙箱目录内的产物
depends: 
suggestedId: DX-47
rejectReason: 
createdAt: 2026-11-04T20:10:30+00:00
---

## 目标
nbflow 请求单与 handoff.json 落工作树，挡住 git-done §11.2 未提交只允许是快照的 pre-check

## 验收
scripts/nbflow.py REQUEST_DIR 与 handoff_path 都不再指向工作树；handoff 落 <worktree>/.sandbox/artifacts/handoff.json，请求单落 .git/neonboard-flow/requests/*.md（approve 时把当批请求单一次性合入 chore(board) 提交的分支也算通过，二选一由实施者决定）; python scripts/nbflow.py --selftest 补一条「产物路径不越出 .git / .sandbox」用例并全绿；完整跑一次 authorize→request→approve→dispatch→close，锚点 git status --porcelain 只剩 progress.json，agent worktree 只剩沙箱目录内的产物

## 边界
scripts/nbflow.py

## 输入
基线 origin/main；依赖：

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
