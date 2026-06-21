# CP5b.2 -- render TitleSonic (head + finger-wave) in the title ring center

Last revised: 2026-06-21

## Goal / deliverable
A SCREENSHOT showing Sonic's head (TitleSonic anim 0) in the CENTER of the gold
title ring emblem, with the finger-wave (anim 1) on the head anim's last frame,
plus a RED->GREEN visual gate (`tools/_portspike/qa_title_sonic.py`). TitleSonic_
Update/Draw/Create/StageLoad are ALREADY ported (verbatim decomp in the cached
`SonicMania_Objects_Title_TitleSonic.c`, 69 L, fully self-contained) -- they're
just not REGISTERED (a NULL closure stub in p6_ovl_ghz.c) + the sheet isn't staged.

This is a MECHANICAL MIRROR of CP5b.1 (commit 9edd29d, which made the SONIC MANIA
logo render) -- same SaturnSheet-stage + VDP1-box + render-diag-witness pattern.

## PRE-FLIGHT measurements (done offline, BEFORE building)

### Sonic.bin frame dims (convert_ring_sprite.parse_spr -- MEASURED)
- sheet[0] = `Title/Sonic.gif`, **1024 x 1024**, 8bpp/256-color (4x the Logo's 512x512).
- anim 0 "Sonic" (the head/body pop-up): **49 frames**, speed 1, loop 48.
  - MAX frame = **241 x 137** (f30 = 241 wide; f15/f17 = 137 tall). Loop target
    f48 = 110 x 120. The pop-up sweeps WIDE mid-animation (the leaning poses).
- anim 1 "Finger Wave": **12 frames**, speed 1, loop 0. MAX = **50 x 63**.
- **OVERALL MAX frame across anim 0 + anim 1 = 241 x 137.**

### VDP1 box (p6_vdp1.c, P6_FRONTEND_TITLE branch) -- MUST GROW
- CP5b.1 TITLE box = **192 x 160** (sized for the 176x144 logo emblem).
- TitleSonic's widest frame is **241** > 192 -> with the 192 box, the wide head
  frames hit `w > P6_SPR_MAXW` in p6_slot_for and OVERSIZE-DROP (the emblem class
  CP5b.1 hit at 144>96). So GROW to cover BOTH the 176x144 emblem AND the 241x137
  head: **248 (241 mult-8 pad) x 160** (covers 144 emblem + 137 head). A single
  VDP1 sprite supports 504(W,mult-8) x 255(H) per ST-013-R3 sec 6.6 (CMDSIZE), so
  241x137 fits ONE sprite. Budget: NSLOTS 10 * 248*160 = 396,800 px <
  JO_VDP1_USER_AREA_SIZE (0x71D38 = 466,232 B). OK.
- NSLOTS: CP5b.1 used 8 (the ~6 logo pieces + ring). Adding the head + finger
  (2 more distinct rects) -> bump to **10** (logo set + sonic, below 10 -> no
  eviction; LRU handles any overflow gracefully per ghz-vdp1-sprite-residency-lru).
- staging buffers s_stage/s_fetch are P6_SPR_MAXW*P6_SPR_MAXH bytes (1 byte/px,
  8bpp). For 248x160 = **39,680 B each** (was 30,720 at 192x160). Re-place in the
  cart window: s_stage @ 0x226E0000 (0xA000 reserved), s_fetch @ 0x226EA000
  (0xA000) -> ends 0x226F4000, < the GFS windows 0x22700000 (gap 48 KB). Disjoint.

### THE GAP = sheet-stage + box BOTH (mirrors CP5b.1's two root causes)
1. SHEET STAGING: TitleSonic's Title/Sonic.gif surface has saturnSheetSlot==-1
   (no TSONIC.SHT staged) -> the p6_ghz_arm_env bind loop skips it -> VDP1 handle
   < 0 -> every TitleSonic blit drops (the CP5b.1 logo class, now for the head).
2. VDP1 BOX: even once staged, the 241-wide head would oversize-drop under the
   192 box. BOTH must change.
(No foreachStackPtr bug here -- CP5b.1 already fixed that in p6_frontend_frame;
TitleSonic_Update doesn't foreach. TitleSetup_State_FlashIn flips TitleSonic
visible via foreach_all(TitleSonic), which now iterates correctly post-CP5b.1.)

### TSONIC.SHT budget (1024x1024 -- DIVERGES from the 512x512 TLOGO)
- TSONIC.SHT (banded, BAND_ROWS=16) = **121,090 B (118.3 KB)** -- MEASURED.
  - This EXCEEDS the 0x10000 (64 KB) load buffer TLOGO.SHT uses. So the TSONIC
    stage block loads with **0x20000 (128 KB)** into P6_LW_ENTITYLIST (region size
    0x6CC00 = 445,440 B -> 128 KB fits with huge margin). THE KEY DIVERGENCE.
  - largest compressed band = 3,424 B (< the 0x4000 scratch limit). OK.
  - raw band = 16 * 1024 = **16,384 B = 0x4000**. SaturnSheet_MakeResident checks
    `rsz > 0x4000` (line 190) -> 16,384 is NOT > 16,384 -> passes (boundary-exact).
    scratch usage = 0x4000 (zbuf) + 0x4000 (raw) = 0x8000 -> the P6_LW_LAYSCRATCH
    32 KB (0x8000) window is exactly sufficient. The resident FetchRect path then
    serves rects with NO inflate (Task #243). Verified boundary-exact.
- Inflated resident = 1024*1024 = **1,048,576 B (1 MB)** in the 3.64 MB cart RES
  store (0x22400000..0x227A0000). With TLOGO (262 KB) + LOGOS (131 KB) also
  resident = ~1.39 MB total < 3.64 MB. OK.
- Banded store: TSONIC 121 KB + TLOGO 19.5 KB + LOGOS 6.4 KB + the 9 GHZ sheets
  -- all in the 384 KB band store (0x227A0000..0x22800000). The 9 GHZ sheets total
  ~325 KB; ADDING 147 KB of front-end sheets would overflow 384 KB. BUT the
  front-end build's GHZ sheets are staged the SAME (CP5a/CP5b.1 already stage all
  9 GHZ + LOGOS + TLOGO in the Title flavor and it boots). Re-confirm at build:
  if SaturnSheet_Stage returns -1 (store full), the slot witness is < 0 -> RED.
  MITIGATION if overflow: the front-end doesn't NEED the GHZ content sheets (no
  GHZ entities on the Title scene) -- but they're staged by the shared load core.
  Measure first; only act if the witness shows a stage failure.

