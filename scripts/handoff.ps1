#Requires -Version 5.1
<#
    DX-149：在办卡原地接手（handoff）

    把「agent 做到一半没后续」的卡交给新 agent：
      删旧 refs/claims/<短号> 占位 → 复用旧 worktree 与分支原地换身份
      → 追加冻结主题的领用提交 → claim-card.py 原子重领 → 生成残局摘要 handoff.json。

    硬约束（DX-149 边界）：
    - 全程非交互（bypass permissions 模式直接调用），不设权限弹窗；
      授权以 -AuthorizedBy 凭证留痕（同 register_board_cards.py 的凭证约定）。
    - 失败模式：缺凭证 / origin 不可达 / 现场缺失 一律非 0 退出且不动服务端占位（Z4）；
      删占位之后任何一步失败，按记录的旧占位 sha 恢复 refs/claims/<短号>（Z13 可重入）。
    - 不删分支、不删 worktree、不 stash、不 checkout --ours/--theirs；
      未提交在制品原样留给接手者。
    - assignments 只经 assignment-store.py（唯一写者）读写，不参与防双领判定。

    用法：
      pwsh -File scripts/handoff.ps1 -Card DX-139 -ToAgent agent-b -AuthorizedBy "维护者 2026-11-08 会话指令"
      pwsh -File scripts/handoff.ps1 ... -DryRun      # 只打印将执行的步骤
      pwsh -File scripts/handoff.ps1 -Selftest       # 自建临时仓库实跑全链路
#>
[CmdletBinding()]
param(
    [string]$Card = '',
    [string]$ToAgent = '',
    [string]$AuthorizedBy = '',
    [string]$Reason = '',
    [switch]$DryRun,
    [switch]$Selftest,
    # 仅 Selftest / 演练用：指向临时 bare 仓库
    [string]$Origin = 'origin'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

# 子进程 stdout 按 UTF-8 解码：git 的中文提交主题默认被 GBK 解成 mojibake，
# 任何「主题 == 冻结文案」的比对都会恒假（selftest 实录，DX-149）。
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

# ---------- 通用工具 ----------
function Find-Python {
    $cmd = Get-Command python -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source -notlike '*WindowsApps*') { return $cmd.Source }
    $local = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'
    if (Test-Path $local) { return $local }
    return 'python'
}

function Invoke-GitQuiet {
    # EAP=Stop 下原生命令的 stderr 会被转成 ErrorRecord 并可能抛 NativeCommandError
    # （git-done.ps1 同款教训），函数内局部降级，不外溢。
    param([string]$Cwd, [string[]]$GitArgs)
    $ErrorActionPreference = 'Continue'
    $out = & git -C $Cwd @GitArgs 2>$null
    return @{ Code = $LASTEXITCODE; Output = @($out) }
}

$script:pyExe = Find-Python

