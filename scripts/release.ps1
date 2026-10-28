<#
    NeonBoard 发布闸门：一处出包，产物可溯源

    五道闸门（顺序执行，任一不过立即中止，不留半成品）：
      1. 发布锁：独占 dist/ 与安装包输出，避免两个 agent 同时覆盖产物
      2. HEAD 必须等于 origin/main（发布物必须来自最新主线）
      3. 工作区必须干净（不允许把未提交内容打进安装包）
      4. Release 构建 + --selftest 自检必须通过
      5. 产物必须带 BUILD-INFO.txt（commit / 分支 / 时间 / SHA256）

    用法：
      pwsh -File scripts/release.ps1
      pwsh -File scripts/release.ps1 -Agent agent-c -SkipInstaller
      pwsh -File scripts/release.ps1 -AllowDirty      # 仅本地调试，不建议
#>
[CmdletBinding()]
param(
    [string]$Agent = 'release',
    [switch]$SkipBuild,
    [switch]$SkipInstaller,
    [switch]$SkipSelfTest,
    [switch]$AllowDirty,
    [switch]$KillRunning
)

$ErrorActionPreference = 'Stop'

# 仓库根按**脚本自身路径**定位（脚本位于 <repo>/scripts/），不按调用方当前目录：
# 否则在锚点区调用 agent worktree 里的脚本，会把锚点误当成本仓库来出包
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

$build = Join-Path $repoRoot 'build'
$dist = Join-Path $repoRoot 'dist'
$portable = Join-Path $dist 'NeonBoard'

# 版本号唯一来源：CMakeLists 的 project(NeonBoard VERSION x.y.z)。
# 安装包版本从这里取，不再依赖 .iss 里的手写常量 —— 两处各写一份必然漂移，
# 表现为「程序自称 0.1.1、安装包 0.1.0」，更新检查会据此误判。
$cmakeLists = Join-Path $repoRoot 'CMakeLists.txt'
$versionMatch = [regex]::Match(
    (Get-Content -LiteralPath $cmakeLists -Raw -Encoding UTF8),
    'project\s*\(\s*NeonBoard\s+VERSION\s+([0-9][0-9A-Za-z.\-]*)')
if (-not $versionMatch.Success) { throw "无法从 $cmakeLists 解析版本号（project(NeonBoard VERSION x.y.z)）" }
$appVersion = $versionMatch.Groups[1].Value

function Resolve-Tool {
    param(
        [string]$Name,
        [string]$EnvVar,
        [string[]]$Fallbacks
    )
    if ($EnvVar -and (Test-Path -LiteralPath $EnvVar)) { return $EnvVar }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($fallback in $Fallbacks) {
        if ($fallback -and (Test-Path -LiteralPath $fallback)) { return $fallback }
    }
    return $null
}

$cmake = Resolve-Tool -Name 'cmake' -EnvVar $env:NEONBOARD_CMAKE -Fallbacks @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\CMake\bin\cmake.exe'
)
if (-not $cmake) { throw '找不到 cmake，请设置 NEONBOARD_CMAKE 或把 cmake 加入 PATH' }

$iscc = Resolve-Tool -Name 'ISCC' -EnvVar $env:NEONBOARD_ISCC -Fallbacks @(
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe',
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
)

function Invoke-NativeLogged {
    # 统一跑原生命令：stderr 一律并入日志文件，并且临时放宽 ErrorActionPreference。
    # MSBuild / ISCC 都会往 stderr 写进度信息，在 ErrorActionPreference=Stop 下
    # 会把整段输出当成终止性错误，导致出包脚本半路夭折（已踩过）。
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = '',
        [string]$LogPath = ''
    )
    if (-not $LogPath) { $LogPath = Join-Path $repoRoot 'build\release-native.log' }
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($WorkingDirectory) {
            Push-Location -LiteralPath $WorkingDirectory
        }
        try {
            # Tee-Object 会把日志写进成功流，必须再用 Out-Host 吃掉，
            # 否则函数返回值会变成「日志文本 + 退出码」的数组（已踩过）
            & $FilePath @Arguments 2>&1 | Tee-Object -FilePath $LogPath | Out-Host
        } finally {
            if ($WorkingDirectory) { Pop-Location }
        }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    return $code
}

function Show-LogTail {
    param([string]$LogPath, [int]$Lines = 40)
    if (-not (Test-Path -LiteralPath $LogPath)) { return }
    Write-Host "· 日志尾部（$LogPath）：" -ForegroundColor DarkGray
    Get-Content -LiteralPath $LogPath -Tail $Lines -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
}

# ---- 闸门 1：发布锁 ----
$workspace = Join-Path $PSScriptRoot 'agent-workspace.ps1'
& $workspace -Action Claim -Agent $Agent -Scope release -Task "发布出包"
if ($LASTEXITCODE -ne 0) { throw '发布锁被占用，本次中止（另一个 agent 正在出包）' }

