param(
    [switch]$SkipBuild,
    # 多代理并行时把产物写进自己的 worktree，避免抢占共享的 dist/
    [string]$BuildDir = '',
    [string]$OutDir = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = if ($BuildDir) { [System.IO.Path]::GetFullPath($BuildDir) } else { Join-Path $root 'build' }
$dist = if ($OutDir) { [System.IO.Path]::GetFullPath($OutDir) } else { Join-Path $root 'dist\NeonBoard' }

# 工具定位顺序：环境变量 -> PATH -> 已知安装路径
# CI（GitHub Actions）里 Qt 由 install-qt-action 安装，cmake 来自 PATH，
# 二者都命中前两级；本机开发时 cmake / windeployqt 不在 PATH，回落到硬编码路径。
function Resolve-Tool {
    param(
        [string]$Name,
        [string]$EnvVar,
        [string[]]$Fallbacks
    )

    if ($EnvVar -and (Test-Path -LiteralPath $EnvVar)) { return $EnvVar }

    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    foreach ($fallback in $Fallbacks) {
        if ($fallback -and (Test-Path -LiteralPath $fallback)) { return $fallback }
    }

    throw "找不到 $Name，请设置对应环境变量或加入 PATH。"
}

$cmake = Resolve-Tool -Name 'cmake' -EnvVar $env:NEONBOARD_CMAKE -Fallbacks @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\CMake\bin\cmake.exe'
)

$windeployqtFallbacks = @()
if ($env:QT_ROOT_DIR) {
    $windeployqtFallbacks += (Join-Path $env:QT_ROOT_DIR 'bin\windeployqt.exe')
}
# 版本与套件目录取自工具链单点 cmake/toolchain.cmake（DX-145），不再写死
$toolchainFile = Join-Path (Split-Path -Parent $PSScriptRoot) 'cmake\toolchain.cmake'
$tcText = Get-Content -LiteralPath $toolchainFile -Raw
$tcVersion = [regex]::Match($tcText, 'set\(NB_QT_VERSION\s+"([^"]+)"\)').Groups[1].Value
$tcSuffix = [regex]::Match($tcText, 'set\(NB_QT_DIR_SUFFIX\s+"([^"]+)"\)').Groups[1].Value
$windeployqtFallbacks += "C:\Qt\$tcVersion\$tcSuffix\bin\windeployqt.exe"

$windeployqt = Resolve-Tool -Name 'windeployqt' -EnvVar $env:NEONBOARD_WINDEPLOYQT -Fallbacks $windeployqtFallbacks

if (-not $SkipBuild) {
    & $cmake --build $build --config Release
    if ($LASTEXITCODE -ne 0) { throw "Release build failed: $LASTEXITCODE" }
}

if (Test-Path -LiteralPath $dist) {
    Remove-Item -LiteralPath $dist -Recurse -Force
}
New-Item -ItemType Directory -Path $dist -Force | Out-Null
# 构建产物位置：Visual Studio 多配置构建在 Release\，Ninja 单配置直接在 build\
$exePath = Join-Path $build 'Release\NeonBoard.exe'
if (-not (Test-Path -LiteralPath $exePath)) {
    $exePath = Join-Path $build 'NeonBoard.exe'
}
$hostExePath = Join-Path $build 'Release\NeonBoardHost.exe'
if (-not (Test-Path -LiteralPath $hostExePath)) {
    $hostExePath = Join-Path $build 'NeonBoardHost.exe'
}

Copy-Item -LiteralPath $exePath -Destination $dist
Copy-Item -LiteralPath $hostExePath -Destination $dist

$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
# 程序在 main.cpp 里写死 QQuickStyle::setStyle("Basic")，运行时只加载 Basic 一套样式。
# windeployqt 默认会把 Fusion/Imagine/Material/Universal 及各自的 styleimpl 一并打进包，
# 这些 DLL 永远不会被 load —— 白白增大包体、拖慢便携包解压与 Defender 首扫。这里显式排除，
# 只留 Basic（quickcontrols2basic / *basicstyleimpl 不在排除列表，默认保留）。
$wdqtArgs = @(
    '--release', '--qmldir', (Join-Path $root 'qml'), '--compiler-runtime',
    # R0-14：QSG_RHI_BACKEND 默认 d3d11（main.cpp 未设环境变量时强制），
    # 软件 OpenGL 后备（opengl32sw.dll 约 20MB）永不加载，不随包。
    '--no-opengl-sw',
    '--no-quickcontrols2fusion', '--no-quickcontrols2fusionstyleimpl',
    '--no-quickcontrols2imagine', '--no-quickcontrols2imaginestyleimpl',
    '--no-quickcontrols2material', '--no-quickcontrols2materialstyleimpl',
    '--no-quickcontrols2universal', '--no-quickcontrols2universalstyleimpl',
    '--no-quickcontrols2fluentwinui3styleimpl', '--no-quickcontrols2windowsstyleimpl',
    (Join-Path $dist 'NeonBoard.exe')
)
& $windeployqt @wdqtArgs 2>&1 | ForEach-Object { Write-Host $_ }
$deployExit = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction
if ($deployExit -ne 0) { throw "windeployqt failed: $deployExit" }

