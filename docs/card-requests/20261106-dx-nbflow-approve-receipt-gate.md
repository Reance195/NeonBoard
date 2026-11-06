---
state: proposed
stage: DX
title: nbflow approve 立卡链路被 DX-98 回执闸门打破：board-apply 提交被 commit-msg 拒且报错被缓冲吞掉只剩退出码 9
author: agent-flowguard
evidence: 2026-11-06 会话实录：nbflow authorize/request 正常，approve 三连败——临时区里 board-apply --commit 的立卡提交被 commit-msg 钩子以「无立卡回执」拒绝（DX-98 的 f99e483 上线后），board-apply 把钩子 stderr 捕获后仅在自身退出路径打印，stdout 块缓冲把报错吞掉，nbflow 只见「立卡失败（退出码 9）」无法诊断；并行会话已全部改走 register_board_cards.py（DX-107~109 实录）。修法二选一：approve 内部改调 register_board_cards.py，或其立卡提交把回执一并生成入提交；无论哪种，board-apply 的钩子拒绝输出必须透传
scope: scripts/nbflow.py;scripts/board-apply.py;docs/pitfalls.md
acceptance: nbflow approve --cards 能在 DX-98 闸门下完成立卡（回执由 approve 链路生成并随提交落库），全程不再依赖绕过；board-apply 的 task_gate/钩子拒绝输出透传到 approve 的可见输出（无缓冲吞没），失败可诊断；预演 --diff 与落盘口径一致（预演过闸 = 落盘过闸）；python scripts/nbflow.py --selftest 全过，pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
suggestedId: 
---

## 目标
nbflow approve 立卡链路被 DX-98 回执闸门打破：board-apply 提交被 commit-msg 拒且报错被缓冲吞掉只剩退出码 9

## 验收
nbflow approve --cards 能在 DX-98 闸门下完成立卡（回执由 approve 链路生成并随提交落库），全程不再依赖绕过；board-apply 的 task_gate/钩子拒绝输出透传到 approve 的可见输出（无缓冲吞没），失败可诊断；预演 --diff 与落盘口径一致（预演过闸 = 落盘过闸）；python scripts/nbflow.py --selftest 全过，pwsh -File scripts/verify.ps1 -ScriptOnly 全绿

## 边界
scripts/nbflow.py;scripts/board-apply.py;docs/pitfalls.md

## 输入
基线 origin/main；依赖：

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
