# emit-agent-task.ps1 —— agent-task-event v1 的统一投递器
#
# 为什么要有这个文件：协议（docs/agent-task-event.md）定义好了，agent-bridge 插件也消费了，
# 但「把清单变成一次投递」这段逻辑此前没有唯一实现 —— 每个适配器各自拼 JSON、各自找
# NeonBoard.exe，exe 路径一换（build / build-local / 安装目录）全部失效，且协议字段很容易
# 少写或写错（少 protocol 就整包被丢弃，且不会有任何提示）。
#
# 本机 agent 的适配器（claude / opencode / cursor / codex）一律只调本脚本，不再自己拼装。
# 投递链路：本脚本 → NeonBoard.exe --emit-hook-file <tmp> --hook-event agent.task → hooks.db
#          → 主进程轮询 → agent.task 事件域 → plugins/agent-bridge 消费。
# 应用未运行时投递也会落库，下次启动补消费，因此适配器无需等待或重试。
#
# 用法：
#   emit-agent-task.ps1 -Agent claude -SessionId <sid> -Items '<json 数组>'
#   emit-agent-task.ps1 -Agent claude -SessionId <sid> -ItemsFile items.json
#   emit-agent-task.ps1 -Agent claude -PayloadFile envelope.json   # 已组装好的整包
#   emit-agent-task.ps1 -SelfTest                                   # 自检（不发真实投递）
#
# 退出码：0 成功 / 2 参数或载荷非法 / 3 找不到 NeonBoard.exe / 4 投递失败

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Agent,

    [string]$SessionId = '',

    # items 的 JSON 数组文本：[{"id":"t1","title":"...","status":"in_progress"}]
    [string]$Items = '',

    # 或从文件读 items（UTF-8）
    [string]$ItemsFile = '',

    # 已组装好的完整 envelope（此时其它字段除 -Exe 外均忽略）
    [string]$PayloadFile = '',

    # 从标准输入读 items。为什么要有这条通道：Windows 上把带双引号的 JSON 塞进 argv
    # 会被层层转义撕碎（实测 powershell.exe -File 传 [{"id":"t1"...}] 时报
    # 「无效的 JSON 基元: t1」—— 引号全丢了）。文件与 stdin 才是跨进程可靠的入口，
    # 适配器一律走这两条，不走 -Items。
    [switch]$Stdin,

    # NeonBoard.exe 路径；不给则按 Resolve-NbExe 的顺序探测
    [string]$Exe = '',

    # 事件域名，默认 agent.task（与 hook.commit 互不掺和）
    [string]$Event = 'agent.task',

    [switch]$DryRun,
    [switch]$Quiet,
    [switch]$SelfTest
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$PROTOCOL = 'agent-task-event'
$VERSION = 1
# 上游 todo 状态 → 看板三列。与 docs/agent-task-event.md 的映射表保持一致；
# 协议层已是看板状态时原样透传。
$STATUS_MAP = @{
    'pending' = 'todo'
    'in_progress' = 'doing'
    'completed' = 'done'
}

function Write-Note {
    param([string]$Message)
    if (-not $Quiet) { Write-Host "[emit-agent-task] $Message" }
}

function Resolve-NbExe {
    param([string]$Explicit)

    if ($Explicit -and (Test-Path -LiteralPath $Explicit -PathType Leaf)) { return $Explicit }
    if ($env:NEONBOARD_EXE -and (Test-Path -LiteralPath $env:NEONBOARD_EXE -PathType Leaf)) {
        return $env:NEONBOARD_EXE
    }

    $root = Split-Path -Parent $PSScriptRoot
    # Ninja 单配置生成器的产物直接落在 build/ 下，多配置生成器（VS）才多一层 Release/Debug。
    # 两种布局都列全 —— 只写 Release 那层的话，换用 Ninja 构建时全部适配器静默失效
    # （找不到 exe → 退出码 3，而适配器是 fail-open 的，看板上什么都不出现也不报错）。
    $candidates = @(
        (Join-Path $root 'build\NeonBoard.exe'),
        (Join-Path $root 'build-local\NeonBoard.exe'),
        (Join-Path $root 'build\Release\NeonBoard.exe'),
        (Join-Path $root 'build-local\Release\NeonBoard.exe'),
        (Join-Path $root 'build\Debug\NeonBoard.exe'),
        (Join-Path $root 'build-local\Debug\NeonBoard.exe')
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c -PathType Leaf) { return $c }
    }

    $cmd = Get-Command 'NeonBoard.exe' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# 解析 items 的 JSON 文本。
