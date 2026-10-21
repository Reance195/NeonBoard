param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist\NeonBoard'
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$windeployqt = 'C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe'

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
