# Feature checklist — Task #309 caveat #2a: render Sonic + Tails in the GHZCutscene arrival cutscene

Status: IN PROGRESS (2026-06-30). Branch: ghzcutscene-intro-ghz-loop.

## Goal
During the AIZ-intro -> GHZCutscene arrival cutscene the two players (SLOT_PLAYER1
Sonic + SLOT_PLAYER2 Tails) currently render as BLACK SQUARES: the front-end build
SKIPS the GHZ player anim pack (GHZANIM.PAK), so `Player_StageLoad`'s
`LoadSpriteAnimation("Players/Sonic.bin"/"Tails.bin")` resolves to NOTHING ->
`aniFrames` unresolved -> no sheet bound -> the engine's Player_Draw blits an
unbound surface (silent drop / black). Make the players RENDER with correct colors,
mirroring the Tier-B.2 Heavies pattern (selective combined atlas + dead-slot stage +
dedicated CRAM palette block + per-object draw-block routing).

## Decomp citations
- `tools/_decomp_raw/SonicMania_Objects_GHZ_GHZCutsceneST.c:173,198,219` -- the
  cutscene sets the players to `ANI_FAN` (the fan-twirl) EVERY frame via
  `RSDK.SetSpriteAnimation(player->aniFrames, ANI_FAN, ...)`; `:158,176,222`
  `Player_State_Static` (does nothing -- Player.c:3797-3800 "just vibe", the anim is
  held at ANI_FAN). So the ONLY player animation the cutscene displays is **ANI_FAN**.
- `tools/_decomp_raw/SonicMania_Objects_Global_Player.h:46` -- `ANI_FAN` enum value =
  **25** (0-indexed; NON-PLUS build, no `ANI_FLUME` before it).
- `tools/_decomp_raw/SonicMania_Objects_Global_Player.c:794,799,840,845` --
  `Player_StageLoad` loads `Players/Sonic.bin` + `Players/Tails.bin` (SCOPE_STAGE).
  The NORMAL player palette is NOT loaded by a separate LoadPalette (only the SUPER
  palette uses SetPaletteEntry, :476-521) -- the sprite pixels index `fullPalette[0]`.

## MEASURED frame set (audit per build_heavy_atlas.py model)
- ANI_FAN = anim 25, 10 frames, on Sonic1.gif / Tails1.gif (`parse_bin`):
  - Sonic Fan f300..f309, rects ~44x24 on Players/Sonic1.gif.
  - Tails Fan f241..f250, rects ~42x21 on Players/Tails1.gif.
- Player .bin: Sonic 547 frames / 53 anims; Tails 524 frames / 54 anims. We rewrite the
  FULL anim/frame STRUCTURE (so `SetSpriteAnimation(aniFrames, 25, ...)` indexes
  correctly) but ONLY the ANI_FAN frames carry real atlas rects; every other frame is
  clamped to a 0x0 transparent rect (mirrors build_heavy_atlas.py unused-frame clamp).
  => the atlas holds only ~20 small rects (10 Sonic + 10 Tails) ~= ONE ~512-wide band-
  aligned 8bpp sheet, far under the cart store budget.

## CRAM palette scheme (DOC-CITED -- R3.3 collision-proof)
- Sprite path: SPCTL=0x23 (Sprite Type 3 = full 11-bit DC; ST-058-R2 Fig 9.1 p201:
  "Type 3 = SD PR1 PR0 CC1 CC0 DC10..DC0"), SPCAOS=0 (CRAOFB; ST-058-R2 sec 10.1
  p203-205). => a VDP1 8bpp sprite's CRAM address = jo `colno` (CMDCOLR high byte;
  ST-013-R3 sec 6.4) + char-pixel. `jo_sprite_set_palette(n)` sets colno = n*256
  (jo/sprites.h:380 `JO_MULT_BY_256`). So colno=block*256 routes pixels to a 256-
  aligned CRAM block.
- CRAM budget (mode 1 = 2048 entries, indices 0..2047):
  - bank0 FG = CRAM[0..255] (block 0).
  - bank1 VDP1 sprites = CRAM[256..511] (block 1, p6_pal_mirror = fullPalette[0]).
  - Heavies (Tier-B.2, DONE) = blocks 2..6 = CRAM[512..1663].
  - **ONE free 256-aligned block remains: block 7 = CRAM[1792..2047].** (block 8 =
    2048 OVERFLOWS the 2048-entry CRAM.)
