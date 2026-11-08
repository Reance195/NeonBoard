#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_toolchain_versions.py —— 工具链版本单点守门（DX-145）。

单点：cmake/toolchain.cmake。本脚本从单点读取版本号，**自身不硬编码版本字面量**，
扫描全仓文本里的该字面量，白名单之外的命中一律退出 1（fail-closed，Z4）。

用法：
  python scripts/check_toolchain_versions.py --check
  python scripts/check_toolchain_versions.py --get NB_QT_VERSION
  python scripts/check_toolchain_versions.py --emit-github-output
  python scripts/check_toolchain_versions.py --selftest

退出码：0 通过 / 1 存在未登记的重复硬编码 / 2 参数或数据错误。
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLCHAIN_REL = "cmake/toolchain.cmake"
SET_RE = re.compile(r'set\(\s*([A-Za-z0-9_]+)\s+"([^"]*)"\s*\)')

# 生成物 / 工作区 / 第三方，一律不扫
SKIP_DIRS = {
    ".git", ".worktrees", "build", "build-local", "build-release", "dist",
    "thirdparty", ".sandbox", "node_modules", "__pycache__", ".qoder",
    ".vs", ".qtc_clangd", ".codebuddy",
}
TEXT_SUFFIXES = {
    ".md", ".py", ".ps1", ".yml", ".yaml", ".cmake", ".txt", ".json",
    ".cpp", ".h", ".hpp", ".qml", ".js", ".iss", ".in", ".qrc", ".ui",
}

# agent-init 生成的沙箱环境脚本（不入库、非仓库源文件），版本字样只是提示语
SKIP_FILES = {".agent-env.ps1"}

# 白名单：路径（文件或前缀）-> 理由。新增项必须在提交里说明理由（acc2）。
ALLOWLIST = {
    TOOLCHAIN_REL: "单点声明，全仓唯一取值来源",
    # 归档 / 历史记录：只记当时的版本，不是构建取值
    "docs/archive/": "历史归档，按定义不维护（AGENTS.md 四）",
    "docs/card-requests/": "历史请求单，描述当时基线",
    "docs/announcement.md": "发布公告，历史记录",
    "docs/plan-hook.md": "故障排查示例，非构建取值",
    "docs/iteration-roadmap.md": "路线图历史验收记录",
    "docs/pitfalls.md": "踩坑台账，历史诊断记录",
    "docs/design/ui-relayout-verify.md": "历史验收记录",
    "docs/charter-appendix.md": "宪章附录等价命令示例；本卡 scope 未覆盖，口径同步另议",
    "docs/agent-memory-review-20261105.md": "历史复盘记录",
    "SECURITY.md": "依赖声明快照，非构建取值",
    "progress.json": "派生快照，版本字样来自历史验收项",
    "docs/board-ledger.json": "台账验收项与证据文本，历史记录",
    # 运行时示例 / 兜底（本卡 scope 外，非构建工具链取值）
    "scripts/agent-init.ps1": "开区提示语里的示例路径，非实际取值",
    "scripts/dev-run.ps1": "错误提示里的示例路径，非实际取值",
    "scripts/check_ninja_deps.py": "测试 fixture 的 Qt include 路径，非工具链配置",
    "src/board/PreviewHub.cpp": "运行时 Qt prefix 兜底值，非构建工具链；scope 未覆盖，另议",
    "src/shell/StartupDefer.h": "注释里的 Qt 版本说明",
}


def load_toolchain(path: Path) -> dict[str, str]:
    if not path.is_file():
        sys.stderr.write("[工具链守门] 找不到单点：%s\n" % path)
        sys.exit(2)
    return {m.group(1): m.group(2)
            for m in SET_RE.finditer(path.read_text(encoding="utf-8"))}


def qt_version(root: Path) -> str:
    values = load_toolchain(root / TOOLCHAIN_REL)
    version = values.get("NB_QT_VERSION")
    if not version:
        sys.stderr.write("[工具链守门] 单点缺 NB_QT_VERSION\n")
        sys.exit(2)
    return version


def is_allowlisted(rel: str) -> bool:
    return any(rel == p or rel.startswith(p) for p in ALLOWLIST)


def scan(root: Path, needle: str) -> list[str]:
    hits: list[str] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if path.name in SKIP_FILES:
            continue
        try:
            rel = path.relative_to(root).as_posix()
        except ValueError:
            continue
        if set(Path(rel).parts[:-1]) & SKIP_DIRS:
            continue
        if path.suffix.lower() not in TEXT_SUFFIXES and path.name != "CMakeLists.txt":
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if needle in text:
            hits.append(rel)
    return sorted(hits)


