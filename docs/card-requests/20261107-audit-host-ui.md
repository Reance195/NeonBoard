---
state: closed
card: DX-143
stage: DX
tier: P3
title: 宿主与界面三处：热键注册静默失败、托盘不随 Explorer 重建、执行队列面板每秒整表重建
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计。其一：src/host/HostMain.cpp:339 RegisterHotKey 返回值被忽略——热键被占用时 Ctrl+Shift+Space 静默失效而 HUD 仍在宣传它（SplashScene.cpp:925）；同文件托盘不监听 TaskbarCreated 广播，Explorer 崩溃重启后托盘图标永久消失。其二：qml/Main.qml:3563-3568 执行队列面板 Timer 每秒 rows = scheduler.queueView(100) 整体重新赋值——ListView 全量 reset，running 行脉冲动画频闪、悬停丢失、滚动位置回跳，注释声称的「避免整表重建」恰是其效果反面。其三：src/board/BoardSync.cpp:45-54 claimForTask 回退标题匹配无词边界——任务标题 #R1-12 会被 code R1-1 的领用命中（taskIdForCode :289-291 用了 \b 正则，两套口径不一致），领用标牌可能挂错卡。
scope: src/host/HostMain.cpp;src/host/HostMain.h;qml/Main.qml;src/board/BoardSync.cpp;tests/**;docs/pitfalls.md
acceptance: tests/tst_boardsync.cpp 扩展：标题 #R1-12 的任务不再被 code R1-1 的领用命中（词边界用例，正反两向）;Main.qml 队列面板刷新改为「内容无变化不重建」（以快照比较或等价机制），委托重建只在真变化时发生——以夹具或代码级断言证明每秒赋值不再发生;HostMain 热键注册失败输出可读告警（含冲突含义），托盘窗口处理 TaskbarCreated 重建图标——以日志断言或代码级断言证明两路接线存在;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）;领用标牌既有匹配不回归（tst_boardsync 全绿）
depends:
---

## 目标

按 Z9（静默失败必须可见）与 Z14（同一匹配两套口径必须归一）修宿主与界面的三处尾巴：热键/托盘失败可见化 + TaskbarCreated 重建；队列面板去掉每秒假重建；领用标题匹配与 taskIdForCode 统一为带词边界的一套实现。

## 验收

见 frontmatter acceptance（4 条）。口径：词边界用例正反两向；队列面板以「无变化不赋值」的机制断言；宿主两项以日志/接线断言；全量 verify 以退出码为准。

## 边界

- Main.qml 行数上限 4260（模块守卫口径），本卡改动净增量趋零。
- 托盘重建只处理 TaskbarCreated 广播这一条路径，不引入托盘管理新抽象。
- 热键冲突的处置是「可见地失败」，不自动换键。

## 输入

基线 origin/main（d93cf6a）。审计现场：上述 file:line；tst_boardsync 既有夹具（DX-135 引入的 anchorRootOverride 可复用）。依赖：无。

## 产出

① 热键/托盘失败可见化 + TaskbarCreated；② 队列面板无变化不重建；③ 领用匹配词边界统一；④ 对应用例。
