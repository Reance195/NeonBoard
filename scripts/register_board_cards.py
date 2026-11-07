#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""register_board_cards.py —— 立卡唯一合法入口（DX-98）。

背景（DX-74/DX-96 竞态实测）：此前立卡靠人 / agent 手工改 docs/board-ledger.json，
卡号 = 「扫描时点的 max+1」，无预占机制；commit 落到 origin/main 之前号都不算数，
多会话并发立卡必撞号（DX-74 一个号被两个会话先后使用）。本工具把立卡收口为程序化：

  1. 发号：在跨 worktree 文件锁（git 通用目录）保护下扫台账取 max+1，锁内重读防竞态；
     **再加服务端占位**（DX-99）：候选号先推 `refs/registrations/<短号>` 到 origin ——
     新建 ref 的推送在服务端是原子的 test-and-set，谁先推成谁占号，被拒即刷新占用集
     换号重试。跨机并发窗口就此缩到一次原子推送（与领用占位 refs/claims 同机制）；
  2. 回执：为每张新卡生成 docs/card-requests/registered/<短号>.json，内含卡片规范
     JSON 的 sha256 —— task_gate 在 commit-msg 阶段核对，无回执 / 哈希不符的新增卡
     一律拒绝，手工改台账的立卡通道就此关闭；
  3. 凭证：授权来源必须经 --authorized-by 显式传入并写入回执，杜绝口头免责；
  4. 一致性：写盘后立即 derive --write 刷快照并 --check 校验，立卡提交自带绿灯；
  5. 陌生 stage 的路线图行（DX-110）：stage 首次出现时，程序自己把
     `docs/iteration-roadmap.md` §9 的里程碑行写进工作区并列入立卡提交指引，
     不再让立卡会话手工补行 —— 锚点区 pre-commit 禁 main 提交，手工补的那行
     永远入不了库，会把锚点快进（git-done 的 Sync-AnchorWorktree）永久卡住。

用法：
  python scripts/register_board_cards.py --form docs/card-requests/<YYYYMMDD>-<slug>.md \
      --authorized-by "维护者 <会话> <日期>" [--dry-run] [--allow-new-stage]

提交拆分（commit-msg 会硬校验，两段缺一不可）：
  commit 1: docs(board): 收录 <短号> 立卡请求单     —— 仅表单 .md
  commit 2: chore(board): 立卡 <短号>               —— 台账 + 快照 + 回执
            body 必须含「授权：」与「批次：」两行
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
import board_priority as bp  # noqa: E402  排位规则唯一实现（DX-123），档位枚举与校验不在此复制（Z5）

LEDGER_REL = "docs/board-ledger.json"
ROADMAP_REL = "docs/iteration-roadmap.md"
RECEIPT_DIR_REL = "docs/card-requests/registered"
REQUIRED_META = ("title", "stage", "scope", "acceptance", "evidence", "author", "tier")
FORM_SECTIONS = ("目标", "验收", "边界", "输入", "产出")
LOCK_STALE_SECONDS = 600
REGISTER_RETRY = 5
REGISTRATION_REF_PREFIX = "refs/registrations/"


def _out():
    # DX-46：GBK 控制台打中文会抛 UnicodeEncodeError，输出一律转 UTF-8
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")


def repo_root() -> Path:
    # 脚本定位仓库一律按脚本自身路径，不按 CWD（与 git-done 同口径）
    return Path(__file__).resolve().parents[1]


def die(message, code=1):
    print("[立卡程序] %s" % message, file=sys.stderr)
    sys.exit(code)


def run(cmd, cwd):
    return subprocess.run(
        cmd, cwd=str(cwd), capture_output=True,
        text=True, encoding="utf-8", errors="replace",
    )


def split_list(value):
    return [x.strip() for x in (value or "").split(";") if x.strip()]


