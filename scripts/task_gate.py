#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""任务闸门：没有任务不许改代码。

四条规则，唯一实现集中在这里；钩子与脚本只负责调用和拦断，不得各自复写判定逻辑。

  1. scope   —— 一次改动是「代码」还是「杂务豁免」：白名单之外一律算代码。
               豁免只看路径，不看人怎么声明（禁止再用「我觉得这是杂务」绕过）。
  2. message —— 触及代码的提交必须带路线图短号（#R4-10 / #M19），且短号已登记在台账。
  3. ledger  —— agent 工作区只能写 claims 与卡片 note；加卡、改 id/title/stage、
               改 baseline、改 scope / acceptance（卡片契约）都属于任务定义变更，
               必须凭审计凭证，不得自授权。
  4. push    —— 推送到主干的每个代码提交都必须有服务端 refs/claims/<短号> 占位。

用法：
  python scripts/task_gate.py scope   --base <ref> --head <ref>
  python scripts/task_gate.py message --file <commit-msg-file>
  python scripts/task_gate.py ledger
  python scripts/task_gate.py push    --remote <sha> --local <sha>

退出码：0 通过 / 1 违规 / 2 用法或环境错误。
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

# 豁免白名单：只有这些路径的改动才允许不领卡。
EXEMPT_PREFIXES = ("docs/", "scripts/", ".githooks/", ".github/")
EXEMPT_FILES = ("progress.json", ".gitignore", ".gitattributes", "LICENSE")
EXEMPT_SUFFIXES = (".md",)

# 这些提交类型天然对应「实现」，即使只碰了豁免路径也要求有卡。
HARD_TYPES = ("feat", "fix", "refactor", "perf", "test")

# 流程提交：建卡 / 领用 / 同步快照，由脚本产生，不带卡号也放行。
PROCESS_SCOPES = ("claim", "roadmap")

# 锚点区代登记审计（DX-06）：维护者可授权在锚点区加卡 / 改 baseline，但那条例外
# 长期只校验「位置」（非 .worktrees 一律放行），内容没人管 —— 于是它退化成口头免责：
# 谁都能站在锚点区以「已获授权」为名加任意卡，事后无从追溯（本仓库自查即五例）。
# 现在把提交信息本身变成审计凭证：类型固定 chore(board)，body 必须有这两行。
AUDIT_TYPE = "chore"
AUDIT_SCOPE = "board"
AUDIT_FIELDS = ("授权：", "批次：")
# 弃单终态（DX-111）：baseline 的第三种终态，与 derive_board_status.py 同名。
ABANDONED_BASELINE = "abandoned"

# 任务号文法：<1-4 个字母><可选数字>-<数字>，外加历史遗留的纯 M 编号。
# 覆盖三种真实形态：R4-10（字母+阶段数字）、DX-01 / OPS-02（纯字母阶段）、M19（小任务）。
# 台账只把卡号当 ID，阶段归属由 stage 字段决定，所以非 R 阶段也必须有号可用，
# 否则新阶段（DX 起）永远提交不了。
CARD = re.compile(r"#((?:[A-Za-z]{1,4}\d*-\d+)|(?:M\d+))")
LEDGER = os.path.join("docs", "board-ledger.json")
ZERO = "0" * 40

# 立卡回执目录（DX-97）：register_board_cards.py 为每张新卡生成
# <RECEIPT_DIR>/<短号>.json，携带卡片规范 JSON 的 sha256。commit-msg 阶段核对：
# 无回执 / 哈希不符的新增卡一律拒绝 —— 手工改台账的立卡通道就此关闭。
RECEIPT_DIR = "docs/card-requests/registered"

# 流程提交允许触碰的唯一路径集（DX-51）：台账与路线图快照。DX-40 只把豁免收到
# 「非代码路径」，但 scripts/*.py、docs/*.md 仍在杂务白名单里 —— 领用/立卡提交把它们
# 卷进来照样两道闸门不可见（R16-05 的 032efd8 洗白通道只堵了一半）。流程提交的
# 合法产物只有台账与快照本身，越面即失效。
PROCESS_PATHS = (LEDGER.replace("\\", "/"), "progress.json")

# 流程 scope 全集：含 board（立卡 / 结卡 / 清理登记），越面判定与 claim/roadmap 同口径。
FLOW_SCOPES = PROCESS_SCOPES + ("board",)


