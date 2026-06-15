# qa_motion_shots.ps1 -- capture a burst of screenshots WHILE holding Right, so
# in-motion artifacts (character blink, missing FG tiles during scroll) are
# actually witnessed (not a static idle frame). Task #241 honesty check.
param(
    [string]$Cue = "game.cue",
    [double]$Wait = 135.0,   # seconds to GHZ-live (shipping load ~110-130s)
    [int]$Shots = 16,
    [double]$Every = 0.35,
    [string]$Out = "motion.png"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$root = Split-Path -Parent $root
$env:MEDNAFEN_HOME = Join-Path $root ".mednafen"
$exe = "C:\Users\gary\AppData\Local\Microsoft\WinGet\Packages\MednafenTeam.Mednafen_Microsoft.Winget.Source_8wekyb3d8bbwe\mednafen.exe"
$lck = Join-Path $env:MEDNAFEN_HOME "mednafen.lck"
if (Test-Path $lck) { Remove-Item $lck -Force }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class QaMot {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll", SetLastError=true)] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@ -ReferencedAssemblies System.Drawing

$p = Start-Process -FilePath $exe -ArgumentList @('-ss.videoip','0','-sound','0',(Join-Path $root $Cue)) -PassThru -WorkingDirectory $root
try {
    Start-Sleep -Milliseconds ([int]($Wait * 1000))
    $h = $p.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { throw "no window" }
    # focus
    $myTid = [QaMot]::GetCurrentThreadId()
    [uint32]$fgPid = 0
    $fg = [QaMot]::GetForegroundWindow()
    $fgTid = if ($fg -ne [IntPtr]::Zero) { [QaMot]::GetWindowThreadProcessId($fg, [ref]$fgPid) } else { 0 }
    if ($fgTid -ne 0 -and $fgTid -ne $myTid) { [QaMot]::AttachThreadInput($myTid, $fgTid, $true) | Out-Null }
    for ($i = 0; $i -lt 20; $i++) {
        [QaMot]::ShowWindow($h, 9) | Out-Null; [QaMot]::BringWindowToTop($h) | Out-Null
        [QaMot]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 150
        if ([QaMot]::GetForegroundWindow() -eq $h) { break }
    }
    # hold Right (set-1 scancode 0x4D, extended)
    [QaMot]::keybd_event(0, 0x4D, (0x0008 -bor 0x0001), [UIntPtr]::Zero)
    $base = [System.IO.Path]::GetFileNameWithoutExtension($Out)
    $ext = [System.IO.Path]::GetExtension($Out)
    for ($i = 1; $i -le $Shots; $i++) {
        $r = New-Object QaMot+RECT
        [QaMot]::GetWindowRect($h, [ref]$r) | Out-Null
        $wd = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        $bmp = New-Object System.Drawing.Bitmap $wd, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc(); [QaMot]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
        $bmp.Save((Join-Path $root ("{0}_{1}{2}" -f $base, $i, $ext)), [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
        Start-Sleep -Milliseconds ([int]($Every * 1000))
    }
    [QaMot]::keybd_event(0, 0x4D, (0x000A -bor 0x0001), [UIntPtr]::Zero)  # release
    if ($fgTid -ne 0 -and $fgTid -ne $myTid) { [QaMot]::AttachThreadInput($myTid, $fgTid, $false) | Out-Null }
    Write-Output "captured $Shots motion shots"
} finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}
