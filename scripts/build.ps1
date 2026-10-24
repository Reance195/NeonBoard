<#
.SYNOPSIS
    NeonBoard 一键构建脚本（探测 CMake / Qt，配置 + 构建，可选自检）。

.DESCRIPTION
    仓库内唯一推荐的命令行构建入口，替代手敲两条 cmake 命令：
      pwsh -File scripts/build.ps1                 # Release 构建
      pwsh -File scripts/build.ps1 -Config Debug   # Debug 构建
      pwsh -File scripts/build.ps1 -SelfTest       # 构建后跑 --selftest
      pwsh -File scripts/build.ps1 -Clean          # 清空 build 目录重新配置

    探测顺序：
      CMake：PATH -> VS 2022 各版自带 CMake -> Program Files 独立安装
      Qt   ：-QtRoot -> $env:NEONBOARD_QT_ROOT -> $env:QT_ROOT -> C:\Qt\<版本>\msvc2022_64（取最高版本）

    自检隔离：-SelfTest 会把 NEONBOARD_DATA_DIR 固定到 <BuildDir>\selftest-sandbox，
    并关闭路线图自动写回，避免污染真实看板库与仓库根 progress.json。
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Release',

    [string]$QtRoot = '',

    [string]$BuildDir = 'build',

    [switch]$Clean,

    [switch]$Reconfigure,

    [switch]$SelfTest,

    [int]$Jobs = 0,

    [string]$Target = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 仓库根按**脚本自身路径**定位，避免在锚点区调用 agent worktree 的脚本时构建错目录
