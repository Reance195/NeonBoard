#Requires -Version 5.1
<#
.SYNOPSIS
    本地串一遍增量发布链路（发布前自测，不进 CI）

.DESCRIPTION
    调 scripts/build-delta.ps1 生成 delta.zip，再调 scripts/build-update-manifest.ps1
    把它写进清单，最后断言 artifacts 里确实有 kind=delta 且 fromVersion / sha256 正确。

    diff、bsdiff、打包、清单条目各自只有一份实现（那两个脚本），本文件不重复拼
    neonboard-delta-tool 命令行 —— 它只回答「发布链路串起来通不通」。

    所有产物落在 .sandbox/delta-test/（已在 .gitignore 内），不污染仓库与真实 dist/。

.EXAMPLE
    pwsh -File scripts/test-delta.ps1

.EXAMPLE
    # 用真实的两版目录跑（更接近发布态）
    pwsh -File scripts/test-delta.ps1 -OldDir dist/NeonBoard-0.1.3 -NewDir dist/NeonBoard
#>
[CmdletBinding()]
param(
    [string]$FromVersion = '0.1.3',
    # 新版本号：留空则取 CMakeLists 的 project VERSION（与发布口径同一个来源）
    [string]$ToVersion = '',
    # 旧版目录：留空则从新版合成一个（少一个文件 + 改一个文件，凑出两类差异）
    [string]$OldDir = '',
    # 新版目录：留空则依次尝试 dist/NeonBoard、build/Release、build
    [string]$NewDir = '',
    # neonboard-delta-tool.exe 路径：留空则交给 build-delta.ps1 自动探测
    [string]$Tool = ''
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $repoRoot
try {
    if (-not $ToVersion) {
        $raw = Get-Content -LiteralPath 'CMakeLists.txt' -Raw -Encoding UTF8
        $m = [regex]::Match($raw, 'project\s*\(\s*NeonBoard\s+VERSION\s+([0-9][0-9A-Za-z.\-]*)')
        if (-not $m.Success) { throw '无法从 CMakeLists.txt 解析版本号' }
        $ToVersion = $m.Groups[1].Value
    }

    if (-not $NewDir) {
        foreach ($cand in @('dist/NeonBoard', 'build/Release', 'build')) {
            if (Test-Path -LiteralPath $cand) { $NewDir = $cand; break }
        }
    }
    if (-not $NewDir -or -not (Test-Path -LiteralPath $NewDir)) {
        throw '没找到新版目录：先跑 scripts/deploy.ps1 出便携包，或用 -NewDir 指定'
    }

    # 会话产物只往 .sandbox 落 —— 仓库根多一个未跟踪目录，就足以把锚点快进同步长期卡住
    $work = Join-Path $repoRoot '.sandbox/delta-test'
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $work -Force | Out-Null

    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host " 增量发布链路自测：$FromVersion -> $ToVersion" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    if (-not $OldDir) {
        $OldDir = Join-Path $work "NeonBoard-$FromVersion"
        Copy-Item -LiteralPath (Resolve-Path $NewDir) -Destination $OldDir -Recurse
        # 少一个文件 -> delta 里是一条 Added；改一个文件末字节 -> 一条 Modified
        $all = @(Get-ChildItem -LiteralPath $OldDir -File -Recurse | Where-Object { $_.Length -gt 0 })
        $mutate = $all | Select-Object -Skip 1 | Where-Object { $_.Length -gt 1024 } | Select-Object -First 1
        if ($all.Count -lt 2 -or -not $mutate) {
            throw "新版目录里没有足够文件来合成两类差异：$NewDir"
        }
        Remove-Item -LiteralPath $all[0].FullName -Force
        $bytes = [System.IO.File]::ReadAllBytes($mutate.FullName)
        $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 0xFF
        [System.IO.File]::WriteAllBytes($mutate.FullName, $bytes)
        Write-Host "✓ 合成旧版：$OldDir（少 $($all[0].Name)、改 $($mutate.Name)）" -ForegroundColor Green
    }

    $deltaOut = Join-Path $work 'delta'
    $deltaArgs = @{
        FromDir     = (Resolve-Path $OldDir).Path
        ToDir       = (Resolve-Path $NewDir).Path
        FromVersion = $FromVersion
        ToVersion   = $ToVersion
        OutDir      = $deltaOut
    }
    if ($Tool) { $deltaArgs['Tool'] = $Tool }
    & scripts\build-delta.ps1 @deltaArgs

    $deltaZip = Join-Path $deltaOut 'delta.zip'
    if (-not (Test-Path -LiteralPath $deltaZip)) { throw "build-delta.ps1 未产出 delta.zip：$deltaZip" }

    # build-delta.ps1 把 delta.json 与 files/ patches/ 放在 <OutDir>/staging，zip 根即该目录
    $deltaJson = Join-Path (Join-Path $deltaOut 'staging') 'delta.json'
    if (-not (Test-Path -LiteralPath $deltaJson)) { throw "未找到 delta.json：$deltaJson" }

    # ChangeType 取值见 src/update/DeltaUpdate.h：0=Added 1=Modified 2=Deleted
    $info = Get-Content -LiteralPath $deltaJson -Raw -Encoding UTF8 | ConvertFrom-Json
    $changes = @($info.changes)
    $added = @($changes | Where-Object { $_.type -eq 0 }).Count
    $modified = @($changes | Where-Object { $_.type -eq 1 }).Count
    $deleted = @($changes | Where-Object { $_.type -eq 2 }).Count
    $patched = @($changes | Where-Object { $_.type -eq 1 -and $_.patchPath }).Count
    Write-Host ("· delta.json：共 {0} 条（added {1} / modified {2}，其中 bsdiff {3} / deleted {4}）" -f `
        $changes.Count, $added, $modified, $patched, $deleted) -ForegroundColor Gray
    if ($added -eq 0 -or $modified -eq 0) {
        throw "合成差异没同时覆盖新增与修改（added=$added modified=$modified），这次测试不算数"
    }

    $manifestOut = Join-Path $work 'update-delta-test.json'
    & scripts\build-update-manifest.ps1 -Version $ToVersion `
        -DeltaZip $deltaZip -DeltaFromVersion $FromVersion -OutFile $manifestOut

    $manifest = Get-Content -LiteralPath $manifestOut -Raw -Encoding UTF8 | ConvertFrom-Json
    $deltaItems = @($manifest.artifacts | Where-Object { $_.kind -eq 'delta' })
    if ($deltaItems.Count -ne 1) {
        throw "清单里应有 1 条 delta 条目，实际 $($deltaItems.Count)：$manifestOut"
    }
    if ($deltaItems[0].fromVersion -ne $FromVersion) {
        throw "fromVersion 应为 $FromVersion，实际 $($deltaItems[0].fromVersion)"
    }
    $expectHash = (Get-FileHash -LiteralPath $deltaZip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($deltaItems[0].sha256 -ne $expectHash) { throw 'delta.sha256 与包体实测值不一致' }

    Write-Host ("✓ 清单条目：kind=delta  fromVersion={0}  size={1:N1} KB" -f `
        $deltaItems[0].fromVersion, ($deltaItems[0].size / 1KB)) -ForegroundColor Green
    Write-Host "`n✅ 增量发布链路自测通过（$FromVersion -> $ToVersion）" -ForegroundColor Green
    Write-Host "   产物：$deltaZip" -ForegroundColor DarkGray
    Write-Host "   清单：$manifestOut" -ForegroundColor DarkGray
} finally {
    Pop-Location
}
