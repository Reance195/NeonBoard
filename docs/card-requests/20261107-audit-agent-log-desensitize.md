---
state: proposed
stage: DX
tier: P3
title: agent 日志脱敏两处漏配与 triage 启动失败报成功
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计。其一：src/agent/TaskRunner.cpp:83-85 凭据正则对「Authorization: Bearer eyJhbG...」只捕获到 "Bearer" 一词——真正令牌留在日志里，JWT 是 base64url 不被 prefixedTokenPattern（sk-/ghp_/xox）与 longHexPattern（纯 hex）命中，明文凭据落 runs/<id>.log。其二：TriageRunner.cpp:505-509 raw（agent 输出末 4000 字符）未脱敏就写进 board.db meta，与 TaskRunner 日志脱敏口径不一致。其三：TriageRunner.cpp:356-358 launch() 内 waitForStarted 失败会同步 finish()（删 session、发 failed 信号）但 start() 仍返回 {ok:true}——启动失败被报成成功，调用方（QML/CLI）拿到假成功。
scope: src/agent/TaskRunner.cpp;src/agent/TaskRunner.h;src/agent/TriageRunner.cpp;tests/**;docs/pitfalls.md
acceptance: TaskRunner::desensitize 收敛为公开静态纯函数（沿 failureSummary 先例）后，tests/tst_taskrunner.cpp 新增用例：「Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.签名段」整串被替换为占位符，普通 bearer/authorization 键值对不回归;新增用例：TriageRunner 的 raw 落库路径经 desensitize（以公开静态函数断言或夹具观测 meta 值不含原始凭据）;新增用例或日志证据：triage 启动失败时 start 返回 ok:false 且错误文案可读;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）
depends:
---

## 目标

补齐脱敏的两类漏配（JWT Bearer 整串、triage raw 落库）并把 triage 的启动失败汇报改成诚实失败（fail-open 汇报违反 Z9）。

## 验收

见 frontmatter acceptance（4 条）。口径：脱敏以公开静态纯函数直接断言；triage 启动失败以返回值/信号为准。

## 边界

- 脱敏正则只新增「Bearer/authorization 后随 JWT 形态」的捕获，不扩大到任意长 base64（避免把构建产物哈希等诊断信息全灭）。
- desensitize 收敛公开静态是沿 TaskRunner::failureSummary 的既有先例，不改行为只改可见性。
- longHexPattern 误伤 git SHA 的低危问题（诊断价值受损）本卡不动，留 pitfalls 记录。

## 输入

基线 origin/main（d93cf6a）。审计现场：上述 file:line；tst_taskrunner.cpp 既有 redactsCredentialsInLog 用例骨架。依赖：无。

## 产出

① JWT/authorization 脱敏补齐；② triage raw 脱敏；③ triage 启动失败诚实汇报；④ 测试用例；⑤ pitfalls 记录 longHex 误伤的待决项。