$scriptDir = $PSScriptRoot
if (-not $scriptDir) { $scriptDir = Split-Path -Parent $PSCommandPath }
$repoRoot = Split-Path -Parent $scriptDir
if (-not (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt'))) {
    throw "未找到仓库根目录（$repoRoot 下没有 CMakeLists.txt）"
}

function Write-Step {
    param([string]$Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Detail {
    param([string]$Message)
    Write-Host "    $Message" -ForegroundColor DarkGray
}

# 原生命令统一入口：临时放开 Stop，靠退出码判定，避免 cmake/MSBuild 的 stderr 进度打断流程
function Invoke-Native {
    param(
        [string]$Exe,
        [string[]]$Arguments,
        [string]$Label
    )
    Write-Detail ("{0} {1}" -f $Exe, ($Arguments -join ' '))
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Exe @Arguments
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($code -ne 0) {
        throw "$Label 失败（退出码 $code）"
    }
}

function Test-QtRoot {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return $false }
    $deploy = Join-Path $Path 'bin\windeployqt.exe'
    $config = Join-Path $Path 'lib\cmake\Qt6\Qt6Config.cmake'
    return (Test-Path -LiteralPath $deploy) -and (Test-Path -LiteralPath $config)
}

function Resolve-CMake {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $candidates = New-Object System.Collections.Generic.List[string]
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $installPaths = & $vswhere -products * -property installationPath 2>$null
        } finally {
            $ErrorActionPreference = $previous
        }
        foreach ($install in $installPaths) {
            if (-not $install) { continue }
            $candidates.Add((Join-Path $install 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'))
        }
    }
    $candidates.Add((Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'))
    $candidates.Add('C:\Program Files (x86)\CMake\bin\cmake.exe')

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw '未找到 cmake.exe：请安装 CMake 或 Visual Studio 的 C++ 工作负载，或把 cmake 加入 PATH。'
}

function Resolve-QtRoot {
    param([string]$Explicit)

    if ($Explicit) {
        if (-not (Test-QtRoot $Explicit)) {
            throw "指定的 Qt 目录无效（缺少 bin\windeployqt.exe 或 lib\cmake\Qt6\Qt6Config.cmake）：$Explicit"
        }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }

    foreach ($envName in @('NEONBOARD_QT_ROOT', 'QT_ROOT')) {
        $value = [Environment]::GetEnvironmentVariable($envName)
        if ($value -and (Test-QtRoot $value)) {
            return (Resolve-Path -LiteralPath $value).Path
        }
    }

    $base = 'C:\Qt'
    if (Test-Path -LiteralPath $base) {
        $versions = Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+' } |
            Sort-Object { [version]($_.Name) } -Descending
        foreach ($version in $versions) {
            $candidate = Join-Path $version.FullName 'msvc2022_64'
            if (Test-QtRoot $candidate) { return $candidate }
        }
    }

    throw '未找到 Qt 6 的 msvc2022_64 构建：请用 -QtRoot 指定，或设置 NEONBOARD_QT_ROOT。'
}

$buildPath = $BuildDir
if (-not [System.IO.Path]::IsPathRooted($buildPath)) {
    $buildPath = Join-Path $repoRoot $buildPath
}

$cmake = Resolve-CMake
$qt = Resolve-QtRoot -Explicit $QtRoot
$qtPrefix = $qt.Replace('\', '/')

Write-Host "NeonBoard 构建" -ForegroundColor Magenta
Write-Detail "仓库   : $repoRoot"
Write-Detail "配置   : $Config"
Write-Detail "CMake  : $cmake"
Write-Detail "Qt     : $qt"
Write-Detail "输出   : $buildPath"

if ($Clean -and (Test-Path -LiteralPath $buildPath)) {
    Write-Step "清理构建目录"
    Remove-Item -LiteralPath $buildPath -Recurse -Force
}

$cacheFile = Join-Path $buildPath 'CMakeCache.txt'
if ($Reconfigure -and (Test-Path -LiteralPath $cacheFile)) {
    Write-Step "删除 CMake 缓存以重新配置"
    Remove-Item -LiteralPath $cacheFile -Force
}

$needsConfigure = -not (Test-Path -LiteralPath $cacheFile)
if ($needsConfigure) {
    Write-Step "配置 CMake"
    Invoke-Native -Exe $cmake -Label 'CMake 配置' -Arguments @(
        '-S', $repoRoot,
        '-B', $buildPath,
        '-G', 'Visual Studio 17 2022',
        '-A', 'x64',
        "-DCMAKE_PREFIX_PATH=$qtPrefix"
    )
} else {
    Write-Step "复用已有 CMake 缓存（加 -Reconfigure 可强制重配）"
}

Write-Step "构建 $Config"
$buildArgs = @('--build', $buildPath, '--config', $Config)
if ($Jobs -gt 0) {
    $buildArgs += @('--parallel', "$Jobs")
} else {
    $buildArgs += '--parallel'
}
if ($Target) {
    $buildArgs += @('--target', $Target)
}
Invoke-Native -Exe $cmake -Label '构建' -Arguments $buildArgs

$exeName = if ($Target) { "$Target.exe" } else { 'NeonBoard.exe' }
$exePath = Join-Path (Join-Path $buildPath $Config) $exeName
if (Test-Path -LiteralPath $exePath) {
    $size = [math]::Round((Get-Item -LiteralPath $exePath).Length / 1MB, 2)
    Write-Step "构建产物"
    Write-Detail "$exePath（$size MB）"
} elseif (-not $Target) {
    throw "构建结束但未找到可执行文件：$exePath"
}

if ($SelfTest) {
    if (-not (Test-Path -LiteralPath $exePath)) {
        throw "自检需要可执行文件，但未找到：$exePath"
    }
    Write-Step "运行 --selftest（沙箱数据目录）"
    $sandbox = Join-Path $buildPath 'selftest-sandbox'
    New-Item -ItemType Directory -Path $sandbox -Force | Out-Null

    $savedData = [Environment]::GetEnvironmentVariable('NEONBOARD_DATA_DIR')
    $savedInstance = [Environment]::GetEnvironmentVariable('NEONBOARD_INSTANCE')
    $savedAutowrite = [Environment]::GetEnvironmentVariable('NEONBOARD_ROADMAP_AUTOWRITE')
    $savedPath = $env:PATH
    try {
        $env:NEONBOARD_DATA_DIR = $sandbox
        $env:NEONBOARD_INSTANCE = 'selftest'
        $env:NEONBOARD_ROADMAP_AUTOWRITE = '0'
        $env:PATH = (Join-Path $qt 'bin') + ';' + $env:PATH
        Invoke-Native -Exe $exePath -Label '--selftest' -Arguments @('--selftest')
    } finally {
        $env:PATH = $savedPath
        [Environment]::SetEnvironmentVariable('NEONBOARD_DATA_DIR', $savedData)
        [Environment]::SetEnvironmentVariable('NEONBOARD_INSTANCE', $savedInstance)
        [Environment]::SetEnvironmentVariable('NEONBOARD_ROADMAP_AUTOWRITE', $savedAutowrite)
    }
    Write-Step "自检通过"
}

Write-Host ''
Write-Host "构建完成：$Config" -ForegroundColor Green
