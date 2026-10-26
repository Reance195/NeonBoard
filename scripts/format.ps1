# format.ps1 —— 用 clang-format 就地格式化源码
#
# 默认只格式化「本次改动涉及」的 C++ 文件（与 check-format.ps1 的棘轮策略配套）；
# -All 则格式化 src/ 与 tests/ 下全部 C++ 文件（一次性整改用，会产生大 diff）。
#
# 用法：
#   pwsh -File scripts/format.ps1              # 只格式化改动过的文件
#   pwsh -File scripts/format.ps1 -Base HEAD~1 # 指定比较基线
#   pwsh -File scripts/format.ps1 -All         # 全量格式化（慎用）

param(
    [string]$Base = '',
    [switch]$All,
    [string]$ClangFormat = 'clang-format'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$exe = (Get-Command $ClangFormat -ErrorAction SilentlyContinue).Source
if (-not $exe) { $exe = (Get-Command clang-format.exe -ErrorAction SilentlyContinue).Source }
if (-not $exe) { throw "找不到 clang-format，请先安装（python -m pip install clang-format）" }
Write-Host "clang-format: $(& $exe --version)"

$exts = @('.cpp', '.h', '.hpp', '.cc', '.cxx')
if ($All) {
    $files = Get-ChildItem 'src', 'tests' -Recurse -File |
        Where-Object { $exts -contains $_.Extension } |
        ForEach-Object { $_.FullName }
} else {
    if (-not $Base) {
        if (git rev-parse --verify origin/main 2>$null) { $Base = 'origin/main' }
        elseif (git rev-parse --verify HEAD~1 2>$null) { $Base = 'HEAD~1' }
        else { Write-Host '没有可比较的基线，跳过'; exit 0 }
    }
    $changed = git diff --name-only "$Base" HEAD 2>$null
    $files = @()
    foreach ($f in $changed) {
        if ($exts -contains [IO.Path]::GetExtension($f)) {
            $full = Join-Path $repoRoot $f
            if (Test-Path -LiteralPath $full) { $files += $full }
        }
    }
}

if (-not $files) { Write-Host '没有需要格式化的文件' -ForegroundColor Green; exit 0 }

$n = 0
foreach ($f in $files) {
    & $exe --style=file -i $f
    $n++
}
Write-Host "已格式化 $n 个文件；请重新暂存并提交" -ForegroundColor Green
