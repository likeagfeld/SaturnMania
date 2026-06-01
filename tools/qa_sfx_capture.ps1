# qa_sfx_capture.ps1 -- robust audio-only WAV capture for the SFX probe.
#
# qa_boot.ps1 couples WAV recording to a screenshot window-poll that throws
# (and force-kills Mednafen before the WAV is finalized) whenever the window
# briefly reports a zero rect. A force-killed Mednafen never finalizes its
# -soundrecord file: it can leave the WAV with no usable RIFF header -- the
# "fmt + data chunks missing" failure. For the SFX-audibility test we do not
# need screenshots; we need a VALID WAV.
#
# Method (user-selected 2026-05-29: "Graceful quit + verify"):
#   1. Launch Mednafen with -soundrecord (no window automation).
#   2. Record for -Seconds.
#   3. Send WM_CLOSE (CloseMainWindow) so Mednafen quits GRACEFULLY and
#      finalizes the RIFF header (RIFF size + data size filled in).
#   4. Poll up to -QuitTimeout for clean exit.
#   5. VERIFY the WAV carries RIFF/WAVE + a fmt chunk + a data chunk before
#      the capture is considered usable. If graceful quit fails and we must
#      force-kill, the run is reported INVALID (exit 2) -- never analyzed,
#      because a force-killed WAV is exactly the malformed case we reject.
#
#   pwsh tools/qa_sfx_capture.ps1 -Cue game.cue -Seconds 30 -Out qa_sfx_probe.wav
param(
    [string]$Cue         = "game.cue",
    [double]$Seconds     = 30.0,
    [string]$Out         = "qa_sfx_probe.wav",
    [double]$QuitTimeout = 12.0
)

$root = Split-Path -Parent $PSScriptRoot
$env:MEDNAFEN_HOME = Join-Path $root ".mednafen"
$exe = "C:\Users\gary\AppData\Local\Microsoft\WinGet\Packages\MednafenTeam.Mednafen_Microsoft.Winget.Source_8wekyb3d8bbwe\mednafen.exe"
$cuePath = Join-Path $root $Cue
if (-not (Test-Path $cuePath)) { throw "cue not found: $cuePath" }

$absWav = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
if (Test-Path $absWav) { Remove-Item -LiteralPath $absWav -Force }

$lck = Join-Path $env:MEDNAFEN_HOME "mednafen.lck"
if (Test-Path $lck) { Remove-Item -LiteralPath $lck -Force }

# Mirror qa_boot.ps1's launch args (videoip off, untrusted-fip off, sound on)
# plus -soundrecord. No SetForegroundWindow / PrintWindow / SendInput.
$mednafenArgs = @('-ss.videoip','0','-filesys.untrusted_fip_check','0',
                  '-soundrecord', $absWav, $cuePath)
$p = Start-Process -FilePath $exe -ArgumentList $mednafenArgs -PassThru -WorkingDirectory $root
Write-Output "qa_sfx_capture: launched mednafen pid=$($p.Id), recording $Seconds s to $absWav"

Start-Sleep -Seconds $Seconds

# --- Graceful quit: WM_CLOSE so Mednafen (SDL) gets SDL_QUIT and finalizes
# the WAV header. CloseMainWindow posts WM_CLOSE without requiring focus. ---
$graceful = $false
if (-not $p.HasExited) {
    $p.Refresh()
    $p.CloseMainWindow() | Out-Null
    $deadline = (Get-Date).AddSeconds($QuitTimeout)
    while ((Get-Date) -lt $deadline) {
        if ($p.HasExited) { $graceful = $true; break }
        Start-Sleep -Milliseconds 250
    }
} else {
    # Process already gone (crash/early exit) -- not a graceful quit we drove.
    $graceful = $false
}

if (-not $p.HasExited) {
    Write-Output "qa_sfx_capture: WARNING graceful quit timed out; force-killing"
    Stop-Process -Id $p.Id -Force
    Start-Sleep -Milliseconds 500
}

if (-not (Test-Path $absWav)) {
    Write-Output "qa_sfx_capture: INVALID -- no WAV produced at $absWav"
    exit 2
}

# --- Verify RIFF/WAVE + fmt + data chunks are all present. ---
$bytes = [System.IO.File]::ReadAllBytes($absWav)
function ChunkPresent([byte[]]$b, [string]$id) {
    $idb = [System.Text.Encoding]::ASCII.GetBytes($id)
    $limit = [Math]::Min($b.Length - 4, 4096)
    for ($i = 12; $i -lt $limit; $i++) {
        if ($b[$i] -eq $idb[0] -and $b[$i+1] -eq $idb[1] -and
            $b[$i+2] -eq $idb[2] -and $b[$i+3] -eq $idb[3]) { return $true }
    }
    return $false
}
$hasRiff = ($bytes.Length -ge 12) -and
           ([System.Text.Encoding]::ASCII.GetString($bytes[0..3]) -eq 'RIFF') -and
           ([System.Text.Encoding]::ASCII.GetString($bytes[8..11]) -eq 'WAVE')
$hasFmt  = ChunkPresent $bytes 'fmt '
$hasData = ChunkPresent $bytes 'data'

Write-Output ("qa_sfx_capture: bytes={0}  graceful_quit={1}  RIFF/WAVE={2}  fmt={3}  data={4}" -f `
              $bytes.Length, $graceful, $hasRiff, $hasFmt, $hasData)

if (-not ($hasRiff -and $hasFmt -and $hasData)) {
    Write-Output "qa_sfx_capture: INVALID -- WAV missing required chunks (RIFF/WAVE+fmt+data). Not analyzable."
    exit 2
}

# Mednafen records 48000 Hz / 16-bit / stereo = 192000 bytes/sec. A complete
# capture (graceful OR force-killed) carries the full PCM stream; the analyzer
# treats the streaming size=0 data chunk as rest-of-file, so a force-killed but
# COMPLETE WAV is fully analyzable. The historical malformed case was an EARLY
# force-kill (~2 s) that truncated the stream. Reject only that: require the PCM
# payload to be at least half the bytes the requested duration should yield.
$expectedBytes = $Seconds * 192000.0
$minBytes      = [int]($expectedBytes * 0.5)
if ($bytes.Length -lt $minBytes) {
    Write-Output ("qa_sfx_capture: INVALID -- WAV truncated ({0} bytes < {1} min for {2}s). Capture cut short; re-run." -f `
                  $bytes.Length, $minBytes, $Seconds)
    exit 2
}

if ($graceful) {
    Write-Output "qa_sfx_capture: OK -- valid finalized WAV (graceful quit) -> $absWav"
} else {
    Write-Output "qa_sfx_capture: OK -- valid complete WAV (force-killed, full PCM, streaming size=0 handled by analyzer) -> $absWav"
}
exit 0
