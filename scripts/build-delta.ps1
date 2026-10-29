#Requires -Version 5.1
<#
.SYNOPSIS
    生成 NeonBoard 增量升级包（R5-06：文件级 diff + bsdiff 二进制增量）。

.DESCRIPTION
    调用 neonboard-delta-tool（与主程序共用 DeltaUpdate / BsdiffWrapper 源码）对比
    新旧版本目录，产出 delta.json + files/（资源）+ patches/（bsdiff patch），
    再打包成 delta.zip 并计算 SHA-256，最后输出一段可直接并入 update.json
    artifacts 的清单条目（kind=delta，带 fromVersion）。

    用法：
      pwsh -File scripts/build-delta.ps1 -FromDir <旧版目录> -ToDir <新版目录> `
          -FromVersion 0.1.3 -ToVersion 0.1.4 [-OutDir dist/delta]

.PARAMETER FromDir
    旧版本安装/便携目录（须与 FromVersion 对应）

.PARAMETER ToDir
    新版本安装/便携目录（通常就是本次 release 的 dist/NeonBoard）

.PARAMETER FromVersion
    旧版本号（客户端须恰好在此版本才会使用本增量包）

.PARAMETER ToVersion
    新版本号（与 CMakeLists 的 project VERSION 一致）

.PARAMETER OutDir
    输出目录（默认 dist/delta）；产出 delta.zip 与 delta-manifest-entry.json

.PARAMETER Tool
    neonboard-delta-tool 可执行文件路径；缺省自动探测 build/ 与 build/Release/

.EXAMPLE
    pwsh -File scripts/build-delta.ps1 -FromDir dist/NeonBoard-0.1.3 `
        -ToDir dist/NeonBoard -FromVersion 0.1.3 -ToVersion 0.1.4
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$FromDir,
    [Parameter(Mandatory = $true)][string]$ToDir,
    [Parameter(Mandatory = $true)][string]$FromVersion,
    [Parameter(Mandatory = $true)][string]$ToVersion,
    [string]$OutDir = 'dist/delta',
    [string]$Tool = ''
)

$ErrorActionPreference = 'Stop'

# 仓库根按脚本自身路径定位（与 release.ps1 同一口径）
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path -LiteralPath $FromDir)) { throw "旧版本目录不存在：$FromDir" }
if (-not (Test-Path -LiteralPath $ToDir)) { throw "新版本目录不存在：$ToDir" }

# ---- 定位 delta 工具 ----
if (-not $Tool) {
    $candidates = @(
        (Join-Path $repoRoot 'build/neonboard-delta-tool.exe'),
        (Join-Path $repoRoot 'build/Release/neonboard-delta-tool.exe')
    )
    $cmd = Get-Command 'neonboard-delta-tool' -ErrorAction SilentlyContinue
    if ($cmd) { $candidates = @($cmd.Source) + $candidates }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { $Tool = $c; break }
    }
}
if (-not $Tool -or -not (Test-Path -LiteralPath $Tool)) {
    throw "找不到 neonboard-delta-tool.exe，请先构建（cmake --build build）或用 -Tool 指定"
}

$staging = Join-Path $OutDir 'staging'
Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $staging -Force | Out-Null

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " 增量升级包生成（R5-06）" -ForegroundColor Cyan
Write-Host "  旧版: $FromDir ($FromVersion)" -ForegroundColor Gray
Write-Host "  新版: $ToDir ($ToVersion)" -ForegroundColor Gray
Write-Host "  输出: $OutDir" -ForegroundColor Gray
Write-Host "========================================" -ForegroundColor Cyan

# ---- 调工具生成 delta 内容 ----
Write-Host "`n· 生成文件级 diff + bsdiff patch..." -ForegroundColor Yellow
& $Tool build `
    --from $FromDir --to $ToDir `
    --from-version $FromVersion --to-version $ToVersion `
    --out $staging
if ($LASTEXITCODE -ne 0) { throw "neonboard-delta-tool 失败（退出码 $LASTEXITCODE）" }
if (-not (Test-Path -LiteralPath (Join-Path $staging 'delta.json'))) {
    throw "工具未产出 delta.json：$staging"
}

# ---- 打包 delta.zip（staging 内容平铺进 zip 根，客户端解压后直接是 delta.json）----
$zipPath = Join-Path $OutDir 'delta.zip'
Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zipPath -CompressionLevel Optimal

$zipItem = Get-Item -LiteralPath $zipPath
$zipSha = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()

# ---- 输出清单条目（供 build-update-manifest.ps1 并入 artifacts）----
$entry = [ordered]@{
    kind        = 'delta'
    url         = $zipItem.Name
    sha256      = $zipSha
    size        = $zipItem.Length
    fromVersion = $FromVersion
}
$entryJson = $entry | ConvertTo-Json -Depth 4
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText((Join-Path $OutDir 'delta-manifest-entry.json'), $entryJson, $utf8NoBom)

Write-Host "`n✓ 增量包生成完成" -ForegroundColor Green
Write-Host ("  delta.zip : {0:N1} KB" -f ($zipItem.Length / 1KB)) -ForegroundColor Gray
Write-Host "  sha256    : $zipSha" -ForegroundColor Gray
Write-Host "  清单条目  : $(Join-Path $OutDir 'delta-manifest-entry.json')" -ForegroundColor Gray
Write-Host "`n把上面条目并入 update.json 的 artifacts 数组即可（fromVersion 决定哪些客户端走增量）。" -ForegroundColor DarkGray
