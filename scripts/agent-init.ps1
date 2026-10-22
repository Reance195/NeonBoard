<#
    为某个 agent 创建独立工作区

    产出：
      <repo>/.worktrees/<agent>/          —— 独立 worktree（独立分支、独立 build/dist）
      <repo>/.worktrees/<agent>/.sandbox/  —— 沙箱数据目录（数据库/插件/日志/hooks.db）
      <repo>/.worktrees/<agent>/.agent-env.ps1 —— 可 dot-source 的沙箱环境变量脚本

    关键点：基线用 origin/main 而不是 main —— main 分支已被根目录 checkout，
    Git 不允许同一分支在两个 worktree 同时检出，用远端引用建分支才能完全绕开根目录。

    用法：
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch fix/stagebar-chip-overflow
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch feature/xxx -Base origin/main
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch feature/xxx -Card R3-01
        # -Card <路线图短号>：领用登记 → 台账 claims → 看板卡自动转「进行中」。
        # 分支头脱离主干的期间内该卡为 doing（派生器按 head 位置判定），
        # 分支合并或丢弃后登记自动失效，收尾用提交标记 #<code>=done。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Agent,
    [string]$Branch = '',
    [string]$Base = 'origin/main',
    [string]$Card = '',
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
$envContent = @(
    "# 由 scripts/agent-init.ps1 生成：多代理沙箱环境变量",
    "# 用法： . .\.agent-env.ps1   （PowerShell 7 推荐 pwsh）",
    "`$env:NEONBOARD_DATA_DIR = '$sandbox'",
    "`$env:NEONBOARD_INSTANCE = '$Agent'",
    "`$env:NEONBOARD_ROADMAP = '$(Join-Path $sandbox 'progress.json')'",
    "`$env:NEONBOARD_ROADMAP_AUTOWRITE = '0'",
    "`$env:NEONBOARD_AGENT_ID = '$Agent'",
    ''
) -join "`n"
[System.IO.File]::WriteAllText($envFile, $envContent, (New-Object System.Text.UTF8Encoding($false)))

# ---------- 领用登记（-Card <路线图短号>） ----------
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
import json, os, subprocess, sys, time

root = os.path.abspath(sys.argv[1])
code, agent, branch = sys.argv[2], sys.argv[3], sys.argv[4]
ledger = os.path.join(root, "docs", "board-ledger.json")
tmp = ledger + ".tmp"

data = json.load(open(ledger, encoding="utf-8"))
if code not in (data.get("cards") or {}):
    sys.stderr.write("[领用] 台账未登记短号：%s\n" % code)
    sys.exit(2)

# 防双领：origin/main 与所有远端分支头里的台账，若同短号已被其它分支领用 → 拒绝
refs = ["origin/main"]
out = subprocess.run(
    ["git", "-C", root, "for-each-ref", "--format=%(refname)", "refs/remotes/origin"],
    capture_output=True, check=False)
if out.returncode == 0:
    for line in out.stdout.decode("utf-8", "replace").splitlines():
        if line and not line.endswith("/HEAD"):
            refs.append(line.strip())
taken = []
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
    if claim and claim.get("branch") != branch:
        taken.append("%s@%s" % (code, ref))
if taken:
    sys.stderr.write("[领用] 短号 %s 已被其它分支领用：%s\n" % (code, "、".join(taken)))
    sys.exit(3)

data.setdefault("claims", {})
data["claims"][code] = {
    "agent": agent,
    "branch": branch,
    "head": subprocess.check_output(
        ["git", "-C", root, "rev-parse", "HEAD"]).decode().strip(),
    "claimedAt": int(time.time()),
}
with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
    json.dump(data, fh, indent=4, sort_keys=True, ensure_ascii=False)
    fh.write("\n")
os.replace(tmp, ledger)
print("[领用] %s ← %s（%s %s）已登记，合并/放弃分支后自动失效" % (code, branch, agent, ""))
'@
        [System.IO.File]::WriteAllText($claimPy, $pySource, (New-Object System.Text.UTF8Encoding($false)))
        try {
            & python $claimPy $worktree $code $Agent $Branch
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
