---
state: closed
card: MOD-16
stage: MOD
tier: P4
title: MOD QML 批次六批：霓虹背景包裹层整迁 components/NeonBackgroundLayer
author: codebuddy-session-2026-11-09
evidence: 2026-11-09 实测（基线 origin/main，Main.qml 2416 行）：NeonBackground 包裹层 Item 块 933-949 共 17 行（NeonBackground + 28% 遮罩 Rectangle）。双向引用实测——前向仅 window.active / window.focusMode 两处（经 hostWin）；taskStore 与 noBackground 均为 C++ 注入 context property（AppMain.cpp:453，组件内直接可用）；反向引用仅 background.burst() 两处（L133 / L887 窗口级函数），经 alias background 暴露后改走实例化 id，其余 15 处 background 字样均为 background: 属性赋值误报（正则逐行核实）。块内零字符串/注释含 window.。MOD-11~15 已验证整块迁移模式与全部坑（QT_RESOURCE_ALIAS、Z6 切片、CMake 上下文锚点、组件文件必须从断言过的数据组装、合并块后全量反向核对——本卡单块迁移且反向引用已全量核对 18 项）。维护者 2026-11-09 会话指令「继续迁移 NeonBackground 包裹层」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/Main.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/NeonBackgroundLayer.qml（块体逐字照搬，仅 window.→hostWin. 两处接口化，background 经 alias 暴露）且 Main.qml 对应区段替换为带绑定的一处实例化;Main.qml 行数 <= 2404（基线 2416）且 LINE_LIMITS 中 qml/Main.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：任务流转时霓虹背景 burst 脉冲与专注模式背景渐变与迁移前一致
depends:
createdAt: 2026-11-09T18:00:00+00:00
---

## 目标

把 `qml/Main.qml` 的 NeonBackground 包裹层 Item（933-949，17 行）整块迁入新建的 `qml/components/NeonBackgroundLayer.qml`。接口仅 2 个：`hostWin`（原 window）与 `alias background`（Main 级 2 处 `burst()` 调用）。

## 验收

见 frontmatter `acceptance`（6 条）。核心是第 1、2 条：全量 verify 绿（--smoke QML 致命告警 0）、块体逐字照搬仅两处接口化。第 6 条诚实边界：burst 脉冲与专注渐变请维护者人工复核。

## 边界

见 frontmatter `scope`，四条路径（CMakeLists.txt 立卡即入 scope）：

- `qml/components/**`：新增 NeonBackgroundLayer.qml；
- `qml/Main.qml`：包裹层区段替换为带绑定的一处实例化，2 处 burst 调用改走实例化 id；
- `CMakeLists.txt`：qt_add_qml_module 源条目 + QT_RESOURCE_ALIAS 裸名；
- `scripts/check_module_deps.py`：LINE_LIMITS 中 qml/Main.qml 同步调低。

**不动**：FocusBadge / 遮罩 MouseArea / Loaders 区（后续批次评估）、已迁五块、任何 C++。不碰台账契约。

## 输入

基线 `origin/main`。MOD-11 ~ MOD-15 已结卡（迁移模式与全部坑已验证；本卡反向引用已对全部 id 全量核对）。

## 产出

`qml/components/NeonBackgroundLayer.qml` + Main.qml 接管 + CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明**单独一条 `docs(board):` 提交**，`note` 写「已完成 X / 待做 Y」与 nextStep（FocusBadge / 遮罩 MouseArea / Loaders 区 / 手工 alias 收敛）。
