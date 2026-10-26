<#
.SYNOPSIS
    把一个插件目录打成市场用的 .neon 单文件包，并写入市场索引 market.json。

.DESCRIPTION
    发布侧脚本，配套客户端的多市场源能力使用：

      1. 读插件目录下的 plugin.json（manifest）
      2. 把插件文件（main.qml / background.qml / rules.js 等）打成 .neon 单文件包
         格式：{ "manifest": {...}, "files": { "相对路径": "文件内容" } }
      3. 写市场索引 market.json，给每个条目算 sha256 与 size
      4. 索引里保留 signature 字段位（本轮不校验，以后接上不改协议）

    .neon 包的下载 URL 由索引里的 download 字段决定；脚本默认写相对文件名
    （例如 demo.neon），客户端按「市场索引所在目录」解析成完整地址。

.PARAMETER PluginPath
    要发布的插件目录（需含 plugin.json），默认 plugins\<插件目录名>。

.PARAMETER OutputDir
    .neon 包输出目录，默认 dist\market。

.PARAMETER IndexPath
    市场索引文件路径，默认 <OutputDir>\market.json。

.PARAMETER DownloadBase
    索引里 download 字段的基地址，留空则只写文件名（客户端按索引所在目录解析）。

.PARAMETER SignatureFile
    索引签名文件（minisign 格式），可留空。内容会写进索引的 signature 字段位。

.PARAMETER RemoveStale
    清理输出目录里没有对应索引条目的旧 .neon 包。

.EXAMPLE
    pwsh -File scripts/publish-market.ps1 -PluginPath plugins\plan-hook

.EXAMPLE
    pwsh -File scripts/publish-market.ps1 -PluginPath plugins\plan-hook `
        -OutputDir D:\market -DownloadBase https://gitee.com/<owner>/<repo>/raw/main
#>
[CmdletBinding()]
param(
    [string]$PluginPath = "",
    [string]$OutputDir = "",
    [string]$IndexPath = "",
    [string]$DownloadBase = "",
    [string]$SignatureFile = "",
    [switch]$RemoveStale
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Write-Step([string]$message) {
    Write-Host "==> $message" -ForegroundColor Cyan
}

function Write-Ok([string]$message) {
    Write-Host "    $message" -ForegroundColor Green
}

function Write-Warn([string]$message) {
    Write-Host "    $message" -ForegroundColor Yellow
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($PluginPath)) {
    throw "必须指定 -PluginPath（要发布的插件目录）"
}
if (-not [System.IO.Path]::IsPathRooted($PluginPath)) {
    $PluginPath = Join-Path $repoRoot $PluginPath
}
$PluginPath = [System.IO.Path]::GetFullPath($PluginPath)

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoRoot "dist\market"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path $repoRoot $OutputDir
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

if ([string]::IsNullOrWhiteSpace($IndexPath)) {
    $IndexPath = Join-Path $OutputDir "market.json"
} elseif (-not [System.IO.Path]::IsPathRooted($IndexPath)) {
    $IndexPath = Join-Path $repoRoot $IndexPath
}
$IndexPath = [System.IO.Path]::GetFullPath($IndexPath)

$manifestPath = Join-Path $PluginPath "plugin.json"
if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "找不到 plugin.json：$manifestPath"
}

Write-Step "读取 manifest：$manifestPath"
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ([string]::IsNullOrWhiteSpace($manifest.id)) {
    throw "manifest 缺少 id"
}
$pluginId = [string]$manifest.id
$packageName = ($pluginId -split '\.')[-1] + ".neon"
$packagePath = Join-Path $OutputDir $packageName
Write-Ok "插件 id：$pluginId"

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

# ---- 收集插件文件 ----
$files = [ordered]@{}
$mainFile = if ($manifest.PSObject.Properties.Name -contains "main") { [string]$manifest.main } else { "main.qml" }
Get-ChildItem -LiteralPath $PluginPath -Recurse -File | Where-Object {
    $_.Name -ne "plugin.json"
} | ForEach-Object {
    $relative = $_.FullName.Substring($PluginPath.Length).TrimStart('\', '/') -replace '\\', '/'
    $files[$relative] = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8
}
if (-not $files.Contains($mainFile)) {
    throw "入口文件不存在：$mainFile"
}
Write-Ok "打包文件 $($files.Count) 个（含 $mainFile）"

# ---- 打成 .neon ----
$package = [ordered]@{
    manifest = $manifest
    files    = $files
}
$packageJson = $package | ConvertTo-Json -Depth 20
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($packagePath, ($packageJson -replace "`r`n", "`n"), $utf8NoBom)
$packageInfo = Get-Item -LiteralPath $packagePath
Write-Ok "写出 $packageName（$($packageInfo.Length) 字节）"

