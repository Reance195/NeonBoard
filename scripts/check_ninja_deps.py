#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_ninja_deps.py —— DX-54 的守卫：Ninja 的依赖条目必须真的在跟踪头文件。

为什么要有这个脚本
------------------
Ninja 靠编译器吐出的 `/showIncludes` 行建立头文件依赖。**前缀对不上就一个依赖都解析不出来**：
每个 obj 的 `#deps` 都是 0 —— 改公共头文件不重编 → 陈旧 obj 被链进新代码 → 类布局
不一致 → 堆损坏（崩溃点还误导性地报在 `QImage::~QImage`）。

这条链上最难防的是「**静默**」：编译器不报错、Ninja 不报错、构建照样成功，
只有改头文件不重编时才在几天后以堆损坏的形式冒出来。DX-52 修好了前缀的**测量**，
DX-54 发现那份测量本身会被 sccache 的缓存污染（探针回放别的工作区的路径），
于是又静默退回 CMake 自探测 —— 实测 418/418 个 obj 的 `#deps` 全是 0。

所以这里不查「声明的样子」（那由 scripts/check_utf8_flag.py 负责），只查
**实际发生的事**：跑一次 `ninja -t deps`，看每个编译单元的 `#deps` 是不是真的非空，
以及它们指向的头文件是不是都属于**当前这棵树**。

两层判定
--------
1. 依赖条目非空：每个 .obj 的 #deps 必须 > 0。全空 = 前缀没对上 = 陈旧 obj 的温床。
2. 依赖条目不越界：依赖路径不得指向**别的 worktree**。sccache 的缓存是按内容寻址、
   跨工作区共享的，命中时回放的是当初那棵树的输出 —— Ninja 会把那些路径当依赖，
   改本地头文件就不重编，那个工作区被回收后构建还会直接报「missing and no known
   rule to make it」。DX-52 用「按源码树分桶的强制包含头」解决，本文只负责**发现**它没生效。

用法
----
    python scripts/check_ninja_deps.py --build-dir build
    python scripts/check_ninja_deps.py --build-dir build --ninja <ninja.exe>
    python scripts/check_ninja_deps.py --selftest

退出码：0 通过 / 1 不通过 / 2 用法或环境错误
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

REPO_ROOT = Path(__file__).resolve().parents[1]

# `ninja -t deps` 的块头：`<输出>: #deps <数量>, deps <方式> [mtime <值>] [(VALID)]`
DEPS_HEADER = re.compile(r"^(.*): #deps (\d+), deps (\w+)")


def parse_deps(text):
    """解析 `ninja -t deps` 的输出。

    返回 [(输出, #deps 数量, 依赖方式, [依赖路径...])]。
    """
    blocks = []
    current = None
    for raw in text.split("\n"):
        m = DEPS_HEADER.match(raw)
        if m:
            current = [m.group(1).strip(), int(m.group(2)), m.group(3), []]
            blocks.append(current)
            continue
        if current is None:
            continue
        if raw[:1] in (" ", "\t"):
            path = raw.strip()
            if path:
                current[3].append(path)
        else:
            current = None
    return blocks


def is_foreign(path, build_dir, repo_root):
    """这条依赖是否指向**别的 worktree**。

    只有「路径里带 .worktrees/ 且不在当前仓库根之下」才算越界：Qt 与 MSVC 的头文件
    在仓库之外是正常的，不能一并当成越界。
    """
    norm = path.replace("\\", "/")
    if ".worktrees/" not in norm.lower():
        return False
    if os.path.isabs(norm):
        full = os.path.normpath(norm)
    else:
        full = os.path.normpath(os.path.join(str(build_dir), norm))
    full = full.replace("\\", "/")
    root = str(repo_root).replace("\\", "/").rstrip("/").lower()
    return not full.lower().startswith(root + "/") and full.lower() != root


def check_blocks(blocks, build_dir, repo_root):
    """判定。返回 (问题列表, 编译单元数, 依赖条目总数)。"""
    problems = []
    objs = [b for b in blocks if b[0].endswith(".obj")]
    if not objs:
        problems.append(
            "没找到任何 .obj 的依赖条目：构建目录 %s 里没有 Ninja 依赖数据"
            "（还没构建过？或用的不是 Ninja 生成器？）" % build_dir
        )
        return problems, 0, 0

    empty = [b[0] for b in objs if b[1] == 0]
    if empty:
        problems.append(
            "有 %d / %d 个编译单元的依赖条目是空的（#deps 0）："
            "Ninja 没解析出任何头文件依赖 —— 改公共头文件不会重编，"
            "陈旧 obj 会被链进新代码（类布局不一致 → 堆损坏）。"
            "根因通常是 /showIncludes 前缀与实际编译链路对不上，"
            "见 docs/pitfalls.md §3.16 与 §3.20。" % (len(empty), len(objs))
        )
        for name in empty[:10]:
            problems.append("    ✗ %s" % name)
        if len(empty) > 10:
            problems.append("    … 另有 %d 项" % (len(empty) - 10))

    foreign = []
    for out, count, _mode, deps in objs:
        for dep in deps:
            if is_foreign(dep, build_dir, repo_root):
                foreign.append((out, dep))
    if foreign:
        problems.append(
            "有 %d 条依赖指向**别的 worktree**：sccache 命中缓存时回放的是当初那棵树的"
            "/showIncludes 输出，Ninja 把那些路径当成了依赖 —— 改本地头文件不重编，"
            "那个工作区被回收后构建还会直接报错。见 docs/pitfalls.md §3.16。" % len(foreign)
        )
        for out, dep in foreign[:10]:
            problems.append("    ✗ %s → %s" % (out, dep))
        if len(foreign) > 10:
            problems.append("    … 另有 %d 项" % (len(foreign) - 10))

    return problems, len(objs), sum(b[1] for b in objs)


