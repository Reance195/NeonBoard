---
state: proposed
stage: DX
tier: P3
title: git-done 临时分支 merge/* 静默残留：单次推送前 branch -d 的 upstream 判据恒假
author: duty-session-2026-11-07
evidence: 2026-11-07 值守会话盘点：本地 refs/heads/merge/* 累计 45 支历史残留（2026-11-06~07 两天），已按维护者「B（全清）」授权清空。根因在 scripts/git-done.ps1:1254 —— DX-106 把删分支挪到单次推送之前，此刻 origin/main 尚未含本次合并，`git branch -d <tmp>` 的安全判据「已合并到 upstream」恒假，git 必然以 not fully merged 拒删；旧实现还把输出 `| Out-Null` 吞掉、不检查返回码，于是每轮正常收尾都静默留一支 merge/<slug>（不同分支名不会被 1084 行的同名残留清理兜住）。同一文件 116 行的 Remove-MergedBranch（DX-109）早已给出正确判据（rev-list --count <branch> --not <合并提交> 为 0 后用 branch -D），但只在 1233（remote 功能分支）与 1629（local 模式）被调用，临时分支 $tmp 漏接，属于「判据已存在、调用点漏一处」的收口缺口。维护者 2026-11-07 会话明确指示「根治」。查重：DX-106（单次推送时序）、DX-109（分支删除判据）、DX-116（推送失败回执与 pin 分支）、DX-120（自身 worktree 回收）均为不同痛点，本卡只收口临时分支这一处漏接，不重做上述卡。
scope: scripts/git-done.ps1;docs/pitfalls.md
acceptance: scripts/git-done.ps1 临时分支 $tmp 的删除改走 Remove-MergedBranch -Name $tmp -AnchorCommit $mergedCommit，不再用裸 branch -d + Out-Null；返回 false 时打印可见诊断与手工 git branch -D 命令，失败不再静默;pwsh -File scripts/git-done.ps1 -SelfTest 全过，新增回归断言：场景 A（首拒重试落地）收尾后 refs/heads/merge/fix-selftest-a 不存在，且场景 B 的 DX-116 pin 分支断言仍通过（有意保留的补推落点不受影响）;负测：把该删除还原成旧 branch -d | Out-Null 写法，-SelfTest 的「临时分支已回收」断言必须 FAIL；还原后恢复全过，证明判据能抓住回归;pwsh -File scripts/verify.ps1 -GitDoneDryRun 通过（DX-50 收尾时序不回归），且演练结束不留 merge/* 临时分支;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿（scope 无代码路径，轻档）;端到端：本卡自身 git-done 收尾后 git for-each-ref refs/heads/merge/ 不含本次 slug，证据写进卡片 note;docs/pitfalls.md 记一条：单次推送时序下 branch -d 的 upstream 判据恒假，删除判据必须锚定本次合并提交
depends: 
suggestedId: DX-128
rejectReason: 
createdAt: 2026-11-07T03:00:00+00:00
---

## 目标
把 git-done 正常收尾路径里漏接的一处判据补齐，让 merge/<slug> 临时分支在推送前
（origin/main 尚未含合并）也能被可靠回收，且删不掉时必须可见、给出可执行的手工命令。
顺带把 dry-run 演练自建的临时分支一并回收，消掉第二个残留源。

## 验收
见 frontmatter acceptance（7 条，分号分隔）。核心是第 2、3 条——把「收尾后不留临时分支」
钉成 -SelfTest 的机器判据，并用负测证明这条断言真的能抓住旧写法回归，而不是恒绿。

## 边界
scripts/git-done.ps1;docs/pitfalls.md

- 只改 1254 行这一处删除调用与 -SelfTest 断言；不动 Remove-MergedBranch 本身
  （DX-109 的判据已正确），不动 1084 行同名残留兼容清理，不动 1394 行推送失败时
  有意 pin 住合并提交的 merge/* 分支（DX-116 的补推落点，属于设计而非残留）。
- 不改三段收尾提交时序（DX-26）、不改 Push-Main 单次推送（DX-106）、不改 B 场景的
  回执与 pin 分支口径。
- 失败仍 fail-soft：临时分支删不掉只告警 + 给手工命令，绝不阻断已成功的合并推送。

## 输入
基线 origin/main（4113d21）。根因注释见 scripts/git-done.ps1:110-115（DX-109）与
:1074（$tmp 命名）、:1149（$mergedCommit）；45 支残留的清理与分类证据见
2026-11-07 值守会话（43 支 -d 可删、2 支需 -D）。

## 产出
scripts/git-done.ps1（1254 行删除调用收口 + -SelfTest 场景 A 新增断言 + 模块注释）；
docs/pitfalls.md 一条；合并提交 body 四段；卡片 note 记端到端证据。
