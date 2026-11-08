---
title: 弹窗与抽屉壳头部关闭键未贴右上角
stage: R11
scope: qml/components/DialogShell.qml;qml/components/DrawerShell.qml
acceptance: 两壳头部 ColumnLayout 内标题与副标题 Text 声明 Layout.fillWidth: true;pwsh -File scripts/verify.ps1 全量通过;沙箱启动 --smoke 无新增 QML 布局告警
evidence: qml.exe 最小复现实测（Qt 6.8.3）：headerRow 宽 680 下关闭键 x=508（正确 654），ColumnLayout 内 Text 补 Layout.fillWidth 后 x=654
author: 维护者
tier: P2
---

## 目标

设置 / 插件库等居中弹窗（DialogShell）与外观 / 插件 / 全景 / 列管理抽屉（DrawerShell）头部右上角关闭键停在标题右侧、随标题宽度浮动，而不是贴壳右上角。修复后关闭键恒定位于头部最右。

## 验收

- qml/components/DialogShell.qml 与 DrawerShell.qml 头部 ColumnLayout 内标题、副标题 Text 均声明 Layout.fillWidth: true（grep 可判定）
- pwsh -File scripts/verify.ps1 全量通过（scope 含 .qml，按代码类档位）
- 沙箱启动 --smoke 无新增 QML 布局 / 锚定告警

## 边界

- 只改两个壳的头部标题布局（4 个 Text 各补一行）；不动 footer、body、其余组件
- 不引入新组件、不改 Theme 令牌、不改关闭键规格（26×26 / hover 危险色）

## 输入

- 根因：Qt Quick Layouts 嵌套布局中，ColumnLayout(Layout.fillWidth: true) 内子项未声明水平扩展意图时，外层 fillWidth 不分配剩余空间，ColumnLayout 收缩到 implicit 宽
- 探针实测（qml.exe，对照四组）：坏 508 / 好 654（row 宽 680、右距 26）；Loader、verticalCenter、anchors.fill 均排除
- 用户截图：设置与插件库弹窗头部 ✕ 位置随副标题宽度浮动，证实同一根因

## 产出

- qml/components/DialogShell.qml、qml/components/DrawerShell.qml 头部 4 个 Text 各补 Layout.fillWidth: true
