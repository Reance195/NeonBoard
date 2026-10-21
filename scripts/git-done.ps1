<#
.SYNOPSIS
    NeonBoard 改动收尾助手 —— 一键把功能分支合并回主分支。

.DESCRIPTION
    按仓库宪章执行「拉分支 → 改动提交 → 合并回主分支」的收尾动作：
      1. 校验工作区干净
      2. 切回主分支
      3. 以 --no-ff 方式合并功能分支（保留分支痕迹，便于回溯）
      4. 删除已合并的本地功能分支

.EXAMPLE
    pwsh -File scripts/git-done.ps1
    pwsh -File scripts/git-done.ps1 -Branch feature/task-filter
    pwsh -File scripts/git-done.ps1 -KeepBranch
#>
[CmdletBinding()]
param(
    [string]$Branch = '',
    [switch]$KeepBranch,
    [string]$MainBranch = 'main'
)

$ErrorActionPreference = 'Stop'

function Invoke-Git {
    $output = & git @args 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw ("git {0} 执行失败（退出码 {1}）：`n{2}" -f ($args -join ' '), $code, ($output -join "`n"))
    }
    return $output
}

$repoRoot = (Invoke-Git rev-parse --show-toplevel).Trim()
Set-Location -LiteralPath $repoRoot
Write-Host "仓库根目录：$repoRoot" -ForegroundColor DarkGray

# ---- 1. 校验工作区 ----
$status = Invoke-Git status --porcelain
if ($status) {
    Write-Host "`n❌ 工作区存在未提交改动，请先提交或暂存：" -ForegroundColor Red
    $status | ForEach-Object { Write-Host "   $_" -ForegroundColor Yellow }
    Write-Host "`n参考：git add -A ; git commit -m 'feat(scope): 说明'`n" -ForegroundColor Yellow
    exit 1
}

# ---- 2. 确定要合并的分支 ----
if (-not $Branch) {
    $Branch = (Invoke-Git rev-parse --abbrev-ref HEAD).Trim()
}
if ($Branch -eq $MainBranch) {
    Write-Host "`n❌ 当前就在主分支 '$MainBranch' 上，无需合并。" -ForegroundColor Red
    exit 1
}

& git rev-parse --verify -q $Branch | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n❌ 分支 '$Branch' 不存在。" -ForegroundColor Red
    exit 1
}

# ---- 3. 切回主分支并合并 ----
Write-Host "`n▶ 切回主分支 $MainBranch ..." -ForegroundColor Cyan
Invoke-Git switch $MainBranch | Out-Null

Write-Host "▶ 合并 $Branch 到 $MainBranch（--no-ff）..." -ForegroundColor Cyan
try {
    Invoke-Git merge --no-ff $Branch -m "merge: 合入 $Branch" | Out-Null
} catch {
    Write-Host "`n❌ 合并失败，可能存在冲突：" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Yellow
    Write-Host "`n解决冲突后执行：git add -A ; git commit ; git branch -d $Branch`n" -ForegroundColor Yellow
    exit 1
}

# ---- 4. 清理分支 ----
if (-not $KeepBranch) {
    Invoke-Git branch -d $Branch | Out-Null
    Write-Host "▶ 已删除本地分支 $Branch" -ForegroundColor DarkGray
}

Write-Host "`n✅ 改动已合并回 $MainBranch" -ForegroundColor Green
Write-Host "   查看历史：git log --oneline --graph -10" -ForegroundColor DarkGray
Write-Host "   如需推送：git push origin $MainBranch`n" -ForegroundColor DarkGray
