---
state: approved
stage: DX
title: 结卡证据闸门整轮连坐：一张无证据候选卡拒结全批，--close-explicit 人工通道也被击穿
author: agent-dx116b-session-2026-11-07
evidence: DX-116 交付日（2026-11-07）实录。git-done 收尾的结卡步骤退出码 2；手工复跑 `python scripts/derive_board_status.py --close-cards --close-explicit DX-116` 输出里只有 DX-88 的拒结行、DX-116 根本未被处理，退出码 2。根因两处：① derive_board_status.py 结卡流程把 DX-91 证据闸门（候选集全量判定 + 无一命中即 return 2）放在 --close-explicit 处理之前且对全候选集生效 —— 而闸门注释自己明说「--close-explicit（DX-33）不经此门：人工确认通道本来就是给『无标记可读』的卡留的，证据闸门对它重复设卡会让补结永远走不通」，实现与声明意图相反；② 拒结粒度是「整轮拒绝落盘」而不是「拒这一张」—— DX-88 的 #DX-88=done 标记挂在 chore(board) 流程提交 3a56345 上（ADMIN 类，全主线无非流程提交引用 #DX-88，交付证据缺失），从它进主线起每轮 --close-cards 都退 2，此后所有交付卡（含 DX-116）的自动结卡全部失败，DX-88 自己也永挂 todo、可被重新领用（DX-111 描述的形态）。一张历史坏卡把整条结卡通道锁成活锁，这本身就是 DX-91 fail-closed 意图的过度外溢：闸门要拦的是「无证据的卡不得结」，不是「有任何无证据的卡就谁也别结」。
scope: scripts/derive_board_status.py;docs/pitfalls.md
tier: P1
acceptance: derive_board_status.py selftest 增用例：候选集中混入一张无证据卡、一张有证据卡、一张 --close-explicit 指定卡时，有证据卡与 explicit 卡正常落盘 baseline=done，无证据卡被逐个点名排除且不落盘，整体退出码 0（带告警行）——「拒这一张」不再放大成「全批不落」;--close-explicit 名单在证据闸门前摘除，不经闸门（对齐代码内既有注释声明的意图）：explicit 卡即使无交付证据也按人工确认落盘;对当前主线实测：python scripts/derive_board_status.py --close-cards --close-explicit DX-116 退出码 0，台账 DX-116 baseline 变 done，DX-88 维持 todo 且被点名证据缺失;python scripts/derive_board_status.py --selftest 全过，pwsh -File scripts/verify.ps1 -ScriptOnly 全绿;docs/pitfalls.md 记条目：fail-closed 的粒度要落在「被拒的对象」上；拒结/拒绝类闸门若把「存在违规项」升级为「整批不落」，一张历史坏卡就把自动通道锁成活锁（DX-88 → 每轮退 2 → 所有交付卡结不上）
depends: DX-116
suggestedId: DX-124
createdAt: 2026-11-07T01:40:00+00:00
---

## 目标
把结卡通道从「一张坏卡锁死全批」的活锁里解出来，让 DX-91 证据闸门拦住该拦的那张卡，
而不是拦住整条通道。

现状（2026-11-07 主线实测）：`--close-cards` 的候选集判定之后、任何落盘之前，
DX-91 证据闸门对**全候选集**做「非流程提交引用」检查，任一卡无证据即
`return 2`、整轮不落盘。DX-88 的 `#DX-88=done` 标记挂在 chore(board) 流程提交
3a56345 上（ADMIN 类提交，全主线无非流程提交引用过 #DX-88），于是：

- DX-88 自己永挂 todo —— 防重复领用的门关不上（DX-111 形态）；
- 此后**每一轮** git-done 收尾的结卡步骤都退 2，DX-116（交付证据完整、
  实现/文档提交均引用 #DX-116）也被连坐结不上；
- `--close-explicit` 人工确认通道同样被击穿 —— 代码注释声称它「不经此门」，
  实现却在闸门之后。

## 验收
见 frontmatter `acceptance`（5 条，分号分隔）。核心是第 1、2 条的**粒度与通道**修复：

1. 拒结粒度收窄到「被拒的那张卡」：无证据候选逐个点名、排除出本轮落盘，
   其余候选（含 explicit）照常落盘，整体退出码 0。
2. explicit 通道回到注释声明的语义：人工确认的卡在证据闸门前摘除，不经闸门。

## 边界
scripts/derive_board_status.py;docs/pitfalls.md

- 只动结卡流程的证据闸门位置与拒结粒度；DX-91 闸门本身（无证据的自动候选不得结）
  语义不变 —— 拦的还是「无证据的卡」，只是不再外溢成整批。
- 不在本卡内擅自处置 DX-88 的存量状态：它被点名排除后维持 todo，处置
  （补交付提交引用或走 explicit 人工确认）留给维护者按 DX-111 的终态设计拍板。
- 不改 git-done.ps1（它的「派生器失败只回滚、留待下次合并」fail-soft 口径不变；
  本卡修好后派生器退出码 0，结卡自然落盘）。

## 输入
基线 origin/main（含 DX-116 合并提交 9e1c685）。复现命令与证据链见 evidence；
结卡流程在 derive_board_status.py 的 `--close-cards` 分支（forced 解析 → 候选集 →
证据闸门 → close_cards 落盘）。

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）；pitfalls 条目（§1.x 顺延编号）。
