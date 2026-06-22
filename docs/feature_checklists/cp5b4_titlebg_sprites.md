# CP5b.4 — un-gate TitleBG + Title3DSprite sprites (Part 4a) + Mode-7 island (Part 4b STRETCH)

Date: 2026-06-21. Builds on bed5bac (backdrop renders; sprites GATED OFF).

## Mission

Un-gate the verbatim-decomp `TitleBG` (Mountain1/2, Reflection, WaterSparkle,
WingShine) + `Title3DSprite` (MountainL/M/S, Tree, Bush billboards) so they
register + render on the engine Title scene, WITHOUT regressing the proven
foreground (logo + Sonic) or the Saturn-native backdrop (sky/clouds/island).

## Decomp sources (authoritative, verbatim)

- `tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c` (203 L) — the real
  TitleBG. Update slides sprites; Draw = `RSDK.DrawSprite`; SetupFX flips
  visibility + assigns scanlineCallbacks + SetPaletteMask.
- `tools/_decomp_raw/SonicMania_Objects_Title_Title3DSprite.c` (90 L) — the real
  Title3DSprite. Update computes relativePos/zdepth from `TitleBG->angle`;
  Draw scales + positions a billboard via `RSDK.DrawSprite(&animator,&drawPos,true)`.
- These compile to `Game_TitleBG.o` / `Game_Title3DSprite.o`
  (build_p6scene_objs.sh:374-383, only under `P6_FRONTEND_TITLE`).

## Measured root cause of the destabilization (the gate the prior agent hit)

`TitleBG_SetupFX` (called from `TitleSetup_State_FlashIn`) does FIVE things:
1. `GetTileLayer(0..3)->drawGroup[0] = ...` — reorders tile-layer draw groups.
2. `cloudLayer->scanlineCallback = TitleBG_Scanline_Clouds`
   `islandLayer->scanlineCallback = TitleBG_Scanline_Island`.
3. `foreach_all(TitleBG){visible=true}` + `foreach_all(Title3DSprite){visible=true}`.
4. `SetPaletteEntry(0,55,0x00FF00)` + `SetPaletteMask(0x00FF00)`.
5. `SetDrawGroupProperties(2, true, StateMachine_None)` (sort drawgroup 2).

MEASURED facts (citations):
- `scanlines` is now allocated (cart 0x22400000, p6_title_reload, bed5bac) so the
  callbacks no longer wild-write. BUT both scanline callbacks call
  `RSDK.SetClipBounds`: Clouds -> `(0,0,0,size.x,SCREEN_YSIZE/2)` = y[0,120];
  Island -> `(0,0,168,size.x,SCREEN_YSIZE)` = y[168,240]. (TitleBG.c:117,140.)
  `SetClipBounds` writes `currentScreen->clipBound_*` (Drawing.hpp:379-391).
- The Saturn `DrawSprite` clip-accept (`p6_draw_flipped`, p6_io_main.cpp:1576-1577)
  REJECTS any sprite outside `clipBound_*`. The callbacks run per-drawgroup inside
  `ProcessObjectDrawLists` (Object.cpp:1192-1199), so the restrictive clip set by
  one group's layer loop persists into entity draws of subsequent layers/state ->
  the foreground logo/Sonic-head get clipped intermittently == the "alternating
  island/logo frames; head vanishes" the prior agent measured.
- `SetPaletteMask(maskColor)` is INERT on Saturn: `maskColor` (p6_pack_stubs.cpp:58)
  is read ONLY by the `DRAW_SPRITE_MASKED` macro in the software rasterizer
  (Drawing.cpp:224-231), which is NOT compiled on Saturn (RETRO_PLATFORM gate).
  The Saturn DrawSprite already early-returns INK_MASKED (p6_io_main.cpp:1609).
