---
state: proposed
stage: DX
title: git-done 从自身 worktree 收尾后残留 detached 工作区：反查失效且无回收闭环
author: maintainer-session-2026-11-06
evidence: 2026-11-06 DX-109 收尾实测：agent-init 开区 .worktrees/agent-dx109 / fix/dx109-branch-delete-anchor，从该 worktree 内跑 git-done.ps1 -Branch fix/dx109-branch-delete-anchor -Close DX-109，EXIT=0（合并 d4fc266、单次推送 d5b0f63、分支已删、DX-109 baseline=done），但 git worktree list 仍列 E:/projects/NeonBoard/.worktrees/agent-dx109 d5b0f63 (detached HEAD)，输出里既无「回收 worktree」也无「目标 worktree 就是当前目录，跳过回收」提示。根因链（origin/main 版 scripts/git-done.ps1）：:783 先 switch -c merge/<slug> origin/main 把当前 worktree 从目标分支摘走 → :843-851 第 4 步才按 branch refs/heads/<Branch> 反查 worktree，此时已无 worktree 检出该分支，$wtPath 为空 → :860 回收段 elseif ((-not $KeepWorktree) -and $wtPath) 整段静默跳过 → :1147 归位块 if ($atAnchor) 不适用于 linked worktree。:1143-1147 注释写「开发工作区保持分离 HEAD：下一步多半就被回收」，与实测不符；AGENTS.md:473「worktree 用完必须回收」与 §7.3「agent 在自己的 worktree 里跑 git-done」在这一姿势下同时落空。DX-21 [done] 把「当前目录就是目标 worktree 时不尝试自删」写进验收项，DX-23 巡检与 §7.10 均只报告不删除，无自动闭环；本次只能人工 git worktree remove 收敛。
scope: scripts/git-done.ps1;docs/pitfalls.md
acceptance: 端到端真实收尾（agent-init 开区 → 提交 → 从该 worktree 内跑 git-done.ps1 -Branch <本分支>，remote 模式 EXIT=0）后 git worktree list --porcelain 不含该 worktree 路径（产物断言，不以脚本是否打印「已回收」为准）;回收只发生在单次推送落地之后，且只收「进入时检出该分支、现已 detached 于本次合并结果、工作区干净、无未进本次合并的提交、占用登记属于自己的那一份」；任一条件不满足时只告警不删，并打印可复制的 git worktree remove <绝对路径> 与原因（打桩负测断言退出码与输出）;从其它检出调用 git-done.ps1 -Branch <别的分支> 时 DX-21 既有默认回收不回归（端到端一次：目录消失、分支已删、占用登记无残留）;-DryRun 不删 worktree、不切走占用，结束仍回到进入时分支（DX-50 时序不回归）;收尾回执仍落锚点区 .sandbox/receipts/ 且可读取，回收 worktree 不带走物证（DX-34 不回归）;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿；docs/pitfalls.md 补现象/根因/解法条目（含「进程 CWD 所在目录不能自删，须在推送落地后由外部检出执行或登记延后回收」）
depends: DX-21;DX-26;DX-34;DX-109
suggestedId: 
---

## 目标

让「从自身 worktree 里跑 git-done 收尾」这条宪章 §7.3 的标准姿势也能走完回收闭环：收尾成功后不留一个干净的 detached 工作区在 `.worktrees/` 下，也不留一句要人自己猜的静默跳过。难点不是删目录，而是**进程 CWD 就在待删目录里**——Windows 不允许自删，所以回收必须发生在推送落地之后、由外部检出执行（或登记成延后待办由下一次收尾消费），且不能误删别人正在办的工作区。

## 验收

见 frontmatter `acceptance`（6 条）。四条硬约束：

- 以**产物**验收：`git worktree list --porcelain` 与 `check-worktree-activity.ps1 -Audit` 的实际输出，不看脚本有没有打印「已回收」（Z8）。
- 必须覆盖**成功路径**：DX-78 只管 non-ff 失败后检出留在临时分支，本卡管 EXIT=0 之后。
- 两条既有路径都不许回归：DX-21 从其它检出的默认回收、DX-34 回执落在锚点区。
- 负测要能拦住误删：脏工作区 / 有未进本次合并的提交 / 占用登记属于别人时，只告警并给出可复制命令。

## 边界

- **会改写 DX-21 的一条验收项**：「当前目录就是目标 worktree 时不尝试自删」需改为「推送落地后由外部检出回收自己那一份，或登记延后回收」。实施时同步更新 `docs/pitfalls.md` 与 `git-done.ps1` 相关注释，不留自相矛盾的口径。
- **不改** `AGENTS.md` §7.10「批量陈旧工作区回收由人拍板」：`check-worktree-activity.ps1 -Audit` 仍是只报告不删除、恒退 0；本卡只收口「本次收尾自己那一份」。
- **不改** §7.1 锚点快进 fail-safe（那是遗留 1，归 DX-101）、DX-106 单次推送时序、DX-109 的 `Remove-MergedBranch` 判据、DX-26「分离 HEAD 上做三步收尾提交」的时序。
- **只动** `scripts/git-done.ps1` 与 `docs/pitfalls.md`：不动 `scripts/verify.ps1` / `scripts/check_board_diff.py`（在办卡 DX-77 / DX-88 / DX-101 持有），回归断言落在 git-done 自身输出与 `git worktree list` 产物上；实施前与 DX-78 排先后顺序，避免并发改同一文件。
- 不改台账结构、不改 `baseline` / `claims` 写权、不加新依赖。

## 输入

基线 `origin/main`（本次现场已落在 d5b0f63）。证据与行号链见 frontmatter `evidence`。依赖 DX-21（按分支反查 worktree）、DX-26（分离 HEAD 时序）、DX-34（回执落锚点区）、DX-109（单次推送下删分支判据，本次现场卡）。

## 产出

① `git-done.ps1` 收尾末尾的回收闭环（进入时记住自身 worktree 路径 + 推送落地后判定 + 外部执行或延后登记），或等价的显式待办回执/输出；② `docs/pitfalls.md` 一条现象/根因/解法；③ 一次真实端到端收尾的前后 `git worktree list` 对比记录；④ 负测（脏工作区不删、DryRun 不删）留痕。立卡与实现分开两条分支、两段提交（宪章条款 6）。
