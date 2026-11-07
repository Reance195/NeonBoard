---
state: proposed
stage: DX
tier: P3
title: QProcess FailedToStart 黑洞族：预览构建/打包队列与领用同步的状态机永久卡死
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计（grep 亲验全仓 errorOccurred 零连接）。src/board/PreviewHub.cpp 打包队列（:1154-1196）与构建阶段（:639-661）的 QProcess 只连 finished——resolveBuildSettings 兜底 cmake 是写死的 VS BuildTools 路径，主机 GUI 实例从 Explorer 启动且锚点无 build/CMakeCache.txt 时 start() 直接 FailedToStart（不发 finished），m_packageProc 永不清空、startNextPackage 永远早退，打包队列死到进程重启，当前 job 永远停在构建中；m_builds[agentId] 同理永不清理。src/board/BoardSync.cpp:101-123（m_branchProcess）与 :125-153（m_mergedProcess）同构：git 不在 GUI 实例 PATH 时 finished 不发，m_refreshing 永久卡 true，之后每 5 秒的领用刷新全部被开头 guard 吞掉——领用标牌与 todo→doing 自动流转静默停摆。四处同族一次修（Z14）。
scope: src/board/PreviewHub.cpp;src/board/PreviewHub.h;src/board/BoardSync.cpp;src/board/BoardSync.h;tests/**;tests/CMakeLists.txt
acceptance: tests 新增用例：以不存在的 program 触发 FailedToStart，断言 PreviewHub 构建状态与打包队列解锁（m_builds 条目清理、队列可继续出队、job 以失败收尾而非永久 running）;tests 新增最小用例：BoardSync 在 git 启动失败后 m_refreshing 复位（经公共状态/信号可观察）且下一轮询可重试;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）;正常路径不回归（构建成功与刷新成功行为不变）
depends:
---

## 目标

给 PreviewHub 与 BoardSync 的全部 QProcess 补 errorOccurred 处理：FailedToStart/Crashed 等未发 finished 的路径统一走「以失败收尾」——清队列游标、清 m_builds/m_packageProc、job 报错出队、m_refreshing 复位，让状态机对「起不来」这个最常见的坏配置自愈而不是永久锁死。

## 验收

见 frontmatter acceptance（4 条）。口径：以 ctest 用例断言状态机解锁；全量 verify 以退出码为准。

## 边界

- 不改预览中心的采集/展示协议与 preview-runtime.json 格式（§7.9 硬约束不动）。
- 不改 BoardSync 轮询周期与领用闸门语义——只修失败后的复位。
- 不动 exitCode 非零的既有失败处理（已正确），本卡只补「未发 finished」的缺口。
- QProcess 生命周期约定随邻：沿用本模块既有 connect 风格，不引入新抽象。

## 输入

基线 origin/main（a63ccbc）。审计现场：上述 file:line；PreviewHub/BoardSync 无既有测试文件，按 tests 既有 tst_* 模式新建最小夹具。依赖：无。

## 产出

① PreviewHub 两处 + BoardSync 两处的 errorOccurred 收口；② 新增最小 ctest 夹具两组；③ pitfalls 沉淀「QProcess 只接 finished 不接 errorOccurred」族判据（配 check 或评审口径）。