# R0-14：运行时裁剪。只删「运行期从不加载」的件，每一项都有运行期证据，不是拍脑袋：
#   qmltooling/       = qmldbg_* QML 调试器插件，只在 QT_QML_DEBUG 调试会话激活，生产路径碰不到；
#   sqldrivers        = 代码只用 QSQLITE（Database.cpp addDatabase），mimer/odbc/psql 无处可加载；
#   QuickEffects      = qml/ 里零 import QtQuick.Effects；
#   QuickShapes       = qml/ 里零 import QtQuick.Shapes；
#   LabsFolderListModel = qml/ 里零 import（check_dist 最小集注释也明确不含它）。
# Qt6Network.dll 与 tls/ 不在裁剪之列：单实例机制（QLocalServer）启动期就要 Network，
# DELAYLOAD 已被 LNK1194 否决（见 docs/iteration-roadmap.md §3.1），它们是活依赖。
$pruneItems = @(
    @{ Path = Join-Path $dist 'qmltooling'; Desc = 'qmltooling (qmldbg_* 调试器插件)' },
    @{ Path = Join-Path $dist 'sqldrivers\qsqlmimer.dll'; Desc = 'sqldrivers/qsqlmimer' },
    @{ Path = Join-Path $dist 'sqldrivers\qsqlodbc.dll'; Desc = 'sqldrivers/qsqlodbc' },
    @{ Path = Join-Path $dist 'sqldrivers\qsqlpsql.dll'; Desc = 'sqldrivers/qsqlpsql' },
    @{ Path = Join-Path $dist 'Qt6QuickEffects.dll'; Desc = 'Qt6QuickEffects' },
    @{ Path = Join-Path $dist 'Qt6QuickShapes.dll'; Desc = 'Qt6QuickShapes' },
    @{ Path = Join-Path $dist 'Qt6LabsFolderListModel.dll'; Desc = 'Qt6LabsFolderListModel' }
)
foreach ($item in $pruneItems) {
    if (Test-Path -LiteralPath $item.Path) {
        Remove-Item -LiteralPath $item.Path -Recurse -Force
        Write-Host "Pruned runtime-dead item: $($item.Desc)"
    }
}

# windeployqt 的 --no-<style> 只排除样式 DLL，QtQuick/Controls 下各样式的 QML 模块目录
# 仍会被 --qmldir 扫描带进来（FluentWinUI3 一个就 5.9MB / 836 文件）。既然只用 Basic，
# 这里把未用样式的 QML 目录一并删掉：Basic 与共享 impl 保留，其余整目录移除。
$controlsDir = Join-Path $dist 'qml\QtQuick\Controls'
if (Test-Path -LiteralPath $controlsDir) {
    foreach ($style in 'Fusion','Imagine','Material','Universal','FluentWinUI3','Windows') {
        $styleDir = Join-Path $controlsDir $style
        if (Test-Path -LiteralPath $styleDir) {
            Remove-Item -LiteralPath $styleDir -Recurse -Force
            Write-Host "Pruned unused style: $style"
        }
    }
}

# ---- 插件（R14-02）：官方插件与示例插件是两批独立内容，都要进包 ----
# 以前这里只拷 plugins-samples —— 官方 6 个（agent-bridge / agent-claude / agent-codex /
# agent-cursor / agent-opencode / plan-hook）一个都没进包。后果不是「少几个可选插件」：
# agent-bridge 缺席意味着「agent 事件自动建卡」整条链路在安装版上从未工作过。
# 这类错误不会让构建变红，只会让功能静默少一半，所以下面紧跟一道对齐自检。
$plugins = Join-Path $dist 'plugins'
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -Path (Join-Path $root 'plugins\*') -Destination $plugins -Recurse -Force
Copy-Item -Path (Join-Path $root 'plugins-samples\*') -Destination $plugins -Recurse -Force

# 对齐自检（DX-29）：清单在 scripts/check_dist.py 里，一份实现三处复用
# （deploy 打包后 / CI 上传 artifact 前 / release 压缩前），判据不会各写一套。
# 它不只查插件：exe、Qt 关键 DLL、platforms、sqldrivers、qml 模块根、0 字节文件
# 都在同一张清单里 —— 那几类缺失的故障是「装完直接打不开」，比少几个插件更严重。
# 这里不复制清单逻辑，也不容忍失败：产物不完整就不该进 dist。
$checkDist = Join-Path $root 'scripts\check_dist.py'
& python $checkDist --dir $dist
if ($LASTEXITCODE -ne 0) {
    throw ("打包产物未通过清单校验（退出码 $LASTEXITCODE）。" +
        "缺项见上方输出，判定口径见 docs/pitfalls.md「凡拷文件环节都必须用产物验收」。")
}

Write-Host "Portable build ready: $dist"
