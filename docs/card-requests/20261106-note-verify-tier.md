---
state: approved
card: DX-113
stage: DX
title: 交付说明 note 提交被判「改契约」：full 档与 -ScriptOnly 验收口径分裂，agent 只能挑时态自证
author: agent-dx94-retro / 维护者 2026-11-06 会话「立卡」批准
evidence: DX-94 交付实测：实现提交 8505f61（含 #DX-94=done）跑 check_board_diff.py --base origin/main 判 derive、-ScriptOnly EXIT=0；追加 docs(board): 补 DX-94 交付说明（540f9c9，仅改 cards[DX-94].note）后同一命令判 full、EXIT=2，verify.ps1 -ScriptOnly 直接 Fail「台账的 cards 结构有变化，-ScriptOnly 不足以验收」。为拿到轻档绿只能 detached 回实现提交跑，卡片 note 却只写「verify.ps1 -ScriptOnly EXIT=0」、未说明时态——验收口径不可机器复核。根因：check_board_diff.py:53 的 CARD_FIELDS 把 note 与 stage/title/scope/acceptance 同列，diff_cards 比整个 card dict，note 一变即判「改动既有卡片的契约」；但 note 是交付说明，不改 tst_projectmap 读的阶段/范围/roadmap 行，与契约字段不同权。同型非个例：DX-81（9ba62fd）、R11-11/12（1be772b）都把交付说明单独成 docs(board) 提交，靠各 agent 沿用惯例绕过，跑过哪一档无人可追溯。
scope: scripts/check_board_diff.py;scripts/verify.ps1;docs/task-card.md;AGENTS.md;docs/pitfalls.md
acceptance: check_board_diff.py 区分契约字段与交付说明：只改 cards[*].note 不判 full（维持 derive 档），改 stage/scope/acceptance 仍判 full（反例不破），分类逻辑抽纯函数进 --selftest;verify.ps1 -ScriptOnly 在「实现提交 + 交付说明提交」的最终分支态也 EXIT=0，端到端回归用例覆盖（造两提交分支跑轻档），不再靠挑时态;docs/task-card.md 与 AGENTS.md 写明交付说明的提交与验收档位口径（单独 docs(board) 提交、判哪一档、卡片验收项该写什么），消除 agent 自证空间;docs/pitfalls.md 记一条现象/根因/解法，点名 DX-94/DX-81/R11-11-12 三处同型;python scripts/check_board_diff.py --selftest 与 pwsh -File scripts/verify.ps1 -ScriptOnly 全绿，task_gate.py selftest 不回归
depends: 
suggestedId: DX-113
rejectReason: 
createdAt: 2026-11-06T00:00:00+00:00
---

## 目标
把「写交付说明」从「改契约」里摘出来：让 `-ScriptOnly` 轻档验收在**含交付说明提交的最终分支态**也能判定、可复核，不再依赖 agent 挑时态自证。交付说明是每张卡收尾的必经动作，它不该把一张脚本卡从轻档踢进全量档，更不该让卡片验收项写着一个只在中间态成立的结论。

## 验收
见 frontmatter `acceptance`（5 条，分号分隔）。核心是第 1、2 条：note 单独改动不升级验收档，且最终分支态轻档端到端绿。

## 边界
scripts/check_board_diff.py;scripts/verify.ps1;docs/task-card.md;AGENTS.md;docs/pitfalls.md

## 输入
基线 origin/main；依赖：无（DX-81 已合入，其快照漂移归因扩展是相邻改动但非前置；本卡只动验收分档口径，不动归因）。

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）。
