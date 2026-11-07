#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_qml_pitfalls.py —— QML 两处已踩坑陷阱的静态判据（DX-102）。

为什么是静态判据
----------------
这两处陷阱都靠运行期才炸，而且症状离现场很远（DX-102 立卡证据）：

A. id 点号访问：QML 的 ``id`` 不是父项属性。``sweepBand.sweepAnimation.restart()``
   里两段都是 id —— 第二段恒为 undefined，调用即抛 JS 类型错误，把卡片永久锁在
   busy。编译器不报错，只有点下去才发现完成按钮失效。
B. 同层全表面 MouseArea 吞点击：内容层与 ``anchors.fill`` 的 MouseArea 是兄弟、
   z 相同、MouseArea 声明在后（后绘制者在上），按钮 press 全被它吃掉。表现是
   「按钮看得见、点不动」，同样没有任何编译期信号。

两条都能从源码结构判定，所以沉淀成脚本而不是再靠临时 qmltestrunner 复现。

用法
----
    python scripts/check_qml_pitfalls.py                 # 扫仓库 qml/
    python scripts/check_qml_pitfalls.py qml plugins     # 指定目录/文件
    python scripts/check_qml_pitfalls.py --selftest      # 纯函数自测，不扫仓库

退出码：0 无发现 / 1 有发现或自测失败 / 2 用法错误。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

ID_RE = re.compile(r"\bid\s*:\s*([A-Za-z_]\w*)")
DOTTED_RE = re.compile(r"\b([A-Za-z_]\w*)\.([A-Za-z_]\w*)")
OBJECT_RE = re.compile(r"^(\s*)([A-Z][A-Za-z0-9_.]*)\s*\{\s*$")
MOUSEAREA_RE = re.compile(r"^(\s*)MouseArea\s*\{\s*$")
Z_RE = re.compile(r"^\s*z\s*:\s*(-?\d+)\s*$")
Z_ANY_RE = re.compile(r"^\s*z\s*:")
# DX-137：C++ 门面返回 QString，进 QML 后是 JS 原始字符串 —— 没有 isEmpty() 方法，
# 调用必抛语言级异常（SettingsPanel 五处踩过）。判空一律 === "" 或 .length === 0。
# （致命形态的词面归 check_qml_warnings 单源所有，这里不复述它的 pattern 字符串。）
JS_IS_EMPTY_RE = re.compile(r"\.isEmpty\(\s*\)")


def strip_qml_noise(text: str) -> list[str]:
    """把注释与字符串替换成空格，保留行号与结构（不改变行数）。"""
    out: list[str] = []
    state = "code"
    quote = ""
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if ch == "/" and nxt == "/":
                state = "line"
                out.append("  ")
                i += 2
                continue
            if ch == "/" and nxt == "*":
                state = "block"
                out.append("  ")
                i += 2
                continue
            if ch in "\"'`":
                state = "string"
                quote = ch
                out.append(" ")
                i += 1
                continue
            out.append(ch)
            i += 1
            continue
        if state == "line":
            if ch == "\n":
                state = "code"
                out.append("\n")
            else:
                out.append(" ")
            i += 1
            continue
        if state == "block":
            if ch == "*" and nxt == "/":
                state = "code"
                out.append("  ")
                i += 2
                continue
            out.append("\n" if ch == "\n" else " ")
            i += 1
            continue
        if ch == "\\" and i + 1 < n:
            out.append("  ")
            i += 2
            continue
        if ch == quote:
            state = "code"
            out.append(" ")
            i += 1
            continue
        out.append("\n" if ch == "\n" else " ")
        i += 1
    return "".join(out).splitlines()


def collect_ids(lines: list[str]) -> set[str]:
    ids: set[str] = set()
    for line in lines:
        m = ID_RE.search(line)
        if m:
            ids.add(m.group(1))
    return ids


def block_end(lines: list[str], start: int) -> int:
    """从 start 行（含 ``{``）起按花括号配对找块尾行号；single-line 块返回同号。"""
    depth = 0
    for idx in range(start, len(lines)):
        depth += lines[idx].count("{") - lines[idx].count("}")
        if depth <= 0 and idx >= start:
            return idx
    return len(lines) - 1


