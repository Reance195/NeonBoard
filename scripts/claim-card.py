#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""任务卡领用校验与登记（唯一实现）。

原先这段逻辑以 here-string 内嵌在 scripts/agent-init.ps1 里，有两个问题：
  1. .ps1 在 Windows 上检出为 CRLF，而 Windows PowerShell 5.1 无法解析 CR 结尾的
     here-string 起始符 —— 整个 agent-init.ps1 在 5.1 上直接「批处理式报错」，
     连 -h 都跑不起来（PowerShell 7 无此问题）；
  2. 200 行 Python 埋在 PS 脚本中间，无法单独跑、无法单元测试。

抽成独立文件后，agent-init.ps1 只负责传参，判定逻辑与本文件一一对应，
和 scripts/task_gate.py 保持同样的「唯一实现」约定。

防双领的两层事实源：
  —— 服务端 refs/claims/<短号>（原子占位，唯一硬门）
  —— 各远端分支头里的台账 claims（本地投影，用于给出「被谁占了」的可读信息）

用法：
  python scripts/claim-card.py <repo-root> <短号> <agent> <branch> [board.db]

退出码：0 成功 / 2 短号未登记 / 3 已被占位或双领 / 4 父子关系缺失 /
        5 离线（无法确认服务端占位）/ 6 卡片已完成禁止重领
