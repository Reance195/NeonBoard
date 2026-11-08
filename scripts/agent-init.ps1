<#
    为某个 agent 创建独立工作区

    产出：
      <repo>/.worktrees/<agent>/          —— 独立 worktree（独立分支、独立 build/dist）
      <repo>/.worktrees/<agent>/.sandbox/  —— 沙箱数据目录（数据库/插件/日志/hooks.db）
      <repo>/.worktrees/<agent>/.agent-env.ps1 —— 可 dot-source 的沙箱环境变量脚本

    关键点：基线用 origin/main 而不是 main —— main 分支已被根目录 checkout，
    Git 不允许同一分支在两个 worktree 同时检出，用远端引用建分支才能完全绕开根目录。

    原地模式（-InPlace / 自动检测）：
      WorkBuddy 的 Worktree Task、或手工 git worktree add，本身已经给了一个隔离检出。
      在里面再开一层 .worktrees/<agent> 属于俄罗斯套娃，改动路径会整体错位一层，
      所以检测到「脚本所属检出本身已是 linked worktree」时自动转为原地初始化：
      复用当前检出，只补 .sandbox / .agent-env.ps1 / 领用登记，不再嵌套开区。

    锚点区不是开发区（DX-05）：
      锚点区（仓库根目录）的 git-dir 等于 common dir。在那里传 -InPlace 会被直接拒绝
      （exit 3），因为它会让两个会话共享 HEAD 与 .sandbox。确需在锚点动手才加 -ForceAnchor，
      脚本会自动登记 root 占用，让后来者撞到的是明确的报错而不是静默覆盖。
      开发区（.worktrees/<agent>）开区即自动登记 worktree:<agent> 占用，git-done 收尾释放。

    用法：
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch fix/stagebar-chip-overflow -Card R3-01
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch feature/xxx -Base origin/main -Card M19
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Branch chore/xxx -Card none
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Card R3-01 -InPlace    # 已在 worktree 里，原地初始化
      pwsh -File scripts/agent-init.ps1 -Agent agent-c -Card R3-01 -InPlace -ForceAnchor   # 确需在锚点区动手
        # -Card 必填：领用期间该卡在台账 claims 里占位，看板卡自动转「进行中」。
        # 大任务（R#-#）可随时领；小任务（M##）仅当其父任务尚未被任何人认领时才可单独领。
        # 分支头脱离主干的期间内该卡为 doing（派生器按 head 位置判定），
        # 分支合并或丢弃后登记自动失效，收尾用提交标记 #<code>=done。
        # 确实不需要领用任何卡片时显式写 -Card none —— 强制表态，避免漏填导致双领。
        # 硬门：领用时会向服务端推 refs/claims/<code> 占位（非强推），
        # 同名 ref 已存在即被拒绝；领子卡前先 ls-remote 父卡占位。需联网。

    出口码：2 = 分支名不合规（入口即拦，未创建任何目录）；3 = 锚点 / 占用冲突（含领用被拒，
            回滚后退出）；4 = 残留工作区或分支已存在（只诊断，不自动删）；
            5 = 调用方检出与脚本所属检出不一致（DX-49 漂移守卫，-AllowCrossTree 显式放行）；
            6 = 网络预检失败（DX-66，此时未创建任何产物）；
            7 = 卡片已按弃单结案（baseline=abandoned，DX-111），或弃单清单核对失败
                （fail-closed，此时未创建任何产物）。

    开区事务（DX-66）：从第一个写动作（worktree add / 占用登记 / 领用提交 / 服务端占位）起
    跟踪本次创建的产物，任一步失败按清单自动回滚并逐项打印 —— 失败即无残留，重跑就是干净起点。
    网络预检与领用前置探针排在所有写动作之前：网络故障 / 卡已被占在第一行报错，不再半途留现场
    （2026-11-06 领 DX-69 被抢占实测：worktree / 分支 / 占用登记三处残留全靠手工清）。
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Agent,
    [string]$Branch = '',
    [string]$Base = 'origin/main',
    [Parameter(Mandatory = $true)][string]$Card = '',
    [switch]$NoFetch,
    # 已在独立 worktree 里时用：不再嵌套开区（WorkBuddy Worktree Task 场景）
    [switch]$InPlace,
    # 锚点区豁免（DX-05）：在锚点区传 -InPlace 默认直接报错；确需在锚点动手才加它
    [switch]$ForceAnchor,
    # 强制嵌套：明知道当前是 linked worktree 也要再开一层子工作区（极少用）
    [switch]$ForceNested,
    # 检出漂移守卫（DX-49）：调用方 CWD 与脚本所属检出是同一仓库的不同工作树时默认中止；
    # 有意的跨区调用（复现旧脚本行为 / 跨区验证）用它显式放行并打显著告警
    [switch]$AllowCrossTree
)

$ErrorActionPreference = 'Stop'

# git 符号引用取值走单一封装（DX-63）：空输出 / 多行 / 分离 HEAD 的空值语义只写一遍，
# 裸 `[string](& git ...)` 模式已被 check_script_hygiene 规则 E 禁止。
. (Join-Path $PSScriptRoot 'git-helpers.ps1')

# 控制台 UTF-8 编码唯一口径（DX-77/DX-83）：开区捕获 python 子进程（claim-card 等）的
# UTF-8 输出不再按 GBK 解码转发（§2.6/§2.20 同类的收口）
. (Join-Path $PSScriptRoot 'nb-console.ps1')
Set-NbUtf8Console

# 仓库根按**脚本自身路径**定位：新工作区永远开在「脚本所属仓库」下，
# 避免在别的目录里误开 worktree
$scriptRepoRoot = $null
if ($PSScriptRoot) {
    $candidate = Split-Path -Parent $PSScriptRoot
    if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate '.git'))) {
        $scriptRepoRoot = (Resolve-Path -LiteralPath $candidate).Path
    }
}
if ($scriptRepoRoot) {
    $repoRoot = $scriptRepoRoot
} else {
    $repoRoot = Get-NbToplevel
    if (-not $repoRoot) { throw '当前目录不是 Git 仓库' }
}

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
Test-CheckoutDrift -ScriptTree $scriptRepoRoot -Allowed:$AllowCrossTree

# ---------- 钩子：确保强制条款真的生效 ----------
# AGENTS.md 的每一条强制都挂在 .githooks 上，而 core.hooksPath 是 clone 级配置、
# 不随 clone 传递。漏配一次，分支保护、提交格式、任务闸门就全部静默失效，
# 所以每次开区都固化一遍（幂等，写的是所有 worktree 共享的那份 config）。
$setupScript = Join-Path $PSScriptRoot 'setup-git-hooks.ps1'
if (Test-Path -LiteralPath $setupScript) {
    $pwshCmd = Get-Command pwsh -ErrorAction SilentlyContinue
    if ($pwshCmd) {
        & $pwshCmd.Source -NoProfile -ExecutionPolicy Bypass -File $setupScript
    } else {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $setupScript
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host '  钩子安装未完成，请手动执行： pwsh -File scripts/setup-git-hooks.ps1' -ForegroundColor Yellow
    }
}