# 为什么不能写 @($Text | ConvertFrom-Json)：PS 5.1 的 ConvertFrom-Json 把整个数组作为
# **一个**对象写出（实测 @(...) 得到 1 个元素，元素本身才是那 3 条 todo，$o.id 会变成
# "t1 t2 t3" 这种成员枚举结果）。再走一次管道才会真正展开 —— 少了这一步的表现是
# 「丢弃 N 条缺 id 的条目」，而适配器是 fail-open 的，看板上什么都不出现也不报错。
function ConvertFrom-ItemsJson {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return @() }
    $parsed = $Text | ConvertFrom-Json
    return @($parsed | ForEach-Object { $_ })
}

# 找 Qt 的 bin 目录（只用来给子进程补 PATH）。
# 为什么需要：源码树里 build/NeonBoard.exe 旁边没有 Qt DLL，脱离 Qt 环境直接跑会
# 0xC0000135（STATUS_DLL_NOT_FOUND，本机实测退出码 -1073741515）。安装版 DLL 就在旁边，
# 不需要补；开发版必须补，否则投递静默失败 —— 而适配器是 fail-open 的，看板上什么都不出现。
function Resolve-QtBin {
    param([string]$ExePath)

    $dir = Split-Path -Parent $ExePath
    if (Test-Path -LiteralPath (Join-Path $dir 'Qt6Core.dll')) { return $null }
    if ($env:QT_BIN_DIR -and (Test-Path -LiteralPath $env:QT_BIN_DIR)) { return $env:QT_BIN_DIR }

    $roots = @()
    if ($env:CMAKE_PREFIX_PATH) {
        foreach ($r in ($env:CMAKE_PREFIX_PATH -split ';')) {
            if ($r) { $roots += (Join-Path $r 'bin') }
        }
    }
    $roots += 'C:\Qt'
    $roots += (Join-Path $HOME 'Qt')
    foreach ($r in $roots) {
        if (-not (Test-Path -LiteralPath $r)) { continue }
        $hits = Get-ChildItem -LiteralPath $r -Filter 'Qt6Core.dll' -Recurse -Depth 4 -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like '*msvc*' } |
            Sort-Object FullName -Descending
        foreach ($h in $hits) { return $h.DirectoryName }
    }
    return $null
}

