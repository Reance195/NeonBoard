---
state: approved
stage: DX
title: 交互回归判据与 GUI 实机验收通道沉淀
author: maintainer-session-2026-11-06
evidence: 2026-11-06 本会话为验证 TaskCard 修复临时写了 qmltestrunner 最小复现（10 例全过，含按钮点击/整卡点击/双击/拖拽与 id 点号访问语义）与 PowerShell UIA 合成输入脚本，用完即删，仓库内没有留下回归判据；computer-use skill 依赖的 orca CLI 在本机不存在，手写 GUI 自动化时 SetForegroundWindow 首次失效、窗口非前台导致 hover 与点击全落空，直到 SwitchToThisWindow 加 Alt 键技巧才激活成功，期间合成键鼠有发到别的前台窗口的风险。两处 QML 陷阱（id 点号访问、MouseArea 层叠）都可静态判定。
scope: scripts/check_qml_pitfalls.py;scripts/gui-uia-verify.ps1;tests/qml/tst_taskcard_actions.qml;tests/CMakeLists.txt;.github/workflows/ci.yml;docs/pitfalls.md
acceptance: 静态检查能扫出 sweepBand.sweepAnimation.restart() 这类 id 点号访问并给出文件与行号（脚本自测断言）;静态检查能报出 anchors.fill 的 MouseArea 声明在内容层之后且 z 相同的层叠隐患（脚本自测断言）;qmltestrunner 用例覆盖按钮点击/整卡点击/双击/拖拽并由 ctest 调度且全绿;GUI 验收工具在目标窗口无法置前时安全中止而不是把合成键鼠发到别的窗口;pwsh -File scripts/verify.ps1 全过
depends: 
suggestedId: DX-102
---

## 目标
把本次靠临时脚本才验证出来的两处 QML 陷阱变成仓库内可复跑的判据，并把「激活窗口 + UIA 按可访问名称定位 + 合成输入」沉淀成不会误伤别的窗口的工具；复杂度入场费是同类问题第二次出现时不必再摸索一遍（Z6）。

## 验收
见 frontmatter `acceptance`（5 条），含静态检查自测、ctest 调度与 GUI 工具的安全中止。

## 边界
只新增检查脚本、QML 交互用例与 GUI 工具，接 `tests/CMakeLists.txt` 与 CI；不改 `TaskCard.qml` 业务代码，不改 README 里「tests 只收纯逻辑 C++ 单测」的约定（若确实要放宽，需在卡片 note 里先写明论证）。

## 输入
基线 `origin/main`；依赖：无；Qt 6.8.3 的 `qmltestrunner` 与 Windows UIA 程序集。

## 产出
合并提交 body 四段；`docs/pitfalls.md` 收录「id 点号访问恒为 undefined」与「兄弟层叠吞点击」两条现象的机器判据。
