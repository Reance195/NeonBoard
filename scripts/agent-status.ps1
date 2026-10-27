#Requires -Version 5.1
<#
.SYNOPSIS
    Agent 状态汇总脚本：整合 worktree 活跃度、当前分支、待办事项。

.DESCRIPTION
    为 AI agent 提供一站式状态查询，内部调用：
    - check-worktree-activity.ps1：检查其他 worktree 是否活跃
    - git status：当前工作区状态
    - git log：最近提交
    
    用途：agent 在汇报状态或决定是否干预其他 worktree 前，先跑这个脚本，
    只关注不活跃的 worktree，避免干扰正在工作的 agent。

.PARAMETER ThresholdMinutes
    worktree 活跃度阈值（分钟），默认 30。

.PARAMETER IncludeActive
    是否包含活跃的 worktree（默认只报告不活跃的）。

.EXAMPLE
    pwsh -File scripts/agent-status.ps1
    # 汇总当前状态，只报告超过 30 分钟未活动的 worktree

.EXAMPLE
    pwsh -File scripts/agent-status.ps1 -ThresholdMinutes 60 -IncludeActive
    # 阈值 60 分钟，包含所有 worktree
#>

[CmdletBinding()]
param(
    [int]$ThresholdMinutes = 30,
    [switch]$IncludeActive
)

$ErrorActionPreference = 'Stop'

# 定位仓库根目录
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $RepoRoot '.git'))) {
    Write-Error "无法定位仓库根目录：$RepoRoot"
    exit 2
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " Agent 状态汇总" -ForegroundColor Cyan
Write-Host " $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Gray
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ---- 1. 当前分支与最近提交 ----
Write-Host "【当前工作区】" -ForegroundColor Yellow
$CurrentBranch = & git -C $RepoRoot branch --show-current
$LastCommit = & git -C $RepoRoot log -1 --format="%h %s (%cr)"
Write-Host "  分支: $CurrentBranch" -ForegroundColor White
Write-Host "  最新: $LastCommit" -ForegroundColor Gray
Write-Host ""

# ---- 2. 工作区状态 ----
Write-Host "【未提交改动】" -ForegroundColor Yellow
$StatusShort = & git -C $RepoRoot status --porcelain
if ([string]::IsNullOrWhiteSpace($StatusShort)) {
    Write-Host "  ✓ 工作区干净" -ForegroundColor Green
}
else {
    $LineCount = ($StatusShort -split "`n").Count
    Write-Host "  ⚠ $LineCount 个文件有改动" -ForegroundColor Yellow
    $StatusShort | Select-Object -First 5 | ForEach-Object {
        Write-Host "    $_" -ForegroundColor Gray
    }
    if ($LineCount -gt 5) {
        Write-Host "    ... 还有 $($LineCount - 5) 个" -ForegroundColor Gray
    }
}
Write-Host ""

# ---- 3. Worktree 活跃度 ----
Write-Host "【其他 Worktree 活跃度】（阈值: $ThresholdMinutes 分钟）" -ForegroundColor Yellow

$ActivityScript = Join-Path $PSScriptRoot 'check-worktree-activity.ps1'
if (-not (Test-Path $ActivityScript)) {
    Write-Host "  ⚠ 找不到 check-worktree-activity.ps1，跳过" -ForegroundColor Yellow
}
else {
    $ActivityResult = & pwsh -File $ActivityScript -ThresholdMinutes $ThresholdMinutes -Json 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  ⚠ 活跃度检查失败" -ForegroundColor Yellow
    }
    else {
        $Activity = $ActivityResult | ConvertFrom-Json
        
        if ($Activity.StaleWorktrees -eq 0) {
            Write-Host "  ✓ 所有 worktree 均在 $ThresholdMinutes 分钟内有活动" -ForegroundColor Green
        }
        else {
            Write-Host "  ⚠ $($Activity.StaleWorktrees) 个 worktree 超过 $ThresholdMinutes 分钟未活动：" -ForegroundColor Yellow
            Write-Host ""
            
            foreach ($wt in $Activity.Details) {
                $WorktreeName = Split-Path $wt.Path -Leaf
                $BranchName = if ($wt.Branch -match 'refs/heads/(.+)') { $Matches[1] } else { $wt.Branch }
                
                Write-Host "    [$WorktreeName] $BranchName" -ForegroundColor Cyan
                Write-Host "      最后提交: $($wt.InactiveMinutes) 分钟前" -ForegroundColor Gray
                Write-Host "      HEAD: $($wt.HEAD.Substring(0, 7))" -ForegroundColor Gray
                
                # 判断是否可关注
                if ($wt.InactiveMinutes -gt 60) {
                    Write-Host "      状态: 可关注（超过 1 小时）" -ForegroundColor Yellow
                }
                elseif ($wt.InactiveMinutes -gt $ThresholdMinutes) {
                    Write-Host "      状态: 观察中（超过 $ThresholdMinutes 分钟）" -ForegroundColor Gray
                }
                Write-Host ""
            }
        }
        
        if ($IncludeActive -and $Activity.TotalWorktrees -gt $Activity.StaleWorktrees) {
            $ActiveCount = $Activity.TotalWorktrees - $Activity.StaleWorktrees
            Write-Host "  ℹ 另有 $ActiveCount 个活跃 worktree（未列出）" -ForegroundColor Gray
        }
    }
}
Write-Host ""

# ---- 4. 待合并分支 ----
Write-Host "【待合并分支】" -ForegroundColor Yellow
$Branches = & git -C $RepoRoot branch --format='%(refname:short) %(upstream:short) %(push:track)'
$Unmerged = @()
foreach ($branch in $Branches) {
    if ($branch -match '^(chore|feature|fix|hotfix|docs|refactor|perf|test|build|ci|style|revert|workbuddy)/') {
        $BranchName = $branch.Split(' ')[0]
        # 检查是否已合并到 main
        $MergedCheck = & git -C $RepoRoot branch --merged main --format='%(refname:short)' 2>$null
        if ($MergedCheck -notcontains $BranchName) {
            $Unmerged += $BranchName
        }
    }
}

if ($Unmerged.Count -eq 0) {
    Write-Host "  ✓ 无待合并分支" -ForegroundColor Green
}
else {
    Write-Host "  ⚠ $($Unmerged.Count) 个分支待合并：" -ForegroundColor Yellow
    foreach ($b in $Unmerged) {
        Write-Host "    - $b" -ForegroundColor Gray
    }
}
Write-Host ""

Write-Host "========================================" -ForegroundColor Cyan
Write-Host " 提示：只对超过阈值的 worktree 采取行动" -ForegroundColor Gray
Write-Host "========================================" -ForegroundColor Cyan
