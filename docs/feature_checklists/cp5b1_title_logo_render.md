# CP5b.1 — render the SONIC MANIA logo (TitleLogo sheet bind + deep capture)

Last revised: 2026-06-21

## Goal / deliverable
A SCREENSHOT showing the recognizable SONIC MANIA logo on the Title scene,
plus a RED->GREEN visual gate (`tools/_portspike/qa_title_logo.py`). The
TitleLogo_Draw code is ALREADY ported + running (CP5a, verbatim decomp in the
overlay). The missing piece is the Title sprite-sheet residency + a deep-enough
capture frame.

This is a MECHANICAL MIRROR of CP4b (which made the SEGA logo render for the
Logos scene, commits 395913d + 790922d).

## PRE-FLIGHT measurements (done offline, BEFORE building)
1. **The gap is sheet-staging.** CP5a registers + runs TitleSetup/TitleLogo and
   `p6_ghz_arm_env` (the VDP1 bind loop, p6_io_main.cpp:2182-2207) runs for ALL
   scenes (called unconditionally from p6_scene_load_and_arm:4499). It binds any
   gfxSurface with `saturnSheetSlot >= 0`. CP5a NEVER stages a Title sheet -> the
   `Title/Logo.gif` surface has `saturnSheetSlot == -1` -> the bind loop skips it
   -> handle<0 -> every TitleLogo blit drops (W18 unbound-surface drop class). The
   CP4b VDP1 box (192x96) + SPRON arm already compile in the Title flavor
   (P6_FRONTEND_TITLE implies P6_FRONTEND_LOGOS). So the ONLY missing pieces are:
   (a) stage TLOGO.SHT, (b) grow the VDP1 box to fit the 144-tall emblem.
2. **Logo.bin frame dims (parsed via convert_ring_sprite.parse_spr):**
   - anim 0 "Logo Wings" (EMBLEM): **144 x 144** (1 frame)
   - anim 1 "Ribbon Unfurl" (RIBBON): up to ~20 x 62 (14 frames, speed 1)
   - anim 2 "Ribbon Wave": 56 x 72 (8 frames) — only after FlashIn (showRibbonCenter)
   - anim 3 "Ribbon Center": **176 x 52** (1 frame)
   - anim 4 "Game Title" (GAMETITLE, type 2 -> anim 4): **137 x 46** + 128 x 21
   - anim 5 "Power LED": 56 x 25 (destroyed in AnimateUntilFlash)
   - anim 6 "Copyright": 45 x 8
   - anim 7 "Ring Bottom": 120 x 25
   - **MAX frame = 176 x 144.** The CP4b FE VDP1 box is 192 x **96** -> the
     144-tall EMBLEM (anim 0) and 72-tall ribbon wave EXCEED 96 -> oversize-drop
     (p6_slot_for: `h > P6_SPR_MAXH`). MUST grow the box to >= 176 x 144. Plan:
     192 x 160 (176->192 mult-8 width per jo-sgl-sprite-width-mult8-shear;
     144->160 height). Budget: 8 slots x 192 x 160 = 245,760 px <
     JO_VDP1_USER_AREA_SIZE (466,232 B). OK.
   - Sheet = `Title/Logo.gif`, **512 x 512**, 8bpp/256-color. Banded 32 bands.
3. **Capture depth.** TitleSetup state timeline (TitleSetup.c):
   - State_Wait: timer 1024 -> -1024 by 16/frame = **128 ticks** of black FillScreen.
   - State_AnimateUntilFlash: ProcessAnimation(Electricity) until frameID==31; at 31
     flips EMBLEM+RIBBON visible=true; draws the ring. ~31+ ticks.
   - State_FlashIn: ProcessAnimation until frameCount-1; flips ALL TitleLogo
     (except PRESSSTART) visible=true (GAMETITLE/COPYRIGHT/RINGBOTTOM). Then
     Draw_Flash (white FillScreen briefly).
   - So the EMBLEM+RIBBON appear ~frame 160; the full SONIC-MANIA gametitle at
     FlashIn (~frame 180-220 of scene-life). Lean boot adds load frames on top.
     CAPTURE DEEP: try SaveFrame/Wait that lands ~frame 200-320. Title is
     sprite-only (light load) so it reaches deep frames fast.

