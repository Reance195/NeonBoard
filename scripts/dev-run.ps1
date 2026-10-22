<#
    以沙箱方式启动 NeonBoard（多代理安全）

    自动注入沙箱环境变量，避免多个 agent 的应用实例互相污染：
      NEONBOARD_DATA_DIR         沙箱数据目录（board.db / plugins / log / hooks.db / 界面设置）
      NEONBOARD_INSTANCE         单实例 key 后缀，允许多实例并存
      NEONBOARD_ROADMAP          路线图快照路径，落在沙箱内，不写脏仓库 progress.json
      NEONBOARD_ROADMAP_AUTOWRITE=0  关闭看板 → 快照自动写回
      NEONBOARD_AGENT_ID         agent 身份，供脚本登记占用

    可执行文件默认优先用 dist\NeonBoard\NeonBoard.exe（便携版已带 Qt，无需编译），
    没有才退回 build\Release\NeonBoard.exe；-Exe 可显式指定。
    只有改了 C++/QML 需要验证时才构建：cmake --build build --config Release

    用法：
      pwsh -File scripts/dev-run.ps1
      pwsh -File scripts/dev-run.ps1 -Wait
      pwsh -File scripts/dev-run.ps1 -Wait -AppArgs --smoke
      pwsh -File scripts/dev-run.ps1 -Wait -AppArgs --selftest,--smoke
#>
[CmdletBinding()]
param(
    [string]$Agent = '',
    [string]$Exe = '',
    [switch]$Wait,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$AppArgs = @()
)

$ErrorActionPreference = 'Stop'

# 仓库根按**脚本自身路径**定位：脚本在哪个 worktree 里，沙箱就落在哪个 worktree。
# 按当前目录定位会在锚点区调用 agent 的脚本时，把沙箱建到锚点里去。
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

# agent 身份：显式参数 > 环境变量 > 从 .worktrees/<agent> 路径推断
if (-not $Agent) { $Agent = $env:NEONBOARD_AGENT_ID }
if (-not $Agent) {
    # Git 在 Windows 上返回正斜杠路径，统一成 '/' 再匹配，避免分隔符差异导致推断失败
    $norm = $repoRoot -replace '\\', '/'
    $marker = '/.worktrees/'
    $idx = $norm.IndexOf($marker)
    if ($idx -ge 0) {
        $rest = $norm.Substring($idx + $marker.Length)
        $Agent = ($rest -split '/')[0]
    }
}
if (-not $Agent) { $Agent = 'solo' }

# 沙箱目录放在自己的 worktree 内，worktree 之间天然隔离
$sandbox = Join-Path $repoRoot '.sandbox'
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null

$env:NEONBOARD_DATA_DIR = $sandbox
$env:NEONBOARD_INSTANCE = $Agent
$env:NEONBOARD_ROADMAP = (Join-Path $sandbox 'progress.json')
$env:NEONBOARD_ROADMAP_AUTOWRITE = '0'
$env:NEONBOARD_AGENT_ID = $Agent

# 默认优先用便携版（dist 已带 Qt 运行时，不需要编译），没有才退回 build 产物。
# 只有改了 C++/QML 需要验证时才构建：cmake --build build --config Release
if (-not $Exe) {
    $candidates = @(
        (Join-Path $repoRoot 'dist\NeonBoard\NeonBoard.exe'),
        (Join-Path $repoRoot 'build\Release\NeonBoard.exe')
    )
    $Exe = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $Exe -or -not (Test-Path -LiteralPath $Exe)) {
    throw "找不到可执行文件（dist\NeonBoard 与 build\Release 均不存在）`n请先出包或构建：pwsh -File scripts\deploy.ps1"
}

Write-Host ("启动 {0}" -f $Exe) -ForegroundColor Cyan
Write-Host ("  agent   : {0}" -f $Agent) -ForegroundColor DarkGray
Write-Host ("  沙箱    : {0}" -f $sandbox) -ForegroundColor DarkGray
Write-Host ("  路线图  : {0}（自动写回已关闭）" -f $env:NEONBOARD_ROADMAP) -ForegroundColor DarkGray

$argumentList = @()
if ($AppArgs) { $argumentList = $AppArgs }

if ($Wait) {
    & $Exe @argumentList
    exit $LASTEXITCODE
}

Start-Process -FilePath $Exe -ArgumentList $argumentList -WorkingDirectory $repoRoot | Out-Null
Write-Host '  已启动（后台）' -ForegroundColor Green
exit 0
