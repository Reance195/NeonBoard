#!/usr/bin/env python3
"""领用闸门：分支必须先走 agent-init 领用（服务端 claim ref 占位）。

唯一事实源 = 服务端 `refs/claims/*`。闸门对分支历史里出现的**每一个**领用短号校验三件事：

1. 分支历史里有 `chore(claim): 领取 #<短号> 实施任务` 提交（领用意图）；
2. 服务端存在 `refs/claims/<短号>`，且该 ref 指向的提交主题也是这条领用信息
   （证明确实是一次领用占位，不是随便指一个提交）；
3. 该占位提交是分支 HEAD 的祖先；若分支经历过 rebase（占位提交被改写），
   退化为「分支历史里存在同短号的领用提交」，只告警不阻断。

**一分支多卡是合法的（DX-03）**：一条分支可以领多张卡（同一模块的顺序改动、一张卡的定义
+另一张卡的实现等）。判定口径从「只允许一张」改为「**每个出现的短号都必须各自持证**」——
少领一张仍被拒，多领合法。台账 claims 本身就是按短号存的字典，同分支多条登记指向同一个
分支即可；派生器 / release-claims 已按短号遍历，无需改动。

另有一条**方向校验（只告警）**：同一张卡的实现提交若早于它的领用提交，说明是「先写代码
后补领用」，两道闸门都查不出这种倒置。这里给出提示但不阻断 —— 历史分支可能确实需要补领。

用法（CI 里先 fetch 一次 claim refs）：

    git fetch origin '+refs/claims/*:refs/claims/*'
    python scripts/check_claim_refs.py --base origin/main --head HEAD

退出码：0 通过 / 1 未领用或占位不匹配 / 2 用法或仓库错误。
"""

import argparse
import re
import subprocess
import sys

CODE = r"(?:[A-Za-z]{1,4}\d*-\d+)|(?:M\d+)"
CLAIM_MESSAGE = re.compile(r"^chore\(claim\):\s*领取\s*#(%s)\s*实施任务$" % CODE)
CARD_IN_SUBJECT = re.compile(r"#(%s)" % CODE)
# 流程提交不算「实现」：领用 / 快照同步由脚本产生，天然晚于或早于实现都无意义。
PROCESS_SUBJECT = re.compile(r"^(?:chore\((?:claim|roadmap|board)\)|merge:)")


