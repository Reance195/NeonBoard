<#
.SYNOPSIS
    将 NeonBoard 的构建/产物目录加入 Windows Defender 实时扫描排除项，加快构建（R4-08）。

.DESCRIPTION
    Defender 实时扫描会在构建时反复扫描 build/ 下大量 .obj/.dll，拖慢 I/O 密集的编译。
    本脚本把仓库的构建与产物目录加入排除列表，减少这类开销。

    需要**管理员权限**运行（Add-MpPreference 改的是机器级策略）。
    仅排除本项目目录，不做任何全局/系统目录排除，安全面可控、可随时 -Remove 还原。

    用法（以「管理员身份」运行 PowerShell）：
      pwsh -File scripts/add-defender-exclusion.ps1              # 添加排除
      pwsh -File scripts/add-defender-exclusion.ps1 -WhatIf      # 预览将操作的目录（不实际改动）
      pwsh -File scripts/add-defender-exclusion.ps1 -Remove      # 移除排除
      pwsh -File scripts/add-defender-exclusion.ps1 -ExtraPaths 'D:\other\build'   # 追加自定义目录
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$Remove,

    [string[]]$ExtraPaths = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 仓库根按脚本自身路径定位
$scriptDir = $PSScriptRoot
if (-not $scriptDir) { $scriptDir = Split-Path -Parent $PSCommandPath }
$repoRoot = Split-Path -Parent $scriptDir

# 需排除的目录：构建目录、产物目录，以及额外传入的目录
$paths = @(
    (Join-Path $repoRoot 'build'),
    (Join-Path $repoRoot 'dist')
) + $ExtraPaths

# 归一化绝对路径 + 去重
$targets = $paths | ForEach-Object { [System.IO.Path]::GetFullPath($_) } | Sort-Object -Unique

Write-Host 'NeonBoard · Defender 排除项' -ForegroundColor Magenta
foreach ($t in $targets) {
    $mark = '[不存在,仍登记]'
    if (Test-Path -LiteralPath $t) { $mark = '[存在]' }
    Write-Host ("  {0}  {1}" -f $mark, $t)
}

# 管理员检查：Add-MpPreference 改机器级策略，非管理员会失败
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$adminRole = [Security.Principal.WindowsBuiltInRole]::Administrator
$isAdmin = $principal.IsInRole($adminRole)
if (-not $isAdmin) {
    Write-Host ''
    Write-Host 'X 需要管理员权限：Add-MpPreference 修改机器级 Defender 策略。' -ForegroundColor Red
    Write-Host '  请右键 PowerShell「以管理员身份运行」再执行本脚本。' -ForegroundColor Yellow
    exit 1
}

# Defender 未启用/不可用时安静跳过
if (-not (Get-Command Add-MpPreference -ErrorAction SilentlyContinue)) {
    Write-Host '· 未找到 Add-MpPreference（Defender 可能未启用），无需排除，跳过。' -ForegroundColor Yellow
    exit 0
}

$action = '添加'
if ($Remove) { $action = '移除' }
foreach ($t in $targets) {
    if (-not $PSCmdlet.ShouldProcess($t, "$action Defender 排除项")) { continue }
    try {
        if ($Remove) {
            Remove-MpPreference -ExclusionPath $t -ErrorAction SilentlyContinue
        } else {
            Add-MpPreference -ExclusionPath $t -ErrorAction Stop
        }
        Write-Host ("OK 已{0}排除：{1}" -f $action, $t) -ForegroundColor Green
    } catch {
        Write-Host ("· {0}排除失败（{1}）：{2}" -f $action, $t, $_.Exception.Message) -ForegroundColor Yellow
    }
}

Write-Host ''
Write-Host ("完成：{0}排除项。可用 Get-MpPreference 查看当前排除列表。" -f $action) -ForegroundColor Green
