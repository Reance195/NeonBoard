# nb-paths.ps1 —— NeonBoard 落盘位置在 PowerShell 侧的唯一入口
#
# 应用的数据目录由 C++ 侧 AppPaths::dataDir() 决定：设了 NEONBOARD_DATA_DIR 就用沙箱，
# 否则落 %APPDATA%\com.lin\NeonBoard。脚本要读 board.db / neonboard.log 一律点源本文件，
# 不要自己拼路径 —— 写死 %APPDATA% 有两重后果：沙箱运行读不到本次日志（验收假失败），
# 没沙箱时又会去动真实实例正占用的文件（verify.ps1 曾被 Remove-Item 直接打断）。
#
# Python 侧的同一口径写在 scripts/claim-card.py（跨语言共用不了一份实现），
# 两处都由 scripts/check_script_hygiene.py 盯住，不许在别处再拼一次。

function Get-NbDataDir {
    if ($env:NEONBOARD_DATA_DIR) { return $env:NEONBOARD_DATA_DIR }
    return Join-Path $env:APPDATA 'com.lin\NeonBoard'
}

function Get-NbLogFile { return Join-Path (Get-NbDataDir) 'neonboard.log' }

function Get-NbBoardDb { return Join-Path (Get-NbDataDir) 'board.db' }
