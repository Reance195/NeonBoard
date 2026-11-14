---
state: closed
card: MOD-22
stage: MOD
tier: P4
title: MOD SettingsPanel 拆分五批：执行分页与 profileEditor 整迁 components/ExecPage
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 实测（基线 origin/main，SettingsPanel.qml 928 行，LINE_LIMITS 同值）：迁移单元 = 分页 4「任务执行」（281-486，206 行，含 execSettingsHint @479）+ profile 系函数段（62-86，25 行：editProfile / profilePromptModes / profileOutputModes / splitArgs）+ profileEditor Dialog（705-926，222 行），合计 453 行。双向引用面正则全扫：单元内 14 个 id 均无单元外引用（execSettingsHint 被 profileEditor @920 写、editProfile @417/464 被执行页调，两者同单元内自洽——MOD-21 note 预判的「跨页提示接口化」因整单元迁移而自然消解，无需 signal）；panel.* 六个成员全部随迁自解：editProfile / splitArgs / profilePromptModes / profileOutputModes 转组件内作用域，panel.contentItem（711）与 panel.width（716）→ hostShell，panel.page（287）→ 实例化处；agentProfile / taskRuns / taskExec 等均为上下文属性。接口面 = property hostShell（2 处）+ visible 绑定一行。维护者 2026-11-10 会话指令「领下一卡：执行页+profileEditor 拆分」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: qml/components/SettingsPanel.qml;qml/components/**;CMakeLists.txt;scripts/check_module_deps.py
acceptance: pwsh -File scripts/verify.ps1 全量通过且 --smoke 日志 QML 致命告警 0 行;新增 qml/components/ExecPage.qml（执行页 ColumnLayout、profile 系函数段、profileEditor Dialog 块体逐字照搬，仅 panel.contentItem/panel.width→hostShell、panel.editProfile/splitArgs/profilePromptModes/profileOutputModes→组件内作用域与 visible 行移实例化处）且 SettingsPanel 中 62-86 函数段、281-486 执行页段、705-926 profileEditor 段删除并替换为带 id/visible/hostShell 绑定的一处实例化;SettingsPanel.qml 行数 <= 490（基线 928）且 LINE_LIMITS 中 qml/components/SettingsPanel.qml 上限同步调低到实际行数;CMakeLists 同时登记 qt_add_qml_module 源条目与 QT_RESOURCE_ALIAS 裸名（MOD-11 教训前置）;python scripts/check_module_deps.py --base origin/main 退出 0 且 python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：执行页（执行器总开关 / agent profile 列表与编辑弹窗全流程 / triage 设置）字段渲染与交互与迁移前一致
depends:
createdAt: 2026-11-10T16:05:00+00:00
---

## 目标

把 SettingsPanel.qml 的迁移单元（执行页 281-486 + profile 系函数段 62-86 + profileEditor 705-926，共 453 行）整块迁入新建 `qml/components/ExecPage.qml`。SettingsPanel 侧替换为一处实例化（id / visible / hostShell 三行绑定）。

## 验收

见 frontmatter `acceptance`（6 条）。第 6 条诚实边界：执行页与 profile 编辑弹窗全流程请维护者人工复核。

## 边界

见 frontmatter `scope`：

- `qml/components/**`：新增 ExecPage.qml；
- `qml/components/SettingsPanel.qml`：删三段 + 实例化接管；
- `CMakeLists.txt`：双登记两条；
- `scripts/check_module_deps.py`：LINE_LIMITS SettingsPanel 同步调低。

**不动**：其余两个分页（关于 / 外观）、DataBackupPage / HotkeyPage / PluginPage、navRail、任何 C++。接手现场单独 `chore(claim)` 提交。

## 输入

基线 `origin/main`。MOD-11~21 已结卡（分页整迁模式四轮验证、CMake 双登记、新迁移模板已根治块根闭括号坑）。

## 产出

`qml/components/ExecPage.qml` + SettingsPanel 瘦身（预计降至 ~481）+ CMake 双登记 + LINE_LIMITS 调低；合并提交 body 四段；交付说明单独 `docs(board):` 提交，note 写「已完成 X / 待做 Y」与 nextStep（关于 / 外观两批次收官盘点）。
