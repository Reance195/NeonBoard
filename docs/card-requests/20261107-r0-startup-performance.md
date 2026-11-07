---
state: proposed
stage: R0
tier: P0
title: 启动性能收尾批次：建表延后 / 扫描后台化 / 字体回退不阻塞 / .qsb 预热
author: maintainer-session-2026-11-07
evidence: roadmap §3.1 启动性能表四行「待做」是 §3.1 仅剩的未收项：①Database.cpp 在 UI 线程同步建表/迁移；②PluginLoader 扫描目录在主线程（实测仅 2–8 ms，优先级已下调但仍占主线程）；③Cascadia Code 缺失时的回退字体加载可能阻塞首帧（src/main.cpp QFontDatabase 路径）；④.qsb 首次链接未预热，冷态 BOOT shown 比热态多 1–2 s。§10 验收指标同批悬着两个 ⬜：首次运行 5–9 s（目标 <3 s）、QML 树构建 781–1851 ms（目标 <800 ms）。前三项各数十 ms、第四项可能秒级，是冷启动达标前剩余的主要可优化面。
scope: src/core/Database.cpp;src/core/Database.h;src/plugin/PluginLoader.cpp;src/plugin/PluginLoader.h;src/main.cpp;src/shell/;qml/Main.qml;tests/;docs/iteration-roadmap.md;docs/pitfalls.md
acceptance: 建表/迁移不再在 UI 线程同步执行（独立线程或延后首帧后完成），首帧不等待建表；--selftest 与 --smoke 退出码 0，建表完成前后的任务/看板读写不回归（迁移幂等，老库照常打开）;PluginLoader 扫描移入后台线程：首帧先渲染、扫描完成后再注入侧栏，扫描期间主线程无阻塞（现有启动计时日志可证），插件装载 / 面板 / 权限行为不回归;字体回退加载不再阻塞首帧（延后或异步），BOOT font families 计时日志保留，Cascadia 缺失场景首帧时间不劣化（模拟缺字体路径验证）;`.qsb` 首次链接挪到 Splash BOOT 阶段预热完成（启动日志可证），冷态 BOOT→首帧 与热态的差值相对改动前收敛，前后实测数据写入 roadmap §3.1;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）;docs/iteration-roadmap.md §3.1 四行「待做」按实测结果更新（含各步前后耗时数据）；过程踩坑补 docs/pitfalls.md 条目
depends: 
suggestedId: 
---

## 目标

把 §3.1 启动性能表最后四项「待做」收掉，让冷启动向 §10 指标（首次运行 < 3 s）靠拢。四项互相独立、可按收益排序实施：`.qsb` 预热（可能秒级）> 数据库延后建表（数十 ms）> 字体回退（数十 ms）> 插件扫描后台化（数十 ms，roadmap 已降级优先级，但后台化本身是正确形态）。

## 验收

见 frontmatter `acceptance`（6 条）。口径说明：

- 以**产物与日志**断言：`--selftest` / `--smoke` 退出码、现有启动计时日志（`BOOT font families` 等既有探针保留）、roadmap 回填的前后实测数据（Z8）。
- 四项是四个独立改动点，允许拆成多个提交落在同一条分支，但验收逐项核对，不以「批次做完」笼统带过。
- 时序类改动（建表延后 / 扫描后台化）必须覆盖竞态面：首帧后 UI 对「尚未就绪」的数据源的行为（空态 / 重试 / 就绪信号），tests/ 补对应用例。

## 边界

- 不动数据库 schema、不升 `user_version`；迁移逻辑本身不改，只挪执行时机。
- 不改插件 API 契约与 manifest 格式；PluginLoader 对外门面签名不变，只改内部线程模型。
- Splash 预热**必须与现有启动路径并行**，不得用同步等待延长 boot 动画总时长（AGENTS 既有约束：预热不得拉长总启动时间）。
- 不为预热引入新的编译期 / 第三方依赖；`.qsb` 链接走 Qt 现有机制。
- 冷启动实测受机器影响：数据以「同机改动前后对比」为准，不写绝对承诺值。

## 输入

基线 `origin/main`。既有实测：热启动 1.23–1.39 s ✅、首次运行 5–9 s、QML 树构建 781–1851 ms、插件扫描 2–8 ms、冷态 BOOT shown 比热态多 1–2 s（roadmap §3.1 / §10）。依赖：无。

## 产出

① 四项实现（`Database` 延后建表、`PluginLoader` 后台扫描、字体回退不阻塞、Splash 阶段 `.qsb` 预热）；② 竞态面测试用例（tests/）；③ roadmap §3.1 四行状态与前后实测数据回填；④ pitfalls 条目（如有踩坑）；⑤ `verify.ps1` 全量绿。立卡与实现分开两条分支、两段提交（宪章条款 6）。
