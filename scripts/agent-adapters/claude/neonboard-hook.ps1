# claude —— Claude Code 的 NeonBoard 入站适配器
#
# 装在 ~/.claude/settings.json 的 hooks 里（由 install-agent-adapters.ps1 追加，
# 不会动已有的 orca-agent-hook）。Claude Code 把事件 JSON 写到本脚本的 stdin：
#   {"hook_event_name":"PostToolUse","session_id":"...","tool_name":"TodoWrite",
#    "tool_input":{"todos":[{"content":"...","status":"pending","activeForm":"..."}]}}
#
# 本脚本只做「抽取 → 交给 emit-agent-task.ps1」，不做任何解析规则 —— 规则归 agent-bridge
# 插件。全程 fail-open：任何异常都静默退出 0，绝不能因为看板旁路拖慢或打断 agent 主流程。

[CmdletBinding()]
param(
    # 投递器位置；不给则按脚本相对位置找
    [string]$Emitter = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# 适配器是 fail-open 的（任何异常都吞掉，绝不影响 agent 主流程），代价是出问题时
# 完全没信号 —— 看板上什么都不出现，也不知道是没触发还是投递失败。
# 设 NEONBOARD_ADAPTER_DEBUG=<日志文件路径> 即可把每一步与异常写进去。
function Write-Dbg {
    param([string]$Message)
    $p = $env:NEONBOARD_ADAPTER_DEBUG
    if (-not $p) { return }
    try { Add-Content -LiteralPath $p -Value ('[claude] ' + $Message) -Encoding UTF8 } catch {}
}

function Get-StableId {
    param([string]$Content)
    # Claude 的 TodoWrite 不给 todo id，只有 content。用 content 的短哈希当 id：
    # 同一条 todo 在多轮投递中 id 稳定（幂等对账的前提），内容改了才换卡。
    # 直接拿 content 当 id 也能用，但 meta 映射键会很长且含空格，哈希更干净。
    $md5 = [System.Security.Cryptography.MD5]::Create()
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Content)
    $hash = $md5.ComputeHash($bytes)
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt 4; $i++) { [void]$sb.Append($hash[$i].ToString('x2')) }
    return ('c' + $sb.ToString())
}

function Resolve-Emitter {
    param([string]$Explicit)
    if ($Explicit -and (Test-Path -LiteralPath $Explicit -PathType Leaf)) { return $Explicit }
    # 用 $PSScriptRoot，不要用 $MyInvocation.MyCommand.Path：后者在**函数内部**指向的是
    # 这次函数调用而不是脚本本身（取 .Path 直接抛「找不到属性 Path」，本机实测），
    # Resolve-Emitter 是个函数，正好踩中。
    $candidate = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'emit-agent-task.ps1'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    $candidate2 = Join-Path (Split-Path -Parent $PSScriptRoot) 'emit-agent-task.ps1'
    if (Test-Path -LiteralPath $candidate2 -PathType Leaf) { return $candidate2 }
    return $null
}

try {
    # 按 UTF-8 字节流强制解码，不用 [Console]::In：后者的编码跟随控制台代码页，
    # 中文 todo 会被解成 '?'（本机实测：'拆解成插件' → '?????'），而且不同内容会被
    # 塌缩成同一个 '?????'，进而算出相同的稳定 id —— 表现为看板上少卡、卡片互相覆盖。
    $stdinReader = New-Object System.IO.StreamReader(
        [Console]::OpenStandardInput(),
        [System.Text.UTF8Encoding]::new($false))
    $raw = $stdinReader.ReadToEnd()
    Write-Dbg ('stdin ' + $raw.Length + ' 字节')
    if ([string]::IsNullOrWhiteSpace($raw)) { exit 0 }

    $evt = $raw | ConvertFrom-Json
    $toolName = ''
    if ($evt.PSObject.Properties.Name -contains 'tool_name') { $toolName = [string]$evt.tool_name }
    Write-Dbg ('tool=' + $toolName)

    # 只认待办类工具。Claude 新版把 TodoWrite 拆成 TaskCreate / TaskUpdate，一并收。
    if (@('TodoWrite', 'TaskCreate', 'TaskUpdate') -notcontains $toolName) { exit 0 }

    $sessionId = ''
    if ($evt.PSObject.Properties.Name -contains 'session_id') { $sessionId = [string]$evt.session_id }

    $todos = @()
    $toolInput = $null
    if ($evt.PSObject.Properties.Name -contains 'tool_input') { $toolInput = $evt.tool_input }
    if ($toolInput) {
        if ($toolInput.PSObject.Properties.Name -contains 'todos') {
            $todos = @($toolInput.todos | ForEach-Object { $_ })
        }
        # TaskCreate / TaskUpdate 单条形态
        elseif ($toolInput.PSObject.Properties.Name -contains 'subject') {
            $todos = @($toolInput)
        }
    }
    Write-Dbg ('todos=' + $todos.Count)
    if ($todos.Count -eq 0) { exit 0 }

    $items = @()
    foreach ($t in $todos) {
        $content = ''
        $status = 'pending'
        foreach ($p in $t.PSObject.Properties) {
            if ($p.Name -eq 'content' -or $p.Name -eq 'subject') { $content = [string]$p.Value }
            if ($p.Name -eq 'status') { $status = [string]$p.Value }
        }
        if ([string]::IsNullOrWhiteSpace($content)) { continue }
        $items += , @{ id = (Get-StableId $content); title = $content; status = $status }
    }
    if ($items.Count -eq 0) { exit 0 }

    Write-Dbg ('items=' + $items.Count)
    $emitter = Resolve-Emitter $Emitter
    Write-Dbg ('emitter=' + $emitter)
    if (-not $emitter) { exit 0 }

    # 走文件通道而不是 -Items：带引号的 JSON 进 argv 会被转义撕碎（见投递器注释）
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('neon-claude-' + [Guid]::NewGuid().ToString('N') + '.json')
    try {
        $enc = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($tmp, ($items | ConvertTo-Json -Depth 8 -Compress), $enc)

        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = 'powershell.exe'
        $psi.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $emitter +
            '" -Agent claude -SessionId "' + $sessionId + '" -ItemsFile "' + $tmp + '"'
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        Write-Dbg ('spawn: ' + $psi.Arguments)
        $proc = [System.Diagnostics.Process]::Start($psi)
        $done = $proc.WaitForExit(8000)
        Write-Dbg ('child exit=' + $proc.ExitCode + ' completed=' + $done)
    }
    finally {
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
    }
}
catch {
    # 旁路可视化：失败绝不能影响 agent 主流程，但要留信号
    Write-Dbg ('EXCEPTION: ' + $_.Exception.Message)
    Write-Dbg ('AT: ' + $_.InvocationInfo.PositionMessage)
}

exit 0
