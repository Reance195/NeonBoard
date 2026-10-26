#requires -Version 5.1
<#
.SYNOPSIS
    生成 NeonBoard 的软件更新清单 update.json。

.DESCRIPTION
    计算安装包 / 便携压缩包的 SHA-256 与体积，写出客户端可读的更新清单。

    清单被消费的方式有两路，脚本两者都照顾到：
      1. 直接拉 update.json（推荐，可托管在 Gitee raw 保证国内可达）；
      2. 回退 GitHub Releases API —— 该接口不提供资产的 SHA-256，
         所以脚本会把 setup-sha256 / portable-sha256 两行附进更新说明，
         客户端从 Release 说明里抓回校验值，两路都能做完整性校验。

    -DownloadBase 留空时，url 只写文件名 —— 客户端会按清单所在目录解析，
    适合把 update.json 与产物放在同一处（自建源 / 本地测试）。

.EXAMPLE
    pwsh -File scripts/build-update-manifest.ps1 -Version 0.2.0 `
        -Setup dist\NeonBoard-Setup-0.2.0.exe `
        -Portable dist\NeonBoard-0.2.0-portable.zip `
        -DownloadBase https://github.com/Reance195/NeonBoard/releases/download/v0.2.0
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    # 安装包（Inno Setup 产物）；不存在则清单里不含 setup 条目
    [string]$Setup,
    # 便携版压缩包；不存在则清单里不含 portable 条目
    [string]$Portable,
    # 下载基址（如 https://host/releases/download/v0.2.0）；留空则只写文件名
    [string]$DownloadBase,
    [string]$OutFile = 'update.json',
    [string]$Channel = 'stable',
    [string]$Notes = '',
    [string]$NotesFile,
    [string]$HtmlUrl = ''
)

$ErrorActionPreference = 'Stop'

function Get-ArtifactEntry {
    param(
        [string]$Path,
        [string]$Kind
    )

    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    if (-not (Test-Path -LiteralPath $Path)) {
        Write-Warning "产物不存在，已跳过 $($Kind)：$Path"
        return $null
    }

    $item = Get-Item -LiteralPath $Path
    $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $name = $item.Name
    $url = if ($DownloadBase) { "$($DownloadBase.TrimEnd('/'))/$name" } else { $name }

    return [ordered]@{
        kind   = $Kind
        url    = $url
        sha256 = $hash
        size   = $item.Length
    }
}

if ([string]::IsNullOrWhiteSpace($Setup) -and [string]::IsNullOrWhiteSpace($Portable)) {
    throw 'Setup 与 Portable 至少要给一个，否则清单没有任何可下载产物。'
}

# 更新说明：优先用 -NotesFile（CI 里常见），其次 -Notes
$notesText = $Notes
if (-not [string]::IsNullOrWhiteSpace($NotesFile)) {
    if (-not (Test-Path -LiteralPath $NotesFile)) { throw "找不到更新说明文件：$NotesFile" }
    $notesText = Get-Content -LiteralPath $NotesFile -Raw -Encoding UTF8
}
$notesText = ($notesText | Out-String).Trim()

[object[]]$artifacts = @()
$setupEntry = Get-ArtifactEntry -Path $Setup -Kind 'setup'
if ($setupEntry) { $artifacts += $setupEntry }
$portableEntry = Get-ArtifactEntry -Path $Portable -Kind 'portable'
if ($portableEntry) { $artifacts += $portableEntry }

if ($artifacts.Count -eq 0) { throw '没有任何产物被写入清单（路径都不存在）。' }

# 回退到 Releases API 时，客户端从说明里抓校验值 —— 这里把两行附上去
$hashLines = @()
if ($setupEntry) { $hashLines += "setup-sha256: $($setupEntry.sha256)" }
if ($portableEntry) { $hashLines += "portable-sha256: $($portableEntry.sha256)" }
if ($hashLines.Count -gt 0) {
    $notesText = if ([string]::IsNullOrWhiteSpace($notesText)) {
        $hashLines -join "`n"
    } else {
        ($notesText + "`n`n" + ($hashLines -join "`n"))
    }
}

$manifest = [ordered]@{
    version     = 1
    channel     = $Channel
    latest      = $Version
    publishedAt = (Get-Date).ToString('yyyy-MM-dd')
    notes       = $notesText
    htmlUrl     = $HtmlUrl
    artifacts   = $artifacts
}

$json = $manifest | ConvertTo-Json -Depth 6

# 防御：PowerShell 5.1 的 ConvertTo-Json 会把单元素数组塌成对象，
# 客户端解析时 artifacts 就不是数组了 —— 宁可失败也不要产出坏清单。
$probe = $json | ConvertFrom-Json
if ($null -eq $probe.artifacts -or -not ($probe.artifacts -is [System.Array])) {
    throw "生成的清单里 artifacts 不是数组，已中止写出：$OutFile"
}

$outDir = Split-Path -Parent $OutFile
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}
# 无 BOM 的 UTF-8：Qt 的 QJsonDocument::fromJson 不吃 BOM
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$fullOut = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutFile)
[System.IO.File]::WriteAllText($fullOut, $json, $utf8NoBom)

Write-Host "更新清单已生成：$fullOut"
Write-Host "  版本     : $Version（$Channel）"
foreach ($a in $artifacts) {
    $mb = [math]::Round($a.size / 1MB, 2)
    $shortHash = $a.sha256.Substring(0, 16) + '...'
    $line = '  {0,-9}: {1}  {2} MB' -f $a.kind, $shortHash, $mb
    Write-Host $line
    Write-Host "             $($a.url)"
}
