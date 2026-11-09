---
state: proposed
card: DX-157
stage: DX
tier: P2
title: DX 台账 canon 序列化收敛单点：五处手写复制改走共享模块并写后自校验
author: codebuddy-session-2026-11-09
evidence: 2026-11-09 实测（MOD-9 会话，基线 origin/main）：docs/board-ledger.json 的规范序列化（indent=2、ensure_ascii=False、尾换行、键序=登记序、禁 sort_keys）在 task_gate.py:172-179 有唯一判据（台账格式漂移报错文案），但**序列化实现本身在 5 处各自手写复制**——register_board_cards.py:778、derive_board_status.py:1273、claim-card.py:275、merge_board.py:197 四个台账写者各自 json.dumps(indent=2)，与 task_gate 的判据不同源；任何一个写者改动或新增写者都可能再漂移。该风险在 MOD-9 期间两次实际爆发：立卡 C2 提交被 pygate 以 5 行「疑似密钥」拦下（实为台账与 HEAD 的全文件重排噪声），rebase 重放又把 6671 行重排带进 main（DX-156 事后把文件拉回规范）。实测核实现行 4 个写者都已写对（indent=2），缺陷是**实现复制无单点**而非某处写错——故本卡做 Z14 修一族：canon 序列化收敛共享模块，判据与写者同源。维护者 2026-11-09 会话指令「立卡修复 register/derive 台账 indent 缺陷」，据此授权立卡、领用、实现、verify、rebase、推 origin/main 与结卡。
scope: scripts/board_ledger_json.py;scripts/task_gate.py;scripts/register_board_cards.py;scripts/derive_board_status.py;scripts/claim-card.py;scripts/merge_board.py
acceptance: python scripts/board_ledger_json.py --selftest 退出 0（断言 indent=2、ensure_ascii=False、尾换行、键序保持登记序、禁 sort_keys、round-trip 幂等）;python scripts/task_gate.py selftest 退出 0（判据改走共享模块后既有断言全过）且其台账格式判据与 board_ledger_json.is_canonical 同源（grep 断言 task_gate.py 不再出现独立的 indent=2 序列化实现）;python scripts/register_board_cards.py --selftest 与 python scripts/check_script_hygiene.py 退出 0;对本仓现行 docs/board-ledger.json 跑 is_canonical 退出 0（现行文件满足规范，证明收敛是无行为变化的等价替换）;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿（scope 无代码路径，轻档）
depends: 
createdAt: 2026-11-09T04:00:00+00:00
---

## 目标

把 `docs/board-ledger.json` 的规范序列化（`indent=2`、`ensure_ascii=False`、尾换行、键序=登记序、禁 `sort_keys`）收敛到新建的 `scripts/board_ledger_json.py` 单点：`dump_ledger` / `is_canonical` / `--selftest`。`task_gate.py` 的格式判据改走 `is_canonical`（与判据同源），四个台账写者（register / derive / claim-card / merge_board）的落盘改调 `dump_ledger`。此后任何写者都无法写歪，判据漂移在结构上不可能（Z5/Z14）。

## 验收

见 frontmatter `acceptance`（5 条）。核心是第 1、2、4 条：共享模块自测过、判据同源、对现行文件等价替换（无行为变化）。第 3 条保证既有工具链不破。

## 边界

见 frontmatter `scope`，六个路径：

- `scripts/board_ledger_json.py`：新建共享模块（唯一新增）；
- `scripts/task_gate.py`：格式判据改调 `is_canonical`，删除本地 canon 实现；
- `scripts/register_board_cards.py` / `scripts/derive_board_status.py` / `scripts/claim-card.py` / `scripts/merge_board.py`：台账落盘点改调 `dump_ledger`。

**不动**：progress.json 的写盘（derive 的 `write_json_atomic` 写的是 progress.json 与 contract 导出，其 indent=4 + sort_keys 是该文件自己的规范，与台账无关）、回执（`registered/*.json` 有自己的 dump_receipt）、roadmap、hooks.db。不改任何台账内容，只改序列化实现归属。

## 输入

基线 `origin/main`。判据文本：task_gate.py:172-179「必须与 json.dumps(indent=2, ensure_ascii=False) + 尾换行 完全一致（键序=登记序，禁止 sort_keys 重排）」。实测：4 个写者现行输出均满足规范（MOD-9 会话逐点核对行号见 evidence）。

## 产出

`scripts/board_ledger_json.py`（单点 + selftest）+ 四个写者落盘点改走单点 + task_gate 判据同源化；合并提交 body 四段；交付说明单独提交，`note` 写「已完成 X / 待做 Y」。