### Asset confirmation
- `extracted/Data/Sprites/Title/Sonic.bin` EXISTS; its sheet[0] = "Title/Sonic.gif".
- `extracted/Data/Sprites/Title/Sonic.gif` EXISTS (1024x1024 P-mode).
- The ENGINE needs TSONIC.SHT staged the SaturnSheet way (like TLOGO.SHT), NOT the
  hand-port cd/TSONIC.ATL (which exists but is unused by the engine path).

## Files to touch (all under P6_FRONTEND_TITLE)
1. `tools/build_sheet_bands.py` -- SHEETS += ("Title/Sonic.gif","TSONIC.SHT");
   FRONTEND_ONLY_SHEETS += {"TSONIC.SHT": "P6_FRONTEND_TITLE"}. Regenerate
   cd/TSONIC.SHT + p6_sheet_model.json + p6_sheet_probes.inc.
2. `platform/Saturn/SaturnSheet.cpp` -- SATURNSHEET_SLOTS 11 -> 12 for the TITLE
   flavor (slot 11 = TSONIC.SHT).
3. `tools/_portspike/_p6/p6_io_main.cpp` -- (a) define the _p6_w_tsonic_* witnesses
   GUARDED under #if defined(P6_FRONTEND_TITLE) (mirror the tlogo block; the default
   GHZ _end has only 96 B margin). (b) in p6_ghz_arm_env: the SURFACE-side hash-scan
   for "Title/Sonic.gif" (tsonic_shtslot/surfidx/surfslot/handle). (c) in p6_scene_run
   after the TLOGO.SHT stage block: stage TSONIC.SHT with a **0x20000** load buffer,
   set hash "Title/Sonic.gif", MakeResident.
4. `tools/_portspike/_p6/p6_vdp1.c` -- TITLE box 192x160 -> **248x160** + NSLOTS
   8 -> 10 + re-place s_stage/s_fetch (39,680 B each) at 0x226E0000 / 0x226EA000.
5. `tools/_portspike/_p6/p6_ovl_ghz.c` -- REMOVE `ObjectTitleSonic *TitleSonic = NULL;`
   stub; add `extern ObjectTitleSonic *TitleSonic;` + register_object_full(...) next
   to TitleSetup/TitleLogo; add the per-tick render-diag witness writes for the live
   TitleSonic entity (handle/visible/onscreen/sheetid; mirror the tlogo block).
6. `tools/_portspike/_p6/build_p6scene_objs.sh` -- add Title_TitleSonic:Game_TitleSonic
   to the P6_FRONTEND_TITLE census compile loop + the -u _p6_w_tsonic_* witness roots.
7. `tools/_portspike/_p6/build_shipping.sh` -- add Game_TitleSonic.o to OVL_FE under
   P6_FRONTEND_TITLE + a [5c]-style tsonic symbol-presence grep.
8. `tools/_portspike/qa_title_sonic.py` -- ALREADY WRITTEN (RED-confirmed). CALIBRATE
   RING_BOX + HEAD_PIX_MIN by measuring the CP5b.1 (head absent) vs CP5b.2 (head
   present) builds. DO NOT rewrite the gate logic.

## PROACTIVE-DETECTION-CHECKLIST (sec 4.5.1)

