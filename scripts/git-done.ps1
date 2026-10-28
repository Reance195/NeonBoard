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
      pwsh -File scripts/git-done.ps1 -SkipClaimCheck      # 已废弃：不再能放行代码改动
      pwsh -File scripts/git-done.ps1 -SkipAnchorSync      # 合并后不快进锚点区
#>
[CmdletBinding()]
param(
    [string]$Branch = '',
    [ValidateSet('remote', 'local')]
    [string]$Mode = 'remote',
    [switch]$KeepBranch,
    [switch]$SkipClaimCheck,
    [switch]$SkipAnchorSync,
    [switch]$RemoveWorktree,
    [string]$MainBranch = 'main',
    [string]$Worktree = ''
)

$ErrorActionPreference = 'Stop'

# 宪章 §7.3：原生命令会往 stderr 写进度（git 的 "Switched to a new branch"、worktree/branch
# 的删除提示、fetch 的计数），在 $ErrorActionPreference='Stop' 下 `2>&1` 会把这些行升级成
# 终止性错误 —— 命令其实成功了也能把脚本拦腰打断（本次就卡在「创建临时分支」那一步，
# 之后清理分支 / 回收 worktree 的几处同样危险）。原生命令一律走这里：stderr 只作记录，
# 成功与否只看退出码。
function Invoke-Cmd {
    param([string]$File, [string[]]$CmdArgs)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & $File @CmdArgs 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $prev
    }
    [pscustomobject]@{ Code = $code; Output = @($out | ForEach-Object { "$_" }) }
}

# 失败即抛（调用方需要读到输出时用）
# 注意：这里必须按名传参，`Invoke-Cmd 'git' @args` 只把第一个元素绑给 $CmdArgs，
# 其余参数会被丢掉（git 收到裸命令名，退出码 129）。
function Invoke-Git {
    $r = Invoke-Cmd -File 'git' -CmdArgs @($args)
    if ($r.Code -ne 0) {
        throw ("git {0} 执行失败（{1}）：`n{2}" -f ($args -join ' '), $r.Code, ($r.Output -join "`n"))
    }
    if ($r.Output.Count -eq 1) { return $r.Output[0] }
    return $r.Output
}

# 失败不抛，由调用方按 Code 自行处置（探测类命令用这里）
function Invoke-GitQuiet { param([string[]]$GitArgs) return Invoke-Cmd -File 'git' -CmdArgs $GitArgs }

# 主 worktree（锚点区）永远是 `git worktree list --porcelain` 的第一块。
function Get-AnchorWorktree {
    $blocks = ((& git worktree list --porcelain) -join "`n") -split "`n`n"
    foreach ($block in $blocks) {
        $m = [regex]::Match($block, '(?m)^worktree (.+)$')
        if ($m.Success) { return $m.Groups[1].Value.Trim() }
    }
    return $null
}

