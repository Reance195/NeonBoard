<#
.SYNOPSIS
    NeonBoard 一键构建脚本（探测 CMake / Qt，配置 + 构建，可选自检）。

.DESCRIPTION
    仓库内唯一推荐的命令行构建入口，替代手敲两条 cmake 命令：
      pwsh -File scripts/build.ps1                 # Release 构建（默认 Ninja 生成器）
      pwsh -File scripts/build.ps1 -Config Debug   # Debug 构建
      pwsh -File scripts/build.ps1 -SelfTest       # 构建后跑 --selftest
      pwsh -File scripts/build.ps1 -Clean          # 清空 build 目录重新配置
      pwsh -File scripts/build.ps1 -Generator VS   # 回退 Visual Studio/MSBuild 生成器

    生成器（R4-08）：默认 Ninja —— 增量构建更快、便于 sccache 编译缓存；探测不到 ninja 时自动回退 VS。
    Ninja 为单配置生成器：若当前不在 MSVC 开发者环境，脚本会自动定位并导入 vcvars64 的环境后再构建；
    exe 直接落在 build/ 根（VS 多配置则在 build/<Config>/ 下）。装有 sccache 时由 CMakeLists 自动启用编译缓存。

    探测顺序（与 scripts/dev-env.ps1 共用同一套规则）：
      CMake：PATH -> %LOCALAPPDATA%\Programs\DevTools -> VS 2022 各版自带 -> Program Files
      Ninja：同上
      Qt   ：-QtRoot -> $env:NEONBOARD_QT_ROOT -> $env:QT_ROOT
             -> C:\Qt / ~/Qt / %LOCALAPPDATA%\Qt 下的 <版本>\msvc*_64（取最高版本）

    自检隔离：-SelfTest 会把 NEONBOARD_DATA_DIR 固定到 <BuildDir>\selftest-sandbox，
    并关闭路线图自动写回，避免污染真实看板库与仓库根 progress.json。

    环境：若本机没开「开发者 PowerShell」，脚本先用 dev-env.ps1 装配（不读注册表，
    受限环境可用），失败才回退 vcvars64.bat（依赖 reg.exe，受限环境会被拦掉进程）。
    也可先手动装备好环境再构建：. ./scripts/dev-env.ps1
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

    [string]$Target = '',

    [ValidateSet('Ninja', 'VS')]
    [string]$Generator = 'Ninja',

    # 关掉编译缓存，用于对比「开 / 不开 sccache」的耗时（DX-16）
    [switch]$NoCache
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

# 钩子：core.hooksPath 是 clone 级配置、不随 clone 传递，漏配会让 AGENTS.md 的
# 强制条款整份失效（分支保护 / 提交格式 / 任务闸门都不再执行）。构建前顺手固化一次。
$hookSetup = Join-Path $scriptDir 'setup-git-hooks.ps1'
if (Test-Path -LiteralPath $hookSetup) {
    $pwshBin = Get-Command pwsh -ErrorAction SilentlyContinue
    if ($pwshBin) {
        & $pwshBin.Source -NoProfile -ExecutionPolicy Bypass -File $hookSetup
    } else {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $hookSetup
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host '· 仓库钩子安装未完成（不阻断构建，但强制条款不会生效）' -ForegroundColor Yellow
    }
}

# 载入 dev-env.ps1 的探测函数（只定义函数、不装配环境，见 -Check）。
# 目的：CMake / Ninja / Qt 的查找规则与 dev-env.ps1 保持同一份，避免两处漂移。
$devEnvScript = Join-Path $scriptDir 'dev-env.ps1'
$devEnvLoaded = $false
if (Test-Path -LiteralPath $devEnvScript) {
    $previousEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        . $devEnvScript -Quiet -Check
        $devEnvLoaded = ((Get-Command Find-NeonBoardDevTool -ErrorAction SilentlyContinue) -ne $null)
    } finally {
        $ErrorActionPreference = $previousEap
    }
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
    # dev-env.ps1 的查找顺序更全（含 %LOCALAPPDATA%\Programs\DevTools 这类便携安装）
    if ($devEnvLoaded) {
        $found = Find-NeonBoardDevTool -Name 'cmake.exe'
        if ($found) { return $found }
    }

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

    # dev-env.ps1 的查找覆盖 C:\Qt 与 ~/Qt（aqt 默认安装位置），比下面的兜底更全
    if ($devEnvLoaded) {
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $found = Resolve-NeonBoardQtRoot
        } finally {
            $ErrorActionPreference = $previous
        }
        if ($found) { return $found }
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