# ---- 算 sha256 ----
$hash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLower()

# ---- 组装索引条目 ----
$download = if ([string]::IsNullOrWhiteSpace($DownloadBase)) {
    $packageName
} else {
    $DownloadBase.TrimEnd('/') + "/" + $packageName
}

$entry = [ordered]@{
    id          = $pluginId
    name        = if ($manifest.PSObject.Properties.Name -contains "name") { [string]$manifest.name } else { $pluginId }
    version     = if ($manifest.PSObject.Properties.Name -contains "version") { [string]$manifest.version } else { "0.0.0" }
    description = if ($manifest.PSObject.Properties.Name -contains "description") { [string]$manifest.description } else { "" }
    author      = if ($manifest.PSObject.Properties.Name -contains "author") { [string]$manifest.author } else { "" }
    homepage    = if ($manifest.PSObject.Properties.Name -contains "homepage") { [string]$manifest.homepage } else { "" }
    download    = $download
    sha256      = $hash
    size        = $packageInfo.Length
}

# ---- 更新索引 ----
$signature = ""
if (-not [string]::IsNullOrWhiteSpace($SignatureFile)) {
    if (-not (Test-Path -LiteralPath $SignatureFile)) {
        throw "签名文件不存在：$SignatureFile"
    }
    $signature = (Get-Content -LiteralPath $SignatureFile -Raw -Encoding UTF8).Trim()
}

$index = $null
if (Test-Path -LiteralPath $IndexPath) {
    $index = Get-Content -LiteralPath $IndexPath -Raw -Encoding UTF8 | ConvertFrom-Json
}
if ($null -eq $index) {
    $index = [ordered]@{ version = 2; updatedAt = ""; signature = ""; plugins = @() }
}

$plugins = @()
if ($index.PSObject.Properties.Name -contains "plugins" -and $null -ne $index.plugins) {
    $plugins = @($index.plugins)
}
$kept = @()
foreach ($item in $plugins) {
    if ([string]$item.id -ne $pluginId) {
        $kept += $item
    }
}
$kept += [pscustomobject]$entry
$index.plugins = $kept
if ($index.PSObject.Properties.Name -contains "signature") {
    $index.signature = $signature
} else {
    Add-Member -InputObject $index -NotePropertyName "signature" -NotePropertyValue $signature
}
$index.updatedAt = (Get-Date).ToString("yyyy-MM-dd")
$index.version = 2

$indexDir = Split-Path -Parent $IndexPath
if (-not (Test-Path -LiteralPath $indexDir)) {
    New-Item -ItemType Directory -Path $indexDir -Force | Out-Null
}
$indexJson = $index | ConvertTo-Json -Depth 20
[System.IO.File]::WriteAllText($IndexPath, ($indexJson -replace "`r`n", "`n"), $utf8NoBom)
Write-Ok "更新索引 $IndexPath（共 $($kept.Count) 个插件）"

# ---- 清理旧包 ----
if ($RemoveStale) {
    $keep = $kept | ForEach-Object { $_.id } | ForEach-Object { ($_ -split '\.')[-1] + ".neon" }
    Get-ChildItem -LiteralPath $OutputDir -Filter "*.neon" -File | Where-Object {
        $keep -notcontains $_.Name
    } | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Force
        Write-Warn "删除过期包 $($_.Name)"
    }
}

Write-Step "完成"
Write-Host "    包：$packagePath"
Write-Host "    sha256：$hash"
Write-Host "    download：$download"
