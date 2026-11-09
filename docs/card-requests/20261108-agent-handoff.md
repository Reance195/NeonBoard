---
state: proposed
stage: DX
tier: P1
title: 在办卡僵尸占位无接手通道：补判活事实源与显式授权的原地接手
author: maintainer-session-2026-11-08
evidence: 维护者 2026-11-08 会话指令「1服用，2都可以，3，独立」（1=复用旧 worktree 保未提交改动；3=独立事实源不写进台账 claims；2=授权通道不挑，由实现方按 fail-closed 定）。现场实测：origin 挂 8 条 refs/claims（DX-138~143 / DX-146 / MOD-7），origin/main 台账这 8 张卡 baseline 全为 todo；agent-fxa/fxb/fxc/fxd（DX-139/140/142/143）4 个 worktree 各只有一条 chore(claim) 提交、占用登记自 2026-11-07 17:43 起约 28 小时无实现；DX-138/DX-141 占位尚存而 worktree 已不存在；全仓 .worktrees/ 下无任何 handoff.json，nbflow state.json 里这些卡 nextStep 全空。死锁根因：refs/claims 非强推原子占位（claim-card.py:122），agent-init.ps1:348-363 见 ref 存在即 exit 3；释放口 git-done.ps1:688-700 要求「占位已合入 main 或认领分支已删」，check_claim_refs.py:138-139 classify_claim 只认 baseline=done，claim-card.py:184-196 claim_is_stale 要求「分支已删且占位已释放」——在办卡三条全不满足，claim 记录又只有 agent/branch/head/claimedAt 四字段（claim-card.py:266-271）无心跳无 nextStep。设计稿 docs/module-split-plan.md:798-879 §7.2 已完整给出 5 字段、接手协议 6 步与载体选型，状态为「待拍板 P7」未落地。拍板项 2 维护者补充澄清（2026-11-08）：非「超时自动放权」——agent 一律运行于 bypass permissions on 模式，授权不设交互式审批门，凭会话内指令执行、由命令凭证留痕审计。
scope: docs/agent-assignments.json;scripts/assignment-store.py;scripts/handoff.ps1;scripts/claim-card.py;scripts/check_claim_refs.py;scripts/agent-init.ps1;scripts/agent-status.ps1;scripts/agent-workspace.ps1;scripts/git-done.ps1;scripts/nbflow.py;.githooks/post-commit;docs/module-split-plan.md;docs/pitfalls.md;docs/charter-appendix.md
acceptance: python scripts/assignment-store.py selftest 全过（upsert / heartbeat / remove / list / stale 幂等、文件锁、写回格式稳定）;python scripts/check_claim_refs.py --selftest 全过且新增 handoff verdict 用例（未 done + 占位超阈值无活动引用 + 分支最后提交超阈值 → 点名；离线或不可判定 → skip；活动卡与 done 卡原判据不回归）;python scripts/check_claim_refs.py --audit-handoff 与 pwsh -File scripts/agent-status.ps1 的可接手清单能区分「活动 / 疑似中断 / 孤儿占位」三类，逐卡给出 code / baseline / branch / 最后提交时间 / 陈旧时长 / nextStep / 建议命令，且不误点名 2026-11-08 新领的 DX-146 与 MOD-7;pwsh -File scripts/handoff.ps1 -Card <code> -ToAgent <agent> -AuthorizedBy <来源> -DryRun 只输出将执行的步骤、不改任何 ref / 文件 / 占用登记；缺 -AuthorizedBy 或 origin 不可达时以非 0 退出且不动服务端占位；全程非交互、无任何确认提示（bypass permissions 模式可直接调用）;在 handoff.ps1 -Selftest 自建的临时 git 仓库里实跑一次接手：删旧 refs/claims/<code> → 不删分支、不删 worktree、未提交改动保留 → 原地改写 .agent-env.ps1 的 NEONBOARD_AGENT_ID、释放 worktree:<old> 占用并登记 worktree:<new> → 追加主题严格为「chore(claim): 领取 #<code> 实施任务」的提交（「接手自」写进 body）→ claim-card.py 原子重推占位成功，python scripts/check_claim_refs.py --base origin/main --head HEAD 放行;接手产物 .sandbox/artifacts/handoff.json（复用 nbflow.py:842 路径约定）含相对 origin/main 的 diff 文件清单、最近 5 条提交、git status --porcelain 未提交清单、卡 acceptance 清单、旧 nextStep；旧 nextStep 为空时仍生成且字段齐全;任一步失败按记录的旧占位 sha 恢复 refs/claims/<code>（Z13 可重入），重跑 handoff.ps1 幂等收敛、不产生重复 claim 提交、不重复删 ref;post-commit 只在 stage 变化 / 完成一项 acceptance / 显式 checkpoint 时刷新 updatedAt 与 nextStep（不每次提交刷台账）；nbflow.py report、git-done.ps1 收尾、nbflow.py abandon 分别 remove 对应 assignment；docs/agent-assignments.json 只由 scripts/assignment-store.py 写，不进 progress.json、不进 board.db、不参与 refs/claims 防双领判定;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
rejectReason: 
createdAt: 2026-11-08T22:00:00+00:00
---

## 目标
给「agent 做到一半没后续」的在办卡开一条接手通道：先用独立事实源把「谁在跑 / 跑到哪 / 下一步做什么 / 是否还活着」变成可判定的事实，再以一次显式授权的动作完成「释放旧占位 → 复用旧 worktree 与分支原地换身份 → 原子重领」，全程保留未提交在制品。恢复 §7.2 设计稿里「断了谁接」的能力，同时不削弱现有防双领闸门。

