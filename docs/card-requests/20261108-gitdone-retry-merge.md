---
state: closed
card: DX-144
stage: DX
tier: P2
title: git-done 非快进重试用裸 rebase 展平 --no-ff 合并提交，主线出现线性提交
author: codebuddy-session-2026-11-08
evidence: 2026-11-08 会话 P0 收尾实录：改动经 git-done.ps1 合并推 main，本地确实创建了 --no-ff 合并提交 c581fa6，但第一次推送被拒（并发）后 Invoke-MainPushWithRetry 在 scripts/git-done.ps1:270 执行裸 git rebase origin/main，把 c581fa6 展平丢弃；最终 origin/main tip = 265ca98（git rev-list --parents -n 1 只有单父 932a11a，git log --graph 无分叉），违反 AGENTS.md 条款 5「主分支只接受合并提交」与条款 4「必须 --no-ff」。该缺陷在每次 non-ff 重试时都会复现，是共享收尾脚本的闸门失效而非个案。维护者 2026-11-08 会话指令「B立卡」选方案 B 并授权立卡治理。
scope: scripts/git-done.ps1
acceptance: Invoke-MainPushWithRetry 推送被拒后的重试不再用裸 git rebase 展平合并提交，改用保留合并结构的重试（--rebase-merges 或重建 --no-ff 合并），使 main tip 恒为合并提交;pwsh -File scripts/git-done.ps1 -SelfTest 全过，DX-78「首拒重试落地」场景补一条断言：重试后落地 tip 是双父合并提交（或等价的 --no-ff 痕迹）;pwsh -File scripts/verify.ps1 -ScriptOnly 全过（scope 仅脚本，轻档）;不改 Push-Main 单次推送语义，不动收尾三步提交的分离 HEAD 时序（DX-26）
depends: 
suggestedId: DX-144
rejectReason: 
createdAt: 2026-11-08T10:09:30+00:00
---

## 目标
修 git-done.ps1 的非快进重试逻辑：撞 non-ff 时保留 --no-ff 合并提交，不再让裸 rebase 把主线历史压成线性提交，守住 AGENTS.md 条款 4/5。

## 验收
见 frontmatter acceptance（4 条，分号分隔）。核心是第 1、2 条——DX-78 打桩自测已有「首拒重试落地」场景，需在该场景补一条「落地 tip 是合并提交」的断言，让缺陷被机器钉住而不是靠人看日志。

## 边界
scripts/git-done.ps1

- 只改 Invoke-MainPushWithRetry 的重试路径，不动 Push-Main 的单次推送、不动合并与收尾时序、不动 --no-ff 合并的创建逻辑。
- 修法二选一由实现卡决定：git rebase --rebase-merges（保留合并结构），或重试时基于新 origin/main 重建 --no-ff 合并；两者都要保证收尾三步提交不丢、不重复。
- 本卡只修脚本，不重写已推送的 main（265ca98 那条线性历史按维护者裁决留痕，不在此卡范围）。

## 输入
基线 origin/main（02212ac）。缺陷定位 scripts/git-done.ps1:259-278（Invoke-MainPushWithRetry）与 :1100-1106（合并提交创建）；DX-78 自测框架见 :760-880（New-StWorkRepo、-StubPushFailures）；AGENTS.md 条款 4/5、docs/pitfalls.md 的多代理收尾约定。2026-11-08 收尾回执 .sandbox/receipts/ci-p0-ratchet-gate-265ca98.json。

## 产出
scripts/git-done.ps1（重试保留 --no-ff 合并 + 注释同步）；-SelfTest 新增断言；合并提交 body 说明修法与证据。