# 合并推送成功后，把锚点区快进到最新 main —— 否则维护者回到根目录看到的还是旧代码。
# 只做 fast-forward：锚点有本地提交 / 非 main / 有 progress.json 之外的改动时一律跳过。
# progress.json 是应用自动写回的快照：上游没动它时直接快进，应用常驻占用文件也不会挡；
# 上游确实改了它才临时 stash 后还原，不丢数据。
function Sync-AnchorWorktree {
    param([string]$Main, [string]$CurrentRoot)
    $anchor = Get-AnchorWorktree
    if (-not $anchor) { return }
    try {
        $anchor = [System.IO.Path]::GetFullPath($anchor)
        $current = [System.IO.Path]::GetFullPath($CurrentRoot)
    } catch {
        return
    }
    if ($anchor -eq $current) {
        Write-Host '· 锚点同步：当前就在锚点区，跳过' -ForegroundColor DarkGray
        return
    }
    if (-not (Test-Path -LiteralPath $anchor)) { return }

    $branchRes = Invoke-GitQuiet @('-C', $anchor, 'symbolic-ref', '--quiet', '--short', 'HEAD')
    $anchorBranch = if ($branchRes.Code -eq 0) { ($branchRes.Output | Select-Object -First 1).ToString().Trim() } else { '' }
    if ($anchorBranch -ne $Main) {
        Write-Host ("· 锚点同步：锚点区当前分支是 '{0}'，跳过" -f $anchorBranch) -ForegroundColor DarkGray
        return
    }

    $dirtyRes = Invoke-GitQuiet @('-C', $anchor, 'status', '--porcelain')
    $dirty = @($dirtyRes.Output | Where-Object { $_ -and $_.ToString().Trim() -ne '' })
    $snapshotOnly = $dirty.Count -gt 0 -and @($dirty | Where-Object { $_.ToString() -notmatch '(^\S+\s+|\s)progress\.json$' }).Count -eq 0
    if ($dirty.Count -gt 0 -and -not $snapshotOnly) {
        Write-Host ("· 锚点同步：锚点区有未提交改动（{0} 项），跳过快进" -f $dirty.Count) -ForegroundColor Yellow
        # 只报数量等于没报：拖脏锚点的通常是某个桌面工具往调用方 CWD 吐的产物目录，
        # 不点名就查不出是谁（docs/pitfalls.md 1.1 / 7.7）。
        $dirty | Select-Object -First 10 | ForEach-Object { Write-Host ("     {0}" -f $_) -ForegroundColor Yellow }
        if ($dirty.Count -gt 10) {
            Write-Host ("     …另有 {0} 项未列出" -f ($dirty.Count - 10)) -ForegroundColor Yellow
        }
        Write-Host '     工具产物目录请补进 .gitignore，别让它长期卡住锚点同步' -ForegroundColor DarkGray
        return
    }

    # 先直接快进：上游没改 progress.json 时，本地快照抓脏不构成阻碍。
    # 应用常驻会占用 progress.json，stash 的 checkout 步骤可能失败 —— 能绕开就绕开。
    Write-Host ("· 锚点同步：快进 {0} → origin/{1}" -f $anchor, $Main) -ForegroundColor DarkGray
    $mergeRes = Invoke-GitQuiet @('-C', $anchor, 'merge', '--ff-only', "origin/$Main")
    $mergeOk = ($mergeRes.Code -eq 0)

    $stashed = $false
    $stashSha = ''
    if (-not $mergeOk -and $snapshotOnly) {
        Write-Host '· 锚点同步：临时暂存 progress.json 后重试 ...' -ForegroundColor DarkGray
        $stashRes = Invoke-GitQuiet @('-C', $anchor, 'stash', 'push', '-q', '-m', 'git-done 锚点同步临时暂存', '--', 'progress.json')
        $stashed = ($stashRes.Code -eq 0)
        if ($stashed) {
            # 记下暂存本身的提交号：stash@{0} 会被后续操作挪位，按号取回才靠得住。
            $shaRes = Invoke-GitQuiet @('-C', $anchor, 'rev-parse', '--verify', 'stash@{0}')
            if ($shaRes.Code -eq 0) { $stashSha = ($shaRes.Output | Select-Object -First 1).ToString().Trim() }
            $mergeRes = Invoke-GitQuiet @('-C', $anchor, 'merge', '--ff-only', "origin/$Main")
            $mergeOk = ($mergeRes.Code -eq 0)
        } else {
            Write-Host '· 锚点同步：progress.json 暂存失败（应用可能在运行并占用它），跳过快进' -ForegroundColor Yellow
        }
    }

    if (-not $mergeOk) {
        $mergeRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
    }

    if ($stashed) {
        $popRes = Invoke-GitQuiet @('-C', $anchor, 'stash', 'pop', '-q')
        if ($popRes.Code -ne 0) {
            # 主线也动过 progress.json（CI 每次回写都会动），pop 必然撞车。
            # 撞车不能只 warn 一句：失败的 pop 会把 <<<<<<< 冲突标记留在工作区、
            # 并把该文件挂成 UU —— 而这份文件正是桌面应用 RoadmapSync 的写回对象，
            # 下一次写回就把冲突标记打进快照；锚点的脏检查也会从此一直误判（已踩过）。
            # 所以工作区一律回退到刚快进好的主线版本；本地快照本就留在暂存里，不丢。
            $popRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
            $restore = Invoke-GitQuiet @('-C', $anchor, 'checkout', 'HEAD', '--', 'progress.json')
            if ($restore.Code -ne 0) {
                Write-Host '   ✗ 冲突标记清理失败，请手工执行：git -C <锚点> checkout HEAD -- progress.json' -ForegroundColor Red
            } else {
                Write-Host '   ⚠ 本地快照与主线快照冲突，工作区已回退为主线版本（无冲突标记）' -ForegroundColor Yellow
                if ($stashSha) {
                    Write-Host ("     本地快照仍在暂存中，需要用时就地取回：git -C {0} checkout {1} -- progress.json" -f $anchor, $stashSha) -ForegroundColor Yellow
                } else {
                    Write-Host '     本地快照仍在暂存中（git stash list 查看）' -ForegroundColor Yellow
                }
            }
        }
    }

    if ($mergeOk) {
        Write-Host '✓ 锚点区已同步到最新 main' -ForegroundColor Green
    } else {
        Write-Host '· 锚点同步未完成（本地提交 / 未跟踪文件冲突），不影响远端合并' -ForegroundColor Yellow
    }
}