function Invoke-Selftest {
    # 自建临时 git 仓库（bare origin + seed + 旧 agent 的 linked worktree），
    # 实跑一次接手并断言：占位重领 / 未提交改动保留 / 身份与占用换主 /
    # 残局摘要字段齐全 / 闸门放行 / 重跑幂等 / 缺凭证与离线 fail-closed。
    # 依据产物断言（Z8），全程不触真实 origin 与真实台账。
    $ErrorActionPreference = 'Continue'
    $failures = New-Object System.Collections.Generic.List[string]
    $tmp = Join-Path $env:TEMP ("nb-handoff-selftest-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null

    function Assert-ST {
        param([string]$Name, $Ok, [string]$Detail = '')
        if ($Ok) { Write-Host "  ✓ $Name" }
        else {
            Write-Host ("  ✗ {0}{1}" -f $Name, $(if ($Detail) { "（实测：$Detail）" })) -ForegroundColor Red
            # 函数作用域链可读父作用域的 $failures（对象引用，Add 不经赋值）
            $failures.Add($Name)
        }
    }
    function Write-Utf8NoBom {
        param([string]$Path, [string]$Text)
        [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
    }

    try {
        Write-Host "selftest 现场：$tmp" -ForegroundColor DarkCyan
        # ---- 1) bare origin + seed 检出 + 最小台账 ----
        $originGit = Join-Path $tmp 'origin.git'
        $seed = Join-Path $tmp 'seed'
        $null = & git init --bare -q $originGit
        $null = & git clone -q $originGit $seed
        Assert-ST 'seed clone 成功' ((Test-Path $seed) -eq $true)
        & git -C $seed config user.name selftest
        & git -C $seed config user.email selftest@example.com
        Set-Content -LiteralPath (Join-Path $seed 'README.md') -Value 'selftest' -Encoding Ascii
        $docsDir = Join-Path $seed 'docs'
        New-Item -ItemType Directory -Force -Path $docsDir | Out-Null
        # 与真仓一致：.sandbox 不入库（否则残局摘要会被算进未提交改动）
        Set-Content -LiteralPath (Join-Path $seed '.gitignore') -Value '.sandbox/' -Encoding Ascii
        $ledgerJson = '{"baseline": {"DX-999": "todo"}, "cards": {"DX-999": {"acceptance": ["验收A：能跑", "验收B：能停"], "id": "u-selftest", "stage": "DX", "title": "自测卡"}}, "claims": {}}'
        Write-Utf8NoBom (Join-Path $docsDir 'board-ledger.json') $ledgerJson
        $null = & git -C $seed add .
        $null = & git -C $seed commit -q -m 'chore(selftest): 最小台账'
        # clone 空仓库时初始分支随本地默认（master），统一归一到 main 再推
        $null = & git -C $seed branch -M main
        $null = & git -C $seed push -q origin main
        Assert-ST 'seed main 推送成功' ($LASTEXITCODE -eq 0)

        # ---- 2) 复制被测脚本（按仓库约定：脚本按自身路径定位仓库） ----
        $seedScripts = Join-Path $seed 'scripts'
        New-Item -ItemType Directory -Force -Path $seedScripts | Out-Null
        foreach ($f in 'handoff.ps1', 'assignment-store.py', 'claim-card.py', 'nb_paths.py',
                 'agent-workspace.ps1', 'git-helpers.ps1', 'check_claim_refs.py') {
            Copy-Item (Join-Path $PSScriptRoot $f) $seedScripts -Force
        }
        Assert-ST '被测脚本已复制' ((Test-Path (Join-Path $seedScripts 'handoff.ps1')) -eq $true)

        # ---- 3) 模拟旧 agent 现场：linked worktree + 领用占位 + 脏工作树 ----
        $wtA = Join-Path $seed '.worktrees\wt-a'
        $null = & git -C $seed worktree add -q $wtA -b feature/t1
        Assert-ST '旧 agent worktree 建成' ($LASTEXITCODE -eq 0)
        # 逐行拼接（不用 here-string：PS 5.1 对 LF 行尾的 here-string 解析不可靠；
        # 单引号拼接防 $env: 被当环境变量展开）
        $envLines = @(
            '# selftest 模拟 agent-init 产物',
            ('$env:NEONBOARD_DATA_DIR = ''' + $wtA + '\.sandbox'''),
            ('$env:NEONBOARD_INSTANCE = ''wt-a'''),
            ('$env:NEONBOARD_AGENT_ID = ''agent-a'''),
            ('$env:NB_SESSION_ARTIFACT_DIR = ''' + $wtA + '\.sandbox\artifacts''')
        )
        Write-Utf8NoBom (Join-Path $wtA '.agent-env.ps1') ($envLines -join "`r`n")
        $null = & git -C $wtA commit --allow-empty -q -m 'chore(claim): 领取 #DX-999 实施任务'
        $null = & git -C $wtA push -q origin HEAD:refs/claims/DX-999
        $oldSha = (& git -C $wtA ls-remote origin refs/claims/DX-999) -replace '\s.*$', ''
        Assert-ST '旧占位已推（非空 sha）' ($oldSha -match '^[0-9a-f]{40}$') $oldSha
        Set-Content -LiteralPath (Join-Path $wtA 'draft-wip.txt') -Value '半成品内容，必须原样保留' -Encoding UTF8
        # 台账 claims 投影（模拟 agent-init 登记）
        $ledgerPath = Join-Path $docsDir 'board-ledger.json'
        $ledger = Get-Content -LiteralPath $ledgerPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $claimEntry = [pscustomobject]@{ agent = 'agent-a'; branch = 'feature/t1'; head = ''; claimedAt = 0 }
        $ledger | Add-Member -Force -MemberType NoteProperty -Name claims -Value ([pscustomobject]@{ })
        $ledger.claims | Add-Member -Force -MemberType NoteProperty -Name 'DX-999' -Value $claimEntry
        Write-Utf8NoBom $ledgerPath (ConvertTo-Json $ledger -Depth 8)
        $null = & git -C $seed add .
        $null = & git -C $seed commit -q -m 'chore(board): 模拟旧 agent 领用登记'
        $null = & git -C $seed push -q origin main
        # assignment 现场（唯一写者）
        $null = & $script:pyExe (Join-Path $seedScripts 'assignment-store.py') upsert `
            --code DX-999 --agent agent-a --branch feature/t1 --worktree '.worktrees/wt-a' `
            --stage '编码中' --next-step '旧 agent 留下的交接话'
        Assert-ST '旧 assignment 登记成功' ($LASTEXITCODE -eq 0)

        # ---- 4) 实跑接手 ----
        & pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $seedScripts 'handoff.ps1') `
            -Card DX-999 -ToAgent agent-b -AuthorizedBy 'selftest 授权'
        $rc = $LASTEXITCODE
        Assert-ST '接手命令 exit 0' ($rc -eq 0) "exit=$rc"

        # ---- 5) 依据产物断言（Z8） ----
        Assert-ST '未提交在制品原样保留' ((Test-Path (Join-Path $wtA 'draft-wip.txt')) -eq $true)
        $envText = [System.IO.File]::ReadAllText((Join-Path $wtA '.agent-env.ps1'), [System.Text.Encoding]::UTF8)
        Assert-ST '.agent-env.ps1 身份已换 agent-b' (($envText -match "NEONBOARD_AGENT_ID\s*=\s*'agent-b'") -eq $true)
        $wsReg = Get-Content (Join-Path $seed '.git\agent-workspace.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        Assert-ST '占用登记已换主 agent-b' ($wsReg.'worktree:wt-a'.agent -eq 'agent-b') ($wsReg | ConvertTo-Json -Depth 3)
        $newSha = ((& git -C $wtA ls-remote origin refs/claims/DX-999) -replace '\s.*$', '')
        Assert-ST '占位已重领且 sha 变化' (($newSha -match '^[0-9a-f]{40}$') -and ($newSha -ne $oldSha)) "$oldSha -> $newSha"
        $newSubject = (& git -C $wtA log -1 --format='%s' $newSha)
        Assert-ST '新占位指向冻结主题领用提交' ($newSubject -eq 'chore(claim): 领取 #DX-999 实施任务') $newSubject
        $null = & git -C $wtA merge-base --is-ancestor $newSha HEAD
        Assert-ST '新占位是分支 HEAD 祖先' ($LASTEXITCODE -eq 0)
        $handoffPath = Join-Path $wtA '.sandbox\artifacts\handoff.json'
        Assert-ST '残局摘要已生成' ((Test-Path $handoffPath) -eq $true)
        $ho = Get-Content $handoffPath -Raw -Encoding UTF8 | ConvertFrom-Json
        Assert-ST '摘要：接手双方记录' (($ho.previousAgent -eq 'agent-a') -and ($ho.agent -eq 'agent-b')) ($ho | ConvertTo-Json -Depth 2)
        Assert-ST '摘要：acceptance 清单带入' (@($ho.cardInfo.acceptance).Count -eq 2) (@($ho.cardInfo.acceptance).Count)
        Assert-ST '摘要：未提交清单含 draft-wip.txt' ((@($ho.uncommitted) -join "`n") -match 'draft-wip\.txt') (@($ho.uncommitted) -join ';')
        Assert-ST '摘要：最近提交非空' (@($ho.recentCommits).Count -ge 1) (@($ho.recentCommits).Count)
        Assert-ST '摘要：nextStep 非空（旧话术保留）' ([string]::IsNullOrEmpty($ho.nextStep) -eq $false) $ho.nextStep
        $asgNow = (& $script:pyExe (Join-Path $seedScripts 'assignment-store.py') list --json | Out-String | ConvertFrom-Json)
        Assert-ST 'assignment：agent 换新且 handoffFrom 留痕' `
            (($asgNow.assignments.'DX-999'.agent -eq 'agent-b') -and ($asgNow.assignments.'DX-999'.handoffFrom -eq 'agent-a')) `
            ($asgNow.assignments.'DX-999' | ConvertTo-Json)

        # ---- 6) 领用闸门放行（真实闸门脚本实跑；先按其文档要求 fetch claim refs 镜像） ----
        $null = & git -C $seed fetch -q origin '+refs/claims/*:refs/claims/*'
        Push-Location $wtA
        & $script:pyExe (Join-Path $seedScripts 'check_claim_refs.py') --base origin/main --head feature/t1
        Assert-ST 'check_claim_refs 闸门放行' ($LASTEXITCODE -eq 0) "exit=$LASTEXITCODE"
        Pop-Location

        # ---- 7) 幂等重跑：不重复删 ref、不重复领用提交 ----
        & pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $seedScripts 'handoff.ps1') `
            -Card DX-999 -ToAgent agent-b -AuthorizedBy 'selftest 授权'
        Assert-ST '重跑 exit 0（幂等收敛）' ($LASTEXITCODE -eq 0) "exit=$LASTEXITCODE"
        $shaAfter = ((& git -C $wtA ls-remote origin refs/claims/DX-999) -replace '\s.*$', '')
        Assert-ST '重跑后占位 sha 不变' ($shaAfter -eq $newSha) "$newSha -> $shaAfter"
        # 旧领用 1 条 + 接手领用 1 条 = 2（幂等修复后重跑不再追加）。
        # 用 rev-list 纯计数：git 输出的中文主题经 GBK 解码后字符串比对恒假（selftest 实录）。
        $claimCount = (& git -C $wtA rev-list --count origin/main..feature/t1)
        Assert-ST '重跑后分支新增提交仍 2 条（不重复追加领用）' ($claimCount -eq 2) "count=$claimCount"

        # ---- 8) fail-closed：缺凭证 / origin 不可达 ----
        & pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $seedScripts 'handoff.ps1') `
            -Card DX-999 -ToAgent agent-c
        Assert-ST '缺 -AuthorizedBy → exit 2' ($LASTEXITCODE -eq 2) "exit=$LASTEXITCODE"
        & pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $seedScripts 'handoff.ps1') `
            -Card DX-999 -ToAgent agent-c -AuthorizedBy 'x' -Origin (Join-Path $tmp 'no-such-origin.git')
        Assert-ST 'origin 不可达 → exit 5' ($LASTEXITCODE -eq 5) "exit=$LASTEXITCODE"
    }
    catch {
        Write-Host "✗ selftest 异常：$($_.Exception.Message)" -ForegroundColor Red
        $failures.Add("exception: $($_.Exception.Message)")
    }
    finally {
        Pop-Location -ErrorAction SilentlyContinue
        if ($failures.Count -gt 0) {
            Write-Host "（失败现场保留：$tmp）" -ForegroundColor Yellow
        } else {
            Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    if ($failures.Count -gt 0) {
        foreach ($f in $failures) { Write-Host "  - 失败：$f" -ForegroundColor Red }
        Write-Host ("handoff selftest 失败 {0} 例" -f $failures.Count) -ForegroundColor Red
        $script:selftestResult = 1
        return
    }
    Write-Host '✓ handoff selftest 全部通过' -ForegroundColor Green
    $script:selftestResult = 0
}

function Read-Assignment {
    param([string]$Code)
    $store = Join-Path $PSScriptRoot 'assignment-store.py'
    if (-not (Test-Path $store)) { return $null }
    $ErrorActionPreference = 'Continue'
    $res = & $script:pyExe $store list --json 2>$null
    $rc = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($rc -ne 0) { return $null }
    try {
        $data = ($res -join "`n") | ConvertFrom-Json
        $entry = $data.assignments.PSObject.Properties[$Code]
        if ($entry) { return $entry.Value }
    } catch { }
    return $null
}