## CP4b precedent (the exact mechanical mirror)
- `git show 790922d` — VDP1 box 64x64->192x96 + 8 slots (p6_vdp1.c, under
  P6_FRONTEND_LOGOS); off-.bss cart staging buffers @0x226E0000; SPRON re-enable
  p6_vdp2_arm_sprites_only (p6_vdp2.c) called each tick from p6_frontend_frame.
  BOTH already active for Title. **I only adjust the box HEIGHT (96->160) under
  a new TITLE-specific guard, and re-place the 2 staging buffers (now 30,720 B
  each).**
- `git show 395913d` — SaturnSheet slot 9->10 (LOGOS); build_sheet_bands.py
  SHEETS += LOGOS.SHT; p6_scene_run LOGOS.SHT stage block (set hash
  "Logos/Logos.gif", MakeResident); the arm_env bind loop binds it.

## Files to touch (all under P6_FRONTEND_TITLE)
1. `tools/build_sheet_bands.py` — SHEETS += `("Title/Logo.gif","TLOGO.SHT")`;
   FRONTEND_ONLY_SHEETS += `"TLOGO.SHT"` (so its probe rows stay flag-gated;
   the existing `#if defined(P6_FRONTEND_LOGOS)` wrapper covers it since
   TITLE implies LOGOS). Regenerate cd/TLOGO.SHT + p6_sheet_model.json +
   p6_sheet_probes.inc.
