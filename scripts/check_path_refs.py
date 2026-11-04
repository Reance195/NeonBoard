#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_path_refs.py —— 校验仓库里对 src/ 文件的硬编码引用是否仍然存在。

为什么要有这个文件
------------------
`src/` 在 MOD-06 拆成 8 个模块子目录，一次拆目录就让全仓几十处旧的顶层路径变成死链。
编译器只看得见 `#include`，看不见脚本 / 台账 / 文档里的路径；当时靠「动手前手工 grep
一次」，仍然漏了两轮（`AGENTS.md`、`scripts/*.ps1`），都靠事后单独提交补救。

而「这个路径还在不在」是可以判定的 —— 不需要人记住 grep 命令和 5 类清单，所以做成闸门。

边界（不在这里重复实现的部分）
------------------------------
  - 卡片 scope 里的精确路径失效 → `scripts/check_scope.py --audit`（DX-18：已结卡不回改）
  - `project-map.json` 的 chain 证据断链 → `tst_projectmap`
  - `#include "Database.h"` 这类裸文件名    → 编译器自己会报，且不经过文本扫描也能发现

棘轮
----
接入当天存量必然不干净（拆目录后未回改的历史卡 scope、归档文档里的旧路径），全量一开就
满屏红的下场是被绕过。所以：
  - 默认 / `--audit`：全量审计，列出全部失效（人看历史包袱时用）；
  - `--base <ref>`：只报本次改动**新增**的失效引用，存量折叠成一行计数。
verify / CI 走 `--base`，闸门随时间收紧而不阻塞存量。

退出码：0 通过 / 1 存在失效引用 / 2 参数或环境错误。
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_ROOT = REPO_ROOT / "src"

# 引用形态：src/<相对路径>.cpp|.h
#   前界 (?<![\w./-])：避免把 my_src/a.cpp、./src/a.cpp 的一段当成独立引用
#   后界 (?![\w./-])：避免把 src/<名>.cpp.bak 截断成 src/<名>.cpp
REF_RE = re.compile(r"(?<![\w./-])(src/[A-Za-z0-9_./-]+\.(?:cpp|h))(?![\w./-])")

# 只有这些文本文件可能承载路径引用。二进制与构建产物一律不读。
SCAN_SUFFIXES = {
    ".ps1", ".py", ".yml", ".yaml", ".json", ".md", ".iss", ".txt",
    ".cmake", ".qml", ".in",
}
SCAN_NAMES = {"CMakeLists.txt"}

SKIP_DIRS = {
    ".git", ".worktrees", ".sandbox", "build", "dist", "node_modules",
    "_deps", "__pycache__", ".cache", "CMakeFiles", ".idea", ".vscode",
}

# 这些文件里的 src/ 字样不是「引用」，或另有归属：
#   docs/board-ledger.json —— 卡片 scope 数据；失效由 check_scope.py --audit 负责
#                             （DX-18 已定「已结卡不回改」，在这里重复报只是噪声）
#   progress.json          —— 派生产物
#   docs/archive/          —— 历史归档，按定义不维护
#   .workbuddy/            —— 本地记忆 / 日志，不进仓库，里面的旧路径是历史记录
SKIP_FILES = {"docs/board-ledger.json", "progress.json"}
SKIP_PREFIXES = ("docs/archive/", ".workbuddy/")

HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@")


def src_files() -> set[str]:
    """仓库里真实存在的 src/ 文件集合（POSIX 相对路径）。"""
    found = set()
    if not SRC_ROOT.is_dir():
        return found
    for p in SRC_ROOT.rglob("*"):
        if p.is_file() and p.suffix.lower() in (".cpp", ".h"):
            found.add(p.relative_to(REPO_ROOT).as_posix())
    return found


def is_scanned(rel: str) -> bool:
    """该路径是否属于「会承载引用」的文本文件。"""
    if not rel:
        return False
    norm = rel.replace("\\", "/")
    parts = set(Path(norm).parts[:-1])
    if parts & SKIP_DIRS:
        return False
    if norm in SKIP_FILES or norm.startswith(SKIP_PREFIXES):
        return False
    path = Path(norm)
    return path.suffix.lower() in SCAN_SUFFIXES or path.name in SCAN_NAMES


def scan_text(text: str) -> list[tuple[int, str]]:
    """逐行找出 src/ 引用，返回 [(行号, 引用)]。行号从 1 开始。"""
    out = []
    for lineno, line in enumerate(text.splitlines(), 1):
        for m in REF_RE.finditer(line):
            out.append((lineno, m.group(1)))
    return out


def iter_text_files():
    """遍历仓库内需要扫描的文本文件（跳过 SKIP_DIRS / SKIP_FILES）。"""
    for path in REPO_ROOT.rglob("*"):
        if not path.is_file():
            continue
        try:
            rel = path.relative_to(REPO_ROOT).as_posix()
        except ValueError:
            continue
        if not is_scanned(rel):
            continue
        yield path, rel


def read_text(path: Path) -> str:
    # 文档里混着各种编码（历史文件有 GBK 残留）：替换非法字节总比整份跳过好，
    # 路径本身是 ASCII，不会因为替换字符而失配。
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def audit(valid: set[str]) -> tuple[int, list[tuple[str, int, str]]]:
    """全量审计。返回 (扫描文件数, 失效列表)。"""
    bad: list[tuple[str, int, str]] = []
    count = 0
    for path, rel in iter_text_files():
        count += 1
        for lineno, ref in scan_text(read_text(path)):
            if ref not in valid:
                bad.append((rel, lineno, ref))
    return count, bad


