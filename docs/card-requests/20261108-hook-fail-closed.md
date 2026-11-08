---
state: proposed
stage: DX
tier: P1
title: pre-commit 与 commit-msg 缺 python 时打一行警告就放行，与 pre-push 的 fail-closed 自相矛盾
author: qodercn-session-2026-11-08
evidence: 维护者 2026-11-08 会话指令「全做」→「授权」，采纳审计轮档位二缺口 #1 的建议。实测：本仓库工作机上 `python` 被 WindowsApps 应用执行别名占位（exit 9009 无输出），本会话首次提交即被这种「静默拒绝」咬过一次，只能靠给每条 git 命令手动前置真 python PATH 才能过闸——这正是失败模式没事先定（AGENTS.md Z4）的症状。宪章 Z4 明文「数据、安全、审计类一律 fail-closed」，§三 表格也宣称 commit-msg 拦「代码改动未关联台账短号」；但 `.githooks/commit-msg` 在 python 不可用时只打印 `⚠ 未找到 python，跳过任务关联校验` 便以 0 退出，`pre-commit` 同样跳过台账保护校验放行。同族对照：`pre-push` 已实现 fail-closed（`❌ … 已阻止推送`，exit 1）——三条钩子对同一判据给了两种答案，违反 Z14「修一处扫一族」，第一道防线在最需要它的机器上静默退化成建议。宪章「五」Z4 先例已按此登记待修（commit 493af70）。
scope: .githooks/pre-commit;.githooks/commit-msg
acceptance: 在 `python` 与 `python3` 均不可用的环境里（含 stub 命中但解析失败的场景），向非豁免路径做代码改动提交时，pre-commit 与 commit-msg 必须打印点名处置动作的响亮告警（指向 `. /scripts/dev-env.ps1` 与本机真 python 路径）并以非 0 退出，提交不落地——用 `PATH` 剔除真 python 的临时环境实测；纯文档/杂务路径改动在同样环境下必须照常放行（豁免面不因本改动扩大或缩小）；`pre-commit`/`commit-msg`/`pre-push` 三条钩子对「python 不可用」给出一致的判定与同一段处置文案（判据一处实现）；`pwsh -File scripts/verify.ps1 -ScriptOnly` 全过；钩子文件保持 LF 行尾、无 BOM
depends: 
rejectReason: 
createdAt: 2026-11-08T13:20:00+00:00
---

## 目标
把 pre-commit 与 commit-msg 在 python 不可用时的行为从「警告后放行」改为「响亮失败」，与 pre-push 统一成同一种失败模式，恢复宪章 §三 表格承诺的拦截能力（Z4 审计类 fail-closed、Z14 同族写法一次修完）。

## 验收
见 frontmatter acceptance（5 条，分号分隔）。核心是第 1 与第 3 条：剔除真 python 的实测里代码提交必须被拒且文案给出可执行的处置；三条钩子的判定与文案出自同一处。第 2 条守住「不扩大豁免面」。

## 边界
见 frontmatter scope。

- 只改两条钩子，不动 `task_gate.py` 的判定逻辑、不改 `.githooks/pre-push` 已有行为（它已是本卡要求的形态，只作对照基准）。
- 不新增任何豁免开关：`--no-verify` 保持零豁免（`docs/adr/0003`），也不引入「跳过 python 校验」的环境变量。
- 不改钩子的其它判定（分支前缀、主分支禁提交、快照闸等）与退出码语义。

## 输入
基线 origin/main。对照实现在 `.githooks/pre-push`（python 缺失 → 响亮 ❌ + exit 1）；判据背景见 `AGENTS.md` Z4/Z14 与宪章 §三；先例登记见附录「五」Z4 条目（commit 493af70）；本会话踩坑事实见 evidence。

## 产出
两条钩子的 fail-closed 分支与共用处置文案；合并提交 body 说明三条钩子判定如何收口到一处；实测记录（临时剔除真 python 后代码提交被拒 + 文档提交照常）写进卡片 note。
