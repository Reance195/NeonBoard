#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""release-claims.py — 清掉台账 claims 里的过期领用登记（唯一实现，供 git-done.ps1 收尾调用）。

为什么要有这个文件：服务端占位（refs/claims/<短号>）随合并被 git-done 释放了，认领分支
也删了，但台账投影里的 claims 条目还留着 —— 派生器每次都要报一句
「领用 X 的分支已不存在（放弃或删除），已忽略」。这警告本身无害（派生按 git 事实走），
但它是纯噪声，而且只有等**下任何人再领一张卡**时才会由 claim-card.py 的
「清理失效登记」顺带删掉 —— 「顺带」不保证发生：R6 收官后 --check 就一直挂着这条。
收尾闭环应该由「合并/删分支」这个动作自己完成，不该依赖下一次别的动作。

判据（与 claim-card.py 的失效判定同口径，保守）：只删 entry.branch 在本地
refs/heads/* 与 refs/remotes/origin/* 都不存在的条目；分支还在的一律保留
（活动卡、或别人刚推的远端分支）。不碰 cards / baseline —— agent 只被授权写
claims 与 note（AGENTS.md §7.2.1），台账保护钩子对这种改动放行。

用法：python scripts/release-claims.py
    在仓库内执行；台账固定 <repo>/docs/board-ledger.json，仓库根按脚本自身位置定位。
输出：实际删除的短号，一行一个；无输出 = 无可删（调用方据此决定是否产生提交）。
退出码：0 正常 / 2 台账不存在或 JSON 非法。
写回格式与 claim-card.py 完全一致（indent=2 / 键序=登记序 / ensure_ascii=False /
LF + 尾换行），两条写入路径不能互相把对方的格式搅乱（否则下次 diff 全是空格噪声）。
"""

import json
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
LEDGER = REPO_ROOT / "docs" / "board-ledger.json"


def branch_exists(name: str) -> bool:
    """分支是否还在（本地 heads 或远端跟踪任一存在）。空名与 "HEAD" 视为不存在。

    DX-42："HEAD" 不是合法命名分支（git check-ref-format --branch HEAD 拒绝），
    但 detached 领用会把它记进 claims；而 refs/remotes/origin/HEAD 是恒存在的
    符号引用，show-ref --verify 对它永远成功 —— 判据被永久骗过，登记成僵尸
    （DX-40 的登记实测中招，只能手工释放）。"""
    if not name or not name.strip() or name == "HEAD":
        return False
    for ref in ("refs/heads/" + name, "refs/remotes/origin/" + name):
        probe = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "show-ref", "--verify", "--quiet", ref],
            capture_output=True, check=False)
        if probe.returncode == 0:
            return True
    return False


def selftest() -> int:
    """DX-42：branch_exists 判据的可执行自测（只读，不写台账）。"""
    failures = []
    if branch_exists("HEAD"):
        failures.append("branch_exists('HEAD') 应为 False：origin/HEAD 是符号引用，不是分支")
    if branch_exists(""):
        failures.append("branch_exists('') 应为 False")
    if branch_exists("   "):
        failures.append("branch_exists('   ') 应为 False")
    if not branch_exists("main"):
        failures.append("branch_exists('main') 应为 True（本仓库 main 恒在）")
    if branch_exists("no-such-branch-dx42"):
        failures.append("branch_exists('no-such-branch-dx42') 应为 False")
    if failures:
        for line in failures:
            sys.stderr.write("  - %s\n" % line)
        sys.stderr.write("[任务闸门] release-claims 自测失败\n")
        return 1
    print("[任务闸门] release-claims 自测通过（5 例）")
    return 0


def main() -> int:
    if len(sys.argv) > 1:
        if sys.argv[1] == "selftest":
            return selftest()
        sys.stderr.write("release-claims.py 不带参数：全量扫描并清理过期登记；selftest 跑内置自测\n")
        return 2
    if not LEDGER.exists():
        sys.stderr.write("找不到台账：%s\n" % LEDGER)
        return 2

    data = json.loads(LEDGER.read_text(encoding="utf-8"))
    claims = data.get("claims") or {}
    if not claims:
        return 0

    removed = []
    for code in sorted(claims):
        claim = claims.get(code) or {}
        if branch_exists(claim.get("branch", "")):
            continue  # 分支还在：活动卡或未删干净，保守保留
        claims.pop(code)
        removed.append(code)

    if removed:
        data["claims"] = claims
        tmp = str(LEDGER) + ".tmp"
        with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(data, fh, indent=2, ensure_ascii=False)
            fh.write("\n")
        os.replace(tmp, str(LEDGER))
        for code in removed:
            print(code)
    return 0


if __name__ == "__main__":
    sys.exit(main())
