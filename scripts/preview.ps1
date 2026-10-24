<#
    NeonBoard 预览中心（命令行版）

    与应用内「预览中心」面板同一套后端：都走 NeonBoard.exe --preview-*，
    这里只是把面板里的操作搬到脚本里，方便无头环境与批量操作。

    用法：
      pwsh -File scripts/preview.ps1 -Action List
      pwsh -File scripts/preview.ps1 -Action Build   -Agent agent-c
      pwsh -File scripts/preview.ps1 -Action Package -Agent agent-c
      pwsh -File scripts/preview.ps1 -Action Start   -Agent agent-c
      pwsh -File scripts/preview.ps1 -Action Stop    -Agent agent-c

    退出码：0 成功 / 2 失败
#>
[CmdletBinding()]
param(
    [ValidateSet('List', 'Build', 'Package', 'Start', 'Stop')]
    [string]$Action = 'List',
    [string]$Agent = '',
    [string]$Exe = '',
    [int]$TimeoutSeconds = 1800
)

$ErrorActionPreference = 'Stop'

function Get-AnchorRoot {
    $common = (& git rev-parse --path-format=absolute --git-common-dir 2>$null | Select-Object -First 1)
    if (-not $common) { throw '当前目录不是 Git 仓库' }
    $common = $common.Trim()
    # common dir 形如 <仓库根>/.git，锚点根就是它的上级目录
    if (Test-Path -LiteralPath (Join-Path $common '..')) {
        return (Resolve-Path -LiteralPath (Join-Path $common '..')).Path
    }
    return $common
}

function Get-QtBinDir {
    $cache = Join-Path $script:AnchorRoot 'build\CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $prefix = Select-String -LiteralPath $cache -Pattern '^CMAKE_PREFIX_PATH:.*=(.+)$' |
            Select-Object -First 1
        if ($prefix) {
            $value = ($prefix.Matches[0].Groups[1].Value).Trim()
            if ($value) {
                $bin = Join-Path $value.Trim() 'bin'
                if (Test-Path -LiteralPath $bin) { return $bin }
            }
        }
    }
    if ($env:NEONBOARD_QT_BIN -and (Test-Path -LiteralPath $env:NEONBOARD_QT_BIN)) {
        return $env:NEONBOARD_QT_BIN
    }
    return ''
}

function Test-PreviewCapable {
    param([Parameter(Mandatory)][string]$Path, [string]$Needle = '--preview-list')
    # 旧版 exe 不认识 --preview-*，参数会被忽略并直接拉起主窗口（脚本看起来像挂死）。
    # 用二进制里是否含该参数字面量来判断能力，避免踩这个坑。
    # 注意：QStringLiteral 里的字面量在二进制里是 UTF-16，必须同时查 ASCII 与 UTF-16，
    # 且比较要用 Ordinal（默认的 IndexOf 是区域比较，行为随 ICU 版本漂移）。
    try {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
        $needle = $Needle
        foreach ($encoding in @([System.Text.Encoding]::ASCII, [System.Text.Encoding]::Unicode)) {
            $text = $encoding.GetString($bytes)
            if ($text.IndexOf($needle, [System.StringComparison]::Ordinal) -ge 0) { return $true }
        }
        return $false
    } catch {
        return $false
    }
}

function Resolve-PreviewExe {
    param([string]$Needle = '--preview-list')
    $candidates = @()
    if ($Exe) {
        if (-not (Test-Path -LiteralPath $Exe)) { throw "找不到可执行文件：$Exe" }
        $candidates += (Resolve-Path -LiteralPath $Exe).Path
    }
    $candidates += (Join-Path $script:AnchorRoot 'build\Release\NeonBoard.exe')
    $candidates += (Join-Path $script:AnchorRoot 'dist\NeonBoard\NeonBoard.exe')
    # 任意 agent worktree 里构建过的新一份
    $worktrees = Join-Path $script:AnchorRoot '.worktrees'
    if (Test-Path -LiteralPath $worktrees) {
        $built = Get-ChildItem -LiteralPath $worktrees -Directory -ErrorAction SilentlyContinue |
            ForEach-Object {
                $path = Join-Path $_.FullName 'build\Release\NeonBoard.exe'
                if (Test-Path -LiteralPath $path) { Get-Item -LiteralPath $path }
            } |
            Sort-Object LastWriteTime -Descending
        foreach ($item in $built) { $candidates += $item.FullName }
    }

    $seen = @{}
    foreach ($candidate in $candidates) {
        if (-not (Test-Path -LiteralPath $candidate)) { continue }
        $key = (Resolve-Path -LiteralPath $candidate).Path
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        if (Test-PreviewCapable -Path $key -Needle $Needle) { return $key }
    }
    throw "找不到支持该操作的 NeonBoard.exe（$Needle，构建版本过旧），请重新构建或在 -Exe 里显式指定"
}

