# install-agent-adapters.ps1 —— 把 NeonBoard 适配器装到本机各 agent 的配置目录
#
# 为什么单独一个安装脚本：适配器必须落在 agent 自己的配置目录里（~/.claude/settings.json、
# ~/.cursor/hooks.json、~/.config/opencode/plugins/），而这些目录里**已经有别的东西** ——
# claude 的 hooks 已被 orca-agent-hook 占满全部事件。手改一定出事：覆盖即删掉 orca。
# 所以本脚本只做「幂等追加」，且每次改前先备份。
#
# 用法：
#   install-agent-adapters.ps1 -Status                 # 体检：CLI 在不在、适配器装没装
#   install-agent-adapters.ps1 -Install               # 装全部
#   install-agent-adapters.ps1 -Install -Agents claude,cursor
#   install-agent-adapters.ps1 -Uninstall             # 摘掉（只删本脚本加的条目）
#
# 退出码：0 成功 / 1 部分失败 / 2 参数错误

[CmdletBinding()]
param(
    [switch]$Install,
    [switch]$Uninstall,
    [switch]$Status,
    [string[]]$Agents = @('claude', 'opencode', 'cursor', 'codex')
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$AdapterDir = Join-Path $PSScriptRoot 'agent-adapters'
$Emitter = Join-Path $PSScriptRoot 'emit-agent-task.ps1'
$Marker = 'neonboard-hook.ps1'
$MarkerJs = 'neonboard-todo-bridge.js'

function Write-Line { param([string]$Text) Write-Host $Text }

function Backup-File {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $bak = $Path + '.neonboard-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.bak'
    Copy-Item -LiteralPath $Path -Destination $bak -Force
    Write-Line ('  备份：' + $bak)
}

# 两个 agent 的 hooks 条目形状**不一样**，必须兼容，不能照抄一边：
#   claude  ~/.claude/settings.json  → 分组：[{"hooks":[{"type":"command","command":"..."}]}]
#   cursor  ~/.cursor/hooks.json     → 扁平：[{"command":"..."}]
# 按 claude 形状写的 helper（直接取 $g.hooks）碰到 cursor 会抛「找不到属性 hooks」，
# 本机实测就是这么炸的。下面三个 helper 一律走 Get-EntryCommands，形状无关。
function Get-EntryCommands {
    param([object]$Group)
    $cmds = @()
    if (-not $Group) { return $cmds }
    $names = @($Group.PSObject.Properties.Name)
    if ($names -contains 'hooks') {
        foreach ($h in @($Group.hooks)) {
            if ($h -and ($h.PSObject.Properties.Name -contains 'command')) { $cmds += [string]$h.command }
        }
    }
    elseif ($names -contains 'command') {
        $cmds += [string]$Group.command
    }
    return $cmds
}

function Test-HasMarker {
    param([object]$Groups, [string]$Mk)
    foreach ($g in @($Groups)) {
        foreach ($c in (Get-EntryCommands $g)) {
            if ($c -like ('*' + $Mk + '*')) { return $true }
        }
    }
    return $false
}

# 只在「条目里没有我们的标记」时追加一条；已有的（orca 等）原样保留。
function Add-HookEntry {
    param([object]$Groups, [string]$Command, [string]$Shape)
    $list = New-Object System.Collections.ArrayList
    foreach ($g in @($Groups)) { [void]$list.Add($g) }
    if ($Shape -eq 'flat') {
        [void]$list.Add([ordered]@{ command = $Command })
    }
    else {
        [void]$list.Add([ordered]@{ hooks = @([ordered]@{ type = 'command'; command = $Command; timeout = 10 }) })
    }
    return @($list.ToArray())
}

function Remove-HookEntry {
    param([object]$Groups, [string]$Mk)
    $kept = New-Object System.Collections.ArrayList
    foreach ($g in @($Groups)) {
        $mine = $false
        foreach ($c in (Get-EntryCommands $g)) {
            if ($c -like ('*' + $Mk + '*')) { $mine = $true }
        }
        if (-not $mine) { [void]$kept.Add($g) }
    }
    return @($kept.ToArray())
}

function Get-ExePath {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# ---------------- claude ----------------

function Get-ClaudeSettingsPath {
    return (Join-Path $env:USERPROFILE '.claude\settings.json')
}

function Install-Claude {
    $path = Get-ClaudeSettingsPath
    if (-not (Test-Path -LiteralPath $path)) { Write-Line '  claude：settings.json 不存在，跳过'; return $false }
    $hook = Join-Path $AdapterDir 'claude\neonboard-hook.ps1'
    $command = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $hook + '"'

    $json = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
    $changed = $false
    foreach ($evt in @('PostToolUse', 'PostToolUseFailure')) {
        $groups = @()
        if ($json.hooks -and ($json.hooks.PSObject.Properties.Name -contains $evt)) {
            $groups = @($json.hooks.$evt)
        }
        if (Test-HasMarker $groups $Marker) { Write-Line ('  claude：' + $evt + ' 已装，跳过'); continue }
        $newGroups = Add-HookEntry $groups $command 'grouped'
        if (-not $json.hooks) { $json | Add-Member -MemberType NoteProperty -Name 'hooks' -Value ([ordered]@{}) }
        if ($json.hooks.PSObject.Properties.Name -contains $evt) {
            $json.hooks.$evt = $newGroups
        }
        else {
            $json.hooks | Add-Member -MemberType NoteProperty -Name $evt -Value $newGroups
        }
        $changed = $true
        Write-Line ('  claude：' + $evt + ' 已追加')
    }
    if (-not $changed) { return $true }
    Backup-File $path
    $text = $json | ConvertTo-Json -Depth 24
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($path, $text, $enc)
    return $true
}

function Uninstall-Claude {
    $path = Get-ClaudeSettingsPath
    if (-not (Test-Path -LiteralPath $path)) { return $true }
    $json = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
    $changed = $false
    if ($json.hooks) {
        foreach ($evt in @($json.hooks.PSObject.Properties.Name)) {
            $groups = @($json.hooks.$evt)
            $kept = Remove-HookEntry $groups $Marker
            if ($kept.Count -ne $groups.Count) {
                $json.hooks.$evt = $kept
                $changed = $true
                Write-Line ('  claude：' + $evt + ' 已摘除')
            }
        }
    }
    if ($changed) {
        Backup-File $path
        $enc = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($path, ($json | ConvertTo-Json -Depth 24), $enc)
    }
    return $true
}

# ---------------- cursor ----------------

function Install-Cursor {
    $path = Join-Path $env:USERPROFILE '.cursor\hooks.json'
    $dir = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $dir)) { Write-Line '  cursor：~/.cursor 不存在，跳过'; return $false }
    $hook = Join-Path $AdapterDir 'cursor\neonboard-hook.ps1'
    $command = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $hook + '"'

    $json = $null
    if (Test-Path -LiteralPath $path) {
        $raw = Get-Content -LiteralPath $path -Raw -Encoding UTF8
        if (-not [string]::IsNullOrWhiteSpace($raw)) { $json = $raw | ConvertFrom-Json }
    }
    if (-not $json) { $json = [ordered]@{ hooks = [ordered]@{} } }
    if (-not $json.hooks) { $json | Add-Member -MemberType NoteProperty -Name 'hooks' -Value ([ordered]@{}) }

    $changed = $false
    foreach ($evt in @('postToolUse', 'afterAgentResponse')) {
        $groups = @()
        if ($json.hooks.PSObject.Properties.Name -contains $evt) { $groups = @($json.hooks.$evt) }
        if (Test-HasMarker $groups $Marker) { Write-Line ('  cursor：' + $evt + ' 已装，跳过'); continue }
        $newGroups = Add-HookEntry $groups $command 'flat'
        if ($json.hooks.PSObject.Properties.Name -contains $evt) { $json.hooks.$evt = $newGroups }
        else { $json.hooks | Add-Member -MemberType NoteProperty -Name $evt -Value $newGroups }
        $changed = $true
        Write-Line ('  cursor：' + $evt + ' 已追加')
    }
    if (-not $changed) { return $true }
    Backup-File $path
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($path, ($json | ConvertTo-Json -Depth 24), $enc)
    return $true
}

