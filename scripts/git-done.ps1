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
      pwsh -File scripts/git-done.ps1                      # .worktrees/ 下的目标区默认回收
      pwsh -File scripts/git-done.ps1 -KeepWorktree        # 保留工作区（调试 / 续做）
      pwsh -File scripts/git-done.ps1 -RemoveWorktree      # 强制回收（跳过未推送/脏检查）
      pwsh -File scripts/git-done.ps1 -Mode local
      pwsh -File scripts/git-done.ps1 -KeepBranch
      pwsh -File scripts/git-done.ps1 -SkipClaimCheck      # 已废弃：不再能放行代码改动
      pwsh -File scripts/git-done.ps1 -SkipAnchorSync      # 合并后不快进锚点区
      pwsh -File scripts/git-done.ps1 -Close DX-32,DX-33   # 人工确认结卡（卡必须是本分支领用的）
      pwsh -File scripts/git-done.ps1 -AllowCrossTree      # 跨区调用显式放行（DX-49 检出漂移守卫）
      pwsh -File scripts/git-done.ps1 -DryRun              # 收尾演练（DX-50）：fetch/push 打桩不触网，本地完整走一遍时序后回滚
      pwsh -File scripts/git-done.ps1 -SelfTest            # DX-78 打桩自测：首拒重试落地 / 连拒不虚报（DX-116 回执口径在其中）
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
    [switch]$KeepWorktree,
    # 结单对账（DX-33）：本分支领了卡、但提交信息里没写 #<短号>=done 时，收尾会点名。
    # -Close 是「我知道，按完成处理」的显式确认；只接受本分支领用过的卡。
    [string]$Close = '',
    [string]$MainBranch = 'main',
    # 推送打桩（DX-78 自测）：前 N 次 Push-Main 注入拒绝，第 N+1 次起走真推送。
    # 仅测试用（-SelfTest 的两个场景靠它制造 non-ff / 连续拒绝）；对真 origin 误传会
    # 假装推不上去，每次注入都在输出里点名。
    [int]$StubPushFailures = 0,
    # 自测（DX-78 acc3）：临时仓 + 打桩跑通「首拒重试落地 / 连拒退出码非 0」两个负例。
    [switch]$SelfTest,
    [string]$Worktree = '',
    # 检出漂移守卫（DX-49）：调用方 CWD 与脚本所属检出是同一仓库的不同工作树时默认中止；
    # 有意的跨区调用（复现旧脚本行为 / 跨区验证）用它显式放行并打显著告警
    [switch]$AllowCrossTree,
    # 收尾时序演练（DX-50）：fetch/push 全部打桩不触网，合并与三步收尾提交在本地真实走一遍，
    # 断言「三步提交在分离 HEAD、归位在最后」，结束切回进入时的分支，演练提交全部丢弃。
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

# git-helpers.ps1 点源时其 param([switch]$SelfTest) 会在**当前作用域**重绑同名变量，
# 把本脚本刚绑定的 -SelfTest 冲成 $false（PS 点源共享作用域，DX-78 实测：自测分支
# 永远进不去）。入口先把真值存进 $script: 作用域再用；点源一个带 param 的脚本时，
# 同名参数就是一次静默覆写——同名即冲突，参数命名要避开或先取值。
$script:gitDoneSelfTest = $SelfTest
$script:gitDoneStubPushFailures = $StubPushFailures

# git 符号引用取值走单一封装（DX-63）：空输出 / 多行 / 分离 HEAD 的空值语义只写一遍，
# 裸 `[string](& git ...)` 模式已被 check_script_hygiene 规则 E 禁止。
. (Join-Path $PSScriptRoot 'git-helpers.ps1')

# 控制台 UTF-8 编码唯一口径（DX-77/DX-83）：收尾捕获 python 子进程（derive/board-apply）的
# UTF-8 输出不再按 GBK 解码转发（§2.6/§2.20 同类的收口）
. (Join-Path $PSScriptRoot 'nb-console.ps1')
Set-NbUtf8Console

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

# 删除「提交已全部进本次合并」的分支（DX-109）：DX-106 的单次推送把删除挪到推送之前，
# 而 `branch -d` 的安全判据是「已合并到 upstream（refs/remotes/origin/main）」—— 此刻
# origin/main 还没含本次合并，判据恒假，git 必然以 "not fully merged" 拒删，每次收尾都
# 残留一支已合并分支。判据改成与 worktree 回收同锚点：分支提交是否已全部进本次合并
# （--no-ff 合并就是收纳证据）。确认后必须用 -D：-d 会再跑一遍同一套 upstream 检查，
# 照样误拒。返回 $true = 已删除或本来就不存在；$false = 保留（未进本次合并 / 被占用）。
function Remove-MergedBranch {
    param([string]$Name, [string]$AnchorCommit)
    $exists = Invoke-GitQuiet @('rev-parse', '--verify', '--quiet', "refs/heads/$Name")
    if ($exists.Code -ne 0) {
        Write-Host ("· 分支 {0} 已不存在，跳过删除" -f $Name) -ForegroundColor DarkGray
        return $true
    }
    $ahead = -1
    $aheadRes = Invoke-GitQuiet @('rev-list', '--count', $Name, '--not', $AnchorCommit)
    if ($aheadRes.Code -eq 0) {
        $first = $aheadRes.Output | Select-Object -First 1
        if ($first) { [void][int]::TryParse($first.ToString().Trim(), [ref]$ahead) }
    }
    if ($aheadRes.Code -ne 0 -or $ahead -ne 0) {
        if ($aheadRes.Code -ne 0) {
            Write-Host ("· 保留分支 {0}：无法核对提交是否已全部进本次合并" -f $Name) -ForegroundColor Yellow
            $aheadRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        } else {
            Write-Host ("· 保留分支 {0}：还有 {1} 个提交不在本次合并提交里" -f $Name, $ahead) -ForegroundColor Yellow
        }
        return $false
    }
    $delRes = Invoke-GitQuiet @('branch', '-D', $Name)
    $delRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
    if ($delRes.Code -eq 0) { return $true }
    Write-Host ("· 保留分支 {0}：可能仍被某个 worktree 占用（先 git worktree remove 再删）" -f $Name) -ForegroundColor Yellow
    return $false
}

# 合并提交正文四段（DX-101）：merge 提交不再只有一行标题 —— 正文按
# 变更 / 验收 / 影响 / 卡片 四段写事实（docs/task-card.md 的合并提交口径）。
# 卡片段从分支提交信息里提取 #短号 并对照台账 baseline 过滤，纯散文噪声
# （#ERROR / #RRGGBB 一类宏与颜色写法）进不了卡片段；取不到的如实写，
# 不编造。台账读不到时不过滤（退化为全收，与派生器的分层口径一致）。
function New-MergeCommitBody {
    param([string]$Branch, [string]$MainBranch)
    $commitLines = @(Invoke-GitQuiet @('log', '--pretty=%s%n%b', ("origin/{0}..{1}" -f $MainBranch, $Branch)) |
        Where-Object { $_ -and $_.ToString().Trim() -ne '' })
    $commitCount = 0
    $countRes = Invoke-GitQuiet @('rev-list', '--count', ("origin/{0}..{1}" -f $MainBranch, $Branch))
    if ($countRes.Code -eq 0 -and $countRes.Output.Count -gt 0) {
        [void][int]::TryParse($countRes.Output[0].ToString().Trim(), [ref]$commitCount)
    }
    $fileRes = Invoke-GitQuiet @('diff', '--name-only', ("origin/{0}...{1}" -f $MainBranch, $Branch))
    $fileCount = @($fileRes | Where-Object { $_ -and $_.ToString().Trim() -ne '' }).Count

    $known = @()
    $ledgerFile = Join-Path (Get-Location).Path 'docs/board-ledger.json'
    if (Test-Path -LiteralPath $ledgerFile) {
        try {
            # 显式按 UTF-8 读：Windows PowerShell 5.1 把无 BOM 的 UTF-8 当 GBK（§4 同坑）
            $ledger = ConvertFrom-Json -InputObject ([System.IO.File]::ReadAllText($ledgerFile, [System.Text.Encoding]::UTF8))
            $known = @($ledger.baseline.PSObject.Properties.Name)
        } catch { }
    }
    $codes = @()
    foreach ($line in $commitLines) {
        foreach ($m in [regex]::Matches([string]$line, '#([A-Za-z][A-Za-z0-9]*(?:-[A-Za-z0-9]+)*)')) {
            $c = $m.Groups[1].Value
            if (($known.Count -eq 0 -or $known -contains $c) -and $codes -notcontains $c) { $codes += $c }
        }
    }
    $cardsLine = if ($codes.Count -gt 0) { (($codes | ForEach-Object { "#$_" }) -join ' ') } else { '无（纯杂务分支）' }
    return ("变更：合入 {0} —— {1} 个提交、{2} 个文件改动。`n" +
            "验收：以分支提交内 #短号=done 标记与 verify 口径为准，合并后 derive --check 校验台账一致性。`n" +
            "影响：main 快进纳入本次分支改动，构建与测试由 CI 兜底。`n" +
            "卡片：{3}") -f $Branch, $commitCount, $fileCount, $cardsLine
}

# 冲突退出回滚现场（DX-107）：合并撞冲突时，不把 merge/* 临时分支、未合并冲突态和
# 「停在临时分支上的检出」留给下一条命令。所有动作都是非破坏性的 —— abort 用
# `git merge --abort`（等价 reset --merge，保留用户未提交改动），归位用 switch，
# 删分支用 branch -D；绝不 reset --hard / checkout -- .，用户的改动一律不碰。
# 任一步失败只告警并给出手工命令，不做二次破坏；已成功的步骤逐条打印，便于对账。
function Restore-ConflictScene {
    param(
        [string]$EntryRef,     # 进入时的符号引用；空串表示分离 HEAD
        [string]$EntryHead,    # 进入时的 HEAD（EntryRef 为空时按它回分离 HEAD）
        [string]$TempBranch    # 本次创建的 merge/* 临时分支；空串表示无（local 模式）
    )
    $mergeHead = Invoke-GitQuiet @('rev-parse', '-q', '--verify', 'MERGE_HEAD')
    if ($mergeHead.Code -eq 0) {
        $abort = Invoke-GitQuiet @('merge', '--abort')
        if ($abort.Code -ne 0) {
            $abort.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
            Write-Host '✗ git merge --abort 失败：冲突态仍在，未归位也未删临时分支，请手工：git merge --abort' -ForegroundColor Red
            return
        }
        Write-Host '· 冲突现场：git merge --abort 已完成（未提交改动原样保留）' -ForegroundColor DarkGray
    } else {
        Write-Host '· 冲突现场：合并未真正开始（无 MERGE_HEAD），跳过 abort' -ForegroundColor DarkGray
    }

    $target = if ($EntryRef) { $EntryRef } else { $EntryHead }
    $backArgs = if ($EntryRef) { @('switch', $EntryRef) } else { @('switch', '--detach', $EntryHead) }
    $back = Invoke-GitQuiet -GitArgs $backArgs
    if ($back.Code -ne 0) {
        $back.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        Write-Host ("· 警告：切回 {0} 失败（可能有未提交改动），请手工：git switch {1}" -f $target, ($backArgs -join ' ')) -ForegroundColor Yellow
        return
    }
    Write-Host ("· 冲突现场：已切回 {0}" -f $target) -ForegroundColor DarkGray

    if ($TempBranch) {
        $exists = Invoke-GitQuiet @('rev-parse', '--verify', '--quiet', "refs/heads/$TempBranch")
        if ($exists.Code -eq 0) {
            $del = Invoke-GitQuiet @('branch', '-D', $TempBranch)
            if ($del.Code -eq 0) {
                Write-Host ("· 冲突现场：已删除临时分支 {0}" -f $TempBranch) -ForegroundColor DarkGray
            } else {
                Write-Host ("· 警告：临时分支 {0} 删除失败，请手工：git branch -D {0}" -f $TempBranch) -ForegroundColor Yellow
            }
        }
    }
}

# 网络动作打桩（DX-50）：-DryRun 下 fetch / push 不执行、只留痕，返回成功形态让调用方
# 走「成功」分支 —— 演练的是时序，不是网络。真实调用一律走 Invoke-GitQuiet。
function Invoke-NetGit {
    param([string[]]$GitArgs)
    if ($DryRun) {
        Write-Host ("  [dry-run] 桩：git {0}" -f ($GitArgs -join ' ')) -ForegroundColor DarkCyan
        return [pscustomobject]@{ Code = 0; Output = @('[dry-run] 桩') }
    }
    return Invoke-GitQuiet -GitArgs $GitArgs
}

