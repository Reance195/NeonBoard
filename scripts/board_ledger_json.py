#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""board_ledger_json —— docs/board-ledger.json 规范序列化的唯一实现（DX-157）。

规范（与 task_gate 的台账格式判据同源）：
  json.dumps(obj, ensure_ascii=False, indent=2) + "\\n"
  —— 键序保持登记序（禁 sort_keys）、UTF-8 原文不转义、文件以单个换行结尾。

背景（MOD-9 会话实录）：规范此前在 task_gate 的判据与四个写点
（register_board_cards / derive_board_status / claim-card / merge_board）各自手写，
五处复制不同源，任何一处改动都可能让台账整本重排 —— 密钥闸门按新增行扫描即被
整本 prose 误拦，rebase 重放还会把重排带进 main。判据与写者同源后，写盘格式在
结构上不可能再漂移（Z5 / Z14）。

用法：
  python scripts/board_ledger_json.py --selftest
"""
import json
import sys


def _out():
    # DX-46：GBK 控制台打 ✓/✗ 会抛 UnicodeEncodeError，输出一律转 UTF-8
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")


def dump_ledger(obj) -> str:
    """台账规范序列化：indent=2、原文不转义、尾换行、键序=登记序。"""
    return json.dumps(obj, ensure_ascii=False, indent=2) + "\n"


def loads(text: str):
    """读取台账（结构与普通 JSON 相同；容 CRLF / 尾空白，与 json.load 一致）。"""
    return json.loads(text)


def is_canonical(raw: str) -> bool:
    """文本是否与规范序列化逐字节一致（task_gate 格式判据同源）。"""
    try:
        return raw == dump_ledger(json.loads(raw))
    except ValueError:
        return False


def _selftest() -> int:
    checks = []

    def check(name, ok):
        checks.append((name, ok))

    sample = {"b": 2, "a": {"中文": "值"}, "cards": {"DX-1": {"k": "v"}}}
    text = dump_ledger(sample)
    check("indent=2（顶层两空格、嵌套四空格递进）",
          '\n  "b": 2,' in text and '\n    "中文": "值"' in text)
    check("ensure_ascii（中文原样不转义）", "中文" in text)
    check("尾单个换行", text.endswith("\n") and not text.endswith("\n\n"))
    check("键序保持登记序（禁 sort_keys）", text.index('"b"') < text.index('"a"'))
    check("round-trip 幂等（is_canonical 认可自身输出）", is_canonical(text))
    # 键序漂移 round-trip 抓不到（与 task_gate 判据同一语义）：排版判据只管
    # indent / 尾换行 / 转义；键序=登记序由 writers 经 dump_ledger 的登记序口径保证。
    check("sort 重排文本同样 self-canonical（判据只管排版）",
          is_canonical(json.dumps(sample, ensure_ascii=False, indent=2,
                                  sort_keys=True) + "\n"))
    check("识破 indent=4",
          not is_canonical(json.dumps(sample, ensure_ascii=False, indent=4) + "\n"))
    check("识破缺尾换行",
          not is_canonical(json.dumps(sample, ensure_ascii=False, indent=2)))
    check("非法 JSON 判非规范（不抛异常）", not is_canonical("{broken"))

    failures = [n for n, ok in checks if not ok]
    for n, ok in checks:
        print("  %s %s" % ("✓" if ok else "✗", n))
    if failures:
        print("[board_ledger_json] 自测失败 %d/%d 例" % (len(failures), len(checks)),
              file=sys.stderr)
        return 1
    print("[board_ledger_json] 自测通过：%d 例" % len(checks))
    return 0


def main(argv=None):
    _out()
    argv = list(sys.argv[1:] if argv is None else argv)
    if "--selftest" in argv:
        return _selftest()
    print("用法：python scripts/board_ledger_json.py --selftest", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