function Uninstall-Cursor {
    $path = Join-Path $env:USERPROFILE '.cursor\hooks.json'
    if (-not (Test-Path -LiteralPath $path)) { return $true }
    $json = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
    $changed = $false
    if ($json.hooks) {
        foreach ($evt in @($json.hooks.PSObject.Properties.Name)) {
            $groups = @($json.hooks.$evt)
            $kept = Remove-HookEntry $groups $Marker
            if ($kept.Count -ne $groups.Count) { $json.hooks.$evt = $kept; $changed = $true; Write-Line ('  cursor：' + $evt + ' 已摘除') }
        }
    }
    if ($changed) {
        Backup-File $path
        $enc = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($path, ($json | ConvertTo-Json -Depth 24), $enc)
    }
    return $true
}

# ---------------- opencode ----------------

function Install-Opencode {
    $dir = Join-Path $env:USERPROFILE '.config\opencode\plugins'
    if (-not (Test-Path -LiteralPath $dir)) { Write-Line ('  opencode：' + $dir + ' 不存在，跳过'); return $false }
    $src = Join-Path $AdapterDir 'opencode\neonboard-todo-bridge.js'
    $dst = Join-Path $dir 'neonboard-todo-bridge.js'
    Copy-Item -LiteralPath $src -Destination $dst -Force
    # 插件被复制出去后与源码树没有相对关系，投递器路径只能显式写进同级配置
    $cfg = [ordered]@{ emitter = $Emitter; agent = 'opencode' }
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText((Join-Path $dir 'neonboard-todo-bridge.json'), ($cfg | ConvertTo-Json -Depth 6), $enc)
    Write-Line ('  opencode：已装到 ' + $dst)
    return $true
}