# ---------- 残留诊断：删不删由人决定，但得先给得起决定的材料（DX-12）----------
# 原先这里只 throw 一句「如需重建请先 git worktree remove」。人拿到这句话没法判断那个残留里
# 有没有未提交改动、有没有还没推的提交 —— 而 §7.10 明令「删不删由人决定」，缺信息就等于
# 逼人闭眼删。函数只做诊断与命令提示，**绝不自动删除**。
function Get-CommitCount {
    param([string]$Path, [string[]]$RevArgs)

    # 注意传数组：把 'HEAD --not --remotes=origin' 整串当一个参数递过去，git 会当成
    # 一个不存在的版本号直接失败，计数恒为 0 —— 这里必须逐个参数展开（DX-12 实测踩过）。
    $raw = & git -C $Path rev-list --count @RevArgs 2>$null | Select-Object -First 1
    $count = 0
    if ($raw) { try { $count = [int]([string]$raw).Trim() } catch { $count = 0 } }
    return $count
}

function Show-StaleWorktree {
    param([string]$Path)

    Write-Host ("✗ 工作区已存在：{0}" -f $Path) -ForegroundColor Red
    $headBranch = Get-NbBranch -Cwd $Path
    $headCommit = Get-NbGitLine -Cwd $Path -GitArgs @('rev-parse', '--short', 'HEAD')
    Write-Host ("  分支 {0}   HEAD {1}" -f $headBranch, $headCommit) -ForegroundColor DarkGray

    $dirty = @(& git -C $Path status --porcelain 2>$null)
    if ($dirty.Count -gt 0) {
        Write-Host ("  未提交改动：{0} 个文件" -f $dirty.Count) -ForegroundColor Yellow
        $dirty | Select-Object -First 5 | ForEach-Object { Write-Host ("    " + $_) -ForegroundColor Yellow }
        if ($dirty.Count -gt 5) { Write-Host ("    …还有 {0} 个" -f ($dirty.Count - 5)) -ForegroundColor Yellow }
    } else {
        Write-Host '  未提交改动：无' -ForegroundColor DarkGray
    }

    $unpushed = Get-CommitCount -Path $Path -RevArgs @('HEAD', '--not', '--remotes=origin')
    if ($unpushed -gt 0) {
        Write-Host ("  未推送提交：{0} 个（origin 上没有任何引用能到达）" -f $unpushed) -ForegroundColor Yellow
        $hashes = @(& git -C $Path log --format=%h --max-count=5 HEAD --not --remotes=origin 2>$null)
        Write-Host ("    " + ($hashes -join ' ')) -ForegroundColor Yellow
        Write-Host ('    看内容： git -C "' + $Path + '" log --oneline -5') -ForegroundColor DarkGray
    } else {
        Write-Host '  未推送提交：无' -ForegroundColor DarkGray
    }

    Write-Host '  确认没有会话在跑之后，清理命令：' -ForegroundColor Yellow
    Write-Host ('    git worktree remove --force "' + $Path + '"') -ForegroundColor Yellow
    if ($headBranch -and $headBranch -ne 'HEAD') {
        Write-Host ("    git branch -D {0}" -f $headBranch) -ForegroundColor Yellow
    }
    Write-Host '  或者换个 -Agent 名重开（.worktrees/<agent> 之间是隔离的）。' -ForegroundColor Yellow
}

function Show-StaleBranch {
    param([string]$Name)

    Write-Host ("✗ 分支已存在：{0}（worktree add -b 会因重名失败）" -f $Name) -ForegroundColor Red
    $headCommit = Get-NbGitLine -Cwd $repoRoot -GitArgs @('rev-parse', '--short', $Name)
    Write-Host ("  HEAD {0}" -f $headCommit) -ForegroundColor DarkGray
    $ahead = Get-CommitCount -Path $repoRoot -RevArgs @("origin/main..$Name")
    Write-Host ("  相对 origin/main：{0} 个提交未合入" -f $ahead) -ForegroundColor DarkGray
    $unpushed = Get-CommitCount -Path $repoRoot -RevArgs @($Name, '--not', '--remotes=origin')
    if ($unpushed -gt 0) {
        Write-Host ("  未推送提交：{0} 个" -f $unpushed) -ForegroundColor Yellow
        $hashes = @(& git -C $repoRoot log --format=%h --max-count=5 $Name --not --remotes=origin 2>$null)
        Write-Host ("    " + ($hashes -join ' ')) -ForegroundColor Yellow
        Write-Host ('    看内容： git -C "' + $repoRoot + '" log --oneline -5 ' + $Name) -ForegroundColor DarkGray
    } else {
        Write-Host '  未推送提交：无（远端已有，删了也能找回）' -ForegroundColor DarkGray
    }
    Write-Host '  确认没有会话在跑之后，清理命令：' -ForegroundColor Yellow
    Write-Host ("    git branch -D {0}" -f $Name) -ForegroundColor Yellow
    Write-Host '  或者直接复用：换 -Branch 名，或用已有分支开区（不带 -b）。' -ForegroundColor Yellow
}

# ---------- 分支名前缀：入口即校验（DX-12 补）----------
# 原先这里什么都不查：分支名要到「领用空提交」才被 commit-msg 钩子拒，而那时 worktree、
# .sandbox、.agent-env.ps1、占用登记全都建好了 —— 只能手工 `git worktree remove --force`
# 再加 `git branch -D` 重来（实测踩过：feat/ 不是合法前缀，正确的是 feature/）。
# 允许表**只认 .githooks/pre-commit 的 allowed_prefixes**：另写一份必然漂移，症状是
# 脚本放行、提交才被拒，比不校验更迷惑人。钩子缺失时放行（上面装钩子那步已报过警告）。
function Get-AllowedBranchPrefixes {
    $hook = Join-Path $repoRoot '.githooks\pre-commit'
    if (-not (Test-Path -LiteralPath $hook)) { return @() }
    try { $hookText = [System.IO.File]::ReadAllText($hook) } catch { return @() }
    $m = [regex]::Match($hookText, '(?m)^\s*allowed_prefixes\s*=\s*"([^"]*)"')
    if (-not $m.Success) { return @() }
    return @($m.Groups[1].Value -split '\s+' | Where-Object { $_ })
}

function Assert-BranchName {
    param([string]$Name)

    $allowed = Get-AllowedBranchPrefixes
    if ($allowed.Count -eq 0) { return }
    $slash = $Name.IndexOf('/')
    $prefix = if ($slash -gt 0) { $Name.Substring(0, $slash) } else { '' }
    if ($prefix -and ($allowed -contains $prefix)) { return }

    Write-Host ("✗ 分支名 {0} 不符合规范（pre-commit 会拒，这里提前拦）" -f $Name) -ForegroundColor Red
    if ($prefix) {
        Write-Host ("  前缀 '{0}' 不在允许列表内。" -f $prefix) -ForegroundColor Yellow
    } else {
        Write-Host '  分支名缺少类型前缀，必须形如 type/简述。' -ForegroundColor Yellow
    }
    Write-Host ("  允许：{0}" -f (($allowed | ForEach-Object { "$_/" }) -join ' ')) -ForegroundColor Yellow
    Write-Host '  示例： -Branch feature/task-board-filter' -ForegroundColor Yellow
    Write-Host '  提示：允许表来自 .githooks/pre-commit，改前缀规则只需改那一处。' -ForegroundColor DarkGray
    exit 2
}

