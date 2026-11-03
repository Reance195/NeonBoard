#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""board-view.py —— 把台账 + 建卡计划渲染成一张只读 HTML 面板。

为什么要有这个文件
------------------
台账是 JSON、计划是命令行输出，两者都要人「读」，读之前还得先知道命令怎么敲。
面板的价值不是好看，是**让人不用问 agent 也能看到当前状态**：哪几张在办、
哪几张是计划中的、被谁挡住、有没有卡在争同一个文件。

与 board-apply.py 的分工：那边判定「能不能立」，这边只负责「画出来」。
判定逻辑全部复用（唯一实现），本文件不重复实现任何校验。

**只读**：不写台账、不写 board.db、不调 --import-ledger。生成的是自包含 HTML
（无外部 CDN），双击即可看。要进 NeonBoard 自己的面板，走
`NeonBoard.exe --import-ledger`（需卡已入账）。

用法
----
    python scripts/board-view.py                          # 仅台账
    python scripts/board-view.py --manifest docs/module-split-card-requests.md
    python scripts/board-view.py -o build/board.html
"""

import argparse
import html
import importlib.util
import io
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent


def _load_apply():
    """按文件路径加载 board-apply.py（文件名带连字符，不能直接 import）。"""
    path = SCRIPT_DIR / "board-apply.py"
    spec = importlib.util.spec_from_file_location("board_apply", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


A = _load_apply()

STAGE_ORDER = ["MOD", "DX", "R14", "R13", "R12", "R11", "R10", "R9", "R8",
               "R7", "R6", "R5", "R4", "R3", "R2", "R1", "R0"]

CSS = """
:root{--bg:#0d1117;--panel:#161b22;--panel2:#1c2128;--line:#30363d;
--fg:#e6edf3;--dim:#8b949e;--accent:#2f81f7;--ok:#3fb950;--warn:#d29922;--bad:#f85149;--neon:#58a6ff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);
font:14px/1.55 -apple-system,"Segoe UI","Microsoft YaHei",sans-serif}
.wrap{max-width:1180px;margin:0 auto;padding:28px 22px 60px}
h1{font-size:19px;margin:0 0 4px;font-weight:600;letter-spacing:.2px}
.sub{color:var(--dim);font-size:12.5px;margin-bottom:22px}
.kpi{display:grid;grid-template-columns:repeat(5,1fr);gap:12px;margin-bottom:26px}
.k{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:13px 15px}
.k .n{font-size:24px;font-weight:600;line-height:1.2}
.k .l{font-size:11.5px;color:var(--dim);margin-top:2px;letter-spacing:.3px}
.k.plan .n{color:var(--neon)}
h2{font-size:13px;text-transform:uppercase;letter-spacing:1.1px;color:var(--dim);
margin:26px 0 11px;font-weight:600}
.cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(330px,1fr));gap:13px}
.card{background:var(--panel);border:1px solid var(--line);border-left:3px solid var(--dim);
border-radius:9px;padding:13px 15px}
.card.ready{border-left-color:var(--ok)}
.card.blocked{border-left-color:var(--warn)}
.card.open{border-left-color:var(--accent)}
.hd{display:flex;align-items:baseline;gap:9px;margin-bottom:5px}
.id{font-family:ui-monospace,Consolas,monospace;font-size:12.5px;color:var(--neon);font-weight:600}
.ttl{font-size:13.5px;font-weight:500}
.meta{font-size:11.5px;color:var(--dim);font-family:ui-monospace,Consolas,monospace}
.chip{display:inline-block;font-size:11px;padding:1.5px 8px;border-radius:20px;
border:1px solid var(--line);color:var(--dim);margin:7px 5px 0 0}
.chip.ok{color:#7ee787;border-color:#238636;background:#3fb95015}
.chip.warn{color:#e3b341;border-color:#9e6a03;background:#d2992215}
.chip.bad{color:#ffa198;border-color:#8e1519;background:#f8514915}
.chip.todo{color:#79c0ff;border-color:#1f6feb;background:#2f81f715}
table{width:100%;border-collapse:collapse;font-size:12.5px;
background:var(--panel);border:1px solid var(--line);border-radius:9px;overflow:hidden}
th{text-align:left;font-weight:600;font-size:11px;letter-spacing:.6px;color:var(--dim);
text-transform:uppercase;padding:9px 12px;background:var(--panel2);border-bottom:1px solid var(--line)}
td{padding:8px 12px;border-bottom:1px solid #21262d;vertical-align:top}
tr:last-child td{border-bottom:none}
td.mono{font-family:ui-monospace,Consolas,monospace;color:var(--neon);white-space:nowrap}
.notes{background:var(--panel);border:1px solid var(--line);border-radius:9px;padding:4px 0}
.note{display:flex;gap:9px;padding:8px 14px;font-size:12.5px;border-bottom:1px solid #21262d}
.note:last-child{border-bottom:none}
.note .tag{font-family:ui-monospace,Consolas,monospace;font-size:11px;color:var(--dim);
min-width:96px;flex-shrink:0}
.note.warn .tag{color:#e3b341}.note.bad .tag{color:#ffa198}
.donegrid{display:flex;flex-wrap:wrap;gap:5px}
.donegrid span{font-family:ui-monospace,Consolas,monospace;font-size:11px;color:var(--dim);
background:var(--panel);border:1px solid var(--line);border-radius:5px;padding:2px 7px}
footer{margin-top:32px;color:#484f58;font-size:11.5px}
code{background:var(--panel2);padding:1px 5px;border-radius:4px;
font-family:ui-monospace,Consolas,monospace;font-size:11.5px}
"""


def esc(value) -> str:
    return html.escape(str(value), quote=True)


def stage_key(cid: str) -> tuple:
    prefix = A.stage_prefix(cid)
    rank = STAGE_ORDER.index(prefix) if prefix in STAGE_ORDER else 99
    return (rank, cid)


def build_page(ledger: dict, cards: dict, blockers, warnings, infos, source: str) -> str:
    base, all_cards = ledger["baseline"], ledger["cards"]
    claims = ledger.get("claims") or {}
    done = [c for c in all_cards if base.get(c) == "done"]
    open_ids = sorted([c for c in all_cards if base.get(c) != "done"], key=stage_key)
    open_scopes = {c: len(all_cards[c].get("scope") or []) for c in open_ids}

    parts = []
    add = parts.append
    add("<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">")
    add("<title>NeonBoard 治理面板</title><style>%s</style></head><body><div class=\"wrap\">" % CSS)
    add("<h1>NeonBoard 治理面板</h1>")
    add("<div class=\"sub\">只读视图 · 数据源 <code>%s</code> · 不写入任何文件</div>" % esc(source))

    add("<div class=\"kpi\">")
    for num, label, cls in (
        (len(all_cards), "台账卡片", ""),
        (len(done), "已结", ""),
        (len(open_ids), "在办", ""),
        (len(claims), "领用占位", ""),
        (len(cards), "计划中（未入账）", "plan"),
    ):
        add("<div class=\"k %s\"><div class=\"n\">%d</div><div class=\"l\">%s</div></div>"
            % (cls, num, label))
    add("</div>")

    if cards:
        add("<h2>计划中 · 尚未入账</h2><div class=\"cards\">")
        for cid in sorted(cards, key=stage_key):
            card = cards[cid]
            state = A.ready_state(cid, card, ledger)
            cls = "ready" if state == "就绪" else "blocked"
            add("<div class=\"card %s\">" % cls)
            add("<div class=\"hd\"><span class=\"id\">%s</span><span class=\"ttl\">%s</span></div>"
                % (esc(cid), esc(card.get("title", ""))))
            add("<div class=\"meta\">stage=%s · scope %d 条 · acceptance %d 条</div>"
                % (esc(card.get("stage", "?")), len(card.get("scope") or []),
                   len(card.get("acceptance") or [])))
            chip_cls = "ok" if state == "就绪" else "warn"
            add("<span class=\"chip %s\">%s</span>" % (chip_cls, esc(state)))
            deps = card.get("depends") or []
            for dep in deps:
                met = base.get(dep) == "done"
                add("<span class=\"chip %s\">%s %s</span>"
                    % ("ok" if met else "warn", "依赖", esc(dep)))
            add("</div>")
        add("</div>")

    if open_ids:
        add("<h2>在办 · %d 张</h2><table><tr><th>卡号</th><th>标题</th>"
            "<th>stage</th><th>scope</th><th>depends</th></tr>" % len(open_ids))
        for cid in open_ids:
            card = all_cards[cid]
            deps = "、".join(card.get("depends") or []) or "—"
            add("<tr><td class=\"mono\">%s</td><td>%s</td><td>%s</td><td>%d</td>"
                "<td class=\"mono\">%s</td></tr>"
                % (esc(cid), esc(card.get("title", "")), esc(card.get("stage", "")),
                   open_scopes[cid], esc(deps)))
        add("</table>")

    if blockers or warnings or infos:
        add("<h2>校验结果</h2><div class=\"notes\">")
        for code, cid, msg in blockers:
            add("<div class=\"note bad\"><span class=\"tag\">✗ %s</span><span>%s · %s</span></div>"
                % (esc(code), esc(cid), esc(msg)))
        for code, cid, msg in warnings:
            add("<div class=\"note warn\"><span class=\"tag\">⚠ %s</span><span>%s · %s</span></div>"
                % (esc(code), esc(cid), esc(msg)))
        for code, cid, msg in infos:
            add("<div class=\"note\"><span class=\"tag\">· %s</span><span>%s · %s</span></div>"
                % (esc(code), esc(cid), esc(msg)))
        add("</div>")

    if done:
        add("<h2>已结 · %d 张</h2><div class=\"donegrid\">" % len(done))
        for cid in sorted(done, key=stage_key):
            add("<span title=\"%s\">%s</span>"
                % (esc(all_cards[cid].get("title", "")), esc(cid)))
        add("</div>")

    add("<footer>生成自 scripts/board-view.py · 卡片定义以 docs/board-ledger.json 为唯一事实源 · "
        "要进 NeonBoard 应用面板请先入账再跑 <code>NeonBoard.exe --import-ledger</code></footer>")
    add("</div></body></html>")
    return "".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser(description="台账 + 建卡计划 → 只读 HTML 面板")
    parser.add_argument("--manifest", help="建卡请求清单（可选）")
    parser.add_argument("--ledger", default=str(REPO_ROOT / "docs" / "board-ledger.json"))
    parser.add_argument("--repo", default=str(REPO_ROOT))
    parser.add_argument("-o", "--out", default=str(REPO_ROOT / "build" / "board-view.html"))
    args = parser.parse_args()

    try:
        ledger = A.load_ledger(Path(args.ledger).resolve())
        cards, _ = A.load_manifest(Path(args.manifest).resolve()) if args.manifest else ({}, [])
    except (OSError, ValueError, SystemExit) as exc:
        sys.stderr.write("✗ 读取失败：%s\n" % exc)
        return 4

    blockers, warnings, infos = A.validate(cards, ledger, Path(args.repo).resolve()) if cards \
        else ([], [], [])

    source = "docs/board-ledger.json" + (" + %s" % args.manifest if args.manifest else "")
    page = build_page(ledger, cards, blockers, warnings, infos, source)

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    io.open(out, "w", encoding="utf-8", newline="\n").write(page)
    print("✓ 面板已生成：%s（%d 字节）" % (out, len(page.encode("utf-8"))))
    print("  在办 %d / 已结 %d / 计划中 %d / 提示 %d"
          % (sum(1 for v in ledger["baseline"].values() if v != "done"),
             sum(1 for v in ledger["baseline"].values() if v == "done"),
             len(cards), len(blockers) + len(warnings) + len(infos)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
