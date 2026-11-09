#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""merge_board.py — 台账 JSON 冲突的三路语义合并（供 merge-resolve.ps1 调用）。

为什么要有这个文件：`docs/board-ledger.json` 的合并冲突几乎必然是「两边各加了几张卡 /
各结了几张卡」，语义上根本不冲突，但 git 按行合并会把 JSON 切成带冲突标记的两半。
手工解这种冲突极容易整段删掉某一方的卡 —— 冲突标记把对象切开了，肉眼很难看出少没少，
而派生器只在「baseline 引用了不存在的卡」这种硬矛盾上才会报。

三路合并的规则（base = 公共祖先，ours = HEAD，theirs = MERGE_HEAD）：

  cards      按 id。一方没有、另一方与 base 相同 → 采信「删除」；一方改了、另一方没改 →
             采信改的那方；两边都改 → 逐字段合并，字段级冲突保留 ours 并把差异打出来。
  baseline   按短号，值冲突保留 ours 并打印。
  claims     按短号，同上。
  其余顶层键 取 ours，与 theirs 不同则打印。

任何「保留 ours 而丢弃 theirs」的情况都会逐条打印到 stderr —— 自动合并可以替人做决定，
但不能悄悄替人做决定。

刻意不做的事：写 baseline / cards 的**判断**（哪张卡真做完了、哪张卡该删）。本脚本只在
git 已经把两边摆在你面前时做机械的取并集，不创造任何一份没有出现在某一方的内容。

用法：
    python scripts/merge_board.py --merge [--dry-run]
    python scripts/merge_board.py --selftest

退出码：0 合并完成并已写回 / 1 无法合并（多半是结构性冲突，需手工）/ 2 参数或环境错误。
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))
import board_ledger_json as blj  # noqa: E402  台账 canon 序列化唯一实现（Z5），判据与写者同源

REPO = Path(__file__).resolve().parent.parent
LEDGER_REL = "docs/board-ledger.json"

CARD_FIELDS = ("id", "stage", "title", "scope", "depends", "acceptance", "note")


class Conflict(Exception):
    """结构性冲突：三路合并无法给出可信结果。"""


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True, encoding="utf-8")


def read_ref(ref: str, rel: str = LEDGER_REL):
    """从 git 对象库读某个 ref 下的文件；不存在返回 None（新增/删除文件的情形）。"""
    out = git("show", f"{ref}:{rel}")
    if out.returncode != 0:
        return None
    try:
        return json.loads(out.stdout)
    except json.JSONDecodeError as exc:
        raise Conflict(f"{ref}:{rel} 不是合法 JSON：{exc}") from exc


def merge_value(base, ours, theirs, path: str, notes: list[str]):
    """三路合并单个值。只有「两边都改了且改得不一样」才算冲突。"""
    if ours == theirs:
        return ours
    if ours == base:
        return theirs          # 我方没动，采信对方
    if theirs == base:
        return ours            # 对方没动，采信我方
    notes.append(f"{path}：两侧值不同，保留 ours（theirs={theirs!r}）")
    return ours


def merge_dict(base: dict, ours: dict, theirs: dict, path: str, notes: list[str],
               deep: bool = False) -> dict:
    merged: dict = {}
    for key in sorted(set(base) | set(ours) | set(theirs)):
        in_b, in_o, in_t = key in base, key in ours, key in theirs
        b = base.get(key)
        o = ours.get(key)
        t = theirs.get(key)
        where = f"{path}.{key}"
        if in_o and in_t:
            merged[key] = merge_dict(b or {}, o, t, where, notes, deep=True) if (deep and isinstance(o, dict) and isinstance(t, dict)) \
                else merge_value(b, o, t, where, notes)
        elif in_o:
            if not in_b:
                merged[key] = o          # 我方新增，对方没有 —— 不是冲突
            elif o == b:
                continue                 # 我方原样未动 → 认对方的删除
            else:
                notes.append(f"{where}：theirs 删除但 ours 有改动，保留 ours")
                merged[key] = o
        else:
            if not in_b:
                merged[key] = t          # 对方新增，我方没有 —— 不是冲突
            elif t == b:
                continue                 # 对方原样未动 → 认我方的删除
            else:
                notes.append(f"{where}：ours 删除但 theirs 有改动，保留 theirs")
                merged[key] = t
    return merged