- `SetPaletteEntry(0,55,0x00FF00)` writes `fullPalette[0][55]` to green. The
  backdrop CRAM is built from `fullPalette[0]` ONCE at load (present_title_backdrop,
  before FlashIn runs), and the per-frame `title_backdrop_scroll` does NOT re-upload
  CRAM -> the already-uploaded backdrop is unaffected. (Index 55 in the Title
  tileset palette = the cloud-shimmer slot; the magenta-key index is 0.)
- The `GetTileLayer drawGroup[0]` reassignment has NO Saturn render consumer: tile
  layers render via the independent VDP2 backdrop driver, NOT via the engine's
  DrawLayer* software path (Saturn-gated off, Object.cpp:1203). Inert for render.

CONCLUSION: the ONLY harmful side-effects of SetupFX on Saturn are the two
`scanlineCallback` assignments (their per-frame SetClipBounds corrupts the FG
clip). Everything else is inert. The fix = neuter the scanlineCallback assigns
(leave the callbacks defined but never installed), KEEP the visibility flips +
palette setup. The drawGroup reassignment is inert but harmless to keep verbatim.

## Plan (Part 4a)

A. Make `TitleBG_SetupFX` Saturn-safe WITHOUT editing the verbatim decomp .c:
   the overlay's closure-edge stub for `TitleBG_SetupFX` currently no-ops it
   entirely (p6_ovl_ghz.c:128-129). When `P6_TITLEBG_SPRITES` is defined we link
   the REAL SetupFX. To neuter ONLY the scanline assignment without touching the
   verbatim file, guard the two `layer->scanlineCallback = ...` lines AND the
   `SetClipBounds` in the two Scanline_* functions behind `RETRO_PLATFORM ==
   RETRO_SATURN`. The decomp .c is compiled in-tree (we control its build), and
   editing the cached decomp raw breaks the "verbatim" contract — so instead:
   neuter at the consumer. Two options measured:
     (i)  Guard the scanlineCallback assignment in TitleBG.c behind a Saturn `#if`.
     (ii) Leave SetupFX verbatim; make the Scanline_* callbacks Saturn-safe by
          NOT calling SetClipBounds on Saturn (early-return body under the Saturn
          gate) so even when installed they don't corrupt the FG clip.
   CHOICE: (ii) — keep SetupFX 100% verbatim (visibility flips + drawGroup +
   palette all run), and make the two Scanline_* bodies a Saturn no-op (they have
   no Saturn VDP2 consumer anyway). This is the minimal, decomp-faithful neuter:
   the callbacks still get installed + called, but they no longer write the FG
   clip. Implement via a tiny Saturn `#if` at the top of each Scanline_* fn that
   `return`s before SetClipBounds. (The decomp raw is OUR cached copy; the guard
   is a Saturn-platform adaptation, documented + cited, mirroring the dozens of
   `RETRO_PLATFORM == RETRO_SATURN` guards already in rsdkv5-src.)

B. Remove the registration gate: flip the default so `P6_FRONTEND_TITLE` links
   `Game_TitleBG.o` + `Game_Title3DSprite.o` and registers both objects. Keep the
   `P6_TITLEBG_SPRITES` knob as an A/B-OFF override (so a regression can be
   bisected), but make the DEFAULT for the Title flavor = sprites ON.

C. inkEffect: thread INK_BLEND/INK_ADD -> VDP1 half-transparency (CL_Trans,
   ST-013-R3 §5.5.4; jo `jo_sprite_enable_half_transparency`, effect=0x3,
   sprites.h:180). MEASURE honestly: VDP1 PMOD half-transparency blends with what
   is already in the VDP1 FRAMEBUFFER (other sprites), NOT with the VDP2 backdrop
   (the island is VDP2 NBG1). So mountain-over-backdrop translucency is NOT
   reproduced by VDP1 PMOD alone (that needs VDP2 color-calc CCRTL on the sprite
   layer). REPORT the exact behavior. INK_MASKED stays skipped (verbatim WingShine
   is the only user; user rejected the stripe boxes).

## Gate FIRST (RED on bed5bac, GREEN after)

