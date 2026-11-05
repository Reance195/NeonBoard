#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_utf8_flag.py —— DX-53 的守卫：/utf-8 只许声明一次，且每一个编译单元都得带上。

为什么要有这个脚本
------------------
MSVC 默认按**本地代码页**（中文机器上是 CP936）解析源码里的中文字面量。漏掉 /utf-8
的症状不是报错，而是「断言莫名失败」「中文常量乱码」—— 编译器一声不吭，按错编码读进去
就完了。仓库里曾有中文的测试数据与中文常量，所以这个开关不能靠自觉。

以前 /utf-8 散在四处：顶层 CMakeLists.txt 里三个 target_compile_options
（NeonBoard / NeonBoardHost / neonboard-delta-tool），加 tests/CMakeLists.txt 的
neonboard_add_test 一处。每加一个目标都得记得再写一遍，漏写就静默回退。
DX-53 把它收敛成顶层目录级 add_compile_options —— 但「靠人记的约定迟早会漂」这句话
同样适用于「收敛」这件事本身，所以这里加一道机器守卫，让漂移当场暴露。

两层口径
--------
源码层（默认）
    仓库里只许有**一处** /utf-8 声明；它必须在顶层 CMakeLists.txt、用目录级
    add_compile_options（目标级声明只覆盖那一个目标，新目标就漏了）；并且必须出现在
    所有 add_subdirectory() 之前 —— 目录属性只对**之后**加入的子目录生效，
    声明放晚了，子目录里的目标（整个 tests/）一个都盖不到。

构建层（--scan-build）
    读生成的 build.ninja / *.vcxproj，确认**每一个**编译单元实际拿到的编译选项里
    都带 utf-8。源码层合规但生成物没带上，同样是漏（生成器行为差异、目录属性位置不对、
    目标被覆盖）。这一层测的是「实际发生的事」，源码层测的是「声明的样子」。

用法
----
    python scripts/check_utf8_flag.py                   # 源码层
    python scripts/check_utf8_flag.py --scan-build build # 构建层
    python scripts/check_utf8_flag.py --selftest        # 可执行自测

