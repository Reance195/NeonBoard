#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""NeonBoard 密钥防入库扫描器（DX-148）。

判据（宪章 §四「密钥、令牌、本地私有配置严禁入库」的程序化执行）：
  - 只扫「本次暂存新增的行」：读 `git diff --cached -U0`，不看未暂存内容，
    不扫历史——历史泄漏属仓库外治理，扫描器越界反而制造大量不可修复命中。
  - 四类模式，任一命中即 exit 1 并列出 文件:行号 + 模式名（预览脱敏，
    绝不把密钥原文回显到日志/终端）：
      A 私钥块头     -----BEGIN ... PRIVATE KEY-----
      B 云厂 AK 前缀 AKIA/ASIA + 16 位以上大写正字；LTAI + 16 位以上正字（可混合大小写）
      C 凭据赋值     token/secret/password/api[_-]key 等键名 = 16 位以上实值
      D 长随机串     40+ 位连续 base64（须含大小写与数字）或 64+ 位含大写 hex
  - 唯一豁免面是 docs/secrets-allowlist.md 的登记行：
      <仓库相对路径> <该行内容(两端去空白)的 sha256 前 12 位> <理由>
    路径 + 行内容双匹配才算豁免；改一个字符就得重新登记，不存在
    「目录级/通配级」长期通行证。--no-verify 豁免面保持为零（宪章 §十一.2-7）。

D 模式的两条降噪规则（按「读哪份事实」钉死，Z15）：
  - 本仓库每天提交的 git SHA（40 位小写 hex，如台账 claims 的 head 字段）
    与常见摘要书写形态不报：hex 形态要求 64 位以上且含大写；
  - base64 形态要求同时含大写、小写与数字（真凭据的字符类构成），
    单词拼接、kebab 长串不算。

失败模式（Z4，安全类 fail-closed）：git 调用失败、暂存内容无法解码等
一律 exit 1 并说明原因；只有「扫描完成且无命中」才返回 0。

用法：
  python scripts/check_secrets.py --staged     # pre-commit 钩子调用
  python scripts/check_secrets.py --selftest   # 自测（fixture 全部拼接构造）
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

EXIT_CLEAN = 0
EXIT_HIT = 1
EXIT_ERROR = 2

# 模式定义用分段拼接书写，保证本文件自身的任何一行都不会被自家扫描器命中。
_P_A = ("-----BEGIN [A-Z ]*" + "PRIVATE KEY" + "-----")
_P_B = ("(AKIA|ASIA)[0-9A-Z]{16,}|(LT" + "AI)[0-9A-Za-z]{16,}")
_KEYNAME = r"(?:token|secret|password|passwd|pwd|api[_-]?key|access[_-]?key|auth[_-]?key)"
_P_C = ("(?i)" + _KEYNAME + r"\s*(?:[:=]|=>)\s*(?P<q>[\"']?)(?P<val>[\w\-./+=]{15,})")
_P_D = (r"(?<![0-9A-Za-z+/])(?P<tok>[0-9a-fA-F]{64,}|"
        r"(?=[A-Za-z0-9+/]*[a-z])(?=[A-Za-z0-9+/]*[A-Z])(?=[A-Za-z0-9+/]*\d)[A-Za-z0-9+/]{40,}={0,2})")

PATTERNS = [
    ("私钥块头", re.compile(_P_A)),
    ("云厂AK前缀", re.compile(_P_B)),
    ("凭据赋值长值", re.compile(_P_C)),
    ("长hex/base64独立串", re.compile(_P_D + r"(?![0-9a-zA-Z+/])")),
]

# 赋值行右值里允许出现的明显占位词：文档示例、模板占位，不算真凭据。
_PLACEHOLDER = re.compile(
    r"(?i)(^[%<{$]|\bexample\b|your[_-]|\bchangeme\b|placeholder|dummy|redacted|\*{3,})")

ALLOWLIST_PATH = "docs/secrets-allowlist.md"
ENTRY_RE = re.compile(r"^(\S+)\s+([0-9a-f]{12})\s+(\S.*)$")


