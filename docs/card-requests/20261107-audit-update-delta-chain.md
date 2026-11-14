---
state: closed
card: DX-136
stage: DX
tier: P3
title: 自更新增量链三洞：delta.json 路径穿越、增量 sha256 fail-open、version 拼路径 + 回滚毁备份
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计（亲验）。其一：src/update/DeltaUpdate.cpp:350 apply 循环 dst = installQDir.absoluteFilePath(toNative(change.path))，path 来自不可信 delta.json 且无任何穿越校验——同文件 :429 对 obsolete-files.txt 明确做了防护并写明动机，同一条信任边界双标。其二：UpdateManager.cpp:1045 增量包 sha256「清单给了才校验」缺失即放行，而全量路径 verifyFile（:1275-1280）清单没给哈希时明确拒绝——同一清单格式两条通道安全姿态相反，与其一组合即任意写。其三：UpdateManager.cpp:892-894（及 :1002/:1120/:1251 同族）version 无字符白名单直接拼 %TEMP%/neonboard-update-<ver>.exe，downloadAndInstall 入口的 QFile::remove 即任意文件删除。其四：DeltaUpdate.cpp:392-404 回滚时 QFile::copy 返回值被丢，随后无条件 removeRecursively 销毁唯一备份——恢复失败即旧文件永久丢失（Z13）。
scope: src/update/DeltaUpdate.cpp;src/update/DeltaUpdate.h;src/update/UpdateManager.cpp;src/update/UpdateManager.h;src/core/SafePaths.h;src/core/SafePaths.cpp;CMakeLists.txt;tests/**;docs/pitfalls.md
acceptance: tests/tst_updatemanager.cpp 新增（或新增 tst_deltaupdate）用例：delta.json 的 path 或 patchPath 含 ../ 与盘符写法的增量包整单拒绝且安装目录无任何文件落盘;用例：清单声明 deltaUrl 但 deltaSha256 为空时在应用前被拒绝（文案含 sha256，fail-closed 口径与全量路径一致）;用例：version 含路径分隔符或 .. 的清单在解析阶段即拒绝（parseManifest 单点校验）;用例：applyDelta 应用失败且回滚 copy 失败时备份目录保留并报错，不再静默删除;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）;全量更新通道行为不回归（既有 tst_updatemanager 全绿）
depends: DX-132
---

## 目标

把自更新链的信任边界补齐：delta.json 的 path/patchPath 复用 DX-132 落地的 SafePaths 判定（Z5，不造第二份）；增量包 sha256 缺失改为 fail-closed（与全量通道同口径）；version 在 parseManifest 单点做字符白名单（单点即全族，892/1002/1120/1251 四处自然收口）；回滚 copy 失败时保留备份目录并报错。

## 验收

见 frontmatter acceptance（6 条）。口径：四洞各有 ctest 用例（产物级：不落盘/备份保留）；全量 verify 以退出码为准。

## 边界

- 不改 delta.json 清单格式与 bspatch 协议（只加校验）；不改全量更新通道。
- extractZip 的 Zip Slip 防护依赖外部 tar 行为，属独立疑点（审计 #7 疑似），本卡不改、留 pitfalls 记录。
- version 白名单只放行版本号合法字符（字母数字与 . _ + -），拒绝后给出明确文案，不尝试「修正」版本号。
- SafePaths 复用 DX-132 的实现，本卡不得复制第二份判定。

## 输入

基线 origin/main（a63ccbc）。审计现场：上述 file:line；tst_updatemanager.cpp 既有用例骨架。依赖：DX-132（SafePaths 先落）。

## 产出

① DeltaUpdate 路径校验 + 回滚保备份；② UpdateManager 增量 sha fail-closed + version 白名单单点；③ ctest 新增四组用例；④ pitfalls 沉淀「同一信任边界多通道必须同口径」。
