---
state: approved
stage: DX
title: DX-84 显式 sys.path 判据只落在 board-apply.py：三个脚本跨模块 import 在 python -I 下即崩，且无机器判据防回归
author: agent-dx116b-session-2026-11-07
evidence: 2026-11-07 DX-116 交付日实录。① `python -I scripts/derive_board_status.py --close-cards` 报 ModuleNotFoundError: No module named 'board_priority'；同日 `python -I scripts/register_board_cards.py --help` 以同一异常当场复现 —— 立卡工具自己立不了卡。② AST 全仓扫描 scripts/*.py：board-apply.py（DX-84 修复现场）有显式 sys.path.insert(SCRIPT_DIR)；而 derive_board_status.py→board_priority、check_board_diff.py→derive_board_status、register_board_cards.py→board_priority 三个脚本同样 import 本地模块、同样没有显式 sys.path 处理。③ DX-84 的坑（python -I 不含脚本目录，跨模块 import 必崩）在 pitfalls 有记录，但只修了撞崩的那一个文件 —— 判据没有沉淀成「凡 scripts/*.py import 同目录本地模块，必须显式 sys.path」的机器检查，同类缺口原样留着，本次第二次撞上（Z6：踩坑两次，沉淀成机器判据）。
scope: scripts/derive_board_status.py;scripts/check_board_diff.py;scripts/register_board_cards.py;scripts/check_script_hygiene.py;docs/pitfalls.md
tier: P2
acceptance: 三个脚本（derive_board_status / check_board_diff / register_board_cards）按 board-apply.py 模式补 SCRIPT_DIR 显式 sys.path.insert，写法与既有实现一致;python -I scripts/derive_board_status.py --selftest、python -I scripts/check_board_diff.py --selftest、python -I scripts/register_board_cards.py --selftest 退出码均为 0（隔离模式不再崩）;Z6 机器判据：check_script_hygiene.py 增规则 —— scripts/*.py 若 import 同目录本地模块而源码无显式 sys.path 处理则拒（无本地 import 的脚本不受影响），自测含正反例;python scripts/check_script_hygiene.py 退出码 0（存量全过），python scripts/derive_board_status.py --selftest 与 python scripts/register_board_cards.py --selftest 不回归;docs/pitfalls.md 记条目：判据只修现场不修类别（DX-84 修了撞崩的 board-apply.py，同类的三个原样留着）—— 第二次撞上才补上机器判据，正好是 Z6 说的「靠记性不靠退出码」的代价
depends: DX-84
suggestedId: DX-125
createdAt: 2026-11-07T01:42:00+00:00
---

## 目标
把 DX-84 的判据从「修一个文件」补成「一条规则 + 一道机器检查」，
让 `python -I`（以及任何不含脚本目录的调用方式）跑仓库脚本不再靠运气。

现状（2026-11-07 AST 全仓扫描）：

| 脚本 | 本地 import | 显式 sys.path |
| --- | --- | --- |
| board-apply.py | acceptance_split, check_scope | ✅（DX-84 修） |
| derive_board_status.py | board_priority | ❌ |
| check_board_diff.py | derive_board_status | ❌ |
| register_board_cards.py | board_priority | ❌ |

正常调用（`python scripts/x.py`）时 Python 会把脚本目录放进 sys.path[0]，所以
CI、钩子、git-done 全都碰不到这个坑 —— 坑只在 `python -I`（隔离模式）下现形，
而 -I 恰是排查不可信脚本时的推荐跑法（DX-84 三连崩实录的起因）。

## 验收
见 frontmatter `acceptance`（5 条，分号分隔）。核心是第 3 条 —— **Z6 机器判据**：
三个脚本的修复是一次性的，规则（check_script_hygiene 新增项）才是防回归的主体；
没有它，下一个新增本地 import 的脚本会原样再坑一次。

## 边界
scripts/derive_board_status.py;scripts/check_board_diff.py;scripts/register_board_cards.py;scripts/check_script_hygiene.py;docs/pitfalls.md

- 三个脚本的修复照抄 board-apply.py 的既有模式（SCRIPT_DIR 计算 + sys.path.insert），
  不发明第二种写法（Z5）。
- check_script_hygiene 的新规则用 AST 判定 import，不做字符串 grep —— 与该文件既有
  规则的实现口径一致；规则只约束 scripts/*.py，不外溢到 plugins/、tests/。
- 不改任何脚本的行为语义：补 sys.path 在正常调用下是幂等的（目录已在 path 里，
  insert 重复无害），CI / 钩子 / git-done 的调用方式零影响。

## 输入
基线 origin/main（含 DX-116 合并提交 9e1c685）。board-apply.py 的修法见其
SCRIPT_DIR 段（约 73 行）；DX-84 的坑记录在 docs/pitfalls.md 相应条目。

## 产出
合并提交 body 四段（变更 / 验收 / 影响 / 卡片）；pitfalls 条目（§1.x 顺延编号，
注意与本批另一张卡的条目编号衔接）。
