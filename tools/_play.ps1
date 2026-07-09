# _play.ps1 -- launch RetroArch (Beetle Saturn core) at REAL-TIME speed with sound, in a
# GL window, for USER observation. Unlike the headless qa_live.ps1 (unthrottled, 6x for QA),
# this uses observe_override.cfg's real-time lock (audio_sync + fastforward_ratio 1.0 + vsync)
# so the emulation runs at normal speed. Uses the same multi-track game.cue.
#   pwsh tools/_play.ps1            # rebuild cue from current game.iso, then launch real-time
#   pwsh tools/_play.ps1 -NoCdda    # skip the cue rebuild (iso unchanged)
param([switch]$NoCdda)
$ErrorActionPreference = "SilentlyContinue"
$root = "D:\sonicmaniasaturn"
$RA   = "$root\tools\retroarch\RetroArch-Win64"
Get-Process retroarch -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
if (-not $NoCdda) {
  Push-Location $root
  python tools/build_cdda.py cd_audio/track02.wav cd_audio/track03.wav --cue-out game.cue --iso game.iso | Out-Null
  Pop-Location
  Write-Output "_play: rebuilt multi-track game.cue"
}
Start-Process "$RA\retroarch.exe" -WorkingDirectory $RA -ArgumentList `
  "--config","$RA\retroarch.cfg","--appendconfig","$RA\observe_override.cfg", `
  "-L","$RA\cores\mednafen_saturn_libretro.dll","$root\game.cue"
Write-Output "_play: launched RetroArch at REAL-TIME (audio-synced, 1x cap). Watch the window; stop with taskkill /F /IM retroarch.exe"