def merge_ledger(base: dict, ours: dict, theirs: dict) -> tuple[dict, list[str]]:
    notes: list[str] = []
    merged = dict(ours)

    merged["cards"] = merge_dict(base.get("cards") or {}, ours.get("cards") or {},
                                 theirs.get("cards") or {}, "cards", notes, deep=True)
    merged["baseline"] = merge_dict(base.get("baseline") or {}, ours.get("baseline") or {},
                                    theirs.get("baseline") or {}, "baseline", notes)
    merged["claims"] = merge_dict(base.get("claims") or {}, ours.get("claims") or {},
                                  theirs.get("claims") or {}, "claims", notes, deep=True)

    for key in set(base) | set(theirs):
        if key in ("cards", "baseline", "claims"):
            continue
        if base.get(key) != ours.get(key) and theirs.get(key) != ours.get(key):
            notes.append(f"{key}：非卡片字段两侧不同，保留 ours")
    return merged, notes


def validate(ledger: dict) -> list[str]:
    """结构自洽：合并最容易犯的错是「卡没了但 baseline 还引用它」。"""
    problems = []
    cards = ledger.get("cards")
    if not isinstance(cards, dict):
        return ["cards 不是对象"]
    for cid, card in cards.items():
        if not isinstance(card, dict):
            problems.append(f"cards.{cid} 不是对象")
            continue
        for field in ("stage", "title"):
            if not card.get(field):
                problems.append(f"cards.{cid} 缺 {field}")
    for code in (ledger.get("baseline") or {}):
        if code not in cards:
            problems.append(f"baseline.{code} 指向不存在的卡（合并时可能误删了 cards 条目）")
    for code in (ledger.get("claims") or {}):
        if code not in cards:
            problems.append(f"claims.{code} 指向不存在的卡")
    return problems


def run_merge(dry_run: bool) -> int:
    head = git("rev-parse", "--verify", "HEAD")
    merge_head = git("rev-parse", "-q", "--verify", "MERGE_HEAD")
    if merge_head.returncode != 0:
        print("✗ 不在合并中（没有 MERGE_HEAD）。冲突合并只能在 git merge 停下时做。", file=sys.stderr)
        return 2
    mb = git("merge-base", "HEAD", "MERGE_HEAD")
    if mb.returncode != 0:
        print("✗ 找不到公共祖先（merge-base 失败）。", file=sys.stderr)
        return 2

    base_ref = mb.stdout.strip()
    try:
        base = read_ref(base_ref)
        ours = read_ref("HEAD")
        theirs = read_ref("MERGE_HEAD")
    except Conflict as exc:
        print(f"✗ {exc}", file=sys.stderr)
        return 1

    if ours is None or theirs is None:
        # 一方没有台账 = 文件新增冲突，无从三路合并（也没有可信的「取并集」语义）
        print("✗ 合并双方有一方没有 docs/board-ledger.json，需手工决定保留哪份。",
              file=sys.stderr)
        return 1
    base = base or {}

    merged, notes = merge_ledger(base, ours, theirs)

    problems = validate(merged)
    if problems:
        print("✗ 合并结果结构不自洽，未写回（git 里的冲突标记原样保留）：", file=sys.stderr)
        for p in problems:
            print(f"    {p}", file=sys.stderr)
        return 1

    added = [c for c in merged["cards"] if c not in ours["cards"]]
    from_theirs = [c for c in added if c in theirs["cards"]]

    if notes:
        print("· 保留 ours 而丢弃 theirs 的项：", file=sys.stderr)
        for n in notes:
            print(f"    {n}", file=sys.stderr)

    if dry_run:
        print(f"· 试算完成（未写盘）：cards {len(ours['cards'])} → {len(merged['cards'])}，"
              f"从 theirs 收入 {len(from_theirs)} 张")
        return 0

    target = REPO / LEDGER_REL
    target.write_text(blj.dump_ledger(merged),
                      encoding="utf-8", newline="\n")
    print(f"✓ 台账已三路合并：cards {len(ours['cards'])} → {len(merged['cards'])}"
          f"（从 theirs 收入 {len(from_theirs)} 张：{'、'.join(from_theirs) or '无'}）")
    print("  下一步：git add docs/board-ledger.json，然后 git commit --no-edit 完成合并")
    return 0


# ---------------------------------------------------------------- selftest

