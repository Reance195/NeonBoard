# verify.ps1 —— 唯一验收入口：本地和 CI 用同一把尺子
#
# 五步：编译 → 单元测试(ctest) → 数据库自检(--selftest) → 冒烟启动(--smoke) → 看板一致性(derive --check)
# 任务卡的 acceptance 就写「pwsh -File scripts/verify.ps1 全过」。
#
# 两处自兜底（本脚本的定位是「拿到就能跑」，不该要求调用方先记住别的）：
#   - PATH 上没有 ctest 时点源 dev-env.ps1 装配工具链；
#   - 未设 NEONBOARD_DATA_DIR 时自开 <BuildDir>\verify-sandbox，不拿真实看板做试验。
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
# 应用落盘路径的唯一口径（Get-NbLogFile / Get-NbBoardDb），不要在本文件里再拼一次
. (Join-Path $PSScriptRoot 'nb-paths.ps1')

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

# ---------- 工具链兜底 ----------
if (-not (Get-Command ctest -ErrorAction SilentlyContinue)) {
    # 编译那步走 build.ps1 子进程（它自带环境探测），ctest 却在本会话里跑：
    # 调用方忘 dot-source dev-env.ps1 的症状是「ctest 不是命令」——
    # 既然本脚本的定位是「唯一验收入口」，就别再要求人先记住一件事。
    Step '装配构建环境（dev-env.ps1）'
    . (Join-Path $PSScriptRoot 'dev-env.ps1') -Quiet
}

# ---------- 沙箱兜底 ----------
if (-not $env:NEONBOARD_DATA_DIR) {
    # --selftest 会写库、--smoke 会写日志与界面设置：不设沙箱时这些全落进
    # %APPDATA% 的真实看板（路线图快照还会写回仓库 progress.json）。
    # 调用方已经设过就尊重它，不覆盖（agent 工作区由 .agent-env.ps1 设）。
    $sandbox = Join-Path (Join-Path $repoRoot $BuildDir) 'verify-sandbox'
    New-Item -ItemType Directory -Path $sandbox -Force | Out-Null
    $env:NEONBOARD_DATA_DIR = $sandbox
    $env:NEONBOARD_INSTANCE = 'verify'
    $env:NEONBOARD_ROADMAP = (Join-Path $sandbox 'progress.json')
    $env:NEONBOARD_ROADMAP_AUTOWRITE = '0'
    Write-Host "沙箱数据目录: $sandbox" -ForegroundColor DarkGray
}

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
# 日志由 AppPaths::logPath() 落在数据目录里，口径统一走 nb-paths.ps1
$log = Get-NbLogFile
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


# ---------- 5. 看板状态一致性（DX-13） ----------
Step '看板状态一致性（derive --check）'
# progress.json 是「git 标记 + 台账」的派生产物（§11），漂移的判据只有一个：
# derive --check 退出 1 并报「待同步：卡片 X 状态 a → b」。
# 只在主干上阻断：特性分支按既有约定完全不碰 progress.json（ci.yml 原话），
# 分支上带 #短号=done 而快照未重算是正常的，那里只告警不拦验收。
$deriveScript = Join-Path $repoRoot 'scripts\derive_board_status.py'
if ((Test-Path $deriveScript) -and (Get-Command python -ErrorAction SilentlyContinue)) {
    & python $deriveScript --repo $repoRoot --check
    $deriveCode = $LASTEXITCODE
    if ($deriveCode -ne 0) {
        $onMain = ((git rev-parse --abbrev-ref HEAD).Trim() -eq 'main')
        $hint = 'python scripts/derive_board_status.py --write'
        if ($onMain) {
            Fail "看板状态与 git 历史/台账不一致（退出码 $deriveCode），请先跑 $hint 并提交"
        } else {
            Write-Host ("! 看板快照落后于派生结果（退出码 {0}）；合并收尾会由 git-done 自动刷新，或手工跑 {1}" -f $deriveCode, $hint) -ForegroundColor Yellow
        }
    }
} else {
    Write-Host '· 跳过：未找到 scripts/derive_board_status.py 或 python' -ForegroundColor DarkGray
}

Write-Host ''
Write-Host '✓ 验收通过：编译 / ctest / --selftest / --smoke / 看板一致性 全过' -ForegroundColor Green
exit 0