"""

import json
import os
import re
import sqlite3
import subprocess
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)
import board_ledger_json as blj  # noqa: E402  台账 canon 序列化唯一实现（Z5），判据与写者同源


def is_anchor_checkout(root):
    """root 是否锚点区检出（git-dir 与 git-common-dir 同值，DX-158 判定唯一实现）。

    锚点区检出的是 main（pre-commit 禁止 main 提交），assignments「随分支提交」的
    设计在这里无处落地——据此跳过 assignment upsert（DX-158）。
    读不到 git 事实（非检出/异常环境）时保守返回 True：宁可少写，不写错位置。
    """
    def _rev(*args):
        r = subprocess.run(["git", "-C", root] + list(args), capture_output=True)
        return r.stdout.decode("utf-8", "replace").strip() if r.returncode == 0 else None
    git_dir = _rev("rev-parse", "--absolute-git-dir")
    common = _rev("rev-parse", "--git-common-dir")
    if not git_dir or not common:
        return True
    if not os.path.isabs(common):
        common = os.path.abspath(os.path.join(root, common))
    return os.path.normcase(os.path.abspath(git_dir)) == os.path.normcase(common)


def selftest():
    """DX-158：锚点判定可执行自测（临时仓库，不碰本仓任何检出）。"""
    import tempfile
    failures = []

    def case(name, ok, detail=""):
        print(("  ✓ " if ok else "  ✗ ") + name + (("（实测：%s）" % detail) if detail and not ok else ""))
        if not ok:
            failures.append(name)

    with tempfile.TemporaryDirectory() as td:
        seed = os.path.join(td, "seed")
        r = subprocess.run(["git", "init", "-q", seed], capture_output=True)
        if r.returncode != 0:
            print("  ✗ selftest 临时仓库创建失败：%s" % r.stderr.decode("utf-8", "replace"))
            return 1
        with open(os.path.join(seed, "README.md"), "w", encoding="utf-8") as fh:
            fh.write("selftest\n")
        subprocess.run(["git", "-C", seed, "add", "."], capture_output=True)
        subprocess.run(["git", "-C", seed, "-c", "user.name=s", "-c", "user.email=s@s",
                        "commit", "-q", "-m", "init"], capture_output=True)
        case("独立检出（锚点）判 True", is_anchor_checkout(seed) is True)
        wt = os.path.join(td, "wt1")
        r = subprocess.run(["git", "-C", seed, "worktree", "add", "-q", wt, "-b", "b1"],
                           capture_output=True)
        if r.returncode == 0:
            case("linked worktree 判 False", is_anchor_checkout(wt) is False)
        else:
            case("linked worktree 判 False", False, r.stderr.decode("utf-8", "replace"))
        case("非检出目录保守判 True（宁可少写不写错位置）",
             is_anchor_checkout(os.path.join(td, "no-such-dir")) is True)

        # 第四例（DX-162）：「脚本在锚点检出、root 为其 linked worktree」——
        # 落盘位置必须唯一由 root 决定。构造：seed 为锚点，把 claim-card /
        # assignment-store 及依赖复制到锚点侧 scripts/，以 root=wt1 调锚点侧
        # 脚本领卡；断言条目落 wt1 侧 docs/agent-assignments.json，锚点侧零写入。
        # 修复前条目会写进锚点（_store 与 REPO_ROOT 都按脚本路径解析）——这正是
        # DX-158 自测暴露不了的脚本/检出分离场景（1.50 同款盲区）。
        if os.path.isdir(wt):
            import shutil
            anchor_scripts = os.path.join(seed, "scripts")
            os.makedirs(anchor_scripts, exist_ok=True)
            for _dep in ("claim-card.py", "assignment-store.py", "board_ledger_json.py"):
                shutil.copy(os.path.join(SCRIPT_DIR, _dep), os.path.join(anchor_scripts, _dep))
            origin = os.path.join(td, "origin.git")
            subprocess.run(["git", "init", "-q", "--bare", origin], capture_output=True)
            subprocess.run(["git", "-C", seed, "remote", "add", "origin", origin],
                           capture_output=True)
            os.makedirs(os.path.join(wt, "docs"), exist_ok=True)
            with open(os.path.join(wt, "docs", "board-ledger.json"), "w",
                      encoding="utf-8", newline="\n") as fh:
                fh.write(blj.dump_ledger({
                    "cards": {"DX-999": {"id": "dx999", "title": "自测卡",
                                         "stage": "DX", "tier": "P2"}},
                    "claims": {},
                }))
            r4 = subprocess.run(
                [sys.executable, os.path.join(anchor_scripts, "claim-card.py"),
                 wt, "DX-999", "agent-x", "b1", os.path.join(td, "board.db")],
                capture_output=True)
            _detail4 = (r4.stdout + r4.stderr).decode("utf-8", "replace").strip()
            _root_store = os.path.join(wt, "docs", "agent-assignments.json")
            _ok_store = False
            if os.path.exists(_root_store):
                with open(_root_store, encoding="utf-8") as _fh:
                    _ok_store = "DX-999" in _fh.read()
            case("脚本在锚点、root 为 linked worktree：条目落 root 侧",
                 r4.returncode == 0 and _ok_store, _detail4)
            case("脚本侧（锚点）零写入",
                 not os.path.exists(os.path.join(seed, "docs", "agent-assignments.json")),
                 os.path.join(seed, "docs"))

    if failures:
        sys.stderr.write("claim-card 锚点判定自测失败 %d 例\n" % len(failures))
        return 1
    print("claim-card 锚点判定与落盘定位自测通过")
    return 0


if "--selftest" in sys.argv:
    # ✓/✗ 与中文在 GBK 控制台直接崩（§2.6 同款），与 check_claim_refs.py 同口径
    for _stream in (sys.stdout, sys.stderr):
        try:
            _stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    sys.exit(selftest())

# ---------- 解析入参 ----------
if len(sys.argv) < 5:
    sys.stderr.write("用法：claim-card.py <root> <短号> <agent> <branch> [board.db]\n")
    sys.exit(2)

root = os.path.abspath(sys.argv[1])
code, agent, branch = sys.argv[2], sys.argv[3], sys.argv[4]
if len(sys.argv) > 5:
    board_db = sys.argv[5]
else:
    # 数据目录口径唯一实现在 scripts/nb_paths.py（与 PowerShell 侧 nb-paths.ps1 同构）：
    # 沙箱变量优先，否则落真实数据目录；由 check_script_hygiene.py 规则 A 盯住。
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from nb_paths import board_db_path
    board_db = str(board_db_path())

ledger = os.path.join(root, "docs", "board-ledger.json")
tmp = ledger + ".tmp"

data = json.load(open(ledger, encoding="utf-8"))
cards = data.get("cards") or {}
if code not in cards:
    sys.stderr.write("[领用] 台账未登记短号：%s\n" % code)
    sys.exit(2)

# 已完成的卡不允许重新领用：done 表示验收项已逐条满足并合并，再领用只能说明
# 这次改动没有对应任务，属于「先写代码后找卡」。真要继续做请新开卡。
# 这道门只看**主线**的 baseline，不看调用方的工作区副本：锚点区与 origin/main 分叉时
# （docs/pitfalls.md 7.4）工作区里的 baseline 是旧的，已交付的卡会被当成可领用重做一遍。
# 取不到 origin/main（离线 / 没有该引用）时退回工作区副本，行为与原来一致。
main_data = data
shown = subprocess.run(
    ["git", "-C", root, "show", "origin/main:docs/board-ledger.json"],
    capture_output=True, check=False,
)
if shown.returncode == 0:
    try:
        main_data = json.loads(shown.stdout.decode("utf-8-sig"))
    except ValueError:
        main_data = data
baseline = main_data.get("baseline") or {}
if baseline.get(code) == "done":
    sys.stderr.write("[领用] 卡片 %s 已完成（baseline=done），不允许重新领用\n" % code)
    sys.stderr.write("        如需继续改动，请在台账新开一张卡再领用。\n")
    sys.exit(6)

# 小任务（M##）只有在父任务未被任何人认领时才允许单独领用
parent_code = None
if not re.match(r"^[A-Za-z]{1,4}\d*-\d+$", code):
    child_id = (cards.get(code) or {}).get("id", "")
    if not child_id:
        sys.stderr.write("[领用] 子卡 %s 缺少卡片 id，无法确定父任务，拒绝领用\n" % code)
        sys.exit(4)
    if not os.path.exists(board_db):
        sys.stderr.write("[领用] 无法确定子卡的父任务：board.db 不存在（%s）\n" % board_db)
        sys.exit(4)
    try:
        con = sqlite3.connect("file:%s?mode=ro" % board_db.replace("\\", "/"), uri=True)
        row = con.execute("SELECT parent_id FROM task_link WHERE child_id = ?", (child_id,)).fetchone()
        con.close()
    except sqlite3.Error as exc:
        sys.stderr.write("[领用] 读取看板父子关系失败：%s\n" % exc)
        sys.exit(4)
    if not row:
        sys.stderr.write("[领用] 子卡 %s 未关联父任务，禁止单独领用\n" % code)
        sys.exit(4)
    by_id = {v.get("id", ""): k for k, v in cards.items()}
    parent_code = by_id.get(row[0], "")
    if not parent_code:
        sys.stderr.write("[领用] 子卡 %s 的父任务未在台账登记，禁止单独领用\n" % code)
        sys.exit(4)

# 服务端硬门：refs/claims/<短号> 唯一占位（非强推）。本地台账/远端分支扫描都可能漏，
# 只有服务端 ref 的创建是原子的：同名 ref 已存在时 push 会被拒绝，绕不过。
def _git(args):
    return subprocess.run(["git", "-C", root] + args, capture_output=True)

claim_ref = "refs/claims/%s" % code
if parent_code:
    probe = _git(["ls-remote", "--exit-code", "origin", "refs/claims/%s" % parent_code])
    if probe.returncode == 0:
        sys.stderr.write("[领用] 父任务 %s 已被认领（服务端 refs/claims/%s 存在），不能单独领用子卡 %s\n"
                         % (parent_code, parent_code, code))
        sys.exit(3)
    if probe.returncode != 2:
        sys.stderr.write("[领用] 无法访问 origin 校验父任务占位（退出码 %d），领用需联网\n" % probe.returncode)
        sys.exit(5)

head_sha = subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"]).decode().strip()
pushed = _git(["push", "origin", "%s:%s" % (head_sha, claim_ref)])
if pushed.returncode != 0:
    detail = (pushed.stderr or pushed.stdout).decode("utf-8", "replace").strip()
    sys.stderr.write("[领用] 服务端占位失败（%s）：短号 %s 可能已被认领，或网络不可用\n%s\n"
                     % (claim_ref, code, detail))
    sys.exit(3)
print("[领用] 服务端已占位 %s" % claim_ref)

# 判活：分支是否还在（本地 heads 或远端跟踪）、服务端占位是否还在。
# 失效登记的判据是「三处皆无」——已合并删分支、放弃后删分支都属此列；
# 只要还有一处存在就当仍被占用，宁可拦错也不放开双领。
def branch_exists(name):
    if not name:
        return False
    for ref in ("refs/heads/" + name, "refs/remotes/origin/" + name):
        probe = subprocess.run(
            ["git", "-C", root, "show-ref", "--verify", "--quiet", ref],
            capture_output=True, check=False)
        if probe.returncode == 0:
            return True
    return False

_branch_ref_cache = {}
def existing_branch_ref(name):
    if name in _branch_ref_cache:
        return _branch_ref_cache[name]
    found = None
    for ref in ("refs/heads/" + name, "refs/remotes/origin/" + name):
        probe = subprocess.run(
            ["git", "-C", root, "show-ref", "--verify", "--quiet", ref],
            capture_output=True, check=False)
        if probe.returncode == 0:
            found = ref
            break
    _branch_ref_cache[name] = found
    return found

_claim_ref_cache = {}
def claim_ref_state(target):
    """服务端占位状态 → ("sha", <commit>) / ("none", None) / ("unreachable", None)。

    不能用 `ls-remote --exit-code` 的退出码判断「有没有」：本次领用会**先**推占位，
    那会让旧登记看起来仍然活动（R4-03 重领时踩过）。所以这里取回具体 sha，交给
    调用方与登记里的 head 比对 —— 占位指向别的提交，说明这张卡已被新的领用接管。
    """
    if target in _claim_ref_cache:
        return _claim_ref_cache[target]
    probe = subprocess.run(
        ["git", "-C", root, "ls-remote", "origin", "refs/claims/%s" % target],
        capture_output=True, check=False)
    if probe.returncode != 0:
        state = ("unreachable", None)
    else:
        state = ("none", None)
        for line in probe.stdout.decode("utf-8", "replace").splitlines():
            parts = line.split()
            if len(parts) >= 2:
                state = ("sha", parts[0])
                break
    _claim_ref_cache[target] = state
    return state

def claim_is_stale(claim, target):
    # 分支与占位都不在了才算失效：历史缺口是「分支删了、登记留着」，
    # 这类残留曾把同一张卡的重新领用挡在防双领校验上（R4-02 踩过两次）。
    if not claim:
        return True
    if branch_exists(claim.get("branch", "")):
        return False
    kind, sha = claim_ref_state(target)
    if kind == "unreachable":
        return False  # 无法确认是否已释放，保守当仍被占用
    if kind == "none":
        return True
    return sha != (claim.get("head") or "")

# 防双领：origin/main 与所有远端分支头里的台账，若同短号已被其它分支领用 → 拒绝。
# 失效登记（分支已删且服务端占位已释放）不算占位，否则没人能再领同一张卡。
refs = ["origin/main"]
out = subprocess.run(
    ["git", "-C", root, "for-each-ref", "--format=%(refname)", "refs/remotes/origin"],
    capture_output=True, check=False)
if out.returncode == 0:
    for line in out.stdout.decode("utf-8", "replace").splitlines():
        if line and not line.endswith("/HEAD"):
            refs.append(line.strip())

taken = []
parent_taken = []
for ref in refs:
    tree = subprocess.run(
        ["git", "-C", root, "show", "%s:docs/board-ledger.json" % ref],
        capture_output=True, check=False)
    if tree.returncode != 0:
        continue
    try:
        other = json.loads(tree.stdout.decode("utf-8", "replace"))
    except ValueError:
        continue
    claim = (other.get("claims") or {}).get(code)
    if claim and claim.get("branch") != branch and not claim_is_stale(claim, code):
        taken.append("%s@%s" % (code, ref))
    if parent_code:
        parent_claim = (other.get("claims") or {}).get(parent_code)
        if (parent_claim and parent_claim.get("branch") != branch
                and not claim_is_stale(parent_claim, parent_code)):
            parent_taken.append("%s@%s" % (parent_code, ref))
if taken:
    sys.stderr.write("[领用] 短号 %s 已被其它分支领用：%s\n" % (code, "、".join(taken)))
    sys.exit(3)
if parent_taken:
    sys.stderr.write("[领用] 父任务 %s 已被认领（%s），不能单独领用子卡 %s\n"
                     % (parent_code, "、".join(parent_taken), code))
    sys.exit(3)

# 清理失效登记：分支已被删除、或已合并进 origin/main 的领用不再需要占位。
# 不清会让短号被永久「占用」，后来者无法领用同一张卡（worktree 回收时也没人负责删登记）。
data.setdefault("claims", {})
stale = []
for stale_code, stale_claim in list(data["claims"].items()):
    if stale_code == code:
        continue
    stale_branch = (stale_claim or {}).get("branch", "")
    if not stale_branch:
        stale.append(stale_code)
        continue
    if stale_branch == branch:
        continue
    # 只在本地删了、远端还在的分支不算失效（可能是别人刚推的活动分支）
    if not branch_exists(stale_branch):
        stale.append(stale_code)
        continue
    merged_ref = existing_branch_ref(stale_branch)
    if merged_ref:
        merged = subprocess.run(
            ["git", "-C", root, "merge-base", "--is-ancestor", merged_ref, "origin/main"],
            capture_output=True, check=False)
        if merged.returncode == 0:
            stale.append(stale_code)
for stale_code in stale:
    data["claims"].pop(stale_code, None)
if stale:
    print("[领用] 清理失效登记：%s" % "、".join(sorted(stale)))

data["claims"][code] = {
    "agent": agent,
    "branch": branch,
    "head": head_sha,
    "claimedAt": int(time.time()),
}
# 键序=登记序（与 register_board_cards.py 追加写口径一致）；sort_keys 会把全台账
# 按键重排，领用 diff 即污染所有卡片行，密钥闸门按新增行扫描时会被整本 prose 误拦。
with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
    fh.write(blj.dump_ledger(data))
os.replace(tmp, ledger)

# DX-149：领用现场落 assignments（接手上下文事实源）。唯一写者 assignment-store.py，
# 本脚本只调它的子命令；旁路失败只告警，绝不阻断领用（assignments 不参与防双领判定）。
# DX-158：锚点区检出（main）不落——assignments 随分支提交，锚点无处落地，写进 main
# 工作树只会制造无人能提交的死数据并绊锚点快进；现场定位由台账 claims / worktree 级兜底。
# DX-162：upsert 一律显式传 --root，落盘位置唯一由 root 检出决定——脚本自身在哪个
# 检出（锚点/工作区）不再影响落盘，消除「脚本在锚点、root 是 linked worktree」时
# 条目必写锚点的第三形态。
_store = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assignment-store.py")
_wt_match = re.search(r"[\\/]\.worktrees[\\/]([^\\/]+)$", root)
_sub = [sys.executable, _store, "upsert",
        "--root", root,
        "--code", code, "--agent", agent, "--branch", branch,
        "--stage", "开区",
        "--next-step", "dot-source .agent-env.ps1，先看 NB_SCOPE，收尾跑 verify.ps1"]
if _wt_match:
    _sub += ["--worktree", ".worktrees/" + _wt_match.group(1)]
_sid = os.environ.get("NEONBOARD_SESSION_ID") or os.environ.get("NB_SESSION_ID")
if _sid:
    _sub += ["--session-id", _sid]
if is_anchor_checkout(root):
    sys.stderr.write("[领用] 锚点区检出（main）不落 assignment（随分支提交无处落地，DX-158）；"
                     "现场定位由台账 claims / worktree 级兜底\n")
elif not os.path.exists(_store):
    sys.stderr.write("[领用] assignment-store 不存在（旁路，不阻断领用）：%s\n" % _store)
else:
    try:
        _run = subprocess.run(_sub, capture_output=True)
        if _run.returncode != 0:
            sys.stderr.write("[领用] assignment 现场登记失败（旁路，不阻断领用）：exit %d\n"
                             % _run.returncode)
    except OSError as _exc:
        sys.stderr.write("[领用] assignment-store 不可达（旁路，不阻断领用）：%s\n" % _exc)

print("[领用] %s ← %s（%s %s）已登记，合并/放弃分支后自动失效%s"
      % (code, branch, agent, "",
         ("，父任务 %s 未被认领" % parent_code) if parent_code else ""))
