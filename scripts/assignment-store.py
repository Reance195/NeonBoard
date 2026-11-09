#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""assignment-store.py — docs/agent-assignments.json 的唯一写者（DX-149）。

为什么要有这个文件：refs/claims/<短号> 是防双领的原子占位，但它是二进制的
（被占 / 已释放），不承载「谁在跑 / 跑到哪 / 下一步做什么 / 是否还活着」。
台账 claims 只有 agent/branch/head/claimedAt 四字段。agent 做到一半没后续时，
接手者只能读 diff 反推意图（docs/module-split-plan.md §7.2 实录）。

本文件是 §7.2 设计的独立事实源（拍板项 3）：按卡号索引的领用现场记录，
随分支提交、跨机/跨会话可见。硬约束（写进卡片边界）：

- **唯一写者**：全仓只有本脚本写 docs/agent-assignments.json（Z5，
  同 task_gate.py 的单一实现约定）；claim-card.py / handoff.ps1 /
  post-commit / git-done.ps1 / nbflow.py 都只调本脚本的子命令。
- **不参与防双领**：防双领唯一事实源仍是服务端 refs/claims/* + 台账
  claims 投影；本文件只承载判活与接手上下文，绝不作为闸门依据（Z15）。
- 不进 progress.json、不进 board.db、不进 v3 快照。

三态判定（active / stalled / stale / orphan / skip）的唯一实现在
check_claim_refs.py::classify_handoff，本脚本 import 它，不复制判据（Z5）。

心跳节流：post-commit 每次提交都会调 heartbeat，但只有距上次刷新超过
THROTTLE_SECONDS（默认 30 分钟）才落盘——「不每次提交刷台账」（DX-149
验收第 8 条）；stage 变化 / 完成验收项 / 显式 checkpoint 走 --force。

用法：
  python scripts/assignment-store.py upsert --code DX-1 --agent a --branch b
        [--worktree W] [--session-id S] [--stage ST] [--next-step T]
        [--handoff-from OLD] [--progress-done N --progress-total M]
        [--root 检出路径]
  python scripts/assignment-store.py heartbeat (--code DX-1 | --branch B)
        [--stage ST] [--next-step T] [--force]
  python scripts/assignment-store.py remove --code DX-1
  python scripts/assignment-store.py list [--json]
  python scripts/assignment-store.py stale [--stalled-minutes 30]
        [--stale-minutes 240] [--json]
  python scripts/assignment-store.py selftest

--root <检出路径>（DX-162）：显式指定检出时，REPO_ROOT / 台账 / 存储路径全部改按
该检出解析——落盘位置唯一由 root 检出决定，与脚本自身在哪个检出无关。生产调用
（claim-card 的 upsert）一律显式传 --root；显式 --root 优先于 NB_ASSIGNMENTS_PATH
（防遗留的演练环境变量悄悄改写生产落盘位置）。

环境变量 NB_ASSIGNMENTS_PATH 可覆盖存储路径（仅供 selftest 与演练使用，不带
--root 时生效；缺省路径按脚本自身检出解析）。
退出码：0 成功（heartbeat 节流跳过也是 0）/ 2 用法错误 / 3 无该卡条目 /
        4 锁竞争超时 / 5 数据文件损坏。
"""

import argparse
import json
import os
import subprocess
import sys
import time
from contextlib import contextmanager
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BOARD_LEDGER = REPO_ROOT / "docs" / "board-ledger.json"
STORE_PATH = Path(
    os.environ.get("NB_ASSIGNMENTS_PATH") or (REPO_ROOT / "docs" / "agent-assignments.json")
)
LOCK_PATH = STORE_PATH.parent / (STORE_PATH.name + ".lock")

# 心跳节流：30 分钟内的重复心跳不落盘（DX-149 验收第 8 条）
THROTTLE_SECONDS = 30 * 60
# 陈旧锁判定：锁文件超过 600 秒视为持有者已死，可夺（同 register_board_cards 口径）
LOCK_STALE_SECONDS = 600
LOCK_RETRIES = 50
LOCK_RETRY_WAIT = 0.1

STATE_VERSION = 1


def out(msg):
    print(msg)


def err(msg):
    sys.stderr.write(msg + "\n")


@contextmanager
def store_lock():
    """互斥写锁。陈旧锁（>600s）可夺；活锁绝不夺，等 5 秒后放弃（Z13：失败可重跑）。"""
    STORE_PATH.parent.mkdir(parents=True, exist_ok=True)
    stream = None
    deadline = time.time() + LOCK_RETRIES * LOCK_RETRY_WAIT
    while True:
        try:
            stream = os.open(str(LOCK_PATH), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            break
        except FileExistsError:
            try:
                age = time.time() - LOCK_PATH.stat().st_mtime
                if age > LOCK_STALE_SECONDS:
                    err("  · assignment 锁超过 %ds 视为陈旧，夺锁" % LOCK_STALE_SECONDS)
                    LOCK_PATH.unlink(missing_ok=True)
                    continue
            except OSError:
                continue
            if time.time() > deadline:
                err("✗ assignment 锁被其它进程持有超过 5 秒，放弃（稍后重跑即可）")
                sys.exit(4)
            time.sleep(LOCK_RETRY_WAIT)
        except OSError as exc:
            err("✗ assignment 锁创建失败：%s" % exc)
            sys.exit(4)
    try:
        os.write(stream, str(os.getpid()).encode())
        yield
    finally:
        try:
            os.close(stream)
        except OSError:
            pass
        try:
            LOCK_PATH.unlink(missing_ok=True)
        except OSError:
            pass


def load_store():
    if not STORE_PATH.exists():
        return {"version": STATE_VERSION, "assignments": {}}
    try:
        data = json.loads(STORE_PATH.read_text(encoding="utf-8-sig"))
    except ValueError as exc:
        err("✗ %s JSON 损坏：%s" % (STORE_PATH, exc))
        sys.exit(5)
    data.setdefault("version", STATE_VERSION)
    data.setdefault("assignments", {})
    return data


def save_store(data):
    tmp = str(STORE_PATH) + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(data, fh, indent=4, sort_keys=True, ensure_ascii=False)
        fh.write("\n")
    os.replace(tmp, STORE_PATH)


def git_lines(*args):
    run = subprocess.run(
        ["git", "-C", str(REPO_ROOT)] + list(args),
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if run.returncode != 0:
        return None
    return [ln.strip() for ln in run.stdout.splitlines() if ln.strip()]


def read_board_baseline():
    """台账 baseline（本地副本）。读不到不致命：stale 判定降级为 skip。"""
    try:
        data = json.loads(BOARD_LEDGER.read_text(encoding="utf-8-sig"))
        return data.get("baseline") or {}
    except (OSError, ValueError):
        return {}


def classify_entry(code, entry, now_ts, stalled_minutes, stale_minutes, baseline):
    """单条 assignment 的三态判定。判据唯一实现在 check_claim_refs.classify_handoff。"""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from check_claim_refs import classify_handoff

    branch = (entry or {}).get("branch", "")
    has_branch = False
    branch_last = 0
    if branch:
        probe = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "show-ref", "--verify", "--quiet",
             "refs/heads/" + branch],
            capture_output=True, check=False)
        remote = subprocess.run(
            ["git", "-C", str(REPO_ROOT), "show-ref", "--verify", "--quiet",
             "refs/remotes/origin/" + branch],
            capture_output=True, check=False)
        has_branch = probe.returncode == 0 or remote.returncode == 0
    if has_branch:
        lines = git_lines("log", "-1", "--format=%ct", branch)
        if lines:
            try:
                branch_last = int(lines[0])
            except ValueError:
                branch_last = 0
    verdict, detail = classify_handoff(
        baseline.get(code),
        int((entry or {}).get("updatedAt") or 0),
        branch_last, now_ts, stalled_minutes, stale_minutes, has_branch)
    return verdict, detail, branch_last


def cmd_upsert(args):
    now = int(time.time())
    with store_lock():
        data = load_store()
        entries = data["assignments"]
        code = args.code.upper()
        entry = entries.get(code) or {}
        created = code not in entries
        entry.setdefault("claimedAt", now)
        entry.update({
            "agent": args.agent,
            "branch": args.branch,
            "updatedAt": now,
        })
        if args.worktree:
            entry["worktree"] = args.worktree
        entry.setdefault("worktree", "")
        if args.session_id is not None:
            entry["sessionId"] = args.session_id
        entry.setdefault("sessionId", "")
        if args.stage:
            entry["stage"] = args.stage
        entry.setdefault("stage", "")
        if args.next_step is not None:
            entry["nextStep"] = args.next_step
        entry.setdefault("nextStep", "")
        if args.handoff_from:
            entry["handoffFrom"] = args.handoff_from
        entry.setdefault("handoffFrom", "")
        if args.progress_done is not None or args.progress_total is not None:
            prog = entry.get("progress") or {"done": 0, "total": 0}
            if args.progress_done is not None:
                prog["done"] = args.progress_done
            if args.progress_total is not None:
                prog["total"] = args.progress_total
            entry["progress"] = prog
        entry.setdefault("progress", {"done": 0, "total": 0})
        entries[code] = entry
        data["updatedAt"] = now
        save_store(data)
    out("%s assignment %s（%s）" % ("新增" if created else "更新", code, args.agent))
    return 0


def cmd_heartbeat(args):
    now = int(time.time())
    with store_lock():
        data = load_store()
        entries = data["assignments"]
        if args.code:
            code = args.code.upper()
            if code not in entries:
                # 无条目属正常（老卡先于本机制领用），不算失败
                out("· %s 无 assignment 条目，心跳跳过" % code)
                return 0
            keys = [code]
        else:
            branch = args.branch
            keys = [c for c, e in entries.items()
                    if (e or {}).get("branch", "") == branch]
            if not keys:
                out("· 分支 %s 无 assignment 条目，心跳跳过" % branch)
                return 0
        changed = False
        for code in keys:
            entry = entries[code]
            new_stage = args.stage or ""
            new_step = args.next_step
            content_changed = (
                (new_stage and entry.get("stage", "") != new_stage)
                or (new_step is not None and entry.get("nextStep", "") != new_step)
            )
            age = now - int(entry.get("updatedAt") or 0)
            if not args.force and not content_changed and age < THROTTLE_SECONDS:
                out("· %s 心跳节流（上次刷新 %d 分钟前）" % (code, age // 60))
                continue
            entry["updatedAt"] = now
            if new_stage:
                entry["stage"] = new_stage
            if new_step is not None:
                entry["nextStep"] = new_step
            changed = True
            out("· %s 心跳已刷新" % code)
        if changed:
            data["updatedAt"] = now
            save_store(data)
    return 0


def cmd_remove(args):
    with store_lock():
        data = load_store()
        code = args.code.upper()
        if code in data["assignments"]:
            data["assignments"].pop(code)
            data["updatedAt"] = int(time.time())
            save_store(data)
            out("· %s assignment 已移除" % code)
        else:
            out("· %s 无 assignment 条目（幂等）" % code)
    return 0


def cmd_list(args):
    data = load_store()
    if args.json:
        # ensure_ascii=True：PS 按 [Console]::OutputEncoding（GBK）解码子进程 stdout，
        # 中文值会破坏 JSON 结构（check_claim_refs 的 --emit-claimed 同款教训），
        # 程序消费的输出必须纯 ASCII；落盘文件仍是 ensure_ascii=False 的 UTF-8 中文。
        print(json.dumps(data, ensure_ascii=True, indent=2))
        return 0
    entries = data.get("assignments") or {}
    if not entries:
        out("（无 assignment 条目）")
        return 0
    for code in sorted(entries):
        e = entries[code]
        out("%-10s %s @ %s  stage=%s  updated=%s" % (
            code, e.get("agent", "?"), e.get("branch", "?"),
            e.get("stage", "-"), time.strftime(
                "%m-%d %H:%M", time.localtime(int(e.get("updatedAt") or 0)))))
        if e.get("nextStep"):
            out("           ↳ %s" % e["nextStep"])
    return 0


def cmd_stale(args):
    now = int(time.time())
    baseline = read_board_baseline()
    data = load_store()
    results = []
    for code, entry in sorted((data.get("assignments") or {}).items()):
        verdict, detail, branch_last = classify_entry(
            code, entry, now, args.stalled_minutes, args.stale_minutes, baseline)
        results.append({
            "code": code,
            "verdict": verdict,
            "detail": detail,
            "agent": (entry or {}).get("agent", ""),
            "branch": (entry or {}).get("branch", ""),
            "nextStep": (entry or {}).get("nextStep", ""),
            "updatedAt": int((entry or {}).get("updatedAt") or 0),
            "lastActivity": max(branch_last, int((entry or {}).get("updatedAt") or 0)),
            "ageMinutes": round((now - max(branch_last, int((entry or {}).get("updatedAt") or 0))) / 60, 1),
        })
    if args.json:
        print(json.dumps({"checkedAt": now, "results": results},
                         ensure_ascii=True, indent=2))
        return 0
    for r in results:
        out("%-10s %-8s %s @ %s  最近活动 %s（%s 分钟前）%s" % (
            r["code"], verdict_label(r["verdict"]), r["agent"], r["branch"],
            time.strftime("%m-%d %H:%M", time.localtime(r["lastActivity"])) if r["lastActivity"] else "-",
            r["ageMinutes"],
            ("  ↳ " + r["nextStep"]) if r["nextStep"] else ""))
    return 0


def verdict_label(v):
    return {"active": "活动", "stalled": "疑似中断", "stale": "陈旧占用",
            "orphan": "孤儿占位", "skip": "跳过"}.get(v, v)


def selftest():
    """可执行自测：锁 / upsert / heartbeat 节流 / remove / 写回格式。纯文件操作，不碰 git。"""
    import tempfile
    failures = []

    def case(name, ok, detail=""):
        if ok:
            out("  ✓ %s" % name)
        else:
            out("  ✗ %s%s" % (name, ("（实测：%s）" % detail) if detail else ""))
            failures.append(name)

    with tempfile.TemporaryDirectory() as td:
        os.environ["NB_ASSIGNMENTS_PATH"] = str(Path(td) / "assignments.json")
        global STORE_PATH, LOCK_PATH
        STORE_PATH = Path(os.environ["NB_ASSIGNMENTS_PATH"])
        LOCK_PATH = STORE_PATH.parent / (STORE_PATH.name + ".lock")

        r = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "upsert",
             "--code", "DX-999", "--agent", "agent-a", "--branch", "feature/t1",
             "--worktree", ".worktrees/agent-a", "--stage", "编码中",
             "--next-step", "已搬 4 条，剩 6 条"],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        case("upsert 建条目", r.returncode == 0, (r.stderr or r.stdout).strip())

        raw = STORE_PATH.read_text(encoding="utf-8")
        data = json.loads(raw)
        e = data["assignments"]["DX-999"]
        case("条目字段齐全",
             e["agent"] == "agent-a" and e["branch"] == "feature/t1"
             and e["stage"] == "编码中" and e["nextStep"] == "已搬 4 条，剩 6 条"
             and e["handoffFrom"] == "" and e["sessionId"] == ""
             and e["progress"] == {"done": 0, "total": 0}, json.dumps(e, ensure_ascii=False))
        case("写回格式（indent=4 / sort_keys / 尾换行）",
             raw.startswith("{\n    \"assignments\"") and raw.endswith("}\n"), repr(raw[:40]))
        case("版本字段", data.get("version") == STATE_VERSION, str(data.get("version")))

        r = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "upsert",
             "--code", "DX-999", "--agent", "agent-b", "--branch", "feature/t1",
             "--handoff-from", "agent-a"],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        case("upsert 幂等更新（保留未给字段）", r.returncode == 0, (r.stderr or "").strip())
        e = json.loads(STORE_PATH.read_text(encoding="utf-8"))["assignments"]["DX-999"]
        case("更新后 stage/nextStep 保留、agent 换新、handoffFrom 落盘",
             e["agent"] == "agent-b" and e["stage"] == "编码中"
             and e["handoffFrom"] == "agent-a", json.dumps(e, ensure_ascii=False))

        r = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "heartbeat",
             "--code", "DX-999"],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        case("心跳节流（刚更新过 → 跳过落盘）",
             r.returncode == 0 and "节流" in (r.stdout or ""), (r.stdout or r.stderr).strip())

        r = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "heartbeat",
             "--code", "DX-999", "--force", "--stage", "自测中"],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        case("--force 心跳落盘并改 stage",
             r.returncode == 0
             and json.loads(STORE_PATH.read_text(encoding="utf-8"))
             ["assignments"]["DX-999"]["stage"] == "自测中",
             (r.stdout or r.stderr).strip())

        r = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "heartbeat",
             "--branch", "no-such-branch"],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        case("按 branch 心跳：无条目跳过且 exit 0", r.returncode == 0, (r.stderr or "").strip())

        r = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "remove",
             "--code", "DX-999"],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        case("remove 删条目", r.returncode == 0, (r.stderr or "").strip())
        r = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "remove",
             "--code", "DX-999"],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        case("remove 幂等", r.returncode == 0, (r.stderr or "").strip())
        case("锁文件不残留", not LOCK_PATH.exists(), str(LOCK_PATH))

        # DX-162：--root 落盘到指定检出，且优先于 NB_ASSIGNMENTS_PATH
        #（本段仍处于演练环境变量生效状态，正好钉住优先级）
        root_b = Path(td) / "checkout-b"
        r = subprocess.run(
            [sys.executable, str(Path(__file__).resolve()), "upsert",
             "--root", str(root_b),
             "--code", "DX-998", "--agent", "agent-r", "--branch", "feature/t2"],
            capture_output=True, text=True, encoding="utf-8", errors="replace")
        store_b = root_b / "docs" / "agent-assignments.json"
        case("--root 落盘到指定检出（优先于 NB_ASSIGNMENTS_PATH）",
             r.returncode == 0 and store_b.exists()
             and "DX-998" in store_b.read_text(encoding="utf-8"),
             ((r.stderr or r.stdout) or "").strip())
        case("--root 时不写演练路径", "DX-998" not in STORE_PATH.read_text(encoding="utf-8"),
             str(STORE_PATH))
        case("--root 的锁文件不残留",
             not (root_b / "docs" / "agent-assignments.json.lock").exists(), str(root_b))

        os.environ.pop("NB_ASSIGNMENTS_PATH", None)
        global_restore()

    if failures:
        err("assignment-store 自测失败 %d 例" % len(failures))
        return 1
    out("assignment-store 自测通过")
    return 0


def global_restore():
    """selftest 结束后还原模块级路径常量（防同进程复用误读临时文件）。"""
    global STORE_PATH, LOCK_PATH
    STORE_PATH = Path(REPO_ROOT / "docs" / "agent-assignments.json")
    LOCK_PATH = STORE_PATH.parent / (STORE_PATH.name + ".lock")


def apply_root(root):
    """DX-162：显式 --root 时，REPO_ROOT / 台账 / 存储路径全部改按指定检出解析。

    优先级：显式 --root > NB_ASSIGNMENTS_PATH > 脚本自身路径派生。--root 是
    生产调用路径（claim-card 的 upsert 一律传），落盘位置必须唯一由 root 检出
    决定，不被遗留的演练环境变量改写；NB_ASSIGNMENTS_PATH 在不带 --root 时
    行为不变（selftest / 演练既有机制）。
    """
    global REPO_ROOT, BOARD_LEDGER, STORE_PATH, LOCK_PATH
    REPO_ROOT = Path(root).resolve()
    BOARD_LEDGER = REPO_ROOT / "docs" / "board-ledger.json"
    STORE_PATH = REPO_ROOT / "docs" / "agent-assignments.json"
    LOCK_PATH = STORE_PATH.parent / (STORE_PATH.name + ".lock")


def main():
    parser = argparse.ArgumentParser(description="agent-assignments 唯一写者")
    sub = parser.add_subparsers(dest="cmd", required=True)

    # DX-162：--root 覆盖 REPO_ROOT（落盘位置唯一由 root 检出决定），缺省仍按脚本自身路径
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--root", default=None, metavar="检出路径",
                        help="覆盖 REPO_ROOT：落盘与 git 操作改按指定检出解析（DX-162）")

    p = sub.add_parser("upsert", help="建/更新领用现场条目", parents=[common])
    p.add_argument("--code", required=True)
    p.add_argument("--agent", required=True)
    p.add_argument("--branch", required=True)
    p.add_argument("--worktree", default="")
    p.add_argument("--session-id", default=None)
    p.add_argument("--stage", default="")
    p.add_argument("--next-step", default=None)
    p.add_argument("--handoff-from", default="")
    p.add_argument("--progress-done", type=int, default=None)
    p.add_argument("--progress-total", type=int, default=None)
    p.set_defaults(func=cmd_upsert)

    p = sub.add_parser("heartbeat", help="刷新 updatedAt（默认节流）", parents=[common])
    p.add_argument("--code", default="")
    p.add_argument("--branch", default="")
    p.add_argument("--stage", default="")
    p.add_argument("--next-step", default=None)
    p.add_argument("--force", action="store_true")
    p.set_defaults(func=cmd_heartbeat)

    p = sub.add_parser("remove", help="删除条目（幂等）", parents=[common])
    p.add_argument("--code", required=True)
    p.set_defaults(func=cmd_remove)

    p = sub.add_parser("list", help="列出条目", parents=[common])
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("stale", help="三态判活清单（判据唯一实现在 check_claim_refs）",
                       parents=[common])
    p.add_argument("--stalled-minutes", type=int, default=30)
    p.add_argument("--stale-minutes", type=int, default=240)
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_stale)

    sub.add_parser("selftest", help="内置自测（写临时文件，不碰真实存储）")

    args = parser.parse_args()
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    if args.cmd == "selftest":
        return selftest()
    if args.root:
        apply_root(args.root)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
