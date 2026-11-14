---
state: approved
card: DX-154
title: git-done 三处 git 输出未解包，合并正文与结单预判失真
stage: DX
scope: scripts/git-done.ps1
acceptance: pwsh -File scripts/git-done.ps1 -SelfTest 退出码 0 且新增产物断言通过（合并正文文件数非 0、卡片段命中分支标记）;pwsh -File scripts/verify.ps1 -ScriptOnly 通过;源码断言：Get-MissedDonePreview 与 New-MergeCommitBody 的 Invoke-GitQuiet 调用均取 .Output（grep 可判定）
evidence: f58a162 与 661d57d 的合并正文实测「3 个提交、0 个文件改动」「卡片：无（纯杂务分支）」——分支实际改 1-2 个文件且领了卡；DX-153 结单预判对带 #DX-153=done 的分支仍报缺失。根因：Invoke-GitQuiet 返回 {Code,Output} 对象，New-MergeCommitBody 152/159 行与 Get-MissedDonePreview 未取 .Output，正则扫描对象字符串恒不命中；697dc92（DX-101）引入起即坏
author: 维护者
tier: P2
---

## 目标

修复 git-done 三处「Invoke-GitQuiet 返回对象未取 .Output」导致的失真：合并正文提交数恒 0、文件数恒 0、卡片段恒「无（纯杂务分支）」，以及 DX-153 新增的结单预判恒误报（提交带 #短号=done 仍报缺失）。三处同根因，一次修齐（Z14），并在 -SelfTest 加产物级断言防回归（Z6）。

## 验收

- pwsh -File scripts/git-done.ps1 -SelfTest 退出码 0，含新增断言：场景 A 合并正文「文件改动数」非 0、「卡片：」段命中分支提交里的 #DX-999 标记
- pwsh -File scripts/verify.ps1 -ScriptOnly 通过
- 源码断言（grep）：scripts/git-done.ps1 中 Get-MissedDonePreview 与 New-MergeCommitBody 的 Invoke-GitQuiet log/diff 调用均带 .Output

## 边界

- 只改 scripts/git-done.ps1；不动 Invoke-GitQuiet / Invoke-Cmd 封装（返回形态是设计，调用方取值错误才是病）
- 不改已推送的合并提交正文（661d57d / f58a162 假数据留历史，不可改写）
- 预判语义与时机不变（DX-153），只修取值

## 输入

- DX-153 交付当日实测：预判误报 + 合并正文假数据，两例合并提交正文为证
- Invoke-GitQuiet 返回 {Code, Output}（git-done.ps1:108）；152/159 行与预判函数直接把返回值当数组用

## 产出

- scripts/git-done.ps1：Get-MissedDonePreview 与 New-MergeCommitBody（152 / 159 行）取 .Output；-SelfTest 场景 A 增加合并正文产物断言