- MEASURED no-conflict (the key result): the ANI_FAN frames of Sonic and Tails use
  DISJOINT palette indices except {0,1,16,18}, and at those 4 the colors are IDENTICAL
  (0=key,1=black,16=(64,0,0),18=(224,0,0)). Sonic-Fan indices {0,1,8-18,40,64-69};
  Tails-Fan indices {0,1,16,18,34-38,41,70-75}. Real conflicts = ZERO.
  => a SINGLE MERGED 256-entry palette (Sonic's color at each Sonic index, Tails'
  color at each Tails index) renders BOTH players faithfully from ONE CRAM block.
- WHY a dedicated block is REQUIRED (not the default bank1/fullPalette[0]): MEASURED --
  GameConfig.bin global palette (mirrored to CRAM bank1) CONTAINS the Sonic colors
  (teal ramp + skin) but is MISSING Tails' fur/orange ramp (indices 71-75: (176,32,0)
  (208,80,0)(224,120,0)(232,144,0)(240,168,0) -- NONE present in GameConfig). So the
  default path would render Tails' body with wrong colors. A dedicated merged block
  guarantees correct colors for both.
- Block 7 chosen = CRAM[1792]. The merged palette ships as cd/PLRPAL.BIN (256 BGR555,
  0x8000|bgr, big-endian) uploaded ONCE to CRAM[1792..2047] (disjoint from FG bank0,
  sprite bank1, AND all 5 Heavy blocks -> R3.3-safe). p6_vdp2 player-pal upload mirrors
  p6_vdp2_hbh_pal_upload (raw CRAM write, no jo +1 pre-shift -- sprite reads CRAM
  [block+pixel] directly).

## Staging (mirror the Heavies exactly)
- cd/PLROBJ.SHT -- selective player atlas (SHB1 banded, build_sheet_bands format),
  staged into a DEAD SaturnSheet slot (see Audit-slot below). Path hash =
  "Cutscene/Players.gif" (the single sheet name the rewritten Sonic.bin/Tails.bin
  reference). Staged BANDED, NO MakeResident (RES-store-overflow-safe, like AIZOBJ/HBH).
- cd/PLROBJ.PAK -- Sonic.bin + Tails.bin rewritten (sheet -> "Cutscene/Players.gif",
  FAN rects -> atlas coords). Loaded into the CART OBJANIMPAK window ALONGSIDE
  HBHOBJ.PAK by COMBINING: build_player_atlas.py rebuilds the cutscene-object pack as
  HBHOBJ.PAK = {5 Heavy bins} + {Sonic.bin, Tails.bin} (the Heavy SHT/PAL/atlas stay
  BYTE-IDENTICAL; only player bins are APPENDED -> Heavy hash-resolution unaffected,
  qa_ghzcut_heavies stays GREEN). Animation.cpp resolves Sonic.bin/Tails.bin from
  paks[1]=P6_HW_OBJANIMPAK by hash -> aniFrames resolves -> the arm_env bind loop binds
  Cutscene/Players.gif's gfxSurface (saturnSheetSlot) to a VDP1 handle (P6_VDP1_NSHEETS
  =12 has room). [Rationale: paks[0]=P6_HW_ANIMPAK is WRAM-H and the front-end .bss
  OVERLAPS it (_end ~0x060ba1d0 > 0x060B6C00), so paks[0] is UNUSABLE; the cart
  OBJANIMPAK is the only resident pack window, and combining avoids a fragile new cart
  window in the tight 0x22700000-0x227A0000 GFS/TMP region.]
- The players are SLOT_PLAYER1/2 (0/1), already REGISTERED + ticking by the engine --
  this is purely staging+binding their texture + routing their palette block. NOT a
  RegisterObject (the CutsceneHBH 592 B pool-cap that blocked the Heavies does NOT apply).

## Draw-block routing
- The engine draws the players via Player_Draw -> DrawSprite -> p6_vdp1_blit_flipped
  with the DEFAULT block 1. To route them to block 7, wrap the per-entity draw dispatch
  (Object.cpp:1262-1264) under `#if defined(P6_GHZCUT_BOOT)`: if the entity's classID ==
  Player's classID, set `p6_heavy_palblock = P6_PLAYER_PALBLOCK (7)` before draw(),
  reset to 1 after. Both players share the ONE merged block, so no per-character logic.
  `#else` keeps Object.cpp byte-identical (GHZ shipping unaffected).