def find_ninja(explicit=""):
    """找 ninja：显式指定 → PATH → 常见安装位置。"""
    if explicit:
        return explicit
    from shutil import which

    found = which("ninja")
    if found:
        return found
    candidates = [
        os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "DevTools", "ninja.exe"),
        os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet", "Links", "ninja.exe"),
        r"C:\ProgramData\chocolatey\bin\ninja.exe",
    ]
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    return ""


def run_ninja_deps(ninja, build_dir):
    proc = subprocess.run(
        [ninja, "-C", str(build_dir), "-t", "deps"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return proc.stdout.decode("utf-8", errors="replace")


def cmd_check(args) -> int:
    build_dir = Path(args.build_dir).resolve()
    if not build_dir.is_dir():
        sys.stderr.write("构建目录不存在：%s\n" % build_dir)
        return 2
    ninja = find_ninja(args.ninja)
    if not ninja:
        sys.stderr.write(
            "找不到 ninja：请用 --ninja <路径> 指定，或把 ninja 放进 PATH。"
            "（本脚本只对 Ninja 生成器有意义 —— MSBuild 自己扫依赖。）\n"
        )
        return 2

    text = run_ninja_deps(ninja, build_dir)
    blocks = parse_deps(text)
    repo_root = Path(args.repo_root).resolve()
    problems, objs, deps = check_blocks(blocks, build_dir, repo_root)

    if problems:
        for p in problems:
            sys.stderr.write(p + "\n")
        return 1
    print(
        "[依赖守卫] 通过：%d 个编译单元，%d 条依赖条目，无空条目、无跨 worktree 越界"
        % (objs, deps)
    )
    return 0


# --------------------------------------------------------------------------
# 自测
# --------------------------------------------------------------------------
def _selftest():
    cases = []

    text_ok = (
        "CMakeFiles/NeonBoard.dir/src/main.cpp.obj: #deps 12, deps msvc\n"
        "    C:/Qt/6.8.3/include/QtCore/QObject\n"
        "    E:/repo/src/main.cpp\n"
        "CMakeFiles/NeonBoard.dir/src/other.cpp.obj: #deps 3, deps msvc\n"
        "    E:/repo/src/other.cpp\n"
    )
    blocks = parse_deps(text_ok)
    cases.append(("解析：块头与缩进依赖行", len(blocks) == 2 and blocks[0][1] == 12,
                  "%d 块" % len(blocks)))

    problems, objs, deps = check_blocks(blocks, Path("E:/repo/build"), Path("E:/repo"))
    cases.append(("放行：依赖条目非空且不越界", not problems and objs == 2 and deps == 15,
                  "objs=%d deps=%d %s" % (objs, deps, problems[:1])))

    text_empty = (
        "CMakeFiles/NeonBoard.dir/src/main.cpp.obj: #deps 0, deps msvc\n"
        "CMakeFiles/NeonBoard.dir/src/other.cpp.obj: #deps 0, deps msvc\n"
    )
    problems, objs, _deps = check_blocks(parse_deps(text_empty), Path("E:/repo/build"), Path("E:/repo"))
    cases.append(("拒绝：依赖条目全空", bool(problems) and "空的" in problems[0], problems[:1]))

    text_foreign = (
        "CMakeFiles/NeonBoard.dir/src/main.cpp.obj: #deps 2, deps msvc\n"
        "    E:/other/.worktrees/agent-old/src/old.h\n"
        "    E:/repo/src/main.cpp\n"
    )
    problems, _objs, _deps = check_blocks(parse_deps(text_foreign), Path("E:/repo/build"), Path("E:/repo"))
    cases.append(("拒绝：依赖指向别的 worktree",
                  bool(problems) and "worktree" in problems[0], problems[:1]))

    text_own = (
        "CMakeFiles/NeonBoard.dir/src/main.cpp.obj: #deps 2, deps msvc\n"
        "    E:/repo/.worktrees/agent-new/src/main.cpp\n"
    )
    problems, _objs, _deps = check_blocks(parse_deps(text_own), Path("E:/repo/build"), Path("E:/repo"))
    cases.append(("放行：指向当前工作区自己", not problems, problems[:1]))

    problems, objs, _deps = check_blocks([], Path("E:/repo/build"), Path("E:/repo"))
    cases.append(("拒绝：找不到任何编译单元", bool(problems) and objs == 0, problems[:1]))

    return cases


def cmd_selftest() -> int:
    cases = _selftest()
    failed = 0
    for name, ok, detail in cases:
        if ok:
            print("  ✓ %s" % name)
        else:
            failed += 1
            print("  ✗ %s —— %s" % (name, detail))
    if failed:
        print("[依赖守卫自测] 失败 %d / %d 例" % (failed, len(cases)))
        return 1
    print("[依赖守卫自测] 通过 %d / %d 例" % (len(cases), len(cases)))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="DX-54 守卫：Ninja 的依赖条目必须非空且不越界")
    parser.add_argument("--build-dir", default="build", help="构建目录（默认 build）")
    parser.add_argument("--ninja", default="", help="ninja 可执行文件路径（默认在 PATH 里找）")
    parser.add_argument("--repo-root", default=str(REPO_ROOT), help="源码树根目录（判定跨 worktree 越界用）")
    parser.add_argument("--selftest", action="store_true", help="跑可执行自测")
    args = parser.parse_args()

    if args.selftest:
        return cmd_selftest()
    return cmd_check(args)


if __name__ == "__main__":
    sys.exit(main())
