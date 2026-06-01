# qa_gate.ps1 - one-command "MANDATORY reference-render diff" gate (QA.md gate 8).
#
# Captures the title view from game.cue with Mednafen and diffs it against
# tools/refs/title_view.golden.png. Exits 0 on PASS, 1 on FAIL -- so this can
# be wired into the build pipeline / pre-commit / CI to STOP regressions from
# landing. This is the gate that catches the jo CRAM off-by-one palette shift
# (and any other Saturn-render divergence from the verified-good baseline) that
# eyeballing misses.
#
# Why a title view: the title state in main.c holds until START is pressed, so
# the FG/sky/sprite are static and the capture is deterministic regardless of
# BIOS-clear timing variance. Don't change the title's behaviour without
# updating the golden too.
#
#   pwsh tools/qa_gate.ps1                        # use defaults
#   pwsh tools/qa_gate.ps1 -Wait 22 -Out qa.png   # tune capture timing
#   pwsh tools/qa_gate.ps1 -UpdateGolden          # overwrite golden after a
#                                                 # VERIFIED visual change
#                                                 # (DO NOT use to paper over
#                                                 # a regression you can't
#                                                 # explain)
param(
    [string]$Cue          = "game.cue",
    [int]$Wait            = 24,
    [string]$Out          = "qa_gate.png",
    [string]$Golden       = "tools\refs\title_view.golden.png",
    [string]$MaskRect     = "75,165,115,205",     # masks Sonic's title pose
    [double]$MeanThresh   = 12.0,
    [double]$P95Thresh    = 120.0,
    [switch]$UpdateGolden
)
# Wait=24: lands ~5s into title state, well past TitleSonic's full
# entrance arc (177 ticks = ~3s), so we capture the settled pose
# (atlas frame 48).  Was 22 before the 49-frame atlas integration; at
# Wait=22 the capture could land before frame 48 settled, producing
# arc-mid-flight captures that varied between runs.

# HARD GUARD (binding, memory title-needs-15s-load-before-capture.md):
# The title scene needs AT LEAST ~15s of emulated boot (SEGA security
# splash + jo asset load) before content renders. A capture taken
# earlier lands on the BIOS/SEGA splash or a blank-blue pre-render and
# is FALSELY diagnosed as a title regression. This has cost the user
# real time repeatedly. Refuse any title capture below the floor so it
# is IMPOSSIBLE to forget.
$TITLE_WAIT_FLOOR = 20
if ($Wait -lt $TITLE_WAIT_FLOOR) {
    Write-Error ("qa_gate: -Wait $Wait is below the title-load floor of " +
        "$TITLE_WAIT_FLOOR s. The title needs >=15s to load; capturing " +
        "earlier grabs the SEGA splash / blank-blue and reads as a false " +
        "regression. Use -Wait 24 (default) or higher.")
    exit 2
}

$root = Split-Path -Parent $PSScriptRoot
$capture = if ($UpdateGolden) { Join-Path $root $Golden } else { Join-Path $root $Out }

# 1. Capture title frame
& pwsh -File (Join-Path $PSScriptRoot 'qa_boot.ps1') -Cue $Cue -Wait $Wait -Shots 1 -Out (Split-Path -Leaf $capture)
if ($LASTEXITCODE -ne 0) { Write-Error "qa_gate: capture failed"; exit 1 }

# When updating the golden, also move the file to the golden path (qa_boot
# writes to repo root by default).
if ($UpdateGolden) {
    $captured = Join-Path $root (Split-Path -Leaf $capture)
    $dst      = Join-Path $root $Golden
    if ($captured -ne $dst) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $dst) -Force | Out-Null
        Move-Item -LiteralPath $captured -Destination $dst -Force
    }
    Write-Host "qa_gate: golden updated -> $Golden" -ForegroundColor Yellow
    exit 0
}

# 2. Diff against golden
$shot = Join-Path $root $Out
$diffOut = Join-Path $root "qa_diff.png"
python (Join-Path $PSScriptRoot 'qa_refdiff.py') $shot `
    --golden (Join-Path $root $Golden) `
    --mask-rect $MaskRect `
    --mean-thresh $MeanThresh `
    --p95-thresh $P95Thresh `
    --out $diffOut
$rc = $LASTEXITCODE

if ($rc -eq 0) {
    Write-Host "qa_gate: PASS (title reference-diff)" -ForegroundColor Green
} else {
    Write-Host "qa_gate: FAIL (heatmap: qa_diff.png) -- shot diverges from golden." -ForegroundColor Red
    Write-Host "        If the divergence is INTENDED (deliberate visual change),"   -ForegroundColor Red
    Write-Host "        re-run with -UpdateGolden after verifying the new look is correct." -ForegroundColor Red
}
exit $rc