2. `tools/_portspike/_p6/p6_io_main.cpp` — under `#if defined(P6_FRONTEND_TITLE)`,
   in p6_scene_run after the LOGOS.SHT stage block: stage TLOGO.SHT, set hash
   "Title/Logo.gif", MakeResident. + Title-sheet bind witness (mirror the Logos
   surf-scan `p6_w_logos_*` diag: p6_w_tlogo_shtslot/surfidx/surfslot/handle) in
   p6_ghz_arm_env. + a per-frame Title draw-state witness (mirror p6_w_uipic_*:
   the first live TitleLogo's visible/onScreen/sheetID/handle).
3. `platform/Saturn/SaturnSheet.cpp` — `SATURNSHEET_SLOTS`: add a
   `#if defined(P6_FRONTEND_TITLE)` -> 11 branch (slot 9=LOGOS, slot 10=TLOGO).
4. `tools/_portspike/_p6/p6_vdp1.c` — `#if defined(P6_FRONTEND_TITLE)` box
   192 x 160 (covers the 176x144 emblem) + re-place s_stage/s_fetch (30,720 B
   each) at 0x226E0000 / 0x226E7800.
5. `tools/_portspike/_p6/build_p6scene_objs.sh` / `build_shipping.sh` — `-u`
   roots for the new Title witnesses; thread P6_FRONTEND_TITLE to SaturnSheet.o
   + p6_vdp1.c (Makefile already threads it). Symbol-presence grep.
6. `Makefile` — already threads -DP6_FRONTEND_TITLE to the jo-side (CP5a).
7. `tools/_portspike/qa_title_logo.py` — NEW gate (RED-first).

## PROACTIVE-DETECTION-CHECKLIST (§4.5.1)

### Audit 1 — Layering / Z-order
TitleLogo pieces are ALL drawGroup 4 (TitleLogo.c:131/137); TitleSetup ring is
drawGroup 12 (drawn last/on top — the electricity ring). Within drawGroup 4 the
draw order is scene-slot order (Scene1.bin placement order). On Saturn the VDP1
sprites are drawn via per-sprite slDispSprite Z (jo_sprite_draw3D) — SGL owns the
priority. The logo pieces overlap minimally (emblem top, ribbon mid, gametitle
mid, copyright bottom), so intra-group Z is not load-bearing for legibility at
CP5b.1 (the pieces occupy mostly-disjoint screen regions). The 2x-draw of the
emblem (FLIP_NONE then FLIP_X, TitleLogo.c:43-47) composes the symmetric ring;
both halves are the SAME sheet rect mirrored — order between them is irrelevant
(identical Z). No Z reversal risk for CP5b.1. Slot-to-Z parity table deferred to
the full title-composition step (CP5b.2, when TitleSonic + BG land and overlaps
matter).

### Audit 2 — Animation cadence
- EMBLEM (anim 0): 1 frame, speed 0 -> static. No walker.
- RIBBON (anim 1 "Ribbon Unfurl"): 14 frames, speed 1, loop 13. Driven by
  RSDK.ProcessAnimation in TitleLogo_Update (TITLELOGO_RIBBON case). The decomp
  engine's ProcessAnimation runs verbatim in the overlay (the ported TU calls
  RSDK.ProcessAnimation) — cadence is the engine's, NOT a Saturn re-implementation.
  At the CP5b.1 capture (early FlashIn) the ribbon shows its unfurl frame; the
  exact frame is whatever the engine animator advanced to. No Saturn walker to
  audit (the cadence is the verbatim engine's; correctness == the engine runs).
- GAMETITLE/COPYRIGHT/RINGBOTTOM (anim 4/6/7): 1-2 frames, speed 0 -> static.
- The only animated piece (ribbon) uses the engine animator; the Saturn side just
  renders whatever frame the engine resolved. Cadence parity is inherited from the
  verbatim engine ProcessAnimation, not re-derived. PASS (no Saturn cadence code).

### Audit 3 — Pivot+flip composite
EMBLEM draws TWICE: FLIP_NONE then FLIP_X, same anim 0 frame (pivot -144,-72
from Logo.bin; the RSDK pivot is stored negated). RIBBON draws FLIP_X then
FLIP_NONE (anim 1). Per `_RSDKv5_Graphics_Drawing.cpp` DrawSpriteFlipped, the
FLIP_X world top-left = entity.x - (width - pivotX) ... the engine's DrawSprite
computes the screen rect; the Saturn p6 path receives the already-resolved
(sx,sy,w,h) rect + the entity position from the SAME engine DrawSprite call
(p6_vdp1 intercepts at the blit, AFTER the engine computed geometry). So the
FLIP_X mirror geometry is the engine's — the Saturn VDP1 HF (PMOD bit 4) mirrors
the pixels within the staged box; the WORLD position comes from the engine. This
is the IDENTICAL path CP4b used for the Logos UIPicture (which is NOT flipped) —
the FLIP_X case is exercised here for the first time in the front-end. RISK: if
the Saturn blit applies the box-relative HF incorrectly for the emblem's two
halves, the ring could look asymmetric. MITIGATION: the gate's pixel measure +
the screenshot catch this (the ring is visibly symmetric or not). Documented;
verified by the screenshot at GREEN.

### Audit 4 — Boot-delay budget
TLOGO.SHT = Title/Logo.gif 512x512 banded. Raw 262,144 B; zlib-banded ~30-40 KB
(measured at build). Added to the lean front-end boot synchronous staging
(p6_scene_run, alongside the 9 GHZ sheets + LOGOS.SHT that CP5a already stages).
Budget: TLOGO.SHT ~35 KB / 150 KB/s + 1 GFS_Seek = ~0.23 + 0.1 = ~0.33s. The
Title flavor ALREADY stages 9 GHZ + LOGOS (CP5a) so this is an incremental
+~0.33s on an already-multi-second load. Title remains sprite-only (no tileset
inflate), reaches deep frames well within capture budget. The MEASURED TLOGO.SHT
size is filled in at build time below. UNDER the 5s cumulative budget for the
incremental add. PASS.

## Visual gate (RED-first)
`tools/_portspike/qa_title_logo.py`:
- RED on the current Title build = TitleLogo blits == 0 (witness: the Title
  draw-state witness shows handle<0 / no Title sprites landed) AND a pixel
  measure on the deep screenshot shows no logo (non-background pixel-mass in the
  logo region below threshold).
- GREEN = the SONIC MANIA logo renders: the Title sprite blit witness > 0 AND a
  quantitative pixel measure (non-magenta/non-black pixel count in the logo
  region) above threshold AND the screenshot visibly shows it.
- A WITNESS ALONE IS NOT ENOUGH (field gotcha #4): the gate's PRIMARY evidence
  is the SCREENSHOT + pixel measure, the witness is corroborating.

## Regression guard
All new code under `#if defined(P6_FRONTEND_TITLE)`. Default GHZ
`_end == 0x060b6ba0` (< ANIMPAK 0x060b6c00) must hold (the TLOGO probe rows are
flag-gated so the default const table is byte-identical). qa_engine_title.py
(CP5a T1-T5) must stay GREEN. Default GHZ ghz_regression unaffected.
