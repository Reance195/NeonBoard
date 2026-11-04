
# DX-39：启动闪屏「有没有被冻住」的截帧探针。
#
# 为什么需要它：启动画面卡顿这件事，肉眼判不准 —— 「看着卡」分不清是动画真的停了，
# 还是只是这一段工作本来就慢。判据只能是**逐帧比对**：启动期间连续采样闪屏窗口的
# 像素，同一画面持续 ≥ FreezeMs 就是冻结（动画停了）。
#
# 实测基线（2026-11-05，改动前）：闪屏存活约 1.9s，其中 686ms + 543ms 两段逐像素
# 完全静止，总计约 1229ms —— 启动画面约 64% 的时间是冻住的。渲染搬到专属线程后，
# 这两段应当消失（主线程再阻塞，动画照走）。
#
# 探针本身踩过的坑（DX-39）：
#   1) 闪屏窗口销毁后，**HWND 会被主窗口复用**。只按 hwnd 判断「窗口还在不在」，
#      会一路跟到主窗口上 —— 主窗口内容一直在动，于是永远测不出冻结（假阴性）。
#      所以锁定时记下窗口类名与标题，之后每帧都要核对「同一个 hwnd + 同一个类名」。
#   2) 用 PNG 编码算哈希太慢（每帧 ~40ms），采样周期比冻结阈值还长，
#      根本分辨不出来。改成直接 LockBits 取原始像素算哈希，采样周期压到 ~15ms。
#
# 用法：
#   pwsh -File scripts/probe-splash-frames.ps1 -Exe build/NeonBoard.exe
#   pwsh -File scripts/probe-splash-frames.ps1 -Exe build/NeonBoard.exe -MaxFreezeTotalMs 150
#
# 退出码：0 = 通过（冻结总时长在阈值内）/ 1 = 不通过（冻太久）/ 2 = 探针自身出错
#   （没抓到闪屏窗口、exe 不存在等）。

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [int]$DurationMs = 5000,
    [int]$SampleMs = 15,
    # 一段画面连续多久不变算「冻结」
    [int]$FreezeMs = 120,
    # 冻结总时长上限（超过即判定不通过）
    [int]$MaxFreezeTotalMs = 150,
    # 数据目录：默认用临时目录，不碰开发机真实数据
    [string]$DataDir = '',
    # 可选：把一份真实 board.db 拷进沙箱，让数据量与真实启动一致
    [string]$SeedDb = '',
    # 可选：Qt 的 bin 目录（闪屏窗口要用 Qt 运行库）。不填则自动找。
    [string]$QtBin = ''
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public class NbProbe {
  [StructLayout(LayoutKind.Sequential)]
  public struct RECT { public int L, T, R, B; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  // PER_MONITOR_AWARE_V2：不设的话 GetWindowRect 返回的是**虚拟化**坐标 ——
  // 1.5x 屏上采样区只有窗口的左上角一块，冻结判据就只覆盖了窗口的一部分。
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
  public static void MakeDpiAware() { SetProcessDpiAwarenessContext(new IntPtr(-4)); }
  public static List<IntPtr> VisibleOf(uint want) {
    var list = new List<IntPtr>();
    EnumWindows((h, p) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == want && IsWindowVisible(h)) list.Add(h);
      return true;
    }, IntPtr.Zero);
    return list;
  }
  public static RECT RectOf(IntPtr h) { RECT r; GetWindowRect(h, out r); return r; }
  public static string ClassOf(IntPtr h) { var s = new StringBuilder(160); GetClassNameW(h, s, 160); return s.ToString(); }
  public static string TitleOf(IntPtr h) { var s = new StringBuilder(160); GetWindowTextW(h, s, 160); return s.ToString(); }
}
"@

[NbProbe]::MakeDpiAware()