def selftest() -> int:
    failures = []

    def check(label: str, ok: bool) -> None:
        if ok:
            print(f"  ✓ {label}")
        else:
            failures.append(label)
            print(f"  ✗ {label}")

    def card(cid: str, title: str = "t") -> dict:
        return {"id": cid, "stage": "DX", "title": title, "scope": [], "depends": [],
                "acceptance": [], "note": ""}

    # 1. 纯新增：两边各加一张，取并集
    base = {"cards": {"A": card("A")}, "baseline": {}, "claims": {}}
    ours = {"cards": {"A": card("A"), "B": card("B")}, "baseline": {}, "claims": {}}
    theirs = {"cards": {"A": card("A"), "C": card("C")}, "baseline": {}, "claims": {}}
    merged, notes = merge_ledger(base, ours, theirs)
    check("纯新增取并集", sorted(merged["cards"]) == ["A", "B", "C"] and not notes)

    # 2. 同一张卡两侧改不同字段：字段级合并
    ours2 = {"cards": {"A": card("A", "ours-title")}, "baseline": {}, "claims": {}}
    theirs2 = {"cards": {"A": dict(card("A"), stage="MOD")}, "baseline": {}, "claims": {}}
    merged, notes = merge_ledger(base, ours2, theirs2)
    check("同卡不同字段能合并",
          merged["cards"]["A"]["title"] == "ours-title"
          and merged["cards"]["A"]["stage"] == "MOD")

    # 3. baseline 值冲突：保留 ours 且必须留痕
    b2 = {"cards": {"A": card("A")}, "baseline": {"A": "todo"}, "claims": {}}
    o2 = {"cards": {"A": card("A")}, "baseline": {"A": "doing"}, "claims": {}}
    t2 = {"cards": {"A": card("A")}, "baseline": {"A": "done"}, "claims": {}}
    merged, notes = merge_ledger(b2, o2, t2)
    check("baseline 值冲突保留 ours 并留痕",
          merged["baseline"]["A"] == "doing" and any("baseline.A" in n for n in notes))

    # 4. claims 双方都有：同号不同分支也必须留痕
    o3 = {"cards": {"A": card("A")}, "baseline": {},
          "claims": {"A": {"branch": "chore/ours"}}}
    t3 = {"cards": {"A": card("A")}, "baseline": {},
          "claims": {"A": {"branch": "chore/theirs"}}}
    merged, notes = merge_ledger(base, o3, t3)
    check("claims 同号冲突保留 ours 并留痕",
          merged["claims"]["A"]["branch"] == "chore/ours" and any("claims.A" in n for n in notes))

    # 5. 一方删除：另一方没动就用删除，动了就保留
    o4 = {"cards": {}, "baseline": {}, "claims": {}}          # ours 删了 A
    t4 = {"cards": {"A": card("A")}, "baseline": {}, "claims": {}}  # theirs 没动
    merged, _ = merge_ledger(base, o4, t4)
    check("我方删除、对方未动 → 认删除", "A" not in merged["cards"])

    o5 = {"cards": {}, "baseline": {}, "claims": {}}          # ours 删了 A
    t5 = {"cards": {"A": card("A", "改过")}, "baseline": {}, "claims": {}}  # theirs 改了
    merged, notes = merge_ledger(base, o5, t5)
    check("我方删除、对方改了 → 保留 theirs 并留痕",
          "A" in merged["cards"] and any("cards.A" in n for n in notes))

    # 6. 结构校验抓得到「卡被删了但 baseline 还在引用」
    bad = {"cards": {}, "baseline": {"A": "done"}, "claims": {}}
    check("结构校验抓 baseline 悬空", any("baseline.A" in p for p in validate(bad)))

    # 7. 合法台账不误报
    ok_ledger = {"cards": {"A": card("A")}, "baseline": {"A": "done"}, "claims": {}}
    check("合法台账不误报", validate(ok_ledger) == [])

    if failures:
        print(f"\n自测失败 {len(failures)} 项")
        return 1
    print("\n自测通过")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="台账 JSON 冲突三路语义合并")
    ap.add_argument("--merge", action="store_true", help="在 git merge 冲突状态下执行合并")
    ap.add_argument("--dry-run", action="store_true", help="只试算，不写回")
    ap.add_argument("--selftest", action="store_true", help="跑内置断言，不依赖仓库状态")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.merge:
        ap.error("必须给 --merge 或 --selftest")
    return run_merge(args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
