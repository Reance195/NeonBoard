---
state: closed
card: DX-131
stage: DX
tier: P1
title: 依赖前缀交接 UTF-8 会话失真：探针把 GBK 前缀解成 U+FFFD，全量 verify 编译步骤在本机必拦
author: maintainer-session-2026-11-07
evidence: R0-13 交付实测（pitfalls §7.7 D）：DX-60 已结但未收敛——nb-console 的 Set-NbUtf8Console 切 65001 后，build.ps1 探针把 cl 的 GBK /showIncludes 前缀按 UTF-8 解码成 U+FFFD 写进 prefix.bin，CMake file(READ) 再逐字节保真进 rules.ninja；而 cl 实际输出恒为系统 ANSI（GBK）字节，两者永远对不上，check_ninja_deps.py 报 296/443 个 #deps 0，build.ps1 依赖自检硬门 throw，全量 verify.ps1 的编译步骤在本机必拦（R0-13 只能以手动 cmake --build 全量佐证 + -SkipBuild 组合交付，验收口径未严格满足）。PYTHONIOENCODING=utf-8 只修 Python 子进程侧（check_qml_warnings 自测的「第 2 行」断言），治不了探针；VSLANG=1033 在 pitfalls §3.20 记录为无效（本机无英文语言包），但未在 65001 会话下实测复核。
scope: scripts/build.ps1;scripts/nb-console.ps1;scripts/check_ninja_deps.py;docs/pitfalls.md
acceptance: UTF-8（chcp 65001）与 CP936 两种控制台各跑一次 build.ps1 -Reconfigure：前缀逐字节核对与依赖条目自检均通过，#deps 0 计数为 0;prefix.bin 字节与 cl 实际 /showIncludes 前缀逐字节一致，判据固化为探针自检用例（不依赖仓库状态）;本机全量 verify.ps1 EXIT 0（编译步骤不再需要 -SkipBuild 绕行）；check_qml_warnings --selftest 不再依赖调用方预设 PYTHONIOENCODING;VSLANG=1033 在 65001 会话实测复核：有效则固化进构建链，无效则如实记录并走探针字节化方案；pitfalls §3.20 / §7.7 相应更新;缓存面不回归：改公共头文件后 ninja 重编引用它的 TU（依赖条目非空且指向本 worktree）
depends:
suggestedId: DX-131
---

## 目标

修掉「UTF-8 控制台 × 中文 Windows × cl 本地化 /showIncludes 前缀」的组合失真：让探针捕获的前缀字节与 cl 实际输出逐字节一致，全量 verify.ps1 在本机不再被编译步骤拦下，R0-13 交付时用 -SkipBuild 绕行的口径可以退役。

## 验收

见 frontmatter `acceptance`（6 条）。口径说明：

- 以**产物与实测**断言：两种控制台代码页下的 `-Reconfigure` 全程 EXIT 0、`check_ninja_deps.py` #deps 0 计数为 0、全量 verify EXIT 0。
- 探针字节一致性的判据要固化成自检用例（--selftest，临时文件构造，不依赖仓库状态），防止下一个人再踩一遍。
- VSLANG=1033 是待复核的假设不是结论：实测有效与无效都要留下记录与判据。

## 边界

- 不改 CMakeLists.txt 的前缀交接机制本身（DX-52 的 file(READ) 字节传递路径保留）。
- 不为绕过硬门放宽 check_ninja_deps.py 的判据（#deps 0 仍必须为 0 才算通过）。
- check_qml_warnings.py 的自测修复限定在子进程编码或判据 ASCII 化，不改致命 pattern 单源断言。

## 输入

基线 origin/main。既有实测：R0-13 交付日志（.sandbox/artifacts/build-reconfigure.log、verify-skipbuild2.log）、pitfalls §3.16 / §3.20 / §3.21 / §7.7。依赖：无。

## 产出

① 探针/编码修复（含 VSLANG=1033 复核结论）；② 探针自检用例；③ check_qml_warnings 自测去 PYTHONIOENCODING 依赖；④ pitfalls §3.20 / §7.7 更新；⑤ 两种控制台代码页的实测记录。立卡与实现分开两条分支、两段提交（宪章条款 6）。