# 定位 ninja：PATH 优先，其次 VS 各版自带，最后独立 CMake 安装目录。找不到返回 $null。
function Resolve-Ninja {
    if ($devEnvLoaded) {
        $found = Find-NeonBoardDevTool -Name 'ninja.exe'
        if ($found) { return $found }
    }

    $command = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

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
            $candidate = Join-Path $install 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    foreach ($candidate in @(
        (Join-Path $env:ProgramFiles 'CMake\bin\ninja.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'CMake\bin\ninja.exe')
    )) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

# sccache 探测（DX-16）：CMakeLists 只做 `find_program(sccache)`，即**只看 PATH**。而 winget
# 安装改的是用户级 PATH —— 已经跑起来的宿主进程（终端 / IDE / 本脚本的父进程）不会重新读，
# 于是「装了却永远不生效」，实测就在这台机器上空转了几轮。这里主动补一遍 PATH，装完即用。
#
# 另一个坑：winget 会在 WinGet\Links 下放一个 **0 字节**的 App Execution Alias，它 Test-Path
# 为真、`& sccache --version` 却报「不是有效的应用程序」。必须按大小过滤 + 实跑验证，
# 否则会把坏的当好的返回，症状是「找到了但构建报 sccache 无法启动」。
function Resolve-Sccache {
    $candidates = @()
    $cmd = Get-Command sccache -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source) { $candidates += $cmd.Source }
    foreach ($dir in @(
            (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'),
            (Join-Path $env:USERPROFILE '.cargo\bin'),
            'C:\ProgramData\chocolatey\bin')) {
        if (-not $dir -or -not (Test-Path -LiteralPath $dir)) { continue }
        $found = @(Get-ChildItem -LiteralPath $dir -Filter 'sccache.exe' -File -Recurse -Depth 3 -ErrorAction SilentlyContinue)
        foreach ($f in $found) { $candidates += $f.FullName }
    }
    foreach ($candidate in $candidates) {
        if (-not $candidate -or -not (Test-Path -LiteralPath $candidate)) { continue }
        $item = Get-Item -LiteralPath $candidate -ErrorAction SilentlyContinue
        if (-not $item -or $item.Length -eq 0) { continue }
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $version = & $candidate --version 2>$null
            if ($LASTEXITCODE -eq 0 -and $version) {
                return [pscustomobject]@{ Path = $candidate; Version = ($version | Select-Object -First 1) }
            }
        } catch {
        } finally {
            $ErrorActionPreference = $previous
        }
    }
    return $null
}

# ---- DX-52：实测「实际编译链路」上的 /showIncludes 前缀 ----
# 为什么必须实测、不能让 CMake 自探测：
#   ① CMake 的编译器探测**绕开启动器** —— CMAKE_DETERMINE_MSVC_SHOWINCLUDES_PREFIX 直接调
#      cl.exe，不走 CMAKE_<LANG>_COMPILER_LAUNCHER，所以它记下的是「干净 cl.exe」的前缀；
#   ② 真正编译经过 sccache，MSVC 的本地化前缀字节被它改写（实测变成含 U+FFFD 的乱码，
#      且两种控制台代码页下结果相同）。
#   两边对不上 → Ninja 一个依赖条目都解析不出来（#deps 0）→ 改公共头文件不重编 →
#   陈旧 obj 被链进新代码 → 类布局不一致 → 堆损坏（崩溃点还误导性地报在 QImage::~QImage）。
# 所以这里用**同一条链路**（同一个启动器）跑一次探针编译，取 include 行里「已知路径」之前的
# 原始字节，再把这个字节序列交给 CMake —— 让 CMake 记录值 == Ninja 实际看到的值。
#
# 只对 Ninja 有意义：MSBuild（-Generator VS）自己扫依赖，不读 msvc_deps_prefix。
function Get-NbShowIncludesPrefix {
    param(
        [string]$Launcher = '',
        [string]$ProbeRoot = ''
    )

    if (-not $ProbeRoot) { return $null }
    try {
        New-Item -ItemType Directory -Path $ProbeRoot -Force | Out-Null
    } catch {
        return $null
    }

    $headerName = 'nb_showincludes_probe.h'
    $sourceName = 'nb_showincludes_probe.cpp'
    $headerPath = Join-Path $ProbeRoot $headerName
    $sourcePath = Join-Path $ProbeRoot $sourceName
    $objPath = Join-Path $ProbeRoot 'nb_showincludes_probe.obj'
    $logPath = Join-Path $ProbeRoot 'probe.log'

    # 探针源码固定不变：sccache 命中缓存时回放的就是同一条输出，测到的前缀与真实编译一致，
    # 同时不会每次构建都往缓存里塞一条用不上的新条目。
    try {
        Set-Content -LiteralPath $headerPath -Value "#pragma once`nint nb_probe();`n" -Encoding UTF8
        Set-Content -LiteralPath $sourcePath -Value "#include `"$headerName`"`nint nb_probe() { return 0; }`n" -Encoding UTF8
    } catch {
        return $null
    }

    $cmdLine = if ($Launcher) {
        '"' + $Launcher + '" cl.exe /nologo /c /TP "' + $sourcePath + '" /Fo"' + $objPath + '" /showIncludes'
    } else {
        'cl.exe /nologo /c /TP "' + $sourcePath + '" /Fo"' + $objPath + '" /showIncludes'
    }
    # 输出重定向到**文件**而不是管道：诊断输出按控制台输出代码页写原始字节，
    # 经 PowerShell 管道会被重新编码，字节就失真了 —— 前缀必须逐字节对得上。
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & cmd.exe /c ($cmdLine + ' > "' + $logPath + '" 2>&1') | Out-Null
    } catch {
        return $null
    } finally {
        $ErrorActionPreference = $previous
    }

    if (-not (Test-Path -LiteralPath $logPath)) { return $null }
    $bytes = [System.IO.File]::ReadAllBytes($logPath)
    if ($bytes.Length -eq 0) { return $null }

    # latin1(28591) 解码：1 字节 ↔ 1 字符，字节保真。按 UTF-8 解会把中文前缀换成 U+FFFD，
    # 那样测到的就不是编译器真正写出的字节了。
    $text = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
    $idx = $text.LastIndexOf($headerPath)
    if ($idx -lt 0) {
        $idx = $text.LastIndexOf($headerPath.Replace('\', '/'))
    }
    if ($idx -lt 0) { return $null }

    # 前缀 = 该行行首到「路径起点」之间的字节
    $lineStart = $text.LastIndexOf("`n", $idx)
    if ($lineStart -lt 0) { $lineStart = 0 } else { $lineStart = 1 + $lineStart }
    if ($idx -le $lineStart) { return $null }

    $prefix = New-Object byte[] ($idx - $lineStart)
    [System.Array]::Copy($bytes, $lineStart, $prefix, 0, $prefix.Length)
    # 逗号包一层：裸 byte[] 会被 PowerShell 管道拆成一堆字节，逐个返回
    return , $prefix
}

# ---- DX-52：编译缓存必须按「源码树」分桶，否则依赖条目会指向别的 worktree ----
# sccache 的缓存是**按内容寻址**的：同样的源码内容在哪个目录编译都命中同一个条目
# （-D 定义、源文件路径都不参与 key —— 实测过）。命中时 sccache 回放的是**当初编译那个目录**
# 的 /showIncludes 输出，里面的头文件路径属于**别的 worktree**。
# Ninja 把这些路径当成依赖记下来，后果有两样：
#   ① 改本地头文件不重编（陈旧 obj —— 正是 DX-52 要根治的那个坑，只是换了个来源）；
#   ② 那个 worktree 被回收后，Ninja 直接报「missing and no known rule to make it」，构建崩掉。
# 办法：给每个源码树生成一份带「源码根路径」的强制包含头。预处理结果因此随源码树而不同，
# sccache 的 key 随之不同 → 缓存天然分桶 → 回放的路径永远属于当前这棵树。
#
# 代价：缓存不再跨 worktree 共享（每个源码树一个桶）。这与 AGENTS.md 里「新 worktree 首次全量
# 构建约 20~30 分钟（零缓存）」的既有预期一致；换来的是依赖条目永远指向本树，不再出陈旧 obj。
function New-NbBuildIdentityHeader {
    param(
        [string]$SourceRoot = '',
        [string]$OutputPath = ''
    )

    if (-not $SourceRoot -or -not $OutputPath) { return $false }
    $root = ($SourceRoot -replace '\\', '/')
    $content = @(
        '/* 由 scripts/build.ps1 自动生成（DX-52）—— 不要手工改，也不要提交进仓库。',
        '   作用：让编译缓存按「源码树」分桶。sccache 的缓存按内容寻址、跨工作区共享，',
        '   命中时回放的是当初那个目录的 /showIncludes 输出（头文件路径属于别的 worktree），',
        '   Ninja 会把那些路径当依赖 —— 改本地头文件就不重编了。这里把源码根路径写进预处理结果，',
        '   sccache 的 key 随之按源码树分桶，回放的路径也就永远属于当前这棵树。 */',
        "static const char nb_build_identity[] = `"$root`";",
        ''
    ) -join "`n"
    try {
        # 内容没变就别重写：这个头会被 Ninja 当成依赖，重写会刷新 mtime → 全量重编。
        if ((Test-Path -LiteralPath $OutputPath) -and
            ([System.IO.File]::ReadAllText($OutputPath) -eq $content)) {
            return $true
        }
        [System.IO.File]::WriteAllText($OutputPath, $content, (New-Object System.Text.UTF8Encoding($false)))
    } catch {
        return $false
    }
    return $true
}

# MSVC 的 /FI 只能吃「一个 token 里剩下的全部字符」当文件名，带引号会把引号当成文件名的一部分
# （Ninja 不经过 shell，引号不会被剥掉）。而选项里一旦有空格，CMake 会给整个选项加引号 ——
# 于是带空格的路径（仓库装在 "C:\Users\John Doe\..." 下）就会踩到「引号进了文件名」。
# 8.3 短路径能绕开：它不含空格，MSVC 也认。
function Get-NbShortPath {
    param([string]$Path)

    if (-not $Path) { return '' }
    try {
        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $out = & cmd.exe /c ('for %I in ("' + $Path + '") do @echo %~sI') 2>$null
        } finally {
            $ErrorActionPreference = $previous
        }
        $short = ([string]($out | Select-Object -First 1)).Trim()
        if ($short -and ($short -notlike '*~*' -or (Test-Path -LiteralPath $short))) {
            if (Test-Path -LiteralPath $short) { return $short }
        }
    } catch {
    }
    return ''
}

# Ninja + MSVC 需要 cl.exe 及 INCLUDE/LIB/PATH 等环境；与 VS 生成器不同，Ninja 不会自带工具集，
# 必须在 MSVC 开发者环境里运行。仅在确实缺失时才装配（已在开发者环境则跳过）。
#
# 两条装配路径，顺序很关键：
#   1) dev-env.ps1 手工拼装——不读注册表，受限环境下同样可用，优先尝试；
#   2) vcvars64.bat——官方做法，但内部调用 reg.exe，受限环境里会被直接拦掉进程
#      （PowerShell 的 try/catch 接不住，是进程级终止），所以放在后面兜底。
function Import-VcVarsIfNeeded {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    if ($env:VCINSTALLDIR -and (Test-Path -LiteralPath (Join-Path $env:VCINSTALLDIR 'Tools\MSVC'))) { return }

    if ($devEnvLoaded) {
        Write-Detail '装配 MSVC 环境：dev-env.ps1（不依赖注册表）'
        Initialize-NeonBoardDevEnv -Quiet | Out-Null
        if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vcvars = $null
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
            $candidate = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path -LiteralPath $candidate) { $vcvars = $candidate; break }
        }
    }
    if (-not $vcvars) {
        throw '未找到 vcvars64.bat：Ninja + MSVC 需在开发者环境运行，或安装 VS C++ 工作负载；也可用 -Generator VS 回退到 MSBuild。'
    }

    Write-Detail ("导入 MSVC 环境：" + $vcvars)
    $cmdLine = '"' + $vcvars + '" && set'
    try {
        $dump = & cmd.exe /c $cmdLine 2>$null
        foreach ($line in $dump) {
            if ($line -match '^([^=]+)=(.*)$') {
                [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
            }
        }
    } catch {
        Write-Host '· vcvars64 导入未完整执行（受限环境常因 reg.exe 被拦截）' -ForegroundColor Yellow
    }

    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        throw '未能装配 MSVC 环境：请从「开发者 PowerShell / 命令提示符」运行，或先执行 . ./scripts/dev-env.ps1，也可用 -Generator VS 回退。'
    }
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

# ---- 生成器选择（R4-08）：默认 Ninja（增量更快、便于 sccache）；-Generator VS 或找不到 ninja 时回退 MSBuild ----
$useNinja = $false
if ($Generator -eq 'VS') {
    Write-Step '使用 Visual Studio 生成器（MSBuild）'
} else {
    $ninja = Resolve-Ninja
    if ($ninja) {
        $useNinja = $true
        Write-Step ("使用 Ninja 生成器：" + $ninja)
        Import-VcVarsIfNeeded
        $ninjaDir = Split-Path -Parent $ninja
        if ($env:PATH -notlike "*$ninjaDir*") {
            $env:PATH = $ninjaDir + ';' + $env:PATH
        }
    } else {
        Write-Host '· 未找到 ninja，回退 Visual Studio 生成器（可安装 Ninja 或用 -Generator VS 显式指定）' -ForegroundColor Yellow
    }
}
$vsGenerator = 'Visual Studio 17 2022'
$wantGenerator = if ($useNinja) { 'Ninja' } else { $vsGenerator }

# ---- 编译缓存（DX-16）：必须在**配置之前**把 sccache 补进 PATH，否则 CMake 的
# find_program 已经跑完、CMAKE_*_COMPILER_LAUNCHER 已经定死，再补也没用 ----
$sccache = $null
if ($NoCache) {
    Write-Host '· -NoCache：跳过 sccache（用于对比开启前后的耗时）' -ForegroundColor DarkGray
} else {
    $foundSccache = Resolve-Sccache
    if ($foundSccache) {
        $sccache = $foundSccache
        $sccacheDir = Split-Path -Parent $sccache.Path
        if ($env:PATH -notlike "*$sccacheDir*") { $env:PATH = $sccacheDir + ';' + $env:PATH }
        Write-Step ("编译缓存：sccache " + $sccache.Version)
        Write-Detail "路径   : $($sccache.Path)"
    } else {
        Write-Host '· 未找到 sccache，编译缓存未启用（安装：winget install --id Mozilla.sccache -e）' -ForegroundColor Yellow
        Write-Host '  首次构建约 20~30 分钟，装完能显著缩短重复编译；判据与说明见 docs/pitfalls.md §2.12' -ForegroundColor Yellow
    }
}

$needsConfigure = -not (Test-Path -LiteralPath $cacheFile)
if (-not $needsConfigure) {
    # 切换生成器不能复用旧缓存：检测到缓存里的生成器与本次选择不一致时，强制重新配置
    $cachedGen = Select-String -LiteralPath $cacheFile -Pattern '^CMAKE_GENERATOR:INTERNAL=(.*)$' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($cachedGen -and $cachedGen.Matches[0].Groups[1].Value.Trim() -ne $wantGenerator) {
        Write-Step ("检测到生成器变更（" + $cachedGen.Matches[0].Groups[1].Value.Trim() + " -> " + $wantGenerator + "），强制重新配置")
        Remove-Item -LiteralPath $cacheFile -Force
        $needsConfigure = $true
    }
}

# ---- DX-52：/showIncludes 前缀必须与实际编译链路一致，否则 Ninja 依赖条目全空 ----
# 探测只在 Ninja 下做（MSBuild 自己扫依赖）；探测失败就退回 CMake 自探测并如实提示，
# 不假装成功 —— 症状是「改公共头文件不重编」，静默降级等于把老坑留在原地。
$prefixFile = ''
$prefixHash = ''
$prefixArgs = @()
if ($useNinja) {
    $probeRoot = Join-Path $buildPath '.nb-deps-probe'
    $launcherPath = if ($sccache) { $sccache.Path } else { '' }
    $probePrefix = Get-NbShowIncludesPrefix -Launcher $launcherPath -ProbeRoot $probeRoot
    if ($probePrefix -and $probePrefix.Length -gt 0) {
        $prefixFile = Join-Path $probeRoot 'showincludes-prefix.bin'
        [System.IO.File]::WriteAllBytes($prefixFile, $probePrefix)
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try {
            $prefixHash = ([System.BitConverter]::ToString($sha.ComputeHash($probePrefix))).Replace('-', '').ToLowerInvariant()
        } finally {
            $sha.Dispose()
        }
        # 注意每个元素都要加括号：PowerShell 里 ',' 的优先级高于 '+'，
        # 不加括号会算成「字符串 + 数组」，两个 -D 被拼成一条传给 cmake。
        $prefixArgs = @(
            ("-DNEONBOARD_SHOWINCLUDES_PREFIX_FILE=" + $prefixFile.Replace('\', '/')),
            ("-DNEONBOARD_SHOWINCLUDES_PREFIX_HASH=$prefixHash")
        )
        Write-Detail ("依赖前缀: " + (($probePrefix | ForEach-Object { '{0:X2}' -f $_ }) -join ' '))
        Write-Detail ("前缀哈希: " + $prefixHash.Substring(0, 16) + "…（启动器: " + $(if ($launcherPath) { Split-Path -Leaf $launcherPath } else { '无' }) + "）")
    } else {
        Write-Host '· 未能实测 /showIncludes 前缀，退回 CMake 自探测 —— 依赖条目可能仍为空，见 docs/pitfalls.md §3.16' -ForegroundColor Yellow
    }
}

# launcher 显式交给 CMake：探针测的就是它，真正编译也必须是它 —— 两边不一致，
# 测到的前缀就对不上，依赖条目又空了。顺带修掉一个老 bug：**-NoCache 以前根本没关掉 sccache**
# （CMakeLists 里的 find_program 照样找到它，于是「关缓存」的构建其实一直在走缓存）。
$launcherArg = if ($sccache) { $sccache.Path.Replace('\', '/') } else { '' }
$prefixArgs += ("-DNEONBOARD_SCCACHE_LAUNCHER=" + $launcherArg)

# 编译缓存按源码树分桶（理由见 New-NbBuildIdentityHeader 的注释）。
# Ninja 与 MSBuild 都要：MSBuild 的依赖信息也是从缓存里的 obj 读出来的，
# 那里同样带着「当初那个目录」的头文件路径。
$identityHeader = Join-Path $buildPath 'nb_build_identity.h'
if (New-NbBuildIdentityHeader -SourceRoot $repoRoot -OutputPath $identityHeader) {
    # /FI 只能吃一个 token；路径带空格时 CMake 会给整个选项加引号，MSVC 就把引号
    # 当成文件名的一部分了。8.3 短路径不含空格，MSVC 照认。
    $identityFlagPath = $identityHeader
    if ($identityFlagPath -like '* *') {
        $short = Get-NbShortPath -Path $identityHeader
        if ($short) {
            $identityFlagPath = $short
        } else {
            Write-Host '· 构建路径含空格且取不到 8.3 短路径：编译缓存将跨工作区共享（依赖条目可能指向别处）' -ForegroundColor Yellow
            $identityFlagPath = ''
        }
    }
    if ($identityFlagPath) {
        $prefixArgs += ("-DNEONBOARD_BUILD_IDENTITY_HEADER=" + $identityFlagPath.Replace('\', '/'))
    }
} else {
    Write-Host '· 未能生成编译身份头，编译缓存将跨工作区共享（依赖条目可能指向别处）' -ForegroundColor Yellow
}

if ($prefixArgs.Count -gt 0) {
    # 传给 cmake 的 -D 参数集合的指纹：新增或改动任何一个 -D 都必须重配。
    # 不做这件事的后果很隐蔽 —— 复用缓存时 cmake 根本不会被调用，新参数进不了缓存，
    # 编译命令行也就没有那个新参数（踩过一次：编译身份头没进命令行，缓存照样跨工作区共享）。
    $fingerprintSource = ($prefixArgs -join '|')
    $sha2 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $argsFingerprint = ([System.BitConverter]::ToString(
            $sha2.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($fingerprintSource)))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha2.Dispose()
    }
    $prefixArgs += ("-DNEONBOARD_CONFIG_ARGS_FINGERPRINT=$argsFingerprint")

    # 前缀变了也必须重配：CMake 记的是普通变量，不重配就还留着旧值。
    # 旧值对不上 → Ninja 又解析不出依赖；旧 obj 也不会重编（编译命令行没变）。
    if (-not $needsConfigure) {
        $cachedFp = Select-String -LiteralPath $cacheFile -Pattern '^NEONBOARD_CONFIG_ARGS_FINGERPRINT:[^=]*=(.*)$' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        $cachedFpValue = if ($cachedFp) { $cachedFp.Matches[0].Groups[1].Value.Trim() } else { '' }
        if ($cachedFpValue -ne $argsFingerprint) {
            Write-Step '检测到配置参数变化（/showIncludes 前缀或编译身份头），强制重新配置（旧 obj 会因编译命令行变化而全部重编）'
            $needsConfigure = $true
        }
    }
}

$swConfigure = [System.Diagnostics.Stopwatch]::StartNew()
if ($needsConfigure) {
    Write-Step "配置 CMake（$wantGenerator）"
    if ($useNinja) {
        Invoke-Native -Exe $cmake -Label 'CMake 配置' -Arguments (@(
            '-S', $repoRoot,
            '-B', $buildPath,
            '-G', 'Ninja',
            "-DCMAKE_BUILD_TYPE=$Config",
            "-DCMAKE_PREFIX_PATH=$qtPrefix"
        ) + $prefixArgs)
    } else {
        Invoke-Native -Exe $cmake -Label 'CMake 配置' -Arguments (@(
            '-S', $repoRoot,
            '-B', $buildPath,
            '-G', $vsGenerator,
            '-A', 'x64',
            "-DCMAKE_PREFIX_PATH=$qtPrefix"
        ) + $prefixArgs)
    }
} else {
    Write-Step "复用已有 CMake 缓存（加 -Reconfigure 可强制重配）"
}
$swConfigure.Stop()

Write-Step "构建 $Config"
$buildArgs = @('--build', $buildPath)
if (-not $useNinja) {
    # 多配置生成器（VS/MSBuild）才需在构建期指定配置；Ninja 为单配置，配置已在 configure 期固定
    $buildArgs += @('--config', $Config)
}
if ($Jobs -gt 0) {
    $buildArgs += @('--parallel', "$Jobs")
} else {
    $buildArgs += '--parallel'
}
if ($Target) {
    $buildArgs += @('--target', $Target)
}
$swBuild = [System.Diagnostics.Stopwatch]::StartNew()
Invoke-Native -Exe $cmake -Label '构建' -Arguments $buildArgs
$swBuild.Stop()

# exe 路径：Ninja 单配置直接落在 build/ 根；VS 多配置在 build/<Config>/ 下
$exeName = if ($Target) { "$Target.exe" } else { 'NeonBoard.exe' }
if ($useNinja) {
    $exePath = Join-Path $buildPath $exeName
} else {
    $exePath = Join-Path (Join-Path $buildPath $Config) $exeName
}
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

# ---- 反馈回路：耗时与缓存命中（DX-16）。没有这行数字，「构建慢」永远只能靠体感 ----
Write-Step '反馈回路'
Write-Detail ("配置   : {0,7:N1} 秒{1}" -f $swConfigure.Elapsed.TotalSeconds, $(if ($needsConfigure) { '' } else { '（复用缓存，未重跑 CMake）' }))
Write-Detail ("编译   : {0,7:N1} 秒" -f $swBuild.Elapsed.TotalSeconds)
Write-Detail ("合计   : {0,7:N1} 秒" -f ($swConfigure.Elapsed.TotalSeconds + $swBuild.Elapsed.TotalSeconds))
if ($sccache) {
    # 只挑四行关键指标：全量输出 20 多行，多数与「这次构建快不快」无关。
    # 用「名称 + 两个以上空格」精确匹配，否则 'Compile requests' 也会命中 'Compile requests executed'。
    $stats = & $sccache.Path --show-stats 2>$null
    foreach ($key in @('Compile requests', 'Cache hits', 'Cache misses', 'Cache hits rate')) {
        $line = $stats | Where-Object { $_ -match ('^\s*' + [regex]::Escape($key) + '\s{2,}') } | Select-Object -First 1
        if ($line) { Write-Detail ($line.Trim() -replace '\s{2,}', '  ') }
    }
} else {
    Write-Detail '编译缓存：未启用（-NoCache 或未安装 sccache）'
}

Write-Host ''
Write-Host "构建完成：$Config" -ForegroundColor Green