def run_check(root: Path) -> int:
    needle = qt_version(root)
    hits = scan(root, needle)
    bad = [h for h in hits if not is_allowlisted(h)]
    allowed = [h for h in hits if is_allowlisted(h)]
    print("[工具链守门] 版本 %s：命中 %d 个文件（白名单 %d / 违规 %d）"
          % (needle, len(hits), len(allowed), len(bad)))
    for h in bad:
        print("  ✗ 未登记的重复硬编码：%s" % h)
    if bad:
        print("[工具链守门] 把版本收敛到 %s，或按 acc2 在白名单里逐条说明理由" % TOOLCHAIN_REL)
        return 1
    print("[工具链守门] 通过：字面量只出现在单点与已登记的白名单文档")
    return 0


def emit_github_output(root: Path) -> int:
    v = load_toolchain(root / TOOLCHAIN_REL)
    keys = (
        ("qt_version", "NB_QT_VERSION"),
        ("qt_arch", "NB_QT_ARCH"),
        ("qt_modules", "NB_QT_MODULES"),
        ("generator", "NB_CMAKE_GENERATOR_CI"),
    )
    for out, key in keys:
        if not v.get(key):
            sys.stderr.write("[工具链守门] 单点缺 %s\n" % key)
            return 2
        print("%s=%s" % (out, v[key]))
    return 0


def selftest() -> int:
    checks: list[tuple[str, bool]] = []

    def check(name: str, ok: bool) -> None:
        checks.append((name, ok))

    with tempfile.TemporaryDirectory(prefix="nb-toolchain-st-") as tmp:
        root = Path(tmp)
        (root / "cmake").mkdir()
        toolchain = (root / TOOLCHAIN_REL)
        toolchain.write_text(
            'set(NB_QT_VERSION "9.9.9")\n'
            'set(NB_QT_ARCH "win64_msvc2022_64")\n'
            'set(NB_QT_MODULES "qtshadertools")\n'
            'set(NB_CMAKE_GENERATOR_CI "Visual Studio 17 2022")\n',
            encoding="utf-8")
        (root / "docs").mkdir()
        (root / "docs/archive").mkdir()
        (root / "docs/archive/old.md").write_text("Qt 9.9.9 历史记录\n", encoding="utf-8")

        check("从单点读版本", qt_version(root) == "9.9.9")
        check("--get 解析", load_toolchain(toolchain).get("NB_QT_ARCH") == "win64_msvc2022_64")
        check("白名单前缀命中", is_allowlisted("docs/archive/old.md"))
        check("普通路径不豁免", not is_allowlisted("scripts/new.ps1"))
        check("只有单点+白名单时通过", run_check(root) == 0)

        (root / "scripts").mkdir()
        (root / "scripts/new.ps1").write_text("C:\\Qt\\9.9.9\\msvc2022_64\n", encoding="utf-8")
        check("新增未登记硬编码退 1", run_check(root) == 1)

        # --emit-github-output 的输出形态（重定向到 StringIO）
        import io
        from contextlib import redirect_stdout
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = emit_github_output(root)
        out = buf.getvalue()
        check("emit 退出码 0", rc == 0)
        check("emit 含 qt_version=9.9.9", "qt_version=9.9.9" in out)
        check("emit 含 qt_arch", "qt_arch=win64_msvc2022_64" in out)

    failed = [n for n, ok in checks if not ok]
    for n, ok in checks:
        print(("  ok   " if ok else "  FAIL ") + n)
    if failed:
        print("[工具链守门] ✗ selftest 失败 %d 项" % len(failed))
        return 1
    print("[工具链守门] ✓ selftest 通过：%d 项断言" % len(checks))
    return 0


def main() -> int:
    # Windows 管道下 Python stdout 默认走 ANSI 代码页，中文/✓/✗ 会 GBK 崩
    # （check_module_deps.py / register_board_cards.py 同一口径）。
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    ap = argparse.ArgumentParser(description="工具链版本单点守门（DX-145）")
    ap.add_argument("--check", action="store_true", help="扫全仓重复硬编码")
    ap.add_argument("--get", metavar="KEY", help="从单点读一个 set() 值")
    ap.add_argument("--emit-github-output", action="store_true",
                    help="输出 GITHUB_OUTPUT 行，供 CI step 使用")
    ap.add_argument("--selftest", action="store_true", help="纯函数自测")
    ap.add_argument("--root", default=str(ROOT), help="仓库根（自测用）")
    args = ap.parse_args()
    root = Path(args.root)

    if args.selftest:
        return selftest()
    if args.get:
        v = load_toolchain(root / TOOLCHAIN_REL).get(args.get)
        if v is None:
            sys.stderr.write("[工具链守门] 单点没有 %s\n" % args.get)
            return 2
        print(v)
        return 0
    if args.emit_github_output:
        return emit_github_output(root)
    return run_check(root)


if __name__ == "__main__":
    sys.exit(main())
