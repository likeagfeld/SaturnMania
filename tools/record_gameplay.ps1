# record_gameplay.ps1 - Capture a video+audio recording of the built ISO running
# in Mednafen, from boot, with scripted autoplay input so Sonic moves through GHZ.
#
#   pwsh tools/record_gameplay.ps1 -Cue game.cue -Duration 60 -Out gameplay.mov
#
# How it works:
#  - Launches Mednafen with -qtrecord <out.mov>; Mednafen writes a QuickTime
#    movie (vcodec cscd = CamStudio lossless) with a 48kHz stereo PCM audio
#    track. Recording begins the instant emulation starts (covers the BIOS +
#    title boot arc the user asked to see).
#  - Injects Saturn pad input by emitting raw PC/XT set-1 scancodes via
#    keybd_event(KEYEVENTF_SCANCODE). SDL2 (Mednafen's input backend) maps the
#    hardware scancode to an SDL scancode; the .mednafen/mednafen.cfg pad map is:
#       START = SDL 40 (Enter)      -> set-1 0x1C
#       RIGHT = SDL  7 ('D')        -> set-1 0x20
#       A/jump= SDL 89 (Keypad 1)   -> set-1 0x4F
#       exit  = SDL 69 (F12)        -> set-1 0x58  (command.exit)
#  - Timeline: press START a few times around the title->GHZ transition, then
#    HOLD RIGHT and tap JUMP periodically so Sonic runs/hops through the level.
#  - Stops Mednafen via WM_CLOSE (SDL_QUIT) so the QuickTime moov atom is
#    finalised. Force-killing leaves an unplayable "moov atom not found" file.
param(
    [string]$Cue        = "game.cue",
    [double]$Duration   = 60.0,   # total wall-clock seconds to record
    [string]$Out        = "gameplay.mov",
    [double]$StartAt    = 22.0,   # first START press (title needs ~15-24s boot)
    [int]   $StartCount = 3,      # number of START presses
    [double]$StartEvery = 2.0,    # seconds between START presses
    [double]$MoveAt     = 30.0,   # begin holding RIGHT at this time
    [double]$JumpEvery  = 2.5     # tap JUMP this often while moving
)

$root = Split-Path -Parent $PSScriptRoot
$env:MEDNAFEN_HOME = Join-Path $root ".mednafen"
$exe = "C:\Users\gary\AppData\Local\Microsoft\WinGet\Packages\MednafenTeam.Mednafen_Microsoft.Winget.Source_8wekyb3d8bbwe\mednafen.exe"
$cuePath = Join-Path $root $Cue
$outPath = Join-Path $root $Out
if (-not (Test-Path $cuePath)) { throw "cue not found: $cuePath" }
$lck = Join-Path $env:MEDNAFEN_HOME "mednafen.lck"
if (Test-Path $lck) { Remove-Item -LiteralPath $lck -Force }
if (Test-Path $outPath) { Remove-Item -LiteralPath $outPath -Force }

Add-Type @"
using System; using System.Runtime.InteropServices;
public class RecInput {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
  // Flags inlined (NOT const fields): PowerShell member lookup is case-
  // insensitive, so a 'DOWN' const collides with the 'Down' method and makes
  // [RecInput]::Down unresolvable. 0x0008 = KEYEVENTF_SCANCODE (down),
  // 0x000A = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP (up).
  public static void Down(byte scan){ keybd_event(0, scan, 0x0008, UIntPtr.Zero); }
  public static void Up(byte scan){ keybd_event(0, scan, 0x000A, UIntPtr.Zero); }
  public static void Tap(byte scan, int holdMs){ Down(scan); System.Threading.Thread.Sleep(holdMs); Up(scan); }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

[byte]$SC_START = 0x1C
[byte]$SC_RIGHT = 0x20
[byte]$SC_JUMP  = 0x4F
[byte]$SC_EXIT  = 0x58

$args = @('-qtrecord', $outPath, '-ss.videoip','0','-filesys.untrusted_fip_check','0', $cuePath)
$p = Start-Process -FilePath $exe -ArgumentList $args -PassThru -WorkingDirectory $root
Write-Output "Launched Mednafen (pid $($p.Id)); recording to $Out"

# Acquire the window (poll up to 25s for a non-zero rect).
$h = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds(25)
while ((Get-Date) -lt $deadline) {
    if ($p.HasExited) { throw "mednafen exited during window acquisition (exit $($p.ExitCode))" }
    $p.Refresh(); $cand = $p.MainWindowHandle
    if ($cand -ne [IntPtr]::Zero) {
        $rr = New-Object RecInput+RECT
        [RecInput]::GetWindowRect($cand, [ref]$rr) | Out-Null
        if (($rr.Right - $rr.Left) -gt 0 -and ($rr.Bottom - $rr.Top) -gt 0) { $h = $cand; break }
    }
    Start-Sleep -Milliseconds 150
}
if ($h -eq [IntPtr]::Zero) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }; throw "Mednafen window never appeared" }
[RecInput]::ShowWindow($h, 9) | Out-Null
[RecInput]::SetForegroundWindow($h) | Out-Null

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$startsSent = 0
$nextStart  = $StartAt
$nextJump   = $MoveAt
$moving     = $false

while ($sw.Elapsed.TotalSeconds -lt $Duration) {
    $t = $sw.Elapsed.TotalSeconds
    [RecInput]::SetForegroundWindow($h) | Out-Null

    if ($startsSent -lt $StartCount -and $t -ge $nextStart) {
        [RecInput]::Tap($SC_START, 140)
        $startsSent++; $nextStart += $StartEvery
        Write-Output ("t={0:N1}s  START #{1}" -f $t, $startsSent)
    }

    if (-not $moving -and $t -ge $MoveAt) {
        [RecInput]::Down($SC_RIGHT)     # hold RIGHT for the rest of the run
        $moving = $true
        Write-Output ("t={0:N1}s  RIGHT held (run)" -f $t)
    }

    if ($moving -and $t -ge $nextJump) {
        [RecInput]::Tap($SC_JUMP, 140)
        $nextJump += $JumpEvery
        Write-Output ("t={0:N1}s  JUMP" -f $t)
    }

    Start-Sleep -Milliseconds 200
}

if ($moving) { [RecInput]::Up($SC_RIGHT) }   # release before quitting

# Clean shutdown so the QuickTime moov atom is written.
$p.CloseMainWindow() | Out-Null
for ($k=0; $k -lt 30 -and -not $p.HasExited; $k++) { Start-Sleep -Milliseconds 500 }
if (-not $p.HasExited) {
    [RecInput]::SetForegroundWindow($h) | Out-Null
    [RecInput]::Tap($SC_EXIT, 140)         # fallback: command.exit (F12)
    for ($k=0; $k -lt 20 -and -not $p.HasExited; $k++) { Start-Sleep -Milliseconds 500 }
}
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "WARNING: had to force-kill; .mov may be unfinalised" }
else { Write-Output "Mednafen exited cleanly (code $($p.ExitCode))" }
Start-Sleep 1
Get-ChildItem $outPath | Select-Object Name, Length
