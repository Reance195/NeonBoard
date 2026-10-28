<#
    NeonBoard 多代理工作区占用登记

    共享状态放在 git common dir（所有 worktree 共享同一份），文件 agent-workspace.json。
    三类占用：
      root            —— 锚点区（仓库根目录）的开发占用；`agent-init -ForceAnchor` 时自动登记
      worktree:<名>   —— `.worktrees/<名>` 开发区的占用；agent-init 开区自动登记，git-done 收尾自动释放
      release         —— 发布锁，独占 dist/ 与安装包输出，避免两个 agent 同时覆盖产物

    用法：
      pwsh -File scripts/agent-workspace.ps1 -Action Status
      pwsh -File scripts/agent-workspace.ps1 -Action Check          # 隔离自检（DX-05 验收项）
      pwsh -File scripts/agent-workspace.ps1 -Action Claim -Agent agent-c -Scope worktree:agent-c
      pwsh -File scripts/agent-workspace.ps1 -Action Claim  -Agent agent-c -Scope root -Task "阶段条修复"
      pwsh -File scripts/agent-workspace.ps1 -Action Release -Agent agent-c -Scope root
#>
[CmdletBinding()]
param(
    [ValidateSet('Claim', 'Release', 'Status', 'Check')]
    [string]$Action = 'Status',
    [string]$Agent = '',
    # root / release / worktree:<agent 名>（DX-05：开发区占用按 agent 名登记）
    [ValidatePattern('^(root|release|worktree:[A-Za-z0-9._-]+)$')]
    [string]$Scope = 'root',
    [string]$Task = '',
    [int]$StaleMinutes = 240
)

$ErrorActionPreference = 'Stop'

function Get-StatePath {
    $dir = (& git rev-parse --path-format=absolute --git-common-dir 2>$null | Select-Object -First 1)
    if (-not $dir) { throw '当前目录不是 Git 仓库，无法登记工作区占用' }
    return (Join-Path $dir.Trim() 'agent-workspace.json')
}

function Read-State {
    $path = Get-StatePath
    # 键不写死：除 root / release 外还有 worktree:<agent>，写死就会在读写时把别人的登记丢掉
    $state = @{}
    $state['root'] = $null
    $state['release'] = $null
    if (-not (Test-Path -LiteralPath $path)) { return $state }
    try {
        $raw = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
        if ([string]::IsNullOrWhiteSpace($raw)) { return $state }
        $obj = ConvertFrom-Json -InputObject $raw
        foreach ($key in @($obj.PSObject.Properties.Name)) {
            $state[$key] = $obj.$key
        }
    } catch {
        # 状态文件损坏时按空状态处理，不阻断流程
    }
    return $state
}

function Write-State {
    param([Parameter(Mandatory)]$State)
    $path = Get-StatePath
    $json = $State | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText($path, $json, (New-Object System.Text.UTF8Encoding($false)))
}

