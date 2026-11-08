# check-perf.ps1 —— 从 --smoke 聚合日志里提取 cpu 中位数并按 R0-11 绝对预算判定
#
# 判据：cpu（首帧 CPU 时间）中位数 ≤ cpuBudgetMs（R0-11 验收 ≤1000ms）。
# 旧的相对基线（单点采样 ×1.5）会被 -Update 用单次噪声覆盖，且墙钟 startup
# 基线 4000ms 与 CI 实测 6368~11381ms 脱节，趋势告警每轮必现，导致「连续若干轮
# 无回退告警」的转正条件永远无法达成。改用绝对预算 + 多次采样中位数：阈值是
# 常量、不随环境漂移，无需观察期（docs/pitfalls.md 4.8：CPU 时间免疫 I/O/调度噪声）。
#
# 墙钟 startup 只写 Step Summary，不产生告警、不参与判定。
#
# 用法：
#   pwsh -File scripts/check-perf.ps1 -LogPath <聚合日志>      # 判定，退出码 0/1/2
#   pwsh -File scripts/check-perf.ps1 -LogPath <日志> -Update   # 追加观测样本
#   pwsh -File scripts/check-perf.ps1 -SelfTest                  # 纯函数自测
#
# 退出码：0 = 达标 / 1 = cpu 中位数超预算 / 2 = 数据不足或无法解析（fail-closed）

[CmdletBinding()]
param(
    [string]$LogPath = '',
    [string]$BaselinePath = '',
    [switch]$Update,
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BaselinePath) { $BaselinePath = Join-Path $repoRoot 'docs/perf-baseline.json' }

# ---- 纯函数（判定与 --selftest 共用，Z5 单一实现）----
function Get-Median([double[]]$values) {
    $sorted = @($values | Sort-Object)
    $n = $sorted.Count
    if ($n -eq 0) { return $null }
    $mid = [int][math]::Floor($n / 2)
    if ($n % 2 -eq 1) { return [double]$sorted[$mid] }
    return ([double]$sorted[$mid - 1] + [double]$sorted[$mid]) / 2.0
}

function Get-Metrics([string[]]$lines) {
    # 一行 METRICS 一个样本；缺 cpu 的样本 Cpu=$null（老格式 exe）
    $out = @()
    foreach ($line in $lines) {
        if ($line -notmatch 'METRICS') { continue }
        $cpu = $null; $startup = $null; $fps = $null; $mem = $null
        if ($line -match 'cpu=(\d+)ms') { $cpu = [int]$matches[1] }
        if ($line -match 'startup=(\d+)ms') { $startup = [int]$matches[1] }
        if ($line -match 'fps=([\d.]+)') { $fps = [double]$matches[1] }
        if ($line -match 'mem=(\d+)MB') { $mem = [int]$matches[1] }
        $out += [pscustomobject]@{ Cpu = $cpu; Startup = $startup; Fps = $fps; Mem = $mem }
    }
    return $out
}

function Get-PerfVerdict([object[]]$samples, [int]$budget) {
    # Code: 0 达标 / 1 超预算 / 2 没有 METRICS 或全部缺 cpu（fail-closed）
    if ($samples.Count -eq 0) {
        return [pscustomobject]@{ Code = 2; Median = $null; Count = 0 }
    }
    $cpu = @($samples | Where-Object { $null -ne $_.Cpu } | ForEach-Object { [double]$_.Cpu })
    if ($cpu.Count -eq 0) {
        return [pscustomobject]@{ Code = 2; Median = $null; Count = 0 }
    }
    $median = Get-Median $cpu
    $code = if ($median -gt $budget) { 1 } else { 0 }
    return [pscustomobject]@{ Code = $code; Median = $median; Count = $cpu.Count }
}

function Get-CpuBudget([object]$baseline) {
    # 绝对预算来自 cpuBudgetMs；旧结构或缺文件回退 R0-11 上限 1000
    if ($baseline -and $baseline.PSObject.Properties['cpuBudgetMs'] -and $baseline.cpuBudgetMs) {
        return [int]$baseline.cpuBudgetMs
    }
    return 1000
}