function Get-LedgerJson {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try {
        return (Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json)
    } catch { return $null }
}

# ---------- Selftest：自建临时仓库实跑全链路 ----------
# 内嵌实现（不另立脚本文件：scope 只放行 scripts/handoff.ps1，DX-149 边界）。
if ($Selftest) {
    $script:selftestResult = 0
    Invoke-Selftest
    exit $script:selftestResult
}

# ---------- 参数校验（fail-closed：缺凭证直接拒，不动任何东西） ----------
if (-not $Card -or -not $ToAgent -or -not $AuthorizedBy) {
    Write-Host "✗ 用法：handoff.ps1 -Card <短号> -ToAgent <新agent名> -AuthorizedBy <授权来源>（必填）" -ForegroundColor Red
    exit 2
}
$code = $Card.TrimStart('#').ToUpperInvariant()

# ---------- origin 可达性（离线 fail-closed，DX-149 边界） ----------
$reach = Invoke-GitQuiet -Cwd $repoRoot -GitArgs @('ls-remote', $Origin, 'HEAD')
if ($reach.Code -ne 0) {
    Write-Host "✗ origin 不可达（离线？），接手需要联网确认服务端占位，已中止（Z4 fail-closed）" -ForegroundColor Red
    exit 5
}

# ---------- 服务端占位现状 ----------
$claimRef = "refs/claims/$code"
$claimRes = Invoke-GitQuiet -Cwd $repoRoot -GitArgs @('ls-remote', $Origin, $claimRef)
$oldClaimSha = $null
foreach ($line in $claimRes.Output) {
    if ($line -match "^([0-9a-f]{40})\s+$([regex]::Escape($claimRef))") { $oldClaimSha = $Matches[1] }
}
if (-not $oldClaimSha) {
    Write-Host "✗ 服务端没有 $claimRef，无占位可接手（卡可能已完成或从未领用）" -ForegroundColor Red
    exit 2
}
Write-Host "→ 接手 $code：旧占位 $claimRef @ $($oldClaimSha.Substring(0,7))" -ForegroundColor Cyan

