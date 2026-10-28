#!/usr/bin/env python3
"""check_scope.py —— 校验本次改动是否越出任务卡允许的路径范围。

用法：
    python scripts/check_scope.py --card R5-03 --base origin/main
    python scripts/check_scope.py --card R5-03 --base HEAD~1

规则：
  - 卡在 docs/board-ledger.json 的 cards[<短号>].scope 里声明允许的 glob；
    未知字段（acceptance / depends 等）本脚本不关心。
  - scope 缺失或为空 → 只打印警告并放行（棘轮：字段补齐后才强制）。
  - 每次改动必须命中至少一个 glob；ALWAYS_ALLOWED 里的路径不受限。
  - 退出码：0 通过 / 1 越界 / 2 参数或数据错误。
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
LEDGER = REPO_ROOT / "docs" / "board-ledger.json"

# 任何卡都允许改的路径（派生产物 / 说明文档）
ALWAYS_ALLOWED = [
    "progress.json",
    "docs/board-ledger.json",
]


def glob_to_regex(pattern: str) -> re.Pattern[str]:
    """把 glob 编译成正则，支持 ** 跨层级。"""
    out = []
    i = 0
    while i < len(pattern):
        ch = pattern[i]
        if pattern.startswith("**/", i):
            out.append(r"(?:.*/)?")
            i += 3
        elif pattern.startswith("**", i):
            out.append(r".*")
            i += 2
        elif ch == "*":
            out.append(r"[^/]*")
            i += 1
        elif ch == "?":
            out.append(r"[^/]")
            i += 1
        else:
            out.append(re.escape(ch))
            i += 1
    return re.compile("^" + "".join(out) + "$")


def changed_files(base: str) -> list[str]:
    proc = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=ACMR", f"{base}...HEAD"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        proc = subprocess.run(
            ["git", "diff", "--name-only", "--diff-filter=ACMR", base, "HEAD"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
    if proc.returncode != 0:
        print(f"git diff 失败：{proc.stderr.strip()}", file=sys.stderr)
        sys.exit(2)
    return [line.strip() for line in proc.stdout.splitlines() if line.strip()]


def load_card(code: str) -> dict:
    if not LEDGER.exists():
        print(f"找不到台账：{LEDGER}", file=sys.stderr)
        sys.exit(2)
    data = json.loads(LEDGER.read_text(encoding="utf-8"))
    cards = data.get("cards") or {}
    card = cards.get(code)
    if card is None:
        print(f"台账里没有卡片 {code}", file=sys.stderr)
        sys.exit(2)
    return card


def detect_card_from_commits(base: str) -> str | None:
    """从 base..HEAD 的提交信息里找 #短号，取第一个唯一值。"""
    proc = subprocess.run(
        ["git", "log", "--format=%B", f"{base}..HEAD"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        return None
    codes: list[str] = []
    for m in re.finditer(r"#([A-Za-z]{1,4}\d*-\d+|M\d+)", proc.stdout, re.IGNORECASE):
        code = m.group(1)
        if code not in codes:
            codes.append(code)
    if len(codes) == 1:
        return codes[0]
    if len(codes) > 1:
        print(f"⚠ 提交里出现多个短号 {codes}，跳过范围检查（请用 --card 显式指定）")
        return None
    return None


def main() -> int:
    # Windows 管道下 Python stdout 默认走 ANSI 代码页，打印 ✓/✗ 与中文直接 GBK 崩，
    # 退出码对了也白搭（R6-05 踩过：`UnicodeEncodeError: 'gbk' codec can't encode '✓'`）。
    # 与 check_script_hygiene.py / task_gate.py 同一口径的 reconfigure。
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    ap = argparse.ArgumentParser()
    ap.add_argument("--card", default="", help="任务短号，如 R5-03；缺省从提交信息里推断")
    ap.add_argument("--base", default="origin/main", help="比较基线，默认 origin/main")
    args = ap.parse_args()

    raw = args.card.strip() or (detect_card_from_commits(args.base) or "")
    if not raw:
        print("本次提交没有任务短号，跳过范围检查")
        return 0
    code = raw.lstrip("#").upper()
    card = load_card(code)

    scope = [s for s in (card.get("scope") or []) if s]
    files = changed_files(args.base)
    if not files:
        print("本次没有文件改动，跳过范围检查")
        return 0

    if not scope:
        print(
            f"⚠ 卡片 {code} 未声明 scope，跳过范围检查"
            f"（补上 cards[\"{code}\"].scope 后即生效）"
        )
        return 0

    patterns = [glob_to_regex(g) for g in scope]
    always = [glob_to_regex(g) for g in ALWAYS_ALLOWED]

    violations = []
    for f in files:
        if any(p.match(f) for p in always):
            continue
        if not any(p.match(f) for p in patterns):
            violations.append(f)

    if violations:
        print(f"✗ 改动越出卡片 {code} 的 scope：", file=sys.stderr)
        for v in violations:
            print(f"    {v}", file=sys.stderr)
        print("\n允许范围：", file=sys.stderr)
        for s in scope:
            print(f"    {s}", file=sys.stderr)
        print(
            "\n如果确实需要扩大范围：改 docs/board-ledger.json 里这张卡的 scope，"
            "在本次分支里一并提交（评审可见）。",
            file=sys.stderr,
        )
        return 1

    print(f"✓ 范围检查通过：{len(files)} 个改动全部落在 {code} 的 scope 内")
    return 0


if __name__ == "__main__":
    sys.exit(main())
