# Feature checklist — front-end (Title) load-time MEASUREMENT + attackable fix

Task: the user reports the engine front-end (P6_FRONTEND_TITLE) title takes
~70 s wall-clock to appear (qa_title_sonic.py waits BOOT_WAIT=66). MEASURE
where the ~70 s goes; fix ONLY the attackable lever, decided by the data.

## Source / cite

- Load path: `tools/_portspike/_p6/p6_io_main.cpp`
  - `p6_engine_boot_and_run` (5291) -> `p6_scene_run` (3038) = the MASKED
    synchronous load core (interrupts off, vblank ISR frozen).
  - `p6_scene_tick` (5314) first lean tick -> `p6_title_reload` ->
    `p6_scene_load_and_arm` (4554) = phase-2, UNMASKED (vblank advances).
  - `main.c:1270-1271`: `p6_engine_boot_and_run()` runs the ENTIRE masked
    core BEFORE `jo_core_add_vblank_callback(p6_perf_vblank)` is registered
    -> `p6_perf_vbl_count` is 0 across the masked core (cannot time it via
    vblanks).
- TSONIC.SHT staging: p6_io_main.cpp:3370-3398 (121,090 B single GFS read,
  staged banded, deliberately NOT MakeResident -- per-frame FetchRect inflate).
- GFS layer: `tools/_portspike/_p6/p6_gfs.c` -- `GFS_TMODE_CPU` busy-poll, NO
  interrupt path (p6_gfs.c:40-42,92-94) => masking does not affect CD reads.
  `p6_w_gfs_io_vbl`/`p6_w_gfs_fills` already measure phase-2 I/O.
- `rsdk_storage_load_to_lwram` (src/rsdk/storage.c:739) = single multi-sector
  `GFS_Fread` of the whole file (does NOT touch p6_w_gfs_fills).
- LoadDataPack: p6_io_main.cpp:3525 (cd/DATA.RSDK = 182,962,115 B, windowed).

## Clock facts (MEASURED, not assumed)

- `p6_w_perf_cks = 1` (read from `_p6_ghzreg.mcs`) => FRT divider /32 =>
  26.8 MHz/32 = 837.5 kHz => 16-bit FRT WRAPS every 78.25 ms. ms = ticks/837.5.
  Confirmed by the in-source comment p6_io_main.cpp:1134-1137.
- GHZ phase-2 (unmasked): `io_vbl=572` vblanks / `gfs_fills=115` = ~83 ms/fill
  (emulated CD latency-dominated, #251). 83 ms > 78 ms => per-fill FRT-delta is
  wrap-AMBIGUOUS. THEREFORE the masked-core I/O metric is (fills, bytes)
  [wrap-immune integers] x measured-ms/fill, NOT FRT-deltas.

## Measurement design (the deliverable)

Add flag-gated (P6_FRONTEND_LOGOS||TITLE only -- NEVER default GHZ; WRAM-H has
~64 B headroom) timing witnesses `p6_w_lt_*`. Per sub-step capture a triple:
(vbl_delta, fills_delta, frt_ticks_delta). MASKED core sub-steps:
  S1  boot pre-load (cart probe + input settle + InitStorage + memsets)
  S2  OVLRING.BIN + GHZ1DORM.BIN + GHZ1LAYT.BIN + ANIMPACK loads
  S3  LOGOS.SHT + TLOGO.SHT staging (512x512 sheets)
  S4  TSONIC.SHT load+stage (1024x1024, 121,090 B) <- the CP5b.2 suspect
  S5  LoadDataPack (DATA.RSDK 182 MB windowed registry walk) <- #251 suspect
  S6  AudioDevice::Init + ScoreAdd + MenuBleep SFX
  S7  LoadGameConfig (GameConfig.bin parse)
PHASE-2 (unmasked, vblank-timed -> EXACT ms + ground-truth ms/fill):
  S8  LoadSceneFolder (Title scene + TileConfig)
  S9  LoadSceneAssets (sprite sheets)
  S10 InitObjects (entity Create/StageLoad) + arm_env bind
Each S: emit `_p6_w_lt_<n>_vbl`, `_p6_w_lt_<n>_fills`, `_p6_w_lt_<n>_kb`,
`_p6_w_lt_<n>_frt`. Masked S: ms ~= fills * (phase2 vbl/fills * 16.67); compute
S: ms = frt/837.5. Phase-2 S: ms = vbl*16.67 (exact).

Acceptance (measurement): the gate prints the per-sub-step ms table, names the
DOMINANT sub-step, and reports the settled-title runtime fps (p6_w_perf_*).

## Fix decision (BY the data)

- IF dominant = TSONIC.SHT decode/inflate (S4 / per-frame): ATTACKABLE +
  separate from pack I/O. Pre-decode to a raw Saturn blob offline OR rebuild
  TSONIC.SHT with only the title frames used. RED->GREEN load-time witness;
  keep title render correct (qa_title_sonic GREEN + screenshot).
- IF dominant = LoadDataPack I/O (S5): the KNOWN #251 budget-blocked class
  (bigger GFS window needs WRAM/cart budget). REPORT, do NOT attempt here.
- IF dominant = something else: report it.

## Files

- `tools/_portspike/_p6/p6_io_main.cpp` -- witnesses + sub-step brackets (flag-gated)
- `tools/_portspike/_p6/p6_gfs.c` + `src/rsdk/storage.c` -- unify the fill/byte counters
- `tools/_portspike/_p6/build_shipping.sh` -- -u root the new witnesses (front-end only)
- `tools/_portspike/qa_frontend_loadtime.py` -- NEW: read the witnesses, print the table

## Do-not-regress

- Default GHZ `_end` + R0-R16 (everything flag-gated to P6_FRONTEND_*).
- Title render correctness: qa_title_sonic.py GREEN + screenshot.