`tools/_portspike/qa_titlebg_sprites.py`:
- V1 (savestate): `p6_w_titlebg_classid` > 0 AND `p6_w_title3d_classid` > 0
  (registered) AND `p6_w_tbg_handle` >= 0 (TBG.SHT bound). RED baseline: all -9.
- V2 (savestate): a count of TitleBG+Title3DSprite entities flipped visible (the
  SetupFX foreach). RED baseline: 0 (objects not registered).
- V3 (SCREENSHOT, primary): the island region gains mountain/billboard structure
  -- measure a DISTINCT-from-flat-green signal (mountain gray/brown + increased
  structured-edge pixel count) in the island band vs the bed5bac flat-green
  baseline. The orchestrator OPENS the PNG and compares to the real Mania title.
- MUST-NOT-REGRESS: re-run qa_title_logo + qa_title_sonic (FG intact).

## Audits (binding §4.5.1)

### Audit 1 — Z-order (Title3DSprite uses engine zdepth sort)
Title3DSprite drawGroup=2 (Create:52), SORTED by zdepth (SetDrawGroupProperties(2,
true,...)); each entity's zdepth = relativePos.y (Update:19) -> the engine bubble-
sort in ProcessObjectDrawLists (Object.cpp:1155-1165) orders them back-to-front.
On Saturn the per-sprite VDP1 Z comes from p6_vdp1_blit's fixed Z (450) — all FG
sprites share Z; ordering is INSERTION order = the engine's sorted drawlist order.
So the engine sort already produces correct back-to-front billboard ordering as
long as the entities draw in drawlist order (they do — ProcessObjectDrawLists
iterates list->entries[] post-sort). TitleBG mountains drawGroup=1 (behind
Title3DSprite drawGroup=2); WingShine drawGroup=4. drawGroup order is honored by
the engine (drawGroups processed 0..N). MEASURED placements: 9 TitleBG + 58
Title3DSprite (parse_title_entities.py).

### Audit 2 — Animation cadence
TitleBG sprites: animator from `Title/Background.bin` anim[type] (Create:61), no
per-frame advance in Update (static frame per type) -> no cadence walker needed.
Title3DSprite: SetSpriteAnimation(...,5,...,frame) — anim 5, frame = the entity's
`frame` field (the billboard kind); static. Palette rotation (140-143 every 6
ticks, StaticUpdate:42-45) runs via the engine StaticUpdate -> RotatePalette ->
fullPalette; NOT auto-pushed to CRAM on Saturn (the backdrop CRAM is uploaded once
+ the sprite CRAM is bank 1). Cadence: none of these are per-frame animated frames;
the visible motion is TitleBG sprites sliding (Update) + Title3DSprite orbiting
(via TitleBG->angle in StaticUpdate). No walker mismatch risk.

### Audit 3 — Pivot+flip composite
TitleBG drawFX=FX_FLIP but direction is NEVER set (stays FLIP_NONE) -> the FX_FLIP
arm takes the FLIP_NONE branch (p6_io_main.cpp:1727-1729): top-left = pos+pivot.
No flip composite. Title3DSprite drawFX=FX_NONE (Create:53) + screenRelative=true
(Draw:38): drawPos is screen-space; FX_NONE arm = pos+pivot. No flip math.

### Audit 4 — Boot-delay budget
TBG.SHT (Title/BG.gif, 256x256 = 65,536 B decoded) is ALREADY staged in bed5bac
(P6_LT_MARK(5), p6_io_main.cpp). Registering the objects adds NO new synchronous
asset load (both StageLoad call LoadSpriteAnimation("Title/Background.bin") which
binds the already-staged sheet). Boot delta ~= 0. (The .bin anim metadata parse is
small.) Budget: under the 5s ceiling, no change.

## OUTCOME (MEASURED, 2026-06-22)

