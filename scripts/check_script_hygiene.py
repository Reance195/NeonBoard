#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""脚本卫生检查：宪章里「唯一入口 / 唯一豁免」两条款的机器校验。

为什么要有这个文件：宪章写了「数据目录走 NEONBOARD_DATA_DIR」「--no-verify 只给紧急情况」，
但都是散文条款，靠人记就会漂 —— 本次 CI 常红（回写提交被自家 pre-commit 拒）和
verify.ps1 读不到沙箱日志，根因都是「规则在多处各写一遍」。条款要成立，得有闸门。

规则（只扫脚本与流水线，不扫 .githooks/ —— 钩子文案里是在*介绍*逃生口，不是在使用）：
  A. 应用数据目录字面量（com.lin）只允许出现在 scripts/nb-paths.ps1（PowerShell 侧）
     与 scripts/nb_paths.py（Python 侧单一实现，claim-card.py / classify-cards.py 等
     一律 import 它）；其余脚本一律取函数/读环境变量。
  B. `git commit --no-verify` 的豁免面**当前为空**：历史上只给 .github/workflows/ci.yml 的
     「自动回写看板状态」开过唯一豁免，但该步骤已随回写职责整体取消而删除（R14-01，写权
     收归 git-done，board.yml 只校验）。豁免理由随实现一起消失，白名单必须同步清空 ——
     留着指向一个已不含该命令的文件，等于「豁免面与实现脱钩」，下次谁照着加回来都不会有人拦。
  C. 每个 .ps1 必须以 UTF-8 BOM 开头（AGENTS.md §4）：Windows PowerShell 5.1 会把
     无 BOM 的 UTF-8 当 GBK 读，中文注释撑破字符串、整份脚本报「批处理式语法错误」。
     编辑 .ps1 的工具常静默剥掉 BOM —— 靠人记得补一定漂（本会话就连续丢了两次），交给闸门。
  D. `.github/workflows/*.yml` 一个都不许出现 `--no-verify`。与 B 分开写不是多余：B 管的是
     「谁被允许绕过」，D 管的是「流水线里根本不该有绕过」。拆成两条，是逼以后想开口子的人
     至少动两次手（改白名单 + 删这条断言），每次都得重新想一遍为什么要开。
  E. git 单值取值只允许出现在单一实现 scripts/git-helpers.ps1（DX-63 立闸，DX-74 扩面）：
     E1 裸 `[string](& git ...)` 强转捕获（DX-63）；E2 「& git ... | Select-Object -First 1」
     手工首行捕获（DX-74）；E3 wrapper 值捕获「(...).Output | Select-Object -First 1」
     （DX-74）。同型空值崩溃多次（DX-49 两处 / DX-50 两处 / DX-59 一处）后，取值收口为
     Get-NbGitLine / Get-NbSymRef / Get-NbToplevel / Get-NbCommonDir / Get-NbBranch，
     其余脚本点源使用。git 无输出时拿到空值继续跑、错误被吞，是各次崩溃的共同起点。
  F. 控制台 UTF-8 编码设置只活一份（scripts/nb-console.ps1，DX-77/DX-83）：四个捕获
     python/git 输出的入口脚本（build / git-done / agent-init / verify）必须点源并调用
     Set-NbUtf8Console。CP 936 控制台下漏接的入口必现 mojibake（DX-77 实测 verify.ps1
     「閫€鍑虹爜」/ nbflow status 授权段；DX-83 实测 claim-card 输出捕获后乱码落地）。
  G. scripts/*.py 若 import 同目录本地模块（scripts/<名>.py），源码必须有显式 sys.path
     处理（DX-125）：python -I 隔离模式不含脚本目录，跨模块 import 必崩（DX-84 修了
     撞崩的 board-apply.py，同类三个原样留着，第二次撞上才立这条机器判据 —— Z6）。
     无本地 import 的脚本不受影响。
  H. scripts/ 顶层受控文件与 docs/scripts-manifest.md 双向对账（DX-151）：
     文件存在而清单无行 = 未登记的膨胀/临时残留（宪章 §四「临时脚本用完即删」的机器眼）；
     清单有行而文件不存在 = 孤儿条目。清单是 scripts/ 用途的唯一事实源（Z5），
     登记行格式「| `文件名` | 一句话用途 |」，豁免面只有文件级显式行，不开通配。

用法：python scripts/check_script_hygiene.py [仓库根目录]
      python scripts/check_script_hygiene.py --selftest   # 反向探针（不依赖真仓库）
退出码：0 通过 / 1 违规 / 2 找不到仓库
"""

import ast
import re
import sys
import tempfile
from pathlib import Path

DATA_DIR_TOKEN = "com.lin"
DATA_DIR_ALLOW = {"scripts/nb-paths.ps1", "scripts/nb_paths.py"}
NO_VERIFY_RE = re.compile(r"git\s+commit\s+--no-verify")
# 规则 B 的豁免面：R14-01（2026-11-03）之后**空集**。曾给 ci.yml 的「自动回写看板状态」
# 开过唯一豁免（给 CI 装钩子后那条提交被自家 pre-commit 拒），但该步骤已随回写职责整体
# 取消而删除 —— 写权归 git-done，board.yml 只校验。**别为了让检查变绿把它加回来**：
# 那是把「谁被允许绕过闸门」这条判断从宪章挪进脚本常量，等于自动放行。
NO_VERIFY_ALLOW = frozenset()
UTF8_BOM = b"\xef\xbb\xbf"
# 规则 E（DX-63 立闸，DX-74 扩面）：git 单值取值只许走单一实现 git-helpers.ps1。
GIT_CAPTURE_RE = re.compile(r"\[string\]\s*\(\s*&?\s*git\b")
# E2（DX-74）：手工首行 —— `& git ...` 与 `| Select-Object -First 1` 同一行；
# [^|]* 卡在第一根管道上，`| Where-Object ... | Select-Object -First 1` 这类集合过滤不误伤。
GIT_FIRSTLINE_RE = re.compile(r"&\s*git\b[^|]*\|\s*Select-Object\s+-First\s+1\b")
# E3（DX-74）：wrapper 值捕获 —— Invoke-Git(Quiet) 结果的 .Output 被掐首行当单值用
# （(Invoke-Git ...).Output 与 $var.Output 两种形态全盖）。全仓校准（DX-74）：
# .Output 只在 git wrapper 上出现，别处无同名属性，零误伤。
GIT_WRAPPER_CAPTURE_RE = re.compile(r"\.Output\s*\|\s*Select-Object\s+-First\s+1\b")
GIT_HELPER_ALLOW = {"scripts/git-helpers.ps1"}
# E2/E3 的存量豁免（棘轮，随卡清空）：两个文件的同类模式各有在办卡承诺收口——
#   scripts/agent-init.ps1 → DX-94（卡契约注入改读 origin/main 时一并迁移这 6 处）；
#   scripts/git-done.ps1   → DX-88（收尾推送段重写时一并迁移这 2 处）。
# 除此之外零豁免：新文件、新写法一律判 E；动这两个文件时必须顺手清掉对应条目与本注释。
# E1（强转捕获）不进豁免面：两文件当前 E1 零命中，保持最紧口径。
GIT_CAPTURE_PENDING_ALLOW = {
    "scripts/agent-init.ps1",
    "scripts/git-done.ps1",
}
# 本文件要靠这两个字面量才能发现违规，所以它不在自己的射程内。
SELF = "scripts/check_script_hygiene.py"
# 规则 F（DX-77/DX-83）：控制台 UTF-8 编码设置只许在 scripts/nb-console.ps1 活一份，
# 四个捕获 python/git 输出的入口必须点源并调用封装——漏接的入口在 CP 936 控制台必现 mojibake。
CONSOLE_DOTSOURCE_RE = re.compile(r"Join-Path\s+\S+\s+'nb-console\.ps1'")
CONSOLE_CALL_RE = re.compile(r"\bSet-NbUtf8Console\b")
CONSOLE_WRAPPER_REQUIRE = {
    "scripts/build.ps1",
    "scripts/git-done.ps1",
    "scripts/agent-init.ps1",
    "scripts/verify.ps1",
}
# 规则 G（DX-125）：显式 sys.path 处理的判据 —— 源码出现对 sys.path 的写操作即算
# （与 board-apply.py 的既有修法一致：SCRIPT_DIR + sys.path.insert）。
LOCAL_IMPORT_SYSPATH_RE = re.compile(r"sys\.path\.(insert|append)")

SCAN_GLOBS = ["scripts/*.ps1", "scripts/*.py", ".github/workflows/*.yml"]
# 注释行不算「使用」：# 开头（PS/Python）与 YAML 里的 # 开头
COMMENT_RE = re.compile(r"^\s*(#|//|\*|/\*)")


def rel(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


# 规则 H（DX-151）：scripts/ 顶层受控文件 ↔ docs/scripts-manifest.md 双向对账。
MANIFEST_REL = "docs/scripts-manifest.md"
MANIFEST_SUFFIXES = {".ps1", ".py", ".cmd", ".bat", ".sh"}
MANIFEST_ROW_RE = re.compile(r"^\|\s*`([^`]+)`\s*\|([^|]*)\|")


def find_manifest_problems(root: Path) -> tuple[list[str], int]:
    """返回 (问题清单, 对账覆盖文件数)。清单缺失本身就是违规（fail-closed）。"""
    scripts_dir = root / "scripts"
    if not scripts_dir.is_dir():
        return [], 0
    files = {p.name for p in scripts_dir.iterdir()
             if p.is_file() and p.suffix.lower() in MANIFEST_SUFFIXES
             and not p.name.startswith((".", "__"))}
    mpath = root / MANIFEST_REL
    if not mpath.is_file():
        return ["H 缺少脚本清单 %s：scripts/ 每个受控文件一行「| `文件名` | 用途 |」" % MANIFEST_REL,
                "   修复：按 scripts/ 现状补建清单（脚本头注释/docstring 摘录，不重写文档）"], 0
    entries = set()
    problems: list[str] = []
    for line in mpath.read_text(encoding="utf-8").splitlines():
        m = MANIFEST_ROW_RE.match(line.strip())
        if not m:
            continue
        name = m.group(1).strip()
        if not name.startswith("scripts/") and "/" not in name:
            name = "scripts/" + name
        entries.add(name)
        if not m.group(2).strip():
            problems.append("H 清单用途为空：%s" % name)
    for name in sorted(files):
        if "scripts/" + name not in entries:
            problems.append("H 脚本未登记清单：scripts/%s" % name)
            problems.append("   修复：在 %s 补一行用途；临时脚本应删除而非登记（AGENTS.md §四）" % MANIFEST_REL)
    for e in sorted(entries):
        base = e[len("scripts/"):] if e.startswith("scripts/") else e
        if base not in files:
            problems.append("H 清单条目无实体：%s（脚本已删除或改名，清单要同步）" % e)
    return problems, len(files)


def scan_files(root: Path):
    seen = set()
    for pattern in SCAN_GLOBS:
        for path in sorted(root.glob(pattern)):
            name = rel(root, path)
            if path.is_file() and name not in seen and name != SELF:
                seen.add(name)
                yield name, path


def find_problems(root: Path) -> tuple[list[str], dict]:
    """扫一遍仓库，返回 (问题清单, 命中统计)。main 与 --selftest 共用这一份扫描。"""
    problems: list[str] = []
    hits = {"data_dir": [], "no_verify": [], "bom_ok": 0, "git_capture": [], "console_ok": 0}
    console_use: dict[str, set[str]] = {}
    scanned: set[str] = set()
    for name, path in scan_files(root):
        scanned.add(name)
        # 规则 C：.ps1 缺 BOM 直接判失败（读原始字节 —— 文本解码会把 BOM 吃掉，看不出）
        if name.endswith(".ps1"):
            if path.read_bytes()[:3] != UTF8_BOM:
                problems.append("C .ps1 缺 UTF-8 BOM：%s" % name)
                problems.append("   修复：以带 BOM 的 UTF-8 重新保存（Windows PowerShell 5.1 按 GBK 读会崩）")
            else:
                hits["bom_ok"] += 1
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            if COMMENT_RE.match(line):
                continue
            if DATA_DIR_TOKEN in line.lower():
                hits["data_dir"].append((name, lineno, line.strip()))
                if name not in DATA_DIR_ALLOW:
                    problems.append("A 应用数据目录被硬编码：%s:%d → %s" % (name, lineno, line.strip()))
                    problems.append("   改为点源 scripts/nb-paths.ps1 取 Get-NbDataDir / Get-NbLogFile / Get-NbBoardDb")
            if NO_VERIFY_RE.search(line):
                hits["no_verify"].append((name, lineno, line.strip()))
                if name.startswith(".github/workflows/"):
                    # 规则 D：流水线自身不许绕过闸门
                    problems.append("D 流水线里出现钩子绕过：%s:%d → %s" % (name, lineno, line.strip()))
                    problems.append("   CI 是闸门的执行环境，不是豁免区：写权归 git-done 后没有绕过的理由"
                                    "（AGENTS.md §11.2 第 7 条）")
                elif name not in NO_VERIFY_ALLOW:
                    problems.append("B 越权的钩子豁免：%s:%d → %s" % (name, lineno, line.strip()))
                    problems.append("   当前零豁免（AGENTS.md §11.2 第 7 条）；确需开口子，先改宪章再改本文件的 NO_VERIFY_ALLOW")
            for ereg in (GIT_CAPTURE_RE, GIT_FIRSTLINE_RE, GIT_WRAPPER_CAPTURE_RE):
                if ereg.search(line):
                    hits["git_capture"].append((name, lineno, line.strip()))
                    if name not in GIT_HELPER_ALLOW and (
                        ereg is GIT_CAPTURE_RE or name not in GIT_CAPTURE_PENDING_ALLOW
                    ):
                        # 规则 E（DX-63 立闸，DX-74 扩面）：git 单值取值只许在单一实现里
                        problems.append("E git 取值绕过单一封装：%s:%d → %s" % (name, lineno, line.strip()))
                        problems.append("   改为点源 scripts/git-helpers.ps1，用 Get-NbGitLine / Get-NbSymRef / "
                                        "Get-NbToplevel / Get-NbCommonDir / Get-NbBranch")
                    break
            if name in CONSOLE_WRAPPER_REQUIRE:
                if CONSOLE_DOTSOURCE_RE.search(line):
                    console_use.setdefault(name, set()).add("dotsource")
                if CONSOLE_CALL_RE.search(line):
                    console_use.setdefault(name, set()).add("call")
    # 规则 F（DX-77/DX-83）：存在却没接封装的入口即拒；文件不存在不代管（删了的脚本无从谈起）
    for name in sorted(CONSOLE_WRAPPER_REQUIRE & scanned):
        got = console_use.get(name, set())
        missing = []
        if "dotsource" not in got:
            missing.append("点源 scripts/nb-console.ps1")
        if "call" not in got:
            missing.append("调用 Set-NbUtf8Console")
        if missing:
            problems.append("F 入口脚本缺 UTF-8 控制台封装：%s（缺 %s）" % (name, " + ".join(missing)))
            problems.append("   编码三跳（chcp 65001 + [Console]::Output/InputEncoding）的单一实现在 scripts/nb-console.ps1")
        else:
            hits["console_ok"] += 1
    # 规则 G（DX-125）：scripts/*.py import 同目录本地模块而源码无显式 sys.path 处理则拒。
    # 判定凭 AST 的 import 名单与本地模块名集合求交，不靠正则猜 import 行。
    local_mods = {p.stem for p in (root / "scripts").glob("*.py") if p.stem != "__init__"}
    for name, path in scan_files(root):
        if not name.endswith(".py"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        try:
            tree = ast.parse(text)
        except SyntaxError:
            continue  # 语法错误不是本规则的射程，交给其它检查
        imported = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imported.update(a.name.split(".")[0] for a in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module and node.level == 0:
                imported.add(node.module.split(".")[0])
        local = sorted(imported & local_mods)
        if local and "sys.path." not in text:
            problems.append("G 本地模块 import 缺显式 sys.path（python -I 即崩）：%s → import %s"
                            % (name, ", ".join(local)))
            problems.append("   按 board-apply.py 模式补：SCRIPT_DIR = Path(__file__).resolve().parent；"
                            "sys.path.insert(0, str(SCRIPT_DIR))")
        elif local:
            hits["local_import_ok"] = hits.get("local_import_ok", 0) + 1
    return problems, hits


def selftest() -> int:
    """反向探针（DX-63）：违规样本必须被抓、单一实现自身与干净样本必须放行。"""
    fails: list[str] = []

    def check(name: str, cond: bool) -> None:
        if not cond:
            fails.append(name)
        print(("  ok   " if cond else "  FAIL ") + name)

    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "scripts").mkdir()
        # 单一实现自身：含同样字面量（本文件靠它才能发现违规），必须放行
        helper = root / "scripts" / "git-helpers.ps1"
        helper.write_bytes(UTF8_BOM + "function F { ([string](& git rev-parse HEAD)).Trim() }\n".encode("utf-8"))
        # 违规样本：E1 强转 / E2 手工首行 / E3 wrapper 值捕获，三类都要被抓
        bad = root / "scripts" / "bad.ps1"
        bad_bytes = UTF8_BOM + (
            "$x = [string](& git symbolic-ref -q --short HEAD 2>$null)\n"
            "$d = (& git rev-parse --show-toplevel 2>$null | Select-Object -First 1)\n"
            "$h = (Invoke-Git rev-parse 'HEAD').Output | Select-Object -First 1\n"
            "# 非 git 的集合过滤不算：Select-String ... | Select-Object -First 1\n"
        ).encode("utf-8")
        bad.write_bytes(bad_bytes)
        problems, hits = find_problems(root)
        check("规则 E 抓到违规脚本", any(p.startswith("E ") and "bad.ps1" in p for p in problems))
        check("E1 强转捕获被抓", any("bad.ps1:1" in p for p in problems))
        check("E2 手工首行被抓", any("bad.ps1:2" in p for p in problems))
        check("E3 wrapper 值捕获被抓", any("bad.ps1:3" in p for p in problems))
        check("非 git 的 -First 1 不误伤", not any("bad.ps1:4" in p for p in problems))
        check("单一实现自身被放行", not any(p.startswith("E ") and "git-helpers.ps1" in p for p in problems))
        check("违规行被点名", any("bad.ps1:1" in p for p in problems))
        # 干净样本：走封装函数，全绿
        bad.write_bytes(UTF8_BOM + "$x = Get-NbSymRef\n".encode("utf-8"))
        problems, hits = find_problems(root)
        check("干净样本放行", not problems and hits["bom_ok"] == 2)
        # 存量豁免面（棘轮）：E2/E3 在豁免文件放行，E1 仍被抓
        pending = root / "scripts" / "agent-init.ps1"
        pending.write_bytes(UTF8_BOM + (
            "$d = (& git rev-parse --show-toplevel 2>$null | Select-Object -First 1)\n"
            "$x = [string](& git rev-parse HEAD)\n"
        ).encode("utf-8"))
        problems, hits = find_problems(root)
        check("豁免文件 E2 放行", not any(p.startswith("E ") and "agent-init.ps1:1" in p for p in problems))
        check("豁免文件 E1 仍被抓", any(p.startswith("E ") and "agent-init.ps1:2" in p for p in problems))

        # 规则 F（DX-77/DX-83）：存在的入口缺封装调用被抓；点源 + 调用齐备则放行。
        # 只有部分入口存在时，缺席的不代管（真实仓库四个入口都在，全量核对）。
        entry = root / "scripts" / "verify.ps1"
        entry.write_bytes(UTF8_BOM + "Write-Host 'no wrapper'\n".encode("utf-8"))
        problems, hits = find_problems(root)
        check("规则 F 抓到缺封装的入口", any(p.startswith("F ") and "verify.ps1" in p for p in problems))
        check("规则 F 点名缺什么", any("Set-NbUtf8Console" in p for p in problems))
        entry.write_bytes(UTF8_BOM + (
            ". (Join-Path $PSScriptRoot 'nb-console.ps1')\n"
            "Set-NbUtf8Console\n"
        ).encode("utf-8"))
        problems, hits = find_problems(root)
        check("接入封装的入口放行", not any(p.startswith("F ") and "verify.ps1" in p for p in problems))
        check("规则 F 计数正确", hits["console_ok"] == 1)

        # 规则 G（DX-125）：import 同目录本地模块而无显式 sys.path 处理被抓；
        # 有 sys.path 处理放行；无本地 import 的脚本不受影响。
        (root / "scripts" / "helper_mod.py").write_text("X = 1\n", encoding="utf-8")
        bad_py = root / "scripts" / "bad_g.py"
        bad_py.write_text("import helper_mod\nprint(helper_mod.X)\n", encoding="utf-8")
        plain_py = root / "scripts" / "plain_g.py"
        plain_py.write_text("import json\nprint(json.dumps({}))\n", encoding="utf-8")
        problems, hits = find_problems(root)
        check("规则 G 抓到缺 sys.path 的本地 import", any(p.startswith("G ") and "bad_g.py" in p for p in problems))
        check("规则 G 点名本地模块", any("helper_mod" in p for p in problems))
        check("规则 G 不误伤无本地 import 的脚本", not any("plain_g.py" in p for p in problems))
        bad_py.write_text(
            "from pathlib import Path\n"
            "import sys\n"
            "SCRIPT_DIR = Path(__file__).resolve().parent\n"
            "sys.path.insert(0, str(SCRIPT_DIR))\n"
            "import helper_mod\nprint(helper_mod.X)\n",
            encoding="utf-8",
        )
        problems, hits = find_problems(root)
        check("有显式 sys.path 的本地 import 放行", not any("bad_g.py" in p for p in problems))

        # 规则 H（DX-151）：scripts/ ↔ docs/scripts-manifest.md 双向对账。
        # 清单缺失 fail-closed；未登记、无实体、用途为空三类都要被抓；对齐则零违规。
        hp, hc = find_manifest_problems(root)
        check("规则 H 抓到清单缺失", any(p.startswith("H ") and MANIFEST_REL in p for p in hp))
        docs_dir = root / "docs"
        docs_dir.mkdir()
        (docs_dir / "scripts-manifest.md").write_text(
            "# 清单\n\n| 文件 | 用途 |\n| --- | --- |\n"
            "| `git-helpers.ps1` | git 取值单一实现 |\n"
            "| `bad.py` |  |\n"
            "| `gone.ps1` | 已删除的脚本 |\n",
            encoding="utf-8",
        )
        hp, hc = find_manifest_problems(root)
        check("规则 H 抓到未登记脚本", any("scripts/verify.ps1" in p for p in hp))
        check("规则 H 抓到无实体条目", any("scripts/gone.ps1" in p for p in hp))
        check("规则 H 抓到用途为空", any("bad.py" in p for p in hp))
        check("规则 H 计数为顶层受控文件数", hc == len([p for p in (root / "scripts").iterdir()
              if p.is_file() and p.suffix.lower() in MANIFEST_SUFFIXES]))
        names = sorted(p.name for p in (root / "scripts").iterdir()
                       if p.is_file() and p.suffix.lower() in MANIFEST_SUFFIXES)
        (docs_dir / "scripts-manifest.md").write_text(
            "# 清单\n\n| 文件 | 用途 |\n| --- | --- |\n"
            + "".join("| `%s` | 自测占位 |\n" % n for n in names),
            encoding="utf-8",
        )
        hp, _ = find_manifest_problems(root)
        check("规则 H 对齐后零违规", not hp)

    if fails:
        print("自测失败 %d 项：%s" % (len(fails), "；".join(fails)))
        return 1
    print("自测通过")
    return 0


def main() -> int:
    # Windows 下管道里 Python stdout 默认走 ANSI 代码页，中文提示会花屏（同 task_gate.py）
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass

    argv = [a for a in sys.argv[1:]]
    if "--selftest" in argv:
        argv.remove("--selftest")
        return selftest()

    root = Path(argv[0] if argv else Path(__file__).resolve().parents[1])
    if not (root / "scripts").is_dir():
        sys.stderr.write("找不到仓库根（scripts/ 不存在）：%s\n" % root)
        return 2

    problems, hits = find_problems(root)
    man_problems, man_count = find_manifest_problems(root)
    problems += man_problems
    hits["manifest_count"] = man_count
    if problems:
        for p in problems:
            sys.stderr.write(p + "\n")
        return 1

    dd = ", ".join("%s:%d" % (n, l) for n, l, _ in hits["data_dir"]) or "无"
    nv = ", ".join("%s:%d" % (n, l) for n, l, _ in hits["no_verify"]) or "零命中"
    gc = ", ".join("%s:%d" % (n, l) for n, l, _ in hits["git_capture"]) or "零命中"
    wf = sum(1 for n, _, _ in hits["no_verify"] if n.startswith(".github/workflows/"))
    print("[脚本卫生] 通过：数据目录口径 %s；--no-verify %s（流水线 %d 处）；裸 git 取值捕获 %s"
          "（仅限 git-helpers.ps1 与在办卡豁免面）；带 BOM 的 .ps1 共 %d 个；UTF-8 控制台封装 %d/%d；"
          "清单对账 %d 个脚本"
          % (dd, nv, wf, gc, hits["bom_ok"], hits["console_ok"], len(CONSOLE_WRAPPER_REQUIRE),
             hits["manifest_count"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