def added_refs(base: str, valid: set[str]) -> list[tuple[str, int, str]] | None:
    """本次改动新增的行里的失效引用。git 出错返回 None。"""
    proc = subprocess.run(
        ["git", "diff", "--unified=0", "--no-color", "--no-ext-diff",
         "--diff-filter=ACMR", base, "--", "."],
        cwd=str(REPO_ROOT), capture_output=True, text=True,
        encoding="utf-8", errors="replace",
    )
    if proc.returncode != 0:
        sys.stderr.write("[路径引用] git diff 失败（base=%s）：\n%s\n"
                         % (base, proc.stderr.strip()))
        return None

    bad: list[tuple[str, int, str]] = []
    cur = None
    newno = 0
    in_hunk = False
    for line in proc.stdout.splitlines():
        if line.startswith("+++ "):
            p = line[4:].split("\t")[0].strip()
            cur = p[2:] if p.startswith("b/") else p
            in_hunk = False
            continue
        m = HUNK_RE.match(line)
        if m:
            newno = int(m.group(1))
            in_hunk = True
            continue
        if not in_hunk or cur is None:
            continue
        if line.startswith("+"):
            if is_scanned(cur):
                for _, ref in scan_text(line[1:]):
                    if ref not in valid:
                        bad.append((cur, newno, ref))
            newno += 1
        elif line.startswith(" "):
            newno += 1
        # '-' 行只存在于旧文件，不推进新行号
    return bad


def selftest() -> int:
    """无仓库依赖的断言。字符串一律拼接构造 —— 本文件自己也会被扫描。"""
    ok = True

    def check(name: str, cond: bool) -> None:
        nonlocal ok
        print(("  ok   " if cond else "  FAIL ") + name)
        ok = ok and cond

    ref = lambda name: "src/" + name  # noqa: E731 —— 拼接避免源码里出现真实引用

    valid = {ref("core/Database.cpp"), ref("shell/MainWindow.h")}

    def refs(text: str) -> list[str]:
        return [r for _, r in scan_text(text)]

    check("识别正常引用", refs("实现见 " + ref("core/Database.cpp") + " 的构造") == [ref("core/Database.cpp")])
    check("识别失效引用", refs("旧路径 " + ref("Database.cpp") + " 已移走") == [ref("Database.cpp")])
    check("一行多个引用", len(refs(ref("a.cpp") + " 与 " + ref("b.h"))) == 2)
    check("忽略 glob", refs("扫描 " + "src/" + "**") == [] and refs("src/" + "*.cpp") == [])
    check("忽略占位符", refs("src/" + "<模块>/X.cpp") == [])
    check("不把 ./src 当引用", refs("./" + ref("a.cpp")) == [])
    check("不截断更长文件名", refs(ref("a.cpp.bak")) == [])
    check("行号正确", scan_text("第一行\n见 " + ref("a.cpp"))[0][0] == 2)
    check("存在性判据", ref("core/Database.cpp") in valid and ref("Database.cpp") not in valid)
    check("非文本后缀不扫", not is_scanned("docs/x.png") and is_scanned("docs/x.md"))
    check("归档不扫", not is_scanned("docs/archive/r6.md"))
    check("本地记忆不扫", not is_scanned(".workbuddy/memory/2026-11-04.md"))
    check("台账不扫（另有所属）", not is_scanned("docs/board-ledger.json"))
    check("CMakeLists 扫", is_scanned("tests/CMakeLists.txt"))

    print("✓ selftest 全过" if ok else "✗ selftest 有失败项")
    return 0 if ok else 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(
        description="校验仓库内对 src/ 文件的硬编码引用是否仍然存在")
    ap.add_argument("--base", default="",
                    help="棘轮模式：只报该 ref 之后新增/修改行里的失效引用")
    ap.add_argument("--audit", action="store_true",
                    help="全量审计（默认行为，显式写出便于阅读）")
    ap.add_argument("--selftest", action="store_true", help="无仓库依赖的断言")
    ap.add_argument("--limit", type=int, default=0, help="最多列出多少条（0=全部）")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    valid = src_files()
    if not valid:
        sys.stderr.write("[路径引用] 在 %s 下没找到 .cpp/.h，检查仓库根是否正确\n" % SRC_ROOT)
        return 2

    if not args.base:
        scanned, bad = audit(valid)
        print("[路径引用] 全量审计：%d 个 src 文件、%d 个文本文件，失效 %d 处"
              % (len(valid), scanned, len(bad)))
        for rel, lineno, ref in (bad[:args.limit] if args.limit else bad):
            print("  %s:%d  %s" % (rel, lineno, ref))
        if args.limit and len(bad) > args.limit:
            print("  …另有 %d 处未列出" % (len(bad) - args.limit))
        if bad:
            print("[路径引用] 拆目录 / 改名后未回改以上引用；确属历史记录请移入 docs/archive/")
            return 1
        return 0

    bad = added_refs(args.base, valid)
    if bad is None:
        return 2
    _, all_bad = audit(valid)
    if bad:
        print("[路径引用] 棘轮（base=%s）：本次新增 %d 处失效引用（存量 %d 处折叠）"
              % (args.base, len(bad), len(all_bad)))
        for rel, lineno, ref in bad:
            print("  %s:%d  %s" % (rel, lineno, ref))
        print("[路径引用] 路径已不存在：要么改对，要么说明它是历史记录")
        return 1
    print("[路径引用] 棘轮（base=%s）：本次新增 0 处失效（存量 %d 处折叠）"
          % (args.base, len(all_bad)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
