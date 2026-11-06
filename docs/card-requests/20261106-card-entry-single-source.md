---
state: approved
stage: DX
title: 立卡入口单一事实源：文档、旧工具注释与预演输出对齐 DX-98 新流程
author: maintainer-session-2026-11-06
evidence: 2026-11-06 本会话照 docs/task-card.md 的立卡流程准备走 board-apply.py --commit，随后发现 AGENTS.md 条款 6 已被 DX-98 收口为 register_board_cards.py 唯一入口，commit-msg 对无回执的新增卡直接拒；同一天 board-apply.py --diff 的纯增量预演夹带既有卡 DX-96 的 depends 数组从 1 行重排为 3 行；register_board_cards.py 的 docstring 把立卡程序化标成 DX-97（实为 DX-98 的交付，DX-97 是卡片按钮修复）。
scope: docs/task-card.md;scripts/board-apply.py;scripts/register_board_cards.py;docs/pitfalls.md
acceptance: docs/task-card.md 的立卡流程示例改为 register_board_cards.py --form 且不再出现 --commit 的旧写法（rg 断言）;board-apply.py 的 --commit 在无回执时明确报错并指向新入口，或降级为只读预演且 rg 断言不再有写台账分支;board-apply.py --diff 的预演输出不再出现既有卡的行删除（纯增量文本判定）;脚本头注释中立卡程序化的卡号统一为 DX-98（rg 断言无 DX-97 误标）;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
suggestedId: DX-100
---

## 目标
让「怎样立卡」只有一份可执行答案：文档示例、旧工具行为、脚本注释都指向 `register_board_cards.py` + 回执，避免后来者按过期文档写出会被闸门拒的提交。

## 验收
见 frontmatter `acceptance`（5 条），含旧入口的行为收敛与预演输出的纯增量保形。

## 边界
只改文档与 `board-apply.py` / `register_board_cards.py` 的注释与入口提示，不改 `task_gate.py` 的回执判定（单一事实源不动），不改卡片契约字段。

## 输入
基线 `origin/main`（已含 DX-98 的回执闸门）；依赖：无。

## 产出
合并提交 body 四段；`docs/pitfalls.md` 补一条「主线被并行会话推进时以 origin/main 为准」的判据。