# ---------- 定位现场（assignments 优先，台账 claims 回退） ----------
$asg = Read-Assignment -Code $code
$ledger = Get-LedgerJson -Path (Join-Path $repoRoot 'docs\board-ledger.json')
$proj = $null
if ($ledger -and $ledger.claims) {
    $prop = $ledger.claims.PSObject.Properties[$code]
    if ($prop) { $proj = $prop.Value }
}
$oldAgent = if ($asg -and $asg.agent) { $asg.agent } elseif ($proj -and $proj.agent) { $proj.agent } else { '' }
$branch = if ($asg -and $asg.branch) { $asg.branch } elseif ($proj -and $proj.branch) { $proj.branch } else { '' }
$oldNextStep = if ($asg -and $asg.nextStep) { [string]$asg.nextStep } else { '' }
if (-not $branch) {
    Write-Host "✗ 无法定位 $code 的认领分支（assignments 与台账 claims 均无）" -ForegroundColor Red
    exit 2
}
$branchCheck = Invoke-GitQuiet -Cwd $repoRoot -GitArgs @('show-ref', '--verify', '--quiet', "refs/heads/$branch")
if ($branchCheck.Code -ne 0) {
    Write-Host "✗ 认领分支 $branch 不在本地（orphan 残留应走占位清理，不是接手）" -ForegroundColor Red
    exit 2
}

