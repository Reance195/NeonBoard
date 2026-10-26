<#
    为某个 agent 创建独立工作区

    产出：
      <repo>/.worktrees/<agent>/          —— 独立 worktree（独立分支、独立 build/dist）
      <repo>/.worktrees/<agent>/.sandbox/  —— 沙箱数据目录（数据库/插件/日志/hooks.db）
      <repo>/.worktrees/<agent>/.agent-env.ps1 —— 可 dot-source 的沙箱环境变量脚本

    关键点：基线用 origin/main 而不是 main —— main 分支已被根目录 checkout，
    Git 不允许同一分支在两个 worktree 同时检出，用远端引用建分支才能完全绕开根目录。

    用法：
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch fix/stagebar-chip-overflow -Card R3-01
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch feature/xxx -Base origin/main -Card M19
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch chore/xxx -Card none
        # -Card 必填：领用期间该卡在台账 claims 里占位，看板卡自动转「进行中」。
        # 大任务（R#-#）可随时领；小任务（M##）仅当其父任务尚未被任何人认领时才可单独领。
        # 分支头脱离主干的期间内该卡为 doing（派生器按 head 位置判定），
        # 分支合并或丢弃后登记自动失效，收尾用提交标记 #<code>=done。
        # 确实不需要领用任何卡片时显式写 -Card none —— 强制表态，避免漏填导致双领。
        # 硬门：领用时会向服务端推 refs/claims/<code> 占位（非强推），
        # 同名 ref 已存在即被拒绝；领子卡前先 ls-remote 父卡占位。需联网。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Agent,
    [string]$Branch = '',
    [string]$Base = 'origin/main',
    [Parameter(Mandatory = $true)][string]$Card = '',
    [switch]$NoFetch
)

$ErrorActionPreference = 'Stop'

# 仓库根按**脚本自身路径**定位：新工作区永远开在「脚本所属仓库」下，
# 避免在别的目录里误开 worktree
$scriptRepoRoot = $null
if ($PSScriptRoot) {
    $candidate = Split-Path -Parent $PSScriptRoot
    if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate '.git'))) {
        $scriptRepoRoot = (Resolve-Path -LiteralPath $candidate).Path
    }
}
if ($scriptRepoRoot) {
    $repoRoot = $scriptRepoRoot
} else {
    $repoRoot = (& git rev-parse --show-toplevel 2>$null | Select-Object -First 1)
    if (-not $repoRoot) { throw '当前目录不是 Git 仓库' }
    $repoRoot = $repoRoot.Trim()
}

if (-not $Branch) { $Branch = "feature/$Agent" }

# -Card none：显式声明「本次不领用任何卡片」。
# 之所以强制表态，是因为漏填时脚本无法区分「忘了」和「不需要」，
# 多代理并行时漏填就会出现两个人同时改同一张卡却都不占位。
if ($Card -and $Card.Trim() -ieq 'none') {
    Write-Host '· 本次未领用任何卡片（-Card none）' -ForegroundColor Yellow
    $Card = ''
}

$worktree = Join-Path (Join-Path $repoRoot '.worktrees') $Agent

if (Test-Path -LiteralPath $worktree) {
    throw "工作区已存在：$worktree（如需重建请先 git worktree remove）"
}

if (-not $NoFetch) {
    Write-Host '· 同步远端引用 ...' -ForegroundColor DarkGray
    & git -C $repoRoot fetch origin --prune
    if ($LASTEXITCODE -ne 0) { throw "git fetch 失败：$LASTEXITCODE" }
}

Write-Host ("· 创建工作区 {0}" -f $worktree) -ForegroundColor Cyan
Write-Host ("  分支 {0}  基线 {1}" -f $Branch, $Base) -ForegroundColor DarkGray
& git -C $repoRoot worktree add $worktree -b $Branch $Base
if ($LASTEXITCODE -ne 0) { throw "git worktree add 失败：$LASTEXITCODE" }

$sandbox = Join-Path $worktree '.sandbox'
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null

$envFile = Join-Path $worktree '.agent-env.ps1'
$envLines = @(
    "# 由 scripts/agent-init.ps1 生成：多代理沙箱环境变量",
    "# 用法： . .\.agent-env.ps1   （PowerShell 7 推荐 pwsh）",
    "`$env:NEONBOARD_DATA_DIR = '$sandbox'",
    "`$env:NEONBOARD_INSTANCE = '$Agent'",
    "`$env:NEONBOARD_ROADMAP = '$(Join-Path $sandbox 'progress.json')'",
    "`$env:NEONBOARD_ROADMAP_AUTOWRITE = '0'",
    "`$env:NEONBOARD_AGENT_ID = '$Agent'"
)