# ---------- 检出形态识别 ----------
# 主工作树的 git-dir 就是 common dir；linked worktree 的 git-dir 落在
# <common>/worktrees/<name> 下。两者不等 ⇒ 当前检出已经是一个独立 worktree。
$gitDir = ''
$commonDir = ''
Push-Location $repoRoot
try {
    $gitDirRaw = Get-NbGitLine -GitArgs @('rev-parse', '--git-dir')
    if ($gitDirRaw) { $gitDir = (Resolve-Path -LiteralPath $gitDirRaw).Path }
    $commonDir = Get-NbCommonDir -RepoPath $repoRoot
} finally {
    Pop-Location
}
if ($gitDir -and $commonDir -and ($gitDir -ne $commonDir) -and -not $ForceNested) {
    $InPlace = $true
}

# ---------- 锚点区不是开发区（DX-05 硬门）----------
# -InPlace 的本意是「脚本所属检出本身已经是隔离 worktree」（WorkBuddy / 手工 worktree）。
# 在锚点区传它，等于把全仓共享的检出降级成自己的开发区：两个会话互相切走 HEAD、
# 共用 .sandbox（2026-10-28 凌晨实测：.worktrees/ 一个都没建，双方都在根目录提交）。
# 现在把它做成不可能：锚点区显式 -InPlace 直接拒；确需在锚点动手必须 -ForceAnchor，
# 且自动登记 root 占用，让后来者看得见、不再撞车。
$isAnchor = [bool]($gitDir -and $commonDir -and ($gitDir -eq $commonDir))
if ($isAnchor -and $PSBoundParameters.ContainsKey('InPlace') -and -not $ForceAnchor) {
    Write-Host '✗ 拒绝在锚点区原地开区（-InPlace）：锚点区不是开发区。' -ForegroundColor Red
    Write-Host '  独立工作区： pwsh -File scripts/agent-init.ps1 -Agent <名> -Branch <分支> -Card <短号>' -ForegroundColor Yellow
    Write-Host '  确需在锚点区动手请显式加 -ForceAnchor（会自动登记 root 占用，避免与别的会话撞车）。' -ForegroundColor Yellow
    exit 3
}
if ($isAnchor -and $ForceAnchor) {
    $InPlace = $true
    Write-Host '⚠ 锚点区原地模式（-ForceAnchor）：锚点不是开发区，改动请尽快合并回 main 并释放占用' -ForegroundColor Yellow
}

if (-not $Branch) { $Branch = "feature/$Agent" }

# 原地模式不校验：那时保留的是当前检出的分支（可能由 WorkBuddy 生成），-Branch 只是提示，
# 真正提交用的分支已经在原处，拦它没有任何收益。
if (-not $InPlace) { Assert-BranchName -Name $Branch }

# -Card none：显式声明「本次不领用任何卡片」。
# 之所以强制表态，是因为漏填时脚本无法区分「忘了」和「不需要」，
# 多代理并行时漏填就会出现两个人同时改同一张卡却都不占位。
if ($Card -and $Card.Trim() -ieq 'none') {
    Write-Host '· 本次未领用任何卡片（-Card none）' -ForegroundColor Yellow
    $Card = ''
}

# ---------- 网络预检（DX-66）----------
# 写动作一旦开始就要求「失败即无残留」，所以联网检查必须排在第一个写动作（worktree add /
# 占用登记）之前：以前网络故障走到半途才炸 —— worktree 建好、占用登记写好、领用提交做了一半，
# 一句 throw 把残局留给调用方。预探不带 --exit-code：探的是「连得上」，不是「ref 存在」；
# 3 次带退避，全失败 exit 6，此刻什么都没建，恢复网络后重跑就是干净起点。
# -NoFetch 且未领卡时全程不需要网络，跳过预检。
$needsNetwork = ((-not $NoFetch) -or ($Card -and $Card.Trim().Length -gt 0))
if ($needsNetwork) {
    Write-Host '· 网络预检（origin 可达性，3 次退避）...' -ForegroundColor DarkGray
    $preflightOk = $false
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        & git ls-remote origin refs/heads/main *> $null
        if ($LASTEXITCODE -eq 0) { $preflightOk = $true; break }
        if ($attempt -lt 3) {
            Write-Host ("  第 {0} 次探测失败（退出码 {1}），{2}s 后重试 ..." -f $attempt, $LASTEXITCODE, (2 * $attempt)) -ForegroundColor Yellow
            Start-Sleep -Seconds (2 * $attempt)
        }
    }
    if (-not $preflightOk) {
        Write-Host '✗ 网络预检失败：origin 不可达（已试 3 次）。' -ForegroundColor Red
        Write-Host '  开区需要联网（fetch / 领用占位推送 refs/claims/*）。本次未创建任何产物，' -ForegroundColor Yellow
        Write-Host '  恢复网络后重跑即可；离线的纯文档杂务可用「-NoFetch -Card none」开区。' -ForegroundColor Yellow
        exit 6
    }
    Write-Host '· 网络预检通过' -ForegroundColor DarkGray
}

if (-not $NoFetch) {
    Write-Host '· 同步远端引用 ...' -ForegroundColor DarkGray
    & git -C $repoRoot fetch origin --prune
    if ($LASTEXITCODE -ne 0) {
        # 预检刚过 fetch 又挂 = 瞬时抖动，同样的处理：未创建任何产物，干净退出
        Write-Host '✗ git fetch 失败（预检已通过，属瞬时网络问题）。本次未创建任何产物，重跑即可。' -ForegroundColor Red
        exit 6
    }
}

# ---------- 领用前置探针（DX-66）----------
# 服务端占位在别人手里时，以前要到「领用空提交做完 + claim-card 推送被拒」才知道 ——
# worktree / 分支 / 占用登记早已建好，回滚全靠手工（2026-11-06 领 DX-69 实测三处残留）。
# 现在在第一个写动作之前探一次：占位存在 → 直接拒绝（此时什么都没建，无需清理）；
# 不可达 → 保守拒绝（fail-closed，§7.2.1：远端不可达时视为仍被占用）；
# 探不到占位（exit 2）→ 放行。权威判定仍在 claim-card 的原子推送（竞态窗口的兜底走回滚）。
# 只对新开 worktree 的领用做探针：原地模式（-InPlace 补领 / 重跑同一检出）的幂等判定
# 在领用段内 —— 分支历史里已有自己的领用提交，前置探针会把重跑误判成双领。
if ($Card -and -not $InPlace) {
    $probeCode = $Card.TrimStart('#').ToUpperInvariant()
    & git ls-remote --exit-code origin ("refs/claims/{0}" -f $probeCode) *> $null
    $probeState = $LASTEXITCODE
    if ($probeState -ne 2) {
        if ($probeState -eq 0) {
            Write-Host ("✗ 卡片 {0} 已被服务端占位（refs/claims/{0} 已存在），不能再领（防双领）。" -f $probeCode) -ForegroundColor Red
            Write-Host '  占位由其它活动分支持有：等对方合并释放，或换别的卡。' -ForegroundColor Yellow
        } else {
            Write-Host ("✗ 无法确认卡片 {0} 的占位状态（ls-remote 退出码 {1}），保守拒绝（fail-closed）。" -f $probeCode, $probeState) -ForegroundColor Red
            Write-Host '  领用需要联网确认占位；恢复网络后重跑。' -ForegroundColor Yellow
        }
        Write-Host '  此刻尚未创建任何工作区产物，无需清理。' -ForegroundColor DarkGray
        exit 3
    }
}