def git(*args):
    return subprocess.run(
        ["git"] + list(args),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def fail(message, code=1):
    sys.stderr.write("[领用闸门] %s\n" % message)
    sys.exit(code)


def claim_subject(code):
    return "chore(claim): 领取 #%s 实施任务" % code


def claimed_codes(subjects):
    """提交主题列表 → 去重后的领用短号列表（保持出现顺序）。"""
    codes = []
    for subject in subjects:
        match = CLAIM_MESSAGE.match(subject)
        if match:
            code = match.group(1).upper()
            if code not in codes:
                codes.append(code)
    return codes


def direction_warnings(entries):
    """方向校验：实现提交早于领用提交 = 先写代码后补领用（只告警）。

    entries 是新到旧的 (sha, subject) 列表 —— git log 的顺序天然是反向时间序，
    所以「下标更大」= 「更旧」。同一短号里，最早的非流程提交比它的领用提交更旧即倒置。
    """
    claims = {}
    implants = {}
    for idx, (sha, subject) in enumerate(entries):
        match = CLAIM_MESSAGE.match(subject)
        if match:
            claims.setdefault(match.group(1).upper(), (idx, sha, subject))
            continue
        if PROCESS_SUBJECT.match(subject):
            continue
        for code in CARD_IN_SUBJECT.findall(subject):
            code = code.upper()
            if code not in implants:
                implants[code] = (idx, sha, subject)

    warnings = []
    for code, (claim_idx, _, _) in claims.items():
        hit = implants.get(code)
        if hit and hit[0] > claim_idx:
            warnings.append(
                "#%s 的实现提交 %s 早于领用提交（先写代码后补领用）：%s"
                % (code, hit[1][:7], hit[2])
            )
    return warnings


def selftest():
    """一分支多卡判定的可执行自测（DX-03）：纯函数不碰 git，任何环境可跑。

    覆盖三类：短号提取、方向倒置告警、以及「多卡 ≠ 放行缺证」的核心口径。
    """
    cases = []

    def case(name, ok, detail=""):
        cases.append((name, ok, detail))

    # ---- 短号提取 ----
    subjects = [
        "chore(claim): 领取 #DX-20 实施任务",
        "chore(claim): 领取 #DX-21 实施任务",
        "fix(git): 修点东西 #DX-20 #DX-21",
    ]
    got = claimed_codes(subjects)
    case("两个领用提交都提取到了（保持出现顺序）", got == ["DX-20", "DX-21"], str(got))

    dup = claimed_codes(
        ["chore(claim): 领取 #DX-20 实施任务",
         "chore(claim): 领取 #DX-20 实施任务"]
    )
    case("同一短号重复领用只算一次", dup == ["DX-20"], str(dup))

    case("没有领用提交时为空", claimed_codes(["fix(x): 直接改 #DX-1"]) == [], "")

    # ---- 方向倒置告警 ----
    entries_ok = [
        ("bbb", "fix(git): 实现 #DX-20"),
        ("aaa", "chore(claim): 领取 #DX-20 实施任务"),
    ]
    case("领用在前、实现在后 → 无倒置告警", direction_warnings(entries_ok) == [], "")

    entries_bad = [
        ("ccc", "chore(claim): 领取 #DX-20 实施任务"),
        ("bbb", "fix(git): 实现 #DX-20"),
    ]
    warn = direction_warnings(entries_bad)
    case("实现早于领用 → 给出倒置告警", len(warn) == 1 and "#DX-20" in warn[0], str(warn))

    entries_noise = [
        ("ddd", "chore(claim): 领取 #DX-20 实施任务"),
        ("ccc", "chore(roadmap): 同步进度快照"),
        ("bbb", "chore(board): 清理过期领用登记 DX-20"),
        ("aaa", "merge: 合入 feat/x"),
    ]
    case("流程提交（claim/roadmap/board/merge）不误报倒置",
         direction_warnings(entries_noise) == [], str(direction_warnings(entries_noise)))

    # ---- 核心口径：多卡合法，但每张都要持证 ----
    # 这里验的是判定前提：提取出 2 个短号 ⇒ 闸门会对 2 个 ref 逐一校验（不是「只允许一张」）。
    # 真实 ref 校验由实跑覆盖 —— CI 跑本分支（两卡）即是一次。
    two = claimed_codes(
        ["chore(claim): 领取 #DX-20 实施任务",
         "chore(claim): 领取 #DX-21 实施任务"]
    )
    case("一分支两卡 ⇒ 需要校验 2 个 claim ref（不是「只允许一张」）", len(two) == 2, str(two))

    failures = [(n, d) for n, ok, d in cases if not ok]
    for name, ok, detail in cases:
        if ok:
            print("  ✓ %s" % name)
        else:
            print("  ✗ %s%s" % (name, ("（实测：%s）" % detail) if detail else ""))
    if failures:
        sys.stderr.write("[领用闸门] 一分支多卡自测失败 %d/%d 例\n" % (len(failures), len(cases)))
        return 1
    print("[领用闸门] 一分支多卡自测通过：%d 例" % len(cases))
    return 0


def main():
    parser = argparse.ArgumentParser(description="校验分支的 claim ref 占位")
    parser.add_argument("--base", default="origin/main", help="基线引用（默认 origin/main）")
    parser.add_argument("--head", default="HEAD", help="分支头引用（默认 HEAD）")
    parser.add_argument("--ref-prefix", default="refs/claims/", help="claim ref 前缀")
    parser.add_argument("--selftest", action="store_true",
                        help="只跑一分支多卡判定的自测（不读仓库）")
    args = parser.parse_args()

    if args.selftest:
        return selftest()

    log = git("log", "--format=%H%x09%s", "%s..%s" % (args.base, args.head))
    if log.returncode != 0:
        sys.stderr.write(log.stderr)
        sys.exit(2)

    entries = []
    for line in log.stdout.splitlines():
        if not line.strip():
            continue
        sha, _, subject = line.partition("\t")
        entries.append((sha.strip(), subject.strip()))
    subjects = [s for _, s in entries]

    codes = claimed_codes(subjects)
    if not codes:
        fail(
            "分支上没有领用提交（chore(claim): 领取 #<短号> 实施任务）。"
            "请用 scripts/agent-init.ps1 -Card <短号> 开区，禁止绕过领用直接建分支。"
        )

    # ---- 逐短号校验：每个都必须在服务端有自己的占位 ----
    problems = []
    rebased = []
    passed = []
    for code in codes:
        ref = args.ref_prefix + code
        rev = git("rev-parse", "--verify", "-q", ref)
        if rev.returncode != 0:
            problems.append(
                "服务端缺少 %s（短号 %s 未占位）。领用需联网执行 agent-init，"
                "提交信息不能替代服务端占位。" % (ref, code)
            )
            continue
        claim_sha = rev.stdout.strip()

        ref_subject = git("log", "-1", "--format=%s", claim_sha)
        actual = ref_subject.stdout.strip() if ref_subject.returncode == 0 else ""
        if actual != claim_subject(code):
            problems.append(
                "%s 指向的提交不是该短号的领用提交（实际主题：%s）。占位被篡改或短号不匹配。"
                % (ref, actual or "<不可读>")
            )
            continue

        ancestor = git("merge-base", "--is-ancestor", claim_sha, args.head)
        if ancestor.returncode != 0:
            if claim_subject(code) in subjects:
                rebased.append("%s @ %s" % (ref, claim_sha[:7]))
            else:
                problems.append(
                    "%s 指向 %s，不在分支历史中，分支上也没有同短号的领用提交。"
                    % (ref, claim_sha[:7])
                )
                continue
        passed.append("%s @ %s" % (ref, claim_sha[:7]))

    if problems:
        sys.stderr.write("[领用闸门] 领用不合规（每个出现在分支上的短号都必须各自持证）：\n")
        for line in problems:
            sys.stderr.write("  - %s\n" % line)
        if len(codes) > 1:
            sys.stderr.write(
                "  本分支领用了 %d 张卡（%s）：一分支多卡是允许的，"
                "但**每一张**都要有自己的 refs/claims/<短号> 占位。\n"
                % (len(codes), "、".join("#" + c for c in codes))
            )
            sys.stderr.write("  补领：在分支所在检出里再跑一次 agent-init -InPlace -Card <短号>。\n")
        sys.exit(1)

    if len(codes) > 1:
        print("[领用闸门] 通过：一分支 %d 卡（%s）均各自持证"
              % (len(codes), "、".join("#" + c for c in codes)))
    for line in passed:
        print("[领用闸门] 通过：%s" % line)
    for line in rebased:
        sys.stderr.write(
            "[领用闸门] 提示：%s 与分支历史不一致（分支经历过 rebase），"
            "已按同短号领用提交放行。\n" % line
        )
    for line in direction_warnings(entries):
        sys.stderr.write("[领用闸门] 提示：%s\n" % line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
