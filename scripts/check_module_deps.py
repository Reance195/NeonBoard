#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_module_deps.py —— 模块依赖方向与文件行数守卫（棘轮式）。

为什么要有这个文件
------------------
`src/` 目前零分层，任何 `#include` 都能成立，编译器不会反对；`docs/module-split-plan.md`
§3.2 的层序（core L0 → … → plugin L7）只是文档约定，没人守就会在重构期间悄悄长回反向边。
行数同理：`qml/Main.qml` 从 1180 涨回 4260，正是「只做一次性拆分、没有持续约束」的实证
（方案 §2.4）。散文条款靠人记一定漂，所以要有闸门。

为什么是棘轮
------------
接入当天存量必然不干净（`src/` 还没分层、55 个类的归属只在文档里）。全量一开就满屏红的下场
是被 `--no-verify` 绕过，比没有守卫更糟。所以只检查**本次改动涉及的文件**，存量不阻塞；
等 `src/` 真分层（MOD-06）之后，同一个脚本自动升级为全量口径。

规则
----
A. 依赖方向：模块只能依赖**层号更小**的模块。`task`(L1) → `shell`(L6) 是反向，报错；
   `shell`(L6) → `task`(L1) 是合法方向。
   归属来源有两条，两种形态都吃，否则 M2 前/M2 后会各红一次：
     - 分层后：路径即归属（`src/task/TaskModel.cpp` → `task`）；
     - 分层前：查 CLASS_MODULE 映射表（55 个类，逐字来自方案 §3.1）。
B. 行数上限：LINE_LIMITS 里的文件不得超过记录值（先设「不得增加」，只报不改）。

用法
----
    python scripts/check_module_deps.py --base origin/main
    python scripts/check_module_deps.py --selftest

退出码：0 通过 / 1 违规 / 2 参数或数据错误。
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

REPO_ROOT = Path(__file__).resolve().parent.parent

# ---- 层序（方案 §3.2，11 条实测边验证过无环）--------------------------------
MODULE_LAYERS = {
    "core": 0,
    "task": 1,
    "theme": 2,
    "update": 3,
    "agent": 4,
    "board": 5,
    "shell": 6,
    "plugin": 7,
}

# ---- 类 → 模块（方案 §3.1，55 个类全覆盖）-----------------------------------
# 分层前唯一可用的归属来源。MOD-06 把文件搬进子目录后，路径优先，本表退为兜底
# （仍需保留：`src/main.cpp` 之外的散落文件、以及未来可能出现的例外）。
CLASS_MODULE = {
    "core": ["Database", "AppPaths", "SingleInstance", "AppActions", "Metrics",
             "UiSettings", "ScopeMatcher"],
    "task": ["TaskModel", "TaskRunStore", "TaskExec", "TaskExtra", "TaskTree",
             "TaskInputParser", "TaskUndo", "TaskFilterModel", "TaskAudit", "TaskStats"],
    "plugin": ["PluginRuntime", "PluginMarket", "PluginLibrary", "PluginLoader",
               "PluginContext", "PluginPackage", "PluginHost", "PluginEventBus",
               "HookEventQueue"],
    "agent": ["AgentProfile", "WorkflowRunner", "WorkflowPresets", "TriageRunner",
              "AcceptanceRunner", "Scheduler", "LedgerImport", "TaskRunner"],
    "board": ["PreviewHub", "ProjectMap", "RoadmapSync", "BoardSync", "BoardColumns",
              "ProjectStore", "CardDetail", "DataPort"],
    "shell": ["WindowsIntegration", "SplashWindow", "WindowController", "TrayController",
              "DragFeel", "Hotkey", "KeyMove", "ReminderScheduler"],
    "update": ["UpdateManager", "DeltaUpdate", "BsdiffWrapper"],
    "theme": ["ThemeMarket", "ThemePack"],
}

CLASS_TO_MODULE = {name: mod for mod, names in CLASS_MODULE.items() for name in names}