## 验收
见 frontmatter acceptance（9 条，分号分隔），分两组：

- **观测层（第 1~3、8、9 条）**：事实源 selftest、handoff verdict 不回归、可接手清单能区分三态且不误点名活动卡、心跳频次与唯一写者、ScriptOnly 全绿。
- **动作层（第 4~7 条）**：DryRun 与 fail-closed、临时仓库实跑接手全链路、残局摘要字段齐全、失败回滚与幂等。

第 4 条守 Z4（授权缺失/离线一律失败且不动服务端），第 5 条守 Z3/Z8（主题冻结、以产物验收而非以动作验收），第 7 条守 Z13（任意一步死掉后原样重跑收敛），第 8 条守 Z5（单写者、不新增第二份防双领事实源）。

## 边界
见 frontmatter scope。硬约束：

- **判活与授权口径（拍板项 2 定稿）**：执行环境为 bypass permissions on——**接手不设交互式审批门**，`handoff.ps1` 全程非交互（不 Read-Host、不等确认），供 agent 在 bypass 模式下直接调用。闸门是程序化的：命令必须带 `-AuthorizedBy` 写明授权来源（维护者会话内指令原文）并落回执，同 `register_board_cards.py` 的凭证约定，防无凭证免责；授权判定留给事后复核与审计留痕，不做权限弹窗。超时**只点名告警、绝不自动转移**（Z4；维护者已明确排除超时自动放权）。双档阈值沿用既有口径：30 分钟（`check-worktree-activity.ps1`）判「疑似中断」、240 分钟（`agent-workspace.ps1 -StaleMinutes`）判「陈旧占用」，两档都只产出清单。origin 不可达时不接手（离线 fail-closed）。台账策略授权通道保留接口但本卡**不实现**无人值守自动放权（策略属任务定义，agent 不得改）。
- **载体（拍板项 3）**：`docs/agent-assignments.json` 独立文件、按卡号索引、随分支提交（跨机/跨会话可见，与 `docs/board-ledger.json` 同治理风格）；合并冲突照台账既有解法「取主线版重插自己的条目」处理。唯一写者是 `scripts/assignment-store.py`（Z5，同 `task_gate.py` 的单一实现约定），其余脚本只调它的子命令。它**只承载判活与上下文，绝不作为防双领依据**；防双领唯一事实源仍是服务端 `refs/claims/*` + 台账 `claims` 投影（Z15）。不进 `progress.json`、不进 `board.db`、不进 v3 快照。
- **接手粒度（拍板项 1）**：复用旧 worktree 与旧分支，不删目录、不删分支、不 stash、不 `git checkout --ours/theirs`；未提交改动原样留给接手者。
- **卡片契约不动**：`baseline` 保持 `todo`（本卡不改任何卡的 `baseline` / `title` / `stage` / `scope` / `acceptance`），因此不触发 `chore(board)` 契约变更通道；新 agent 仍受原卡 `scope` 与 `acceptance` 约束（Z11）。
- **不动现场**：本卡只交付机制与临时仓库实跑证据，**不触碰当前 8 条 `refs/claims/*`、不回收任何 worktree、不删任何分支**——现场清理由维护者另行授权，避免误伤 11-08 新领的活动卡 DX-146 / MOD-7。
- 不碰锚点区（§7.1），不引入 `--no-verify`（`docs/adr/0003` 零豁免），不改 `refs/claims/*` 的非强推语义、不改 Ruleset，不改 `src/` 与 `board.db` schema。

## 输入
基线 `origin/main`（tip `e4b2a4b`，2026-11-08）。设计依据 `docs/module-split-plan.md` §7.2（5 字段建议、接手协议 6 步、其中第 5 步「先跑 acceptance 再信 stage」、载体选型 A/B/C 与「收工必写 nextStep」强制项）；`docs/agent-task-event.md` 的 note 全量语义不复用（本卡上下文走独立文件）。可复用零件：`nbflow.py:842` 的 `handoff_path` 路径约定、`nbflow.py:1109-1148` `cmd_abandon` 已验证的 `push origin --delete refs/claims/<code>`、`agent-workspace.ps1` 的 `Claim/Release` 与文件锁、`git-done.ps1 -Selftest` 自建临时仓库的演练范式、`check_claim_refs.py::classify_claim` 作为释放判据的单一实现。冻结语法：`chore(claim): 领取 #<code> 实施任务`（`check_claim_refs.py:49` 正则带 `$` 锚定，「接手自」只能进 body）。并发环境为本仓 20+ worktrees、主线分钟级前进，立卡与实现须在基于最新 `origin/main` 的干净 worktree 内、先 rebase 再收尾。

## 产出
① `docs/agent-assignments.json` + `scripts/assignment-store.py`（新建，唯一写者）；② `scripts/handoff.ps1`（新建，含 `-Selftest` 临时仓库演练）；③ 6 个接入点改动与 `check_claim_refs.py` 的 handoff verdict；④ `docs/module-split-plan.md` §7.2 标注「已落地」+ `docs/pitfalls.md` / `docs/charter-appendix.md` 补条目；⑤ 合并提交 body 四段（变更 / 验收 / 影响 / 卡片），`verify.ps1 -ScriptOnly` 与临时仓库实跑记录写进卡片 note；⑥ 按宪章 §7.7 走脚本层验收（scope 内无 `.cpp/.h/.qml/CMakeLists.txt`）。
