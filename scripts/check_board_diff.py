#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_board_diff.py —— 改台账时判断「这一档验收够不够」。

为什么要有这个文件
------------------
`docs/board-ledger.json` 看起来像文档，实际是 `tst_projectmap` 的测试输入：它读 `stage`
排序、把 `baseline` 当 done 判据、校验 `cards` 与 roadmap 行对应。改了它而只跑
`verify.ps1 -ScriptOnly`，本地全绿、CI 红 —— 主线被弄红过一次就是这条路径。

但反过来「改台账就全量」也太重：全量要编译。实测增量构建 196 秒、冷启动 3.8 分钟，
塞进 `-ScriptOnly` 会让「两秒验收」失去意义。

所以按「改了什么」分流，判定是机械的（比 JSON），不需要人记：

  只动 baseline / claims（结卡、领用登记，最高频）
      → `derive_board_status.py --check` 覆盖派生一致性，秒级
  动了 cards 结构（立卡、改 scope、改 stage，低频但影响 tst_projectmap）
      → 必须全量

退出码（与 verify.ps1 的接入约定）
  0 这一档够（台账没改，或只有 baseline / claims 变化且派生一致）
  1 派生不一致 —— 本脚本已转发 derive 的诊断，修完重跑
  2 需要全量 —— cards 结构变了，-ScriptOnly 不适用

边界
----
本脚本不做 `tst_projectmap` 的等价校验，只决定「这一档够不够」。真校验仍在编译产物里。
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
LEDGER_REL = "docs/board-ledger.json"
DERIVE = "scripts/derive_board_status.py"

# 卡片字段：任一变化都要全量（tst_projectmap 会读 stage / scope，其余字段变了
# 虽不直接影响断言，但改卡片结构属于低频高风险动作，一并要求全量更省心）
CARD_FIELDS = ("stage", "title", "scope", "depends", "acceptance", "note")