### Audit 1 -- Layering / Z-order
TitleSonic is drawGroup 4 (TitleSonic_Create:49) -- the SAME group as TitleLogo (4).
The head must draw ON TOP of the ring's black interior but the ring emblem (anim 0)
is also group 4. Scene-slot order within group 4 decides intra-group draw order.
TitleSonic's Scene1.bin slot vs the logo pieces: the head sits in the ring HOLE
(disjoint from the gold ring pixels), so even if drawn behind the emblem the head
occupies the transparent center -- legible either way. The decomp draws the head
with SetClipBounds(0,0,0,ScreenInfo->size.x,160) (TitleSonic_Draw:30) clipping it
to y<160 (the upper title region). On Saturn the per-sprite VDP1 Z (slDispSprite)
owns priority; the head + logo occupy mostly-disjoint screen regions (head in the
ring hole, gold ring around it), so intra-group Z is not load-bearing for
legibility at CP5b.2. Documented; the screenshot confirms the head is visible.

### Audit 2 -- Animation cadence
- anim 0 "Sonic" (head): 49 frames, speed 1, per-frame durations vary (f0=64,
  most=2-3). Driven by RSDK.ProcessAnimation in TitleSonic_Update (verbatim decomp
  -- the ENGINE animator runs, NOT a Saturn re-implementation). Cadence parity is
  inherited from the verbatim engine ProcessAnimation. f0 holds 64 ticks (the rest
  pose), then the pop-up sweeps the 49 frames -> settles at loop f48.
- anim 1 "Finger Wave": 12 frames, speed 1, durations 1-4. Only processed when
  animatorSonic.frameID == frameCount-1 (== f48, the settled pose) per
  TitleSonic_Update:18-19. The verbatim engine animator drives it.
- NO Saturn walker -- both anims use the engine animator; the Saturn side renders
  whatever frame the engine resolved. Cadence parity inherited from the engine.
  PASS (no Saturn cadence code to audit).

### Audit 3 -- Pivot+flip composite
TitleSonic draws NON-flipped (TitleSonic_Draw:31,36 -> DrawSprite(..., false), no
FLIP). The head + finger are single un-mirrored sprites. The engine DrawSprite
computes the screen rect from the entity position + the frame pivot (Sonic.bin
stores negated pivots, e.g. f48 pivot (-50,-91)); the Saturn p6_vdp1 path receives
the already-resolved (sx,sy,w,h) rect from the engine DrawSprite call (intercepts
at the blit, AFTER geometry). So the head's world position is the engine's -- no
flip math, no composite risk. The finger draws at its own pivot (anim 1 f0 pivot
(6,-44)) as a separate sprite, again engine-positioned. No flip -> no asymmetry
risk. Simpler than CP5b.1's 2x-flipped emblem. PASS.

### Audit 4 -- Boot-delay budget
TSONIC.SHT = 121,090 B. Added to the lean front-end boot synchronous staging
(p6_scene_run, alongside the 9 GHZ + LOGOS + TLOGO that CP5b.1 already stages).
Budget: 121 KB / 150 KB/s + 1 GFS_Seek = ~0.81 + 0.1 = ~0.91s. MakeResident
inflates 64 bands (1 MB) -- WRAM->WRAM inflate, ~fast, but 64 bands * ~a few ms.
The Title flavor already stages 9 GHZ + LOGOS + TLOGO (CP5b.1) so this is an
incremental +~0.91s on an already-multi-second load. Title is sprite-only (no
tileset inflate), reaches deep frames within the capture budget (the gate's t=42s+
deep burst already accommodates the front-end boot). UNDER the 5s cumulative
budget for the incremental add. PASS.

## Visual gate (RED-first -- ALREADY WRITTEN, CALIBRATE only)
`tools/_portspike/qa_title_sonic.py`:
- RED on the CURRENT (CP5b.1) Title build: TitleSonic is the NULL stub, its sheet
  is not staged -> the _p6_w_tsonic_* witnesses are ABSENT from game.map AND the
  ring interior is black (Sonic-blue head px ~0).
- GREEN once CP5b.2 lands: the head binds (witnesses present + handle>=0) AND the
  screenshot shows Sonic-blue px in the ring interior > HEAD_PIX_MIN.
- CALIBRATE RING_BOX to the actual ring interior (measure the CP5b.1 logo PNG) +
  HEAD_PIX_MIN cleanly between the CP5b.1 baseline (black, ~0) and the CP5b.2 head.
- The gate's PRIMARY evidence is the SCREENSHOT + pixel measure (field gotcha #4).

## Regression guard
All new code under `#if defined(P6_FRONTEND_TITLE)`. Default GHZ
`_end < ANIMPAK 0x060b6c00` must hold (TSONIC probe rows flag-gated -> default
const table byte-identical). qa_engine_title.py (CP5a T1-T5) + qa_title_logo.py
(CP5b.1) must stay GREEN -- the logo must STILL render. Default GHZ ghz_regression
unaffected. Leave the DEFAULT GHZ build on disk at the end (rebuild last, no flag).
