# CP5b.5 -- Title foreground STABILITY (remove the TitleBG VDP1 thrash)

Status: IN PROGRESS (2026-06-22)
Supersedes the "clean title" claim of CP5b.4 (5131bed) -- which was only clean
in the best frame.

## Problem (MEASURED 2026-06-22, multi-frame capture + savestate)

CP5b.4 un-gated the verbatim decomp TitleBG parallax sprites (Mountain1/Mountain2/
Reflection/WaterSparkle/WingShine -- 9 entities) drawn through the VDP1 slot cache.
The Title flavor's slot cache is `P6_VDP1_NSLOTS = 10` (p6_vdp1.c:75) and the
foreground already holds ~9 distinct rects (6 logo pieces + ring + Sonic head +
finger, per p6_vdp1.c:68). When the TitleBG parallax sprites scroll on-screen the
distinct-rect working set exceeds 10 -> LRU THRASH -> the FG (Sonic head/body)
slots get evicted and the foreground BREAKS UP.

Evidence:
- Savestate witnesses (`_titlebg_sprites.mcs`): `p6_w_vdp1_evicts = 273`,
  `p6_w_vdp1_slots = 10` (full), `p6_w_vdp1_handle_drops = 158`.
- Worst-frame gate `qa_title_fg_stable.py` on the 40-frame settled burst:
  **25 / 40 settled frames have a BROKEN foreground (sonic_head < 5000)**; the
  clean frames are only those where few/no TitleBG sprites are on-screen.
  - clean frame head ~10778; broken frame head ~700-3000 (head displaced /
    body gone + a detached glove + a brown mountain fragment + a water-line).
- The CP5b.4 gate (`qa_titlebg_sprites.py`) used `pick_best` -> selected the one
  clean frame (shot_1) and PASSED. Field-gotcha #4 (measured the best frame, not
  the worst).

## Root cause

VDP1 10-slot LRU cache thrash. FG (~9 rects) + TitleBG (1..4 on-screen rects)
> 10 slots. The fragmentary TitleBG mountains/water also do not read as the Mania
backdrop on the 320 px Saturn screen (they are authored for a wider PC screen and
wrap). The decomp's real island is the Title3DSprite billboards (58, anim 5) which
are ALSO gated off behind P6_TITLE3D_ON for the same slot-thrash reason
(FG 9 + 5 billboard rects = 14 > 10).

## Fix (this step -- one port, one gate)

Stop drawing the TitleBG parallax sprites as VDP1 sprites. The title backdrop stays
the Saturn-native VDP2 NBG1 sky+cloud+island present (`p6_vdp2_present_title_backdrop`,
`p6_w_title_backdrop_done = 1` confirmed) which costs ZERO VDP1 slots, so the FG keeps
all 10 slots in every frame. Mechanism: make the existing `P6_TITLEBG_SPRITES_OFF`
A/B knob (p6_ovl_ghz.c:132,312; build_shipping.sh:139; build_p6scene_objs.sh:404)
the DEFAULT for the Title flavor.

A separate follow-up (CP5b.6) makes the VDP2 island more prominent (re-park below the
logo) and is where the rotating island work (VDP2 RBG0 + coefficient table) lands.
The VDP1 billboards stay deferred.

## Gate (RED-first)

`tools/_portspike/qa_title_fg_stable.py` -- worst settled frame: among frames with
the SONIC MANIA logo present, EVERY frame's `sonic_head >= 5000`.
- RED on CP5b.4 (TitleBG on): 25/40 settled frames broken, worst head=708.
- GREEN target (TitleBG off): 0 broken frames, min head >= 5000.

## PROACTIVE-DETECTION-CHECKLIST (§4.5.1)

- Audit 1 (Z-order): N/A -- this REMOVES draw calls. FG sprite Z-order unchanged;
  the VDP2 backdrop stays priority 1 below the VDP1 sprites (priority 7).
- Audit 2 (animation cadence): N/A -- no animated sprite added.
- Audit 3 (pivot+flip): N/A -- no flipped sprite added.
- Audit 4 (boot-delay budget): NEGATIVE delta -- removes Game_TitleBG.o +
  Game_Title3DSprite.o from the overlay link (smaller OVLRING.BIN, slightly faster
  load). No asset added to the synchronous boot path.
- Root-cause audit (the one that mattered): VDP1 slot budget. FG distinct rects
  (~9) + TitleBG on-screen rects must stay <= P6_VDP1_NSLOTS (10). Removing the
  TitleBG VDP1 sprites returns the working set to ~9 <= 10 -> no eviction of FG.

## Files

- `tools/_portspike/qa_title_fg_stable.py` (NEW -- worst-frame RED gate).
- `tools/_portspike/_p6/build_shipping.sh` + `build_p6scene_objs.sh`
  (default P6_TITLEBG_SPRITES_OFF for the Title flavor; A/B override preserved).
- `docs/feature_checklists/cp5b5_title_fg_stability.md` (this).