退出码：0 通过 / 1 不通过 / 2 用法或环境错误
"""

from __future__ import annotations

import argparse
import io
import os
import re
import sys
import tempfile
from pathlib import Path

# 输出统一 UTF-8：Windows 控制台默认 GBK，中文诊断信息会被打成乱码，
# 看不清就等于没有诊断（同 scripts/check_script_hygiene.py 的处理）。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

REPO_ROOT = Path(__file__).resolve().parents[1]

# MSVC 的开关大小写不敏感，/utf-8 与 -utf-8 等价；两种写法都认。
FLAG_RE = re.compile(r"utf-8", re.IGNORECASE)

# 扫描源码层时跳过的目录：构建产物目录（build*）、发布目录、git 元数据、工作区。
SKIP_DIRS = {".git", ".worktrees", ".sandbox", "dist", "node_modules", "plugins-samples"}


# --------------------------------------------------------------------------
# 源码层
# --------------------------------------------------------------------------
def strip_cmake_comment(line: str) -> str:
    """去掉行尾注释。

    CMake 的注释是 `#`（在引号外即生效）。仓库里的 CMake 没用括号注释（`#[[ ]]`），
    所以只处理引号状态就够了：引号内的 `#` 不能当注释起点。
    """
    out = []
    quote = None
    i = 0
    n = len(line)
    while i < n:
        ch = line[i]
        if quote is not None:
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(line[i + 1])
                i += 2
                continue
            if ch == quote:
                quote = None
            i += 1
            continue
        if ch in ('"', "'"):
            quote = ch
            out.append(ch)
            i += 1
            continue
        if ch == "#":
            break
        out.append(ch)
        i += 1
    return "".join(out)


def command_of(code: str):
    """取一行的命令名（`add_compile_options(...)` → `add_compile_options`）。"""
    m = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(", code)
    return m.group(1) if m else None


def collect_cmake_files(root: Path):
    """收集仓库里所有 CMakeLists.txt，返回 {相对路径(正斜杠): [行]}。"""
    files = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS and not d.startswith("build")]
        for name in filenames:
            if name != "CMakeLists.txt":
                continue
            path = Path(dirpath) / name
            rel = path.relative_to(root).as_posix()
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            files[rel] = text.split("\n")
    return files


def check_sources(files):
    """源码层判定。files: {相对路径: [行]}。返回问题列表（空 = 通过）。"""
    problems = []
    sites = []
    for rel in sorted(files):
        for lineno, raw in enumerate(files[rel], 1):
            code = strip_cmake_comment(raw)
            if not FLAG_RE.search(code):
                continue
            sites.append((rel, lineno, code.strip(), command_of(code)))

    if not sites:
        problems.append(
            "仓库里找不到任何 /utf-8 声明：MSVC 会按本地代码页解析源码里的中文，"
            "症状是断言莫名失败 / 中文常量乱码。"
        )
        return problems

    if len(sites) > 1:
        detail = "\n".join("    %s:%d  %s" % (r, n, c) for r, n, c, _ in sites)
        problems.append(
            "/utf-8 声明有 %d 处，应只有 1 处（顶层目录级 add_compile_options）：\n%s"
            % (len(sites), detail)
        )
        return problems

    rel, lineno, code, cmd = sites[0]
    if rel != "CMakeLists.txt":
        problems.append(
            "/utf-8 声明在 %s:%d，必须放在顶层 CMakeLists.txt：目录级声明只对"
            "「本目录 + 此后加入的子目录」生效，放进子目录盖不到主目标。" % (rel, lineno)
        )
    if cmd != "add_compile_options":
        problems.append(
            "/utf-8 声明用的是 %s()，必须是目录级 add_compile_options()："
            "目标级声明只覆盖那一个目标，以后每加一个目标都得记得再写一遍。" % (cmd or "未知命令")
        )
    if rel in files:
        first_sub = None
        for idx, raw in enumerate(files[rel], 1):
            if command_of(strip_cmake_comment(raw)) == "add_subdirectory":
                first_sub = idx
                break
        if first_sub is not None and lineno > first_sub:
            problems.append(
                "/utf-8 声明在 %s:%d，却晚于同文件第 %d 行的 add_subdirectory()："
                "目录属性只对**之后**加入的子目录生效，声明放晚了，子目录里的目标"
                "（整个 tests/）一个都盖不到。" % (rel, lineno, first_sub)
            )
    return problems


def cmd_source(repo_root: Path) -> int:
    files = collect_cmake_files(repo_root)
    problems = check_sources(files)
    if problems:
        for p in problems:
            sys.stderr.write(p + "\n")
        return 1
    print("[utf-8 守卫] 通过：源码里只有一处 /utf-8 声明（顶层目录级 add_compile_options）")
    return 0


# --------------------------------------------------------------------------
# 构建层
# --------------------------------------------------------------------------
def scan_ninja(ninja_path: Path):
    """扫 build.ninja：返回 (总数, 带 utf-8 的数, 缺失清单)。"""
    text = ninja_path.read_text(encoding="utf-8", errors="replace")
    lines = text.split("\n")
    total = 0
    ok = 0
    missing = []
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        if not line.startswith("build "):
            i += 1
            continue
        m = re.match(r"build\s+([^:]+):", line)
        outputs = m.group(1).strip() if m else ""
        j = i + 1
        flags = ""
        while j < n and lines[j].startswith("  "):
            fm = re.match(r"\s+FLAGS\s*=\s*(.*)$", lines[j])
            if fm:
                flags = fm.group(1)
            j += 1
        if outputs.endswith(".obj"):
            total += 1
            if FLAG_RE.search(flags):
                ok += 1
            else:
                missing.append(outputs)
        i = j
    return total, ok, missing


def scan_vcxproj(build_dir: Path):
    """扫 VS 生成器的 *.vcxproj：返回 (总数, 带 utf-8 的数, 缺失清单)。

    VS 生成器与 Ninja 的结构完全不同，要分三层看：

    项目级：<ItemDefinitionGroup> → <ClCompile> → <AdditionalOptions>，
            这是整个项目的编译选项，对该项目的所有源文件生效 —— **它才是判据**。
    每文件项：<ItemGroup> → <ClCompile Include=...> → <AdditionalOptions>，
            覆盖项。MSBuild 里 `%(AdditionalOptions)` 表示「继承项目级的值」，
            所以带这个标记就等于继承了项目级选项（不必自己再写 utf-8）；
            既没有 utf-8 又不继承，才是漏。
    不编译源码的工程（ALL_BUILD / autogen / qmllint / …）没有 ClCompile 项，
        不算编译单元，跳过。
    """
    import xml.etree.ElementTree as ET

    projects = []
    for dirpath, _dirnames, filenames in os.walk(build_dir):
        for name in filenames:
            if name.endswith(".vcxproj"):
                projects.append(Path(dirpath) / name)

    total = 0
    ok = 0
    missing = []
    for proj in sorted(projects):
        rel = proj.relative_to(build_dir).as_posix()
        if "CompilerId" in rel:
            # CMake 自己的编译器探测工程，编的是探测源码，不是本仓库的代码
            continue
        try:
            root = ET.parse(str(proj)).getroot()
        except Exception as exc:  # noqa: BLE001 —— 解析失败也算「没盖住」，要报出来
            missing.append("%s（无法解析：%s）" % (rel, exc))
            continue
        ns = root.tag[1:].split("}")[0] if root.tag.startswith("{") else ""

        def q(tag, _ns=ns):
            return "{%s}%s" % (_ns, tag) if _ns else tag

        file_items = []
        for ig in root.iter(q("ItemGroup")):
            for cl in ig.findall(q("ClCompile")):
                if cl.get("Include"):
                    file_items.append(cl)
        if not file_items:
            # 不编译任何源文件的工程（ALL_BUILD / autogen / qmllint …）
            continue

        project_opts = []
        for idg in root.iter(q("ItemDefinitionGroup")):
            for cl in idg.iter(q("ClCompile")):
                for ao in cl.findall(q("AdditionalOptions")):
                    project_opts.append((ao.text or "").strip())
        if not project_opts:
            missing.append("%s :: 没有项目级 ClCompile/AdditionalOptions" % rel)
            continue
        for opt in project_opts:
            total += 1
            if FLAG_RE.search(opt):
                ok += 1
            else:
                missing.append("%s :: 项目级 AdditionalOptions 缺 utf-8：%s" % (rel, opt[:120]))

        for cl in file_items:
            for ao in cl.findall(q("AdditionalOptions")):
                text = (ao.text or "").strip()
                if FLAG_RE.search(text) or "%(AdditionalOptions)" in text:
                    continue
                missing.append(
                    "%s :: 单文件编译选项既没有 utf-8 也不继承项目级：%s" % (rel, text[:120])
                )
    return total, ok, missing


def cmd_scan_build(build_dir: Path) -> int:
    if not build_dir.is_dir():
        sys.stderr.write("构建目录不存在：%s\n" % build_dir)
        return 2

    ninja = build_dir / "build.ninja"
    if ninja.is_file():
        total, ok, missing = scan_ninja(ninja)
        kind = "Ninja（build.ninja）"
    else:
        total, ok, missing = scan_vcxproj(build_dir)
        kind = "VS 生成器（*.vcxproj）"

    if total == 0 and not missing:
        sys.stderr.write(
            "在 %s 里没找到任何编译单元：构建目录可能不是本仓库的，或还没生成过。\n" % build_dir
        )
        return 2

    if missing:
        for m in missing[:20]:
            sys.stderr.write("  ✗ %s\n" % m)
        if len(missing) > 20:
            sys.stderr.write("  … 另有 %d 项\n" % (len(missing) - 20))
        sys.stderr.write(
            "[utf-8 守卫] 不通过：%s 里 %d 个编译单元中有 %d 个没带 utf-8\n"
            % (kind, total + len(missing), len(missing))
        )
        return 1

    print("[utf-8 守卫] 通过：%s 里 %d 个编译单元全部带 utf-8" % (kind, total))
    return 0


# --------------------------------------------------------------------------
# 自测
# --------------------------------------------------------------------------
def _selftest_sources():
    """源码层用例：每项返回 (用例名, 是否符合预期, 实际结果描述)。"""
    cases = []

    def run(name, files, expect_ok):
        problems = check_sources(files)
        got_ok = not problems
        cases.append(
            (name, got_ok == expect_ok, "通过" if got_ok else problems[0].split("\n")[0])
        )

    good = {
        "CMakeLists.txt": [
            "cmake_minimum_required(VERSION 3.21)",
            "project(NeonBoard)",
            "if (MSVC)",
            "    add_compile_options(/utf-8)",
            "endif()",
            "add_subdirectory(tests)",
        ],
        "tests/CMakeLists.txt": [
            "function(neonboard_add_test target)",
            "    qt_add_executable(${target} ${target}.cpp)",
            "endfunction()",
        ],
    }
    run("合规：顶层目录级声明且早于 add_subdirectory", good, True)

    two = dict(good)
    two["CMakeLists.txt"] = good["CMakeLists.txt"] + [
        "target_compile_options(NeonBoard PRIVATE /utf-8)"
    ]
    run("拒绝：声明散落在两处", two, False)

    per_target = dict(good)
    per_target["CMakeLists.txt"] = [
        "project(NeonBoard)",
        "target_compile_options(NeonBoard PRIVATE /utf-8)",
        "add_subdirectory(tests)",
    ]
    run("拒绝：用了目标级 target_compile_options", per_target, False)

    late = dict(good)
    late["CMakeLists.txt"] = [
        "project(NeonBoard)",
        "add_subdirectory(tests)",
        "if (MSVC)",
        "    add_compile_options(/utf-8)",
        "endif()",
    ]
    run("拒绝：声明晚于 add_subdirectory（子目录继承不到）", late, False)

    in_sub = dict(good)
    in_sub["CMakeLists.txt"] = ["project(NeonBoard)", "add_subdirectory(tests)"]
    in_sub["tests/CMakeLists.txt"] = [
        "if (MSVC)",
        "    add_compile_options(/utf-8)",
        "endif()",
    ]
    run("拒绝：声明写在子目录里（盖不到主目标）", in_sub, False)

    missing = {"CMakeLists.txt": ["project(NeonBoard)", "add_subdirectory(tests)"]}
    run("拒绝：根本没有声明", missing, False)

    commented = dict(good)
    commented["CMakeLists.txt"] = good["CMakeLists.txt"] + [
        "# 以前这里写过 target_compile_options(NeonBoard PRIVATE /utf-8)，已撤掉"
    ]
    run("放行：注释里提到 /utf-8 不算声明", commented, True)

    return cases


def _selftest_build():
    """构建层用例。"""
    cases = []
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)

        good = root / "good"
        good.mkdir()
        (good / "build.ninja").write_text(
            "build a.obj: CXX_COMPILER__x\n"
            "  FLAGS = /O2 /utf-8 -utf-8\n"
            "build b.obj: CXX_COMPILER__x\n"
            "  FLAGS = /O2 /utf-8\n",
            encoding="utf-8",
        )
        total, ok, missing = scan_ninja(good / "build.ninja")
        cases.append(("放行：全部 obj 都带 utf-8", total == 2 and ok == 2 and not missing,
                      "%d/%d" % (ok, total)))

        bad = root / "bad"
        bad.mkdir()
        (bad / "build.ninja").write_text(
            "build a.obj: CXX_COMPILER__x\n"
            "  FLAGS = /O2 /utf-8\n"
            "build b.obj: CXX_COMPILER__x\n"
            "  FLAGS = /O2\n",
            encoding="utf-8",
        )
        total, ok, missing = scan_ninja(bad / "build.ninja")
        cases.append(("拒绝：有 obj 缺 utf-8", total == 2 and ok == 1 and len(missing) == 1,
                      "%d/%d，缺 %s" % (ok, total, missing)))

        empty = root / "empty"
        empty.mkdir()
        (empty / "build.ninja").write_text(
            "build all: phony\n", encoding="utf-8"
        )
        total, ok, missing = scan_ninja(empty / "build.ninja")
        cases.append(("放行：没有 obj 可查", total == 0 and not missing, "%d 个" % total))

        # VS 生成器口径（项目级 / 单文件项 / 不编译源码的工程）
        vs = root / "vs"
        vs.mkdir()
        (vs / "Good.vcxproj").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
            "  <ItemDefinitionGroup>\n"
            "    <ClCompile>\n"
            "      <AdditionalOptions>%(AdditionalOptions) /utf-8</AdditionalOptions>\n"
            "    </ClCompile>\n"
            "  </ItemDefinitionGroup>\n"
            "  <ItemGroup>\n"
            '    <ClCompile Include="a.cpp">\n'
            "      <AdditionalOptions>%(AdditionalOptions) /bigobj</AdditionalOptions>\n"
            "    </ClCompile>\n"
            "  </ItemGroup>\n"
            "</Project>\n",
            encoding="utf-8",
        )
        (vs / "NoUtf8.vcxproj").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
            "  <ItemDefinitionGroup>\n"
            "    <ClCompile>\n"
            "      <AdditionalOptions>%(AdditionalOptions) /O2</AdditionalOptions>\n"
            "    </ClCompile>\n"
            "  </ItemDefinitionGroup>\n"
            "  <ItemGroup>\n"
            '    <ClCompile Include="a.cpp" />\n'
            "  </ItemGroup>\n"
            "</Project>\n",
            encoding="utf-8",
        )
        (vs / "NoInherit.vcxproj").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
            "  <ItemDefinitionGroup>\n"
            "    <ClCompile>\n"
            "      <AdditionalOptions>%(AdditionalOptions) /utf-8</AdditionalOptions>\n"
            "    </ClCompile>\n"
            "  </ItemDefinitionGroup>\n"
            "  <ItemGroup>\n"
            '    <ClCompile Include="a.cpp">\n'
            "      <AdditionalOptions>/bigobj</AdditionalOptions>\n"
            "    </ClCompile>\n"
            "  </ItemGroup>\n"
            "</Project>\n",
            encoding="utf-8",
        )
        (vs / "Utility.vcxproj").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">\n'
            "  <ItemDefinitionGroup>\n"
            "    <ClCompile>\n"
            "      <AdditionalOptions>%(AdditionalOptions) /O2</AdditionalOptions>\n"
            "    </ClCompile>\n"
            "  </ItemDefinitionGroup>\n"
            "</Project>\n",
            encoding="utf-8",
        )
        total, ok, missing = scan_vcxproj(vs)
        cases.append(
            (
                "拒绝：vcxproj 项目级缺 utf-8 / 单文件项不继承",
                total == 3 and ok == 2 and len(missing) == 2,
                "%d/%d，缺 %d 项" % (ok, total, len(missing)),
            )
        )

    return cases


def cmd_selftest() -> int:
    cases = _selftest_sources() + _selftest_build()
    failed = 0
    for name, ok, detail in cases:
        if ok:
            print("  ✓ %s" % name)
        else:
            failed += 1
            print("  ✗ %s —— %s" % (name, detail))
    if failed:
        print("[utf-8 守卫自测] 失败 %d / %d 例" % (failed, len(cases)))
        return 1
    print("[utf-8 守卫自测] 通过 %d / %d 例" % (len(cases), len(cases)))
    return 0


# --------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(description="DX-53 守卫：/utf-8 只许声明一次且覆盖每个编译单元")
    parser.add_argument("--scan-build", metavar="DIR", help="构建层：扫生成物里的编译单元")
    parser.add_argument("--selftest", action="store_true", help="跑可执行自测")
    parser.add_argument("--repo", default=str(REPO_ROOT), help="仓库根目录（默认按脚本自身位置推）")
    args = parser.parse_args()

    if args.selftest:
        return cmd_selftest()
    if args.scan_build:
        return cmd_scan_build(Path(args.scan_build))
    return cmd_source(Path(args.repo))


if __name__ == "__main__":
    sys.exit(main())
