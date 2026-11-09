---
state: proposed
card: MOD-15
stage: MOD
tier: P4
title: MOD QML 批次五批：标题栏与主看板区整迁 components/BoardChrome
author: codebuddy-session-2026-11-09
evidence: 2026-11-09 实测（基线 origin/main，Main.qml 2964 行）：titleBar（964-1086，123 行）与 RowLayout 主看板区（1088-1519，432 行）合并迁移共 556 行。组合动机——titleBar 的 focusButton 与看板区 focusMenu 互引、RowLayout 锚定 titleBar.bottom、看板区聚焦菜单定位引用 titleBar/focusButton，合并后互引全部内化，接口从 8 个收敛到 5 个。块内零字符串/注释含 window.（机械替换安全）；34 处 window.（25 个成员，全部 window 级 property/function：dragOverColumnId / focusColumn / beginTaskDrag / heatmapCollapsed 等）经 hostWin 接口化；columnManager/dragSpace(root) 两个前向引用经 ref property；反向引用仅 4 行（searchField @525/580、columnRepeater @169-170，经 alias 暴露）；boardColumnModel 为委托 required property 自包含。更正：此前盘点所称「主内容层 Item 933-1851（919 行）」实为多个兄弟块的连续区段，本卡取其中最大耦合单元（titleBar+RowLayout），NeonBackground 包裹层 / FocusBadge / 遮罩 MouseArea 留 Main。MOD-11~14 已验证整块迁移模式与全部坑（QT_RESOURCE_ALIAS、body 切片 Z6 修正、property var 声明、CMake find 锚点撞行——本卡 CMake 插入改用带上下文的唯一锚点）。维护者 2026-11-09 会话指令「继续迁移主内容层 Item」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/Main.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/BoardChrome.qml（titleBar 与 RowLayout 逐字照搬合并，仅 window.→hostWin.、columnManager→columnManagerRef、dragSpace: root→dragSpace: dragSpaceRef 接口化，searchField/columnRepeater 经 alias 暴露）且 Main.qml 对应区段替换为带绑定的一处实例化;Main.qml 行数 <= 2417（基线 2964）且 LINE_LIMITS 中 qml/Main.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：标题栏拖拽/窗口按钮/专注按钮/项目切换/看板列拖拽跨列/搜索过滤行为与迁移前一致
depends:
createdAt: 2026-11-09T17:20:00+00:00
---

## 目标

把 `qml/Main.qml` 的 titleBar（964-1086，123 行）与 RowLayout 主看板区（1088-1519，432 行）合并迁入新建的 `qml/components/BoardChrome.qml`（Item 根包裹，556 行）。互引内化后接口仅 5 个：`hostWin`（原 window）、`columnManagerRef`、`dragSpaceRef`（原 root）、`searchField` 与 `columnRepeater` 两个 alias。

更正说明：此前盘点把 root 下 933-1851 连续区段统称「主内容层 Item（919 行）」，实测是多个兄弟块；本卡取其中最大耦合单元（titleBar + RowLayout），NeonBackground 包裹层 / FocusBadge / 遮罩 MouseArea 留 Main（后续批次评估）。

## 验收

见 frontmatter `acceptance`（6 条）。核心是第 1、2 条：全量 verify 绿（--smoke QML 致命告警 0）、两块逐字照搬仅四处接口化。第 6 条诚实边界：拖拽跨列 / 专注模式 / 搜索过滤等交互态请维护者人工复核。

## 边界

见 frontmatter `scope`，四条路径（CMakeLists.txt 立卡即入 scope）：

- `qml/components/**`：新增 BoardChrome.qml；
- `qml/Main.qml`：titleBar + RowLayout 区段替换为带绑定的一处实例化，5 处外部引用改走实例化 id；
- `CMakeLists.txt`：qt_add_qml_module 源条目 + QT_RESOURCE_ALIAS 裸名；
- `scripts/check_module_deps.py`：LINE_LIMITS 中 qml/Main.qml 同步调低。

**不动**：NeonBackground 包裹层 / FocusBadge / 遮罩 MouseArea（后续批次评估）、已迁四张弹窗、Loaders 区、任何 C++。不碰台账契约。

## 输入

基线 `origin/main`。MOD-11 ~ MOD-14 已结卡（迁移模式与全部坑已验证，含 CMake 锚点撞行教训——本卡用带上下文唯一锚点插入）。

## 产出

`qml/components/BoardChrome.qml` + Main.qml 接管 + CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明**单独一条 `docs(board):` 提交**，`note` 写「已完成 X / 待做 Y」与 nextStep（NeonBackground 层 / Loaders 区 / 手工 alias 收敛）。
