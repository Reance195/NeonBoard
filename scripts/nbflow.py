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
    close     程序验收通过后 git-done.ps1 合入 / 结卡 / 回收工作区
    abandon   弃卡时释放服务端 refs/claims 并回收工作区（补上原来的硬死锁）
    status    查看状态机、派发包与回执是否齐备

会话级批量授权（维护者拍板）
----------------------------
一次 ``authorize --by ... --batch ...`` 把凭证写进程序状态；此后本会话内
``approve`` 不必重复传 --authorize/--batch，程序仍把「授权：」「批次：」两行
写进**每一条** ``chore(board):`` 提交体 —— 凭证不因为批量而消失。
程序只搬运授权，不判断「这张卡该不该立」（board-apply.py:15）。

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
    python scripts/nbflow.py status
    python scripts/nbflow.py --selftest
    python scripts/nbflow.py <任意子命令> --dry-run    # 演练：不执行外部命令、不写仓库文件

退出码：0 成功 / 1 校验或状态不合法 / 2 外部命令失败 / 3 用法错误 / 4 读取失败。
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
REQUEST_DIR = REPO / "docs" / "card-requests"
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
    "created": {"dispatched", "abandoned"},
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


def ps_script(name: str, params: list[str]) -> list[str]:
    """定位 pwsh；没有则按 DX-12 回退 Windows PowerShell 5.1 + Bypass。"""
    exe = shutil.which("pwsh")
    if exe:
        base = [exe, "-NoProfile", "-File"]
    else:
        base = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File"]
    return base + [str(REPO / "scripts" / name)] + params


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
    card = args.card.upper()
    if not CARD_ID_RE.match(card):
        err("✗ 卡号不合规：%s" % card)
        return 1
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
    state["sessions"][card] = {"state": "requested", "request": str(p.relative_to(REPO)),
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
    for card in cards:
        p = find_request(card)
        if not p:
            err("✗ 找不到 %s 的请求单" % card)
            return 4
        req = parse_request(p.read_text(encoding="utf-8"))
        if req.get("state") in ("approved", "rejected"):
            err("✗ %s 已 %s，不能重复批准" % (card, req["state"]))
            return 1
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
        # ③ 合回主线：先推立卡分支，再由 git-done 在调用者检出里做 --no-ff 合并 + 快进推送
        code, _ = run(["git", "-C", str(wt), "push", "-u", "origin", branch], dry=ctx.dry_run)
        if code != 0:
            err("✗ 推送立卡分支失败；分支仍在 %s，可手工合并" % wt)
            return 2
        if not ctx.dry_run:
            run(["git", "-C", str(REPO), "worktree", "remove", str(wt)], check=True)
        code, _ = run(ps_script("git-done.ps1", ["-Branch", branch]), dry=ctx.dry_run)
        if code != 0:
            err("✗ 合回主线失败（可能与其他会话撞车，重新同步后重跑 close/approve）")
            return 2
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


def handoff_path(worktree: str) -> Path:
    return Path(worktree) / "handoff.json"


def report_path(worktree: str) -> Path:
    return Path(worktree) / "run-report.json"


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
    hp = handoff_path(wt)
    if ctx.dry_run:
        out("  · [dry-run] 将写派发包 %s\n%s" % (hp, json.dumps(handoff, ensure_ascii=False, indent=2)))
    else:
        hp.write_text(json.dumps(handoff, ensure_ascii=False, indent=2), encoding="utf-8")
        out("  · 派发包：%s" % hp)
    transition(ctx, state, card, "dispatched", branch=args.branch, agent=args.agent,
               worktree=wt, sessionId=handoff["sessionId"], handoff=str(hp))
    return 0


def parse_results(items: list[str], acceptance: list[str] | None = None) -> dict[str, dict]:
    """'<验收项>=pass:证据' → {验收项: {result, evidence}}。

    验收项可以照抄原文，也可以用序号 ``1=pass:证据`` 指向 acceptance[0]，
    避免长验收命令里的等号与转义把 agent 绕晕。
    """
    parsed: dict[str, dict] = {}
    for raw in items:
        if "=" not in raw:
            raise ValueError("结果格式应为 <验收项或序号>=pass|fail:证据，收到 %r" % raw)
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
        if result.strip().lower() not in ("pass", "fail"):
            raise ValueError("结果只能是 pass / fail：%r" % raw)
        parsed[key] = {"result": result.strip().lower(), "evidence": evidence.strip()}
    return parsed


def check_report(acceptance: list[str], results: dict, verify_exit: int,
                 unfinished: list[str]) -> list[str]:
    """程序验收：逐条 acceptance 必须有结论，verify 必须为 0，未完成项必须为空。"""
    problems: list[str] = []
    for item in acceptance:
        if item not in results:
            problems.append("缺验收结论：%s" % item)
        elif results[item]["result"] != "pass":
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
    }
    rp = report_path(wt)
    if ctx.dry_run:
        out("  · [dry-run] 将写执行回执 %s\n%s" % (rp, json.dumps(rep, ensure_ascii=False, indent=2)))
    else:
        rp.write_text(json.dumps(rep, ensure_ascii=False, indent=2), encoding="utf-8")
        out("  · 执行回执：%s" % rp)
    transition(ctx, state, card, "reported", report=str(rp),
               nextStep=rep["nextStep"], allPass=True)
    return 0