# 合并收尾：释放已合并 / 孤儿任务的认领占位（refs/claims/<短号>）。
# 判据（满足任一即释放）：
#   1) 占位指向的提交已是 origin/<Main> 的祖先（任务已合入主线）；
#   2) 台账 claims 中该卡的认领分支已不存在（本地 / 远端均无），属历史残留孤儿。
# 二者都不满足（分支仍存在且未合入，如开发中的卡）则保留，可被领用闸门正常校验。
# 历史缺口：agent-init 仅清本地台账 claims、从不删 origin ref，导致合并后占位永久残留、
# 误拦重新认领；本函数补齐「释放」环节（宪章 AGENTS.md §7.2.1 第 4/5 条）。
# 注意：若 origin 删除因离线失败，下次脚本领用闸门的 fetch 会把 origin 残留镜像回本地，
# 故离线时仅清本地，联网后由下次运行彻底清 origin。
function Release-ClaimRef {
    param([string]$Ref)
    & git update-ref -d $Ref 2>$null
    Write-Host ('· 认领占位清理：本地释放 {0}' -f $Ref) -ForegroundColor DarkGray
    $del = Invoke-GitQuiet @('push', 'origin', '--delete', $Ref)
    if ($del.Code -eq 0) {
        Write-Host ('✓ 认领占位清理：服务端释放 {0}' -f $Ref) -ForegroundColor Green
    } else {
        Write-Host ('· 认领占位清理：服务端 {0} 删除失败（离线或本就不存在），联网后下次运行再清' -f $Ref) -ForegroundColor Yellow
    }
}

function Test-ClaimBranchGone {
    param([string]$Branch)
    if (-not $Branch) { return $true }
    $r1 = Invoke-GitQuiet @('rev-parse', '--verify', '-q', $Branch)
    if ($r1.Code -eq 0) { return $false }
    $r2 = Invoke-GitQuiet @('rev-parse', '--verify', '-q', ('origin/' + $Branch))
    return ($r2.Code -ne 0)
}

function Invoke-ClaimRefCleanup {
    param([string]$Main)
    # for-each-ref 可靠枚举整命名空间（git show-ref 的 pattern 匹配对松散引用不稳定，会漏列）
    $refs = @(& git for-each-ref --format='%(objectname) %(refname)' 'refs/claims/*' 2>$null)
    if ($refs.Count -eq 0) { return }
    $originMain = $null
    try { $originMain = (Invoke-Git rev-parse --verify -q "origin/$Main").Trim() } catch { $originMain = $null }
    if (-not $originMain) {
        Write-Host ('· 认领占位清理：origin/{0} 不可达，跳过（联网后下次运行再清）' -f $Main) -ForegroundColor Yellow
        return
    }
    # 台账 claims 映射 code -> branch，用于判断认领分支是否已删除
    $ledger = $null
    $ledgerPath = Join-Path $repoRoot 'docs/board-ledger.json'
    if (Test-Path -LiteralPath $ledgerPath) {
        try { $ledger = (Get-Content -LiteralPath $ledgerPath -Raw -Encoding utf8 | ConvertFrom-Json) } catch { $ledger = $null }
    }
    $claimBranches = @{}
    if ($ledger -and $ledger.claims) {
        foreach ($k in $ledger.claims.PSObject.Properties.Name) {
            $claimBranches[$k] = if ($ledger.claims.$k) { $ledger.claims.$k.branch } else { $null }
        }
    }
    foreach ($line in $refs) {
        $parts = ($line -split ' ')
        $sha = $parts[0]; $ref = $parts[1]
        $code = $ref -replace '^refs/claims/',''
        $isAnc = $false
        try { & git merge-base --is-ancestor $sha ('origin/' + $Main) 2>$null; $isAnc = ($LASTEXITCODE -eq 0) } catch { $isAnc = $false }
        if ($isAnc) { Release-ClaimRef $ref; continue }
        # 未合入：仅当该 code 在台账中登记过、且其认领分支已不存在（孤儿 / 历史残留）时才释放；
        # 台账未登记的 code（可能是其它机制认领的活动卡）一律保守保留，绝不误删。
        if ($claimBranches.ContainsKey($code) -and (Test-ClaimBranchGone $claimBranches[$code])) {
            Release-ClaimRef $ref
        }
    }
}