try {
    # ---- 闸门 2：HEAD 必须等于 origin/main ----
    & git -C $repoRoot fetch origin --quiet
    $head = (& git -C $repoRoot rev-parse HEAD).Trim()
    $originMain = (& git -C $repoRoot rev-parse origin/main).Trim()
    if ($head -ne $originMain) {
        throw ("HEAD 与 origin/main 不一致，拒绝出包`n  HEAD        = {0}`n  origin/main = {1}`n请先同步主线（git fetch && git rebase origin/main）。" -f $head, $originMain)
    }
    $branch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD).Trim()

    # ---- 闸门 3：工作区干净 ----
    $status = & git -C $repoRoot status --porcelain
    if ($status -and -not $AllowDirty) {
        Write-Host '✗ 工作区不干净，拒绝出包：' -ForegroundColor Red
        $status | ForEach-Object { Write-Host "   $_" -ForegroundColor Yellow }
        throw '请先提交或 stash（确需强制请加 -AllowDirty）'
    }

    # ---- 闸门 4：构建 + 自检 ----
    if (-not $SkipBuild) {
        Write-Host '· 构建 Release ...' -ForegroundColor Cyan
        $buildLog = Join-Path $repoRoot 'build\release-build.log'
        $buildExit = Invoke-NativeLogged -FilePath $cmake `
            -Arguments @('--build', $build, '--config', 'Release') `
            -WorkingDirectory $repoRoot -LogPath $buildLog
        if ($buildExit -ne 0) {
            Show-LogTail -LogPath $buildLog
            throw "Release 构建失败：$buildExit"
        }
        Write-Host '  构建日志：build\release-build.log' -ForegroundColor DarkGray
    }

    # 构建产物位置：Visual Studio 多配置构建在 Release\，Ninja 单配置直接在 build\
    $exe = Join-Path $build 'Release\NeonBoard.exe'
    if (-not (Test-Path -LiteralPath $exe)) {
        $exe = Join-Path $build 'NeonBoard.exe'
    }
    if (-not (Test-Path -LiteralPath $exe)) { throw "找不到构建产物：$exe" }

    $running = @(Get-Process -Name 'NeonBoard' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        if ($KillRunning) {
            Write-Host ("· 结束正在运行的 NeonBoard（{0} 个）" -f $running.Count) -ForegroundColor Yellow
            $running | ForEach-Object { Stop-Process -Id $_.Id -Force }
            Start-Sleep -Milliseconds 800
        } else {
            throw ("有 {0} 个 NeonBoard 正在运行，会锁住 dist 文件。`n请先关闭，或加 -KillRunning 强制结束。" -f $running.Count)
        }
    }

    if (-not $SkipSelfTest) {
        Write-Host '· 数据层自检 ...' -ForegroundColor Cyan
        $selfTestDir = Join-Path $repoRoot '.sandbox\release-selftest'
        New-Item -ItemType Directory -Path $selfTestDir -Force | Out-Null
        $env:NEONBOARD_DATA_DIR = $selfTestDir
        $env:NEONBOARD_INSTANCE = "$Agent-release-selftest"
        # 自检 exe 是 GUI 子系统，输出只进日志文件；退出码才是判据
        $selfTestExit = Invoke-NativeLogged -FilePath $exe -Arguments @('--selftest') `
            -WorkingDirectory $repoRoot -LogPath (Join-Path $repoRoot 'build\release-selftest.log')
        if ($selfTestExit -ne 0) { throw "自检失败：$selfTestExit" }
        Remove-Item Env:\NEONBOARD_DATA_DIR -ErrorAction SilentlyContinue
        Remove-Item Env:\NEONBOARD_INSTANCE -ErrorAction SilentlyContinue
    }

    # ---- 便携版 ----
    Write-Host '· 生成便携版 ...' -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot 'deploy.ps1') -SkipBuild -OutDir $portable
    if ($LASTEXITCODE -ne 0) { throw "便携版生成失败：$LASTEXITCODE" }

    # ---- 安装包 ----
    $setup = $null
    if (-not $SkipInstaller) {
        if (-not $iscc) { throw '找不到 ISCC（Inno Setup 编译器），可用 -SkipInstaller 跳过安装包' }
        Write-Host "· 编译安装包（版本 $appVersion）..." -ForegroundColor Cyan
        $isccLog = Join-Path $repoRoot 'build\release-iscc.log'
        $isccExit = Invoke-NativeLogged -FilePath $iscc `
            -Arguments @((Join-Path $repoRoot 'installer\neonboard.iss'), "/O$dist", "/DMyAppVersion=$appVersion") `
            -WorkingDirectory $repoRoot -LogPath $isccLog
        if ($isccExit -ne 0) {
            Show-LogTail -LogPath $isccLog
            throw "安装包编译失败：$isccExit"
        }
        $setup = Get-ChildItem -LiteralPath $dist -Filter 'NeonBoard-Setup-*.exe' -ErrorAction SilentlyContinue |
                 Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $setup) { throw '安装包未生成' }
    }

    # ---- 闸门 5：溯源信息 ----
    $info = @()
    $info += "NeonBoard build info"
    $info += "generated : $((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))"
    $info += "commit    : $head"
    $info += "branch    : $branch"
    $info += "agent     : $Agent"
    $info += "exe sha256: $((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)"
    $info += "portable  : $portable"
    if ($setup) {
        $info += "setup     : $($setup.FullName)"
        $info += "setup sha256: $((Get-FileHash -LiteralPath $setup.FullName -Algorithm SHA256).Hash)"
    }
    $infoPath = Join-Path $dist 'BUILD-INFO.txt'
    [System.IO.File]::WriteAllText($infoPath, (($info -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))

    Write-Host ''
    Write-Host '✓ 发布完成' -ForegroundColor Green
    Write-Host ("  便携版 : {0}" -f $portable)
    if ($setup) { Write-Host ("  安装包 : {0}" -f $setup.FullName) }
    Write-Host ("  溯源   : {0}" -f $infoPath)
} finally {
    & $workspace -Action Release -Agent $Agent -Scope release | Out-Null
}
