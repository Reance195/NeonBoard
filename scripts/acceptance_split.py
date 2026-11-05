#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""acceptance_split.py —— 验收项子项拆分的单一实现（DX-84）。

为什么单独一个文件
------------------
拆分规则要在三处生效：nbflow 立卡时把请求单验收字段拆成数组落台账、
nbflow report 对存量单串卡兼容展开、board-apply 校验拦截未拆分的手工清单。
规则只实现一次（AGENTS.md Z5），nbflow 与 board-apply 都 import 这份；
收口之前同一规则在 split_multi（只认半角 ``;``）与 flatten_acceptance
（只认 ``|``）各活了一份，全角 ``；`` 两侧都不认 —— 同一天台账里
DX-72/DX-74/DX-81 是数组、DX-70/DX-73/DX-82/DX-83 是 ``；`` 单串。

拆分口径
--------
分隔符：半角 ``;``、全角 ``；``、竖线 ``|``（DX-62 遗留形态）。
引号内的分隔符不切（``ctest -R "a|b"`` 这类正则参数、``pwsh -Command "a; b"``）。
拆不出两个以上非空子项时原样返回 —— 单串没有歧义就别动它。
"""

from __future__ import annotations

import sys

# 全量分隔符；split_multi 这类「字段级」拆分用字段子集（scope/depends 里 | 不是分隔符）
SEPARATORS = ";；|"
FIELD_SEPARATORS = ";；"


def split_subitems(text: str, separators: str = SEPARATORS) -> list[str]:
    """按引号外分隔符拆一条串；拆不出两条以上非空子项时原样返回。"""
    pieces: list[str] = []
    buf: list[str] = []
    quoted = ""
    for ch in text or "":
        if quoted:
            if ch == quoted:
                quoted = ""
        elif ch in "\"'":
            quoted = ch
        elif ch in separators:
            pieces.append("".join(buf))
            buf = []
            continue
        buf.append(ch)
    pieces.append("".join(buf))
    # 单条非空子项时 stripped 就是 [text.strip()]，与「原样返回」等价；
    # 纯分隔符串 stripped 为空列表 —— 不能兜底回原串（那会把「；；」原样放行）
    return [p.strip() for p in pieces if p.strip()]


def flatten_entries(entries: list[str], separators: str = SEPARATORS) -> list[str]:
    """逐条拆子项并展平（幂等：拆过的数组再进来不动）。"""
    flat: list[str] = []
    for entry in entries or []:
        flat.extend(split_subitems(entry, separators))
    return flat


def is_unsplit(text: str, separators: str = SEPARATORS) -> bool:
    """校验判据：这条 acceptance 是否还是含引号外分隔符的多子项单串。"""
    return len(split_subitems(text, separators)) > 1


def selftest() -> int:
    # Windows 控制台默认 GBK，打 ✓/✗ 会抛 UnicodeEncodeError（与 board-apply.py 同一写法）
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    failures: list[str] = []

    def check(name: str, ok: bool) -> None:
        print("  %s %s" % ("✓" if ok else "✗", name))
        if not ok:
            failures.append(name)

    check("半角分号拆分", split_subitems("a;b") == ["a", "b"])
    check("全角分号拆分（split_multi 旧漏拆形态）", split_subitems("a；b") == ["a", "b"])
    check("竖线拆分（DX-62 形态）", split_subitems("a|b|c") == ["a", "b", "c"])
    check("三形态混合", split_subitems("a；b;c|d") == ["a", "b", "c", "d"])
    check("引号内分隔符保留", split_subitems('ctest -R "a|b" 通过') == ['ctest -R "a|b" 通过'])
    check("引号内分号保留", split_subitems('pwsh -Command "a; b" 退出 0')
          == ['pwsh -Command "a; b" 退出 0'])
    check("先开引号后无闭引号不崩", split_subitems('a "b|c') == ['a "b|c'])
    check("无分隔符原样返回", split_subitems("单独一条验收") == ["单独一条验收"])
    check("空串返回空", split_subitems("") == [])
    check("纯分隔符返回空", split_subitems("；；") == [])
    check("子项去首尾空白", split_subitems(" a ； b ") == ["a", "b"])
    check("字段级子集不切竖线", split_subitems("a|b", FIELD_SEPARATORS) == ["a|b"])
    check("字段级子集切全角", split_subitems("a；b", FIELD_SEPARATORS) == ["a", "b"])

    check("展平：单串卡拆成子项", flatten_entries(["a；b|c"]) == ["a", "b", "c"])
    check("展平：数组卡幂等不动", flatten_entries(["a", "b"]) == ["a", "b"])
    check("展平：引号子项整体保留",
          flatten_entries(['ctest -R "x|y" 过', "另一条"]) == ['ctest -R "x|y" 过', "另一条"])
    check("展平：空列表", flatten_entries([]) == [])

    check("is_unsplit 正例", is_unsplit("a；b") is True)
    check("is_unsplit 反例（数组元素）", is_unsplit("单独一条验收") is False)
    check("is_unsplit 引号内不算", is_unsplit('pwsh -Command "a; b"') is False)

    if failures:
        print("✗ selftest 失败：%d 项" % len(failures))
        return 1
    print("✓ selftest 通过：%d 项断言" % 19)
    return 0


if __name__ == "__main__":
    sys.exit(selftest())
