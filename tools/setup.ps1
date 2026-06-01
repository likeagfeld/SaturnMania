# setup.ps1 - Turn a fresh SaturnMania clone into a buildable tree.
#
#   pwsh tools/setup.ps1            # extract Data.rsdk + build the ISO
#   pwsh tools/setup.ps1 -SkipBuild # extract only (no Docker build)
#
# You must supply your OWN legally-obtained Sonic Mania PC `Data.rsdk` at the
# repo root. No copyrighted game data ships in this repository (see SETUP.md).
#
# What this does, in order:
#   1. Verify prerequisites (python, docker, the Data.rsdk you provide).
#   2. Extract your Data.rsdk into extracted/ (RSDKv5 datapack unpack).
#   3. Invoke build.bat, which regenerates the runtime asset subset and
#      builds game.iso. (See the "Honest limitation" section of SETUP.md:
#      a clean-clone build is not yet guaranteed to regenerate every static
#      cd/ visual asset; that converter consolidation is a tracked follow-up.)
[CmdletBinding()]
param(
    [string]$DataRsdk = "",        # path to your Data.rsdk (default: repo-root Data.rsdk)
    [switch]$SkipBuild             # extract only; skip the Docker make + ISO step
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot   # repo root (tools/.. )

function Fail($msg) { Write-Host "[setup] ERROR: $msg" -ForegroundColor Red; exit 1 }
function Info($msg) { Write-Host "[setup] $msg" -ForegroundColor Cyan }

# --- 1. Prerequisites -----------------------------------------------------
Info "Checking prerequisites..."

if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Fail "python not found on PATH. Install Python 3.10+ and retry."
}
if (-not $SkipBuild -and -not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Fail "docker not found on PATH. Install Docker Desktop (or pass -SkipBuild)."
}

if ([string]::IsNullOrWhiteSpace($DataRsdk)) {
    $DataRsdk = Join-Path $root "Data.rsdk"
}
if (-not (Test-Path -LiteralPath $DataRsdk)) {
    Fail @"
Data.rsdk not found at: $DataRsdk

Place your own legally-obtained Sonic Mania (PC/Steam) Data.rsdk at the repo
root, or pass its path with -DataRsdk <path>. No game data ships in this repo;
see SETUP.md.
"@
}
$sz = (Get-Item -LiteralPath $DataRsdk).Length
if ($sz -lt 100MB) {
    Fail "Data.rsdk at $DataRsdk is only $([math]::Round($sz/1MB,1)) MB; expected ~175 MB. Wrong/corrupt file?"
}
Info "Found Data.rsdk ($([math]::Round($sz/1MB,1)) MB)."

# --- 2. Extract -----------------------------------------------------------
$filelist = Join-Path $root "tools\maniafilelist.txt"
if (-not (Test-Path -LiteralPath $filelist)) {
    Fail "Extraction filelist missing: $filelist (should be tracked in the repo)."
}
$extractOut = Join-Path $root "extracted"
Info "Extracting Data.rsdk -> $extractOut (this can take a minute)..."
python (Join-Path $root "tools\rsdk_extract.py") $DataRsdk --filelist $filelist --out $extractOut
if ($LASTEXITCODE -ne 0) { Fail "rsdk_extract.py failed (exit $LASTEXITCODE)." }

$dataDir = Join-Path $extractOut "Data"
if (-not (Test-Path -LiteralPath $dataDir)) {
    Fail "Extraction produced no $dataDir. Check tools/maniafilelist.txt against your Data.rsdk version."
}
Info "Extraction complete."

# --- 3. Build -------------------------------------------------------------
if ($SkipBuild) {
    Info "-SkipBuild set; stopping after extraction. Run build.bat to produce game.iso."
    exit 0
}

# Ensure the Docker build image exists (build it once if missing).
$img = docker images -q joengine-saturn:latest 2>$null
if ([string]::IsNullOrWhiteSpace($img)) {
    Info "Docker image joengine-saturn:latest not found; building it from Dockerfile..."
    docker build -t joengine-saturn:latest $root
    if ($LASTEXITCODE -ne 0) { Fail "docker build failed (exit $LASTEXITCODE)." }
}

Info "Running build.bat (regenerates runtime assets + builds game.iso)..."
Info "NOTE: see SETUP.md 'Honest limitation' -- a clean clone may still need the"
Info "      remaining static-asset converters listed there to fully populate cd/."
& (Join-Path $root "build.bat")
if ($LASTEXITCODE -ne 0) { Fail "build.bat failed (exit $LASTEXITCODE). See SETUP.md asset-converter map." }

$iso = Join-Path $root "game.iso"
if (Test-Path -LiteralPath $iso) {
    Info "Done. Built: $iso  ->  run with: mednafen game.cue"
} else {
    Fail "build.bat exited 0 but game.iso is missing. Check the build log."
}
