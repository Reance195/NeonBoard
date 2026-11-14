---
state: closed
card: DX-146
stage: DX
tier: P1
title: GUI 主流程只有自包含夹具与日志字符串匹配，缺真实组件回归
author: codebuddy-session-2026-11-08
evidence: 工程化分析实测：tests/CMakeLists.txt:604-606 的 tst_taskcard_actions 注释自承是「自包含夹具复刻」，不加载真实 qml/components/TaskCard.qml；GUI 主流程回归实际靠 ci.yml 的 --smoke 4 秒日志加 scripts/check_qml_warnings.py 字符串匹配再加人工。仓库已有 scripts/gui-uia-verify.ps1（Windows UIA 冒烟）但未接进 CI。45 个 CTest 目标里没有第二个 QML 用例。结构性缺口：真实组件的事件路由、层叠、焦点回归没有机器判据。维护者 2026-11-08 会话指令「按你的建议来」采纳建议 C，授权立卡并排位 P1。
scope: tests/CMakeLists.txt;tests/qml;.github/workflows/ci.yml;scripts/gui-uia-verify.ps1;scripts/check_qml_warnings.py
acceptance: 新增至少 1 个基于真实 qml/components/TaskCard.qml 的 QML Test（qmltestrunner），覆盖点击/双击/拖拽事件路由，不再只靠自包含复刻夹具;scripts/gui-uia-verify.ps1 接进 ci.yml 冒烟步骤或独立 job，失败即拦合入，--smoke 日志判据保留作为补充;ctest --test-dir build -C Release --output-on-failure 全过；pwsh -File scripts/verify.ps1 -ScriptOnly 全过（scope 无代码路径，轻档）
depends: 
suggestedId: DX-146
rejectReason: 
createdAt: 2026-11-08T10:45:00+00:00
---

## 目标
给 GUI 主流程补一层真实组件回归，把「日志无致命告警」升级为「关键交互断言」，并让已有的 UIA 冒烟真正进 CI 闸门。

## 验收
见 frontmatter acceptance（3 条，分号分隔）。核心是第 1、2 条：真实组件用例加 UIA 冒烟接 CI；两者都必须是机器判据，不能只靠人肉看日志。

## 边界
见 frontmatter scope。

- 不改应用侧 QML 组件行为（发现缺陷另立卡），只补测试与 CI 接线。
- 保留 --smoke 与 check_qml_warnings.py 的日志判据，不替换、只叠加。
- QML Test 要能在无显示器的 CI（offscreen）跑，不依赖真实窗口。

## 输入
基线 origin/main。现有 QML 测试见 tests/CMakeLists.txt:604-606（QT_QPA_PLATFORM=offscreen、qmltestrunner）；CI 冒烟步骤见 ci.yml:241-269；UIA 脚本见 scripts/gui-uia-verify.ps1；日志判据单源见 scripts/check_qml_warnings.py。

## 产出
tests/qml/ 下新增真实组件用例、tests/CMakeLists.txt 注册、ci.yml 接 UIA 冒烟；合并提交 body 说明断言覆盖面与 CI 落点。
