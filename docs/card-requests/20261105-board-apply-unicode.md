---
state: approved
card: DX-46
stage: DX
title: board-apply.py 未 reconfigure UTF-8，GBK 控制台打 ✓ 抛 UnicodeEncodeError 让 nbflow 误判失败
author: agent
evidence: 本会话 nbflow approve 时：board-apply.py 已经把 ledger 与 baseline 写完并落 commit（91a2a15），最后一步 print('✓ 已写入 %s...') 在 Windows 默认 GBK 控制台抛 UnicodeEncodeError: 'gbk' codec can't encode character '\u2713'，进程退出码非 0，nbflow 判为失败。同批派生器 derive_board_status.py main() 开头已经 sys.stdout/stderr.reconfigure(encoding='utf-8', errors='replace')，board-apply.py 漏了。当前绕开：调用方加 PYTHONIOENCODING=utf-8 前缀。
scope: scripts/board-apply.py
acceptance: python scripts/board-apply.py <manifest> --commit --authorize --batch 不设 PYTHONIOENCODING 直接在 GBK 控制台跑，中文与 ✓/✗ 正常打印、退出码正确；pwsh -File scripts/verify.ps1 -ScriptOnly 全绿；nbflow approve 调用 board-apply 时不再需要外部设 env 兜住
depends: 
suggestedId: DX-46
rejectReason: 
createdAt: 2026-11-04T20:10:22+00:00
---

## 目标
board-apply.py 未 reconfigure UTF-8，GBK 控制台打 ✓ 抛 UnicodeEncodeError 让 nbflow 误判失败

## 验收
python scripts/board-apply.py <manifest> --commit --authorize --batch 不设 PYTHONIOENCODING 直接在 GBK 控制台跑，中文与 ✓/✗ 正常打印、退出码正确；pwsh -File scripts/verify.ps1 -ScriptOnly 全绿；nbflow approve 调用 board-apply 时不再需要外部设 env 兜住

## 边界
scripts/board-apply.py

## 输入
基线 origin/main；依赖：

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
