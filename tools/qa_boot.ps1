# qa_boot.ps1 - Automated Saturn boot-validation gate.
# Boots a .cue/.iso in Mednafen (accurate Saturn core) with real BIOS, waits for
# the program to come up past the BIOS security screen, screen-captures the
# emulator window to a PNG, then terminates. Used as the final QA gate for every
# build so we confirm it renders on Saturn (not just compiles).
#
#   pwsh tools/qa_boot.ps1 -Cue game.cue -Wait 2 -Every 0.25 -Shots 120 -Out qa.png
#
# QA RULE (learned the angry way -- twice):
#   Capture from BIOS boot, not after it.  The interesting action -- engine
#   bring-up garbage, title -> game transition, the first ~1-2 seconds of
#   gameplay (where most physics bugs surface) -- happens BEFORE Wait=22 and
#   at < 1 fps you miss it entirely.  Default below covers BIOS + title +
#   ~10s of gameplay at 4 fps so every transient is captured.
param(
    [string]$Cue   = "game.cue",
    [double]$Wait  = 2.0,           # start capturing 2s after Mednafen launches
                                    # (during BIOS) so the boot+title+game arc
                                    # is fully covered
    [string]$Out   = "qa_boot.png",
    [int]$Shots    = 1,             # default 1 shot for legacy callers; QA
                                    # gates should pass Shots>=120 + Every=0.25
    [double]$Every = 2.0,
    [string]$SoundRecord = "",      # if set, Mednafen records audio to this WAV;
                                    # used by verify_done.ps1 Gate 7 (audio
                                    # presence)
    [switch]$Silent,                 # legacy `-sound 0` mute (default OFF now)
    [double]$PressStartAt = 0,      # if >0, inject Saturn START (Enter,
                                    # scancode 0x1C) once elapsed capture time
                                    # >= this many seconds. Drives the title's
                                    # TS_WAIT_FOR_ENTER -> TS_TRANSITION_TO_GHZ
                                    # path (Game.c:2005-2017) so an UNATTENDED
                                    # capture reaches the GHZ act-intro
                                    # (TitleCard). The card is a pure timed
                                    # state machine (no input skip), so the
                                    # press burst below is harmless once in GHZ.
    [int]$PressCount = 6,           # number of Enter presses in the burst
    [double]$PressEvery = 1.5       # seconds between burst presses (each press
                                    # also coincides with a capture-loop tick)
)

$root = Split-Path -Parent $PSScriptRoot
$env:MEDNAFEN_HOME = Join-Path $root ".mednafen"
$exe = "C:\Users\gary\AppData\Local\Microsoft\WinGet\Packages\MednafenTeam.Mednafen_Microsoft.Winget.Source_8wekyb3d8bbwe\mednafen.exe"
$cuePath = Join-Path $root $Cue
$outPath = Join-Path $root $Out