# 仓库根按**脚本自身路径**定位（脚本在 <repo>/scripts/ 下），不按当前工作目录 ——
# 否则从别的目录调用时会把合并动作做进错误的 worktree（踩过：误改了锚点区）。
$scriptRepoRoot = $null
if ($PSScriptRoot) {
    $candidate = Split-Path -Parent $PSScriptRoot
    if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate '.git'))) {
        $scriptRepoRoot = (Resolve-Path -LiteralPath $candidate).Path
    }
}
$repoRoot = if ($scriptRepoRoot) { $scriptRepoRoot } else { (Invoke-Git rev-parse --show-toplevel).Trim() }
Set-Location -LiteralPath $repoRoot
Write-Host "仓库目录：$repoRoot" -ForegroundColor DarkGray

# ---- 1. 工作区必须干净 ----
# 结卡自动化闭环：看板应用（RoadmapSync）会把卡片状态自动写回 progress.json，
# agent 收尾时该文件常处于未提交态。它不是 agent 手改的产物，而是单写者（应用）
# 自动写回的快照 —— 由本脚本代为提交；其余任何未提交改动仍然拒绝（fail-closed）。
$status = @(Invoke-Git status --porcelain)
$snapshotLines = @($status | Where-Object { $_ -match '(^\S+\s+|\s)progress\.json$' })
$otherLines    = @($status | Where-Object { $snapshotLines -notcontains $_ })

if ($otherLines.Count -gt 0) {
    Write-Host "`n✗ 当前工作区有未提交改动（progress.json 之外的也会一并拦下），请先提交：" -ForegroundColor Red
    $otherLines | ForEach-Object { Write-Host "   $_" -ForegroundColor Yellow }
    Write-Host "`n参考：git add -A ; git commit -m 'feat(scope): 说明'`n" -ForegroundColor Yellow
    exit 1
}

