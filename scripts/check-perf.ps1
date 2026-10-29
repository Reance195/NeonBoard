# check-perf.ps1 —— 从 --smoke 日志里提取性能指标并与基线比较
#
# --smoke 会在退出前打印一行：METRICS startup=1234ms fps=60.0 mem=180MB frames=240 cpu=700ms
# 本脚本解析它，写入 GitHub Step Summary，并在超过基线阈值时给出警告。
#
# 达标门是 cpu（首帧 CPU 时间）：应用可控、跨环境稳定（R0-11 验收 ≤1000ms）。
# 墙钟 startup 受 CI runner 的 Defender 冷扫主导、非应用可控，只作信息性趋势告警。
#
# 用法：
#   pwsh -File scripts/check-perf.ps1 -LogPath <日志>
#   pwsh -File scripts/check-perf.ps1 -LogPath <日志> -Update   # 用本次测量值刷新基线

param(
    [Parameter(Mandatory = $true)][string]$LogPath,
    [string]$BaselinePath = '',
    [double]$Tolerance = 1.5,
    [switch]$Update
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BaselinePath) { $BaselinePath = Join-Path $repoRoot 'docs/perf-baseline.json' }

if (-not (Test-Path $LogPath)) { throw "找不到日志：$LogPath" }

# ---- 解析 METRICS ----
$line = Get-Content $LogPath -Encoding UTF8 | Where-Object { $_ -match 'METRICS' } | Select-Object -Last 1
if (-not $line) {
    Write-Host '::warning::日志里没有 METRICS 行（--smoke 未跑完或格式变了），跳过性能校验'
    exit 0
}

$startupMs = $null; $fps = $null; $memMb = $null; $cpuMs = $null
if ($line -match 'startup=(\d+)ms') { $startupMs = [int]$matches[1] }
if ($line -match 'fps=([\d.]+)')     { $fps = [double]$matches[1] }
if ($line -match 'mem=(\d+)MB')      { $memMb = [int]$matches[1] }
if ($line -match 'cpu=(\d+)ms')      { $cpuMs = [int]$matches[1] }

if ($null -eq $startupMs) {
    Write-Host "::warning::METRICS 行无法解析 startup：$line"
    exit 0
}

Write-Host ("本次测量：cpu={0}ms startup={1}ms fps={2} mem={3}MB" -f $cpuMs, $startupMs, $fps, $memMb)

# ---- 基线 ----
$baseline = $null
if (Test-Path $BaselinePath) {
    $baseline = Get-Content $BaselinePath -Raw | ConvertFrom-Json
}

if (-not $baseline -or $Update) {
    $data = [ordered]@{
        smokeCpuMs     = $cpuMs
        smokeStartupMs = $startupMs
        smokeFps       = $fps
        smokeMemMb     = $memMb
        updatedAt      = (Get-Date -Format 'yyyy-MM-dd')
        note           = 'CI --smoke 基线；达标门是 cpu（应用可控），墙钟 startup 受环境 Defender 冷扫主导仅作趋势。波动超过 tolerance 会告警。用 -Update 刷新。'
    }
    $data | ConvertTo-Json | Set-Content -Path $BaselinePath -Encoding utf8
    Write-Host "已写入基线：$BaselinePath（cpu=${cpuMs}ms startup=${startupMs}ms）"
    exit 0
}

# ---- 达标门：cpu（首帧 CPU 时间，应用可控） ----
$hasCpuBase = $null -ne $baseline.PSObject.Properties['smokeCpuMs'] -and $null -ne $baseline.smokeCpuMs
$cpuGate = $false
$baseCpu = 0; $cpuLimit = 0; $cpuDelta = 0
if ($hasCpuBase -and $null -ne $cpuMs) {
    $baseCpu = [int]$baseline.smokeCpuMs
    $cpuLimit = [math]::Round($baseCpu * $Tolerance)
    $cpuDelta = $cpuMs - $baseCpu
    $cpuGate = $true
}

# ---- 趋势：墙钟 startup（受环境 Defender 冷扫主导，非达标门） ----
$baseMs = [int]$baseline.smokeStartupMs
$limit = [math]::Round($baseMs * $Tolerance)
$delta = $startupMs - $baseMs
$deltaPct = if ($baseMs -gt 0) { [math]::Round(100.0 * $delta / $baseMs, 1) } else { 0 }

$summary = @(
    "### 性能基线（--smoke）",
    "",
    "| 指标 | 本次 | 基线 | 变化 |",
    "| --- | --- | --- | --- |",
    ("| cpu（达标门） | {0} ms | {1} ms | {2} |" -f $cpuMs, $(if ($cpuGate) { $baseCpu } else { '-' }), $(if ($cpuGate) { '{0}{1} ms' -f $(if ($cpuDelta -ge 0) { '+' } else { '' }), $cpuDelta } else { '-' })),
    ("| startup（趋势） | {0} ms | {1} ms | {2}{3} ms ({4}%) |" -f $startupMs, $baseMs, $(if ($delta -ge 0) { '+' } else { '' }), $delta, $deltaPct),
    ("| fps | {0} | {1} | - |" -f $fps, $baseline.smokeFps),
    ("| mem | {0} MB | {1} MB | - |" -f $memMb, $baseline.smokeMemMb)
)
if ($env:GITHUB_STEP_SUMMARY) {
    $summary | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
}

if ($cpuGate) {
    if ($cpuMs -gt $cpuLimit) {
        Write-Host ("::warning::首帧 CPU {0}ms 超过基线 {1}ms 的 {2} 倍（阈值 {3}ms，验收上限 1000ms），疑似应用侧性能回退" -f $cpuMs, $baseCpu, $Tolerance, $cpuLimit)
    } else {
        Write-Host ("cpu 达标门通过（{0}ms / 阈值 {1}ms）" -f $cpuMs, $cpuLimit) -ForegroundColor Green
    }
} else {
    Write-Host '::warning::METRICS 无 cpu 或基线缺 smokeCpuMs，跳过达标门（仅比趋势）'
}

if ($startupMs -gt $limit) {
    Write-Host ("::warning::墙钟启动 {0}ms 超过趋势基线 {1}ms 的 {2} 倍（阈值 {3}ms）——受环境 Defender 冷扫主导，非达标门，仅供趋势参考" -f $startupMs, $baseMs, $Tolerance, $limit)
} else {
    Write-Host ("墙钟启动在趋势基线范围内（{0}ms / 阈值 {1}ms）" -f $startupMs, $limit) -ForegroundColor Green
}
exit 0
