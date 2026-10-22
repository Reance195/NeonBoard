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
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Agent,
    [string]$Branch = '',
    [string]$Base = 'origin/main',
    [switch]$NoFetch
)

$ErrorActionPreference = 'Stop'

$repoRoot = (& git rev-parse --show-toplevel 2>$null | Select-Object -First 1)
if (-not $repoRoot) { throw '当前目录不是 Git 仓库' }
$repoRoot = $repoRoot.Trim()

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
