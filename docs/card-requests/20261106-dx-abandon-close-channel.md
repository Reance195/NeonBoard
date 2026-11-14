---
state: closed
card: DX-111
stage: DX
title: 弃单卡没有结案通道：DX-43/R0-12 永挂 todo 且可被重新领用
author: agent-flowguard
evidence: DX-43 note 已记弃单（nbflow 让位）仍挂 todo 可被重领；R0-12 弃卡处置 2026-11-06 已获维护者批准但无通道执行；DX-62 同款「可被重复领用」风险的弃单变体——交付型漂移已有 DX-62 兜，弃单型滞留无人管
scope: scripts/agent-init.ps1;scripts/derive_board_status.py;scripts/task_gate.py;docs/pitfalls.md
acceptance: 台账支持弃单终态（建议 baseline=abandoned，命名实现时定）：弃单卡 agent-init 拒领（退出码明确）、派生器 --check 不再对它报漂移、--close-cards 不误碰；弃单结案走 chore(board) 人工授权行通道（baseline-only），自动来源行不适用；task_gate selftest 增用例：弃单结案放行，冒充弃单改定义拒绝；存量弃单卡 DX-43、R0-12 经维护者确认后按新终态结案，提交体带授权与批次两行；python scripts/task_gate.py selftest 与 python scripts/derive_board_status.py --selftest 全过，pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
suggestedId: 
---

## 目标
弃单卡没有结案通道：DX-43/R0-12 永挂 todo 且可被重新领用

## 验收
台账支持弃单终态（建议 baseline=abandoned，命名实现时定）：弃单卡 agent-init 拒领（退出码明确）、派生器 --check 不再对它报漂移、--close-cards 不误碰；弃单结案走 chore(board) 人工授权行通道（baseline-only），自动来源行不适用；task_gate selftest 增用例：弃单结案放行，冒充弃单改定义拒绝；存量弃单卡 DX-43、R0-12 经维护者确认后按新终态结案，提交体带授权与批次两行；python scripts/task_gate.py selftest 与 python scripts/derive_board_status.py --selftest 全过，pwsh -File scripts/verify.ps1 -ScriptOnly 全绿

## 边界
scripts/agent-init.ps1;scripts/derive_board_status.py;scripts/task_gate.py;docs/pitfalls.md

## 输入
基线 origin/main；依赖：

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
