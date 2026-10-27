# verify.ps1 —— 唯一验收入口：本地和 CI 用同一把尺子
#
# 四步：编译 → 单元测试(ctest) → 数据库自检(--selftest) → 冒烟启动(--smoke)
# 任务卡的 acceptance 就写「pwsh -File scripts/verify.ps1 全过」。
#
# 用法：
#   pwsh -File scripts/verify.ps1                    # 完整验收
#   pwsh -File scripts/verify.ps1 -SkipBuild         # 已构建过，只跑测试
#   pwsh -File scripts/verify.ps1 -Config Release -BuildDir build

param(
    [switch]$SkipBuild,
    [string]$QtRoot = '',
    [string]$BuildDir = 'build',
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Step([string]$name) {
    Write-Host ''
    Write-Host ("==== $name ====") -ForegroundColor Cyan
}
function Fail([string]$msg) {
    Write-Host "✗ $msg" -ForegroundColor Red
    exit 1
}

# ---------- 1. 构建 ----------
if (-not $SkipBuild) {
    Step '编译'
    $buildArgs = @('-NoProfile', '-File', 'scripts/build.ps1', '-Config', $Config, '-BuildDir', $BuildDir)
    if ($QtRoot) { $buildArgs += @('-QtRoot', $QtRoot) }
    & pwsh @buildArgs
    if ($LASTEXITCODE -ne 0) { Fail "编译失败（退出码 $LASTEXITCODE）" }
} else {
    Step '编译（已跳过）'
}

# ---------- 定位产物 ----------
$candidates = @(
    (Join-Path $repoRoot "$BuildDir\$Config\NeonBoard.exe"),   # VS 多配置
    (Join-Path $repoRoot "$BuildDir\NeonBoard.exe")           # Ninja 单配置
)
$exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { Fail "找不到 NeonBoard.exe（找过：$($candidates -join ' , ')）" }
Write-Host "产物: $exe" -ForegroundColor DarkGray

# Qt DLL 不在系统 PATH 时补上
if ($QtRoot) { $env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH }

# ---------- 2. 单元测试 ----------
Step '单元测试（ctest）'
& ctest --test-dir $BuildDir -C $Config --output-on-failure
if ($LASTEXITCODE -ne 0) { Fail '单元测试未通过' }

# ---------- 3. 数据库自检 ----------
Step '数据库自检（--selftest）'
$out = Join-Path $env:TEMP 'neonboard-selftest.out'
$err = Join-Path $env:TEMP 'neonboard-selftest.err'
$proc = Start-Process -FilePath $exe -ArgumentList '--selftest' -Wait -PassThru -NoNewWindow `
    -RedirectStandardOutput $out -RedirectStandardError $err
Get-Content $out -ErrorAction SilentlyContinue
Get-Content $err -ErrorAction SilentlyContinue
if ($proc.ExitCode -ne 0) { Fail "--selftest 失败（退出码 $($proc.ExitCode)）" }

# ---------- 4. 冒烟启动 ----------
Step '冒烟启动（--smoke，45 秒上限）'
# 日志跟着数据目录走（AppPaths::logPath = dataDir()/neonboard.log）：写死 %APPDATA%
# 时，沙箱运行读不到本次日志，而未沙箱又会去删真实实例正占用的日志，Remove-Item 直接失败。
$dataDir = if ($env:NEONBOARD_DATA_DIR) { $env:NEONBOARD_DATA_DIR } else { Join-Path $env:APPDATA 'com.lin\NeonBoard' }
$log = Join-Path $dataDir 'neonboard.log'
if (Test-Path $log) { Remove-Item $log -Force }
$proc = Start-Process -FilePath $exe -ArgumentList '--smoke' -PassThru
$deadline = (Get-Date).AddSeconds(45)
while (-not $proc.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
if (-not $proc.HasExited) {
    Stop-Process -Id $proc.Id -Force
    Fail '冒烟启动超时（45 秒未退出）'
}
if (-not (Test-Path $log)) { Fail '未生成运行日志' }

$patterns = 'is not a type|is not a member|TypeError|ReferenceError|' +
            'Unable to assign|Cannot assign|QQmlApplicationEngine failed|' +
            '不是一种类型|不是一个类型|不存在的附加对象|没有安装|' +
            'ASSERT|FATAL|Segmentation'
$lines = [System.IO.File]::ReadAllLines($log, [System.Text.Encoding]::UTF8)
$bad = $lines | Where-Object { $_ -match $patterns }
if ($bad) {
    $bad | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    Fail "冒烟日志检测到致命错误（$($bad.Count) 行）"
}

Write-Host ''
Write-Host '✓ 验收通过：编译 / ctest / --selftest / --smoke 全过' -ForegroundColor Green
exit 0