def read_from_git(ref: str, rel: str) -> dict | None:
    """读 `ref:rel` 的 JSON；文件在该 ref 下不存在时返回 None。"""
    proc = subprocess.run(
        ["git", "show", f"{ref}:{rel}"],
        cwd=str(REPO_ROOT), capture_output=True, check=False,
    )
    if proc.returncode != 0:
        return None
    try:
        return json.loads(proc.stdout.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None


def diff_cards(old: dict, new: dict) -> tuple[list[str], list[str], list[str]]:
    """返回 (新增卡号, 删除卡号, 变动的卡号)。只比 id 集合与字段值。"""
    old_cards = old.get("cards") or {}
    new_cards = new.get("cards") or {}
    added = sorted(set(new_cards) - set(old_cards))
    removed = sorted(set(old_cards) - set(new_cards))
    changed = sorted(
        code for code in (set(old_cards) & set(new_cards))
        if old_cards[code] != new_cards[code]
    )
    return added, removed, changed


def describe_changed(old: dict, new: dict, codes: list[str]) -> list[str]:
    """给每张变动卡列出「改了哪几个字段」，摘要要能直接定位问题。"""
    lines = []
    for code in codes:
        a = old.get("cards", {}).get(code, {}) or {}
        b = new.get("cards", {}).get(code, {}) or {}
        fields = [f for f in CARD_FIELDS if a.get(f) != b.get(f)]
        extra = sorted(k for k in (set(a) | set(b)) if k not in CARD_FIELDS and a.get(k) != b.get(k))
        parts = fields + extra
        lines.append(f"{code}（{'、'.join(parts) if parts else '整体'}）")
    return lines


def run_derive() -> int:
    """跑派生一致性校验，原样转发输出。返回其退出码。"""
    proc = subprocess.run(
        [sys.executable, str(REPO_ROOT / DERIVE), "--check"],
        cwd=str(REPO_ROOT), capture_output=True, check=False,
    )
    out = proc.stdout.decode("utf-8", errors="replace")
    err = proc.stderr.decode("utf-8", errors="replace")
    for line in (out + err).splitlines():
        print("  " + line)
    return proc.returncode


def classify(old: dict, new: dict) -> tuple[str, list[str]]:
    """判断这次台账改动属于哪一档，返回 (action, 说明行)。

    action：
      'skip'   —— 台账没实质变化，这一档够
      'derive' —— 可以留在轻档，但仍要校验派生一致性（子进程 derive --check）
      'full'   —— 必须全量

    为什么「新增卡」不等于「必须全量」：立卡是每张卡的必经步骤，而它必然改 cards。
    若把新增卡也判成 full，任何含立卡的分支在 -ScriptOnly 上都会被拦 —— 脚本卡就再也
    用不了轻档（DX-15 白做）。真正需要警惕的是**改动既有卡的契约**（stage / scope 等）
    和**删卡**，那才是会让 tst_projectmap 与 roadmap 对不上的操作。

    「新增卡的 stage 是否是陌生取值」要判：stage 第一次出现时，roadmap 里未必有对应行，
    这正是 `stageOrderFollowsRoadmapRows` 会红的情形。
    """
    old_cards = old.get("cards") or {}
    new_cards = new.get("cards") or {}
    added, removed, changed = diff_cards(old, new)
    lines: list[str] = []

    if removed or changed:
        lines = [f"  卡片结构有变化（{len(added)} 新增 / {len(removed)} 删除 / {len(changed)} 修改）"]
        if added:
            lines.append("    新增：" + "、".join(added))
        if removed:
            lines.append("    删除：" + "、".join(removed))
        for line in describe_changed(old, new, changed):
            lines.append("    修改：" + line)
        lines.append("  改动了既有卡片的契约，tst_projectmap 可能读不到对应 roadmap 行；请跑全量：")
        lines.append("    pwsh -File scripts/verify.ps1")
        return "full", lines

    if added:
        known = {c.get("stage") for c in old_cards.values() if c.get("stage")}
        fresh = sorted({new_cards[c].get("stage") for c in added} - known - {None})
        lines = [f"  新增 {len(added)} 张卡：{'、'.join(added)}（立卡，属正常流动）"]
        if fresh:
            lines.append(f"    首次出现的 stage：{'、'.join(fresh)} —— roadmap 里未必有对应行")
            lines.append("  新 stage 需要全量确认 roadmap 行：")
            lines.append("    pwsh -File scripts/verify.ps1")
            return "full", lines

    base_changed = (old.get("baseline") or {}) != (new.get("baseline") or {})
    claims_changed = (old.get("claims") or {}) != (new.get("claims") or {})
    if not (added or base_changed or claims_changed):
        return "skip", ["  · 台账未改动，跳过"]

    what = [n for n, f in (("cards 新增", bool(added)), ("baseline", base_changed), ("claims", claims_changed)) if f]
    lines.append(f"  · 只动了 {'、'.join(what)}，校验派生一致性（新增卡必须已在快照里）")
    return "derive", lines


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="改台账时判断验收档位")
    ap.add_argument("--base", help="比较基准 ref（如 origin/main）；省略则只跑 --selftest")
    ap.add_argument("--selftest", action="store_true", help="跑内置断言，不依赖仓库状态")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.base:
        ap.error("必须给 --base <ref> 或 --selftest")

    old = read_from_git(args.base, LEDGER_REL)
    new = read_from_git("HEAD", LEDGER_REL)
    if old is None or new is None:
        # base 里还没有台账，或 HEAD 里没有（不该发生）—— 无从比较，放过
        print("  · 台账在基准或 HEAD 中不存在，跳过")
        return 0

    action, lines = classify(old, new)
    for line in lines:
        print(line)
    if action == "skip":
        return 0
    if action == "full":
        return 2

    code = run_derive()
    if code != 0:
        print("  派生快照与 git 历史 / 台账不一致：先跑 derive_board_status.py --write 补推")
        return 1
    return 0


# ---------------- 自测 ----------------