# ---------- 找旧 worktree（拍板项 1：复用，不新建） ----------
$wtPath = $null
if ($asg -and $asg.worktree) {
    $candidate = $asg.worktree
    if (-not [System.IO.Path]::IsPathRooted($candidate)) { $candidate = Join-Path $repoRoot $candidate }
    if (Test-Path (Join-Path $candidate '.agent-env.ps1')) { $wtPath = $candidate }
}
if (-not $wtPath) {
    $worktreesRoot = Join-Path $repoRoot '.worktrees'
    if (Test-Path $worktreesRoot) {
        foreach ($dir in (Get-ChildItem -LiteralPath $worktreesRoot -Directory)) {
            $envFile = Join-Path $dir.FullName '.agent-env.ps1'
            if (-not (Test-Path $envFile)) { continue }
            $envText = [System.IO.File]::ReadAllText($envFile, [System.Text.Encoding]::UTF8)
            # 单引号模式串（同换身份步骤：双引号转义后 $ 变行尾锚，永不匹配）
            $m = [regex]::Match($envText, '(?m)^\s*\$env:NEONBOARD_AGENT_ID\s*=\s*''([^'']*)''')
            if ($m.Success -and $m.Groups[1].Value -eq $oldAgent) { $wtPath = $dir.FullName; break }
        }
    }
}
if (-not $wtPath) {
    Write-Host "✗ 找不到 $oldAgent 的旧 worktree（.agent-env.ps1 缺失）——拍板项 1 要求复用旧区，请先人工找回现场" -ForegroundColor Red
    Write-Host "  已尝试：assignment.worktree=$($asg -and $asg.worktree) → $(if ($asg -and $asg.worktree) { Join-Path $repoRoot $asg.worktree })；fallback 扫 $(Join-Path $repoRoot '.worktrees')/*/ 的 NEONBOARD_AGENT_ID=$oldAgent" -ForegroundColor DarkGray
    exit 2
}
$wtLeaf = Split-Path -Leaf $wtPath
Write-Host "  现场：$oldAgent @ $wtPath（分支 $branch）" -ForegroundColor DarkGray

# ---------- 快照现场事实（Z15：动第一下之前先记录） ----------
$oldHead = (Invoke-GitQuiet -Cwd $wtPath -GitArgs @('rev-parse', 'HEAD')).Output[0]
if (-not $oldHead) {
    Write-Host "✗ 读不到 $wtPath 的 HEAD（现场异常），中止" -ForegroundColor Red
    exit 2
}
$ErrorActionPreference = 'Continue'
$statusRaw = @(& git -C $wtPath status --porcelain 2>$null)
$ErrorActionPreference = 'Stop'
$statusOut = @($statusRaw) | Where-Object { $_ }

