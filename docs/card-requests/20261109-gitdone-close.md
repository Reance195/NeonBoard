---
state: approved
card: DX-153
title: git-done 结单预判前移，-Close 补跑提示改可执行
stage: DX
scope: scripts/git-done.ps1
acceptance: pwsh -File scripts/git-done.ps1 -SelfTest 退出码 0（既有 A/B 场景断言不回归）;pwsh -File scripts/verify.ps1 -ScriptOnly 通过;源码断言：结单预判调用位于删功能分支之前，原「重跑收尾并加 -Close」提示改为真实出路（grep 可判定）
evidence: R11-14 实测：git-done 对账提示「重跑收尾并加 -Close」发出时功能分支已在 1250 行删除（提示在 1472 行），补跑被 964 行「分支不存在」exit 1 拒，claim 已清理，补结通道关闭
author: 维护者
tier: P2
---

## 目标

修复 git-done 的结单对账提示时序矛盾：对账点名「没结的卡」并建议「重跑收尾并加 -Close」，但该建议发出时功能分支与临时分支均已删除，重跑必然被「分支不存在」拒绝（R11-14 实测）。改为：合并落本地后、删分支之前预判「领了但没写 =done」的卡并给出可执行提示；分支已删后的正式对账措辞改为真实出路。

## 验收

- pwsh -File scripts/git-done.ps1 -SelfTest 退出码 0（DX-78 场景 A/B 断言不回归）
- pwsh -File scripts/verify.ps1 -ScriptOnly 通过（scope 仅 scripts/，轻档）
- 源码断言（grep）：结单预判调用位于 Remove-MergedBranch(功能分支) 调用之前；1472 行附近不再输出「重跑收尾并加 -Close <短号,...>」字样

## 边界

- 只改 scripts/git-done.ps1；不动 derive_board_status.py（结卡判定唯一实现仍在那边，Z5）
- 预判只影响提示措辞与时机，不改变结卡判定（--close-cards 口径不变）
- 预判正则仅匹配 ASCII 的 #短号=done（与 New-MergeCommitBody 的卡号提取同款手法与编码风险面）

## 输入

- R11-14 收尾实测：提示与动作顺序自相矛盾，-Close 补跑通道在提示发出时刻已关闭
- 预判口径：claimedCodes 与「分支区间提交里含 #短号=done」的差集；用户已传 -Close 时跳过预判（已显式确认）

## 产出

- scripts/git-done.ps1：新增结单预判（删分支前打印可执行提示：中断重跑带 -Close / 或继续收尾留人结）；正式对账的提示语改为真实出路（看板点结 / re-claim 后带 -Close）
