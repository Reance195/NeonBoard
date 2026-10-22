<#
    NeonBoard 多代理合并助手 —— 合并分支回主分支并清理

    两种模式：

      remote（默认，多代理并行用）
        合并不再依赖主 worktree：在当前 worktree 里从 origin/main 拉临时分支，
        git merge --no-ff <分支>，再 git push origin HEAD:main（快进推送）。
        远端的快进保护天然完成串行化 —— 两人同时推，后一个会被 non-fast-forward 拒绝，
        重新同步后再合并即可，根目录因此不需要被任何 agent 占用。

      local（传统方式，需要能 checkout main，一般在根目录执行）
        切换到 main → --no-ff 合并 → 删除分支。

    用法：
      pwsh -File scripts/git-done.ps1
      pwsh -File scripts/git-done.ps1 -Branch feature/task-filter
      pwsh -File scripts/git-done.ps1 -RemoveWorktree .worktrees/agent-c
      pwsh -File scripts/git-done.ps1 -Mode local
      pwsh -File scripts/git-done.ps1 -KeepBranch
#>
[CmdletBinding()]
param(
    [string]$Branch = '',
    [ValidateSet('remote', 'local')]
    [string]$Mode = 'remote',
    [switch]$KeepBranch,
    [switch]$RemoveWorktree,
    [string]$MainBranch = 'main',
    [string]$Worktree = ''
)

$ErrorActionPreference = 'Stop'

function Invoke-Git {
    $output = & git @args 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw ("git {0} 执行失败（{1}）：`n{2}" -f ($args -join ' '), $code, ($output -join "`n"))
    }
    return $output
}

$repoRoot = (Invoke-Git rev-parse --show-toplevel).Trim()
Set-Location -LiteralPath $repoRoot
Write-Host "仓库目录：$repoRoot" -ForegroundColor DarkGray

# ---- 1. 工作区必须干净 ----
$status = Invoke-Git status --porcelain
if ($status) {
    Write-Host "`n✗ 当前工作区有未提交改动，请先提交：" -ForegroundColor Red
    $status | ForEach-Object { Write-Host "   $_" -ForegroundColor Yellow }
    Write-Host "`n参考：git add -A ; git commit -m 'feat(scope): 说明'`n" -ForegroundColor Yellow
    exit 1
}

# ---- 2. 确认要合并的分支 ----
if (-not $Branch) {
    $Branch = (Invoke-Git rev-parse --abbrev-ref HEAD).Trim()
}
if ($Branch -eq $MainBranch) {
    Write-Host "`n✗ 当前分支就是主分支 '$MainBranch'，没有可合并的内容" -ForegroundColor Red
    exit 1
}

& git rev-parse --verify -q $Branch | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n✗ 分支 '$Branch' 不存在或已删除" -ForegroundColor Red
    exit 1
}

Write-Host "`n→ 合并分支 '$Branch'（模式：$Mode）" -ForegroundColor Cyan

