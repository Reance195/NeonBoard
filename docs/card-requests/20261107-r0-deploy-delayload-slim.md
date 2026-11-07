---
state: proposed
stage: R0
tier: P2
title: 部署侧依赖收敛：CMake DELAYLOAD 精简随包 DLL，并纠偏 pitfalls §7.6 的过时前提
author: scheduled-direction-cron-2026-11-07
evidence: pitfalls §7.6（2026-10-30 诊断定案）把冷启动瓶颈记为「portable 包带 7 套 QML 样式 + 35 个 Qt DLL 的冷 I/O」，治本路径（deploy.ps1 只随包 Basic 样式 + CMake 改 DELAYLOAD）因不在 R0-11 scope 内被挂起待拍板。立卡时点（2026-11-07）核实两半现状：①样式半已由 R0-11 落实——8e52812 在 scripts/deploy.ps1 显式排除 Fusion/Imagine/Material/Universal 等 non-Basic 样式 DLL 并整目录删除未用样式 QML（L73–99），§7.6 该半句已过时；②DLL 半仍未动——CMakeLists.txt 全文无 DELAYLOAD 配置（grep 无命中），windeployqt 按 exe 静态导入复制运行时，链接多少 Qt 模块就随包多少 DLL。README「已知限制」首条仍为「首次运行约 5–9 秒」；R0-13（2026-11-07 合入）已交付代码侧批次（建表延后 / 扫描后台化 / 字体回退 / .qsb 预热），其立卡单引用 §10 指标「首次运行目标 <3 s」，部署侧 DLL 冷 I/O 是该指标剩余的主要可优化面。
scope: CMakeLists.txt;scripts/deploy.ps1;docs/iteration-roadmap.md;docs/pitfalls.md;README.md
acceptance: CMake 对可延后加载的 Qt 模块改 /DELAYLOAD 链接（MSVC delayimp），dumpbin /dependents 对改动前后 NeonBoard.exe 的静态导入清单逐模块对照并写入 docs/iteration-roadmap.md §3.1（移出启动期的模块逐个点名）;便携包产物核验（拿产物验收）：deploy.ps1 出包后清点 dist 内 Qt 运行时 DLL 文件数与包体积，前后对照写入 §3.1 且随包 DLL 数下降，7b18c18 引入的便携包清单自检同步更新且 deploy.ps1 退出码 0;功能不回归：NeonBoard.exe --selftest 退出码 0 且 --smoke 退出码 0、日志无致命 QML 告警（沿用既有 check_qml_warnings 判据）;安装版冒烟：Inno 安装包装入干净目录后运行安装目录 NeonBoard.exe --selftest 退出码 0（DELAYLOAD 不引入运行期缺 DLL）;冷启动同机前后实测（沿用 R0-13 口径：Release --smoke 每档 5 轮 hot / 3 轮 cold 取中位）写入 §3.1，冷态 BOOT→首帧 差值不劣化，收益不显著也要有数据结论;docs/pitfalls.md §7.6 纠偏：样式半标注已由 R0-11（8e52812）落实，条目随本卡交付迁出「尚无解决方案（需人拍板）」节并标提交号，README「已知限制」首条按新实测数据更新;pwsh -File scripts/verify.ps1 全量通过（编译 + ctest + --selftest + --smoke 退出码 0）
depends: R0-13
suggestedId: 
createdAt: 2026-11-07T04:55:00+00:00
---

## 目标

把冷启动的部署侧剩余可优化面收掉：CMake 对可延后加载的 Qt 模块改 DELAYLOAD 链接，
让 windeployqt 不再为「只有静态导入才复制」之外的模块陪跑，随包 DLL 数与冷 I/O 同步下降；
同时把 pitfalls §7.6 的过时前提纠偏（样式半已由 R0-11 落实），让该条目带着数据结论迁出
「尚无解决方案（需人拍板）」节。与 R0-13（代码侧批次）互补，共同服务 §10「首次运行 <3 s」指标。

## 验收

见 frontmatter `acceptance`（7 条）。核心口径：机制（DELAYLOAD）只是手段，验收锚在产物与数据上——
随包 DLL 清点下降（第 2 条）、安装 / 便携双形态不缺模块（第 3 / 4 条）、同机前后实测落 §3.1（第 5 条）。
收益不显著也必须留数据结论，不许「感觉变快了」（Z8 / Z9）。

## 边界

CMakeLists.txt（链接配置）、scripts/deploy.ps1（随包内容变化后清单自检同步）、
docs/iteration-roadmap.md（§3.1 数据与结论）、docs/pitfalls.md（§7.6 纠偏）、README.md（已知限制首条）。
**不改 src/ 任何代码**——本卡是纯部署 / 构建行为改动；若 DELAYLOAD 暴露出代码侧初始化次序问题，
停下来说明并另立卡，不在本卡顺手修（Z3）。

## 输入

基线 origin/main（7807a7f）。§7.6 诊断与处方见 docs/pitfalls.md（2026-10-30 定案）；
R0-11 的 Basic-only 样式先例在 scripts/deploy.ps1 L73–99（排除表 + 样式 QML 目录清理）；
R0-13 的实测口径（同机 Release --smoke 每档 5 轮 hot / 3 轮 cold 取中位）见 roadmap §3.1。

## 产出

合并提交 body 四段（变更 / 验收 / 影响 / 卡片）；§3.1 新增「部署侧依赖收敛」小节（前后对照表）；
pitfalls §7.6 条目迁出待拍板节并标提交号。
