#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""看板状态派生器：从 git 提交历史 + 卡片台账派生看板卡片状态。

三件套的职责边界（v3 快照）：
  docs/board-ledger.json   卡片身份与初始状态的人工台账（唯一人工入口）
  progress.json             派生产物：status / code 由本脚本写入，CI 独占维护
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

于是状态变更永远有据可查：要么在提交信息里（代码事实），要么在台账里（显式声明），
不存在「悄悄改了快照」这条路径 —— 派生器 --check 会把不一致直接报出来。

用法：
  python scripts/derive_board_status.py --check    校验快照与派生结果一致，不一致退出码 1
  python scripts/derive_board_status.py --write    把派生结果写回快照（仅在有实质变化时落盘）
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

SNAPSHOT_VERSION = 3
ORDER_STEP = 1024
MARKER = re.compile(r"#([A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)*)(?:=(todo|doing|done))?")
# 路线图短号格式：R0-01 / R1-03 / M01 这类。写错这类短号属于真错误，必须拦下；
# 其余短号（例如 plan-hook 用的 #T12）不属于看板台账范畴，只提示不拦截。
ROADMAP_CODE = re.compile(r"^(?:R\d+-\d+|M\d+)$")
STATUS_RANK = {"todo": 0, "doing": 1, "done": 2}
VALID_STATUS = ("todo", "doing", "done")


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


def git_log(root):
    """按时间正序返回 [(sha, subject, body)]。"""
    out = subprocess.run(
        ["git", "-C", root, "log", "--reverse", "--format=%x1e%H%x1f%s%x1f%b"],
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


def derive_status(root, known_codes):
    """返回 (derived, unknown_codes, warnings)。

    derived[code] = 历史里最后一次 `#code=状态` 标记给出的状态。
    unknown_codes = 历史里出现过但台账未登记的「路线图短号」（属于写错，必须拦下）。
    """
    derived = {}
    unknown = set()
    warnings = set()
    for _sha, subject, body in git_log(root):
        text = subject + "\n" + body
        for match in MARKER.finditer(text):
            code = match.group(1).upper()
            status = match.group(2)
            if code not in known_codes:
                if ROADMAP_CODE.match(code):
                    unknown.add(code)
                else:
                    warnings.add(code)
                continue
            if status:
                derived[code] = status
    return derived, unknown, warnings


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


def claims_doing(root, claims, main_ref):
    """台账领用登记 → doing。返回 (active: {code: "doing"}, notes)。

    只认 git 事实，登记不需要「释放」动作。四态判定：
    - claims.branch 的本地 / 远端 ref 都不存在 → 分支已删除（放弃），失效
    - head 不是该分支当前 tip 的祖先（或相等）→ 分支被 rebase/改写，登记过期失效
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
            # head 不是该分支当前 tip 的祖先：分支被 rebase / 重写，登记过期
            notes.append("领用 %s 的分支 %s 已推进或重写，旧登记过期，请重新领用" % (code, branch))
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
        return baseline[code]
    return "todo"


def sort_key(item):
    status = item.get("status", "todo")
    return (STATUS_RANK.get(status, 3), status, int(item.get("orderKey", 0) or 0), item.get("id", ""))


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
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="只校验，不写文件")
    mode.add_argument("--write", action="store_true", help="把派生结果写回快照")
    parser.add_argument(
        "--main-ref", default=None,
        help="claims 失效判定用的主干引用（默认依次尝试 origin/main、main）",
    )
    args = parser.parse_args()

    root = os.path.abspath(args.repo)
    snapshot_path = os.path.join(root, args.snapshot)
    ledger_path = os.path.join(root, args.ledger)

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
    max_order = {"todo": 0, "doing": 0, "done": 0}
    for item in items:
        status = item.get("status", "todo")
        if status in max_order:
            max_order[status] = max(max_order[status], int(item.get("orderKey", 0) or 0))

    for code in sorted(cards):
        card = cards[code]
        card_id = card.get("id", "")
        want = status_of(code, derived, claims_active, baseline)
        item = by_id.get(card_id)
        if item is None:
            if args.check:
                problems.append("台账里的卡片 %s（%s）还不在快照里" % (code, card.get("title", "")))
                continue
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