# DX-111 弃单终态：origin/main 台账里 baseline=abandoned 的卡是已结案的弃单，
# 不得再领用（旧门只认 baseline=done，弃单会永挂 todo 被反复重领）。
# 判据只在 derive_board_status.py --abandoned-codes 一处实现，与 claim-card 的
# 重开门同读 origin/main 那份台账（唯一事实源）；这里不重复解析 JSON。
function Test-AbandonedClaim {
    param([string]$Code)
    # 返回 $true = 本次领用必须被拒（核对不了也按 fail-closed 拒），$false = 放行。
    $derivePy = Join-Path $repoRoot 'scripts\derive_board_status.py'
    if (-not (Test-Path -LiteralPath $derivePy)) {
        Write-Host '✗ 找不到 derive_board_status.py，无法核对弃单终态，保守拒绝（fail-closed，DX-111）。' -ForegroundColor Red
        return $true
    }
    $abandonedCodes = @(& python $derivePy --repo $repoRoot --ledger-ref origin/main --abandoned-codes 2>$null |
        ForEach-Object { "$_".Trim().ToUpperInvariant() } | Where-Object { $_ })
    $abandonedRc = $LASTEXITCODE
    if ($abandonedRc -ne 0) {
        Write-Host ("✗ 无法核对弃单终态清单（derive --abandoned-codes 退出码 {0}），保守拒绝（fail-closed，DX-111）。" -f $abandonedRc) -ForegroundColor Red
        return $true
    }
    if ($abandonedCodes -contains $Code.ToUpperInvariant()) {
        Write-Host ("✗ 卡片 {0} 已按弃单结案（baseline=abandoned），不允许重新领用（DX-111）。" -f $Code) -ForegroundColor Red
        Write-Host '  弃单是终态：确需重做，先由维护者经 chore(board) 授权把 baseline 改回 todo。' -ForegroundColor Yellow
        return $true
    }
    return $false
}

# 标准开区路径：放在第一个写动作之前，拒绝时不产生 worktree / 分支 / 占位，无需回滚。
if ($Card -and -not $InPlace -and (Test-AbandonedClaim $probeCode)) {
    Write-Host '  此刻尚未创建任何工作区产物，无需清理。' -ForegroundColor DarkGray
    exit 7
}

# ---------- 开区事务（DX-66）----------
# 从第一个写动作起登记本次创建的产物，任一步失败调 Invoke-TxnRollback 按固定顺序回滚：
# 提交 → 服务端占位 → 占用登记 → 工作区 → 分支。顺序不能按创建时间倒着来 ——
# 分支要等 worktree 删完才能删（检出占用）。失败即无残留，重跑就是干净起点。
$script:txnWorktree = ''       # 本次创建的 worktree 路径
$script:txnBranch = ''         # 本次创建的分支
$script:txnWsScope = ''        # 本次登记的占用（root / worktree:<agent>）
$script:txnServerClaim = ''    # 本次可能推过的服务端占位（短号；回滚按 sha 归属判定后才删）
$script:txnHeadSha = ''        # 领用空提交头，用于判定服务端占位是否属于本次运行
$script:txnInPlaceCommits = 0  # 原地模式追加的领用提交数（非原地随 worktree 一起删）

