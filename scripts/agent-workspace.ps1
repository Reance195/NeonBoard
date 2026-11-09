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
      pwsh -File scripts/agent-workspace.ps1 -Action Drain [-Register <绝对路径>]   # DX-155 空壳清扫
#>
[CmdletBinding()]
param(
    [ValidateSet('Claim', 'Release', 'Status', 'Check', 'Drain')]
    [string]$Action = 'Status',
    [string]$Agent = '',
    # root / release / worktree:<agent 名>（DX-05：开发区占用按 agent 名登记）
    [ValidatePattern('^(root|release|worktree:[A-Za-z0-9._-]+)$')]
    [string]$Scope = 'root',
    [string]$Task = '',
    # DX-155：-Action Drain 时把「回收失败待删」的绝对路径先登记进待清理清单再清扫
    [string]$Register = '',
    [int]$StaleMinutes = 240
)

$ErrorActionPreference = 'Stop'

# git 取值统一走单一封装（DX-74：规则 E 扩到手工首行捕获后，本脚本不得再裸写）
. (Join-Path $PSScriptRoot 'git-helpers.ps1')
# DX-155：待清理清单落在数据目录，路径必须走 nb-paths.ps1 单点入口（宪章 §7.6，不得拼字面量）
. (Join-Path $PSScriptRoot 'nb-paths.ps1')

