<#
    NeonBoard 多代理工作区占用登记

    共享状态放在 git common dir（所有 worktree 共享同一份），文件 agent-workspace.json。
    两类占用：
      root    —— 仓库根目录（锚点区）的开发占用，任何 agent 想在根目录动手前先登记
      release —— 发布锁，独占 dist/ 与安装包输出，避免两个 agent 同时覆盖产物

    用法：
      pwsh -File scripts/agent-workspace.ps1 -Action Status
      pwsh -File scripts/agent-workspace.ps1 -Action Claim -Agent agent-c -Scope root -Task "阶段条修复"
      pwsh -File scripts/agent-workspace.ps1 -Action Claim -Agent agent-c -Scope release
      pwsh -File scripts/agent-workspace.ps1 -Action Release -Agent agent-c -Scope root
#>
[CmdletBinding()]
param(
    [ValidateSet('Claim', 'Release', 'Status')]
    [string]$Action = 'Status',
    [string]$Agent = '',
    [ValidateSet('root', 'release')]
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
    $state = @{ root = $null; release = $null }
    if (-not (Test-Path -LiteralPath $path)) { return $state }
    try {
        $raw = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
        if ([string]::IsNullOrWhiteSpace($raw)) { return $state }
        $obj = ConvertFrom-Json -InputObject $raw
        foreach ($key in @('root', 'release')) {
            if ($obj.PSObject.Properties.Name -contains $key -and $obj.$key) {
                $state[$key] = $obj.$key
            }
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

if (-not $Agent) { $Agent = $env:NEONBOARD_AGENT_ID }

switch ($Action) {
    'Status' {
        $state = Read-State
        Write-Host '工作区占用状态：' -ForegroundColor DarkGray
        foreach ($key in @('root', 'release')) {
            $entry = $state[$key]
            if (-not $entry) {
                Write-Host ("  {0,-8} : 空闲" -f $key) -ForegroundColor DarkGray
                continue
            }
            $age = [int](Get-EntryAgeMinutes $entry)
            Write-Host ("  {0,-8} : {1}  (pid {2}, {3} 分钟前)  {4}" -f $key, $entry.agent, $entry.pid, $age, $entry.task) -ForegroundColor Yellow
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
            $state[$Scope] = $null
            Write-State -State $state
            Write-Host ("✓ 已释放 {0} 占用（{1}）" -f $Scope, $Agent) -ForegroundColor Green
        }
        exit 0
    }
}
