#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""看板状态派生器：从 git 提交历史 + 卡片台账派生看板卡片状态。

三件套的职责边界（v3 快照）：
  docs/board-ledger.json   卡片身份与初始状态的人工台账（唯一人工入口）
  progress.json             派生产物：status / code 由本脚本写入；写者是 git-done 收尾
                            与看板应用，**CI 只校验不回写**（R14-01）
  git 提交信息 #短号=状态   状态变更的唯一「代码事实」来源

派生规则：
  status(code) = git 历史中最后一次出现的 `#code=状态` 标记
              > 台账 claims 里登记未释放的领用（agent 开分支时登记，领用即 doing）
              > 退回 ledger.baseline[code]
              > 台账里也没有时为 todo

「领用 → doing」只认 git 事实，不新增防御面：
  claims[code] = {agent, branch, head, claimedAt}，由 scripts/agent-init.ps1 -Card 在
  开 worktree 时登记进自己 worktree 的台账副本，随 feature 分支提交、合并后进入主线。
  派生时只按 head 位置判定：
  - head 是主干的祖先（分支已合并）→ 登记失效，提示用 `#code=done` 收尾
  - head 在 git 里不可达（分支已放弃）→ 登记失效
  - 其余 → doing 生效
  因此领用登记不需要「释放」动作：分支合并或丢弃后自动失效。
  子卡（M##）的领用前提是父任务未被认领，由 scripts/agent-init.ps1 在领用时校验；
  派生器只按 claims 原样派生，不重复校验父卡归属。

于是状态变更永远有据可查：要么在提交信息里（代码事实），要么在台账里（显式声明），
不存在「悄悄改了快照」这条路径 —— 派生器 --check 会把不一致直接报出来。

用法：
  python scripts/derive_board_status.py --check    校验快照与派生结果一致，不一致退出码 1
  python scripts/derive_board_status.py --write    把派生结果写回快照（仅在有实质变化时落盘）
  python scripts/derive_board_status.py --baseline-check
      只报「git 已交付但台账 baseline 仍非 done」与「cards 有卡但 baseline 无条目」，
      只报不改、恒退 0（baseline 归维护者，agent 不得自行结卡）。
  python scripts/derive_board_status.py --stuck-codes
      只输出滞留卡短号（一行一个）：交付提交已合入主干但全历史无 #<短号>=done 的卡
      （DX-62）。供 agent-init 领用前告警，配 --ledger-ref 读主线台账。
  python scripts/derive_board_status.py --contract DX-94 --out <文件>
      输出卡片契约（scope/acceptance/depends）到 --out（DX-94）。台账只认
      --ledger-ref（默认 origin/main），与领用重开门判据同源；结果落盘而非 stdout，
      规避 PowerShell 捕获 Python 子进程输出时的控制台代码页 GBK 解码坑。
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
import board_priority as bp  # noqa: E402  排位规则唯一实现（DX-123），档位落点与校验不在此复制（Z5）

SNAPSHOT_VERSION = 3
ORDER_STEP = 1024
MARKER = re.compile(r"#([A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)*)(?:=(todo|doing|done))?")
# 领用提交的主题（agent-init.ps1 生成）。结单对账靠它区分「这条分支领了哪些卡」。
CLAIM_SUBJECT = re.compile(
    r"^chore\(claim\):\s*领取\s*#((?:[A-Za-z]{1,4}\d*-\d+)|(?:M\d+))\s*实施任务$")
# 流程管理类提交（领用 / 登记 / 立卡 / 补 scope）。它们的裸 #<短号> 是流程痕迹不是交付
# —— 滞留判定（DX-62）必须排除，否则每张被领过的卡都会被误报「交付疑似已在 main」。
ADMIN_SUBJECT = re.compile(r"^chore\((?:claim|board)\):")
# 路线图短号格式：R0-01 / R1-03 / M01 这类。写错这类短号属于真错误，必须拦下；
# 其余短号（例如 plan-hook 用的 #T12）不属于看板台账范畴，只提示不拦截。
ROADMAP_CODE = re.compile(r"^(?:[A-Za-z]{1,4}\d*-\d+|M\d+)$")
# 「形似看板短号」：字母段 + 数字，可选 -数字（台账里的卡号无一例外都是这个形态）。
# 只给纯提示性质的 warnings 用；unknown 的判定口径完全不变（见 scan_markers）。
LIKELY_CODE = re.compile(r"^[A-Za-z]{1,4}\d+(?:-\d+)?$")
STATUS_RANK = {"todo": 0, "doing": 1, "done": 2}
VALID_STATUS = ("todo", "doing", "done")
# 弃单终态（DX-111）：baseline 的第三种终态 —— 不是 done（没有交付），也不是
# todo（不得再被领用）。它只活在台账里：派生器不把它投到快照的三态，agent-init
# 直接读 baseline 拒领。重开必须由维护者经 chore(board) 授权改回 todo。
ABANDONED = "abandoned"


def is_abandoned(baseline, code):
    """台账 baseline 是否把这张卡标成弃单终态（大小写不敏感）。"""
    return (baseline.get(code) or "").lower() == ABANDONED


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_json(path):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def write_json_atomic(path, data):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(data, fh, ensure_ascii=False, indent=4, sort_keys=True)
        fh.write("\n")
    os.replace(tmp, path)


def git_log(root, ref=None):
    """按时间正序返回 [(sha, subject, body)]；ref 缺省扫 HEAD 全历史。"""
    out = subprocess.run(
        ["git", "-C", root, "log", "--reverse", "--format=%x1e%H%x1f%s%x1f%b",
         ref or "HEAD"],
        capture_output=True,
        check=False,
    )
    if out.returncode != 0:
        sys.stderr.write("git log 执行失败：%s\n" % out.stderr.decode("utf-8", "replace"))
        return []
    text = out.stdout.decode("utf-8", "replace")
    commits = []
    for chunk in text.split("\x1e"):
        chunk = chunk.strip("\n")
        if not chunk.strip():
            continue
        parts = chunk.split("\x1f")
        if len(parts) < 2:
            continue
        commits.append((parts[0], parts[1], parts[2] if len(parts) > 2 else ""))
    return commits


def emit_claimed(root, base, head):
    """输出 base..head 范围内领用过的短号，一行一个（供 git-done 结单对账）。

    为什么单独开一个出口，而不是让 git-done 自己跑 `git log --format=%s` 再匹配：
    PowerShell 捕获子进程输出时按控制台代码页解码，中文「领取」会变成乱码，
    换行还会丢（两条提交粘成一行），正则永不命中 —— 实测踩过。这里显式 utf-8 解码，
    输出的又是纯 ASCII 短号，跨代码页安全。

    返回 None 表示 git 失败（调用方据此跳过对账，而不是当成「没领过卡」）。
    """
    out = subprocess.run(
        ["git", "-C", root, "log", "--format=%s", "%s..%s" % (base, head)],
        capture_output=True, check=False,
    )
    if out.returncode != 0:
        sys.stderr.write("[派生器] 取领用登记失败：%s\n" % out.stderr.decode("utf-8", "replace"))
        return None
    seen = []
    for line in out.stdout.decode("utf-8", "replace").splitlines():
        m = CLAIM_SUBJECT.match(line.strip())
        if m:
            code = m.group(1).upper()
            if code not in seen:
                seen.append(code)
    return seen


def scan_markers(subject, body, known_codes):
    """把一条提交里的 # 标记分类成 (derived_patch, unknown, warnings)。

    三类信号的口径故意不同（DX-31）—— 别再图省事把 subject + body 拼起来一次扫完：

      · 状态标记 `#code=done`：subject 与 body **都认**。全历史里 15 个提交把标记写在
        body，其中 7 张卡的 derived 状态只有那一处；只认 subject 会改掉派生结果。
      · unknown（形如路线图短号却没登记）：subject 与 body **都收**。写错短号必须拦下，
        不能因为写在正文里就放过。
      · warnings（登记之外的其它 #词）：**只认 subject，且必须形似短号**。body 是散文，
        `#IFDEF` / `#RRGGBB` / `#R7-0X` 这类技术写法是正常内容 —— 把它们报成
        「提交信息里的短号不在台账」纯属噪声，每次收尾刷一屏。
    """
    derived = {}
    unknown = set()
    warnings = set()

    # 同一条提交内 body 后于 subject，与原先拼串扫描的顺序一致。
    for text in (subject, body):
        for match in MARKER.finditer(text):
            code = match.group(1).upper()
            status = match.group(2)
            if code in known_codes and status:
                derived[code] = status

    for text, allow_warning in ((subject, True), (body, False)):
        for match in MARKER.finditer(text):
            code = match.group(1).upper()
            if code in known_codes:
                continue
            if ROADMAP_CODE.match(code):
                unknown.add(code)
            elif allow_warning and LIKELY_CODE.match(code):
                warnings.add(code)
    return derived, unknown, warnings


