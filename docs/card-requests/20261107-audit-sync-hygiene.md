---
state: closed
card: DX-142
stage: DX
tier: P3
title: 同步三处：更新竞速状态机重入与 reply 泄漏、ThemeMarket 先删后 rename、RoadmapSync poll 吞变更
author: maintainer-session-2026-11-07
evidence: 2026-11-07 全仓审计。其一：src/update/UpdateManager.cpp:622-628 多源竞速成功分支在遍历 m_activeManifestReplies 中 abort 其他 reply——直连信号下 onManifestReplyFinished 同步重入、removeOne 使迭代器失效；异步路径下被 clear() 的 reply 永不 deleteLater，常驻进程逐次累积。判胜标准（:!payload.isEmpty()）不含解析有效性，captive portal 的 200 HTML 永久压过真源且不回退缓存；kCacheTtlSeconds（:57）定义后从未引用。其二：src/theme/ThemeMarket.cpp:280-286 更新主题先 remove(target) 再 rename，Windows rename 不覆盖——失败即旧包已毁新包未落。其三：src/board/RoadmapSync.cpp:645-653 poll 先记 mtime 再导入，导入失败（外部写一半/坏 JSON）不回退 mtime——该次文件变更被吞直到下次外部改动（importAll 启动导入不走此路，重启能救回反而掩盖问题）。
scope: src/update/UpdateManager.cpp;src/theme/ThemeMarket.cpp;src/theme/ThemeMarket.h;src/board/RoadmapSync.cpp;tests/**;docs/pitfalls.md
acceptance: tests/tst_thememarket.cpp 扩展：对已安装主题重装且目标被占用（rename 失败可注入的等价场景）时旧包完好回滚，不再「先删后建」;tests/tst_roadmapsync.cpp 扩展：坏 JSON 文件触发导入失败后，mtime 不前移，文件修好后同 mtime 序列可再次导入（不吞变更）;tests/tst_updatemanager.cpp 扩展：竞速胜者 payload 非法（非 JSON/缺版本）时回退其余源或缓存，不再整体报错;kCacheTtlSeconds 接线生效（过期缓存不再无限期使用）或删除定义并在 pitfalls 记录决策;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）
depends:
---

## 目标

按 Z13/Z14 修「同步写坏一半」一族：竞速 abort 改为先复制列表再逐个 abort（消灭遍历重入）、所有 reply 全路径 deleteLater、判胜加解析有效性、TTL 接线或显式删除；ThemeMarket 改「先备份后替换、失败回滚」；RoadmapSync poll 改「导入成功才记 mtime」。

## 验收

见 frontmatter acceptance（5 条）。口径：ThemeMarket 与 RoadmapSync 用例以可注入场景断言；竞速判胜以 payload 非法的可观测回退证明；全量 verify 以退出码为准。

## 边界

- 不改清单格式 / 缓存文件结构 / 增量协议（DX-136 已定）。
- abort 重入的修复不改竞速语义（首胜者胜），只改遍历与释放的时序。
- RoadmapSync 的 mtime 语义改动只影响「失败重试」，成功路径时序不变。

## 输入

基线 origin/main（d93cf6a）。审计现场：上述 file:line；tst_thememarket / tst_roadmapsync / tst_updatemanager 既有骨架。依赖：无。

## 产出

① 竞速时序修复 + TTL 决策落地；② ThemeMarket 原子替换；③ RoadmapSync 失败不吞变更；④ 三处测试扩展；⑤ pitfalls 沉淀「先毁旧后建新」族。
