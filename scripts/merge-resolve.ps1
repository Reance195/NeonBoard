<#
    merge-resolve.ps1 — 合并撞冲突后的两阶段善后（DX-27）

    背景：AGENTS.md / docs/git-workflow.md 里「合并撞冲突后」写的是一串手工步骤 ——
    临时分支 merge/ 不在允许前缀内要先切 chore/、冲突里 --ours 才是 origin/main、
    中断后要补结卡 / 删 refs/claims、派生补推、锚点快进、--check 到 0。全部是可判定的
    机械动作，却每次靠人回忆顺序（2026-11-04 那次就漏了派生补推）。

    两个阶段分开，因为它们的触发时机不同：

      -Merge    在 git merge 停下、有冲突标记时跑：可语义合并的（台账 JSON）自动合并
                并 git add，其余如实列出。合并完仍需要你自己 git commit --no-edit ——
                提交信息的含义（这次合并解决了什么）机器写不出来。
      -Recover  在合并提交已经落地后跑：清 claims → 刷派生快照 → 校验一致性 →
                锚点区归位 → 终检。每一步都可跳过，失败不中断后续。

    为什么 -Recover 不自动结卡：结卡是任务定义变更（baseline 里写下「这张卡做完了」），
    需要人判断「真的做完了」。脚本只指出 --check 说还差哪几个短号并给出命令 ——
    能判定的自动化，拍板的留给人（与 DX-23 同一原则）。

    用法：
      pwsh -File scripts/merge-resolve.ps1 -Merge
      pwsh -File scripts/merge-resolve.ps1 -Recover
      pwsh -File scripts/merge-resolve.ps1                 # 两个阶段依次跑（在合并中时）
      pwsh -File scripts/merge-resolve.ps1 -Merge -DryRun  # 只试算台账合并，不写盘

    退出码：0 全部完成（含「无事可做」）/ 1 有需手工处理的项 / 2 环境不满足前置条件
#>
[CmdletBinding()]
param(
    [switch]$Merge,
    [switch]$Recover,
    [string]$MainBranch = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {

if (-not $MainBranch) { $MainBranch = 'main' }

$script:failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$what) { $script:failures.Add($what) | Out-Null }

function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Args)
    $out = & git @Args 2>&1
    return [pscustomobject]@{ Code = $LASTEXITCODE; Output = @($out) }
}

function Write-Step([string]$text) {
    Write-Host ''
    Write-Host "== $text" -ForegroundColor Cyan
}

function Test-InMerge {
    return ((Invoke-Git rev-parse '-q' '--verify' 'MERGE_HEAD').Code -eq 0)
}

function Get-BranchName {
    $r = Invoke-Git symbolic-ref '--quiet' '--short' 'HEAD'
    if ($r.Code -ne 0) { return '' }
    return ($r.Output | Select-Object -First 1).ToString().Trim()
}

function Get-AbsGitPath([string]$p) {
    if ([string]::IsNullOrWhiteSpace($p)) { return '' }
    if ([System.IO.Path]::IsPathRooted($p)) { return [System.IO.Path]::GetFullPath($p) }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $p))
}

function Test-AtAnchor {
    # 锚点区 = 主工作区。worktree 里 --git-dir 指向 .git/worktrees/<名>，
    # --git-common-dir 指向共享的 .git；两者相等即主区。
    $d = (Invoke-Git rev-parse '--git-dir').Output | Select-Object -First 1
    $c = (Invoke-Git rev-parse '--git-common-dir').Output | Select-Object -First 1
    if (-not $d -or -not $c) { return $false }
    return ((Get-AbsGitPath $d.ToString().Trim()) -eq (Get-AbsGitPath $c.ToString().Trim()))
}

