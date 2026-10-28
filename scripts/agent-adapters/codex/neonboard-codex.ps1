# codex —— Codex CLI 的 NeonBoard 适配器
#
# 与另外三个 agent 不同：**Codex 没有 hooks 机制**（实测 codex-cli 0.164.0 没有 hook
# 子命令，config.toml 里也没有 hooks 段），所以拿不到它的内部待办清单。本适配器因此只做
# 两件事：
#
#   1. -Run：以只读沙箱跑一次 codex exec，把结果作为一张卡片投递到看板（入站）
#   2. -Probe：体检 codex CLI 是否可用、版本多少（供插件面板显示）
#
# 出站（看板派活给 codex）走 profile，声明在同目录的 agent.json。
#
# 两个必须遵守的实测坑（见 docs/agent-profile.md）：
#   - stdin 不重定向会卡在 "Reading additional input from stdin..."，一律接 NUL
#   - --json 吐的是事件流不是最终回复，末条回复要另用 -o <file> 收

[CmdletBinding()]
param(
    [string]$Prompt = '',
    [string]$Cwd = '',
    [string]$Codex = '',
    [string]$Emitter = '',
    [int]$TimeoutSec = 180,
    [switch]$Probe,
    [switch]$Run
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Resolve-Codex {
    param([string]$Explicit)
    if ($Explicit -and (Test-Path -LiteralPath $Explicit -PathType Leaf)) { return $Explicit }
    if ($env:CODEX_EXE -and (Test-Path -LiteralPath $env:CODEX_EXE -PathType Leaf)) { return $env:CODEX_EXE }

    # Store 版装在 WindowsApps 下，版本号目录随升级变化，而**这个根目录列不出来**：
    # 实测 Get-ChildItem 对 C:\Program Files\WindowsApps 直接「访问被拒绝」，但 Test-Path
    # 直取具体文件是可以的。所以不能靠枚举，要先拿到包的安装位置再拼相对路径 ——
    # Get-AppxPackage 给出的 InstallLocation 正是版本化目录，多个版本会返回多条。
    $pkgs = @()
    try {
        $pkgs = @(Get-AppxPackage -Name '*Codex*' -ErrorAction SilentlyContinue)
    }
    catch { $pkgs = @() }
    foreach ($p in $pkgs) {
        if (-not $p.InstallLocation) { continue }
        $cand = Join-Path $p.InstallLocation 'app\resources\codex.exe'
        if (Test-Path -LiteralPath $cand) { return $cand }
    }
    # 兜底：PATH 上有就直接用它
    $cmd = Get-Command 'codex.exe' -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Resolve-Emitter {
    param([string]$Explicit)
    if ($Explicit -and (Test-Path -LiteralPath $Explicit -PathType Leaf)) { return $Explicit }
    $here = Split-Path -Parent $MyInvocation.MyCommand.Path
    $candidate = Join-Path (Split-Path -Parent (Split-Path -Parent $here)) 'emit-agent-task.ps1'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    return $null
}

$exe = Resolve-Codex $Codex

if ($Probe) {
    if (-not $exe) { Write-Output 'codex: NOT FOUND'; exit 3 }
    Write-Output ('codex: ' + $exe)
    $v = & $exe --version 2>&1 | Select-Object -First 1
    Write-Output ('version: ' + $v)
    exit 0
}

if (-not $Run) {
    Write-Output '用法：neonboard-codex.ps1 -Run -Prompt "<提示>" [-Cwd <目录>] | -Probe'
    exit 2
}

if (-not $exe) { Write-Output '找不到 codex.exe，用 -Codex 指定或设 CODEX_EXE'; exit 3 }
if ([string]::IsNullOrWhiteSpace($Prompt)) { Write-Output '缺少 -Prompt'; exit 2 }
if ([string]::IsNullOrWhiteSpace($Cwd)) { $Cwd = (Get-Location).Path }

$outFile = Join-Path ([System.IO.Path]::GetTempPath()) ('neon-codex-out-' + [Guid]::NewGuid().ToString('N') + '.md')
$sid = 'codex-' + [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()

try {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    # -s read-only 是原生沙箱（实测确实拦住写文件），triage 必须走这一档
    $psi.Arguments = 'exec -s read-only -C "' + $Cwd + '" --json -o "' + $outFile + '" "' + ($Prompt -replace '"', '\"') + '"'
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $proc = [System.Diagnostics.Process]::Start($psi)
    # 关键：不写 stdin 就直接关掉，否则 codex 一直等输入
    $proc.StandardInput.Close()
    $null = $proc.StandardOutput.ReadToEnd()
    $null = $proc.StandardError.ReadToEnd()
    if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
        $proc.Kill()
        Write-Output ('codex 超时（' + $TimeoutSec + 's），未投递')
        exit 4
    }
    $code = $proc.ExitCode

    $answer = ''
    if (Test-Path -LiteralPath $outFile) {
        $answer = [System.IO.File]::ReadAllText($outFile, (New-Object System.Text.UTF8Encoding($false)))
    }
    if ($answer.Length -gt 2000) { $answer = $answer.Substring(0, 2000) + '…（已截断）' }

    $title = $Prompt
    if ($title.Length -gt 60) { $title = $title.Substring(0, 60) + '…' }

    # PowerShell 的 if 不是表达式，不能写在 hashtable 字面量的值位置上，先算成变量
    $status = 'pending'
    if ($code -eq 0) { $status = 'completed' }
    $noteText = '（无回复；退出码 ' + $code + '）'
    if ($answer) { $noteText = $answer }

    $item = @(@{
        id = 'run'
        title = 'codex: ' + $title
        status = $status
        note = $noteText
        tags = 'codex'
    })

    $emitter = Resolve-Emitter $Emitter
    if (-not $emitter) { Write-Output '找不到 emit-agent-task.ps1'; exit 3 }

    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('neon-codex-items-' + [Guid]::NewGuid().ToString('N') + '.json')
    try {
        $enc = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($tmp, ($item | ConvertTo-Json -Depth 8 -Compress), $enc)
        $psi2 = New-Object System.Diagnostics.ProcessStartInfo
        $psi2.FileName = 'powershell.exe'
        $psi2.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $emitter +
            '" -Agent codex -SessionId "' + $sid + '" -ItemsFile "' + $tmp + '"'
        $psi2.UseShellExecute = $false
        $psi2.CreateNoWindow = $true
        $p2 = [System.Diagnostics.Process]::Start($psi2)
        $p2.WaitForExit(8000) | Out-Null
    }
    finally {
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
    }

    Write-Output ('已投递（退出码 ' + $code + '）')
    exit 0
}
finally {
    if (Test-Path -LiteralPath $outFile) { Remove-Item -LiteralPath $outFile -Force -ErrorAction SilentlyContinue }
}