if (-not (Test-Path $cuePath)) { throw "cue not found: $cuePath" }
$lck = Join-Path $env:MEDNAFEN_HOME "mednafen.lck"
if (Test-Path $lck) { Remove-Item -LiteralPath $lck -Force }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class QaCap {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  // PrintWindow with flag PW_RENDERFULLCONTENT (0x2) copies a window's
  // visible client area into a DC regardless of whether it's occluded by
  // other windows -- essential for our QA captures, otherwise a stray
  // Discord/Slack/notification toast in front of Mednafen's window region
  // contaminates the screenshot. Flag 0x3 = client area + render fully.
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  // keybd_event with KEYEVENTF_SCANCODE injects a raw hardware scancode, which
  // is what SDL2 (Mednafen's input backend) reads -- Mednafen maps Saturn START
  // to "keyboard 0x0 40" (SDL_SCANCODE_RETURN). Enter's PC/XT make code is 0x1C;
  // SDL translates 0x1C -> SCANCODE_RETURN(40). down: flags 0x0008; up: 0x000A.
  [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
  public static void TapEnter() {
    keybd_event(0, 0x1C, 0x0008, UIntPtr.Zero);            // KEYEVENTF_SCANCODE down
    System.Threading.Thread.Sleep(140);                    // hold ~8 frames so the
                                                           // 60Hz input poll samples
                                                           // the down edge
    keybd_event(0, 0x1C, 0x000A, UIntPtr.Zero);            // SCANCODE | KEYUP
  }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

# -ss.videoip 0 disables Mednafen's bilinear interpolation so captures are
# crisp (point-sampled), avoiding false colour-fringe artifacts in QA
# screen checks.
# -filesys.untrusted_fip_check 0 allows the CUE to reference its audio bin
# via a relative path (e.g. cd_audio/track02.bin) -- Mednafen 1.32+ flags
# these as "potentially unsafe" by default which makes the standard multi-
# track CUE format fail. We trust our own build output; turn the check off.
# Audio: enabled by default (was -sound 0 previously, which silenced the
# whole capture; that was the cause of "I don't hear any sound" reports).
# Use -Silent to opt back into mute. Use -SoundRecord <path.wav> to
# record audio (Gate 7).
$mednafenArgs = @('-ss.videoip','0','-filesys.untrusted_fip_check','0')
if ($Silent) {
    $mednafenArgs += @('-sound','0')
}
if ($SoundRecord) {
    $absWav = if ([System.IO.Path]::IsPathRooted($SoundRecord)) { $SoundRecord } else { Join-Path $root $SoundRecord }
    if (Test-Path $absWav) { Remove-Item -LiteralPath $absWav -Force }
    # Mednafen 1.32 ArgumentList parsing: an array element is passed as one
    # argv element, so DO NOT double-quote even if the path contains spaces.
    # Embedded literal quotes break the path lookup ("can't open ...wav").
    $mednafenArgs += @('-soundrecord', $absWav)
}
$mednafenArgs += $cuePath
$p = Start-Process -FilePath $exe -ArgumentList $mednafenArgs -PassThru -WorkingDirectory $root
# Window acquisition (hardened 2026-05-28). A fixed Start-Sleep followed by a
# single MainWindowHandle resolve is RACY: at low -Wait the OS may not have
# laid out Mednafen's window yet, so GetWindowRect returns a zero rect and the
# capture throws "window not found". That silently breaks EVERY QA gate that
# calls this script (verify_done Gates 3.5/6/7/V*, qa_gate, manual probes) and
# was the #1 reason title QA became flaky enough to skip. Fix: sleep the
# requested -Wait (this still sets WHEN the first capture lands in the boot
# arc), THEN poll up to 25s for a handle whose GetWindowRect is non-zero,
# refreshing the process each iteration. Throw only if the window genuinely
# never appears. Capturing "immediately from BIOS, continuously" (e.g.
# -Wait 2 -Every 0.333) is now robust regardless of host load.
Start-Sleep -Milliseconds ([int]($Wait * 1000))
$h = [IntPtr]::Zero
$acqDeadline = (Get-Date).AddSeconds(25)
while ((Get-Date) -lt $acqDeadline) {
    if ($p.HasExited) { throw "mednafen exited during window acquisition (exit $($p.ExitCode))" }
    $p.Refresh()
    $cand = $p.MainWindowHandle
    if ($cand -ne [IntPtr]::Zero) {
        $rr = New-Object QaCap+RECT
        [QaCap]::GetWindowRect($cand, [ref]$rr) | Out-Null
        if (($rr.Right - $rr.Left) -gt 0 -and ($rr.Bottom - $rr.Top) -gt 0) { $h = $cand; break }
    }
    Start-Sleep -Milliseconds 150
}
if ($h -eq [IntPtr]::Zero) {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    throw "window not found (Mednafen window never appeared with a non-zero rect within 25s of -Wait $Wait)"
}
[QaCap]::ShowWindow($h, 9) | Out-Null
[QaCap]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700
$base = [System.IO.Path]::GetFileNameWithoutExtension($Out)
$ext  = [System.IO.Path]::GetExtension($Out)
$pressesSent = 0
$nextPressAt = $PressStartAt
for ($i = 1; $i -le $Shots; $i++) {
    # Elapsed capture time = $Wait + (i-1)*$Every (matches when frame i lands in
    # the boot arc). Inject the START burst once we cross each press threshold.
    if ($PressStartAt -gt 0 -and $pressesSent -lt $PressCount) {
        $elapsed = $Wait + ($i - 1) * $Every
        if ($elapsed -ge $nextPressAt) {
            [QaCap]::SetForegroundWindow($h) | Out-Null
            Start-Sleep -Milliseconds 60
            [QaCap]::TapEnter()
            $pressesSent++
            $nextPressAt += $PressEvery
            Write-Output "QA inject: START press #$pressesSent at t~$([math]::Round($elapsed,1))s"
        }
    }
    $r = New-Object QaCap+RECT
    [QaCap]::GetWindowRect($h, [ref]$r) | Out-Null
    $wd = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($wd -le 0 -or $ht -le 0) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }; throw "window not found" }
    $name = if ($Shots -eq 1) { $Out } else { "$base`_$i$ext" }
    $shotPath = Join-Path $root $name
    $bmp = New-Object System.Drawing.Bitmap $wd, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    # PrintWindow direct copy bypasses the screen compositor, so whatever
    # window is overlapping Mednafen doesn't pollute the screenshot.
    # Flag 0x2 = PW_RENDERFULLCONTENT (Win 8.1+, falls back to legacy if
    # unsupported). If the call returns false, fall back to CopyFromScreen.
    $hdc = $g.GetHdc()
    $ok = [QaCap]::PrintWindow($h, $hdc, 2)
    $g.ReleaseHdc($hdc)
    if (-not $ok) {
        $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $wd, $ht))
    }
    $bmp.Save($shotPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "QA capture saved: $name (${wd}x${ht})"
    if ($i -lt $Shots) { Start-Sleep -Milliseconds ([int]($Every * 1000)) }
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