# ---- 行数上限：只许减，不许增（上限取当前实际行数，不冻结历史峰值）--------------
# 方案 §2.2 / §2.4 点名的「拆过又涨回来」受害者：Main.qml 曾涨回 4260 行、
# main.cpp 1910 行。此前把 4260（历史坏状态）设成上限，等于把技术债合法化：
# 只要不碰这两个文件，守卫永远绿，check_limits 每次都白提示「可一并调低」。
#
# 口径：上限 = 当前实际行数。任何一轮拆分把文件降下来后，必须同步把这些数字
# 调低（脚本在 actual < limit 时会点名提示），否则「只许减不许增」失去意义。
# SettingsPanel.qml 是同一类巨型 QML 组件，一并纳入。
LINE_LIMITS = {
    "src/main.cpp": 1910,
    "qml/Main.qml": 4131,
    "qml/components/SettingsPanel.qml": 2072,
}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
SOURCE_SUFFIXES = (".cpp", ".h")


def module_of(rel_path: str) -> str | None:
    """文件 → 模块。路径优先（分层后），映射表兜底（分层前）。"""
    parts = PurePosixPath(rel_path).parts
    if len(parts) >= 3 and parts[0] == "src" and parts[1] in MODULE_LAYERS:
        return parts[1]
    return CLASS_TO_MODULE.get(PurePosixPath(rel_path).stem)


def is_reverse(from_module: str, to_module: str) -> bool:
    """依赖方向判定（纯函数，便于自测）。同模块永远合法。"""
    if from_module == to_module:
        return False
    return MODULE_LAYERS[to_module] > MODULE_LAYERS[from_module]


def scan_includes(path: Path) -> list[tuple[int, str]]:
    """只取引号形式的本项目头文件；<Qt/...> 与 thirdparty 一律不参与判定。"""
    out: list[tuple[int, str]] = []
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return out
    for lineno, line in enumerate(text.splitlines(), 1):
        m = INCLUDE_RE.match(line)
        if m:
            out.append((lineno, m.group(1)))
    return out


def in_scope(rel_path: str) -> bool:
    """只判定 src/ 顶层的源文件；thirdparty/ resources/ host/ 不在拆分范围（方案 §3.1）。"""
    parts = PurePosixPath(rel_path).parts
    if not parts or parts[0] != "src":
        return False
    if len(parts) >= 2 and parts[1] in ("host", "thirdparty", "resources"):
        return False
    return PurePosixPath(rel_path).suffix in SOURCE_SUFFIXES


