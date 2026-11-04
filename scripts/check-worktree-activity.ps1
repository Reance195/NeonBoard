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

.PARAMETER Audit
    逐区审计「这个 worktree 现在能不能删」：未推送提交 / 未提交改动 / 是否 detached，
    三项全 0 才标「可删」，并附上占用登记的 agent 与 since 时间戳。
    同时列出「登记还在但目录已删」的僵尸占用。
    只报告不删除（删除不可逆，判定可以自动，决定权不自动），恒退出码 0。

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
    [switch]$Json,
    [switch]$Audit
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

# ---------- -Audit：逐区回答「这个能不能删」 ----------
# 默认模式只答「多久没动了」，答不了「能不能删」。要删之前必须确认三件事，全是可判定的：
# 有没有未推送的提交、有没有没提交的改动、是不是停在 detached HEAD 上。
# 归属同样可判定：agent-workspace.json 里每条登记都带 since，不靠回忆。
if ($Audit) {
    $currentRoot = $PWD.Path

    # 「主区」不能拿 $RepoRoot 代替：本脚本自己就住在某个 worktree 里时，
    # $RepoRoot 是那个工作区的根，主区判断（以及 .worktrees/ 的拼法）会全错。
    # 主区 = git common dir 的父目录，与脚本所在位置无关。
    $commonDir = & git -C $RepoRoot rev-parse --git-common-dir 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $commonDir) {
        Write-Error "git rev-parse --git-common-dir 失败"
        exit 2
    }
    if (-not [System.IO.Path]::IsPathRooted($commonDir)) {
        $commonDir = Join-Path $RepoRoot $commonDir
    }
    $mainRoot = Split-Path -Parent ([System.IO.Path]::GetFullPath($commonDir))

    function Get-NormPath {
        param([string]$In)
        if (-not $In) { return '' }
        try { return ([System.IO.Path]::GetFullPath($In)).TrimEnd('\', '/').ToLowerInvariant() }
        catch { return $In.TrimEnd('\', '/').ToLowerInvariant() }
    }
    $mainNorm = Get-NormPath $mainRoot

    # 占用登记放在 git common dir（所有 worktree 共享一份）
    $registry = @{}
    $regPath = Join-Path $commonDir 'agent-workspace.json'
    if (Test-Path -LiteralPath $regPath) {
        try {
            $raw = Get-Content -LiteralPath $regPath -Raw -Encoding UTF8 | ConvertFrom-Json
            foreach ($prop in $raw.PSObject.Properties) {
                if ($prop.Value -is [System.Management.Automation.PSCustomObject]) {
                    $registry[$prop.Name] = $prop.Value
                }
            }
        } catch {
            Write-Host "  ! 占用登记解析失败：$($_.Exception.Message)" -ForegroundColor Yellow
        }
    }

    Write-Host ""
    Write-Host "worktree 可删性审计（$($Worktrees.Count) 个区，主区 $mainRoot）" -ForegroundColor Magenta
    Write-Host ""

    foreach ($wt in $Worktrees) {
        $path = $wt.Path
        $isMain = ((Get-NormPath $path) -eq $mainNorm)
        $isCurrent = ((Get-NormPath $path) -eq (Get-NormPath $currentRoot))
        $leaf = Split-Path -Leaf $path
        $regKey = "worktree:$leaf"
        $entry = $registry[$regKey]

        $branchName = (& git -C $path symbolic-ref --short HEAD 2>$null)
        $detached = ($LASTEXITCODE -ne 0) -or (-not $branchName)
        if ($detached) {
            $shortHead = (& git -C $path rev-parse --short HEAD 2>$null)
            $branchLabel = "detached @$shortHead"
        } else {
            $branchLabel = $branchName
        }

        $unpushed = 0
        $revOut = & git -C $path rev-list --count HEAD --not --remotes=origin 2>$null
        if ($LASTEXITCODE -eq 0 -and $revOut) { $unpushed = [int]$revOut }

        $dirty = 0
        $statusOut = @(& git -C $path status --porcelain 2>$null)
        if ($LASTEXITCODE -eq 0) { $dirty = @($statusOut | Where-Object { $_ }).Count }

        if ($isMain) {
            $verdict = '主区（不参与删除）'
            $verdictColor = 'DarkGray'
        } elseif ($unpushed -eq 0 -and $dirty -eq 0 -and -not $detached) {
            $verdict = '可删'
            $verdictColor = 'Green'
        } else {
            $blockers = @()
            if ($unpushed -gt 0) { $blockers += "未推送 $unpushed" }
            if ($dirty -gt 0) { $blockers += "未提交 $dirty" }
            if ($detached) { $blockers += 'detached' }
            $verdict = '保留（' + ($blockers -join ' / ') + '）'
            $verdictColor = 'Yellow'
        }

        $sinceText = ''
        if ($entry -and $entry.since) {
            # ConvertFrom-Json 会把 ISO 时间串转成 DateTime，直接 (string) 会得到区域化格式
            if ($entry.since -is [datetime]) { $sinceText = $entry.since.ToString('yyyy-MM-dd HH:mm') }
            else { $sinceText = [string]$entry.since }
        }

        Write-Host ("  {0}" -f $path) -ForegroundColor Cyan
        if ($entry) {
            Write-Host ("    登记 {0}（agent={1}，自 {2}）" -f $regKey, $entry.agent, $sinceText) -ForegroundColor DarkGray
        } else {
            Write-Host ("    登记 无（目录名 {0} 未在 agent-workspace.json 里）" -f $leaf) -ForegroundColor DarkGray
        }
        Write-Host ("    分支 {0}   未推送 {1}   脏 {2}" -f $branchLabel, $unpushed, $dirty) -ForegroundColor Gray
        $suffix = if ($isCurrent) { '（当前目录）' } else { '' }
        Write-Host ("    → {0}{1}" -f $verdict, $suffix) -ForegroundColor $verdictColor

        if ($verdict -eq '可删') {
            Write-Host ("      git worktree remove --force `"{0}`"" -f $path) -ForegroundColor DarkGray
        }
        Write-Host ""
    }

    # 僵尸登记：登记还在、目录已删。这正是 DX-21 之前 git-done 留下的那类残留。
    $zombies = @()
    foreach ($key in $registry.Keys) {
        if ($key -notlike 'worktree:*') { continue }
        $agentName = $key.Substring('worktree:'.Length)
        $expected = Join-Path (Join-Path $mainRoot '.worktrees') $agentName
        if (-not (Test-Path -LiteralPath $expected)) {
            $z = $registry[$key]
            $zSince = ''
            if ($z.since) {
                if ($z.since -is [datetime]) { $zSince = $z.since.ToString('yyyy-MM-dd HH:mm') }
                else { $zSince = [string]$z.since }
            }
            $zombies += [pscustomobject]@{ Key = $key; Agent = $agentName; Since = $zSince }
        }
    }
    if ($zombies.Count -gt 0) {
        Write-Host ("僵尸登记（目录已删、登记还在）：{0} 条" -f $zombies.Count) -ForegroundColor Yellow
        foreach ($z in $zombies) {
            Write-Host ("  {0}   agent={1}   since {2}" -f $z.Key, $z.Agent, $z.Since) -ForegroundColor Yellow
            Write-Host ("    pwsh -File scripts/agent-workspace.ps1 -Action Release -Agent {0} -Scope {1}" -f $z.Agent, $z.Key) -ForegroundColor DarkGray
        }
        Write-Host ""
    } else {
        Write-Host "✓ 无僵尸登记" -ForegroundColor Green
        Write-Host ""
    }

    exit 0
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