def derive_status(root, known_codes):
    """返回 (derived, unknown_codes, warnings)。

    derived[code] = 历史里最后一次 `#code=状态` 标记给出的状态。
    unknown_codes = 历史里出现过但台账未登记的「路线图短号」（属于写错，必须拦下）。
    """
    derived = {}
    unknown = set()
    warnings = set()
    for _sha, subject, body in git_log(root):
        patch, u, w = scan_markers(subject, body, known_codes)
        derived.update(patch)   # git_log 按时间正序，最后一次标记胜出
        unknown |= u
        warnings |= w
    return derived, unknown, warnings


def scan_delivery_commit(subject, body, known_codes):
    """单条提交的交付信号（DX-62）：返回 (done_codes, bare_codes)。

    done_codes：这条提交里出现过的 `#<短号>=done`。subject 与 body 都认 ——
    与 derive 的状态标记口径一致（DX-31：全历史有提交把标记写在 body）。
    bare_codes：出现过的裸 `#<短号>`（无 =状态）。**只认 subject**：body 是散文，
    正文里的 `#DX-38` 交叉引用会被当成「交付痕迹」误报滞留，DX-31 同源教训。

    领用 / 立卡等流程提交的排除在 collect_delivery_signals 里做，这里不重复判。
    """
    done_codes = set()
    bare_codes = set()
    for text, is_subject in ((subject, True), (body, False)):
        for match in MARKER.finditer(text):
            code = match.group(1).upper()
            if code not in known_codes:
                continue
            status = (match.group(2) or "").lower()
            if status == "done":
                done_codes.add(code)
            elif not status and is_subject:
                bare_codes.add(code)
    return done_codes, bare_codes


def collect_delivery_signals(commits, known_codes):
    """聚合一批提交的交付信号。返回 (ever_done, bare_codes)。

    ever_done：出现过 `#<短号>=done` 的卡（含后来被显式重开的 —— 重开是显式动作，
    不算滞留）。bare_codes：出现裸 `#<短号>` 的非流程提交（ADMIN_SUBJECT 排除）。
    「已合入 main」这半边判定由调用方决定喂哪段历史（git_log(root, main_ref)）。
    """
    ever_done = set()
    bare = set()
    for _sha, subject, body in commits:
        done_codes, bare_codes = scan_delivery_commit(subject, body, known_codes)
        ever_done |= done_codes
        if not ADMIN_SUBJECT.match(subject):
            bare |= bare_codes
    return ever_done, bare


def collect_delivery_evidence(commits, known_codes):
    """DX-91 结卡证据：非流程提交里引用过的短号集合（subject 或 body 都算）。

    为什么 body 也算：状态标记本来就认 body（DX-31，全历史有提交把 #code=done 写在
    body，其中 7 张卡的派生状态只有那一处）——交付证据若只认 subject，这批合法交付
    会被误拦。闸门要拦的是「零提交引用」的机械假结（DX-79 实录），不是散文引用；
    流程提交（chore(claim)/chore(board)）排除 —— 结卡提交自己写的 #code=done
    不能给自己当证据。
    """
    evidenced = set()
    for _sha, subject, body in commits:
        if ADMIN_SUBJECT.match(subject):
            continue
        for text in (subject, body):
            for match in MARKER.finditer(text):
                code = match.group(1).upper()
                if code in known_codes:
                    evidenced.add(code)
    return evidenced


def close_evidence_problems(candidates, evidenced):
    """DX-91：候选结卡里缺交付证据的短号（纯函数，便于自测）。"""
    return sorted(code for code in candidates if code not in evidenced)


def apply_close_gate(candidates, forced, evidenced):
    """结卡证据闸门的粒度收口（DX-124）—— 纯函数，便于自测。

    返回 (可落盘候选, 被排除的卡)：
      - --close-explicit 名单（forced）在闸门**前**摘除，不经闸门 —— 人工确认通道
        本来就是给「无标记可读」的卡留的（DX-33），证据闸门对它重复设卡会让补结
        永远走不通（DX-116 交付日实测：DX-88 一张无证据卡把 DX-116 的 explicit
        补结连坐拒掉）；
      - 自动候选里缺交付证据的卡被**逐张排除**（点名告警由调用方打），其余照常
        结卡 —— 「拒这一张」不再放大成「整轮不落」：那会把闸门从 fail-closed
        变成一张历史坏卡就能锁死整条结卡通道的活锁。
    """
    forced = set(forced or ())
    auto = {c for c in candidates if c not in forced}
    rejected = set(close_evidence_problems(auto, evidenced))
    return auto - rejected, rejected


def stuck_cards(cards, baseline, ever_done, bare_merged):
    """滞留卡判定（DX-62）—— 纯函数，不碰磁盘。

    交付提交已合入主干（提交信息含裸 #<短号>，非流程提交）但全历史从未出现
    #<短号>=done、baseline 仍未结的卡：--close-cards 永远读不到它的 done 标记，
    这张卡就永久滞留在 todo/doing，且防重复领用的门（baseline=done）永远不关 ——
    任何人都能把已交付的卡再领一遍重做（DX-38/DX-44 实录）。

    返回 [(code, baseline 原值)]，按短号排序。缺 baseline 条目的卡同样算滞留
    （领用门按 todo 处理，等于没关）。
    """
    stuck = []
    for code in sorted(cards):
        current = (baseline.get(code) or "").lower()
        if current == "done" or current == ABANDONED:
            # 弃单是终态（DX-111）：已结案，不该被当成「交付证据缺失的滞留卡」。
            continue
        if code in ever_done:
            continue
        if code in bare_merged:
            stuck.append((code, baseline.get(code) or "(无条目)"))
    return stuck


def resolve_main_ref(root, requested=None):
    """确定 claims 失效判定用的主干参照：显式请求 → origin/main → main。"""
    for ref in ([requested] if requested else []) + ["origin/main", "main"]:
        out = subprocess.run(
            ["git", "-C", root, "cat-file", "-e", ref + "^{commit}"],
            capture_output=True, check=False,
        )
        if out.returncode == 0:
            return ref
    return None


def stale_claim_note(code, branch, claim_commit_in_history):
    """过期登记的二分归因（DX-82，纯函数供 selftest）。

    rebase 是 §7.3 合并前的常规动作：登记 head 指向重写前的 chore(claim) 提交，
    服务端 refs/claims/<短号> 不随 rebase 重写（Ruleset 禁强推）——属预期态，
    领用有效（合并闸门按同主题回退放行），提示里不许再出现走不通的「请重新领用」。
    分支历史里连领用提交都没有 = 改写把它丢了，维持原失效口径。
    返回 (kind, note)：kind 为 "rebase"（预期态，登记继续生效）或 "rewritten"（失效）。
    """
    if claim_commit_in_history:
        return ("rebase",
                "领用 %s 的登记随 rebase 过期（预期），服务端占位为准、合并闸门按同主题回退放行"
                % code)
    return ("rewritten",
            "领用 %s 的分支 %s 已推进或重写，旧登记过期，请重新领用" % (code, branch))


def branch_has_claim_commit(root, ref, code):
    """分支历史里是否仍有该短号的 chore(claim) 领用提交（CLAIM_SUBJECT 同一正则，DX-82）。"""
    out = subprocess.run(
        ["git", "-C", root, "log", ref, "--format=%s"],
        capture_output=True, check=False,
    )
    if out.returncode != 0:
        return False
    want = code.upper()
    for line in out.stdout.decode("utf-8", "replace").splitlines():
        m = CLAIM_SUBJECT.match(line.strip())
        if m and m.group(1).upper() == want:
            return True
    return False


def claims_doing(root, claims, main_ref):
    """台账领用登记 → doing。返回 (active: {code: "doing"}, notes)。

    只认 git 事实，登记不需要「释放」动作。五态判定：
    - claims.branch 的本地 / 远端 ref 都不存在 → 分支已删除（放弃），失效
    - head 不是该分支当前 tip 的祖先 → 分支被 rebase / 改写，按 stale_claim_note 二分：
      历史仍有同短号领用提交 = rebase 预期态，登记继续生效（DX-82）；没有 = 改写丢失，失效
    - head 是主干祖先 → 分支已并入，失效（收尾请用提交标记 #code=done）
    - 其余 → doing 生效
    """
    active = {}
    notes = []

    def ref_tip(ref):
        out = subprocess.run(
            ["git", "-C", root, "rev-parse", "--verify", "--quiet", ref + "^{commit}"],
            capture_output=True, check=False,
        )
        if out.returncode != 0:
            return None
        return out.stdout.decode("utf-8", "replace").strip()

    def is_ancestor(head, ref):
        if not ref:
            return False
        out = subprocess.run(
            ["git", "-C", root, "merge-base", "--is-ancestor", head, ref],
            capture_output=True, check=False,
        )
        return out.returncode == 0

    for code in sorted(claims or {}):
        info = claims[code] or {}
        head = info.get("head", "")
        branch = info.get("branch", "")
        if not head or not branch:
            notes.append("领用 %s 登记不完整（缺 head 或 branch），忽略" % code)
            continue
        ref = "refs/heads/%s" % branch
        if ref_tip(ref) is None:
            ref = "refs/remotes/%s" % branch
        if ref_tip(ref) is None:
            notes.append("领用 %s 的分支 %s 已不存在（放弃或删除），已忽略" % (code, branch))
            continue
        if not is_ancestor(head, ref):
            # head 不是该分支当前 tip 的祖先：rebase / 重写二分（DX-82）
            kind, note = stale_claim_note(code, branch, branch_has_claim_commit(root, ref, code))
            if kind == "rebase":
                # 登记 head 指向重写前的 chore(claim) 提交属预期态：服务端 refs/claims/<短号>
                # 不随 rebase 重写（Ruleset 禁强推），领用本身有效 → doing 继续生效
                active[code] = "doing"
                notes.append(note)
                continue
            notes.append(note)
            continue
        if main_ref and is_ancestor(head, main_ref):
            notes.append("领用 %s 的分支已并入 %s，请用提交标记 #%s=done 收尾（或回台账 baseline）"
                         % (code, main_ref, code))
            continue
        active[code] = "doing"
    return active, notes