def git_lines(args: list[str]) -> list[str] | None:
    proc = subprocess.run(["git"] + args, cwd=REPO_ROOT,
                          capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    if proc.returncode != 0:
        return None
    return [line.strip() for line in proc.stdout.splitlines() if line.strip()]


def changed_files(base: str) -> list[str] | None:
    """本次改动 = 分支相对基线引入的 + 工作区尚未提交的。

    两者都要：验收要求「临时加一条反向 include 就该红」，那种改动通常还没提交，
    只看 `base...HEAD` 会漏。不能只取后者 —— PR 场景下 base 已前进，`git diff base`
    会把别人的改动算进来。
    """
    committed = git_lines(["diff", "--name-only", "--diff-filter=ACMR", f"{base}...HEAD"])
    if committed is None:
        committed = git_lines(["diff", "--name-only", "--diff-filter=ACMR", base, "HEAD"])
    if committed is None:
        return None
    dirty = git_lines(["diff", "--name-only", "--diff-filter=ACMR"]) or []
    # 新增文件在本地还是 untracked，git diff 看不见 —— 不补这一条，
    # 「新写的文件带反向依赖」在提交前完全无声，只有 CI 才会红。
    others = git_lines(["ls-files", "--others", "--exclude-standard"]) or []
    return sorted(set(committed) | set(dirty) | set(others))


def check_deps(files: list[str]) -> tuple[list[str], int, int]:
    """返回 (违规描述, 判定过的 include 边数, 无法归属而跳过的文件数)。"""
    problems: list[str] = []
    edges = 0
    skipped = 0
    for rel in files:
        if not in_scope(rel):
            continue
        me = module_of(rel)
        if not me:
            skipped += 1
            continue
        for lineno, target in scan_includes(REPO_ROOT / rel):
            dep = CLASS_TO_MODULE.get(PurePosixPath(target).stem)
            if not dep:
                continue
            edges += 1
            if is_reverse(me, dep):
                problems.append(
                    "%s:%d  反向依赖 %s(L%d) → %s(L%d)：#include \"%s\""
                    % (rel, lineno, me, MODULE_LAYERS[me], dep, MODULE_LAYERS[dep], target))
    return problems, edges, skipped


def check_limits() -> tuple[list[str], list[str]]:
    """返回 (超限项, 可下调提示)。两个文件都要读，不看是否在 diff 里。"""
    over: list[str] = []
    hints: list[str] = []
    for rel, limit in sorted(LINE_LIMITS.items()):
        path = REPO_ROOT / rel
        if not path.exists():
            continue
        actual = len(path.read_text(encoding="utf-8", errors="replace").splitlines())
        if actual > limit:
            over.append("%s 已 %d 行，超过上限 %d（+%d）" % (rel, actual, limit, actual - limit))
        elif actual < limit:
            hints.append("%s 已降到 %d 行，可把 LINE_LIMITS 的 %d 一并调低"
                         % (rel, actual, limit))
    return over, hints


def selftest() -> int:
    cases: list[tuple[str, bool]] = []

    def check(name: str, cond: bool) -> None:
        cases.append((name, cond))

    # 归属：分层前靠映射表，分层后靠路径
    check("分层前 src/TaskModel.cpp → task", module_of("src/TaskModel.cpp") == "task")
    check("分层后 src/task/TaskModel.cpp → task", module_of("src/task/TaskModel.cpp") == "task")
    check("路径优先于映射表", module_of("src/board/TaskModel.cpp") == "board")
    check("main.cpp 无归属", module_of("src/main.cpp") is None)
    check("thirdparty 不在判定范围", not in_scope("src/thirdparty/x.cpp"))

    # 方向：只认「层号更大」为反向
    check("task→shell 反向", is_reverse("task", "shell"))
    check("shell→task 合法", not is_reverse("shell", "task"))
    check("task→core 合法", not is_reverse("task", "core"))
    check("core→task 反向", is_reverse("core", "task"))
    check("同模块合法", not is_reverse("task", "task"))
    check("agent→board 反向", is_reverse("agent", "board"))

    # include 解析：只吃引号形式
    incs = scan_includes(Path(__file__))
    check("自扫描不把 <...> 当项目头文件",
          all(not t.startswith("<") for _, t in incs))

    failed = [name for name, ok in cases if not ok]
    for name, ok in cases:
        print(("  ✓ " if ok else "  ✗ ") + name)
    if failed:
        print("✗ selftest 失败 %d 项" % len(failed))
        return 1
    print("✓ selftest 通过：%d 项断言语义正确" % len(cases))
    return 0


def main() -> int:
    # Windows 管道下 Python stdout 默认走 ANSI 代码页，✓/✗ 与中文会 GBK 崩（check_scope 踩过）
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    ap = argparse.ArgumentParser(description="模块依赖方向与行数守卫（棘轮）")
    ap.add_argument("--base", default="origin/main", help="比较基线，默认 origin/main")
    ap.add_argument("--selftest", action="store_true", help="只跑纯函数自测")
    ap.add_argument("-v", "--verbose", action="store_true", help="列出被判定/跳过的文件")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    files = changed_files(args.base)
    if files is None:
        print("✗ 取不到改动清单（基线 %s 无效？先 git fetch origin）" % args.base, file=sys.stderr)
        return 2

    dep_problems, edges, skipped = check_deps(files)
    limit_problems, hints = check_limits()

    if args.verbose:
        print("判定 %d 个改动文件：" % len(files))
        for f in files:
            mod = module_of(f) if in_scope(f) else "-"
            print("    %-45s %s" % (f, mod or ("<未归属>" if in_scope(f) else "<不在范围>")))

    for hint in hints:
        print("· " + hint, file=sys.stderr)

    problems = dep_problems + limit_problems
    if problems:
        print("✗ 模块守卫未通过：", file=sys.stderr)
        for p in problems:
            print("    " + p, file=sys.stderr)
        print("\n依赖层序（方案 §3.2，只能使用层号更小的模块）：", file=sys.stderr)
        for mod in sorted(MODULE_LAYERS, key=lambda m: MODULE_LAYERS[m]):
            print("    L%d %s" % (MODULE_LAYERS[mod], mod), file=sys.stderr)
        print("\n如果这是有意的新增依赖：先用文档/任务卡说明理由，再改本脚本的层序表 —— "
              "不要直接加 --no-verify 绕过。", file=sys.stderr)
        return 1

    src_files = sum(1 for f in files if in_scope(f) and module_of(f))
    print("✓ 模块守卫通过：%d 个改动文件（src/ 下可归属 %d 个、跳过 %d 个），"
          "判定 %d 条模块边；行数上限 %d 个文件均未超"
          % (len(files), src_files, skipped, edges, len(LINE_LIMITS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