# Qt 运行库：闪屏是 Qt 程序，PATH 里没有 Qt6Core.dll 就起不来（表现为「一个窗口都抓不到」）。
function Resolve-QtBin {
    param([string]$Explicit)
    if ($Explicit) { return $Explicit }
    if ($env:QT_BIN) { return $env:QT_BIN }
    foreach ($root in @('C:\Qt', 'C:\Qt6')) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Get-ChildItem -LiteralPath $_.FullName -Directory -Filter 'msvc*_64' -ErrorAction SilentlyContinue } |
            ForEach-Object { $bin = Join-Path $_.FullName 'bin'; if (Test-Path (Join-Path $bin 'Qt6Core.dll')) { $bin } } |
            Select-Object -First 1
    }
}
$qtBin = Resolve-QtBin -Explicit $QtBin
if ($qtBin) {
    $env:PATH = "$qtBin;" + $env:PATH
    Write-Host "Qt 运行库：$qtBin"
} else {
    Write-Host "⚠ 找不到 Qt 的 bin 目录（可用 -QtBin 指定）；闪屏可能起不来" -ForegroundColor Yellow
}

if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Host "找不到可执行文件：$Exe" -ForegroundColor Red
    exit 2
}
$exePath = (Resolve-Path -LiteralPath $Exe).Path

if (-not $DataDir) {
    $DataDir = Join-Path ([System.IO.Path]::GetTempPath()) ("nb-splash-probe-" + [Guid]::NewGuid().ToString('N'))
}
New-Item -ItemType Directory -Force -Path $DataDir | Out-Null
if ($SeedDb) {
    if (-not (Test-Path -LiteralPath $SeedDb)) {
        Write-Host "找不到种子数据库：$SeedDb" -ForegroundColor Red
        exit 2
    }
    Copy-Item -LiteralPath $SeedDb -Destination (Join-Path $DataDir 'board.db') -Force
}

$env:NEONBOARD_DATA_DIR = $DataDir
$env:NEONBOARD_ROADMAP = Join-Path $DataDir 'progress.json'
$env:NEONBOARD_ROADMAP_AUTOWRITE = '0'

$proc = Start-Process -FilePath $exePath -PassThru
$sw = [Diagnostics.Stopwatch]::StartNew()
$frames = New-Object System.Collections.Generic.List[object]
$locked = [IntPtr]::Zero
$lockedClass = ''
$lockedTitle = ''
$lockedRect = $null
$sawWindows = 0
$exited = $false

