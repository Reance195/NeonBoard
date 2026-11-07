---
state: proposed
stage: DX
tier: P3
title: 插件网络与生命周期四处：异步下载无超时、并发下载状态污染、活跃 context 泄漏、manifest 入口路径穿越
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计。其一：src/plugin/PluginMarket.cpp:792-818 异步 downloadPackage（QML 实际调用频率最高）完全无超时——索引路径有 kIndexTimeoutMs、blocking 下载有 kDownloadTimeoutMs，唯独这条裸奔，恶意服务器只建连不发数据即永久挂起。其二：:784-817 状态按「单飞行」假设写死——m_pendingUrl/m_pendingSha/m_pendingDest 是成员，两次安装重叠时后者覆盖前者，前一包拿后一包的 sha 校验、payload 写进后者的临时文件。其三：PluginRuntime.cpp:452-461 refreshContexts 对「被移除的活跃 context」跳过退订与 deleteLater——PluginEventBus::publish 持裸指针派发，热重载后幽灵 context 继续收事件且 m_activeContext 置空未发 activeContextChanged。其四：PluginLoader.cpp:302-328 对 manifest 声明的 main/background/settings/slots 不做路径校验，../../../ 可指向插件目录外任意 QML 被 file:// 加载——用 manifest 引用绕过 files 落盘围栏（isSafeRelativePath 知识在 PluginPackage.cpp 未复用，违反 Z5）。
scope: src/plugin/PluginMarket.cpp;src/plugin/PluginMarket.h;src/plugin/PluginRuntime.cpp;src/plugin/PluginLoader.cpp;tests/**;docs/pitfalls.md
acceptance: tests/tst_pluginruntime.cpp 扩展：卸载/热重载活跃 context 后事件不再向其投递、activeContextChanged 照发（以 spy 断言）;新增（或按既有夹具）用例：manifest 的 main/background 字段含 ../ 或盘符写法时加载校验拒绝（以 PluginLoader 可直接构造的校验入口断言，若无独立校验函数则抽出后测）;下载超时以常量/看门狗接线证明（对不存在主机触发一次异步下载，断言有限时间内以失败收尾而非永久挂起，超时上限随 kDownloadTimeoutMs 同源）;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）;市场多源合并与缓存协议不回归（tst_pluginruntime、tst_pluginpackage 全绿）
depends:
---

## 目标

按 Z14 收口插件域的「异步路径缺防」一族：异步下载补看门狗（与 blocking 同源常量）、pending 状态随 reply 闭包携带（消灭共享成员竞态）、活跃 context 移除时正常退订与释放、manifest 入口字段复用 SafePaths 做相对路径校验。

## 验收

见 frontmatter acceptance（5 条）。口径：context 生命周期以 spy 断言；入口穿越以校验入口断言；超时以有限时间收尾证明；全量 verify 以退出码为准。

## 边界

- 不改市场索引格式 / .neon 包格式 / 缓存文件结构（§9 协议冻结）。
- 并发下载的处置口径：两次并发同 id 安装仍按现有「后到覆盖」的用户语义，只保证校验与落盘各自正确，不引入下载队列。
- loader 的入口校验只拦「出插件目录」的引用，不收紧子目录深度。

## 输入

基线 origin/main（d93cf6a）。审计现场：上述 file:line；tst_pluginruntime 既有管线夹具（R16-01）。依赖：无（SafePaths 已在 main）。

## 产出

① 异步下载看门狗 + 闭包化状态；② 活跃 context 退订/释放/信号；③ loader 入口路径校验（SafePaths 复用）；④ tst_pluginruntime 扩展用例；⑤ pitfalls 沉淀「共享可变成员扛并发」族判据。