function Invoke-SelfTest {
    $okLines = @(
        'METRICS startup=4000ms fps=60.0 mem=200MB frames=240 cpu=686ms'
        'METRICS startup=4200ms fps=60.0 mem=200MB frames=240 cpu=700ms'
        'METRICS startup=4100ms fps=59.8 mem=201MB frames=240 cpu=702ms'
    )
    $overLines = @(
        'METRICS startup=9000ms fps=60.0 mem=200MB frames=240 cpu=1200ms'
        'METRICS startup=9000ms fps=60.0 mem=200MB frames=240 cpu=1300ms'
        'METRICS startup=9000ms fps=60.0 mem=200MB frames=240 cpu=1400ms'
    )
    $samplesOk = @(Get-Metrics $okLines)
    $samplesOver = @(Get-Metrics $overLines)
    $samplesNoCpu = @(Get-Metrics @('METRICS startup=4000ms fps=60.0 mem=200MB frames=240'))
    $vOk = Get-PerfVerdict $samplesOk 1000
    $vOver = Get-PerfVerdict $samplesOver 1000
    $vNoCpu = Get-PerfVerdict $samplesNoCpu 1000
    $vEmpty = Get-PerfVerdict @() 1000
    $vEdge = Get-PerfVerdict (@(Get-Metrics @('METRICS startup=1ms fps=60.0 mem=1MB frames=1 cpu=1000ms'))) 1000
    $budgetNew = Get-CpuBudget ([pscustomobject]@{ cpuBudgetMs = 900 })
    $budgetOld = Get-CpuBudget ([pscustomobject]@{ smokeCpuMs = 700 })

    $checks = @()
    $checks += [pscustomobject]@{ Name = '中位数：奇数个'; Ok = ((Get-Median @(686, 700, 702)) -eq 700) }
    $checks += [pscustomobject]@{ Name = '中位数：偶数个'; Ok = ((Get-Median @(700, 702)) -eq 701) }
    $checks += [pscustomobject]@{ Name = '中位数：单样本'; Ok = ((Get-Median @(700)) -eq 700) }
    $checks += [pscustomobject]@{ Name = '中位数：空数组'; Ok = ($null -eq (Get-Median @())) }
    $checks += [pscustomobject]@{ Name = '解析：3 行 METRICS'; Ok = ($samplesOk.Count -eq 3) }
    $checks += [pscustomobject]@{ Name = '解析：cpu 值'; Ok = ($samplesOk[0].Cpu -eq 686 -and $samplesOk[2].Cpu -eq 702) }
    $checks += [pscustomobject]@{ Name = '解析：缺 cpu 记 null'; Ok = ($null -eq $samplesNoCpu[0].Cpu) }
    $checks += [pscustomobject]@{ Name = '判定：达标 Code=0'; Ok = ($vOk.Code -eq 0 -and $vOk.Median -eq 700 -and $vOk.Count -eq 3) }
    $checks += [pscustomobject]@{ Name = '判定：超预算 Code=1'; Ok = ($vOver.Code -eq 1 -and $vOver.Median -eq 1300) }
    $checks += [pscustomobject]@{ Name = '判定：边界等于预算不超'; Ok = ($vEdge.Code -eq 0) }
    $checks += [pscustomobject]@{ Name = '判定：全缺 cpu Code=2'; Ok = ($vNoCpu.Code -eq 2) }
    $checks += [pscustomobject]@{ Name = '判定：空样本 Code=2'; Ok = ($vEmpty.Code -eq 2) }
    $checks += [pscustomobject]@{ Name = '预算：新结构取 cpuBudgetMs'; Ok = ($budgetNew -eq 900) }
    $checks += [pscustomobject]@{ Name = '预算：旧结构回退 1000'; Ok = ($budgetOld -eq 1000) }

    $bad = @($checks | Where-Object { -not $_.Ok })
    foreach ($c in $checks) {
        if ($c.Ok) { Write-Host ('  ok   ' + $c.Name) -ForegroundColor DarkGray }
        else { Write-Host ('  FAIL ' + $c.Name) -ForegroundColor Red }
    }
    if ($bad.Count -gt 0) {
        Write-Host ("✗ check-perf selftest 失败 {0} 项" -f $bad.Count) -ForegroundColor Red
        return 1
    }
    Write-Host ("✓ check-perf selftest 通过：{0} 项断言" -f $checks.Count) -ForegroundColor Green
    return 0
}