function Invoke-TxnRollback {
    $touched = ($script:txnWorktree -or $script:txnBranch -or $script:txnWsScope -or
                $script:txnServerClaim -or ($script:txnInPlaceCommits -gt 0))
    if (-not $touched) { return }
    Write-Host ''
    Write-Host '✗ 开区失败，自动回滚本次已创建的产物（DX-66）：' -ForegroundColor Red
    $undoFailed = $false

    if ($script:txnInPlaceCommits -gt 0) {
        & git -C $worktree reset --hard ("HEAD~{0}" -f $script:txnInPlaceCommits) *> $null
        if ($LASTEXITCODE -eq 0) {
            Write-Host ("  ↩ 已回退原地领用提交 {0} 个" -f $script:txnInPlaceCommits) -ForegroundColor DarkGray
        } else {
            $undoFailed = $true
            Write-Host ("  ✗ 原地提交回退失败，手工清理： git -C `"{0}`" reset --hard HEAD~{1}" -f $worktree, $script:txnInPlaceCommits) -ForegroundColor Yellow
        }
    }

    if ($script:txnServerClaim) {
        # 占位是否属于本次运行，看 sha：等于本次领用提交头 → 是本次孤儿占位，删掉让卡回到可领；
        # 指向别的提交 → 别的会话的占位，一概不碰（回滚不是回收站，不能替别人放卡）。
        $remoteLine = & git ls-remote origin ("refs/claims/{0}" -f $script:txnServerClaim) 2>$null | Select-Object -First 1
        $remoteSha = if ($remoteLine) { (([string]$remoteLine).Trim() -split '\s+')[0] } else { '' }
        if ($remoteSha -and $remoteSha -eq $script:txnHeadSha) {
            & git push origin --delete ("refs/claims/{0}" -f $script:txnServerClaim) *> $null
            if ($LASTEXITCODE -eq 0) {
                Write-Host ("  ↩ 已释放本次的服务端占位 refs/claims/{0}" -f $script:txnServerClaim) -ForegroundColor DarkGray
            } else {
                $undoFailed = $true
                Write-Host ("  ✗ 服务端占位释放失败（离线/被拒），恢复后手工清理： git push origin --delete refs/claims/{0}" -f $script:txnServerClaim) -ForegroundColor Yellow
            }
        } else {
            Write-Host ("  · 服务端占位 refs/claims/{0} 不属于本次运行（sha 不符或不存在），不代删" -f $script:txnServerClaim) -ForegroundColor DarkGray
        }
    }

    if ($script:txnWsScope) {
        $undoPwsh = Get-Command pwsh -ErrorAction SilentlyContinue
        $undoExe = if ($undoPwsh) { $undoPwsh.Source } else { 'powershell' }
        & $undoExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot 'scripts\agent-workspace.ps1') -Action Release -Agent $Agent -Scope $script:txnWsScope *> $null
        if ($LASTEXITCODE -eq 0) {
            Write-Host ("  ↩ 已释放占用登记 {0}" -f $script:txnWsScope) -ForegroundColor DarkGray
        } else {
            $undoFailed = $true
            Write-Host ("  ✗ 占用登记释放失败，手工清理： pwsh -File scripts\agent-workspace.ps1 -Action Release -Agent {0} -Scope {1}" -f $Agent, $script:txnWsScope) -ForegroundColor Yellow
        }
    }

    if ($script:txnWorktree) {
        & git -C $repoRoot worktree remove --force $script:txnWorktree *> $null
        if ($LASTEXITCODE -eq 0) {
            Write-Host ("  ↩ 已删除工作区 {0}" -f $script:txnWorktree) -ForegroundColor DarkGray
        } else {
            $undoFailed = $true
            Write-Host ("  ✗ 工作区删除失败，手工清理： git worktree remove --force `"{0}`"" -f $script:txnWorktree) -ForegroundColor Yellow
        }
    }

    if ($script:txnBranch) {
        & git -C $repoRoot branch -D $script:txnBranch *> $null
        if ($LASTEXITCODE -eq 0) {
            Write-Host ("  ↩ 已删除分支 {0}" -f $script:txnBranch) -ForegroundColor DarkGray
        } else {
            $undoFailed = $true
            Write-Host ("  ✗ 分支删除失败，手工清理： git branch -D {0}" -f $script:txnBranch) -ForegroundColor Yellow
        }
    }

    if ($undoFailed) {
        Write-Host '  上面标 ✗ 的项清完之前，同名开区重跑会撞残留诊断（exit 4）。' -ForegroundColor Yellow
    }
}

if ($InPlace) {
    # 已经有隔离目录了（WorkBuddy Worktree Task / 手工 worktree）：改用它，不再嵌套。
    $worktree = $repoRoot
    $currentBranch = Get-NbBranch -Cwd $worktree
    if (-not $currentBranch -or $currentBranch -eq 'HEAD') {
        throw '当前检出处于分离 HEAD，取不到原分支名，无法原地初始化（DX-63）'
    }
    if (-not $Branch -or $Branch -eq "feature/$Agent") { $Branch = $currentBranch }
    if ($Branch -ne $currentBranch) {
        Write-Host ("  分支请求 {0} ≠ 当前检出 {1}：保留原分支，不擅自切换别人的工作区" -f $Branch, $currentBranch) -ForegroundColor Yellow
        $Branch = $currentBranch
    }
    Write-Host ("· 原地初始化：复用当前检出 {0}" -f $worktree) -ForegroundColor Cyan
    Write-Host ("  分支 {0}（未嵌套创建子工作区）" -f $Branch) -ForegroundColor DarkGray

    # 检出里的 .agent-env.ps1 是「谁在用这份检出」的物证。指向别人说明另一个会话
    # 正把它当开发区（DX-05 的原始现场），两边会共享 HEAD 与 .sandbox —— 默认拒绝。
    $probeEnv = Join-Path $worktree '.agent-env.ps1'
    $owner = ''
    if (Test-Path -LiteralPath $probeEnv) {
        try {
            $envText = [System.IO.File]::ReadAllText($probeEnv, [System.Text.Encoding]::UTF8)
            $mOwner = [regex]::Match($envText, '(?m)^\s*\$env:NEONBOARD_AGENT_ID\s*=\s*''([^'']*)''')
            if ($mOwner.Success) { $owner = $mOwner.Groups[1].Value.Trim() }
        } catch { $owner = '' }
    }
    if ($owner -and $owner -ne $Agent) {
        if (-not $ForceAnchor) {
            Write-Host ("✗ 检出里已有他人 in-place 环境（NEONBOARD_AGENT_ID = {0}）" -f $owner) -ForegroundColor Red
            Write-Host ("  路径：{0}" -f $worktree) -ForegroundColor DarkGray
            Write-Host '  另一个会话正在用它，你们会共享 HEAD 与 .sandbox（沙箱数据互相覆盖）。' -ForegroundColor Yellow
            Write-Host '  独立开区：去掉 -InPlace 改用 .worktrees/<agent>；确需接管请加 -ForceAnchor。' -ForegroundColor Yellow
            exit 3
        }
        Write-Host ("⚠ 已按 -ForceAnchor 接管 '{0}' 的检出（请确认对方会话已停）" -f $owner) -ForegroundColor Yellow
    }
} else {
    $worktree = Join-Path (Join-Path $repoRoot '.worktrees') $Agent

    if (Test-Path -LiteralPath $worktree) {
        Show-StaleWorktree -Path $worktree
        exit 4
    }

    & git -C $repoRoot show-ref --verify --quiet "refs/heads/$Branch"
    if ($LASTEXITCODE -eq 0) {
        Show-StaleBranch -Name $Branch
        exit 4
    }

    Write-Host ("· 创建工作区 {0}" -f $worktree) -ForegroundColor Cyan
    Write-Host ("  分支 {0}  基线 {1}" -f $Branch, $Base) -ForegroundColor DarkGray
    & git -C $repoRoot worktree add $worktree -b $Branch $Base
    if ($LASTEXITCODE -ne 0) {
        # add 半途失败可能留下部分产物（目录或 -b 的分支），探到就清：失败即无残留
        Write-Host ("✗ git worktree add 失败（退出码 {0}）" -f $LASTEXITCODE) -ForegroundColor Red
        & git -C $repoRoot worktree remove --force $worktree *> $null
        & git -C $repoRoot branch -D $Branch *> $null
        exit 1
    }
    $script:txnWorktree = $worktree
    $script:txnBranch = $Branch
}

$sandbox = Join-Path $worktree '.sandbox'
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null
# 会话产物的显式落点：桌面工具默认往调用方 CWD 写，落在锚点就会拖脏它、卡住 git-done 的快进同步
# （docs/pitfalls.md 1.1 / 7.7）。我方脚本与文档统一约定写到这里；外部工具不吃这个变量的仍靠 .gitignore 点名。
$artifacts = Join-Path $sandbox 'artifacts'
New-Item -ItemType Directory -Path $artifacts -Force | Out-Null

$envFile = Join-Path $worktree '.agent-env.ps1'
$envLines = @(
    "# 由 scripts/agent-init.ps1 生成：多代理沙箱环境变量",
    "# 用法： . .\.agent-env.ps1   （PowerShell 7 推荐 pwsh）",
    "`$env:NEONBOARD_DATA_DIR = '$sandbox'",
    "`$env:NEONBOARD_INSTANCE = '$Agent'",
    "`$env:NEONBOARD_ROADMAP = '$(Join-Path $sandbox 'progress.json')'",
    "`$env:NEONBOARD_ROADMAP_AUTOWRITE = '0'",
    "`$env:NEONBOARD_AGENT_ID = '$Agent'",
    "`$env:NB_SESSION_ARTIFACT_DIR = '$artifacts'"
)

# ---------- 任务卡字段注入（docs/task-card.md）----------
# 让 agent 一进工作区就知道：这张卡要验收什么、能改哪些路径、依赖哪张卡。
# 生成的是单引号 PowerShell 字面量：值里的 ' 必须双写，否则验收项自带的引号会把
# 字符串提前截断，dot-source 直接 ParserError（DX-99；DX-97 的 `rg 'z: 1'` 实测踩过）。
function Format-PsSingleQuoted {
    param([string]$Value)

    if ($null -eq $Value) { return '' }
    return $Value.Replace("'", "''")
}

if ($Card -and $Card -ne 'none') {
    # 卡契约的唯一数据源是 origin/main 台账（DX-94）：与领用重开门判据同源。
    # 旧实现读 $repoRoot 下的本地台账副本 —— 锚点检出停在分叉的 main 时会注入过期
    # 契约（agent-dx74 领 DX-74 拿到 DX-88 契约），check_scope 必拒、白干一轮。
    # 台账读取收口在 derive_board_status.py（--contract），结果落盘再按字节读：
    # PowerShell 捕获 Python 子进程 stdout 会按控制台代码页（GBK）解码中文（DX-83 同源），
    # 落盘文件绕开管道。worktree 与 -InPlace 两条路径共用这段代码，同源。
    $cardCode = $Card.TrimStart('#').ToUpperInvariant()
    $contractPy = Join-Path $repoRoot 'scripts\derive_board_status.py'
    $contractPath = Join-Path ([System.IO.Path]::GetTempPath()) `
        ("nb-contract-{0}.json" -f [guid]::NewGuid().ToString('N'))
    $contract = $null
    try {
        if (Test-Path -LiteralPath $contractPy) {
            $null = & python $contractPy --repo $repoRoot --ledger-ref origin/main `
                --contract $cardCode --out $contractPath 2>$null
            $contractRc = $LASTEXITCODE
            if ($contractRc -eq 0 -and (Test-Path -LiteralPath $contractPath)) {
                # 按字节读：显式 UTF-8，不经 PS 管道解码
                $contract = [System.IO.File]::ReadAllText(
                    $contractPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
            } else {
                Write-Host ("· 无法从 origin/main 读取 #{0} 契约（退出码 {1}），跳过 NB_* 注入" -f $cardCode, $contractRc) -ForegroundColor Yellow
            }
        } else {
            Write-Host ("· 契约源脚本缺失（{0}），跳过 NB_* 注入" -f $contractPy) -ForegroundColor Yellow
        }
    } catch {
        Write-Host ("· 读取 #{0} 契约失败：{1}；跳过 NB_* 注入" -f $cardCode, $_.Exception.Message) -ForegroundColor Yellow
    } finally {
        if (Test-Path -LiteralPath $contractPath) {
            Remove-Item -LiteralPath $contractPath -Force -ErrorAction SilentlyContinue
        }
    }
    # 不回落到本地台账：分叉副本正是本卡要根治的陈旧源，宁可少注入也不注入错契约。
    if ($contract) {
        $envLines += "`$env:NB_CARD = '$cardCode'"
        if ($contract.scope)      { $envLines += ("`$env:NB_SCOPE = '" + (Format-PsSingleQuoted (($contract.scope) -join ';')) + "'") }
        if ($contract.acceptance) { $envLines += ("`$env:NB_ACCEPTANCE = '" + (Format-PsSingleQuoted (($contract.acceptance) -join ';')) + "'") }
        if ($contract.depends)    { $envLines += ("`$env:NB_DEPENDS = '" + (Format-PsSingleQuoted (($contract.depends) -join ';')) + "'") }
    }
}
$envContent = ($envLines -join "`n")
[System.IO.File]::WriteAllText($envFile, $envContent, (New-Object System.Text.UTF8Encoding($false)))