def selftest() -> int:
    fails: list[str] = []

    def check(name: str, cond: bool) -> None:
        if not cond:
            fails.append(name)
        print(("  ok   " if cond else "  FAIL ") + name)

    def ledger(cards=None, baseline=None, claims=None) -> dict:
        return {"cards": cards or {}, "baseline": baseline or {}, "claims": claims or {}}

    def card(code: str, **kw) -> dict:
        base = {"id": code, "stage": "DX", "title": "t", "scope": ["a"], "depends": [], "acceptance": [], "note": ""}
        base.update(kw)
        return base

    # 1. 完全相同
    a = ledger({"DX-1": card("DX-1")}, {"DX-1": "done"}, {"DX-1": {"branch": "x"}})
    b = ledger({"DX-1": card("DX-1")}, {"DX-1": "done"}, {"DX-1": {"branch": "x"}})
    check("未改动 → 三个差异集全空", diff_cards(a, b) == ([], [], []))

    # 2. 新增卡
    b = ledger({"DX-1": card("DX-1"), "DX-2": card("DX-2")}, {"DX-1": "done"}, {})
    added, removed, changed = diff_cards(a, b)
    check("新增卡 → added 命中 DX-2", added == ["DX-2"] and not removed and not changed)

    # 3. 删除卡
    added, removed, changed = diff_cards(b, a)
    check("删除卡 → removed 命中 DX-2", removed == ["DX-2"] and not added and not changed)

    # 4. 卡片字段改动（stage）
    b = ledger({"DX-1": card("DX-1", stage="R14")}, {"DX-1": "done"}, {})
    added, removed, changed = diff_cards(a, b)
    check("改 stage → changed 命中 DX-1", changed == ["DX-1"] and not added and not removed)
    check("改 stage → 摘要点名字段", describe_changed(a, b, changed) == ["DX-1（stage）"])

    # 5. 改 scope 也在 CARD_FIELDS 里
    b = ledger({"DX-1": card("DX-1", scope=["b"])}, {"DX-1": "done"}, {})
    _, _, changed = diff_cards(a, b)
    check("改 scope → 仍判为结构变化", changed == ["DX-1"])

    # 6. 只改 baseline → cards 无差异
    b = ledger({"DX-1": card("DX-1")}, {"DX-1": "todo"}, {})
    check("仅 baseline 变化 → cards 无差异", diff_cards(a, b) == ([], [], []))
    check("仅 baseline 变化 → baseline 判定为变", (a.get("baseline") or {}) != (b.get("baseline") or {}))

    # 7. 只改 claims → cards 无差异
    b = ledger({"DX-1": card("DX-1")}, {"DX-1": "done"}, {"DX-2": {"branch": "y"}})
    check("仅 claims 变化 → cards 无差异", diff_cards(a, b) == ([], [], []))
    check("仅 claims 变化 → claims 判定为变", (a.get("claims") or {}) != (b.get("claims") or {}))

    # 8. 空 inputs 不炸
    check("缺 cards 键不炸", diff_cards({}, {}) == ([], [], []))

    # 9-13. 分档（classify）—— 这才是闸门的实际判据
    act, _ = classify(a, a)
    check("未改动 → skip", act == "skip")

    act, _ = classify(a, ledger({"DX-1": card("DX-1")}, {"DX-1": "todo"}, {}))
    check("仅 baseline 变化 → derive", act == "derive")

    act, _ = classify(a, ledger({"DX-1": card("DX-1")}, {"DX-1": "done"}, {"DX-9": {"branch": "z"}}))
    check("仅 claims 变化 → derive", act == "derive")

    # 立卡：新增卡但 stage 是既有取值 → 不该逼人全量（否则脚本卡的轻档永远用不了）
    act, lines = classify(a, ledger({"DX-1": card("DX-1"), "DX-2": card("DX-2")}, {"DX-1": "done"}, {}))
    check("新增卡 + stage 已知 → derive（不是 full）", act == "derive")
    check("新增卡 → 说明里列出卡号", any("DX-2" in l for l in lines))

    # 新增卡但 stage 是全新的 → roadmap 里未必有对应行，必须全量
    fresh = ledger({"DX-1": card("DX-1"), "M99": card("M99", stage="M99X")}, {"DX-1": "done"}, {})
    act, lines = classify(a, fresh)
    check("新增卡 + 陌生 stage → full", act == "full")
    check("陌生 stage → 说明里点名", any("M99X" in l for l in lines))

    # 改既有卡 / 删卡 → full
    act, _ = classify(a, ledger({"DX-1": card("DX-1", stage="R14")}, {"DX-1": "done"}, {}))
    check("改既有卡 stage → full", act == "full")
    act, _ = classify(a, ledger({}, {"DX-1": "done"}, {}))
    check("删卡 → full", act == "full")

    # 改既有卡的非契约字段（title）也算改动既有卡 → full（保守）
    act, _ = classify(a, ledger({"DX-1": card("DX-1", title="改了标题")}, {"DX-1": "done"}, {}))
    check("改既有卡 title → full", act == "full")

    print()
    if fails:
        print(f"自测失败 {len(fails)} 项：" + "；".join(fails))
        return 1
    print("自测通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
