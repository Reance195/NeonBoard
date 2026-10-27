<#
    为某个 agent 创建独立工作区

    产出：
      <repo>/.worktrees/<agent>/          —— 独立 worktree（独立分支、独立 build/dist）
      <repo>/.worktrees/<agent>/.sandbox/  —— 沙箱数据目录（数据库/插件/日志/hooks.db）
      <repo>/.worktrees/<agent>/.agent-env.ps1 —— 可 dot-source 的沙箱环境变量脚本

    关键点：基线用 origin/main 而不是 main —— main 分支已被根目录 checkout，
    Git 不允许同一分支在两个 worktree 同时检出，用远端引用建分支才能完全绕开根目录。

    原地模式（-InPlace / 自动检测）：
      WorkBuddy 的 Worktree Task、或手工 git worktree add，本身已经给了一个隔离检出。
      在里面再开一层 .worktrees/<agent> 属于俄罗斯套娃，改动路径会整体错位一层，
      所以检测到「脚本所属检出本身已是 linked worktree」时自动转为原地初始化：
      复用当前检出，只补 .sandbox / .agent-env.ps1 / 领用登记，不再嵌套开区。

    用法：
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch fix/stagebar-chip-overflow -Card R3-01
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch feature/xxx -Base origin/main -Card M19
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch chore/xxx -Card none
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Card R3-01 -InPlace    # 已在 worktree 里，原地初始化
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
    [switch]$NoFetch,
    # 已在独立 worktree 里时用：不再嵌套开区（WorkBuddy Worktree Task 场景）
    [switch]$InPlace,
    # 强制嵌套：明知道当前是 linked worktree 也要再开一层子工作区（极少用）
    [switch]$ForceNested
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

# ---------- 钩子：确保强制条款真的生效 ----------
# AGENTS.md 的每一条强制都挂在 .githooks 上，而 core.hooksPath 是 clone 级配置、
# 不随 clone 传递。漏配一次，分支保护、提交格式、任务闸门就全部静默失效，
# 所以每次开区都固化一遍（幂等，写的是所有 worktree 共享的那份 config）。
$setupScript = Join-Path $PSScriptRoot 'setup-git-hooks.ps1'
if (Test-Path -LiteralPath $setupScript) {
    $pwshCmd = Get-Command pwsh -ErrorAction SilentlyContinue
    if ($pwshCmd) {
        & $pwshCmd.Source -NoProfile -ExecutionPolicy Bypass -File $setupScript
    } else {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $setupScript
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host '  钩子安装未完成，请手动执行： pwsh -File scripts/setup-git-hooks.ps1' -ForegroundColor Yellow
    }
}

# ---------- 检出形态识别 ----------
# 主工作树的 git-dir 就是 common dir；linked worktree 的 git-dir 落在
# <common>/worktrees/<name> 下。两者不等 ⇒ 当前检出已经是一个独立 worktree。
$gitDir = ''
$commonDir = ''
Push-Location $repoRoot
try {
    $gitDir = (Resolve-Path -LiteralPath (& git rev-parse --git-dir).Trim()).Path
    $commonDir = (Resolve-Path -LiteralPath (& git rev-parse --git-common-dir).Trim()).Path
} finally {
    Pop-Location
}
if ($gitDir -and $commonDir -and ($gitDir -ne $commonDir) -and -not $ForceNested) {
    $InPlace = $true
}

if (-not $Branch) { $Branch = "feature/$Agent" }

# -Card none：显式声明「本次不领用任何卡片」。
# 之所以强制表态，是因为漏填时脚本无法区分「忘了」和「不需要」，
# 多代理并行时漏填就会出现两个人同时改同一张卡却都不占位。
if ($Card -and $Card.Trim() -ieq 'none') {
    Write-Host '· 本次未领用任何卡片（-Card none）' -ForegroundColor Yellow
    $Card = ''
}

if (-not $NoFetch) {
    Write-Host '· 同步远端引用 ...' -ForegroundColor DarkGray
    & git -C $repoRoot fetch origin --prune
    if ($LASTEXITCODE -ne 0) { throw "git fetch 失败：$LASTEXITCODE" }
}