def git(*args):
    return subprocess.run(
        ["git"] + list(args),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def git_ok(*args):
    return git(*args).returncode == 0


def fail(message, code=1):
    sys.stderr.write("[任务闸门] %s\n" % message)
    sys.exit(code)


def normalize(path):
    p = path.replace("\\", "/")
    while p.startswith("./"):
        p = p[2:]
    return p


def is_exempt(path):
    p = normalize(path)
    return (
        p.startswith(EXEMPT_PREFIXES)
        or p in EXEMPT_FILES
        or p.endswith(EXEMPT_SUFFIXES)
    )


def changed_files(base, head):
    """只算 head 相对 base **引入**的改动，所以用三点（从 merge-base 起算）。

    两点比较的是两棵树的差异：分支只要落后主线，别人的改动就会被记到它头上，
    纯杂务分支于是被误判成「触及代码」、领用闸门直接卡死合并。CI 侧的
    scripts/check_scope.py 一直是三点，这里与它对齐，免得两套实现漂移。
    """
    spec = "%s...%s" % (base, head)
    out = git("diff", "--name-only", "--diff-filter=ACMRD", spec)
    if out.returncode != 0:
        fail("无法比较改动范围 %s：%s" % (spec, out.stderr.strip()), 2)
    return [normalize(line.strip()) for line in out.stdout.splitlines() if line.strip()]


def code_files(paths):
    return [p for p in paths if not is_exempt(p)]


def process_commit_exempt(kind, scope, paths):
    """流程提交（chore(claim) / chore(roadmap)）默认豁免短号校验 —— 豁免面只覆盖
    流程自身路径（DX-51）。

    agent-init 的领用提交走流程豁免：若不收口，脏树里的实现改动会被裸 git commit
    卷进领用提交，代码就这么失去短号凭证，commit-msg 与 pre-push 双闸门均不可见
    （R16-05 的 032efd8 实测卷走 7 个文件）。DX-40 先把豁免收到「非代码路径」，
    但 scripts/*.py、docs/*.md 仍在杂务白名单里，卷进来照样两道闸门不可见；
    DX-51 收口到流程自身路径：触及 PROCESS_PATHS 之外的任何文件（含杂务白名单）
    都不再豁免，按普通提交校验。"""
    return (
        kind == "chore"
        and scope in PROCESS_SCOPES
        and process_paths_ok(paths)
    )


def flow_scope_beyond_paths(kind, scope, paths):
    """流程 scope（claim / roadmap / board）的提交触及流程路径之外的文件（DX-51）。

    豁免失效后不能就此落进「非 HARD 类型 + 无代码路径 → 放行」的杂务兜底：
    那等于换个入口继续洗白。流程提交越面时必须带短号并过 claim 校验，
    与「卷入代码」同口径，commit-msg 与 pre-push 双闸门一致。"""
    return (
        kind == "chore"
        and scope in FLOW_SCOPES
        and not process_paths_ok(paths)
    )


def read_ledger(src):
    """src 可以是 git 修订表达式（HEAD / :index）或磁盘路径。"""
    if os.path.sep in src or src.startswith("."):
        with open(src, "r", encoding="utf-8") as fh:
            return json.load(fh)
    out = git("show", "%s:%s" % (src, LEDGER.replace("\\", "/")))
    if out.returncode != 0:
        return None
    return json.loads(out.stdout)


def ledger_codes(rev=":0"):
    try:
        data = read_ledger(rev)
    except (OSError, ValueError):
        return set()
    if not data:
        return set()
    return {str(k).upper() for k in (data.get("cards") or {})}


def extract_cards(text):
    return [m.group(1).upper() for m in CARD.finditer(text or "")]


def commit_type(message):
    first = (message or "").strip().splitlines()[0] if (message or "").strip() else ""
    head = re.match(r"^([A-Za-z]+)(?:\(([^)]*)\))?:", first)
    if not head:
        return "", ""
    return head.group(1).lower(), (head.group(2) or "").lower()


def cmd_scope(args):
    paths = changed_files(args.base, args.head)
    if not paths:
        print("exempt")
        return 0
    code = code_files(paths)
    if not code:
        print("exempt")
        return 0
    print("code")
    sys.stderr.write("[任务闸门] 触及代码路径 %d 个，需要任务卡：\n" % len(code))
    for p in code[:10]:
        sys.stderr.write("  - %s\n" % p)
    if len(code) > 10:
        sys.stderr.write("  … 共 %d 个\n" % len(code))
    return 0


def cmd_message(args):
    if not os.path.isfile(args.file):
        fail("找不到提交信息文件：%s" % args.file, 2)
    with open(args.file, "r", encoding="utf-8", errors="replace") as fh:
        raw = fh.read()
    lines = [ln for ln in raw.splitlines() if not ln.lstrip().startswith("#")]
    message = "\n".join(lines).strip()
    if not message:
        return 0  # 空信息交给 commit-msg 原有格式校验处理

    kind, scope = commit_type(message)
    if kind == "merge":
        return 0

    staged = git("diff", "--cached", "--name-only", "--diff-filter=ACMR")
    staged = [normalize(x.strip()) for x in staged.stdout.splitlines() if x.strip()]
    touches_code = bool(code_files(staged))

    # DX-40 / DX-51：流程豁免只覆盖流程自身路径 —— 卷入其它文件（含杂务白名单）
    # 的流程提交按普通提交校验
    if process_commit_exempt(kind, scope, staged):
        return 0

    # 代登记审计（DX-06 + DX-07）：只要这次提交改了台账**任务定义**，就必须留下凭证。
    #
    # DX-07 修正了 DX-06 的一处死锁：原先位置校验要求「代登记只能在锚点区」，
    # 但锚点区检出在 main 上、pre-commit 规则 1 禁止在 main 提交、§7.1 又禁止 agent
    # 在锚点区切分支 —— 三条叠加后，代登记**没有任何合法执行位置**（实测被三道闸门连续拒）。
    # 结论：位置不是凭证，**内容才是**。改成「任何检出里都能代登记，但必须带完整审计行」，
    # 由提交信息本身证明授权来源；cmd_ledger 相应只拦「没有审计凭证的定义变更」。
    if LEDGER.replace("\\", "/") in staged:
        try:
            old = read_ledger("HEAD")
            new = read_ledger(":0")
        except (OSError, ValueError):
            old = new = None
        if old is not None and new is not None and ledger_definition_changes(old, new):
            # DX-51：混提交判定从「代码路径」收紧到「流程路径之外」—— 立卡提交
            # 卷走 docs/、scripts/ 白名单文件同样洗白，一律要求拆开
            beyond = [p for p in staged if p not in PROCESS_PATHS
                      and not p.startswith(RECEIPT_DIR + "/")]
            problems = audit_problems(
                kind, scope, message, beyond,
                ledger_definition_changes(old, new),
            )
            # DX-97：新增卡必须带程序回执 —— 手工改台账的立卡通道就此关闭。
            # 回执从暂存区读（:path），与本提交的台账版本同源，防「先登记后改卡」。
            old_cards, _ = ledger_shape(old)
            new_cards, _ = ledger_shape(new)
            receipts = {}
            for code in sorted(set(new_cards) - set(old_cards)):
                path = "%s/%s.json" % (RECEIPT_DIR, code)
                if path not in staged:
                    receipts[code] = None
                    continue
                shown = git("show", ":%s" % path)
                try:
                    receipts[code] = json.loads(shown.stdout)
                except ValueError:
                    receipts[code] = None
            problems += receipt_problems(old, new, receipts)
            # DX-121 半落地闸门：立卡提交的台账必须真的新增卡，且每个新增短号
            # 都能对上 origin 的发号占位（服务端占位是发号的原子事实源；
            # ls-remote 需网络 —— 与领用 fail-closed 同口径）。
            problems += card_addition_problems(old, new, message)
            added_codes = sorted(set(new_cards) - set(old_cards))
            if added_codes:
                remote = git("ls-remote", "origin",
                             *["refs/registrations/%s" % c for c in added_codes])
                if remote.returncode != 0:
                    problems.append("无法核对 origin 发号占位（ls-remote 失败：%s）"
                                    % (remote.stderr or remote.stdout).strip())
                else:
                    problems += registration_ref_problems(added_codes, remote.stdout)
            if problems:
                sys.stderr.write("[任务闸门] 代登记需要可审计凭证（AGENTS.md 条款 6 例外）：\n")
                for line in problems:
                    sys.stderr.write("  - %s\n" % line)
                sys.stderr.write(
                    "  正确写法：\n"
                    "    chore(board): 登记 <批次简述>\n"
                    "\n"
                    "    授权：<维护者在哪次会话/哪条指令里授权的>\n"
                    "    批次：<本批卡号，如 DX-05、R7-01>\n"
                )
                return 1
        elif old is not None and new is not None:
            # DX-121 半落地闸门（视图无变化分支）：7ee6040 的台账 diff 只有结尾
            # 换行，ledger_definition_changes 看不出任何变更 —— 但 subject 是立卡
            # 就必须查「真的新增了卡吗」。这一支专抓被冲突解决抹掉卡片定义的
            # 半落地提交，也是它当时能溜过 commit-msg 的原因。
            half_problems = card_addition_problems(old, new, message)
            if half_problems:
                sys.stderr.write("[任务闸门] 立卡半落地（DX-121）：\n")
                for line in half_problems:
                    sys.stderr.write("  - %s\n" % line)
                return 1

    # DX-51：流程 scope 越面（卷入台账/快照之外的文件）时不吃杂务兜底，
    # 与触及代码同口径，必须带短号
    if (not touches_code and kind not in HARD_TYPES
            and not flow_scope_beyond_paths(kind, scope, staged)):
        return 0

    codes = extract_cards(message)
    if not codes:
        fail(
            "本次改动触及代码路径，提交信息必须带任务号（如 `feat(board): 增加筛选 #R4-10`）。\n"
            "  —— 没有任务就不改代码；先由维护者在台账里立卡，再用 agent-init 领用。"
        )

    known = ledger_codes(":0")
    unknown = [c for c in codes if c not in known]
    if unknown:
        fail(
            "提交信息里的任务号未在台账登记：%s\n"
            "  台账是人工入口，agent 不得自行加卡（doc 里也没有对应验收项）。"
            % "、".join("#" + c for c in unknown)
        )
    return 0


def contract_items(value):
    """把 scope / acceptance 规整成可比较的集合：去首尾空白、丢空项、排序。

    顺序不承载语义（check_scope.py 按集合判定越界），所以**只调换顺序或改空白
    不算任务定义变更** —— 否则一次纯排版整理就会要求审计凭证，规则会被绕过。
    """
    if not isinstance(value, list):
        return []
    return sorted(str(item).strip() for item in value if str(item).strip())


def contract_delta(old_items, new_items):
    """给出可读的增删摘要，让人不必打开 diff 就知道这次动了什么。"""
    added = [item for item in new_items if item not in old_items]
    removed = [item for item in old_items if item not in new_items]
    parts = []
    if added:
        parts.append("新增 %d 项" % len(added))
    if removed:
        parts.append("删除 %d 项" % len(removed))
    return "、".join(parts) or "内容调整"


def ledger_shape(data):
    """把台账压成「任务定义」视图：cards 的 id/title/stage/scope/acceptance + 整份 baseline。

    claims 与卡片 note 不在视图里 —— 它们是 agent 唯一被允许写的两部分（§条款 6）。
    scope / acceptance 是卡片契约（DX-10）：scope 决定「允许改哪些路径」，
    acceptance 决定「怎样算做完」。放任它们不进视图，等于给 agent 留了一条
    自我扩权、自我放宽验收的后门，且不留任何痕迹。
    """
    cards = {}
    for code, card in ((data or {}).get("cards") or {}).items():
        cards[code] = {
            "id": card.get("id", ""),
            "title": card.get("title", ""),
            "stage": card.get("stage", ""),
            "scope": contract_items(card.get("scope")),
            "acceptance": contract_items(card.get("acceptance")),
            # 排位档位（DX-123）：决定新卡在看板的落点，属任务定义，
            # 改动必须走 chore(board)+授权/批次，不给静默改序留后门。
            "tier": card.get("tier", ""),
        }
    return cards, dict((data or {}).get("baseline") or {})


def ledger_definition_changes(old, new):
    """列出「任务定义」层面的改动。

    含：新增/删除卡、改 id/title/stage、改 scope/acceptance、改 baseline。
    空列表 = 这次只动了 claims 或卡片 note，属 agent 合法范围。
    """
    old_cards, old_base = ledger_shape(old)
    new_cards, new_base = ledger_shape(new)
    problems = []
    added = sorted(set(new_cards) - set(old_cards))
    removed = sorted(set(old_cards) - set(new_cards))
    if added:
        problems.append("新增卡片：%s" % "、".join(added))
    if removed:
        problems.append("删除卡片：%s" % "、".join(removed))
    for code in sorted(set(old_cards) & set(new_cards)):
        before, after = old_cards[code], new_cards[code]
        renamed = [field for field in ("id", "title", "stage")
                   if before[field] != after[field]]
        if renamed:
            problems.append(
                "改动卡片定义 %s（%s 被修改）" % (code, "/".join(renamed)))
        if before["scope"] != after["scope"]:
            problems.append(
                "改 scope：%s（%s）"
                % (code, contract_delta(before["scope"], after["scope"])))
        if before["acceptance"] != after["acceptance"]:
            problems.append(
                "改 acceptance：%s（%s）"
                % (code, contract_delta(before["acceptance"], after["acceptance"])))
    for code in sorted(set(old_base) | set(new_base)):
        if old_base.get(code) != new_base.get(code):
            problems.append(
                "改动 baseline %s（%s → %s）"
                % (code, old_base.get(code, "<无>"), new_base.get(code, "<无>"))
            )
    # numbering.mode 是发号治理配置（DX-105）：把它改弱（remote→local）等价于
    # 拆掉服务端占位这层闸门，必须与改契约同等审计，不给「改配置绕占位」留后门。
    old_numbering = (old or {}).get("numbering") or {}
    new_numbering = (new or {}).get("numbering") or {}
    if old_numbering != new_numbering:
        problems.append(
            "改动立卡发号配置 numbering（%s → %s）"
            % (old_numbering or "<无>", new_numbering or "<无>")
        )
    return problems


def receipt_problems(old, new, receipts):
    """新增卡的程序回执校验（DX-97）：立卡只能经 register_board_cards.py 完成。

    receipts: {短号: 回执 dict 或 None（缺文件/坏 JSON）}。返回问题列表；
    纯函数不碰 git，便于 --selftest 与 commit-msg 复用同一套判定。
    """
    old_cards, _ = ledger_shape(old)
    new_cards, _ = ledger_shape(new)
    problems = []
    for code in sorted(set(new_cards) - set(old_cards)):
        receipt = receipts.get(code)
        if receipt is None:
            problems.append(
                "新增卡 %s 无立卡回执：手工改台账立卡通道已关闭（DX-97），"
                "只能用 python scripts/register_board_cards.py "
                "--form <请求单> --authorized-by <授权来源> 立卡" % code)
            continue
        if receipt.get("card") != code:
            problems.append("回执 card 字段（%r）与新增卡（%s）不符"
                            % (receipt.get("card"), code))
        card = (new.get("cards") or {}).get(code) or {}
        payload = json.dumps(card, ensure_ascii=False, sort_keys=True,
                             separators=(",", ":"))
        digest = hashlib.sha256(payload.encode("utf-8")).hexdigest()
        if receipt.get("sha256") != digest:
            problems.append("回执 sha256 与卡片内容不符（登记后被手工改动？）：%s" % code)
        if not receipt.get("authorizedBy"):
            problems.append("回执缺 authorizedBy（授权来源必须写入回执）：%s" % code)
    return problems


def card_addition_problems(old, new, subject):
    """DX-121 半落地闸门：`chore(board): 立卡` 提交的台账必须真的新增卡片。

    7ee6040 实录：rebase 误解析把程序写入的卡片定义抹掉，立卡提交的台账
    只剩结尾换行变化，两个回执文件却照常落库 —— 回执/占位在而台账卡不在。
    纯函数不碰 git，selftest 与 commit-msg 共用同一套判定。
    """
    if not re.match(r"^chore\(board\):\s*立卡", (subject or "").splitlines()[0]
                    if subject else ""):
        return []
    old_cards, old_base = ledger_shape(old)
    new_cards, new_base = ledger_shape(new)
    if (set(new_cards) - set(old_cards)) or (set(new_base) - set(old_base)):
        return []
    same_semantics = old_cards == new_cards and old_base == new_base
    hint = ("台账与上一版语义完全相同（仅空白/换行差异，7ee6040 形态）"
            if same_semantics else "台账 diff 不含 cards/baseline 新增")
    return ["立卡提交的台账 diff 不含任何卡片新增（%s）：卡片定义被冲突解决抹掉"
            "即「半落地」（DX-121），回执/占位会悬空 —— 回到 register_board_cards.py"
            "重放立卡，或补落卡片定义后重跑收尾" % hint]


def registration_ref_problems(codes, lsremote_output):
    """DX-121：立卡新增短号必须在 origin 有发号占位（refs/registrations/<短号>）。

    服务端占位是发号的原子事实源（DX-99 test-and-set）；本地回执可以是半落地
    残留，对不上占位即不可信。lsremote_output 传 `git ls-remote` 的原始输出，
    纯函数便于 selftest。
    """
    held = {line.split("\t")[-1] for line in (lsremote_output or "").splitlines()
            if line.strip()}
    return ["新增卡 %s 在 origin 无发号占位 refs/registrations/%s：回执可能是"
            "半落地残留或伪造（DX-121，7ee6040 形态）" % (code, code)
            for code in codes if "refs/registrations/%s" % code not in held]


def process_paths_ok(paths):
    """流程提交合法路径集（DX-97 扩）：台账、快照与立卡回执。"""
    prefix = RECEIPT_DIR + "/"
    return all(p in PROCESS_PATHS or p.startswith(prefix) for p in paths)


def staged_files():
    out = git("diff", "--cached", "--name-only", "--diff-filter=ACMR")
    return [normalize(x.strip()) for x in out.stdout.splitlines() if x.strip()]


def is_abandon_close(definition_changes):
    """本提交是否把某张卡的 baseline 落到弃单终态（DX-111）。

    只认 ``ledger_definition_changes`` 产出的
    「改动 baseline <短号>（<旧值> → abandoned）」行；纯字符串判定，与派生器
    的终态值同名。弃单结案是人工授权动作，不是 git 事实的机械投影。
    """
    pattern = re.compile(r"→\s*%s\s*）?\s*$" % ABANDONED_BASELINE)
    return any(
        line.startswith("改动 baseline ") and pattern.search(line)
        for line in definition_changes
    )


def audit_problems(kind, scope, message, code_paths, definition_changes):
    """台账任务定义变更的审计判定（DX-06 / DX-07 / DX-10）。纯函数：不碰 git，便于自测与 CI 跑。

    「代登记」= 这次提交改了台账的任务定义（新增/删除卡、改 id/title/stage、
    改 scope/acceptance、改 baseline）。
    凭证有两类，来自两个不同的写入者：

      A. 人工代登记（维护者在会话中授权）：
         - 类型固定 chore(board)，不得混进 feat/fix/docs 等其它语义；
         - body 必须有「授权：」与「批次：」两行（授权来源 + 批次）；
         - 不得同时改动代码路径 —— 立卡与实现分开提交。
         DX-10 起 scope / acceptance 也走这条：可改范围与验收条件是任务契约，
         改它跟加卡一样需要授权与留痕（R10-02 实测过两次自补 scope 不留痕）。

      B. 脚本自动结卡（DX-07 的 git-done 收尾，只把 baseline 补成 done）：
         它不是「谁授权加卡」，而是 git 事实的机械投影，**没有人可授权**。若也要求
         「授权行」，收尾提交就会被自家闸门拦下 —— 实测正是这样（结卡：提交失败）。
         所以给它一条显式通道：body 写明 `自动来源：` 即可，其余约束照旧
         （仍必须 chore(board)、仍不得混代码）。
         ⚠ 这条通道不发「加卡」权：自动结卡**只允许改 baseline**，一旦出现新增/删除卡片、
         改 id/title/stage、改 scope/acceptance，仍按 A 类要求授权行。
    """
    if not definition_changes:
        return []
    problems = []
    if kind != AUDIT_TYPE or scope != AUDIT_SCOPE:
        problems.append(
            "改任务定义（立卡 / 改 scope / 改 acceptance / 改 baseline）"
            "必须用 chore(board): 提交（当前类型：%s%s）"
            % (kind or "<空>", ("(%s)" % scope) if scope else "")
        )
    body = "\n".join((message or "").splitlines()[1:])

    baseline_only = all(
        line.startswith("改动 baseline ") for line in definition_changes
    )
    abandon_close = is_abandon_close(definition_changes)
    # DX-111：自动来源通道只服务「git 事实投影成 done」的自动结卡；弃单结案是
    # 人的决定，不适用该通道，必须走 A 类人工授权行。且弃单结案只改 baseline，
    # 借弃单之名夹带卡片定义变更（哪怕带齐授权/批次两行）一律拒绝。
    auto_close = (bool(re.search(r"(?m)^\s*自动来源：\S", body))
                  and baseline_only and not abandon_close)
    if abandon_close and not baseline_only:
        problems.append(
            "弃单结案提交只改 baseline，不得夹带卡片定义变更"
            "（冒充弃单改定义，DX-111）"
        )

    if not auto_close:
        missing = [f for f in AUDIT_FIELDS if not re.search(
            r"(?m)^\s*%s\S" % re.escape(f), body)]
        if missing:
            problems.append(
                "提交 body 缺少审计行：%s（每行后要写出授权来源 / 批次号，事后可追溯）；"
                "脚本自动结卡请改用「自动来源：」行" % "、".join(missing)
            )
    if code_paths:
        shown = "、".join(code_paths[:5]) + ("…" if len(code_paths) > 5 else "")
        problems.append(
            "改任务定义的提交不得同时改动流程路径之外的文件：%s"
            "（立卡与实现分开提交，杂务白名单路径也算夹带 —— DX-51）" % shown)
    return problems


def cmd_registrations(args):
    """DX-121：回执/占位 ↔ 台账卡片 双向一致性对账（半落地 7ee6040 防回归）。

    扫 docs/card-requests/registered/*.json 与 origin refs/registrations/*，台账
    cards 无对应卡时分两档：
      硬拦（退出 1）—— 回执文件在而卡不在：register 程序已写卡片定义却没进台账，
        即 7ee6040 半落地形态；
      告警 —— 仅占位在（无回执文件）：立卡在「推占位 → 写回执」之间半途失败的
        孤儿占位（DX-118 形态），留痕点名，补立或弃由维护者定。
    sha256 失配不在此校验：合法定义变更（chore(board)+授权，DX-10）会让立卡时刻的
    哈希漂移，全量硬闸必误报历史在办/已结卡；新增时刻的强校验在 commit-msg 的
    receipt_problems（对新增卡）与 registration_ref_problems（对 origin 占位）处，
    那里才是「登记后即被改」能被确证的时机。占位核对需联网（fail-closed，与领用同口径）。
    """
    try:
        with open(LEDGER.replace("\\", "/"), encoding="utf-8") as fh:
            ledger = json.load(fh)
    except (OSError, ValueError) as exc:
        fail("台账读取失败：%s" % exc, 2)
    cards = ledger.get("cards") or {}
    receipts = {}
    receipt_dir = RECEIPT_DIR.replace("\\", "/")
    if os.path.isdir(receipt_dir):
        for name in sorted(os.listdir(receipt_dir)):
            if not name.endswith(".json"):
                continue
            try:
                with open(os.path.join(receipt_dir, name), encoding="utf-8") as fh:
                    receipts[name[:-len(".json")]] = json.load(fh)
            except ValueError:
                receipts[name[:-len(".json")]] = None
    remote = git("ls-remote", "origin", "refs/registrations/*")
    if remote.returncode != 0:
        fail("无法核对 origin 发号占位（ls-remote 失败：%s）—— 本检查 fail-closed 需联网"
             % (remote.stderr or remote.stdout).strip(), 2)
    placeholder_codes = {line.split("\t")[-1].split("/")[-1]
                         for line in remote.stdout.splitlines() if line.strip()}
    problems = []
    warnings = []
    for code in sorted(set(receipts) | placeholder_codes):
        if code in cards:
            continue
        if code in receipts:
            problems.append("回执文件在而台账 cards 无此卡：%s"
                            "（半落地，DX-121 / 7ee6040 形态）" % code)
        else:
            warnings.append("占位 refs/registrations/%s 在而台账无此卡、也无回执文件："
                            "立卡半途失败的孤儿占位（DX-118 形态），补立或弃由维护者定"
                            % code)
    if warnings:
        for line in warnings:
            sys.stderr.write("[任务闸门] 告警：%s\n" % line)
    if problems:
        for line in problems:
            sys.stderr.write("[任务闸门] %s\n" % line)
        sys.stderr.write("[任务闸门] 立卡半落地（DX-121）：回执文件在而台账卡不在。"
                         "补落卡片定义（重放 register_board_cards.py 或按台账冲突"
                         "语义合并保留程序写入的卡）后重跑本检查。\n")
        return 1
    print("[任务闸门] 立卡一致性对账通过：回执 %d / 占位 %d / 台账卡 %d（孤儿占位告警 %d）"
          % (len(receipts), len(placeholder_codes), len(cards), len(warnings)))
    return 0


def cmd_ledger(args):
    """pre-commit 阶段的台账保护：拦住**没有凭证也会被拒**的定义变更，但不单独判死刑。

    DX-07 修正：本命令跑在 pre-commit，**手里没有提交信息**，所以它无法判定一次定义变更
    是否带了「授权：」「批次：」两行 —— 原先靠「只允许锚点区」来替代判定，结果与
    「锚点区禁止 agent 切分支 / main 禁止直接提交」撞成死锁。现在把判定权交给
    `cmd_message`（commit-msg 阶段，那里才有 message），本命令只负责**提前告知**：
    有定义变更就打印即将被审计什么，让写提交信息的人当场知道该怎么写。
    """
    top = git("rev-parse", "--show-toplevel").stdout.strip()
    in_worktree = ".worktrees" in normalize(top)

    old = read_ledger("HEAD")
    if old is None:
        fail("读不到 HEAD 的台账，无法校验", 2)
    try:
        new = read_ledger(":0")
    except (OSError, ValueError):
        fail("暂存区的台账不是合法 JSON", 2)

    problems = ledger_definition_changes(old, new)
    if not problems:
        return 0  # 只动 claims / note：agent 的合法范围，直接放行

    where = "agent 工作区" if in_worktree else "锚点区"
    sys.stderr.write("[任务闸门] %s正在改动台账的任务定义：\n" % where)
    for line in problems:
        sys.stderr.write("  - %s\n" % line)
    sys.stderr.write(
        "  这本身是允许的（维护者可授权代登记），但**提交信息必须带审计凭证**，\n"
        "  由 commit-msg 阶段硬校验，缺一即拒：\n"
        "    chore(board): 立卡 / 调整 scope / 结卡 <批次简述>\n"
        "\n"
        "    授权：<维护者在哪次会话/哪条指令里授权的>\n"
        "    批次：<本批卡号，如 DX-05、R7-01>\n"
        "  且该提交不得同时改动代码路径（契约调整与实现分开提交）。\n"
        "  其中**新增卡**只能走程序（DX-97）：\n"
        "    python scripts/register_board_cards.py --form <请求单> --authorized-by <授权来源>\n"
        "  手工往台账塞卡没有回执，commit-msg 阶段一律拒绝。\n"
    )
    return 0


def cmd_push(args):
    remote, local = args.remote, args.local
    if set(remote) == {"0"}:
        remote = "origin/main"
        if not git_ok("rev-parse", "--verify", "-q", remote):
            return 0
    commits = git("rev-list", "--no-merges", "%s..%s" % (remote, local))
    if commits.returncode != 0:
        fail("无法枚举推送范围 %s..%s" % (remote, local), 2)

    fetched = git("fetch", "origin", "+refs/claims/*:refs/claims/*")
    online = fetched.returncode == 0
    if not online:
        sys.stderr.write(
            "[任务闸门] 提示：拉取服务端 claim refs 失败，改用本地已有占位校验\n"
        )

    known = ledger_codes(local)
    problems = []
    for sha in [line.strip() for line in commits.stdout.splitlines() if line.strip()]:
        subject = git("log", "-1", "--format=%s", sha).stdout.strip()
        kind, scope = commit_type(subject)
        if kind == "merge":
            continue
        files = git(
            "diff-tree", "--no-commit-id", "--name-only", "-r", "--root", sha
        ).stdout
        paths = [normalize(p.strip()) for p in files.splitlines() if p.strip()]
        # DX-40 / DX-51：流程豁免只覆盖流程自身路径 —— 卷入其它文件（含杂务白名单）
        # 的流程提交按普通提交校验
        if process_commit_exempt(kind, scope, paths):
            continue
        # DX-51：流程 scope 越面时不吃「非代码路径 → 跳过」的杂务兜底
        if not code_files(paths) and not flow_scope_beyond_paths(kind, scope, paths):
            continue
        codes = extract_cards(subject)
        if not codes:
            problems.append("%s %s：触及代码但提交信息没有任务号" % (sha[:7], subject))
            continue
        for code in codes:
            if known and code not in known:
                problems.append("%s %s：任务号 #%s 未在台账登记" % (sha[:7], subject, code))
                continue
            if not git_ok("rev-parse", "--verify", "-q", "refs/claims/%s" % code):
                problems.append(
                    "%s %s：缺少服务端占位 refs/claims/%s（未领用，或需联网才能校验）"
                    % (sha[:7], subject, code)
                )

    if problems:
        sys.stderr.write("[任务闸门] 推送到主干的代码改动缺少任务凭据：\n")
        for line in problems:
            sys.stderr.write("  - %s\n" % line)
        sys.stderr.write(
            "  正确做法：scripts/agent-init.ps1 -Card <短号> 开区领用；\n"
            "  纯文档/脚本/钩子改动不在此列（豁免只看路径）。\n"
        )
        return 1
    return 0


def cmd_selftest(args):
    """DX-06 的可执行自测：允许 / 拒绝各若干例，纯函数不碰 git，任何环境可跑。

    为什么不放进 tests/：那是 C++ 单测目录，闸门是 python 脚本，两套跑法不同。
    把用例钉在执行判定逻辑的同一个文件里，改规则时用例跟着一起改，不会漂。
    """
    def ledger_with(cards=None, baseline=None, notes=None, claims=None):
        return {
            "version": 3,
            "cards": cards or {"DX-01": {"id": "a", "title": "t", "stage": "DX"}},
            "baseline": baseline or {"DX-01": "done"},
            "claims": claims or {},
        }

    base = ledger_with()
    new_card = ledger_with(
        cards={
            "DX-01": {"id": "a", "title": "t", "stage": "DX"},
            "DX-07": {"id": "b", "title": "新的", "stage": "DX"},
        },
        baseline={"DX-01": "done", "DX-07": "todo"},
    )
    # agent 合法范围：只改 note / 只改 claims，不动定义
    note_only = ledger_with(
        cards={"DX-01": {"id": "a", "title": "t", "stage": "DX", "note": "交付说明"}},
        claims={"DX-01": {"agent": "x", "branch": "fix/x"}},
    )
    baseline_changed = ledger_with(baseline={"DX-01": "todo"})

    # DX-10：scope / acceptance 是卡片契约，改它跟加卡一样要凭证
    contract = ledger_with(cards={
        "DX-01": {
            "id": "a", "title": "t", "stage": "DX",
            "scope": ["src/a.cpp", "src/b.cpp"],
            "acceptance": ["跑通 A", "跑通 B"],
        },
    })
    scope_widened = ledger_with(cards={
        "DX-01": {
            "id": "a", "title": "t", "stage": "DX",
            "scope": ["src/a.cpp", "src/b.cpp", "src/c.cpp"],
            "acceptance": ["跑通 A", "跑通 B"],
        },
    })
    # 只调换顺序 + 首尾空白：不该被当成定义变更，否则排版整理会被规则拦住
    contract_reordered = ledger_with(cards={
        "DX-01": {
            "id": "a", "title": "t", "stage": "DX",
            "scope": ["  src/b.cpp", "src/a.cpp "],
            "acceptance": ["跑通 B", "跑通 A"],
        },
    })
    acceptance_softened = ledger_with(cards={
        "DX-01": {
            "id": "a", "title": "t", "stage": "DX",
            "scope": ["src/a.cpp", "src/b.cpp"],
            "acceptance": ["跑通 A"],
        },
    })
    tier_changed = ledger_with(cards={
        "DX-01": {
            "id": "a", "title": "t", "stage": "DX",
            "scope": ["scripts/a.py", "scripts/b.py"],
            "acceptance": ["跑通 A"],
            "tier": "P1",
        },
    })
    scope_and_baseline = ledger_with(
        cards={
            "DX-01": {
                "id": "a", "title": "t", "stage": "DX",
                "scope": ["src/a.cpp", "src/b.cpp", "src/c.cpp"],
                "acceptance": ["跑通 A", "跑通 B"],
            },
        },
        baseline={"DX-01": "todo"},
    )

    cases = []

    def case(name, expect_ok, kind, scope, message, code_paths, old, new):
        problems = audit_problems(
            kind, scope, message, code_paths, ledger_definition_changes(old, new)
        )
        cases.append((name, expect_ok, problems))

    # ---- 允许（4 例）----
    case(
        "锚点区合规代登记：chore(board) + 授权行 + 批次行 + 只动台账",
        True, "chore", "board",
        "chore(board): 登记 R7 批次卡\n\n授权：维护者 2026-10-28 会话「授权登记 R7」\n批次：R7-01、R7-02",
        [], base, new_card,
    )
    case(
        "只改 claims / 卡片 note，不算代登记（无需凭证）",
        True, "chore", "claim",
        "chore(claim): 登记 #DX-01 领用到台账",
        [], base, note_only,
    )
    case(
        "改 scope 带审计行、只动台账 → 放行（可纠正，但必须留痕）",
        True, "chore", "board",
        "chore(board): 补 R10-02 的 scope\n\n授权：维护者 2026-10-29 会话\n批次：R10-02",
        [], contract, scope_widened,
    )
    case(
        "改排位档位无审计行 → 拒（档位属任务定义，DX-123）",
        False, "chore", "board",
        "chore(board): 改 DX-01 排位档位",
        [], contract, tier_changed,
    )
    case(
        "改排位档位带授权/批次 → 放行",
        True, "chore", "board",
        "chore(board): 调 DX-01 排位档位\n\n授权：维护者 2026-11-07 会话\n批次：DX-01",
        [], contract, tier_changed,
    )

    # ---- DX-121 半落地闸门（纯函数）----
    half_a = {"cards": {"DX-1": {"id": "a", "title": "t", "stage": "DX"}},
              "baseline": {"DX-1": "todo"}}
    half_b = {"cards": {"DX-1": {"id": "a", "title": "t", "stage": "DX"}},
              "baseline": {"DX-1": "todo"}}
    lika = "chore(board): 立卡 DX-111、DX-112"
    def check_half(name, ok):
        cases.append((name, True, [] if ok else ["半落地断言失败"]))

    cases_failed = card_addition_problems(half_a, half_b, lika)
    check_half("半落地：立卡提交台账纯空白变化 → 拒（7ee6040 形态）", len(cases_failed) == 1)
    check_half("半落地：改既有卡但无新增 → 拒",
          len(card_addition_problems(
              half_a,
              {"cards": {"DX-1": {"id": "a", "title": "t2", "stage": "DX"}},
               "baseline": {"DX-1": "todo"}}, lika)) == 1)
    grown = {"cards": {"DX-1": {"id": "a", "title": "t", "stage": "DX"},
                       "DX-2": {"id": "b", "title": "t2", "stage": "DX"}},
             "baseline": {"DX-1": "todo", "DX-2": "todo"}}
    check_half("正常立卡（有新增）→ 放", card_addition_problems(half_a, grown, lika) == [])
    check_half("非立卡提交不吃此闸",
          card_addition_problems(half_a, half_b, "chore(board): 结卡 #DX-1=done") == [])
    check_half("占位核对：origin 有占位 → 放",
          registration_ref_problems(["DX-2"], "abc\trefs/registrations/DX-2") == [])
    check_half("占位核对：origin 无占位 → 拒",
          len(registration_ref_problems(["DX-2"], "abc\trefs/registrations/DX-3")) == 1)
    case(
        "只调换 scope / acceptance 顺序或改空白，不算定义变更 → 放行",
        True, "chore", "board",
        "chore(board): 整理卡片契约排版",
        [], contract, contract_reordered,
    )

    # ---- 拒绝（8 例）----
    case(
        "立卡用了 feat 类型",
        False, "feat", "board",
        "feat(board): 顺手加两张卡\n\n授权：有人说过\n批次：DX-07",
        [], base, new_card,
    )
    case(
        "立卡缺「授权：」「批次：」两行",
        False, "chore", "board",
        "chore(board): 登记几张卡\n\n就加了几张卡而已",
        [], base, new_card,
    )
    case(
        "立卡与代码混提交",
        False, "chore", "board",
        "chore(board): 登记并实现\n\n授权：维护者\n批次：DX-07",
        ["src/Database.cpp"], base, new_card,
    )
    case(
        "只改 baseline 也算代登记，同样要凭证",
        False, "docs", "board",
        "docs(board): 把 DX-01 改回 todo",
        [], base, baseline_changed,
    )
    case(
        "只改 scope、无审计行 → 拦截（DX-10 的核心用例）",
        False, "chore", "board",
        "chore(board): 顺手补一下 scope",
        [], contract, scope_widened,
    )
    case(
        "scope 变更与代码路径混提交 → 拦截",
        False, "chore", "board",
        "chore(board): 补 scope 并实现\n\n授权：维护者\n批次：R10-02",
        ["src/TaskFilterModel.cpp"], contract, scope_widened,
    )
    case(
        "改软 acceptance（少一条验收）无审计行 → 拦截",
        False, "chore", "board",
        "chore(board): 把验收条件改成一条",
        [], contract, acceptance_softened,
    )
    case(
        "自动结卡通道不扩权：夹带 scope 改动仍要授权行 → 拦截",
        False, "chore", "board",
        "chore(board): 结卡 DX-01\n\n自动来源：derive_board_status.py --close-cards",
        [], contract, scope_and_baseline,
    )

    # ---- 自动结卡通道（DX-07）----
    # git-done 收尾的结卡提交没有「谁授权」可写（它是 git 事实的机械投影），
    # 若也硬要授权行，收尾会被自家闸门拦下（实测「结卡：提交失败」）。给它显式通道。
    case(
        "脚本自动结卡：chore(board) + 「自动来源：」行，只改 baseline → 放行",
        True, "chore", "board",
        "chore(board): 结卡 DX-01\n\n自动来源：git-done.ps1 合并收尾的 derive_board_status.py --close-cards。",
        [], base, baseline_changed,
    )
    case(
        "自动结卡通道**不扩权**：夹带新增卡片仍要授权行 → 拦截",
        False, "chore", "board",
        "chore(board): 结卡并顺手加一张\n\n自动来源：derive --close-cards",
        [], base, new_card,
    )
    case(
        "自动结卡仍不得混代码 → 拦截",
        False, "chore", "board",
        "chore(board): 结卡\n\n自动来源：derive --close-cards",
        ["src/Database.cpp"], base, baseline_changed,
    )

    # ---- DX-111：弃单终态结案通道 ----
    abandoned_close = ledger_with(baseline={"DX-01": ABANDONED_BASELINE})
    abandoned_mixed = ledger_with(
        cards={"DX-01": {"id": "a", "title": "改了标题", "stage": "DX"}},
        baseline={"DX-01": ABANDONED_BASELINE},
    )
    case(
        "弃单结案：chore(board) + 授权/批次 + baseline→abandoned → 放行",
        True, "chore", "board",
        "chore(board): 结案弃单卡\n\n授权：维护者 2026-11-05 会话弃单批准\n批次：DX-111",
        [], base, abandoned_close,
    )
    case(
        "弃单结案不适用自动来源：自动来源 + baseline→abandoned → 拦截",
        False, "chore", "board",
        "chore(board): 结案弃单卡\n\n自动来源：derive --close-cards",
        [], base, abandoned_close,
    )
    case(
        "冒充弃单改定义：baseline→abandoned 夹带改标题（无授权行）→ 拦截",
        False, "chore", "board",
        "chore(board): 结案弃单卡并顺手改标题",
        [], base, abandoned_mixed,
    )
    case(
        "冒充弃单改定义：带齐授权/批次行也拦",
        False, "chore", "board",
        "chore(board): 结案弃单卡并顺手改标题\n\n授权：维护者\n批次：DX-111",
        [], base, abandoned_mixed,
    )

    # ---- DX-40：流程豁免收口（process_commit_exempt 直测，4 例）----
    dx40 = [
        ("流程豁免：chore(claim) 只动台账 → 放行",
         process_commit_exempt("chore", "claim", ["docs/board-ledger.json"]) is True),
        ("流程豁免：chore(roadmap) 只动快照 → 放行",
         process_commit_exempt("chore", "roadmap", ["progress.json"]) is True),
        ("豁免收口：chore(claim) 卷入代码 → 不豁免",
         process_commit_exempt("chore", "claim",
                               ["docs/board-ledger.json", "src/main.cpp"]) is False),
        ("豁免收口：非流程类型不受豁免影响",
         process_commit_exempt("feat", "x", ["docs/a.md"]) is False),
    ]
    for name, ok in dx40:
        cases.append((name, True, [] if ok else ["process_commit_exempt 断言失败"]))

    # ---- DX-51：流程豁免收口到流程自身路径（杂务白名单文件也算夹带）----
    case(
        "代登记夹带杂务白名单文件（docs/）→ 拦截（DX-51）",
        False, "chore", "board",
        "chore(board): 登记并顺手补文档\n\n授权：维护者\n批次：DX-07",
        ["docs/pitfalls.md"], base, new_card,
    )
    dx51 = [
        ("流程豁免：chore(board) 不走 claim/roadmap 豁免（走审计通道）",
         process_commit_exempt("chore", "board", ["docs/board-ledger.json"]) is False),
        ("豁免收口：chore(claim) 卷入白名单脚本 → 不豁免（DX-51）",
         process_commit_exempt("chore", "claim",
                               ["docs/board-ledger.json", "scripts/tool.py"]) is False),
        ("豁免收口：chore(roadmap) 卷入白名单文档 → 不豁免（DX-51）",
         process_commit_exempt("chore", "roadmap",
                               ["progress.json", "docs/note.md"]) is False),
        ("越面兜底：chore(claim) 卷入白名单脚本仍要短号（DX-51）",
         flow_scope_beyond_paths("chore", "claim",
                                 ["docs/board-ledger.json", "scripts/tool.py"]) is True),
        ("越面兜底：chore(board) 只动台账不算越面",
         flow_scope_beyond_paths("chore", "board", ["docs/board-ledger.json"]) is False),
        ("越面兜底：非流程类型不受影响",
         flow_scope_beyond_paths("feat", "x", ["docs/a.md"]) is False),
    ]
    for name, ok in dx51:
        cases.append((name, True, [] if ok else ["flow 豁免收口断言失败"]))

    # ---- DX-97：新增卡必须带程序回执（手工改台账立卡通道关闭）----
    def receipt_card(code, title):
        return {"id": code, "title": title, "stage": "DX",
                "scope": ["scripts/x.py"], "acceptance": ["验收项"]}

    def receipt_payload(card):
        payload = json.dumps(card, ensure_ascii=False, sort_keys=True,
                             separators=(",", ":"))
        return hashlib.sha256(payload.encode("utf-8")).hexdigest()

    dx97_old = {"cards": {"DX-01": receipt_card("DX-01", "旧卡")},
                "baseline": {"DX-01": "done"}}
    dx97_card = receipt_card("DX-02", "新卡")
    dx97_new = {"cards": {"DX-01": receipt_card("DX-01", "旧卡"),
                          "DX-02": dx97_card},
                "baseline": {"DX-01": "done", "DX-02": "todo"}}
    good_receipt = {"card": "DX-02", "sha256": receipt_payload(dx97_card),
                    "authorizedBy": "维护者 13706",
                    "tool": "scripts/register_board_cards.py"}

    cases.append(("DX-97 回执：有合法回执 → 放行",
                  True, receipt_problems(dx97_old, dx97_new, {"DX-02": good_receipt})))
    cases.append(("DX-97 回执：缺回执 → 拒绝",
                  False, receipt_problems(dx97_old, dx97_new, {"DX-02": None})))
    cases.append(("DX-97 回执：哈希篡改 → 拒绝",
                  False, receipt_problems(
                      dx97_old, dx97_new,
                      {"DX-02": dict(good_receipt, sha256="0" * 64)})))
    cases.append(("DX-97 回执：缺授权来源 → 拒绝",
                  False, receipt_problems(
                      dx97_old, dx97_new,
                      {"DX-02": dict(good_receipt, authorizedBy="")})))
    dx97_paths = [
        ("DX-97 路径：回执目录属于流程合法路径",
         process_paths_ok([LEDGER.replace("\\", "/"),
                           RECEIPT_DIR + "/DX-02.json"]) is True),
        ("DX-97 路径：回执之外的白名单文件仍算越面",
         process_paths_ok([LEDGER.replace("\\", "/"), "docs/note.md"]) is False),
    ]
    for name, ok in dx97_paths:
        cases.append((name, True, [] if ok else ["process_paths_ok 断言失败"]))

    # ---- DX-105：numbering.mode 是任务定义，改动必须走审计通道 ----
    num_old = {"cards": {}, "baseline": {}, "numbering": {"mode": "remote"}}
    num_new = {"cards": {}, "baseline": {}, "numbering": {"mode": "local"}}
    num_delta = ledger_definition_changes(num_old, num_new)
    dx105 = [
        ("numbering 计入任务定义：无审计行 → 拦截（DX-105）",
         audit_problems("chore", "board",
                        "chore(board): 切本地发号", [], num_delta) != []),
        ("numbering 计入任务定义：带审计行 → 放行（DX-105）",
         audit_problems("chore", "board",
                        "chore(board): 切本地发号\n\n授权：维护者\n批次：DX-105",
                        [], num_delta) == []),
    ]
    for name, ok in dx105:
        cases.append((name, True, [] if ok else ["numbering 审计断言失败"]))

    failures = []
    for name, expect_ok, problems in cases:
        ok = not problems
        if ok == expect_ok:
            print("  ✓ %s（%s）" % (name, "放行" if ok else "拦截 %d 条" % len(problems)))
        else:
            print("  ✗ %s：期望%s，实际%s" % (
                name, "放行" if expect_ok else "拦截",
                "放行" if ok else "拦截 %d 条" % len(problems)))
            for line in problems:
                print("      - %s" % line)
            failures.append(name)

    if failures:
        sys.stderr.write("[任务闸门] 代登记审计自测失败 %d/%d 例\n" % (len(failures), len(cases)))
        return 1
    allowed = len([1 for _, expect_ok, _ in cases if expect_ok])
    print("[任务闸门] 代登记审计自测通过：%d 例（允许 %d / 拒绝 %d）"
          % (len(cases), allowed, len(cases) - allowed))
    return 0


def main():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    parser = argparse.ArgumentParser(description="任务闸门")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("scope", help="判定改动是否触及代码路径")
    p.add_argument("--base", required=True)
    p.add_argument("--head", required=True)
    p.set_defaults(func=cmd_scope)

    p = sub.add_parser("message", help="校验提交信息是否关联任务")
    p.add_argument("--file", required=True)
    p.set_defaults(func=cmd_message)

    p = sub.add_parser("ledger", help="校验台账只被追加了 claims / note")
    p.set_defaults(func=cmd_ledger)
    p = sub.add_parser("registrations", help="DX-121：回执/占位 ↔ 台账卡片双向一致性对账（需联网）")
    p.set_defaults(func=cmd_registrations)

    p = sub.add_parser("push", help="校验推送到主干的代码提交都有 claim ref")
    p.add_argument("--remote", required=True)
    p.add_argument("--local", required=True)
    p.set_defaults(func=cmd_push)

    p = sub.add_parser("selftest", help="代登记审计判定的可执行自测（DX-06）")
    p.set_defaults(func=cmd_selftest)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
