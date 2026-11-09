#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""README 同步提示器（DX-150）：宪章 §五 硬性约定 6 的机器执行者。

判据（Z15：读的是 `git log base..HEAD` 本身，不读任何投影）：
  - 范围内提交改动 `qml/` 或 `shaders/` 路径 = 界面行为改动候选；
  - 整个范围内 `README.md` 无任何改动 → 逐个点名未同步提交 + `::warning::` 摘要；
  - 体验类规范按 Z4 走 fail-soft + 必须告警：本脚本退出码恒 0，定位就是
    「让漏同步每次被点名」，不做阻断闸门（升级阻断需另行立卡）。

用法：
  python scripts/check_readme_sync.py --base <ref>   # CI/本地增量检查
  python scripts/check_readme_sync.py --selftest     # 临时仓库三类场景自测
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

UI_PREFIXES = ("qml/", "shaders/")
README = "README.md"


def git(repo, *args):
    res = subprocess.run(["git", "-C", repo] + list(args),
                         capture_output=True, check=False)
    if res.returncode != 0:
        raise RuntimeError("git %s 失败：%s" % (
            " ".join(args), res.stderr.decode("utf-8", "replace").strip()))
    return res.stdout.decode("utf-8", "replace")


def find_unsynced(repo, base):
    """返回 (全部提交, 未同步提交列表 [(短号, 主题, [界面路径...])], 范围内 README 是否已改)。"""
    out = git(repo, "log", "--format=%H\x01%s", "--name-only", "%s..HEAD" % base)
    commits = []
    readme_touched = False
    cur = None
    for line in out.splitlines():
        if "\x01" in line:
            sha, subj = line.split("\x01", 1)
            cur = (sha, subj, [])
            commits.append(cur)
        elif line.strip() and cur is not None:
            path = line.strip()
            if path == README:
                readme_touched = True
            elif path.startswith(UI_PREFIXES):
                cur[2].append(path)
    offenders = [(c[0][:7], c[1], c[2]) for c in commits if c[2]]
    return (commits, offenders, readme_touched)


def report(repo, base):
    try:
        _, offenders, readme_touched = find_unsynced(repo, base)
    except RuntimeError as e:
        print("::warning::README 同步检查无法执行（不阻断）：%s" % e)
        return 0
    if not offenders or readme_touched:
        return 0
    print("")
    print("界面路径（qml/ 或 shaders/）有改动，但范围内 %s 未同步更新（宪章 §五 硬性约定 6）：" % README)
    for short, subj, paths in offenders:
        print("  %s  %s" % (short, subj))
        for p in paths:
            print("        · " + p)
    print("")
    print("::warning::README 同步提示：%d 个提交改动界面路径而 %s 未同步（警告级，不阻断）"
          % (len(offenders), README))
    return 0


def selftest():
    root = tempfile.mkdtemp(prefix="nb-readme-sync-")
    repo = os.path.join(root, "repo")
    os.makedirs(repo)
    def g(*args):
        return git(repo, *args)
    g("init", "-b", "main")
    g("config", "user.name", "selftest")
    g("config", "user.email", "selftest@example.invalid")
    g("config", "core.autocrlf", "false")

    def write(rel, text):
        full = os.path.join(repo, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
        git(repo, "add", "--", rel)
        git(repo, "commit", "-m", "t:" + rel)

    write("README.md", "seed\n")
    write("qml/Main.qml", "seed\n")
    base = g("rev-parse", "HEAD").strip()
    fails = []

    # 场景 1：界面改动 + README 同提交 → 静默
    write("qml/Main.qml", "v2\n")
    write("README.md", "v2\n")
    _, offenders, touched = find_unsynced(repo, base)
    if offenders and not touched:
        fails.append("场景1 README 已同步应静默：offenders=%r touched=%s" % (offenders, touched))
    if report(repo, base) != 0:
        fails.append("场景1 report 应静默通过")
    base = g("rev-parse", "HEAD").strip()

    # 场景 2：纯界面改动（shaders）无 README → 点名
    write("shaders/neon.glsl", "v3\n")
    _, offenders, touched = find_unsynced(repo, base)
    if len(offenders) != 1 or touched:
        fails.append("场景2 应点名 1 提交：offenders=%r touched=%s" % (offenders, touched))
    elif not offenders[0][2][0].startswith("shaders/"):
        fails.append("场景2 路径点名错误：%r" % (offenders,))
    base = g("rev-parse", "HEAD").strip()

    # 场景 3：非界面改动 → 静默
    write("src/main.cpp", "v4\n")
    _, offenders, touched = find_unsynced(repo, base)
    if offenders:
        fails.append("场景3 应静默：offenders=%r" % (offenders,))

    # 场景 4：report 走一遍不抛错、退出码恒 0
    if report(repo, base) != 0:
        fails.append("report 退出码必须恒 0")
    print("[README 同步自测] %s" % ("通过" if not fails else "失败"))
    for f in fails:
        print("  ✗ " + f)
    return 0 if not fails else 1


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser(description="README 同步提示器（警告级）")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--base", help="增量范围基线 ref")
    g.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    try:
        root = git(".", "rev-parse", "--show-toplevel").strip()
    except RuntimeError as e:
        print("::warning::README 同步检查无法执行（不阻断）：%s" % e)
        return 0
    return report(root.replace("\\", "/"), args.base)


if __name__ == "__main__":
    sys.exit(main())
