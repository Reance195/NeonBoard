#!/usr/bin/env python3
"""check_scope.py —— 校验本次改动是否越出任务卡允许的路径范围。

用法：
    python scripts/check_scope.py --card R5-03 --base origin/main
    python scripts/check_scope.py --card R5-03 --base HEAD~1
    python scripts/check_scope.py --audit        # 体检：哪些 scope 一条文件都命中不了
    python scripts/check_scope.py --selftest     # 无仓库依赖的断言

规则：
  - 卡在 docs/board-ledger.json 的 cards[<短号>].scope 里声明允许的 glob；
    未知字段（acceptance / depends 等）本脚本不关心。
  - **目录式写法（尾斜杠）等价于 `dir/**`**：`plugins/agent-cursor/` 覆盖该目录下全部文件。
    DX-17 之前这里只按 glob 编译，`^plugins/agent-cursor/$` 只能匹配字面量目录名，
    一条真实文件都盖不住 —— 写了等于没写。
  - scope 缺失或为空 → 只打印警告并放行（棘轮：字段补齐后才强制）。
  - 每次改动必须命中至少一个 glob；ALWAYS_ALLOWED 里的路径不受限。
  - 退出码：0 通过 / 1 越界（--audit 时为「目录式 scope 命中 0」）/ 2 参数或数据错误。
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
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


def normalize_scope(pattern: str) -> str:
    """目录式写法统一成 `dir/**`。

    人写 scope 时很自然会写 `plugins/agent-cursor/`，而 glob 语义里这只是一串字面量，
    编译出来是 `^plugins/agent-cursor/$` —— 能匹配的只有「路径恰好等于目录名」这种
    不存在的东西。台账里因此有 6 处 scope 形同虚设（DX-17）。规范化放在这里，
    **不动 glob_to_regex 的语义**：board-apply.py 复用它做建卡校验，改语义会波及那边。
    """
    text = pattern.strip().replace("\\", "/")
    if text.endswith("/"):
        text = text.rstrip("/") + "/**"
    return text


def matches(scope: list[str], path: str) -> bool:
    """任一条 scope 命中该路径即为真（供 --selftest 与 --audit 共用）。"""
    return any(glob_to_regex(normalize_scope(s)).match(path) for s in scope if s)


def repo_files() -> list[str]:
    """仓库内全部文件的相对路径（POSIX 分隔符），跳过构建产物与工作区。"""
    skip = {
        ".git",
        "build",
        "build-local",
        "build-release",
        "node_modules",
        ".worktrees",
        "__pycache__",
        "dist",
        ".vs",
        ".qtc_clangd",
    }
    out: list[str] = []
    for root, dirs, files in os.walk(REPO_ROOT):
        dirs[:] = [d for d in dirs if d not in skip]
        for name in files:
            rel = os.path.relpath(os.path.join(root, name), REPO_ROOT)
            out.append(rel.replace(os.sep, "/"))
    return out


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


def load_ledger() -> dict:
    if not LEDGER.exists():
        print(f"找不到台账：{LEDGER}", file=sys.stderr)
        sys.exit(2)
    return json.loads(LEDGER.read_text(encoding="utf-8"))


def audit() -> int:
    """体检：把「写了但一条文件都命中不了」的 scope 挖出来。

    这类条目人眼看不出来 —— 台账里 6 处尾斜杠活了很久，直到有人在那些卡上改文件
    被判越界才暴露。判据分两档：目录式命中 0 属缺陷（退出 1）；其它命中 0 多半是
    「卡要建的文件还没建」，只提示不拦。
    """
    cards = (load_ledger().get("cards") or {})
    files = repo_files()
    total = 0
    dir_total = 0
    dir_empty: list[tuple[str, str]] = []
    other_empty: list[tuple[str, str]] = []
    for cid, card in sorted(cards.items()):
        for raw in card.get("scope") or []:
            if not raw:
                continue
            total += 1
            hit = any(glob_to_regex(normalize_scope(raw)).match(f) for f in files)
            if raw.strip().replace("\\", "/").endswith("/"):
                dir_total += 1
                if not hit:
                    dir_empty.append((cid, raw))
            elif not hit:
                other_empty.append((cid, raw))

    print(f"审计：{len(cards)} 张卡 / {total} 条 scope / 仓库 {len(files)} 个文件")
    print(f"  目录式（尾斜杠）{dir_total} 条 → 命中 0 的 {len(dir_empty)} 条")
    if other_empty:
        print(f"  其它 scope 命中 0 的 {len(other_empty)} 条（多为「卡要建的文件还没建」，仅提示）：")
        for cid, raw in other_empty:
            print(f"    {cid:9s} {raw}")

    if dir_empty:
        print("✗ 目录式 scope 一条文件都命中不了（写了等于没写）：", file=sys.stderr)
        for cid, raw in dir_empty:
            print(f"    {cid:9s} {raw}", file=sys.stderr)
        return 1
    print("✓ 目录式 scope 全部命中至少一个文件")
    return 0


def selftest() -> int:
    """无仓库依赖的断言：目录式语义、glob 边界、误伤防护。"""
    failures: list[str] = []
    checks = [0]

    def check(name: str, cond: bool) -> None:
        checks[0] += 1
        if not cond:
            failures.append(name)

    check("尾斜杠补 **", normalize_scope("plugins/agent-cursor/") == "plugins/agent-cursor/**")
    check("已是 ** 不重复补", normalize_scope("plugins/agent-cursor/**") == "plugins/agent-cursor/**")
    check("精确路径不变", normalize_scope("src/Database.cpp") == "src/Database.cpp")
    check("反斜杠统一", normalize_scope("plugins\\agent-cursor\\") == "plugins/agent-cursor/**")

    check(
        "目录式命中首层文件",
        matches(["plugins/agent-opencode/"], "plugins/agent-opencode/main.qml"),
    )
    check(
        "目录式命中嵌套文件",
        matches(["plugins/agent-opencode/"], "plugins/agent-opencode/i18n/zh.qml"),
    )
    check(
        "目录式不误伤同前缀目录",
        not matches(["plugins/agent-opencode/"], "plugins/agent-opencode-x/main.qml"),
    )
    check("单星不跨目录", not matches(["src/*.cpp"], "src/task/TaskModel.cpp"))
    check("双星跨目录", matches(["src/**"], "src/task/TaskModel.cpp"))
    check("docs/ 覆盖子目录", matches(["docs/"], "docs/adr/0002-run-state-and-executor-safety.md"))
    check("空 scope 不命中", not matches([], "src/main.cpp"))

    if failures:
        print("✗ 自检失败：", file=sys.stderr)
        for name in failures:
            print(f"    {name}", file=sys.stderr)
        return 1
    print(f"✓ 自检通过：{checks[0]} 项断言")
    return 0


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
    ap.add_argument("--audit", action="store_true", help="体检台账：列出命中 0 个文件的 scope")
    ap.add_argument("--selftest", action="store_true", help="跑内置断言（不读仓库）")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if args.audit:
        return audit()

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

    patterns = [glob_to_regex(normalize_scope(g)) for g in scope]
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