function Invoke-CommitAndPush {
    param([string]$Message, [string[]]$Body, [string[]]$Paths)
    $addArgs = @('add', '--') + $Paths
    if ((Invoke-Git @addArgs).Code -ne 0) { return $false }
    $commitArgs = @('commit', '-m', $Message)
    foreach ($line in $Body) { $commitArgs += @('-m', $line) }
    $c = Invoke-Git @commitArgs
    if ($c.Code -ne 0) {
        $c.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        # 必须连暂存区一起撤（DX-26 的教训）：只还工作区会把 add 过的结果留给下一次提交
        $restoreArgs = @('restore', '--staged', '--worktree', '--') + $Paths
        Invoke-Git @restoreArgs | Out-Null
        return $false
    }
    $branch = Get-BranchName
    # 只在分支已有上游时推：临时分支（merge/、probe/、chore/ 的一次性分支）没有上游，
    # 推上去等于在远端造垃圾 —— 是否推送由人决定，本脚本只补齐本地状态。
    $hasUpstream = $branch -and ((Invoke-Git rev-parse '--abbrev-ref' "$branch@{u}").Code -eq 0)
    if ($hasUpstream) {
        $p = Invoke-Git push origin "HEAD:$branch"
        if ($p.Code -eq 0) {
            Write-Host ("   ✓ 已推送到 origin/{0}" -f $branch) -ForegroundColor DarkGray
        } else {
            Write-Host ("   · 推送失败（网络或权限），本地提交已存在：git push origin HEAD:{0}" -f $branch) -ForegroundColor Yellow
        }
    } else {
        Write-Host '   · 分支无上游，未推送（合并与推送交给 git-done）' -ForegroundColor DarkGray
    }
    return $true
}

# ---------------------------------------------------------------- -Merge

function Invoke-MergeStage {
    Write-Step '阶段一：解决可语义合并的冲突'

    if (-not (Test-InMerge)) {
        Write-Host '· 不在合并中（没有 MERGE_HEAD），跳过' -ForegroundColor DarkGray
        return $true
    }

    $conflicts = @()
    $c = Invoke-Git diff '--name-only' '--diff-filter=U'
    if ($c.Code -eq 0) { $conflicts = @($c.Output | Where-Object { $_ -and $_.ToString().Trim() }) }
    if ($conflicts.Count -eq 0) {
        Write-Host '· 没有未解决的冲突文件（可能是 add 完还没 commit）' -ForegroundColor DarkGray
        Write-Host '  下一步：git commit --no-edit' -ForegroundColor DarkGray
        return $true
    }

    $manual = @()
    foreach ($raw in $conflicts) {
        $file = $raw.ToString().Trim()
        if ($file -eq 'docs/board-ledger.json') {
            Write-Host "· 台账冲突：走三路语义合并（$file）" -ForegroundColor DarkGray
            $pyArgs = @((Join-Path $PSScriptRoot 'merge_board.py'), '--merge')
            if ($DryRun) { $pyArgs += '--dry-run' }
            & python @pyArgs
            if ($LASTEXITCODE -ne 0) {
                $manual += "$file（三路合并失败，需手工解）"
            } elseif (-not $DryRun) {
                if ((Invoke-Git add '--' $file).Code -ne 0) { $manual += "$file（git add 失败）" }
            }
        } elseif ($file -eq 'progress.json') {
            # DX-64：快照是派生物（AGENTS.md §11——agent 禁止手改，分支上的快照改动
            # 只会来自 git-done 收尾的派生），两边必然重叠。按「取主线为底加重算」的
            # 手工舞步自动处理：先取 origin/main 版本，合并提交后 -Recover 的
            # derive --write 按合并后的全量历史重算收敛，提交体带自动来源行留痕。
            Write-Host "· 快照冲突（$file）：取 origin/main 版本为底，合并提交后由 -Recover 派生重算收敛" -ForegroundColor DarkGray
            if ($DryRun) {
                Write-Host '  [dry-run] 将执行：git checkout origin/main -- progress.json && git add' -ForegroundColor DarkGray
            } else {
                $co = Invoke-Git checkout 'origin/main' '--' $file
                if ($co.Code -ne 0) {
                    $manual += "$file（取 origin/main 版本失败，需手工解）"
                } elseif ((Invoke-Git add '--' $file).Code -ne 0) {
                    $manual += "$file（git add 失败）"
                }
            }
        } else {
            $manual += "$file（无自动合并规则）"
        }
    }

    if ($manual.Count -gt 0) {
        Write-Host ''
        Write-Host ("✗ {0} 个冲突需要手工处理：" -f $manual.Count) -ForegroundColor Yellow
        foreach ($m in $manual) {
            Write-Host "    $m" -ForegroundColor Yellow
            Add-Failure "手工解决冲突：$m"
        }
        Write-Host '  提示：冲突里 --ours 是 HEAD（当前分支），--theirs 是被合入的那一方。' -ForegroundColor DarkGray
        return $false
    }

    if (-not $DryRun) {
        Write-Host ''
        Write-Host '✓ 冲突文件已全部解决并 add' -ForegroundColor Green
        Write-Host '  下一步：git commit --no-edit（合并提交的含义机器写不出来）' -ForegroundColor DarkGray
        Write-Host '  之后：pwsh -File scripts/merge-resolve.ps1 -Recover' -ForegroundColor DarkGray
    }
    return $true
}

