---
state: closed
card: DX-134
stage: DX
tier: P3
title: agent 执行器三洞：超时宽限定时器 UAF、cmd 包装器引号注入、验收白名单绝对路径绕过
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计（亲验）。其一：src/agent/TaskRunner.cpp:314-320 超时宽限用 QTimer::singleShot 按引用捕获堆上 Process，cleanup（:595-608）delete &p 而该定时器无法取消——进程在 5 秒宽限内自行退出即 use-after-free；maxAttempts>1 时旧定时器看到的已是重试轮新进程，killTree 击毙自己的重试并误报 timeout（R6-08 的重试语义被破坏）。其二：TaskRunner.cpp:67-77 lossyPayloadReason 只拦换行与 %VAR% 对——cmd 包装器（cursor-agent 的 .cmd 由 Qt 6.8 套 cmd.exe /c 转调）不认 MSVCRT 反斜杠转义，prompt（来自卡片 goal）含英文双引号即可越出引号让 cmd 把 & 当命令分隔符（BatBadBut 类）。其三：AcceptanceRunner.cpp:136-142 白名单 fallback 用 QFileInfo(QDir(cwd), program) 判存在——绝对路径原样采用，C:\Windows\System32\cmd.exe 不含禁字符即放行，白名单可绕（宪章 §12.2「只允许白名单脚本路径」被开洞）。
scope: src/agent/TaskRunner.cpp;src/agent/TaskRunner.h;src/agent/AcceptanceRunner.cpp;src/agent/AcceptanceRunner.h;tests/**;docs/pitfalls.md
acceptance: tests/tst_taskrunner.cpp 新增用例：超时宽限期内进程自行退出，settle 完成后宽限定时器不再访问已释放的 Process（以成员标志/归属检查断言无迟到回调）且重试轮不被旧定时器击毙、ending 状态正确;新增用例：isCmdWrapper 的 profile 在 prompt 含英文双引号时被判 lossy 拒绝 arg 模式（文案含引号原因）;tests/tst_acceptance.cpp 新增用例：白名单外绝对路径（如 C:\Windows\System32\cmd.exe）被拒绝；cwd 内真实脚本相对路径仍放行不回归;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）
depends:
---

## 目标

把执行器安全边界的三道门按 Z14 一次修齐：宽限定时器改为可跟踪可取消的成员 QTimer（cleanup 停表 + m_processes 归属检查双保险，不再按引用悬垂）；lossy 判据补英文双引号（cmd 包装器的一切引号逃逸入口）；验收白名单 fallback 增加「解析后路径必须位于 cwd 之下」的 canonical 校验。

## 验收

见 frontmatter acceptance（4 条）。口径：三洞各有 ctest 用例（UAF 以行为断言替代内存断言：无迟到回调 + 重试轮存活）；全量 verify 以退出码为准。

## 边界

- 不改 TaskRunner 对外 Q_INVOKABLE 形态与 run 状态机终态集（§12 硬约束不动）。
- 不动 moveTask/看板列语义；执行器仍不自动 push、不自动改列。
- AcceptanceRunner 白名单语义保持「白名单 + cwd 内真实文件」两档，只收紧「cwd 内」的判定为 canonical 包含，不引入扩展名黑名单新策略。
- cmd 元字符族本卡只补双引号（引号逃逸是唯一被实证的注入入口），& 等在引号内的字面语义不动——边界在请求单明示，避免过度收紧破坏现有 profile。

## 输入

基线 origin/main（a63ccbc）。审计现场：上述 file:line；tst_taskrunner.cpp、tst_acceptance.cpp 既有用例骨架。依赖：无。

## 产出

① TaskRunner 宽限定时器生命周期修复；② lossy 判据补引号；③ AcceptanceRunner fallback canonical 收紧；④ 两测试文件新增用例；⑤ pitfalls 沉淀「按引用捕获堆对象的单发定时器」族判据。
