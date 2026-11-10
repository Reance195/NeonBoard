---
state: proposed
card: DX-163
stage: DX
tier: P0
title: 补 CMake 双登记 AppearancePage.qml 并加 qml 登记齐套机器判据
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 会话全仓清理扫描实证：MOD-24 交付提交 3909366（已在 origin/main）只含 AppearancePage.qml 新建、SettingsPanel 瘦身与 check_module_deps.py 共三个文件，未触及 CMakeLists.txt —— MOD-24 请求单验收第 4 条「CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名（MOD-11 教训前置）」未兑现。后果：SettingsPanel.qml:163 实例化 AppearancePage，而构建产物 qrc 中无此文件，运行时 QML 类型解析失败，设置面板「外观与动效」分页打不开；构建不报错，--smoke 4 秒窗口不点开设置分页，verify 与 CI 全部漏过。判据缺口：check_module_deps.py 只做 diff 棘轮与行数上限（LINE_LIMITS 甚至已收录 qml/components/AppearancePage.qml: 97 仍漏），全仓没有「qml/ 磁盘文件必须登记进 qml 模块」的自动校验。维护者 2026-11-10 会话指令「立卡修 P0：补 CMake 双登记」，据此授权立卡、领用、实施、verify、rebase、推 origin/main。
scope: CMakeLists.txt;scripts/check_module_deps.py
acceptance: CMakeLists.txt 双登记补齐：qt_add_qml_module 源条目 + set_source_files_properties QT_RESOURCE_ALIAS 裸名（兑现 MOD-24 请求单验收第 4 条）;check_module_deps.py 新增全树判据：qml/ 下磁盘 .qml/.js 必须同时出现在 qt_add_qml_module 源列表与 set_source_files_properties QT_RESOURCE_ALIAS 登记中，缺任一登记退出非 0 并指名文件与缺失面;python scripts/check_module_deps.py --selftest 通过且覆盖新判据（缺登记样本必须报错、双登记齐套样本必须通过）;pwsh -File scripts/verify.ps1 全量通过且 --smoke QML 致命告警 0 行;Z8 产物验收：构建目录 qml 模块资源（.qrc 或生成 cpp）中出现 qml/components/AppearancePage.qml 条目;维护者人工复核：沙箱实例设置面板「外观与动效」分页可打开且渲染正常
depends:
createdAt: 2026-11-10T18:30:00+00:00
---

## 目标

补齐 MOD-24 遗留的 CMake 双登记（`qml/components/AppearancePage.qml` 进 `qt_add_qml_module` 源列表 + `QT_RESOURCE_ALIAS` 裸名），修复设置面板「外观与动效」分页运行时类型解析失败；并按 Z6 把这次的坑沉淀成机器判据：`check_module_deps.py` 新增「qml/ 磁盘文件登记齐套」全树校验，缺登记在守卫处当场拦下，不再依赖人眼对照请求单。

## 验收

见 frontmatter `acceptance`（6 条）。第 6 条诚实边界：分页渲染效果请维护者人工复核。

## 边界

见 frontmatter `scope`：

- `CMakeLists.txt`：只追加 AppearancePage.qml 的两条登记，不动其它条目；
- `scripts/check_module_deps.py`：新增登记齐套判据与 selftest 用例，不动既有 diff 棘轮与行数上限语义。

**不动**：任何 QML 文件、任何 C++、LINE_LIMITS 既有上限值。

## 输入

基线 `origin/main`（a4b68c8）。MOD-24 已合并。缺陷现场：SettingsPanel.qml:163 实例化 AppearancePage；CMakeLists.txt 缺该文件登记。

## 产出

CMakeLists.txt 双登记两条 + check_module_deps.py 新判据（含 selftest）；外观分页恢复可用；同类缺登记由守卫拦截。