def direct_z(lines: list[str], start: int, end: int, indent: int) -> int | None:
    """块内直接层级的 z 字面量；默认 0，表达式返回 None（未知，不判）。"""
    for idx in range(start, end + 1):
        if len(lines[idx]) - len(lines[idx].lstrip()) != indent:
            continue
        text = lines[idx].strip()
        if not Z_ANY_RE.match(text):
            continue
        m = Z_RE.match(text)
        return int(m.group(1)) if m else None
    return 0


def find_id_dotted_access(lines: list[str]) -> list[tuple[int, str]]:
    ids = collect_ids(lines)
    hits: list[tuple[int, str]] = []
    for idx, line in enumerate(lines):
        for m in DOTTED_RE.finditer(line):
            first, second = m.group(1), m.group(2)
            if first in ids and second in ids:
                hits.append((idx + 1, m.group(0)))
                break
    return hits


def find_js_is_empty(lines: list[str]) -> list[tuple[int, str]]:
    hits: list[tuple[int, str]] = []
    for idx, line in enumerate(lines):
        m = JS_IS_EMPTY_RE.search(line)
        if m:
            hits.append((idx + 1, m.group(0)))
    return hits


def find_overlay_mouseareas(lines: list[str]) -> list[tuple[int, str]]:
    """找「全表面 MouseArea 压在含交互内容的前序兄弟层上且 z 相同」的写法。"""
    hits: list[tuple[int, str]] = []
    for idx, line in enumerate(lines):
        m = MOUSEAREA_RE.match(line)
        if not m:
            continue
        indent = len(m.group(1))
        end = block_end(lines, idx)
        prop_indent = indent + 4
        own = [ln for ln in lines[idx:end + 1]
               if len(ln) - len(ln.lstrip()) == prop_indent]
        if not any(re.search(r"anchors\.fill\s*:", ln) for ln in own):
            continue
        if any(re.search(r"acceptedButtons\s*:\s*Qt\.NoButton", ln) for ln in own):
            continue
        if any(re.search(r"enabled\s*:\s*false", ln) for ln in own):
            continue
        mouse_z = direct_z(lines, idx, end, prop_indent)
        if mouse_z is None:
            continue
        parent_start = 0
        for back in range(idx - 1, -1, -1):
            p = OBJECT_RE.match(lines[back])
            if p and len(p.group(1)) < indent and block_end(lines, back) >= idx:
                parent_start = back
                break
        for sib in range(parent_start, idx):
            s = OBJECT_RE.match(lines[sib])
            if not s or len(s.group(1)) != indent:
                continue
            sib_end = block_end(lines, sib)
            if sib_end >= idx:
                continue
            if s.group(2) == "MouseArea":
                continue
            sib_block = lines[sib:sib_end + 1]
            if not any(re.search(r"\bMouseArea\s*\{", ln) for ln in sib_block):
                continue
            sib_z = direct_z(lines, sib, sib_end, indent + 4)
            if sib_z is None or sib_z != mouse_z:
                continue
            hits.append((
                idx + 1,
                "全表面 MouseArea 与含交互内容的前序兄弟同 z（%d）：后声明者在上面，"
                "按钮 press 会被吞；给内容层显式抬 z" % mouse_z,
            ))
            break
    return hits


def find_qml_files(paths: list[str]) -> list[Path]:
    files: list[Path] = []
    for raw in paths:
        p = (REPO_ROOT / raw).resolve() if not Path(raw).is_absolute() else Path(raw)
        if p.is_file() and p.suffix == ".qml":
            files.append(p)
        elif p.is_dir():
            files.extend(sorted(p.rglob("*.qml")))
    return files


