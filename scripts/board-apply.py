#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""board-apply.py —— 建卡请求的应用器（v1 只读 dry-run，绝不写文件）。

为什么要有这个文件
------------------
建卡目前只有一条路：人手工把 JSON 贴进 docs/board-ledger.json 的 cards，再手加
baseline。这条路的问题是「贴」这个动作没法预制、没法复查，贴错一个字段要到
下次派生才暴露（结卡快照漂移已经出过两次）。

本文件把「贴」之前的判断全部机器化：读一份建卡请求清单（.md 里的 ```json 片段，
或纯 .json），对照台账与实际文件系统，输出「如果批准，会多出哪几行」。**v1 只输出，
不落盘**，--apply 会被显式拒绝并给出退出码 5 —— 因为「写台账」是维护者授权动作，
程序替人写字据之前，必须先有一段时间让人看它 20 次输出对不对。

校验的不是格式，是「这张卡能不能立」
------------------------------------
  id.format          卡号不合 task_gate.py 的正则 → 提交时闸门直接拒
  id.exists          台账已有同号卡（重复立卡会覆盖既有定义）
  id.duplicate       清单内部重复
  id.claimed         已有 claim 占位但卡片不存在（孤儿占位）
  card.field         缺 stage/title/scope/acceptance
  scope.missing_path scope 里的精确路径在仓库里不存在（搬运后必踩）
  scope.glob_empty   scope 的 glob 匹配不到任何文件
  scope.overlap      与**在办卡**的 scope 重叠 → 那张卡的精确 scope 会失效
  depends.missing    依赖的卡号在台账和本批里都不存在
  depends.cycle      本批依赖成环
  depends.not_done   依赖未结（不阻断，但不入就绪队列）
  acceptance.prose   验收项里没有任何 ASCII 记号（命令/路径/数字）→ 不可执行
  plan.not_additive  计划不是纯增量（会改既有行）→ 拒绝

用法
----
    python scripts/board-apply.py docs/module-split-card-requests.md
    python scripts/board-apply.py docs/batches/mod-1.md --diff
    python scripts/board-apply.py req.json --json
    python scripts/board-apply.py --selftest

退出码：0 通过 / 2 有阻断问题 / 3 无需变更 / 4 读取失败 / 5 拒绝 --apply
"""

import argparse
import difflib
import io
import json
import os
import re
import sys
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent

sys.path.insert(0, str(SCRIPT_DIR))
from check_scope import glob_to_regex, normalize_scope  # noqa: E402  唯一实现，不复制一份

# 与 scripts/task_gate.py 的正则保持一致：前缀 ≤4 字母；无短横形态（M19）走第二分支。
CARD_ID_RE = re.compile(r"(?:[A-Za-z]{1,4}\d*-\d+)|(?:M\d+)")
GLOB_CHARS = set("*?[")
SKIP_DIRS = {
    ".git", "build", "build-local", "build-release", "node_modules",
    ".worktrees", "__pycache__", "dist", ".vs", ".qtc_clangd",
}
REQUIRED_FIELDS = ("id", "stage", "title", "scope", "acceptance")


# ---------------------------------------------------------------- 载入

def load_ledger(path: Path) -> dict:
    with io.open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    for key in ("cards", "baseline"):
        data.setdefault(key, {})
    data.setdefault("claims", {})
    return data


def _card_like(obj) -> bool:
    """判断一块 JSON 是否像一张卡（避免把清单里无关的 JSON 片段吃进来）。"""
    return isinstance(obj, dict) and ("title" in obj or "scope" in obj)


def parse_manifest_text(text: str, notes: list) -> dict:
    """从 Markdown 或 JSON 文本里抽出 {卡号: 卡片}。

    兼容三种形态：
      1. ```json 围栏里的无花括号片段：  "MOD-02": { ... }
      2. ```json 围栏里的完整对象：      { "MOD-02": { ... } }  或  { "id": "...", ... }
      3. 整个文件就是 JSON（.json 清单）
    """
    cards = {}

    def absorb(obj, where):
        if not isinstance(obj, dict):
            return
        if "id" in obj and _card_like(obj):
            cards[obj["id"]] = obj
            return
        for key, val in obj.items():
            if isinstance(val, dict) and _card_like(val):
                card = dict(val)
                card.setdefault("id", key)
                cards[key] = card
            else:
                notes.append("%s：忽略非卡片键 %r" % (where, key))

    fences = re.findall(r"```json\n(.*?)\n```", text, re.S)
    if not fences:
        try:
            absorb(json.loads(text), "整文件")
        except ValueError as exc:
            raise SystemExit("清单既不是 Markdown JSON 片段也不是合法 JSON：%s" % exc)
        return cards

    for idx, block in enumerate(fences, 1):
        where = "json 块 #%d" % idx
        for candidate in (block, "{" + block + "}"):
            try:
                absorb(json.loads(candidate), where)
                break
            except ValueError:
                continue
        else:
            notes.append("%s：无法解析为 JSON，已跳过" % where)
    return cards


def load_manifest(path: Path) -> tuple:
    with io.open(path, encoding="utf-8") as fh:
        text = fh.read()
    notes = []
    cards = parse_manifest_text(text, notes)
    if not cards:
        raise SystemExit("清单里没有解析到任何卡片：%s" % path)
    return cards, notes


# ---------------------------------------------------------------- 校验

def repo_files(repo: Path, limit: int = 20000) -> list:
    out = []
    for root, dirs, files in os.walk(repo):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            rel = (Path(root) / name).relative_to(repo).as_posix()
            out.append(rel)
            if len(out) >= limit:
                return out
    return out


def stage_prefix(cid: str) -> str:
    if "-" in cid:
        return cid.split("-", 1)[0]
    m = re.match(r"[A-Za-z]+", cid)
    return m.group(0) if m else cid


def validate(cards: dict, ledger: dict, repo: Path) -> tuple:
    blockers, warnings, infos = [], [], []
    existing_cards = ledger["cards"]
    baseline = ledger["baseline"]
    claims = ledger["claims"] or {}
    files = repo_files(repo)
    # 目录式 scope（plugins/x/）先规范成 plugins/x/**，与 check_scope 同一口径；
    # 否则「目录式」会被当精确路径，明明覆盖一片文件却报 scope.glob_empty（DX-17）。
    open_scopes = {cid: [normalize_scope(p) for p in (c.get("scope") or []) if p]
                   for cid, c in existing_cards.items()
                   if baseline.get(cid, "done") != "done"}

    def block(code, cid, msg):
        blockers.append((code, cid, msg))

    def warn(code, cid, msg):
        warnings.append((code, cid, msg))

    for cid, card in cards.items():
        # --- id
        if not CARD_ID_RE.fullmatch(cid):
            block("id.format", cid, "不合 task_gate.py 正则（前缀 ≤4 字母，形如 R14-01 / M19）")
        if cid in existing_cards:
            block("id.exists", cid, "台账已有同号卡，重复立卡会覆盖既有定义")
        if cid in claims and cid not in existing_cards:
            block("id.claimed", cid, "服务端/台账已有 claim 占位但卡片不存在（孤儿占位，先释放）")
        if cid in baseline and cid not in existing_cards:
            block("id.exists", cid, "baseline 已有该号但 cards 无定义（先核对，别叠加）")

        # --- 必填字段
        for field in REQUIRED_FIELDS:
            if not card.get(field):
                block("card.field", cid, "缺少必填字段 %s" % field)
        if not (card.get("note") or "").strip():
            warn("card.note", cid, "note 为空，建议写清「起因 / 目标 / 边界」")
        if card.get("stage") and card["stage"] != stage_prefix(cid):
            warn("card.stage", cid, "stage=%r 与卡号前缀 %r 不一致（阶段条会归错组）"
                 % (card["stage"], stage_prefix(cid)))

        # --- acceptance
        acc = card.get("acceptance") or []
        if isinstance(acc, str):
            block("card.field", cid, "acceptance 必须是数组，不能是字符串")
            acc = []
        seen = set()
        for item in acc:
            if item in seen:
                warn("acceptance.dup", cid, "验收项重复：%s" % item[:40])
            seen.add(item)
            if not re.search(r"[A-Za-z0-9/\\._-]", str(item)):
                warn("acceptance.prose", cid, "验收项无可验证记号（命令/路径/数字）：%s" % str(item)[:40])
            elif len(str(item)) < 12:
                warn("acceptance.short", cid, "验收项过短，可能不可判定：%s" % item)

        # --- scope
        scope = card.get("scope") or []
        clashes, covers = {}, {}
        for raw_pattern in scope:
            pattern = normalize_scope(raw_pattern)
            is_glob = any(ch in GLOB_CHARS for ch in pattern)
            if is_glob:
                rx = glob_to_regex(pattern)
                if not any(rx.match(f) for f in files):
                    block("scope.glob_empty", cid, "glob 匹配不到任何文件：%s" % raw_pattern)
            elif not (repo / pattern).exists():
                # 新增文件是合法的 scope；笔误的特征是**父目录也不存在**。
                if (repo / pattern).parent.exists():
                    warn("scope.new_path", cid,
                         "路径尚不存在（若为本卡新建则正常）：%s" % pattern)
                else:
                    block("scope.missing_path", cid,
                          "精确路径与父目录都不存在，疑似笔误：%s" % pattern)

            for other_id, other_patterns in open_scopes.items():
                for other_pattern in other_patterns:
                    other_is_glob = any(ch in GLOB_CHARS for ch in other_pattern)
                    if not is_glob and not other_is_glob:
                        if pattern == other_pattern:
                            clashes.setdefault(pattern, set()).add(other_id)
                    elif is_glob and other_is_glob:
                        continue          # glob 之间重叠是正常现象，不报
                    elif is_glob:
                        if glob_to_regex(pattern).match(other_pattern):
                            covers.setdefault(other_id, set()).add(other_pattern)
                    elif glob_to_regex(other_pattern).match(pattern):
                        covers.setdefault(other_id, set()).add(pattern)

        for path, owners in sorted(clashes.items()):
            warn("scope.clash", cid,
                 "与在办卡 %s 争同一精确路径 %s —— 双方都会合法地改它，先后顺序需人工定"
                 % ("、".join(sorted(owners)), path))
        if covers:
            total = sum(len(v) for v in covers.values())
            warn("scope.cover", cid,
                 "本卡的 glob 覆盖 %d 张在办卡的 %d 个精确路径（%s）"
                 "—— 合并后 check_scope 无法分辨改动归属"
                 % (len(covers), total,
                    "、".join("%s:%d" % (k, len(v)) for k, v in sorted(covers.items()))))

        # --- depends
        deps = card.get("depends") or []
        for dep in deps:
            if dep == cid:
                block("depends.self", cid, "依赖自己")
            elif dep not in cards and dep not in existing_cards:
                block("depends.missing", cid, "依赖的 %s 在台账与本批中都不存在" % dep)
            elif dep in cards:
                infos.append(("depends.same_batch", cid,
                              "依赖 %s 属本批新增，需先结卡才能开本卡" % dep))
            elif dep not in baseline:
                warn("depends.baseline", cid, "依赖 %s 在 cards 里但没有 baseline 条目" % dep)
            elif baseline[dep] != "done":
                infos.append(("depends.not_done", cid,
                              "依赖 %s 状态为 %s，本卡暂不入就绪队列" % (dep, baseline[dep])))

    # --- 本批依赖成环
    graph = {cid: [d for d in (cards[cid].get("depends") or []) if d in cards] for cid in cards}
    state = {}

    def walk(node, stack):
        state[node] = 1
        for nxt in graph.get(node, []):
            if state.get(nxt) == 1:
                block("depends.cycle", node, "依赖成环：%s" % " → ".join(stack + [nxt]))
            elif state.get(nxt) is None:
                walk(nxt, stack + [nxt])
        state[node] = 2

    for cid in graph:
        if state.get(cid) is None:
            walk(cid, [cid])

    return blockers, warnings, infos


# ---------------------------------------------------------------- 计划

def serialize(ledger: dict) -> str:
    """与 claim-card.py / release-claims.py 的写回格式完全一致。"""
    return json.dumps(ledger, indent=4, sort_keys=True, ensure_ascii=False) + "\n"


def build_plan(ledger: dict, cards: dict) -> tuple:
    plan = json.loads(serialize(ledger))
    for cid, card in cards.items():
        normalized = dict(card)
        normalized["id"] = cid
        normalized.setdefault("depends", [])
        normalized.setdefault("note", "")
        plan["cards"][cid] = normalized
        plan["baseline"][cid] = "todo"
    return plan


def diff_text(before: str, after: str) -> tuple:
    lines = list(difflib.unified_diff(
        before.splitlines(True), after.splitlines(True),
        fromfile="docs/board-ledger.json（当前）",
        tofile="docs/board-ledger.json（批准后）", n=3))
    removed = [ln for ln in lines if ln.startswith("-") and not ln.startswith("---")]
    return lines, removed


def additive_violations(old: dict, new: dict) -> list:
    """语义层判定「纯增量」，不看文本 diff。

    文本层做不了这件事：往 JSON 字典里插一个键，前一行必须补逗号，
    任何追加都会在 diff 里表现为「一行改写 + 一行新增」。真正的判据是
    「旧数据是否被原样保留」，只有它可以证明这次写入没有篡改既有卡。
    """
    bad = []
    if old.get("version") != new.get("version"):
        bad.append("version 被改动")
    old_cards, new_cards = old.get("cards") or {}, new.get("cards") or {}
    for cid, card in old_cards.items():
        if new_cards.get(cid) != card:
            bad.append("cards[%s] 被改动或删除" % cid)
    old_base, new_base = old.get("baseline") or {}, new.get("baseline") or {}
    for cid, state in old_base.items():
        if new_base.get(cid) != state:
            bad.append("baseline[%s] 被改动" % cid)
    if (old.get("claims") or {}) != (new.get("claims") or {}):
        bad.append("claims 被改动（建卡不应触碰领用登记）")
    return bad


def ready_state(cid: str, card: dict, ledger: dict) -> str:
    waiting = [d for d in (card.get("depends") or [])
               if ledger["baseline"].get(d) != "done"]
    if (ledger.get("claims") or {}).get(cid):
        return "已占用"
    return "就绪" if not waiting else "被 %s 阻塞" % "、".join(waiting)


# ---------------------------------------------------------------- 输出

def render(path: Path, cards: dict, ledger: dict, blockers, warnings, infos,
           diff_lines, show_diff: bool) -> None:
    def out(text=""):
        print(text)

    out("清单：%s" % path)
    out("台账：%d 张卡 / baseline 已结 %d / 在办 %d"
        % (len(ledger["cards"]),
           sum(1 for v in ledger["baseline"].values() if v == "done"),
           sum(1 for v in ledger["baseline"].values() if v != "done")))
    out("")
    out("计划：新增 %d 张卡，baseline +%d 行 todo，claims 不动" % (len(cards), len(cards)))
    for cid, card in cards.items():
        out("  %-9s %s" % (cid, card.get("title", "")[:64]))
        out("            stage=%-4s scope=%d  acceptance=%d  depends=%s  → %s"
            % (card.get("stage", "?"), len(card.get("scope") or []),
               len(card.get("acceptance") or []),
               "、".join(card.get("depends") or []) or "-",
               ready_state(cid, card, ledger)))

    if show_diff and diff_lines:
        out("")
        out("待写入 diff（%d 行）：" % len(diff_lines))
        for line in diff_lines:
            sys.stdout.write(line if line.endswith("\n") else line + "\n")

    out("")
    if blockers:
        out("阻断问题 %d 条：" % len(blockers))
        for code, cid, msg in blockers:
            out("  ✗ [%s] %s：%s" % (code, cid, msg))
    if warnings:
        out("提示 %d 条：" % len(warnings))
        for code, cid, msg in warnings:
            out("  ⚠ [%s] %s：%s" % (code, cid, msg))
    if infos:
        out("依赖状态 %d 条：" % len(infos))
        for code, cid, msg in infos:
            out("  · [%s] %s：%s" % (code, cid, msg))

    out("")
    if blockers:
        out("结论：不可建卡 —— 先修掉 %d 条阻断问题。" % len(blockers))
    else:
        ready = [c for c in cards if ready_state(c, cards[c], ledger) == "就绪"]
        out("结论：可以建卡。就绪 %d 张（%s），其余需等依赖结卡。"
            % (len(ready), "、".join(ready) or "-"))
        out("本脚本不写文件：请维护者按上面的 diff 手工落库，或另行授权后执行。")


# ---------------------------------------------------------------- 自检

def selftest() -> int:
    failures = []
    total = [0]

    def check(name, cond, detail=""):
        total[0] += 1
        if not cond:
            failures.append("%s %s" % (name, detail))

    with tempfile.TemporaryDirectory() as tmp:
        repo = Path(tmp)
        (repo / "src").mkdir()
        (repo / "src" / "A.cpp").write_text("", encoding="utf-8")
        (repo / "src" / "B.cpp").write_text("", encoding="utf-8")
        (repo / "docs").mkdir()

        ledger = {
            "version": 1,
            "cards": {
                "R1-01": {"id": "R1-01", "stage": "R1", "title": "在办",
                          "scope": ["src/A.cpp"], "acceptance": ["退出码 0"],
                          "note": "n", "depends": []},
                "R1-02": {"id": "R1-02", "stage": "R1", "title": "已结",
                          "scope": ["src/B.cpp"], "acceptance": ["退出码 0"],
                          "note": "n", "depends": []},
            },
            "baseline": {"R1-01": "todo", "R1-02": "done"},
            "claims": {},
        }

        good = {"id": "R2-01", "stage": "R2", "title": "ok",
                "scope": ["src/*.cpp"], "acceptance": ["python scripts/x.py 退出 0"],
                "note": "起因/目标/边界", "depends": ["R1-02"]}
        bl, wl, _ = validate({"R2-01": good}, ledger, repo)
        check("clean", not bl, str(bl))
        check("cover", any(c == "scope.cover" for c, _, _ in wl), str(wl))

        clash = dict(good, id="R2-10", scope=["src/A.cpp"])
        bl, wl, _ = validate({"R2-10": clash}, ledger, repo)
        check("clash", any(c == "scope.clash" for c, _, _ in wl), str(wl))

        bl, _, _ = validate({"R1-01": good}, ledger, repo)
        check("exists", any(c == "id.exists" for c, _, _ in bl), str(bl))

        bad_id = dict(good, id="MODULE-1")
        bl, _, _ = validate({"MODULE-1": bad_id}, ledger, repo)
        check("id.format", any(c == "id.format" for c, _, _ in bl), str(bl))

        bl, _, _ = validate({"R2-02": dict(good, id="R2-02",
                                          scope=["nodir/Nope.cpp"])}, ledger, repo)
        check("missing_path", any(c == "scope.missing_path" for c, _, _ in bl), str(bl))

        bl, wl, _ = validate({"R2-09": dict(good, id="R2-09",
                                           scope=["src/New.cpp"])}, ledger, repo)
        check("new_path", not bl and any(c == "scope.new_path" for c, _, _ in wl),
              "%s / %s" % (bl, wl))

        bl, _, _ = validate({"R2-03": dict(good, id="R2-03",
                                          scope=["nothing/*.cpp"])}, ledger, repo)
        check("glob_empty", any(c == "scope.glob_empty" for c, _, _ in bl), str(bl))

        bl, _, _ = validate({"R2-04": dict(good, id="R2-04",
                                          depends=["R9-99"])}, ledger, repo)
        check("dep_missing", any(c == "depends.missing" for c, _, _ in bl), str(bl))

        cyc = {"R2-05": dict(good, id="R2-05", depends=["R2-06"]),
               "R2-06": dict(good, id="R2-06", depends=["R2-05"])}
        bl, _, _ = validate(cyc, ledger, repo)
        check("cycle", any(c == "depends.cycle" for c, _, _ in bl), str(bl))

        bl, wl, infos = validate({"R2-07": dict(good, id="R2-07",
                                                depends=["R1-01"])}, ledger, repo)
        check("not_done", any(c == "depends.not_done" for c, _, _ in infos), str(infos))

        prose = dict(good, id="R2-08", acceptance=["代码整洁优雅"])
        bl, wl, _ = validate({"R2-08": prose}, ledger, repo)
        check("acceptance.prose", any(c == "acceptance.prose" for c, _, _ in wl), str(wl))

        plan = build_plan(ledger, {"R2-01": good})
        before, after = serialize(ledger), serialize(plan)
        check("additive", not additive_violations(ledger, plan),
              str(additive_violations(ledger, plan)))
        _, removed = diff_text(before, after)
        added = set(after.splitlines())
        churn = [ln for ln in removed if ln[1:].rstrip() + "," not in added]
        check("additive_text", not churn, str(churn[:3]))
        check("format", after.endswith("}\n") and "\n    " in after, "缩进/尾换行")

        md = '```json\n"R2-09": {"stage":"R2","title":"t","scope":["src/A.cpp"],' \
             '"acceptance":["退出码 0"],"note":"n"}\n```\n'
        parsed = parse_manifest_text(md, [])
        check("parse_fence", list(parsed) == ["R2-09"], str(parsed))

    if failures:
        print("✗ selftest 失败 %d 项：" % len(failures))
        for item in failures:
            print("  - %s" % item)
        return 2
    print("✓ selftest 通过：%d 项断言语义正确" % total[0])
    return 0


# ---------------------------------------------------------------- 主流程

def main() -> int:
    parser = argparse.ArgumentParser(description="建卡请求 dry-run 应用器（只读）")
    parser.add_argument("manifest", nargs="?", help="建卡请求清单（.md 或 .json）")
    parser.add_argument("--ledger", default=str(REPO_ROOT / "docs" / "board-ledger.json"))
    parser.add_argument("--repo", default=str(REPO_ROOT))
    parser.add_argument("--diff", action="store_true", help="打印待写入的 unified diff")
    parser.add_argument("--json", action="store_true", help="机器可读输出")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--apply", action="store_true", help="（v1 不支持）写入台账")
    args = parser.parse_args()

    if args.apply:
        sys.stderr.write(
            "✗ --apply 在 v1 中被拒绝：写台账是维护者授权动作。\n"
            "  本脚本只负责「如果批准会写成什么」。落库请手工按 --diff 应用，\n"
            "  或先立卡把 apply 通道并入 task_gate.py 的授权模型后再启用。\n")
        return 5

    if args.selftest:
        return selftest()

    if not args.manifest:
        parser.error("需要一份清单（或 --selftest）")

    try:
        ledger_path = Path(args.ledger).resolve()
        before = io.open(ledger_path, encoding="utf-8").read()
        ledger = load_ledger(ledger_path)
        cards, notes = load_manifest(Path(args.manifest).resolve())
    except (OSError, ValueError) as exc:
        sys.stderr.write("✗ 读取失败：%s\n" % exc)
        return 4

    pending = [cid for cid in cards if cid in ledger["cards"]]
    if len(pending) == len(cards):
        print("无需变更：清单里 %d 张卡台账全部已有。" % len(cards))
        return 3

    repo = Path(args.repo).resolve()
    blockers, warnings, infos = validate(cards, ledger, repo)
    plan = build_plan(ledger, cards)
    after = serialize(plan)
    diff_lines, removed = diff_text(before, after)
    violations = additive_violations(ledger, plan)
    if violations:
        blockers.append(("plan.not_additive", "-",
                         "；".join(violations[:3]) + ("（等 %d 处）" % len(violations)
                                                     if len(violations) > 3 else "")))
    if json.loads(after) == ledger:
        print("无需变更：应用后台账无任何差异。")
        return 3

    if args.json:
        print(json.dumps({
            "manifest": str(args.manifest),
            "new_cards": sorted(cards),
            "baseline_additions": {cid: "todo" for cid in cards},
            "ready": [c for c in cards if ready_state(c, cards[c], ledger) == "就绪"],
            "blockers": [{"code": c, "card": k, "message": m} for c, k, m in blockers],
            "warnings": [{"code": c, "card": k, "message": m} for c, k, m in warnings],
            "dependency_status": [{"code": c, "card": k, "message": m} for c, k, m in infos],
            "ignored": notes,
            "additive_only": not violations,
        }, ensure_ascii=False, indent=2))
        return 2 if blockers else 0

    if notes:
        print("解析提示：")
        for note in notes:
            print("  · %s" % note)
        print("")
    render(Path(args.manifest), cards, ledger, blockers, warnings, infos,
           diff_lines, args.diff)
    return 2 if blockers else 0


if __name__ == "__main__":
    sys.exit(main())
