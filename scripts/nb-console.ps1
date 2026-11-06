# nb-console.ps1 —— 控制台 UTF-8 编码的单一实现（DX-77 / DX-83）
#
# 为什么收口到这里：同一类「按控制台代码页解释 UTF-8 字节」的坑已经炸了三轮——
#   DX-60 build.ps1（chcp 65001 保 CMake file(READ) 字节保真）、
#   DX-77 verify.ps1 / nbflow.py（CP 936 控制台直读 UTF-8 输出成 mojibake）、
#   DX-83 git-done / agent-init（捕获 python 子进程的 UTF-8 输出按 GBK 解码再转发）。
# 编码设置在四个入口各写一遍就是四份漂移的副本（Z5/Z6），收口成一个函数；
# 入口脚本点源调用，漏接的由 check_script_hygiene 规则 F 拦住。
#
# 一次管三跳，缺一不可：
#   chcp 65001                 —— 控制台代码页：子进程（CMake file(READ)）与直写控制台的
#                                 字节按 65001 解释（DX-60 的原始动机）；
#   [Console]::OutputEncoding  —— PowerShell 解码 native 子进程 stdout 的口径
#                                 （pitfalls §2.20 的病根）；
#   [Console]::InputEncoding   —— PowerShell 向 native 子进程喂输入的编码。
# 副作用与 DX-60 相同：宿主控制台代码页被留在 65001 —— 本仓库脚本与日志全按
# UTF-8，这是更对的状态。没有控制台的场合（计划任务 / CI 服务会话）设置会抛
# IOException，捕获后告警放行（fail-soft，Z4）：本函数管的是显示与捕获的体验，
# build.ps1 的依赖前缀逐字节核对才是硬门，切换失败在那一步会当场拦下。

function Set-NbUtf8Console {
    # 幂等：同一进程内重复调用只生效一次（入口脚本可能嵌套调用）。
    # Get-Variable 探测而非直读：宿主普遍 Set-StrictMode，直读未初始化变量会抛错。
    if ($null -ne (Get-Variable -Name NbUtf8ConsoleSet -Scope Script -ErrorAction SilentlyContinue)) { return }
    $script:NbUtf8ConsoleSet = $true
    try {
        # ① 控制台代码页。cmd /d /c 包一层：无控制台 / chcp 失败时 stderr 不至于
        #    撞上调用方的 $ErrorActionPreference='Stop'（pitfalls §2.3）。
        $null = & cmd.exe /d /c "chcp 65001>nul 2>&1"
        # ② PS 解码 native 子进程 stdout；③ PS 向 native 子进程喂输入。
        #    没有控制台时这两行抛 IOException，由下面的 catch 告警放行。
        [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
        [Console]::InputEncoding = [System.Text.Encoding]::UTF8
    } catch {
        Write-Warning ("控制台无法切到 UTF-8（{0}）：非交互环境可忽略；中文显示/捕获可能乱码。" -f $_.Exception.Message)
    }
}
