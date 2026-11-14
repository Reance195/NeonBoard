---
state: closed
card: DX-152
stage: DX
tier: P2
title: 手工 git worktree add 绕过占用登记，-Action Check 发现不了未登记 worktree
author: qodercn-session-2026-11-09
evidence: 维护者 2026-11-09 指令「1。授权」，采纳审计轮遗留缺口 #6。实测：宪章 §7.1 规定「占用登记是强制的（DX-05）：开区即登记、收尾即释放、自检即体检」，但体检只查已登记项的自洽（锚点/工作区/登记三者），`agent-workspace.ps1 -Action Check` 不比对 `git worktree list`：任何绕过 `agent-init.ps1` 的手工 `git worktree add` 开出的工作区对占用体系完全不可见——同名分支检出、沙箱变量缺失、收尾泄漏都无从点破，「开区即登记」目前只对走正门的人成立（Z5：登记是唯一事实源，却有第二条开区路径不写它；Z6：本会话实录过清理残留目录时靠人肉数 worktree）。
scope: scripts/agent-workspace.ps1
acceptance: `-Action Check` 增补判定：`git worktree list --porcelain` 里每个 linked worktree（锚点除外），若占用登记中不存在对应 `worktree:<目录名>` 条目 → 列入硬冲突并逐个点名「未登记 worktree：<路径>」，退出码 3；`-Action Status` 对这类项标注「未登记」但维持信息性输出（Status 不新增退出码变化）；已登记项的判定行为不变（他人占用拒绝、同 agent 放行等既有语义零改动）；实测三条：当前仓库登记齐全时 Check 绿；手工 `git worktree add` 一个临时 worktree 后 Check 红且点名，`git worktree remove` 后恢复绿；`-Action Status` 在同样场景标注未登记且原有各行输出不回归；`.ps1` UTF-8 BOM 与脚本卫生检查全过；不改 `agent-init.ps1` 开区流程与登记存储格式
depends: 
rejectReason: 
createdAt: 2026-11-09T02:00:00+00:00
---

## 目标
把「开区即登记」从习惯升级成判据：体检能发现绕过正门的 worktree，未登记即硬冲突点名——占用登记表重新成为工作区存在性的唯一事实源。

## 验收
见 frontmatter acceptance。核心是第 1 条（Check 增补未登记判定 + 退出码 3）与第 4 条实测三场景；Status 只加标注、不改行为是兼容约束（既有脚本消费 Status 输出）。

## 边界
见 frontmatter scope。

- 只改 `agent-workspace.ps1`：登记读写、Claim/Release、agent-init 开区链路、check-worktree-activity 的删除判据都不动。
- 只报不删（§7.10：回收由人拍板）；未登记 worktree 的处置是补登记或回收，由维护者决定。
- 存量已登记项若缺目录（登记泄漏方向）维持既有报告口径，本卡不翻转该判定。

## 输入
基线 origin/main。`agent-workspace.ps1` 现有 Claim/Release/Status/Check 四个动作与登记键格式（root / release / worktree:<名>）；`git worktree list --porcelain` 输出结构；既有冲突分类与退出码约定（硬冲突 3）。

## 产出
`agent-workspace.ps1` 的 Check/Status 增补实现；卡片 note 记录三场景实测命令与退出码。
