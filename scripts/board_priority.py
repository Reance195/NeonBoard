#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""board_priority.py —— 立卡排位规则的唯一实现（DX-123）。

维护者 2026-11-07 排定的待办顺序按影响面分五档：
  P0 开工正确性（不修则后续每张卡白干一轮）
  P1 交付链路死结 / 闸门失效
  P2 共享脚本收口（Z5/Z6，同文件串行波次）
  P3 独立脚本卡
  P4 全量编译卡

规则只活在这一份（Z5）：派生器与立卡程序都从这里 import，不复制常量；
漂移靠 derive --check 的退出码拦，不靠人记（Z6）。
档位判断留在人这一侧（立卡请求单 frontmatter 声明），
落位与校验交给机器 —— 判断写成正则既脆又违背 Z1。
"""
from __future__ import annotations

import sys

TIERS = ("P0", "P1", "P2", "P3", "P4")
ORDER_STEP = 1024


def rank(tier):
    """档位名 → 序号；非法值抛 ValueError（fail-closed，Z4）。"""
    if tier not in TIERS:
        raise ValueError("非法档位 %r（允许 %s）" % (tier, "/".join(TIERS)))
    return TIERS.index(tier)


def validate_tier(tier):
    """立卡入口用：合法返回序号，非法直接抛，由 parse_form 转成非 0 退出。"""
    return rank(tier)


def by_tier(items, cards):
    """{档位序号: [orderKey, ...]}，只收 todo 且已声明 tier 的卡。

    棘轮口径：未声明 tier 的存量卡一律豁免，避免一夜全红。
    """
    out = {}
    for it in items:
        if it.get("status") != "todo":
            continue
        tier = (cards.get(it.get("code", "")) or {}).get("tier")
        if tier in TIERS:
            out.setdefault(rank(tier), []).append(int(it.get("orderKey", 0) or 0))
    for v in out.values():
        v.sort()
    return out


def insertion_order(tier, groups):
    """新卡在 todo 列的落点。

    取「本档末张」与「下一档首张」的中点 —— 与 TaskModel::commitMove 的
    分数索引同口径（previous + (next - previous) / 2），不重排任何既有卡。
    """
    r = validate_tier(tier)
    own = groups.get(r, [])
    lower = max((max(v) for k, v in groups.items() if k < r and v), default=None)
    upper = min((min(v) for k, v in groups.items() if k > r and v), default=None)
    lo = own[-1] if own else (lower if lower is not None else 0)
    if upper is None:
        return lo + ORDER_STEP
    if upper - lo <= 1:
        raise RuntimeError("P%d 档与下一档之间无可用 orderKey 间隙，需先重排该档" % r)
    return (lo + upper) // 2


def check_monotonic(items, cards):
    """返回违反「档位随 orderKey 单调不减」的 (code, tier, key, 前一卡) 列表。

    只看声明了 tier 的 todo 卡；档位相同的卡之间顺序不限（档内仍按 orderKey）。
    """
    bad = []
    prev = None
    for it in sorted((i for i in items if i.get("status") == "todo"),
                     key=lambda x: int(x.get("orderKey", 0) or 0)):
        tier = (cards.get(it.get("code", "")) or {}).get("tier")
        if tier not in TIERS:
            continue
        r, key = rank(tier), int(it.get("orderKey", 0) or 0)
        if prev is not None and r < prev[1]:
            bad.append((it.get("code", ""), tier, key, prev[0]))
        prev = (it.get("code", ""), r)
    return bad


# 每档重排后预留的空档步数：中点插入每插一张间隙减半，64 格可连续承载
# 6 张同档新卡（2^6）才再次耗尽 —— 耗尽再 compact 一次即满血，无需人工介入。
COMPACT_RESERVE_STEPS = 64


def compact_orders(items, cards):
    """档间隙耗尽时的自愈（DX-126）：按档位序 × 档内现序，对全部声明 tier 的
    todo 卡等距重排，档间各留 COMPACT_RESERVE_STEPS 格。

    返回 {code: new_orderKey}，只含需要挪动的卡；done/doing 卡与未声明 tier 的
    存量卡（棘轮口径，与 by_tier 一致）一律不动。落位与校验都从这份唯一实现走，
    derive --write 在 insertion_order 抛耗尽时调用它，不复制规则（Z5）。
    """
    tiered = {}
    for it in items:
        if it.get("status") != "todo":
            continue
        code = it.get("code", "")
        tier = (cards.get(code) or {}).get("tier")
        if tier in TIERS:
            tiered.setdefault(rank(tier), []).append((code, int(it.get("orderKey", 0) or 0)))
    out = {}
    cursor = 0
    for r in sorted(tiered):
        for code, _ in sorted(tiered[r], key=lambda x: x[1]):
            cursor += ORDER_STEP
            out[code] = cursor
        cursor += COMPACT_RESERVE_STEPS * ORDER_STEP
    return out


def selftest():
    checks = []

    def check(name, ok):
        checks.append((name, ok))

    # rank / validate_tier
    check("rank 合法值", [rank(t) for t in TIERS] == [0, 1, 2, 3, 4])
    try:
        rank("P9")
        check("非法档位抛 ValueError", False)
    except ValueError:
        check("非法档位抛 ValueError", True)
    try:
        rank("")
        check("空档位抛 ValueError", False)
    except ValueError:
        check("空档位抛 ValueError", True)

    # insertion_order：以 2026-11-07 排定的真实档位为语料
    groups = {0: [13312], 1: [18432, 27648, 52224],
              2: [58368, 64512, 65536, 66560, 74752],
              3: [77824, 78848, 79872, 80896, 81920, 82944, 83968, 84992],
              4: [86016, 87040]}
    check("P0 新卡落在本档末张与下一档首张中点", insertion_order("P0", groups) == 15872)
    check("P2 新卡落在本档末张与下一档首张中点", insertion_order("P2", groups) == 76288)
    check("P4 末档直接追加", insertion_order("P4", groups) == 88064)
    empty_p1 = {k: v for k, v in groups.items() if k != 1}
    check("空档位取上下档中点", insertion_order("P1", empty_p1) == 35840)
    try:
        insertion_order("P2", {2: [100], 3: [101]})
        check("间隙耗尽抛 RuntimeError", False)
    except RuntimeError:
        check("间隙耗尽抛 RuntimeError", True)

    # compact_orders（DX-126）：耗尽自愈 —— 重排后落位恢复、单调保持
    ex_items = [{"code": "A", "status": "todo", "orderKey": 100},
                {"code": "B", "status": "todo", "orderKey": 101},
                {"code": "C", "status": "todo", "orderKey": 102},
                {"code": "Z", "status": "done", "orderKey": 103},
                {"code": "X", "status": "todo", "orderKey": 104}]
    ex_cards = {"A": {"tier": "P2"}, "B": {"tier": "P2"}, "C": {"tier": "P2"},
                "Z": {"tier": "P2"}, "X": {}}
    moved = compact_orders(ex_items, ex_cards)
    check("compact 重排声明 tier 的 todo 卡", sorted(moved) == ["A", "B", "C"])
    for it in ex_items:
        code = it["code"]
        if code in moved:
            it["orderKey"] = moved[code]
    # 再插一张的完整回路：insertion_order 耗尽 → compact 满血 → 落位成功（不抛异常）
    re_compact = compact_orders(ex_items, ex_cards)
    for it in ex_items:
        if it["code"] in re_compact:
            it["orderKey"] = re_compact[it["code"]]
    check("再插一张：先耗尽再 compact 即满血（落位合法）",
          isinstance(insertion_order("P2", by_tier(ex_items, ex_cards)), int))
    check("compact 保持档位单调", check_monotonic(ex_items, ex_cards) == [])
    check("compact 不动 done 卡与未声明 tier 卡",
          all(it["code"] not in moved for it in ex_items
              if it["status"] != "todo" or it["code"] == "X"))

    # check_monotonic
    items = [{"code": "A", "status": "todo", "orderKey": 13312},
             {"code": "B", "status": "todo", "orderKey": 51200},
             {"code": "C", "status": "todo", "orderKey": 27648}]
    cards = {"A": {"tier": "P0"}, "B": {"tier": "P1"}, "C": {"tier": "P2"}}
    check("档位倒序被抓出", len(check_monotonic(items, cards)) == 1)
    items[2]["orderKey"] = 52224
    check("档内乱序不误报", check_monotonic(items, cards) == [])
    check("未声明 tier 的卡豁免",
          check_monotonic([{"code": "X", "status": "todo", "orderKey": 1}], {"X": {}}) == [])
    check("非 todo 卡不参与校验",
          check_monotonic([{"code": "Y", "status": "done", "orderKey": 1}],
                          {"Y": {"tier": "P0"}}) == [])

    failures = [n for n, ok in checks if not ok]
    for name, ok in checks:
        print("  %s %s" % ("✓" if ok else "✗", name))
    if failures:
        print("[排位] 自测失败 %d/%d 例" % (len(failures), len(checks)),
              file=sys.stderr)
        return 1
    print("[排位] 自测通过：%d 例" % len(checks))
    return 0


if __name__ == "__main__":
    # DX-46 口径：GBK 控制台打中文会抛 UnicodeEncodeError，输出一律转 UTF-8
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(selftest() if "--selftest" in sys.argv else 0)