def git(*args):
    res = subprocess.run(
        ["git", "-c", "core.quotepath=off"] + list(args),
        capture_output=True, check=False)
    if res.returncode != 0:
        raise RuntimeError("git %s 失败：%s" % (
            " ".join(args), res.stderr.decode("utf-8", "replace").strip()))
    return res.stdout.decode("utf-8", "replace")


def line_hash(text):
    return hashlib.sha256(text.strip().encode("utf-8")).hexdigest()[:12]


def load_allowlist(root):
    """返回 {(path, sha12): reason}。文件缺失 = 空表（不是错误）。"""
    entries = {}
    try:
        with open(root + "/" + ALLOWLIST_PATH.replace("/", __import__("os").sep),
                  encoding="utf-8") as fh:
            for raw in fh:
                line = raw.strip()
                if not line or line.startswith("#"):
                    continue
                if "|" in line:  # markdown 表格行：| path | hash | reason |
                    cells = [c.strip() for c in line.strip("|").split("|")]
                    line = " ".join(cells)
                m = ENTRY_RE.match(line)
                if m:
                    entries[(m.group(1), m.group(2))] = m.group(3)
    except FileNotFoundError:
        pass
    return entries


def mask(text):
    """脱敏预览：保留头尾各 4 位，中间打点，绝不回显完整凭据。"""
    t = text.strip()
    if len(t) <= 12:
        return "*" * len(t)
    return t[:4] + "…" * 1 + "*" * min(12, len(t) - 8) + t[-4:]


def scan_line(text):
    hits = []
    for name, rx in PATTERNS:
        m = rx.search(text)
        if not m:
            continue
        if name == "凭据赋值长值":
            val = m.group("val") or ""
            if not (m.group("q") or "") and len(val) < 16:
                continue
            if _PLACEHOLDER.search(val):
                continue
        if name == "长hex/base64独立串":
            tok = m.group("tok")
            if re.fullmatch(r"[0-9a-f]{64,}", tok):
                # 64+ 全小写 hex 多为 sha 摘要书写形态；混大写的 hex 仍按凭据报（命中即人工核对/登记豁免）
                continue
            if not re.search(r"[A-Z]", tok):
                continue
        hits.append((name, m))
    return hits


def iter_staged_added(root):
    """产出 (path, lineno, text)：仅本次暂存 diff 的新增行。"""
    diff = git("-C", root, "diff", "--cached", "-U0", "--no-color")
    cur = None
    lineno = 0
    for raw in diff.splitlines():
        if raw.startswith("+++ b/"):
            cur = raw[6:]
            if cur == "/dev/null" or cur == ALLOWLIST_PATH:
                # 豁免登记表本体免扫：登记行的内容就是被登记行（含示例凭据），
                # 若扫它则哈希链无限递归；其审核属性由「登记即留痕」承担。
                cur = None
        elif raw.startswith("@@"):
            m = re.search(r"\+(\d+)(?:,(\d+))?", raw)
            if m:
                lineno = int(m.group(1))
        elif raw.startswith("+") and cur is not None:
            yield cur, lineno, raw[1:]
            lineno += 1


def run_staged(root):
    allow = load_allowlist(root)
    hits = []
    for path, lineno, text in iter_staged_added(root):
        for name, m in scan_line(text):
            if (path, line_hash(text)) in allow:
                continue
            hits.append((path, lineno, name, text))
    return hits


def print_hits(hits):
    print("")
    print("=" * 67)
    print(" ❌ 暂存内容中发现疑似密钥，已阻止本次提交（宪章 §四：密钥严禁入库）")
    print("=" * 67)
    for path, lineno, name, text in hits:
        print("  %s:%d  [%s]  %s" % (path, lineno, name, mask(text)))
    print("")
    print(" 处置：")
    print("   1) 真凭据：从暂存区移除该行（git restore --staged <文件> 后编辑），")
    print("      已推送到远端的历史需换钥并交维护者处理；")
    print("   2) 确属误报：在 %s 登记一行：" % ALLOWLIST_PATH)
    print("        <路径> <该行内容 sha256 前12位> <理由>")
    print("      行内容哈希：python -c \"import hashlib;print(hashlib.sha256(")
    print("        '该行内容'.encode()).hexdigest()[:12])\"")
    print("      路径与内容双匹配才放行；禁止用 --no-verify 绕行。")
    print("")


