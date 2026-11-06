---
state: approved
stage: DX
title: 立卡入口单一事实源：文档、旧工具注释与预演输出对齐 DX-98 新流程
author: maintainer-session-2026-11-06
evidence: 2026-11-06 本会话照 docs/task-card.md 的立卡流程准备走 board-apply.py --commit，随后发现 AGENTS.md 条款 6 已被 DX-98 收口为 register_board_cards.py 唯一入口，commit-msg 对无回执的新增卡直接拒；同一天 board-apply.py --diff 的纯增量预演夹带既有卡 DX-96 的 depends 数组从 1 行重排为 3 行；register_board_cards.py 的 docstring 把立卡程序化标成 DX-97（实为 DX-98 的交付，DX-97 是卡片按钮修复）。
scope: docs/task-card.md;scripts/board-apply.py;scripts/register_board_cards.py;docs/pitfalls.md
acceptance: docs/task-card.md 的立卡流程示例改为 register_board_cards.py --form 且不再出现 --commit 的旧写法（rg 断言）;board-apply.py 的 --commit 在无回执时明确报错并指向新入口，或降级为只读预演且 rg 断言不再有写台账分支;board-apply.py --diff 的预演输出不再出现既有卡的行删除（纯增量文本判定）;脚本头注释中立卡程序化的卡号统一为 DX-98（rg 断言无 DX-97 误标）;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿
depends: 
suggestedId: DX-100
---

## 目标
让「怎样立卡」只有一份可执行答案：文档示例、旧工具行为、脚本注释都指向 `register_board_cards.py` + 回执，避免后来者按过期文档写出会被闸门拒的提交。

## 验收
见 frontmatter `acceptance`（5 条），含旧入口的行为收敛与预演输出的纯增量保形。

## 边界
只改文档与 `board-apply.py` / `register_board_cards.py` 的注释与入口提示，不改 `task_gate.py` 的回执判定（单一事实源不动），不改卡片契约字段。

## 输入
基线 `origin/main`（已含 DX-98 的回执闸门）；依赖：无。

## 产出
合并提交 body 四段；`docs/pitfalls.md` 补一条「主线被并行会话推进时以 origin/main 为准」的判据。


## 补充需求：验收项与 scope 交叉校验（2026-11-06 合并自草案）

> 来源：本会话起草的 `docs/card-requests/20261106-acceptance-scope-crosscheck.md` 草案；
> 维护者 2026-11-06 指令「合并草案进 DX-100」，**不另立卡、不占号**。本节与本卡原有五段式并列，
> 是本卡的补充需求。原请求单 frontmatter 保持立卡时的原貌，不回改历史输入。

### 触发证据
2026-11-06 DX-107 交付实录：该卡 `scope` 为 `scripts/git-done.ps1` + `docs/pitfalls.md`，
`acceptance` 第 3 条却写「`verify.ps1 -GitDoneDryRun` 在冲突分支上演练不再留现场（回归用例）」
——「回归用例」暗示要新增/修改 `scripts/verify.ps1`，而它不在 scope 内；实施 agent 按 Z11
不扩 scope，自行判为「实跑该命令作回归验证」并把复现步骤写进 `docs/pitfalls.md` 2.27，
未按 Z12 把歧义停下来上报。

根因两层：
1. DX-98 把立卡唯一入口收口到 `scripts/register_board_cards.py`，其 `parse_form` 只校验
   6 个 `REQUIRED_META` 与五段式章节，`board-apply.py` 的 20 项语义校验
   （`acceptance.prose` / `acceptance.unsplit` / `scope.missing_path` /
   `scope.glob_empty` / `scope.clash` / `scope.cover`）没有随之迁移；
2. 同类事故 R10-02「立卡漏写三个必需路径，agent 两次自补 scope」只作为反面案例
   写进 AGENTS.md 条款 6，没有机器判据。

### 建议判据
立卡写盘前加一条 `acceptance.scope_mismatch`：**验收项含新增语义词
（回归用例 / 新增用例 / 补用例 / 新增脚本）且引用 `scripts/*.ps1` / `scripts/*.py`
路径不在本卡 `scope` 内 → 拒绝立卡并点名该路径**。

不可一刀切按「出现的路径都必须在 scope 内」硬拦：几乎每张卡的验收项都含
`verify.ps1` 这条**只读验证工具**，会全量误伤。判据只针对带新增语义的验收项；
存量卡按棘轮只告警、不硬拦。判据实现只落一处（Z5），由 `register_board_cards.py`
写盘前调用，不改 `task_gate.py` 的回执判定、不改 `check_scope.py` 的越界判定、
不改卡片契约字段。

### 补充验收项（4 条；第 5 条 `-ScriptOnly 全绿` 已在原 acceptance）
1. `python scripts/register_board_cards.py --selftest` 覆盖新增判据：上述形态被拒
   且点名路径（退出码非 0）；
2. 纯验证命令（如 `pwsh -File scripts/verify.ps1 -ScriptOnly 全绿`）引用 scope 外脚本
   时不拦，selftest 断言零误伤；
3. 对 `docs/board-ledger.json` 全部在办卡回放新判据，命中清单打印出来，
   存量命中只告警不硬拦（棘轮，命中项要么修表单、要么点名进有主豁免面）；
4. `docs/task-card.md` 补一条「验收项提到的文件若在 scope 外，须写明是只读验证工具
   还是本卡产物」的写法约定（rg 断言）。

### 边界与输入
本卡 `scope` 已覆盖 `register_board_cards.py` / `board-apply.py` / `task-card.md` /
`pitfalls.md`，**无需扩 scope**。判据落点（复用 `board-apply.py` 的 `validate()`，
还是迁入 `register_board_cards.py`）由维护者拍板。无前置依赖。

### 待办
本节目前只在请求单层生效。若要让上述 4 条进入机器验收口径，还需把它们并入台账
`cards["DX-100"].acceptance`（任务定义变更：单独一条 `chore(board)` 提交，
body 带「授权：」「批次：」两行，且 `check_board_diff.py` 会按「改既有卡契约」
判为需全量验收）。

