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
    # 无变化不落盘，绝不产生空提交
    $snapDirty = @((Invoke-Git status --porcelain -- progress.json) | Where-Object { $_ -match 'progress\.json$' })
    if ($snapDirty.Count -eq 0) {
        Write-Host '· 快照：派生结果与现有快照一致，无需提交' -ForegroundColor DarkGray
        return $true
    }
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
    for ($i = 1; $i -le 3; $i++) {
        $snapPush = Invoke-GitQuiet @('push', 'origin', "HEAD:$MainBranch")
        if ($snapPush.Code -eq 0) {
            Write-Host ('✓ 快照：派生状态已同步并推上 {0}' -f $MainBranch) -ForegroundColor Green
            return $true
        }
        if ($i -eq 3) {
            Write-Host '· 警告：快照推送重试 3 次仍失败，本地回滚，留待下次合并' -ForegroundColor Yellow
            Invoke-GitQuiet @('reset', '--hard', 'HEAD~1') | Out-Null
            return $false
        }
        Write-Host ("   · 快照推送被拒（第 {0} 次）：拉取主线变基后重试" -f $i) -ForegroundColor DarkGray
        Invoke-GitQuiet @('fetch', 'origin', $MainBranch) | Out-Null
        $rb = Invoke-GitQuiet @('rebase', "origin/$MainBranch")
        if ($rb.Code -ne 0) {
            # 快照的唯一事实源就是派生结果，重叠提交丢弃即可，绝不手工解冲突
            Invoke-GitQuiet @('rebase', '--abort') | Out-Null
            Invoke-GitQuiet @('reset', '--hard', 'HEAD~1') | Out-Null
            Write-Host '· 警告：快照与主线撞车且无法变基，回滚，留待下次合并' -ForegroundColor Yellow
            return $false
        }
        Start-Sleep -Seconds 5
    }
    return $false
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
    $dirRes = Invoke-GitQuiet @('-C', $Anchor, 'rev-parse', '--path-format=absolute', '--git-common-dir')
    if ($dirRes.Code -ne 0) { return $null }
    $dir = ($dirRes.Output | Select-Object -First 1).ToString().Trim()
    if ($dir -notmatch '^[A-Za-z]:[\\/]' -and $dir -notmatch '^/') { $dir = Join-Path $Anchor $dir }
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
    # 但绝不 stash 那份 progress.json —— 快照的处置权归占用者，否则两边互相把对方的改动暂存掉（docs/pitfalls.md 7.3）。
    $rootClaim = Get-AnchorRootClaim -Anchor $anchor
    if ($rootClaim) {
        $who = [string]$rootClaim.agent
        if (-not $who) { $who = '(未记名)' }
        $task = [string]$rootClaim.task
        Write-Host ("⚠ 锚点同步：root 已被 '{0}' 登记占用{1}，本次只快进、不动 progress.json" -f $who, $(if ($task) { "（$task）" } else { '' })) -ForegroundColor Yellow
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
    if (-not $mergeOk -and $snapshotOnly -and -not $rootClaim) {
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
        $why = '本地提交 / 未跟踪文件冲突'
        if ($rootClaim -and $snapshotOnly) { $why = 'root 被占用（按登记让给占用者处理快照）' }
        Write-Host ("· 锚点同步未完成（{0}），不影响远端合并" -f $why) -ForegroundColor Yellow
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
    $pristineRes = Invoke-GitQuiet @('symbolic-ref', '--quiet', '--short', 'HEAD')
    $pristineBranch = if ($pristineRes.Code -eq 0) { ($pristineRes.Output | Select-Object -First 1).ToString().Trim() } else { '' }
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

    $recycleAgent = ''
    $recycleScope = ''
    # DX-21：分支所在路径本来就能从上面的 `worktree list` 反查，不该再要求调用方记一次参数
    # （忘传 → 目录不删 → 分支被占用删不掉 → release-claims 判分支还在 → 台账留僵尸 claim）。
    # 默认回收 .worktrees/ 下的目标区；-KeepWorktree 显式保留；-RemoveWorktree 退化为
    # 「强制回收」（跳过下面两道安全检查），旧命令照旧能用。
    if ((-not $KeepWorktree) -and $wtPath) {
        $abs = if ([System.IO.Path]::IsPathRooted($wtPath)) { $wtPath } else { Join-Path $repoRoot $wtPath }
        $abs = [System.IO.Path]::GetFullPath($abs)
        $currentRoot = [System.IO.Path]::GetFullPath($repoRoot)
        if ($abs -eq $currentRoot) {
            Write-Host "· 目标 worktree 就是当前目录，跳过回收（请在其它目录执行 git worktree remove）" -ForegroundColor Yellow
        } elseif (Test-Path -LiteralPath $abs) {
            # 安全检查：删掉别人正在办的工作区是事故。两道判据都不成立才回收 ——
            #   1) 还有未推送提交（说明它不是「已合入的尾巴」）；
            #   2) 还有未提交改动（含 untracked），删了就没了。
            # 本次分支刚合并并推送过，它的提交此刻已是 origin/<main> 的祖先，判据自然为 0。
            $unsafe = ''
            if (-not $RemoveWorktree) {
                $ahead = 0
                $aheadRes = Invoke-GitQuiet @('-C', $abs, 'rev-list', '--count', 'HEAD', '--not', '--remotes=origin')
                if ($aheadRes.Code -eq 0) {
                    $first = $aheadRes.Output | Select-Object -First 1
                    if ($first) { [void][int]::TryParse($first.ToString().Trim(), [ref]$ahead) }
                }
                $dirtyNum = 0
                if ($ahead -le 0) {
                    $dirtyRes = Invoke-GitQuiet @('-C', $abs, 'status', '--porcelain')
                    $dirtyNum = @($dirtyRes.Output | Where-Object { $_ -and $_.ToString().Trim() -ne '' }).Count
                }
                if ($ahead -gt 0) { $unsafe = "还有 $ahead 个未推送提交" }
                elseif ($dirtyNum -gt 0) { $unsafe = "还有 $dirtyNum 项未提交改动" }
            }
            if ($unsafe) {
                Write-Host ("· 跳过回收 {0}：{1}（可能是别的工作区在办；确认要删用 -RemoveWorktree）" -f $abs, $unsafe) -ForegroundColor Yellow
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

    if (-not $KeepBranch) {
        Write-Host ("· 删除分支 {0}" -f $Branch) -ForegroundColor DarkGray
        $delRes = Invoke-GitQuiet @('branch', '-d', $Branch)
        $delRes.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
        if ($delRes.Code -ne 0) {
            Write-Host "   （分支可能仍被某个 worktree 占用，先 git worktree remove 再删）" -ForegroundColor DarkGray
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
            # 必须连暂存区一起撤（DX-26）：git checkout -- 只还工作区，add 过的结果会留在
            # 暂存区，被下一次提交顺带夹带 —— P0 收尾时就这样把代码文件带进了 chore(board)。
            $rcCommit.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
            Invoke-GitQuiet @('restore', '--staged', '--worktree', '--', 'docs/board-ledger.json') | Out-Null
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
    $ccRes = Invoke-Cmd -File 'python' -CmdArgs $ccArgs
    $closedCodes = @($ccRes.Output | Where-Object { $_ -and $_ -match '^\S+$' })
    $explicitClosed = @($closedCodes | Where-Object { $closeList -contains $_ })
    if ($ccRes.Code -ne 0) {
        Write-Host ("· 结卡：派生器失败（退出码 {0}），baseline 留待下次合并再结" -f $ccRes.Code) -ForegroundColor Yellow
    } elseif ($closedCodes.Count -gt 0) {
        Invoke-GitQuiet @('add', 'docs/board-ledger.json') | Out-Null
        # DX-41：把 `#<短号>=done` 写进主题，让 .githooks/post-commit 的
        # `grep -Eq '#[A-Za-z][A-Za-z0-9]*'` 认到标记，投 hook.commit 事件到 hooks.db，
        # plan-hook 才会把看板上的对应任务从 doing 挪到 done。
        # 以前只写「结卡 <短号>」（不带 #），钩子判不到标记，六张已交付的卡在看板上
        # 一直挂僵尸 doing（R9-04 / DX-26 / R15-02 / R16-02 / R16-03 / R16-05）。
        $ccMarkers = ($closedCodes | ForEach-Object { "#{$_}=done" }) -join ' '
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
            Invoke-GitQuiet @('restore', '--staged', '--worktree', '--', 'docs/board-ledger.json') | Out-Null
        } else {
            $ccPush = Invoke-GitQuiet @('push', 'origin', "HEAD:$MainBranch")
            if ($ccPush.Code -eq 0) {
                Write-Host ('✓ 结卡：{0} 的 baseline 已补为 done 并推上 {1}' -f ($closedCodes -join '、'), $MainBranch) -ForegroundColor Green
            } else {
                Write-Host '· 结卡：补推失败（离线 / 主线被推进），本地回滚，留待下次合并再结' -ForegroundColor Yellow
                Invoke-GitQuiet @('reset', '--hard', 'HEAD~1') | Out-Null
            }
        }
    }

    # ---- 结单对账（DX-33）----
    # --close-cards 只处理「已经写了 =done 标记」的卡；没写的卡一张都不结，而原先收尾
    # 在这种情况下连一行提示都没有 —— 干完活、合并、全绿，卡还挂在 todo，下次有人接着领
    # 一遍重做。程序判不了 acceptance 满不满足，但「领了没结」是两份名单的差集，能判。
    $missedCodes = @($claimedCodes | Where-Object { $closedCodes -notcontains $_ })
    if ($claimedCodes.Count -gt 0) {
        Write-Host ''
        if ($missedCodes.Count -eq 0) {
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


    # 快照刷新闭环（DX-13）：结卡改的是 baseline，快照是派生产物，必须单独重算一次。
    # 放在结卡之后 —— 先改台账再派生，才能把刚结掉的卡写进快照。
    Sync-BoardSnapshot -RepoRoot $repoRoot -MainBranch $MainBranch | Out-Null

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
        Invoke-GitQuiet @('fetch', 'origin', '--quiet') | Out-Null
        $sw = Invoke-GitQuiet @('switch', $homeBranch)
        if ($sw.Code -eq 0) {
            if ($homeBranch -eq $MainBranch) {
                $ff = Invoke-GitQuiet @('merge', '--ff-only', "origin/$MainBranch")
                if ($ff.Code -eq 0) {
                    Write-Host ("✓ 锚点区已归位 {0} 并快进到 origin/{0}" -f $MainBranch) -ForegroundColor Green
                } else {
                    Write-Host ("· 锚点区已切回 {0}，但快进失败（本地分叉或被未跟踪文件挡住），手工：" -f $MainBranch) -ForegroundColor Yellow
                    Write-Host ("   git -C {0} merge --ff-only origin/{1}" -f $repoRoot, $MainBranch) -ForegroundColor Yellow
                    $ff.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
                }
            } else {
                Write-Host ("· 锚点区已归位分支 {0}（非 {1}，不自动快进）" -f $homeBranch, $MainBranch) -ForegroundColor DarkGray
            }
        } else {
            Write-Host ("· 锚点区归位失败（工作区改动会冲突），请手工：git -C {0} switch {1}" -f $repoRoot, $homeBranch) -ForegroundColor Yellow
            $sw.Output | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
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
            mergedInto     = "origin/$MainBranch"
            head           = $headSha
            claimed        = @($claimedCodes)
            closed         = @($closedCodes)
            closedExplicit = @($explicitClosed)
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
