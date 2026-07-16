# AIZ seam CD-read elimination (#302 / task 2026-07-16)

## Measured root cause (live chain HEAD c4ad000, RetroArch UDP witnesses)

- The AIZ intro "slow motion" has TWO regimes (tools/_aiz_cdlog.txt, full-leg log):
  1. Menu->AIZ seam storm: ~8.3 s with tick FROZEN, 23 GFS fills, io_vbl +285
     (4.75 s pure CD wait). qa_aiz_speed.py RED: 1.6 tick/s, 60 io_vbl/frame in
     the arrival window.
  2. Play phase (fly-in through all beats): 84 s at 23.5 tick/s (39% realtime),
     fills 63->64 (ONE fill), io_vbl delta = 0 -> the ticking fly-in is CD-QUIET;
     its slowness is the render/draw wall (tick = 4 x render-fps catch-up cap),
     out of scope here (do NOT touch P6_TICK_CATCHUP).
  3. AIZ->GHZCutscene seam storm: ~7.7 s frozen, 21 fills, io_vbl +248.

- Fill-by-fill attribution (sector -> DATA.RSDK entry, tools/_aiz_cdprobe.py +
  entry-start-within-sector disambiguation):
  - Eliminable at Menu->AIZ: Players/Sonic.bin, Players/SuperSonic.bin,
    Players/Tails.bin, Global/SuperButtons.bin (+59881 cluster ambiguity:
    Global/StarPost.bin), UI/TextEN.bin, UI/WaitSpinner.bin, (Global/EggPrison.bin,
    Global/SpeedGate.bin ambiguous-cheap).
  - Eliminable at AIZ->GHZCutscene: Players/SuperSonic.bin, Global/SuperButtons.bin,
    UI/TextEN.bin, UI/WaitSpinner.bin, GHZCutscene/Claw.bin (AIZKingClaw.c:89).
  - NOT eliminable this pass (report): scene files (Scene1/StageConfig/TileConfig/
    16x16Tiles), Localization StringsEN.txt (decomp-faithful per-scene reload,
    Localization.c:22-90), UI/SmallFont.bin header read (UIWidgets.c:38; its
    264,420 B frame table can never fit a Saturn pack window - alloc-fail witness
    p6_w_anim_lastfail id|0x1CB1), one stage-SFX header open per seam
    (RedCube_L.wav / LedgeBreak.wav; StageConfig list, #271 latch resets at
    ClearStageSfx), 2-3 tiny UNK pack entries.

## Fix (proven #322/GHZANIM idiom - pack-resident anim bins, zero engine change)

- tools/build_anim_pack.py AIZ_OBJ_BINS += the Menu->AIZ eliminables
  (AIZOBJ.PAK, mounted at p6_aiz_reload into P6_HW_OBJANIMPAK 256 KB cart
  window; currently 3,124 B used).
- tools/build_player_atlas.py HBHOBJ.PAK += verbatim SuperSonic/SuperButtons/
  TextEN/WaitSpinner/GHZCutscene-Claw (mounted at the GHZCutscene seam into the
  same window; internal cap 0x20000).
- No new WRAM-H .bss; no engine source change; plain GHZ byte-identical
  (GHZOBJ.PAK untouched; AIZOBJ/HBHOBJ load only in chain/front-end flavors).

## Gate

- tools/qa_aiz_speed.py (RED-first, run on HEAD artifact game.iso 2026-07-16):
  - Seam axis: fills accumulated Menu->AIZ arrival storm (RED baseline 23)
  - Play axes: tick/s (>=50 target) + io_vbl/frame (<=1.0)
- RED shown: 1.6 tick/s, 60.0 io_vbl/frame, 4.5 fills/frame (arrival window).
- Expected post-fix: seam fills ~<=14; play io stays 0; play tick UNCHANGED
  (~23/s, render-bound) - reported honestly per task guardrail.
