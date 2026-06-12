# qa_savestate.ps1 - Capture a Mednafen savestate + parse it.
#
# Pipeline:
#   1. Boot the .cue in Mednafen with the same flags as qa_boot.ps1.
#   2. Wait $SaveFrame seconds after launch (default 22s, well into title).
#   3. Bring the Mednafen window to the foreground, send F5 (the
#      keyboard scancode 62 = SDL F5 = Mednafen's default save-state
#      hotkey per the global mednafen.cfg [command.save_state] entry).
#   4. Wait briefly for Mednafen to flush the .mc0 state file to
#      $env:MEDNAFEN_HOME/mcs/ (filesys.path_state = "mcs" per default
#      Mednafen config).
#   5. Politely shut down our launched Mednafen PID (no force-kill of
#      any other mednafen.exe — Codex may run its own concurrently).
#   6. Locate the freshly-written .mc0 (newest file in mcs/).
#   7. Copy it to -Out (if given) and/or pipe through
#      tools/mcs_extract.py with -Peek/-PeekJson arguments.
#
# WHY KEYBD_EVENT NOT SendKeys: SendKeys '{F5}' delivers WM_KEYDOWN only,
# which Mednafen sometimes misses; keybd_event injects a real virtual-key
# event into the OS-level keyboard queue and is reliably picked up by
# SDL2's polling loop. Verified during Phase 1.30 capture probe.
#
# Mednafen 1.32.1 LACKS the ss.dbg_mask / ss.dbg_break_on_unknown_io
# settings, so we don't pass any debug-log flags here (see
# tools/README_debugger.md for the gap notice). The savestate path
# provides the same diagnostic power for the cases that motivated
# Phase 1.30.
#
# Phase 2.3i HARDENING (2026-05-28):
#   - Track ONLY our launched PID. Never enumerate or kill other
#     mednafen.exe instances (Codex / user may be running their own).
#   - Foreground-confirmation loop before F5: SetForegroundWindow + poll
#     GetForegroundWindow until it returns our HWND, up to 25 x 200ms.
#     Fail-fast with clear error if the focus never confirms.
#   - F5 retry with mtime verification: after each F5 stroke, poll mcs/
#     up to 3 sec for a .mc0 whose mtime is >= the pre-stroke wall-clock
#     time. Retry F5 up to 3 strokes total. Fail-fast after the 3rd
#     unsuccessful stroke.
#   - Optional -FpsScale knob: passes -fps.scale N to Mednafen so the
#     wall-clock seconds to reach SaveFrame N can be compressed. Default
#     1.0 (real time) preserves the existing behaviour.
#   - Optional -McsHome override: defaults to "$env:MEDNAFEN_HOME/mcs"
#     (set to $root/.mednafen/mcs by this script). Robust fallback paths
#     scanned if the primary doesn't have a fresh .mc0 (defensive).
#
# Usage:
#   pwsh tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 60 `
#     -Peek 0x25F800E0,0x05D00004 -Out samples/qa_state.mcs `
#     -PeekJson out.json
#
# Examples:
#   # Just capture the state at deep frame:
#   pwsh tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 60 `
#     -Out samples/qa_ghz_active.mcs
#
#   # Capture + extract SPCTL + VDP1 PTMR into JSON:
#   pwsh tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 18 `
#     -Peek 0x25F800E0,0x05D00004 -PeekJson qa_state.json

