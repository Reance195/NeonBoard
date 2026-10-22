param(
    [switch]$SkipBuild,
    # 多代理并行时把产物写进自己的 worktree，避免抢占共享的 dist/
    [string]$BuildDir = '',
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = if ($BuildDir) { [System.IO.Path]::GetFullPath($BuildDir) } else { Join-Path $root 'build' }
$dist = if ($OutDir) { [System.IO.Path]::GetFullPath($OutDir) } else { Join-Path $root 'dist\NeonBoard' }

# 工具定位顺序：环境变量 -> PATH -> 已知安装路径
# CI（GitHub Actions）里 Qt 由 install-qt-action 安装，cmake 来自 PATH，
# 二者都命中前两级；本机开发时 cmake / windeployqt 不在 PATH，回落到硬编码路径。
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

    throw "找不到 $Name，请设置对应环境变量或加入 PATH。"
}

$cmake = Resolve-Tool -Name 'cmake' -EnvVar $env:NEONBOARD_CMAKE -Fallbacks @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\CMake\bin\cmake.exe'
)

$windeployqtFallbacks = @()
if ($env:QT_ROOT_DIR) {
    $windeployqtFallbacks += (Join-Path $env:QT_ROOT_DIR 'bin\windeployqt.exe')
}
$windeployqtFallbacks += 'C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe'

$windeployqt = Resolve-Tool -Name 'windeployqt' -EnvVar $env:NEONBOARD_WINDEPLOYQT -Fallbacks $windeployqtFallbacks

if (-not $SkipBuild) {
    & $cmake --build $build --config Release
    if ($LASTEXITCODE -ne 0) { throw "Release build failed: $LASTEXITCODE" }
}

if (Test-Path -LiteralPath $dist) {
    Remove-Item -LiteralPath $dist -Recurse -Force
}
New-Item -ItemType Directory -Path $dist -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'Release\NeonBoard.exe') -Destination $dist

$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $windeployqt --release --qmldir (Join-Path $root 'qml') --compiler-runtime (Join-Path $dist 'NeonBoard.exe') 2>&1 | ForEach-Object { Write-Host $_ }
$deployExit = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
if ($deployExit -ne 0) { throw "windeployqt failed: $deployExit" }

$plugins = Join-Path $dist 'plugins'
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -Path (Join-Path $root 'plugins-samples\*') -Destination $plugins -Recurse -Force

Write-Host "Portable build ready: $dist"
