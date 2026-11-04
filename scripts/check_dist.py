#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_dist.py — 便携包「该随包走」的清单校验（唯一实现，deploy / CI / release 共用）。

为什么要有这个文件：R14-02 的教训是 `deploy.ps1` 只拷 `plugins-samples`，6 个官方插件
从未进过安装包（开发机读仓库目录所以看不出来）。当时的修法是补一条 Copy-Item 加一段
内联比对，但那段比对只盯着插件 —— exe 缺失、platforms/qwindows.dll 缺失、
sqldrivers/qsqlite.dll 缺失、Qt6Qml.dll 是 0 字节，这几种「装完直接打不开」的故障
构建脚本统统不检查，只有 release.yml 里有半段内联检测（且只覆盖 0 字节）。

三个检查器各写一套判据必然漂。合并成一份之后：deploy 打包后立刻跑、CI 在上传
artifact 前跑、release 在压缩前跑，判据只有一份 —— 而且它**能在任意已存在的 dist
目录上单独复跑**，不必重跑构建。凡「拷文件」环节都必须用产物验收，产物验收的前提
就是检查器能脱离构建过程独立执行。

清单为什么是「最小集」而不是「全量」：windeployqt 的产物集合随 Qt 版本变化，
写死全量清单会天天误报。这里只断言「少了必然起不来」的那些。

用法：
    python scripts/check_dist.py                      # 默认查 <repo>/dist/NeonBoard
    python scripts/check_dist.py --dir <便携包目录>
    python scripts/check_dist.py --selftest           # 不依赖仓库与产物