function Read-SharedFile {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    # 脱离启动的实例可能还攥着输出文件的句柄，用共享读打开
    $stream = [System.IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
    try {
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        return $reader.ReadToEnd()
    } finally {
        $stream.Dispose()
    }
}

function Invoke-PreviewCli {
    param([Parameter(Mandatory)][string[]]$CliArgs, [int]$WaitSeconds = 120, [string]$Needle = '--preview-list')

    $exePath = Resolve-PreviewExe -Needle $Needle
    $qtBin = Get-QtBinDir
    if ($qtBin) { $env:PATH = "$qtBin;$env:PATH" }
    $env:NEONBOARD_REPO_ROOT = $script:AnchorRoot

    $outFile = Join-Path ([System.IO.Path]::GetTempPath()) ("neonboard-preview-{0}.out" -f [guid]::NewGuid().ToString('N'))
    $errFile = "$outFile.err"
    try {
        # 不用 & + 管道：预览实例是脱离启动的子进程，会继承管道写句柄导致管道等不到 EOF 而挂死；
        # 重定向到文件 + 只等直接子进程，才能既拿到输出又不被孙进程拖住
        $proc = Start-Process -FilePath $exePath -ArgumentList $CliArgs -NoNewWindow -PassThru `
            -RedirectStandardOutput $outFile -RedirectStandardError $errFile
        if (-not $proc.WaitForExit($WaitSeconds * 1000)) {
            try { $proc.Kill() } catch { }
            throw "预览命令超时（$WaitSeconds 秒）：$($CliArgs -join ' ')"
        }
        $stdout = Read-SharedFile -Path $outFile
        $stderr = Read-SharedFile -Path $errFile
        return [pscustomobject]@{
            ExitCode = $proc.ExitCode
            StdOut   = $stdout
            StdErr   = $stderr
            Exe      = $exePath
        }
    } finally {
        Remove-Item -LiteralPath $outFile, $errFile -ErrorAction SilentlyContinue
    }
}

$script:AnchorRoot = Get-AnchorRoot

switch ($Action) {
    'List' {
        $result = Invoke-PreviewCli -CliArgs @('--preview-list')
        $json = $result.StdOut.Trim()
        if (-not $json) {
            Write-Host '✗ 预览中心没有返回数据，所选 exe 可能不支持 --preview-list（需重新构建）' -ForegroundColor Red
            exit 2
        }
        try {
            $data = $json | ConvertFrom-Json
        } catch {
            Write-Host '✗ 预览中心返回的数据无法解析' -ForegroundColor Red
            Write-Host $json -ForegroundColor DarkGray
            exit 2
        }
        $items = @()
        if ($data -is [array]) {
            $items = $data
        } elseif ($data.PSObject.Properties.Name -contains 'agents') {
            $items = @($data.agents)
        }
        Write-Host ("{0,-12} {1,-22} {2,-10} {3,-8} {4,-10} {5}" -f 'ID', '分支', 'HEAD', '已构建', '运行中', '备注') -ForegroundColor DarkGray
        foreach ($item in $items) {
            $head = if ($item.head) { $item.head.Substring(0, [Math]::Min(7, $item.head.Length)) } else { '' }
            $built = if ($item.built) { '是' } else { '否' }
            $running = if ($item.running) { "是($($item.pid))" } else { '否' }
            $note = @()
            if ($item.current) { $note += '当前实例' }
            if ($item.dirty) { $note += '有未提交改动' }
            if ($item.detached) { $note += '游离 HEAD' }
            Write-Host ("{0,-12} {1,-22} {2,-10} {3,-8} {4,-10} {5}" -f `
                $item.id, $item.branch, $head, $built, $running, ($note -join '、')) -ForegroundColor Gray
        }
        exit 0
    }

    default {
        if (-not $Agent) { throw '必须提供 -Agent（工作区 ID，可用 -Action List 查看）' }
        $cliArgs = switch ($Action) {
            'Build'   { @('--preview-build', $Agent) }
            'Package' { @('--preview-package', $Agent) }
            'Start'   { @('--preview-start', $Agent) }
            'Stop'    { @('--preview-stop', $Agent) }
        }
        $needle = switch ($Action) {
            'Build'   { '--preview-build' }
            'Package' { '--preview-package' }
            'Start'   { '--preview-start' }
            'Stop'    { '--preview-stop' }
        }
        $wait = if ($Action -eq 'Build' -or $Action -eq 'Package') { $TimeoutSeconds } else { 120 }
        Write-Host ("→ {0} {1}" -f $Action, $Agent) -ForegroundColor DarkGray
        $result = Invoke-PreviewCli -CliArgs $cliArgs -WaitSeconds $wait -Needle $needle
        $output = $result.StdOut.Trim()
        if ($result.ExitCode -eq 0) {
            Write-Host ("✓ {0} {1} 完成{2}" -f $Action, $Agent, $(if ($output) { "：$output" } else { '' })) -ForegroundColor Green
            exit 0
        }
        Write-Host ("✗ {0} {1} 失败（退出码 {2}）" -f $Action, $Agent, $result.ExitCode) -ForegroundColor Red
        if ($output) { Write-Host $output -ForegroundColor Red }
        if ($result.StdErr) { Write-Host $result.StdErr.Trim() -ForegroundColor DarkGray }
        exit 2
    }
}
