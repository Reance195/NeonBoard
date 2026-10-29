<#
    以沙箱方式启动 NeonBoard（多代理安全）

    自动注入沙箱环境变量，避免多个 agent 的应用实例互相污染：
      NEONBOARD_DATA_DIR         沙箱数据目录（board.db / plugins / log / hooks.db / 界面设置）
      NEONBOARD_INSTANCE         单实例 key 后缀，允许多实例并存
      NEONBOARD_ROADMAP          路线图快照路径，落在沙箱内，不写脏仓库 progress.json
      NEONBOARD_ROADMAP_AUTOWRITE=0  关闭看板 → 快照自动写回
      NEONBOARD_AGENT_ID         agent 身份，供脚本登记占用

    可执行文件默认优先用 dist\NeonBoard\NeonBoard.exe（便携版已带 Qt，无需编译），
    没有才退回构建产物：build\Release\NeonBoard.exe（VS 多配置）或 build\NeonBoard.exe
    （Ninja 单配置，R4-08 起默认）；-Exe 可显式指定。
    构建产物旁边没有 Qt DLL，脚本会自动解析 Qt bin 并前置到 PATH，否则会连弹
    「找不到 Qt6Sql.dll」；解析不到时按报错提示设 NEONBOARD_QT_BIN。
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

# 从 CMakeCache 里取构建时用的 Qt 安装前缀（-DCMAKE_PREFIX_PATH 的值），取不到返回空串。
function Get-QtPrefixFromCache {
    param([string]$CacheFile)

    if (-not (Test-Path -LiteralPath $CacheFile)) { return '' }
    $hit = Select-String -LiteralPath $CacheFile -Pattern '^CMAKE_PREFIX_PATH:[^=]*=(.+)$' |
        Select-Object -First 1
    if (-not $hit) { return '' }
    $value = $hit.Matches[0].Groups[1].Value.Trim()
    if ($value -and (Test-Path -LiteralPath (Join-Path $value 'bin'))) { return $value }
    return ''
}

# 锚点区根目录：linked worktree 的 git-dir 在 <锚点>/.git/worktrees/<名>，common dir 才是锚点本身。
function Get-AnchorRoot {
    $common = (& git rev-parse --path-format=absolute --git-common-dir 2>$null | Select-Object -First 1)
    if (-not $common) { return '' }
    $parent = Split-Path -Parent $common.Trim()
    if ($parent -and (Test-Path -LiteralPath $parent)) { return $parent }
    return ''
}

# Qt 运行时目录（bin）。构建产物旁边没有 Qt DLL，只有 dist 便携版自带，
# 所以拉起构建产物前必须把 Qt bin 前置到 PATH，否则会连弹「找不到 Qt6Sql.dll」。
# 顺序：本工作区 CMakeCache -> 锚点区 CMakeCache -> NEONBOARD_QT_BIN -> QT_ROOT_DIR -> 默认安装目录。
function Get-QtBinDir {
    param([string]$RepoRoot, [string]$AnchorRoot)

    foreach ($root in @($RepoRoot, $AnchorRoot)) {
        if (-not $root) { continue }
        $prefix = Get-QtPrefixFromCache -CacheFile (Join-Path $root 'build\CMakeCache.txt')
        if ($prefix) { return (Join-Path $prefix 'bin') }
    }
    if ($env:NEONBOARD_QT_BIN -and (Test-Path -LiteralPath $env:NEONBOARD_QT_BIN)) {
        return $env:NEONBOARD_QT_BIN
    }
    if ($env:QT_ROOT_DIR) {
        $bin = Join-Path $env:QT_ROOT_DIR 'bin'
        if (Test-Path -LiteralPath (Join-Path $bin 'Qt6Core.dll')) { return $bin }
    }

    # 默认安装根下多版本并存时取最高版本，与 build.ps1 的探测口径一致
    $found = @()
    foreach ($root in @('C:\Qt', (Join-Path $env:USERPROFILE 'Qt'))) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        foreach ($ver in (Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue)) {
            foreach ($kit in (Get-ChildItem -LiteralPath $ver.FullName -Directory -Filter 'msvc*_64' -ErrorAction SilentlyContinue)) {
                $bin = Join-Path $kit.FullName 'bin'
                if (Test-Path -LiteralPath (Join-Path $bin 'Qt6Core.dll')) {
                    $found += [pscustomobject]@{ Version = $ver.Name; Bin = $bin }
                }
            }
        }
    }
    if ($found) {
        $best = $found |
            Sort-Object -Property @{ Expression = { try { [version]$_.Version } catch { [version]'0.0' } } } -Descending |
            Select-Object -First 1
        return $best.Bin
    }
    return ''
}

# 默认优先用便携版（dist 已带 Qt 运行时，不需要编译），没有才退回构建产物。
# 构建产物布局随生成器变化：VS 多配置在 build\Release\，Ninja 单配置直接在 build\。
# 只有改了 C++/QML 需要验证时才构建：cmake --build build --config Release
if (-not $Exe) {
    $candidates = @(
        (Join-Path $repoRoot 'dist\NeonBoard\NeonBoard.exe'),
        (Join-Path $repoRoot 'build\Release\NeonBoard.exe'),
        (Join-Path $repoRoot 'build\NeonBoard.exe')
    )
    $Exe = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $Exe) {
        throw ("找不到可执行文件，已探测：`n  {0}`n请先出包或构建：pwsh -File scripts\deploy.ps1" -f ($candidates -join "`n  "))
    }
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw ("指定的可执行文件不存在：{0}" -f $Exe)
}

# 便携版自带 Qt 运行时，不碰 PATH；构建产物必须借 Qt bin 才能加载，缺了就是四张缺 DLL 弹窗。
$exePath = [System.IO.Path]::GetFullPath($Exe)
$portableRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'dist\NeonBoard'))
$qtBin = ''
if (-not $exePath.StartsWith($portableRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    $qtBin = Get-QtBinDir -RepoRoot $repoRoot -AnchorRoot (Get-AnchorRoot)
    if (-not $qtBin) {
        throw ("找不到 Qt 运行时，构建产物旁边没有 Qt DLL，直接启动会连弹「找不到 Qt6Sql.dll」。`n  {0}`n请设置 NEONBOARD_QT_BIN=C:\Qt\6.8.3\msvc2022_64\bin，或改用便携版：pwsh -File scripts\deploy.ps1" -f $exePath)
    }
    $env:PATH = $qtBin + ';' + $env:PATH
}

Write-Host ("启动 {0}" -f $Exe) -ForegroundColor Cyan
Write-Host ("  agent   : {0}" -f $Agent) -ForegroundColor DarkGray
Write-Host ("  沙箱    : {0}" -f $sandbox) -ForegroundColor DarkGray
Write-Host ("  路线图  : {0}（自动写回已关闭）" -f $env:NEONBOARD_ROADMAP) -ForegroundColor DarkGray
if ($qtBin) {
    Write-Host ("  Qt      : {0}（已前置到 PATH）" -f $qtBin) -ForegroundColor DarkGray
} else {
    Write-Host ("  Qt      : 便携版自带，无需注入") -ForegroundColor DarkGray
}

$argumentList = @()
if ($AppArgs) { $argumentList = $AppArgs }

if ($Wait) {
    & $Exe @argumentList
    exit $LASTEXITCODE
}

Start-Process -FilePath $Exe -ArgumentList $argumentList -WorkingDirectory $repoRoot | Out-Null
Write-Host '  已启动（后台）' -ForegroundColor Green
exit 0
