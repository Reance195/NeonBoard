---
state: closed
card: DX-161
stage: DX
tier: P2
title: 收尾的 assignment 移除是工作树操作不落提交：done 卡条目永久残留 main
author: maintainer-session-2026-11-09
evidence: 维护者 2026-11-09 会话指令「把三个尾巴立成小卡」（尾巴 3）。实录（DX-143 交付）：agent-h4 的接手登记提交「chore(claim): 登记 #DX-143 接手现场」把 assignment 条目随分支带进 main；收尾时 git-done 的 assignment remove（DX-149 接入点）只改了 worktree 工作树文件——收尾链的代提交只覆盖 claims/结卡/快照三步，assignments 清理不落盘；worktree 回收后该变更丢失，origin/main 的 docs/agent-assignments.json 永久残留 done 卡条目（2026-11-09 实测 origin/main 存在 DX-143 条目，卡已 done、占位已释放）。assignments 设计为「随分支提交」，收尾清理却不在任何提交里——与台账 claims「合并/放弃分支后自动失效」的闭环不对称。
scope: scripts/git-done.ps1;docs/agent-assignments.json;docs/pitfalls.md
acceptance: git-done.ps1 收尾链新增 assignment 清理代提交：占位释放的卡条目移除后若 docs/agent-assignments.json 有实质变更，随一条独立 chore 提交落地（分离 HEAD 时序，与 claims 清理/结卡/快照同段），无变更不产生空提交，DryRun 有桩不落盘;git-done.ps1 -Selftest 扩展场景：演练仓库收尾后断言 assignments.json 中已释放卡的条目已被提交移除、且无空提交;存量清理：docs/agent-assignments.json 移除 DX-143 残留条目（独立提交，body 带授权与批次两行，不与代码路径混改）;docs/pitfalls.md 补条目（随分支提交的文件其「收尾移除」必须同样随收尾提交，否则 worktree 回收即丢失）;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
rejectReason: 
createdAt: 2026-11-09T21:30:00+00:00
---

## 目标
补齐 assignments 的收尾闭环：占位释放 → 条目移除 → **移除落进收尾提交**，让「合并/放弃后 assignment 自动失效」与台账 claims 同等对称；顺带清掉 main 上 DX-143 残留条目。

## 验收
见 frontmatter acceptance（5 条，分号分隔）。核心：selftest 产物断言（条目被提交移除 + 无空提交）；存量 DX-143 清理独立提交带授权两行。

## 边界
见 frontmatter scope。硬约束：

- 收尾时序不变（DX-50 三步断言基础上扩第四步，仍在分离 HEAD）；assignments 清理提交不与代码路径混改（条款 6.2）。
- 不改 assignment-store.py 写者语义、不改 handoff.ps1、不改台账结构；docs/agent-assignments.json 本卡只允许移除 DX-143 一条。
- 台账/快照既有代提交逻辑不动。

## 输入
基线 origin/main。既有实现：git-done.ps1 收尾三步自动提交（分离 HEAD，DX-50）与 DX-149 的 assignment remove 接入点（Invoke-ClaimRefCleanup 内，$goneAll）；对比范式：release-claims.py 的「清理过期领用登记」代提交（1293 行段）。实录与残留现状见 evidence。

## 产出
① git-done.ps1 第四步代提交 + Selftest 场景；② 存量 DX-143 条目清理提交；③ pitfalls 条目；④ 合并提交 body 四段，验收记录进卡片 note。