function Invoke-WithStateLock {
    param([Parameter(Mandatory)][scriptblock]$Body)
    $path = Get-StatePath
    $lockPath = "$path.lock"
    $stream = $null
    for ($i = 0; $i -lt 50; $i++) {
        try {
            $stream = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None')
            break
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    try {
        & $Body
    } finally {
        if ($stream) { $stream.Close(); $stream.Dispose() }
        try { [System.IO.File]::Delete($lockPath) } catch { }
    }
}

function Get-EntryAgeMinutes {
    param($Entry)
    $since = [datetime]::MinValue
    [void][datetime]::TryParse([string]$Entry.since, [ref]$since)
    if ($since -eq [datetime]::MinValue) { return [double]::PositiveInfinity }
    return ((Get-Date) - $since).TotalMinutes
}

function Get-PreviewRuntime {
    $dir = (& git rev-parse --path-format=absolute --git-common-dir 2>$null | Select-Object -First 1)
    if (-not $dir) { return $null }
    $path = Join-Path $dir.Trim() 'preview-runtime.json'
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try {
        $raw = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
        if ([string]::IsNullOrWhiteSpace($raw)) { return $null }
        return (ConvertFrom-Json -InputObject $raw)
    } catch {
        return $null
    }
}

# 检出里的 .agent-env.ps1 是「谁在用这份检出」的物证（agent-init 生成）。
function Get-AgentEnvId {
    param([string]$File)
    if (-not (Test-Path -LiteralPath $File)) { return '' }
    try {
        $text = [System.IO.File]::ReadAllText($File, [System.Text.Encoding]::UTF8)
    } catch {
        return ''
    }
    $m = [regex]::Match($text, '(?m)^\s*\$env:NEONBOARD_AGENT_ID\s*=\s*''([^'']*)''')
    if ($m.Success) { return $m.Groups[1].Value.Trim() }
    return ''
}

# `git worktree list --porcelain` 的第一块永远是锚点区（主 worktree）。
function Get-WorktreeList {
    $blocks = ((& git worktree list --porcelain 2>$null) -join "`n") -split "`n`n"
    $list = @()
    foreach ($block in $blocks) {
        $mPath = [regex]::Match($block, '(?m)^worktree (.+)$')
        if (-not $mPath.Success) { continue }
        $mHead = [regex]::Match($block, '(?m)^HEAD (.+)$')
        $mBranch = [regex]::Match($block, '(?m)^branch refs/heads/(.+)$')
        $list += [pscustomobject]@{
            Path     = $mPath.Groups[1].Value.Trim()
            Head     = if ($mHead.Success) { $mHead.Groups[1].Value.Trim() } else { '' }
            Branch   = if ($mBranch.Success) { $mBranch.Groups[1].Value.Trim() } else { '' }
            Detached = [bool][regex]::IsMatch($block, '(?m)^detached$')
        }
    }
    return $list
}

function Get-WorktreeScopeName {
    param([string]$Path)
    if ($Path -match '[\\/]\.worktrees[\\/]([^\\/]+)$') { return $Matches[1] }
    return ''
}

if (-not $Agent) { $Agent = $env:NEONBOARD_AGENT_ID }

switch ($Action) {
    'Status' {
        $state = Read-State
        Write-Host '工作区占用状态：' -ForegroundColor DarkGray
        $keys = @('root', 'release') + @($state.Keys | Where-Object { $_ -like 'worktree:*' } | Sort-Object)
        foreach ($key in $keys) {
            $entry = $state[$key]
            if (-not $entry) {
                Write-Host ("  {0,-20} : 空闲" -f $key) -ForegroundColor DarkGray
                continue
            }
            $age = [int](Get-EntryAgeMinutes $entry)
            Write-Host ("  {0,-20} : {1}  (pid {2}, {3} 分钟前)  {4}" -f $key, $entry.agent, $entry.pid, $age, $entry.task) -ForegroundColor Yellow
        }
        Write-Host ("  状态文件 : {0}" -f (Get-StatePath)) -ForegroundColor DarkGray

        # 预览实例登记（由 PreviewHub 写入，这里只做展示，不重复解析逻辑）
        $runtime = Get-PreviewRuntime
        $agents = $null
        if ($runtime -and ($runtime.PSObject.Properties.Name -contains 'agents')) { $agents = $runtime.agents }
        if ($agents) {
            Write-Host '预览实例：' -ForegroundColor DarkGray
            foreach ($prop in $agents.PSObject.Properties) {
                $entry = $prop.Value
                Write-Host ("  {0,-10} : pid {1}  {2}  {3}" -f $prop.Name, $entry.pid, $entry.startedAt, $entry.exe) -ForegroundColor Cyan
            }
        } else {
            Write-Host '  (暂无运行中的预览实例)' -ForegroundColor DarkGray
        }
        exit 0
    }

    'Check' {
        # 隔离自检（DX-05 验收）：锚点区 / 各工作区 / 占用登记三者必须自洽。
        # 硬失败（exit 3）只留给「两个会话真的在同一份检出里」这类会互相覆盖的情形；
        # 登记缺项与僵尸占用属于卫生问题，只告警，不拦流程。
        $problems = New-Object System.Collections.Generic.List[string]
        $warnings = New-Object System.Collections.Generic.List[string]
        $state = Read-State
        $worktrees = Get-WorktreeList
        if ($worktrees.Count -eq 0) {
            Write-Host '✗ 读不到任何 worktree（不在 Git 仓库里？）' -ForegroundColor Red
            exit 3
        }
        $anchor = $worktrees[0].Path

        # 1) 锚点区里有他人 in-place 环境 = 两个会话共享 HEAD 与 .sandbox（2026-10-28 凌晨的原现场景）
        $anchorEnvFile = Join-Path $anchor '.agent-env.ps1'
        $anchorEnvId = Get-AgentEnvId -File $anchorEnvFile
        $rootEntry = $state['root']
        if ($anchorEnvId) {
            if (-not $rootEntry) {
                $problems.Add(("锚点区存在 in-place 环境 '{0}'，但没有任何 root 占用登记（应由 agent-init -ForceAnchor 自动登记）" -f $anchorEnvId))
            } elseif ([string]$rootEntry.agent -ne $anchorEnvId) {
                $problems.Add(("锚点区 in-place 环境属 '{0}'，root 占用却登记为 '{1}'" -f $anchorEnvId, $rootEntry.agent))
            }
        } elseif ($rootEntry) {
            $warnings.Add(("root 占用登记 '{0}' 仍在，但锚点区没有 in-place 环境（可能是忘了释放）" -f $rootEntry.agent))
        }

        # 2) 同一分支被两个检出同时占用（Git 天然禁止，仍断言一次，避免被手工操作绕过）
        foreach ($group in @($worktrees | Where-Object { $_.Branch } | Group-Object Branch | Where-Object { $_.Count -gt 1 })) {
            $problems.Add(("分支 '{0}' 被 {1} 个检出同时占用" -f $group.Name, $group.Count))
        }

        # 3) 每个 .worktrees/<名> 必须有独立 .sandbox，且登记过占用
        $liveNames = @()
        foreach ($wt in $worktrees) {
            $name = Get-WorktreeScopeName -Path $wt.Path
            if (-not $name) { continue }
            $liveNames += $name
            if (-not (Test-Path -LiteralPath (Join-Path $wt.Path '.sandbox'))) {
                $problems.Add(("工作区 '{0}' 缺独立 .sandbox（运行时数据会与别人共用）" -f $name))
            }
            if (-not $state.ContainsKey("worktree:$name")) {
                $warnings.Add(("工作区 '{0}' 没有占用登记（旧工作区，或没走 agent-init）" -f $name))
            }
        }

        # 4) 目录已消失的 worktree 登记 = 僵尸占用
        foreach ($key in @($state.Keys)) {
            if ($key -notlike 'worktree:*') { continue }
            $name = $key.Substring('worktree:'.Length)
            if ($liveNames -notcontains $name) {
                $warnings.Add(("占用登记 '{0}' 对应的目录已不存在（僵尸占用，建议 agent-workspace.ps1 -Action Release 释放）" -f $key))
            }
        }

        Write-Host ("锚点区：{0}" -f $anchor) -ForegroundColor DarkGray
        Write-Host ("工作区：{0} 个（.worktrees {1} 个）" -f $worktrees.Count, @($liveNames).Count) -ForegroundColor DarkGray
        foreach ($w in $warnings) { Write-Host ("⚠ {0}" -f $w) -ForegroundColor Yellow }
        foreach ($p in $problems) { Write-Host ("✗ {0}" -f $p) -ForegroundColor Red }
        if ($problems.Count -gt 0) {
            Write-Host ("`n✗ 隔离自检未通过：{0} 项冲突 / {1} 项告警" -f $problems.Count, $warnings.Count) -ForegroundColor Red
            Write-Host '  独立开区：pwsh -File scripts/agent-init.ps1 -Agent <名> -Branch <分支> -Card <短号>' -ForegroundColor Yellow
            exit 3
        }
        Write-Host ("`n✓ 隔离自检通过：{0} 个检出各自独立，{1} 项告警" -f $worktrees.Count, $warnings.Count) -ForegroundColor Green
        exit 0
    }

    'Claim' {
        if (-not $Agent) { throw '必须提供 -Agent（或设置环境变量 NEONBOARD_AGENT_ID）' }
        $exitCode = 0
        Invoke-WithStateLock {
            $state = Read-State
            $current = $state[$Scope]
            if ($current -and $current.agent -ne $Agent) {
                $age = Get-EntryAgeMinutes $current
                if ($age -lt $StaleMinutes) {
                    Write-Host ("✗ {0} 已被 '{1}' 占用（{2}，{3} 分钟前）" -f $Scope, $current.agent, $current.task, [int]$age) -ForegroundColor Red
                    Write-Host '  改用独立 worktree，或等对方释放后再来。' -ForegroundColor Yellow
                    $script:exitCode = 3
                    return
                }
                Write-Host ("! {0} 的占用 '{1}' 已超过 {2} 分钟，按陈旧占用接管" -f $Scope, $current.agent, $StaleMinutes) -ForegroundColor Yellow
            }
            $state[$Scope] = [pscustomobject]@{
                agent = $Agent
                pid   = $PID
                since = (Get-Date).ToString('s')
                task  = $Task
            }
            Write-State -State $state
            Write-Host ("✓ 已登记 {0} 占用：{1}{2}" -f $Scope, $Agent, $(if ($Task) { "（$Task）" } else { '' })) -ForegroundColor Green
        }
        exit $exitCode
    }

    'Release' {
        if (-not $Agent) { throw '必须提供 -Agent（或设置环境变量 NEONBOARD_AGENT_ID）' }
        Invoke-WithStateLock {
            $state = Read-State
            $current = $state[$Scope]
            if (-not $current) {
                Write-Host ("· {0} 本来就是空闲的" -f $Scope) -ForegroundColor DarkGray
                return
            }
            if ($current.agent -ne $Agent) {
                Write-Host ("✗ {0} 的占用属于 '{1}'，不是 {2}，不予释放" -f $Scope, $current.agent, $Agent) -ForegroundColor Red
                exit 3
            }
            # 开发区登记直接删键，root / release 保留空位（老逻辑按属性名判断是否存在）
            if ($Scope -like 'worktree:*') { $state.Remove($Scope) } else { $state[$Scope] = $null }
            Write-State -State $state
            Write-Host ("✓ 已释放 {0} 占用（{1}）" -f $Scope, $Agent) -ForegroundColor Green
        }
        exit 0
    }
}
