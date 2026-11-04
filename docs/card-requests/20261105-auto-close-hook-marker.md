---
state: approved
card: DX-41
stage: DX
title: 自动结卡提交缺 # 标记：plan-hook 收不到 done、看板留僵尸 doing
author: agent
evidence: 现场：progress.json 与 ledger baseline 都已把这 6 张卡判为 done；board.db 里它们仍停在 doing、completed_at=0。根因：git-done 收尾的自动结卡提交形如 chore(board): 结卡 R16-05（标题短号不带 #），post-commit 用 grep -Eq '#[A-Za-z][A-Za-z0-9]*' 判标记，判不到就不投 hook.commit；plan-hook 因此收不到 done 信号。RoadmapSync file→board 兜底通道又被 fileUpdated<boardUpdated 的冲突判据保留看板版本（board.db 里这些卡的 updated_at 都晚于 progress.json 里的 updatedAt），僵尸回不来。
scope: scripts/derive_board_status.py;scripts/git-done.ps1
acceptance: python scripts/derive_board_status.py --selftest 通过（含新用例：--close-cards 生成的提交标题带 #<code>=done、body 保留「自动来源：」行；replay 通道按 ledger baseline=done 生成对应 hook 事件文件）;pwsh -File scripts/verify.ps1 -ScriptOnly 全过（含 check_script_hygiene 的 BOM/YAML/hard-coded-path 检查）;在一次性沙箱检出里跑 --close-cards 后，git log 最新一条 chore(board) 提交标题形如 chore(board): 结卡 #<code>=done，且 .githooks/post-commit 触发后 hooks.db 里能看到对应 hook.commit 事件;跑 --replay-closed 后，看板 board.db 里 R9-04/DX-26/R15-02/R16-02/R16-03/R16-05 六张僵尸从 doing 列消失、completed_at 非零（由维护者本机主实例消费 hooks.db 事件后一次性核对，不在本卡 CI 断言内）
depends: 
suggestedId: DX-41
rejectReason: 
createdAt: 2026-11-04T19:44:53+00:00
---

## 目标
自动结卡提交缺 # 标记：plan-hook 收不到 done、看板留僵尸 doing

## 验收
python scripts/derive_board_status.py --selftest 通过（含新用例：--close-cards 生成的提交标题带 #<code>=done、body 保留「自动来源：」行；replay 通道按 ledger baseline=done 生成对应 hook 事件文件）;pwsh -File scripts/verify.ps1 -ScriptOnly 全过（含 check_script_hygiene 的 BOM/YAML/hard-coded-path 检查）;在一次性沙箱检出里跑 --close-cards 后，git log 最新一条 chore(board) 提交标题形如 chore(board): 结卡 #<code>=done，且 .githooks/post-commit 触发后 hooks.db 里能看到对应 hook.commit 事件;跑 --replay-closed 后，看板 board.db 里 R9-04/DX-26/R15-02/R16-02/R16-03/R16-05 六张僵尸从 doing 列消失、completed_at 非零（由维护者本机主实例消费 hooks.db 事件后一次性核对，不在本卡 CI 断言内）

## 边界
scripts/derive_board_status.py;scripts/git-done.ps1

## 输入
基线 origin/main；依赖：

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
