#Requires -Version 5.1
<#
.SYNOPSIS
    检查所有 worktree 的最后活跃时间，报告超过阈值未活动的。

.DESCRIPTION
    遍历仓库下所有 worktree，检查每个 worktree 的最后一次提交时间。
    如果距离现在超过指定分钟数（默认 30 分钟），则报告为"可关注"。
    
    用途：多 agent 并行时，避免干扰正在工作的 agent，只关注可能卡住的任务。

.PARAMETER ThresholdMinutes
    判定为"不活跃"的阈值（分钟），默认 30。

.PARAMETER Json
    输出 JSON 格式（便于程序解析），默认人类可读文本。

.EXAMPLE
    pwsh -File scripts/check-worktree-activity.ps1
    # 检查所有 worktree，报告超过 30 分钟未活动的

.EXAMPLE
    pwsh -File scripts/check-worktree-activity.ps1 -ThresholdMinutes 60
    # 阈值改为 60 分钟

.EXAMPLE
    pwsh -File scripts/check-worktree-activity.ps1 -Json
    # 输出 JSON 格式
#>

[CmdletBinding()]
param(
    [int]$ThresholdMinutes = 30,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

# 定位仓库根目录（脚本自身在 scripts/ 下，上级即仓库根）
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $RepoRoot '.git'))) {
    Write-Error "无法定位仓库根目录：$RepoRoot"
    exit 2
}

$ThresholdSeconds = $ThresholdMinutes * 60
$Now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()

# 解析 git worktree list --porcelain
$WorktreeList = & git -C $RepoRoot worktree list --porcelain
if ($LASTEXITCODE -ne 0) {
    Write-Error "git worktree list 失败"
    exit 2
}

$Worktrees = @()
$CurrentWorktree = $null

foreach ($line in $WorktreeList) {
    if ($line -match '^worktree\s+(.+)$') {
        $CurrentWorktree = @{
            Path = $Matches[1].Trim()
            Branch = $null
            HEAD = $null
            LastCommitTime = $null
            InactiveSeconds = $null
            IsStale = $false
        }
        $Worktrees += $CurrentWorktree
    }
    elseif ($CurrentWorktree -and $line -match '^branch\s+(.+)$') {
        $CurrentWorktree.Branch = $Matches[1].Trim()
    }
    elseif ($CurrentWorktree -and $line -match '^HEAD\s+(.+)$') {
        $CurrentWorktree.HEAD = $Matches[1].Trim()
    }
}

# 对每个 worktree 检查最后提交时间
foreach ($wt in $Worktrees) {
    $wtPath = $wt.Path
    
    # 跳过主 worktree（锚点区）
    if ($wtPath -eq $RepoRoot) {
        continue
    }
    
    # 获取最后提交的时间戳
    $LastCommitTimestamp = & git -C $wtPath log -1 --format=%ct 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $LastCommitTimestamp) {
        # 没有提交记录的 worktree，跳过
        continue
    }
    
    $wt.LastCommitTime = [int]$LastCommitTimestamp
    $wt.InactiveSeconds = $Now - $wt.LastCommitTime
    $wt.IsStale = $wt.InactiveSeconds -gt $ThresholdSeconds
}

# 过滤出超过阈值的
$StaleWorktrees = $Worktrees | Where-Object { $_.IsStale }

# 输出
if ($Json) {
    $Output = @{
        ThresholdMinutes = $ThresholdMinutes
        CheckedAt = [DateTime]::UtcNow.ToString('o')
        TotalWorktrees = $Worktrees.Count
        StaleWorktrees = $StaleWorktrees.Count
        Details = $StaleWorktrees | ForEach-Object {
            @{
                Path = $_.Path
                Branch = $_.Branch
                HEAD = $_.HEAD
                LastCommitTime = $_.LastCommitTime
                InactiveSeconds = $_.InactiveSeconds
                InactiveMinutes = [math]::Round($_.InactiveSeconds / 60, 1)
            }
        }
    }
    $Output | ConvertTo-Json -Depth 10
}
else {
    if ($StaleWorktrees.Count -eq 0) {
        Write-Host "✓ 所有 worktree 均在 $ThresholdMinutes 分钟内有活动" -ForegroundColor Green
        exit 0
    }
    
    Write-Host "⚠ 发现 $($StaleWorktrees.Count) 个 worktree 超过 $ThresholdMinutes 分钟未活动：" -ForegroundColor Yellow
    Write-Host ""
    
    foreach ($wt in $StaleWorktrees) {
        $InactiveMinutes = [math]::Round($wt.InactiveSeconds / 60, 1)
        $LastCommitDate = [DateTimeOffset]::FromUnixTimeSeconds($wt.LastCommitTime).LocalDateTime.ToString('yyyy-MM-dd HH:mm:ss')
        
        Write-Host "  $($wt.Path)" -ForegroundColor Cyan
        Write-Host "    分支: $($wt.Branch)" -ForegroundColor Gray
        Write-Host "    HEAD: $($wt.HEAD)" -ForegroundColor Gray
        Write-Host "    最后提交: $LastCommitDate ($InactiveMinutes 分钟前)" -ForegroundColor Gray
        Write-Host ""
    }
    
    exit 1
}
