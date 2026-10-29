#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把存量任务卡归入项目（R10-04）。

背景：R4-01 的 agent.task 事件桥不区分来源项目，别的项目的卡会直接落进同一张
task 表。实测本机 board.db 的 115 张卡里有 13 张属于另外两个项目
（抢号系统 6 张、启动窗改造 7 张），看板上分不出来。

这个脚本按可复核的标题关键词做归类，**默认只干跑**：打印对照表、不写库。
加 --apply 才落库，落库前自动备份 board.db，且可重复执行（幂等）。

用法：
    python scripts/classify-cards.py                     # 干跑，打印对照表
    python scripts/classify-cards.py --apply             # 备份后写库
    python scripts/classify-cards.py --db path/to/board.db
    python scripts/classify-cards.py --apply --db ...
"""

import argparse
import hashlib
import json
import os
import shutil
import sqlite3
import sys
import time
import uuid
from pathlib import Path

# 归类规则：按顺序匹配标题，先命中先得。
# 关键词都取自两个项目的真实卡面措辞，不是猜的 —— 改规则时请同步改这里的注释。
RULES = [
    (
        "抢号系统",
        ["抢号", "号源", "QPS", "Webhook", "定时任务", "限速", "抖动", "总号源", "明细"],
    ),
    (
        "启动窗改造",
        [
            "SplashHandover",
            "splash",
            "启动窗",
            "startup-window",
            "bun test",
            "cargo check",
            "Rust:",
            "TS:",
            "Agent Note",
            "功能矩阵",
        ],
    ),
]

FALLBACK_PROJECT = "NeonBoard"

# 标题精确覆盖：关键词判不出来的个别卡，在这里点名归类。
# 只收「标题本身看不出项目、但上下文明确属于某个项目」的卡 —— 加一条就要写清依据，
# 否则这里会变成第二个拍脑袋的地方。
OVERRIDES = {
    # 抢号系统的错误日志卡，标题里没有「抢号 / 号源」这类字眼，
    # 但它与「定时任务运行日志」「Webhook 抢到通知」是同一批投递的。
    "错误分类并写入日志": "抢号系统",
}

PROJECT_COLORS = {
    "NeonBoard": "#59d6ff",
    "抢号系统": "#ff7ad9",
    "启动窗改造": "#ffb454",
}

PROJECT_DDL = """
CREATE TABLE IF NOT EXISTS project (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  color TEXT NOT NULL DEFAULT '',
  order_key INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL DEFAULT 0,
  updated_at INTEGER NOT NULL DEFAULT 0
)
"""

ASSIGNMENT_DDL = """
CREATE TABLE IF NOT EXISTS task_project (
  task_id TEXT PRIMARY KEY,
  project_id TEXT NOT NULL,
  created_at INTEGER NOT NULL DEFAULT 0
)
"""


def default_db_path() -> Path:
    """与 AppPaths::dataDir() 的落点保持一致：统一取 scripts/nb_paths.py 的口径。"""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from nb_paths import board_db_path
    return board_db_path()


def classify(title: str):
    """返回 (项目名, 命中的依据)。"""
    for exact, project in OVERRIDES.items():
        if title.strip() == exact:
            return project, "精确覆盖"
    for project, keywords in RULES:
        for keyword in keywords:
            if keyword in title:
                return project, keyword
    return FALLBACK_PROJECT, ""


def md5_of(path: Path) -> str:
    digest = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_project(conn: sqlite3.Connection, name: str) -> str:
    """按名字取项目 id，没有就建一个（幂等）。"""
    row = conn.execute("SELECT id FROM project WHERE name = ? LIMIT 1", (name,)).fetchone()
    if row:
        return row[0]

    now = int(time.time())
    project_id = str(uuid.uuid4())
    order_key = (
        conn.execute("SELECT COALESCE(MAX(order_key), 0) + 1024 FROM project").fetchone()[0]
    )
    conn.execute(
        "INSERT INTO project (id, name, color, order_key, created_at, updated_at)"
        " VALUES (?, ?, ?, ?, ?, ?)",
        (project_id, name, PROJECT_COLORS.get(name, ""), order_key, now, now),
    )
    return project_id


def main() -> int:
    parser = argparse.ArgumentParser(description="把存量任务卡归入项目（干跑优先）")
    parser.add_argument("--db", help="board.db 路径；默认取 NEONBOARD_DATA_DIR 或 AppData")
    parser.add_argument("--apply", action="store_true", help="真正写库（默认只干跑）")
    parser.add_argument("--quiet", action="store_true", help="只打印统计，不打印逐行对照表")
    args = parser.parse_args()

    db_path = Path(args.db) if args.db else default_db_path()
    if not db_path.is_file():
        print(f"[错误] 找不到数据库：{db_path}", file=sys.stderr)
        return 2

    before_md5 = md5_of(db_path)
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    try:
        tasks = conn.execute(
            "SELECT id, title, status FROM task ORDER BY created_at"
        ).fetchall()
        if not tasks:
            print("[提示] task 表里没有卡片，无事可做。")
            return 0

        plans = []
        for task in tasks:
            project, hit = classify(task["title"] or "")
            plans.append((task["id"], task["title"] or "", task["status"], project, hit))

        counts = {}
        for _, _, _, project, _ in plans:
            counts[project] = counts.get(project, 0) + 1

        if not args.quiet:
            print(f"{'#':>3}  {'状态':<5} {'归属项目':<12} {'依据':<14} 标题")
            print("-" * 110)
            for index, (_, title, status, project, hit) in enumerate(plans, 1):
                print(
                    f"{index:>3}  {status:<5} {project:<12} {(hit or '—'):<14} {title[:52]}"
                )
            print("-" * 110)

        print(f"共 {len(plans)} 张卡，归类结果：")
        for project in sorted(counts, key=lambda p: (-counts[p], p)):
            print(f"  {project:<12} {counts[project]:>3} 张")

        if not args.apply:
            after_md5 = md5_of(db_path)
            print()
            print("干跑：未写库。" + ("库文件未变。" if before_md5 == after_md5 else "[警告] 库文件被改动！"))
            print("确认无误后加 --apply 落库（会自动备份）。")
            return 0

        backup = db_path.with_name(f"{db_path.name}.bak-{time.strftime('%Y%m%d-%H%M%S')}")
        shutil.copy2(db_path, backup)
        print()
        print(f"已备份：{backup}")

        # 裸库（应用还没升级过、project 表还不存在）先补表再落库。
        # 干跑分支不执行这段 —— 干跑必须零写入，否则 md5 断言就没意义了。
        conn.execute(PROJECT_DDL)
        conn.execute(ASSIGNMENT_DDL)

        project_ids = {}
        for project in counts:
            project_ids[project] = ensure_project(conn, project)

        now = int(time.time())
        # 一卡一项目：task_id 是主键，REPLACE 覆盖，重复执行不会堆行。
        conn.executemany(
            "INSERT OR REPLACE INTO task_project (task_id, project_id, created_at)"
            " VALUES (?, ?, ?)",
            [(task_id, project_ids[project], now) for task_id, _, _, project, _ in plans],
        )
        conn.commit()

        print("落库完成：")
        for project, project_id in project_ids.items():
            actual = conn.execute(
                "SELECT COUNT(*) FROM task_project tp JOIN task t ON t.id = tp.task_id"
                " WHERE tp.project_id = ?",
                (project_id,),
            ).fetchone()[0]
            print(f"  {project:<12} {actual:>3} 张  (id={project_id[:8]}…)")

        unassigned = conn.execute(
            "SELECT COUNT(*) FROM task t LEFT JOIN task_project tp ON tp.task_id = t.id"
            " WHERE tp.task_id IS NULL"
        ).fetchone()[0]
        print(f"  未归属       {unassigned:>3} 张")
        return 0
    finally:
        conn.close()


if __name__ == "__main__":
    sys.exit(main())