param(
    [string]$Cue        = "game.cue",
    [double]$SaveFrame  = 18.0,   # seconds after Mednafen launch to wait
                                  # before pressing F5 (must be past BIOS
                                  # + settled title; 18s matches the
                                  # verify_done.ps1 Gate 7 timing).
    [string]$Out        = "",     # optional path to copy the .mc0 to
                                  # (relative -> $root). If blank, leave
                                  # the file in $env:MEDNAFEN_HOME/mcs/.
    [string]$Peek       = "",     # comma-separated Saturn addresses
                                  # (hex 0x... accepted) to peek32.
    [string]$Peek16     = "",     # same, peek16.
    [string]$Peek8      = "",     # same, peek8.
    [string]$PeekJson   = "",     # if set, mcs_extract --json output
                                  # goes to this file.
    [switch]$Sh2mRegs,            # include SH2-Master regs in JSON.
    [switch]$Sh2sRegs,            # include SH2-Slave regs in JSON.
    [switch]$KeepRunning,         # do not auto-kill Mednafen after F5
                                  # (debug only; default OFF so verify_
                                  # done's later gates have a clean
                                  # window).
    [double]$FpsScale   = 1.0,    # Phase 2.3i: pass -fps.scale N so the
                                  # emulator runs N x realtime. SaveFrame
                                  # is still wall-clock seconds; e.g.
                                  # -SaveFrame 60 -FpsScale 1.0 = 60s
                                  # wall clock to reach Saturn frame
                                  # ~3600. Default 1.0 = real time.
    [double]$PressStartAt = 0,    # Phase 2.4j.2: if >0, inject a Saturn
                                  # START (Enter, scancode 0x1C) burst
                                  # beginning this many seconds after
                                  # launch, to drive title -> GHZ -> card
                                  # before the F5 save. Mirrors qa_boot.ps1.
    [int]$PressCount    = 8,      # number of Enter taps in the burst.
    [double]$PressEvery = 1.0,    # seconds between burst taps.
    # FR-1 (Task #178) held-input capture. After the title->GHZ Enter burst
    # and foreground confirmation, hold these PS/2 set-1 scancodes through the
    # F5 save so the player reaches a movement/action state, then release.
    #   walk     = HoldScans 0x4D (Right, extended)
    #   crouch   = HoldScans 0x50 (Down, extended)
    #   spindash = HoldScans 0x50 (Down) + TapScans 0x1E (A) tapped while held
    #   jump     = TapScans 0x1E (A) tapped once, short SettleMs (capture airborne)
    # Mednafen ss.input.port1.gamepad keymap (verified in .mednafen/mednafen.cfg):
    #   Right=SDL79 Left=SDL80 Down=SDL81 Up=SDL82 A=SDL4 -> Win set-1
    #   Right=0x4D(ext) Left=0x4B(ext) Down=0x50(ext) Up=0x48(ext) A=0x1E.
    [string]$HoldScans  = "",     # comma list of hex set-1 scancodes to hold.
    [string]$HoldExt    = "",     # comma list 0/1, extended-key flag per hold.
    [string]$TapScans   = "",     # comma list of hex scancodes tapped while held.
    [string]$TapExt     = "",     # comma list 0/1, extended-key flag per tap.
    [int]$TapCount      = 3,      # taps per TapScans entry before F5.
    [int]$SettleMs      = 700     # ms to let the game process held input
                                  # before F5 (short for jump's airborne window).
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$env:MEDNAFEN_HOME = Join-Path $root ".mednafen"
$exe = "C:\Users\gary\AppData\Local\Microsoft\WinGet\Packages\MednafenTeam.Mednafen_Microsoft.Winget.Source_8wekyb3d8bbwe\mednafen.exe"
$cuePath = Join-Path $root $Cue
if (-not (Test-Path $cuePath)) { throw "qa_savestate: cue not found: $cuePath" }
$lck = Join-Path $env:MEDNAFEN_HOME "mednafen.lck"
if (Test-Path $lck) { Remove-Item -LiteralPath $lck -Force }

$mcsDir = Join-Path $env:MEDNAFEN_HOME "mcs"
if (-not (Test-Path $mcsDir)) {
    New-Item -ItemType Directory -Path $mcsDir | Out-Null
}

# Clear any stale .mc0 so we can identify the new one unambiguously.
Get-ChildItem $mcsDir -Filter "*.mc*" -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class QaSaveState {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", SetLastError=true)] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  // Attach our input thread to the foreground-window's input thread so
  // SetForegroundWindow actually grants focus (Windows refuses to set
  // foreground across input contexts unless this is true).
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
}
"@

$mednafenArgs = @(
    '-ss.videoip','0',
    '-filesys.untrusted_fip_check','0',
    '-sound','0'
)
# Phase 2.3i: optional fps.scale to compress wall-clock-to-game-frame
# latency. 1.0 = real time. Mednafen's -fps.scale param is a float; we
# only emit it when != 1.0 to keep the default invocation byte-identical
# with the pre-2.3i behaviour (matters for the title-scene gates that
# might subtly tolerate the timing).
if ($FpsScale -ne 1.0) {
    $mednafenArgs += @('-fps.scale', ([string]::Format("{0}", $FpsScale)))
}
$mednafenArgs += $cuePath

$p = Start-Process -FilePath $exe -ArgumentList $mednafenArgs `
    -PassThru -WorkingDirectory $root
$ourPid = $p.Id

try {
    # Phase 2.4j.2: optional START burst to drive title -> GHZ -> card
    # before the F5 save. We must own the foreground for the Enter taps to
    # land in Mednafen (same constraint as F5). Acquire focus, tap Enter
    # (scancode 0x1C -> SDL_SCANCODE_RETURN -> Saturn START) on the
    # schedule, then fall through to the SaveFrame wait + F5.
    if ($PressStartAt -gt 0) {
        Start-Sleep -Milliseconds ([int]($PressStartAt * 1000))
        $hb = $p.MainWindowHandle
        if ($hb -ne [IntPtr]::Zero) {
            $myTidB = [QaSaveState]::GetCurrentThreadId()
            [uint32]$fgPidB = 0
            $fgHwndB = [QaSaveState]::GetForegroundWindow()
            $fgTidB = 0
            if ($fgHwndB -ne [IntPtr]::Zero) {
                $fgTidB = [QaSaveState]::GetWindowThreadProcessId($fgHwndB, [ref]$fgPidB)
                if ($fgTidB -ne 0 -and $fgTidB -ne $myTidB) {
                    [QaSaveState]::AttachThreadInput($myTidB, $fgTidB, $true) | Out-Null
                }
            }
            for ($bp = 0; $bp -lt $PressCount; $bp++) {
                [QaSaveState]::ShowWindow($hb, 9) | Out-Null
                [QaSaveState]::BringWindowToTop($hb) | Out-Null
                [QaSaveState]::SetForegroundWindow($hb) | Out-Null
                Start-Sleep -Milliseconds 60
                [QaSaveState]::keybd_event(0, 0x1C, 0x0008, [UIntPtr]::Zero)  # Enter down (scancode)
                Start-Sleep -Milliseconds 60
                [QaSaveState]::keybd_event(0, 0x1C, 0x000A, [UIntPtr]::Zero)  # Enter up
                Write-Output "QA inject: START tap #$($bp+1)"
                Start-Sleep -Milliseconds ([int]($PressEvery * 1000))
            }
            if ($fgTidB -ne 0 -and $fgTidB -ne $myTidB) {
                [QaSaveState]::AttachThreadInput($myTidB, $fgTidB, $false) | Out-Null
            }
        }
    }

    # Wait $SaveFrame seconds for game to reach the settled state.
    # Note: the wait is wall-clock; FpsScale compresses the game-frame
    # advance per wall-second.
    $remain = ($SaveFrame - $PressStartAt)
    if ($remain -lt 0) { $remain = 0 }
    Start-Sleep -Milliseconds ([int]($remain * 1000))

    $p.Refresh()
    if ($p.HasExited) {
        throw ("qa_savestate: Mednafen exited unexpectedly before save " +
               "(exit code $($p.ExitCode))")
    }
    $h = $p.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) {
        throw "qa_savestate: Mednafen window did not appear"
    }
    [QaSaveState]::ShowWindow($h, 9) | Out-Null
    [QaSaveState]::BringWindowToTop($h) | Out-Null

    # Phase 2.3i: foreground confirmation loop. SetForegroundWindow can
    # be silently denied if our thread doesn't share an input queue with
    # the current foreground thread; AttachThreadInput fixes that. We
    # poll GetForegroundWindow until it returns our HWND or we exhaust
    # the budget. Up to 25 attempts at 200ms = 5s total. If the window
    # genuinely never accepts focus, fail-fast — no point sending F5
    # into a background process where it lands in PowerShell or
    # whichever window owns input.
    $focusBudgetMs = 5000
    $focusPollMs   = 200
    $focused = $false
    $myTid   = [QaSaveState]::GetCurrentThreadId()
    [uint32]$fgPid = 0
    $fgHwnd = [QaSaveState]::GetForegroundWindow()
    if ($fgHwnd -ne [IntPtr]::Zero) {
        $fgTid = [QaSaveState]::GetWindowThreadProcessId($fgHwnd, [ref]$fgPid)
        if ($fgTid -ne 0 -and $fgTid -ne $myTid) {
            [QaSaveState]::AttachThreadInput($myTid, $fgTid, $true) | Out-Null
        }
    } else { $fgTid = 0 }
    $attempts = [int]($focusBudgetMs / $focusPollMs)
    for ($i = 0; $i -lt $attempts; $i++) {
        [QaSaveState]::ShowWindow($h, 9) | Out-Null
        [QaSaveState]::BringWindowToTop($h) | Out-Null
        [QaSaveState]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -Milliseconds $focusPollMs
        if ([QaSaveState]::GetForegroundWindow() -eq $h -and
            [QaSaveState]::IsWindowVisible($h)) {
            $focused = $true
            break
        }
    }
    if ($fgTid -ne 0 -and $fgTid -ne $myTid) {
        [QaSaveState]::AttachThreadInput($myTid, $fgTid, $false) | Out-Null
    }
    if (-not $focused) {
        $current = [QaSaveState]::GetForegroundWindow()
        throw ("qa_savestate: could not bring Mednafen to foreground " +
               "after ${focusBudgetMs}ms (HWND=$h, current_fg=$current). " +
               "F5 would be delivered to the wrong window; aborting.")
    }

    # FR-1 (Task #178): held-input injection. Mednafen now owns the
    # foreground (focus loop above confirmed). Hold the directional
    # scancodes, optionally tap action scancodes while held, settle, then
    # fall into the F5 stroke -- the savestate captures the held state.
    # $heldList records what we pressed-down so the finally block releases
    # them even if F5 throws.
    $script:heldList = @()
    if ($HoldScans -or $TapScans) {
        $holdArr = @(); $holdExtArr = @()
        if ($HoldScans) {
            $holdArr = $HoldScans.Split(",") | ForEach-Object { [int]($_.Trim()) }
            if ($HoldExt) {
                $holdExtArr = $HoldExt.Split(",") | ForEach-Object { [int]($_.Trim()) }
            }
        }
        for ($hi = 0; $hi -lt $holdArr.Count; $hi++) {
            $ext = if ($hi -lt $holdExtArr.Count -and $holdExtArr[$hi] -eq 1) { 0x0001 } else { 0 }
            [QaSaveState]::keybd_event(0, [byte]$holdArr[$hi], (0x0008 -bor $ext), [UIntPtr]::Zero)
            $script:heldList += ,@($holdArr[$hi], $ext)
            Write-Output ("QA hold-down scancode 0x{0:X2} (ext={1})" -f $holdArr[$hi], $ext)
            Start-Sleep -Milliseconds 40
        }
        if ($TapScans) {
            $tapArr = $TapScans.Split(",") | ForEach-Object { [int]($_.Trim()) }
            $tapExtArr = @()
            if ($TapExt) {
                $tapExtArr = $TapExt.Split(",") | ForEach-Object { [int]($_.Trim()) }
            }
            for ($tc = 0; $tc -lt $TapCount; $tc++) {
                for ($ti = 0; $ti -lt $tapArr.Count; $ti++) {
                    $te = if ($ti -lt $tapExtArr.Count -and $tapExtArr[$ti] -eq 1) { 0x0001 } else { 0 }
                    [QaSaveState]::keybd_event(0, [byte]$tapArr[$ti], (0x0008 -bor $te), [UIntPtr]::Zero)
                    Start-Sleep -Milliseconds 50
                    [QaSaveState]::keybd_event(0, [byte]$tapArr[$ti], (0x000A -bor $te), [UIntPtr]::Zero)
                }
                Start-Sleep -Milliseconds 120
            }
            Write-Output ("QA tapped {0} action scancode(s) x{1}" -f $tapArr.Count, $TapCount)
        }
        Start-Sleep -Milliseconds $SettleMs
    }

    # FR-2 #192 (deep held-input captures): a long SettleMs hold can let the
    # foreground drift away from Mednafen (focus steal / toast), so the F5
    # below would be delivered to the wrong window and never write a .mc0
    # (observed on a 60s held-Right GHZ capture: 3 F5 strokes, 0 files). The
    # pre-hold confirm loop above guarantees focus to DELIVER the held keys,
    # but not focus at F5 time AFTER the settle. Re-acquire focus here,
    # best-effort (the F5 retry loop still reports the hard error if it
    # truly cannot save), gated on a hold/tap having occurred so the
    # no-input capture path stays byte-identical (title-scene gate timing;
    # see the fps.scale note above). The held scancodes remain depressed
    # across this re-acquire (keydown was sent once; keyup is in finally).
    if ($HoldScans -or $TapScans) {
        $myTidR = [QaSaveState]::GetCurrentThreadId()
        [uint32]$fgPidR = 0
        $fgHwndR = [QaSaveState]::GetForegroundWindow()
        if ($fgHwndR -ne [IntPtr]::Zero) {
            $fgTidR = [QaSaveState]::GetWindowThreadProcessId($fgHwndR, [ref]$fgPidR)
            if ($fgTidR -ne 0 -and $fgTidR -ne $myTidR) {
                [QaSaveState]::AttachThreadInput($myTidR, $fgTidR, $true) | Out-Null
            }
        } else { $fgTidR = 0 }
        for ($ri = 0; $ri -lt 25; $ri++) {
            [QaSaveState]::ShowWindow($h, 9) | Out-Null
            [QaSaveState]::BringWindowToTop($h) | Out-Null
            [QaSaveState]::SetForegroundWindow($h) | Out-Null
            Start-Sleep -Milliseconds 200
            if ([QaSaveState]::GetForegroundWindow() -eq $h -and
                [QaSaveState]::IsWindowVisible($h)) { break }
        }
        if ($fgTidR -ne 0 -and $fgTidR -ne $myTidR) {
            [QaSaveState]::AttachThreadInput($myTidR, $fgTidR, $false) | Out-Null
        }
    }

    # Phase 2.3i: F5 retry with mtime verification. After each F5 stroke
    # we wait up to 3s for a .mc0 in mcs/ whose LastWriteTimeUtc is >=
    # the pre-stroke timestamp. If we observe one, success. Otherwise
    # retry F5; bail after 3 total strokes.
    $maxStrokes = 3
    $strokeWindowMs = 3000
    $strokePollMs = 100
    $captured = $null
    for ($stroke = 1; $stroke -le $maxStrokes; $stroke++) {
        $strokeStart = [DateTime]::UtcNow
        # F5 = VK 0x74, scancode 0x3F. Press + release.
        [QaSaveState]::keybd_event(0x74, 0x3F, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 80
        [QaSaveState]::keybd_event(0x74, 0x3F, 2, [UIntPtr]::Zero)
        # Poll mcs/ for a fresh .mc0.
        $deadline = $strokeStart.AddMilliseconds($strokeWindowMs)
        while ([DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds $strokePollMs
            $candidate = Get-ChildItem $mcsDir -Filter "*.mc*" `
                -ErrorAction SilentlyContinue |
                Where-Object { $_.LastWriteTimeUtc -ge $strokeStart -and $_.Length -gt 0 } |
                Sort-Object LastWriteTimeUtc -Descending |
                Select-Object -First 1
            if ($candidate) {
                # Mednafen may write the file in stages (open, write,
                # close). Re-check 200ms later to ensure size has stabilised
                # before we copy it.
                Start-Sleep -Milliseconds 200
                $candidate.Refresh()
                $sizeNow = $candidate.Length
                Start-Sleep -Milliseconds 200
                $candidate.Refresh()
                if ($candidate.Length -eq $sizeNow -and $candidate.Length -gt 0) {
                    $captured = $candidate
                    break
                }
            }
        }
        if ($captured) { break }
        Write-Warning ("qa_savestate: F5 stroke $stroke produced no .mc0 " +
                       "within ${strokeWindowMs}ms; retrying...")
    }

    if (-not $captured) {
        throw ("qa_savestate: no .mc* file written to $mcsDir after " +
               "$maxStrokes F5 strokes ($($strokeWindowMs * $maxStrokes / 1000)s total)")
    }
    Write-Host "qa_savestate: captured state $($captured.Name) ($($captured.Length) bytes)"
    $statePath = $captured.FullName

    if ($Out) {
        $absOut = if ([System.IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
        $absOutDir = Split-Path -Parent $absOut
        if ($absOutDir -and -not (Test-Path $absOutDir)) {
            New-Item -ItemType Directory -Path $absOutDir | Out-Null
        }
        Copy-Item $captured.FullName $absOut -Force
        Write-Host "qa_savestate: copied to $absOut"
        $statePath = $absOut
    }
} finally {
    # FR-1 (Task #178): release any held scancodes so they don't leak into
    # the desktop after Mednafen exits.
    if ($script:heldList) {
        foreach ($h in $script:heldList) {
            [QaSaveState]::keybd_event(0, [byte]$h[0], (0x000A -bor $h[1]), [UIntPtr]::Zero)
        }
    }
    # Phase 2.3i: PID-tracked shutdown. ONLY stop our launched process.
    # Never enumerate or kill other mednafen.exe (Codex may run its own).
    if (-not $KeepRunning) {
        try {
            $ourProc = Get-Process -Id $ourPid -ErrorAction SilentlyContinue
            if ($ourProc -and -not $ourProc.HasExited) {
                Stop-Process -Id $ourPid -Force -ErrorAction SilentlyContinue
            }
        } catch {
            # Process already gone; ignore.
        }
    }
}

# If any --peek/--json flag is set, invoke mcs_extract.
$extractor = Join-Path $PSScriptRoot "mcs_extract.py"
if ($Peek -or $Peek16 -or $Peek8 -or $PeekJson -or $Sh2mRegs -or $Sh2sRegs) {
    $args = @($statePath)
    foreach ($a in ($Peek -split ',' | Where-Object { $_ })) {
        $args += @('--peek', $a)
    }
    foreach ($a in ($Peek16 -split ',' | Where-Object { $_ })) {
        $args += @('--peek16', $a)
    }
    foreach ($a in ($Peek8 -split ',' | Where-Object { $_ })) {
        $args += @('--peek8', $a)
    }
    if ($Sh2mRegs) { $args += '--sh2m-regs' }
    if ($Sh2sRegs) { $args += '--sh2s-regs' }

    if ($PeekJson) {
        $args += '--json'
        $absJson = if ([System.IO.Path]::IsPathRooted($PeekJson)) {
            $PeekJson
        } else {
            Join-Path $root $PeekJson
        }
        $jsonDir = Split-Path -Parent $absJson
        if ($jsonDir -and -not (Test-Path $jsonDir)) {
            New-Item -ItemType Directory -Path $jsonDir | Out-Null
        }
        # With --json set, mcs_extract suppresses human lines and emits
        # only the JSON object. Redirect stdout directly to the output
        # file via cmd.exe (PowerShell's pipeline reformats Object[]
        # arrays in a way that mangles multi-line output for capture).
        $cmdLine = "python `"$extractor`" " + (
            ($args | ForEach-Object { '"' + $_ + '"' }) -join ' '
        ) + " > `"$absJson`""
        cmd /c $cmdLine
        $rc = $LASTEXITCODE
        if (Test-Path $absJson) {
            $sz = (Get-Item $absJson).Length
            if ($sz -gt 0) {
                Write-Host "qa_savestate: wrote JSON to $absJson ($sz bytes)"
            } else {
                Write-Warning "qa_savestate: mcs_extract emitted empty JSON"
                $rc = [Math]::Max($rc, 3)
            }
        } else {
            Write-Warning "qa_savestate: JSON file not created"
            $rc = [Math]::Max($rc, 3)
        }
        exit $rc
    } else {
        & python $extractor @args
        exit $LASTEXITCODE
    }
}