if ($SelfTest) { exit (Invoke-SelfTest) }

if (-not $LogPath -or -not (Test-Path -LiteralPath $LogPath)) {
    Write-Host ("::error::找不到聚合日志：{0}（冒烟步骤应产出 METRICS 行）" -f $LogPath)
    exit 2
}

$lines = [System.IO.File]::ReadAllLines($LogPath, [System.Text.Encoding]::UTF8)
$samples = @(Get-Metrics $lines)

$baseline = $null
if (Test-Path -LiteralPath $BaselinePath) {
    $baseline = Get-Content -LiteralPath $BaselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
}
$budget = Get-CpuBudget $baseline
$verdict = Get-PerfVerdict $samples $budget

# ---- Step Summary：startup 仅为信息，不告警、不判定 ----
$startupVals = @($samples | Where-Object { $null -ne $_.Startup } | ForEach-Object { [double]$_.Startup })
$startupMedian = Get-Median $startupVals
$medianText = if ($null -ne $verdict.Median) { $verdict.Median } else { '-' }
$startupText = if ($null -ne $startupMedian) { $startupMedian } else { '-' }
$verdictText = switch ($verdict.Code) { 0 { '达标' } 1 { '超预算' } default { '数据不足' } }
$summary = @(
    '### 性能判定（--smoke，R0-11 绝对预算）'
    ''
    '| 指标 | 值 |'
    '| --- | --- |'
    ('| cpu 样本 | {0} 个 |' -f $verdict.Count)
    ('| cpu 中位数 | {0} ms |' -f $medianText)
    ('| 预算 cpuBudgetMs | {0} ms |' -f $budget)
    ('| 墙钟 startup 中位数（仅信息） | {0} ms |' -f $startupText)
    ('| 结论 | {0} |' -f $verdictText)
)
if ($env:GITHUB_STEP_SUMMARY) {
    $summary | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
}
$summary | ForEach-Object { Write-Host $_ }

if ($Update) {
    # 只追加观测样本，不改 cpuBudgetMs（绝对预算是契约，不随测量漂移）
    $history = @()
    if ($baseline -and $baseline.PSObject.Properties['samples']) {
        $history = @($baseline.samples)
    }
    if ($null -ne $verdict.Median) {
        $history += [pscustomobject]@{
            at          = (Get-Date -Format 'yyyy-MM-dd')
            cpuMedianMs = $verdict.Median
            count       = $verdict.Count
        }
    }
    $data = [ordered]@{
        cpuBudgetMs = $budget
        note        = 'R0-11 验收上限：--smoke 的 cpu 中位数 ≤ 1000ms。绝对预算，不是相对基线；墙钟 startup 仅作趋势观测、不参与判定（docs/pitfalls.md 4.8）。check-perf.ps1 用 cpuBudgetMs 作阈值；-Update 只追加 samples（滚动窗口，保留最近 20 条），不改预算。'
        samples     = @($history | Select-Object -Last 20)
        updatedAt   = (Get-Date -Format 'yyyy-MM-dd')
    }
    $data | ConvertTo-Json -Depth 5 | Set-Content -Path $BaselinePath -Encoding utf8
    Write-Host ("已追加观测样本：{0}" -f $BaselinePath)
    exit 0
}

switch ($verdict.Code) {
    0 {
        Write-Host ("cpu 达标（中位数 {0}ms / 预算 {1}ms / 样本 {2} 个）" -f $verdict.Median, $budget, $verdict.Count) -ForegroundColor Green
        exit 0
    }
    1 {
        Write-Host ("::error::首帧 CPU 中位数 {0}ms 超过 R0-11 预算 {1}ms（样本 {2} 个）" -f $verdict.Median, $budget, $verdict.Count)
        exit 1
    }
    default {
        Write-Host '::error::性能数据不足：没有 METRICS 行或全部缺 cpu（fail-closed）'
        exit 2
    }
}
