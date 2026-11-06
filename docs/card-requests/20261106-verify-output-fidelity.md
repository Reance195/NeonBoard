---
state: approved
stage: DX
title: 收尾与验收输出保真：合并提交四段、锚点跳过提示、红绿一致
author: maintainer-session-2026-11-06
evidence: 2026-11-06 本会话 git-done 生成的合并提交 fbf6e9b 的提交体只有标题 merge: 合入 fix/card-action-click，正文全空，与 docs/task-card.md 要求的变更/验收/影响/卡片四段不符；同一次收尾因锚点区 4 项未提交改动跳过快进，只打印一行跳过提示，本地 main 随后落后 origin/main 20 个提交且无后续指引；verify.ps1 在看板一致性段打印「! 看板快照落后于派生结果（退出码 1）」后仍输出「✓ 验收通过」并退 0，红字与绿灯矛盾。
scope: scripts/git-done.ps1;scripts/verify.ps1;scripts/check_board_diff.py
acceptance: git-done -DryRun 生成的合并提交 body 含 变更：/验收：/影响：/卡片： 四行（rg 断言）;锚点快进被跳过时输出含 git merge --ff-only origin/main 与落后提交数（rg 断言）;verify.ps1 对本分支领用造成的预期快照落后输出明确说明且不再出现「退出码 1」字样的误导红字;一次真实 git-done 端到端 EXIT=0 且合并提交正文四段非空;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
suggestedId: DX-101
---

## 目标
让收尾与验收的输出如实反映状态：合并提交留痕完整、锚点不同步时给人下一步、预期的一次性红灯不再与「验收通过」同屏矛盾（Z9 / Z10）。

## 验收
见 frontmatter `acceptance`（5 条），含 DryRun 断言与一次真实收尾的端到端核对。

## 边界
只改 `git-done.ps1` / `verify.ps1` / `check_board_diff.py` 的输出与提示，不改合并策略、不改 `--no-ff` 与快进判定、不降低任何闸门。

## 输入
基线 `origin/main`；依赖：无。注意在办卡 DX-77 / DX-88 / DX-96 分别持有 `verify.ps1` / `git-done.ps1`，实施前需排先后顺序，避免并发改同一文件。

## 产出
合并提交 body 四段；`docs/pitfalls.md` 补一条「一次性红灯与常驻红灯」的判读说明。