if ($snapshotLines.Count -gt 0) {
    Write-Host "→ 检测到 progress.json 未提交（RoadmapSync 自动写回），先按 git 事实校正状态 ..." -ForegroundColor Cyan
    # 应用写回的是它内存里的状态，可能落后于本分支的 `#code=done` 标记。原样提交会把
    # 过期状态带上 main（踩过：R4-02 已 done 却被写回 doing，靠 CI 的 derive --write 才纠正）。
    # 先跑一次派生器对齐；它只改状态字段、保留 app 生成的其它卡片，无变化时不落盘。
    # 派生器失败不阻断合并（CI 落 main 后还会再纠正），但必须显著告警，不许静默推过期快照。
    $deriveScript = Join-Path $repoRoot 'scripts\derive_board_status.py'
    $pythonExe = (Get-Command python -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ((Test-Path -LiteralPath $deriveScript) -and $pythonExe) {
        # 原生命令的 stderr 不许打断流程（宪章 §7.3）：统一走 Invoke-Cmd，判定只看退出码。
        $deriveRes = Invoke-Cmd -File 'python' -CmdArgs @($deriveScript, '--write')
        $deriveRes.Output | ForEach-Object { Write-Host ("   {0}" -f $_) -ForegroundColor DarkGray }
        if ($deriveRes.Code -ne 0) {
            Write-Host '· 警告：派生器未能校正快照（见上），将按应用写回的原样提交，CI 会再纠正' -ForegroundColor Yellow
        }
    } else {
        Write-Host '· 警告：缺少派生器脚本或 python，跳过快照校正' -ForegroundColor Yellow
    }
    Invoke-Git add progress.json | Out-Null
    try {
        Invoke-Git commit -m 'chore(roadmap): 同步进度快照' | Out-Null
        Write-Host "✓ 快照已提交：chore(roadmap): 同步进度快照" -ForegroundColor Green
    } catch {
        Write-Host "`n✗ progress.json 提交失败（commit-msg 钩子或其它原因），请手工处理：" -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }
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

    # 领用闸门：分支必须先有服务端 claim ref 占位（agent-init 领用时创建）。
    # 豁免不再靠自报：按 diff 路径自动判定，改动全部落在 docs/ scripts/ .githooks/
    # .github/ *.md 等豁免路径才算杂务分支，其它一律要求 claim ref。
    # -SkipClaimCheck 仅为兼容旧脚本保留，已不能放行任何代码改动。
    $claimExempt = $false
    $gateScript = Join-Path $repoRoot 'scripts\task_gate.py'
    if (Test-Path -LiteralPath $gateScript) {
        # 判定走 stdout（exempt / code），解释走 stderr。PS 5.1 下 `& python ... 2>$null`
        # 在 Stop 里同样会把 stderr 升级成终止性错误（今天就在这一行断过一次合并），
        # 所以必须走 Invoke-Cmd，并且只从输出里取判定行本身。
        $scopeRes = Invoke-Cmd -File 'python' -CmdArgs @($gateScript, 'scope', '--base', "origin/$MainBranch", '--head', $Branch)
        $scopeVerdict = ($scopeRes.Output | Where-Object { $_ -match '^(exempt|code)$' } | Select-Object -Last 1)
        if ($scopeRes.Code -eq 0 -and $scopeVerdict -eq 'exempt') {
            $claimExempt = $true
        }
    }

    if ($claimExempt) {
        Write-Host '· 领用闸门：改动全在豁免路径（文档/脚本/钩子），免 claim ref' -ForegroundColor DarkGray
    } else {
        if ($SkipClaimCheck) {
            Write-Host '· 警告：-SkipClaimCheck 已废弃，本次仍按实际改动校验 claim ref' -ForegroundColor Yellow
        }
        Write-Host '· 领用闸门：校验服务端 claim ref ...' -ForegroundColor DarkGray
        # git fetch 的进度全在 stderr，且离线时必须 fail-closed（宪章 §7.2.1）——
        # 原先 `2>&1 | Out-Null` 既会被 Stop 打断，又把失败吞成「继续往下跑」。
        $claimFetch = Invoke-GitQuiet @('fetch', 'origin', '+refs/claims/*:refs/claims/*')
        if ($claimFetch.Code -ne 0) {
            Write-Host "`n✗ 领用闸门未通过：拉取服务端 refs/claims/* 失败（退出码 $($claimFetch.Code)），需要联网。" -ForegroundColor Red
            $claimFetch.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
            exit 1
        }
        $refsRes = Invoke-Cmd -File 'python' -CmdArgs @(
            (Join-Path $repoRoot 'scripts\check_claim_refs.py'), '--base', "origin/$MainBranch", '--head', $Branch)
        $refsRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        if ($refsRes.Code -ne 0) {
            Write-Host "`n✗ 领用闸门未通过：分支缺少服务端 claim ref 占位。" -ForegroundColor Red
            Write-Host '  先用 scripts/agent-init.ps1 -Card <短号> 领用；纯文档/杂务分支无需手动豁免。' -ForegroundColor Yellow
            exit 1
        }
    }

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

    # 合并收尾：释放已合入主线的认领占位（refs/claims/*），避免永久残留误拦重新认领。
    Invoke-ClaimRefCleanup -Main $MainBranch

    if (-not $SkipAnchorSync) {
        Sync-AnchorWorktree -Main $MainBranch -CurrentRoot $repoRoot
    } else {
        Write-Host '· 已跳过锚点同步（-SkipAnchorSync）' -ForegroundColor Yellow
    }

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
            if ((Invoke-GitQuiet @('worktree', 'remove', $abs)).Code -ne 0) {
                Invoke-GitQuiet @('worktree', 'remove', '--force', $abs) | Out-Null
            }
        }
    }

    if (-not $KeepBranch) {
        Write-Host ("· 删除分支 {0}" -f $Branch) -ForegroundColor DarkGray
        $delRes = Invoke-GitQuiet @('branch', '-d', $Branch)
        $delRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        if ($delRes.Code -ne 0) {
            Write-Host "   （分支可能仍被某个 worktree 占用，先 git worktree remove 再删）" -ForegroundColor DarkGray
        }
    }

    # 解除临时分支占用后一并删除，工作区停在分离 HEAD 上，不留悬挂分支
    Invoke-GitQuiet @('switch', '--detach') | Out-Null
    Invoke-GitQuiet @('branch', '-d', $tmp) | Out-Null

    # 台账过期领用登记清理：服务端占位与认领分支此刻都已没了，投影条目留着只会让
    # 派生器每次报「分支已不存在」的噪声，而且要等下任何人领卡才被顺带清 ——
    # 收尾闭环应该由「合并 + 删分支」这个动作自己完成（AGENTS.md §7.2.1 释放义务）。
    # 分离 HEAD 上就地提交（只动 claims，台账保护钩子放行），快进补推；
    # 离线或撞车就回滚这个提交、留待下次合并再清 —— 绝不阻断已经成功的合并。
    $rcRes = Invoke-Cmd -File 'python' -CmdArgs @((Join-Path $repoRoot 'scripts\release-claims.py'))
    $goneCodes = @($rcRes.Output | Where-Object { $_ -and $_ -match '^\S+$' })
    if ($rcRes.Code -ne 0) {
        Write-Host ("· 台账领用登记清理：脚本失败（退出码 {0}），留待下次合并再清" -f $rcRes.Code) -ForegroundColor Yellow
    } elseif ($goneCodes.Count -gt 0) {
        Invoke-GitQuiet @('add', 'docs/board-ledger.json') | Out-Null
        $rcCommit = Invoke-GitQuiet @('commit', '-m', ('chore(board): 清理过期领用登记 {0}' -f ($goneCodes -join '、')))
        if ($rcCommit.Code -ne 0) {
            Write-Host '· 台账领用登记清理：提交失败，留待下次合并再清' -ForegroundColor Yellow
            Invoke-GitQuiet @('checkout', '--', 'docs/board-ledger.json') | Out-Null
        } else {
            $rcPush = Invoke-GitQuiet @('push', 'origin', "HEAD:$MainBranch")
            if ($rcPush.Code -eq 0) {
                Write-Host ('✓ 台账领用登记清理：{0} 已删除并推上 {1}' -f ($goneCodes -join '、'), $MainBranch) -ForegroundColor Green
            } else {
                Write-Host '· 台账领用登记清理：补推失败（离线 / 主线被推进），本地回滚，留待下次合并再清' -ForegroundColor Yellow
                Invoke-GitQuiet @('reset', '--hard', 'HEAD~1') | Out-Null
            }
        }
    }

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

# 合并收尾：本模式未自动推送，origin/main 尚未含本次合并，认领占位暂不释放
# （其 head 尚未成为 origin/main 的祖先）；推送后由 remote 模式或下次运行释放。
Invoke-ClaimRefCleanup -Main $MainBranch

if (-not $KeepBranch) {
    Write-Host "· 删除分支 $Branch" -ForegroundColor DarkGray
    Invoke-Git branch -d $Branch | Out-Null
    Write-Host "✓ 已删除分支 $Branch" -ForegroundColor DarkGray
}

Write-Host "`n✓ 合并完成：$Branch → $MainBranch" -ForegroundColor Green
Write-Host "   查看历史：git log --oneline --graph -10" -ForegroundColor DarkGray
Write-Host "   推送主分支：git push origin $MainBranch`n" -ForegroundColor DarkGray
exit 0