def status_of(code, derived, claims_active, baseline):
    # 优先级：提交历史显式标记 > 领用登记 > 台账 baseline > todo
    if code in derived:
        return derived[code]
    if code in claims_active:
        return claims_active[code]
    if code in baseline:
        # 弃单是台账终态，不是看板三态（DX-111）：派生器不维护它的快照项
        #（--check / --write 主循环已整卡跳过）。返回 todo 是为了让跨模块比对
        #（check_board_diff.snapshot_drift_codes）拿到的值与现存快照一致，
        # 不把「台账终态 vs 快照原状」误判成漂移、连坐后续领卡分支的轻档验收。
        # 拒领判据只读 baseline 本身，不经过这里。
        if is_abandoned(baseline, code):
            return "todo"
        return baseline[code]
    return "todo"


def sort_key(item):
    status = item.get("status", "todo")
    return (STATUS_RANK.get(status, 3), status, int(item.get("orderKey", 0) or 0), item.get("id", ""))


def close_cards(cards, baseline, derived, forced=None):
    """结卡判定（DX-07 / DX-33）—— 纯函数，不碰磁盘，便于自测与复用。

    返回 (new_baseline, changed_codes)：
      - git 里最后一次标记是 done 的卡 → baseline 补成 done（结卡）；
      - forced 里的卡 → 按人工确认直接结（DX-33：没写标记但确实做完的卡）；
      - 缺 baseline 条目、git 里也没有 done 标记的卡 → 补 todo（消缺项，领用门按 todo 处理）；
      - 其余一律不动（不把 done 改回 todo —— 结过的卡由人用 #<短号>=todo 显式重开）。
    只改 baseline，不碰 cards 的 id/title/stage。

    forced 的语义边界：它只影响「这张卡的 baseline 该不该变 done」，不影响任何其它判定。
    卡号是否存在由调用方先校验 —— 这里是纯函数，遇到不认识的号会把它当成普通新增条目，
    那会让台账长出没有定义的 baseline 行，所以入口必须挡住。
    """
    forced = set(forced or ())
    out = dict(baseline)
    changed = []
    for code in sorted(cards):
        if is_abandoned(out, code):
            # 弃单终态（DX-111）：--close-cards 不误碰。即便 git 里残留 #done
            # 标记、或 --close-explicit 点名，也不把它改成 done / todo ——
            # 重开是维护者的 chore(board) 授权动作，不是 git 事实的机械投影。
            continue
        mark = (derived.get(code) or "").lower()
        current = (out.get(code) or "").lower()
        if code in forced:
            want = "done"
        elif mark == "done":
            want = "done"
        elif code not in out:
            want = "todo"
        else:
            want = ""
        if want and want != current:
            out[code] = want
            changed.append(code)
    return out, changed


def close_commit_marker(codes):
    """自动结卡提交里用来触发 post-commit 的标记串。

    .githooks/post-commit 用 `grep -Eq '#[A-Za-z][A-Za-z0-9]*'` 判 has_marker；
    DX-07 起 chore(board): 结卡 <短号> 标题里没有 `#`，事件从未投出去，plan-hook
    就永远看不到 done —— 看板留僵尸 doing（DX-41 现场）。这里把codes 拼成
    `#<code>=done` 空格分隔的一串，塞进提交体，钩子就认。
    与 --replay-closed 用同一份拼串，一条实现管两处。
    """
    return " ".join("#%s=done" % c for c in codes)


def replay_marker_text(codes):
    """--replay-closed 事件文本：一段自然语言 + 每个短号一个 `#<code>=done`。

    文本会被喂给 `NeonBoard.exe --emit-hook-file`，plan-hook 按 MARKER_PATTERN 逐个
    提取标记、匹配标题含同一短号的任务、调用 moveTaskToEnd(id, done)。
    已在目标列由插件自己的 skip 规则跳过，因此重放幂等，不怕多张卡一起投。
    """
    if not codes:
        return ""
    return "chore(board): 补投自动结卡标记\n\n" + close_commit_marker(codes)


def find_neonboard_exe(root=None):
    """按 post-commit 同款顺序找 NeonBoard.exe；找不到返回 None。

    与钩子对齐的原因：replay 与 post-commit 是同一条投递链路的两个入口，
    一边找到一边找不到就是行为分裂，宁可写两行相同的候选路径。
    """
    env = os.environ.get("NEONBOARD_EXE")
    if env and os.path.isfile(env):
        return env
    root = root or repo_root()
    for rel in (
        ("build", "Release", "NeonBoard.exe"),
        ("build", "NeonBoard.exe"),
        ("build", "Debug", "NeonBoard.exe"),
        ("build-local", "NeonBoard.exe"),
        ("build-local", "Release", "NeonBoard.exe"),
        ("dist", "NeonBoard", "NeonBoard.exe"),
        ("dist", "NeonBoard.exe"),
    ):
        candidate = os.path.join(root, *rel)
        if os.path.isfile(candidate):
            return candidate
    local = os.environ.get("LOCALAPPDATA")
    if local:
        candidate = os.path.join(local, "Programs", "NeonBoard", "NeonBoard.exe")
        if os.path.isfile(candidate):
            return candidate
    pf = os.environ.get("PROGRAMFILES")
    if pf:
        candidate = os.path.join(pf, "NeonBoard", "NeonBoard.exe")
        if os.path.isfile(candidate):
            return candidate
    return None


def _qt_bin_candidates():
    """Qt bin 目录候选，跟 .githooks/post-commit 里的探测序列一致。

    源码树 build/NeonBoard.exe 是 GUI 子系统、旁边不带 Qt DLL，直接跑就是 0xC0000135
    「找不到 Qt6Core.dll」。post-commit 一直靠给 PATH 前置 Qt bin 兜住；
    replay 若沿用同一份候选清单却不做这一步，等于「同一个坑两次踩」。
    """
    q = os.environ.get("NEONBOARD_QT_BIN")
    if q:
        yield q
    import glob
    for pat in ("C:/Qt/*/msvc2022_64/bin", "C:/Qt/*/mingw*/bin", "C:/Qt/*/gcc_64/bin"):
        for p in sorted(glob.glob(pat)):
            if os.path.isfile(os.path.join(p, "Qt6Core.dll")):
                yield p


def _run_emit_default(argv):
    """默认 emit：给子进程 PATH 前置 exe 目录 + Qt bin，让源码树 exe 也能起。"""
    env = os.environ.copy()
    exe_dir = os.path.dirname(os.path.abspath(argv[0])) if argv else ""
    extra = [p for p in [exe_dir] + list(_qt_bin_candidates()) if p]
    if extra:
        env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
    return subprocess.call(argv, env=env)


def replay_closed(codes, exe, emit=None):
    """把给定短号的 `#<code>=done` 标记投一次事件，走 NeonBoard.exe --emit-hook-file。

    - codes 为空 → 什么都不做，返回 (0, 0)；
    - exe 为空 → 返回 (2, 0) 并打印待投短号，不静默成功（否则维护者以为已清僵尸）；
    - emit 注入用于自测，默认走 _run_emit_default（会补 PATH，见该函数注释）。

    返回 (退出码, 投递的卡数)。这里不判看板状态：派生器不该伸手进 board.db，
    消费是插件与主实例的事，我们的职责止于「把标记原样投进 hooks.db」。
    调用方若需要按环境自动找 exe，自己先跑 find_neonboard_exe() 再把结果传进来。
    """
    if not codes:
        sys.stderr.write("[replay] 台账里没有 baseline=done 的卡（或 --replay-codes 全滤空），无事可做\n")
        return 0, 0
    if not exe:
        sys.stderr.write(
            "[replay] 未找到 NeonBoard.exe，事件未投递。设置 NEONBOARD_EXE 指向 exe 后重跑。\n"
            "[replay] 将要投递的短号：%s\n" % " ".join(codes))
        return 2, 0
    text = replay_marker_text(codes)
    runner = emit if emit is not None else _run_emit_default
    tmp = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".txt", delete=False) as fh:
            fh.write(text)
            tmp = fh.name
        rc = int(runner([exe, "--emit-hook-file", tmp]))
        if rc != 0:
            sys.stderr.write("[replay] NeonBoard.exe 退出码 %d（投递失败）\n" % rc)
            return rc, 0
        print("[replay] 已投递 %d 张卡的 done 标记：%s" % (len(codes), " ".join(codes)))
        return 0, len(codes)
    finally:
        if tmp:
            try:
                os.unlink(tmp)
            except OSError:
                pass