function Get-ScreenHash([int]$x, [int]$y, [int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $g.Dispose()
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    $bmp.Dispose()
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = [System.BitConverter]::ToString($sha.ComputeHash($bytes))
    $sha.Dispose()
    return $hash.Substring(0, 16)
}

while ($sw.ElapsedMilliseconds -lt $DurationMs) {
    if ($proc.HasExited) { $exited = $true; break }
    if ($locked -eq [IntPtr]::Zero) {
        # 还没锁定窗口：挑面积最小的那个 —— 闪屏（700x430）比主窗口小得多
        $handles = [NbProbe]::VisibleOf([uint32]$proc.Id)
        $sawWindows = [Math]::Max($sawWindows, $handles.Count)
        $best = $null; $bestArea = [int]::MaxValue; $bestHandle = [IntPtr]::Zero
        foreach ($h in $handles) {
            $r = [NbProbe]::RectOf($h)
            $area = ($r.R - $r.L) * ($r.B - $r.T)
            if ($area -gt 0 -and $area -lt $bestArea) { $bestArea = $area; $best = $r; $bestHandle = $h }
        }
        if ($best) {
            $locked = $bestHandle
            $lockedRect = $best
            $lockedClass = [NbProbe]::ClassOf($bestHandle)
            $lockedTitle = [NbProbe]::TitleOf($bestHandle)
        }
    } else {
        # 窗口身份校验：闪屏销毁后，HWND 可能被主窗口复用 ——
        # 主窗口和闪屏**类名、标题都一样**（都是 Qt 窗口、都叫 NeonBoard），
        # 只看类名/标题拦不住。闪屏是固定尺寸居中的，所以再加一条：矩形不能变。
        $alive = [NbProbe]::VisibleOf([uint32]$proc.Id).Contains($locked)
        if (-not $alive) { break }
        if ([NbProbe]::ClassOf($locked) -ne $lockedClass -or [NbProbe]::TitleOf($locked) -ne $lockedTitle) {
            break
        }
        $now = [NbProbe]::RectOf($locked)
        $moved = ([Math]::Abs($now.L - $lockedRect.L) -gt 2) -or
                 ([Math]::Abs($now.T - $lockedRect.T) -gt 2) -or
                 ([Math]::Abs($now.R - $lockedRect.R) -gt 2) -or
                 ([Math]::Abs($now.B - $lockedRect.B) -gt 2)
        if ($moved) { break }
    }

    if ($lockedRect) {
        $w = $lockedRect.R - $lockedRect.L
        $h = $lockedRect.B - $lockedRect.T
        if ($w -gt 0 -and $h -gt 0) {
            $frames.Add([pscustomobject]@{
                t = $sw.ElapsedMilliseconds
                hash = Get-ScreenHash $lockedRect.L $lockedRect.T $w $h
            })
        }
    }
    Start-Sleep -Milliseconds $SampleMs
}

if (-not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit(3000) | Out-Null }

if ($frames.Count -lt 3) {
    Write-Host "✗ 没抓到闪屏画面（采样 $($frames.Count) 帧）：" -ForegroundColor Red
    Write-Host "   应用是否已退出：$exited（退出码 $($proc.ExitCode)）" -ForegroundColor Red
    Write-Host "   期间最多看到 $sawWindows 个可见窗口" -ForegroundColor Red
    Write-Host "   是不是用了 --smoke（不显示闪屏）？" -ForegroundColor Red
    exit 2
}
$SamplingCount = $frames.Count

# 把连续同帧的区间切成「静止段」
$runs = New-Object System.Collections.Generic.List[object]
$runStart = 0
for ($i = 1; $i -lt $frames.Count; $i++) {
    if ($frames[$i].hash -ne $frames[$i - 1].hash) {
        $runs.Add([pscustomobject]@{ startMs = $frames[$runStart].t; lenMs = $frames[$i - 1].t - $frames[$runStart].t })
        $runStart = $i
    }
}
$runs.Add([pscustomobject]@{ startMs = $frames[$runStart].t; lenMs = $frames[$frames.Count - 1].t - $frames[$runStart].t })

$frozen = @($runs | Where-Object { $_.lenMs -ge $FreezeMs })
$frozenTotal = ($frozen | Measure-Object -Property lenMs -Sum).Sum
if ($null -eq $frozenTotal) { $frozenTotal = 0 }
$span = $frames[$frames.Count - 1].t - $frames[0].t
$distinct = @($frames | Select-Object -ExpandProperty hash -Unique).Count

Write-Host ""
Write-Host "闪屏窗口：$($lockedClass) / $($lockedTitle)" -ForegroundColor Cyan
Write-Host "闪屏矩形：$($lockedRect.L),$($lockedRect.T) $(($lockedRect.R - $lockedRect.L))x$(($lockedRect.B - $lockedRect.T))" -ForegroundColor Cyan
Write-Host "闪屏存活窗口：$($frames[0].t)ms → $($frames[$frames.Count - 1].t)ms（跨度 $span ms）" -ForegroundColor Cyan
Write-Host "采样 $SamplingCount 帧 / 不同画面 $distinct 个" -ForegroundColor Cyan
if ($frozen.Count -gt 0) {
    Write-Host "冻结段（连续 ≥ $FreezeMs ms 画面无变化）：" -ForegroundColor Yellow
    foreach ($f in $frozen) {
        Write-Host ("   {0}ms 起，持续 {1}ms" -f $f.startMs, $f.lenMs) -ForegroundColor Yellow
    }
} else {
    Write-Host "冻结段：无" -ForegroundColor Green
}
Write-Host ("冻结总时长：{0} ms（阈值 {1} ms）" -f $frozenTotal, $MaxFreezeTotalMs) -ForegroundColor Cyan

if ($frozenTotal -gt $MaxFreezeTotalMs) {
    Write-Host "✗ 不通过：启动画面冻住的时间超标" -ForegroundColor Red
    exit 1
}
Write-Host "✓ 通过：启动画面没有长时间冻住" -ForegroundColor Green
exit 0
