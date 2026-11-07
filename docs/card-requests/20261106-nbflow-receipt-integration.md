---
state: closed
card: DX-115
stage: DX
title: nbflow approve 立卡通道被 DX-98 回执闸门击穿：程序级立卡只剩 register_board_cards 一条活路，nbflow 未随闸门迁移
author: agent-dx75
evidence: 2026-11-06 agent-dx75 实录：nbflow request DX-112 → approve 两连拒，board-apply 在 fresh origin/main 检出的 chore worktree 里 git commit 被 commit-msg 拒（退出码 9）：『新增卡 DX-112 无立卡回执：手工改台账立卡通道已关闭（DX-97），只能用 register_board_cards.py』。DX-98（f99e483）落地后 task_gate 新增 receipt_problems 闸门，而 nbflow/board-apply 不产回执——两个立卡程序只被闸门认识一个，nbflow 的 request/dedup/授权机器全链变孤儿。DX-112 已 abandon 改走 register 正道（真实卡 DX-114 落地）。同一职责两个程序、闸门只认其一，另一个不迁移也不退役，Z5 现实版。
scope: scripts/nbflow.py;AGENTS.md;docs/pitfalls.md
acceptance: nbflow approve 对新增卡的落盘步骤改走 register_board_cards.py（消费同一回执机制，Z5 单一写者），立卡提交含回执、commit-msg 放行；或按宪章显式退役 nbflow 立卡路径：request 保留（dedup/授权仍有效），approve 直接拒绝并打印 register_board_cards.py 指引，杜绝退出码 9 的暗失败;端到端实测任一方案：nbflow 侧发起一张演练请求，立卡最终合入 main 且 docs/card-requests/registered/ 出现对应回执;AGENTS.md 立卡条款与 nbflow 文档段同步实际口径（谁是唯一入口、另一个的角色）；pitfalls 记条目：同一职责出现两个程序时，闸门只认一个，另一个必须随闸门迁移或显式退役，不迁移不退役的第三个程序就是下一次的退出码 9
depends: DX-98
suggestedId: AUTO
---

## 目标
nbflow approve 立卡通道被 DX-98 回执闸门击穿：程序级立卡只剩 register_board_cards 一条活路，nbflow 未随闸门迁移

## 验收
nbflow approve 对新增卡的落盘步骤改走 register_board_cards.py（消费同一回执机制，Z5 单一写者），立卡提交含回执、commit-msg 放行；或按宪章显式退役 nbflow 立卡路径：request 保留（dedup/授权仍有效），approve 直接拒绝并打印 register_board_cards.py 指引，杜绝退出码 9 的暗失败
端到端实测任一方案：nbflow 侧发起一张演练请求，立卡最终合入 main 且 docs/card-requests/registered/ 出现对应回执
AGENTS.md 立卡条款与 nbflow 文档段同步实际口径（谁是唯一入口、另一个的角色）；pitfalls 记条目：同一职责出现两个程序时，闸门只认一个，另一个必须随闸门迁移或显式退役，不迁移不退役的第三个程序就是下一次的退出码 9

## 边界
scripts/nbflow.py;AGENTS.md;docs/pitfalls.md

## 输入
基线 origin/main；依赖：DX-98

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
