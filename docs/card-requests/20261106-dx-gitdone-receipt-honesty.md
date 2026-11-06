---
state: proposed
stage: DX
title: git-done 推送失败仍打印「合并完成」且回执记录未落地 head：收尾总结按推送事实打印
author: agent-flowguard
evidence: 2026-11-06 会话实录：chore/board-dx111-dx112 的合并提交 c016431 推送撞 non-ff、重试变基冲突 abort 后，git-done 继续打印「✓ 合并完成：chore/board-dx111-dx112 → origin/main」（git-done.ps1:1252 无条件打印）且回执记录 head=c016431/mergedInto=origin/main，而 c016431 从未成为 origin/main 祖先（ls-remote 实证），假成功让收尾失败静默漂移。与 DX-78（重试与检出归位）、DX-58（closed 字段诚实性）互补，本卡管「总结行与 head 字段按推送事实」
scope: scripts/git-done.ps1;docs/pitfalls.md
acceptance: Push-Main 全部重试耗尽且单独推合并也失败时，不打印「✓ 合并完成」，改为打印失败总结（留在本地的 head 与下次合并的恢复指引），退出码非 0；回执 head 字段只记已落地的提交，推送失败时 head 置空或标 pushed=false，mergedInto 不写 origin/main，杜绝回执虚报；负测：打桩让两次推送均拒，断言无「合并完成」行且回执 head 为空，推送成功路径不回归（现有验收仍绿）；pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
suggestedId: 
---

## 目标
git-done 推送失败仍打印「合并完成」且回执记录未落地 head：收尾总结按推送事实打印

## 验收
Push-Main 全部重试耗尽且单独推合并也失败时，不打印「✓ 合并完成」，改为打印失败总结（留在本地的 head 与下次合并的恢复指引），退出码非 0；回执 head 字段只记已落地的提交，推送失败时 head 置空或标 pushed=false，mergedInto 不写 origin/main，杜绝回执虚报；负测：打桩让两次推送均拒，断言无「合并完成」行且回执 head 为空，推送成功路径不回归（现有验收仍绿）；pwsh -File scripts/verify.ps1 -ScriptOnly 全绿

## 边界
scripts/git-done.ps1;docs/pitfalls.md

## 输入
基线 origin/main；依赖：

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
