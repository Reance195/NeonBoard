---
state: requested
card: DX-44
stage: DX
title: board.yml 把注定短暂的中间态当最终状态校验，每次收尾固定产 1~3 封失败邮件
author: agent
evidence: 机制四条叠加：① git-done 分步快进补推（合并/清理登记/结卡/快照各一次 push origin HEAD:main）② board.yml 只校验 derive --check 且不回写（R14-01 收权，permissions contents:read）③ 合并提交推上去那一刻 #短号=done 标记即生效而 baseline/快照仍旧 ④ 快照必须最后刷新（派生器要读前两步写完的 git 事实）。因此红 N 次 = 收尾推了几个不自洽中间态，确定性不是竞态。实测 2026-11-05 19:24-19:42 与 20:05 两组收尾：afd08e1/11355db/a7cce03 三连 failure 后 9a7a23a success；52c908f+4a2a6bb failure 后 7116780 success；本次 47d1ced 前后同样红两轮再转绿。本地按三个提交逐个跑 --check 得 exit 1/1/0。修复取向：board.yml 的 concurrency.cancel-in-progress 由 false 翻成 true（job 约 40s、推送间隔 8s，中间 run 被取消且不发失败通知），使判据落点自动挪到 main tip；否决「--check 区分滞后与矛盾」（收尾失败的唯一表现就是快照一直滞后，等于拆掉眼睛）。方案 A（收尾三步合并成一次补推）列二期，动 git-done.ps1 不在本卡 scope。附带发现：nbflow request 不查台账卡号占用，本请求原提 DX-42 与另一会话 03:58 立的 DX-42 撞号（已弃单重提为 DX-44），该缺口属 scripts/nbflow.py，不在本卡 scope。
scope: .github/workflows/board.yml;AGENTS.md;docs/pitfalls.md
acceptance: pwsh -File scripts/verify.ps1 -ScriptOnly 全过（只动流水线与文档，走轻档，不涉代码路径）; 中间态判据未被改松：依次 checkout 一次收尾推到 main 的三个提交（合并/结卡/快照）跑 python scripts/derive_board_status.py --check，退出码仍是 1/1/0（实测基线 R5-10 收尾 9e43177→47d1ced→3a78cc8；R5-08 收尾 52c908f=1、4a2a6bb=1、7116780=0）; 折叠生效：改后走一次真实结卡收尾，gh run list --workflow=board.yml --branch main --limit 8 中 main tip 那条为 success、中间提交对应的 run 不再产生 failure，维护者侧本批收尾失败邮件 0 封; 眼睛还在：人为只推合并不推快照的一次性演练（做完立刻补推转绿），main tip 的 board run 必须 failure 并打印 --close-cards/--write 提示; 单写者约束不动：board.yml 保持 permissions: contents: read，文件内 grep 不到任何 git commit/git push；AGENTS.md §11.2 第 8 条与文件顶部注释同步改写为「红灯只可能落在 main tip 的 run 上」; docs/pitfalls.md 补一条现象/根因/解法：拿注定短暂的中间态套最终状态判据，每次收尾固定产 1~3 封邮件（实测 R16-05 3 封、R5-08 2 封、R5-10 2 封、DX-40 立卡 1 封）
depends: 
suggestedId: DX-44
rejectReason: 
createdAt: 2026-11-04T20:09:19+00:00
---

## 目标
board.yml 把注定短暂的中间态当最终状态校验，每次收尾固定产 1~3 封失败邮件

## 验收
pwsh -File scripts/verify.ps1 -ScriptOnly 全过（只动流水线与文档，走轻档，不涉代码路径）; 中间态判据未被改松：依次 checkout 一次收尾推到 main 的三个提交（合并/结卡/快照）跑 python scripts/derive_board_status.py --check，退出码仍是 1/1/0（实测基线 R5-10 收尾 9e43177→47d1ced→3a78cc8；R5-08 收尾 52c908f=1、4a2a6bb=1、7116780=0）; 折叠生效：改后走一次真实结卡收尾，gh run list --workflow=board.yml --branch main --limit 8 中 main tip 那条为 success、中间提交对应的 run 不再产生 failure，维护者侧本批收尾失败邮件 0 封; 眼睛还在：人为只推合并不推快照的一次性演练（做完立刻补推转绿），main tip 的 board run 必须 failure 并打印 --close-cards/--write 提示; 单写者约束不动：board.yml 保持 permissions: contents: read，文件内 grep 不到任何 git commit/git push；AGENTS.md §11.2 第 8 条与文件顶部注释同步改写为「红灯只可能落在 main tip 的 run 上」; docs/pitfalls.md 补一条现象/根因/解法：拿注定短暂的中间态套最终状态判据，每次收尾固定产 1~3 封邮件（实测 R16-05 3 封、R5-08 2 封、R5-10 2 封、DX-40 立卡 1 封）

## 边界
.github/workflows/board.yml;AGENTS.md;docs/pitfalls.md

## 输入
基线 origin/main；依赖：

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）

