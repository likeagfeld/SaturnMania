# qa_live.ps1 -- TURNKEY headless RetroArch live harness + full-arc monitor.
#
# The proven live-memory setup (2026-07-04): Beetle Saturn (= Mednafen's core)
# under RetroArch, driven HEADLESS so it steps the core with no window and no
# focus-pause. One command: (re)build the multi-track cue, boot headless, run the
# arc monitor. READ_CORE_RAM works; qa_arc/qa_trace/qa_aiz_camera/qa_ci read live.
#
# WHY headless: the gl video driver only steps the core while its window renders;
# a detached launch froze it. video_driver="null" (+ menu/audio null) -> RA steps
# the core on the runloop timer. And the disc MUST be the multi-track cue
# (build_cdda) -- a data-only / malformed cue gives crc32=0 + no boot in Beetle.
#
#   pwsh tools/qa_live.ps1 [-Cue game.cue] [-Secs 180] [-NoCdda] [-NoBoot]
#   ... then read live with: python tools/qa_arc.py / qa_ci.py --... / qa_aiz_camera.py --live
#   stop: taskkill /F /IM retroarch.exe
param(
  [string]$Cue  = "game.cue",
  [double]$Secs = 180,
  [switch]$NoCdda,   # skip the CD-DA multi-track cue rebuild
  [switch]$NoBoot,   # just run the monitor against an already-running headless RA
  [switch]$NoMonitor # boot only, leave RA running for manual live reads
)
$ErrorActionPreference = "SilentlyContinue"
$root = "D:\sonicmaniasaturn"
$RA   = "$root\tools\retroarch\RetroArch-Win64"

# 1. multi-track cue (Beetle needs a valid disc; build_shipping emits a malformed one)
if (-not $NoCdda) {
  Push-Location $root
  python tools/build_cdda.py cd_audio/track02.wav cd_audio/track03.wav --cue-out game.cue --iso game.iso | Out-Null
  Pop-Location
  Write-Output "qa_live: rebuilt multi-track game.cue"
}

# 2. boot RetroArch HEADLESS (config already pins video_driver=null etc.)
if (-not $NoBoot) {
  Get-Process retroarch -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 1
  Start-Process "$RA\retroarch.exe" -WorkingDirectory $RA -ArgumentList `
    "--config","$RA\retroarch.cfg","-L","$RA\cores\mednafen_saturn_libretro.dll","$root\$Cue" | Out-Null
  Write-Output "qa_live: booted headless RA ($Cue); waiting for engine init..."
  Start-Sleep -Seconds 10
}

# 3. run the full-arc monitor (or leave running for manual reads)
if (-not $NoMonitor) {
  python "$root\tools\qa_arc.py" --secs $Secs
} else {
  Write-Output "qa_live: RA left running headless. Read it: python tools/qa_arc.py ; stop: taskkill /F /IM retroarch.exe"
}