SHIPPED (Title flavor on disk, P6_FRONTEND_TITLE):
- TitleBG (Mountain1, Mountain2, Reflection, WaterSparkle) REGISTERED + RENDERED.
  MEASURED: titlebg_classid=9, titlebg_vis=9, tbg_handle=2. The distant mountain
  ridge + reflections/water-sparkle render behind the logo. WingShine stays skipped
  (INK_MASKED, the user-rejected stripe boxes).
- The FOREGROUND IS INTACT: Sonic head sonic_head=10778 (== the gated-off baseline
  10667), full SONIC MANIA logo (logo=208870). The title is STABLE.
- SetupFX made Saturn-safe: the per-frame scanlineCallback INSTALL is #if-guarded
  OUT on Saturn (verbatim TitleBG.c:119-122, P6_FRONTEND_TITLE) -- MEASURED via
  objdump: Game_TitleBG.o has ZERO relocations to the Scanline_* fns. The
  visibility flips + palette setup are kept.

DEFERRED (behind flags, NOT shipped -- HONEST accounting):
- Title3DSprite (the 58 MountainL/M/S/Tree/Bush billboards): CONFIRMED to render a
  rich island (mountains/trees on the floating island) when enabled, BUT registering
  it CORRUPTS the FG -- the Sonic head + emblem scramble to the bottom of the screen
  (MEASURED A/B ISOLATION: with ONLY TitleBG, sonic_head=10763; adding Title3DSprite
  -> sonic_head=0 and FG scattered). Root cause not yet found (its orbiting Draw
  math islandSize*relativePos/depth, or its 58-sprite draw-path interaction; the
  entity logic is correct -- savestate shows visible+onscreen -- so it is a
  render-path issue). Behind P6_TITLE3D_ON.
- INK_BLEND/INK_ADD half-transparency: MEASURED that wiring VDP1 CL_Trans via jo's
  sticky half-transparency attribute (jo_sprite_enable_half_transparency, effect 0x3)
  LEAKS onto the FG and drops the Sonic head (sonic_head 10778 -> 702 with ink on).
  SHIPPING OPAQUE (the honest fallback the task allows). The ink code is behind
  P6_TITLE_INK for a future fix. (Hardware note: even if the leak were fixed, VDP1
  PMOD half-transparency blends only with OTHER VDP1 sprites in the framebuffer, NOT
  the VDP2 island backdrop -- a true mountain-over-island translucency needs VDP2
  color-calc CCRTL on the sprite layer, ST-058-R2 sec 12.)

## Part 4b (STRETCH) — live Mode-7 island rotation
NOT DONE. The static VDP2 backdrop (sky/clouds/green island) is unchanged from
bed5bac. The live Mode-7 rotation (RBG0 + KTBL coefficient table + line-scroll) was
not attempted -- Part 4a's FG-stability work consumed the budget, and 4b depends on
the Title3DSprite/scanline path that is deferred. Reported, not shipped.

## Part 4b (ORIGINAL plan, for the follow-on)
Feed `TitleBG_Scanline_Island`'s per-row affine into VDP2 RBG0 + KTBL coefficient
table (ST-058-R2 §6.4; DEMOCOEF sample MAIN.C); `Scanline_Clouds` into N0LSCX/
N0LSCY line-scroll (ST-058 §5.3). ONLY if 4a is solid AND it lands without
destabilizing. Otherwise LEAVE the static backdrop + REPORT 4b not done.

## Build / verify
- `MSYS_NO_PATHCONV=1 docker run --rm -v "D:/sonicmaniasaturn":/work -w /work
  -e P6_FRONTEND_TITLE=1 joengine-saturn:latest bash tools/_portspike/_p6/build_shipping.sh`
  + host CD-DA `python tools/build_cdda.py cd_audio/track02.wav cd_audio/track03.wav
  --cue-out game.cue --iso game.iso`.
- grep game.map + .o mtimes (build_p6scene_objs.sh swallows compile errors).
- Default GHZ must stay `_end=0x060B6BA0` + R0-R16 (flag-gate all additions).
- Re-capture tools/_portspike/_title_backdrop.png AFTER (showing the sprites).
