#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""nbflow.py —— NeonBoard 开发会话派发程序（单一入口）。

为什么是「一个程序」而不是一串脚本
------------------------------------
立卡 / 领卡 / 开区 / 移交 / 反馈 / 结卡 / 清理以前散在 board-apply.py、
claim-card.py、agent-init.ps1、git-done.ps1、release-claims.py 里，靠人记顺序。
本程序把它们收口成**一个状态机 + 一组子命令**，是这些脚本的调用方，
不重复实现任何判定逻辑（task_gate.py / board-apply.py 仍是唯一规则实现）。

    request   agent 提交立卡请求 → docs/card-requests/<日期>-<slug>.md（状态 requested）
    authorize 维护者给一次「会话级批量授权」，本会话后续 approve 自动复用
    approve   程序受理 + 整理成五段式清单 + 20 项预演 + 落盘立卡（+ 合回主线）
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
  2. 有会话级授权时自动 approve 立卡并 dispatch 派回 agent（handoff.json 带
     conflict 字段与 nextStep）；无授权则只留请求单，提示先 authorize ——
     程序不自己发明批准；
  3. 原卡状态保持不变（可重跑 close），state 里记 conflictTicket；agent 在
     自己的 worktree 里合并、消除冲突标记、report 逐条填证据、close 上交。
退出码 5 = 「已建冲突工单并交回 agent，本次未合入」。