# ---------- 工作区占用登记（自动，DX-05）----------
# 以前占用登记是自愿的：Status 显示 root 空闲、两个会话都没登记，于是「谁在用这份检出」
# 全靠口头约定。现在开区即登记 —— 锚点区原地模式登记 root，其余一律登记 worktree:<agent>，
# git-done 收尾自动 Release；后来者 agent-init 撞到占用会被拒，而不是等出事后才发现。
# 注意判定对象是**本次要用的检出**，不是脚本所在检出：正常开区时脚本就在锚点区跑，
# 但新建的是 .worktrees/<agent>，按 $isAnchor 判会把每个开发区都登记成 root（已踩过）。
$pwshExe = 'powershell'
$pwshCmd2 = Get-Command pwsh -ErrorAction SilentlyContinue
if ($pwshCmd2) { $pwshExe = $pwshCmd2.Source }
$wsScript = Join-Path $repoRoot 'scripts\agent-workspace.ps1'
$claimScope = if ($InPlace -and $isAnchor) { 'root' } else { "worktree:$Agent" }
$workspaceClaimed = $false
if (Test-Path -LiteralPath $wsScript) {
    $claimTask = if ($Card) { $Card } else { $Branch }
    $claimOut = @()
    $claimCode = 0
    Push-Location $repoRoot
    try {
        $prevEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $claimOut = @(& $pwshExe -NoProfile -ExecutionPolicy Bypass -File $wsScript -Action Claim -Agent $Agent -Scope $claimScope -Task $claimTask 2>&1)
            $claimCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $prevEap
        }
    } finally {
        Pop-Location
    }
    foreach ($line in $claimOut) { Write-Host ("   {0}" -f $line) -ForegroundColor DarkGray }
    if ($claimCode -ne 0) {
        Write-Host ("✗ 工作区占用登记被拒绝（{0}，退出码 {1}）" -f $claimScope, $claimCode) -ForegroundColor Red
        Write-Host '  已有会话在占用它。换一个 agent 名独立开区，或等对方释放：' -ForegroundColor Yellow
        Write-Host '  pwsh -File scripts/agent-workspace.ps1 -Action Status' -ForegroundColor Yellow
        Invoke-TxnRollback
        exit 3
    }
    $workspaceClaimed = $true
    $script:txnWsScope = $claimScope
}

# ---------- 领用登记（-Card <短号>） ----------
# 领用规则：大任务（R#-#）直接领；小任务（M##）必须父任务未被任何人认领，
# 父任务映射取自 board.db 的 task_link（child_id -> parent_id）与台账 cards[].id。
# 流程：
#   1) 空提交 `chore(claim): 领取 #<code> 实施任务` —— 信息里的 #<code> 会触发
#      post-commit 钩子投递（无 = 默认 doing），同时给出分支第一个提交的 head
#   2) python 校验短号存在 + 防双领（遍历 origin/main 与各远端分支的台账 claims），
#      把 claims[<code>] = {agent, branch, head, claimedAt} 写进**本 worktree** 的台账副本
#   3) 第二个提交把登记落进分支；合并回 main 后 CI 派生即可识别 doing
#   4) 显式 --emit-hook-file 投一次 #<code>=doing 兜底（不阻塞、幂等）
#
# 一分支多卡（DX-03）：在同一个检出里再跑一次 -InPlace -Card <另一张卡> 就是补领，
# 会**追加**一条领用登记而不重写已有的。所以这里必须先判「本卡是否已领」——
# 已领就整段跳过，否则每补领一次都会多出两条同卡号的领用提交（实测踩过）。
if ($Card) {
    $code = $Card.TrimStart('#').ToUpperInvariant()
    if ($code -notmatch '^([A-Za-z]{1,4}\d*-\d+|M\d+)$') {
        throw "领用失败：-Card 需为路线图短号（如 R3-01 / M19），收到：$Card"
    }

    # 幂等门：本分支历史里已有该短号的领用提交 → 不重复领用。
    # DX-40：主题串含中文，git 的 UTF-8 输出会被 PS 按控制台代码页（GBK）解码成乱码
    #（DX-33 同源问题），精确匹配必失 —— 改用 ASCII 稳定片段（短号）做正则匹配。
    # DX-35：历史命中后还要看**服务端事实**——新分支从 main 切出来会继承全部历史，
    # 卡合并后占位被 git-done 释放，历史里的领用提交却还在，按旧逻辑判「已在本分支
    # 领用」直接跳过，同一张卡再也领不回来（R5-10 现场），只能直调 claim-card.py
    # 绕过唯一入口。占位已释放（ls-remote --exit-code 退 2）→ 放行重新登记并重推占位；
    # 占位仍在 → 跳过；网络不可用（其它退出码）→ 保守当仍被占用（fail-closed，
    # 离线时 claim-card 的占位推送本来也走不通）。
    $claimPattern = 'chore\(claim\):.*#' + [regex]::Escape($code) + '\b'
    $alreadyClaimed = @(& git -C $worktree log --format=%s 2>$null) |
        Where-Object { $_ -match $claimPattern }
    if ($alreadyClaimed) {
        & git ls-remote --exit-code origin ("refs/claims/{0}" -f $code) *> $null
        $refState = $LASTEXITCODE
        if ($refState -eq 2) {
            Write-Host ("· #{0} 的服务端占位已释放（旧领用随合并/放弃失效），放行重新登记（DX-35）" -f $code) -ForegroundColor DarkGray
        } else {
            if ($refState -ne 0) {
                Write-Host ("· 无法确认 #{0} 的占位状态（ls-remote 退出码 {1}），保守跳过" -f $code, $refState) -ForegroundColor Yellow
            } else {
                Write-Host ("· #{0} 已在本分支领用且占位仍在，跳过重复登记（一分支多卡允许，只补缺的卡）" -f $code) -ForegroundColor DarkGray
            }
            # DX-82：rebase 后本地登记 head 指向重写前提交属预期态，跳过不是「该重新领用」的信号
            Write-Host "· 本地登记 head 已随 rebase 过期（预期，服务端占位为准）" -ForegroundColor DarkGray
            $Card = ''
        }
    }
}

