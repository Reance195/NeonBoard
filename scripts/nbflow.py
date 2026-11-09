#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""nbflow.py —— NeonBoard 开发会话派发程序（单一入口）。

为什么是「一个程序」而不是一串脚本
------------------------------------
立卡 / 领卡 / 开区 / 移交 / 反馈 / 结卡 / 清理以前散在 board-apply.py、
claim-card.py、agent-init.ps1、git-done.ps1、release-claims.py 里，靠人记顺序。
本程序把它们收口成**一个状态机 + 一组子命令**，是这些脚本的调用方，
不重复实现任何判定逻辑（task_gate.py / board-apply.py 仍是唯一规则实现）。

    request   agent 提交立卡请求 → docs/card-requests/<日期>-<slug>.md（状态 requested）；
              写单前按标题关键词查重，命中现有卡/请求须 --ack-duplicate 放行（DX-67）
    authorize 维护者给一次「会话级批量授权」，本会话后续 approve 自动复用
    approve   【已退役，DX-115】立卡唯一入口是 register_board_cards.py（宪章条款 6，
              DX-98 回执闸门）；调用即打印迁移 recipe 并退出码 9，会话状态不变
    dispatch  领卡与建分支移交是**一步**：一次 agent-init.ps1 -Card 调用 + handoff.json
    report    agent 反馈结构化执行回执，程序逐条核 acceptance 与 verify 退出码
    close     先试合并一次，无冲突才 git-done.ps1 合入 / 结卡 / 回收工作区
    anchor    锚点区先合并一次；阻塞/冲突则建冲突工单交回 agent（见下）
    abandon   弃卡时释放服务端 refs/claims 并回收工作区（补上原来的硬死锁）
    status    查看状态机、派发包与回执是否齐备

会话级批量授权（维护者拍板）
----------------------------
一次 ``authorize --by ... --batch ...`` 把凭证写进程序状态；此后本会话内
``approve`` 不必重复传 --authorize/--batch，程序仍把「授权：」「批次：」两行
写进**每一条** ``chore(board):`` 提交体 —— 凭证不因为批量而消失。
程序只搬运授权，不判断「这张卡该不该立」（board-apply.py:15）。

合并冲突 → 新工单交回 agent（锚点区问题）
------------------------------------------
程序**先合并一次**再决定是否合入：
  close / approve 调 git-done.ps1 之前，先在 .worktrees/_nbflow-probe-* 的临时
  detached worktree 里试 ``git merge --no-commit --no-ff <branch>``，收集未合并
  文件与冲突标记，随后 abort + 回收，绝不把冲突留在调用者检出或锚点区。
  anchor 子命令针对锚点区：先诊断（检出分支 / 落后领先 / 脏项 / root 占用），
  再真的执行**一次** ``git merge --ff-only origin/main``；成功即快进，失败不
  stash、不覆盖他人改动，只留证据（而不是像以前那样静默跳过）。
一旦探测到冲突或阻塞，程序不自行解决，而是：
  1. 生成 ``docs/card-requests/<日期>-conflict-<slug>.md``（DX-n 工单，沿用 §9
     已有的 DX 阶段以免新增 stage 触发全量；含冲突文件、双方提交、诊断、
     验收项与操作步骤，并写明锚点区不得由 agent 动手的 §7.1 约束）；
  2. 有会话级授权时经唯一入口 register_board_cards.py 自动立卡（专用立卡工作区，
     DX-127）并 dispatch 派回 agent（handoff.json 带 conflict 字段与 nextStep）；
     无授权则只留请求单，提示先 authorize —— 程序不自己发明批准；
  3. 原卡状态保持不变（可重跑 close），state 里记 conflictTicket；agent 在
     自己的 worktree 里合并、消除冲突标记、report 逐条填证据、close 上交。
退出码 5 = 「已建冲突工单并交回 agent，本次未合入」。

状态载体
--------
运行时状态在 ``<git-common-dir>/neonboard-flow/state.json``（不入库、跨 worktree 共享）。
任务定义仍只存在于 ``docs/board-ledger.json``，本程序不把它当状态库。
审计留痕仍走既有通道：提交信息、台账、refs/claims、合并提交体。
``--dry-run`` 的状态转移只在内存演算，不落盘也不执行任何外部 git/pwsh 命令
（不联网、不推送、不开区）：预演不留痕迹，预演后紧跟真跑不会被「非法状态转移」拒（DX-65）。
状态机是台账与 git 事实的**投影**：每个子命令入口先隐式对账（缺会话补影、僵尸对齐），
删掉 state.json 各子命令照常工作（DX-65）；重建同时消费服务端 refs/claims 领用占位，
已领用卡不因状态文件丢失退回 created（DX-73）——否则 report/close 全被拒，
回路被逼绕过（DX-66 实录）。

用法
----
    python scripts/nbflow.py --help
    python scripts/nbflow.py authorize --by "维护者 2026-11-04 会话" --batch "FLOW-01~04"
    python scripts/nbflow.py request --card FLOW-01 --stage FLOW --title "..." ^
        --scope "scripts/**" --acceptance "python scripts/nbflow.py --selftest 通过" --evidence "..."
    python scripts/nbflow.py approve --cards FLOW-01
    python scripts/nbflow.py dispatch --card FLOW-01 --agent agent-a --branch feature/xxx
    python scripts/nbflow.py report --card FLOW-01 --run-verify --result "<验收项>=pass:证据"
    python scripts/nbflow.py close --card FLOW-01
    python scripts/nbflow.py abandon --card FLOW-01 --reason "上游卡已弃"
    python scripts/nbflow.py anchor --report-only        # 只看锚点诊断，先试合并一次
    python scripts/nbflow.py anchor --agent agent-conflict
    python scripts/nbflow.py anchor --simulate-conflict --dry-run   # 演练冲突工单
    python scripts/nbflow.py status
    python scripts/nbflow.py --selftest
    python scripts/nbflow.py <任意子命令> --dry-run    # 演练：不执行外部命令、不写仓库文件

退出码：0 成功 / 1 校验或状态不合法 / 2 外部命令失败 / 3 用法错误 / 4 读取失败
        / 5 已建冲突工单并交回 agent（本次未合入）。
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "scripts"))
from acceptance_split import FIELD_SEPARATORS, flatten_entries, split_subitems  # noqa: E402  拆分规则单一实现（DX-84）


def _git_common_dir() -> Path:
    """请求单与派生状态的落盘基点：跨 worktree 共享、天然在 git 元数据目录里（DX-47）。"""
    try:
        proc = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--git-common-dir"],
                              capture_output=True, text=True, encoding="utf-8", errors="replace")
        if proc.returncode == 0 and proc.stdout.strip():
            p = Path(proc.stdout.strip().splitlines()[0])
            return p if p.is_absolute() else (REPO / p).resolve()
    except OSError:
        pass
    return (REPO / ".git").resolve()


REQUEST_DIR = _git_common_dir() / "neonboard-flow" / "requests"
ARCHIVE_REQUEST_DIR = REPO / "docs" / "card-requests"  # 归档副本，只读扫描（DX-47）
STATE_VERSION = 1

# 与 scripts/board-apply.py:77 / task_gate.py 保持同一口径
CARD_ID_RE = re.compile(r"^(?:[A-Za-z]{1,4}\d*-\d+|M\d+)$")
BRANCH_RE = re.compile(
    r"^(feature|fix|hotfix|chore|docs|refactor|perf|test|build|ci|style|revert|workbuddy)"
    r"/[a-z0-9][a-z0-9-]*$")
SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
CODE_SUFFIXES = (".cpp", ".h", ".hpp", ".qml", "CMakeLists.txt")

STATES = ("requested", "accepted", "approved", "created", "dispatched",
          "running", "reported", "closed", "cleaned", "abandoned", "rejected", "blocked")
# 允许的状态转移；dispatch 直接从 created → dispatched（领卡与移交是一步，不设中间态）。
# blocked 是「步骤失败停放态」：进哪步失败就停在哪步后面，出路对准入局原因——
# approve 落盘失败 → blocked→accepted 重批（DX-72），dispatch 开区失败 →
# blocked→dispatched 重跑领卡（DX-75：created 必须能进 blocked，否则失败处理
# 自己撞「非法状态转移」，卡永远停不进停放态）。
TRANSITIONS = {
    "requested": {"accepted", "approved", "rejected", "abandoned"},
    "accepted": {"approved", "rejected", "blocked", "abandoned"},
    "approved": {"created", "blocked", "abandoned"},
    "created": {"dispatched", "blocked", "abandoned", "requested"},   # requested 仅 requeue 用（台账确无此卡，DX-57）
    "dispatched": {"running", "reported", "abandoned"},
    "running": {"reported", "abandoned"},
    "reported": {"closed", "running", "abandoned"},
    "closed": {"cleaned"},
    "cleaned": set(),
    "abandoned": {"requested"},
    "rejected": {"requested"},
    "blocked": {"accepted", "dispatched", "abandoned"},
}


def now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def rel_or_abs(p: Path) -> str:
    """state.json 里的路径尽量相对 REPO；linked worktree 下请求单落在 common dir，不在工作树里。"""
    try:
        return str(p.relative_to(REPO))
    except ValueError:
        return str(p)


def out(text: str = "") -> None:
    print(text)


def err(text: str) -> None:
    print(text, file=sys.stderr)


class Ctx:
    def __init__(self, dry_run: bool = False):
        self.dry_run = dry_run
        self._common: Path | None = None

    @property
    def common(self) -> Path:
        if self._common is None:
            code, text = run(["git", "-C", str(REPO), "rev-parse", "--git-common-dir"],
                             capture=True, dry=False, quiet=True)
            if code != 0 or not text.strip():
                raise RuntimeError("取不到 git-common-dir（不是 git 仓库？）")
            p = Path(text.strip().splitlines()[0])
            self._common = p if p.is_absolute() else (REPO / p).resolve()
        return self._common

    @property
    def state_file(self) -> Path:
        return self.common / "neonboard-flow" / "state.json"


def run(argv: list[str], cwd: Path | None = None, capture: bool = False,
        dry: bool = False, check: bool = False, quiet: bool = False) -> tuple[int, str]:
    """唯一的外部命令出口。dry-run 只打印，不执行；绝不 shell=True。"""
    display = "  $ " + " ".join(shlex.quote(str(a)) for a in argv)
    if dry:
        out(display + "   [dry-run]")
        return 0, ""
    if not quiet:
        out(display)
    proc = subprocess.run([str(a) for a in argv], cwd=str(cwd or REPO),
                          capture_output=capture, text=True,
                          encoding="utf-8", errors="replace")
    text = (proc.stdout or "") if capture else ""
    if not capture and proc.stdout and not quiet:
        out(proc.stdout.rstrip())
    if proc.stderr and not quiet:
        (err if proc.returncode else out)(proc.stderr.rstrip())
    if check and proc.returncode != 0:
        raise RuntimeError("命令失败（退出码 %d）：%s" % (proc.returncode, " ".join(map(str, argv))))
    return proc.returncode, text


def ps_script(name: str, params: list[str], root: Path = REPO) -> list[str]:
    """定位 pwsh；没有则按 DX-12 回退 Windows PowerShell 5.1 + Bypass。

    root：脚本所在检出。git-done 一类按「自身路径」定位仓库的脚本，必须用
    目标检出自己的那份跑，否则它会把调用者检出当成操作对象（DX-45）。
    """
    exe = shutil.which("pwsh")
    if exe:
        base = [exe, "-NoProfile", "-File"]
    else:
        base = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File"]
    return base + [str(root / "scripts" / name)] + params


def py_script(name: str, params: list[str]) -> list[str]:
    return [sys.executable, str(REPO / "scripts" / name)] + params


# ---------------------------------------------------------------- 状态文件

def blank_state() -> dict:
    return {"version": STATE_VERSION, "updatedAt": now(), "sessions": {}, "authorizations": []}