# ---------- 任务卡字段注入（docs/task-card.md）----------
# 让 agent 一进工作区就知道：这张卡要验收什么、能改哪些路径、依赖哪张卡。
if ($Card -and $Card -ne 'none') {
    $ledgerPath = Join-Path $repoRoot 'docs\board-ledger.json'
    if (Test-Path $ledgerPath) {
        $ledger = Get-Content $ledgerPath -Raw | ConvertFrom-Json
        $cardCode = $Card.TrimStart('#').ToUpperInvariant()
        $cardData = $ledger.cards.$cardCode
        if ($cardData) {
            $envLines += "`$env:NB_CARD = '$cardCode'"
            if ($cardData.scope)      { $envLines += ("`$env:NB_SCOPE = '" + (($cardData.scope) -join ';') + "'") }
            if ($cardData.acceptance) { $envLines += ("`$env:NB_ACCEPTANCE = '" + (($cardData.acceptance) -join ';') + "'") }
            if ($cardData.depends)    { $envLines += ("`$env:NB_DEPENDS = '" + (($cardData.depends) -join ';') + "'") }
        }
    }
}
$envContent = ($envLines -join "`n")
[System.IO.File]::WriteAllText($envFile, $envContent, (New-Object System.Text.UTF8Encoding($false)))

