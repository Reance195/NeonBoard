---
state: approved
card: DX-171
stage: DX
tier: P4
title: 更新中心右上角关闭键无响应：补接 DialogShell.closeRequested
author: maintainer-session-2026-11-16
evidence: 维护者 2026-11-16 会话以截图指出「软件更新」对话框右上角 ✕ 点击没效果，并在选项中明确选择「授权立卡并修好」。只读核查结论：qml/components/DialogShell.qml:156-162 的关闭键 MouseArea 只 emit shell.closeRequested()，统一壳自身并不执行 close()，需由每个弹窗显式接这个信号；qml/components/ 下 14 个 DialogShell 派生组件中 13 个都声明了 onCloseRequested（TaskComposer.qml:21、SettingsPanel.qml:33、CardDetailPanel.qml:28 等），唯独 DX-170 新增的 qml/components/UpdateCenterDialog.qml 只写了 onSecondaryTriggered: root.close()，漏接 onCloseRequested。截图里 ✕ 已呈红色 hover 边框，证明点击确实到达 MouseArea，是信号无接收者而非遮挡或 hover 失效；底部「关闭」按钮、Esc 与点遮罩（closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside）三条路径不受影响，只有右上角 ✕ 无效。
scope: qml/components/UpdateCenterDialog.qml
acceptance: qml/components/UpdateCenterDialog.qml 声明 onCloseRequested: root.close()，且 onSecondaryTriggered / onPrimaryTriggered / refresh() 与版本历史渲染逻辑原样不动，实现提交对该文件的 diff 只新增这一行;pwsh -File scripts/verify.ps1 全量档退出码 0，覆盖编译、ctest、--selftest、--smoke、模块守卫、QML 致命告警与看板一致性;--smoke 启动日志无 ReferenceError、无「is not a type」且 QML 致命告警 0 行，python scripts/check_qml_pitfalls.py 与 python scripts/check_qml_warnings.py 退出码 0;python scripts/check_scope.py --base origin/main 与 python scripts/check_path_refs.py --base origin/main 退出码 0，改动不越出 scope 声明的唯一文件;只读核查 qml/components/ 下全部 DialogShell 派生组件均声明 onCloseRequested（同族写法零遗漏），DialogShell.qml 公共壳本身零改动
depends: 
createdAt: 2026-11-16T00:00:00+00:00
---

## 目标

把更新中心弹窗右上角 ✕ 接回统一壳既有的关闭契约：补一行 `onCloseRequested: root.close()`，
让四条关闭路径（右上角 ✕、底部「关闭」、Esc、点遮罩）行为一致。DX-170 建这个弹窗时
接了底栏次操作，却漏了壳头部关闭键的信号；本卡只补这一处接线，不新造关闭机制。

## 验收

见 frontmatter `acceptance`（5 条）。判据落在「信号已接且 diff 只有一行」「全量 verify 绿灯」
「启动无 QML 致命告警」「scope 唯一文件」「同族写法零遗漏」上，不落在「肉眼看着关掉了」
这种主观判断上。scope 含 .qml，按宪章 §7.7 走 P4 全量档，编译与冒烟由本地 `verify.ps1`
验证，CI 对 main 常规兜底。

## 边界

- 只改 `qml/components/UpdateCenterDialog.qml` 一行。不动 `qml/components/DialogShell.qml`
  公共壳：13 个存量弹窗已各自接好信号，若在壳里加默认关闭 handler，14 个弹窗的关闭语义
  会一起变，且已有 handler 的组件可能被重复处理，回归面远大于本缺陷。
- 不改任何既有 handler 的业务逻辑（`onPrimaryTriggered` 的检查／下载分支、
  `onSecondaryTriggered` 的 close、`refresh()`），不改版本历史四态渲染与外链白名单。
- 不新增静态检查脚本／钩子：这是同族写法第一次漏接，是否沉淀成常驻闸门另卡再议（Z6）。
- 不动 C++、CMakeLists.txt、测试、台账治理脚本、CI workflow；不改 README —— 界面行为描述
  里没有专门写右上角 ✕，功能项与快捷键均无变化。
- 不借本卡重排 QML 格式、不统一别人的写法（Z2）。

## 输入

基线 origin/main（a90f5dc），远端 HTTPS 可达，本地 main 与 origin/main 一致。
现场：`qml/components/DialogShell.qml:58` 声明 `signal closeRequested()`、`:161` 的
MouseArea `onClicked: shell.closeRequested()`；`qml/components/UpdateCenterDialog.qml:38-44`
是现有两个 handler 与 `refresh()`，`:46-48` 的注释说明开窗链路；对照实现见
`qml/components/TaskComposer.qml:20-21`、`qml/components/SettingsPanel.qml:32-33`、
`qml/components/CardDetailPanel.qml:26-33`（后者关闭时还要丢弃草稿）。
`qml/Main.qml:1297` 以 `UpdateCenterDialog { id: updateCenterDialog; hostWindow: window }`
常驻实例化，`:254` 暴露 `property alias updateCenter`，开窗走弹窗自身的 `open()`。
依赖：无。

## 产出

① `qml/components/UpdateCenterDialog.qml` 新增 `onCloseRequested: root.close()`，紧邻既有
   `onSecondaryTriggered`，与同族组件同一形态；
② 实现提交带 `#<短号>=done`，合并提交 body 写全四段（变更／验收／影响／卡片），
   交付说明写回卡片 `note`，收尾由 `git-done.ps1` 自动结卡。
