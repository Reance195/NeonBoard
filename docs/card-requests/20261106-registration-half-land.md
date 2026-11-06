---
state: approved
card: DX-121
stage: DX
title: 立卡半落地：回执/占位在而台账卡片被并发冲突抹去，回执闸门只查单向
author: agent-retro-2026-11-06 / 维护者 2026-11-06 会话批准
evidence: 2026-11-06 并发立卡实录：7ee6040 chore(board): 立卡 DX-111、DX-112 的 docs/board-ledger.json diff 仅「去掉文件末尾换行」，却新增 docs/card-requests/registered/DX-111.json 与 DX-112.json 两份回执，服务端 refs/registrations/DX-111、DX-112 原子占位亦已落地；该提交经 e059469 merge: 合入 chore/board-dx111-dx112 进 main 后台账 243 张卡、无 DX-111/112，形成「回执+占位在、卡片定义不在」的半落地。根因：并发 rebase/合并的台账冲突按「主线版」语义重建，抹掉 register_board_cards.py 写入的 cards/baseline 增量；回执是新增文件、占位是远端 ref，不在同一冲突面而幸存。闸门缺口：scripts/task_gate.py:397 的 receipt_problems 只遍历 set(new_cards)-set(old_cards) 保证「新增卡必须有回执」，从不校验反向「有回执/占位却无卡」，故 chore(board): 立卡 提交台账 diff 为空或纯空白也照过 commit-msg 与 pre-push；derive --check 只看台账↔快照自洽，少一张卡照样绿。发现与修复全靠人肉：0615b3a chore(board): 补落 DX-111、DX-112 卡片定义（rebase 误解析重放）+ 1b2b131 合入后当前 main 已完整（248 张卡），但闸门缺口原样保留，9258113 等重复补收录请求单说明半落地仍在反复发生。查重：DX-98（新增卡必须有回执）、DX-103/104/105（发号占位原子性）均为正向；DX-115/119 是 nbflow approve 通道被回执闸门击穿；DX-116 是 git-done 假成功输出。无一张覆盖反向完整性对账。
scope: scripts/task_gate.py;scripts/register_board_cards.py;scripts/verify.ps1;.githooks/;docs/pitfalls.md;.github/workflows/ci.yml
acceptance: 新增一致性检查：扫 docs/card-requests/registered/*.json 与服务端 refs/registrations/*，凡回执/占位存在而台账 cards 无对应卡、或卡片规范 JSON 与回执 sha256 不符 → 非零；接入 commit-msg 与 CI，且不误报历史在办/已结卡;chore(board): 立卡 提交的台账 diff 若不含任何 cards/baseline 新增（含仅空白或换行变化）→ 拒，直接命中 7ee6040 形态；判定抽纯函数进 task_gate selftest;回归用例覆盖「回执在卡不在」与「立卡提交台账纯空白变化」两个半落地必须拒，以及 DX-98 正向口径（新增卡无回执被拒 / 有回执放行）与正常立卡不回归，python scripts/task_gate.py selftest 全过;并发冲突解决护栏：立卡分支 rebase/merge 后台账语义合并必须保留程序写入的卡片定义，或至少让不一致在闸门层可见；造一次并发冲突重放验证不再静默半落地;docs/pitfalls.md 记一条现象/根因/解法，点名 7ee6040 / e059469 / 0615b3a；pwsh -File scripts/verify.ps1 -ScriptOnly 全绿，CI 接入后 main 上该检查恒绿
depends: 
suggestedId: DX-121
rejectReason: 
createdAt: 2026-11-06T00:00:00+00:00
---

## 目标
把立卡从「只保证新增卡有回执」补成「回执/占位/请求单 ↔ 台账卡片 ↔ 快照」双向自洽：并发冲突抹掉卡片定义这类半落地要在闸门层被拦下，不再靠人肉发现与重放 JSON。补落（0615b3a）是临时方案，本卡要的是程序化对策。

## 验收
见 frontmatter `acceptance`（5 条，分号分隔）。核心是第 1、2 条：反向完整性检查 + 立卡提交台账 diff 形态校验，两者都要有可执行自测与回归用例。

## 边界
scripts/task_gate.py;scripts/register_board_cards.py;scripts/verify.ps1;.githooks/;docs/pitfalls.md;.github/workflows/ci.yml

## 输入
基线 origin/main；依赖：无（DX-98/103/104/105 已合入，其正向口径须回归不破但非前置）。

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）。
