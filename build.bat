@echo off
REM Build Sonic Mania (Saturn) inside the joengine-saturn Docker image,
REM then auto-run the QA reference-diff gate (qa_gate.ps1) to catch render
REM regressions (e.g., palette shifts) before they ship.
REM Produces game.iso / game.cue in this directory.
REM
REM Pass --skip-qa to run the docker build only (skip the gate). Pass any
REM other args directly through to `make`.

setlocal
set SKIP_QA=0
REM Capture pass-through args for `make`. NOTE: cmd.exe `shift` does NOT
REM update %*, so we cannot consume --skip-qa with `shift` and then forward
REM %* to make -- that would forward --skip-qa to GNU make (which errors with
REM a usage dump and never compiles). Build an explicit EXTRA_ARGS list that
REM excludes the consumed --skip-qa token instead. (Pre-existing build.bat
REM bug surfaced Phase 2.4g.3: verify_done's `build.bat --skip-qa` invocation
REM forwarded --skip-qa to make, so no .o ever recompiled and the cue stayed
REM single-track.)
set "EXTRA_ARGS="
:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--skip-qa" (
  set SKIP_QA=1
) else (
  set "EXTRA_ARGS=%EXTRA_ARGS% %~1"
)
shift
goto parse_args
:args_done

REM %~dp0 ends with a trailing backslash; docker mounts treat "...\":/work
REM as an escaped quote and fail. Strip the trailing slash.
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

REM Phase 3.2 + 3.2b REVERTED 2026-05-28 — MENUSCN1.BIN staging removed.

