---
state: approved
stage: DX
title: 立卡排位中点插入把档间隙指数耗尽：P2 新卡立卡时 derive --write 直接崩，DX-123 自身 orderKey 错位，排位守卫无自愈
author: agent-dx116b-session-2026-11-07
evidence: 2026-11-07 DX-116 交付日实录。① 立 DX-124（P1）时 derive --check 报「卡片 DX-123 声明 P2 但 orderKey=88064 排在更低档 DX-102 之后，违反排位规则」—— DX-123（排位规则的实现卡，3592b e2 已 done）自己的 orderKey 都不满足自己定的单调规则；② 紧接着立 DX-125（P2）时 derive --write 在 board_priority.insertion_order 抛 RuntimeError:「P2 档与下一档之间无可用 orderKey 间隙，需先重排该档」—— 台账/回执/占位已落，快照却刷不了，立卡流程卡在半落地（台账有卡、快照无卡）。根因：insertion_order 用「本档末张与下一档首张的中点」落位，中点法每插一张间隙减半，同档连续立卡必然指数耗尽（ORDER_STEP=1024 撑不了几张）；耗尽后的处置是 raise 并提示「需先重排该档」，但仓库没有任何重排通道 —— 人重排又违反「agent 不得手改快照/台账」（§11）。后果：P2 档（共享脚本收口波次，恰是最密集的档）立一张废一次快照，立卡通道对 P2 实际不可用。
scope: scripts/board_priority.py;scripts/derive_board_status.py;docs/pitfalls.md
tier: P0
acceptance: insertion_order 的落点策略改为「耗尽可自愈」：本档与下一档间隙不足时自动执行等距重排（compact）—— 按档位序 P0→P4、档内按现序对全部声明 tier 的 todo 卡重新等距分配 orderKey，重排结果由 derive --write 正常落盘，全程不需要人工改任何文件;python scripts/board_priority.py --selftest 增正反例：同档连续插入到间隙耗尽后再插一张 → 不抛异常、落位合法（档位单调保持）;既有卡不回归;对当前主线实测：python scripts/derive_board_status.py --write 退出码 0，DX-123 的 orderKey 错位被重排消除（derive --check 退出码 0），DX-124/DX-125 均出现在快照且档位落点正确;python scripts/derive_board_status.py --selftest 与 --check 全过，pwsh -File scripts/verify.ps1 -ScriptOnly 全绿;docs/pitfalls.md 记条目：中点插入的间隙指数耗尽是已知数学性质，凡「取中点」的落位策略必须配「耗尽即重排」的自愈通道，否则第 N+1 张卡就是死锁;fail-fast 的错误信息不许指向一条不存在的通道（「需先重排该档」而仓库没有重排程序）
depends: DX-123
suggestedId: DX-126
createdAt: 2026-11-07T02:00:00+00:00
---

## 目标
让立卡排位在任何档位插入多少张卡都不会死锁：间隙耗尽时机器自己重排，
不留给人工一条走不通的「需先重排该档」提示。

现状（2026-11-07 主线实测，两次连续复现）：

1. `insertion_order` 用中点落位 —— 每插一张，可用间隙减半，
   `ORDER_STEP=1024` 的初始间隙撑不了同档几张卡；
2. 耗尽后 `RuntimeError("P%d 档与下一档之间无可用 orderKey 间隙，需先重排该档")`，
   但**仓库里不存在任何重排程序**，人工重排又踩「agent 不得手改快照」的红线；
3. DX-123 自己的 orderKey=88064 排在更低档之后，derive --check 的排位守卫
   每次立卡都在报 —— 守卫在告警，自愈不存在，告警变成永久噪声。

## 验收
见 frontmatter `acceptance`（5 条，分号分隔）。核心是第 1 条 —— **自愈**：
compact 是排位规则的组成部分（在 board_priority.py 实现，derive --write 调用），
不是散在调用方的一次性脚本；重排只动 orderKey，不改任何卡的 status/note/tier。

## 边界
scripts/board_priority.py;scripts/derive_board_status.py;docs/pitfalls.md

- 只动「落点计算 + 耗尽自愈」；tier 声明、validate_tier、check_monotonic 的既有
  语义不变（Z3）。
- compact 只作用于「声明了 tier 的 todo 卡」：done/doing 卡的 orderKey 是历史位置
  不参与；未声明 tier 的存量卡按棘轮口径豁免（与 by_tier 同口径）。
- 重排必须幂等且可从 --check 观察：重排后 check_monotonic 零违例。

## 输入
基线 origin/main（含 DX-116 合并提交 9e1c685 与 DX-123 实现 3592be2）。
当前台账快照处于「DX-124 已入、DX-125 未入」的半落地态 —— 本卡交付后的第一次
derive --write 必须把两张卡都落进快照（acceptance 第 3 条实测覆盖）。

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）；pitfalls 条目（§2.x 编号顺延）。