function Uninstall-Opencode {
    $dir = Join-Path $env:USERPROFILE '.config\opencode\plugins'
    foreach ($f in @('neonboard-todo-bridge.js', 'neonboard-todo-bridge.json')) {
        $p = Join-Path $dir $f
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force; Write-Line ('  opencode：已删 ' + $f) }
    }
    return $true
}

# ---------------- codex ----------------

function Install-Codex {
    # Codex 没有 hooks 机制，无适配器可装，只做体检提示
    Write-Line '  codex：无 hooks 机制，无需安装入站适配器（出站 profile 见 agent.json）'
    return $true
}

function Uninstall-Codex { return $true }

# ---------------- 体检 ----------------

function Show-Status {
    Write-Line '=== NeonBoard agent 适配器体检 ==='
    foreach ($a in $Agents) {
        Write-Line ('agent: ' + $a)
        switch ($a) {
            'claude' {
                $exe = Get-ExePath 'claude'
                Write-Line ('  CLI    : ' + $(if ($exe) { $exe } else { '未找到' }))
                $p = Get-ClaudeSettingsPath
                $installed = $false
                if (Test-Path -LiteralPath $p) {
                    $j = Get-Content -LiteralPath $p -Raw -Encoding UTF8 | ConvertFrom-Json
                    if ($j.hooks -and ($j.hooks.PSObject.Properties.Name -contains 'PostToolUse')) {
                        $installed = Test-HasMarker @($j.hooks.PostToolUse) $Marker
                    }
                }
                Write-Line ('  适配器 : ' + $(if ($installed) { '已装' } else { '未装' }))
            }
            'opencode' {
                $exe = Get-ExePath 'opencode'
                Write-Line ('  CLI    : ' + $(if ($exe) { $exe } else { '未找到' }))
                $p = Join-Path $env:USERPROFILE '.config\opencode\plugins\neonboard-todo-bridge.js'
                Write-Line ('  适配器 : ' + $(if (Test-Path -LiteralPath $p) { '已装' } else { '未装' }))
            }
            'cursor' {
                $exe = Join-Path $env:LOCALAPPDATA 'cursor-agent\cursor-agent.cmd'
                if (-not (Test-Path -LiteralPath $exe)) { $exe = Get-ExePath 'cursor-agent' }
                Write-Line ('  CLI    : ' + $(if ($exe) { $exe } else { '未找到' }))
                $p = Join-Path $env:USERPROFILE '.cursor\hooks.json'
                $installed = $false
                if (Test-Path -LiteralPath $p) {
                    $j = Get-Content -LiteralPath $p -Raw -Encoding UTF8 | ConvertFrom-Json
                    if ($j.hooks -and ($j.hooks.PSObject.Properties.Name -contains 'postToolUse')) {
                        $installed = Test-HasMarker @($j.hooks.postToolUse) $Marker
                    }
                }
                Write-Line ('  适配器 : ' + $(if ($installed) { '已装' } else { '未装' }))
            }
            'codex' {
                $probe = & (Join-Path $AdapterDir 'codex\neonboard-codex.ps1') -Probe 2>&1
                foreach ($l in @($probe)) { Write-Line ('  ' + $l) }
            }
        }
        $agentJson = Join-Path $AdapterDir ($a + '\agent.json')
        Write-Line ('  profile: ' + $(if (Test-Path -LiteralPath $agentJson) { $agentJson } else { '缺失' }))
    }
    Write-Line ('投递器  : ' + $(if (Test-Path -LiteralPath $Emitter) { $Emitter } else { '缺失' }))
}

# ---------------- 主流程 ----------------

if (-not ($Install -or $Uninstall -or $Status)) {
    Write-Line '用法：install-agent-adapters.ps1 -Status | -Install | -Uninstall [-Agents a,b]'
    exit 2
}

if ($Status) { Show-Status; exit 0 }

$failed = 0
foreach ($a in $Agents) {
    Write-Line ('--- ' + $a)
    try {
        if ($Install) {
            $ok = switch ($a) {
                'claude' { Install-Claude }
                'opencode' { Install-Opencode }
                'cursor' { Install-Cursor }
                'codex' { Install-Codex }
                default { Write-Line ('  未知 agent：' + $a); $false }
            }
        }
        else {
            $ok = switch ($a) {
                'claude' { Uninstall-Claude }
                'opencode' { Uninstall-Opencode }
                'cursor' { Uninstall-Cursor }
                'codex' { Uninstall-Codex }
                default { Write-Line ('  未知 agent：' + $a); $false }
            }
        }
        if (-not $ok) { $failed++ }
    }
    catch {
        Write-Line ('  失败：' + $_.Exception.Message)
        $failed++
    }
}

Write-Line ('完成，失败 ' + $failed + ' 项')
exit $(if ($failed -gt 0) { 1 } else { 0 })