REM Phase 2.4f (Task #143) — extract canonical Mania-mode GHZ Act 1 Player
REM spawn coord from extracted/Data/Stages/GHZ/Scene1.bin into
REM cd/GHZ1SPWN.BIN (12 B big-endian: i32 x, i32 y, u32 characterID).
REM Decomp authority: Player_Create reads entity->position from the
REM Scene1.bin entity-table dispatch (Scene.cpp:528-665 LoadSceneAssets ->
REM ProcessObjects). The Saturn engine layer currently bypasses
REM rsdk_load_scene for GHZ, so we ship the coord as a build artefact.
python "%ROOT%\tools\extract_ghz_spawn.py" --act 1
if errorlevel 1 ( exit /b %ERRORLEVEL% )

REM Phase 2.4g.1 (Task #153) — stage the GHZ Act 1 scene bin for the RSDK
REM scene loader. memory/ghz-pivot-to-rsdk-engine.md: GHZ entities now
REM spawn from the canonical Scene1.bin object table via rsdk_load_scene
REM ("GHZ" -> GHZSCN1.BIN per src/rsdk/scene.c build_scene_path) instead
REM of bespoke GHZ1*.BIN coord files. Shipped verbatim (no transform) so
REM the runtime parser sees the exact decomp asset.
copy /Y "%ROOT%\extracted\Data\Stages\GHZ\Scene1.bin" "%ROOT%\cd\GHZSCN1.BIN" >nul
if errorlevel 1 ( exit /b %ERRORLEVEL% )

REM Phase 2.4i (Task #154) -- regenerate AUTHENTIC HUD + SFX assets from
REM extracted/Data so no fabricated asset can drift back in. Per
REM memory/decomp-assets-only-no-synthesis.md every shipped pixel/sample
REM traces to the Data.rsdk extraction. The fabricating scripts
REM (make_digit_font.py / make_audio.py / make_object_sprites.py) were
REM DELETED; these two steps replace them.
REM   - cd/HUD.SP2 + cd/HUD.MET  <- extracted/Data/Sprites/Global/HUD.bin
REM     (build_entity_atlas.py MANIFEST "HUD": HUD Elements + Numbers 0-9
REM      + Life Icons; 30 frames, fits ENTITY_ATLAS_MAX_FRAMES=34).
python "%ROOT%\tools\build_entity_atlas.py" --bin "%ROOT%\extracted\Data\Sprites\Global\HUD.bin" --spr "%ROOT%\cd\HUD.SP2" --met "%ROOT%\cd\HUD.MET" --drop "Player Name" --drop "Got Through" --drop "Act" --drop "Game Over" --drop "Time Over" --drop "Competition" --drop "Hyper Numbers"
if errorlevel 1 ( exit /b %ERRORLEVEL% )
REM NOTE: the CLI --drop path does not apply the Numbers frame cap, so
REM run the manifest entry through the --all-equivalent inline call below
REM to enforce the 10-decimal-digit Numbers cap (build_entity_atlas.py
REM MANIFEST "caps":{"Numbers":10}).
python -c "import sys; sys.path.insert(0, r'%ROOT%\tools'); import build_entity_atlas as b; b.build_atlas(r'%ROOT%\extracted\Data\Sprites\Global\HUD.bin', r'%ROOT%\cd\HUD.SP2', r'%ROOT%\cd\HUD.MET', drop_anims=['Player Name','Got Through','Act','Game Over','Time Over','Competition','Hyper Numbers'], frame_caps={'Numbers':10})"
if errorlevel 1 ( exit /b %ERRORLEVEL% )

REM Phase 2.4j.1 (Task #156) -- TitleCard (GHZ act-intro card). Ship ALL
REM 36 frames (drop=[]) of every anim (Decorations / Name Letters /
REM Zone Letters / Act Numbers); the .MET carries the per-frame unicode
REM the text trio (SetSpriteString/GetStringWidth/DrawString) matches
REM string chars against. cd/TITLCARD.SP2 + cd/TITLCARD.MET <-
REM extracted/Data/Sprites/Global/TitleCard.bin. 8.3-compliant base
REM "TITLCARD" (8 chars -> 12-char filename) per GFS_FNAME_LEN=12
REM (SEGA_GFS.H:37); "TITLECARD.SP2" (13) would fail GFS_NameToId. (2.4j.2)
python "%ROOT%\tools\build_entity_atlas.py" --bin "%ROOT%\extracted\Data\Sprites\Global\TitleCard.bin" --spr "%ROOT%\cd\TITLCARD.SP2" --met "%ROOT%\cd\TITLCARD.MET"
if errorlevel 1 ( exit /b %ERRORLEVEL% )

REM   - cd/*SFX.PCM  <- extracted/Data/SoundFX/Global/*.wav @ 22050 Hz s8 mono.
REM     Mapping cites the decomp RSDK.GetSfx/PlaySfx site (Entities.c SFX block).
python "%ROOT%\tools\convert_audio.py" "%ROOT%\extracted\Data\SoundFX\Global\Ring.wav"      --out "%ROOT%\cd\RINGSFX.PCM"   --rate 22050
if errorlevel 1 ( exit /b %ERRORLEVEL% )
python "%ROOT%\tools\convert_audio.py" "%ROOT%\extracted\Data\SoundFX\Global\Jump.wav"      --out "%ROOT%\cd\JUMPSFX.PCM"   --rate 22050
if errorlevel 1 ( exit /b %ERRORLEVEL% )
python "%ROOT%\tools\convert_audio.py" "%ROOT%\extracted\Data\SoundFX\Global\Destroy.wav"   --out "%ROOT%\cd\BREAKSFX.PCM"  --rate 22050
if errorlevel 1 ( exit /b %ERRORLEVEL% )
python "%ROOT%\tools\convert_audio.py" "%ROOT%\extracted\Data\SoundFX\Global\Destroy.wav"   --out "%ROOT%\cd\STOMPSFX.PCM"  --rate 22050
if errorlevel 1 ( exit /b %ERRORLEVEL% )
python "%ROOT%\tools\convert_audio.py" "%ROOT%\extracted\Data\SoundFX\Global\Spring.wav"    --out "%ROOT%\cd\BOUNCESFX.PCM" --rate 22050
if errorlevel 1 ( exit /b %ERRORLEVEL% )
python "%ROOT%\tools\convert_audio.py" "%ROOT%\extracted\Data\SoundFX\Global\Hurt.wav"      --out "%ROOT%\cd\HURTSFX.PCM"   --rate 22050
if errorlevel 1 ( exit /b %ERRORLEVEL% )
python "%ROOT%\tools\convert_audio.py" "%ROOT%\extracted\Data\SoundFX\Global\LoseRings.wav" --out "%ROOT%\cd\LOSESFX.PCM"   --rate 22050
if errorlevel 1 ( exit /b %ERRORLEVEL% )

REM ALL .o files under src/ must be removed before each docker make because
REM -D flag swaps (QA_MODE, GHZ_AUTOADVANCE_TICKS, future per-build defines)
REM don't touch any source-file mtime, so make would otherwise skip the
REM rebuild and the binary would carry whichever flag the previous build used.
REM (Same gotcha class as the jo pool stale-core.o bug.)
REM
REM Phase 2.3k-pre (2026-05-28): broadened from main.o-only to src\**\*.o.
REM Phase 2.3j shipped its "GREEN" evidence on a binary where
REM -DGHZ_AUTOADVANCE_TICKS=480 only landed in main.o, while Game.o (which
REM holds the actual autoadvance timer check) kept the prior build's
REM compiled-in value. The fresh capture in Phase 2.3k iter-2 showed this:
REM WRAM-L all-zero + g_ghz_fg_xs=0 + GHZ NBG1 never reconfigured, despite
REM the agent's report claiming GHZ-active. Trust no rebuild evidence
REM unless the .o purge covers every TU that consumes -D flags.

REM 1. Build the QA binary first (title held for deterministic capture) and
REM    run the reference-diff gate against it.
if not "%SKIP_QA%"=="1" (
  del /q /s "%ROOT%\src\*.o" >nul 2>&1
  docker run --rm -v "%ROOT%":/work -w /work joengine-saturn:latest make QA_MODE=1%EXTRA_ARGS%
  if errorlevel 1 ( exit /b %ERRORLEVEL% )
  pwsh -NoProfile -File "%ROOT%\tools\qa_gate.ps1"
  if errorlevel 1 ( exit /b %ERRORLEVEL% )
)

REM 2. Build the RELEASE binary (no QA_MODE: title auto-advances to the demo,
REM    Sonic auto-runs, FG cell-scrolls, sky parallaxes). This is the game.iso
REM    you're left with after build.bat. Force full src/ recompile (see above).
del /q /s "%ROOT%\src\*.o" >nul 2>&1
docker run --rm -v "%ROOT%":/work -w /work joengine-saturn:latest make%EXTRA_ARGS%
if errorlevel 1 ( exit /b %ERRORLEVEL% )

REM 2b. CD-DA BGM. Multi-track support: if both track02.wav (GHZ Act 1) AND
REM     track03.wav (title) exist, emit a 3-track CUE with both. Otherwise
REM     fall back to single-track (track 02 only) for backward compat.
REM     Title state uses track 03, game state uses track 02.
if exist "%ROOT%\cd_audio\track03.wav" (
  if exist "%ROOT%\cd_audio\track02.wav" (
    python "%ROOT%\tools\build_cdda.py" "%ROOT%\cd_audio\track02.wav" "%ROOT%\cd_audio\track03.wav" --cue-out "%ROOT%\game.cue" --iso "%ROOT%\game.iso"
    if errorlevel 1 ( exit /b %ERRORLEVEL% )
  )
) else if exist "%ROOT%\cd_audio\track02.wav" (
  python "%ROOT%\tools\build_cdda.py" "%ROOT%\cd_audio\track02.wav" --bin-out "%ROOT%\cd_audio\track02.bin" --cue-out "%ROOT%\game.cue" --iso "%ROOT%\game.iso"
  if errorlevel 1 ( exit /b %ERRORLEVEL% )
)

if "%SKIP_QA%"=="1" ( exit /b 0 )

REM 3. Grounded gate -- capture several game-state frames from the RELEASE
REM    binary and require the MAJORITY to show Sonic grounded. Individual
REM    frames may legitimately catch him mid-jump (auto-jumping a cliff); the
REM    real "floating bug" makes ALL frames bad. We sample over ~5s of demo
REM    so steady-state running gets covered alongside any jump arcs.
REM Wait 24 lands AFTER BIOS (~14-18s) + title (~2s) + title card (~2s).
REM Wait=18 was too aggressive: frames 5-8 still captured title-card hold
REM phase (steady visuals registered as scroll-stall in Gate 8). Verified
REM 2026-05-26 by examining qa_ground_6 which showed title-card content
REM (ring icon + Act-1 digit) instead of true gameplay state. Wait=24
REM puts the first capture ~6s past title-card start so deep gameplay
REM is sampled.
pwsh -NoProfile -File "%ROOT%\tools\qa_boot.ps1" -Cue game.cue -Wait 24 -Shots 8 -Every 0.7 -Out qa_ground.png
if errorlevel 1 ( exit /b %ERRORLEVEL% )
python "%ROOT%\tools\qa_grounded_majority.py" qa_ground_1.png qa_ground_2.png qa_ground_3.png qa_ground_4.png qa_ground_5.png qa_ground_6.png qa_ground_7.png qa_ground_8.png
if errorlevel 1 (
  echo build.bat: FAIL -- player floating in majority of game-state frames 1>&2
  exit /b 1
)
echo build.bat: PASS -- title reference-diff + game-state grounded check.
exit /b 0
