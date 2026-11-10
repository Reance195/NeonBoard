---
state: requested
stage: R0
tier: P1
title: 启动性能收尾二批：首次运行冷态收敛与 QML 树构建 <800ms
author: codebuddy-session-2026-11-11
evidence: roadmap §10 仅剩两个 ⬜ 指标：首次运行 5–9 s（目标 <3 s，冷页缓存）、QML 树构建 781–1851 ms（min 781，目标 <800 ms）。批次一 R0-13 已收口建表延后 / 扫描后台化 / 字体回退不阻塞 / .qsb 预热（部分），R0-14 已用 DELAYLOAD 精简随包 DLL；R0-12 打包瘦样式路线已弃单。剩余冷态差距来自进程冷页（exe + Qt DLL + qrc 资源首次读入），运行时侧未做预热；QML 树构建中位数仍超目标一倍，Main.qml 现存 1350 行、23 处按需加载点，首帧前同步实例化的面未再审计过。
scope: qml/Main.qml;src/main.cpp;src/app/AppMain.cpp;src/app/AppWiring.cpp;src/shell/;CMakeLists.txt;scripts/check_module_deps.py;tests/;docs/iteration-roadmap.md;docs/pitfalls.md
acceptance: 冷态实测沿用 R0-13 的冷/热双口径与轮次方法（同机、改动前后各多轮取中位数）：首次运行（BOOT shown 口径）中位数相对改动前收敛 ≥50%，绝对值若仍未达 <3 s，剩余差距量化写进 roadmap §10（Z9 诚实边界）;QML 树构建（engine→loaded）中位数 <800 ms；若实现后仍不达标，实测数据与瓶颈分析如实写进 roadmap §3.1，不虚报达成;启动路径新增 / 延后动作均有既有 BOOT 探针或新增计时行可证（Z8）；预热必须与既有启动路径并行，不得同步等待拉长 BOOT→首帧总时长（§R0 既有约束）;竞态面不回归：插件后台装载注入时序、提醒启动补发、首次引导判定（shouldOnboard，R13）行为不变；涉及时序改动在 tests/ 补用例覆盖「数据源未就绪」空态 / 就绪信号路径;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0，致命 QML 告警 0 行）;docs/iteration-roadmap.md §3.1 / §10 回填前后实测数据；过程踩坑补 docs/pitfalls.md 条目
depends: R0-13;R0-14
createdAt: 2026-11-11T00:00:00+00:00
---

## 目标

收掉 §10 仅剩的两个 ⬜ 启动指标：

1. **首次运行（冷态）收敛**：R0-13 的 `.qsb` 预热只盖了 shader 冷 I/O，进程冷页（主 exe + Qt DLL + qrc 首次读入）仍占首次运行 5–9 s 与热态 1.2–1.4 s 之间约 4–8 s 差距的主体。在既有 `src/shell/StartupDefer` 预热先例上扩展「与首帧并行的冷页预热读」（Splash BOOT 阶段后台顺序读本进程关键映像与资源），或按实测瓶颈另选运行时侧手段——R0-12 的打包瘦样式路线已弃单，本卡不再走 deploy 侧。
2. **QML 树构建 <800 ms**：R0-10 按需加载后 min 已到 781 ms，说明目标可达；中位数 1448–1851 ms 的方差说明首帧前仍有可去重的同步实例化面。以 BOOT 探针数据为准审计 `Main.qml`（现 1350 行、23 处 Loader/active:false）首帧前同步实例化清单，把可见面之外的首帧同步成本延后或并行化。

## 验收

见 frontmatter `acceptance`（7 条）。口径说明：

- 冷态测量方法沿用 R0-13 交付的口径（同机前后对比，不写绝对承诺值）；两条主指标的达成状态以实测数据回填 roadmap 为准，未达标的部分按 Z9 如实报差距与原因，不以「批次做完」笼统带过。
- 时序类改动必须覆盖竞态面（验收第 5 条）：任何延后执行的动作都要回答「UI 在它完成前会看到什么」。

## 边界

见 frontmatter `scope`。逐项说明：

- `qml/Main.qml`：只动首帧前同步实例化面与既有 Loader 分区，不做无关重排（§7.4 分区纪律）；
- `src/app/AppMain.cpp` / `src/app/AppWiring.cpp` / `src/main.cpp`：仅启动时序与探针接线；不改数据 schema、不升 `user_version`、不改插件 API 契约；
- `src/shell/`：StartupDefer 预热扩展落此；新增源文件须 CMake 双登记（`qt_add_executable` 尾部追加）并登记 `check_module_deps.py` LINE_LIMITS；
- 不改 `scripts/deploy.ps1`（R0-12 路线已弃单）；不动数据库迁移逻辑本身；
- 预热永不删除 / 跳过任何既有启动语义，只改执行时机与并行性。

## 输入

基线 `origin/main`（ff19357，v0.1.8 发版后）。既有资产：BOOT 探针（`BOOT db` / `BOOT firstframe` / `BOOT qsb prewarm` / `BOOT shown`）、`src/shell/StartupDefer.h`、R0-13 前后实测方法、pitfalls §7.6 / §7.7。依赖：R0-13、R0-14 均已结卡。

## 产出

① 冷页预热（或等价运行时侧手段）落地并有 BOOT 计时可证；② Main.qml 首帧同步实例化审计结论 + 收敛改动；③ tests/ 竞态用例（涉及时序改动时）；④ roadmap §3.1 / §10 前后实测数据回填；⑤ pitfalls 条目（如有踩坑）；⑥ `verify.ps1` 全量绿。立卡与实现分开两条分支、两段提交（宪章条款 6）。