退出码：0 全过 / 1 有缺失或 0 字节文件 / 2 参数错误或目录不存在。
"""

import argparse
import sys
import tempfile
from pathlib import Path

# ---- 清单（缺一即失败；改这里等于改「什么算可发布产物」）------------------------

# 1. 可执行文件：少了任何一个，装完就是「点了没反应」
REQUIRED_EXES = ("NeonBoard.exe", "NeonBoardHost.exe")

# 2. Qt 运行时 DLL 最小集。刻意不含 Qt6LabsFolderListModel / QuickDialogs2 这类
#    「用到才加载」的模块：它们缺失的故障是某个面板打不开，而非启动即崩，属另一种
#    严重度，混进来会把「装完能启动」这条判据稀释掉。
REQUIRED_QT_DLLS = (
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Qml.dll",
    "Qt6Quick.dll",
    "Qt6QuickControls2.dll",
    "Qt6Network.dll",
    "Qt6Sql.dll",
)

# 3. Qt 平台插件与 SQL 驱动：这两类走的是「运行时 dlopen」，缺了不会有编译或链接错误，
#    只会在用户机器上弹「could not find or load the Qt platform plugin windows」。
REQUIRED_QT_PLUGINS = (
    "platforms/qwindows.dll",
    "sqldrivers/qsqlite.dll",
)

# 4. QML 模块根目录：QML 是运行时按 import 解析的，整个 qml/QtQuick 缺失时
#    窗口都建不起来，而 windeployqt 在 --qmldir 扫描失败时会静默少拷。
REQUIRED_QT_QML_DIRS = ("qml/QtQuick",)

# 5. 两批插件以**磁盘为准**：仓库 plugins/ 与 plugins-samples/ 下每个目录都必须在
#    产物里出现且带 plugin.json。以磁盘为准意味着以后新增插件不用改本文件 ——
#    也就不会出现「忘了同步清单」（R14-02 的原始故障形态）。
PLUGIN_SOURCE_DIRS = ("plugins", "plugins-samples")


def repo_root_of(script: Path) -> Path:
    return script.resolve().parent.parent


def check_dirs(dist: Path, repo: Path) -> list[tuple[bool, str]]:
    """目录类断言：qml 模块根。"""
    out = []
    for rel in REQUIRED_QT_QML_DIRS:
        target = dist / rel
        out.append((target.is_dir(), f"{rel}/（QML 模块根）"))
    return out


def check_files(dist: Path, repo: Path) -> list[tuple[bool, str]]:
    """文件类断言：exe + Qt DLL + Qt 插件。"""
    out = []
    for name in REQUIRED_EXES:
        out.append(((dist / name).is_file(), name))
    for name in REQUIRED_QT_DLLS:
        out.append(((dist / name).is_file(), name))
    for rel in REQUIRED_QT_PLUGINS:
        out.append(((dist / rel).is_file(), rel))
    return out


def check_plugins(dist: Path, repo: Path) -> list[tuple[bool, str]]:
    """插件类断言：两批都以仓库磁盘为真值，逐个比对产物副本。"""
    out = []
    for source in PLUGIN_SOURCE_DIRS:
        src_dir = repo / source
        if not src_dir.is_dir():
            out.append((False, f"{source}/ 在仓库中不存在（清单依赖它作真值）"))
            continue
        names = sorted(p.name for p in src_dir.iterdir() if p.is_dir())
        if not names:
            out.append((False, f"{source}/ 下没有任何插件目录"))
            continue
        for name in names:
            target = dist / "plugins" / name / "plugin.json"
            out.append((target.is_file(), f"plugins/{name}/plugin.json（来自 {source}/）"))
    return out


def check_zero_bytes(dist: Path) -> list[tuple[bool, str]]:
    """0 字节的可执行文件与 DLL：文件存在但内容空，运行时同样是加载失败。

    release.yml 里原本有一段内联逻辑做这件事，收敛到这里以免两处判据漂移。
    windeployqt 偶尔只建文件不写内容（Qt6Qml.dll 踩过）。
    """
    out = []
    emptied = []
    for path in dist.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix.lower() not in (".dll", ".exe"):
            continue
        if path.stat().st_size == 0:
            emptied.append(path.relative_to(dist).as_posix())
    if emptied:
        out.append((False, "0 字节： " + "、".join(sorted(emptied))))
    else:
        out.append((True, "无 0 字节的 dll / exe"))
    return out


def run_checks(dist: Path, repo: Path) -> list[tuple[bool, str]]:
    return (check_files(dist, repo)
            + check_dirs(dist, repo)
            + check_plugins(dist, repo)
            + check_zero_bytes(dist))


def selftest() -> int:
    """不依赖仓库、不依赖产物：临时目录里造一棵最小树，逐项验证判定。"""
    failures = []

    def check(label: str, ok: bool) -> None:
        if ok:
            print(f"  ✓ {label}")
        else:
            failures.append(label)
            print(f"  ✗ {label}")

    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        repo = base / "repo"
        dist = base / "dist"
        (repo / "plugins" / "p-official").mkdir(parents=True)
        (repo / "plugins" / "p-official" / "plugin.json").write_text("{}", encoding="utf-8")
        (repo / "plugins-samples" / "p-sample").mkdir(parents=True)
        (repo / "plugins-samples" / "p-sample" / "plugin.json").write_text("{}", encoding="utf-8")
        dist.mkdir(parents=True)

        for name in REQUIRED_EXES:
            (dist / name).write_bytes(b"MZ")
        for name in REQUIRED_QT_DLLS:
            (dist / name).write_bytes(b"x")
        for rel in REQUIRED_QT_PLUGINS:
            (dist / rel).parent.mkdir(parents=True, exist_ok=True)
            (dist / rel).write_bytes(b"x")
        for rel in REQUIRED_QT_QML_DIRS:
            (dist / rel).mkdir(parents=True)
        for src, name in (("plugins", "p-official"), ("plugins-samples", "p-sample")):
            (dist / "plugins" / name).mkdir(parents=True)
            (dist / "plugins" / name / "plugin.json").write_text("{}", encoding="utf-8")

        check("完整树判为全过", all(ok for ok, _ in run_checks(dist, repo)))

        # 官方插件缺失（R14-02 的原形）
        (dist / "plugins" / "p-official" / "plugin.json").unlink()
        results = run_checks(dist, repo)
        check("缺官方插件能被抓到",
              any(not ok and "p-official" in label for ok, label in results))
        (dist / "plugins" / "p-official" / "plugin.json").write_text("{}", encoding="utf-8")

        # 平台插件缺失（运行时 dlopen 失败，编译期看不见）
        (dist / "platforms" / "qwindows.dll").unlink()
        check("缺 qwindows.dll 能被抓到",
              any(not ok and "qwindows" in label for ok, label in run_checks(dist, repo)))
        (dist / "platforms" / "qwindows.dll").write_bytes(b"x")

        # 0 字节 DLL
        (dist / "Qt6Qml.dll").write_bytes(b"")
        check("0 字节 DLL 能被抓到",
              any(not ok and "0 字节" in label for ok, label in run_checks(dist, repo)))
        (dist / "Qt6Qml.dll").write_bytes(b"x")

        # QML 模块根缺失
        for child in (dist / "qml" / "QtQuick").iterdir():
            child.unlink()
        (dist / "qml" / "QtQuick").rmdir()
        check("缺 qml/QtQuick 能被抓到",
              any(not ok and "qml/QtQuick" in label for ok, label in run_checks(dist, repo)))

    if failures:
        print(f"\n自测失败 {len(failures)} 项")
        return 1
    print("\n自测通过")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="便携包内容清单校验")
    ap.add_argument("--dir", dest="dist", help="便携包目录（默认 <repo>/dist/NeonBoard）")
    ap.add_argument("--repo", help="仓库根（默认脚本所在目录的上级）")
    ap.add_argument("--selftest", action="store_true", help="跑内置断言，不依赖仓库与产物")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    repo = Path(args.repo).resolve() if args.repo else repo_root_of(Path(__file__))
    dist = Path(args.dist).resolve() if args.dist else repo / "dist" / "NeonBoard"

    if not dist.is_dir():
        print(f"✗ 便携包目录不存在：{dist}", file=sys.stderr)
        return 2

    results = run_checks(dist, repo)
    missing = [(ok, label) for ok, label in results if not ok]
    for ok, label in results:
        print(f"  {'✓' if ok else '✗'} {label}")

    print()
    if missing:
        print(f"✗ 便携包缺 {len(missing)} 项（共 {len(results)} 项）：", file=sys.stderr)
        for _, label in missing:
            print(f"    {label}", file=sys.stderr)
        print("  见 docs/pitfalls.md「凡拷文件环节都必须用产物验收」一节。", file=sys.stderr)
        return 1
    print(f"✓ 便携包清单校验通过（{len(results)} 项）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