def load_state(ctx: Ctx) -> dict:
    p = ctx.state_file
    if not p.exists():
        return blank_state()
    try:
        data = json.loads(p.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise RuntimeError("状态文件损坏：%s（%s）" % (p, exc))
    data.setdefault("sessions", {})
    data.setdefault("authorizations", [])
    return data


def save_state(ctx: Ctx, state: dict) -> None:
    state["updatedAt"] = now()
    if ctx.dry_run:
        # dry-run 纯化（DX-65）：状态转移只在内存演算。以前 dry-run 也写 state.json
        # （在 .git/ 下）好让逐条演练串起来，代价是预演后紧跟真跑会被自己留下的
        # 状态拒掉（「非法状态转移」，DX-65 实录）；投影化后状态可由事实重建，
        # 演练痕迹不再值得留盘。
        return
    p = ctx.state_file
    p.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix="state-", suffix=".json", dir=str(p.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            json.dump(state, fh, ensure_ascii=False, indent=2)
        os.replace(tmp, p)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise


def transition(ctx: Ctx, state: dict, card: str, target: str, **fields) -> None:
    sess = state["sessions"].setdefault(card, {"state": "requested", "history": []})
    cur = sess["state"]
    if target != cur and target not in TRANSITIONS.get(cur, set()):
        raise RuntimeError("非法状态转移：%s %s → %s（允许：%s）"
                           % (card, cur, target, ", ".join(sorted(TRANSITIONS.get(cur, set()))) or "无"))
    sess["state"] = target
    sess["updatedAt"] = now()
    sess.update(fields)
    sess.setdefault("history", []).append({"at": now(), "from": cur, "to": target})
    out("  · %s: %s → %s" % (card, cur, target))
    save_state(ctx, state)


# ---------------------------------------------------------------- 请求单

def parse_request(text: str) -> dict:
    """解析 --- front matter ---；多值字段用 ';' 分隔。"""
    lines = text.splitlines()
    if not lines or lines[0].strip() != "---":
        raise ValueError("请求单缺少 front matter（第一行必须是 ---）")
    end = next((i for i in range(1, len(lines)) if lines[i].strip() == "---"), None)
    if end is None:
        raise ValueError("front matter 没有结束标记 ---")
    data: dict = {}
    for line in lines[1:end]:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if ":" not in line:
            raise ValueError("front matter 行缺少冒号：%s" % line)
        key, val = line.split(":", 1)
        data[key.strip()] = val.strip()
    data["_body"] = "\n".join(lines[end + 1:]).strip()
    return data


def render_request(req: dict) -> str:
    order = ("state", "card", "stage", "title", "author", "evidence",
             "scope", "acceptance", "depends", "suggestedId", "rejectReason", "createdAt")
    head = "\n".join("%s: %s" % (k, req.get(k, "")) for k in order)
    body = req.get("_body") or req.get("body", "")
    if not body:
        body = ("## 目标\n%s\n\n## 验收\n%s\n\n## 边界\n%s\n\n## 输入\n基线 origin/main；依赖：%s\n\n"
                "## 产出\n合并提交 body 四段（变更 / 验收 / 影响 / 卡片）\n"
                % (req.get("title", ""), req.get("acceptance", ""),
                   req.get("scope", ""), req.get("depends", "")))
    return "---\n%s\n---\n\n%s\n" % (head, body)


def split_multi(value: str) -> list[str]:
    """请求单多值字段拆分；口径与验收拆分同一实现（DX-84），但不含竖线 —— scope/depends 的 | 不是分隔符。"""
    return split_subitems(value, FIELD_SEPARATORS)


def request_path(card: str, slug: str = "") -> Path:
    date = datetime.now().strftime("%Y%m%d")
    name = "%s-%s.md" % (date, slug or card.lower())
    return REQUEST_DIR / name


def find_request(card: str) -> Path | None:
    if not REQUEST_DIR.is_dir():
        return None
    for p in sorted(REQUEST_DIR.glob("*.md")):
        try:
            if parse_request(p.read_text(encoding="utf-8")).get("card", "").upper() == card.upper():
                return p
        except (OSError, ValueError):
            continue
    return None


def request_to_manifest(req: dict) -> dict:
    """把请求单整理成 board-apply.py 吃的五段式卡片（不替人判断该不该立）。"""
    card = req.get("card", "").upper()
    problems = validate_manifest_card(card, req)
    if problems:
        raise RuntimeError("请求单 %s 不合格：\n  - %s" % (card, "\n  - ".join(problems)))
    return {
        "id": card,
        "stage": req.get("stage", "").strip(),
        "title": req.get("title", "").strip(),
        "note": "触发证据：%s\n\n%s" % (req.get("evidence", ""), req.get("_body", "")),
        "scope": split_multi(req.get("scope", "")),
        # DX-84：立卡即结构化。创建侧只认 ;／；——与 board-apply 的 acceptance.unsplit
        # 闸门同一口径（| 在文案里可能是正文，如「竖线| 三形态」，DX-84 自己实录被误切）；
        # 请求单里要写含 | 的验收命令请加引号。report 侧对存量单串（含 DX-62 竖线）仍全形态兼容。
        "acceptance": split_multi(req.get("acceptance", "")),
        "depends": split_multi(req.get("depends", "")),
    }


def validate_manifest_card(card: str, req: dict) -> list[str]:
    problems: list[str] = []
    if not CARD_ID_RE.match(card):
        problems.append("卡号 %r 不符合 task_gate.py 正则" % card)
    for key in ("stage", "title", "scope", "acceptance"):
        if not req.get(key, "").strip():
            problems.append("缺少 %s" % key)
    if not split_multi(req.get("acceptance", "")):
        problems.append("acceptance 至少要有一条")
    if not split_multi(req.get("scope", "")):
        problems.append("scope 至少要有一条")
    return problems


def acceptance_has_mark(item: str) -> bool:
    """与 board-apply.py 的 acceptance.prose 同口径：至少有一个可执行记号。"""
    return bool(re.search(r"[A-Za-z0-9_./\\-]", item or ""))


# ---------------------------------------------------------------- 会话级授权

def authorization(state: dict, by: str = "", batch: str = "", ttl_hours: int = 24) -> dict:
    """authorize 时写一条；approve 时取最近一条未过期的。凭证不因批量而省略。"""
    if by and batch:
        rec = {"id": uuid.uuid4().hex[:8], "by": by, "batch": batch,
               "issuedAt": now(),
               "expiresAt": (datetime.now(timezone.utc) + timedelta(hours=ttl_hours)).isoformat(timespec="seconds")}
        state["authorizations"].append(rec)
        return rec
    valid = [a for a in reversed(state["authorizations"])
             if a.get("expiresAt", "") > now() and a.get("by") and a.get("batch")]
    if not valid:
        raise RuntimeError("没有有效的会话级授权：先跑 authorize --by ... --batch ...，"
                           "或在 approve 时直接给 --authorize/--batch")
    return valid[0]


# ---------------------------------------------------------------- 命令实现

def cmd_authorize(ctx: Ctx, state: dict, args) -> int:
    if not args.by or not args.batch:
        err("✗ authorize 需要 --by（授权来源）与 --batch（批次）")
        return 3
    rec = authorization(state, args.by, args.batch, args.ttl_hours)
    out("  · 会话级批量授权已登记：%s / %s（有效至 %s）" % (rec["by"], rec["batch"], rec["expiresAt"]))
    save_state(ctx, state)
    return 0


def cmd_request(ctx: Ctx, state: dict, args) -> int:
    # 编号自动分配（DX-55）：--card 省略时按 stage 命名空间从台账取下一个空号；
    # 显式给号时也查台账占用——请求阶段就拦，别等 approve 才炸。
    # 占用集含台账 + 活队列 + 在途会话（DX-57：只看台账会撞队列/会话内占号，DX-56 实录）。
    stage = (args.stage or "DX").upper()
    used = ledger_card_ids() | queued_card_ids() | {
        k for k, v in state["sessions"].items()
        if v.get("state") not in ("abandoned", "rejected")
    }
    if args.card:
        card = args.card.upper()
        if not CARD_ID_RE.match(card):
            err("✗ 卡号不合规：%s" % card)
            return 1
        if card in used:
            err("✗ 卡号已被台账占用：%s（省略 --card 由程序取号，或换号）" % card)
            return 1
    else:
        card = next_card_id(used, stage)
        out("  · 程序取号：%s（%s 命名空间）" % (card, stage))
    if find_request(card):
        err("✗ 已有 %s 的请求单：%s（驳回或换号）" % (card, find_request(card)))
        return 1
    if card in state["sessions"] and state["sessions"][card]["state"] not in ("abandoned", "rejected"):
        err("✗ 卡 %s 已在流程中（%s）" % (card, state["sessions"][card]["state"]))
        return 1
    slug = args.slug or re.sub(r"[^a-z0-9]+", "-", card.lower()).strip("-")
    if not SLUG_RE.match(slug):
        err("✗ slug 不合规：%s" % slug)
        return 1
    # 语义查重（DX-67）：DX-51 教训——立卡只查号段占用没查痛点重叠，与已交付卡重复。
    # 查重是提示不是闸门，但放行必须显式（--ack-duplicate 留在调用方命令里）。
    dup = duplicate_hits(args.title or "", known_titles(exclude=card))
    if dup:
        for cid, t, shared in dup[:8]:
            out("  · 疑似重复：%s %s（共同：%s）" % (cid, t, "、".join(shared)))
        if len(dup) > 8:
            out("  · …另有 %d 条疑似重复" % (len(dup) - 8))
        if not args.ack_duplicate:
            err("✗ 标题命中 %d 条疑似重复卡/请求；确认是新痛点请加 --ack-duplicate 放行" % len(dup))
            return 1
        out("  · --ack-duplicate 已放行（%d 条疑似重复仅提示）" % len(dup))
    req = {
        "state": "requested", "card": card, "stage": args.stage, "title": args.title,
        "author": args.author, "evidence": args.evidence, "scope": args.scope,
        "acceptance": args.acceptance, "depends": args.depends,
        "suggestedId": card, "rejectReason": "", "createdAt": now(),
    }
    problems = validate_manifest_card(card, req)
    if problems:
        err("✗ 请求单不合格：\n  - " + "\n  - ".join(problems))
        return 1
    p = request_path(card, slug)
    if ctx.dry_run:
        out("  · [dry-run] 将写请求单 %s\n%s" % (p, render_request(req)))
    else:
        REQUEST_DIR.mkdir(parents=True, exist_ok=True)
        p.write_text(render_request(req), encoding="utf-8")
        out("  · 请求单：%s" % p)
    state["sessions"][card] = {"state": "requested", "request": rel_or_abs(p),
                               "history": [{"at": now(), "from": "-", "to": "requested"}]}
    save_state(ctx, state)
    return 0


def _write_manifest(cards: list[dict], dry: bool) -> tuple[str, list[str]]:
    """生成一份 board-apply.py 可吃的 JSON 清单；dry-run 不落盘。"""
    manifest = {c["id"]: {k: v for k, v in c.items() if k != "id"} for c in cards}
    text = json.dumps(manifest, ensure_ascii=False, indent=2)
    if dry:
        out("  · [dry-run] 清单内容：\n" + text)
        return "<dry-run-manifest>", []
    fd, path = tempfile.mkstemp(prefix="nbflow-batch-", suffix=".json")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path, [path]


def _board_worktree(ctx: Ctx, batch_slug: str) -> Path:
    return REPO / ".worktrees" / ("_nbflow-board-" + batch_slug)


def worktree_alive(wt: Path, common: Path) -> bool:
    """目录存在 ≠ 检出可用（DX-93）：半删除的 worktree 尸体没有 .git 指针，在里面跑
    ``git -C`` 会沿目录树向上落到别的仓库——实测把锚点检出从 main 静默切走。复用前
    必须验：脚本在、注册可达、且注册指向本仓库的 common dir。"""
    if not (wt / "scripts" / "board-apply.py").is_file():
        return False
    code, text = run(["git", "-C", str(wt), "rev-parse", "--git-common-dir"],
                     capture=True, quiet=True)
    if code != 0 or not text.strip():
        return False
    p = Path(text.strip().splitlines()[0])
    if not p.is_absolute():
        p = (wt / p).resolve()
    try:
        return p.samefile(common)
    except OSError:
        return False


def discard_corpse_worktree(ctx: Ctx, wt: Path) -> bool:
    """验活失败后的清理：prune 注销残 ref + 删目录；删不掉（被占用）返回 False，
    调用方必须中止（fail-closed），绝不带着尸体继续（DX-93）。"""
    return remove_dir_with_retry(ctx, wt, reason="尸体探测区清理")


# DX-112：nbflow 自己经手的回收（probe 区 / 尸体区 / abandon）失败不再静默降级 ——
# 实录（2026-11-06）：worktree remove Permission denied 只打一行「不影响主线」，
# _nbflow-board-2026-11-06 目录与分支残留，靠人肉 ls 才发现。这里统一为：
# 重试至少 2 次（瞬态占用自愈）→ 仍失败落 pending-recycle.json 显式留痕并大声点名，
# 回收决定仍归人（§7.10），nbflow 只负责「让人看见」。
PENDING_RECYCLE_ATTEMPTS = 3  # 首次 + 重试 2 次


def pending_recycle_file(ctx: Ctx) -> Path:
    return ctx.common / "neonboard-flow" / "pending-recycle.json"


def record_pending_recycle(ctx: Ctx, wt: Path, reason: str) -> None:
    """回收终败的显式留痕：独立于会话状态（失败点可能根本没有会话），
    落在 common dir 的 pending-recycle.json；agent-workspace -Action Check 会点名。"""
    if ctx.dry_run:
        return
    p = pending_recycle_file(ctx)
    try:
        data = json.loads(p.read_text(encoding="utf-8"))
        if not isinstance(data.get("items"), dict):
            data["items"] = {}
    except (OSError, ValueError):
        data = {"version": 1, "items": {}}
    data["updatedAt"] = now()
    data["items"][rel_or_abs(wt)] = {"reason": reason, "recordedAt": now()}
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def _default_worktree_remove(ctx: Ctx, wt: Path) -> bool:
    """一次回收尝试：git 注销 + 兜底 rmtree；目录已不存在视为成功。"""
    if not wt.exists():
        return True
    run(["git", "-C", str(REPO), "worktree", "prune"], quiet=True)
    run(["git", "-C", str(REPO), "worktree", "remove", "--force", str(wt)], quiet=True)
    if wt.exists():
        try:
            shutil.rmtree(wt)
        except OSError:
            pass
    return not wt.exists()


def remove_dir_with_retry(ctx: Ctx, wt: Path, reason: str = "工作区回收",
                          attempts: int = PENDING_RECYCLE_ATTEMPTS,
                          remove_fn=None) -> bool:
    """带重试的目录回收：失败重试 ≥2 次，仍失败落显式待回收记录（DX-112）。

    remove_fn 仅供自测注入；生产路径走 _default_worktree_remove。
    """
    remove = remove_fn or (lambda: _default_worktree_remove(ctx, wt))
    for i in range(1, max(attempts, 1) + 1):
        try:
            if remove():
                if i > 1:
                    out("  · %s：第 %d 次重试后成功" % (reason, i))
                return True
        except OSError:
            pass
        if i < attempts:
            out("  · ⚠ %s 失败（第 %d/%d 次，多为文件被占用），重试…" % (reason, i, attempts))
    record_pending_recycle(ctx, wt, reason=reason)
    err("✗ %s 失败：重试 %d 次仍删不掉 %s；已落待回收记录（pending-recycle.json），"
        "请人工处理 —— 回收由人拍板（§7.10），但残留必须被看见（DX-112）"
        % (reason, attempts, rel_or_abs(wt)))
    return False


def board_apply_outcome(code: int) -> str:
    """board-apply 退出码归类（DX-72）：ok=已落盘；noop=台账已有所需卡，无需变更；fail=其余一律失败。

    3 是 board-apply 的「无需变更」信号（清单里的卡台账全都有 / 应用后无差异），
    旧实现把落盘阶段的 3 当失败，重复批准 / 并行会话抢先落盘都会把会话卡死。
    """
    if code == 0:
        return "ok"
    if code == 3:
        return "noop"
    return "fail"


def _mark_created(ctx: Ctx, state: dict, card: str, p: Path, auth: dict, branch: str) -> None:
    """落盘确认后回写请求单并推进会话；approved→created 两跳都发生在台账落盘之后（DX-72）。"""
    data = parse_request(p.read_text(encoding="utf-8")) if p.exists() else {}
    data["state"] = "approved"
    data["approvedBy"] = auth["by"]
    data["batch"] = auth["batch"]
    if not ctx.dry_run:
        p.write_text(render_request(data), encoding="utf-8")
    transition(ctx, state, card, "approved")
    transition(ctx, state, card, "created", boardBranch=branch)


# DX-115：approve 的立卡通道退役。DX-98 起 task_gate 要求新增卡必须带程序回执
# （docs/card-requests/registered/<短号>.json），而 board-apply 不产回执——经此通道的
# 立卡提交会被 commit-msg 拒（退出码 9 暗失败，DX-114 立卡时实录两次）。宪章条款 6 已把
# register_board_cards.py 定为唯一入口，这里显式退役并给出可执行的 recipe，
# 不再让调用方撞闸门猜原因。request / dedup / 授权机器不涉及回执，继续可用。
APPROVE_RETIRE_NOTICE = """✗ nbflow approve 的立卡通道已退役（DX-115）：DX-98 的回执闸门要求新增卡带程序回执，
  而 board-apply 不产回执，经此通道的立卡提交会被 commit-msg 拒（退出码 9 暗失败）。
  立卡唯一入口（宪章条款 6，DX-98）：
    1) 把活队列请求单复制进仓库：cp <.git/neonboard-flow/requests/<单>.md> docs/card-requests/<同名>.md
    2) python scripts/register_board_cards.py --form docs/card-requests/<单>.md --authorized-by "<授权原文>"
    3) 按工具打印的两段式指引提交（docs(board) 收录 + chore(board) 立卡），git-done 合入
  request（查重）/ status / dispatch / report / close 等子命令不受影响；本次会话状态未变更。"""


def cmd_approve(ctx: Ctx, state: dict, args) -> int:
    err(APPROVE_RETIRE_NOTICE)
    return 9


def _read_card(card: str) -> dict:
    """派发包里的卡片定义以 origin/main 的台账为准（与 agent-init 的领用判定同源）。"""
    code, text = run(["git", "-C", str(REPO), "show", "origin/main:docs/board-ledger.json"],
                     capture=True, quiet=True)
    if code != 0 or not text.strip():
        text = (REPO / "docs" / "board-ledger.json").read_text(encoding="utf-8")
    return json.loads(text).get("cards", {}).get(card, {})


def ledger_doc() -> dict | None:
    """origin/main 台账原文；离线退回工作区副本，再退 None（DX-57 对账用）。"""
    code, text = run(["git", "-C", str(REPO), "show", "origin/main:docs/board-ledger.json"],
                     capture=True, quiet=True)
    if code != 0 or not text.strip():
        p = REPO / "docs" / "board-ledger.json"
        text = p.read_text(encoding="utf-8") if p.exists() else ""
    try:
        return json.loads(text)
    except ValueError:
        return None


def ledger_card_ids() -> set[str]:
    """origin/main 台账的全部卡号；离线退回工作区副本，再退空集（DX-55 取号用）。

    DX-92：并入本地 main（锚点检出）的台账——锚点可能有未推送的立卡（DX-88 实录：
    45e1e93 停在锚点，origin/main 的 DX-74 却被另一任务占用），只看 origin/main 会把
    同一卡号配给两个不同任务。并集是保守方向：多拒一个号，换号即可。
    """
    return ledger_ids_from_docs(ledger_doc(), local_main_doc())


def local_main_doc() -> dict | None:
    """本地 main（锚点检出）的台账原文；取不到退 None（DX-92）。"""
    code, text = run(["git", "-C", str(REPO), "show", "main:docs/board-ledger.json"],
                     capture=True, quiet=True)
    if code != 0 or not text.strip():
        return None
    try:
        return json.loads(text)
    except ValueError:
        return None


def ledger_ids_from_docs(*docs: dict | None) -> set[str]:
    """纯函数：若干份台账文档的卡号并集（DX-92 自测口径）。"""
    ids: set[str] = set()
    for d in docs:
        if d:
            ids |= set(d.get("cards", {}))
    return ids


def queued_card_ids() -> set[str]:
    """活队列请求单的卡号（DX-57：取号占用集漏了这里就会撞队列内占号）。"""
    used: set[str] = set()
    if REQUEST_DIR.is_dir():
        for p in REQUEST_DIR.glob("*.md"):
            try:
                card = parse_request(p.read_text(encoding="utf-8")).get("card", "")
            except (OSError, ValueError):
                continue
            if card:
                used.add(card.upper())
    return used


def claim_refs() -> dict[str, str]:
    """服务端 refs/claims/* 领用占位快照：卡号 → 占位指向的提交（DX-73）。

    服务端可达性以 ls-remote 为准（与 agent-init 的防双领同源）；不可达退回本地
    镜像（git-done / task_gate 会 fetch 回来）——镜像可能含孤儿占位，但投影本就
    fail-soft，宁可用略旧的证据，也别把已领用卡重建成 created（DX-66 实录）。
    """
    code, text = run(["git", "-C", str(REPO), "ls-remote", "origin", "refs/claims/*"],
                     capture=True, quiet=True)
    if code != 0:
        out("  · 服务端 claim 占位不可达（离线？），退回本地 refs/claims 镜像（可能含孤儿）")
        code, text = run(["git", "-C", str(REPO), "for-each-ref",
                          "--format=%(objectname) %(refname)", "refs/claims/"],
                         capture=True, quiet=True)
        if code != 0:
            return {}
    refs: dict[str, str] = {}
    for line in (text or "").splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[1].startswith("refs/claims/"):
            refs[parts[1][len("refs/claims/"):].upper()] = parts[0]
    return refs


def recover_claim_context(card: str, head: str) -> dict:
    """按领用现场恢复分支 / 工作区字段（尽力而为；都拿不到也不影响状态本身，DX-73）。

    ① 派发包：扫各 worktree 的 handoff.json，卡号对上即恢复 branch/worktree/sessionId
       （锚点区根 = git common dir 父目录，.worktrees 只在那里；report/close 常从
       linked worktree 里调 nbflow，按 REPO 扫会扫空）；
    ② 分支历史：占位指向的提交仍在某条分支上且**未**并入 origin/main 时取该分支——
       已并入 main 的领用现场不可靠（之后从 main 新切的分支都「包含」它），不猜。
    """
    facts: dict = {}
    wt_root = _git_common_dir().parent / ".worktrees"
    if wt_root.is_dir():
        for d in sorted(wt_root.iterdir()):
            if not d.is_dir() or d.name.startswith("_nbflow-"):
                continue
            try:
                data = json.loads(handoff_path(str(d)).read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            if (data or {}).get("card", "").upper() == card.upper():
                facts.update(worktree=str(d), branch=data.get("branch", ""),
                             agent=data.get("agent", ""),
                             sessionId=data.get("sessionId", ""))
                break
    if not facts.get("branch") and head:
        code, _ = run(["git", "-C", str(REPO), "merge-base", "--is-ancestor",
                       head, "origin/main"], check=False, quiet=True)
        if code != 0:
            code, text = run(["git", "-C", str(REPO), "for-each-ref", "--contains", head,
                              "--format=%(refname)", "refs/heads/", "refs/remotes/origin/"],
                             capture=True, quiet=True)
            names = [ln.strip() for ln in (text or "").splitlines() if ln.strip()] if code == 0 else []
            local = [n for n in names if n.startswith("refs/heads/") and n != "refs/heads/main"]
            remote = [n for n in names if n.startswith("refs/remotes/origin/")
                      and n != "refs/remotes/origin/main" and not n.endswith("/HEAD")]
            if local:
                facts["branch"] = local[0][len("refs/heads/"):]
            elif remote:
                facts["branch"] = remote[0][len("refs/remotes/origin/"):]
    return {k: v for k, v in facts.items() if v}


# ---------------------------------------------------------------- 语义查重（DX-67）

STAGE_PREFIX_RE = re.compile(r"^(?:dx|r\d+|m\d+|mod)$")


def title_tokens(title: str) -> set[str]:
    """标题关键词：ASCII 词（≥2 字符，小写）+ CJK 连续段的二元组。

    stage 前缀（dx / r3 / m01 / mod）几乎每张卡的标题都带，剔掉防全员命中。
    """
    toks: set[str] = set()
    for m in re.finditer(r"[A-Za-z0-9][A-Za-z0-9_.-]*", title or ""):
        tok = m.group(0).lower().strip("-._")
        if len(tok) >= 2 and not STAGE_PREFIX_RE.match(tok):
            toks.add(tok)
    for m in re.finditer(r"[\u4e00-\u9fff]+", title or ""):
        run = m.group(0)
        if len(run) == 1:
            toks.add(run)
        else:
            toks.update(run[i:i + 2] for i in range(len(run) - 1))
    return toks


def duplicate_hits(title: str, known: list[tuple[str, str]],
                   min_shared: int = 3) -> list[tuple[str, str, list[str]]]:
    """疑似重复判定：与既有卡/请求的共同关键词 ≥ min_shared 即命中。

    阈值 3 按本仓库语料校准：DX-51↔DX-40（≥6 共同词）与 DX-69↔DX-39（3 共同词）
    都必须命中，而「自定义」拆出的 自定/定义 这类通用二元组不再误报。
    返回 [(卡号, 已有标题, 共同关键词)]，共同词多者在前；纯函数，便于自测。
    """
    want = title_tokens(title)
    hits: list[tuple[str, str, list[str]]] = []
    for cid, t in known:
        shared = sorted(want & title_tokens(t))
        if len(shared) >= min_shared:
            hits.append((cid, t, shared))
    hits.sort(key=lambda h: (-len(h[2]), h[0]))
    return hits


def known_titles(exclude: str = "") -> list[tuple[str, str]]:
    """查重语料：台账 cards（origin/main + 本地 main，DX-92）+ 活队列 + 归档请求单
    （DX-51 的重复痛点就停在已交付卡里）。

    同卡号在队列与归档各有一份时按卡号去重，台账版本优先。
    """
    corpus: list[tuple[str, str]] = []
    seen: set[str] = set()
    for doc in (ledger_doc(), local_main_doc()):
        for cid, c in ((doc or {}).get("cards") or {}).items():
            t = (c or {}).get("title") or ""
            if cid != exclude and t and cid not in seen:
                corpus.append((cid, t))
                seen.add(cid)
    for d in (REQUEST_DIR, ARCHIVE_REQUEST_DIR):
        if not d.is_dir():
            continue
        for p in d.glob("*.md"):
            try:
                req = parse_request(p.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            cid = (req.get("card") or "").upper()
            t = req.get("title") or ""
            if cid and cid != exclude and t and cid not in seen:
                corpus.append((cid, t))
                seen.add(cid)
    return corpus


def handoff_path(worktree: str) -> Path:
    return Path(worktree) / ".sandbox" / "artifacts" / "handoff.json"


def assignment_store(*argv: str) -> None:
    """旁路调用 assignment-store（DX-149 唯一写者）。失败只提示，不影响主流程——
    assignments 不参与任何闸门判定。"""
    store = Path(__file__).resolve().parent / "assignment-store.py"
    try:
        code, _ = run([sys.executable, str(store), *argv], capture=True)
        if code != 0:
            out("  · assignment-store 更新失败（旁路，不阻断）：exit %d" % code)
    except OSError:
        out("  · assignment-store 不可达（旁路，不阻断）")


def report_path(worktree: str) -> Path:
    return Path(worktree) / ".sandbox" / "artifacts" / "run-report.json"


def dispatch_argv(ctx: Ctx, card: str, agent: str, branch: str, base: str = "") -> list[str]:
    """dispatch 的 agent-init 命令行。root 必须是锚点区 = git common dir 父目录（DX-89）：
    领卡语义 = 在锚点建 .worktrees/<agent>，与调用位置无关——从 linked worktree 里跑
    nbflow 时若用调用者检出的副本，会触发 §7.10 的自动原地模式把领用塞回调用者检出
    （in-place 守卫拒绝，exit 3；close → 冲突工单自动派发这条链因此恒挂，DX-85 实录）。
    handoff 记录的现场路径同理，也要落在锚点，否则 report/close 找不到派发包。
    """
    params = ["-Agent", agent, "-Branch", branch, "-Card", card]
    if base:
        params += ["-Base", base]
    return ps_script("agent-init.ps1", params, root=ctx.common.parent)


def cmd_dispatch(ctx: Ctx, state: dict, args) -> int:
    """领卡 + 建分支 + 移交 = 一次 agent-init.ps1 -Card 调用 + handoff.json。"""
    card = args.card.upper()
    if not BRANCH_RE.match(args.branch):
        err("✗ 分支名不符合宪章前缀规范：%s" % args.branch)
        return 1
    sess = state["sessions"].get(card)
    if not sess or sess["state"] not in ("created", "blocked"):
        err("✗ %s 不处于 created/blocked（当前：%s）；先 approve，或从 blocked 重跑本命令（DX-75）"
            % (card, sess["state"] if sess else "无"))
        return 1
    code, _ = run(dispatch_argv(ctx, card, args.agent, args.branch, args.base), dry=ctx.dry_run)
    if code != 0:
        err("✗ 开区失败（退出码 %d）：离线 / 占用冲突 / 已有同名分支，见上方诊断" % code)
        transition(ctx, state, card, "blocked", nextStep="排查 agent-init 失败原因后重跑 dispatch")
        return 2
    wt = str(ctx.common.parent / ".worktrees" / args.agent)
    card_def = _read_card(card)
    handoff = {
        "card": card, "branch": args.branch, "agent": args.agent,
        "worktree": wt, "sandbox": str(Path(wt) / ".sandbox"),
        "sessionId": uuid.uuid4().hex[:12], "issuedAt": now(),
        "cardInfo": {
            "title": card_def.get("title", ""), "stage": card_def.get("stage", ""),
            "scope": card_def.get("scope", []), "acceptance": card_def.get("acceptance", []),
            "depends": card_def.get("depends", []),
        },
        "envFile": str(Path(wt) / ".agent-env.ps1"),
        "nextStep": "dot-source .agent-env.ps1，先看 NB_SCOPE，收尾跑 verify.ps1",
    }
    extra = getattr(args, "extra", None)
    if extra:
        handoff.update(extra)
    hp = handoff_path(wt)
    if ctx.dry_run:
        out("  · [dry-run] 将写派发包 %s\n%s" % (hp, json.dumps(handoff, ensure_ascii=False, indent=2)))
    else:
        hp.parent.mkdir(parents=True, exist_ok=True)
        hp.write_text(json.dumps(handoff, ensure_ascii=False, indent=2), encoding="utf-8")
        out("  · 派发包：%s" % hp)
    transition(ctx, state, card, "dispatched", branch=args.branch, agent=args.agent,
               worktree=wt, sessionId=handoff["sessionId"], handoff=str(hp))
    return 0


# 结果词紧跟在「=」后（可带空白，后随冒号或串尾）才是分割点（DX-70）：
# 键与证据都可能含「=」，rsplit / lsplit 都会取错 —— 按结果词定位两种形态都稳。
_RESULT_SPLIT_RE = re.compile(r"=\s*(pass|fail|defer)\s*(?::|$)", re.IGNORECASE)


def flatten_acceptance(entries: list[str]) -> list[str]:
    """存量卡把多条验收写进一条串（DX-62 的竖线 / DX-73 起的全角分号形态）：拆成子项再映射序号。

    拆分规则唯一实现在 acceptance_split（DX-84）：引号内分隔符不切，
    幂等（拆过的数组再进来不动）。report 侧对存量单串卡兼容展开，
    立卡侧（request_to_manifest）同一规则直接落数组。
    """
    return flatten_entries(entries)


def parse_results(items: list[str], acceptance: list[str] | None = None) -> dict[str, dict]:
    """'<验收项>=pass:证据' → {验收项: {result, evidence}}。

    验收项可以照抄原文，也可以用序号 ``1=pass:证据`` 指向 acceptance[0]，
    避免长验收命令里的等号与转义把 agent 绕晕。
    分割点取「= 后紧跟结果词」的第一处（DX-70）：键或证据含等号都不会误切；
    竖线分隔的存量 acceptance 先拆子项，序号映射按子项走。
    ``defer`` 表示「待合并后核验」（发版卡的 Release 资产 / update.json / 真机升级
    这类结构性排在合并之后的验收项，DX-61）：必须给出非空理由，close 时会大声打印
    并留痕，但不算失败 —— 把待核验混成 pass 才是这里要防的。
    """
    acceptance = flatten_acceptance(acceptance or [])
    parsed: dict[str, dict] = {}
    for raw in items:
        m = _RESULT_SPLIT_RE.search(raw)
        if not m:
            raise ValueError("结果格式应为 <验收项或序号>=pass|fail|defer:证据，收到 %r" % raw)
        key, tail = raw[:m.start()], raw[m.end():]
        key = key.strip()
        if key.isdigit():
            if not acceptance or not (1 <= int(key) <= len(acceptance)):
                raise ValueError("序号 %s 超出 acceptance 范围（共 %d 条）"
                                 % (key, len(acceptance or [])))
            key = acceptance[int(key) - 1]
        if tail.startswith(":"):
            evidence = tail[1:]
        else:
            evidence = tail
        result = m.group(1).lower()
        evidence = evidence.strip()
        if result == "defer" and not evidence:
            raise ValueError("defer 必须给出待核验的理由（合并前核不了什么、由谁核）：%r" % raw)
        parsed[key] = {"result": result, "evidence": evidence}
    return parsed


def check_report(acceptance: list[str], results: dict, verify_exit: int,
                 unfinished: list[str]) -> list[str]:
    """程序验收：逐条 acceptance 必须有结论，verify 必须为 0，未完成项必须为空。

    结论取值 pass / fail / defer（DX-61）：fail 与缺结论照旧拦下；defer 是发版类卡
    「待合并后核验」的合法结论，不算问题，但会带进回执，由 close 大声打印并留痕。
    """
    problems: list[str] = []
    for item in acceptance:
        if item not in results:
            problems.append("缺验收结论：%s" % item)
        elif results[item]["result"] == "fail":
            problems.append("验收未通过：%s（%s）" % (item, results[item]["evidence"]))
    for key in results:
        if key not in acceptance:
            problems.append("回执里出现卡片未声明的验收项：%s" % key)
    if verify_exit != 0:
        problems.append("verify 退出码为 %d（应为 0）" % verify_exit)
    if unfinished:
        problems.append("仍有未完成项：%s" % "; ".join(unfinished))
    return problems


def assignment_store(*argv: str, dry: bool = False) -> None:
    """旁路调用 assignment-store（DX-149 唯一写者）。失败只提示，不影响主流程——
    assignments 不参与任何闸门判定。dry-run 纯化（DX-65）：只打印不执行。"""
    store = Path(__file__).resolve().parent / "assignment-store.py"
    code, _ = run([sys.executable, str(store), *argv], capture=True, dry=dry)
    if not dry and code != 0:
        out("  · assignment-store 更新失败（旁路，不阻断）：exit %d" % code)


def cmd_report(ctx: Ctx, state: dict, args) -> int:
    card = args.card.upper()
    sess = state["sessions"].get(card)
    if not sess or sess["state"] not in ("dispatched", "running", "reported"):
        err("✗ %s 不在可执行态（当前：%s）" % (card, sess["state"] if sess else "无"))
        return 1
    wt = sess.get("worktree", "")
    hp = handoff_path(wt)
    if not ctx.dry_run and not hp.exists():
        err("✗ 找不到派发包 %s；先 dispatch" % hp)
        return 4
    handoff = json.loads(hp.read_text(encoding="utf-8")) if hp.exists() else {
        "cardInfo": {"acceptance": [], "scope": []}, "card": card, "branch": sess.get("branch", ""),
        "worktree": wt, "sessionId": sess.get("sessionId", "")}
    acceptance = handoff.get("cardInfo", {}).get("acceptance", [])
    if not acceptance:
        # 演练时 handoff.json 未落盘、或存量卡没写 acceptance 时，从请求单取同一份验收项
        rq = find_request(card)
        if rq:
            acceptance = split_multi(parse_request(rq.read_text(encoding="utf-8")).get("acceptance", ""))
    # 竖线分隔的存量卡（DX-62 形态）拆成子项，序号映射与逐条核验都按子项走（DX-70）
    acceptance = flatten_acceptance(acceptance)
    results: dict[str, dict] = {}
    if args.results_json:
        results = json.loads(Path(args.results_json).read_text(encoding="utf-8"))
    results.update(parse_results(args.result or [], acceptance or None))
    if not acceptance and results:
        # 存量卡未声明 acceptance 时按回执项核验，别让「多余验收项」把交付卡死
        acceptance = list(results.keys())
        out("  · 卡片未声明 acceptance，按回执项核验（建议由维护者补齐卡片定义）")
    verify_exit = args.verify_exit
    if args.run_verify:
        mode = args.verify_mode
        params = ["-ScriptOnly"] if mode == "script" else []
        code, _ = run(ps_script("verify.ps1", params), cwd=Path(wt), dry=ctx.dry_run)
        verify_exit = code
    problems = check_report(acceptance, results, verify_exit, split_multi(args.unfinished))
    if problems:
        err("✗ 回执不合格：\n  - " + "\n  - ".join(problems))
        transition(ctx, state, card, "running", nextStep="补齐验收结论后重跑 report")
        return 1
    rep = {
        "card": card, "branch": handoff.get("branch", ""), "sessionId": handoff.get("sessionId", ""),
        "reportedAt": now(), "acceptance": acceptance,
        "results": results, "verifyExitCode": verify_exit,
        "verifyCommand": "pwsh -File scripts/verify.ps1" + (" -ScriptOnly" if args.verify_mode == "script" else ""),
        "unfinished": [], "nextStep": args.next_step or "可进入 close",
        "allPass": True,
        # DX-61：defer 不算失败，但要随回执带走，close 时大声打印并留痕
        "deferred": sorted(k for k, v in results.items() if v.get("result") == "defer"),
    }
    rp = report_path(wt)
    if ctx.dry_run:
        out("  · [dry-run] 将写执行回执 %s\n%s" % (rp, json.dumps(rep, ensure_ascii=False, indent=2)))
    else:
        rp.parent.mkdir(parents=True, exist_ok=True)
        rp.write_text(json.dumps(rep, ensure_ascii=False, indent=2), encoding="utf-8")
        out("  · 执行回执：%s" % rp)
    if rep["deferred"]:
        out("  · ⚠ %d 项验收「待核验」（defer），结卡 ≠ 已核验：" % len(rep["deferred"]))
        for k in rep["deferred"]:
            out("      - %s：%s" % (k, results[k].get("evidence", "")))
    transition(ctx, state, card, "reported", report=str(rp),
               nextStep=rep["nextStep"], allPass=True, deferred=len(rep["deferred"]))
    # DX-149：报告完成即把 nextStep 刷进 assignment（§7.2「收工必写剩余工作」强制项）
    assignment_store("heartbeat", "--code", card, "--force", "--stage", "报告完成",
                     "--next-step", rep["nextStep"] or "", dry=ctx.dry_run)
    return 0


def cmd_close(ctx: Ctx, state: dict, args) -> int:
    card = args.card.upper()
    sess = state["sessions"].get(card)
    if not sess or sess["state"] != "reported":
        err("✗ %s 不在 reported（当前：%s）；先 report" % (card, sess["state"] if sess else "无"))
        return 1
    wt = sess.get("worktree", "")
    rp = report_path(wt)
    deferred: list[str] = []
    if not ctx.dry_run:
        if not rp.exists():
            err("✗ 找不到执行回执 %s" % rp)
            return 4
        rep = json.loads(rp.read_text(encoding="utf-8"))
        if not rep.get("allPass") or rep.get("verifyExitCode") != 0 or rep.get("unfinished"):
            err("✗ 回执未全绿，拒绝结卡")
            return 1
        # DX-61：defer 项不拦 close（发版类卡的验收结构性排在合并后），但必须大声说出来
        deferred = sorted(k for k, v in (rep.get("results") or {}).items()
                          if v.get("result") == "defer")
        if deferred:
            out("  · ⚠ %d 项验收「待核验」（defer），结卡 ≠ 已核验：" % len(deferred))
            for k in deferred:
                out("      - %s：%s" % (k, (rep.get("results") or {}).get(k, {}).get("evidence", "")))
    branch = sess.get("branch", args.branch)
    if not branch:
        err("✗ 缺分支名（--branch）")
        return 3
    code, text = run(["git", "-C", str(REPO), "log", branch, "--oneline", "--grep",
                      "#%s=done" % card], capture=True, dry=ctx.dry_run)
    if code == 0 and not ctx.dry_run and "#%s=done" % card not in (text or ""):
        err("✗ 分支历史里没有 #%s=done；git-done 靠它自动结卡，请先补标记或传 --force-close" % card)
        return 1
    if not getattr(args, "no_probe", False):
        probe = probe_merge(ctx, branch)
        if not probe["ok"]:
            out("  · 先合并一次发现冲突：%s"
                % (", ".join(probe["conflicts"][:5]) or probe["reason"]))
            _ticket, tcode = raise_conflict_ticket(ctx, state, branch=branch, info=probe,
                                                   agent=getattr(args, "conflict_agent", CONFLICT_AGENT),
                                                   source_card=card)
            return 5 if tcode == 0 else tcode
    params = ["-Branch", branch]
    if args.force_close:
        params += ["-Close", card]
    code, _ = run(ps_script("git-done.ps1", params), dry=ctx.dry_run)
    if code != 0:
        err("✗ 合入 / 结卡 / 清理失败（退出码 %d）；状态保持 reported，可重跑" % code)
        return 2
    if deferred:
        transition(ctx, state, card, "closed",
                   nextStep="已交付；%d 项验收 defer 待核验（见执行回执）" % len(deferred),
                   deferCount=len(deferred))
    else:
        transition(ctx, state, card, "closed")
    transition(ctx, state, card, "cleaned", cleanedAt=now(),
               nextStep="已交付，工作区由 git-done 回收")
    out("%s %s 已结卡并清理" % ("[演练] 未执行 ·" if ctx.dry_run else "✓", card))
    return 0


def cmd_abandon(ctx: Ctx, state: dict, args) -> int:
    """弃卡出口：释放服务端占位 + 本地 ref + 回收 worktree + 删分支 + 留痕。"""
    card = args.card.upper()
    sess = state["sessions"].get(card)
    if not sess:
        err("✗ 没有 %s 的会话记录" % card)
        return 1
    if sess["state"] in ("cleaned", "abandoned"):
        out("  · %s 已是 %s，无需重复弃卡" % (card, sess["state"]))
        return 0
    if not args.reason:
        err("✗ 弃卡必须给 --reason（留痕）")
        return 3
    wt = sess.get("worktree", "")
    branch = sess.get("branch", "")
    if wt and Path(wt).is_dir() and not args.force:
        code, text = run(["git", "-C", wt, "status", "--porcelain"], capture=True, dry=ctx.dry_run)
        if code == 0 and text.strip():
            err("✗ 工作区有未提交改动，拒绝回收（确认丢弃请加 --force）：\n%s" % text.strip())
            return 1
    ref = "refs/claims/%s" % card
    code, _ = run(["git", "-C", str(REPO), "push", "origin", "--delete", ref], dry=ctx.dry_run)
    if code != 0:
        out("  · 服务端占位不存在或已释放（忽略）")
    run(["git", "-C", str(REPO), "update-ref", "-d", ref], dry=ctx.dry_run)
    if wt and Path(wt).is_dir():
        if not remove_dir_with_retry(ctx, Path(wt), reason="弃卡工作区回收"):
            transition(ctx, state, card, "abandoned", reason=args.reason,
                       nextStep="工作区回收失败，已落 pending-recycle.json 待人工处理（DX-112）")
            out("%s %s 已弃卡并释放占位，但工作区回收失败（原因：%s）"
                % ("[演练] 未执行 ·" if ctx.dry_run else "!", card, args.reason))
            return 0
    if branch:
        run(["git", "-C", str(REPO), "branch", "-d" if not args.force else "-D", branch],
            dry=ctx.dry_run)
    transition(ctx, state, card, "abandoned", reason=args.reason,
               nextStep="服务端占位已释放；台账 claims 投影由下次 agent-init 判失效清理")
    # DX-149：弃卡即清接手现场（占位已释放，assignment 无存在意义）
    assignment_store("remove", "--code", card, dry=ctx.dry_run)
    out("%s %s 已弃卡并释放占位（原因：%s）"
        % ("[演练] 未执行 ·" if ctx.dry_run else "✓", card, args.reason))
    return 0


# ------------------------------------------- 合并冲突 → 自动建工单交回 agent

CONFLICT_STAGE = "DX"     # 路线图 §9 已有阶段；新增 stage 会触发 check_board_diff 全量档
CONFLICT_PREFIX = "DX"
CONFLICT_AGENT = "agent-conflict"


def _slug(text: str, limit: int = 30) -> str:
    return re.sub(r"[^a-z0-9]+", "-", (text or "").lower()).strip("-")[:limit] or "conflict"


def anchor_root(ctx: Ctx) -> Path:
    """锚点区 = git common dir 的父目录；在任意 worktree 里都能定位。"""
    code, text = run(["git", "-C", str(REPO), "rev-parse", "--git-common-dir"],
                     capture=True, quiet=True)
    if code == 0 and text.strip():
        p = Path(text.strip().splitlines()[0])
        if not p.is_absolute():
            p = (REPO / p).resolve()
        return p.parent
    return REPO.parent


def probe_merge(ctx: Ctx, branch: str, simulate: bool = False) -> dict:
    """程序先合并一次：临时 detached worktree 里试合并，收集冲突证据后原样清理。

    不碰调用者检出、不碰锚点区；试完一定 abort + 回收，绝不留下冲突标记。
    """
    info = {"branch": branch, "ok": True, "conflicts": [], "evidence": "",
            "ours": "", "theirs": "", "reason": ""}
    if simulate:
        info.update(ok=False, reason="simulated-merge-conflict",
                    conflicts=["docs/board-ledger.json"],
                    evidence="演练用模拟冲突：docs/board-ledger.json 三路合并撞车",
                    ours="origin/main", theirs=branch)
        return info
    if ctx.dry_run:
        out("  · [dry-run] 将先试合并 %s → origin/main（不建探测区）" % branch)
        return info
    wt = REPO / ".worktrees" / ("_nbflow-probe-" + _slug(branch))
    try:
        if wt.exists() and not worktree_alive(wt, ctx.common):
            # DX-93：尸体探测区先清干净；清不掉会让下面的 add 失败走 probe-setup-failed
            if not remove_dir_with_retry(ctx, wt, reason="尸体探测区清理"):
                info.update(ok=False, reason="probe-setup-failed",
                            evidence="旧探测区清不掉：%s（见 pending-recycle.json）" % wt)
                return info
        if wt.exists():
            run(["git", "-C", str(REPO), "worktree", "remove", "--force", str(wt)], quiet=True)
        code, _ = run(["git", "-C", str(REPO), "worktree", "add", "--detach",
                       str(wt), "origin/main"], quiet=True)
        if code != 0:
            info.update(ok=False, reason="probe-setup-failed",
                        evidence="临时探测区建立失败：%s" % wt)
            return info
        _, ours = run(["git", "-C", str(wt), "rev-parse", "--short", "HEAD"],
                      capture=True, quiet=True)
        _, theirs = run(["git", "-C", str(REPO), "rev-parse", "--short", branch],
                        capture=True, quiet=True)
        info["ours"], info["theirs"] = ours.strip(), theirs.strip()
        code, _ = run(["git", "-C", str(wt), "merge", "--no-commit", "--no-ff", branch],
                      quiet=True)
        _, unmerged = run(["git", "-C", str(wt), "diff", "--name-only", "--diff-filter=U"],
                          capture=True, quiet=True)
        conflicts = [x.strip() for x in (unmerged or "").splitlines() if x.strip()]
        if code != 0 or conflicts:
            snippets = []
            for rel in conflicts[:8]:
                fp = wt / rel
                if fp.is_file():
                    marks = [ln.strip() for ln in
                             fp.read_text(encoding="utf-8", errors="replace").splitlines()
                             if ln.startswith(("<<<<<<<", "=======", ">>>>>>>"))][:6]
                    snippets.append("%s: %s" % (rel, " / ".join(marks) or "无冲突标记"))
            info.update(ok=False, reason="merge-conflict", conflicts=conflicts,
                        evidence="; ".join(snippets) or ("git merge 退出码 %d" % code))
        return info
    finally:
        if wt.exists():
            run(["git", "-C", str(wt), "merge", "--abort"], quiet=True)
            remove_dir_with_retry(ctx, wt, reason="探测区回收")


def anchor_probe(ctx: Ctx, simulate: bool = False) -> dict:
    """锚点区：先诊断，再真的快进合并一次；失败不 stash、不覆盖他人改动，只留证据。"""
    root = anchor_root(ctx)
    info = {"anchor": str(root), "branch": "", "ahead": 0, "behind": 0,
            "dirty": [], "nonSnapshotDirty": [], "rootClaim": None,
            "merged": False, "ok": True, "reason": "", "evidence": "",
            "conflicts": []}
    _, b = run(["git", "-C", str(root), "symbolic-ref", "--short", "HEAD"],
               capture=True, quiet=True)
    info["branch"] = b.strip()
    _, lr = run(["git", "-C", str(root), "rev-list", "--left-right", "--count",
                 "HEAD...origin/main"], capture=True, quiet=True)
    parts = (lr or "0 0").split()
    if len(parts) == 2 and all(x.isdigit() for x in parts):
        info["ahead"], info["behind"] = int(parts[0]), int(parts[1])
    _, st = run(["git", "-C", str(root), "status", "--porcelain"], capture=True, quiet=True)
    dirty = []
    for line in (st or "").splitlines():
        p = line[3:].strip()
        if " -> " in p:
            p = p.split(" -> ", 1)[1]
        if p:
            dirty.append(p)
    info["dirty"] = dirty
    # status --porcelain 对未跟踪目录会输出 'docs/'，它不是文件，不能进工单 scope
    info["nonSnapshotDirty"] = [p for p in dirty
                                if p != "progress.json" and not p.endswith("/")]
    try:
        ws = json.loads((ctx.common / "agent-workspace.json").read_text(encoding="utf-8"))
        info["rootClaim"] = ws.get("root")
    except (OSError, ValueError):
        pass
    if simulate:
        info.update(ok=False, reason="simulated-anchor-block",
                    conflicts=info["nonSnapshotDirty"] or ["progress.json"],
                    evidence="演练用模拟阻塞：锚点落后 %d / 领先 %d，脏项 %s"
                             % (info["behind"], info["ahead"],
                                ", ".join(info["nonSnapshotDirty"]) or "无"))
        return info
    if ctx.dry_run:
        out("  · [dry-run] 将先试一次 git merge --ff-only origin/main（不执行）")
        return info
    if info["branch"] != "main":
        info.update(ok=False, reason="anchor-not-main",
                    conflicts=info["nonSnapshotDirty"],
                    evidence="锚点检出 %s；按 §7.1 不在此切分支或合并" % info["branch"])
        return info
    code, _ = run(["git", "-C", str(root), "merge", "--ff-only", "origin/main"], quiet=True)
    if code == 0:
        info["merged"] = True
        return info
    claim = info["rootClaim"]
    claim_agent = claim.get("agent", "无") if isinstance(claim, dict) else "无"
    info.update(ok=False, reason="ff-only-blocked",
                conflicts=info["nonSnapshotDirty"] or ["progress.json"],
                evidence="落后 %d / 领先 %d；脏项：%s；root 占用：%s；ff-only 退出码 %d"
                         % (info["behind"], info["ahead"],
                            ", ".join(info["dirty"]) or "无", claim_agent, code))
    return info


def next_conflict_card(state: dict) -> str:
    """冲突工单沿用 §9 已有的 DX 阶段与编号，自动避开已占用号。"""
    used = set(state.get("sessions", {})) | ledger_card_ids()
    if REQUEST_DIR.is_dir():
        for p in REQUEST_DIR.glob("*.md"):
            try:
                used.add(parse_request(p.read_text(encoding="utf-8")).get("card", "").upper())
            except (OSError, ValueError):
                continue
    return pick_conflict_card(used)


def next_card_id(used: set[str], stage: str = CONFLICT_STAGE) -> str:
    """纯函数：按 stage 命名空间从已占用卡号里挑下一个空号，便于自测，不碰 git（DX-55）。

    命名空间：DX / R<阶段> / MOD 用 <前缀>-nn，M 用 Mnn；未知 stage 兜底 DX。
    """
    s = (stage or CONFLICT_STAGE).upper()
    if s == "M":
        pat, fmt = r"^M(\d+)$", "M{:02d}"
    elif re.match(r"^(?:DX|MOD|R\d+)$", s):
        pat, fmt = r"^%s-(\d+)$" % s, "%s-{:02d}" % s
    else:
        pat, fmt = r"^DX-(\d+)$", "DX-{:02d}"
    nums = [int(m.group(1)) for c in used
            for m in [re.match(pat, c)] if m]
    return fmt.format(max(nums) + 1 if nums else 1)


def pick_conflict_card(used: set[str]) -> str:
    return next_card_id(used, CONFLICT_STAGE)


def conflict_ticket_body(card: str, branch: str, info: dict,
                         scope: list[str], acceptance: list[str]) -> str:
    anchor_note = ""
    if info.get("anchor"):
        claim = info.get("rootClaim")
        claim_agent = claim.get("agent", "无") if isinstance(claim, dict) else "无"
        anchor_note = ("\n## 锚点区约束\n"
                       "- 锚点 %s 检出 %s，落后 %d / 领先 %d；root 占用：%s\n"
                       "- 程序不 stash、不覆盖、不在锚点区改代码（§7.1）；本地改动由维护者/占用者处理，\n"
                       "  agent 只在自己的 worktree 里解决合并并把结果合回主线，让锚点后续能自然快进。\n"
                       % (info["anchor"], info.get("branch", ""), info.get("behind", 0),
                          info.get("ahead", 0), claim_agent))
    return "\n".join([
        "## 来源",
        "由 nbflow 自动建单：程序先合并一次，遇到冲突/阻塞不自行解决，交回 agent 合并后上交。",
        "", "## 冲突证据",
        "- 触发：%s" % info.get("reason", ""),
        "- 分支：%s（ours %s / theirs %s）" % (branch, info.get("ours", ""), info.get("theirs", "")),
        "- 冲突/脏项文件：%s" % (", ".join(info.get("conflicts", [])) or "无"),
        "- 诊断：%s" % (info.get("evidence", "") or "无"),
        anchor_note,
        "", "## 目标",
        "由 agent 在自己的 worktree 里完成合并、消除冲突标记，再合回主线并结卡上交。",
        "", "## 步骤",
        "1. dot-source .agent-env.ps1，先看 NB_SCOPE / NB_ACCEPTANCE",
        "2. git fetch origin; git merge --no-ff %s（或基于最新 origin/main 重建合并）" % branch,
        "3. 台账冲突用 scripts/merge_board.py / scripts/merge-resolve.ps1 辅助，禁止留冲突标记",
        "4. 提交信息带 #%s=done，合并体写四段（变更/验收/影响/卡片）" % card,
        "5. 跑 verify.ps1，再用 nbflow report 逐条填证据，最后 nbflow close 上交",
        "", "## 验收", "; ".join(acceptance),
        "", "## 边界", "; ".join(scope),
        "", "## 交接",
        "原卡保持原状态（可重跑 close）；本工单 cleaned 后再重跑原卡的 close。",
    ])


def raise_conflict_ticket(ctx: Ctx, state: dict, *, branch: str, info: dict,
                          agent: str, source_card: str = "") -> tuple[str, int]:
    """把冲突建成一张新工单，立卡并派发回 agent；无会话授权时只留请求单。"""
    card = next_conflict_card(state)
    conflicts = [p for p in info.get("conflicts", []) if (REPO / p).exists()]
    scope = conflicts[:8] or ["docs/**", "scripts/**"]
    acceptance = [
        "冲突文件无 <<<<<<< 标记",
        "git merge --no-ff %s 完成" % branch,
        "pwsh -File scripts/verify.ps1 -ScriptOnly 通过",
        "合并结果用 merge 提交合回 origin/main",
        "工单 note 记录冲突文件与解决方式",
    ]
    evidence = ("reason=%s branch=%s behind=%s files=%s %s"
                % (info.get("reason", ""), branch, info.get("behind", ""),
                   ",".join(info.get("conflicts", [])[:8]), info.get("evidence", "")))
    evidence = " ".join(evidence.split())[:480]
    title = ("锚点区合并阻塞：快进 origin/main 未通过（程序自动建单）" if info.get("anchor")
             else "合并冲突：%s 与 origin/main（程序自动建单）" % branch)
    req = {
        "state": "requested", "card": card, "stage": CONFLICT_STAGE,
        "title": title,
        "author": "nbflow", "evidence": evidence,
        "scope": ";".join(scope), "acceptance": ";".join(acceptance),
        "depends": "", "suggestedId": card, "rejectReason": "", "createdAt": now(),
        "body": conflict_ticket_body(card, branch, info, scope, acceptance),
    }
    p = request_path(card, "conflict-" + _slug(branch))
    if not ctx.dry_run:
        REQUEST_DIR.mkdir(parents=True, exist_ok=True)
        p.write_text(render_request(req), encoding="utf-8")
    out("  · 冲突工单请求单：%s（%s）" % (p, card))
    rel = rel_or_abs(p)
    state["sessions"][card] = {"state": "requested", "request": rel,
                               "conflictOf": source_card or branch,
                               "requestText": render_request(req),
                               "history": [{"at": now(), "from": "-", "to": "requested"}]}
    if source_card and source_card in state["sessions"]:
        state["sessions"][source_card]["conflictTicket"] = card
        state["sessions"][source_card]["nextStep"] = "等 %s 解决合并后重跑 close" % card
    save_state(ctx, state)
    # 程序不自己发明批准：没有会话级授权就停在 requested，等人授权后再走立卡
    try:
        auth = authorization(state)
    except RuntimeError as exc:
        err("  · %s" % exc)
        out("  · 工单已建但未立卡/未派发；请维护者 authorize 后按 DX-115 recipe 立卡："
            "register_board_cards.py --form %s" % rel)
        return card, 0
    # DX-127：approve 退役（DX-115）后工单自动立卡改走唯一入口 register_board_cards.py，
    # 沿用旧 approve 的隔离模式——专用立卡工作区 + chore/board-* 分支，表单落
    # docs/card-requests/，两段提交后 git-done 合回主线，不污染调用者检出。
    # 工单自己的立卡分支不再试合并：否则冲突会再生成一张工单，形成递归。
    slug = "conflict-" + _slug(branch)
    reg_branch = "chore/board-" + slug
    wt = _board_worktree(ctx, slug)
    if not ctx.dry_run:
        if wt.exists() and not worktree_alive(wt, ctx.common):
            if not discard_corpse_worktree(ctx, wt):
                err("✗ 尸体目录删不掉（被占用），请手工删除后重跑：%s" % wt)
                return card, 2
        if not wt.exists():
            code, _ = run(["git", "-C", str(REPO), "worktree", "add", "-b", reg_branch,
                           str(wt), "origin/main"], check=False)
            if code != 0:
                err("✗ 建立立卡工作区失败（%s 已存在？先手工回收）" % wt)
                return card, 2
        else:
            run(["git", "-C", str(wt), "checkout", "-B", reg_branch, "origin/main"], check=True)
        form_dir = wt / "docs" / "card-requests"
        form_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, form_dir / p.name)
    code, reg_out = run([sys.executable, str(wt / "scripts" / "register_board_cards.py"),
                         "--form", "docs/card-requests/%s" % p.name,
                         "--authorized-by", auth["by"], "--allow-new-stage"],
                        cwd=wt, capture=True, dry=ctx.dry_run)
    if code != 0:
        err("✗ 冲突工单立卡失败（退出码 %d）；register 输出末段：\n%s" % (code, reg_out[-800:]))
        return card, code
    actual = card
    m = re.search(r"已登记 (\S+)：", reg_out)
    if m:
        actual = m.group(1)
    if actual != card:
        # register 按台账重配了号（并发发号窗口）：表单、会话、派发全部跟新号走（DX-55 同款）
        for f in (p, wt / "docs" / "card-requests" / p.name):
            if f.exists():
                data = parse_request(f.read_text(encoding="utf-8"))
                data["card"] = actual
                data["suggestedId"] = actual
                f.write_text(render_request(data), encoding="utf-8")
        state["sessions"][actual] = state["sessions"].pop(card)
        if source_card and state["sessions"].get(source_card, {}).get("conflictTicket") == card:
            state["sessions"][source_card]["conflictTicket"] = actual
        card = actual
        save_state(ctx, state)
    if not ctx.dry_run:
        # 两段提交（register 只落盘并打印指引，不提交；形态即其打印的 recipe）
        run(["git", "-C", str(wt), "add", "docs/card-requests/%s" % p.name,
             "docs/iteration-roadmap.md"], check=False)
        run(["git", "-C", str(wt), "commit", "-m",
             "docs(board): 收录 %s 立卡请求单" % actual], check=False)
        run(["git", "-C", str(wt), "add", "docs/board-ledger.json",
             "progress.json", "docs/card-requests/registered/%s.json" % actual], check=False)
        code, _ = run(["git", "-C", str(wt), "commit", "-m", "chore(board): 立卡 %s" % actual,
                       "-m", "授权：%s" % auth["by"], "-m", "批次：%s" % actual], check=False)
        if code != 0:
            err("✗ 工单立卡提交失败；立卡分支留在 %s（%s）" % (reg_branch, wt))
            return card, code
        # 合回主线：git-done 在立卡工作区自己的检出里跑（DX-45），从不推工作分支
        code, _ = run(ps_script("git-done.ps1", ["-Branch", reg_branch], root=wt), cwd=wt,
                      dry=ctx.dry_run)
        if code != 0:
            err("✗ 工单立卡合回主线失败（可能撞并行收尾）；立卡分支仍在 %s" % wt)
            return card, code
        run(["git", "-C", str(REPO), "worktree", "remove", str(wt)], check=False)
    _mark_created(ctx, state, card, p, auth, reg_branch)
    out("%s 冲突工单已立卡并合回主线：%s" % ("[演练] 未落盘 ·" if ctx.dry_run else "✓", card))
    dbranch = "fix/conflict-" + _slug(branch)
    dispatch_args = argparse.Namespace(card=card, agent=agent, branch=dbranch, base="",
                                       dry_run_sub=ctx.dry_run,
                                       extra={"conflict": info,
                                              "nextStep": "在派发 worktree 里解决 %s 合并冲突，"
                                                          "report 逐条填证据后 close 上交" % branch})
    code = cmd_dispatch(ctx, state, dispatch_args)
    if code != 0:
        err("✗ 冲突工单派发失败（%d）；请手工：nbflow dispatch --card %s --agent %s --branch %s"
            % (code, card, agent, dbranch))
        return card, code
    out("✓ 冲突已建工单 %s 并交回 %s（分支 %s）；agent 合并后 report → close 再上交"
        % (card, agent, dbranch))
    return card, 0


def cmd_anchor(ctx: Ctx, state: dict, args) -> int:
    """锚点区问题：先合并一次，阻塞/冲突则自动建工单交回 agent。"""
    out("→ 锚点区：先合并一次，阻塞则建工单交回 agent")
    info = anchor_probe(ctx, simulate=args.simulate_conflict)
    claim = info["rootClaim"]
    claim_agent = claim.get("agent", "无") if isinstance(claim, dict) else "无"
    out("  · 检出 %s，落后 %d / 领先 %d，脏项 %d（非快照 %d），root 占用 %s"
        % (info["branch"] or "detached", info["behind"], info["ahead"],
           len(info["dirty"]), len(info["nonSnapshotDirty"]), claim_agent))
    if info["ok"]:
        out("✓ %s" % ("锚点已快进到 origin/main" if info.get("merged")
                      else "锚点无需合并（演练/诊断通过）"))
        return 0
    out("  · 先合并一次未通过：%s —— %s" % (info["reason"], info["evidence"][:200]))
    if args.report_only:
        out("  · --report-only：只诊断，未建工单")
        return 0
    _card, code = raise_conflict_ticket(ctx, state, branch="origin/main", info=info,
                                        agent=args.agent)
    return code


def cmd_status(ctx: Ctx, state: dict, args) -> int:
    out("nbflow 状态机（%s）" % ctx.state_file)
    auths = [a for a in state["authorizations"] if a.get("expiresAt", "") > now()]
    out("有效会话授权：%s" % (", ".join("%s/%s" % (a["by"], a["batch"]) for a in auths) or "无"))
    if not state["sessions"]:
        out("（无会话。用 request 提交立卡请求）")
        return 0
    for card, s in sorted(state["sessions"].items()):
        wt = s.get("worktree", "")
        bits = ["state=%s" % s["state"]]
        for key in ("branch", "agent", "worktree", "nextStep"):
            if s.get(key):
                bits.append("%s=%s" % (key, s[key]))
        if wt:
            bits.append("handoff=%s" % ("有" if handoff_path(wt).exists() else "缺"))
            bits.append("report=%s" % ("有" if report_path(wt).exists() else "缺"))
        if s["state"] == "blocked":
            # DX-75：停放态必须自带出路——停在哪步失败，出路就是重跑哪步（或放弃）
            out("  %-10s ↳ 出路：重跑 dispatch（开区失败）/ 对应失败步骤，或 abandon 放弃" % card)
        out("  %-10s %s" % (card, " | ".join(bits)))
    return 0


def reconcile_plan(sessions: dict, done_facts: set[str],
                   ledger_cards: set[str],
                   claim_heads: dict[str, str] | None = None) -> list[tuple[str, str, str]]:
    """纯函数：产出对账动作 [(card, from, to)]，不改任何状态（DX-57）。

    事实判据由调用方收集（baseline=done 或会话 head 已并入 origin/main 记入
    done_facts；claim_heads 是服务端 refs/claims 占位快照，DX-73）。只动
    dispatched/running 的僵尸在办、approved 的滞后态，与会话滞留 created 但
    服务端占位仍在的卡（领用事实成立，升回 dispatched）；created 且台账无卡
    的断链项不动，交 requeue 显式处理。
    """
    actions: list[tuple[str, str, str]] = []
    claims = claim_heads or {}
    for card, sess in sorted(sessions.items()):
        st = sess.get("state", "")
        if st in ("dispatched", "running") and card in done_facts:
            actions.append((card, st, "closed"))
        elif st == "approved" and card in ledger_cards:
            actions.append((card, st, "created"))
        elif st == "created" and card in claims:
            actions.append((card, st, "dispatched"))
    return actions


def reconcile_state(ctx: Ctx, state: dict) -> tuple[list[tuple[str, str, str]], list[str]]:
    """投影对账（DX-65）：按台账/git 事实把内存状态对齐，可被任意子命令隐式调用。

    ① 缺会话补影：台账未结卡且无会话 → created；服务端 refs/claims 占位仍在的
       已领用卡 → dispatched 并尽力恢复领用现场（DX-73：领用是已发生的 git 事实，
       不因状态文件丢失退回 created，否则 report/close 全被拒、回路被逼绕过）。
       已结卡不补，status 不被 181 张历史卡淹没。
    ② 僵尸对齐：dispatched/running 且事实已并入 origin/main → closed（两跳，DX-57）；
       approved 且台账已有卡 → created；会话滞留 created 但占位仍在 → dispatched（DX-73）。
    只改内存 state；是否落盘由调用方定（dry-run 永不落盘）。返回 (对齐动作, 补影卡号)。
    """
    doc = ledger_doc() or {}
    baseline = doc.get("baseline", {}) or {}
    ledger_cards = set(doc.get("cards", {}))
    done_facts = {c for c, b in baseline.items() if b == "done"}
    claims = claim_refs()
    synthesized: list[str] = []
    for card in sorted(ledger_cards - set(state["sessions"])):
        if baseline.get(card) == "done":
            continue
        head = claims.get(card, "")
        if head:
            state["sessions"][card] = {"state": "dispatched", "claimHead": head,
                                       "rebuiltBy": "refs/claims",
                                       **recover_claim_context(card, head),
                                       "history": [{"at": now(), "from": "-", "to": "dispatched",
                                                    "by": "projection:refs/claims"}]}
        else:
            state["sessions"][card] = {"state": "created", "history": [
                {"at": now(), "from": "-", "to": "created", "by": "projection"}]}
        synthesized.append(card)
    for card, sess in state["sessions"].items():
        head = sess.get("head", "")
        if head and sess.get("state") in ("dispatched", "running"):
            code, _ = run(["git", "-C", str(REPO), "merge-base", "--is-ancestor",
                           head, "origin/main"], check=False, quiet=True)
            if code == 0:
                done_facts.add(card)
    actions = reconcile_plan(state["sessions"], done_facts, ledger_cards, claim_heads=claims)
    for card, frm, to in actions:
        if to == "closed" and "closed" not in TRANSITIONS.get(frm, set()):
            # 「未 report 不能 close」的纪律不破：reconcile 的 git 事实就是补交的
            # 完成报告，走合法两跳 dispatched/running → reported → closed（DX-57）。
            transition(ctx, state, card, "reported", reconcileBy="git-facts")
            transition(ctx, state, card, "closed")
        elif to == "dispatched" and frm == "created":
            # DX-73：升回 dispatched 时补齐领用现场，口径与补影一致
            transition(ctx, state, card, to, claimHead=claims.get(card, ""),
                       rebuiltBy="refs/claims",
                       **recover_claim_context(card, claims.get(card, "")))
        else:
            transition(ctx, state, card, to)
    return actions, synthesized


def cmd_reconcile(ctx: Ctx, state: dict, args) -> int:
    """按 git 事实对齐状态机；只改程序状态，绝不写台账与工作树（DX-57）。"""
    actions, synthesized = reconcile_state(ctx, state)
    for card in synthesized:
        out("  · %s: 补影 → created（台账未结卡缺会话，DX-65）" % card)
    for card, frm, to in actions:
        out("  · %s: %s → %s（git 事实对账）" % (card, frm, to))
    if not actions and not synthesized:
        out("✓ 状态机与 git 事实已对齐，无需动作")
    stranded = sorted(c for c, s in state["sessions"].items()
                      if s.get("state") == "created" and c not in set(ledger_card_ids()))
    if stranded:
        out("  · 断链项（created 且台账无卡，可 requeue 重批）：%s" % ", ".join(stranded))
    if actions or synthesized:
        save_state(ctx, state)
        out("✓ 对账完成：%d 项对齐 / %d 项补影" % (len(actions), len(synthesized)))
    return 0


def cmd_requeue(ctx: Ctx, state: dict, args) -> int:
    """断链恢复：created 且台账确无此卡 → 重置 requested 重新走 approve（DX-57）。"""
    card = args.card.upper()
    sess = state["sessions"].get(card)
    if not sess or sess.get("state") != "created":
        err("✗ %s 不在 created 态，无法重批（当前：%s）"
            % (card, (sess or {}).get("state", "无会话")))
        return 1
    if card in ledger_card_ids():
        err("✗ %s 已在台账，立卡已落地——请走 dispatch 领卡，不是 requeue" % card)
        return 1
    transition(ctx, state, card, "requested", requeuedAt=now())
    p = find_request(card)
    if p:
        data = parse_request(p.read_text(encoding="utf-8"))
        data["state"] = "requested"
        if not ctx.dry_run:
            p.write_text(render_request(data), encoding="utf-8")
    out("✓ %s 已重置 requested，可重新 approve" % card)
    return 0


# ---------------------------------------------------------------- 自测

def selftest() -> int:
    """纯函数 + 临时目录夹具，不碰仓库、不联网、不跑 git/pwsh。"""
    cases: list[tuple[str, bool]] = []

    def check(name: str, cond: bool) -> None:
        cases.append((name, bool(cond)))

    req = {"state": "requested", "card": "FLOW-01", "stage": "FLOW", "title": "测试卡",
           "author": "agent-a", "evidence": "日志 L12", "scope": "scripts/**;docs/**",
           "acceptance": "python scripts/nbflow.py --selftest 通过;ctest 全绿",
           "depends": "", "suggestedId": "FLOW-01", "rejectReason": "", "createdAt": now()}
    text = render_request(req)
    parsed = parse_request(text)
    check("front matter 往返", parsed["card"] == "FLOW-01" and parsed["state"] == "requested")
    check("多值字段拆分", split_multi(parsed["scope"]) == ["scripts/**", "docs/**"])
    # DX-84：拆分收口单一实现 —— 全角分号也拆，立卡落数组，存量单串兼容展开
    check("多值字段拆分认全角分号", split_multi("a；b") == ["a", "b"])
    check("多值字段拆分不切竖线", split_multi('ctest -R "a|b"') == ['ctest -R "a|b"'])
    mreq = {"card": "DX-99", "stage": "DX", "title": "拆分数组化", "scope": "scripts/x.py",
            "acceptance": "自测全过；python scripts/x.py 退出 0|退出码 0",
            "evidence": "三形态", "_body": ""}
    check("立卡 acceptance 落数组（创建侧只认 ;／；，字面 | 不切）",
          request_to_manifest(mreq)["acceptance"]
          == ["自测全过", "python scripts/x.py 退出 0|退出码 0"])
    check("report 侧存量单串仍全形态兼容（DX-62 竖线）",
          flatten_acceptance(["自测覆盖：删 state 后重建；已领用卡不退回 created|补结落地"])
          == ["自测覆盖：删 state 后重建", "已领用卡不退回 created", "补结落地"])
    check("正文保留五段式", "## 验收" in parsed["_body"])
    check("批准回写不丢正文", parse_request(render_request(parsed))["_body"] == parsed["_body"])
    check("缺 front matter 被拒", _raises(lambda: parse_request("随便写点东西")))
    check("卡号正则", CARD_ID_RE.match("FLOW-01") and CARD_ID_RE.match("M19")
          and not CARD_ID_RE.match("FLOW-01-EXTRA"))
    check("分支前缀", BRANCH_RE.match("chore/nbflow-dispatch-program")
          and not BRANCH_RE.match("main") and not BRANCH_RE.match("hack/../x"))

    manifest = request_to_manifest(parsed)
    check("请求整理成五段式", manifest["id"] == "FLOW-01"
          and manifest["scope"] == ["scripts/**", "docs/**"]
          and len(manifest["acceptance"]) == 2 and manifest["depends"] == [])
    check("note 带触发证据", manifest["note"].startswith("触发证据：日志 L12"))
    bad = dict(req, title="")
    check("缺字段被拦", bool(validate_manifest_card("FLOW-01", bad)))
    check("验收可执行记号", acceptance_has_mark("ctest 全绿") and not acceptance_has_mark("体验更好"))

    check("状态机：created→dispatched 一步到位",
          "dispatched" in TRANSITIONS["created"] and "claimed" not in STATES)
    check("状态机：未 report 不能 close", "closed" not in TRANSITIONS["dispatched"])
    check("状态机：reported→closed→cleaned",
          "closed" in TRANSITIONS["reported"] and "cleaned" in TRANSITIONS["closed"])
    check("状态机：任意在办态可 abandoned",
          all("abandoned" in TRANSITIONS[s] for s in ("created", "dispatched", "running", "reported")))
    # DX-75：dispatch 失败必须停得进 blocked，恢复出路直达重跑（否则失败处理自己撞
    # 「非法状态转移」，卡永远悬在半空——DX-71 实录）
    check("状态机：created→blocked 失败停放合法（DX-75）",
          "blocked" in TRANSITIONS["created"])
    check("状态机：blocked→dispatched 重跑出路合法（DX-75）",
          "dispatched" in TRANSITIONS["blocked"])
    check("状态机：blocked 不可直达 closed（未 report 不能 close 不被绕过）",
          "closed" not in TRANSITIONS["blocked"])
    # DX-75 转移场景演练：dispatch 失败 → 停放 blocked → 重跑出路（内存演算，dry-run 不落盘）
    stw = blank_state()
    stw["sessions"]["FLOW-01"] = {"state": "created", "history": []}
    transition(Ctx(dry_run=True), stw, "FLOW-01", "blocked", nextStep="排查 agent-init 失败原因后重跑 dispatch")
    transition(Ctx(dry_run=True), stw, "FLOW-01", "dispatched", agent="agent-x", branch="fix/x")
    check("DX-75 场景：created→blocked→dispatched 重跑走通",
          stw["sessions"]["FLOW-01"]["state"] == "dispatched"
          and stw["sessions"]["FLOW-01"]["nextStep"] == "排查 agent-init 失败原因后重跑 dispatch")
    # DX-115：approve 立卡通道退役 —— 通知必须指到唯一入口、迁移 recipe 与退出码
    check("approve 退役通知含 register_board_cards.py 指引与退出码（DX-115）",
          "register_board_cards.py" in APPROVE_RETIRE_NOTICE
          and "docs/card-requests" in APPROVE_RETIRE_NOTICE
          and "退出码 9" in APPROVE_RETIRE_NOTICE)
    # DX-127：冲突工单自动立卡跟着迁移 —— 撞冲突是常规路径，不能停留在已退役通道
    import inspect
    ticket_src = inspect.getsource(raise_conflict_ticket)
    check("冲突工单立卡走唯一入口且不含 cmd_approve（DX-127）",
          "register_board_cards.py" in ticket_src
          and "_board_worktree" in ticket_src
          and "git-done.ps1" in ticket_src
          and "cmd_approve" not in ticket_src)

    st = blank_state()
    rec = authorization(st, "维护者 2026-11-04 会话", "FLOW-01~04", 24)
    check("会话级授权写入", len(st["authorizations"]) == 1 and rec["batch"] == "FLOW-01~04")
    reused = authorization(st)
    check("后续 approve 复用同一凭证", reused["id"] == rec["id"])
    st["authorizations"][-1]["expiresAt"] = "2000-01-01T00:00:00+00:00"
    check("过期授权不可用", _raises(lambda: authorization(st)))
    check("无授权时明确拒绝", _raises(lambda: authorization(blank_state())))

    results = parse_results(["ctest 全绿=pass:ctest 4/4", "冒烟=pass:--smoke 退出码 0"])
    acc = ["ctest 全绿", "冒烟"]
    check("回执全绿", not check_report(acc, results, 0, []))
    check("缺验收项被拒", any("缺验收结论" in p for p in check_report(acc, {"ctest 全绿": results["ctest 全绿"]}, 0, [])))
    check("验收 fail 被拒", bool(check_report(acc, dict(results, 冒烟={"result": "fail", "evidence": "红灯"}), 0, [])))
    check("verify 非 0 被拒", bool(check_report(acc, results, 1, [])))
    check("未完成项被拒", bool(check_report(acc, results, 0, ["补截图"])))
    check("多余验收项被拒", bool(check_report(acc, dict(results, 额外项={"result": "pass", "evidence": ""}), 0, [])))
    check("结果格式校验", _raises(lambda: parse_results(["不成样子"])))
    check("序号可引用验收项",
          parse_results(["1=pass:证据"], acc) == {"ctest 全绿": {"result": "pass", "evidence": "证据"}})
    check("序号越界被拒", _raises(lambda: parse_results(["9=pass:证据"], acc)))

    # DX-70：分割点按「= 后紧跟结果词」定位，键与证据含等号都不误切
    eq = parse_results(["合并落位=pass:95bb295（c69cc7f=父提交）均已在 main"])
    check("证据含等号不误切",
          eq["合并落位"] == {"result": "pass", "evidence": "95bb295（c69cc7f=父提交）均已在 main"})
    keyed = parse_results(['-R "tst_a|tst_b" 全过=pass:ctest 2/2'])
    check("键含等号不误切",
          keyed['-R "tst_a|tst_b" 全过'] == {"result": "pass", "evidence": "ctest 2/2"})
    check("证据里再出现 =pass: 仍取第一分割点",
          parse_results(["项=pass:步骤=pass:均绿"])["项"]["evidence"] == "步骤=pass:均绿")

    # DX-70：竖线分隔的存量卡（DX-62 形态）先拆子项再做序号映射
    pipe_acc = ["derive 能点名滞留卡|agent-init 领用时显式警告|selftest 全过|补结落地 baseline 归位"]
    check("竖线单条拆成四子项", len(flatten_acceptance(pipe_acc)) == 4)
    check("子项序号可回报",
          parse_results(["2=pass:警告已投递"], pipe_acc)
          == {"agent-init 领用时显式警告": {"result": "pass", "evidence": "警告已投递"}})
    check("引号内的竖线不是分隔符",
          flatten_acceptance(['ctest -R "tst_a|tst_b" 通过']) == ['ctest -R "tst_a|tst_b" 通过'])
    check("拆分幂等", flatten_acceptance(flatten_acceptance(pipe_acc)) == flatten_acceptance(pipe_acc))
    pipe_results = parse_results(["1=pass:a", "2=pass:b", "3=pass:c", "4=pass:d"], pipe_acc)
    check("四子项逐条回报互不冲突", not check_report(flatten_acceptance(pipe_acc), pipe_results, 0, []))

    # DX-61：defer 结论 —— 待合并后核验的合法通道（必须有理由，不算失败）
    defer_results = parse_results(
        ["ctest 全绿=pass:ctest 4/4", "2=defer:推 tag 后由 release.yml 核验资产"], acc)
    check("defer 解析且带理由",
          defer_results["冒烟"] == {"result": "defer", "evidence": "推 tag 后由 release.yml 核验资产"})
    check("defer 不算失败", not check_report(acc, defer_results, 0, []))
    check("defer 缺理由被拒", _raises(lambda: parse_results(["冒烟=defer"], acc)))
    check("defer 与 fail 混合仍拦 fail",
          bool(check_report(acc, dict(defer_results, **{"ctest 全绿": {"result": "fail", "evidence": "红灯"}}), 0, [])))

    # 命令拼装：领卡与移交是一次 agent-init.ps1 -Card 调用
    argv = ps_script("agent-init.ps1", ["-Agent", "agent-a", "-Branch", "feature/x", "-Card", "FLOW-01"])
    check("dispatch 只调一次 agent-init 且带 -Card",
          "agent-init.ps1" in " ".join(argv) and "-Card" in argv and "FLOW-01" in argv)
    # DX-89：建区脚本必须落在锚点（common dir 父目录），与调用位置无关
    with tempfile.TemporaryDirectory() as td89:
        class _StubCtx(Ctx):
            def __init__(self):
                super().__init__(dry_run=True)
                self._common = Path(td89)

        dargv = dispatch_argv(_StubCtx(), "FLOW-01", "agent-a", "feature/x")
        check("dispatch root=锚点而非调用者检出（DX-89）",
              str(Path(td89).parent / "scripts" / "agent-init.ps1") in " ".join(dargv)
              and str(REPO / "scripts" / "agent-init.ps1") not in " ".join(dargv))
    close_argv = ps_script("git-done.ps1", ["-Branch", "feature/x"])
    check("close 调 git-done 且带分支", "git-done.ps1" in " ".join(close_argv) and "-Branch" in close_argv)
    check("abandon 释放服务端 ref", "refs/claims/FLOW-01" in "refs/claims/%s" % "FLOW-01")
    check("handoff 与 report 文件名固定",
          handoff_path("W").name == "handoff.json" and report_path("W").name == "run-report.json")
    check("产物路径不越出 .git / .sandbox（DX-47）",
          REQUEST_DIR == _git_common_dir() / "neonboard-flow" / "requests"
          and handoff_path("W").as_posix().endswith("/.sandbox/artifacts/handoff.json")
          and report_path("W").as_posix().endswith("/.sandbox/artifacts/run-report.json"))

    # 临时目录里验证状态文件读写与幂等
    with tempfile.TemporaryDirectory() as td:
        class FakeCtx(Ctx):
            def __init__(self):
                super().__init__(dry_run=False)
                self._common = Path(td)
        c = FakeCtx()
        save_state(c, st)
        check("状态文件原子写入", c.state_file.exists())
        loaded = load_state(c)
        check("状态文件可回读", loaded["sessions"] == {} and len(loaded["authorizations"]) == 1)
        with contextlib.redirect_stdout(io.StringIO()):
            transition(c, loaded, "FLOW-01", "accepted")
        check("状态转移落盘", load_state(c)["sessions"]["FLOW-01"]["state"] == "accepted")
        check("非法转移被拒", _raises(lambda: transition(c, loaded, "FLOW-01", "cleaned")))

        # 合并冲突 → 工单：试合并证据、卡号分配、请求单结构、无授权不立卡
        info = probe_merge(c, "feature/x", simulate=True)
        check("试合并探测能识别冲突", info["ok"] is False
              and "docs/board-ledger.json" in info["conflicts"])
        anchor_info = {"anchor": str(REPO), "branch": "main", "behind": 45, "ahead": 0,
                       "reason": "ff-only-blocked", "conflicts": ["scripts/git-done.ps1"],
                       "evidence": "演练", "rootClaim": None}
        body = conflict_ticket_body("DX-99", "origin/main", anchor_info,
                                    ["docs/**", "scripts/**"], ["验收一", "验收二"])
        check("锚点阻塞工单写明约束与交接",
              "## 锚点区约束" in body and "§7.1" in body and "nbflow close 上交" in body)
        acc = ["冲突文件无 <<<<<<< 标记", "git merge --no-ff feature/x 完成",
               "pwsh -File scripts/verify.ps1 -ScriptOnly 通过",
               "合并结果用 merge 提交合回 origin/main", "工单 note 记录冲突文件与解决方式"]
        ticket = {"state": "requested", "card": "DX-99", "stage": CONFLICT_STAGE,
                  "title": "合并冲突：feature/x 与 origin/main（程序自动建单）", "author": "nbflow",
                  "evidence": "reason=merge-conflict", "scope": "docs/**;scripts/**",
                  "acceptance": ";".join(acc), "depends": "", "suggestedId": "DX-99",
                  "rejectReason": "", "createdAt": now(), "body": body}
        tparsed = parse_request(render_request(ticket))
        check("冲突工单 front matter 可解析",
              tparsed["card"] == "DX-99" and tparsed["stage"] == "DX")
        check("冲突工单验收逐条且 scope 非空",
              len(split_multi(tparsed["acceptance"])) == 5
              and split_multi(tparsed["scope"]) == ["docs/**", "scripts/**"])
        check("冲突卡号避开已占用",
              pick_conflict_card({"DX-01", "DX-03", "DX-25", "FLOW-01"}) == "DX-26")
        check("取号按 stage 命名空间（DX/R/M/MOD）",
              next_card_id({"DX-07"}, "DX") == "DX-08"
              and next_card_id({"R11-04", "R11-07"}, "R11") == "R11-08"
              and next_card_id({"M01", "M19"}, "M") == "M20"
              and next_card_id({"MOD-02", "MOD-05"}, "MOD") == "MOD-06"
              and next_card_id(set(), "DX") == "DX-01")
        check("未知 stage 兜底 DX 命名空间",
              next_card_id({"DX-01"}, "ZZ") == "DX-02")
        check("撞号改配顺延且避开已占",
              next_card_id({"DX-55", "DX-56"}, "DX") == "DX-57")
        check("对账规划：僵尸 dispatched/running 对齐 closed，approved+已立卡对齐 created",
              reconcile_plan(
                  {"DX-90": {"state": "dispatched"}, "DX-91": {"state": "running"},
                   "DX-92": {"state": "approved"}, "DX-93": {"state": "created"},
                   "DX-94": {"state": "requested"}},
                  {"DX-90", "DX-91"}, {"DX-90", "DX-91", "DX-92", "DX-94"})
              == [("DX-90", "dispatched", "closed"), ("DX-91", "running", "closed"),
                  ("DX-92", "approved", "created")])
        check("对账不动 created 断链项与正常项",
              reconcile_plan({"DX-93": {"state": "created"}, "DX-95": {"state": "closed"}},
                             set(), set()) == [])
        check("转移表：created→requested 已合法，未 report 不能 close 的纪律未松",
              "requested" in TRANSITIONS["created"]
              and "closed" not in TRANSITIONS["dispatched"]
              and "closed" not in TRANSITIONS["running"])
        # DX-72：落盘失败退 blocked 可重试，approved 只在落盘后一跳进 created
        check("board-apply 退出码归类（3=noop 不是失败）",
              board_apply_outcome(0) == "ok" and board_apply_outcome(3) == "noop"
              and all(board_apply_outcome(c) == "fail" for c in (1, 2, 4, 5)))
        check("accepted 可退 blocked，blocked 可重试回 accepted",
              "blocked" in TRANSITIONS["accepted"] and "accepted" in TRANSITIONS["blocked"])
        check("accepted 不得直跳 created（须经 approved，落盘后才推进）",
              "created" not in TRANSITIONS["accepted"]
              and "created" in TRANSITIONS["approved"])
        check("slug 只留安全字符", _slug("feature/Foo Bar~x") == "feature-foo-bar-x")
        old_dir = REQUEST_DIR
        globals()["REQUEST_DIR"] = Path(td) / "card-requests"
        try:
            st3 = blank_state()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                ticket_card, ticket_code = raise_conflict_ticket(
                    c, st3, branch="feature/x", info=info, agent="agent-conflict")
            check("无会话授权只建单不立卡",
                  ticket_code == 0 and st3["sessions"][ticket_card]["state"] == "requested")
            check("冲突请求单已落盘", len(list((Path(td) / "card-requests").glob("*.md"))) == 1)
            (Path(td) / "card-requests" / "fake.md").write_text(
                "---\nstate: requested\ncard: DX-77\n---\n", encoding="utf-8")
            check("queued_card_ids 读活队列", queued_card_ids() >= {"DX-77"})
        finally:
            globals()["REQUEST_DIR"] = old_dir

        # DX-67 语义查重：命中即拒 / ack 放行 / 无命中直通（语料全部注入，不碰真仓库）。
        # DX-114：「不碰真仓库」必须连占用集事实源一起注入——ledger_card_ids 直读真台账
        # （DX-92 并集），语料卡号与真实已立卡撞车就会被「卡号已被占用」拒收（DX-71/72/73
        # 实录，真仓库环境恒假红）；queued_card_ids 由 REQUEST_DIR 重定向覆盖，
        # claim 占位由 reconcile 探针单独 stub。
        old_dir2, old_arch, old_doc, old_ids = REQUEST_DIR, ARCHIVE_REQUEST_DIR, ledger_doc, ledger_card_ids
        globals()["REQUEST_DIR"] = Path(td) / "dup-q"
        globals()["ARCHIVE_REQUEST_DIR"] = Path(td) / "dup-arch"
        globals()["ledger_doc"] = lambda: {"cards": {
            "DX-40": {"title": "流程提交豁免收口：agent-init 拒绝脏树领用，闸门对卷入代码的流程提交失效"}}}
        globals()["ledger_card_ids"] = lambda: {"DX-40"}
        # known_titles 还从 local_main_doc() 取本地 main 台账（DX-92 并集的另一半），一并注入
        old_lmd = local_main_doc
        globals()["local_main_doc"] = lambda: {"cards": {}}
        try:
            def req_args(card: str, title: str, ack: bool = False):
                return argparse.Namespace(
                    card=card, stage="DX", title=title, scope="scripts/**",
                    acceptance="python scripts/nbflow.py --selftest 通过",
                    evidence="自测", author="selftest", depends="", slug="",
                    ack_duplicate=ack)

            dup_title = "堵流程豁免洗白：agent-init 领用前脏树即拒，流程 scope 触及代码路径时豁免失效"
            clean_title = "启动闪屏百分比读数在高 DPI 下乱码：数字图集按逻辑像素采样"
            dx40_title = "流程提交豁免收口：agent-init 拒绝脏树领用，闸门对卷入代码的流程提交失效"
            hits = duplicate_hits(dup_title, [("DX-40", dx40_title)])
            check("查重：DX-51 式重复命中且共同词≥3",
                  len(hits) == 1 and len(hits[0][2]) >= 3)
            check("查重：通用二元组/单共同词/无关标题不命中",
                  not duplicate_hits("侧栏支持拖拽自定义排序并记住顺序",
                                     [("R4-03", "R4 多板 / 自定义列架构解锁")])
                  and not duplicate_hits("nbflow 卡片", [("DX-55", "nbflow 编号自动分配：request/approve 取号")])
                  and not duplicate_hits(clean_title, [("DX-40", dx40_title)]))
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                rc_hit = cmd_request(c, st := blank_state(), req_args("DX-71", dup_title))
            check("查重命中未 ack → 拒绝且不写单",
                  rc_hit == 1 and not list((Path(td) / "dup-q").glob("*.md")))
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                rc_ack = cmd_request(c, st, req_args("DX-72", dup_title, ack=True))
            check("查重命中 ack 放行 → 请求单落盘",
                  rc_ack == 0 and len(list((Path(td) / "dup-q").glob("*.md"))) == 1)
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                rc_clean = cmd_request(c, st, req_args("DX-73", clean_title))
            check("无命中直通",
                  rc_clean == 0 and len(list((Path(td) / "dup-q").glob("*.md"))) == 2)
        finally:
            globals()["REQUEST_DIR"] = old_dir2
            globals()["ARCHIVE_REQUEST_DIR"] = old_arch
            globals()["ledger_doc"] = old_doc
            globals()["ledger_card_ids"] = old_ids
            globals()["local_main_doc"] = old_lmd

        # DX-65 投影化：断链重算 / 缺会话补影 / dry-run 纯化 / 删 state.json 可重建。
        # DX-114：claim_refs 直读真 refs/claims——语料卡号命中真实占位时（DX-83 实录，
        # 并行会话的真实 DX-83 占位让语料卡被重建为 dispatched）补影断言恒假，
        # 一并 stub 进语料边界。
        old_doc65, old_refs = ledger_doc, claim_refs
        globals()["ledger_doc"] = lambda: {
            "baseline": {"DX-80": "done", "DX-81": "todo", "DX-83": "todo"},
            "cards": {"DX-80": {"title": "已交付卡"}, "DX-81": {"title": "在库卡有会话"},
                      "DX-83": {"title": "在库卡缺会话"}}}
        globals()["claim_refs"] = lambda: {}
        try:
            st6 = blank_state()
            st6["sessions"]["DX-80"] = {"state": "dispatched", "history": []}
            st6["sessions"]["DX-81"] = {"state": "approved", "history": []}
            with contextlib.redirect_stdout(io.StringIO()):
                actions, synth = reconcile_state(c, st6)
            check("投影：僵尸 dispatched 事实已结 → 两跳 closed",
                  ("DX-80", "dispatched", "closed") in actions
                  and st6["sessions"]["DX-80"]["state"] == "closed")
            check("投影：approved 且台账已有卡 → created",
                  ("DX-81", "approved", "created") in actions
                  and st6["sessions"]["DX-81"]["state"] == "created")
            check("投影：台账未结卡缺会话补影 created，已结卡不补",
                  synth == ["DX-83"] and st6["sessions"]["DX-83"]["state"] == "created")

            class DryCtx(Ctx):
                def __init__(self):
                    super().__init__(dry_run=True)
                    self._common = Path(td) / "dry-projection"

            dry = DryCtx()
            st7 = blank_state()
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                transition(dry, st7, "DX-90", "accepted")
            check("dry-run 纯化：状态转移不落盘", not dry.state_file.exists())
            fresh = FakeCtx()
            fresh._common = Path(td) / "fresh-projection"
            loaded = load_state(fresh)
            check("删 state.json 后 load 得空投影", loaded["sessions"] == {})
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                transition(fresh, loaded, "DX-90", "accepted")
            check("删 state.json 后真跑不被预演状态拒",
                  load_state(fresh)["sessions"]["DX-90"]["state"] == "accepted")
        finally:
            globals()["ledger_doc"] = old_doc65
            globals()["claim_refs"] = old_refs

        # DX-73：投影重建消费服务端 refs/claims —— 已领用卡不因状态丢失退回 created
        old_doc73, old_refs73, old_rec73 = ledger_doc, claim_refs, recover_claim_context
        globals()["ledger_doc"] = lambda: {
            "baseline": {"DX-90": "todo", "DX-91": "todo", "DX-92": "todo"},
            "cards": {"DX-90": {"title": "已领用但缺会话"}, "DX-91": {"title": "未领用缺会话"},
                      "DX-92": {"title": "会话滞留 created"}}}
        globals()["claim_refs"] = lambda: {"DX-90": "abc123", "DX-92": "def456"}
        globals()["recover_claim_context"] = lambda card, head: (
            {"branch": "fix/%s-rebuild" % card.lower(), "worktree": "W"} if card == "DX-90" else {})
        try:
            st73 = blank_state()  # 空投影 = 删 state.json 后的最坏现场
            st73["sessions"]["DX-92"] = {"state": "created", "history": []}
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                _acts73, _synth73 = reconcile_state(c, st73)
            sess90 = st73["sessions"]["DX-90"]
            check("投影：占位仍在的缺会话卡重建 dispatched 并恢复现场",
                  sess90["state"] == "dispatched" and sess90["claimHead"] == "abc123"
                  and sess90["branch"] == "fix/dx-90-rebuild" and sess90["worktree"] == "W")
            check("投影：无占位的缺会话卡仍补影 created",
                  st73["sessions"]["DX-91"]["state"] == "created")
            check("投影：滞留 created 但占位仍在 → 对账升回 dispatched",
                  st73["sessions"]["DX-92"]["state"] == "dispatched"
                  and st73["sessions"]["DX-92"]["claimHead"] == "def456")
            check("对账规划：created+占位 → dispatched；无占位不动（回归不破）",
                  reconcile_plan({"DX-93": {"state": "created"}}, set(), {"DX-93"},
                                 claim_heads={"DX-93": "abc"})
                  == [("DX-93", "created", "dispatched")]
                  and reconcile_plan({"DX-93": {"state": "created"}}, set(), {"DX-93"}) == [])
        finally:
            globals()["ledger_doc"] = old_doc73
            globals()["claim_refs"] = old_refs73
            globals()["recover_claim_context"] = old_rec73

        # DX-92：取号占用集并入本地 main 台账（锚点未推立卡不进占用集就会撞号）
        check("台账卡号并集（纯函数）",
              ledger_ids_from_docs({"cards": {"DX-1": {}, "DX-2": {}}},
                                   {"cards": {"DX-2": {}, "DX-3": {}}}) == {"DX-1", "DX-2", "DX-3"}
              and ledger_ids_from_docs(None, None) == set())
        old_doc92, old_main92 = ledger_doc, local_main_doc
        globals()["ledger_doc"] = lambda: {"cards": {"DX-74": {}, "DX-88": {}}}
        globals()["local_main_doc"] = lambda: {"cards": {"DX-74": {}, "DX-75": {}}}
        try:
            check("ledger_card_ids 含本地 main 独有卡号（DX-75 在 origin/main 缺席也占用）",
                  ledger_card_ids() == {"DX-74", "DX-75", "DX-88"})
        finally:
            globals()["ledger_doc"] = old_doc92
            globals()["local_main_doc"] = old_main92

        # DX-93：目录存在 ≠ 检出可用（尸体里的 git -C 会落到别的仓库）
        corpse = Path(td) / "corpse-wt"
        (corpse / "scripts").mkdir(parents=True)
        (corpse / "scripts" / "board-apply.py").write_text("# placeholder\n", encoding="utf-8")
        check("尸体判死：脚本在但目录不是注册 worktree（git 解析失败）",
              worktree_alive(corpse, Path(td)) is False)
        noscript = Path(td) / "noscript-wt"
        noscript.mkdir()
        check("尸体判死：缺 scripts/board-apply.py（纯 os 判定先行）",
              worktree_alive(noscript, Path(td)) is False)

        # DX-112：回收失败重试 ≥2 次；终败落 pending-recycle.json 点名，不再静默降级
        ctx112 = Ctx()
        ctx112._common = Path(td)
        calls = {"n": 0}

        def flaky_remove():
            calls["n"] += 1
            if calls["n"] < 3:
                raise OSError("Permission denied")
            return True

        wt_flaky = Path(td) / "wt-flaky"
        wt_flaky.mkdir()
        check("回收重试：前两次失败第三次成功（共 3 次尝试）",
              remove_dir_with_retry(ctx112, wt_flaky, reason="自测回收", remove_fn=flaky_remove)
              is True and calls["n"] == 3)
        calls_one = {"n": 0}

        def once_ok():
            calls_one["n"] += 1
            return True

        check("回收重试：首次成功不多试",
              remove_dir_with_retry(ctx112, Path(td) / "不存在的目录", reason="自测回收",
                                    remove_fn=once_ok) is True and calls_one["n"] == 1)

        def hopeless_remove():
            raise OSError("Permission denied")

        wt_stuck = Path(td) / "wt-stuck"
        wt_stuck.mkdir()
        ok_stuck = remove_dir_with_retry(ctx112, wt_stuck, reason="自测回收",
                                         remove_fn=hopeless_remove)
        pf = pending_recycle_file(ctx112)
        recorded = json.loads(pf.read_text(encoding="utf-8"))["items"] if pf.exists() else {}
        check("回收终败：返回 False 且待回收记录点名路径",
              ok_stuck is False and any("wt-stuck" in k for k in recorded))

    failed = [n for n, ok in cases if not ok]
    for name, ok in cases:
        out(("  ✓ " if ok else "  ✗ ") + name)
    if failed:
        err("✗ selftest 失败 %d 项" % len(failed))
        return 1
    out("✓ selftest 通过：%d 项断言" % len(cases))
    return 0


def _raises(fn) -> bool:
    try:
        fn()
    except Exception:
        return True
    return False


# ---------------------------------------------------------------- CLI

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="nbflow", description="NeonBoard 开发会话派发程序（立卡→领卡→移交→反馈→结卡→清理）")
    dry_help = "演练：不执行外部命令、不写仓库文件（仅写 .git 下的程序内部状态）"
    p.add_argument("--dry-run", action="store_true", help=dry_help)
    p.add_argument("--selftest", action="store_true", help="跑纯函数自测（不碰仓库/不联网）")
    # --dry-run 放在子命令后面也要能用（argparse 的全局选项不会被子 parser 继承）。
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--dry-run", dest="dry_run_sub", action="store_true", help=dry_help)
    sub = p.add_subparsers(dest="cmd")

    a = sub.add_parser("authorize", parents=[common], help="登记一次会话级批量授权")
    a.add_argument("--by", required=True, help="授权来源，写进提交体的「授权：」一行")
    a.add_argument("--batch", required=True, help="批次，写进提交体的「批次：」一行")
    a.add_argument("--ttl-hours", type=int, default=24)

    r = sub.add_parser("request", parents=[common], help="agent 提交立卡请求")
    r.add_argument("--card", default="",
                   help="省略则由程序按 --stage 命名空间取下一个空号（DX-55）")
    r.add_argument("--stage", required=True)
    r.add_argument("--title", required=True)
    r.add_argument("--scope", required=True, help="分号分隔的路径 glob")
    r.add_argument("--acceptance", required=True, help="分号分隔的可执行验收项")
    r.add_argument("--evidence", default="")
    r.add_argument("--author", default="agent")
    r.add_argument("--depends", default="")
    r.add_argument("--slug", default="")
    r.add_argument("--ack-duplicate", action="store_true",
                   help="查重命中时显式放行（DX-67：提示不阻断，放行必须显式）")

    ap = sub.add_parser("approve", parents=[common], help="【已退役，DX-115】立卡走 register_board_cards.py（宪章条款 6）；调用即打印迁移 recipe 退出 9")
    ap.add_argument("--cards", default="", help="逗号分隔的卡号")
    ap.add_argument("--all", action="store_true", help="批准全部 requested/accepted")
    ap.add_argument("--authorize", default="", help="不传则复用会话级授权")
    ap.add_argument("--batch", default="")
    ap.add_argument("--ttl-hours", type=int, default=24)
    ap.add_argument("--no-merge", action="store_true", help="只落盘立卡分支，不合回主线")
    ap.add_argument("--no-probe", action="store_true", help="跳过合回主线前的试合并探测")
    ap.add_argument("--conflict-agent", default=CONFLICT_AGENT, help="冲突工单派给哪个 agent")

    d = sub.add_parser("dispatch", parents=[common], help="领卡 + 建分支 + 移交（一步）")
    d.add_argument("--card", required=True)
    d.add_argument("--agent", required=True)
    d.add_argument("--branch", required=True)
    d.add_argument("--base", default="")

    rp = sub.add_parser("report", parents=[common], help="agent 反馈结构化执行结果")
    rp.add_argument("--card", required=True)
    rp.add_argument("--result", action="append", default=[], help="'<验收项或序号>=pass:证据'，可重复")
    rp.add_argument("--results-json", default="", help="预先写好的 {验收项:{result,evidence}} JSON")
    rp.add_argument("--verify-exit", type=int, default=-1, help="verify.ps1 退出码")
    rp.add_argument("--run-verify", action="store_true", help="由程序跑 verify.ps1 取退出码")
    rp.add_argument("--verify-mode", choices=("script", "full"), default="script")
    rp.add_argument("--unfinished", default="")
    rp.add_argument("--next-step", default="")

    c = sub.add_parser("close", parents=[common], help="程序验收通过后合入 / 结卡 / 清理")
    c.add_argument("--card", required=True)
    c.add_argument("--branch", default="")
    c.add_argument("--force-close", action="store_true", help="缺 #短号=done 时用 git-done -Close 兜底")
    c.add_argument("--no-probe", action="store_true", help="跳过合入前的试合并探测")
    c.add_argument("--conflict-agent", default=CONFLICT_AGENT, help="冲突工单派给哪个 agent")

    ab = sub.add_parser("abandon", parents=[common], help="弃卡：释放 claim ref + 回收工作区")
    ab.add_argument("--card", required=True)
    ab.add_argument("--reason", required=True)
    ab.add_argument("--force", action="store_true", help="允许丢弃未提交改动 / 强删分支")

    an = sub.add_parser("anchor", parents=[common],
                        help="锚点区：先合并一次，阻塞/冲突则建工单交回 agent")
    an.add_argument("--agent", default=CONFLICT_AGENT, help="冲突工单派给哪个 agent")
    an.add_argument("--simulate-conflict", action="store_true", help="演练：模拟锚点阻塞")
    an.add_argument("--report-only", action="store_true", help="只诊断，不建工单")

    rq2 = sub.add_parser("requeue", parents=[common],
                         help="断链恢复：created 且台账无卡 → 重置 requested 重新 approve")
    rq2.add_argument("--card", required=True)

    sub.add_parser("reconcile", parents=[common],
                   help="状态机对账：按 git 事实对齐僵尸态（只改程序状态）")

    sub.add_parser("status", parents=[common], help="查看状态机")
    return p


def _force_utf8_console() -> None:
    """控制台代码页切 65001（DX-77）：reconfigure 之后 python 写出的是 UTF-8 字节，
    CP 936 控制台按 GBK 解释即 mojibake（nbflow status 授权段实测）；切到 65001
    后显示与字节一致。仅 Windows 实控台生效：GetConsoleWindow() 为 0（管道 /
    计划任务 / CI 服务会话）时跳过——管道消费者本就按 UTF-8 解码。与 PS 侧
    scripts/nb-console.ps1 同口径（DX-83），语言不同无法共用一份实现。"""
    if os.name != "nt":
        return
    try:
        import ctypes

        k32 = ctypes.windll.kernel32
        if k32.GetConsoleWindow():
            k32.SetConsoleOutputCP(65001)
            k32.SetConsoleCP(65001)
    except Exception:
        pass


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    _force_utf8_console()
    args = build_parser().parse_args()
    args.dry_run = bool(args.dry_run or getattr(args, "dry_run_sub", False))
    if args.selftest:
        return selftest()
    if not args.cmd:
        build_parser().print_help()
        return 3
    ctx = Ctx(dry_run=args.dry_run)
    try:
        state = load_state(ctx)
    except RuntimeError as exc:
        err("✗ %s" % exc)
        return 4
    # 投影化（DX-65）：每个子命令入口先按事实对账，status 结论恒等于现算结果；
    # state.json 丢失也能重建。reconcile 子命令自己就是对账，跳过免得做两遍。
    if args.cmd != "reconcile":
        try:
            fixes, _synth = reconcile_state(ctx, state)
        except (RuntimeError, OSError, ValueError):
            fixes = []  # 对账是投影校正（fail-soft）：取不到事实就按现状继续
        if fixes and not ctx.dry_run:
            save_state(ctx, state)
    handlers = {
        "authorize": cmd_authorize, "request": cmd_request, "approve": cmd_approve,
        "dispatch": cmd_dispatch, "report": cmd_report, "close": cmd_close,
        "abandon": cmd_abandon, "anchor": cmd_anchor, "status": cmd_status,
        "requeue": cmd_requeue, "reconcile": cmd_reconcile,
    }
    try:
        return handlers[args.cmd](ctx, state, args)
    except (RuntimeError, OSError, ValueError, json.JSONDecodeError) as exc:
        err("✗ %s" % exc)
        return 1
    except Exception as exc:
        # DX-72：唯一出口 —— 未预期异常也转成干净的非零退出，traceback 不喷进输出；
        # NBFLOW_DEBUG=1 重跑可看完整堆栈（排障用，不改变退出语义）。
        if os.environ.get("NBFLOW_DEBUG"):
            raise
        err("✗ %s 内部错误：%s: %s（设 NBFLOW_DEBUG=1 重跑可看堆栈）"
            % (args.cmd, type(exc).__name__, exc))
        return 5


if __name__ == "__main__":
    sys.exit(main())
