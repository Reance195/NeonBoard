---
state: proposed
card: MOD-11
stage: MOD
tier: P4
title: MOD QML 批次首批：执行队列弹窗整迁 components/ExecQueueDialog
author: codebuddy-session-2026-11-09
evidence: 2026-11-09 实测（基线 origin/main）：qml/Main.qml 4131 行（LINE_LIMITS 一致）。方案 v4 写的「URI 化 + 消 31 条 alias」已被 R11-9/10 提前消化——components/ 下 32 个组件均已入 qt_add_qml_module（URI NeonBoard），手工 alias 仅剩 10 条 property。剩余实质是把内联区块继续迁 components。逐块花括号配对 + 标识符核对选出首批对象：execQueueDialog（3507-3964，458 行，R7-02 执行队列弹窗）完全自包含——块内零 window 引用、引用的七个 id（execQueueList / queueHeader / queueHeaderLine / itemPulse / upMouse / downMouse / skipMouse）全部块内自声明、括号平衡，迁移接口面为零；依赖仅 context property（taskRuns / scheduler / Theme）。其余三张执行域弹窗（execConfirmDialog / execPanelDialog / execBatchDialog，合计约 370 行）与主内容层 Item（933-1851，919 行）耦合较深，留后续批次。维护者 2026-11-09 会话指令「剩余仅 QML 批次（Main.qml 3946 行 URI 化 + 拆分）与 §9 补 MOD 阶段行 执行」（§9 MOD 行已先行合并 aae4a19），据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/Main.qml;qml/components/**;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过（编译 → ctest → --selftest → --smoke → 模块守卫 → 看板一致性）且 --smoke 日志 QML 致命告警 0;新增 qml/components/ExecQueueDialog.qml（根元素 DialogShell，块体逐字照搬原 458 行）且 Main.qml 中 execQueueDialog 段替换为一行实例化（id 保留原名，Main 与其它组件的既有调用点零改动）;Main.qml 行数 <= 3700（基线 4131）且 LINE_LIMITS 中 qml/Main.qml 上限同步调低到实际行数;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：界面上打开执行队列面板（--exec-queue 或按钮）内容渲染与交互与迁移前一致
depends: 
createdAt: 2026-11-09T13:40:00+00:00
---

## 目标

把 `qml/Main.qml` 里完全自包含的 `execQueueDialog`（3507-3964，458 行，R7-02 执行队列弹窗）整块迁入新建的 `qml/components/ExecQueueDialog.qml`，Main.qml 以 `ExecQueueDialog { id: execQueueDialog }` 一行实例化替换。这是 QML 批次的第一批：选择它的依据是逐块核对证实零外部引用（七个子 id 全部块内自声明、零 window 引用），迁移接口面为零、行为风险最低。

## 验收

见 frontmatter `acceptance`（5 条）。核心是第 1、2、3 条：全量 verify 绿（--smoke QML 致命告警 0）、组件块体逐字照搬、id 保留原名使调用点零改动。第 5 条是诚实边界：弹窗 content 的绑定错误只在打开时暴露，--smoke 的 4 秒窗口测不到——打开路径请维护者人工复核（界面或 --exec-queue 启动参数）。

## 边界

见 frontmatter `scope`：

- `qml/components/**`：新增 ExecQueueDialog.qml；
- `qml/Main.qml`：execQueueDialog 段替换为一行实例化；
- `scripts/check_module_deps.py`：LINE_LIMITS 中 qml/Main.qml 上限从 4131 同步调低到实际行数。

**不动**：其余三张执行域弹窗（execConfirmDialog / execPanelDialog / execBatchDialog）、主内容层 Item（933-1851）、十个手工 alias、任何 C++。不碰台账契约。

## 输入

基线 `origin/main`。§9 MOD 阶段行已先行合入（aae4a19）。组件经 qt_add_qml_module 的 URI NeonBoard 注册，Main.qml 与组件互相引用走模块 import（R11-9/10 已就绪）。

## 产出

`qml/components/ExecQueueDialog.qml` + Main.qml 接管 + LINE_LIMITS 调低；合并提交 body 四段；交付说明单独提交，`note` 写「已完成 X / 待做 Y」与 nextStep（其余三张执行域弹窗、主内容层 933-1851 的拆分批次）。