if ($Card) {
    $code = $Card.TrimStart('#').ToUpperInvariant()

    # DX-111：-InPlace（补领）不走上面的开区前探针，这里补同一道弃单终态门。
    # 拒绝时 worktree 是调用方自己的，Invoke-TxnRollback 只清本次产物（空），不会误删。
    if ($InPlace -and (Test-AbandonedClaim $code)) {
        Invoke-TxnRollback
        exit 7
    }

    # DX-42：detached 检出禁止领用 —— 领用登记会记下 branch="HEAD"：
    # release-claims 的「分支不存在」判据被恒存在的 refs/remotes/origin/HEAD 符号引用
    # 永久骗过（登记成僵尸，DX-40 实测中招）；且游离提交变基会断开 refs/claims 祖先，
    # CI 领用闸门跟着红。fail-closed，切到具名分支后再领。
    $currentBranch = (& git -C $worktree branch --show-current 2>$null | Select-Object -First 1)
    if (-not $currentBranch -or $currentBranch.Trim().Length -eq 0) {
        Write-Host '[任务闸门] 检出处于 detached HEAD，禁止领用：会记下 branch="HEAD" 的僵尸登记，游离提交还会断开 claim 祖先（DX-42）。' -ForegroundColor Red
        Write-Host '  先 git switch -c <分支名>（或切到具名分支）再重跑。' -ForegroundColor Yellow
        Invoke-TxnRollback
        exit 3
    }

    # DX-40：领用提交（chore(claim)）走流程豁免，闸门不校验其内容 ——
    # 脏树里的未提交改动会被下面的裸 git commit 卷进领用提交，代码就这么
    # 失去短号凭证（R16-05 的 032efd8 实测卷走 7 个文件）。领用前必须干净
    #（fail-closed）：.agent-env.ps1 / .sandbox 均在 .gitignore，不会误伤自身产物。
    $dirty = @(& git -C $worktree status --porcelain 2>$null | Where-Object { $_.Trim().Length -gt 0 })
    if ($dirty.Count -gt 0) {
        Write-Host '[任务闸门] 领用前工作区必须干净：领用提交走流程豁免，卷入的改动会失去短号凭证（DX-40）。' -ForegroundColor Red
        $dirty | Select-Object -First 10 | ForEach-Object { Write-Host ("  " + $_) -ForegroundColor Yellow }
        Write-Host '  先提交 / stash，或把工具产物目录补进 .gitignore 后重跑。' -ForegroundColor Yellow
        Invoke-TxnRollback
        exit 3
    }

    # DX-62：滞留卡告警 —— 裸 #<短号> 交付提交已合入主线、全历史却无 #<短号>=done 的卡，
    # 自动结卡永远读不到它的 done 标记，防重复领用的门也关不上（DX-38/DX-44 实录）。
    # 判定单一实现在 derive_board_status.py（--stuck-codes），这里只取结果：命中即显式
    # 警告，不静默放行；是否真的需要重做由人与维护者确认。台账按领用门同口径只认
    # origin/main 那份；告警是 fail-soft —— 核对不了不拦领用，但必须可见。
    $derivePy = Join-Path $repoRoot 'scripts\derive_board_status.py'
    if (Test-Path -LiteralPath $derivePy) {
        $stuckCodes = @(& python $derivePy --repo $repoRoot --ledger-ref origin/main --stuck-codes 2>$null |
            ForEach-Object { "$_".Trim().ToUpperInvariant() } | Where-Object { $_ })
        if ($LASTEXITCODE -eq 0 -and ($stuckCodes -contains $code)) {
            Write-Host ("⚠ #{0} 交付疑似已在 main：裸 #{0} 标记的提交已合入主线，但全历史无 #{0}=done（DX-62）。" -f $code) -ForegroundColor Yellow
            Write-Host '  这张卡大概率已交付、只是漏投 done 标记 —— 领用前先与维护者确认是否真的重做，别直接开写。' -ForegroundColor Yellow
        } elseif ($LASTEXITCODE -ne 0) {
            Write-Host ("· 滞留卡核对未完成（derive_board_status 退出码 {0}），本次跳过告警" -f $LASTEXITCODE) -ForegroundColor DarkGray
        }
    }

    Write-Host ("· 领用 {0}：登记 ↔ 看板卡转「进行中」" -f $code) -ForegroundColor Cyan
    Push-Location $worktree
    try {
        # 子卡领用需要读本地看板的父子边；数据目录口径统一走 nb-paths.ps1
        # （搬进 try：路径解析抛错同样算领用失败，走回滚而不是留半成品）
        . (Join-Path $repoRoot 'scripts\nb-paths.ps1')
        $boardDb = Get-NbBoardDb

        # 1) 空提交：给分支一个 head，也让 post-commit 钩子顺手投递一次
        & git commit --allow-empty -m ("chore(claim): 领取 #{0} 实施任务" -f $code) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "领用空提交失败：$LASTEXITCODE" }
        $script:txnHeadSha = (& git rev-parse HEAD | Select-Object -First 1)
        if ($InPlace) { $script:txnInPlaceCommits++ }

        # 2) 校验 + 防双领 + 写本 worktree 台账副本。claim-card 先推服务端占位后做扫描，
        #    失败时占位可能已推成（本次的孤儿）也可能没推 —— 回滚按 sha 归属判定，非本次推的不代删。
        $script:txnServerClaim = $code
        $claimPy = Join-Path $repoRoot 'scripts\claim-card.py'
        if (-not (Test-Path -LiteralPath $claimPy)) { throw "找不到领用脚本：$claimPy" }
        & python $claimPy $worktree $code $Agent $Branch $boardDb
        if ($LASTEXITCODE -ne 0) { throw "领用登记失败：python 退出码 $LASTEXITCODE" }

        # 3) 登记落进分支
        & git add docs/board-ledger.json
        & git commit -m ("chore(claim): 登记 #{0} 领用到台账" -f $code) | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "领用登记提交失败：$LASTEXITCODE" }
        if ($InPlace) { $script:txnInPlaceCommits++ }
    } catch {
        Write-Host ("✗ {0}" -f $_.Exception.Message) -ForegroundColor Red
        Invoke-TxnRollback
        exit 3
    } finally {
        Pop-Location
    }

    # 4) 显式投递兜底（应用在跑就立即做 doing，不在跑则下次启动补投递）
    $deliverFile = Join-Path $env:TEMP ("neonboard-claim-{0}.txt" -f $Agent)
    [System.IO.File]::WriteAllText($deliverFile, ("#{0}=doing" -f $code), (New-Object System.Text.UTF8Encoding($false)))
    $exeProbe = $env:NEONBOARD_EXE
    if (-not ($exeProbe -and (Test-Path -LiteralPath $exeProbe))) {
        $candidates = @(
            (Join-Path $repoRoot 'build\Release\NeonBoard.exe'),
            (Join-Path $repoRoot 'dist\NeonBoard.exe'),
            (Join-Path $env:LOCALAPPDATA 'Programs\NeonBoard\NeonBoard.exe'),
            (Join-Path $env:ProgramFiles 'NeonBoard\NeonBoard.exe')
        )
        $exeProbe = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    }
    if ($exeProbe -and (Test-Path -LiteralPath $exeProbe)) {
        $oldPath = $env:PATH
        $qtBin = Get-ChildItem 'C:\Qt\*\msvc2022_64\bin' -Directory -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        $exeDir = Split-Path -Parent $exeProbe
        $env:PATH = (@($exeDir, $qtBin) | Where-Object { $_ }) -join ';' + ';' + $env:PATH
        try {
            & $exeProbe --emit-hook-file $deliverFile *> $null
            if ($LASTEXITCODE -eq 0) {
                Write-Host ("· 领用已投递：看板 #{0} → 进行中" -f $code) -ForegroundColor DarkGray
            } else {
                Write-Host ("领用投递未完成（退出码 {0}），看板启动时会补投递" -f $LASTEXITCODE) -ForegroundColor Yellow
            }
        } finally {
            $env:PATH = $oldPath
            Remove-Item -LiteralPath $deliverFile -ErrorAction SilentlyContinue
        }
    } else {
        Remove-Item -LiteralPath $deliverFile -ErrorAction SilentlyContinue
        Write-Host '未找到 NeonBoard.exe，看板启动时会补投递' -ForegroundColor Yellow
    }
}

