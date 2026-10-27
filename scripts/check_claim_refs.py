#!/usr/bin/env python3
"""领用闸门：分支必须先走 agent-init 领用（服务端 claim ref 占位）。

唯一事实源 = 服务端 `refs/claims/*`。闸门校验三件事：

1. 分支历史里有 `chore(claim): 领取 #<短号> 实施任务` 提交（领用意图）；
2. 服务端存在 `refs/claims/<短号>`，且该 ref 指向的提交主题也是这条领用信息
   （证明确实是一次领用占位，不是随便指一个提交）；
3. 该占位提交是分支 HEAD 的祖先；若分支经历过 rebase（占位提交被改写），
   退化为「分支历史里存在同短号的领用提交」，只告警不阻断。

一个分支只允许领一张卡。

用法（CI 里先 fetch 一次 claim refs）：

    git fetch origin '+refs/claims/*:refs/claims/*'
    python scripts/check_claim_refs.py --base origin/main --head HEAD

退出码：0 通过 / 1 未领用或占位不匹配 / 2 用法或仓库错误。
"""

import argparse
import re
import subprocess
import sys

CLAIM_MESSAGE = re.compile(
    r"^chore\(claim\):\s*领取\s*#((?:[A-Za-z]{1,4}\d*-\d+)|(?:M\d+))\s*实施任务$"
)


def git(*args):
    return subprocess.run(
        ["git"] + list(args),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def fail(message):
    sys.stderr.write("[领用闸门] %s\n" % message)
    sys.exit(1)


def claim_subject(code):
    return "chore(claim): 领取 #%s 实施任务" % code


def main():
    parser = argparse.ArgumentParser(description="校验分支的 claim ref 占位")
    parser.add_argument("--base", default="origin/main", help="基线引用（默认 origin/main）")
    parser.add_argument("--head", default="HEAD", help="分支头引用（默认 HEAD）")
    parser.add_argument("--ref-prefix", default="refs/claims/", help="claim ref 前缀")
    args = parser.parse_args()

    log = git("log", "--format=%s", "%s..%s" % (args.base, args.head))
    if log.returncode != 0:
        sys.stderr.write(log.stderr)
        sys.exit(2)

    subjects = [line.strip() for line in log.stdout.splitlines() if line.strip()]
    codes = []
    for subject in subjects:
        match = CLAIM_MESSAGE.match(subject)
        if match:
            codes.append(match.group(1).upper())

    if not codes:
        fail(
            "分支上没有领用提交（chore(claim): 领取 #<短号> 实施任务）。"
            "请用 scripts/agent-init.ps1 -Card <短号> 开区，禁止绕过领用直接建分支。"
        )

    unique = sorted(set(codes))
    if len(unique) > 1:
        fail("一个分支只能领一张卡，检测到多个短号：%s" % "、".join(unique))
    code = unique[0]

    ref = args.ref_prefix + code
    rev = git("rev-parse", "--verify", "-q", ref)
    if rev.returncode != 0:
        fail(
            "服务端缺少 %s（短号 %s 未占位）。领用需联网执行 agent-init，"
            "提交信息不能替代服务端占位。" % (ref, code)
        )
    claim_sha = rev.stdout.strip()

    ref_subject = git("log", "-1", "--format=%s", claim_sha)
    actual = ref_subject.stdout.strip() if ref_subject.returncode == 0 else ""
    if actual != claim_subject(code):
        fail(
            "%s 指向的提交不是该短号的领用提交（实际主题：%s）。占位被篡改或短号不匹配。"
            % (ref, actual or "<不可读>")
        )

    ancestor = git("merge-base", "--is-ancestor", claim_sha, args.head)
    if ancestor.returncode != 0:
        if claim_subject(code) in subjects:
            sys.stderr.write(
                "[领用闸门] 提示：%s 与分支历史不一致（分支经历过 rebase），"
                "已按同短号领用提交放行。\n" % ref
            )
        else:
            fail(
                "%s 指向 %s，不在分支历史中，分支上也没有同短号的领用提交。"
                % (ref, claim_sha[:7])
            )

    print("[领用闸门] 通过：%s @ %s" % (ref, claim_sha[:7]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
