---
state: closed
card: DX-150
stage: DX
tier: P2
title: 宪章硬性约定 6「界面行为改动同步 README」无机器执行，靠代理自觉
author: qodercn-session-2026-11-09
evidence: 维护者 2026-11-09 指令「1。授权」，采纳审计轮遗留缺口 #3。实测：全部钩子、verify.ps1、三个 workflow 里没有任何一处把「qml/ 或 shaders/ 有改动而 README.md 未同步」与机器判据挂钩（grep README 在 .github/workflows/ 零命中）；宪章 §五 硬性约定 6 属体验类规范，按 Z4 口径可以 fail-soft，但「必须告警」——当前连告警都没有，静默吞错即缺陷（Z6 同类坑已在多会话返工中出现：界面改动合并后 README 描述过期）。
scope: scripts/check_readme_sync.py;.github/workflows/ci.yml
acceptance: 新建 `scripts/check_readme_sync.py`：`--base <ref>` 读指定范围内提交的改动路径，命中 `qml/` 或 `shaders/` 路径而该范围内 `README.md` 无任何改动时，逐条打印未同步提交并输出 `::warning::` 摘要，警告级退出码恒 0（不阻断，Z4 体验类口径）；`--selftest` 用临时 git 仓库覆盖三类：UI 改动+README 同改=静默、UI 改动无 README=告警且点名提交、纯非 UI 改动=静默，全过；`.github/workflows/ci.yml` 增加一步调用本脚本（对比 base 用该次 CI 的基线）；`pwsh -File scripts/verify.ps1 -ScriptOnly` 全过；不改钩子、不新增阻断闸门、不动 hygiene 规则 B/D 的零豁免面
depends: 
rejectReason: 
createdAt: 2026-11-09T02:00:00+00:00
---

## 目标
给宪章 §五 硬性约定 6 装上第一个机器执行者：CI 侧警告级「界面改动未同步 README」检查——不阻断交付，但让漏同步每次都被点名，不再依赖散文自觉。

## 验收
见 frontmatter acceptance。核心是第 2、3 条：selftest 三类场景全绿、ci.yml 步骤真实调用；第 1 条的「警告级恒 exit 0」是定位约束，做成阻断即超范围。

## 边界
见 frontmatter scope。

- 界面路径只认 `qml/`、`shaders/` 两个前缀，不做 src/ 内容语义判定（启发式越准越容易误杀，警告级够用先跑起来）。
- 「已同步」只判范围内 README.md 出现过改动，不校验改的段落是否对应功能项。
- 不回扫历史；只判本次 CI 构建的增量范围。
- 不改任何钩子与本地阻断闸门；若日后要升级为阻断，另行立卡。

## 输入
基线 origin/main。宪章 §五 硬性约定 6 原文；ci.yml 现有步骤结构与 python 环境；同类 selftest 风格参照 `check_secrets.py`/`task_gate.py`（临时目录构造、断言退出码与输出）。

## 产出
`scripts/check_readme_sync.py`（检查 + --selftest）、ci.yml 新步骤；卡片 note 记录 selftest 三类结果与 CI 步骤链接。
