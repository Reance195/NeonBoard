# verify.ps1 —— 唯一验收入口：本地和 CI 用同一把尺子
#
# 两种口径（DX-15）：
#   全量（默认）：编译 → 单元测试(ctest) → 数据库自检(--selftest) → 冒烟启动(--smoke)
#                 → 模块守卫 → 看板一致性(derive --check)
#   脚本层(-ScriptOnly)：PowerShell 语法解析 → 脚本卫生(check_script_hygiene)
#                 → 工作流 YAML 解析 → 范围检查(check_scope) → 路径引用(check_path_refs)
#                 「scope 内没有 .cpp/.h/.qml/CMakeLists.txt」的卡片走这一档：
#                 这些检查与编译无关，却要在冷启动 worktree 里付 20~30 分钟编译费才拿得到
#                 （DX-13 实施实测）。全量编译仍由 CI 兜底，门槛不降。
#                 两种口径的退出码语义一致：0 通过 / 1 失败。
#
# 两处自兜底（本脚本的定位是「拿到就能跑」，不该要求调用方先记住别的）：
#   - PATH 上没有 ctest 时点源 dev-env.ps1 装配工具链；
#   - 未设 NEONBOARD_DATA_DIR 时自开 <BuildDir>\verify-sandbox，不拿真实看板做试验。
#
# 用法：
#   pwsh -File scripts/verify.ps1                    # 完整验收
#   pwsh -File scripts/verify.ps1 -SkipBuild         # 已构建过，只跑测试
#   pwsh -File scripts/verify.ps1 -ScriptOnly        # 纯脚本/配置类卡片的脚本层验收
#   pwsh -File scripts/verify.ps1 -Config Release -BuildDir build

param(
    [switch]$SkipBuild,
    [switch]$ScriptOnly,
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

# 拉起 NeonBoard.exe 并等它真的退出，返回退出码。
# 两个坑叠在一起，决定了这个函数只能这么写：
#   1) 不能用 Start-Process —— 它重建子进程环境块，机器上同时存在 HTTP_PROXY 与 http_proxy
#      （仅大小写不同）时，.NET 环境字典按大小写不敏感加键，抛「已添加项…」，Windows
#      PowerShell 5.1 下必现（DX-12 实测：旧代码正是死在这里）。
#   2) 也不能用调用运算符 `& $exe` —— 本程序是 GUI 子系统（WIN32_EXECUTABLE），PowerShell
#      对 GUI 程序不等待、不设 $LASTEXITCODE，读到的是上一条命令的残留值。实测过：selftest
#      崩了也会显示「退出码 0」，是最坏的那种假通过。
# UseShellExecute=$true 走 ShellExecuteEx：不重建环境块，且返回的 Process 自带句柄，
# 因此能 WaitForExit + 强杀 + 拿 ExitCode。5.1 / 7 两代实测均可用。
function Invoke-Exe {
    param([string]$FilePath, [string]$Arguments, [int]$TimeoutSec, [string]$What)

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = $Arguments
    $psi.UseShellExecute = $true
    $psi.WindowStyle = 'Hidden'
    $proc = [System.Diagnostics.Process]::Start($psi)
    if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
        try { $proc.Kill() } catch { }
        Fail ("{0} 超时（{1} 秒未退出）" -f $What, $TimeoutSec)
    }
    return $proc.ExitCode
}