function Get-StatePath {
    $dir = Get-NbCommonDir -RepoPath (Get-Location).Path
    if (-not $dir) { throw '当前目录不是 Git 仓库，无法登记工作区占用' }
    return (Join-Path $dir 'agent-workspace.json')
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
    $dir = Get-NbCommonDir -RepoPath (Get-Location).Path
    if (-not $dir) { return $null }
    $path = Join-Path $dir 'preview-runtime.json'
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

# ---------- DX-155：待清理清单与空壳 drain ----------
# 根因：git-done「自回收」删的是自己脚下的检出——父 shell 的句柄还握着目录，
# Windows 连空目录的 rmdir 都拒绝，且不能 --force 硬删（会留半删除尸体，DX-93）。
# 于是每次收尾失败都在 .worktrees/ 下留一个「0 文件、无 .git、git 侧无注册」的空壳
# （2026-11-09 实测堆积 28 个）。修复：失败路径登记进待清理清单，agent-init 开区与
# git-done 收尾两个入口先 drain——三判据全过才直删空目录，缺一律保留并记一行原因
# （§7.10 回收由人拍板，非空目录永不删）。

function Get-NormalizedWtPath {
    param([string]$Path)
    if (-not $Path) { return '' }
    try {
        $full = [System.IO.Path]::GetFullPath($Path)
        $drive = [System.IO.Path]::GetPathRoot($full)
        if ($full -eq $drive) { return '' }          # 盘根不作为候选
        return $full.TrimEnd('\', '/').ToUpperInvariant()
    } catch { return '' }
}

function Get-RegisteredWorktreePathSet {
    # 判据数据源：git worktree list --porcelain 的当下输出（Z15：读事实源本身，不读投影）
    $set = @{}
    foreach ($wt in @(Get-WorktreeList)) {
        $n = Get-NormalizedWtPath -Path $wt.Path
        if ($n) { $set[$n] = $true }
    }
    return $set
}

function Get-HuskKeepReason {
    # 三判据的唯一实现（Z5），Drain 与 Check 共用；返回 '' 表示是空壳、可直删。
    param([string]$Path, [hashtable]$Registered)
    $n = Get-NormalizedWtPath -Path $Path
    if (-not $n) { return '路径无法规范化' }
    if ($Registered.ContainsKey($n)) { return 'git worktree list 仍注册' }
    if (Test-Path -LiteralPath (Join-Path $Path '.git')) { return '存在 .git 指针（真实检出）' }
    if (@(Get-ChildItem -LiteralPath $Path -Force -ErrorAction SilentlyContinue).Count -gt 0) { return '目录非空（回收由人拍板）' }
    return ''
}

function Get-PendingSweepPath { return Join-Path (Get-NbDataDir) 'pending-worktrees.json' }

function Read-PendingSweep {
    # 键 = 绝对路径（规范化大写），值 = 一行原因。清单只是收敛辅助：
    # 缺失 / 损坏都按空处理（fail-soft，Z4 收尾装饰类），漏登记的壳照样被 .worktrees 扫描收敛。
    $map = [ordered]@{}
    $path = Get-PendingSweepPath
    if (-not (Test-Path -LiteralPath $path)) { return $map }
    try {
        $raw = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
        if ([string]::IsNullOrWhiteSpace($raw)) { return $map }
        $obj = ConvertFrom-Json -InputObject $raw
        foreach ($key in @($obj.PSObject.Properties.Name)) {
            # PS7 的 ConvertFrom-Json '{}' 会给出一条空名伪属性，不过滤就成了幽灵清单项
            if ([string]::IsNullOrWhiteSpace($key)) { continue }
            $map[$key] = [string]$obj.$key
        }
    } catch {
        Write-Host ("⚠ 待清理清单不可读（{0}），按空处理" -f $path) -ForegroundColor Yellow
    }
    return $map
}

function Write-PendingSweep {
    param($Map)
    $path = Get-PendingSweepPath
    $dir = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $props = [ordered]@{}
    foreach ($key in $Map.Keys) { $props[$key] = [string]$Map[$key] }
    $json = if ($props.Count -gt 0) { ConvertTo-Json -InputObject ([pscustomobject]$props) -Depth 3 } else { '{}' }
    $tmp = "$path.tmp"
    [System.IO.File]::WriteAllText($tmp, $json, (New-Object System.Text.UTF8Encoding($false)))
    Move-Item -LiteralPath $tmp -Destination $path -Force   # 原子替换，写坏不留半截 JSON
}

function Invoke-WorktreeHuskDrain {
    param([string]$RegisterPath = '', [switch]$ProbeOnly)
    # ProbeOnly：只分类不删除（Check 自检用）。返回 Deleted / Kept / Pending / Husks。
    $out = [pscustomobject]@{ Deleted = @(); Kept = @(); Pending = 0; Husks = 0 }
    $worktrees = @(Get-WorktreeList)
    if ($worktrees.Count -eq 0) {
        Write-Host '· drain 跳过：当前目录不在 Git 检出里' -ForegroundColor DarkGray
        return $out
    }
    $anchorNorm = Get-NormalizedWtPath -Path $worktrees[0].Path
    $registered = Get-RegisteredWorktreePathSet
    $pending = Read-PendingSweep
    if ($RegisterPath) {
        $reg = Get-NormalizedWtPath -Path $RegisterPath
        # 幂等登记：同路径重复登记只补一次，不倍增（键即路径，天然去重）
        if ($reg -and -not $pending.Contains($reg)) {
            if (-not $ProbeOnly) {
                $pending[$reg] = ("登记 {0}（git-done 自回收失败，待入口 drain 收敛）" -f (Get-Date).ToString('s'))
                Write-PendingSweep -Map $pending
            }
        }
    }
    # 候选 = 待清理清单每项 + 锚点 .worktrees/ 顶层目录（清单丢了也能收敛）
    $candidates = New-Object System.Collections.Generic.List[object]
    $seen = @{}
    foreach ($key in @($pending.Keys)) {
        $seen[$key] = $true
        $candidates.Add([pscustomobject]@{ Norm = $key; Path = $key })
    }
    $wtRoot = Join-Path $worktrees[0].Path '.worktrees'
    if (Test-Path -LiteralPath $wtRoot) {
        foreach ($d in @(Get-ChildItem -LiteralPath $wtRoot -Directory -Force -ErrorAction SilentlyContinue)) {
            $n = Get-NormalizedWtPath -Path $d.FullName
            if (-not $n -or $n -eq $anchorNorm -or $seen.ContainsKey($n)) { continue }
            $seen[$n] = $true
            $candidates.Add([pscustomobject]@{ Norm = $n; Path = $d.FullName })
        }
    }
    $dirty = [bool]$RegisterPath
    foreach ($c in $candidates) {
        if (-not (Test-Path -LiteralPath $c.Path)) {
            # 目录已不在（手工删了或别的入口清了）：清单条目随之收敛
            if ($pending.Contains($c.Norm)) { $pending.Remove($c.Norm); $dirty = $true }
            continue
        }
        $reason = Get-HuskKeepReason -Path $c.Path -Registered $registered
        if ($reason -eq '') {
            $out.Husks++
            if ($ProbeOnly) { $out.Deleted += $c.Path; continue }
            try {
                [System.IO.Directory]::Delete($c.Path, $false)   # 到这里必是空目录
                $out.Deleted += $c.Path
                if ($pending.Contains($c.Norm)) { $pending.Remove($c.Norm) }
                $dirty = $true
            } catch {
                $out.Kept += ("{0} ：目录句柄仍被占用，保留" -f $c.Path)
                if ($pending.Contains($c.Norm)) {
                    $pending[$c.Norm] = ("保留 {0} ：句柄占用" -f (Get-Date).ToString('s')); $dirty = $true
                }
            }
        } else {
            $out.Kept += ("{0} ：{1}" -f $c.Path, $reason)
            if ($pending.Contains($c.Norm) -and -not $ProbeOnly) {
                $pending[$c.Norm] = ("保留 {0} ：{1}" -f (Get-Date).ToString('s'), $reason)
                $dirty = $true
            }
        }
    }
    if (-not $ProbeOnly) {
        if ($dirty) { Write-PendingSweep -Map $pending }
    } else {
        # ProbeOnly 不写清单：RegisterPath 的登记在 Check 里不生效
        $dirty = $false
    }
    $out.Pending = @($pending.Keys).Count
    return $out
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
        # DX-152：登记之外存在的 linked worktree 标注未登记；Status 只做展示，不改退出码
        $wtList = @(Get-WorktreeList)
        for ($i = 1; $i -lt $wtList.Count; $i++) {
            $n = Get-WorktreeScopeName -Path $wtList[$i].Path
            if (-not $n) { $n = Split-Path $wtList[$i].Path -Leaf }
            if (-not $state.ContainsKey("worktree:$n")) {
                Write-Host ("  {0,-20} : 未登记（{1}）" -f "worktree:$n", $wtList[$i].Path) -ForegroundColor Red
            }
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
        # 硬失败（exit 3）覆盖「两个会话真的在同一份检出里」与「linked worktree 没有占用登记」
        # （DX-152：开区即登记是强制的，绕过正门的 worktree 必须点名）；僵尸占用仍只告警。
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

        # 3) 每个 linked worktree（锚点除外）必须有占用登记；.worktrees 下的还必须有独立 .sandbox。
        #    DX-152：登记缺失从告警升级为硬冲突——手工 git worktree add 绕过正门同样要点名。
        $liveNames = @()
        for ($i = 1; $i -lt $worktrees.Count; $i++) {
            $wt = $worktrees[$i]
            $name = Get-WorktreeScopeName -Path $wt.Path
            $underWorktreesDir = [bool]$name
            if (-not $name) { $name = Split-Path $wt.Path -Leaf }
            $liveNames += $name
            if ($underWorktreesDir -and -not (Test-Path -LiteralPath (Join-Path $wt.Path '.sandbox'))) {
                $problems.Add(("工作区 '{0}' 缺独立 .sandbox（运行时数据会与别人共用）" -f $name))
            }
            if (-not $state.ContainsKey("worktree:$name")) {
                $problems.Add(("未登记 worktree：{0}（未走 agent-init 开区，占用体系不可见；补登记或回收由维护者拍板）" -f $wt.Path))
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

        # 5) .worktrees/ 下的未注册目录与仓库根的散落产物（DX-112）：半删除尸体、
        #    nbflow 临时区残留、根下 *.log（会挡锚点快进）—— 只点名报告，不删除（§7.10 回收由人拍板）
        $worktreeRoot = Join-Path $anchor '.worktrees'
        if (Test-Path -LiteralPath $worktreeRoot) {
            foreach ($dir in (Get-ChildItem -LiteralPath $worktreeRoot -Directory)) {
                if ($liveNames -notcontains $dir.Name) {
                    $warnings.Add((".worktrees/{0} 未注册为 worktree（半删除尸体或残留目录，回收由人拍板；登记还在时可先 -Action Release）" -f $dir.Name))
                }
            }
        }
        foreach ($f in (Get-ChildItem -LiteralPath $anchor -File -Filter '*.log' -ErrorAction SilentlyContinue)) {
            $warnings.Add(("仓库根散落产物 {0}（工具落错位置，会挡锚点快进，回收由人拍板）" -f $f.Name))
        }

        # 6) DX-155：待清理清单与空壳只读统计（ProbeOnly，不删除、不改清单、不新增退出码；
        #    真正的删除只发生在 agent-init / git-done 入口的 -Action Drain）
        $drainProbe = Invoke-WorktreeHuskDrain -ProbeOnly

        Write-Host ("锚点区：{0}" -f $anchor) -ForegroundColor DarkGray
        Write-Host ("工作区：{0} 个（.worktrees {1} 个）" -f $worktrees.Count, @($liveNames).Count) -ForegroundColor DarkGray
        Write-Host ("待清理清单 {0} 项 / 空壳 {1} 个" -f $drainProbe.Pending, $drainProbe.Husks) -ForegroundColor DarkGray
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

    'Drain' {
        # DX-155：两入口（agent-init 开区 / git-done 收尾）先 drain 再干活。
        # 只删三判据全过的空壳，其余保留并记一行原因；绝不阻断调用方流程（恒 exit 0，
        # 真出意外也按「清扫是收尾装饰、失败只告警」处理，Z4）。
        try {
            $r = Invoke-WorktreeHuskDrain -RegisterPath $Register
            foreach ($p in $r.Deleted) { Write-Host ("✓ 已清扫空壳 {0}" -f $p) -ForegroundColor Green }
            foreach ($k in $r.Kept) { Write-Host ("· 保留 {0}" -f $k) -ForegroundColor DarkGray }
            Write-Host ("drain：清扫 {0} 个 / 保留 {1} 个 / 待清理清单剩 {2} 项" -f $r.Deleted.Count, $r.Kept.Count, $r.Pending) -ForegroundColor DarkGray
        } catch {
            Write-Host ("· drain 失败（不阻断主流程）：{0}" -f $_.Exception.Message) -ForegroundColor Yellow
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
            # 开发区登记直接删键，root / release 保留空位（老逻辑按属性名判断是否存在）
            if ($Scope -like 'worktree:*') { $state.Remove($Scope) } else { $state[$Scope] = $null }
            Write-State -State $state
            Write-Host ("✓ 已释放 {0} 占用（{1}）" -f $Scope, $Agent) -ForegroundColor Green
        }
        exit 0
    }
}
