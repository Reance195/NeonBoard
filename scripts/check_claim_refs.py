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

**僵尸占位巡检（DX-76，`--audit-zombies`）**：git-done 的释放判据够不着两类历史残留——
① 占位提交虽是 origin/main 祖先、但那次收尾没跑到清理；② 分支合并前被 rebase 重写，
占位仍指向重写前的提交（既不是 main 祖先，台账 claims 投影又已被 release-claims 清空，
判据 2 恒打不中）。巡检按台账 baseline 全量扫描 refs/claims/*，对「卡已 done 且占位再无
在办用途」的点名叫「应释放」；离线（服务端不可达）保守跳过并告警。释放判据的单一实现
在本文件（`classify_claim`），git-done 收尾的自动释放与 CI 巡检共用同一份判定。

另有一条**方向校验（只告警）**：同一张卡的实现提交若早于它的领用提交，说明是「先写代码
后补领用」，两道闸门都查不出这种倒置。这里给出提示但不阻断 —— 历史分支可能确实需要补领。

用法（CI 里先 fetch 一次 claim refs）：

    git fetch origin '+refs/claims/*:refs/claims/*'
    python scripts/check_claim_refs.py --base origin/main --head HEAD

退出码：0 通过 / 1 未领用或占位不匹配 / 2 用法或仓库错误。

巡检模式（DX-76）：

    python scripts/check_claim_refs.py --audit-zombies

退出码：0 干净或保守跳过（离线）/ 1 存在应释放的僵尸占位 / 2 用法或仓库错误。
"""

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

CODE = r"(?:[A-Za-z]{1,4}\d*-\d+)|(?:M\d+)"
CLAIM_MESSAGE = re.compile(r"^chore\(claim\):\s*领取\s*#(%s)\s*实施任务$" % CODE)
CARD_IN_SUBJECT = re.compile(r"#(%s)" % CODE)
# 流程提交不算「实现」：领用 / 快照同步由脚本产生，天然晚于或早于实现都无意义。
PROCESS_SUBJECT = re.compile(r"^(?:chore\((?:claim|roadmap|board)\)|merge:)")

# 台账固定在仓库根（按脚本自身路径定位，与 release-claims.py 同口径）——
# CI 检出的就是 origin/main tip 的台账，本地跑的是当前分支的副本；done 判定两边一致。
LEDGER = Path(__file__).resolve().parent.parent / "docs" / "board-ledger.json"


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


def classify_claim(baseline, is_ancestor_of_main, referenced_refs):
    """僵尸占位判定（DX-76，释放判据的单一实现）。

    baseline             : 台账 baseline 值（'done' / 'todo' / …；台账无登记时为 None）
    is_ancestor_of_main  : 占位提交是否 origin/main 祖先（工作已合入主线）
    referenced_refs      : 仍包含占位提交的分支引用名列表（refs/heads/* + refs/remotes/origin/*）

    返回 (verdict, detail)，verdict ∈ {'zombie', 'keep', 'skip'}：
    - zombie：卡已 done 且占位再无在办用途（判据 A：已合入主线；判据 B：rebase 重写后
              占位指向的提交已不被任何分支引用）。done 卡不允许重新领用，占位的存在
              只剩两个用途——给在办分支的领用闸门放行、防双领；都不成立即应释放。
    - keep  ：卡未 done（可能仍在办），或虽 done 但仍被分支引用（多卡分支里活跃卡
              还要靠它过闸门，删了会把别人的 CI 打红）。
    - skip  ：台账无此卡，无法判定——保守跳过并告警，绝不误删。
    """
    if baseline is None:
        return ("skip", "台账无此卡的 baseline 登记，保守跳过")
    if "done" not in str(baseline).lower():
        return ("keep", "卡未 done（baseline=%s），占位可能仍服务在办工作" % baseline)
    if is_ancestor_of_main:
        return ("zombie", "指向提交已是 origin/main 祖先且卡已 done（判据 A）")
    if not referenced_refs:
        return ("zombie",
                "卡已 done 且占位提交不被任何本地/远端分支引用"
                "（判据 B：分支 rebase 重写后典型残留）")
    return ("keep", "虽 done 但仍被 %d 个分支引用，在办分支的闸门还要用它放行"
            % len(referenced_refs))


# 接手点名双档阈值（DX-149 拍板项 2）：沿用既有口径——
# 30 分钟（check-worktree-activity.ps1 默认）判「疑似中断」、
# 240 分钟（agent-workspace.ps1 -StaleMinutes 默认）判「陈旧占用」。
HANDOFF_STALLED_MINUTES = 30
HANDOFF_STALE_MINUTES = 240


def classify_handoff(baseline, updated_at, branch_last_commit, now_ts,
                     stalled_minutes=HANDOFF_STALLED_MINUTES,
                     stale_minutes=HANDOFF_STALE_MINUTES,
                     has_branch=True):
    """接手三态判定（DX-149，判活的单一实现；assignment-store / --audit-handoff 共用）。

    baseline            : 台账 baseline 值（None = 台账无登记）
    updated_at          : assignment 心跳时间戳（无记录为 0）
    branch_last_commit  : 认领分支最后提交时间戳（分支不存在/取不到为 0）
    now_ts              : 当前时间戳
    has_branch          : 认领分支是否仍存在（本地 heads 或远端 origin）

    返回 (verdict, detail)，verdict ∈ {'active','stalled','stale','orphan','skip'}：
    - active : 30 分钟内有动静（心跳或提交），别碰；
    - stalled: 30~240 分钟无动静，疑似中断——先确认对方是否真停，勿自动接手；
    - stale  : 超 240 分钟无动静，陈旧占用——维护者授权后可走 handoff.ps1 接手；
    - orphan : 占位尚存但认领分支已不存在（DX-138/DX-141 型残留）；
    - skip   : 无法判定（台账无登记 / done 卡归僵尸巡检 / 时间事实全缺），保守不点名。

    判据钉死「读哪份事实、哪个时点」（Z15）：活动性取 max(心跳, 分支最后提交)——
    心跳可被节流（assignment-store 30 分钟），提交是 git 硬事实；两者取新者防误判
    「刚提交完没来得及心跳」的 agent。
    """
    if baseline is None:
        return ("skip", "台账无此卡的 baseline 登记，无法判定")
    if "done" in str(baseline).lower():
        return ("skip", "卡已 done，占位处置归僵尸巡检（classify_claim），不在接手点名范围")
    if not has_branch:
        return ("orphan", "占位尚存但认领分支已不存在（历史残留）")
    ref_ts = max(int(updated_at or 0), int(branch_last_commit or 0))
    if ref_ts <= 0:
        return ("skip", "无心跳也无分支提交时间，无法判定活动性")
    age_minutes = (now_ts - ref_ts) / 60.0
    if age_minutes <= stalled_minutes:
        return ("active", "最近 %.0f 分钟内有动静" % age_minutes)
    if age_minutes <= stale_minutes:
        return ("stalled", "%.0f 分钟无动静，疑似中断（先确认勿自动接手）" % age_minutes)
    return ("stale", "%.0f 分钟无动静，陈旧占用（授权后可接手）" % age_minutes)


def _git_lines(*args):
    run = git(*args)
    if run.returncode != 0:
        return None
    return [line.strip() for line in run.stdout.splitlines() if line.strip()]


def _scan_zombie_refs():
    """服务端实态扫描 + classify（audit 与 release 共用的唯一扫描实现）。

    占位清单以 ls-remote 的服务端实态为准（§7.2.1：服务端 ref 是唯一事实源），
    本地镜像可能滞后于他人已做的释放；同时刷新 origin/main 跟踪引用，
    保证祖先判定读到的是服务器主线（合并刚推完、本地 tracking 尚未跟上的场景）。
    返回 (zombies, skips, total)；origin / 台账不可达时返回 (None, None, 0)
    表示整轮保守跳过。zombies 元素为 (ref, sha, code, detail)。
    """
    server = _git_lines("ls-remote", "origin", "refs/claims/*", "refs/heads/main")
    if server is None:
        return None, None, 0
    # 对象可达性：先把服务端占位与主线镜像到本地，merge-base / for-each-ref 才有对象可用。
    git("fetch", "origin", "+refs/claims/*:refs/claims/*",
        "+refs/heads/main:refs/remotes/origin/main")

    # ls-remote 混入了 main 的跟踪行，占位遍历只留 claims；main 的存在性用 rev-parse 复核。
    claims = [l for l in server if l.split("\t")[-1].strip().startswith("refs/claims/")]
    main_sha_run = git("rev-parse", "--verify", "-q", "origin/main")
    if main_sha_run.returncode != 0:
        sys.stderr.write("[巡检] origin/main 不可解析，本轮保守跳过，不计僵尸。\n")
        return None, None, 0

    try:
        baseline = json.loads(LEDGER.read_text(encoding="utf-8")).get("baseline") or {}
    except (OSError, ValueError) as exc:
        sys.stderr.write("[巡检] 台账不可读（%s），本轮保守跳过，不计僵尸。\n" % exc)
        return None, None, 0

    zombies, skips = [], []
    for line in claims:
        sha, _, ref = line.partition("\t")
        sha, ref = sha.strip(), ref.strip()
        code = ref[len("refs/claims/"):]
        if not code:
            continue
        exists = git("cat-file", "-e", "%s^{commit}" % sha)
        if exists.returncode != 0:
            skips.append("%s：占位提交 %s 本地不可达，无法判定，保守跳过" % (ref, sha[:7]))
            continue
        ancestor = git("merge-base", "--is-ancestor", sha, "origin/main")
        contained = _git_lines(
            "for-each-ref", "--format=%(refname)", "--contains", sha,
            "refs/heads", "refs/remotes") or []
        verdict, detail = classify_claim(
            baseline.get(code), ancestor.returncode == 0, contained)
        if verdict == "zombie":
            zombies.append((ref, sha, code, detail))
        elif verdict == "skip":
            skips.append("%s：%s" % (ref, detail))
    return zombies, skips, len(claims)


def audit_zombies():
    """DX-76 巡检：全量扫描服务端 refs/claims/*，点名「应释放」的僵尸占位。

    退出码：0 干净（或保守跳过）/ 1 存在僵尸（CI 红，按提示补清）/ 2 仓库错误。
    """
    zombies, skips, total = _scan_zombie_refs()
    if zombies is None:
        sys.stderr.write("[巡检] origin 不可达（离线？），本轮保守跳过，不计僵尸。\n")
        return 0
    for line in skips:
        sys.stderr.write("[巡检] 跳过：%s\n" % line)
    if not zombies:
        print("[巡检] 僵尸占位巡检通过：%d 条服务端占位均无可释放项。" % total)
        return 0
    sys.stderr.write("[巡检] 发现 %d 条应释放的僵尸占位（卡已 done、占位再无在办用途）：\n"
                     % len(zombies))
    for ref, sha, code, detail in zombies:
        sys.stderr.write("  - %s @ %s：%s\n" % (ref, sha[:7], detail))
        sys.stderr.write("    释放：git push origin --delete %s\n" % ref)
    sys.stderr.write("[巡检] 补清后重跑本巡检应转绿；收尾侧由 git-done 自动释放（单一实现见 classify_claim）。\n")
    return 1


def release_zombies():
    """DX-90 收尾释放：对巡检判定的僵尸逐条本地释放 + 服务端删除。

    判定与巡检共用 _scan_zombie_refs / classify_claim（单一实现，不在 PS 复制判据）。
    离线时服务端删除失败只告警（下次收尾再试，巡检会兜底点名），不阻断收尾。
    输出实际释放的短号，一行一个（调用方据此留痕）。退出码恒 0（除仓库错误 2）。
    """
    zombies, skips, _ = _scan_zombie_refs()
    if zombies is None:
        sys.stderr.write("[释放] origin 或台账不可达（离线？），本轮保守跳过，下次收尾再试。\n")
        return 0
    for line in skips or []:
        sys.stderr.write("[释放] 跳过：%s\n" % line)
    for ref, sha, code, detail in zombies:
        git("update-ref", "-d", ref)
        print(code)
        del_res = git("push", "origin", "--delete", ref)
        if del_res.returncode == 0:
            sys.stderr.write("[释放] %s @ %s：%s → 本地与服务端均已释放\n"
                             % (ref, sha[:7], detail))
        else:
            sys.stderr.write("[释放] %s：服务端删除失败（离线？），本地已清，下次收尾再试\n" % ref)
    if not zombies:
        sys.stderr.write("[释放] 无可释放的僵尸占位。\n")
    return 0


def audit_handoff(json_output=False):
    """DX-149 接手点名巡检：全量扫描服务端 refs/claims/*，结合 assignment 现场
    （心跳 / branch / nextStep）与分支提交事实，输出三态清单。

    与 audit_zombies 的分工：僵尸巡检管「done 卡的残留占位」，本巡检管
    「未 done 卡的死现场」。判定唯一实现 classify_handoff；assignments 只
    作现场信息源，绝不作为防双领依据（DX-149 边界）。

    退出码：0 全部活动或保守跳过 / 1 存在 stalled/stale/orphan 点名项。
    """
    now = int(time.time())
    server = _git_lines("ls-remote", "origin", "refs/claims/*")
    if server is None:
        sys.stderr.write("[接手巡检] origin 不可达（离线？），本轮保守跳过，不点名。\n")
        return 0
    try:
        ledger_data = json.loads(LEDGER.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        sys.stderr.write("[接手巡检] 台账不可读（%s），本轮保守跳过，不点名。\n" % exc)
        return 0
    baseline = ledger_data.get("baseline") or {}
    claims_proj = ledger_data.get("claims") or {}

    # assignment 现场表：只读文件不 import（assignment-store.py 文件名带连字符，
    # import 语法加载不了；读不违反「唯一写者」，写仍只能经 assignment-store.py）
    asg_path = LEDGER.parent / "agent-assignments.json"
    assignments = {}
    if asg_path.exists():
        try:
            assignments = (json.loads(asg_path.read_text(encoding="utf-8-sig"))
                           .get("assignments") or {})
        except (OSError, ValueError) as exc:
            sys.stderr.write("[接手巡检] assignments 不可读（%s），现场信息降级。\n" % exc)

    # 第三级信息源：远端各分支头的台账 claims（领用投影随分支走，主线台账看不到
    # 未合入分支的领用——DX-139 系 4 张 28h 卡曾被据此误判成孤儿）。一次性建索引。
    remote_claims = {}
    for ref in (_git_lines("for-each-ref", "--format=%(refname)",
                           "refs/remotes/origin") or []):
        if ref.endswith("/HEAD") or ref.endswith("/main"):
            continue
        tree = git("show", "%s:docs/board-ledger.json" % ref)
        if tree.returncode != 0:
            continue
        try:
            other = json.loads(tree.stdout.decode("utf-8", "replace"))
        except ValueError:
            continue
        for oc_code, oc in (other.get("claims") or {}).items():
            if oc and oc.get("branch") and oc_code not in remote_claims:
                remote_claims[oc_code] = oc

    # 第四级：.worktrees/* 物理现场——未推送分支的领用投影只存在本地检出台账里
    # （DX-139 系「本地独有提交」连远端都扫不到），worktree 是最接近实况的事实源。
    # .worktrees 只在锚点区存在（pitfalls 1.31 同款）：按 git common dir 父目录定位，
    # 不能用本脚本检出根——从 linked worktree 里跑必扫空。
    wt_claims = {}
    anchor_root = LEDGER.parent.parent
    common = git("rev-parse", "--git-common-dir")
    if common.returncode == 0:
        cd = common.stdout.strip()
        cd_path = Path(cd) if Path(cd).is_absolute() else (anchor_root / cd)
        wt_root = cd_path.parent / ".worktrees"
    else:
        wt_root = anchor_root / ".worktrees"
    if wt_root.is_dir():
        for d in sorted(wt_root.iterdir()):
            wt_ledger = d / "docs" / "board-ledger.json"
            if not d.is_dir() or not wt_ledger.exists():
                continue
            try:
                wdata = json.loads(wt_ledger.read_text(encoding="utf-8-sig"))
            except (OSError, ValueError):
                continue
            agent_id = ""
            env_file = d / ".agent-env.ps1"
            if env_file.exists():
                try:
                    mag = re.search(r"(?m)^\s*\$env:NEONBOARD_AGENT_ID\s*=\s*'([^']*)'",
                                    env_file.read_text(encoding="utf-8-sig"))
                    if mag:
                        agent_id = mag.group(1)
                except OSError:
                    pass
            for wc_code, wc in (wdata.get("claims") or {}).items():
                if wc and wc.get("branch") and wc_code not in wt_claims:
                    wt_claims[wc_code] = {"agent": agent_id or wc.get("agent", ""),
                                          "branch": wc.get("branch", "")}

    results = []
    for line in server:
        sha, _, ref = line.partition("\t")
        ref = ref.strip()
        if not ref.startswith("refs/claims/"):
            continue
        code = ref[len("refs/claims/"):]
        if not code:
            continue
        asg = assignments.get(code) or {}
        wcl = wt_claims.get(code) or {}
        proj = claims_proj.get(code) or {}
        rmt = remote_claims.get(code) or {}
        branch = (asg.get("branch") or wcl.get("branch") or proj.get("branch")
                  or rmt.get("branch") or "")
        has_branch = False
        branch_last = 0
        if branch:
            probe_local = git("show-ref", "--verify", "--quiet", "refs/heads/" + branch)
            probe_remote = git("show-ref", "--verify", "--quiet",
                               "refs/remotes/origin/" + branch)
            has_branch = probe_local.returncode == 0 or probe_remote.returncode == 0
            if has_branch:
                lines = _git_lines("log", "-1", "--format=%ct", branch)
                if lines:
                    try:
                        branch_last = int(lines[0])
                    except ValueError:
                        branch_last = 0
        verdict, detail = classify_handoff(
            baseline.get(code), int(asg.get("updatedAt") or 0), branch_last,
            now, has_branch=has_branch)
        last_activity = max(branch_last, int(asg.get("updatedAt") or 0))
        age_min = round((now - last_activity) / 60, 1) if last_activity > 0 else None
        suggestion = ""
        if verdict == "stale":
            suggestion = ("pwsh -File scripts/handoff.ps1 -Card %s -ToAgent <新agent名>"
                          " -AuthorizedBy <授权来源>" % code)
        elif verdict == "orphan":
            suggestion = "git push origin --delete %s（维护者确认后清理）" % ref
        elif verdict == "stalled":
            suggestion = "先确认 %s 是否真停，勿自动接手" % (asg.get("agent") or "对方")
        results.append({
            "code": code, "verdict": verdict, "detail": detail,
            "agent": (asg.get("agent") or wcl.get("agent") or proj.get("agent")
                      or rmt.get("agent") or ""),
            "branch": branch, "nextStep": asg.get("nextStep", ""),
            "lastActivity": last_activity, "ageMinutes": age_min,
            "suggestion": suggestion,
        })

    if json_output:
        print(json.dumps({"checkedAt": now, "results": results},
                         ensure_ascii=False, indent=2))
    else:
        labeled = {"active": "活动", "stalled": "疑似中断", "stale": "陈旧占用",
                   "orphan": "孤儿占位", "skip": "跳过"}
        for r in results:
            sys.stderr.write("%-10s %-6s %s @ %s  最近活动 %s%s\n" % (
                r["code"], labeled.get(r["verdict"], r["verdict"]),
                r["agent"] or "?", r["branch"] or "?",
                time.strftime("%m-%d %H:%M", time.localtime(r["lastActivity"]))
                if r["lastActivity"] else "-",
                ("（%s 分钟前）" % r["ageMinutes"]) if r["ageMinutes"] is not None else ""))
            if r["nextStep"]:
                sys.stderr.write("           ↳ %s\n" % r["nextStep"])
            sys.stderr.write("           · %s\n" % r["detail"])
            if r["suggestion"]:
                sys.stderr.write("             建议：%s\n" % r["suggestion"])
        named = [r for r in results if r["verdict"] in ("stalled", "stale", "orphan")]
        if not named:
            sys.stderr.write("[接手巡检] %d 条占位全部活动，无可接手项。\n" % len(results))
        else:
            sys.stderr.write("[接手巡检] 点名 %d 条（疑似中断/陈旧占用/孤儿占位），"
                             "动作由人触发，不自动转移。\n" % len(named))
    return 1 if any(r["verdict"] in ("stalled", "stale", "orphan") for r in results) else 0


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

    # ---- 僵尸占位判定（DX-76：classify_claim 单一实现）----
    v, _ = classify_claim("done", True, [])
    case("判据 A：卡已 done + 占位是 main 祖先 → 应释放", v == "zombie", v)

    v, _ = classify_claim("done", False, [])
    case("判据 B：卡已 done + 占位无任何分支引用（rebase 重写残留）→ 应释放", v == "zombie", v)

    v, detail = classify_claim("done", False, ["refs/heads/fix/other-work"])
    case("卡 done 但仍被在办分支引用 → 保留（闸门还要放行）", v == "keep", detail)

    v, _ = classify_claim("done", True, ["refs/remotes/origin/main"])
    case("祖先是 main 自身引用不算「仍被在办分支引用」→ 判据 A 优先释放", v == "zombie", v)

    v, _ = classify_claim("todo", False, [])
    case("卡未 done（todo）→ 保留", v == "keep", v)

    v, _ = classify_claim(None, False, [])
    case("台账无此卡 → 跳过不误删", v == "skip", v)

    v, _ = classify_claim("DX-84 done", True, [])
    case("baseline 带前缀的 done 写法同样识别", v == "zombie", v)

    # ---- 接手三态判定（DX-149：classify_handoff 单一实现）----
    now = 1_800_000_000
    v, _ = classify_handoff("todo", now - 300, 0, now)
    case("心跳 5 分钟前 → active", v == "active", v)
    v, _ = classify_handoff("todo", 0, now - 600, now)
    case("提交 10 分钟前 → active", v == "active", v)
    v, _ = classify_handoff("todo", now - 3600, now - 3600, now)
    case("60 分钟无动静 → stalled（先确认勿接手）", v == "stalled", v)
    v, _ = classify_handoff("todo", now - 300 * 60, now - 300 * 60, now)
    case("300 分钟无动静 → stale（可接手）", v == "stale", v)
    v, _ = classify_handoff("todo", 0, 0, now, has_branch=False)
    case("分支已删、占位尚存 → orphan", v == "orphan", v)
    v, _ = classify_handoff(None, 0, 0, now)
    case("台账无登记 → skip", v == "skip", v)
    v, _ = classify_handoff("done", 0, 0, now)
    case("done 卡 → skip（归僵尸巡检，不重复点名）", v == "skip", v)
    v, _ = classify_handoff("todo", 0, 0, now)
    case("心跳与提交时间全缺 → skip", v == "skip", v)
    v, _ = classify_handoff("todo", now - 300 * 60, now - 300, now)
    case("心跳旧提交新 → 按提交判 active（取新者防误判）", v == "active", v)
    v, _ = classify_handoff("DX-84 done", 0, 0, now)
    case("baseline 带前缀 done 同样 skip", v == "skip", v)

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
    parser.add_argument("--audit-zombies", action="store_true",
                        help="DX-76 巡检：全量扫描服务端 claim 占位，点名应释放的僵尸（不校验分支）")
    parser.add_argument("--release-zombies", action="store_true",
                        help="DX-90 收尾释放：对巡检判定的僵尸逐条本地释放并删服务端占位（stdout 输出释放短号）")
    parser.add_argument("--audit-handoff", action="store_true",
                        help="DX-149 接手点名：扫服务端占位 + assignment 现场，输出活动/疑似中断/孤儿三态清单")
    parser.add_argument("--json", action="store_true",
                        help="与 --audit-handoff 连用：输出 JSON（供 agent-status.ps1 等程序消费）")
    args = parser.parse_args()

    # Windows 管道下 stdout 默认走 ANSI 代码页，打印 ✓ / ✗ 与中文直接 GBK 崩
    # （本脚本的自测就因此让 CI 红过一轮）。与 task_gate.py / check_scope.py 同口径。
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    if args.selftest:
        return selftest()
    if args.audit_zombies:
        return audit_zombies()
    if args.release_zombies:
        return release_zombies()
    if args.audit_handoff:
        return audit_handoff(json_output=args.json)

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
