# check-format.ps1 —— 只检查本次改动涉及的 C++ 文件（棘轮策略）
#
# 用途：CI 里挡住「新增/修改的 C++ 代码不符合 .clang-format」的情况。
# 现存历史文件不做一次性重排，避免 1 万多行的格式提交把历史冲乱；
# 文件一旦被本次改动触及，就必须整体符合格式。
#
# 用法：
#   pwsh -File scripts/check-format.ps1                  # 默认与 origin/main 比较（没有则比 HEAD~1）
#   pwsh -File scripts/check-format.ps1 -Base HEAD~1
#   pwsh -File scripts/check-format.ps1 -All             # 检查全部源码（用于一次性整改评估）

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
if (-not $exe) {
    $exe = (Get-Command clang-format.exe -ErrorAction SilentlyContinue).Source
}
if (-not $exe) {
    throw "找不到 clang-format，请先安装（python -m pip install clang-format 或 choco install llvm）"
}
Write-Host "clang-format: $(& $exe --version)"

# ---- 收集待检查文件 ----
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
    Write-Host "比较基线: $Base"
    $changed = git diff --name-only --diff-filter=ACMR "$Base...HEAD" 2>$null
    if (-not $changed) { $changed = git diff --name-only "$Base" HEAD 2>$null }
    $files = @()
    foreach ($f in $changed) {
        if ($exts -contains [IO.Path]::GetExtension($f)) {
            $full = Join-Path $repoRoot $f
            if (Test-Path -LiteralPath $full) { $files += $full }
        }
    }
}

if (-not $files -or $files.Count -eq 0) {
    Write-Host '本次改动没有 C++ 文件，跳过格式检查' -ForegroundColor Green
    exit 0
}

Write-Host "待检查 $($files.Count) 个文件"

# ---- 逐个检查（--dry-run --Werror：有差异即非零退出）----
$bad = @()
foreach ($f in $files) {
    $rel = [IO.Path]::GetRelativePath($repoRoot, $f)
    & $exe --style=file --dry-run --Werror $f 2>&1 | ForEach-Object { $null = $_ }
    if ($LASTEXITCODE -ne 0) { $bad += $rel }
}

if ($bad.Count -gt 0) {
    Write-Host ''
    Write-Host "以下 $($bad.Count) 个文件不符合 .clang-format：" -ForegroundColor Red
    $bad | ForEach-Object { Write-Host "  - $_" }
    Write-Host ''
    Write-Host '修复：pwsh -File scripts/format.ps1   （默认只格式化本次改动涉及的文件）' -ForegroundColor Yellow
    exit 1
}

Write-Host "格式检查通过（$($files.Count) 个文件）" -ForegroundColor Green
exit 0
