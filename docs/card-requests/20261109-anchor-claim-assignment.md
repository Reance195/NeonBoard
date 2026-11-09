---
state: proposed
stage: DX
tier: P2
title: 锚点区领卡把 assignment 写进 main 工作树：错位条目没人能提交还绊锚点快进
author: maintainer-session-2026-11-09
evidence: 维护者 2026-11-09 会话指令「立」（针对 DX-149 交付后残留第 2 条）。实录：agent-dxl 于锚点区领 DX-157 后，锚点 docs/agent-assignments.json 出现未提交修改（DX-157 条目，agent-dxl/branch=refactor/dx157-ledger-canon）——claim-card.py 的 assignment upsert 按 root 定位存储，root=锚点（git-dir == common-dir，-ForceAnchor / nbflow dispatch 场景）时写入的是 main 工作树；assignments 设计为「随分支提交」，锚点检出 main 且 pre-commit 禁止 main 提交，该条目成为无人能提交的死数据，并使锚点快进在 ff-only 时可能撞脏文件（git-done 锚点同步跳过）。DX-149 的 audit 四级信息源（worktree 台账 claims / 远端分支头 / assignments）可完整兜底定位，锚点缺 assignment 条目不损失接手能力。
scope: scripts/claim-card.py;docs/agent-assignments.json;docs/pitfalls.md
acceptance: python scripts/claim-card.py --selftest 全过，锚点检出判定（git-dir 与 git-common-dir 同值 → 锚点）对锚点区判 True、对 linked worktree 判 False，判定为唯一实现;claim-card 领用流程在锚点检出执行时跳过 assignment upsert 并打印点名告警（旁路、不阻断领用、退出码不变），非锚点（linked worktree / 普通 clone）行为不变;锚点 docs/agent-assignments.json 还原为 origin/main 版本（移除 DX-157 错位条目；agent-dxl 现场由台账 claims 与 audit worktree 级兜底，不损失定位能力），git status 对该文件恢复干净;docs/pitfalls.md 补条目记录根因（assignments 随分支提交 vs 锚点检出 main 无处落地）与处置;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
rejectReason: 
createdAt: 2026-11-09T20:10:00+00:00
---

## 目标
堵住 DX-149 机制的一个边界缺陷：锚点区领卡（`agent-init -ForceAnchor` / nbflow dispatch 场景，root = 锚点检出）时 `claim-card.py` 的 assignment upsert 把条目写进 main 工作树的 `docs/agent-assignments.json`——该文件设计为随分支提交，锚点检出的是 main 且钩子禁止 main 直接提交，条目成为死数据并绊锚点快进。

## 验收
见 frontmatter acceptance（5 条，分号分隔）。核心：锚点判定唯一实现 + selftest 两例；跳过是旁路告警而非静默（Z4 体验类 fail-soft 必须告警）；锚点文件还原以 `git status` 干净为准（Z8）。

## 边界
见 frontmatter scope。硬约束：

- 非锚点路径（开发区 worktree / 普通 clone）的 assignment upsert 行为**一个字节都不变**——DX-149 主链路不回归。
- 跳过仅限「root 是锚点检出」这一场景，且必须点名告警（说明 assignment 由 audit 的台账/worktree 级兜底），不做静默降级。
- 还原锚点 `docs/agent-assignments.json` 只移除 DX-157 错位条目（还原为 origin/main 版本），不触碰其它任何卡的条目；这是移除本机制自己写错位置的数据，不动 agent-dxl 的 worktree 现场。
- 不改 assignment-store.py（写者语义不变）、不改 audit_handoff、不改 git-done / nbflow。

## 输入
基线 `origin/main`。既有事实：DX-149 已交付（合并提交 9e43067）；实录见 evidence（锚点 `git status` 中 `M docs/agent-assignments.json`，条目 DX-157）；DX-149 audit 的四级信息源在 `check_claim_refs.py::audit_handoff`（assignments / worktree 现场 / 主线台账 / 远端分支头台账），锚点跳过 upsert 后定位能力不缺。判据先例：pitfalls 1.31「.worktrees 只在锚点存在」同族的位置意识；`agent-init.ps1` 的锚点判定（git-dir == common-dir）可作对照实现。

## 产出
① claim-card.py 锚点判定 + 跳过分支 + `--selftest`；② 锚点 assignments 还原；③ pitfalls 条目；④ 合并提交 body 四段，`verify.ps1 -ScriptOnly` 记录进卡片 note。