# ---------- 残局摘要（主流程与幂等路径共用，不复制两份） ----------
function New-HandoffArtifact {
    param([string]$Code, [string]$Branch, [string]$WtPath, [string]$WtLeaf,
          [string]$OldAgent, [string]$NewAgent, [string]$AuthorizedBy,
          [string]$OldNextStep, $StatusOut, $Ledger)
    $artifactDir = Join-Path $WtPath '.sandbox\artifacts'
    New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
    $ErrorActionPreference = 'Continue'
    $diffRaw = @(& git -C $WtPath diff --name-only 'origin/main...HEAD' 2>$null)
    $recentRaw = @(& git -C $WtPath log -5 --format='%h %s' 2>$null)
    $ErrorActionPreference = 'Stop'
    $diffFiles = @($diffRaw) | Where-Object { $_ }
    $recent = @($recentRaw) | Where-Object { $_ }
    $acceptance = @()
    if ($Ledger -and $Ledger.cards) {
        $cardProp = $Ledger.cards.PSObject.Properties[$Code]
        if ($cardProp -and $cardProp.Value.acceptance) { $acceptance = @($cardProp.Value.acceptance) }
    }
    if (-not $OldNextStep) { $OldNextStep = '旧 agent 未留 nextStep；以 acceptance 清单核实真实进度' }
    $handoff = [ordered]@{
        card          = $Code
        branch        = $Branch
        worktree      = "worktree:$WtLeaf"
        previousAgent = $OldAgent
        agent         = $NewAgent
        sessionId     = ''
        createdAt     = (Get-Date).ToString('o')
        authorizedBy  = $AuthorizedBy
        nextStep      = $OldNextStep
        cardInfo      = @{ acceptance = $acceptance }
        diffFiles     = $diffFiles
        recentCommits = $recent
        uncommitted   = $StatusOut
    }
    $handoffPath = Join-Path $artifactDir 'handoff.json'
    [System.IO.File]::WriteAllText($handoffPath,
        ($handoff | ConvertTo-Json -Depth 6), (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "✓ 残局摘要已生成 $handoffPath" -ForegroundColor Green
    # assignment 补接手痕迹（claim-card 已 upsert agent/branch，这里补 handoffFrom 与 nextStep）
    $storePy = Join-Path $PSScriptRoot 'assignment-store.py'
    $null = & $script:pyExe $storePy upsert --code $Code --agent $NewAgent --branch $Branch `
        --worktree ".worktrees/$WtLeaf" --handoff-from $OldAgent `
        --next-step "已由 $NewAgent 接手（自 $OldAgent）；先逐条跑 acceptance 核实现场，再继续开发" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "· assignment 接手痕迹登记失败（旁路，不阻断）" -ForegroundColor Yellow
    }
}

# ---------- 幂等短路（Z13）：重跑收敛，不重复删占位、不重复领用提交 ----------
# 判据全 ASCII（占位是否 HEAD 祖先 + assignment 已换主）——中文提交主题经
# [Console]OutputEncoding（GBK）解码后比对恒假（selftest 实录），不可作判据。
$asgNow = Read-Assignment -Code $code
$alreadyHanded = $false
if ($oldClaimSha -and $oldHead) {
    $null = Invoke-GitQuiet -Cwd $wtPath -GitArgs @('merge-base', '--is-ancestor', $oldClaimSha, $oldHead)
    if ($LASTEXITCODE -eq 0 -and $asgNow -and $asgNow.agent -eq $ToAgent -and [string]$asgNow.handoffFrom -ne '') {
        $alreadyHanded = $true
    }
}
if ($alreadyHanded) {
    Write-Host "· $code 已由 $ToAgent 接手（占位是分支 HEAD 祖先且 assignment 已换主），幂等收敛：仅刷新残局摘要" -ForegroundColor DarkGray
    # 接手痕迹不覆盖：previousAgent 用 assignment 里的 handoffFrom（重跑时 $oldAgent
    # 已从 assignment 读出为新身份，直接传会把 handoffFrom 改成自己——selftest 实录）
    $prevAgent = if ($asgNow.handoffFrom) { [string]$asgNow.handoffFrom } else { $oldAgent }
    New-HandoffArtifact -Code $code -Branch $branch -WtPath $wtPath -WtLeaf $wtLeaf `
        -OldAgent $prevAgent -NewAgent $ToAgent -AuthorizedBy $AuthorizedBy `
        -OldNextStep $oldNextStep -StatusOut $statusOut -Ledger $ledger
    exit 0
}

# ---------- DryRun ----------
if ($DryRun) {
    Write-Host ""
    Write-Host "[dry-run] 将执行（本次什么都没动）：" -ForegroundColor DarkCyan
    Write-Host "  1. git push $Origin --delete $claimRef（删旧占位，旧 sha $oldClaimSha 留作回滚）"
    Write-Host "  2. 改写 $wtPath\.agent-env.ps1 的 NEONBOARD_AGENT_ID → $ToAgent"
    Write-Host "  3. agent-workspace：Release worktree:$wtLeaf（$oldAgent）→ Claim worktree:$wtLeaf（$ToAgent）"
    Write-Host "  4. 在 $branch 追加领用提交 chore(claim): 领取 #$code 实施任务（body 记「接手自 $oldAgent」+ 授权）"
    Write-Host "  5. claim-card.py 原子重领 $claimRef"
    Write-Host "  6. 生成 $($wtPath)\.sandbox\artifacts\handoff.json（残局摘要）"
    Write-Host "  7. assignment-store upsert handoffFrom=$oldAgent"
    Write-Host "  未提交改动 $($statusOut.Count) 项将原样保留：不 stash、不提交、不丢弃。"
    if ($oldNextStep) { Write-Host "  旧 nextStep：$oldNextStep" }
    exit 0
}

# ---------- 实跑（c 步之后任一步失败 → 按旧 sha 恢复占位并回滚领用提交） ----------
function Restore-Claim {
    param([string]$Sha)
    if ($Sha) {
        $null = Invoke-GitQuiet -Cwd $repoRoot -GitArgs @('push', $Origin, "${Sha}:${claimRef}")
        Write-Host "  ↩ 已按旧 sha 恢复 $claimRef @ $($Sha.Substring(0,7))" -ForegroundColor Yellow
    }
}

try {
    # 1) 删旧占位（本地镜像若有也清）
    $del = Invoke-GitQuiet -Cwd $repoRoot -GitArgs @('push', $Origin, '--delete', $claimRef)
    if ($del.Code -ne 0) {
        Write-Host "✗ 删服务端占位失败（可能已被并发释放/接管），中止且未改任何本地状态" -ForegroundColor Red
        exit 3
    }
    $null = Invoke-GitQuiet -Cwd $repoRoot -GitArgs @('update-ref', '-d', $claimRef)
    Write-Host "✓ 旧占位已释放 $claimRef" -ForegroundColor Green

    # 2) 原地换身份（只换 agent id；目录与数据路径不变）。
    #    模式串用单引号：双引号里 `$env 的反引号转义后 .NET 收到裸 $（行尾锚），永不匹配
    #    （本仓 Get-AgentEnvId 的单引号写法是正解，selftest 实录 DX-149）。
    $envFile = Join-Path $wtPath '.agent-env.ps1'
    $envText = [System.IO.File]::ReadAllText($envFile, [System.Text.Encoding]::UTF8)
    $agentLinePattern = '(?m)^(\s*\$env:NEONBOARD_AGENT_ID\s*=\s*)''[^'']*'''
    if ([regex]::IsMatch($envText, $agentLinePattern)) {
        $newEnvText = [regex]::Replace($envText, $agentLinePattern, ('$1''' + $ToAgent + "'"))
    } else {
        # 缺该行时追加（老版本 agent-init 或异构环境），保证身份一定落上
        $newEnvText = $envText.TrimEnd() + "`r`n" + ('$env:NEONBOARD_AGENT_ID = ''' + $ToAgent + "'") + "`r`n"
    }
    [System.IO.File]::WriteAllText($envFile, $newEnvText, (New-Object System.Text.UTF8Encoding($false)))
    if (([System.IO.File]::ReadAllText($envFile, [System.Text.Encoding]::UTF8) -match [regex]::Escape($ToAgent))) {
        Write-Host "✓ .agent-env.ps1 身份已换：$oldAgent → $ToAgent" -ForegroundColor Green
    } else {
        throw "换身份失败：写回后读不到 $ToAgent（$envFile）"
    }

    # 3) 占用登记换主（Release 旧登记缺失时只提示，不阻断）。
    #    agent-workspace.ps1 按【调用方 cwd】定位 common dir（Get-Location），
    #    必须切到 worktree 里再调，否则会把占用登记写到别的检出去（selftest 实录）。
    $wsScript = Join-Path $PSScriptRoot 'agent-workspace.ps1'
    $pwshExe = (Get-Command pwsh -ErrorAction SilentlyContinue).Source
    if (-not $pwshExe) { $pwshExe = 'powershell' }
    Push-Location $wtPath
    try {
        $ErrorActionPreference = 'Continue'
        $null = & $pwshExe -NoProfile -ExecutionPolicy Bypass -File $wsScript -Action Release -Agent $oldAgent -Scope "worktree:$wtLeaf" 2>$null
        $claimOut = & $pwshExe -NoProfile -ExecutionPolicy Bypass -File $wsScript -Action Claim -Agent $ToAgent -Scope "worktree:$wtLeaf" -Task $code 2>&1
        $wsCode = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
    } finally {
        Pop-Location
    }
    if ($wsCode -ne 0) {
        throw "占用登记失败：$($claimOut -join ' ')"
    }
    Write-Host "✓ 占用登记 worktree:$wtLeaf → $ToAgent" -ForegroundColor Green

    # 4) 追加接手领用提交（主题冻结，body 记「接手自」+ 授权）。
    #    不能按主题判重跳过：旧 agent 的领用提交与它同主题（冻结语法的必然），
    #    跳过会让重领占位指回旧提交、sha 不变（selftest 实录）。幂等由本函数
    #    开头的短路（占位是 HEAD 祖先 + assignment 已换主）保证，不在这里做。
    $claimSubject = "chore(claim): 领取 #$code 实施任务"
    $body = "接手自 $oldAgent`r`n授权：$AuthorizedBy`r`n批次：$code"
    if ($Reason) { $body += "`r`n说明：$Reason" }
    $ErrorActionPreference = 'Continue'
    $null = & git -C $wtPath commit --allow-empty -m $claimSubject -m $body
    $commitCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($commitCode -ne 0) { throw "领用提交失败（exit $commitCode）" }
    Write-Host "✓ 已追加接手领用提交（接手自 $oldAgent）" -ForegroundColor Green

    # 5) 原子重领（claim-card：非强推占位 + 台账 claims + assignment upsert）
    $storePy = Join-Path $PSScriptRoot 'assignment-store.py'
    $null = & $script:pyExe (Join-Path $PSScriptRoot 'claim-card.py') $wtPath $code $ToAgent $branch
    if ($LASTEXITCODE -ne 0) { throw "claim-card 重领失败（占位可能被并发抢占）" }
    Write-Host "✓ 服务端占位已重领 $claimRef" -ForegroundColor Green

    # 6) 残局摘要 handoff.json（复用 nbflow 的 .sandbox/artifacts 约定）
    New-HandoffArtifact -Code $code -Branch $branch -WtPath $wtPath -WtLeaf $wtLeaf `
        -OldAgent $oldAgent -NewAgent $ToAgent -AuthorizedBy $AuthorizedBy `
        -OldNextStep $oldNextStep -StatusOut $statusOut -Ledger $ledger

    Write-Host ""
    Write-Host ("✅ 接手完成：{0} 已由 {1} 接管（占位 {2} → {3}）" -f `
        $code, $ToAgent, $oldClaimSha.Substring(0, 7),
        (Invoke-GitQuiet -Cwd $wtPath -GitArgs @('rev-parse', 'HEAD')).Output[0].Substring(0, 7)) -ForegroundColor Green
    Write-Host "  授权来源：$AuthorizedBy" -ForegroundColor DarkGray
    Write-Host "  下一步（接手协议第 5 步）：先逐条跑卡 acceptance 核实现场，别信 stage 字面值；" -ForegroundColor Yellow
    Write-Host "  未提交改动 $($statusOut.Count) 项原样保留，读 handoff.json 了解残局。"
    exit 0
}
catch {
    Write-Host "✗ 接手失败：$($_.Exception.Message)" -ForegroundColor Red
    if ($oldClaimSha) { Restore-Claim -Sha $oldClaimSha }
    # 领用提交若已追加，回滚到旧分支头。必须用 --mixed（不动工作树）——
    # --hard 会把接手者的未提交在制品一并吃掉，违反拍板项 1（Z4 体验类也不得静默丢数据）。
    $ErrorActionPreference = 'Continue'
    $curHead = (& git -C $wtPath rev-parse HEAD 2>$null)
    $ErrorActionPreference = 'Stop'
    if ($curHead -and $curHead -ne $oldHead) {
        $null = Invoke-GitQuiet -Cwd $wtPath -GitArgs @('reset', '--mixed', $oldHead)
        Write-Host "  ↩ 已回滚接手提交，分支头还原 $($oldHead.Substring(0,7))（工作树文件原样保留）" -ForegroundColor Yellow
    }
    exit 1
}