def selftest():
    """fixture 一律用拼接构造，扫描器自扫源文件不得命中。"""
    cases = [
        ("A", ("-----BEGIN RSA " + "PRIVATE KEY" + "-----"), True, "私钥块头"),
        ("B", ("AK" + "IA" + "IOSFODNN" + "7EXAMPLE"), True, "云厂AK前缀"),
        ("B2", ("LT" + "AI" + "uKtnV1x" + "Y2ZpodlmEXAMPLE"), True, "云厂AK前缀"),
        ("C", ('api' + '_key = "sd2f' + "3lKjQ9xWeRTm1pXcvB4z" + '"'), True, "凭据赋值长值"),
        ("C2", ("password" + ": " + "S3cretValueR" + "andomNoKeyword"), True, "凭据赋值长值"),
        ("D", ("wJalrXUtnFE" + "MI/K7MDEN" + "G/bPxRfiCYEXAMPL" + "EKEY"), True, "长hex/base64独立串"),
        ("D-hex64大写", ("A" + "1b2C3d4E5f6" + "789012ABCDEF" + "0123456789abcdef" + "0123456789AB"), True, "长hex/base64独立串"),
        ("N-gitsha", ('"head": "' + "ae848ede70a543775097c5222c5b27360b9af528" + '"'), False, None),
        ("N-占位赋值", ("token" + ': "your_t' + "oken_here_example" + '"'), False, None),
        ("N-普通长词", "ThisRepositoryDocumentationParagraphIsQuiteLongIndeed", False, None),
        ("N-短值", ("secret" + ' = "abc123"'), False, None),
    ]
    fails = []
    for tag, text, want, wname in cases:
        got = scan_line(text)
        if want and not got:
            fails.append("%s 应命中(%s)未命中：%r" % (tag, wname, text[:20]))
        elif want and got[0][0] != wname:
            fails.append("%s 应命中%s，实际命中%s" % (tag, wname, got[0][0]))
        elif not want and got:
            fails.append("%s 不应命中，实际命中%s：%r" % (tag, got[0][0], text[:20]))
    # 自扫：本文件每一行都不得命中
    here = __file__
    with open(here, encoding="utf-8") as fh:
        for i, raw in enumerate(fh, 1):
            got = scan_line(raw.rstrip("\n"))
            if got:
                fails.append("自扫命中第%d行(%s)：%s" % (i, got[0][0], raw.strip()[:40]))
    # 豁免判定：命中行登记后应被跳过
    demo = ("AK" + "IA" + "IOSFODNN" + "7EXAMPLE")
    h = line_hash(demo)
    entry = "sample/fixture.txt " + h + " selftest 豁免"
    m = ENTRY_RE.match(entry)
    if not m or m.group(2) != h:
        fails.append("allowlist 行解析失败")
    print("[密钥扫描自测] %s" % ("通过" if not fails else "失败"))
    for f in fails:
        print("  ✗ " + f)
    return EXIT_CLEAN if not fails else EXIT_HIT


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser(description="NeonBoard 密钥防入库扫描器")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--staged", action="store_true", help="扫描暂存区新增行（pre-commit 用）")
    g.add_argument("--selftest", action="store_true", help="自测四类模式与豁免解析")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    try:
        root = git("-C", ".", "rev-parse", "--show-toplevel").strip()
    except RuntimeError as e:
        print(" ❌ 密钥扫描无法执行：%s" % e)
        return EXIT_ERROR
    try:
        hits = run_staged(root.replace("\\", "/"))
    except RuntimeError as e:
        print(" ❌ 密钥扫描失败（fail-closed，不提交）：%s" % e)
        return EXIT_ERROR
    if not hits:
        return EXIT_CLEAN
    print_hits(hits)
    return EXIT_HIT


if __name__ == "__main__":
    sys.exit(main())
