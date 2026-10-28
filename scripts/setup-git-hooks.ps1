<#
    setup-git-hooks.ps1 —— 让仓库钩子真正生效（幂等）

    为什么需要这一步：
      AGENTS.md 的所有强制条款都挂在 .githooks/ 下的钩子上，而钩子只有当
      core.hooksPath 指向它时才会被执行。这个值是**仓库级配置**，不随 clone 传递 ——
      漏配一次，分支保护、提交格式、任务闸门、台账防护就全部静默失效，
      章程退化为「写在文档里的建议」。本机实测就复现过：core.hooksPath 为空而无人察觉。

    写到哪儿：
      core.hooksPath 保存在「common dir」（所有 worktree 共享的那份 config），
      因此在任何一个 worktree 里执行一次都等效，重复执行无副作用。

    用法：
      pwsh -File scripts/setup-git-hooks.ps1            # 安装（幂等）
      pwsh -File scripts/setup-git-hooks.ps1 -Check     # 只校验，不一致退出码 1

    调用点（不要各自再写一遍判断）：
      scripts/agent-init.ps1 开区时 / scripts/build.ps1 构建前 / .github/workflows/ci.yml
#>
[CmdletBinding()]
param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'

# 仓库根按脚本自身路径定位（与本仓库其它脚本一致），锚点区与 worktree 里都能用
$repoRoot = ''
if ($PSScriptRoot) {
    $candidate = Split-Path -Parent $PSScriptRoot
    if ($candidate) {
        $repoRoot = (Resolve-Path -LiteralPath $candidate).Path
    }
}

$isRepo = $false
if ($repoRoot -and (Get-Command git -ErrorAction SilentlyContinue)) {
    $probe = (& git -C $repoRoot rev-parse --is-inside-work-tree 2>$null)
    $isRepo = ($LASTEXITCODE -eq 0)
}
if (-not $isRepo) {
    Write-Host '· 非 Git 仓库（或未安装 git），跳过钩子安装' -ForegroundColor DarkGray
    exit 0
}

$hooksDir = Join-Path $repoRoot '.githooks'
if (-not (Test-Path -LiteralPath $hooksDir)) {
    Write-Host "[钩子] 目录下没有 .githooks，无法安装：$hooksDir" -ForegroundColor Red
    exit 1
}

$desired = '.githooks'
$raw = (& git -C $repoRoot config --local --get core.hooksPath 2>$null)
$current = ''
if ($LASTEXITCODE -eq 0) { $current = ([string]$raw).Trim() }

if ($current -eq $desired) {
    Write-Host '· 仓库钩子已就位（core.hooksPath = .githooks）' -ForegroundColor DarkGray
} elseif ($Check) {
    Write-Host "[钩子] core.hooksPath 当前为「$current」，应为「$desired」" -ForegroundColor Red
    Write-Host '       修复： pwsh -File scripts/setup-git-hooks.ps1' -ForegroundColor Yellow
    exit 1
} else {
    & git -C $repoRoot config --local core.hooksPath $desired
    if ($LASTEXITCODE -ne 0) {
        Write-Host '[钩子] 写入 core.hooksPath 失败' -ForegroundColor Red
        exit 1
    }
    Write-Host '· 已安装仓库钩子（core.hooksPath = .githooks）' -ForegroundColor Green
}

# ---------- rerere：解冲突记忆（同为不随 clone 传递的仓库级配置）----------
# AGENTS.md §7.5 要求开启 rerere，但它和 core.hooksPath 一个性质：仓库级、不随 clone
# 走、漏了静默失效。只写在文档里靠每个 agent 手工开必然漂 —— 本机实测就是 R6 全程没人
# 开、直到收官才发现。本脚本是「仓库级 git 配置的唯一装配点」，rerere 就在这里一并装上。
# 幂等；-Check 只校验钩子路径，不因 rerere 缺失而红（CI 用不到它，它只是解冲突加速器）。
foreach ($key in @('core.rerere.enabled', 'core.rerere.autoupdate')) {
    $cur = (& git -C $repoRoot config --local --get $key 2>$null)
    $curVal = if ($LASTEXITCODE -eq 0) { ([string]$cur).Trim() } else { '' }
    if ($curVal -eq 'true') {
        if (-not $Check) { Write-Host ("· {0} 已就位" -f $key) -ForegroundColor DarkGray }
    } elseif ($Check) {
        Write-Host ("· {0} 未开启（不影响 CI，本地/多代理解冲突建议开）" -f $key) -ForegroundColor DarkGray
    } else {
        & git -C $repoRoot config --local $key 'true'
        if ($LASTEXITCODE -ne 0) {
            Write-Host ("[rerere] 写入 {0} 失败" -f $key) -ForegroundColor Red
            exit 1
        }
        Write-Host ("· 已开启 {0}" -f $key) -ForegroundColor Green
    }
}

# ---------- 体检：装了不等于能跑 ----------
# Windows 上钩子由 sh 执行：文件一旦被检出成 CRLF，shebang 会带上 \r 而找不到解释器，
# 表现是「钩子静静地不执行」——比没装更难排查，所以每次都顺手量一遍。
$hooks = @(Get-ChildItem -LiteralPath $hooksDir -File | Sort-Object Name)
if ($hooks.Count -eq 0) {
    Write-Host "[钩子] .githooks 下没有任何钩子文件" -ForegroundColor Yellow
    exit 0
}

$crlf = @()
foreach ($h in $hooks) {
    $bytes = [System.IO.File]::ReadAllBytes($h.FullName)
    foreach ($b in $bytes) {
        if ($b -eq 13) { $crlf += $h.Name; break }
    }
}
if ($crlf.Count -gt 0) {
    Write-Host "[钩子] 以下钩子含 CR，应为 LF：$($crlf -join '、')" -ForegroundColor Yellow
    Write-Host '       修复： git add --renormalize .githooks' -ForegroundColor Yellow
    if ($Check) { exit 1 }
}

# 非 Windows 平台钩子必须有执行位，否则同样「装了不跑」
if ($env:OS -ne 'Windows_NT') {
    foreach ($h in $hooks) {
        & chmod +x $h.FullName 2>$null
    }
}

Write-Host ("  钩子清单：{0}" -f ($hooks.Name -join ' / ')) -ForegroundColor DarkGray
exit 0
