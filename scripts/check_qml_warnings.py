"""启动日志致命告警检查（DX-122）：按形态识别，规则单源。

判据不再枚举已知坏法 —— 枚举式 pattern 漏掉了 R11-13 的
``Cannot anchor to an item that isn't a parent or sibling``，让它连过
R11-08 ~ R11-12 五张卡的验收（现象与教训记 docs/pitfalls.md §4.12）。
改为识别日志行的**形态**，不必预知每一种坏法：

1. 崩溃形态      ``ASSERT:`` / ``FATAL`` / ``Segmentation`` / ``QQmlApplicationEngine failed``
2. JS 异常形态   ``TypeError:`` / ``ReferenceError:`` 等语言级异常前缀（封闭集合）
3. QML 定位形态  ``<file>.qml:<行>[:<列>]: <消息>`` —— QML 引擎只对「有问题」的行打
                 源定位，正常启动日志不出现该形态，零容忍默认全拦

用法::

    python scripts/check_qml_warnings.py <logfile> [--strict] [--base origin/main]
    python scripts/check_qml_warnings.py --selftest

退出码：0 = 通过；1 = 命中致命行（或自测未过）；2 = 用法 / 内部错误
（含棘轮口径下取不到改动清单）。

口径：
- ``--strict``      全量：任何命中都失败（CI 冒烟步骤用，主线必须零容忍）。
- ``--base <ref>``  棘轮：崩溃类必拦；带文件归属的命中只有该文件在本次改动里
                    才拦，存量只告警（本地 verify 用）。改动清单与
                    ``check_module_deps.py`` 同一套语义（base...HEAD + 工作区
                    脏文件 + untracked），不给 ``--base`` 时默认它。

单源断言：致命 pattern 字符串只许出现在本文件（--selftest 里强制扫描
scripts/ 与 .github/），verify.ps1 与 ci.yml 只调用本脚本。
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# 应用日志行首的时间戳（2026-11-07 09:20:24.510 ），可缺省（续行没有）。
_TS_RE = re.compile(r"^\s*(?:\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}[.,]\d+\s+)?")

# QML 定位形态：路径以 file:/// / qrc:/ / 盘符 / 任意斜杠路径开头，以 .qml 结尾，
# 后跟行号（:列号可选）与消息（可选 —— 续行可能只有定位本身）。
_QML_LOC_RE = re.compile(
    r"^(?P<loc>(?:file:///|qrc:/|[A-Za-z]:[\\/]|[^\s]*[\\/])\S*\.qml)"
    r":(?P<ln>\d+)(?::(?P<col>\d+))?(?::\s*(?P<msg>.*))?$"
)

# 崩溃形态：进程级致命信号，与文件归属无关，两种口径下都必拦。
_CRASH_RES = [
    re.compile(r"\bASSERT\s*:"),
    re.compile(r"\bFATAL\b"),
    re.compile(r"Segmentation"),
    re.compile(r"QQmlApplicationEngine failed"),
]

# JS 异常形态：语言规范里的异常类名是封闭集合，不会冒出新词。
_JS_EXC_RE = re.compile(
    r"\b(?:TypeError|ReferenceError|SyntaxError|RangeError|EvalError|URIError)\s*:"
)

# 单源断言清单：旧枚举 pattern 的全部字符串 + 本卡反向证伪的锚定告警。
# 这些字符串只许出现在本文件；verify.ps1 / ci.yml 必须调用本脚本而不是自带一份。
SINGLE_SOURCE_STRINGS = [
    "is not a type",
    "is not a member",
    "TypeError",
    "ReferenceError",
    "Unable to assign",
    "Cannot assign",
    "QQmlApplicationEngine failed",
    "ASSERT",
    "FATAL",
    "Segmentation",
    "Cannot anchor",
]


def classify(line: str) -> list[tuple[str, str | None]]:
    """按形态分类一行日志，返回 [(规则名, 文件归属或 None), ...]。"""
    body = _TS_RE.sub("", line, count=1)
    hits: list[tuple[str, str | None]] = []
    if any(cre.search(body) for cre in _CRASH_RES):
        hits.append(("crash", None))
    m = _QML_LOC_RE.match(body)
    if m:
        hits.append(("qml-location", os.path.basename(m.group("loc"))))
    elif _JS_EXC_RE.search(body):
        hits.append(("js-exception", None))
    return hits


def git_lines(args: list[str]) -> list[str] | None:
    try:
        proc = subprocess.run(["git"] + args, cwd=REPO_ROOT, capture_output=True,
                              text=True, encoding="utf-8", errors="replace")
    except OSError:
        return None
    if proc.returncode != 0:
        return None
    return [line.strip() for line in proc.stdout.splitlines() if line.strip()]


def changed_files(base: str) -> list[str] | None:
    """本次改动 = 分支相对基线引入的 + 工作区尚未提交的 + untracked。

    与 check_module_deps.changed_files 同一套语义（Z5：棘轮只有一把尺）。
    """
    committed = git_lines(["diff", "--name-only", "--diff-filter=ACMR", f"{base}...HEAD"])
    if committed is None:
        committed = git_lines(["diff", "--name-only", "--diff-filter=ACMR", base, "HEAD"])
    if committed is None:
        return None
    dirty = git_lines(["diff", "--name-only", "--diff-filter=ACMR"]) or []
    others = git_lines(["ls-files", "--others", "--exclude-standard"]) or []
    return sorted(set(committed) | set(dirty) | set(others))


def check_lines(lines: list[str], changed: set[str] | None) -> tuple[list, list]:
    """返回 (阻塞命中, 存量告警)。changed=None 表示 strict（无存量通道）。"""
    blocked: list[tuple[int, str, str | None, str]] = []
    stock: list[tuple[int, str, str | None, str]] = []
    for i, line in enumerate(lines, 1):
        for rule, fname in classify(line):
            hit = (i, rule, fname, line.rstrip("\n"))
            if rule == "crash":
                blocked.append(hit)          # 崩溃类与归属无关，恒拦
            elif changed is None:
                blocked.append(hit)          # strict：全拦
            elif fname is None or fname in changed:
                blocked.append(hit)          # 无归属（fail-closed）或属本次改动
            else:
                stock.append(hit)            # 存量：只告警
    return blocked, stock


def report(lines: list[str]) -> None:
    for i, rule, fname, text in lines:
        where = f"{rule}" + (f" {fname}" if fname else "")
        print(f"  第 {i} 行 [{where}] {text}")


def run_log_check(logfile: str, strict: bool, base: str | None) -> int:
    try:
        data = Path(logfile).read_bytes().decode("utf-8", errors="replace")
    except OSError as exc:
        print(f"✗ 读不到日志 {logfile}：{exc}", file=sys.stderr)
        return 2
    lines = data.splitlines()

    changed: set[str] | None = None
    if not strict:
        base = base or "origin/main"
        names = changed_files(base)
        if names is None:
            print(f"✗ 棘轮口径取不到改动清单（基线 {base} 无效？先 git fetch origin）",
                  file=sys.stderr)
            return 2
        changed = {os.path.basename(n) for n in names}

    blocked, stock = check_lines(lines, changed)
    mode = "strict" if strict else f"棘轮 --base {base}"
    if blocked:
        print(f"✗ 命中致命告警 {len(blocked)} 行（口径：{mode}）：")
        report(blocked)
        if stock:
            print(f"  另有存量 {len(stock)} 行不阻塞（文件不在本次改动内）：")
            report(stock)
        return 1
    if stock:
        print(f"✓ 致命告警 0 行（存量 {len(stock)} 行只告警，文件不在本次改动内）：")
        report(stock)
    else:
        print(f"✓ 致命告警 0 行（{len(lines)} 行日志，口径：{mode}）")
    return 0


def _selftest_cases() -> tuple[list[str], list[str]]:
    anchor = ("2026-11-07 09:00:00.000 qrc:/qt/qml/NeonBoard/Main.qml:1703:9: "
              "QML EmptyState: Cannot anchor to an item that isn't a parent or sibling")
    positives = [
        # R11-13 反向证伪：这条连过五张卡验收的告警必须命中
        anchor,
        # 中文本地化同形态（真实日志里就是它）
        "2026-11-07 09:00:00.000 qrc:/qt/qml/NeonBoard/Main.qml:1703:9: "
        "QML EmptyState: 无法锚定到不是父级或同级的项目",
        "qrc:/qt/qml/NeonBoard/X.qml:12:34: Unable to assign [undefined] to QString",
        "file:///C:/repo/qml/Main.qml:5:1: Cannot assign to non-existent property \"foo\"",
        "2026-11-07 09:00:00.000 qrc:/qt/qml/NeonBoard/P.qml:9: "
        "TypeError: Cannot read property 'x' of null",
        # 裸 JS 异常（无 QML 定位前缀）：由异常形态兜底
        "TypeError: Cannot read property 'x' of null",
        "qrc:/qt/qml/NeonBoard/PluginLibraryPanel.qml:493: "
        "ReferenceError: libraryPanel is not defined",
        "ASSERT: \"size >= 0\" in file src/taskmodel.cpp, line 123",
        "FATAL: exception thrown",
        "Segmentation fault (core dumped)",
        "QQmlApplicationEngine failed to load component",
        "qrc:/qt/qml/NeonBoard/Z.qml:3:1: Foo is not a type",
        # 续行形态：无时间戳无消息，只有定位本身
        "qrc:/qt/qml/NeonBoard/ThemePicker.qml:79:9",
    ]
    negatives = [
        "2026-11-07 09:00:00.000 BOOT db 79",
        "TaskModel: loaded 835 tasks",
        "BOARD columns 3 col-todo,col-doing,col-done",
        "2026-11-07 09:00:00.001 PERF startup 812ms fps 120 mem 185MB",
        "EMIT-HOOK cannot read C:\\Users\\x\\AppData\\Local\\Temp\\neonboard-claim.txt",
        # 裸路径无行号：不是 QML 定位形态
        "qrc:/qt/qml/NeonBoard/Main.qml",
        "C:\\repo\\qml\\Main.qml 载入完成",
        "",
    ]
    return positives, negatives


def single_source_violations() -> list[str]:
    """致命 pattern 字符串在 scripts/ 与 .github/ 下只许出现在本文件。"""
    me = Path(__file__).resolve()
    problems: list[str] = []
    for d in ("scripts", ".github"):
        root = REPO_ROOT / d
        if not root.is_dir():
            continue
        for p in sorted(root.rglob("*")):
            if not p.is_file() or p.resolve() == me:
                continue
            try:
                text = p.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for s in SINGLE_SOURCE_STRINGS:
                if s in text:
                    problems.append(f"{p.relative_to(REPO_ROOT)} 含致命 pattern 字符串「{s}」")
    return problems


def selftest() -> int:
    positives, negatives = _selftest_cases()
    failed = False

    # 1) 正例必须命中，且覆盖三类规则
    seen_rules: set[str] = set()
    for line in positives:
        hits = classify(line)
        if not hits:
            print(f"✗ 正例未命中：{line}")
            failed = True
        else:
            seen_rules.update(r for r, _ in hits)
    missing = {"crash", "qml-location", "js-exception"} - seen_rules
    if missing:
        print(f"✗ 正例未覆盖规则：{sorted(missing)}")
        failed = True

    # 2) 反例必须零命中
    for line in negatives:
        if classify(line):
            print(f"✗ 反例误报：{line}")
            failed = True

    # 3) 反向证伪（端到端）：真起子进程喂含 Cannot anchor 的日志，必须非 0 且点名该行
    with tempfile.TemporaryDirectory() as td:
        fixture = Path(td) / "smoke.log"
        fixture.write_text("\n".join([
            "2026-11-07 09:00:00.000 BOOT db 79",
            _selftest_cases()[0][0],
            "2026-11-07 09:00:00.001 PERF startup 812ms",
        ]) + "\n", encoding="utf-8")
        proc = subprocess.run([sys.executable, str(Path(__file__).resolve()),
                               str(fixture), "--strict"],
                              capture_output=True, text=True, encoding="utf-8",
                              errors="replace")
        out = proc.stdout + proc.stderr
        if proc.returncode == 0:
            print("✗ 反向证伪失败：含 Cannot anchor 的日志竟然通过（检查没在工作）")
            failed = True
        elif "Cannot anchor" not in out or "第 2 行" not in out:
            print(f"✗ 反向证伪输出未点名命中行（退出码 {proc.returncode}）：\n{out}")
            failed = True
        else:
            print("✓ 反向证伪：Cannot anchor 命中且点名行号")

    # 4) 单源断言：致命 pattern 字符串只在脚本内出现
    problems = single_source_violations()
    if problems:
        print("✗ 致命 pattern 字符串泄漏到脚本之外（规则必须只有一份）：")
        for p in problems:
            print(f"  {p}")
        failed = True
    else:
        print("✓ 单源断言：致命 pattern 只存在于 check_qml_warnings.py")

    if failed:
        print("selftest 未通过", file=sys.stderr)
        return 1
    print(f"✓ selftest 通过（正例 {len(positives)} / 反例 {len(negatives)}）")
    return 0


def main() -> int:
    # 控制台编码不可预知（DX-60）：命中行可能含中文/特殊字符，只降级不崩溃。
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")

    ap = argparse.ArgumentParser(description="启动日志致命告警检查（按形态识别）")
    ap.add_argument("logfile", nargs="?", help="待检查的日志文件（--smoke 产物）")
    ap.add_argument("--strict", action="store_true", help="全量口径：任何命中都失败")
    ap.add_argument("--base", default=None, help="棘轮口径的基线引用，默认 origin/main")
    ap.add_argument("--selftest", action="store_true", help="内置自测（含反向证伪与单源断言）")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not args.logfile:
        ap.error("需要 <logfile> 或 --selftest")
    return run_log_check(args.logfile, strict=args.strict, base=args.base)


if __name__ == "__main__":
    sys.exit(main())