状态载体
--------
运行时状态在 ``<git-common-dir>/neonboard-flow/state.json``（不入库、跨 worktree 共享）。
任务定义仍只存在于 ``docs/board-ledger.json``，本程序不把它当状态库。
审计留痕仍走既有通道：提交信息、台账、refs/claims、合并提交体。
``--dry-run`` 只写程序内部状态（.git 下），不写仓库文件、不执行任何外部 git/pwsh 命令
（不联网、不推送、不开区），因此逐条演练也能把状态机串起来。

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
# 允许的状态转移；dispatch 直接从 created → dispatched（领卡与移交是一步，不设中间态）
TRANSITIONS = {
    "requested": {"accepted", "approved", "rejected", "abandoned"},
    "accepted": {"approved", "rejected", "blocked", "abandoned"},
    "approved": {"created", "blocked", "abandoned"},
    "created": {"dispatched", "abandoned", "requested"},   # requested 仅 requeue 用（台账确无此卡，DX-57）
    "dispatched": {"running", "reported", "abandoned"},
    "running": {"reported", "abandoned"},
    "reported": {"closed", "running", "abandoned"},
    "closed": {"cleaned"},
    "cleaned": set(),
    "abandoned": {"requested"},
    "rejected": {"requested"},
    "blocked": {"accepted", "abandoned"},
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
    p = ctx.state_file
    # dry-run 也写程序内部状态（在 .git/ 下，不属于仓库工作树），这样逐条 --dry-run
    # 能把状态机串起来；仓库文件（请求单/派发包/回执）与外部命令仍一律不碰。
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
    return [v.strip() for v in (value or "").split(";") if v.strip()]


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


def cmd_approve(ctx: Ctx, state: dict, args) -> int:
    cards = [c.strip().upper() for c in (args.cards or "").split(",") if c.strip()]
    if args.all:
        cards = [c for c, s in state["sessions"].items() if s["state"] in ("requested", "accepted")]
    if not cards:
        err("✗ 没有待批准的请求（--cards A,B 或 --all）")
        return 3
    auth = authorization(state, args.authorize, args.batch, args.ttl_hours)
    out("  · 会话授权：%s / %s" % (auth["by"], auth["batch"]))

    reqs: list[tuple[str, Path, dict]] = []
    allocated: set[str] = set()
    batch_ids = set(cards)
    for card in cards:
        p = find_request(card)
        cached = state["sessions"].get(card, {}).get("requestText", "")
        if p:
            req = parse_request(p.read_text(encoding="utf-8"))
        elif cached:
            # 演练时请求单不落盘，程序状态里留一份等价文本，保证整条链路仍可串起来
            p = REQUEST_DIR / (card.lower() + ".md")
            req = parse_request(cached)
        else:
            err("✗ 找不到 %s 的请求单" % card)
            return 4
        if req.get("state") in ("approved", "rejected"):
            err("✗ %s 已 %s，不能重复批准" % (card, req["state"]))
            return 1
        # 撞号改配（DX-55）：建议号已被台账（或本批先到的卡）占用时，按 stage
        # 命名空间改配下一个空号；会话键、请求单 card:、台账 note 全部跟着新号走。
        # 占用集含台账 + 活队列 + 在途会话（DX-57），扣除本批自身的号防自撞。
        used = (ledger_card_ids() | queued_card_ids()
                | set(state["sessions"]) | allocated) - batch_ids
        suggested = (req.get("card") or card).upper()
        if suggested in used:
            new_id = next_card_id(used, req.get("stage") or "DX")
            out("  · 建议号 %s 已被占用，自动改配 %s" % (suggested, new_id))
            req["evidence"] = ((req.get("evidence") or "").rstrip("；;") +
                               "；原建议号 %s 已占用，程序改配 %s" % (suggested, new_id))
            req["suggestedId"] = new_id
            req["card"] = new_id
            sess = state["sessions"].pop(card, None)
            if sess:
                state["sessions"][new_id] = sess
            card = new_id
        allocated.add(card)
        reqs.append((card, p, req))
        transition(ctx, state, card, "accepted")

    manifests = []
    for card, _p, req in reqs:
        manifests.append(request_to_manifest(req))
        transition(ctx, state, card, "approved")
    path, temps = _write_manifest(manifests, ctx.dry_run)
    try:
        # ① 预演：20 项校验由 board-apply.py 负责，程序不重复实现
        code, _ = run(py_script("board-apply.py", [path, "--diff"]), dry=ctx.dry_run)
        if code not in (0, 3):
            err("✗ 预演未通过（退出码 %d），整批不落盘" % code)
            return 2
        # ② 落盘：专用立卡 worktree，不改调用者检出；提交体带授权/批次两行
        slug = re.sub(r"[^a-z0-9]+", "-", auth["batch"].lower()).strip("-")[:40] or "batch"
        branch = "chore/board-" + slug
        wt = _board_worktree(ctx, slug)
        if not ctx.dry_run:
            if not wt.exists():
                code, _ = run(["git", "-C", str(REPO), "worktree", "add", "-b", branch,
                               str(wt), "origin/main"], check=False)
                if code != 0:
                    err("✗ 建立立卡工作区失败（%s 已存在？先手工回收）" % wt)
                    return 2
            else:
                run(["git", "-C", str(wt), "checkout", "-B", branch, "origin/main"], check=True)
        code, _ = run([sys.executable, str(wt / "scripts" / "board-apply.py"), path,
                       "--commit", "--authorize", auth["by"], "--batch", auth["batch"]],
                      cwd=wt, dry=ctx.dry_run)
        if code != 0:
            err("✗ 立卡失败（退出码 %d）；未改主线" % code)
            return 2
        for card, p, _req in reqs:
            data = parse_request(p.read_text(encoding="utf-8")) if p.exists() else {}
            data["state"] = "approved"
            data["approvedBy"] = auth["by"]
            data["batch"] = auth["batch"]
            if not ctx.dry_run:
                p.write_text(render_request(data), encoding="utf-8")
            transition(ctx, state, card, "created", boardBranch=branch)
        if args.no_merge:
            out("  · --no-merge：立卡分支 %s 已留在 %s，未合回主线" % (branch, wt))
            return 0
        if not getattr(args, "no_probe", False):
            probe = probe_merge(ctx, branch)
            if not probe["ok"]:
                out("  · 先合并一次发现冲突：%s"
                    % (", ".join(probe["conflicts"][:5]) or probe["reason"]))
                _ticket, tcode = raise_conflict_ticket(ctx, state, branch=branch, info=probe,
                                                       agent=getattr(args, "conflict_agent", CONFLICT_AGENT))
                return 5 if tcode == 0 else tcode
        # ③ 合回主线：git-done 在立卡工作区自己的检出里跑（DX-45）。
        #    旧实现先 git push -u origin <立卡分支>——被 pre-push 规则 1 拒
        #    （分支尚未合并进 main，禁止推送），立卡分支永远推不上去，
        #    approve 只能手工 git-done 兜底。git-done 本就从不推工作分支：
        #    它从分支所在检出跑（root=wt，repo 定位到 wt），remote 模式
        #    基于 origin/main 建临时合并分支 --no-ff 合并 + 快进推 origin/main + 删分支。
        if not ctx.dry_run:
            code, _ = run(ps_script("git-done.ps1", ["-Branch", branch], root=wt), cwd=wt,
                          dry=ctx.dry_run)
            if code != 0:
                err("✗ 合回主线失败（可能与其他会话撞车，重新同步后重跑 close/approve）；分支仍在 %s" % wt)
                return 2
            code, _ = run(["git", "-C", str(REPO), "worktree", "remove", str(wt)], check=False)
            if code != 0:
                # 主线已合入，工作区回收失败（目录被占用等）只提示，不算失败
                out("  · 立卡工作区回收失败，不影响已合入的主线：%s" % wt)
        out("%s 立卡完成并合回主线：%s"
            % ("[演练] 未落盘 ·" if ctx.dry_run else "✓", ", ".join(cards)))
        return 0
    finally:
        for t in temps:
            try:
                os.unlink(t)
            except OSError:
                pass


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
    """origin/main 台账的全部卡号；离线退回工作区副本，再退空集（DX-55 取号用）。"""
    return set((ledger_doc() or {}).get("cards", {}))


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


def handoff_path(worktree: str) -> Path:
    return Path(worktree) / ".sandbox" / "artifacts" / "handoff.json"


def report_path(worktree: str) -> Path:
    return Path(worktree) / ".sandbox" / "artifacts" / "run-report.json"


def cmd_dispatch(ctx: Ctx, state: dict, args) -> int:
    """领卡 + 建分支 + 移交 = 一次 agent-init.ps1 -Card 调用 + handoff.json。"""
    card = args.card.upper()
    if not BRANCH_RE.match(args.branch):
        err("✗ 分支名不符合宪章前缀规范：%s" % args.branch)
        return 1
    sess = state["sessions"].get(card)
    if not sess or sess["state"] != "created":
        err("✗ %s 不处于 created（当前：%s）；先 approve" % (card, sess["state"] if sess else "无"))
        return 1
    params = ["-Agent", args.agent, "-Branch", args.branch, "-Card", card]
    if args.base:
        params += ["-Base", args.base]
    code, _ = run(ps_script("agent-init.ps1", params), dry=ctx.dry_run)
    if code != 0:
        err("✗ 开区失败（退出码 %d）：离线 / 占用冲突 / 已有同名分支，见上方诊断" % code)
        transition(ctx, state, card, "blocked", nextStep="排查 agent-init 失败原因后重跑 dispatch")
        return 2
    wt = str(REPO / ".worktrees" / args.agent)
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


def parse_results(items: list[str], acceptance: list[str] | None = None) -> dict[str, dict]:
    """'<验收项>=pass:证据' → {验收项: {result, evidence}}。

    验收项可以照抄原文，也可以用序号 ``1=pass:证据`` 指向 acceptance[0]，
    避免长验收命令里的等号与转义把 agent 绕晕。
    ``defer`` 表示「待合并后核验」（发版卡的 Release 资产 / update.json / 真机升级
    这类结构性排在合并之后的验收项，DX-61）：必须给出非空理由，close 时会大声打印
    并留痕，但不算失败 —— 把待核验混成 pass 才是这里要防的。
    """
    parsed: dict[str, dict] = {}
    for raw in items:
        if "=" not in raw:
            raise ValueError("结果格式应为 <验收项或序号>=pass|fail|defer:证据，收到 %r" % raw)
        key, tail = raw.rsplit("=", 1)
        key = key.strip()
        if key.isdigit():
            if not acceptance or not (1 <= int(key) <= len(acceptance)):
                raise ValueError("序号 %s 超出 acceptance 范围（共 %d 条）"
                                 % (key, len(acceptance or [])))
            key = acceptance[int(key) - 1]
        if ":" in tail:
            result, evidence = tail.split(":", 1)
        else:
            result, evidence = tail, ""
        if result.strip().lower() not in ("pass", "fail", "defer"):
            raise ValueError("结果只能是 pass / fail / defer：%r" % raw)
        result = result.strip().lower()
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
        argv = ["git", "-C", str(REPO), "worktree", "remove", wt]
        if args.force:
            argv.append("--force")
        run(argv, dry=ctx.dry_run)
    if branch:
        run(["git", "-C", str(REPO), "branch", "-d" if not args.force else "-D", branch],
            dry=ctx.dry_run)
    transition(ctx, state, card, "abandoned", reason=args.reason,
               nextStep="服务端占位已释放；台账 claims 投影由下次 agent-init 判失效清理")
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
            run(["git", "-C", str(REPO), "worktree", "remove", "--force", str(wt)], quiet=True)


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
    used = set(state.get("sessions", {}))
    code, text = run(["git", "-C", str(REPO), "show", "origin/main:docs/board-ledger.json"],
                     capture=True, quiet=True)
    if code == 0 and text.strip():
        try:
            used |= set(json.loads(text).get("cards", {}))
        except ValueError:
            pass
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
    # 程序不自己发明批准：没有会话级授权就停在 requested，等人授权后再 approve
    try:
        authorization(state)
    except RuntimeError as exc:
        err("  · %s" % exc)
        out("  · 工单已建但未立卡/未派发；请维护者 authorize 后跑：nbflow approve --cards %s" % card)
        return card, 0
    # 工单自己的立卡分支不再试合并：否则冲突会再生成一张工单，形成递归
    approve_args = argparse.Namespace(cards=card, all=False, authorize="", batch="",
                                      ttl_hours=24, no_merge=False, no_probe=True,
                                      conflict_agent=agent, dry_run_sub=ctx.dry_run)
    code = cmd_approve(ctx, state, approve_args)
    if code != 0:
        err("✗ 冲突工单立卡失败（%d）；请求单已留档 %s" % (code, p))
        return card, code
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
        out("  %-10s %s" % (card, " | ".join(bits)))
    return 0


def reconcile_plan(sessions: dict, done_facts: set[str],
                   ledger_cards: set[str]) -> list[tuple[str, str, str]]:
    """纯函数：产出对账动作 [(card, from, to)]，不改任何状态（DX-57）。

    事实判据由调用方收集（baseline=done 或会话 head 已并入 origin/main 记入
    done_facts）。只动 dispatched/running 的僵尸在办与 approved 的滞后态；
    created 且台账无卡的断链项不动，交 requeue 显式处理。
    """
    actions: list[tuple[str, str, str]] = []
    for card, sess in sorted(sessions.items()):
        st = sess.get("state", "")
        if st in ("dispatched", "running") and card in done_facts:
            actions.append((card, st, "closed"))
        elif st == "approved" and card in ledger_cards:
            actions.append((card, st, "created"))
    return actions


def cmd_reconcile(ctx: Ctx, state: dict, args) -> int:
    """按 git 事实对齐状态机；只改程序状态，绝不写台账与工作树（DX-57）。"""
    doc = ledger_doc() or {}
    baseline = doc.get("baseline", {}) or {}
    ledger_cards = set(doc.get("cards", {}))
    done_facts = {c for c, b in baseline.items() if b == "done"}
    for card, sess in state["sessions"].items():
        head = sess.get("head", "")
        if head and sess.get("state") in ("dispatched", "running"):
            code, _ = run(["git", "-C", str(REPO), "merge-base", "--is-ancestor",
                           head, "origin/main"], check=False, quiet=True)
            if code == 0:
                done_facts.add(card)
    actions = reconcile_plan(state["sessions"], done_facts, ledger_cards)
    if not actions:
        out("✓ 状态机与 git 事实已对齐，无需动作")
    for card, frm, to in actions:
        out("  · %s: %s → %s（git 事实对账）" % (card, frm, to))
        if to == "closed" and "closed" not in TRANSITIONS.get(frm, set()):
            # 「未 report 不能 close」的纪律不破：reconcile 的 git 事实就是补交的
            # 完成报告，走合法两跳 dispatched/running → reported → closed（DX-57）。
            transition(ctx, state, card, "reported", reconcileBy="git-facts")
            transition(ctx, state, card, "closed")
        else:
            transition(ctx, state, card, to)
    stranded = sorted(c for c, s in state["sessions"].items()
                      if s.get("state") == "created" and c not in ledger_cards)
    if stranded:
        out("  · 断链项（created 且台账无卡，可 requeue 重批）：%s" % ", ".join(stranded))
    if actions:
        out("✓ 对账完成：%d 项对齐" % len(actions))
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

    ap = sub.add_parser("approve", parents=[common], help="受理 + 整理 + 预演 + 立卡（+ 合回主线）")
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


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
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


if __name__ == "__main__":
    sys.exit(main())