function Get-EpochMs {
    $epoch = [DateTimeOffset]::new(1970, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
    return [int64]([DateTimeOffset]::UtcNow - $epoch).TotalMilliseconds
}

# 归一化单个 item：只保留协议认识的字段，缺 id / title 的条目整条丢弃（宁缺勿错 ——
# 一条非法 id 会让 agent-bridge 的 meta 映射键错位，污染整批对账）。
# 从一条原始 todo 里取字段（大小写不敏感）。
# 为什么单独抽出来：适配器喂进来的形状有三种 —— hashtable（脚本内联）、
# PSCustomObject（ConvertFrom-Json）、以及部分 agent 直接给的字符串。而 hashtable
# 走 $Raw.PSObject.Properties 拿到的是 Keys/Values/Count 这些 Hashtable 自身的属性，
# 不是数据字段，直接遍历会静默读空 —— 必须分形状取。
function Get-Field {
    param([object]$Raw, [string[]]$Names)

    if ($Raw -is [hashtable] -or $Raw -is [System.Collections.IDictionary]) {
        foreach ($n in $Names) {
            foreach ($k in $Raw.Keys) {
                if ([string]::Compare([string]$k, $n, $true) -eq 0) { return $Raw[$k] }
            }
        }
        return $null
    }
    foreach ($p in $Raw.PSObject.Properties) {
        foreach ($n in $Names) {
            if ([string]::Compare($p.Name, $n, $true) -eq 0) { return $p.Value }
        }
    }
    return $null
}

function Format-Item {
    param([object]$Raw)

    if ($Raw -is [string]) {
        return @{ id = $Raw; title = $Raw; status = 'pending' }
    }
    if ($Raw -eq $null) { return $null }

    $id = [string](Get-Field $Raw @('id'))
    $title = [string](Get-Field $Raw @('title'))
    if ([string]::IsNullOrWhiteSpace($title)) {
        # Claude 的 TodoWrite 用 content 而不是 title
        $title = [string](Get-Field $Raw @('content', 'activeForm'))
    }
    $rawStatus = Get-Field $Raw @('status')
    if ($rawStatus -eq $null) { $rawStatus = 'pending' }
    $status = [string]$rawStatus
    $note = Get-Field $Raw @('note')
    $tags = Get-Field $Raw @('tags')
    $taskId = Get-Field $Raw @('taskId')

    if ([string]::IsNullOrWhiteSpace($id)) { return $null }
    if ([string]::IsNullOrWhiteSpace($title)) { $title = $id }

    $lower = $status.ToLowerInvariant()
    if ($STATUS_MAP.ContainsKey($lower)) { $status = $STATUS_MAP[$lower] }
    elseif (@('todo', 'doing', 'done') -contains $lower) { $status = $lower }
    else { $status = 'todo' }

    $item = [ordered]@{ id = $id; title = $title; status = $status }
    if ($note -ne $null) { $item['note'] = $note }
    if ($tags -ne $null) { $item['tags'] = $tags }
    if ($taskId) { $item['taskId'] = $taskId }
    return $item
}

function New-Envelope {
    param(
        [string]$AgentId,
        [string]$Sid,
        [object[]]$RawItems
    )

    if ([string]::IsNullOrWhiteSpace($AgentId)) { throw 'Agent 不能为空' }
    if ([string]::IsNullOrWhiteSpace($Sid)) {
        # 协议要求 sessionId 必填。适配器拿不到会话 id 时用进程启动时刻兜底，
        # 保证同一轮会话内 id 稳定（幂等对账靠的就是这个稳定性）。
        $Sid = 'pid' + $PID
    }

    $items = @()
    $dropped = 0
    foreach ($r in $RawItems) {
        $item = Format-Item $r
        if ($item) { $items += , $item } else { $dropped++ }
    }

    $env = [ordered]@{
        protocol = $PROTOCOL
        version = $VERSION
        agent = $AgentId
        sessionId = $Sid
        ts = Get-EpochMs
        items = $items
    }
    return @{ Envelope = $env; Dropped = $dropped }
}

function ConvertTo-JsonText {
    param([object]$Value)
    # -Compress 去掉换行，投递文件更小；PowerShell 5.1 会把非 ASCII 转义成 \uXXXX，
    # QJsonDocument 能正确解析，不影响中文标题。
    return $Value | ConvertTo-Json -Depth 12 -Compress
}

function Write-TempJson {
    param([string]$Text)
    $dir = [System.IO.Path]::GetTempPath()
    $name = 'neon-agent-task-' + [Guid]::NewGuid().ToString('N') + '.json'
    $path = Join-Path $dir $name
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($path, $Text, $enc)
    return $path
}

function Invoke-Emit {
    param(
        [string]$Json,
        [string]$ExePath,
        [int]$Count = -1
    )

    if ($DryRun) {
        Write-Note ('干跑：不调用宿主。载荷 ' + $Json.Length + ' 字节')
        Write-Note ('干跑：命令 ' + $ExePath + ' --emit-hook-file <tmp> --hook-event ' + $Event)
        return 0
    }

    $tmp = Write-TempJson $Json
    try {
        # 用 Arguments 字符串而不是 ArgumentList：ArgumentList 是 .NET Core 才有的属性，
        # Windows PowerShell 5.1 跑在 .NET Framework 上，访问它直接 PropertyNotFound
        # （本机实测就是这个错）。临时目录路径带空格的概率不低，逐个加引号。
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $ExePath
        $psi.Arguments = '--emit-hook-file "' + $tmp + '" --hook-event "' + $Event + '"'
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.CreateNoWindow = $true

        # 补 PATH 而不是写 $psi.EnvironmentVariables：后者在 .NET Framework 上取出来是
        # null，索引赋值直接抛「无法对 Null 数组进行索引」（本机实测）。子进程继承当前
        # 进程环境，改 $env:PATH 效果一样且没有这个坑。
        $qtBin = Resolve-QtBin $ExePath
        if ($qtBin) {
            $env:PATH = $qtBin + ';' + $env:PATH
            Write-Note ('补 Qt bin 到 PATH：' + $qtBin)
        }

        $proc = [System.Diagnostics.Process]::Start($psi)
        $stdout = $proc.StandardOutput.ReadToEnd()
        $stderr = $proc.StandardError.ReadToEnd()
        $proc.WaitForExit()

        if ($proc.ExitCode -ne 0) {
            Write-Note ('投递失败，退出码 ' + $proc.ExitCode)
            if ($stdout) { Write-Note ('stdout: ' + $stdout.Trim()) }
            if ($stderr) { Write-Note ('stderr: ' + $stderr.Trim()) }
            return 4
        }
        if ($Count -ge 0) { Write-Note ('投递成功（' + $Count + ' 条，事件域 ' + $Event + '）') }
        else { Write-Note ('投递成功（事件域 ' + $Event + '）') }
        return 0
    }
    finally {
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
    }
}

function Invoke-SelfTest {
    $failed = 0
    function Check([string]$Name, [bool]$Ok) {
        if ($Ok) { Write-Host "  PASS  $Name" }
        else { Write-Host "  FAIL  $Name"; $script:failed++ }
    }

    Write-Host '[emit-agent-task] 自检'

    # 1. 状态映射：上游三态 → 看板三列，且看板态原样透传
    $r1 = New-Envelope -AgentId 't' -Sid 's' -RawItems @(
        @{ id = 'a'; title = 'A'; status = 'pending' },
        @{ id = 'b'; title = 'B'; status = 'in_progress' },
        @{ id = 'c'; title = 'C'; status = 'completed' },
        @{ id = 'd'; title = 'D'; status = 'doing' },
        @{ id = 'e'; title = 'E'; status = '乱七八糟' }
    )
    $st = @($r1.Envelope.items | ForEach-Object { $_.status })
    Check '状态映射 pending/in_progress/completed' (($st[0] -eq 'todo') -and ($st[1] -eq 'doing') -and ($st[2] -eq 'done'))
    Check '看板态原样透传' ($st[3] -eq 'doing')
    Check '未知状态回落 todo' ($st[4] -eq 'todo')

    # 2. 必填字段：protocol / version / agent / sessionId 齐全
    $j = ConvertTo-JsonText $r1.Envelope
    $parsed = $j | ConvertFrom-Json
    Check 'envelope 含 protocol/version' (($parsed.protocol -eq $PROTOCOL) -and ($parsed.version -eq $VERSION))
    Check 'envelope 含 agent/sessionId' (($parsed.agent -eq 't') -and ($parsed.sessionId -eq 's'))
    Check 'items 数量正确' ($parsed.items.Count -eq 5)

    # 3. 缺 id 的条目整条丢弃，且计数可查
    $r2 = New-Envelope -AgentId 't' -Sid 's' -RawItems @(@{ title = '无 id' }, @{ id = 'ok'; title = '有 id' })
    Check '缺 id 条目被丢弃' (($r2.Envelope.items.Count -eq 1) -and ($r2.Dropped -eq 1))

    # 4. content 字段（Claude TodoWrite 的形状）可当标题
    $r3 = New-Envelope -AgentId 't' -Sid 's' -RawItems @(@{ id = 'x'; content = '待办正文'; status = 'pending' })
    $r3first = @($r3.Envelope.items)[0]
    Check 'content 兜底为 title' ($r3first.title -eq '待办正文')

    # 5. 空 sessionId 有兜底，不会产出空串（空串会让插件丢包）
    $r4 = New-Envelope -AgentId 't' -Sid '' -RawItems @(@{ id = 'x'; title = 'X' })
    Check '空 sessionId 有兜底' ($r4.Envelope.sessionId.Length -gt 0)

    # 6. 空清单也要能投递（agent 清完 todo 时是合法场景）
    # 不用 -like：方括号在 PowerShell 通配符里是字符类，'"items":[]' 会直接抛
    # WildcardPatternException（自检第 10 项在本机就是这么红的）。
    $r5 = New-Envelope -AgentId 't' -Sid 's' -RawItems @()
    Check '空清单合法' ((ConvertTo-JsonText $r5.Envelope).Contains('"items":[]'))

    if ($failed -gt 0) {
        Write-Host ('[emit-agent-task] 自检失败 ' + $failed + ' 项')
        return 1
    }
    Write-Host '[emit-agent-task] 自检全部通过'
    return 0
}

# ---------------- 主流程 ----------------

if ($SelfTest) { exit (Invoke-SelfTest) }

if (-not $PayloadFile -and [string]::IsNullOrWhiteSpace($Agent)) {
    Write-Note '缺少 -Agent（或改用 -PayloadFile 直接投整包）'
    exit 2
}

$exePath = Resolve-NbExe $Exe
if (-not $exePath) {
    Write-Note '找不到 NeonBoard.exe。用 -Exe 指定，或设 NEONBOARD_EXE 环境变量'
    exit 3
}

$json = $null
if ($PayloadFile) {
    if (-not (Test-Path -LiteralPath $PayloadFile -PathType Leaf)) {
        Write-Note "载荷文件不存在：$PayloadFile"
        exit 2
    }
    $json = [System.IO.File]::ReadAllText($PayloadFile, (New-Object System.Text.UTF8Encoding($false)))
    $itemsCount = -1
}
else {
    $rawItems = @()
    # 只在「确实有管道进来」或显式 -Stdin 时读标准输入：未重定向时 [Console]::In.ReadToEnd()
    # 会一直等到 EOF（Ctrl+Z），交互调用会直接挂住。
    $wantStdin = $Stdin -or (-not $ItemsFile -and [string]::IsNullOrWhiteSpace($Items) -and [Console]::IsInputRedirected)
    if ($ItemsFile) {
        if (-not (Test-Path -LiteralPath $ItemsFile -PathType Leaf)) {
            Write-Note "items 文件不存在：$ItemsFile"
            exit 2
        }
        $rawItems = ConvertFrom-ItemsJson ([System.IO.File]::ReadAllText($ItemsFile, (New-Object System.Text.UTF8Encoding($false))))
    }
    elseif ($wantStdin) {
        # 按 UTF-8 字节流解码，不用 [Console]::In（编码跟控制台代码页走，中文会变 '?'）
        $stdinReader = New-Object System.IO.StreamReader(
            [Console]::OpenStandardInput(),
            [System.Text.UTF8Encoding]::new($false))
        $rawItems = ConvertFrom-ItemsJson ($stdinReader.ReadToEnd())
    }
    elseif (-not [string]::IsNullOrWhiteSpace($Items)) {
        $rawItems = ConvertFrom-ItemsJson $Items
    }

    $built = New-Envelope -AgentId $Agent -Sid $SessionId -RawItems $rawItems
    $json = ConvertTo-JsonText $built.Envelope
    $itemsCount = $built.Envelope.items.Count
    if ($built.Dropped -gt 0) {
        Write-Note ('丢弃 ' + $built.Dropped + ' 条缺 id 的条目')
    }
}

if ([string]::IsNullOrWhiteSpace($json)) {
    Write-Note '载荷为空，不投递'
    exit 2
}

exit (Invoke-Emit -Json $json -ExePath $exePath -Count $itemsCount)
