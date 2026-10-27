<#
.SYNOPSIS
    NeonBoard 构建环境一键就绪（MSVC / Windows SDK / CMake / Ninja / Qt）。

.DESCRIPTION
    Ninja + MSVC 必须在「开发者环境」中运行。常规做法是执行 vcvars64.bat，但该脚本
    依赖 reg.exe 读注册表；在受限沙箱里 reg.exe 被拦截时它会静默少导入一批变量，
    典型症状是 CMake 报 "rc.exe not found" 或 "CXX compiler is not able to compile
    a simple test program"。

    本脚本改走「直接拼装 VCINSTALLDIR / VCToolsInstallDir / INCLUDE / LIB / PATH」，
    全程不读注册表、不需要 UAC，因此在受限环境下同样可用。

    用法（注意点号 + 空格：必须 dot-source，否则变量只存在于子进程）：
      . ./scripts/dev-env.ps1              # 装配环境并打印清单
      . ./scripts/dev-env.ps1 -Quiet       # 只装配，不打印
      pwsh -File scripts/dev-env.ps1 -Check   # 只体检，不改环境

    装好之后再构建：
      pwsh -File scripts/build.ps1 -SelfTest

    缺件怎么补（全部用户级，无需管理员）：
      Qt    python -m pip install aqtinstall
            python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtshadertools -O "$HOME\Qt"
      CMake 解压 cmake-*-windows-x86_64.zip 到 %LOCALAPPDATA%\Programs\DevTools\cmake
      Ninja 解压 ninja-win.zip 到 %LOCALAPPDATA%\Programs\DevTools\ninja
      MSVC  VS BuildTools 勾选「使用 C++ 的桌面开发」（vcvars 不可用时的兜底依赖）
