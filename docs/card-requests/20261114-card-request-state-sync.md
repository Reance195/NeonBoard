---
state: proposed
stage: DX
tier: P2
title: 立卡程序不回写请求单状态，44 份已完成卡的请求单永久挂 proposed 造成假积压
author: maintainer-session-2026-11-14
evidence: 2026-11-14 承接上一轮治理（#DX-166）后的对账实录。上一轮把「45 份 state=proposed 且无回执的请求单」判为「无客观依据判定已处理、按 Z12 不动」，该判断有误：以标题与 docs/board-ledger.json 的 cards.title 做精确匹配，44 份无 card 字段的请求单全部 1.00 命中且 baseline 全为 done（唯一例外 DX-119 为 abandoned），即它们早已立卡并交付，只是请求单自身没写 card 字段。根因两处：① scripts/register_board_cards.py 立卡成功只在 registered/<code>.json 写回执（含 form 路径），从不回写请求单 frontmatter 的 state / card，导致「受理」这个事实在请求单上不可见；② scripts/task_gate.py registrations 只做「回执/占位 ↔ 台账 cards」双向对账，完全不读请求单 state，于是漂移无人发现、CI 全绿。上一轮按「有 card 字段 + 有回执 + 卡 done」自动回写的 27 份恰好漏掉这一整类。剩余 1 份 docs/card-requests/20261107-r20-01.md 写了 card: R20-01 但台账无卡、registered/ 无回执，是唯一真实未受理项（P4 定时任务插件）。假积压的代价：看板与请求单清单无法据 state 判断真实积压，维护者需逐份人肉反查台账才能分辨，且每次审计都会重复得出「45 张待办」的错误结论。
scope: scripts/register_board_cards.py;scripts/task_gate.py;docs/card-requests/*;docs/card-requests/registered/*.json
acceptance: python scripts/register_board_cards.py --selftest 全过，且新增用例覆盖「立卡后回写 state: approved 与 card 字段」「--dry-run 不写请求单」「请求单已有 card 字段时只更新不重复插入」;python scripts/task_gate.py selftest 全过，且新增用例覆盖「卡已 done 但请求单仍 proposed 判为状态滞后」「无 card 无回执的真未受理请求单不误报」;python scripts/task_gate.py registrations 退出码 0，且能点名并硬拦状态滞后请求单;44 份已完成卡的请求单按标题精确匹配回写（done→state: closed，abandoned→state: rejected）并补齐 card 字段，R20-01 保持 proposed 不动;python scripts/check_scope.py --base origin/main 与 python scripts/check_path_refs.py --base origin/main 退出码 0;pwsh -File scripts/verify.ps1 -ScriptOnly 退出码 0;不新增脚本文件、不改 CI workflow、--no-verify 豁免面保持为空
depends: 
---

## 目标

让「请求单已被受理、对应卡已交付」成为请求单上可读且可机器校验的事实：立卡程序在发号成功后即回写 state/card，收尾对账能抓出任何状态滞后；同时把存量 44 份假积压一次清零，只保留 R20-01 这一张真实未受理项，使请求单清单首次能真实反映积压。

## 验收

见 frontmatter acceptance（7 条）。口径：两个 selftest 与 registrations 以退出码为准；范围与路径检查以 `--base origin/main` 增量口径为准；本卡只改 scripts/ 与 docs/card-requests/，不触及 qml/ 与 src/，脚本层跑 `verify.ps1 -ScriptOnly`（C++ 编译由 CI 对 main 常规兜底）。

## 边界

- 只改 scripts/register_board_cards.py 与 scripts/task_gate.py 两个已登记脚本，不新增脚本文件（docs/scripts-manifest.md 无需同步）、不改 .github/workflows/*（CI 已有 task_gate.py registrations 步骤，状态对账挂进去即自动生效）。
- 回写只发生在「立卡占位已推、台账与回执已落盘」之后；`--dry-run` 保持零写入（DX-65 纯化口径），发号失败/重试中间态不得提前改请求单。
- 状态机沿用既有取值：立卡成功→approved，卡 done→closed，卡 abandoned/rejected→rejected；不引入新状态值，不改 nbflow.py 的 STATES。
- 对账判据以台账事实为准，不猜：有 card 字段按卡号查 baseline，无 card 字段按 title 精确匹配 cards；两者都查不到且无回执的（R20-01）只告警不硬拦，避免把真未受理项判成违规。
- 不动回执 sha256 判据、不动 task_gate 既有「回执/占位 ↔ cards」硬拦语义、不改 baseline、不手改任何既有卡的契约字段。
- R20-01 的受理与否属产品范围决策，本卡不代办：保持 state: proposed，仅在请求单正文补一句「台账无卡无回执，为唯一真实未受理项」供维护者拍板。

## 输入

基线 origin/main 9378012。现场：scripts/register_board_cards.py:115-150（parse_form）、:409（selftest）、:761-785（写台账与回执）；scripts/task_gate.py:697-755（cmd_registrations）、:900+（selftest）；docs/card-requests/ 下 119 份请求单与 registered/ 下 106 份回执。状态口径样例：20261105-auto-close-hook-marker.md（approved + card）、20261105-board-tip-only-check.md（closed + card）、20261106-dx-115.md（rejected + card）。依赖：无。

## 产出

① register_board_cards.py 新增回写函数（一处实现、正式路径调用）+ 3 例 selftest；② task_gate.py registrations 新增状态滞后对账（硬拦滞后 / 告警真未受理）+ selftest 用例；③ 44 份请求单回写 state 与 card、R20-01 正文标注、本卡自身请求单补 approved + card；④ 合并提交 body 写明根因（回执写了但请求单没写）与两处判据的收口方式；⑤ 卡片 note 记录对账数字（44 份 1.00 命中 / 43 done / 1 abandoned / 1 真未受理）。
