---
state: approved
stage: DX
title: agent 契约注入转义：含单引号的验收项不再让 .agent-env.ps1 语法报错
author: maintainer-session-2026-11-06
evidence: 2026-11-06 本会话领用 DX-97 后 dot-source .agent-env.ps1 实测 ParserError（第 11 行「表达式或语句中包含意外的标记 z:」），NB_CARD / NB_SCOPE / NB_ACCEPTANCE 全为空。根因是 scripts/agent-init.ps1 把 acceptance 用半角分号拼进单引号字符串却未转义单引号，而 DX-97 验收项含 rg -n 加引号的 z: 1。本次只在该 worktree 本地把单引号双写绕过，脚本本身未修。
scope: scripts/agent-init.ps1
acceptance: 生成的 .agent-env.ps1 对含单引号的 acceptance 可被 PowerShell 7 正常 dot-source 且退出码 0;NB_ACCEPTANCE 与台账 acceptance 逐字一致（含单引号与中文），用 python 读台账与生成文件比对;PowerShell Parser 对含单引号/双引号/中文分号三类验收项生成的脚本解析无语法错误;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
suggestedId: DX-99
---

## 目标
把 `agent-init.ps1` 的任务卡字段注入做成正确转义的拼装，验收项里出现单引号不再让整份 `.agent-env.ps1` 语法失败，也不改变注入内容的语义。

## 验收
见 frontmatter `acceptance`（4 条）。核心是「生成的脚本可解析」加「注入值与台账逐字一致」，两头都不能只看动作。

## 边界
只改 `scripts/agent-init.ps1` 的注入拼装，不动领用、占用登记、网络预检与 worktree 流程，不改台账结构，不新增依赖。

## 输入
基线 `origin/main`；依赖：无；证据来自本会话 DX-97 开区实测与本地修复记录。

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片），并在 `docs/pitfalls.md` 留一条「生成 PowerShell 字符串必须转义单引号」的判据说明。