if ($Mode -eq 'remote') {
    # ---- 3. remote 模式：临时分支 + 快进推送，不碰主 worktree ----
    Invoke-Git fetch origin --quiet | Out-Null
    $originMain = (Invoke-Git rev-parse "origin/$MainBranch").Trim()

    $tmp = 'merge/' + ($Branch -replace '[^A-Za-z0-9]+', '-')
    & git rev-parse --verify -q $tmp | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "· 清理残留临时分支 $tmp" -ForegroundColor DarkGray
        Invoke-Git branch -D $tmp | Out-Null
    }

    Write-Host "· 基于 origin/$MainBranch 创建临时分支 $tmp" -ForegroundColor DarkGray
    Invoke-Git switch -c $tmp "origin/$MainBranch" | Out-Null

    Write-Host "· 合并 $Branch（--no-ff，保留分支合并痕迹）" -ForegroundColor DarkGray
    try {
        Invoke-Git merge --no-ff $Branch -m "merge: 合入 $Branch" | Out-Null
    } catch {
        Write-Host "`n✗ 合并冲突，请手工解决后继续：" -ForegroundColor Yellow
        Write-Host $_.Exception.Message -ForegroundColor Yellow
        Write-Host "`n解决后：git add -A ; git commit ; git push origin HEAD:$MainBranch`n" -ForegroundColor Yellow
        exit 1
    }

    Write-Host "· 快进推送 $tmp → origin/$MainBranch" -ForegroundColor DarkGray
    try {
        Invoke-Git push origin "HEAD:$MainBranch" | Out-Null
    } catch {
        Write-Host "`n✗ 推送主分支失败（多半是别人刚推了新提交）：" -ForegroundColor Yellow
        Write-Host $_.Exception.Message -ForegroundColor Yellow
        Write-Host "`n恢复步骤：`n  git fetch origin`n  git switch -c merge/retry origin/$MainBranch`n  git merge --no-ff $Branch -m `"merge: 合入 $Branch`"`n  git push origin HEAD:$MainBranch`n" -ForegroundColor Yellow
        exit 1
    }

    $merged = (Invoke-Git rev-parse HEAD).Trim()
    Write-Host ("✓ 已推送到 origin/{0}（{1}）" -f $MainBranch, $merged.Substring(0, 7)) -ForegroundColor Green

    # ---- 4. 清理：先回收 worktree（解除分支占用），再删分支 ----
    $wtPath = $Worktree
    if (-not $wtPath) {
        $blocks = ((& git worktree list --porcelain) -join "`n") -split "`n`n"
        foreach ($block in $blocks) {
            if ($block -match ('branch refs/heads/' + [regex]::Escape($Branch))) {
                $m = [regex]::Match($block, 'worktree (.+)')
                if ($m.Success) { $wtPath = $m.Groups[1].Value.Trim() }
            }
        }
    }

    if ($RemoveWorktree -and $wtPath) {
        $abs = if ([System.IO.Path]::IsPathRooted($wtPath)) { $wtPath } else { Join-Path $repoRoot $wtPath }
        $abs = [System.IO.Path]::GetFullPath($abs)
        $currentRoot = [System.IO.Path]::GetFullPath($repoRoot)
        if ($abs -eq $currentRoot) {
            Write-Host "· 目标 worktree 就是当前目录，跳过回收（请在其它目录执行 git worktree remove）" -ForegroundColor Yellow
        } elseif (Test-Path -LiteralPath $abs) {
            Write-Host ("· 回收 worktree {0}" -f $abs) -ForegroundColor DarkGray
            & git worktree remove $abs 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) {
                & git worktree remove --force $abs 2>&1 | Out-Null
            }
        }
    }

    if (-not $KeepBranch) {
        Write-Host ("· 删除分支 {0}" -f $Branch) -ForegroundColor DarkGray
        & git branch -d $Branch 2>&1 | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        if ($LASTEXITCODE -ne 0) {
            Write-Host "   （分支可能仍被某个 worktree 占用，先 git worktree remove 再删）" -ForegroundColor DarkGray
        }
    }

    # 解除临时分支占用后一并删除，工作区停在分离 HEAD 上，不留悬挂分支
    & git switch --detach 2>&1 | Out-Null
    & git branch -d $tmp 2>&1 | Out-Null
    Write-Host "`n✓ 合并完成：$Branch → origin/$MainBranch" -ForegroundColor Green
    Write-Host "   查看历史：git log --oneline --graph -10" -ForegroundColor DarkGray
    exit 0
}

# ---- local 模式：传统流程（需要能 checkout main） ----
Write-Host "`n· 切换主分支 $MainBranch ..." -ForegroundColor Cyan
Invoke-Git switch $MainBranch | Out-Null

Write-Host "· 合并 $Branch（--no-ff）..." -ForegroundColor Cyan
try {
    Invoke-Git merge --no-ff $Branch -m "merge: 合入 $Branch" | Out-Null
} catch {
    Write-Host "`n✗ 合并冲突，请手工解决：" -ForegroundColor Yellow
    Write-Host $_.Exception.Message -ForegroundColor Yellow
    Write-Host "`n解决后：git add -A ; git commit ; git branch -d $Branch`n" -ForegroundColor Yellow
    exit 1
}

if (-not $KeepBranch) {
    Write-Host "· 删除分支 $Branch" -ForegroundColor DarkGray
    Invoke-Git branch -d $Branch | Out-Null
    Write-Host "✓ 已删除分支 $Branch" -ForegroundColor DarkGray
}

Write-Host "`n✓ 合并完成：$Branch → $MainBranch" -ForegroundColor Green
Write-Host "   查看历史：git log --oneline --graph -10" -ForegroundColor DarkGray
Write-Host "   推送主分支：git push origin $MainBranch`n" -ForegroundColor DarkGray
exit 0