if ($InPlace) {
    # 已经有隔离目录了（WorkBuddy Worktree Task / 手工 worktree）：改用它，不再嵌套。
    $worktree = $repoRoot
    $currentBranch = (& git -C $worktree rev-parse --abbrev-ref HEAD).Trim()
    if (-not $Branch -or $Branch -eq "feature/$Agent") { $Branch = $currentBranch }
    if ($Branch -ne $currentBranch) {
        Write-Host ("  分支请求 {0} ≠ 当前检出 {1}：保留原分支，不擅自切换别人的工作区" -f $Branch, $currentBranch) -ForegroundColor Yellow
        $Branch = $currentBranch
    }
    Write-Host ("· 原地初始化：复用当前检出 {0}" -f $worktree) -ForegroundColor Cyan
    Write-Host ("  分支 {0}（未嵌套创建子工作区）" -f $Branch) -ForegroundColor DarkGray
} else {
    $worktree = Join-Path (Join-Path $repoRoot '.worktrees') $Agent

    if (Test-Path -LiteralPath $worktree) {
        throw "工作区已存在：$worktree（如需重建请先 git worktree remove）"
    }

    Write-Host ("· 创建工作区 {0}" -f $worktree) -ForegroundColor Cyan
    Write-Host ("  分支 {0}  基线 {1}" -f $Branch, $Base) -ForegroundColor DarkGray
    & git -C $repoRoot worktree add $worktree -b $Branch $Base
    if ($LASTEXITCODE -ne 0) { throw "git worktree add 失败：$LASTEXITCODE" }
}

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
        # 必须显式指定 utf8：Windows PowerShell 5.1 会把无 BOM 的 UTF-8 当 GBK 读，
        # 中文的尾字节能吞掉紧随其后的 JSON 引号，台账一长就炸在 ConvertFrom-Json。
        try { $ledger = (Get-Content -LiteralPath $ledgerPath -Raw -Encoding utf8 | ConvertFrom-Json) } catch { $ledger = $null }
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

    # 子卡领用需要读本地看板的父子边；数据目录口径统一走 nb-paths.ps1
    . (Join-Path $repoRoot 'scripts\nb-paths.ps1')
    $boardDb = Get-NbBoardDb

    Write-Host ("· 领用 {0}：登记 ↔ 看板卡转「进行中」" -f $code) -ForegroundColor Cyan
    Push-Location $worktree
    try {
        # 1) 空提交：给分支一个 head，也让 post-commit 钩子顺手投递一次
        & git commit --allow-empty -m ("chore(claim): 领取 #{0} 实施任务" -f $code) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "领用空提交失败：$LASTEXITCODE" }
        $headSha = (& git rev-parse HEAD | Select-Object -First 1)

        # 2) 校验 + 防双领 + 写本 worktree 台账副本
        $claimPy = Join-Path $repoRoot 'scripts\claim-card.py'
        if (-not (Test-Path -LiteralPath $claimPy)) { throw "找不到领用脚本：$claimPy" }
        & python $claimPy $worktree $code $Agent $Branch $boardDb
        if ($LASTEXITCODE -ne 0) { throw "领用登记失败：python 退出码 $LASTEXITCODE" }

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

# ---------- 工作区回收提示（只提示，不自动删）----------
# worktree 不是临时目录：不回收会长期占磁盘，也会让 `git worktree list` 越来越难读。
# 这里刻意不自动删 —— 自动回收很容易端掉一个正在跑的任务，删不删由人决定。
$nested = @()
$porcelain = @(& git -C $repoRoot worktree list --porcelain 2>$null)
foreach ($line in $porcelain) {
    if ($line -like 'worktree *') {
        $path = $line.Substring(9).Trim()
        if ((($path -replace '\\', '/')) -like '*/.worktrees/*') { $nested += $path }
    }
}
if ($nested.Count -ge 5) {
    $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    Write-Host ''
    Write-Host ("· 工作区回收：已有 {0} 个 agent 工作区" -f $nested.Count) -ForegroundColor Cyan
    foreach ($path in $nested) {
        $stamp = (& git -C $path log -1 --format=%ct 2>$null | Select-Object -First 1)
        if (-not $stamp) { $stamp = 0 }
        $ageDays = [int](($now - [int64]$stamp) / 86400)
        $name = Split-Path -Leaf $path
        $tip = ''
        if ($ageDays -ge 14) { $tip = '  ← 超过 14 天无提交，建议回收' }
        Write-Host ("    {0,-16} {1,3} 天前{2}" -f $name, $ageDays, $tip) -ForegroundColor DarkGray
    }
    Write-Host '    回收命令： pwsh -File scripts\git-done.ps1 -RemoveWorktree .worktrees\<agent>' -ForegroundColor DarkGray
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
