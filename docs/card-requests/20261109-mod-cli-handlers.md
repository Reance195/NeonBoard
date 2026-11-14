---
state: closed
card: MOD-10
stage: MOD
tier: P4
title: MOD CLI 派发块抽取：AppMain 的六组命令处理迁入 CliHandlers
author: codebuddy-session-2026-11-09
evidence: 2026-11-09 实测（基线 origin/main，AppMain.cpp 1105 行）：CLI 派发块合计约 600 行仍嵌在装配函数里，逐块依赖分析（花括号配对切块 + 标识符对照声明表，含 MOD-8 别名行）结论——早期组 dataMode（约 60 行，依赖 database/taskStore/dataPort）、syncMode（约 60 行，依赖 roadmap/taskStore/taskTree/taskUndo/database）、emit-hook（约 30 行，同域）、assoc/file-assoc（约 25 行，无对象依赖）四块之间穿插 boardColumns/dragFeel/keyMove/projectStore 四个散点构造（MOD-8 论证留 AppMain）；晚期组 plugin-report（约 100 行，依赖 database/taskStore/taskExtra/tray）之后构造 HookEventQueue（约 6 行，常驻对象、GUI 段 hookQueue.start(1000) 仍用），随后 market-* / install-plugin-url / preview-* / update-* / run-summary / import-ledger 合计约 330 行依赖高度重叠（pluginMarket/pluginLibrary/pluginEventBus/previewHub/updateManager/taskRuns/ledgerImport）。MOD-7 立卡时已证「CLI 块不能统一后移」，MOD-9 的 wirePluginAnnouncements 已验证「函数 + 引用参数 + 调用点原位」模式安全。维护者 2026-11-09 会话指令「开始 CLI 派发块抽取」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: src/main.cpp;src/app/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过（编译 → ctest → --selftest → --smoke → 模块守卫 → 看板一致性）;--selftest 退出码 0 且沙箱日志 SELFTEST 标签序列与基线一致（末行 result OK remaining 0，无 FAIL）;src/app/CliHandlers.h 定义 runDataMode 与 runSyncMode 与 runEmitHook 与 runFileAssoc 与 runPluginReport 五个命令函数及 LateCliRunner 结构（HookEventQueue 作成员随结构体活到 main 退出，run 以 std::optional<int> 返回：nullopt 未命中继续、值即进程退出码），AppMain.cpp 各 CLI 块改由调用点原位的函数调用接管，四个散点构造与调用点次序保持;逐条 CLI 退出码语义不变（成功 0 失败 2），抽出的命令在无头模式下行为与基线一致（--assoc-status 与 --plugin-report 与 --market-sources 退出码抽验）;AppMain.cpp 行数 <= 620（基线 1105）且 LINE_LIMITS 同步调低到实际行数;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0
depends: 
createdAt: 2026-11-09T12:10:00+00:00
---

## 目标

把 `src/app/AppMain.cpp` 里约 600 行 CLI 派发块抽进新建的 `src/app/CliHandlers.h/.cpp`：早期组四个函数（runDataMode / runSyncMode / runEmitHook / runFileAssoc，调用点保持在原位、四个散点构造穿插保留在调用点之间）、runPluginReport（HookEventQueue 构造前的独立入口）、`LateCliRunner` 结构（HookEventQueue 作成员、run 方法覆盖 market-* / install-plugin-url / preview-* / update-* / run-summary / import-ledger 全部晚期命令）。统一返回语义 `std::optional<int>`：nullopt = 未命中继续，值 = 进程退出码。装配函数退化为「调用点原位的命令分发」。

## 验收

见 frontmatter `acceptance`（6 条）。核心是第 1、2、3、4 条：全量 verify 绿、`--selftest` 与基线一致、六组命令按原次序接管、CLI 退出码语义不变。第 5 条行数棘轮防回弹。

## 边界

见 frontmatter `scope`：

- `src/app/**`：新建 CliHandlers.h/.cpp，AppMain.cpp 各 CLI 块改函数调用；
- `src/main.cpp`：薄入口不动内容（防误触列入）；
- `CMakeLists.txt`：尾部追加 CliHandlers.cpp；
- `scripts/check_module_deps.py`：LINE_LIMITS 中 AppMain.cpp 上限同步调低到实际行数。

**不动**：`--selftest` 调用（已是 runSelfTest）、`emitCliLine` / `neonboardLogHandler`（emitCliLine 迁入 CliHandlers.h 作共享 inline 或保持 AppMain static 由 CliHandlers 自带同源实现——以实现期最小改动定夺并记录）、四个散点构造、QML 注入区、启动收尾、GUI 段。不碰台账契约。

**生命周期与语义论证**：HookEventQueue 是常驻事件队列（GUI 段 hookQueue.start(1000) 仍用），搬进 LateCliRunner 成员后结构体在 main 栈上存活至函数退出，与原局部声明等价（MOD-9 BackgroundWiring 同款论证）；各函数 lambda 捕获引用参数（main 栈对象别名）；块内 `return 0/2` 即函数返回值，块外继续返回 nullopt，调用点 `if (auto rc = ...) return *rc;` 语义逐字等价。

## 输入

基线 `origin/main`。前置 MOD-7/8/9 已结卡（src/app 六类文件在位，别名清单齐）。依赖分析：逐块花括号配对切块 + 标识符对照声明表（含别名行）。

## 产出

`src/app/CliHandlers.h/.cpp`（六组命令处理）+ AppMain.cpp CLI 块接管 + CMakeLists.txt 尾部追加 + LINE_LIMITS 调低；合并提交 body 四段；交付说明单独提交，`note` 写「已完成 X / 待做 Y」与残留（散点构造与启动收尾留 AppMain 的理由、命令表与 handler 挂接属后续可选项）。