def scan_file(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = strip_qml_noise(text)
    problems: list[str] = []
    for line, snippet in find_id_dotted_access(lines):
        problems.append("%s:%d: [id-dotted] %s —— id 不是父项属性，第二段恒为 undefined"
                        % (path, line, snippet))
    for line, snippet in find_js_is_empty(lines):
        problems.append("%s:%d: [js-isEmpty] %s —— QString 进 QML 即 JS 字符串，"
                        "没有 isEmpty() 方法（必抛语言级异常）；判空用 === \"\""
                        % (path, line, snippet))
    for line, message in find_overlay_mouseareas(lines):
        problems.append("%s:%d: [mousearea-stacking] %s" % (path, line, message))
    return problems


def selftest() -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    failures: list[str] = []

    def check(name: str, ok: bool) -> None:
        print("  %s %s" % ("✓" if ok else "✗", name))
        if not ok:
            failures.append(name)

    positive_id = """
Item {
    id: sweepBand
    Item { id: sweepAnimation }
    function play() { sweepBand.sweepAnimation.restart() }
}
"""
    negative_id = """
Item {
    id: sweepBand
    Item { id: sweepAnimation }
    function play() { sweepAnimation.restart() }
}
"""
    positive_overlay = """
Rectangle {
    Item {
        id: content
        MouseArea { }
    }
    MouseArea {
        anchors.fill: parent
    }
}
"""
    negative_overlay_z = """
Rectangle {
    Item {
        id: content
        z: 1
        MouseArea { }
    }
    MouseArea {
        anchors.fill: parent
    }
}
"""
    negative_overlay_order = """
Rectangle {
    MouseArea {
        anchors.fill: parent
    }
    Item {
        id: content
        MouseArea { }
    }
}
"""
    check("id 点号访问被扫出且带行号",
          [(n, s) for n, s in find_id_dotted_access(strip_qml_noise(positive_id))]
          == [(5, "sweepBand.sweepAnimation")])
    check("直接 id 访问不误报",
          find_id_dotted_access(strip_qml_noise(negative_id)) == [])
    check("注释与字符串里的同形文本不误报",
          find_id_dotted_access(strip_qml_noise(
              "// sweepBand.sweepAnimation\nItem { id: a; Item { id: b } }\n"
              "property string s: \"a.b\"")) == [])
    check("同 z 的覆盖 MouseArea 被扫出",
          len(find_overlay_mouseareas(strip_qml_noise(positive_overlay))) == 1)
    check("内容层抬 z 后不误报",
          find_overlay_mouseareas(strip_qml_noise(negative_overlay_z)) == [])
    check("MouseArea 声明在内容层之前不误报",
          find_overlay_mouseareas(strip_qml_noise(negative_overlay_order)) == [])
    positive_empty = """
Item {
    function refresh(path, error) {
        hint.text = error.isEmpty() ? "ok" : error
    }
}
"""
    negative_empty = """
Item {
    function refresh(path, error) {
        hint.text = error === "" ? "ok" : error
        label.text = name.length === 0 ? "-" : name
    }
}
"""
    check("JS 字符串上的 isEmpty() 被扫出且带行号",
          [(n, s) for n, s in find_js_is_empty(strip_qml_noise(positive_empty))]
          == [(4, ".isEmpty()")])
    check("判空的等价写法不误报",
          find_js_is_empty(strip_qml_noise(negative_empty)) == [])
    if failures:
        print("✗ 自测失败 %d 项" % len(failures))
        return 1
    print("✓ 自测通过（8 项）")
    return 0


def main(argv: list[str]) -> int:
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    if "--selftest" in argv:
        return selftest()
    paths = [a for a in argv[1:] if not a.startswith("-")]
    if any(a.startswith("-") for a in argv[1:]):
        sys.stderr.write("用法：python scripts/check_qml_pitfalls.py [目录或 .qml 文件 ...] [--selftest]\n")
        return 2
    files = find_qml_files(paths or ["qml"])
    problems: list[str] = []
    for path in files:
        problems.extend(scan_file(path))
    for line in problems:
        print(line)
    if problems:
        print("✗ 发现 %d 处 QML 陷阱" % len(problems))
        return 1
    print("✓ %d 个 QML 文件未发现已知陷阱" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