# 对 main 的唯一推送点（DX-106）：合并提交与三步收尾提交全部落本地后**一次**推上 main，
# Board 每批收尾只建 1 个 run 且落点必为自洽 tip。fail-soft 回退（收尾提交整体回滚后
# 单独推合并）收口在同一条管道。-StubPushFailures（DX-78 自测）在前 N 次注入拒绝。
function Push-Main {
    if ($script:gitDoneStubPushFailures -gt 0) {
        if ($null -eq $script:pushCalls) { $script:pushCalls = 0 }
        $script:pushCalls += 1
        if ($script:pushCalls -le $script:gitDoneStubPushFailures) {
            Write-Host ("  [stub] 注入推送拒绝（第 {0}/{1} 次，-StubPushFailures）" -f $script:pushCalls, $script:gitDoneStubPushFailures) -ForegroundColor DarkCyan
            return [pscustomobject]@{ Code = 1; Output = @("[stub] 注入拒绝（第 $script:pushCalls 次）") }
        }
    }
    return Invoke-NetGit @('push', 'origin', "HEAD:$MainBranch")
}

# 对 main 推送的非快进自动重试（DX-78 单一实现，收口口径沿 DX-58）：
# 推送被拒 → fetch 重取主线 → 变基到新 origin/main → 重推，上限 3 次；
# 变基冲突即放弃重试（现场处置由调用方裁决）。全仓只此一份重试实现——
# 再出现第二处「撞 non-ff 退出/重试」的推送步骤，就把它改成调用本函数。
function Invoke-MainPushWithRetry {
    param([int]$MaxAttempts = 3)
    for ($i = 1; $i -le $MaxAttempts; $i++) {
        $p = Push-Main
        if ($p.Code -eq 0) { return [pscustomobject]@{ Ok = $true; Attempts = $i } }
        if ($i -ge $MaxAttempts) { return [pscustomobject]@{ Ok = $false; Attempts = $i } }
        Write-Host ("· 推送被拒（第 {0} 次）：重取主线变基后重试" -f $i) -ForegroundColor Yellow
        Invoke-NetGit @('fetch', 'origin', '--prune') | Out-Null
        $rb = Invoke-GitQuiet @('rebase', "origin/$MainBranch")
        if ($rb.Code -ne 0) {
            Write-Host '· 推送重试：变基冲突，放弃重试' -ForegroundColor Yellow
            Invoke-GitQuiet @('rebase', '--abort') | Out-Null
            return [pscustomobject]@{ Ok = $false; Attempts = $i }
        }
    }
    return [pscustomobject]@{ Ok = $false; Attempts = $MaxAttempts }
}

# 收尾时序断言（DX-50）：三步自动提交（claims 清理 / 结卡 / 快照）必须发生在分离 HEAD 上 ——
# pre-commit 规则 1 禁止在 main 直接提交，DX-20 的回归正是归位提前导致三步全被自家钩子拒掉。
function Assert-DryCloseStep {
    param([string]$Step)
    if (-not $DryRun) { return }
    $ref = Get-NbSymRef
    if ($ref) {
        Write-Host ("✗ dry-run 断言失败：{0} 发生在命名分支 {1} 上（DX-26 要求收尾提交在分离 HEAD）" -f $Step, $ref) -ForegroundColor Red
        exit 7
    }
    Write-Host ("  [dry-run] {0} @ detached HEAD {1}" -f $Step, (& git rev-parse --short HEAD)) -ForegroundColor DarkCyan
}

function Sync-BoardSnapshot {
    param([string]$RepoRoot, [string]$MainBranch)
    # 快照刷新闭环（DX-13）：progress.json 是「git 标记 + 台账」的派生产物，只有 --write
    # 才会重算。结卡（--close-cards）改的是 docs/board-ledger.json 的 baseline，从不碰
    # 快照；而上面的步骤 1 只在「progress.json 已脏」时才校正 —— 应用（RoadmapSync）没写回
    # 快照的会话因此必漂（踩过两次：DX-11 于 10-29、R13-01 于 10-30，判据都是
    # derive --check 退出 1 并报「待同步」）。CI 回写兜不住：ci.yml 的 paths-ignore 含
    # docs/**，结卡这类 docs-only 提交根本不触发流水线。故收尾必刷一次。
    # 沿用本脚本的 fail-soft 约定：派生/提交/推送失败只告警，绝不阻断已经成功的合并。
    $deriveScript = Join-Path $RepoRoot 'scripts\derive_board_status.py'
    $pythonExe = (Get-Command python -ErrorAction SilentlyContinue | Select-Object -First 1)
    if (-not ((Test-Path -LiteralPath $deriveScript) -and $pythonExe)) {
        Write-Host '· 快照：缺少派生器脚本或 python，跳过刷新' -ForegroundColor Yellow
        return $false
    }
    $dwRes = Invoke-Cmd -File 'python' -CmdArgs @($deriveScript, '--write')
    $dwRes.Output | ForEach-Object { Write-Host ("   {0}" -f $_) -ForegroundColor DarkGray }
    if ($dwRes.Code -ne 0) {
        Write-Host '· 警告：派生器未能刷新快照（见上）。board.yml 只校验不回写（R14-01），这份落差只能等下次合并补' -ForegroundColor Yellow
        return $false
    }
    # 无变化不落盘，绝不产生空提交。返回值语义（DX-96）=「本次是否产生了待推提交」。
    $snapDirty = @((Invoke-Git status --porcelain -- progress.json) | Where-Object { $_ -match 'progress\.json$' })
    if ($snapDirty.Count -eq 0) {
        Write-Host '· 快照：派生结果与现有快照一致，无需提交' -ForegroundColor DarkGray
        return $false
    }
    Assert-DryCloseStep -Step '快照提交'
    Invoke-GitQuiet @('add', 'progress.json') | Out-Null
    $snapBody = "对象：合并收尾按 git 事实重算派生状态，使 progress.json 与台账一致（DX-13）。`n`n" +
                "自动来源：git-done.ps1 的 derive_board_status.py --write。"
    $snapCommit = Invoke-GitQuiet @('commit', '-m', 'chore(roadmap): 同步进度快照', '-m', $snapBody)
    if ($snapCommit.Code -ne 0) {
        Write-Host '· 警告：快照提交失败（commit-msg 钩子或其它原因），留待下次合并' -ForegroundColor Yellow
        $snapCommit.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        Invoke-GitQuiet @('restore', '--staged', '--worktree', '--', 'progress.json') | Out-Null
        return $false
    }
    # DX-106：快照提交不再独自补推 —— 留在本地，随合并+收尾单次推送（重试与回滚集中在
    # Push-Main 处，撞 non-ff 时整体变基或回滚到 $closeoutBase 后单独推合并）。
    if ($DryRun) {
        Write-Host '✓ [dry-run] 快照：本地已提交（未推送）' -ForegroundColor Green
    } else {
        Write-Host '✓ 快照：派生状态已提交（随收尾一次性补推）' -ForegroundColor Green
    }
    return $true
}

# 主 worktree（锚点区）永远是 `git worktree list --porcelain` 的第一块。
function Get-AnchorWorktree {
    $blocks = ((& git worktree list --porcelain) -join "`n") -split "`n`n"
    foreach ($block in $blocks) {
        $m = [regex]::Match($block, '(?m)^worktree (.+)$')
        if ($m.Success) { return $m.Groups[1].Value.Trim() }
    }
    return $null
}

# 锚点区（root）的占用登记：由 scripts/agent-workspace.ps1 写在 git common dir，所有 worktree 共享。
# 读不到 / 文件损坏一律按「未占用」处理 —— 登记文件的问题不能反过来停掉锚点同步。
function Get-AnchorRootClaim {
    param([string]$Anchor)
    # 一律按锚点路径解析，不按调用方 CWD：git-done 通常在 agent worktree 里跑，
    # CWD 解析出来的虽然也是同一份共享 common dir，但依赖调用位置本身就是不稳的源头。
    $dir = Get-NbCommonDir -RepoPath $Anchor
    if (-not $dir) { return $null }
    $file = Join-Path $dir 'agent-workspace.json'
    if (-not (Test-Path -LiteralPath $file)) { return $null }
    try {
        # 必须显式按 UTF-8 读：Windows PowerShell 5.1 会把无 BOM 的 UTF-8 当 GBK，中文尾字节会撑破 JSON
        $obj = ConvertFrom-Json -InputObject ([System.IO.File]::ReadAllText($file, [System.Text.Encoding]::UTF8))
        if ($obj.PSObject.Properties.Name -contains 'root' -and $obj.root) { return $obj.root }
    } catch { return $null }
    return $null
}

# 开发区（.worktrees/<agent>）占用的收尾释放（DX-05）：开区时 agent-init 自动登记，
# 合并成功即释放；不释放的话登记会一直显示「被别人占用」，后来者开同名目录会被误拦。
# 只释放「自己」那份 —— agent 身份取自当前检出（NEONBOARD_AGENT_ID / .agent-env.ps1），
# 与登记者不一致时 agent-workspace.ps1 会自己拒绝，绝不踢掉别人的占用。
#
# **调用时机**：身份必须在回收 worktree **之前**抓下来（见调用点）。`.agent-env.ps1` 是
# 「谁在用这份检出」的唯一物证，目录一删就没了；事后只能读到空，于是每次都走
# 「读不到 agent 身份，跳过释放」，登记文件里堆满已删工作区的僵尸条目。
function Get-WorktreeAgent {
    param([string]$Path)

    # 目标检出自己的 .agent-env.ps1 最准：调用方的环境变量可能属于另一个会话。
    $envFile = Join-Path $Path '.agent-env.ps1'
    if (Test-Path -LiteralPath $envFile) {
        try {
            $txt = [System.IO.File]::ReadAllText($envFile, [System.Text.Encoding]::UTF8)
            $m = [regex]::Match($txt, '(?m)^\s*\$env:NEONBOARD_AGENT_ID\s*=\s*''([^'']*)''')
            if ($m.Success -and $m.Groups[1].Value.Trim()) { return $m.Groups[1].Value.Trim() }
        } catch { }
    }
    if ($env:NEONBOARD_AGENT_ID) { return $env:NEONBOARD_AGENT_ID }
    # 兜底：.worktrees/<agent> 的 leaf 就是 agent 名（agent-init 的命名约定）。
    # 只有确实位于 .worktrees 下才敢这么推 —— 其它路径的 leaf 是仓库名。
    if ((Split-Path -Leaf (Split-Path -Parent $Path)) -eq '.worktrees') { return (Split-Path -Leaf $Path) }
    return ''
}

function Release-WorkspaceClaim {
    param([string]$Root, [string]$Agent = '', [string]$Scope = '')
    $agent = $Agent
    if (-not $agent) { $agent = Get-WorktreeAgent -Path $Root }
    if (-not $agent) {
        Write-Host '· 工作区占用：读不到 agent 身份，跳过释放（可手工 agent-workspace.ps1 -Action Release）' -ForegroundColor DarkGray
        return
    }
    if (-not $Scope) {
        $leaf = Split-Path -Leaf $Root
        $Scope = if ((Split-Path -Leaf (Split-Path -Parent $Root)) -eq '.worktrees') { "worktree:$leaf" } else { 'root' }
    }
    $scope = $Scope
    $wsScript = Join-Path $Root 'scripts\agent-workspace.ps1'
    if (-not (Test-Path -LiteralPath $wsScript)) { return }
    $pwshExe = 'powershell'
    $pwshCmd = Get-Command pwsh -ErrorAction SilentlyContinue
    if ($pwshCmd) { $pwshExe = $pwshCmd.Source }
    $out = @()
    $code = 0
    Push-Location $Root
    try {
        $prevEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $out = @(& $pwshExe -NoProfile -ExecutionPolicy Bypass -File $wsScript -Action Release -Agent $agent -Scope $scope 2>&1)
            $code = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $prevEap
        }
    } finally {
        Pop-Location
    }
    foreach ($line in $out) { Write-Host ("   {0}" -f $line) -ForegroundColor DarkGray }
    if ($code -ne 0) {
        Write-Host ("· 工作区占用未释放（{0}，退出码 {1}），不影响已完成的合并" -f $scope, $code) -ForegroundColor Yellow
    }
}

