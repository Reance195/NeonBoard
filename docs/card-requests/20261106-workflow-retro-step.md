---
state: closed
card: R19-1
stage: R19
title: workflow 新增 retro 复盘步骤：agent 干完活后自省并沉淀程序化对策
author: agent-retro
evidence: 2026-11-06 维护者会话。现状：角色链（R7-03/04）所有预设到 qa / orchestrator 为止，链失败即 fail-fast 收口，orchestrator 的 summary 只有一句「回到 engineer 重跑」；执行过程中碰到的问题不落任何结构化产物，下次同类任务还会踩。维护者要求让 agent 干完活后自省、并把问题沉淀为程序化对策（对应 AGENTS.md Z6「踩坑两次沉淀成机器判据」）。
scope: src/agent/WorkflowPresets.cpp;src/agent/WorkflowPresets.h;src/agent/WorkflowRunner.cpp;src/agent/WorkflowRunner.h;src/task/TaskExec.cpp;src/task/TaskExec.h;tests/
acceptance: ctest --test-dir build -C Release -R "tst_workflowpresets|tst_workflow" --output-on-failure 通过，新增用例覆盖：TaskExec::isKnownRole 认识 retro、六条内置预设角色序在 qa 之后插入 retro（两处 presetSteps/builtinPresets 断言一致）、小规模裁剪可裁 retro 且 isProtectedRole 不含 retro;WorkflowRunner 用例：retro 步 prompt 含三段式交付物要求（问题清单/根因/程序化对策）、retro handoff 落盘 .neonboard/handoff/<task>/retro.md、链失败时 retro 仍执行且其失败只告警不改变链 failed 结论（fail-soft）;buildSummary 产出含「复盘结论」段并引用 retro handoff 路径（纯函数断言）;build/Release/NeonBoard.exe --selftest 退出码 0;pwsh -File scripts/verify.ps1 全量通过
depends: R7-04
suggestedId: 
---

## 目标
在角色链执行器里加一个正式的复盘步骤：卡进入执行后，qa（或失败收口）之后自动拉起 retro 角色的 agent 跑一次自省，产出「问题清单 → 根因 → 程序化对策」三段式交接物，让失败与坑位可被人和下一次执行消费，而不是随链收口一起蒸发。

## 验收
见 frontmatter `acceptance`（6 条）。设计要点：retro 挂在执行链上、不挂在卡片状态机上（不动看板列、不动 schema、不升 user_version）；链失败时 retro 仍运行（复盘最需要失败现场），retro 自身按 Z4 定性为收尾装饰类 → fail-soft，失败只告警。

## 边界
只改 workflow 预设与执行链两侧（WorkflowPresets / WorkflowRunner / TaskExec）及测试；不改 AcceptanceRunner / ScopeMatcher / TaskRunStore / board.db schema；复盘产物只落 `.neonboard/handoff/`（数据目录，不进仓库，与 runs/ 同待遇）；不自动改看板列、不自动 push（宪章 §12.2 既有硬约束原样生效）。

## 输入
基线 origin/main；依赖 R7-04（预设与 orchestrator 收口已交付）；维护者已在 2026-11-06 会话中明确授权本卡（「行吧，就这么办吧」+「授权」）。

## 产出
① TaskExec::isKnownRole / presetSteps 增 retro 角色；② 六条内置预设插入 retro 步（qa 后、orchestrator 前，小规模可裁）；③ WorkflowRunner：retro prompt 三段式契约、失败链仍跑 retro、fail-soft 结算；④ buildSummary 增「复盘结论」段；⑤ tests/tst_workflowpresets、tests/tst_workflow 新增用例；⑥ 本请求单批准后由 `python scripts/register_board_cards.py --form <本文件> --authorized-by <维护者会话授权>` 发号。