# ---------- 领用登记（-Card <短号>） ----------
# 领用规则：大任务（R#-#）直接领；小任务（M##）必须父任务未被任何人认领，
# 父任务映射取自 board.db 的 task_link（child_id -> parent_id）与台账 cards[].id。
# 流程：
#   1) 空提交 `chore(claim): 领取 #<code> 实施任务` —— 信息里的 #<code> 会触发
#      post-commit 钩子投递（无 = 默认 doing），同时给出分支第一个提交的 head
#   2) python 校验短号存在 + 防双领（遍历 origin/main 与各远端分支的台账 claims），
#      把 claims[<code>] = {agent, branch, head, claimedAt} 写进**本 worktree** 的台账副本
#   3) 第二个提交把登记落进分支；合并回 main 后 CI 派生即可识别 doing
#   4) 显式 --emit-hook-file 投一次 #<code>=doing 兜底（不阻塞、幂等）
if ($Card) {
    $code = $Card.TrimStart('#').ToUpperInvariant()
    if ($code -notmatch '^(R\d+-\d+|M\d+)$') {
        throw "领用失败：-Card 需为路线图短号（如 R3-01 / M19），收到：$Card"
    }

    # 子卡领用需要读本地看板的父子边；数据目录优先沙箱变量
    $boardDataDir = if ($env:NEONBOARD_DATA_DIR) { $env:NEONBOARD_DATA_DIR } else { Join-Path $env:APPDATA 'com.lin\NeonBoard' }
    $boardDb = Join-Path $boardDataDir 'board.db'

    Write-Host ("· 领用 {0}：登记 ↔ 看板卡转「进行中」" -f $code) -ForegroundColor Cyan
    Push-Location $worktree
    try {
        # 1) 空提交：给分支一个 head，也让 post-commit 钩子顺手投递一次
        & git commit --allow-empty -m ("chore(claim): 领取 #{0} 实施任务" -f $code) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "领用空提交失败：$LASTEXITCODE" }
        $headSha = (& git rev-parse HEAD | Select-Object -First 1)

        # 2) 校验 + 防双领 + 写本 worktree 台账副本
        $claimPy = Join-Path $env:TEMP ("neonboard-claim-{0}.py" -f $Agent)
        $pySource = @'
import json, os, re, sqlite3, subprocess, sys, time

root = os.path.abspath(sys.argv[1])
code, agent, branch, board_db = sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
ledger = os.path.join(root, "docs", "board-ledger.json")
tmp = ledger + ".tmp"

data = json.load(open(ledger, encoding="utf-8"))
cards = data.get("cards") or {}
if code not in cards:
    sys.stderr.write("[领用] 台账未登记短号：%s\n" % code)
    sys.exit(2)

# 已完成的卡不允许重新领用：done 表示验收项已逐条满足并合并，再领用只能说明
# 这次改动没有对应任务，属于「先写代码后找卡」。真要继续做请新开卡。
baseline = data.get("baseline") or {}
if baseline.get(code) == "done":
    sys.stderr.write("[领用] 卡片 %s 已完成（baseline=done），不允许重新领用\n" % code)
    sys.stderr.write("        如需继续改动，请在台账新开一张卡再领用。\n")
    sys.exit(6)

# 小任务（M##）只有在父任务未被任何人认领时才允许单独领用
parent_code = None
if not re.match(r"^R\d+-\d+$", code):
    child_id = (cards.get(code) or {}).get("id", "")
    if not child_id:
        sys.stderr.write("[领用] 子卡 %s 缺少卡片 id，无法确定父任务，拒绝领用\n" % code)
        sys.exit(4)
    if not os.path.exists(board_db):
        sys.stderr.write("[领用] 无法确定子卡的父任务：board.db 不存在（%s）\n" % board_db)
        sys.exit(4)
    try:
        con = sqlite3.connect("file:%s?mode=ro" % board_db.replace("\\", "/"), uri=True)
        row = con.execute("SELECT parent_id FROM task_link WHERE child_id = ?", (child_id,)).fetchone()
        con.close()
    except sqlite3.Error as exc:
        sys.stderr.write("[领用] 读取看板父子关系失败：%s\n" % exc)
        sys.exit(4)
    if not row:
        sys.stderr.write("[领用] 子卡 %s 未关联父任务，禁止单独领用\n" % code)
        sys.exit(4)
    by_id = {v.get("id", ""): k for k, v in cards.items()}
    parent_code = by_id.get(row[0], "")
    if not parent_code:
        sys.stderr.write("[领用] 子卡 %s 的父任务未在台账登记，禁止单独领用\n" % code)
        sys.exit(4)

# 服务端硬门：refs/claims/<短号> 唯一占位（非强推）。本地台账/远端分支扫描都可能漏，
# 只有服务端 ref 的创建是原子的：同名 ref 已存在时 push 会被拒绝，绕不过。
def _git(args):
    return subprocess.run(["git", "-C", root] + args, capture_output=True)

claim_ref = "refs/claims/%s" % code
if parent_code:
    probe = _git(["ls-remote", "--exit-code", "origin", "refs/claims/%s" % parent_code])
    if probe.returncode == 0:
        sys.stderr.write("[领用] 父任务 %s 已被认领（服务端 refs/claims/%s 存在），不能单独领用子卡 %s\n"
                         % (parent_code, parent_code, code))
        sys.exit(3)
    if probe.returncode != 2:
        sys.stderr.write("[领用] 无法访问 origin 校验父任务占位（退出码 %d），领用需联网\n" % probe.returncode)
        sys.exit(5)

head_sha = subprocess.check_output(["git", "-C", root, "rev-parse", "HEAD"]).decode().strip()
pushed = _git(["push", "origin", "%s:%s" % (head_sha, claim_ref)])
if pushed.returncode != 0:
    detail = (pushed.stderr or pushed.stdout).decode("utf-8", "replace").strip()
    sys.stderr.write("[领用] 服务端占位失败（%s）：短号 %s 可能已被认领，或网络不可用\n%s\n"
                     % (claim_ref, code, detail))
    sys.exit(3)
print("[领用] 服务端已占位 %s" % claim_ref)

# 判活：分支是否还在（本地 heads 或远端跟踪）、服务端占位是否还在。
# 失效登记的判据是「三处皆无」——已合并删分支、放弃后删分支都属此列；
# 只要还有一处存在就当仍被占用，宁可拦错也不放开双领。
def branch_exists(name):
    if not name:
        return False
    for ref in ("refs/heads/" + name, "refs/remotes/origin/" + name):
        probe = subprocess.run(
            ["git", "-C", root, "show-ref", "--verify", "--quiet", ref],
            capture_output=True, check=False)
        if probe.returncode == 0:
            return True
    return False

_branch_ref_cache = {}
def existing_branch_ref(name):
    if name in _branch_ref_cache:
        return _branch_ref_cache[name]
    found = None
    for ref in ("refs/heads/" + name, "refs/remotes/origin/" + name):
        probe = subprocess.run(
            ["git", "-C", root, "show-ref", "--verify", "--quiet", ref],
            capture_output=True, check=False)
        if probe.returncode == 0:
            found = ref
            break
    _branch_ref_cache[name] = found
    return found

_claim_ref_cache = {}
def claim_ref_state(target):
    """服务端占位状态 → ("sha", <commit>) / ("none", None) / ("unreachable", None)。

    不能用 `ls-remote --exit-code` 的退出码判断「有没有」：本次领用会**先**推占位，
    那会让旧登记看起来仍然活动（R4-03 重领时踩过）。所以这里取回具体 sha，交给
    调用方与登记里的 head 比对 —— 占位指向别的提交，说明这张卡已被新的领用接管。
    """
    if target in _claim_ref_cache:
        return _claim_ref_cache[target]
    probe = subprocess.run(
        ["git", "-C", root, "ls-remote", "origin", "refs/claims/%s" % target],
        capture_output=True, check=False)
    if probe.returncode != 0:
        state = ("unreachable", None)
    else:
        state = ("none", None)
        for line in probe.stdout.decode("utf-8", "replace").splitlines():
            parts = line.split()
            if len(parts) >= 2:
                state = ("sha", parts[0])
                break
    _claim_ref_cache[target] = state
    return state

def claim_is_stale(claim, target):
    # 分支与占位都不在了才算失效：历史缺口是「分支删了、登记留着」，
    # 这类残留曾把同一张卡的重新领用挡在防双领校验上（R4-02 踩过两次）。
    if not claim:
        return True
    if branch_exists(claim.get("branch", "")):
        return False
    kind, sha = claim_ref_state(target)
    if kind == "unreachable":
        return False  # 无法确认是否已释放，保守当仍被占用
    if kind == "none":
        return True
    return sha != (claim.get("head") or "")

# 防双领：origin/main 与所有远端分支头里的台账，若同短号已被其它分支领用 → 拒绝。
# 失效登记（分支已删且服务端占位已释放）不算占位，否则没人能再领同一张卡。
refs = ["origin/main"]
out = subprocess.run(
    ["git", "-C", root, "for-each-ref", "--format=%(refname)", "refs/remotes/origin"],
    capture_output=True, check=False)
if out.returncode == 0:
    for line in out.stdout.decode("utf-8", "replace").splitlines():
        if line and not line.endswith("/HEAD"):
            refs.append(line.strip())

taken = []
parent_taken = []
for ref in refs:
    tree = subprocess.run(
        ["git", "-C", root, "show", "%s:docs/board-ledger.json" % ref],
        capture_output=True, check=False)
    if tree.returncode != 0:
        continue
    try:
        other = json.loads(tree.stdout.decode("utf-8", "replace"))
    except ValueError:
        continue
    claim = (other.get("claims") or {}).get(code)
    if claim and claim.get("branch") != branch and not claim_is_stale(claim, code):
        taken.append("%s@%s" % (code, ref))
    if parent_code:
        parent_claim = (other.get("claims") or {}).get(parent_code)
        if (parent_claim and parent_claim.get("branch") != branch
                and not claim_is_stale(parent_claim, parent_code)):
            parent_taken.append("%s@%s" % (parent_code, ref))
if taken:
    sys.stderr.write("[领用] 短号 %s 已被其它分支领用：%s\n" % (code, "、".join(taken)))
    sys.exit(3)
if parent_taken:
    sys.stderr.write("[领用] 父任务 %s 已被认领（%s），不能单独领用子卡 %s\n"
                     % (parent_code, "、".join(parent_taken), code))
    sys.exit(3)

# 清理失效登记：分支已被删除、或已合并进 origin/main 的领用不再需要占位。
# 不清会让短号被永久「占用」，后来者无法领用同一张卡（worktree 回收时也没人负责删登记）。
data.setdefault("claims", {})
stale = []
for stale_code, stale_claim in list(data["claims"].items()):
    if stale_code == code:
        continue
    stale_branch = (stale_claim or {}).get("branch", "")
    if not stale_branch:
        stale.append(stale_code)
        continue
    if stale_branch == branch:
        continue
    # 只在本地删了、远端还在的分支不算失效（可能是别人刚推的活动分支）
    if not branch_exists(stale_branch):
        stale.append(stale_code)
        continue
    merged_ref = existing_branch_ref(stale_branch)
    if merged_ref:
        merged = subprocess.run(
            ["git", "-C", root, "merge-base", "--is-ancestor", merged_ref, "origin/main"],
            capture_output=True, check=False)
        if merged.returncode == 0:
            stale.append(stale_code)
for stale_code in stale:
    data["claims"].pop(stale_code, None)
if stale:
    print("[领用] 清理失效登记：%s" % "、".join(sorted(stale)))

data["claims"][code] = {
    "agent": agent,
    "branch": branch,
    "head": head_sha,
    "claimedAt": int(time.time()),
}
with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
    json.dump(data, fh, indent=4, sort_keys=True, ensure_ascii=False)
    fh.write("\n")
os.replace(tmp, ledger)
print("[领用] %s ← %s（%s %s）已登记，合并/放弃分支后自动失效%s"
      % (code, branch, agent, "",
         ("，父任务 %s 未被认领" % parent_code) if parent_code else ""))
'@
        [System.IO.File]::WriteAllText($claimPy, $pySource, (New-Object System.Text.UTF8Encoding($false)))
        try {
            & python $claimPy $worktree $code $Agent $Branch $boardDb
            if ($LASTEXITCODE -ne 0) { throw "领用登记失败：python 退出码 $LASTEXITCODE" }
        } finally {
            Remove-Item -LiteralPath $claimPy -ErrorAction SilentlyContinue
        }

        # 3) 登记落进分支
        & git add docs/board-ledger.json
        & git commit -m ("chore(claim): 登记 #{0} 领用到台账" -f $code) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "领用登记提交失败：$LASTEXITCODE" }
    } finally {
        Pop-Location
    }

    # 4) 显式投递兜底（应用在跑就立即做 doing，不在跑则下次启动补投递）
    $deliverFile = Join-Path $env:TEMP ("neonboard-claim-{0}.txt" -f $Agent)
    [System.IO.File]::WriteAllText($deliverFile, ("#{0}=doing" -f $code), (New-Object System.Text.UTF8Encoding($false)))
    $exeProbe = $env:NEONBOARD_EXE
    if (-not ($exeProbe -and (Test-Path -LiteralPath $exeProbe))) {
        $candidates = @(
            (Join-Path $repoRoot 'build\Release\NeonBoard.exe'),
            (Join-Path $repoRoot 'dist\NeonBoard.exe'),
            (Join-Path $env:LOCALAPPDATA 'Programs\NeonBoard\NeonBoard.exe'),
            (Join-Path $env:ProgramFiles 'NeonBoard\NeonBoard.exe')
        )
        $exeProbe = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    }
    if ($exeProbe -and (Test-Path -LiteralPath $exeProbe)) {
        $oldPath = $env:PATH
        $qtBin = Get-ChildItem 'C:\Qt\*\msvc2022_64\bin' -Directory -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        $exeDir = Split-Path -Parent $exeProbe
        $env:PATH = (@($exeDir, $qtBin) | Where-Object { $_ }) -join ';' + ';' + $env:PATH
        try {
            & $exeProbe --emit-hook-file $deliverFile *> $null
            if ($LASTEXITCODE -eq 0) {
                Write-Host ("· 领用已投递：看板 #{0} → 进行中" -f $code) -ForegroundColor DarkGray
            } else {
                Write-Host ("领用投递未完成（退出码 {0}），看板启动时会补投递" -f $LASTEXITCODE) -ForegroundColor Yellow
            }
        } finally {
            $env:PATH = $oldPath
            Remove-Item -LiteralPath $deliverFile -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item -LiteralPath $deliverFile -ErrorAction SilentlyContinue
        Write-Host '未找到 NeonBoard.exe，看板启动时会补投递' -ForegroundColor Yellow
    }
}

Write-Host ''
Write-Host '✓ 工作区就绪' -ForegroundColor Green
Write-Host ("  路径   : {0}" -f $worktree)
Write-Host ("  分支   : {0}" -f $Branch)
Write-Host ("  沙箱   : {0}" -f $sandbox)
Write-Host ''
Write-Host '后续步骤：' -ForegroundColor Cyan
Write-Host ("  cd {0}" -f $worktree)
Write-Host ("  . .\.agent-env.ps1")
Write-Host ("  cmake -S . -B build -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64")
Write-Host ('  cmake --build build --config Release')
Write-Host ("  pwsh -File scripts\dev-run.ps1          # 沙箱启动，不污染真实数据")
if ($Card) { Write-Host ("  领用： {0}（进行中由台账 claims 派生并自动投递）" -f $Card) }