# DX-100：验收项里声明要写「回归用例 / 新增用例 / 补用例 / 新增脚本」却引用
# scope 外的 scripts/*.py|ps1，意味着本卡要改 scope 外文件 —— 立卡即拒，
# 把矛盾暴露在登记时而不是验收时（DX-107 曾以 scope 外的 verify.ps1 演练承诺收场）。
CROSSCHECK_MARKERS = ("回归用例", "新增用例", "补用例", "新增脚本")
# 纯验证命令只跑不改（selftest / -ScriptOnly / --smoke / --check），引用 scope 外
# 脚本是合法的「借工具验收」，不拦。
READONLY_VERIFY_MARKERS = ("selftest", "-ScriptOnly", "--smoke", "--check")
SCRIPT_REF_RE = re.compile(r"scripts/[\w\-./]+\.(?:py|ps1)")


def acceptance_scope_conflicts(meta):
    """返回 [(验收项序号, 越界脚本路径)]：声明产出用例/脚本却引用 scope 外脚本。"""
    scope = set(meta.get("scope") or [])
    conflicts = []
    for i, item in enumerate(meta.get("acceptance") or [], 1):
        if not any(m in item for m in CROSSCHECK_MARKERS):
            continue
        if any(m in item for m in READONLY_VERIFY_MARKERS):
            continue
        for path in SCRIPT_REF_RE.findall(item):
            if path not in scope:
                conflicts.append((i, path))
    return conflicts


def parse_form(text, form_path):
    """解析五段式请求单：--- frontmatter --- + ## 目标/验收/边界/输入/产出。"""
    lines = text.replace("\r\n", "\n").split("\n")
    if not lines or lines[0].strip() != "---":
        die("表单 %s 缺少 frontmatter（首行必须是 ---）" % form_path)
    try:
        end = next(i for i in range(1, len(lines)) if lines[i].strip() == "---")
    except StopIteration:
        die("表单 %s 的 frontmatter 没有闭合 ---" % form_path)

    meta = {}
    for line in lines[1:end]:
        if not line.strip() or ":" not in line:
            continue
        key, value = line.split(":", 1)
        meta[key.strip()] = value.strip()

    body = "\n".join(lines[end + 1:])
    missing_sections = [s for s in FORM_SECTIONS if ("## %s" % s) not in body]
    if missing_sections:
        die("表单 %s 缺少章节：%s（五段式缺一不可）"
            % (form_path, "、".join(missing_sections)))
    missing_meta = [k for k in REQUIRED_META if not meta.get(k)]
    if missing_meta:
        die("表单 %s frontmatter 缺字段：%s"
            % (form_path, "、".join(missing_meta)))
    try:
        bp.validate_tier(meta["tier"])
    except ValueError as e:
        die("表单 %s 的 tier 非法：%s" % (form_path, e))
    return meta


def next_code(existing, stage):
    """发号规则与既有台账一致：<stage>-<max+1>，在锁内调用。"""
    prefix = "%s-" % stage
    nums = []
    for code in existing:
        if code.startswith(prefix):
            tail = code[len(prefix):]
            if tail.isdigit():
                nums.append(int(tail))
    return "%s-%d" % (stage, (max(nums) + 1) if nums else 1)


def resolve_mode(cli_value, ledger_value):
    """发号模式解析：CLI --mode 单次覆盖 > 台账 numbering.mode > 默认 remote。

    remote = 服务端占位（跨机安全）；local = 仅本地锁+台账（单机/离线项目，
    零网络动作，跨机并发不受保护）。返回 None 表示配置非法。
    """
    mode = (cli_value or ledger_value or "remote").lower()
    return mode if mode in ("remote", "local") else None


def build_card(meta, code):
    return {
        "acceptance": split_list(meta["acceptance"]),
        "author": meta["author"],
        "depends": split_list(meta.get("depends", "")),
        "evidence": meta["evidence"],
        "id": code,
        "note": "",
        "scope": split_list(meta["scope"]),
        "stage": meta["stage"],
        "tier": meta["tier"],
        "title": meta["title"],
    }


def _roadmap_cell(value, limit=120):
    """表格单元格：压成单行、去掉会破列的竖线、限长。"""
    text = " ".join(str(value).split())
    return text.replace("|", "／")[:limit]


