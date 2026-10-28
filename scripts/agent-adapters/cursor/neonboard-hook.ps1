# cursor —— Cursor Agent 的 NeonBoard 入站适配器
#
# 装到 ~/.cursor/hooks.json（install-agent-adapters.ps1 只做追加，不动已有条目）。
# Cursor 把事件 JSON 写到 stdin，形态与 Claude Code 的 hooks 接近：
#   {"hook_event_name":"postToolUse","conversation_id":"...","tool_name":"...",
#    "tool_input":{...}}
#
# 与 claude 适配器的差别只有两处：会话 id 字段可能是 conversation_id / generation_id，
# 以及待办工具的命名。其余（抽取 → 交投递器 → fail-open）完全一致。
# 刻意不合并成一个文件：合并就要靠「按 agent 分支」猜字段，而两个 agent 的字段是会各自演进的，
# 分开各自的失败只影响自己。

[CmdletBinding()]
param(
    [string]$Emitter = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Get-StableId {
    param([string]$Content)
    $md5 = [System.Security.Cryptography.MD5]::Create()
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Content)
    $hash = $md5.ComputeHash($bytes)
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt 4; $i++) { [void]$sb.Append($hash[$i].ToString('x2')) }
    return ('r' + $sb.ToString())
}

function Resolve-Emitter {
    param([string]$Explicit)
    if ($Explicit -and (Test-Path -LiteralPath $Explicit -PathType Leaf)) { return $Explicit }
    # 同 claude 适配器：$MyInvocation.MyCommand.Path 在函数内部取不到，用 $PSScriptRoot
    $candidate = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'emit-agent-task.ps1'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    return $null
}

function Get-Prop {
    param([object]$Obj, [string[]]$Names)
    if (-not $Obj) { return $null }
    foreach ($n in $Names) {
        foreach ($p in $Obj.PSObject.Properties) {
            if ([string]::Compare($p.Name, $n, $true) -eq 0) { return $p.Value }
        }
    }
    return $null
}

try {
    # 同 claude 适配器：强制按 UTF-8 解码，避免控制台代码页把中文塌缩成 '?'
    $stdinReader = New-Object System.IO.StreamReader(
        [Console]::OpenStandardInput(),
        [System.Text.UTF8Encoding]::new($false))
    $raw = $stdinReader.ReadToEnd()
    if ([string]::IsNullOrWhiteSpace($raw)) { exit 0 }
    $evt = $raw | ConvertFrom-Json

    # 会话 id：不同版本字段名不同，逐个试
    $sessionId = [string](Get-Prop $evt @('conversation_id', 'conversationId', 'generation_id', 'session_id', 'sessionId'))
    if (-not $sessionId) { $sessionId = '' }

    $todos = @()

    $toolName = [string](Get-Prop $evt @('tool_name', 'toolName'))
    $toolInput = Get-Prop $evt @('tool_input', 'toolInput')

    if ($toolInput) {
        $found = Get-Prop $toolInput @('todos', 'items')
        if ($found) { $todos = @($found | ForEach-Object { $_ }) }
        elseif (Get-Prop $toolInput @('subject', 'content', 'title')) { $todos = @($toolInput) }
    }
    # 有些事件直接把清单挂在顶层（如 afterAgentResponse）
    if ($todos.Count -eq 0) {
        $top = Get-Prop $evt @('todos', 'items')
        if ($top) { $todos = @($top | ForEach-Object { $_ }) }
    }

    if ($todos.Count -eq 0) { exit 0 }

    $items = @()
    foreach ($t in $todos) {
        if ($t -is [string]) { $content = $t } else { $content = [string](Get-Prop $t @('content', 'subject', 'title')) }
        if ([string]::IsNullOrWhiteSpace($content)) { continue }
        $status = [string](Get-Prop $t @('status'))
        if (-not $status) { $status = 'pending' }
        $items += , @{ id = (Get-StableId $content); title = $content; status = $status }
    }
    if ($items.Count -eq 0) { exit 0 }

    $emitter = Resolve-Emitter $Emitter
    if (-not $emitter) { exit 0 }

    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('neon-cursor-' + [Guid]::NewGuid().ToString('N') + '.json')
    try {
        $enc = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($tmp, ($items | ConvertTo-Json -Depth 8 -Compress), $enc)

        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = 'powershell.exe'
        $psi.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $emitter +
            '" -Agent cursor-agent -SessionId "' + $sessionId + '" -ItemsFile "' + $tmp + '"'
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $proc = [System.Diagnostics.Process]::Start($psi)
        $proc.WaitForExit(8000) | Out-Null
    }
    finally {
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
    }
}
catch {
    # 旁路可视化：失败绝不影响 agent 主流程
}

exit 0
