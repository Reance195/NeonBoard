---
state: closed
card: DX-159
stage: DX
tier: P2
title: handoff.ps1 现场定位只有两级信息源：领用投影在未推分支台账时接手被拒
author: maintainer-session-2026-11-09
evidence: 维护者 2026-11-09 会话指令「把三个尾巴立成小卡」（尾巴 1）。实录（2026-11-09 DX-138~143 接手批次）：handoff.ps1 -Card DX-139 exit 2「无法定位认领分支」——agent-fxa 的领用投影只存在于未推送分支（fix/audit-judgment-hygiene）的检出台账里，主线台账与 assignments 均无；handoff.ps1 的定位只有两级（assignments + 主线台账 claims），而同仓 check_claim_refs.py::audit_handoff 有四级（assignments / .worktrees 物理现场 / 主线台账 / 远端分支头台账）。本次靠手工 assignment-store upsert 预登记绕过——绕过成功恰证明信息 audit 侧齐全，handoff 侧缺同一份查找（Z5：同一条定位判据应只有一份实现）。
scope: scripts/check_claim_refs.py;scripts/handoff.ps1;docs/pitfalls.md
acceptance: python scripts/check_claim_refs.py 新增 --locate <短号> 子模式：四级查找（assignments → .worktrees 物理现场 → 主线台账 claims → 远端分支头台账）输出 JSON（agent / branch / worktree / nextStep / 来源级），查找实现与 audit_handoff 共用同一函数（audit_handoff 改调它，不保留第二份遍历）;handoff.ps1 的现场定位改调 --locate 消费 JSON，删除 PS 侧自带的两级查找；handoff.ps1 -Selftest 扩展双场景断言：有 assignment 条目与「登记只在 worktree 台账（无 assignment）」两种现场都能定位并完成接手，后者不再需要预登记;python scripts/check_claim_refs.py --selftest 与 handoff.ps1 -Selftest 全过;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
rejectReason: 
createdAt: 2026-11-09T21:30:00+00:00
---

## 目标
把 handoff.ps1 的领用现场定位收口到与 audit_handoff 同一份四级查找实现（Z5）：接手不再依赖「恰有 assignment 条目」，未推送分支的领用投影（.worktrees 检出台账）直接可定位。

## 验收
见 frontmatter acceptance（5 条，分号分隔）。核心：`--locate` 是四级查找的唯一实现入口，audit_handoff 与 handoff.ps1 都消费它；selftest 双场景钉住「无 assignment 也能接手」。

## 边界
见 frontmatter scope。硬约束：

- 四级查找的顺序、保守语义（离线 skip、.worktrees 按锚点 common dir 定位——pitfalls 1.31）与现有 audit_handoff 完全一致，不引入新判据。
- handoff.ps1 其余流程（换身份、占用登记、原子重领、残局摘要、幂等短路、回滚）不动；assignments 仍只经 assignment-store.py 写。
- 不改 assignment-store.py、不改台账结构、不改 refs/claims 语义。

## 输入
基线 origin/main。既有实现：check_claim_refs.py::audit_handoff 的四级查找（wt_claims 的 .agent-env agent 名解析、remote_claims 的远端分支头台账扫描）；handoff.ps1 的定位段（assignments 优先 → 主线台账 claims 回退 → .agent-env 扫描）。实战实录见 evidence；pitfalls 1.31（锚点定位）与 1.48（GBK/单引号正则）为同族前科，实现时照守。

## 产出
① check_claim_refs.py 的 --locate 子模式与共用查找函数；② handoff.ps1 定位段替换；③ handoff.ps1 -Selftest 双场景扩展；④ 合并提交 body 四段，验收记录进卡片 note。
