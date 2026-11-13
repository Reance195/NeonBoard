---
state: proposed
stage: DX
tier: P2
title: 内置 opencode profile 缺 stdin:"null"，执行器拉起即 EUNKNOWN read 退出 126
author: maintainer-session-2026-11-13
evidence: 2026-11-13 真实端到端验证实测（沙箱 harness + 本机 opencode CLI，三轮定位）。opencode（Bun 运行时）启动即读 stdin：TaskRunner 按 R9-05「都没声明 → 不碰，交给子进程继承」时，子进程拿到无效句柄 → 秒退 exit 126 `EUNKNOWN: unknown error, read`，run 结算 blocked。裸调对照（stdin 接空文件）exit 0 正常完成真实 LLM 回合；经 L2 覆盖层 saveProfile 给 opencode 设 stdin:"null" 后全链通过（run passed / acceptance 1/1 / scope 无越界 / verdict passed，产物由真 agent 落盘）。GUI 无控制台场景同型炸（GUI 进程无有效 stdin）。R9-05 的 stdin:"null"（setStandardInputFile NUL）机制即为此设计，codex 先例同因（docs/agent-profile.md §stdin）。内置三条中 opencode 是唯一被实测确认启动即读 stdin 的；claude / cursor-agent 无此症状，不动。
scope: src/agent/AgentProfile.cpp;tests/tst_agentprofile.cpp;docs/agent-profile.md
acceptance: tests/tst_agentprofile.cpp 新增断言：内置 opencode profile 的 stdin == "null"，claude / cursor-agent 的 stdin 仍为空（不误伤）;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke，退出码 0）;docs/agent-profile.md 内置 profile 表补 stdin 说明（opencode=null 及理由，引用 codex 先例）
depends:
---

## 目标

给内置 opencode profile 补 `stdin: "null"`，让执行器拉起 opencode 时子进程 stdin 接到 NUL（读到 EOF），消灭「启动即读无效 stdin → exit 126」的整族失败。修复后开箱即用，不再要求用户进设置页手工补覆盖。

## 验收

见 frontmatter acceptance（3 条）。口径：ctest 断言为准，全量 verify 以退出码为准。

## 边界

- 只改内置 opencode 这一条；claude / cursor-agent 保持现状（无证据显示它们启动读 stdin，Z14 修一处说清为何不扫全族）。
- 不改 R9-05 的三条 stdin 通道语义与优先级（TaskRunner::setupProcess 不动）。
- 不改 L2 覆盖层与迁移逻辑（migrateLegacyProfiles 不动）。
- 范围面盘点：无新增文件（CMakeLists.txt 不动）；不触及装配层（src/app/AppMain.cpp 不动）；无新模块（scripts/check_module_deps.py 不动）；仅 AgentProfile.cpp 内一处 makeBuiltin 参数化。

## 输入

基线 origin/main（d847c26）。实测现场：src/agent/AgentProfile.cpp:49-67（makeBuiltin 硬编码 `stdin: QString()`）、:284-288（opencode 条目）；tests/tst_agentprofile.cpp:152-157（既有 opencode 断言面）。依赖：无。

## 产出

① makeBuiltin 参数化 stdin（默认空串，opencode 传 "null"）；② tst_agentprofile 断言补齐；③ docs/agent-profile.md 内置表更新。
