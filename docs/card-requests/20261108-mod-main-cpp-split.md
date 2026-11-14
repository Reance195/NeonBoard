---
state: closed
card: MOD-7
stage: MOD
tier: P4
title: 模块化续批：建立 src/app 边界并把 main.cpp 从 1910 行降到 450 行以内
author: codebuddy-session-2026-11-08
evidence: 2026-11-08 实物复测（基线 origin/main）：src/main.cpp 1910 行（LF 实测，与 check_module_deps.py 的 LINE_LIMITS 一致），CLI 派发 10 块共 848 行，其中 --selftest 独占 607 行（522-1128），setContextProperty 46 处（406-1729），return 0 共 22 处、return 2 共 23 处；--selftest 是 GUI 子系统程序，stdout 恒为空，真实输出走 qInfo 落沙箱 neonboard.log（34 行，末行 SELFTEST result OK remaining 0），故方案 v4 的「逐字节比对 stdout」不成立，改以退出码 + 日志 SELFTEST 标签序列 + result OK 作基线。docs/module-split-plan.md v4 §5 M1 目标 <=450 行，§2.2 已证「CLI 分支穿插在对象构造之间不能整体剪切」，§7.4 把 MOD-03/MOD-04 列为可穿插的下一批且从未立卡；台账 baseline 仅 MOD-02 / MOD-05 / MOD-06 / DX-18 为 done，后续批次一张卡都没有，docs/card-requests/ 下也无已递交未立的模块化请求单，路线图 §9 无 **MOD** 行。维护者 2026-11-08 会话指令「继续实现模块化」，在证据链与台账核对后选定「拆 main.cpp（推荐）」+「完整闭环（推荐）」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: src/main.cpp;src/app/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过（编译 → ctest → --selftest → --smoke → 模块守卫 → 看板一致性）;src/main.cpp 行数 <= 450（基线 1910）且只保留薄入口（声明 neonboardMain 并转调）;新增 src/app/ 目录含 AppMain 与 CliCommands 与 SelfTestRunner 与 AppContext 四类实现，CMakeLists.txt 的 qt_add_executable 源列表尾部追加这些 .cpp;--selftest 退出码 0 且沙箱 neonboard.log 含 SELFTEST result OK 且无 FAIL 行，SELFTEST 标签序列与重构前基线一致;逐条 CLI 退出码语义不变（成功 0 失败 2），新增一条 utility 命令后仍走无头模式不拉起 GUI;python scripts/check_module_deps.py --base origin/main 退出 0 且 LINE_LIMITS 中 src/main.cpp 上限已从 1910 同步调低到实际行数;python scripts/check_scope.py --base origin/main 退出 0
depends: 
createdAt: 2026-11-08T13:20:00+00:00
---

## 目标

把 1910 行的 `src/main.cpp` 拆进新建的 `src/app/` 模块目录，`main.cpp` 退化为薄入口（≤450 行），同时抽出 CLI 命令表、自检脚本与 QML 上下文注入。承接 `docs/module-split-plan.md` v4 §5 的 M1 / M1.5（方案对照号 MOD-03 / MOD-04），在 MOD-06 已完成的 `src/` 八模块分层之上补齐「应用装配层」边界。

## 验收

见 frontmatter `acceptance`（7 条，分号分隔）。核心是第 1、2、4 条：全量 verify 绿、`main.cpp` ≤450 行、`--selftest` 行为与基线一致。第 6 条要求同步调低 `LINE_LIMITS` —— 守卫注释明确「上限 = 当前实际行数，拆完不调低等于把技术债合法化」。

## 边界

见 frontmatter `scope`，四条路径各有不可省的理由：

- `src/main.cpp`：改薄入口；
- `src/app/**`：新建的应用装配层（AppMain / CliCommands / SelfTestRunner / AppContext）；
- `CMakeLists.txt`：按 §7.4 热点文件约定，只在 `qt_add_executable` 源文件列表**尾部追加**新 `.cpp`；
- `scripts/check_module_deps.py`：把 `LINE_LIMITS["src/main.cpp"]` 从 1910 调低到实际行数。

不碰 `src/` 下八个既有模块目录、不碰 `qml/`、不碰台账 `cards` 契约、不碰 `progress.json`。

## 输入

基线 `origin/main`（多 agent 并发仓，动手前 rebase）。前置全部已结：MOD-02 路径解耦、MOD-05 守卫接入 CI、MOD-06 `src/` 物理分层、DX-18。实物：`src/` 顶层只剩 `main.cpp`，core/task/plugin/agent/board/shell/update/theme 八子目录非空（+ host）。构建环境齐全：Release CMakeCache、MSVC 14.44、Qt 6.8.3、Ninja、sccache 已真实生效，`build/NeonBoard.exe` 可直接取重构前基线。

**实施策略（行为优先、分阶段）**：方案 §2.2 已证 CLI 块穿插在对象构造之间，不能整体剪切，故按「任意一步死掉后原样重跑都能恢复」的粒度推进，每一步都是可交付状态：

1. 纯搬迁：把 `main()` 函数体整体移入 `src/app/AppMain.cpp` 并改名 `neonboardMain`，`main.cpp` 只留薄转调 —— 函数体一字不改，行为 100% 不变，此时 ≤450 行已达成；
2. 抽 `--selftest` 607 行到 `src/app/SelfTestRunner.cpp`（只依赖早期构造的对象）；
3. 抽其余 CLI 派发到 `src/app/CliCommands.cpp`，`CliCommand` 一次带齐 `arity` / `repeatable` / `CliMode`，`utilityMode` 由 `allCommands()` 反推以保单点（方案 R7 / R8）；
4. 46 个 `setContextProperty` 收进 `src/app/AppContext.cpp` 批量注入；
5. 同步调低 `LINE_LIMITS`。

每一步重编译并比对 `--selftest` 退出码与 `neonboard.log` 的 SELFTEST 序列。M1d 对象构造工厂（约 700 行、构造次序敏感）不在本批，留作 `nextStep`。

## 产出

`src/app/` 四个实现文件 + 薄 `main.cpp` + `CMakeLists.txt` 尾部追加 + `LINE_LIMITS` 调低；合并提交 body 四段（变更 / 验收 / 影响 / 卡片）；交付说明单独一条 `docs(board): 补 MOD-7 交付说明`，`note` 写「已完成 X / 待做 Y」并给出 M1d 的 nextStep。