## ===== PROACTIVE-DETECTION-CHECKLIST (4 audits) =====
### Audit 1 -- Layering / Z-order
Players draw in their normal player drawGroup (engine-assigned, unchanged). We do NOT
re-order any draw; the cutscene's existing entity Z (Heavies/claw/platform/ruby on the
King-claw, players in the player drawGroup) is the decomp's. No new draw call inserted --
the engine already dispatches Player_Draw; we only swap its CRAM block. So Audit-1 = no
Z change (the players were ALREADY being dispatched as black squares at the correct Z).

### Audit 2 -- Animation cadence
ANI_FAN: speed=80, 10 frames. Per-frame durations from the .bin (rewritten verbatim).
The engine's own ProcessAnimation advances the animator (we do NOT hand-walk it -- the
real Player object ticks). Cadence = decomp-exact by construction (the engine reads the
rewritten .bin's speed/duration). Cycle = sum(duration)/speed at 60Hz, matched to the
decomp within 0% (same bytes). No Saturn-side walker.

### Audit 3 -- Pivot / flip composite
Each player frame carries its OWN pivot (px,py) copied verbatim from the source .bin
into PLROBJ.PAK. The engine applies the RSDK FLIP_X formula per-sprite
(Drawing.cpp:2796-2808 world_topleft = x - width - pivotX) via p6_vdp1_blit_flipped.
No parent/child flip composite (the players are independent single sprites). Pivots
preserved byte-exact -> position math is the decomp's.

### Audit 4 -- Boot-delay budget (MEASURED, build_player_atlas.py output)
Assets ADDED to the GHZCutscene staging/seam path (NOT the cold-boot path):
- PLROBJ.SHT = 6,673 B -> 6673/1024/150 = 0.043 s + 1 seek 0.1 s = ~0.14 s.
- PLRPAL.BIN = 512 B -> ~0.003 s + 1 seek 0.1 s = ~0.10 s.
- HBHOBJ.PAK grew 30,024 -> 72,772 B (+42,748 B for Sonic.bin + Tails.bin) = +0.28 s on
  the ALREADY-existing seam load (0 NEW seeks; the pack is already read at the seam).
Cumulative add ~= 0.52 s << 5 s. PLROBJ.SHT raw band = 0x2000 (MakeResident-safe).
HBHOBJ.PAK 72,772 B fits the 256 KB P6_HW_OBJANIMPAK window with huge margin.

### Audit-slot -- SaturnSheet store slot (MEASURED from RED baseline savestate)
RED capture (tools/_portspike/_ghzcut_players_RED.mcs): p6_w_sht_staged = 10 ->
the front-end stages 10 SaturnSheet slots (title sheets 0..7 + AIZOBJ=slot 8 + HBHOBJ=
slot 9). SATURNSHEET_SLOTS=16 -> 6 FREE slots. PLROBJ.SHT stages SEQUENTIALLY (SaturnSheet
_Stage uses s_count++, platform/Saturn/SaturnSheet.cpp:194,203) -> slot 10 (the 11th).
NO grow to slot 16 (=#228 trap). Band-store bytes: front-end (P6_FRONTEND_MENU implies
TITLE) uses the 0x22720000 base = 896 KB store (SaturnSheet.cpp:115); the title sheets +
AIZOBJ + HBHOBJ + 6,673 B PLROBJ fit with huge margin. VDP1 bind handle: P6_VDP1_NSHEETS
=12; the arm_env bind loop binds Cutscene/Players.gif's surface (1 handle) -> demand stays
under 12.

## Files created / modified
- NEW tools/build_player_atlas.py -- builds PLROBJ.SHT + PLRPAL.BIN (merged 256-color)
  + rebuilds HBHOBJ.PAK to include Sonic.bin/Tails.bin (Heavy SHT/PAL byte-identical).
- NEW tools/_portspike/qa_ghzcut_players.py -- the RED->GREEN render gate.
- MOD tools/_portspike/_p6/p6_vdp2.c -- p6_vdp2_player_pal_upload (CRAM[1792]).
- MOD tools/_portspike/_p6/p6_io_main.cpp -- stage PLROBJ.SHT + upload PLRPAL.BIN in the
  GHZCUT staging block (mirror HBHOBJ); add player witnesses for the gate.
- MOD rsdkv5-src/RSDKv5/RSDK/Scene/Object.cpp -- the Player draw-block wrap
  (#if P6_GHZCUT_BOOT; #else byte-identical).
- MOD tools/_portspike/_p6/build_p6scene_objs.sh -- `-u` force the player witnesses.

## GATE (tools/_portspike/qa_ghzcut_players.py) -- a RENDER gate, NOT a proxy
- MEASURES (savestate-primary + screenshot-binding):
  - p6_w_plrsht_slot >= 0 (PLROBJ.SHT staged + hashed).
  - the GHZCutscene-cutscene player aniFrames >= 0 (Sonic.bin resolved from the pack).
  - p6_w_plr_cut_landed > 0 (player-region VDP1 blits landed this frame).
  - CRAM[1792..1792+N] == PLRPAL.BIN (the merged palette landed in block 7, correct
    colors -- byte-match the authoritative uploaded bytes, NOT a heuristic).
  - the player VDP1 pixel-mass at the cutscene player coords (the on-screen symptom).
  - cont_frames > 0.
- RED (current build): no PLROBJ staging -> slot < 0, aniFrames < 0, 0 player pixel-mass.
- GREEN (after fix): slot >= 0, aniFrames >= 0, CRAM block matches PLRPAL.BIN, player
  pixel-mass > 0 at the coords + the SCREENSHOT shows recognizable Sonic+Tails (blue
  Sonic, orange Tails), NOT black squares, NOT magenta.
- Capture: P6_GHZCUT_SEAMTEST + P6_GHZCUT_HOLD=1 P6_GHZCUT_HOLD_WHITE=0 (fade cleared,
  cutscene held in the twirl), burst-capture a PNG, VIEW it.

## CONSTRAINTS
- All new code `#if defined(P6_GHZCUT_BOOT)` -> GHZ shipping (`_end 0x060b5a40`) + plain
  P6_AIZ_TEST byte-identical (0 player-sheet-staging symbols). Do NOT grow
  SATURNSHEET_SLOTS/NSHEETS (#228 trap) -- reuse a dead slot.
- Heavies (qa_ghzcut_heavies) MUST stay GREEN (Heavy SHT/PAL byte-identical).
- `python` not `python3`; `pwsh` via Bash. ONE Docker build at a time;
  `> _playersbuild.log 2>&1` + `rm -f game.iso`. After build:
  `python tools/build_cdda.py cd_audio/track02.wav cd_audio/track03.wav --cue-out game.cue --iso game.iso`.
  Do NOT commit. No emoji. Up to ~5 build iterations.

## ===== OUTCOME (2026-06-30) =====
ROOT-CAUSE FIX DONE + WITNESS-PROVEN; the BINDING SCREENSHOT is BLOCKED by a separate
cutscene VDP1-character-visibility issue (affects the Heavies too). MEASURED state:

STAGING/BINDING/PALETTE/ANIM = ALL GREEN (gate qa_ghzcut_players.py, savestate
tools/_portspike/_ghzcut_players_60.mcs at cont=108):
- p6_w_plrsht_slot = 10 (PLROBJ.SHT staged + hashed; front-end uses 0..9, slot 10 free;
  SATURNSHEET_SLOTS=16, NO #228 grow). build_player_atlas.py: PLROBJ.SHT 6,673 B
  (atlas 512x80, 5 bands, raw band 0x2000), PLRPAL.BIN 512 B, HBHOBJ.PAK 30,024->72,772 B
  (binCount 5->7 = +Sonic.bin +Tails.bin).
- p6_w_plr_cut_anif = 1 AND p6_w_plr_cut_anif2 = 1 -> Sonic.bin AND Tails.bin resolved from
  HBHOBJ.PAK (paks[1]) -> aniFrames RESOLVED (the reported "unresolved aniFrames / black
  squares" ROOT CAUSE is FIXED).
- p6_w_plr_cut_aniid = 25 (ANI_FAN) -> the players are in the cutscene fan-twirl.
- p6_w_plr_cut_surf = 0, p6_w_plr_cut_handle = 0 -> Cutscene/Players.gif bound to VDP1.
- CRAM[1792] = PLRPAL.BIN byte-match 7/7 -> merged player palette in block 7 (the R3.3
  no-collision class; CRAM banks 0/1 verified UNCHANGED -> no clobber from the upload).
- p6_w_draw_calls = 3560 (CUMULATIVE) + draw_nents = 23/frame -> the cutscene render path
  IS live (DrawSprite running).
GHZ BYTE-IDENTICAL: no-flag build _end = 0x060b5a40 (exact baseline), 0 player-staging
symbols in game.map. Heavy SHT/PAL byte-identical (build_player_atlas S6 PASS).

OPEN (the BINDING screenshot): the cutscene CHARACTERS do not appear on-screen in ANY
captured frame (~200 screenshots across HOLD + DIRECTBOOT; programmatic scan = 0 Sonic-blue,
0 Heavy-red pixels). This affects the HEAVIES TOO (hbh_count=5, hbh_vis onScreen=1, but
absent on screen) -- so it is a SHARED cutscene VDP1-sprite-visibility issue, NOT the player
staging. The GHZ background (palms/checkerboard) renders cleanly; the VDP1 sprites do not
composite even though their world coords are on-screen (Heavy world (196,~900) vs cam Y 780
-> screen (196,~120), within the 320x224 viewport) and draw_nents/draw_calls show them
drawing. Per-frame p6_w_vdp1_landed reads 0 from savestates (capture lands post-present at a
vblank boundary -- the Tier-B.2 _ghzcut_heav capture's vdp1_landed=1843 was a lucky mid-frame
land). Capture obstacles compounded it: (1) the FXRuby held-FadeIn produces a full-screen wash
on some frames (FillScreen/DrawRect INK_TINT -- Tier-B.1 territory; the SW framebuffer is
Saturn-stubbed so these write a dead buffer, yet a wash appears -> likely the VDP2 fade
offset); (2) the DIRECTBOOT GHZCutscene load is ~58-60s + a long FadeIn ramp (cont 130 still
FadeIn) -> the character-reveal/landing window is deep + narrow; (3) the malformed _arc.wav
CueMaker output + leftover-mednafen multi-instance errors repeatedly invalidated captures
(fix: run build_cdda AFTER the build is FULLY done; kill ALL mednafen first).
NEXT (separate sub-task): diagnose why the cutscene VDP1 sprites (players + Heavies) don't
composite in the held/early-FadeIn frames (a present/erase or camera-vs-NBG-scroll
coordination issue) -- re-confirm against the Tier-B.2 _ghzcut_heav_GREEN.png moment. The
player-staging layer is complete + ready; once the shared sprite-visibility is restored the
players render with the correct merged palette automatically.

## ===== CORRECTED ATTEMPT (#2a re-attempt) PLAN (2026-06-30) =====
Branch ghzcutscene-intro-ghz-loop, base = cc9f333 (verified milestone). The tree is a
CLEAN slate: `git diff cc9f333 -- p6_io_main.cpp p6_vdp1.c p6_vdp2.c` == empty; NO player
witnesses anywhere; cd/PLROBJ.SHT + cd/PLRPAL.BIN absent; HBHOBJ.PAK = 30,024 B
(Heavies-only). Build script CONFIRMED: build_p6scene_objs.sh:218-220 compiles Scene_Object.o
from UNMODIFIED Object.cpp with NO -DP6_GHZCUT_BOOT -> the attempt-1 player-draw wrap would
not even compile in. So the corrected route lives ENTIRELY in the Saturn layer.

ARCHITECTURE FINDING (decisive, measured from Animation.cpp:85-86): the engine pak resolver
reads EXACTLY TWO fixed windows: `paks[2] = { P6_HW_ANIMPAK (0x060B6C00 WRAM-H), P6_HW_OBJANIMPAK
(0x22760000 cart 256KB) }`. paks[0] is unusable in front-end (.bss overlaps it). So the player
.bin MUST physically reside in the OBJANIMPAK window ALONGSIDE the Heavy bins -- a literally
separate PLROBJ.PAK at a 3rd address would be INVISIBLE to the engine (and adding a 3rd window
requires editing the shared Animation.cpp = FORBIDDEN). The checklist line 86 already documented
this and chose the combined pack. THEREFORE: the PAK is combined (HBHOBJ.PAK grows to hold the 2
player bins), but the Heavy SHT/PAL stay BYTE-IDENTICAL (build_player_atlas.py S6 enforces it;
build_heavy_atlas.py is UNTOUCHED) and the SHEET is fully separate (PLROBJ.SHT, own SaturnSheet
slot, own CRAM block 7). This removes ALL THREE attempt-1 high-risk elements: (a) the Object.cpp
shared-draw wrap, (b) -DP6_GHZCUT_BOOT on Scene_Object.o, (d) -- only (b-pak) the combined PAK
remains, and the pak resolver is a READ of unmodified Animation.cpp (no engine change).

CORRECTED PALETTE ROUTE (the only new idea vs attempt #1): SURFACE-DRIVEN colno in p6_vdp1.c,
NOT the shared draw loop. p6_vdp1.c already has `p6_heavy_palblock` (set/reset by the Heavies'
OWN Draw shim p6_cuthbh_draw in p6_ovl_ghz.c:826, NOT by the engine loop). The blit functions
p6_vdp1_blit / p6_vdp1_blit_flipped take a `sheet` index (the SaturnSheet slot bound to a VDP1
handle). In those functions, AFTER p6_slot_for resolves the jid, detect `sheet == s_plr_sheet`
(the PLROBJ.SHT slot, captured at stage time) and override the colno to 1792 (block 7) for THAT
blit only; every other surface keeps P6_BLIT_PALBLOCK. This is surface-driven, lives in the
Saturn VDP1 layer, and never touches ProcessObjectDrawLists. Cite ST-013-R3 sec 6.4 (CMDCOLR
high-byte = colno) + ST-058-R2 sec 10.1 (SPCTL Type-3 full-11-bit DC, SPCAOS=0 -> colno = CRAM
block base).

BUILD SEQUENCE (RED-first; the BINDING proof is the screenshot, not witnesses):
- BUILD 1 (baseline/clean reference): build the CURRENT clean milestone, P6_GHZCUT_BOOT=1
  P6_GHZCUT_HOLD=1 P6_GHZCUT_HOLD_WHITE=0 (direct-boot GHZCutscene, fade cleared). Capture
  `pwsh tools/qa_boot.ps1 -Cue game.cue -Wait 42 -Every 2 -Shots 42 -Out _pv.png`. CONFIRM the
  late shots (_pv_30.._pv_42) show Heavies + King Claw render (= the clean reference, mirrors
  _ghzcut_heav_GREEN.png). This proves the capture method + the milestone baseline BEFORE any
  player change. NO players yet (RED for the gate: plrsht_slot absent).
- BUILD 2 (the fix): run build_player_atlas.py (writes PLROBJ.SHT + PLRPAL.BIN + combined
  HBHOBJ.PAK, S6 keeps Heavy SHT/PAL byte-identical). Add to the Saturn layer:
  * p6_io_main.cpp: stage PLROBJ.SHT into the next free SaturnSheet slot (mirror the HBHOBJ.SHT
    block ~4288); SetHash "Cutscene/Players.gif"; capture the slot into a file-scope
    `p6_plr_sheet_slot` AND the witness `p6_w_plrsht_slot`. Upload PLRPAL.BIN -> CRAM[1792] via a
    new p6_vdp2_player_pal_upload. Mirror the staging in BOTH the direct-boot reload
    (p6_ghzcut_reload ~6347) and the live seam (~6565) where HBHOBJ.PAK is loaded. Add the
    plr_cut_* witnesses (anif/anif2/aniid/surf/handle/landed) the gate reads.
  * p6_vdp1.c: add `extern int p6_plr_sheet_slot;` + the surface-check override in both blit fns.
  * p6_vdp2.c: add p6_vdp2_player_pal_upload (256 colors -> CRAM[1792..2047]), mirror
    p6_vdp2_hbh_pal_upload.
  * build_p6scene_objs.sh: -u force the new player witnesses (mirror the hbh -u line 696).
  Capture the SAME way. CONFIRM Sonic (blue) + Tails (orange) + the 5 Heavies + the King Claw
  ALL render with correct colors. Name the proof _ghzcut_players_FIXED.png.
ISOLATION: if BUILD 1 shows Heavies but BUILD 2 does not, the combined PAK or my Saturn-layer
additions regressed it -> bisect (re-test with PLROBJ staging present but the combined PAK
reverted to Heavies-only) and REPORT the measured state -- do NOT fall back to the Object.cpp
wrap. If the players won't render correct-colored without the shared draw loop, STOP + report.

CONSTRAINTS RE-AFFIRMED: all new code `#if defined(P6_GHZCUT_BOOT)`; GHZ shipping byte-identical
(_end 0x060b5a40, 0 player symbols in the no-flag map); do NOT grow SATURNSHEET_SLOTS/NSHEETS;
do NOT touch Object.cpp or any shared RSDKv5 engine file; do NOT add -DP6_GHZCUT_BOOT to
Scene_Object.o. ONE Docker build at a time, `> _pbuild.log 2>&1` + `rm -f game.iso`, kill
mednafen before captures, build_cdda AFTER each build. <=5 builds.

## ===== ROOT CAUSE FOUND (2026-06-30, corrected attempt) =====
BISECT (empirical, screenshot-verified):
- BUILD 1 (clean milestone cc9f333, no players): Heavies + King Claw RENDER (baseline
  _ghzcut_baseline_heavies.png == _ghzcut_heav_GREEN.png). Capture 912x749 late shots (38-42).
- BUILD 3 (COMBINED 72KB HBHOBJ.PAK + all Saturn-layer code): ALL cutscene VDP1 sprites GONE
  (Heavies + Claw + players). Savestate: witnesses ALL GREEN (plrsht_slot=10, plr_cut_anif=1,
  aniid=25, CRAM[1792] 7/7, hbh_aniframes=20, hbh_handle=2) BUT p6_w_vdp1_landed=0 and
  p6_w_vdp1_drops=8818==draw_calls -> EVERY blit dropped in p6_slot_for (the GREEN-witness-
  blank-screen trap).
- BUILD 4 (Heavies-ONLY 30KB PAK + all Saturn-layer code UNCHANGED): Heavies + Claw RENDER;
  savestate p6_w_vdp1_landed=4093 + players still resolved (plr_cut_anif=1). => the Saturn-layer
  code (PLROBJ staging + p6_vdp1.c surface route + p6_vdp2 palette + overlay witnesses) is CLEAN;
  the regression is 100% the COMBINED PAK SIZE.
ROOT CAUSE (measured): P6_HW_OBJANIMPAK = 0x22760000 (Animation.hpp), where HBHOBJ.PAK loads,
sits INSIDE the front-end SaturnSheet band store region 0x22720000..0x22800000 (SaturnSheet.cpp:115).
The 30KB Heavy pak (0x22760000..0x22767800) fits WITHOUT collision; the 72KB combined pak's 42KB
TAIL (0x22767800..0x22771000) OVERLAPPED that region -> corrupted the banded fetch of the Heavy/
claw sheets -> p6_slot_for FetchRect returned garbage -> every cutscene VDP1 sprite dropped. NOT
the player draw logic (Build 4 draws with players resolved), NOT the Object.cpp wrap (never
touched). CONFIRMS the task's directive #2 ("separate PLROBJ.PAK, not a HBHOBJ rebuild") + the
memory's attempt-1 combined-PAK suspicion -- as a MEMORY OVERLAP, not a code bug.
FIX (respects the 2-window Animation.cpp paks[2] -- a truly-separate PLROBJ.PAK at a 3rd address
is invisible to the engine, and paks[0]=WRAM-H overlaps .bss): SHRINK the player bins to FAN-ONLY.
build_player_atlas.py now emits ONLY the frames of the anims the cutscene displays (Idle=0 + FAN=25);
every other anim keeps its HEADER (so SetSpriteAnimation index 25 stays valid) but fc=0 -> no frames.
Sonic 547->11 frames, Tails 524->11 frames. Combined HBHOBJ.PAK 72,772 B -> 33,960 B (0x22760000..
0x22768498) -- only 4KB past the proven-safe 30KB Heavy footprint, WELL short of the collision.
Heavy SHT/PAL byte-identical (S6). Default ON; PLR_FULLFRAMES=1 reproduces the 72KB RED.
BUILD 5 = the FAN-only combined pak; capture pending.