def roadmap_stage_rows(text):
    """解析 §9 里程碑表，返回 [(行下标, stage)]。

    与 src/board/ProjectMap.cpp 的 readRoadmap 同口径：只认「## 9」段内、第二格
    整格加粗（| **R0** | … |）的行，表头 / 分隔行 / 说明行一律跳过。
    """
    rows = []
    in_section = False
    for i, raw in enumerate(text.replace("\r\n", "\n").split("\n")):
        line = raw.strip()
        if line.startswith("## "):
            in_section = line.startswith("## 9")
            continue
        if not in_section or not line.startswith("|"):
            continue
        cells = line.split("|")
        if len(cells) < 6:
            continue
        stage_cell = cells[1].strip()
        if not (stage_cell.startswith("**") and stage_cell.endswith("**")):
            continue
        stage = stage_cell.strip("*").strip().upper()
        if stage:
            rows.append((i, stage))
    return rows


def roadmap_has_stage(text, stage):
    want = stage.strip().upper()
    return any(s == want for _, s in roadmap_stage_rows(text))


def build_roadmap_row(meta, code):
    """为新 stage 生成一行 §9 里程碑（列数/加粗形态须过 readRoadmap）。"""
    stage = _roadmap_cell(meta["stage"], 16).replace("*", "")
    title = _roadmap_cell(meta["title"], 90)
    topic = title.split("：", 1)[0][:24] or stage
    raw = meta.get("acceptance", "")
    items = raw if isinstance(raw, list) else split_list(raw)
    exit_std = _roadmap_cell(items[0], 120) if items else "见卡片验收项"
    return "| **%s** | %s | %s | %s | **待办**：%s 已立卡，待实现 |" % (
        stage, topic, title, exit_std, code)


def insert_roadmap_row(text, row, stage):
    """把行插到 §9 表格最后一行之后；该 stage 已有行则原样返回（幂等）。

    返回 (新文本, 是否插入)。找不到表格返回 (原文本, False)，由调用方报错 ——
    宁可 fail-closed，也不往猜错的位置写。行尾沿用原文件（本仓库为 LF）。
    """
    if roadmap_has_stage(text, stage):
        return text, False
    eol = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines()
    rows = roadmap_stage_rows(text)
    if not rows:
        return text, False
    lines.insert(rows[-1][0] + 1, row)
    tail = eol if text.endswith(("\n", "\r")) else ""
    return eol.join(lines) + tail, True


