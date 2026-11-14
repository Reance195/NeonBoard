---
state: closed
card: DX-138
stage: DX
tier: P3
title: ScopeMatcher 三处判定缺陷：中文路径 octal 不反转义、git 失败 fail-open、diff 单向
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计（两个域的审查代理独立发现 porcelain 问题，亲验）。其一：src/core/ScopeMatcher.cpp:84-90 只剥引号并把 \ 换 /，不解 git 默认 core.quotePath 的 octal 转义——中文文件名变成 "\346\226\207..." 剥完仍是字面转义串，永远匹配不上 scope glob，越界判定（阻断性）对合法中文改动整体假阳性，triage 只读门同样误报。其二：:205-212 git 失败返回 ok:true + skipped:true 静默放行——.git 被删 / index.lock 残留 / git 超时都当「跳过检查」，安全边界 fail-open，违反宪章 Z4。其三：:222-224 只比对「after 有 before 无」——agent 还原用户未提交改动、删 untracked 文件（基线条目消失）不检测，「前后 git status 必须一致」的只读承诺只兑现一半（TriageRunner.cpp:393-398 同构）。
scope: src/core/ScopeMatcher.cpp;src/core/ScopeMatcher.h;tests/**;docs/pitfalls.md
acceptance: tests/tst_scopematcher.cpp 新增用例：porcelain 带引号 octal 转义路径（如 "docs/\346\226\207\346\241\243.md"）被反转义为真实中文名并与 scope glob 正确匹配;新增用例：git 失败（含 captureBaseline 与 check 两条路径）不再返回 ok:true+skipped，而是按 fail-closed 给出可观察的失败态;新增用例：基线有、之后消失的条目（还原/删除用户改动）计入违规或至少进入结果集（双向比对）;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）;既有越界判定与 triage 行为不回归（tst_scopematcher、tst_triage 全绿）
depends:
---

## 目标

把越界/只读判定从「看起来在做」修成「真的在做」：porcelain 引号路径按 git 的 C 转义规则反转义（octal → 字节 → UTF-8），失败不再静默放行，diff 改双向（消失的基线条目也要进结果）。

## 验收

见 frontmatter acceptance（5 条）。口径：octal 反转义以 tst_scopematcher 数据驱动用例钉住；fail-closed 以失败态断言；全量 verify 以退出码为准。

## 边界

- 不改 ScopeMatcher 的公开 API 形态与调用方（WorkflowRunner/AcceptanceRunner/TriageRunner 零改动），只修判定内部。
- 双向比对只新增「消失条目」的可见性，违规阈值与调用方的处置语义不动。
- octal 反转义只处理 porcelain 的 C 风格转义（\"、\\、\NNN），不做通配展开。
- core.quotepath 不改全局配置，判定在应用侧自行反转义（Z5：单点实现）。

## 输入

基线 origin/main（d93cf6a）。审计现场：上述 file:line；tst_scopematcher.cpp 既有用例骨架。依赖：无。

## 产出

① porcelain 反转义单点实现；② git 失败 fail-closed；③ 双向 diff；④ tst_scopematcher 新增三组用例；⑤ pitfalls 沉淀「引用路径必须反转义后再判定」。