# ---------- 脚本层验收（-ScriptOnly） ----------
# 与编译无关的五项：语法解析 / 脚本卫生 / 工作流 YAML / 范围 / 路径引用。
# 判定规则见 docs/task-card.md「两类验收口径」：scope 内没有 .cpp/.h/.qml/CMakeLists.txt
# 即脚本类。为防止「脚本卡偷偷改了代码」，第一项先自查改动清单 —— 有代码就拒绝并指回全量。
if ($ScriptOnly) {
    $scriptBase = 'origin/main'
    $hasBase = [bool](git rev-parse --verify -q origin/main 2>$null)
    if (-not $hasBase) { $scriptBase = 'HEAD~1' }

    Step '口径自检（改动里有没有代码路径）'
    $codeExt = @('.cpp', '.h', '.hpp', '.cc', '.cxx', '.qml', '.qrc', '.ui', '.js')
    $changed = @()
    if ($hasBase) {
        $changed = @(git diff --name-only --diff-filter=ACMR "$scriptBase...HEAD" 2>$null)
    }
    $codeHits = @()
    foreach ($f in $changed) {
        if (-not $f) { continue }
        if (($codeExt -contains [IO.Path]::GetExtension($f)) -or
            ((Split-Path -Leaf $f) -eq 'CMakeLists.txt')) {
            $codeHits += $f
        }
    }
    if ($codeHits.Count -gt 0) {
        Write-Host ("  含代码路径 {0} 个：" -f $codeHits.Count) -ForegroundColor DarkGray
        $codeHits | Select-Object -First 8 | ForEach-Object { Write-Host "    $_" }
        Fail '本次改动涉及代码，-ScriptOnly 不适用；请跑 pwsh -File scripts/verify.ps1（全量）'
    }
    Write-Host ("  ✓ 改动 {0} 个文件，无代码路径" -f $changed.Count) -ForegroundColor DarkGray

    Step 'PowerShell 语法解析（scripts/*.ps1）'
    $psFiles = @(Get-ChildItem (Join-Path $repoRoot 'scripts') -Filter '*.ps1' -File)
    $syntaxBad = 0
    foreach ($f in $psFiles) {
        $tokens = $null
        $parseErrors = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile(
            $f.FullName, [ref]$tokens, [ref]$parseErrors)
        if ($parseErrors -and $parseErrors.Count -gt 0) {
            $syntaxBad++
            Write-Host ("  ✗ {0}：{1} 处语法错误" -f $f.Name, $parseErrors.Count) -ForegroundColor Red
            $parseErrors | Select-Object -First 3 | ForEach-Object {
                Write-Host ("      L{0} {1}" -f $_.Extent.StartLineNumber, $_.Message) -ForegroundColor Red
            }
        }
    }
    if ($syntaxBad -gt 0) { Fail "$syntaxBad 个脚本存在语法错误" }
    Write-Host ("  ✓ {0} 个脚本语法正确" -f $psFiles.Count) -ForegroundColor DarkGray

    Step '脚本卫生（check_script_hygiene）'
    & python (Join-Path $repoRoot 'scripts\check_script_hygiene.py')
    if ($LASTEXITCODE -ne 0) { Fail '脚本卫生检查未通过' }

    Step '工作流 YAML 解析（.github/workflows/*.yml）'
    # 提示语只用 ASCII：这段程序的输出要被 capture 回来再打印，PowerShell 用控制台代码页
    # 解码子进程输出，中文会变乱码（「3 涓?workflow」）。少一类 bug 比多一行中文划算。
    $yamlProg = @'
import glob
import sys
paths = sorted(glob.glob(".github/workflows/*.yml"))
try:
    import yaml
except Exception:
    print("SKIP PyYAML not installed (pip install pyyaml to enable)")
    sys.exit(0)
bad = []
for p in paths:
    try:
        with open(p, encoding="utf-8") as fh:
            yaml.safe_load(fh)
    except Exception as exc:
        bad.append((p, exc))
for p, exc in bad:
    print("X %s: %s" % (p, exc))
print("OK %d workflow file(s)" % len(paths))
sys.exit(1 if bad else 0)
'@
    $yamlOut = $yamlProg | & python - 2>&1
    $yamlCode = $LASTEXITCODE
    $yamlOut | ForEach-Object { Write-Host "  $_" }
    if ($yamlCode -ne 0) { Fail '工作流 YAML 解析失败' }

    Step '范围检查（check_scope）'
    & python (Join-Path $repoRoot 'scripts\check_scope.py') --base $scriptBase
    if ($LASTEXITCODE -ne 0) { Fail '改动越出卡片 scope（或范围检查无法完成）' }

    Step '路径引用（check_path_refs）'
    # 棘轮口径：只报本次改动新增的失效引用（存量折叠计数），与 check_scope / check-format
    # 同一策略 —— 一开就满屏红的闸门会被绕过。
    & python (Join-Path $repoRoot 'scripts\check_path_refs.py') --base $scriptBase
    if ($LASTEXITCODE -ne 0) { Fail '新增了指向不存在的 src/ 文件的引用（拆目录后未回改）' }

    Write-Host ''
    Write-Host '✓ 脚本层验收通过：语法 / 卫生 / YAML / 范围 / 路径引用 全过（未触发编译）' -ForegroundColor Green
    exit 0
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
# 走 Invoke-Exe（定义见文件头）：Start-Process 在环境块有大小写重名变量时会抛「已添加项」，
# 而调用运算符对 GUI 子系统的程序不等待、不设 $LASTEXITCODE —— 后者更危险，会把崩溃显示成
# 「退出码 0」。这里要的是真退出码，两种宿主下都成立。
$selfCode = Invoke-Exe -FilePath $exe -Arguments '--selftest' -TimeoutSec 180 -What '--selftest'
if ($selfCode -ne 0) { Fail "--selftest 失败（退出码 $selfCode）" }
Write-Host '  ✓ --selftest 已执行并正常退出（退出码 0）' -ForegroundColor DarkGray

# ---------- 4. 冒烟启动 ----------
Step '冒烟启动（--smoke，45 秒上限）'
# 日志由 AppPaths::logPath() 落在数据目录里，口径统一走 nb-paths.ps1
$log = Get-NbLogFile
if (Test-Path $log) { Remove-Item $log -Force }
# 同样走 Invoke-Exe：45 秒兜底 + 超时强杀（GUI 程序卡在缺 DLL 的对话框上时，这是唯一出路）
$smokeCode = Invoke-Exe -FilePath $exe -Arguments '--smoke' -TimeoutSec 45 -What '冒烟启动'
Write-Host ("  ✓ --smoke 已执行并自行退出（退出码 {0}）" -f $smokeCode) -ForegroundColor DarkGray
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


# ---------- 5. 模块依赖方向与行数（MOD-05 守卫） ----------
Step '模块依赖方向与行数（check_module_deps）'
# 与 ci.yml 同一个脚本、同一把尺：只查本次改动的文件（棘轮），存量不阻塞。
# 层序见 docs/module-split-plan.md §3.2；行数上限只许减不许增（main.cpp / Main.qml）。
$depsScript = Join-Path $repoRoot 'scripts\check_module_deps.py'
if ((Test-Path $depsScript) -and (Get-Command python -ErrorAction SilentlyContinue)) {
    & python $depsScript --base origin/main
    if ($LASTEXITCODE -ne 0) {
        Fail "模块依赖方向或行数上限未通过（退出码 $LASTEXITCODE）：本次改动引入了反向依赖，或撑破了行数上限"
    }
} else {
    Write-Host '· 跳过：未找到 scripts/check_module_deps.py 或 python' -ForegroundColor DarkGray
}

# ---------- 6. 看板状态一致性（DX-13） ----------
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
Write-Host '✓ 验收通过：编译 / ctest / --selftest / --smoke / 模块守卫 / 看板一致性 全过' -ForegroundColor Green
exit 0
