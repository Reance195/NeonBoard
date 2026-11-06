#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""register_board_cards.py —— 立卡唯一合法入口（DX-97）。

背景（DX-74/DX-96 竞态实测）：此前立卡靠人 / agent 手工改 docs/board-ledger.json，
卡号 = 「扫描时点的 max+1」，无预占机制；commit 落到 origin/main 之前号都不算数，
多会话并发立卡必撞号（DX-74 一个号被两个会话先后使用）。本工具把立卡收口为程序化：

  1. 发号：在跨 worktree 文件锁（git 通用目录）保护下扫台账取 max+1，锁内重读防竞态；
  2. 回执：为每张新卡生成 docs/card-requests/registered/<短号>.json，内含卡片规范
     JSON 的 sha256 —— task_gate 在 commit-msg 阶段核对，无回执 / 哈希不符的新增卡
     一律拒绝，手工改台账的立卡通道就此关闭；
  3. 凭证：授权来源必须经 --authorized-by 显式传入并写入回执，杜绝口头免责；
  4. 一致性：写盘后立即 derive --write 刷快照并 --check 校验，立卡提交自带绿灯。

用法：
  python scripts/register_board_cards.py --form docs/card-requests/20261106-dx-97.md \
      --authorized-by "维护者 13706 会话 2026-11-06" [--dry-run] [--allow-new-stage]

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
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

LEDGER_REL = "docs/board-ledger.json"
RECEIPT_DIR_REL = "docs/card-requests/registered"
REQUIRED_META = ("title", "stage", "scope", "acceptance", "evidence", "author")
FORM_SECTIONS = ("目标", "验收", "边界", "输入", "产出")
LOCK_STALE_SECONDS = 600


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
        "title": meta["title"],
    }


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


def ledger_view(ledger):
    return ledger.get("cards") or {}, ledger.get("baseline") or {}


def dump_receipt(receipt):
    return json.dumps(receipt, ensure_ascii=False, indent=2) + "\n"


def main(argv=None):
    _out()
    ap = argparse.ArgumentParser(
        description="立卡唯一合法入口：校验请求单、跨 worktree 发号、写台账与回执、刷快照")
    ap.add_argument("--form", required=True,
                    help="请求单路径 docs/card-requests/<YYYYMMDD>-<slug>.md")
    ap.add_argument("--authorized-by", required=True,
                    help="授权来源（维护者会话/指令），写入回执")
    ap.add_argument("--dry-run", action="store_true",
                    help="只推演：打印卡号与卡片/回执内容，不写任何文件")
    ap.add_argument("--allow-new-stage", action="store_true",
                    help="stage 是陌生取值时放行（check_board_diff 会要求全量验收）")
    args = ap.parse_args(argv)

    root = repo_root()
    form_path = Path(args.form)
    if not form_path.is_absolute():
        form_path = root / form_path
    if not form_path.is_file():
        die("找不到请求单：%s" % form_path)
    meta = parse_form(form_path.read_text(encoding="utf-8"), form_path)
    stage = meta["stage"]

    ledger = json.loads((root / LEDGER_REL).read_text(encoding="utf-8"))
    cards, baseline = ledger_view(ledger)
    known_stages = {c.get("stage") for c in cards.values() if c.get("stage")}
    if stage not in known_stages and not args.allow_new_stage:
        die("stage '%s' 是陌生取值（现有：%s）。确认 roadmap 有对应行后加 --allow-new-stage。"
            % (stage, "、".join(sorted(x for x in known_stages if x)) or "<空>"))

    if args.dry_run:
        code = next_code(set(cards) | set(baseline), stage)
        card = build_card(meta, code)
        print("[立卡程序] dry-run：将发号 %s" % code)
        print(json.dumps(card, ensure_ascii=False, indent=2))
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
        # 锁内重读台账再发号：同机多个 worktree 并发立卡时，谁持锁谁说了算
        ledger = json.loads(ledger_path.read_text(encoding="utf-8"))
        cards, baseline = ledger_view(ledger)
        code = next_code(set(cards) | set(baseline), stage)
        if code in cards or code in baseline:
            die("卡号 %s 已存在（发号在锁内进行，不应出现）" % code)
        suggested = (meta.get("suggestedId") or "").strip()
        if suggested and suggested != code:
            print("[立卡程序] 提示：请求单 suggestedId=%s，按发号规则实际取 %s"
                  % (suggested, code))

        card = build_card(meta, code)
        digest = canonical_hash(card)
        receipt = {
            "card": code,
            "sha256": digest,
            "authorizedBy": args.authorized_by.strip(),
            "form": rel_to(root, form_path),
            "registeredAt": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "tool": "scripts/register_board_cards.py",
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
        % (slug, rel_to(root, form_path), code, LEDGER_REL,
           RECEIPT_DIR_REL, code, code, args.authorized_by.strip(), code,
           slug, slug)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