#>
[CmdletBinding()]
param(
    # 只体检、不装配：打印解析结果，不改动任何环境变量
    [switch]$Check,

    # 静默：不打印清单（供 build.ps1 加载函数时使用）
    [switch]$Quiet,

    # 指定 Qt 套件目录，例如 C:\Users\me\Qt\6.8.3\msvc2022_64
    [string]$QtRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# 把目录插到 PATH 最前，已存在则不重复添加（避免反复 dot-source 把 PATH 撑爆）
function Add-PathFront {
    param([string]$Dir)

    if (-not $Dir) { return }
    $normalized = $Dir.TrimEnd('\')
    foreach ($part in ($env:PATH -split ';')) {
        if ($part.TrimEnd('\') -ieq $normalized) { return }
    }
    $env:PATH = $Dir + ';' + $env:PATH
}

function Get-EnvValue {
    param([string]$Name)

    $value = [Environment]::GetEnvironmentVariable($Name)
    if ($value) { return $value }
    return ''
}

# VS / BuildTools 安装根：vswhere 优先，其次常见的非标准安装位置
function Get-VsInstallRoots {
    $roots = New-Object System.Collections.Generic.List[string]

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $lines = & $vswhere -products * -property installationPath 2>$null
        } finally {
            $ErrorActionPreference = $previous
        }
        if ($lines) {
            foreach ($line in $lines) {
                if ($line -and -not $roots.Contains($line)) { $roots.Add($line) }
            }
        }
    }

    foreach ($candidate in @(
        'C:\BuildTools',
        (Join-Path $env:LOCALAPPDATA 'Programs\VSBuildTools'),
        (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\2022\BuildTools')
    )) {
        if (-not $candidate) { continue }
        if ((Test-Path -LiteralPath $candidate) -and -not $roots.Contains($candidate)) { $roots.Add($candidate) }
    }

    return , $roots
}

# MSVC 工具集：取各安装根下 VC\Tools\MSVC 里版本号最高的、且 cl.exe 确实存在的那个
function Find-MsvcToolset {
    foreach ($root in (Get-VsInstallRoots)) {
        $msvcDir = Join-Path $root 'VC\Tools\MSVC'
        if (-not (Test-Path -LiteralPath $msvcDir)) { continue }

        $versions = Get-ChildItem -LiteralPath $msvcDir -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+' } |
            Sort-Object { [version]($_.Name) } -Descending
        foreach ($version in $versions) {
            $cl = Join-Path $version.FullName 'bin\Hostx64\x64\cl.exe'
            if (Test-Path -LiteralPath $cl) {
                return [pscustomobject]@{
                    Root    = $root
                    Tools   = $version.FullName
                    Version = $version.Name
                    Cl      = $cl
                }
            }
        }
    }
    return $null
}

# Windows SDK：取 Include 下版本号最高的、且 bin\<ver>\x64\rc.exe 存在的那个
function Find-WindowsSdk {
    $base = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
    if (-not (Test-Path -LiteralPath $base)) { return $null }

    $versions = Get-ChildItem -LiteralPath (Join-Path $base 'Include') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+' } |
        Sort-Object { [version]($_.Name) } -Descending
    foreach ($version in $versions) {
        $rc = Join-Path $base ('bin\' + $version.Name + '\x64\rc.exe')
        if (Test-Path -LiteralPath $rc) {
            return [pscustomobject]@{
                Dir     = $base
                Version = $version.Name
                Rc      = $rc
            }
        }
    }
    return $null
}

# cmake.exe / ninja.exe：PATH -> DevTools 便携目录 -> VS 自带 -> Program Files
function Find-NeonBoardDevTool {
    param([Parameter(Mandatory = $true)][string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $devTools = Join-Path $env:LOCALAPPDATA 'Programs\DevTools'
    if (Test-Path -LiteralPath $devTools) {
        $hit = Get-ChildItem -LiteralPath $devTools -Recurse -Filter $Name -ErrorAction SilentlyContinue |
            Sort-Object FullName | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }

    foreach ($root in (Get-VsInstallRoots)) {
        foreach ($relative in @(
            'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin',
            'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
        )) {
            $candidate = Join-Path (Join-Path $root $relative) $Name
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }

    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles ('CMake\bin\' + $Name)),
        (Join-Path ${env:ProgramFiles(x86)} ('CMake\bin\' + $Name))
    )) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    return $null
}

function Test-NeonBoardQtRoot {
    param([string]$Path)

    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return $false }
    $deploy = Join-Path $Path 'bin\windeployqt.exe'
    $config = Join-Path $Path 'lib\cmake\Qt6\Qt6Config.cmake'
    return ((Test-Path -LiteralPath $deploy) -and (Test-Path -LiteralPath $config))
}

# Qt 套件：-QtRoot -> NEONBOARD_QT_ROOT -> QT_ROOT -> C:\Qt / ~/Qt / DevTools 下的 <版本>\msvc*_64
function Resolve-NeonBoardQtRoot {
    param([string]$Explicit = '')

    if ($Explicit) {
        if (-not (Test-NeonBoardQtRoot $Explicit)) {
            throw "指定的 Qt 目录无效（缺少 bin\windeployqt.exe 或 lib\cmake\Qt6\Qt6Config.cmake）：$Explicit"
        }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }

    foreach ($envName in @('NEONBOARD_QT_ROOT', 'QT_ROOT')) {
        $value = Get-EnvValue $envName
        if ($value -and (Test-NeonBoardQtRoot $value)) { return (Resolve-Path -LiteralPath $value).Path }
    }

    foreach ($base in @('C:\Qt', (Join-Path $HOME 'Qt'), (Join-Path $env:LOCALAPPDATA 'Qt'))) {
        if (-not $base -or -not (Test-Path -LiteralPath $base)) { continue }

        # 允许环境变量直接指向套件目录本身
        if (Test-NeonBoardQtRoot $base) { return (Resolve-Path -LiteralPath $base).Path }

        $versions = Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+' } |
            Sort-Object { [version]($_.Name) } -Descending
        foreach ($version in $versions) {
            foreach ($kit in @('msvc2022_64', 'msvc2019_64')) {
                $candidate = Join-Path $version.FullName $kit
                if (Test-NeonBoardQtRoot $candidate) { return $candidate }
            }
        }
    }

    return $null
}

function Get-DevEnvReport {
    $report = [ordered]@{
        Msvc  = Find-MsvcToolset
        Sdk   = Find-WindowsSdk
        CMake = Find-NeonBoardDevTool -Name 'cmake.exe'
        Ninja = Find-NeonBoardDevTool -Name 'ninja.exe'
    }
    try { $report.Qt = Resolve-NeonBoardQtRoot -Explicit $QtRoot } catch { $report.Qt = $null }
    return [pscustomobject]$report
}

function Show-DevEnvReport {
    param([switch]$Quiet)

    if ($Quiet) { return }
    $report = Get-DevEnvReport

    Write-Host '构建环境体检' -ForegroundColor Cyan
    Write-Detail ('MSVC  : ' + $(if ($report.Msvc) { $report.Msvc.Tools } else { '未找到' }))
    Write-Detail ('SDK   : ' + $(if ($report.Sdk) { $report.Sdk.Version + '（' + $report.Sdk.Dir + '）' } else { '未找到' }))
    Write-Detail ('CMake : ' + $(if ($report.CMake) { $report.CMake } else { '未找到' }))
    Write-Detail ('Ninja : ' + $(if ($report.Ninja) { $report.Ninja } else { '未找到' }))
    Write-Detail ('Qt    : ' + $(if ($report.Qt) { $report.Qt } else { '未找到' }))

    $missing = @()
    if (-not $report.Msvc) { $missing += 'MSVC（VS BuildTools 的 C++ 工作负载）' }
    if (-not $report.CMake) { $missing += 'CMake' }
    if (-not $report.Qt) { $missing += 'Qt 6（msvc2022_64 套件）' }
    if ($missing.Count -gt 0) {
        Write-Host ''
        Write-Host ('缺件：' + ($missing -join '、')) -ForegroundColor Yellow
        Write-Host '补法见脚本头部注释，或用 -QtRoot 显式指定 Qt 目录。' -ForegroundColor Yellow
    }
}

# 装配环境：不读注册表，直接拼出 MSVC / SDK 需要的所有变量
function Initialize-NeonBoardDevEnv {
    param([switch]$Quiet, [string]$QtRoot = '')

    $msvc = Find-MsvcToolset
    $sdk = Find-WindowsSdk

    if ($msvc) {
        $env:VCINSTALLDIR = (Join-Path $msvc.Root 'VC') + '\'
        $env:VCToolsInstallDir = $msvc.Tools + '\'
        $env:VCToolsVersion = $msvc.Version
        Add-PathFront (Join-Path $msvc.Tools 'bin\Hostx64\x64')

        if ($sdk) {
            $env:WindowsSdkDir = $sdk.Dir + '\'
            $env:WindowsSDKVersion = $sdk.Version + '\'
            $inc = @(
                (Join-Path $msvc.Tools 'include'),
                (Join-Path $sdk.Dir ('Include\' + $sdk.Version + '\ucrt')),
                (Join-Path $sdk.Dir ('Include\' + $sdk.Version + '\um')),
                (Join-Path $sdk.Dir ('Include\' + $sdk.Version + '\shared')),
                (Join-Path $sdk.Dir ('Include\' + $sdk.Version + '\winrt')),
                (Join-Path $sdk.Dir ('Include\' + $sdk.Version + '\cppwinrt'))
            )
            $lib = @(
                (Join-Path $msvc.Tools 'lib\x64'),
                (Join-Path $sdk.Dir ('Lib\' + $sdk.Version + '\ucrt\x64')),
                (Join-Path $sdk.Dir ('Lib\' + $sdk.Version + '\um\x64'))
            )
            $oldInc = Get-EnvValue 'INCLUDE'
            $oldLib = Get-EnvValue 'LIB'
            $env:INCLUDE = $(if ($oldInc) { ($inc -join ';') + ';' + $oldInc } else { $inc -join ';' })
            $env:LIB = $(if ($oldLib) { ($lib -join ';') + ';' + $oldLib } else { $lib -join ';' })
            Add-PathFront (Join-Path $sdk.Dir ('bin\' + $sdk.Version + '\x64'))
        }
    }

    $cmake = Find-NeonBoardDevTool -Name 'cmake.exe'
    if ($cmake) { Add-PathFront (Split-Path -Parent $cmake) }
    $ninja = Find-NeonBoardDevTool -Name 'ninja.exe'
    if ($ninja) { Add-PathFront (Split-Path -Parent $ninja) }

    $qt = $null
    try { $qt = Resolve-NeonBoardQtRoot -Explicit $QtRoot } catch { $qt = $null }
    if ($qt) {
        $env:NEONBOARD_QT_ROOT = $qt
        Add-PathFront (Join-Path $qt 'bin')
    }

    if (-not $Quiet) {
        Write-Host '构建环境已装配' -ForegroundColor Green
        Show-DevEnvReport
    }
    return [pscustomobject]@{ Msvc = $msvc; Sdk = $sdk; CMake = $cmake; Ninja = $ninja; Qt = $qt }
}

function Write-Detail {
    param([string]$Message)
    Write-Host ('  ' + $Message) -ForegroundColor DarkGray
}

# ---- 入口 ----
# 环境变量要留在当前会话就必须 dot-source；直接运行时只在子进程生效，等于白做，
# 所以这种情况下退化成体检并给出正确用法。
$isDotSourced = ($MyInvocation.InvocationName -eq '.') -or ($MyInvocation.Line -match '^\s*\.\s')
if ($isDotSourced) {
    if ($Check) { Show-DevEnvReport -Quiet:$Quiet }
    else { Initialize-NeonBoardDevEnv -Quiet:$Quiet -QtRoot $QtRoot | Out-Null }
} else {
    Write-Host 'dev-env.ps1 需要用 dot-source 方式运行，否则环境变量只存在于子进程：' -ForegroundColor Yellow
    Write-Host '    . ./scripts/dev-env.ps1' -ForegroundColor Yellow
    Write-Host ''
    Show-DevEnvReport
}