# 合并推送成功后，把锚点区快进到最新 main —— 否则维护者回到根目录看到的还是旧代码。
# 只做 fast-forward：锚点有本地提交 / 非 main / 未跟踪文件 / 受控文件之外的改动时一律跳过。
# 受控文件（progress.json 与 docs/iteration-roadmap.md）允许临时 stash 后还原，不丢数据。
# DX-110：跳过原因按「本地提交 / 已跟踪文件脏（逐个点名）/ 未跟踪冲突」三类分开报，
# 不再一律甩一句「本地提交 / 未跟踪文件冲突」把排查带偏。
function Sync-AnchorWorktree {
    param([string]$Main, [string]$CurrentRoot)

    # 只有这两个文件允许在快进前临时暂存：progress.json 是应用自动写回的派生产物；
    # docs/iteration-roadmap.md 是立卡程序写进立卡提交前的 §9 里程碑行（DX-110）。
    # 工具产物目录与其它任何文件一律不碰（§7.10 回收 / 处置由人拍板）。
    $safePaths = @('progress.json', 'docs/iteration-roadmap.md')

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

    $anchorBranch = Get-NbSymRef -Cwd $anchor
    if ($anchorBranch -ne $Main) {
        if ($anchorBranch) {
            Write-Host ("· 锚点同步：锚点区当前分支是 '{0}'，跳过" -f $anchorBranch) -ForegroundColor DarkGray
            return
        }
        # 锚点区停在分离 HEAD：DX-20 之前每次收尾都这么留下，属历史状态，顺手修好。
        # 切不回来（锚点区有改动会与之冲突）就跳过 —— 绝不为了同步而覆盖别人的工作区。
        $back = Invoke-GitQuiet @('-C', $anchor, 'switch', $Main)
        if ($back.Code -ne 0) {
            Write-Host '· 锚点同步：锚点区停在分离 HEAD 且无法切回主分支（有改动会冲突），跳过' -ForegroundColor Yellow
            return
        }
        Write-Host ("· 锚点同步：锚点区原为分离 HEAD，已切回 {0}" -f $Main) -ForegroundColor DarkGray
        $anchorBranch = $Main
    }

    # root 占用登记：别人正声明「我要在锚点区动手」时，快进照做（非破坏性），
    # 但绝不 stash 受控文件 —— 处置权归占用者，否则两边互相把对方的改动暂存掉
    #（docs/pitfalls.md 1.16）。
    $rootClaim = Get-AnchorRootClaim -Anchor $anchor
    if ($rootClaim) {
        $who = [string]$rootClaim.agent
        if (-not $who) { $who = '(未记名)' }
        $task = [string]$rootClaim.task
        Write-Host ("⚠ 锚点同步：root 已被 '{0}' 登记占用{1}，本次只快进、不动受控文件" -f $who, $(if ($task) { "（$task）" } else { '' })) -ForegroundColor Yellow
    }

    # ---------- 脏树精确分类（DX-110）----------
    $dirtyRes = Invoke-GitQuiet @('-C', $anchor, 'status', '--porcelain')
    $dirty = @($dirtyRes.Output | Where-Object { $_ -and $_.ToString().Trim() -ne '' })
    $tracked = @()
    $untracked = @()
    foreach ($entry in $dirty) {
        $line = $entry.ToString()
        if ($line.Length -lt 4) { continue }
        $xy = $line.Substring(0, 2)
        $path = $line.Substring(3).Trim()
        # 重命名 "old -> new" 取新路径；core.quotePath 的引号去掉
        if ($path -match ' -> ') { $path = ($path -split ' -> ')[-1] }
        $path = ($path -replace '^"|"$', '').Replace('\', '/')
        if ($xy -eq '??') { $untracked += $path } else { $tracked += $path }
    }
    $safeDirty = @($tracked | Where-Object { $safePaths -contains $_ })
    $otherDirty = @($tracked | Where-Object { $safePaths -notcontains $_ })

    # 本地提交：锚点 HEAD 不是 origin/<Main> 的祖先时 ff-only 必失败，stash 也救不了。
    $ahead = 0
    $aheadRes = Invoke-GitQuiet @('-C', $anchor, 'rev-list', '--count', ("origin/{0}..HEAD" -f $Main))
    if ($aheadRes.Code -eq 0 -and $aheadRes.Output.Count -gt 0) {
        [void][int]::TryParse($aheadRes.Output[0].ToString().Trim(), [ref]$ahead)
    }

    # 三类原因分开报、逐个点名（DX-110 验收 2）：不再一律「本地提交 / 未跟踪文件冲突」。
    if ($ahead -gt 0 -or $untracked.Count -gt 0 -or $otherDirty.Count -gt 0) {
        Write-Host '· 锚点同步：跳过快进，原因分类如下' -ForegroundColor Yellow
        if ($ahead -gt 0) {
            Write-Host ("   · 本地提交：锚点 {0} 相对 origin/{0} 有 {1} 个本地提交，快进会分叉" -f $Main, $ahead) -ForegroundColor Yellow
        }
        if ($otherDirty.Count -gt 0) {
            Write-Host ("   · 已跟踪文件脏（{0} 个，逐个点名）：" -f $otherDirty.Count) -ForegroundColor Yellow
            $otherDirty | Select-Object -First 10 | ForEach-Object { Write-Host ("       {0}" -f $_) -ForegroundColor Yellow }
            if ($otherDirty.Count -gt 10) { Write-Host ("       …另有 {0} 个未列出" -f ($otherDirty.Count - 10)) -ForegroundColor Yellow }
            Write-Host '     受控文件（progress.json / docs/iteration-roadmap.md）之外的改动一律不碰' -ForegroundColor DarkGray
        }
        # DX-101：跳过提示必须给出现成命令与落后提交数，不能再只报原因让人自己拼
        $behind = 0
        $behindRes = Invoke-GitQuiet @('-C', $anchor, 'rev-list', '--count', ("HEAD..origin/{0}" -f $Main))
        if ($behindRes.Code -eq 0 -and $behindRes.Output.Count -gt 0) {
            [void][int]::TryParse($behindRes.Output[0].ToString().Trim(), [ref]$behind)
        }
        Write-Host ("    现场清好后手工同步：git -C {0} merge --ff-only origin/{1}（当前落后 {2} 个提交）" -f $anchor, $Main, $behind) -ForegroundColor Yellow
        if ($untracked.Count -gt 0) {
            Write-Host ("   · 未跟踪冲突（{0} 个，逐个点名）：" -f $untracked.Count) -ForegroundColor Yellow
            $untracked | Select-Object -First 10 | ForEach-Object { Write-Host ("       {0}" -f $_) -ForegroundColor Yellow }
            if ($untracked.Count -gt 10) { Write-Host ("       …另有 {0} 个未列出" -f ($untracked.Count - 10)) -ForegroundColor Yellow }
            Write-Host '     工具产物目录请补进 .gitignore，别让它长期卡住锚点同步' -ForegroundColor DarkGray
        }
        if ($safeDirty.Count -gt 0) {
            Write-Host ("   · 另有受控文件脏（{0}），但被上面几类原因挡住，本次不暂存" -f ($safeDirty -join '、')) -ForegroundColor DarkGray
        }
        return
    }

    # 到这里：无本地提交 / 无未跟踪 / 无非受控的已跟踪改动；只剩 0~N 个受控文件脏。
    if ($safeDirty.Count -gt 0) {
        Write-Host ("· 锚点同步：受控文件脏（{0}），按 stash / 还原 / 主线优先口径处理，其它文件不碰" -f ($safeDirty -join '、')) -ForegroundColor DarkGray
    }
    # 先直接快进：上游没动这些文件时，本地抓脏不构成阻碍。应用常驻会占用
    # progress.json，stash 的 checkout 步骤可能失败 —— 能绕开就绕开（docs/pitfalls.md 1.2）。
    Write-Host ("· 锚点同步：快进 {0} → origin/{1}" -f $anchor, $Main) -ForegroundColor DarkGray
    $mergeRes = Invoke-GitQuiet @('-C', $anchor, 'merge', '--ff-only', "origin/$Main")
    $mergeOk = ($mergeRes.Code -eq 0)

    $stashed = $false
    $stashSha = ''
    if (-not $mergeOk -and $safeDirty.Count -gt 0 -and -not $rootClaim) {
        Write-Host ("· 锚点同步：临时暂存受控文件（{0}）后重试 ..." -f ($safeDirty -join '、')) -ForegroundColor DarkGray
        $stashArgs = @('-C', $anchor, 'stash', 'push', '-q', '-m', 'git-done 锚点同步临时暂存', '--') + $safeDirty
        $stashRes = Invoke-GitQuiet $stashArgs
        $stashed = ($stashRes.Code -eq 0)
        if ($stashed) {
            # 记下暂存本身的提交号：stash@{0} 会被后续操作挪位，按号取回才靠得住。
            $shaRes = Invoke-GitQuiet @('-C', $anchor, 'rev-parse', '--verify', 'stash@{0}')
            if ($shaRes.Code -eq 0 -and $shaRes.Output.Count -gt 0) {
                $stashSha = $shaRes.Output[0].ToString().Trim()
            }
            $mergeRes = Invoke-GitQuiet @('-C', $anchor, 'merge', '--ff-only', "origin/$Main")
            $mergeOk = ($mergeRes.Code -eq 0)
        } else {
            Write-Host '· 锚点同步：受控文件暂存失败（应用可能在运行并占用它），跳过快进' -ForegroundColor Yellow
        }
    }

    if (-not $mergeOk) {
        $mergeRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
    }

    if ($stashed) {
        $popRes = Invoke-GitQuiet @('-C', $anchor, 'stash', 'pop', '-q')
        if ($popRes.Code -ne 0) {
            # 主线也动过这些文件（CI 每次回写都会动 progress.json），pop 必然撞车。
            # 撞车不能只 warn 一句：失败的 pop 会把 <<<<<<< 冲突标记留在工作区并把文件
            # 挂成 UU —— progress.json 是 RoadmapSync 的写回对象，下次写回就把冲突标记
            # 打进快照（docs/pitfalls.md 1.3）。所以工作区一律回退到刚快进好的主线版本；
            # 本地内容本就完整留在 stash 里，不丢。
            $popRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
            $restoreArgs = @('-C', $anchor, 'checkout', 'HEAD', '--') + $safeDirty
            $restore = Invoke-GitQuiet $restoreArgs
            if ($restore.Code -ne 0) {
                Write-Host ("   ✗ 冲突标记清理失败，请手工执行：git -C <锚点> checkout HEAD -- {0}" -f ($safeDirty -join ' ')) -ForegroundColor Red
            } else {
                Write-Host '   ⚠ 本地受控文件与主线冲突，工作区已回退为主线版本（无冲突标记）' -ForegroundColor Yellow
                if ($stashSha) {
                    foreach ($item in $safeDirty) {
                        Write-Host ("     本地版本仍在暂存中，需要时逐文件取回：git -C {0} checkout {1} -- {2}" -f $anchor, $stashSha, $item) -ForegroundColor Yellow
                    }
                } else {
                    Write-Host '     本地版本仍在暂存中（git stash list 查看）' -ForegroundColor Yellow
                }
            }
        }
    }

    if ($mergeOk) {
        Write-Host '✓ 锚点区已同步到最新 main' -ForegroundColor Green
    } else {
        # 精确分类（DX-110 验收 2）：走到这里说明快进本身失败，按实际命中的原因点名，
        # 不再一律报「本地提交 / 未跟踪文件冲突」。
        $why = @()
        if ($ahead -gt 0) { $why += ("本地提交 {0} 个" -f $ahead) }
        if ($rootClaim -and $safeDirty.Count -gt 0) { $why += 'root 被占用（按登记让给占用者处理受控文件）' }
        if ($safeDirty.Count -gt 0 -and $stashed) { $why += ('受控文件已暂存但快进仍失败：' + ($safeDirty -join '、')) }
        if ($why.Count -eq 0) {
            $firstLine = if ($mergeRes.Output.Count -gt 0) { $mergeRes.Output[0] } else { '' }
            $why += ('快进被拒绝：' + $firstLine)
        }
        Write-Host ("· 锚点同步未完成（{0}），不影响远端合并" -f ($why -join '；')) -ForegroundColor Yellow
    }
}

# 合并收尾：释放已合并 / 孤儿任务的认领占位（refs/claims/<短号>）。# 判据（满足任一即释放）：
#   1) 占位指向的提交已是 origin/<Main> 的祖先（任务已合入主线）；
#   2) 台账 claims 中该卡的认领分支已不存在（本地 / 远端均无），属历史残留孤儿。
# 二者都不满足（分支仍存在且未合入，如开发中的卡）则保留，可被领用闸门正常校验。
# 历史缺口：agent-init 仅清本地台账 claims、从不删 origin ref，导致合并后占位永久残留、
# 误拦重新认领；本函数补齐「释放」环节（宪章 AGENTS.md §7.2.1 第 4/5 条）。
# 注意：若 origin 删除因离线失败，下次脚本领用闸门的 fetch 会把 origin 残留镜像回本地，
# 故离线时仅清本地，联网后由下次运行彻底清 origin。
function Release-ClaimRef {
    param([string]$Ref)
    if ($DryRun) {
        Write-Host ("  [dry-run] 桩：git update-ref -d {0}（演练不动引用）" -f $Ref) -ForegroundColor DarkCyan
    } else {
        & git update-ref -d $Ref 2>$null
        Write-Host ('· 认领占位清理：本地释放 {0}' -f $Ref) -ForegroundColor DarkGray
    }
    $del = Invoke-NetGit @('push', 'origin', '--delete', $Ref)
    if ($del.Code -eq 0) {
        if ($DryRun) {
            Write-Host ('✓ [dry-run] 认领占位清理：本地与远端均未动（演练）' -f $Ref) -ForegroundColor Green
        } else {
            Write-Host ('✓ 认领占位清理：服务端释放 {0}' -f $Ref) -ForegroundColor Green
        }
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
    # DX-90：rebase 重写占位（判据 1 打不中）+ 台账投影已清空（判据 2 打不中）的死角，
    # 收口到 check_claim_refs.py 的僵尸判定（classify_claim 单一实现，按占位提交的引用
    # 实态评估，不依赖分支已删），这里只调用不复制判据。脚本内部对离线 / 台账不可达
    # 保守跳过，退出码恒 0，失败不会阻断收尾；巡检（CI）兜底点名漏网的。
    if ($DryRun) {
        Write-Host '  [dry-run] 桩：check_claim_refs.py --release-zombies（演练不动占位）' -ForegroundColor DarkCyan
        return
    }
    $relRes = Invoke-Cmd -File 'python' -CmdArgs @((Join-Path $repoRoot 'scripts\check_claim_refs.py'), '--release-zombies')
    # 释放的短号走 stdout 单词行（与 release-claims 同约定），按此提取回显；
    # 判定 / 跳过细节走 stderr，已被封装捕获，不在这里复述。
    $relCodes = @($relRes.Output | Where-Object { $_ -and $_ -match '^\S+$' })
    if ($relCodes.Count -gt 0) {
        Write-Host ('✓ 僵尸占位释放：{0}（本地与服务端均已清，清单随本行留痕）' -f ($relCodes -join '、')) -ForegroundColor Green
    }
    if ($relRes.Code -ne 0) {
        Write-Host ('· 僵尸占位释放：脚本失败（退出码 {0}），留待下次收尾或 CI 巡检点名' -f $relRes.Code) -ForegroundColor Yellow
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
$repoRoot = if ($scriptRepoRoot) { $scriptRepoRoot } else { Get-NbToplevel }


# ---------- 检出漂移守卫（DX-49）----------
# 多会话并行时，终端 / Bash 工具的 CWD 会在锚点区与各 worktree 之间静默漂移：调用方以为
# 站在树 A，脚本却按自身路径作用于树 B（§7.3 的定位设计），两棵树不一致时此前没有任何
# 声音 —— R16-05 实测把示例插件改动误写进锚点区并 stage（pitfalls 1.25）。入口把话说破：
# 同一仓库的不同工作树 → 默认中止并给两条出路；不在检出里 / 属于别的仓库没有漂移信号，不拦。
# （Get-NbCommonDir 已收口到 scripts/git-helpers.ps1，DX-63。）
function Test-CheckoutDrift {
    param([string]$ScriptTree, [switch]$Allowed)

    if (-not $ScriptTree) { return }                          # 脚本不在检出内（异常布局），无从比对
    $callerTop = Get-NbToplevel
    if (-not $callerTop) { return }                           # 调用方不在任何 git 检出里，无漂移信号
    $callerTop = (Resolve-Path -LiteralPath $callerTop).Path -replace '/', '\'
    $scriptTop = (Resolve-Path -LiteralPath $ScriptTree).Path -replace '/', '\'
    if ($callerTop -ieq $scriptTop) { return }                # 同区调用，正常
    $callerCommon = Get-NbCommonDir $callerTop
    $scriptCommon = Get-NbCommonDir $scriptTop
    if (-not $callerCommon -or -not $scriptCommon -or $callerCommon -ine $scriptCommon) { return }
    if ($Allowed) {
        Write-Host '⚠ 检出漂移放行（-AllowCrossTree）：请确认这是有意的跨区调用（DX-49）。' -ForegroundColor Yellow
        Write-Host ("  调用方检出   : {0}" -f $callerTop) -ForegroundColor Yellow
        Write-Host ("  脚本所属检出 : {0}" -f $scriptTop) -ForegroundColor Yellow
        return
    }
    Write-Host '✗ 调用方检出与脚本所属检出不一致，已中止（DX-49 检出漂移守卫）。' -ForegroundColor Red
    Write-Host ("  调用方 CWD 所在检出  : {0}" -f $callerTop)
    Write-Host ("  脚本按自身路径作用于 : {0}" -f $scriptTop)
    Write-Host '  多会话下 CWD 会静默漂移，跨区误操作会写错工作树。两条出路：' -ForegroundColor Yellow
    Write-Host ('    1) 先切到目标检出再跑：  cd "' + $scriptTop + '"') -ForegroundColor Yellow
    Write-Host '    2) 确属有意跨区（复现旧脚本 / 跨区验证）：加 -AllowCrossTree 放行' -ForegroundColor Yellow
    exit 5
}
# ---------- 自测（DX-78 acc3 负测 + DX-116 回执口径）----------
# 在临时仓里用 -StubPushFailures 跑完整 remote 收尾链：
#   A 首拒重试落地：推送第 1 次注入拒绝、重试成功 —— 断言退出码 0、合并已上 origin、检出归位不在 merge/*；
#   B 连续拒绝：3 次重试全被拒、fail-soft 回滚后补推仍被拒 —— 断言退出码非 0、检出不在 merge/*、
#     合并提交 pin 在重建的 merge/* 分支上（DX-116：补推指引的落点必须真实存在）。
#   回执口径（DX-116）：A 的回执 pushed=true/head 记已落地提交；B 的回执 pushed=false、head 置空、
#     mergedInto 不写 origin/main、恢复信息记 localHead/localBranch —— 推送没落地就不得虚报。
# 不触真 origin、不仓库外留文件。依赖 git / pwsh / python（claim 闸门的 task_gate.py）。
if ($script:gitDoneSelfTest) {
    $ErrorActionPreference = 'Stop'
    $script:stFails = @()
    foreach ($dep in 'python', 'pwsh') {
        if (-not (Get-Command $dep -ErrorAction SilentlyContinue)) {
            Write-Host "✗ git-done selftest 依赖 $dep（claim 闸门 / 子进程收尾链）" -ForegroundColor Red
            exit 2
        }
    }

    function Invoke-In([string]$Repo, [string[]]$GitArgs) {
        $r = Invoke-Cmd -File 'git' -CmdArgs (@('-C', $Repo) + $GitArgs)
        if ($r.Code -ne 0) {
            throw ("git -C {0} {1} 失败（{2}）：{3}" -f $Repo, ($GitArgs -join ' '), $r.Code, ($r.Output -join "`n"))
        }
        # 逗号保住数组不被 return 摊平：单行输出摊成标量后 [0] 会取到首字符（DX-78 自测踩过）
        return ,$r.Output
    }
    function Assert-St([string]$Name, [bool]$Cond) {
        if (-not $Cond) { $script:stFails += $Name }
        if ($Cond) { Write-Host ("  ok   " + $Name) -ForegroundColor DarkGray }
        else { Write-Host ("  FAIL " + $Name) -ForegroundColor Red }
    }
    function New-StWorkRepo([string]$Work, [string]$OriginPath) {
        $r0 = Invoke-Cmd -File 'git' -CmdArgs @('-c', 'init.defaultBranch=main', 'init', '-q', $Work)
        if ($r0.Code -ne 0) { throw ("init 失败：" + ($r0.Output -join "`n")) }
        Invoke-In $Work @('config', 'user.name', 'selftest') | Out-Null
        Invoke-In $Work @('config', 'user.email', 'selftest@local') | Out-Null
        Invoke-In $Work @('remote', 'add', 'origin', $OriginPath) | Out-Null
        # git-done 按脚本自身路径定位仓库：整份 scripts/ 拷进临时检出并随种子提交入库，
        # 收尾才作用于临时仓；入库也保证子进程收尾时工作区是干净的（step 1 闸门）
        $dstScripts = Join-Path $Work 'scripts'
        New-Item -ItemType Directory -Path $dstScripts -Force | Out-Null
        Copy-Item -Path (Join-Path $PSScriptRoot '*') -Destination $dstScripts -Recurse -Force
        Set-Content -LiteralPath (Join-Path $Work 'README.md') -Value '# selftest seed' -Encoding utf8
        Invoke-In $Work @('add', '-A') | Out-Null
        Invoke-In $Work @('commit', '-q', '-m', 'chore: seed scripts + readme') | Out-Null
        Invoke-In $Work @('push', '-q', 'origin', 'main') | Out-Null
    }

    $tmpRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("nb-gitdone-st-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path $tmpRoot | Out-Null
    try {
        # ---- 场景 A：首拒重试落地 ----
        $originA = Join-Path $tmpRoot 'a-origin.git'
        Invoke-Cmd -File 'git' -CmdArgs @('init', '-q', '--bare', $originA) | Out-Null
        $workA = Join-Path $tmpRoot 'a-work'
        New-StWorkRepo -Work $workA -OriginPath $originA
        Invoke-In $workA @('switch', '-c', 'fix/selftest-a') | Out-Null
        Add-Content -LiteralPath (Join-Path $workA 'README.md') -Value 'feature line a' -Encoding utf8
        Invoke-In $workA @('commit', '-q', '-am', 'docs: feature a') | Out-Null
        $resA = Invoke-Cmd -File 'pwsh' -CmdArgs @('-NoProfile', '-File', (Join-Path $workA 'scripts\git-done.ps1'), '-Branch', 'fix/selftest-a', '-StubPushFailures', '1')
        $outA = $resA.Output -join "`n"
        Assert-St 'A：退出码 0' ($resA.Code -eq 0)
        Assert-St 'A：注入拒绝可见' ($outA -match '注入推送拒绝')
        Assert-St 'A：自动重试发生' ($outA -match '重取主线变基后重试')
        if ($resA.Code -eq 0) {
            $bareTip = (Invoke-In $originA @('rev-parse', 'main'))[0]
            $workTip = (Invoke-In $workA @('rev-parse', 'HEAD'))[0]
            Assert-St 'A：合并已上 origin（bare main = 检出 HEAD）' ("$bareTip".Trim() -eq "$workTip".Trim())
        }
        $curA = (Invoke-In $workA @('branch', '--show-current'))[0]
        Assert-St "A：检出已归位（$curA，不在 merge/*）" ("$curA" -eq 'main')
        # DX-128：正常收尾不得留 merge/<slug> 临时分支。旧实现在临时分支删除处用裸
        # `branch -d`（单次推送前 origin/main 还没含本次合并，upstream 判据恒假）拒删且
        # 把输出吞掉，每轮收尾静默留一支。这条断言钉住「推送落地路径已回收」。
        $tmpA = Invoke-Cmd -File 'git' -CmdArgs @('-C', $workA, 'rev-parse', '--verify', '--quiet', 'refs/heads/merge/fix-selftest-a')
        Assert-St 'A：临时分支已回收（不留 merge/* 残留）' ($tmpA.Code -ne 0)

        # ---- 场景 B：连续拒绝 ----
        $originB = Join-Path $tmpRoot 'b-origin.git'
        Invoke-Cmd -File 'git' -CmdArgs @('init', '-q', '--bare', $originB) | Out-Null
        $workB = Join-Path $tmpRoot 'b-work'
        New-StWorkRepo -Work $workB -OriginPath $originB
        Invoke-In $workB @('switch', '-c', 'fix/selftest-b') | Out-Null
        Add-Content -LiteralPath (Join-Path $workB 'README.md') -Value 'feature line b' -Encoding utf8
        Invoke-In $workB @('commit', '-q', '-am', 'docs: feature b') | Out-Null
        $resB = Invoke-Cmd -File 'pwsh' -CmdArgs @('-NoProfile', '-File', (Join-Path $workB 'scripts\git-done.ps1'), '-Branch', 'fix/selftest-b', '-StubPushFailures', '99')
        $outB = $resB.Output -join "`n"
        Assert-St 'B：退出码非 0' ($resB.Code -ne 0)
        Assert-St 'B：重试耗尽给出补推指引' ($outB -match '推送未落地')
        $curB = (Invoke-In $workB @('branch', '--show-current'))[0]
        Assert-St "B：检出不在 merge/*（$curB）" ("$curB" -notlike 'merge/*')

        # ---- 回执口径（DX-116）：回执只记「已发生」的事实，推送没落地不得虚报 ----
        # 场景 A 推送已落地：pushed=true、head 记已落地提交、mergedInto=origin/main；
        # 场景 B 全被拒：pushed=false、head 置空、mergedInto 不写 origin/main，恢复信息
        # 改记 localHead/localBranch（pin 分支），且该分支真实存在 —— 补推指引才可执行。
        function Read-StReceipt([string]$Work, [string]$BranchSlug) {
            $dir = Join-Path $Work '.sandbox\receipts'
            $f = @(Get-ChildItem -LiteralPath $dir -Filter "$BranchSlug-*.json" -ErrorAction SilentlyContinue)
            if ($f.Count -ne 1) { return $null }
            return Get-Content -LiteralPath $f[0].FullName -Raw -Encoding utf8 | ConvertFrom-Json
        }
        $rcA = Read-StReceipt -Work $workA -BranchSlug 'fix-selftest-a'
        if ($null -ne $rcA) {
            Assert-St 'A：回执 pushed=true' ($rcA.pushed -eq $true)
            Assert-St 'A：回执 head 非空（已落地提交）' (-not [string]::IsNullOrEmpty("$($rcA.head)"))
            Assert-St 'A：回执 mergedInto=origin/main' ("$($rcA.mergedInto)" -eq 'origin/main')
        } else {
            Assert-St 'A：回执存在（fix-selftest-a-*.json）' $false
        }
        $rcB = Read-StReceipt -Work $workB -BranchSlug 'fix-selftest-b'
        if ($null -ne $rcB) {
            Assert-St 'B：回执 pushed=false' ($rcB.pushed -eq $false)
            Assert-St 'B：回执 head 置空（只记已落地提交）' ([string]::IsNullOrEmpty("$($rcB.head)"))
            Assert-St 'B：回执 mergedInto 不写 origin/main' ([string]::IsNullOrEmpty("$($rcB.mergedInto)"))
            Assert-St 'B：回执 localHead 留痕本地合并提交' (-not [string]::IsNullOrEmpty("$($rcB.localHead)"))
            Assert-St 'B：回执 localBranch 记 pin 分支' ("$($rcB.localBranch)" -eq 'merge/fix-selftest-b')
        } else {
            Assert-St 'B：回执存在（fix-selftest-b-*.json）' $false
        }
        $pinB = Invoke-Cmd -File 'git' -CmdArgs @('-C', $workB, 'rev-parse', '--verify', '--quiet', 'refs/heads/merge/fix-selftest-b')
        Assert-St 'B：合并提交已 pin 到 merge/fix-selftest-b（补推落点真实存在）' ($pinB.Code -eq 0)
    } finally {
        if (Test-Path -LiteralPath $tmpRoot) { Remove-Item -Recurse -Force $tmpRoot -ErrorAction SilentlyContinue }
    }
    if ($script:stFails.Count -gt 0) {
        $script:stFails | ForEach-Object { Write-Host ("  ✗ " + $_) -ForegroundColor Red }
        Write-Host ("✗ git-done selftest 失败 {0} 项" -f $script:stFails.Count) -ForegroundColor Red
        exit 1
    }
    Write-Host '✓ git-done selftest 通过：首拒重试落地 / 连拒不虚报（退出码非 0、回执只记已落地、pin 分支在）且检出归位'
    exit 0
}

Test-CheckoutDrift -ScriptTree $scriptRepoRoot -Allowed:$AllowCrossTree

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
    $Branch = Get-NbBranch
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

# ---- 收尾对账的准备（DX-33）----
# 必须在合并**之前**抓：合并一推送，这条分支的所有提交都成了 origin/main 的祖先，
# 再想区分「本次领的卡」和「历史上领过的卡」就只能靠猜。这份名单用来回答一个问题 ——
# 这条分支领了卡、收尾却没把它结掉，是漏了还是确实没做完。
#
# 解析交给 derive_board_status.py --emit-claimed，不在 PS 里做正则：直接抓
# `git log --format=%s` 再匹配中文「领取」，会因外层按控制台代码页解码子进程输出
# 变成乱码（换行还会丢，两条提交粘成一行），正则永不命中 —— 实测踩过。
# 那边显式 encoding="utf-8"，输出的又是纯 ASCII 短号，跨代码页安全。
$claimedCodes = @()
$claimListScript = Join-Path $repoRoot 'scripts\derive_board_status.py'
if (Test-Path -LiteralPath $claimListScript) {
    $claimListRes = Invoke-Cmd -File 'python' -CmdArgs @(
        $claimListScript, '--emit-claimed', '--base', "origin/$MainBranch", '--head', $Branch)
    if ($claimListRes.Code -eq 0) {
        $claimedCodes = @($claimListRes.Output | Where-Object { $_ -and ($_ -match '^[A-Za-z0-9-]+$') })
    } else {
        Write-Host '· 结单对账：读不到本次分支的领用登记，收尾将不做对账' -ForegroundColor DarkGray
    }
}
$claimedCodes = @($claimedCodes | Select-Object -Unique | Sort-Object)

# -Close：只受理本分支领用过的卡。凭空结卡两头都有害 —— 已交付的卡会被重新领用重做，
# 没做完的卡会显示完成，而 baseline=done 正是「拒绝重新领用」的唯一门（条款 6）。
$closeList = @()
if ($Close) {
    $closeList = @($Close -split '[,\s]+' | Where-Object { $_ } |
        ForEach-Object { ($_.TrimStart('#')).ToUpperInvariant() } | Select-Object -Unique)
}
if ($closeList.Count -gt 0) {
    $outside = @($closeList | Where-Object { $claimedCodes -notcontains $_ })
    if ($outside.Count -gt 0) {
        Write-Host ("`n✗ -Close 指定的卡不属于本次分支领用：{0}" -f ($outside -join '、')) -ForegroundColor Red
        if ($claimedCodes.Count -gt 0) {
            Write-Host ("  本次分支领用：{0}" -f ($claimedCodes -join '、')) -ForegroundColor DarkGray
        } else {
            Write-Host '  本次分支没有领用任何卡。' -ForegroundColor DarkGray
        }
        Write-Host '  结卡只对「领了、做完了、但没写 =done 标记」这一种情形开口子。' -ForegroundColor Yellow
        exit 8
    }
}

if ($Mode -eq 'remote') {
    # ---- 3. remote 模式：临时分支 + 快进推送，不碰主 worktree ----
    # 收尾要能回到「进来时在哪」（DX-20）：本模式中途会 checkout 临时分支，原逻辑最后一律
    # `switch --detach`，于是锚点区被永久留在分离 HEAD —— 下一次收尾 Sync-AnchorWorktree
    # 读到的分支名是空串，判成「锚点区不在 main」直接跳过，本地 main 从此永远落后远端一步。
    # 先记住起点与「当前根是不是锚点」，收尾按它归位。
    $pristineBranch = Get-NbSymRef
    $pristineHead = (Invoke-Git rev-parse HEAD).Trim()
    $anchorRoot = Get-AnchorWorktree
    $atAnchor = $false
    if ($anchorRoot) {
        try {
            $atAnchor = ([System.IO.Path]::GetFullPath($repoRoot) -eq [System.IO.Path]::GetFullPath($anchorRoot))
        } catch { $atAnchor = $false }
    }

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
        $claimFetch = Invoke-NetGit @('fetch', 'origin', '+refs/claims/*:refs/claims/*')
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

    # DX-120：目标分支所在 worktree 的反查必须发生在 `switch -c $tmp` **之前** ——
    # switch 会把当前检出从 $Branch 摘走（merge/<slug> 临时分支接管），此后按
    # `branch refs/heads/<Branch>` 反查永远落空，$wtPath 为空，回收段整段静默跳过：
    # 从自身 worktree 跑收尾（§7.3 的标准姿势）永远回收不了自己，只留 detached 尸体
    # （DX-109 收尾实测）。此刻分支仍被检出，反查才有效。
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
    $tmp = 'merge/' + ($Branch -replace '[^A-Za-z0-9]+', '-')
    & git rev-parse --verify -q $tmp | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "· 清理残留临时分支 $tmp" -ForegroundColor DarkGray
        # DX-78 acc2：先脱离自家检出占用 —— 上一次运行可能把本检出留在 merge/* 上（旧版
        # 冲突退出行为 / 手工中断），branch -D 对被检出的分支必然拒绝，重跑就走不通了。
        if ((Get-NbSymRef) -eq $tmp) {
            Invoke-GitQuiet @('switch', '--detach') | Out-Null
            Write-Host ("  · 检出正停在 {0} 上，已先脱离（DX-78）" -f $tmp) -ForegroundColor DarkGray
        }
        $delTmp = Invoke-GitQuiet @('branch', '-D', $tmp)
        if ($delTmp.Code -ne 0) {
            # 别的检出占用等删不掉的情形：不在此处中止 —— 下一步 switch -c 会给出明确
            # 诊断并走 Restore-ConflictScene，这里降级为提示即可。
            Write-Host "  · 临时分支删除失败（可能被其它检出占用），继续：" -ForegroundColor Yellow
            $delTmp.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        }
    }

    $script:dryEntryRef = Get-NbSymRef
    if ($DryRun -and -not $script:dryEntryRef) { Write-Host '· [dry-run] 进入时已在分离 HEAD，结束时无可回滚' -ForegroundColor DarkCyan }
    Write-Host "· 基于 origin/$MainBranch 创建临时分支 $tmp" -ForegroundColor DarkGray
    $switchRes = Invoke-GitQuiet @('switch', '-c', $tmp, "origin/$MainBranch")
    $mergeRes = $null
    if ($switchRes.Code -eq 0) {
        Write-Host "· 合并 $Branch（--no-ff，保留分支合并痕迹）" -ForegroundColor DarkGray
        $mergeBody = New-MergeCommitBody -Branch $Branch -MainBranch $MainBranch
        $mergeRes = Invoke-GitQuiet @('merge', '--no-ff', $Branch, '-m', "merge: 合入 $Branch", '-m', $mergeBody)
    }
    $inConflict = ($switchRes.Code -eq 0) -and
        ((Invoke-GitQuiet @('rev-parse', '--verify', '--quiet', 'MERGE_HEAD')).Code -eq 0)
    if ($switchRes.Code -ne 0 -or ($mergeRes -and $mergeRes.Code -ne 0 -and -not $inConflict)) {
        # DX-107：失败退出前先回滚现场 —— 不留 merge/* 临时分支、不留冲突态、不停在临时分支上；
        # dry-run 承诺的「切回原分支」同样在这里兑现。
        Restore-ConflictScene -EntryRef $pristineBranch -EntryHead $pristineHead -TempBranch $tmp
        Write-Host "`n✗ 合并失败（非冲突），现场已回滚：" -ForegroundColor Yellow
        if ($switchRes.Code -ne 0) { $switchRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor Yellow } }
        if ($mergeRes) { $mergeRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor Yellow } }
        exit 1
    }
    if ($inConflict) {
        $conflicted = @((Invoke-GitQuiet @('diff', '--name-only', '--diff-filter', 'U')).Output |
            Where-Object { $_ -and $_.ToString().Trim() -ne '' })
        # DX-108：progress.json 是纯派生产物（AGENTS §11.2——唯一写者是收尾的 derive --write），
        # 内容冲突不存在需要人脑的语义合并，正确解恒为「任取一侧后由派生器整体重算」。
        # 台账 docs/board-ledger.json 承载任务定义（加卡/改契约要授权），**不是**纯派生物，
        # 含它的冲突不进本通道，仍走人工。这里的 --ours 也不是 §7.5 禁止的盲解：
        # 取来的内容不作为最终结果，收尾的 Sync-BoardSnapshot（derive --write）必然整体重算。
        # 实测背景：DX-106 交付当日三次撞 progress.json 冲突，每次手工五步善后全靠人记。
        $nonDerived = @($conflicted | Where-Object { ($_ -replace '\\', '/') -ne 'progress.json' })
        if ($conflicted.Count -gt 0 -and $nonDerived.Count -eq 0) {
            foreach ($f in $conflicted) { [void](Invoke-GitQuiet @('checkout', '--ours', '--', $f)) }
            [void](Invoke-GitQuiet (@('add', '--') + $conflicted))
            $mcRes = Invoke-GitQuiet @('commit', '--no-edit')
            if ($mcRes.Code -eq 0) {
                Write-Host ("· 快照冲突自动善后（DX-108）：冲突仅 progress.json（纯派生物），取主线版完成合并，收尾派生器整体重算（{0}）" -f ($conflicted -join '、')) -ForegroundColor DarkGray
            } else {
                Write-Host "`n✗ 快照冲突自动善后失败（完成合并提交被拒），请手工解决：" -ForegroundColor Yellow
                $mcRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor Yellow }
                Write-Host "`n解决后：git add -A ; git commit ; git push origin HEAD:$MainBranch`n" -ForegroundColor Yellow
                exit 1
            }
        } else {
            # DX-107：含非派生文件的冲突转人工，退出前同样回滚现场（不留冲突态与临时分支）。
            Restore-ConflictScene -EntryRef $pristineBranch -EntryHead $pristineHead -TempBranch $tmp
            Write-Host "`n✗ 合并冲突（含非派生文件，不自动善后），现场已回滚，请手工解决后重跑收尾：" -ForegroundColor Yellow
            $mergeRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor Yellow }
            $conflicted | ForEach-Object { Write-Host ("   冲突文件：{0}" -f $_) -ForegroundColor Yellow }
            Write-Host "`n解决后：git add -A ; git commit ; 重跑 pwsh -File scripts/git-done.ps1 -Branch $Branch`n" -ForegroundColor Yellow
            exit 1
        }
    }

    # DX-106：合并先落本地，不单独推送 —— 与三步收尾提交合成对 main 的**唯一一次**推送
    # （收口在 Push-Main），Board 每批收尾只建 1 个 run 且落点必为自洽 tip。
    $mergedCommit = (Invoke-Git rev-parse HEAD).Trim()
    if ($DryRun) {
        Write-Host ("✓ [dry-run] 合并在本地完成（合并提交 {0}，将随收尾一次推送 origin/{1}）" -f $mergedCommit.Substring(0, 7), $MainBranch) -ForegroundColor Green
    } else {
        Write-Host ("✓ 合并在本地完成（合并提交 {0}，将随收尾一次推送 origin/{1}）" -f $mergedCommit.Substring(0, 7), $MainBranch) -ForegroundColor Green
    }

    # ---- 4. 清理：先回收 worktree（解除分支占用），再删分支 ----
    # DX-120：$wtPath 的反查已提前到 switch -c 之前（见上），此处只做判定与执行。
    $recycleAgent = ''
    $recycleScope = ''
    # DX-21：分支所在路径本来就能从 `worktree list` 反查，不该再要求调用方记一次参数
    # （忘传 → 目录不删 → 分支被占用删不掉 → release-claims 判分支还在 → 台账留僵尸 claim）。
    # 默认回收 .worktrees/ 下的目标区；-KeepWorktree 显式保留；-RemoveWorktree 退化为
    # 「强制回收」（跳过下面两道安全检查），旧命令照旧能用。
    $selfWorktreePending = $null
    if ($DryRun -and -not $KeepWorktree) {
        Write-Host '· [dry-run] 跳过 worktree 回收与分支删除（演练不动现场，结束时切回原分支）' -ForegroundColor DarkCyan
    }
    if ((-not $KeepWorktree) -and $wtPath) {
        $abs = if ([System.IO.Path]::IsPathRooted($wtPath)) { $wtPath } else { Join-Path $repoRoot $wtPath }
        $abs = [System.IO.Path]::GetFullPath($abs)
        $currentRoot = [System.IO.Path]::GetFullPath($repoRoot)
        # 安全检查：删掉正在办的工作区是事故。两道判据都不成立才回收 ——
        #   1) 有提交没进本次合并（说明它不是「已合入的尾巴」）；
        #   2) 还有未提交改动（含 untracked），删了就没了。
        # DX-106：单次推送下他区回收发生在推送**之前**，origin 上还没有这些提交 ——
        # 旧的「未推送提交」判据（--not --remotes=origin）在这里恒真，会把每个
        # 工作区都判成不可回收、永远留僵尸。改判「分支提交是否已全部进本次合并」：
        # 刚做的 --no-ff 合并就在 $mergedCommit 里，不在其中的提交才是真会丢的东西。
        $unsafe = ''
        if (-not $RemoveWorktree) {
            $ahead = 0
            $aheadRes = Invoke-GitQuiet @('-C', $abs, 'rev-list', '--count', 'HEAD', '--not', $mergedCommit)
            if ($aheadRes.Code -eq 0) {
                $first = $aheadRes.Output | Select-Object -First 1
                if ($first) { [void][int]::TryParse($first.ToString().Trim(), [ref]$ahead) }
            }
            $dirtyNum = 0
            if ($ahead -le 0) {
                $dirtyRes = Invoke-GitQuiet @('-C', $abs, 'status', '--porcelain')
                $dirtyNum = @($dirtyRes.Output | Where-Object { $_ -and $_.ToString().Trim() -ne '' }).Count
            }
            if ($ahead -gt 0) { $unsafe = "有 $ahead 个提交未进本次合并" }
            elseif ($dirtyNum -gt 0) { $unsafe = "还有 $dirtyNum 项未提交改动" }
        }
        if ($abs -eq $currentRoot) {
            # DX-120：当前检出就是目标 worktree。不能在推送前删 —— 三步收尾提交与
            # 回执都还要靠这个检出完成，且 Windows 不允许进程删除 CWD 所在目录。
            # 记下来，推送落地后（结单对账与回执之后）由外部检出执行回收。
            if ($unsafe) {
                Write-Host ("· 跳过回收当前 worktree：{0}（确认要删用 -RemoveWorktree）" -f $unsafe) -ForegroundColor Yellow
            } elseif ($DryRun) {
                Write-Host '· [dry-run] 当前 worktree 将在推送落地后回收（演练不删）' -ForegroundColor DarkCyan
            } else {
                $selfWorktreePending = $abs
            }
        } elseif (Test-Path -LiteralPath $abs) {
            if ($unsafe) {
                Write-Host ("· 跳过回收 {0}：{1}（可能是别的工作区在办；确认要删用 -RemoveWorktree）" -f $abs, $unsafe) -ForegroundColor Yellow
            } elseif ($DryRun) {
                # DX-120：演练只判定不动手，把「会怎么处置」说出来，供负测断言。
                Write-Host ("· [dry-run] 将回收 worktree {0}（演练不删）" -f $abs) -ForegroundColor DarkCyan
            } else {
                # 身份与 scope 必须在回收**之前**抓：.agent-env.ps1 随目录一起消失，事后再读只能
                # 得到空，于是每次都「跳过释放」，agent-workspace.json 里堆已删工作区的僵尸条目
                # （2026-11-04 实测 dx12 / dx14 / dx16 / r1402 / initfix 五条全在）。
                $recycleAgent = Get-WorktreeAgent -Path $abs
                if ($recycleAgent -and ((Split-Path -Leaf (Split-Path -Parent $abs)) -eq '.worktrees')) {
                    $recycleScope = "worktree:$recycleAgent"
                }
                Write-Host ("· 回收 worktree {0}" -f $abs) -ForegroundColor DarkGray
                if ((Invoke-GitQuiet @('worktree', 'remove', $abs)).Code -ne 0) {
                    Invoke-GitQuiet @('worktree', 'remove', '--force', $abs) | Out-Null
                }
            }
        }
    }
    if ($DryRun) {
        Write-Host ("· [dry-run] 保留分支 {0}（演练不删）" -f $Branch) -ForegroundColor DarkCyan
    } elseif (-not $KeepBranch) {
        Write-Host ("· 删除分支 {0}" -f $Branch) -ForegroundColor DarkGray
        # DX-109：判据换锚点（见 Remove-MergedBranch）—— 单次推送下删除在推送之前，
        # `branch -d` 的 upstream 判据必然误拒。
        if (Remove-MergedBranch -Name $Branch -AnchorCommit $mergedCommit) {
            Write-Host ("✓ 已删除分支 {0}" -f $Branch) -ForegroundColor DarkGray
        }
    }

    # 开发区占用收尾释放（DX-05）：合并已经成功，这里失败只当卫生问题告警，不回头改结果。
    # 传入回收前抓到的身份 / scope；没走回收路径时留空，函数自己按 Root 解析（原地模式）。
    Release-WorkspaceClaim -Root $repoRoot -Agent $recycleAgent -Scope $recycleScope

    # 解除临时分支占用：先**无条件**脱离分支（DX-26）。
    # 收尾还有三步自动提交（claims 清理 / 结卡 / 快照），它们必须不在受保护分支上做 ——
    # pre-commit 规则 1 禁止在 main 直接提交，而锚点区此刻若还停在 main 上，三步必被自家
    # 钩子拒掉。这正是 DX-20 把「归位」提前到提交之前造成的回归：一天内两次收尾、六次
    # 提交全失败，每次都留 staged 脏文件（回滚只还工作区不撤暂存区）。分离 HEAD 读不到
    # 分支名，规则 1 不触发；三步做完再归位，见本节末尾「锚点区归位（DX-20）」。
    $detachRes = Invoke-GitQuiet @('switch', '--detach')
    if ($detachRes.Code -ne 0) {
        Write-Host '· 警告：无法脱离当前分支（工作区有冲突改动），随后的自动清理会被分支保护拦下' -ForegroundColor Yellow
        $detachRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
    }
    if ($DryRun) { Write-Host '  [dry-run] 三步收尾前已 switch --detach（DX-26 时序）' -ForegroundColor DarkCyan }
    # DX-128：临时分支 $tmp 同样删在单次推送之前（此刻 origin/main 还没含本次合并），
    # 裸 `branch -d` 的 upstream 判据恒假、必然以 not fully merged 拒删 —— 旧实现还把
    # 输出 Out-Null 吞掉、不检查返回码，于是每轮正常收尾静默留一支 merge/<slug>
    #（不同分支名连 1084 行的同名残留清理都兜不住）。判据统一走 Remove-MergedBranch
    #（DX-109：确认提交已全部进本次合并后用 -D），与功能分支 1233 / 1629 同锚点；
    # 演练也回收自己建的临时分支（纯本地、不触网）。删不掉必须可见、给可执行命令，
    # 但仍是 fail-soft，绝不阻断已成功的合并。
    # 有意保留的例外：1394 行推送失败时重建的 pin 分支是 DX-116 的补推落点，不在此处。
    if (-not (Remove-MergedBranch -Name $tmp -AnchorCommit $mergedCommit)) {
        Write-Host ("· 临时分支 {0} 未回收（见上方诊断），收尾后手工：git branch -D {0}" -f $tmp) -ForegroundColor Yellow
    }

    # DX-106：收尾提交栈底 = 本次合并提交。单次推送失败需整体回滚时回到这里 ——
    # 回滚只丢弃收尾提交，随后**单独推送合并**（fail-soft 交付不被收尾拖住）。
    $closeoutBase = (Invoke-Git rev-parse HEAD).Trim()
    $hasCloseoutCommit = $false

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
        Assert-DryCloseStep -Step '台账领用登记清理提交'
        Invoke-GitQuiet @('add', 'docs/board-ledger.json') | Out-Null
        $rcCommit = Invoke-GitQuiet @('commit', '-m', ('chore(board): 清理过期领用登记 {0}' -f ($goneCodes -join '、')))
        if ($rcCommit.Code -ne 0) {
            Write-Host '· 台账领用登记清理：提交失败，留待下次合并再清' -ForegroundColor Yellow
            # 必须连暂存区一起撤（DX-26）：git checkout -- 只还工作区，add 过的结果会留在
            # 暂存区，被下一次提交顺带夹带 —— P0 收尾时就这样把代码文件带进了 chore(board)。
            $rcCommit.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
            Invoke-GitQuiet @('restore', '--staged', '--worktree', '--', 'docs/board-ledger.json') | Out-Null
        } else {
            # DX-106：提交留在本地，随合并+收尾单次推送（重试集中在 Push-Main 处）。
            $hasCloseoutCommit = $true
            if ($DryRun) {
                Write-Host '✓ [dry-run] 台账领用登记清理：本地已提交（未推送）' -ForegroundColor Green
            } else {
                Write-Host ('✓ 台账领用登记清理：{0} 已提交（随收尾一次性补推）' -f ($goneCodes -join '、')) -ForegroundColor Green
            }
        }
    }

    # 结卡自动化（DX-07）：baseline=<短号> done 是「拒绝重新领用」的唯一门（条款 6）。
    # 过去靠维护者记得批量结 —— 一定漂；每漂一次，已交付的卡就能被再领一遍重做
    # （pitfalls 1.14 踩过，7.1 记作「待拍板」）。现在合并成功即结：从 git 事实里取
    # 最后一次 #<短号>=done，把 baseline 补成 done（顺带补掉缺 baseline 条目的卡）。
    # 与上面的 claims 清理同一套路 —— 分离 HEAD 上就地提交、快进补推；
    # 失败只回滚该提交，绝不阻断已经成功的合并。
    $ccArgs = @((Join-Path $repoRoot 'scripts\derive_board_status.py'), '--close-cards')
    if ($closeList.Count -gt 0) {
        $ccArgs += @('--close-explicit', ($closeList -join ','))
    }
    # 结果真相（DX-58）：closed 只记「推送已落地」的，回滚挂起的进 closePending——
    # DX-57 实测回执凭派生器输出记意图，补推撞 non-ff 回滚后仍虚报 closed，
    # baseline 悬空期间卡可被重新领用（pitfalls 1.14 风险回归）。
    $ccLanded = @()
    $ccPending = @()
    $ccRes = Invoke-Cmd -File 'python' -CmdArgs $ccArgs
    $closedCodes = @($ccRes.Output | Where-Object { $_ -and $_ -match '^\S+$' })
    $explicitClosed = @($closedCodes | Where-Object { $closeList -contains $_ })
    if ($ccRes.Code -ne 0) {
        Write-Host ("· 结卡：派生器失败（退出码 {0}），baseline 留待下次合并再结" -f $ccRes.Code) -ForegroundColor Yellow
    } elseif ($closedCodes.Count -gt 0) {
        Assert-DryCloseStep -Step '结卡提交'
        Invoke-GitQuiet @('add', 'docs/board-ledger.json') | Out-Null
        # DX-41：把 `#<短号>=done` 写进主题，让 .githooks/post-commit 的
        # `grep -Eq '#[A-Za-z][A-Za-z0-9]*'` 认到标记，投 hook.commit 事件到 hooks.db，
        # plan-hook 才会把看板上的对应任务从 doing 挪到 done。
        # 以前只写「结卡 <短号>」（不带 #），钩子判不到标记，六张已交付的卡在看板上
        # 一直挂僵尸 doing（R9-04 / DX-26 / R15-02 / R16-02 / R16-03 / R16-05）。
        # 注意 PS 插值写法：`"#{$_}=done"` 会把 `{` / `}` 当字面量留进字符串（本会话实测踩过：
        # 提交主题变成 chore(board): 结卡 #{DX-42}=done，post-commit 的 grep
        # `#[A-Za-z][A-Za-z0-9]*` 判不到 `{`，标记照旧不投）。`"#$_=done"` 才展开成 #DX-42=done。
        $ccMarkers = ($closedCodes | ForEach-Object { "#$_=done" }) -join ' '
        $ccSubject = 'chore(board): 结卡 {0}' -f $ccMarkers
        $ccBody = "对象：按 git 标记 #<短号>=done 自动结卡（DX-07），把 baseline 补成 done。`n`n" +
                  "自动来源：git-done.ps1 合并收尾的 derive_board_status.py --close-cards。"
        if ($explicitClosed.Count -gt 0) {
            # 人工确认与 git 推导走同一条只改 baseline 的通道，但必须分得开：
            # 事后查「这张卡凭什么结的」，一处看标记，一处看这行。
            $ccBody += "`n`n人工确认：" + ($explicitClosed -join '、') +
                       "（-Close 显式声明，提交信息里没有 #<短号>=done 标记）"
        }
        $ccCommit = Invoke-GitQuiet @('commit', '-m', $ccSubject, '-m', $ccBody)
        if ($ccCommit.Code -ne 0) {
            Write-Host '· 结卡：提交失败，baseline 留待下次合并再结' -ForegroundColor Yellow
            $ccCommit.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
            $ccPending = @($closedCodes)
            Invoke-GitQuiet @('restore', '--staged', '--worktree', '--', 'docs/board-ledger.json') | Out-Null
        } else {
            # DX-106：结卡提交留在本地，随合并+收尾单次推送；是否 landed 由这次推送统一裁定
            # （DX-58 口径不变：closed 只记「推送已落地」的，其余进 pending）。
            $hasCloseoutCommit = $true
            if ($DryRun) {
                Write-Host '✓ [dry-run] 结卡：本地已提交（未推送）' -ForegroundColor Green
            } else {
                Write-Host ('✓ 结卡：{0} 的 baseline 已补为 done（随收尾一次性补推）' -f ($closedCodes -join '、')) -ForegroundColor Green
            }
        }
    }

    # 快照刷新闭环（DX-13）：结卡改的是 baseline，快照是派生产物，必须单独重算一次。
    # 放在结卡之后 —— 先改台账再派生，才能把刚结掉的卡写进快照。
    # DX-96：返回值 =「本次是否产生了待推提交」；是则计入收尾单次补推。
    $snapPending = Sync-BoardSnapshot -RepoRoot $repoRoot -MainBranch $MainBranch
    if ($snapPending) { $hasCloseoutCommit = $true }

    # ---- 合并+收尾单次推送（DX-106，A2b）----
    # 合并提交与清理登记 / 结卡 / 快照的提交全部落在本地后，对 main 只推**一次**：
    # Board 每批收尾只建 1 个 run，落点必为自洽 tip —— 中间态在结构上不存在，
    # DX-44 的 cancel-in-progress 与沉降等待降级为纯双保险。
    # 撞 non-ff 时重取变基重试（上限 3 次，DX-58 重试收口保留）；仍失败则整体回滚
    # 收尾提交到 $closeoutBase，**单独推送合并**（fail-soft 重排：交付不被收尾拖住）。
    # DX-116：本次对 main 的推送是否落地。「✓ 合并完成」、回执的 pushed/head/mergedInto
    # 全部以它为准 —— 推送没落地就报完成，等于把「我做了」说成「它好了」（Z9）。
    $mainPushLanded = $false
    # 撞 non-ff 自动恢复重试（DX-78 单一实现 Invoke-MainPushWithRetry，上限 3 次）；
    # 仍失败则整体回滚收尾提交到 $closeoutBase，**单独推送合并**（fail-soft 重排：交付不被收尾拖住）。
    $finalPush = Invoke-MainPushWithRetry
    $finalOk = $finalPush.Ok
    if ($finalOk) {
        $mainPushLanded = $true
        $ccLanded = @($closedCodes)
        if ($DryRun) {
            Write-Host '✓ [dry-run] 合并+收尾单次推送：桩成功（未触网）' -ForegroundColor Green
        } else {
            Write-Host ('✓ 单次推送：合并与清理/结卡/快照已一起推上 {0}（落点自洽 tip）' -f $MainBranch) -ForegroundColor Green
        }
    } else {
        if ($hasCloseoutCommit) {
            Write-Host '· 单次推送失败（离线 / 主线被推进 / 变基冲突）：整体回滚收尾提交到合并点，改为单独推送合并（fail-soft 交付）' -ForegroundColor Yellow
            Invoke-GitQuiet @('reset', '--hard', $closeoutBase) | Out-Null
        } else {
            Write-Host '· 单次推送失败（离线 / 主线被推进 / 变基冲突）：改为单独推送合并（fail-soft 交付）' -ForegroundColor Yellow
        }
        $ccPending = @($closedCodes)
        $mergePush = Push-Main
        if ($mergePush.Code -eq 0) {
            $mainPushLanded = $true
            Write-Host '✓ 合并已单独推上 origin/main；收尾提交已回滚，留待下次合并' -ForegroundColor Yellow
        } else {
            Write-Host '· 合并也未能推送：全部留在本地，下次合并继续' -ForegroundColor Yellow
            # DX-116：临时分支已在三步收尾前删除，合并提交此刻只活在分离 HEAD 上 ——
            # 检出一挪走它就只剩 reflog 可打捞。重建同名临时分支把合并提交 pin 住，
            # 结尾「推送未落地」总结里的补推指引才真正可执行（acc3 声称的落点从谎言变事实）。
            [void](Invoke-GitQuiet @('branch', $tmp, $mergedCommit))
        }
    }

    # 合并收尾：释放已合入主线的认领占位（refs/claims/*），避免永久残留误拦重新认领。
    # DX-106：单次推送落地后才释放 —— 此刻本次合并的 head 才成为 origin/main 的祖先。
    Invoke-ClaimRefCleanup -Main $MainBranch

    if ($DryRun) {
        Write-Host '· [dry-run] 跳过锚点同步（演练不碰锚点区）' -ForegroundColor DarkCyan
    } elseif (-not $SkipAnchorSync) {
        Sync-AnchorWorktree -Main $MainBranch -CurrentRoot $repoRoot
    } else {
        Write-Host '· 已跳过锚点同步（-SkipAnchorSync）' -ForegroundColor Yellow
    }

    # ---- 结单对账（DX-33）----
    # --close-cards 只处理「已经写了 =done 标记」的卡；没写的卡一张都不结，而原先收尾
    # 在这种情况下连一行提示都没有 —— 干完活、合并、全绿，卡还挂在 todo，下次有人接着领
    # 一遍重做。程序判不了 acceptance 满不满足，但「领了没结」是两份名单的差集，能判。
    # DX-96：挪到单次补推之后 —— landed/pending 此刻才有最终答案。
    $missedCodes = @($claimedCodes | Where-Object { $ccLanded -notcontains $_ -and $ccPending -notcontains $_ })
    if ($claimedCodes.Count -gt 0) {
        Write-Host ''
        if ($ccPending.Count -gt 0) {
            Write-Host ("· 结单对账：本次领用 {0} 张、已结 {1} 张、挂起 {2} 张（{3}）——挂起的 baseline 未落，待下次合并再结" -f `
                $claimedCodes.Count, $ccLanded.Count, $ccPending.Count, ($ccPending -join '、')) -ForegroundColor Yellow
        } elseif ($missedCodes.Count -eq 0) {
            Write-Host ("✓ 结单对账：本次领用的 {0} 张已全部结掉（{1}）" -f `
                $claimedCodes.Count, ($claimedCodes -join '、')) -ForegroundColor Green
        } else {
            Write-Host ("· 结单对账：本次领用 {0} 张、结掉 {1} 张，还有 {2} 张没结" -f `
                $claimedCodes.Count, $closedCodes.Count, $missedCodes.Count) -ForegroundColor Yellow
            $ledgerPath = Join-Path $repoRoot 'docs\board-ledger.json'
            $ledgerObj = $null
            if (Test-Path -LiteralPath $ledgerPath) {
                # 必须显式 utf8：5.1 会把无 BOM 的 UTF-8 当 GBK 读，中文尾字节能吞掉 JSON 引号
                try { $ledgerObj = (Get-Content -LiteralPath $ledgerPath -Raw -Encoding utf8 | ConvertFrom-Json) } catch { $ledgerObj = $null }
            }
            foreach ($code in $missedCodes) {
                $card = $null
                if ($ledgerObj) { $card = $ledgerObj.cards.$code }
                if ($card) {
                    Write-Host ("    {0}  {1}" -f $code, $card.title) -ForegroundColor Yellow
                    foreach ($item in @($card.acceptance)) {
                        if ($item) { Write-Host ("        · {0}" -f $item) -ForegroundColor DarkGray }
                    }
                } else {
                    Write-Host ("    {0}  （台账里查不到这张卡）" -f $code) -ForegroundColor Yellow
                }
            }
            Write-Host '    做完了只是没写标记：重跑收尾并加 -Close <短号,...>；' -ForegroundColor Yellow
            Write-Host '    确实没做完：让卡留在 todo，下次接着领。' -ForegroundColor Yellow
        }
    }

    # DX-95：747 行的清理跑在 release-claims / close-cards 之前 —— 本次合并自身的卡那时
    # baseline 还是 todo，判据 B（done 且无分支引用）必然 keep，rebase 重写占位漏释
    # （DX-90 交付当日实测：DX-76/90/91 三条滞留 origin，只能手工 --release-zombies 补清；
    # 1111 行那次调用属 local 模式专属，remote 模式 1110 行 exit 0 根本到不了）。
    # 台账三步（登记清理 / 结卡 / 快照）完成后 baseline 已落 done，这里再清一次即命中；
    # 函数内部判定与释放全幂等，重复调用无害。
    Invoke-ClaimRefCleanup -Main $MainBranch

    # ---- 锚点区归位（DX-20；DX-26 起移到所有自动提交之后）----
    # 锚点区必须回到主分支并快进 —— 它被留在分离 HEAD 正是「本地 main 永远落后远端」
    # 的源头（Sync-AnchorWorktree 见到空分支名就跳过）。开发工作区保持「停在合并结果
    # 上的分离 HEAD」：下一步多半就被回收，硬切回合并前的分支反而把工作区内容退回去。
    # 位置：必须在上面所有 commit 之后 —— main 上禁止直接提交（pre-commit 规则 1），
    # 提前归位会让 claims / 结卡 / 快照三步自动提交全部被拒（DX-20 → DX-26 实测）。
    if ($atAnchor) {
        $homeBranch = if ($pristineBranch) { $pristineBranch } else { $MainBranch }
        # 两个坑（2026-11-04 实测，都会让归位切到一个不存在的引用）：
        #   1) 进入时所在的分支可能刚被本脚本删掉 —— 在锚点区直接对 main 开分支时，
        #      $Branch 与 $pristineBranch 是同一个名字，收尾先删分支再归位必然失败；
        #   2) 上一次归位失败会把锚点留在 merge/<...> 临时分支上，它不是归宿。
        # 两种都回主分支。
        if ($homeBranch -eq $tmp -or $homeBranch -like 'merge/*') {
            $homeBranch = $MainBranch
        } elseif ((Invoke-GitQuiet @('rev-parse', '--verify', '--quiet', "refs/heads/$homeBranch")).Code -ne 0) {
            Write-Host ("· 归位目标 {0} 已不存在（本次收尾刚删除），改回 {1}" -f $homeBranch, $MainBranch) -ForegroundColor DarkGray
            $homeBranch = $MainBranch
        }
        # 三步提交各自 push 过，但 remote-tracking ref 未必已最新；快进前再 fetch 一次
        # （离线时失败也无妨：ff 那里会给出明确的失败诊断与手工命令）。
        Invoke-NetGit @('fetch', 'origin', '--quiet') | Out-Null
        $sw = Invoke-GitQuiet @('switch', $homeBranch)
        if ($sw.Code -eq 0) {
            if ($homeBranch -eq $MainBranch) {
                if ($DryRun) {
                    Write-Host '  [dry-run] 桩：git merge --ff-only origin/main（演练不移动锚点分支）' -ForegroundColor DarkCyan
                    Write-Host '✓ [dry-run] 归位发生在所有自动提交之后（DX-26 时序，演练确认）' -ForegroundColor Green
                } else {
                    $ff = Invoke-GitQuiet @('merge', '--ff-only', "origin/$MainBranch")
                    if ($ff.Code -eq 0) {
                        Write-Host ("✓ 锚点区已归位 {0} 并快进到 origin/{0}" -f $MainBranch) -ForegroundColor Green
                    } else {
                        Write-Host ("· 锚点区已切回 {0}，但快进失败（本地分叉或被未跟踪文件挡住），手工：" -f $MainBranch) -ForegroundColor Yellow
                        Write-Host ("   git -C {0} merge --ff-only origin/{1}" -f $repoRoot, $MainBranch) -ForegroundColor Yellow
                        $ff.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
                    }
                }
            } else {
                Write-Host ("· 锚点区已归位分支 {0}（非 {1}，不自动快进）" -f $homeBranch, $MainBranch) -ForegroundColor DarkGray
            }
        } else {
            Write-Host ("· 锚点区归位失败（工作区改动会冲突），请手工：git -C {0} switch {1}" -f $repoRoot, $homeBranch) -ForegroundColor Yellow
            $sw.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        }
    }

    # ---- 演练回滚（DX-50）----
    # -DryRun 的合并与三步收尾提交只发生在本地，切回进入时的分支即全部丢弃；
    # 锚点区演练不走这里（归位块自己打桩，不移动任何分支）。
    if ($DryRun -and -not $atAnchor) {
        if ($script:dryEntryRef) {
            $back = Invoke-GitQuiet @('switch', $script:dryEntryRef)
            if ($back.Code -eq 0) {
                Write-Host ("✓ [dry-run] 演练结束，已切回 {0}（演练提交全部丢弃，未触网）" -f $script:dryEntryRef) -ForegroundColor Green
            } else {
                Write-Host ("· [dry-run] 切回 {0} 失败，请手工 git switch {0}" -f $script:dryEntryRef) -ForegroundColor Yellow
                $back.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
            }
        }
    }

    # ---- 结构化回执（DX-34）----
    # agent 读的是屏幕输出，但收尾之后工作区多半立刻被回收，屏幕一滚就没了。
    # 回执必须落在锚点区的 .sandbox/receipts/（已加到 .gitignore），不能在 worktree 内部，
    # 否则回收 worktree 时会连带丢失。按「分支 + 短 sha」命名，事后能回答「那次收尾到底结了什么、漏了什么、清掉了什么」。
    # fail-soft：写不出来只提示，绝不阻断已经成功的合并。
    try {
        # DX-34：强制使用锚点区路径，不在 worktree 内部创建 .sandbox
        $anchorRoot = Get-AnchorWorktree
        if (-not $anchorRoot) {
            throw '无法定位锚点区（git worktree list 没有返回主检出）'
        }
        $receiptRoot = Join-Path (Join-Path $anchorRoot '.sandbox') 'receipts'
        New-Item -ItemType Directory -Path $receiptRoot -Force | Out-Null
        $headSha = (Invoke-Git rev-parse --short HEAD).Trim()
        $closeMode = 'marker'
        if ($closeList.Count -gt 0) { $closeMode = 'explicit' }
        $receipt = [ordered]@{
            finishedAt     = (Get-Date).ToString('s')
            branch         = $Branch
            # DX-116：回执只记「已发生」的事实 —— pushed=false 时 head 置空、mergedInto 不写
            # origin/main，本地合并提交改记 localHead/localBranch（pin 分支），杜绝虚报；
            # dryRun=true 的回执是演练产物，一切字段按演练论。
            dryRun         = [bool]$DryRun
            pushed         = [bool]$mainPushLanded
            mergedInto     = $(if ($mainPushLanded) { "origin/$MainBranch" } else { '' })
            head           = $(if ($mainPushLanded) { $headSha } else { '' })
            localHead      = $headSha
            localBranch    = $(if ($mainPushLanded) { '' } else { $tmp })
            claimed        = @($claimedCodes)
            closed         = @($ccLanded)
            closePending   = @($ccPending)
            closedExplicit = @($explicitClosed | Where-Object { $ccLanded -contains $_ })
            missed         = @($missedCodes)
            closeMode      = $closeMode
        }
        $receiptFile = Join-Path $receiptRoot ('{0}-{1}.json' -f ($Branch -replace '[^A-Za-z0-9]+', '-'), $headSha)
        [System.IO.File]::WriteAllText($receiptFile, ($receipt | ConvertTo-Json -Depth 4),
            (New-Object System.Text.UTF8Encoding($false)))
        Write-Host ("· 回执：{0}" -f $receiptFile) -ForegroundColor DarkGray
    } catch {
        Write-Host ("· 回执写入跳过：{0}" -f $_.Exception.Message) -ForegroundColor DarkGray
    }

    if (-not $mainPushLanded) {
        # DX-78 acc3：连续拒绝 / 离线时交付没有落地，不能以 0 告终 —— 否则调用方
        # （nbflow / 收尾链）会把「没推上去」当成功。DX-116：临时分支已在三步收尾前删除，
        # 合并提交只活在分离 HEAD 上，失败路径上已把它重建 pin 到 $tmp —— 下面的补推
        # 指引才真正可执行；回执 localHead/localBranch 同步留痕。
        Write-Host "`n✗ 推送未落地：合并提交保留在本地分支 $tmp 上，恢复后补推：" -ForegroundColor Red
        Write-Host "   git push origin $tmp`:$MainBranch" -ForegroundColor Yellow
        Write-Host "   推送成功后清理：git branch -d $tmp；挂起的结卡（closePending）由下次合并收尾自动补结" -ForegroundColor Yellow
        exit 1
    }



    # ---- DX-120：自身 worktree 的延后回收 ----
    # 单次推送已落地、结单对账与回执都完成后，才轮到删自己脚下的检出：
    # 三步收尾提交与回执都靠它完成，且 Windows 不允许进程删除 CWD 所在目录。
    # 先把 CWD 让位到锚点区再执行 git worktree remove；本段之后只剩输出，无 git 动作。
    if ($selfWorktreePending -and (Test-Path -LiteralPath $selfWorktreePending)) {
        $cwdHome = Get-AnchorWorktree
        if (-not ($cwdHome -and (Test-Path -LiteralPath $cwdHome))) { $cwdHome = $env:TEMP }
        Set-Location -LiteralPath $cwdHome
        Write-Host ("· 回收当前 worktree {0}（推送已落地，CWD 已让位到 {1}）" -f $selfWorktreePending, $cwdHome) -ForegroundColor DarkGray
        $selfRm = Invoke-GitQuiet @('worktree', 'remove', $selfWorktreePending)
        if ($selfRm.Code -eq 0) {
            Write-Host '✓ 已回收当前 worktree' -ForegroundColor Green
        } else {
            # 不无脑 --force：失败多半是句柄占用（终端 / 应用还站在里面），硬删会留下
            # 半删除尸体（DX-93）。只告警并给出可直接复制的收敛命令。
            Write-Host ("· 未能自动回收当前 worktree：{0}" -f $selfWorktreePending) -ForegroundColor Yellow
            Write-Host ("  手工：git worktree remove {0}" -f $selfWorktreePending) -ForegroundColor Yellow
        }
    }
    Write-Host "`n✓ 合并完成：$Branch → origin/$MainBranch" -ForegroundColor Green
    Write-Host "   查看历史：git log --oneline --graph -10" -ForegroundColor DarkGray
    exit 0
}

# ---- local 模式：传统流程（需要能 checkout main） ----
$localEntryRef = Get-NbSymRef
$localEntryHead = (Invoke-Git rev-parse HEAD).Trim()
try {
    Write-Host "`n· 切换主分支 $MainBranch ..." -ForegroundColor Cyan
    Invoke-Git switch $MainBranch | Out-Null

    Write-Host "· 合并 $Branch（--no-ff）..." -ForegroundColor Cyan
    $localBody = New-MergeCommitBody -Branch $Branch -MainBranch $MainBranch
    Invoke-Git merge --no-ff $Branch -m "merge: 合入 $Branch" -m $localBody | Out-Null
} catch {
    $mergeError = $_.Exception.Message
    # DX-107：local 模式同样不留冲突态、不停在 main 上（详见 Restore-ConflictScene）。
    Restore-ConflictScene -EntryRef $localEntryRef -EntryHead $localEntryHead -TempBranch ''
    Write-Host "`n✗ 合并冲突，现场已回滚（合并已 abort、已切回进入时检出）：" -ForegroundColor Yellow
    Write-Host $mergeError -ForegroundColor Yellow
    Write-Host "`n解决冲突后重跑：pwsh -File scripts/git-done.ps1 -Mode local`n" -ForegroundColor Yellow
    exit 1
}

# 合并收尾：本模式未自动推送，origin/main 尚未含本次合并，认领占位暂不释放
# （其 head 尚未成为 origin/main 的祖先）；推送后由 remote 模式或下次运行释放。
Invoke-ClaimRefCleanup -Main $MainBranch

if (-not $KeepBranch) {
    Write-Host "· 删除分支 $Branch" -ForegroundColor DarkGray
    # DX-109：local 模式的分支同样跟踪 origin/main、合入的却是本地 main —— 与 remote 同一
    # 病根，判据统一换成「已全部进本次合并（这里就是本地合并结果）」。
    $localMergedCommit = (Invoke-Git rev-parse HEAD).Trim()
    if (Remove-MergedBranch -Name $Branch -AnchorCommit $localMergedCommit) {
        Write-Host "✓ 已删除分支 $Branch" -ForegroundColor DarkGray
    }
}

# 开发区占用收尾释放（DX-05）：与 remote 模式同一口径
Release-WorkspaceClaim -Root $repoRoot

if ($claimedCodes.Count -gt 0) {
    # local 模式是遗留通道：它不推送、也不结卡（结卡依赖「已推到 origin/main」这个前提
    # 来判定 claims 失效）。领过卡却得不到对账的话，-Close 在这个模式下会变成静默失效 ——
    # 所以要明说，别让人以为「没报错就是结过了」。
    Write-Host ''
    Write-Host ("· local 模式不做结卡与对账：本次领用的 {0} 张请用 remote 模式收尾，或手动执行：" -f `
        ($claimedCodes -join '、')) -ForegroundColor Yellow
    Write-Host '  python scripts/derive_board_status.py --close-cards' -ForegroundColor Yellow
}

Write-Host "`n✓ 合并完成：$Branch → $MainBranch" -ForegroundColor Green
Write-Host "   查看历史：git log --oneline --graph -10" -ForegroundColor DarkGray
Write-Host "   推送主分支：git push origin $MainBranch`n" -ForegroundColor DarkGray
exit 0
