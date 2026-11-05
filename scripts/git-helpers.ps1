# git-helpers.ps1 —— git 符号引用取值的空值安全封装（单一实现，DX-63）
#
# 同一类「git 无输出 → 对空引用调方法」的崩溃出现三次（DX-49 漂移守卫两处、
# DX-50 演练自抓两处、DX-59 agent-init 空引用一处）后按 Z6 收口：取值只留
# 这一份实现，其余脚本点源使用；check_script_hygiene.py 规则 E 同步禁止
# 别处再写裸 `[string](& git ...)` 强转捕获 —— 此后这一类 bug 物理上写不出来。
#
# 用法（点源，函数在点源后生效）：
#     . (Join-Path $PSScriptRoot 'git-helpers.ps1')
# 独立自测（覆盖 空输出 / 多行 / 分离 HEAD / 非仓库目录 四态）：
#     pwsh -File scripts/git-helpers.ps1 -SelfTest

param([switch]$SelfTest)

function Get-NbGitLine {
    # git 单行取值的唯一空值语义：出错 / 无输出返回空串，多行取首行，去首尾空白。
    # 本文件的四个语义封装都走它；四种语义之外的新取值需求也从这里延伸。
    param([string]$Cwd, [Parameter(Mandatory = $true)][string[]]$GitArgs)
    $argv = @()
    if ($Cwd) { $argv += @('-C', $Cwd) }
    $argv += $GitArgs
    $out = & git @argv 2>$null
    if (-not $out) { return '' }
    return ([string]($out | Select-Object -First 1)).Trim()
}

function Get-NbSymRef {
    # 当前分支的符号引用名（refs/heads/... 的 short 形）；分离 HEAD / 非仓库 → 空串。
    param([string]$Cwd)
    return Get-NbGitLine -Cwd $Cwd -GitArgs @('symbolic-ref', '--quiet', '--short', 'HEAD')
}

function Get-NbBranch {
    # 当前分支名；分离 HEAD → 空串（--abbrev-ref 对分离 HEAD 返回 "HEAD"，调用方
    # 靠空串 / "HEAD" 都能判分离 —— 与 verify.ps1 既有的判定口径一致）。
    param([string]$Cwd)
    return Get-NbGitLine -Cwd $Cwd -GitArgs @('rev-parse', '--abbrev-ref', 'HEAD')
}

function Get-NbToplevel {
    # 调用方（或 -Cwd 指定检出）的工作树顶层路径；非仓库 → 空串。
    param([string]$Cwd)
    return Get-NbGitLine -Cwd $Cwd -GitArgs @('rev-parse', '--show-toplevel')
}

function Get-NbCommonDir {
    # 漂移守卫用的 git common dir：优先绝对路径输出，老 git 只给相对路径时按
    # RepoPath 拼成绝对路径；非仓库 → $null（fail-fast 还是 fail-soft 由调用方定）。
    # 行为与被收口的 agent-init / git-done 两份本地实现逐字一致（Z3：不改既有语义）。
    param([string]$RepoPath)
    $raw = & git -C $RepoPath rev-parse --path-format=absolute --git-common-dir 2>$null
    $p = if ($raw) { ([string]$raw).Trim() } else { '' }
    if (-not $p) {
        $raw = & git -C $RepoPath rev-parse --git-common-dir 2>$null
        $p = if ($raw) { ([string]$raw).Trim() } else { '' }
        if (-not $p) { return $null }
        if (-not [System.IO.Path]::IsPathRooted($p)) { $p = Join-Path $RepoPath $p }
    }
    return ($p -replace '/', '\')
}

if ($SelfTest) {
    $ErrorActionPreference = 'Stop'
    $failures = @()
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("nb-git-helpers-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $nonRepo = Join-Path $tmp 'non-repo'
    $repo = Join-Path $tmp 'repo'
    New-Item -ItemType Directory -Force -Path $nonRepo, $repo | Out-Null
    try {
        function Assert-Helper([string]$Name, $Actual, $Expected) {
            $ok = if ($null -eq $Expected) { $null -eq $Actual } else { "$Actual" -eq "$Expected" }
            if (-not $ok) {
                $script:failures += ("{0}：期望 {1}，实际 {2}" -f $Name, ($Expected ?? '<null>'), ($Actual ?? '<null>'))
            }
        }

        # ① 非仓库目录 / 空输出：四态里的两态 —— 不抛异常，空值语义统一
        Assert-Helper '非仓库：Toplevel 为空' (Get-NbToplevel -Cwd $nonRepo) ''
        Assert-Helper '非仓库：Branch 为空' (Get-NbBranch -Cwd $nonRepo) ''
        Assert-Helper '非仓库：SymRef 为空' (Get-NbSymRef -Cwd $nonRepo) ''
        Assert-Helper '非仓库：CommonDir 为 null' (Get-NbCommonDir -RepoPath $nonRepo) $null
        Assert-Helper '空输出：不存在的 config 键为空' `
            (Get-NbGitLine -Cwd $repo -GitArgs @('config', '--get', 'nb.selftest.missing')) ''

        # ② 多行输出取首行：local config 多条记录，--list 输出多行；
        # 首行恒为 init 自带的 core.repositoryformatversion=0（插入序在前）
        & git -C $repo init -q | Out-Null
        & git -C $repo config nb.selftest.key1 a
        & git -C $repo config nb.selftest.key2 b
        $line = Get-NbGitLine -Cwd $repo -GitArgs @('config', '--list', '--local')
        Assert-Helper '多行：取首行' $line 'core.repositoryformatversion=0'

        # ③ 正常态：Toplevel 指回临时仓库（路径分隔符差异不影响相等性）
        $top = Get-NbToplevel -Cwd $repo
        $topNorm = ($top -replace '/', '\')
        $repoNorm = ((Resolve-Path -LiteralPath $repo).Path -replace '/', '\')
        Assert-Helper '正常态：Toplevel 指回仓库' $topNorm $repoNorm

        # ④ 分离 HEAD：SymRef 取不到名字返回空串；--abbrev-ref 返回字面量 "HEAD"
        # （既有调用方 verify.ps1 / git-done 正是按空串或 "HEAD" 判分离，语义不变）
        & git -C $repo -c user.name=t -c user.email=t@t.local commit --allow-empty -q -m selftest | Out-Null
        & git -C $repo checkout -q --detach HEAD | Out-Null
        Assert-Helper '分离 HEAD：Branch 为字面量 HEAD' (Get-NbBranch -Cwd $repo) 'HEAD'
        Assert-Helper '分离 HEAD：SymRef 为空' (Get-NbSymRef -Cwd $repo) ''
        Assert-Helper '分离 HEAD：CommonDir 仍可用' `
            ((Test-Path -LiteralPath (Get-NbCommonDir -RepoPath $repo))) 'True'
    } finally {
        if (Test-Path -LiteralPath $tmp) { Remove-Item -Recurse -Force $tmp }
    }
    if ($failures.Count -gt 0) {
        $failures | ForEach-Object { Write-Host ("  ✗ " + $_) -ForegroundColor Red }
        Write-Host ("✗ git-helpers 自测失败 {0} 项" -f $failures.Count) -ForegroundColor Red
        exit 1
    }
    Write-Host '✓ git-helpers 自测通过：空输出 / 多行 / 分离 HEAD / 非仓库目录 四态 + 正常态'
    exit 0
}