# ---------------------------------------------------------------- -Recover

function Invoke-RecoverStage {
    Write-Step '阶段二：合并后的善后（claims / 派生 / 校验 / 归位）'

    if (Test-InMerge) {
        Write-Host '✗ 合并还没提交（MERGE_HEAD 仍在），先 git commit --no-edit 再跑 -Recover' -ForegroundColor Red
        Add-Failure '合并未提交'
        return $false
    }

    $branch = Get-BranchName

    # 2.1 过期领用登记：合并/删分支后服务端 ref 已释放，台账投影还留着
    $rcOut = @(& python (Join-Path $PSScriptRoot 'release-claims.py') 2>&1)
    $rcCode = $LASTEXITCODE
    if ($rcCode -ne 0) {
        Write-Host "· 领用登记清理失败（退出码 $rcCode），继续后续步骤" -ForegroundColor Yellow
        $rcOut | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        Add-Failure 'release-claims.py'
    } elseif (@($rcOut | Where-Object { "$_" -and "$_".Trim() }).Count -eq 0) {
        Write-Host '· 无过期领用登记' -ForegroundColor DarkGray
    } else {
        $codes = @($rcOut | Where-Object { "$_" -and "$_".Trim() } | ForEach-Object { "$_".Trim() })
        Write-Host ("· 清理过期领用登记：{0}" -f ($codes -join '、')) -ForegroundColor DarkGray
        $ok = Invoke-CommitAndPush -Message ('chore(board): 清理过期领用登记 {0}' -f ($codes -join '、')) `
            -Body @('自动来源：merge-resolve.ps1 的 release-claims.py。') `
            -Paths @('docs/board-ledger.json')
        if ($ok) { Write-Host '   ✓ 已提交' -ForegroundColor Green } else { Add-Failure '提交领用登记清理' }
    }

    # 2.2 派生快照：合并会把两边的 progress.json 都带进来，必须按合并后的台账重算
    $before = (Get-FileHash -LiteralPath (Join-Path $repoRoot 'progress.json') -Algorithm SHA256).Hash
    & python (Join-Path $PSScriptRoot 'derive_board_status.py') '--write' 2>&1 | ForEach-Object {
        if ("$_" -match '^\s*$') { return }
        Write-Host "   $_" -ForegroundColor DarkGray
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host '· 派生器执行失败，继续后续步骤' -ForegroundColor Yellow
        Add-Failure 'derive_board_status.py --write'
    } else {
        $after = (Get-FileHash -LiteralPath (Join-Path $repoRoot 'progress.json') -Algorithm SHA256).Hash
        if ($after -eq $before) {
            Write-Host '· 快照无变化（已与台账一致）' -ForegroundColor DarkGray
        } else {
            $ok = Invoke-CommitAndPush -Message 'chore(roadmap): 同步进度快照' `
                -Body @('自动来源：merge-resolve.ps1 的 derive_board_status.py --write。') `
                -Paths @('progress.json')
            if ($ok) { Write-Host '   ✓ 已提交' -ForegroundColor Green } else { Add-Failure '提交派生快照' }
        }
    }

    # 2.3 一致性校验：非 0 时只报告，不替人写 baseline
    $checkOut = @(& python (Join-Path $PSScriptRoot 'derive_board_status.py') '--check' 2>&1)
    $checkCode = $LASTEXITCODE
    if ($checkCode -eq 0) {
        Write-Host '✓ 派生一致性校验通过' -ForegroundColor Green
    } else {
        Write-Host '✗ 派生一致性校验未通过，诊断如下：' -ForegroundColor Yellow
        $checkOut | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        Write-Host '  缺结卡标记的卡需要人判断「真的做完了」再写 baseline —— 本脚本不代写。' -ForegroundColor Yellow
        Write-Host '  补法：在提交里带 #<短号>=done 标记，或由 git-done 收尾时统一结卡。' -ForegroundColor DarkGray
        Add-Failure '派生一致性（需补结卡）'
    }

    # 2.4 锚点归位：只在主工作区做，开发工作区保持原位不挪
    if (Test-AtAnchor) {
        $cur = Get-BranchName
        if ($cur -ne $MainBranch) {
            Write-Host ("· 锚点区当前在 '{0}'，切回 {1}" -f $cur, $MainBranch) -ForegroundColor DarkGray
            if ((Invoke-Git switch $MainBranch).Code -ne 0) {
                Write-Host '· 切回失败（工作区有改动？），跳过归位' -ForegroundColor Yellow
                Add-Failure '锚点区切回主分支'
            }
        }
        Invoke-Git fetch origin '--quiet' | Out-Null
        $ff = Invoke-Git merge '--ff-only' "origin/$MainBranch"
        if ($ff.Code -eq 0) {
            Write-Host ("✓ 锚点区已快进到 origin/{0}" -f $MainBranch) -ForegroundColor Green
        } else {
            Write-Host ("· 快进失败（本地分叉或被未跟踪文件挡住），手工：git merge --ff-only origin/{0}" -f $MainBranch) -ForegroundColor Yellow
            $ff.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
            Add-Failure '锚点区快进'
        }
    } else {
        Write-Host '· 当前不是锚点区，跳过锚点归位' -ForegroundColor DarkGray
        Write-Host ("  （回到锚点区后：git switch {0} && git merge --ff-only origin/{1}）" -f $MainBranch, $MainBranch) -ForegroundColor DarkGray
    }

    # 2.5 终检三判据：分支非空、锚点区与远端一致、派生一致
    Write-Step '终检'
    $b = Get-BranchName
    if ($b) {
        Write-Host ("  ✓ 当前分支：{0}" -f $b) -ForegroundColor Green
    } else {
        Write-Host '  ✗ 当前在 detached HEAD 上' -ForegroundColor Red
        Add-Failure 'detached HEAD'
    }
    if (Test-AtAnchor) {
        $head = (Invoke-Git rev-parse 'HEAD').Output | Select-Object -First 1
        $origin = (Invoke-Git rev-parse "origin/$MainBranch").Output | Select-Object -First 1
        if ($head -and $origin -and ($head.ToString().Trim() -eq $origin.ToString().Trim())) {
            Write-Host '  ✓ 锚点区 HEAD 与 origin 一致' -ForegroundColor Green
        } else {
            Write-Host ("  · 锚点区 HEAD {0} ≠ origin {1}" -f "$head".Trim(), "$origin".Trim()) -ForegroundColor Yellow
            Add-Failure '锚点区与远端不一致'
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot '.git'))) {
        Write-Host '  · （worktree 检出，跳过 .git 目录检查）' -ForegroundColor DarkGray
    }
    $dirty = @((Invoke-Git status '--porcelain').Output | Where-Object { "$_".Trim() })
    if ($dirty.Count -eq 0) {
        Write-Host '  ✓ 工作区干净' -ForegroundColor Green
    } else {
        Write-Host ("  · 工作区有 {0} 项改动（可能是你没提交的在制品）" -f $dirty.Count) -ForegroundColor Yellow
        $dirty | ForEach-Object { Write-Host "     $_" -ForegroundColor DarkGray }
    }
    return $true
}

# ---------------------------------------------------------------- 主流程

$runMerge = $Merge -or (-not $Merge -and -not $Recover)
$runRecover = $Recover -or (-not $Merge -and -not $Recover)

$ok = $true
if ($runMerge) { $ok = (Invoke-MergeStage) -and $ok }
if ($runRecover) { $ok = (Invoke-RecoverStage) -and $ok }

Write-Host ''
if ($script:failures.Count -gt 0) {
    Write-Host ("✗ 有 {0} 项需要处理：" -f $script:failures.Count) -ForegroundColor Red
    foreach ($f in $script:failures) { Write-Host "    $f" -ForegroundColor Red }
    exit 1
}
Write-Host '✓ 合并善后完成' -ForegroundColor Green
exit 0

} finally {
    Pop-Location
}
