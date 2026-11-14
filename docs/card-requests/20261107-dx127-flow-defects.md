---
state: closed
card: DX-128
stage: DX
tier: P3
title: 收口 R20-1 交付观察：nbflow 冲突工单立卡走已退役 approve、.zcode 卡收尾闸门、README 分发口径过时
author: agent-sched-session-2026-11-07
evidence: R20-1 定时任务插件交付实录（2026-11-07）：①close 撞台账冲突时，冲突工单自动立卡调用已退役的 cmd_approve（DX-115 退出码 9），工单卡立不起来只留请求单——approve 退役时只给了人工 recipe，raise_conflict_ticket 的自动通道漏迁；②锚点区出现未跟踪 .zcode/（ZCode 会话产物），git-done 的「工作区干净」闸门拒绝合并收尾，手动删除才放行；③README.md 同页两处 plan-hook 分发口径矛盾（0.1.4 起随包分发 vs 需先手工复制）。另：交付期间撞过的 verify.ps1 fail 路径 $bad.Count 崩溃已被 DX-122（8549179）在主线根治且残留 .Count 均为数组安全模式，本卡核实后不再涉及。维护者 2026-11-07 会话明确指示「你处理下这几个缺陷」。请求单建议号 DX-128，立卡程序按台账发号实取 DX-127（register 只扫台账、不扫请求单命名空间）。查重命中的 DX-89/113/121/72 均为不同痛点（dispatch 原地守卫 / 验收档位 / 回执完整性 / board-apply 异常），新痛点成立。
scope: scripts/nbflow.py;.gitignore;README.md
acceptance: 冲突工单自动立卡改走唯一入口：raise_conflict_ticket 经专用立卡工作区调 register_board_cards.py（表单落 docs/card-requests/、两段提交、git-done 合回主线），不再调用 cmd_approve；python scripts/nbflow.py --selftest 全过且含「冲突工单走唯一入口、不含 cmd_approve 调用」判定项;anchor --simulate-conflict --dry-run 演练不回归（工单生成路径可用；完整立卡后半程需真实冲突才能走到，dry-run 与自测钉住前半程与调用面，真跑记为未执行）;gitignore 增加 .zcode/（ZCode 会话产物不再卡 git-done 干净闸门）；README.md plan-hook 分发口径统一为 0.1.4 起随安装包分发，删除「需手工复制才生效」的过时说明;pwsh -File scripts/verify.ps1 -ScriptOnly 全过（scope 无代码路径，轻档）
depends: 
suggestedId: DX-127
rejectReason: 
createdAt: 2026-11-07T02:33:59+00:00
---

## 目标
把 R20-1 交付实录暴露的三处流程缺口收口成机器判据与正确通道：
①冲突工单自动立卡漏迁 DX-115（仍调 cmd_approve → 恒 exit 9）；②ZCode 会话产物
目录 .zcode/ 落仓库根且未跟踪，卡死 git-done 干净闸门；③README 的 plan-hook
分发口径同页矛盾。另核实：verify.ps1 fail 路径 $bad.Count 崩溃已被 DX-122
（8549179）根治，残留 .Count 均数组安全模式，本卡不涉及（Z14 已扫族）。

## 验收
见 frontmatter acceptance（4 条，分号分隔）。核心是第 1 条——冲突工单是并行
协作的常规路径，approve 退役后它必须走唯一入口，否则每次撞冲突都暗失败一次。

## 边界
scripts/nbflow.py;.gitignore;README.md

- 只迁 raise_conflict_ticket 的自动立卡通道，不动 cmd_approve 本身（人工通道
  的退役提示照旧，DX-115 的 recipe 文案不改）。
- 立卡工作区沿用 _board_worktree 模式（专用 worktree + chore/board-* 分支），
  不让工单提交污染调用者检出或锚点区。
- 完整立卡后半程（真实两段提交 + 推送）需真实冲突才走到，本卡以 dry-run 演练
  + 自测判定项钉住，不做真实冲突演练（不污染台账）。

## 输入
基线 origin/main（149264f）。approve 退役前实现见 e857414~1 的 cmd_approve
（_board_worktree / worktree_alive / discard_corpse_worktree / _mark_created /
git-done 合回主线均可复用）。DX-115 的 recipe 见 APPROVE_RETIRE_NOTICE。

## 产出
scripts/nbflow.py（raise_conflict_ticket 迁移 + 模块头注同步 + selftest 判定项）；
.gitignore（.zcode/）；README.md（分发口径）；合并提交 body 四段。