def cmd_close(ctx: Ctx, state: dict, args) -> int:
    card = args.card.upper()
    sess = state["sessions"].get(card)
    if not sess or sess["state"] != "reported":
        err("✗ %s 不在 reported（当前：%s）；先 report" % (card, sess["state"] if sess else "无"))
        return 1
    wt = sess.get("worktree", "")
    rp = report_path(wt)
    if not ctx.dry_run:
        if not rp.exists():
            err("✗ 找不到执行回执 %s" % rp)
            return 4
        rep = json.loads(rp.read_text(encoding="utf-8"))
        if not rep.get("allPass") or rep.get("verifyExitCode") != 0 or rep.get("unfinished"):
            err("✗ 回执未全绿，拒绝结卡")
            return 1
    branch = sess.get("branch", args.branch)
    if not branch:
        err("✗ 缺分支名（--branch）")
        return 3
    code, text = run(["git", "-C", str(REPO), "log", branch, "--oneline", "--grep",
                      "#%s=done" % card], capture=True, dry=ctx.dry_run)
    if code == 0 and not ctx.dry_run and "#%s=done" % card not in (text or ""):
        err("✗ 分支历史里没有 #%s=done；git-done 靠它自动结卡，请先补标记或传 --force-close" % card)
        return 1
    params = ["-Branch", branch]
    if args.force_close:
        params += ["-Close", card]
    code, _ = run(ps_script("git-done.ps1", params), dry=ctx.dry_run)
    if code != 0:
        err("✗ 合入 / 结卡 / 清理失败（退出码 %d）；状态保持 reported，可重跑" % code)
        return 2
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

    # 命令拼装：领卡与移交是一次 agent-init.ps1 -Card 调用
    argv = ps_script("agent-init.ps1", ["-Agent", "agent-a", "-Branch", "feature/x", "-Card", "FLOW-01"])
    check("dispatch 只调一次 agent-init 且带 -Card",
          "agent-init.ps1" in " ".join(argv) and "-Card" in argv and "FLOW-01" in argv)
    close_argv = ps_script("git-done.ps1", ["-Branch", "feature/x"])
    check("close 调 git-done 且带分支", "git-done.ps1" in " ".join(close_argv) and "-Branch" in close_argv)
    check("abandon 释放服务端 ref", "refs/claims/FLOW-01" in "refs/claims/%s" % "FLOW-01")
    check("handoff 与 report 文件名固定",
          handoff_path("W").name == "handoff.json" and report_path("W").name == "run-report.json")

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
    r.add_argument("--card", required=True)
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

    ab = sub.add_parser("abandon", parents=[common], help="弃卡：释放 claim ref + 回收工作区")
    ab.add_argument("--card", required=True)
    ab.add_argument("--reason", required=True)
    ab.add_argument("--force", action="store_true", help="允许丢弃未提交改动 / 强删分支")

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
        "abandon": cmd_abandon, "status": cmd_status,
    }
    try:
        return handlers[args.cmd](ctx, state, args)
    except (RuntimeError, OSError, ValueError, json.JSONDecodeError) as exc:
        err("✗ %s" % exc)
        return 1


if __name__ == "__main__":
    sys.exit(main())
