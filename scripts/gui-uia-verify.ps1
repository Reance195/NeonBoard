<#
    gui-uia-verify.ps1 —— GUI 实机验收通道（DX-102）

    为什么需要它：
      按钮「看得见、点不动」这类层级问题，纯 QML 单测能测夹具、测不到真窗口；
      真窗口验收又要合成键鼠 —— 一旦目标窗口没抢到前台，点击就落到别的窗口上，
      可能误触用户正在用的应用。所以本工具把「前台守卫」放在任何输入之前：
      只有 GetForegroundWindow() 等于目标窗口句柄时才继续，否则直接退 2。

    用法：
      pwsh -File scripts/gui-uia-verify.ps1 -ProcessName NeonBoard -Name "完成"
      pwsh -File scripts/gui-uia-verify.ps1 -ProcessName NeonBoard -AutomationId "completeButton"
      pwsh -File scripts/gui-uia-verify.ps1 -SelfTest

    退出码：
      0 成功（已置前并调用目标控件）/ 1 自测失败 / 2 前台守卫或用法拒绝 /
      3 找不到目标控件 / 4 控件不支持 InvokePattern
#>
[CmdletBinding()]
param(
    [string]$ProcessName = 'NeonBoard',
    [string]$WindowTitle = '',
    [string]$AutomationId = '',
    [string]$Name = '',
    [switch]$SelfTest,
    [int]$TimeoutSec = 10
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

if (-not ('NbGuiNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NbGuiNative {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
}
'@
}

function Test-ForegroundMatch {
    param([IntPtr]$TargetHandle, [IntPtr]$ForegroundHandle)
    return ($TargetHandle -ne [IntPtr]::Zero -and $TargetHandle -eq $ForegroundHandle)
}

function Assert-Foreground {
    param([IntPtr]$TargetHandle, [IntPtr]$ForegroundHandle)
    if (-not (Test-ForegroundMatch -TargetHandle $TargetHandle -ForegroundHandle $ForegroundHandle)) {
        throw ('目标窗口不在前台（target=0x{0:X} foreground=0x{1:X}），已中止，未发送任何输入' -f
            $TargetHandle.ToInt64(), $ForegroundHandle.ToInt64())
    }
}

function Invoke-SelfTest {
    $script:guiSelfTestFailures = 0
    function Check([string]$label, [bool]$ok) {
        if ($ok) {
            Write-Host ("  ✓ " + $label)
        } else {
            Write-Host ("  ✗ " + $label) -ForegroundColor Red
            $script:guiSelfTestFailures += 1
        }
    }

    Check '前台句柄相同才放行' (Test-ForegroundMatch -TargetHandle ([IntPtr]123) -ForegroundHandle ([IntPtr]123))
    Check '前台句柄不同拒绝' (-not (Test-ForegroundMatch -TargetHandle ([IntPtr]123) -ForegroundHandle ([IntPtr]456)))
    Check '空句柄拒绝' (-not (Test-ForegroundMatch -TargetHandle ([IntPtr]0) -ForegroundHandle ([IntPtr]0)))

    $threw = $false
    try {
        Assert-Foreground -TargetHandle ([IntPtr]123) -ForegroundHandle ([IntPtr]456)
    } catch {
        $threw = $true
    }
    Check '守卫失败抛出且不继续' $threw

    if ($script:guiSelfTestFailures -gt 0) {
        Write-Host ("✗ GUI 前台守卫自测失败 {0} 项" -f $script:guiSelfTestFailures) -ForegroundColor Red
        return 1
    }
    Write-Host '✓ GUI 前台守卫自测通过（4 项）' -ForegroundColor Green
    return 0
}

if ($SelfTest) {
    exit (Invoke-SelfTest)
}

if ([string]::IsNullOrWhiteSpace($AutomationId) -and [string]::IsNullOrWhiteSpace($Name)) {
    Write-Host '✗ 必须给 -AutomationId 或 -Name 指定目标控件' -ForegroundColor Red
    exit 2
}

$deadline = (Get-Date).AddSeconds($TimeoutSec)
$proc = $null
while ((Get-Date) -lt $deadline) {
    $candidates = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero })
    if ($WindowTitle) {
        $candidates = @($candidates | Where-Object { $_.MainWindowTitle -like ('*' + $WindowTitle + '*') })
    }
    if ($candidates.Count -gt 0) {
        $proc = $candidates[0]
        break
    }
    Start-Sleep -Milliseconds 200
}
if ($null -eq $proc) {
    Write-Host ("✗ 找不到带窗口的进程 {0}（超时 {1}s）" -f $ProcessName, $TimeoutSec) -ForegroundColor Red
    exit 2
}

$hwnd = [IntPtr]$proc.MainWindowHandle
[NbGuiNative]::ShowWindow($hwnd, 9) | Out-Null
[NbGuiNative]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 300

try {
    Assert-Foreground -TargetHandle $hwnd -ForegroundHandle ([NbGuiNative]::GetForegroundWindow())
} catch {
    Write-Host ("✗ " + $_.Exception.Message) -ForegroundColor Red
    exit 2
}

$root = [System.Windows.Automation.AutomationElement]::FromHandle($hwnd)
if ($null -eq $root) {
    Write-Host '✗ 无法从窗口句柄取得 UI Automation 根元素' -ForegroundColor Red
    exit 2
}

if ($AutomationId) {
    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $AutomationId)
} else {
    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::NameProperty, $Name)
}

$element = $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
if ($null -eq $element) {
    Write-Host ("✗ 没找到目标控件（AutomationId={0} Name={1}）" -f $AutomationId, $Name) -ForegroundColor Red
    exit 3
}

$pattern = $null
if (-not $element.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$pattern)) {
    Write-Host '✗ 目标控件不支持 InvokePattern，未执行任何输入' -ForegroundColor Red
    exit 4
}

([System.Windows.Automation.InvokePattern]$pattern).Invoke()
Write-Host ("✓ 已置前并调用控件（hwnd=0x{0:X}）：{1}" -f $hwnd.ToInt64(),
    ($(if ($AutomationId) { $AutomationId } else { $Name }))) -ForegroundColor Green
exit 0
