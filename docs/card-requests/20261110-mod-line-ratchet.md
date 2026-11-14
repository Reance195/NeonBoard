---
state: closed
card: MOD-26
stage: MOD
tier: P4
title: MOD 拆分成果棘轮：迁移产出的 15 个组件文件登记 LINE_LIMITS
author: codebuddy-session-2026-11-10
evidence: 2026-11-10 实测（基线 origin/main）：MOD-11~24 迁移产出 16 个组件文件，仅 SettingsPanel.qml（185）在 LINE_LIMITS 管控内，其余 15 个未入管——「拆过又涨回来」的防线上有 15 个口子（check_module_deps.py:9 自述的历史教训）。15 文件现行行数（splitlines 口径，脚本实测生成非手抄）：BoardChrome 599、TaskEditorDialog 905、ExecQueueDialog 467、DataBackupPage 596、ExecPage 464、PluginPage 335、AboutPage 214、ExecPanelDialog 272、NightBatchDialog 227、ExecConfirmDialog 248、HotkeyPage 133、AppearancePage 97、NeonComboBox 76、DataButton 32、NeonField 18。本卡把 15 文件按当前行数登记为初始上限（棘轮「不得增加」语义，后续只降不升）。纯 scripts/ 改动，杂务豁免路径，仍按维护者指令立卡走台账。维护者 2026-11-10 会话指令「开立 MOD-26 棘轮下调卡」，据此授权立卡、实现、verify、rebase、推 origin/main 与结卡。
scope: scripts/check_module_deps.py;docs/card-requests/**
acceptance: pwsh -File scripts/verify.ps1 -ScriptOnly 通过（纯 scripts 改动走轻档）且 python scripts/check_module_deps.py --base origin/main 退出 0;LINE_LIMITS 新增上述 15 个组件文件条目，登记值与实测 splitlines 行数逐一相等（清单见 evidence，登记值由脚本生成防手抄错）;check_module_deps selftest 通过（python scripts/check_module_deps.py selftest 若无此子命令则以 --base origin/main 退出 0 为准）;python scripts/check_scope.py --base origin/main 退出 0;维护者人工复核：LINE_LIMITS 清单与 15 文件行数核对一致
depends:
createdAt: 2026-11-10T19:10:00+00:00
---

## 目标

把 MOD-11~24 迁移产出的 15 个组件文件按当前行数登记进 `check_module_deps.py` 的 `LINE_LIMITS`，棘轮锁死「不得增加」，补齐拆分成果的持续约束防线。

## 验收

见 frontmatter `acceptance`（5 条）。登记值由脚本实测生成，避免手抄错。

## 边界

见 frontmatter `scope`：`scripts/check_module_deps.py`（LINE_LIMITS 字典追加）与请求单收录。**不动**任何 QML/C++、不动既有 3 条 LINE_LIMITS 条目。

## 输入

基线 `origin/main`。MOD-11~25 已结卡。

## 产出

LINE_LIMITS 15 条新登记 + 合并提交；交付说明单独 `docs(board):` 提交。
