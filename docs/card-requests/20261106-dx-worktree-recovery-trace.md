---
state: proposed
stage: DX
title: 工作区回收静默失败无追踪：nbflow 临时区 Permission denied 与 .worktrees 未注册目录靠人肉发现
author: agent-flowguard
evidence: 2026-11-06 会话实录：nbflow approve 收尾 worktree remove Permission denied 静默降级，_nbflow-board-2026-11-06 目录与 chore/board-2026-11-06 分支残留；.worktrees/ 下 5 个未注册目录（agent-dx35/dx41/dx42/dx47-close/agent-memrev）与 dx61-verify*.log 靠人肉 ls 发现。与 DX-66（开区半途回滚）、DX-109（分支删除判据）互补，本卡只管回收侧追踪与体检点名
scope: scripts/nbflow.py;scripts/agent-workspace.ps1;.gitignore;docs/pitfalls.md
acceptance: nbflow 立卡工作区回收失败（如 Permission denied）自动重试至少 2 次，仍失败落显式待回收记录（state.json 或 receipts），status 点名，不再只打一行「不影响主线」；agent-workspace.ps1 -Action Check 点名 .worktrees/ 下未注册目录与根下散落产物（dx61-verify.log 类），当前 main 上 agent-dx35、dx41、dx42、dx47-close、agent-memrev 全部被点名；点名只报告不删除（§7.10 回收由人拍板）；.gitignore 点名 .worktrees/*.log；python scripts/nbflow.py --selftest 全过（回收重试用例入自测），pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
suggestedId: 
---

## 目标
工作区回收静默失败无追踪：nbflow 临时区 Permission denied 与 .worktrees 未注册目录靠人肉发现

## 验收
nbflow 立卡工作区回收失败（如 Permission denied）自动重试至少 2 次，仍失败落显式待回收记录（state.json 或 receipts），status 点名，不再只打一行「不影响主线」；agent-workspace.ps1 -Action Check 点名 .worktrees/ 下未注册目录与根下散落产物（dx61-verify.log 类），当前 main 上 agent-dx35、dx41、dx42、dx47-close、agent-memrev 全部被点名；点名只报告不删除（§7.10 回收由人拍板）；.gitignore 点名 .worktrees/*.log；python scripts/nbflow.py --selftest 全过（回收重试用例入自测），pwsh -File scripts/verify.ps1 -ScriptOnly 全绿

## 边界
scripts/nbflow.py;scripts/agent-workspace.ps1;.gitignore;docs/pitfalls.md

## 输入
基线 origin/main；依赖：

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）
