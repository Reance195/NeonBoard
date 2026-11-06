---
state: approved
stage: DX
title: 立卡排位规则落地：tier 档位声明与 orderKey 档位落位
author: maintainer-session-2026-11-07
evidence: 2026-11-07 维护者会话按「先修开工/交付链路，再修独立项，最后开全量编译卡」排定 21 张待办卡顺序并写入 progress.json，实测两次被应用回写与锚点还原冲掉（AGENTS.md 快照撞车主线优先）；derive_board_status.py:1062 对新卡一律 max_order+1024 追加列尾，排位规则无处落地。维护者确认按「判断留给人（立卡声明 tier）、落位交给机器（派生器计算）、漂移用退出码拦（derive --check）」落地。
scope: scripts/board_priority.py;scripts/register_board_cards.py;scripts/derive_board_status.py;scripts/task_gate.py;docs/task-card.md;docs/board-sync.md;AGENTS.md;docs/pitfalls.md;docs/board-ledger.json;progress.json;docs/card-requests/20261107-board-priority-tier.md
acceptance: python scripts/board_priority.py --selftest 全过（含档位倒序抓取、间隙耗尽、空档位落点、未声明 tier 豁免四类用例）;python scripts/register_board_cards.py --selftest 全过，新增判据：tier 缺失或非法枚举拒绝、build_card 写入 tier、改 tier 后 canonical_hash 变化;python scripts/task_gate.py --selftest 全过，新增判据：仅改 cards[].tier 判为任务定义变更（须 chore(board)+授权/批次两行）;python scripts/derive_board_status.py --check 退出码 0，新卡 orderKey 由 cards[].tier 经 insertion_order 计算落点、不再追加列尾;负测：请求单 frontmatter 写 tier: P9 时 register_board_cards.py --dry-run 非 0 退出并点名「非法档位」;负测：临时把任一已声明 tier 的 todo 卡 orderKey 移到更低档之前，derive --check 非 0 且点名卡号，还原后恢复 0;端到端：本卡交付后 DX-123 的 orderKey 落在 P2 档（介于 DX-113 与 DX-100 之间），存量 todo 卡 orderKey 除本卡外一个不变;docs/board-ledger.json 回填在办 todo 卡 cards[].tier 与排定档位一致且 derive --check 通过;pwsh -File scripts/verify.ps1 -ScriptOnly 全绿（本卡 scope 无代码路径，按脚本领验收）;pwsh -File scripts/verify.ps1 全量通过（回填既有卡被判「改既有卡」走 full 档）
depends:
suggestedId: DX-123
tier: P2
---

## 目标
把维护者 2026-11-07 排定的待办优先级从「一次性人工排序」变成机器判据：立卡时在请求单 frontmatter 声明 `tier`（P0..P4 五档），派生器按档位给新卡计算 orderKey 落点（本档末张与下一档首张的中点，同 TaskModel 拖拽的分数索引口径），`derive --check` 校验「档位随 orderKey 单调不减」拦住 UI 拖动与手改快照造成的漂移。判断留给人，落位交给机器（Z5 单一实现，Z6 退出码）。

## 验收
见 frontmatter `acceptance`（10 条）。要点：四条 selftest（board_priority / register / task_gate / derive --check）全部接入既有自测入口；两条负测（非法枚举、档位漂移）各有非 0 退出与点名输出；端到端以本卡自身为用例（P2 落点）。

## 边界
只新增 `scripts/board_priority.py` 并改动上列 4 个脚本与 4 份文档；不改 `scripts/check_board_diff.py`（其 `diff_cards` 按整卡 dict 比对，加字段自动判「改既有卡 → full」）、不改 `scripts/verify.ps1` 与 `scripts/check_script_hygiene.py`（新检查挂在已被 verify.ps1 与 board.yml 调用的 `derive --check` 上）；不改 `progress.json` 的 item schema（tier 只存台账，快照按 code 关联）；不改看板应用 C++ 侧（新卡导入本就采纳快照 orderKey，存量卡保留自身 order_key 是既有设计）。

## 输入
基线 origin/main；依赖：无（DX-113/DX-100 等持有同文件在办卡已在本卡排期中按序避让，实施前确认无未合并的同文件分支）。档位锚点：2026-11-07 已排定并落主线的 21 张待办卡顺序（P0: DX-82；P1: DX-117/110/121/78；P2: DX-88/116/120/101/113/122；P3: DX-100/111/115/119/114/112/43/37；P4: R0-12/102）。

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）。交付物：① `scripts/board_priority.py`（档位枚举、落点计算、单调校验、selftest）；② register/derive/task_gate 三处接入与 selftest 扩充；③ 在办 todo 卡 `cards[].tier` 回填；④ `docs/task-card.md` 五段式补「排位」字段与生效说明、`docs/board-sync.md` 补 orderKey 派生来源、`AGENTS.md` §6 补一条、`docs/pitfalls.md` 收录「立卡追加列尾 → 规则无处落地」条目。