def read_ledger(root, ledger_rel, ledger_ref=None):
    """读台账：给了 ledger_ref 就从该 git 引用读（如 origin/main），否则读工作区文件。

    领用门与 claim-card 的重开门同口径：判定「卡是不是已交付」只认主线那份台账，
    工作区副本可能停在旧状态。失败返回 None 并已写 stderr。
    """
    if ledger_ref:
        # git show 的 rev:path 语法只认正斜杠：Windows 上 --ledger 默认值经 os.path.join
        # 拼出反斜杠，git 会报「exists on disk, but not in '<ref>'」—— 统一归一。
        out = subprocess.run(
            ["git", "-C", root, "show", "%s:%s" % (ledger_ref, ledger_rel.replace(os.sep, "/"))],
            capture_output=True, check=False,
        )
        if out.returncode != 0:
            sys.stderr.write("从 %s 读台账失败：%s\n"
                             % (ledger_ref, out.stderr.decode("utf-8", "replace")))
            return None
        try:
            return json.loads(out.stdout.decode("utf-8"))
        except ValueError as exc:
            sys.stderr.write("%s 里的台账不是合法 JSON：%s\n" % (ledger_ref, exc))
            return None
    path = os.path.join(root, ledger_rel)
    if not os.path.isfile(path):
        sys.stderr.write("找不到台账文件：%s\n" % path)
        return None
    return read_json(path)


def extract_contract(ledger, code):
    """从台账 dict 投影出一张卡的任务契约（DX-94）。纯函数：读哪一份台账由调用方
    （read_ledger）决定，这里只做字段投影，不碰文件、不碰 git —— 让「数据源」这一
    决定只有一个入口。缺字段回落空列表，未知卡返回 None。
    """
    code = (code or "").strip().lstrip("#").upper()
    cards = ledger.get("cards") or {}
    card = cards.get(code)
    if not isinstance(card, dict):
        return None
    return {
        "code": code,
        "scope": list(card.get("scope") or []),
        "acceptance": list(card.get("acceptance") or []),
        "depends": list(card.get("depends") or []),
    }