# ---------- 工作区回收提示（只提示，不自动删）----------
# worktree 不是临时目录：不回收会长期占磁盘，也会让 `git worktree list` 越来越难读。
# 这里刻意不自动删 —— 自动回收很容易端掉一个正在跑的任务，删不删由人决定。
$nested = @()
$porcelain = @(& git -C $repoRoot worktree list --porcelain 2>$null)
foreach ($line in $porcelain) {
    if ($line -like 'worktree *') {
        $path = $line.Substring(9).Trim()
        if ((($path -replace '\\', '/')) -like '*/.worktrees/*') { $nested += $path }
    }
}
# ---------- 未推送成果警告（宪章 §7.2.1，只提示不代劳）----------
# HEAD 上有提交、但没有任何 origin 引用能到达它 = 这份成果只在这台机器上。
# 认领分支不推 origin，claim ref 指向的提交在远端不可达，CI 领用闸门也校验不了。
# 判定用本地已有的 origin/* 引用，不额外联网（开区前面已经 fetch 过一次）。
# **本次工作区要排除**：它刚刚建好，领用提交按设计还没推（git-done 合并时才推）。
# 不排除的话每个带 -Card 的开区都会自报「本地独有 2 个提交」，看着像有东西要丢（DX-12）。
$selfPath = ($worktree -replace '\\', '/').TrimEnd('/')
$selfUnpushed = 0
$orphans = @()
foreach ($path in $nested) {
    $cntRaw = & git -C $path rev-list --count HEAD --not --remotes=origin 2>$null | Select-Object -First 1
    if (-not $cntRaw) { continue }
    $cnt = 0
    if (-not [int]::TryParse(($cntRaw -replace '\s', ''), [ref]$cnt)) { continue }
    if ($cnt -le 0) { continue }
    if ($selfPath -and ((($path -replace '\\', '/').TrimEnd('/')) -ieq $selfPath)) {
        $selfUnpushed = $cnt
        continue
    }
    $refName = Get-NbSymRef -Cwd $path
    if (-not $refName) { $refName = '(detached)' }
    $orphans += [pscustomobject]@{ Path = $path; Branch = ($refName | Out-String).Trim(); Count = $cnt }
}
if ($selfUnpushed -gt 0) {
    Write-Host ''
    Write-Host ("· 本次工作区 {0} 有 {1} 个提交未推 origin：属正常，git-done 合并时统一推送" -f (Split-Path -Leaf $worktree), $selfUnpushed) -ForegroundColor DarkGray
}
if ($orphans.Count -gt 0) {
    Write-Host ''
    Write-Host ("⚠ 未推送成果：{0} 个其它工作区的提交不在任何 origin 引用可达范围内" -f $orphans.Count) -ForegroundColor Yellow
    foreach ($o in $orphans) {
        $leaf = Split-Path -Leaf $o.Path
        Write-Host ("    {0,-16} {1,-28} 本地独有 {2} 个提交" -f $leaf, $o.Branch, $o.Count) -ForegroundColor Yellow
    }
    Write-Host '    宪章 §7.2.1：认领分支必须推 origin，否则 claim ref 指向的提交远端不可达；机器故障这份成果就没了。' -ForegroundColor DarkGray
    Write-Host '    确认无独有内容再回收 —— 自动清理极易端掉正在跑的任务，删不删由人决定（§7.10）。' -ForegroundColor DarkGray
}

if ($nested.Count -ge 5) {
    $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    Write-Host ''
    Write-Host ("· 工作区回收：已有 {0} 个 agent 工作区" -f $nested.Count) -ForegroundColor Cyan
    foreach ($path in $nested) {
        $stamp = (& git -C $path log -1 --format=%ct 2>$null | Select-Object -First 1)
        if (-not $stamp) { $stamp = 0 }
        $ageDays = [int](($now - [int64]$stamp) / 86400)
        $name = Split-Path -Leaf $path
        $tip = ''
        if ($ageDays -ge 14) { $tip = '  ← 超过 14 天无提交，建议回收' }
        Write-Host ("    {0,-16} {1,3} 天前{2}" -f $name, $ageDays, $tip) -ForegroundColor DarkGray
    }
    Write-Host '    回收命令： pwsh -File scripts\git-done.ps1 -RemoveWorktree .worktrees\<agent>' -ForegroundColor DarkGray
}

Write-Host ''
Write-Host '✓ 工作区就绪' -ForegroundColor Green
Write-Host ("  路径   : {0}" -f $worktree)
Write-Host ("  分支   : {0}" -f $Branch)
Write-Host ("  沙箱   : {0}" -f $sandbox)
Write-Host ''
Write-Host '后续步骤：' -ForegroundColor Cyan
Write-Host ("  cd {0}" -f $worktree)
Write-Host ("  . .\.agent-env.ps1")
Write-Host ("  cmake -S . -B build -G `"Visual Studio 17 2022`" -A x64 -DCMAKE_PREFIX_PATH=<本机Qt目录>/6.8.3/msvc2022_64  # 或走 scripts/build.ps1 自动探测")
Write-Host ('  cmake --build build --config Release')
Write-Host ("  pwsh -File scripts\dev-run.ps1          # 沙箱启动，不污染真实数据")
if ($Card) { Write-Host ("  领用： {0}（进行中由台账 claims 派生并自动投递）" -f $Card) }
if ($workspaceClaimed) { Write-Host ("  占用： {0}（git-done 收尾自动释放）" -f $claimScope) }