def canonical_hash(card):
    """卡片规范 JSON 的 sha256 —— 回执与 task_gate 双方同一口径，防手工篡改。"""
    payload = json.dumps(card, ensure_ascii=False, sort_keys=True,
                         separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def acquire_lock(common_dir):
    """跨 worktree 文件锁：锁文件放 git 通用目录，本机所有检出共享一份。"""
    path = common_dir / "board-register.lock"
    for attempt in range(2):
        try:
            fd = os.open(str(path), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            os.write(fd, ("%d %s" % (
                os.getpid(),
                datetime.now(timezone.utc).isoformat())).encode("utf-8"))
            os.close(fd)
            return path
        except FileExistsError:
            age = time.time() - path.stat().st_mtime
            if attempt == 0 and age > LOCK_STALE_SECONDS:
                path.unlink()
                continue
            die("另一个立卡进程持有锁：%s（约 %d 秒前创建）。\n"
                "  该锁跨本机所有 worktree 生效；确认无进程在立卡可手工删除后重试。"
                % (path, int(age)), 3)
    die("无法获取立卡锁：%s" % path, 3)


def rel_to(root, path):
    try:
        return path.resolve().relative_to(root).as_posix()
    except ValueError:
        return str(path)


def parse_registrations(text):
    """从 ls-remote 输出解析已占用的注册号（纯函数，selftest 可测）。"""
    codes = set()
    for line in (text or "").splitlines():
        line = line.strip()
        if not line or "\t" not in line:
            continue
        ref = line.split("\t", 1)[1]
        if ref.startswith(REGISTRATION_REF_PREFIX):
            codes.add(ref[len(REGISTRATION_REF_PREFIX):])
    return codes


def ls_registrations(root):
    """远端注册占位全集。不可达时 fail-closed（DX-99）：跨机安全的前提是
    占用集必须包含 origin 的实时事实，拿不到就不发号。"""
    out = run(["git", "ls-remote", "origin",
               "%s*" % REGISTRATION_REF_PREFIX], root)
    if out.returncode != 0:
        die("无法访问 origin 的注册占位（ls-remote 失败）：%s\n"
            "  跨机并发下没有远端事实就不能发号；确要离线立卡请加 --offline\n"
            "  （号为暂定，推送落地前必须重验）。"
            % (out.stderr.strip() or out.stdout.strip()), 5)
    return parse_registrations(out.stdout)


def ledger_view(ledger):
    return ledger.get("cards") or {}, ledger.get("baseline") or {}


def dump_receipt(receipt):
    return json.dumps(receipt, ensure_ascii=False, indent=2) + "\n"


def selftest():
    """纯函数断言（DX-99）：占位集解析、发号避开已占号、换号收敛。"""
    checks = []

    def check(name, ok):
        checks.append((name, ok))

    # parse_registrations：标准输出 / 噪声行 / 空输入
    sample = ("abc123\trefs/heads/main\n"
              "def456\trefs/registrations/DX-98\n"
              "789aaa\trefs/claims/ZZ-9\n"
              "bbbccc\trefs/registrations/R5-01\n")
    regs = parse_registrations(sample)
    check("占位集解析：识别 refs/registrations/*", regs == {"DX-98", "R5-01"})
    check("占位集解析：不混入 heads/claims",
          "main" not in regs and "ZZ-9" not in regs)
    check("占位集解析：空输入得空集", parse_registrations("") == set())

    # next_code：占用集含本地台账与远端占位的并集时仍取 max+1 且避开全部已占号
    occupied = {"DX-1", "DX-2", "DX-98", "R5-1"}
    check("发号：避开全部已占号", next_code(occupied, "DX") == "DX-99")
    check("发号：首次取 1", next_code(set(), "DX") == "DX-1")

    # 换号收敛：模拟占位被拒后占用集更新，下一次发号必须跳过
    occupied = {"DX-98"}
    first = next_code(occupied, "DX")
    occupied |= {first}  # 别的机器占走了 first
    second = next_code(occupied, "DX")
    check("换号重试：被拒后跳过被占号", first == "DX-99" and second == "DX-100")

    # resolve_mode：优先级与非法值（DX-105）
    check("模式解析：CLI 覆盖台账", resolve_mode("local", "remote") == "local")
    check("模式解析：台账缺省回退 remote", resolve_mode(None, None) == "remote")
    check("模式解析：非法值返回 None", resolve_mode(None, "yolo") is None)

    # 陌生 stage 的 roadmap 行（DX-110）：只认 §9 表里第二格整格加粗的行
    sample = ("# 路线图\n"
              "## 8. 明确不做\n"
              "| **R8** | 假行 | 不该被算进 §9 | - | - |\n"
              "## 9. 里程碑\n"
              "| 版本 | 主题 | 内容 | 出口标准 | 进度 |\n"
              "| --- | --- | --- | --- | --- |\n"
              "| **R0** | 炫酷地基 | 剖析 | 热启动 | 完成 |\n"
              "| **DX** | 开发体验 | 面板 | 打开即知 | 完成 |\n"
              "\n### 9.1 R6 派发器\n")
    stages = [s for _, s in roadmap_stage_rows(sample)]
    check("roadmap 解析：只取 §9 段、跳过表头", stages == ["R0", "DX"])
    check("roadmap 解析：已有 stage 幂等", roadmap_has_stage(sample, "dx"))
    check("roadmap 解析：陌生 stage 未命中", not roadmap_has_stage(sample, "R19"))
    meta = {"stage": "R19", "title": "执行链：复盘", "acceptance": ["验收一|含竖线;验收二"]}
    row = build_roadmap_row(meta, "R19-1")
    check("roadmap 行：第二格整格加粗且列数正确",
          row.split("|")[1].strip() == "**R19**" and len(row.split("|")) == 7)
    check("roadmap 行：表格竖线被转义", "|" not in row.split("|")[3])
    new_text, inserted = insert_roadmap_row(sample, row, "R19")
    check("roadmap 插入：陌生 stage 加在表格末行之后", inserted and
          [s for _, s in roadmap_stage_rows(new_text)] == ["R0", "DX", "R19"])
    same, again = insert_roadmap_row(new_text, row, "R19")
    check("roadmap 插入：重复调用不重复加行", not again and same == new_text)
    broken, no_rows = insert_roadmap_row("## 9. 里程碑\n没有表格\n", row, "R19")
    check("roadmap 插入：找不到表格 fail-closed", not no_rows and broken.startswith("## 9"))
    # tier 排位档位（DX-123）：声明必填、枚举受控、进卡片定义受回执哈希覆盖
    tier_meta = {"acceptance": "a", "author": "t", "depends": "", "evidence": "e",
                 "scope": "scripts/x.py", "stage": "DX", "title": "t"}
    check("档位校验：合法值通过", bp.validate_tier("P2") == 2)
    try:
        bp.validate_tier("P9")
        check("档位校验：非法枚举抛错", False)
    except ValueError:
        check("档位校验：非法枚举抛错", True)
    check("build_card 写入 tier", build_card(dict(tier_meta, tier="P2"), "DX-999")["tier"] == "P2")
    hash_with = canonical_hash(build_card(dict(tier_meta, tier="P2"), "DX-999"))
    check("改 tier 后 canonical_hash 变化（回执审计覆盖）",
          hash_with != canonical_hash(build_card(dict(tier_meta, tier="P3"), "DX-999")))

    # 验收项 × scope 交叉判据（DX-100）：声明产出用例/脚本却引用 scope 外脚本 → 拒；
    # 路径在 scope 内、纯验证命令、无脚本引用 → 均不拦（零误伤）。
    def conflicts_for(item, scope):
        return acceptance_scope_conflicts({"acceptance": [item], "scope": scope})

    check("交叉判据：回归用例引用 scope 外脚本被拒",
          conflicts_for("回归用例：造一次并发冲突重放验证不再静默半落地，"
                        "见 scripts/task_gate.py", ["docs/pitfalls.md"])
          == [(1, "scripts/task_gate.py")])
    check("交叉判据：脚本在 scope 内不拦",
          conflicts_for("回归用例：造一次冲突重放，见 scripts/task_gate.py",
                        ["scripts/task_gate.py"]) == [])
    check("交叉判据：纯验证命令（selftest）不拦",
          conflicts_for("python scripts/board_priority.py --selftest 全过", []) == [])
    check("交叉判据：纯验证命令（-ScriptOnly）不拦",
          conflicts_for("pwsh -File scripts/verify.ps1 -ScriptOnly 全绿", []) == [])
    check("交叉判据：普通验收项无脚本引用不拦",
          conflicts_for("新增用例：界面上肉眼核对进度环", []) == [])
    check("交叉判据：无触发词的脚本引用不拦（只管声明产出的项）",
          conflicts_for("跑 scripts/board-view.py 确认渲染", []) == [])

    failures = [n for n, ok in checks if not ok]
    for name, ok in checks:
        print("  %s %s" % ("✓" if ok else "✗", name))
    if failures:
        print("[立卡程序] 自测失败 %d/%d 例" % (len(failures), len(checks)),
              file=sys.stderr)
        return 1
    print("[立卡程序] 自测通过：%d 例" % len(checks))
    return 0


def main(argv=None):
    _out()
    ap = argparse.ArgumentParser(
        description="立卡唯一合法入口：校验请求单、跨 worktree 发号、写台账与回执、刷快照")
    ap.add_argument("--form", help="请求单路径 docs/card-requests/<YYYYMMDD>-<slug>.md")
    ap.add_argument("--authorized-by",
                    help="授权来源（维护者会话/指令），写入回执")
    ap.add_argument("--dry-run", action="store_true",
                    help="只推演：打印卡号与卡片/回执内容，不写任何文件")
    ap.add_argument("--allow-new-stage", action="store_true",
                    help="stage 是陌生取值时放行（check_board_diff 会要求全量验收）")
    ap.add_argument("--offline", action="store_true",
                    help="跳过服务端占位与远端占用集（离线应急）：号为暂定，推送落地前必须重验")
    ap.add_argument("--mode", choices=("remote", "local"),
                    help="发号模式：remote=服务端占位（默认）/ local=仅本地锁+台账"
                         "（单机/离线项目）。省略则读台账 numbering.mode，再缺省 remote")
    ap.add_argument("--selftest", action="store_true",
                    help="跑内置纯函数断言（占位集解析 / 发号换号），不依赖仓库状态")
    ap.add_argument("--audit-acceptance-scope", action="store_true",
                    help="DX-100：对台账全部在办卡回放「验收项引用 scope 外脚本」判据，"
                         "命中只打印告警（棘轮），恒退 0")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if args.audit_acceptance_scope:
        root = repo_root()
        ledger = json.loads((root / LEDGER_REL).read_text(encoding="utf-8"))
        cards, baseline = ledger_view(ledger)
        hits = 0
        for cid in sorted(cards):
            if baseline.get(cid) == "done":
                continue
            for i, path in acceptance_scope_conflicts(cards[cid]):
                hits += 1
                print("[立卡程序] ⚠ 存量命中 %s：验收项 %d 引用 scope 外脚本 %s"
                      "（只告警不硬拦，修表单或登记豁免）" % (cid, i, path))
        print("[立卡程序] 在办卡回放完成：命中 %d 处" % hits)
        return 0
    if not args.form:
        ap.error("必须给 --form <请求单>（或 --selftest）")
    if not args.authorized_by:
        ap.error("必须给 --authorized-by <授权来源>（立卡必须留授权凭证）")

    root = repo_root()
    form_path = Path(args.form)
    if not form_path.is_absolute():
        form_path = root / form_path
    if not form_path.is_file():
        die("找不到请求单：%s" % form_path)
    meta = parse_form(form_path.read_text(encoding="utf-8"), form_path)
    stage = meta["stage"]

    conflicts = acceptance_scope_conflicts(meta)
    if conflicts:
        detail = "\n".join("  验收项 %d 引用 %s（scope 未含）" % (i, p)
                           for i, p in conflicts)
        die("验收项声明了用例/脚本产出，却引用 scope 外的脚本（DX-100）——"
            "验收时会因越界被闸门拒，立卡阶段先拦：\n%s\n"
            "  把该路径加进 scope，或把验收项改写成只读验证"
            "（selftest / -ScriptOnly / --smoke / --check）。" % detail, 2)

    ledger = json.loads((root / LEDGER_REL).read_text(encoding="utf-8"))
    cards, baseline = ledger_view(ledger)
    mode = resolve_mode(args.mode, (ledger.get("numbering") or {}).get("mode"))
    if mode is None:
        die("numbering.mode 取值非法（只接受 remote|local），请修正台账或 --mode", 2)
    known_stages = {c.get("stage") for c in cards.values() if c.get("stage")}
    if stage not in known_stages and not args.allow_new_stage:
        die("stage '%s' 是陌生取值（现有：%s）。程序会自动把 §9 里程碑行并入立卡提交，"
            "确认后加 --allow-new-stage。"
            % (stage, "、".join(sorted(x for x in known_stages if x)) or "<空>"))

    if args.dry_run:
        code = next_code(set(cards) | set(baseline), stage)
        card = build_card(meta, code)
        print("[立卡程序] dry-run（mode=%s）：将发号 %s" % (mode, code))
        print(json.dumps(card, ensure_ascii=False, indent=2))
        if stage not in known_stages:
            print("[立卡程序] dry-run：stage '%s' 首次出现，将把下列行写入 %s 并列入立卡提交："
                  % (stage, ROADMAP_REL))
            print(build_roadmap_row(meta, code))
        return 0

    if not args.authorized_by.strip():
        die("--authorized-by 不能为空（立卡必须留授权来源）")

    out = run(["git", "rev-parse", "--git-common-dir"], root)
    if out.returncode != 0:
        die("无法定位 git 通用目录：%s" % out.stderr.strip())
    cd = out.stdout.strip()
    common = Path(cd) if os.path.isabs(cd) else (root / cd).resolve()

    ledger_path = root / LEDGER_REL
    lock_path = acquire_lock(common)
    try:
        # 锁内重读台账再发号：同机多个 worktree 并发立卡时，谁持锁谁说了算。
        # 跨机并发靠服务端占位（DX-99）：候选号推 refs/registrations/<短号>，
        # 新建 ref 的推送在服务端是原子的 test-and-set —— 推成即占号，被拒即
        # 号刚被别的机器抢走，刷新占用集换号重试。
        ledger = json.loads(ledger_path.read_text(encoding="utf-8"))
        cards, baseline = ledger_view(ledger)
        local_occupied = set(cards) | set(baseline)
        reservation = None
        if mode == "local" or args.offline:
            if mode == "local" and not args.offline:
                print("[立卡程序] 本地发号模式（numbering.mode=local）：零网络动作，"
                      "号为暂定 —— 跨机并发不受保护，推共享远端前必须对台账重验")
            else:
                print("[立卡程序] ⚠ --offline：跳过服务端占位，卡号为暂定，"
                      "推送落地前必须对 origin 重验")
            code = next_code(local_occupied, stage)
        else:
            occupied = local_occupied | ls_registrations(root)
            for attempt in range(1, REGISTER_RETRY + 1):
                code = next_code(occupied, stage)
                # 占位 sha 必须是**唯一的 off-main 空提交**（commit-tree 直造，不走钩子）：
                # ref 尖端不在任何主线历史里，任何人的 HEAD 都不可能是它的后代 ——
                # 异 sha 重推必因 non-fast-forward 被拒，plain push 即严格的
                # 「仅创建」test-and-set，不依赖 force-with-lease 语义。
                # （v1 指向 HEAD 是洞：同主线的后代提交可快进顶掉占位 —— DX-104。）
                base_tree = run(["git", "rev-parse", "HEAD^{tree}"],
                                root).stdout.strip()
                stamp = datetime.now(timezone.utc).isoformat(timespec="microseconds")
                res_msg = ("立卡占位 %s\n\nform: %s\nauthorizedBy: %s\nat: %s\n"
                           % (code, rel_to(root, form_path),
                              args.authorized_by.strip(), stamp))
                res_sha = run(["git", "commit-tree", base_tree, "-p", "HEAD",
                               "-m", res_msg], root).stdout.strip()
                if not res_sha:
                    die("无法创建占位提交（commit-tree 失败）", 5)
                push = run(["git", "push", "origin",
                            "%s:%s%s" % (res_sha, REGISTRATION_REF_PREFIX, code)],
                           root)
                if push.returncode == 0:
                    reservation = {"ref": REGISTRATION_REF_PREFIX + code,
                                   "sha": res_sha}
                    print("[立卡程序] 服务端占位成功：%s（%s）"
                          % (reservation["ref"], res_sha[:7]))
                    break
                err = (push.stderr or push.stdout or "").strip()
                if "[rejected]" in err:
                    # 仅创建语义下，[rejected] 只可能是「ref 已存在」（异 sha 必非快进）
                    print("[立卡程序] %s 占位被拒（号被并发机器抢走），换号重试 %d/%d"
                          % (code, attempt, REGISTER_RETRY))
                    occupied = local_occupied | ls_registrations(root)
                    continue
                # 撞号以外的失败（网络 / 钩子）fail-fast 并照实报错，
                # 不再吞掉真实原因误报成撞号（DX-104 第二处修正）
                die("占位推送失败：%s\n%s" % (code, err or "<无输出>"), 5)
            if reservation is None:
                die("连续 %d 次占位被拒：跨机竞争异常或远端异常，请稍后重试"
                    % REGISTER_RETRY, 5)
        if code in cards or code in baseline:
            die("卡号 %s 已存在（发号在锁内进行，不应出现）" % code)
        suggested = (meta.get("suggestedId") or "").strip()
        if suggested and suggested != code:
            print("[立卡程序] 提示：请求单 suggestedId=%s，按发号规则实际取 %s"
                  % (suggested, code))

        card = build_card(meta, code)

        # DX-110：陌生 stage 的里程碑行由程序自己落盘，随立卡提交入库。先算插入结果，
        # 找不到 §9 表格就 die —— 此时台账 / 回执都还没写盘，不留半截现场。
        # 锁内重算 known_stages：并发立卡时别的机器可能刚把这个 stage 立起来。
        roadmap_path = root / ROADMAP_REL
        roadmap_text = roadmap_path.read_text(encoding="utf-8")
        roadmap_new = roadmap_text
        roadmap_added = False
        locked_stages = {c.get("stage") for c in cards.values() if c.get("stage")}
        if stage not in locked_stages:
            row = build_roadmap_row(meta, code)
            roadmap_new, roadmap_added = insert_roadmap_row(roadmap_text, row, stage)
            if not roadmap_added:
                die("无法在 %s 的 §9 里程碑表插入 stage '%s' 的行；"
                    "请检查表格是否仍是 '| **阶段** | ... |' 形态"
                    % (ROADMAP_REL, stage))

        digest = canonical_hash(card)
        receipt = {
            "card": code,
            "sha256": digest,
            "authorizedBy": args.authorized_by.strip(),
            "form": rel_to(root, form_path),
            "registeredAt": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "tool": "scripts/register_board_cards.py",
            "mode": mode,
            "reservation": reservation,
        }

        cards[code] = card
        baseline[code] = "todo"
        ledger["cards"] = cards
        ledger["baseline"] = baseline
        ledger_path.write_text(
            json.dumps(ledger, ensure_ascii=False, indent=4) + "\n",
            encoding="utf-8")

        receipt_dir = root / RECEIPT_DIR_REL
        receipt_dir.mkdir(parents=True, exist_ok=True)
        (receipt_dir / ("%s.json" % code)).write_text(
            dump_receipt(receipt), encoding="utf-8")
        if roadmap_added:
            # newline=""：不让 Windows 把 LF 换成 CRLF（.gitattributes 固定 eol=lf）。
            with open(roadmap_path, "w", encoding="utf-8", newline="") as fh:
                fh.write(roadmap_new)
            print("[立卡程序] 陌生 stage '%s'：已把 §9 里程碑行写入 %s（随立卡提交入库）"
                  % (stage, ROADMAP_REL))
    finally:
        try:
            lock_path.unlink()
        except FileNotFoundError:
            pass

    print("[立卡程序] 已登记 %s：%s" % (code, card["title"]))
    print("[立卡程序] 回执：%s/%s.json（sha256=%s…）"
          % (RECEIPT_DIR_REL, code, digest[:12]))

    for step in (["--write"], ["--check"]):
        d = run([sys.executable, "scripts/derive_board_status.py"] + step, root)
        if d.stdout.strip():
            print(d.stdout.strip())
        if d.returncode != 0:
            if d.stderr.strip():
                print(d.stderr.strip(), file=sys.stderr)
            die("derive %s 失败（台账/回执已写盘）：git checkout -- %s progress.json 还原后重试"
                % (" ".join(step), LEDGER_REL), 4)

    slug = code.lower()
    form_rel = rel_to(root, form_path)
    # 陌生 stage 的 roadmap 行并入**第一段** docs(board) 提交：docs/ 在任务闸门里是
    # 杂务豁免路径。若塞进第二段 chore(board)，DX-51 的「改任务定义的提交不得
    # 夹带流程路径之外的文件」会直接拒（连杂务白名单路径也算夹带）。
    first_add = form_rel + ((" " + ROADMAP_REL) if roadmap_added else "")
    print(
        "\n下一步（commit-msg 会硬校验，两段提交缺一不可）：\n"
        "  git switch -c chore/board-%s\n"
        "  git add %s\n"
        "  git commit -m \"docs(board): 收录 %s 立卡请求单\"\n"
        "  git add %s progress.json %s/%s.json\n"
        "  git commit -m \"chore(board): 立卡 %s\" \\\n"
        "           -m \"授权：%s\" -m \"批次：%s\"\n"
        "  git switch main && git merge --no-ff chore/board-%s \\\n"
        "           -m \"merge: 合入 chore/board-%s\" && git push origin main\n"
        % (slug, first_add, code, LEDGER_REL,
           RECEIPT_DIR_REL, code, code, args.authorized_by.strip(), code,
           slug, slug)
    )
    if roadmap_added:
        print("  · stage '%s' 的 §9 里程碑行已并入第一段 docs(board) 提交（docs 路径豁免）——"
              "不要再手工往锚点工作区补行，那样会把 git-done 的锚点快进永久卡死。" % stage)
    return 0


if __name__ == "__main__":
    sys.exit(main())