def selftest():
    """--close-cards 的可执行自测（DX-07 验收项）：四种情形一次跑完。"""
    cards = {"A-1": {}, "B-2": {}, "C-3": {}, "D-4": {}}
    cases = []

    # ① 有 done 标记且 baseline 是 todo → 结卡
    base = {"A-1": "todo", "B-2": "todo", "C-3": "done"}
    derived = {"A-1": "done", "B-2": "doing", "C-3": "done"}
    out, changed = close_cards(cards, base, derived)
    cases.append(("有 #done 标记 → baseline 补 done", out.get("A-1") == "done" and "A-1" in changed))

    # ② 无 done 标记 → 不动（doing 不能被当成 done，也不能被改回 todo）
    cases.append(("无 #done 标记 → baseline 保持 todo", out.get("B-2") == "todo" and "B-2" not in changed))

    # ③ 已经 done → 不重复结（幂等）
    cases.append(("已 done → 不重复改动", out.get("C-3") == "done" and "C-3" not in changed))

    # ④ 缺 baseline 条目 → 补 todo（消缺项，且绝不误结为 done）
    cases.append(("缺条目且无标记 → 补 todo 而非 done", out.get("D-4") == "todo"
                  and "D-4" in changed))

    # ⑤ 幂等：对结果再跑一次应无变化
    out2, changed2 = close_cards(cards, out, derived)
    cases.append(("对结果再跑一次 → 无变化", out2 == out and changed2 == []))

    # ⑥ 反向保护：done 的卡即便 git 里没标记也不得被动回 todo
    base_done = {"C-3": "done"}
    out3, _ = close_cards({"C-3": {}}, base_done, {})
    cases.append(("已 done 但无标记 → 保持 done（不擅自重开）", out3.get("C-3") == "done"))

    # ⑦ 标记分类（DX-31）：正文里的技术散文不该被当成「写错的卡号」报出来
    known = {"A-1", "DX-31"}
    prose = ("C++ 预处理示例 #IFDEF / #INCLUDE / #IFNDEF，颜色 #RGB / #RRGGBB，\n"
             "占位符 #R7-0X、#ERROR、#R，已知卡 #A-1=done，写错 #R99-98=done")
    d, u, w = scan_markers("feat(theme): 主题包格式 #DX-31=done", prose, known)
    cases.append(("body 里的宏 / 颜色 / 占位符不进 warnings", w == set()))
    cases.append(("body 里的已知卡状态仍派生", d.get("A-1") == "done"))
    cases.append(("body 里写错的路线图短号仍进 unknown", "R99-98" in u))
    cases.append(("subject 里的已知卡状态仍派生", d.get("DX-31") == "done"))

    d2, u2, w2 = scan_markers("chore: plan-hook 用 #T12；写错 #R99-99", "", known)
    cases.append(("subject 里形似短号的 #T12 仍提示", "T12" in w2))
    cases.append(("subject 里写错的 #R99-99 仍进 unknown", "R99-99" in u2))

    # ⑧ 人工确认结卡（DX-33）：没写标记但确实做完的卡，靠显式声明结，不是靠放宽推导
    base8 = {"A-1": "todo", "B-2": "todo"}
    doing8 = {"A-1": "doing", "B-2": "doing"}
    out8, changed8 = close_cards({"A-1": {}, "B-2": {}}, base8, doing8, {"A-1"})
    cases.append(("--close-explicit 指定的卡 → baseline 补 done",
                  out8.get("A-1") == "done" and "A-1" in changed8))
    cases.append(("--close-explicit 不波及未指定的卡",
                  out8.get("B-2") == "todo" and "B-2" not in changed8))
    cases.append(("--close-explicit 不改变「无标记不结」的默认口径",
                  close_cards({"B-2": {}}, {"B-2": "todo"}, doing8)[0].get("B-2") == "todo"))

    # ⑨ 人工确认对已结卡幂等
    out9, changed9 = close_cards({"A-1": {}}, {"A-1": "done"}, {}, {"A-1"})
    cases.append(("已 done 的卡再被人工确认 → 无变化",
                  out9.get("A-1") == "done" and changed9 == []))

    # ⑪ 弃单终态（DX-111）：--close-cards 不误碰、--check 不报漂移、滞留探测不点名
    base_ab = {"A-1": "todo", "B-2": ABANDONED, "C-3": "done"}
    out_ab, changed_ab = close_cards(cards, base_ab,
                                     {"B-2": "done", "A-1": "done"}, {"B-2"})
    cases.append(("弃单卡即便有 #done 标记也不被 --close-cards 误结",
                  out_ab.get("B-2") == ABANDONED and "B-2" not in changed_ab))
    cases.append(("弃单卡即便 --close-explicit 点名也不动",
                  out_ab.get("B-2") == ABANDONED))
    cases.append(("弃单不波及同批其它卡正常结卡",
                  out_ab.get("A-1") == "done" and "A-1" in changed_ab))
    cases.append(("弃单终态不算滞留卡",
                  ("B-2", ABANDONED) not in stuck_cards(cards, base_ab, set(), {"B-2"})))
    cases.append(("is_abandoned 大小写不敏感",
                  is_abandoned({"X-1": "Abandoned"}, "X-1")
                  and not is_abandoned({"X-1": "todo"}, "X-1")))
    cases.append(("status_of：弃单终态返回 todo（跨模块比对不误判漂移），done 不受影响",
                  status_of("X-1", {}, {}, {"X-1": ABANDONED}) == "todo"
                  and status_of("X-2", {}, {}, {"X-2": "todo"}) == "todo"
                  and status_of("X-3", {}, {}, {"X-3": "done"}) == "done"))

    # ⑩ 结卡提交标记（DX-41）：拼串里每个短号都带 # 与 =done，
    #    post-commit 的 grep 与 plan-hook 的 MARKER_PATTERN 才认。
    text = close_commit_marker(["R9-04", "DX-26", "R16-05"])
    cases.append(("结卡标记串每个短号都带 #", text.count("#") == 3))
    cases.append(("结卡标记串每个短号都写 =done", text.count("=done") == 3))
    parsed = {m.group(1).upper(): m.group(2) for m in MARKER.finditer(text)}
    cases.append(("结卡标记串可被 MARKER 完整解析",
                  parsed == {"R9-04": "done", "DX-26": "done", "R16-05": "done"}))

    # ⑬ 结卡证据闸门粒度（DX-124）：拒这一张，不再放大成整轮不落
    gate_ev = {"DX-116"}
    ok, bad = apply_close_gate({"DX-116", "DX-88"}, set(), gate_ev)
    cases.append(("闸门只排无证据卡：有证据的照结、无证据的逐张点名",
                  ok == {"DX-116"} and bad == {"DX-88"}))
    ok2, bad2 = apply_close_gate({"DX-116", "DX-88"}, {"DX-88"}, gate_ev)
    cases.append(("explicit 名单不经闸门：无证据的人工确认卡仍放行",
                  ok2 == {"DX-116"} and bad2 == set()))
    ok3, bad3 = apply_close_gate(set(), set(), gate_ev)
    cases.append(("空候选零误伤", ok3 == set() and bad3 == set()))

    # ⑫ 过期登记二分（DX-82）：rebase 预期态不再喊重新领用；改写丢失维持原口径
    kind, note = stale_claim_note("DX-82", "fix/x", True)
    cases.append(("rebase 过期归因为预期态（提示含服务端占位口径，不再喊重新领用）",
                  kind == "rebase" and "随 rebase 过期" in note
                  and "服务端占位为准" in note and "请重新领用" not in note))
    kind2, note2 = stale_claim_note("DX-82", "fix/x", False)
    cases.append(("改写丢失维持「请重新领用」原口径（反例不破）",
                  kind2 == "rewritten" and "请重新领用" in note2))

    # ⑪ replay 文本（DX-41）：codes 空 → 空串；非空 → 一段说明 + 标记串
    cases.append(("replay 空 codes → 空文本", replay_marker_text([]) == ""))
    rtext = replay_marker_text(["A-1", "B-2"])
    rparse = {m.group(1).upper(): m.group(2) for m in MARKER.finditer(rtext)}
    cases.append(("replay 文本携带所有传入短号的 done 标记",
                  rparse.get("A-1") == "done" and rparse.get("B-2") == "done"))

    # ⑫ replay_closed 走注入 emit，不碰真实 exe；空/失败/成功三条路径
    def _fake_emit_ok(argv):
        _fake_emit_ok.calls.append(argv)
        return 0
    _fake_emit_ok.calls = []
    rc, n = replay_closed(["A-1"], exe="fake.exe", emit=_fake_emit_ok)
    cases.append(("replay_closed 非空 codes → 调用 emit 一次并返回 0",
                  rc == 0 and n == 1 and len(_fake_emit_ok.calls) == 1
                  and _fake_emit_ok.calls[0][1] == "--emit-hook-file"))
    rc0, n0 = replay_closed([], exe="fake.exe", emit=_fake_emit_ok)
    cases.append(("replay_closed 空 codes → 不调用 emit，返回 0/0",
                  rc0 == 0 and n0 == 0))
    rc2, n2 = replay_closed(["A-1"], exe=None, emit=_fake_emit_ok)
    cases.append(("replay_closed 缺 exe → 返回 2，未消费 codes",
                  rc2 == 2 and n2 == 0 and len(_fake_emit_ok.calls) == 1))

    # ⑬ 默认 emit 会给子进程 PATH 前置 exe 目录 + Qt bin（治 0xC0000135 缺 Qt6Core.dll）
    saved_qt = os.environ.get("NEONBOARD_QT_BIN")
    os.environ["NEONBOARD_QT_BIN"] = "C:/Qt/fake/bin"
    try:
        cands = list(_qt_bin_candidates())
        cases.append(("_qt_bin_candidates 首条尊重 NEONBOARD_QT_BIN",
                      cands and cands[0] == "C:/Qt/fake/bin"))
    finally:
        if saved_qt is None:
            del os.environ["NEONBOARD_QT_BIN"]
        else:
            os.environ["NEONBOARD_QT_BIN"] = saved_qt

    captured = {}

    def _fake_call(argv, env=None):
        captured["argv"] = argv
        captured["env"] = env
        return 0
    fake_exe = os.path.abspath(os.path.join("fake_dir", "NeonBoard.exe"))
    saved_call = subprocess.call
    subprocess.call = _fake_call  # type: ignore[attr-defined]
    try:
        rc3, n3 = replay_closed(["A-1"], exe=fake_exe)
    finally:
        subprocess.call = saved_call  # type: ignore[attr-defined]
    new_path = (captured.get("env") or {}).get("PATH", "")
    head = new_path.split(os.path.pathsep)[0] if new_path else ""
    cases.append(("replay_closed 默认走 _run_emit_default，前置 exe 目录到 PATH",
                  rc3 == 0 and n3 == 1 and head == os.path.dirname(fake_exe)))

    # ⑭ 滞留卡判定（DX-62）：复刻 DX-38/DX-44 的真实历史形态 —— 交付提交带裸 #短号、
    #    领用/立卡流程提交也带裸 #短号、全历史无 #=done。点名交付、排除流程痕迹。
    known14 = {"DX-38", "DX-44", "DX-62"}
    hist14 = [
        ("a", "chore(claim): 领取 #DX-38 实施任务", ""),
        ("b", "chore(claim): 登记 #DX-38 领用到台账", ""),
        ("c", "chore(board): 补 DX-38 scope 清单路径 #DX-38", ""),
        ("d", "fix(update): 增量路径清理弃用文件并写安装回执 #DX-38", ""),
        ("e", "fix(ci): board 校验只认 main tip 的 run #DX-44",
         "pitfalls 里引用了 #DX-62 的现场记录（body 交叉引用不是交付）"),
    ]
    ever14, bare14 = collect_delivery_signals(hist14, known14)
    cases.append(("滞留判定：裸标记只认 subject，流程提交与 body 交叉引用都排除",
                  bare14 == {"DX-38", "DX-44"}))
    cases.append(("滞留判定：全历史无 #=done → ever_done 为空", ever14 == set()))
    s14 = stuck_cards({"DX-38": {}, "DX-44": {}, "DX-62": {}},
                      {"DX-38": "todo", "DX-44": "todo", "DX-62": "doing"},
                      ever14, bare14)
    cases.append(("滞留判定：交付已合入但无 done 标记 → 点名滞留（doing 也算）",
                  s14 == [("DX-38", "todo"), ("DX-44", "todo")]))

    # ⑮ 滞留判定的反例：补结 / 重开 / 未实施都不该被点名
    cases.append(("滞留判定：baseline 已 done（人工补结后）→ 不再点名",
                  stuck_cards({"DX-38": {}}, {"DX-38": "done"}, set(), {"DX-38"}) == []))
    cases.append(("滞留判定：全历史有过 #=done（哪怕后来重开）→ 不点名",
                  stuck_cards({"A-1": {}}, {"A-1": "todo"}, {"A-1"}, {"A-1"}) == []))
    cases.append(("滞留判定：没有裸交付标记（纯立卡未实施）→ 不点名",
                  stuck_cards({"A-1": {}}, {"A-1": "todo"}, set(), set()) == []))
    cases.append(("滞留判定：缺 baseline 条目且有裸交付标记 → 点名并记 (无条目)",
                  stuck_cards({"A-1": {}}, {}, set(), {"A-1"}) == [("A-1", "(无条目)")]))

    # ⑯ 单条提交的交付信号：状态标记与裸标记分开收
    d16, b16 = scan_delivery_commit("fix(x): 修复 #A-1", "回滚了 #A-1=done 的部分", {"A-1"})
    cases.append(("交付信号：subject 裸标记与 body done 标记都收",
                  d16 == {"A-1"} and b16 == {"A-1"}))
    d17, b17 = scan_delivery_commit("chore(x): 重开 #A-1=todo", "", {"A-1"})
    cases.append(("交付信号：=todo 既不是 done 也不是裸标记",
                  d17 == set() and b17 == set()))

    # ⑰ 结卡证据闸门（DX-91）：复刻 DX-79 虚假结卡实录 —— 全历史只有立卡 / 结卡
    #    两类流程提交引用了短号，非流程实现提交为零，不得结。
    known17 = {"DX-79", "DX-76", "DX-90"}
    hist17 = [
        ("a", "chore(board): 立卡 DX-79 DX-90（nbflow 竞态改走宪章代登记）",
         "授权：维护者会话指令\n批次：2026-11-06 过程立卡批次\nDX-79：孤儿占位三不管"),
        ("b", "fix(gate): 巡检判据进领用闸门 #DX-76=done",
         "pitfalls 2.24 收录 #DX-79 的现场记录（body 交叉引用算证据，见 collect_delivery_evidence）"),
        ("c", "chore(board): 结卡 #DX-79=done #DX-90=done", ""),
    ]
    ev17 = collect_delivery_evidence(hist17, known17)
    cases.append(("证据：非流程实现提交的 subject 引用算证据",
                  "DX-76" in ev17))
    cases.append(("证据：body 交叉引用算证据（DX-31 口径，body 标记是合法交付位）",
                  "DX-79" in ev17))
    cases.append(("证据：只有流程提交引用 → 不算证据（DX-79 立卡/结卡双双排除）",
                  "DX-90" not in ev17))
    problems = close_evidence_problems({"DX-79", "DX-90"}, ev17)
    cases.append(("闸门：候选缺证据 → 点名拒绝", problems == ["DX-90"]))
    cases.append(("闸门：候选有证据 → 放行",
                  close_evidence_problems({"DX-76"}, ev17) == []))
    cases.append(("闸门：全流程引用的假结（复刻 DX-79 原始形态）→ 拒",
                  close_evidence_problems({"DX-90"}, set()) == ["DX-90"]))

    # ⑱ 契约注入数据源（DX-94）：--contract 只认 git 引用里的台账，工作区文件分叉不影响
    real = {"cards": {"DX-94": {"scope": ["scripts/a.ps1"],
                                "acceptance": ["验收一"], "depends": ["DX-1"]}}}
    stale = {"cards": {"DX-94": {"scope": ["SENTINEL/STALE"],
                                 "acceptance": ["SENTINEL"], "depends": ["SENTINEL"]}}}
    cases.append(("契约投影：字段逐项带出", extract_contract(real, "dx-94") == {
        "code": "DX-94", "scope": ["scripts/a.ps1"],
        "acceptance": ["验收一"], "depends": ["DX-1"]}))
    cases.append(("契约投影：# 前缀与大小写归一",
                  extract_contract(real, "#dx-94")["code"] == "DX-94"))
    cases.append(("契约投影：未知卡返回 None", extract_contract(real, "DX-999") is None))
    cases.append(("契约投影：缺字段回落空列表",
                  extract_contract({"cards": {"A-1": {}}}, "A-1") == {
                      "code": "A-1", "scope": [], "acceptance": [], "depends": []}))
    try:
        with tempfile.TemporaryDirectory() as td:
            rel = os.path.join("docs", "board-ledger.json")
            full = os.path.join(td, rel)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "w", encoding="utf-8") as fh:
                json.dump(real, fh, ensure_ascii=False)

            def _git(*argv):
                return subprocess.run(
                    ["git", "-C", td, "-c", "commit.gpgsign=false", *argv],
                    capture_output=True, check=True,
                    cwd=td, env=dict(os.environ))

            _git("init", "-q")
            _git("add", rel.replace(os.sep, "/"))
            _git("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "init")
            _git("update-ref", "refs/heads/mainline", "HEAD")
            # 工作区文件被改成分叉的陈旧版本（模拟锚点本地台账），ref 仍是真实契约
            with open(full, "w", encoding="utf-8") as fh:
                json.dump(stale, fh, ensure_ascii=False)
            from_ref = read_ledger(td, rel, "refs/heads/mainline")
            from_local = read_ledger(td, rel)
            cases.append(("契约源：分叉时 --ledger-ref 读 ref 而非工作区文件",
                          from_ref == real and from_local == stale))
            cases.append(("契约源：ref 版本注入的契约不含 SENTINEL",
                          extract_contract(from_ref or {}, "DX-94")["scope"] == ["scripts/a.ps1"]))
            cases.append(("契约源：工作区版本确实带 SENTINEL（反例不破）",
                          extract_contract(from_local or {}, "DX-94")["scope"] == ["SENTINEL/STALE"]))
    except (OSError, subprocess.CalledProcessError) as exc:
        cases.append(("契约源：临时 git 仓库分叉实验可执行", False))
        sys.stderr.write("[派生器] 契约源实验异常：%s\n" % exc)

    # ⑮ 排位档位（DX-123）：新卡按 tier 落点、漂移被 check 点名、未声明豁免
    tier_items = [
        {"id": "a", "code": "A-1", "status": "todo", "orderKey": 13312},
        {"id": "b", "code": "B-2", "status": "todo", "orderKey": 27648},
        {"id": "c", "code": "C-3", "status": "todo", "orderKey": 51200},
        {"id": "d", "code": "D-4", "status": "done", "orderKey": 1024},
    ]
    tier_cards = {"A-1": {"tier": "P0"}, "B-2": {"tier": "P1"},
                  "C-3": {"tier": "P2"}, "D-4": {"tier": "P0"}}
    groups = bp.by_tier(tier_items, tier_cards)
    cases.append(("by_tier 只收 todo 且已声明档位",
                  groups == {0: [13312], 1: [27648], 2: [51200]}))
    cases.append(("insertion_order 取本档末张与下一档首张中点",
                  bp.insertion_order("P1", groups) == (27648 + 51200) // 2))
    drift_items = [
        {"id": "a", "code": "A-1", "status": "todo", "orderKey": 13312},
        {"id": "c", "code": "C-3", "status": "todo", "orderKey": 27648},
        {"id": "b", "code": "B-2", "status": "todo", "orderKey": 51200},
    ]
    bad = bp.check_monotonic(drift_items, tier_cards)
    cases.append(("档位倒序被点名", len(bad) == 1 and bad[0][0] == "B-2"))

    failed = [name for name, ok in cases if not ok]
    for name, ok in cases:
        print("  %s %s" % ("✓" if ok else "✗", name))
    if failed:
        sys.stderr.write("[派生器] 结卡自测失败 %d/%d 例\n" % (len(failed), len(cases)))
        return 1
    print("[派生器] 结卡自测通过：%d 例" % len(cases))
    return 0


def main():
    # Windows 控制台默认是 cp1252，打印中文会直接抛 UnicodeEncodeError，
    # CI 里正是这样崩的 —— 强制把标准输出切到 UTF-8。
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    parser = argparse.ArgumentParser(description="看板状态派生器")
    parser.add_argument("--repo", default=repo_root(), help="仓库根目录")
    parser.add_argument("--snapshot", default="progress.json", help="快照相对路径")
    parser.add_argument("--ledger", default=os.path.join("docs", "board-ledger.json"), help="台账相对路径")
    parser.add_argument(
        "--ledger-ref", default=None,
        help="从指定 git 引用读台账（如 origin/main），优先于 --ledger 文件；"
             "领用前告警只认主线那份台账（与 claim-card 重开门同口径，DX-62）",
    )
    # 互斥组改为非必填：--selftest 不读仓库，允许单独使用；
    # 缺少实际 mode 时由下方显式报错（保持原有「必须选一个模式」的口径）。
    mode = parser.add_mutually_exclusive_group(required=False)
    mode.add_argument("--check", action="store_true", help="只校验，不写文件")
    mode.add_argument("--write", action="store_true", help="把派生结果写回快照")
    mode.add_argument(
        "--baseline-check", action="store_true",
        help="只报台账 baseline 与 git 交付标记的漂移，不写文件、不阻断（baseline 归维护者）",
    )
    mode.add_argument(
        "--stuck-codes", action="store_true",
        help="只输出滞留卡短号（一行一个）：交付提交已合入主干但全历史无 #<短号>=done（DX-62）；"
             "供 agent-init 领用前告警，配 --ledger-ref 读主线台账",
    )
    mode.add_argument(
        "--abandoned-codes", action="store_true",
        help="只输出弃单终态卡短号（一行一个，baseline=abandoned，DX-111）；"
             "供 agent-init 领用前拒领，配 --ledger-ref 读主线台账",
    )
    mode.add_argument(
        "--close-cards", action="store_true",
        help="结卡（DX-07）：把 git 里已有 #<短号>=done 的卡的 baseline 补成 done；"
             "写回台账的 baseline 字段，输出实际结掉的短号，一行一个",
    )
    mode.add_argument(
        "--contract", default="", metavar="CODE",
        help="输出该卡的任务契约（scope/acceptance/depends）到 --out 文件（DX-94）；"
             "台账只认 --ledger-ref（默认 origin/main），与领用重开门判据同源",
    )
    parser.add_argument(
        "--out", default="",
        help="配合 --contract：契约 JSON 落盘路径（agent-init 按字节读，不经管道）",
    )
    parser.add_argument(
        "--close-explicit", default="",
        help="配合 --close-cards：额外把这些短号按人工确认直接结掉（逗号分隔）。"
             "走同一条只改 baseline 的通道，不给它扩权到改 cards",
    )
    mode.add_argument(
        "--emit-claimed", action="store_true",
        help="只输出 --base..--head 范围内领用过的短号（一行一个），供 git-done 结单对账",
    )
    mode.add_argument(
        "--replay-closed", action="store_true",
        help="把台账 baseline=done 的卡重投一次 #<code>=done hook 事件（DX-41）。"
             "用于修复合入前遗留的僵尸 doing 卡；已在目标列的会被插件 skip。",
    )
    parser.add_argument(
        "--replay-codes", default="",
        help="配合 --replay-closed：只投这串短号（逗号分隔），默认全表 baseline=done。",
    )
    parser.add_argument(
        "--base", default="origin/main",
        help="--emit-claimed 的范围起点（默认 origin/main）",
    )
    parser.add_argument(
        "--head", default="HEAD",
        help="--emit-claimed 的范围终点（默认 HEAD）",
    )
    parser.add_argument(
        "--main-ref", default=None,
        help="claims 失效判定用的主干引用（默认依次尝试 origin/main、main）",
    )
    parser.add_argument(
        "--selftest", action="store_true",
        help="只跑结卡判定的自测（不读仓库、不需要快照）；与各 mode 互不影响",
    )
    args = parser.parse_args()

    # 自测不依赖任何文件，先于 mode 校验与磁盘读取执行
    if args.selftest:
        return selftest()

    if not any((args.check, args.write, args.baseline_check, args.close_cards,
                args.emit_claimed, args.replay_closed, args.stuck_codes,
                args.abandoned_codes, bool(args.contract))):
        parser.error("必须指定 --check / --write / --baseline-check / --close-cards / "
                     "--emit-claimed / --replay-closed / --stuck-codes / "
                     "--abandoned-codes / --contract 之一")

    root = os.path.abspath(args.repo)

    if args.contract:
        # 契约注入的唯一数据源（DX-94）：与领用重开门判据同读 origin/main 台账。
        # 默认锁 origin/main，绝不回落到工作区文件 —— 锚点检出台账是共享陈旧投影。
        if not args.out:
            parser.error("--contract 需要同时指定 --out")
        ledger = read_ledger(root, args.ledger, args.ledger_ref or "origin/main")
        if ledger is None:
            return 2
        contract = extract_contract(ledger, args.contract)
        if contract is None:
            sys.stderr.write("卡片 %s 不在 %s 台账中\n"
                             % (args.contract.strip().lstrip("#").upper(),
                                args.ledger_ref or "origin/main"))
            return 2
        write_json_atomic(args.out, contract)
        return 0

    if args.emit_claimed:
        # 不需要快照也不需要台账：纯 git 查询，先于文件检查返回。
        codes = emit_claimed(root, args.base, args.head)
        if codes is None:
            return 2
        for code in codes:
            print(code)
        return 0

    ledger_path = os.path.join(root, args.ledger)

    if args.replay_closed:
        # replay 只读台账 baseline，不碰快照 —— 快照可能正被看板应用写，读它是竞争。
        if not os.path.isfile(ledger_path):
            sys.stderr.write("找不到台账文件：%s\n" % ledger_path)
            return 2
        ledger = read_json(ledger_path)
        baseline = ledger.get("baseline") or {}
        cards = ledger.get("cards") or {}
        if args.replay_codes:
            requested = [c.strip().upper() for c in args.replay_codes.split(",") if c.strip()]
            unknown = [c for c in requested if c not in cards]
            if unknown:
                sys.stderr.write("[replay] --replay-codes 里这些短号不在台账：%s\n"
                                 % "、".join(unknown))
                return 2
            closed = [c for c in requested]
        else:
            closed = sorted(c for c, v in baseline.items()
                            if (v or "").lower() == "done")
        rc, _count = replay_closed(closed, exe=find_neonboard_exe(root))
        return rc

    if args.stuck_codes:
        # 滞留卡机器可读出口（DX-62）：与 --baseline-check 的滞留项同一套判定函数
        # （stuck_cards，单一实现），只是输出格式不同 —— 一行一个短号，供
        # agent-init 领用前告警解析。不依赖快照：领用发生在开区时刻，快照还没有。
        ledger = read_ledger(root, args.ledger, args.ledger_ref)
        if ledger is None:
            return 2
        cards = ledger.get("cards") or {}
        baseline = ledger.get("baseline") or {}
        if not cards:
            sys.stderr.write("台账里没有任何卡片\n")
            return 2
        known = set(cards.keys())
        main_ref = resolve_main_ref(root, args.main_ref)
        ever_done, _ = collect_delivery_signals(git_log(root), known)
        bare_merged = set()
        if main_ref:
            _, bare_merged = collect_delivery_signals(git_log(root, main_ref), known)
        for code, _current in stuck_cards(cards, baseline, ever_done, bare_merged):
            print(code)
        return 0

    if args.abandoned_codes:
        # 弃单终态机器可读出口（DX-111）：与 --stuck-codes 同源，只认主线台账，
        # 供 agent-init 在开区前判「这张卡是否已被结案为弃单」。不依赖快照。
        ledger = read_ledger(root, args.ledger, args.ledger_ref)
        if ledger is None:
            return 2
        cards = ledger.get("cards") or {}
        baseline = ledger.get("baseline") or {}
        for code in sorted(cards):
            if is_abandoned(baseline, code):
                print(code)
        return 0

    snapshot_path = os.path.join(root, args.snapshot)

    if not os.path.isfile(snapshot_path):
        sys.stderr.write("找不到快照文件：%s\n" % snapshot_path)
        return 2
    if not os.path.isfile(ledger_path):
        sys.stderr.write("找不到台账文件：%s\n" % ledger_path)
        return 2

    ledger = read_json(ledger_path)
    cards = ledger.get("cards") or {}
    baseline = ledger.get("baseline") or {}
    if not cards:
        sys.stderr.write("台账里没有任何卡片：%s\n" % ledger_path)
        return 2

    snapshot = read_json(snapshot_path)
    items = list(snapshot.get("items") or [])
    version = int(snapshot.get("version", 1))
    if version < 2:
        sys.stderr.write("快照版本过旧（v%d），无法派生\n" % version)
        return 2

    derived, unknown, warnings = derive_status(root, set(cards.keys()))

    if args.close_cards:
        # 结卡自动化（DX-07）：baseline=<短号> done 是「拒绝重新领用」的唯一门（条款 6）。
        # 过去靠维护者记得批量结 —— 一定漂，每漂一次已交付的卡就能被再领一遍重做
        # （pitfalls 1.14 踩过；7.1 记作「待拍板」）。现在由 git 事实直接结：
        # 历史里最后一次 #<短号> 标记是 done 的卡，baseline 补成 done。
        #
        # 只动 baseline，不碰 cards（id/title/stage）——与 agent 的合法写范围一致；
        # 缺 baseline 条目的卡按 git 标记补 done 或 todo，消除「有卡无条目」的缺项。
        # 输出被改动的短号（一行一个），调用方据此决定是否产生提交；无输出 = 无变化。
        #
        # --close-explicit（DX-33）：人工确认结卡。derive_board_status 的本职是
        # 「从 git 事实推导」，人工确认不属于事实，所以它必须显式传参、且只走同一条
        # 只改 baseline 的通道 —— 不给它新增 / 删除卡的能力，也不改判定规则。
        forced = set()
        if args.close_explicit:
            for raw in args.close_explicit.split(","):
                code = raw.strip().upper()
                if not code:
                    continue
                if code not in cards:
                    sys.stderr.write(
                        "[结卡] --close-explicit 指定的 %s 不在台账卡片里（先核对卡号）\n" % code)
                    return 2
                forced.add(code)

        changed = []
        # DX-91 结卡证据闸门：#短号=done 标记谁都能写（写标记与验卡是同一人），
        # DX-79 虚假结卡实录 —— baseline=done 而全主线无非流程提交引用过它，acceptance
        # 一项都没交付。在唯一写者处 fail-closed：候选结卡的短号必须至少被一条非流程
        # 提交引用过（subject/body 均算，口径见 collect_delivery_evidence）。
        # DX-124 粒度收口：闸门只拒「无证据的那几张」（逐张排除、点名告警、维持现状
        # baseline），其余照常结卡、整体退出 0 —— 曾经的「整轮拒绝落盘（退出码 2）」
        # 被 DX-88 一张无证据卡锁成活锁（每轮退 2、所有交付卡结不上，DX-116 连坐实证）。
        # --close-explicit（DX-33）不经此门：人工确认通道本来就是给「无标记可读」的卡
        # 留的，证据闸门对它重复设卡会让补结永远走不通。
        candidates = {
            code for code in cards
            if ((derived.get(code) or "").lower() == "done"
                and (baseline.get(code) or "").lower() != "done")
        }
        evidenced = collect_delivery_evidence(git_log(root), set(cards.keys()))
        _, unevidenced = apply_close_gate(candidates, forced, evidenced)
        for code in sorted(unevidenced):
            sys.stderr.write(
                "[结卡] %s：主线无非流程提交引用 #%s（交付证据缺失），本轮排除不结 —— "
                "实现提交需按约定引用短号；其余候选照常落盘\n"
                % (code, code))
        # 被排除的卡从 derived 里摘掉，close_cards 就读不到它的 done 标记 ——
        # 维持现状 baseline；forced 卡走人工确认通道，不受摘除影响。
        baseline_out = close_cards(cards, baseline,
                                   {k: v for k, v in derived.items() if k not in unevidenced},
                                   forced)[0]
        for code in sorted(cards):
            before = (baseline.get(code) or "").lower()
            after = (baseline_out.get(code) or "").lower()
            if after != before:
                changed.append(code)
        for code in sorted(forced):
            if code in changed:
                sys.stderr.write("[结卡] %s：按 --close-explicit 人工确认结卡（无 git 标记）\n" % code)
            else:
                sys.stderr.write("[结卡] %s：已由 git 标记结过，--close-explicit 未产生额外改动\n" % code)
        if changed:
            ledger["baseline"] = dict(sorted(baseline_out.items()))
            tmp = ledger_path + ".tmp"
            with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
                json.dump(ledger, fh, indent=4, sort_keys=True, ensure_ascii=False)
                fh.write("\n")
            os.replace(tmp, ledger_path)
            for code in changed:
                print(code)
        return 0

    if args.baseline_check:
        # 「结项标记在 git 里、baseline 还在 todo」是防重复领用的门失效的唯一形态：
        # baseline=<短号> done 才拒绝重新领用（AGENTS.md 条款 6），漂着就意味着任何人都能
        # 把已交付的卡再领一遍。DX-07 起改由 `--close-cards` 在合并收尾自动结，
        # 本检查降级为**回归探针**：它不再是「只报不改」的待决项，而是验「自动结有没有生效」。
        drift = []
        for code in sorted(cards):
            if (derived.get(code) or "").lower() != "done":
                continue
            current = (baseline.get(code) or "todo").lower()
            if current != "done":
                drift.append((code, baseline.get(code) or "(无条目)"))
        missing = [code for code in sorted(cards) if code not in baseline]
        # 滞留卡（DX-62）：交付提交已合入主干（裸 #<短号>）但全历史无 #<短号>=done。
        # 这类卡 --close-cards 读不到 done 标记、永远结不掉，防重复领用的门也关不上，
        # 上面的漂移探针等不到它归位 —— 必须单独点名，走人工确认通道补结。
        ever_done, _ = collect_delivery_signals(git_log(root), set(cards.keys()))
        main_ref = resolve_main_ref(root, args.main_ref)
        bare_merged = set()
        if main_ref:
            _, bare_merged = collect_delivery_signals(git_log(root, main_ref), set(cards.keys()))
        stuck = stuck_cards(cards, baseline, ever_done, bare_merged)
        for code, current in drift:
            print("[baseline] %s：git 历史里有 #%s=done，但 baseline 仍是 %s" % (code, code, current))
        for code in missing:
            print("[baseline] %s：cards 里有这张卡，baseline 却没有条目（领用门按 todo 处理）" % code)
        for code, current in stuck:
            print("[baseline] %s：交付提交已合入 %s（裸 #%s 标记），但全历史无 #%s=done，baseline 仍是 %s"
                  " —— 已交付却永久滞留、可被重复领用（DX-62）"
                  % (code, main_ref or "main", code, code, current))
        if drift or missing or stuck:
            print(
                "[baseline] 漂移 %d 项 / 缺项 %d 项 / 滞留 %d 项 —— 漂移/缺项应已被 git-done 收尾的"
                " --close-cards 结掉；滞留卡没有 done 标记可读，需人工确认通道补结"
                "（--close-cards --close-explicit）" % (len(drift), len(missing), len(stuck))
            )
        else:
            print("[baseline] 漂移 0 项 / 缺项 0 项 / 滞留 0 项 —— 结卡自动化（DX-07）与滞留探测（DX-62）生效")
        return 0

    for code in sorted(warnings):
        print("[派生器] 提示：提交信息里的 #%s 不在看板台账中（可能是计划钩子用途，已忽略）" % code)
    problems = []
    if unknown:
        problems.append("提交信息里出现台账未登记的路线图短号：%s" % "、".join(sorted(unknown)))

    claims = ledger.get("claims") or {}
    main_ref = resolve_main_ref(root, args.main_ref)
    claims_active, claim_notes = claims_doing(root, claims, main_ref)
    for line in claim_notes:
        sys.stderr.write("[派生器] 提示：%s\n" % line)

    by_id = {}
    for item in items:
        by_id[item.get("id", "")] = item

    # 快照里带 code 但台账没有登记
    for item in items:
        code = item.get("code")
        if code and code not in cards:
            problems.append("快照里的卡片带了台账未登记的短号：%s（%s）" % (code, item.get("title", "")))

    changes = []
    tier_groups = bp.by_tier(items, cards)  # DX-123：todo 新卡按档位落位，不再追加列尾
    max_order = {"doing": 0, "done": 0}  # 非 todo 状态保留旧追加口径（列序无排位语义）
    for item in items:
        status = item.get("status", "todo")
        if status in max_order:
            max_order[status] = max(max_order[status], int(item.get("orderKey", 0) or 0))

    for code in sorted(cards):
        if is_abandoned(baseline, code):
            # 弃单终态（DX-111）：不派生状态、不比对快照、不报漂移。
            # 它靠 baseline 本身拦领用，不需要在快照三态里再找一个位置；
            # 跳过也保证 --check 不会把「todo → abandoned」报成漂移。
            continue
        card = cards[code]
        card_id = card.get("id", "")
        want = status_of(code, derived, claims_active, baseline)
        item = by_id.get(card_id)
        if item is None:
            if args.check:
                problems.append("台账里的卡片 %s（%s）还不在快照里" % (code, card.get("title", "")))
                continue
            if want == "todo":
                tier = card.get("tier")
                if tier not in bp.TIERS:
                    problems.append("卡片 %s 缺少合法 tier（立卡必须经 register_board_cards.py 声明档位，DX-123）" % code)
                    continue
                try:
                    order = bp.insertion_order(tier, tier_groups)
                except RuntimeError:
                    # DX-126：中点插入把档间隙指数耗尽 —— 等距重排全部声明 tier 的
                    # todo 卡后重算，不留「需先重排该档」的人工提示（仓库没有那条通道，
                    # 人工重排又踩「不得手改快照」的红线）。重排只动 orderKey。
                    compacted = bp.compact_orders(items, cards)
                    for it in items:
                        oc = it.get("code", "")
                        if oc in compacted and int(it.get("orderKey", 0) or 0) != compacted[oc]:
                            changes.append("卡片 %s 档间隙耗尽，自动等距重排 orderKey %d → %d"
                                           % (oc, int(it.get("orderKey", 0) or 0), compacted[oc]))
                        if oc in compacted:
                            it["orderKey"] = compacted[oc]
                    tier_groups = bp.by_tier(items, cards)
                    order = bp.insertion_order(tier, tier_groups)
                tier_groups.setdefault(bp.rank(tier), []).append(order)
            else:
                order = max_order.get(want, 0) + ORDER_STEP
                max_order[want] = order
            item = {
                "id": card_id,
                "code": code,
                "title": card.get("title", ""),
                "note": card.get("note", ""),
                "status": want,
                "priority": 2,
                "tags": [card.get("stage", "OTHER")],
                "stage": card.get("stage", "OTHER"),
                "orderKey": order,
                "dueAt": 0,
                "createdAt": int(time.time()),
                "updatedAt": int(time.time()),
                "completedAt": int(time.time()) if want == "done" else 0,
                "deleted": False,
            }
            items.append(item)
            by_id[card_id] = item
            changes.append("新增卡片 %s（%s）状态 %s" % (code, card.get("title", ""), want))
            continue

        if item.get("code") != code:
            changes.append("卡片 %s 绑定短号 %s" % (item.get("title", ""), code))
            item["code"] = code
        if item.get("status") != want:
            changes.append("卡片 %s（%s）状态 %s → %s" % (code, item.get("title", ""), item.get("status"), want))
            item["status"] = want
            # 状态变了就必须抬高时间戳，否则导入端会按「updatedAt 大者胜」判定为
            # 看板侧更新，从而跳过这次状态变更 —— 派生结果就落不到看板上。
            item["updatedAt"] = int(time.time())
        if card.get("note") and not item.get("note"):
            item["note"] = card["note"]
            item["updatedAt"] = int(time.time())
            changes.append("卡片 %s 补全交付说明" % code)
        if item.get("stage") != card.get("stage", "OTHER"):
            item["stage"] = card.get("stage", "OTHER")
        if want == "done" and not item.get("completedAt"):
            item["completedAt"] = int(time.time())

    # 台账标题 / 阶段只作参考：看板上的标题与标签由用户自由编辑，不阻塞派生
    items.sort(key=sort_key)

    if args.check:
        # 排位漂移门（DX-123）：档位必须随 orderKey 单调不减，UI 拖动或手改快照
        # 造成越档都会在这里点名 —— 规则靠退出码拦，不靠人记（Z6）。
        for bad_code, bad_tier, bad_key, prev_code in bp.check_monotonic(items, cards):
            problems.append("卡片 %s 声明 %s 但 orderKey=%d 排在更低档 %s 之后，违反排位规则"
                            % (bad_code, bad_tier, bad_key, prev_code))
        if problems:
            for line in problems:
                sys.stderr.write("[派生器] %s\n" % line)
            sys.stderr.write("[派生器] 快照与派生结果不一致，请执行：python scripts/derive_board_status.py --write\n")
            return 1
        if changes:
            for line in changes:
                sys.stderr.write("[派生器] 待同步：%s\n" % line)
            sys.stderr.write("[派生器] 快照状态落后于派生结果，请执行：python scripts/derive_board_status.py --write\n")
            return 1
        print("[派生器] 校验通过：%d 张卡片状态与 git 历史 / 台账一致" % len(cards))
        return 0

    if problems:
        for line in problems:
            sys.stderr.write("[派生器] %s\n" % line)
        return 1

    snapshot["version"] = SNAPSHOT_VERSION
    snapshot["items"] = items
    old_items = sorted(
        [{k: v for k, v in item.items() if k not in ("code", "status")} for item in read_json(snapshot_path).get("items", [])],
        key=sort_key,
    )
    new_items = sorted(
        [{k: v for k, v in item.items() if k not in ("code", "status")} for item in items],
        key=sort_key,
    )
    if old_items == new_items and changes == []:
        print("[派生器] 状态无变化，未写盘")
        return 0

    snapshot["updated"] = time.strftime("%Y-%m-%dT%H:%M:%S")
    write_json_atomic(snapshot_path, snapshot)
    print("[派生器] 已更新 %s：%d 项变更" % (args.snapshot, len(changes)))
    for line in changes:
        print("  - %s" % line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
