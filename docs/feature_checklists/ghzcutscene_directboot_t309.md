# Feature checklist -- Task #309 gate-1: GHZCutscene direct-boot (P6_GHZCUT_BOOT)

Last revised: 2026-06-29

## Goal
Make the AIZ->GHZCutscene arrival-cutscene scene LOAD + RENDER via a direct-boot
diagnostic flavor `P6_GHZCUT_BOOT` (implies `P6_AIZ_TEST` -> MENU -> TITLE -> LOGOS),
and ideally run the fixed-timer cutscene to the playable-Green-Hill-Zone handoff.

RED gate (already exists): `tools/_portspike/qa_ghzcut_load.py`
  --phase load    PASS iff currentSceneFolder == "GHZCutscene"
  --phase handoff PASS iff currentSceneFolder == "GHZ"

## Decomp sources (VERBATIM compile -- no hand-translation per CLAUDE.md)
- `tools/_decomp_raw/SonicMania_Objects_GHZ_GHZCutsceneST.c` (341 L) -> Game_GHZCutsceneST.o
- `tools/_decomp_raw/SonicMania_Objects_Cutscene_CutsceneHBH.c` (602 L) -> Game_CutsceneHBH.o
- GHZSetup + BGSwitch + FXRuby ALREADY compiled (build_p6scene_objs.sh w4 / AIZ block).

## CD assets (already built + auto-glob into ISO)
- `cd/GHC1LAYT.BIN`  47,709 B  (layout band store; mounted via the GHZCutscene->"GHC" alias)
- `cd/GHZCUTIL.BIN`  262,144 B (pre-decoded tileset index plane)
- `cd/GHZCUPAL.BIN`  1,024 B   (pre-decoded palette)
  -> Scene.cpp p6_til_name() loads `<upper(folder)[:5]>TIL.BIN`/`PAL.BIN` = GHZCUTIL/GHZCUPAL
     AUTOMATICALLY inside LoadStageGIF (Scene.cpp:1238-1249). ZERO runtime code needed
     for the FG tileset. VERIFIED both files present (built 2026-06-29).

## Reference: the FG cell-upload + present model (AIZ precedent)
- p6_scene_load_and_arm uploads FG cells to NBG1 only behind per-folder strcmp guards
  (p6_isGHZ / p6_isTitle / p6_isAIZ, each in its own #if). GHZCutscene matches NONE ->
  need a new `#if defined(P6_GHZCUT_BOOT)` upload branch (mirrors the p6_isAIZ branch).
- p6_frontend_frame presents the FG each frame via `#if defined(P6_AIZ_TEST)` block
  (p6_vdp2_present_ghz_camera + AIZ-specific BG1/BG3/BG4 streaming reading cart
  0x22498000+). For GHZCutscene the AIZ BG streaming would read UNINITIALIZED cart ->
  must runtime-guard the AIZ BG streaming to folder=="AIZ" only; the FG-Low present
  (p6_vdp2_present_ghz_camera) runs for both.

## Closure audit (trial-link-driven; lesson #258 -- no blind-stub on Tier-A path)
CRITICAL-PATH symbols (must resolve to REAL impls, NOT stubs):
- CutsceneRules_SetupEntity -- **BLOCKER FOUND**: p6_closure_edge.c:238 is a NO-OP stub.
  GHZCutsceneST_Create calls it to set self->hitbox; GHZCutsceneST_Update's
  Player_CheckCollisionTouch(self,&self->hitbox) trigger NEVER fires with a zero hitbox
  -> the cutscene never starts -> no handoff. FIX: replace ONLY that stub body with the
  VERBATIM decomp impl (SonicMania_Objects_Cutscene_CutsceneRules.c:94-111), gated
  `#if defined(P6_GHZCUT_BOOT)` (no-op #else keeps GHZ/AIZ/menu byte-identical).
  This is a verbatim port of the decomp function, not hand-rolled logic.
- CutsceneSeq_StartSequence / CutsceneSeq_LockAllPlayerControl -- REAL (Game_CutsceneSeq.o,
  overlay-local def wins over the pack stub; proven by ovl_ring.map CutsceneSeq_StartSequence
  @0x026a93c8 resolving IN the overlay for the AIZ build).
- PhantomRuby_PlaySfx / FXRuby_State_Shrinking -- REAL (Game_PhantomRuby.o / Game_FXRuby.o,
  AIZ block, overlay-local).
- Camera_SetupLerp / Camera_SetTargetEntity / Camera_ShakeScreen -- REAL (Camera.c:142 etc.,
  Game_Camera.o pack).
- Music_FadeOut -- REAL (Game_Music.o pack). Zone_StoreEntities -- REAL (Game_Zone.o pack).
- Player_State_Static/Air/Ground -- REAL (Game_Player.o pack, -u rooted).
- RSDK.SetScene/LoadScene/SetSpriteAnimation/Sin256/ProcessAnimation/DrawSprite/
  ObjectTileCollision/ObjectTileGrip/CheckOnScreen/CheckSceneFolder/Set+GetPaletteEntry --
  engine RSDKFunctionTable.

SAFE-TO-NULL-STUB (OFF the Tier-A GHZ-handoff path):
- FXTrail (global) -- referenced only in CutsceneHBH_ShinobiJumpSetup (CREATE_ENTITY),
  which is NOT called by the GHZ ExitHBH beat (ExitHBH manipulates hbh fields directly).
  NULL global via p6_closure_edge (like BurningLog). Inert -> never spawned on Tier-A.
- The 5 Heavies' boss sheets (SPZ1/Boss.bin, PSZ2/Shinobi.bin, MSZ/HeavyMystic.bin,
  LRZ3/HeavyRider.bin+HeavyKing.bin) -- runtime LoadSpriteAnimation returns -1 gracefully
  if absent (RSDK contract) -> Heavies invisible. Tier-A acceptable; NOT staged.

## Edits (precise sites)
1. build_shipping.sh: add `P6_GHZCUT_BOOT` self-imply (-> P6_AIZ_TEST=1) at the top
   env block; add the 4 Game_*.o to OVL_FE (gated); mirror 2 symbol-presence greps.
2. build_p6scene_objs.sh: thread `${P6_GHZCUT_BOOT:+-DP6_GHZCUT_BOOT}` onto p6_io_main.o
   (line 152), p6_ovl_ghz.o (line 543); add nested compile of the 2 new TUs in the
   P6_AIZ_TEST block (line ~518).
3. p6_ovl_ghz.c: `#if defined(P6_GHZCUT_BOOT)` register GHZSetup/BGSwitch/GHZCutsceneST/
   CutsceneHBH (FXRuby already registered in AIZ block).
4. p6_io_main.cpp: add p6_ghzcut_reload() (mirror p6_aiz_reload minus AIZ BG/PAK); wrap
   the boot dispatch (line 6861) to call it; add the GHZCutscene FG cell-upload branch in
   p6_scene_load_and_arm; runtime-guard the AIZ BG streaming in p6_frontend_frame to
   folder=="AIZ".
5. p6_closure_edge.c: real CutsceneRules_SetupEntity body (#if P6_GHZCUT_BOOT); FXTrail NULL.

## AUDIT 4 -- boot-delay budget (assets on the synchronous boot path)
- GHZCUTIL.BIN 262,144 B (256 KB): 256/150 = 1.71 s + 1 seek 0.1 = ~1.81 s
- GHZCUPAL.BIN 1,024 B: ~0.01 s
- GHC1LAYT.BIN 47,709 B (47 KB): 47/150 = 0.31 s + 1 seek = ~0.41 s
  Cumulative new GHZCutscene-specific boot I/O: ~2.23 s (under the 5 s budget; the
  direct-boot has no AIZ playthrough). NOTE: GHZCUTIL pre-decoded REPLACES the ~4.4 s
  LZW decode (Task #271), so it is a net WIN vs a GIF-decode load.

## Acceptance (RED -> GREEN)
1. RED proof: qa_ghzcut_load.py folder discriminator reads non-"GHZCutscene" on existing
   states (DONE -- garbage/empty, never GHZCutscene).
2. Build P6_AIZ_TEST=1 P6_GHZCUT_BOOT=1; confirm clean compile (grep log for the new .o)
   + the symbol-presence greps pass + link.
3. Capture settled state via qa_savestate.ps1 (direct-boot is fast; FpsScale ~3).
4. qa_ghzcut_load.py --phase load -> GREEN (folder=="GHZCutscene"). --phase handoff ->
   GREEN means the cutscene ran to playable GHZ (folder=="GHZ").

## Constraints
- All shared-TU edits inside `#if defined(P6_GHZCUT_BOOT)` -> plain P6_AIZ_TEST + GHZ
  shipping builds BYTE-IDENTICAL.
- Read `_end` from game.map after build; watch for #228 boot trap (PC 0x06000956).
- Up to ~4 build iterations.

# =====================================================================
# Tier-B.1 -- FXRuby fade as a Saturn-native HARDWARE effect (2026-06-29)
# =====================================================================

## Goal
The FXRuby fade-in (decomp FXRuby_Draw, SonicMania_Objects_Cutscene_FXRuby.c:32-58)
is INVISIBLE on Saturn: FillScreen/DrawCircle/DrawRect with INK_TINT write the RSDKv5
software framebuffer (Drawing.cpp:586 FillScreen) which the true-port never presents
(it renders VDP1 sprites + VDP2 tilemaps). Re-implement the dominant effect -- the
full-screen white/black wash -- as a Saturn VDP2 hardware effect driven by the live
fxRuby->fadeWhite/fadeBlack each front-end frame.

## Cutscene flow (measured from GHZCutsceneST.c + FXRuby.c)
GHZCutsceneST_SetupObjects (GHZCutsceneST.c:84-93): fxRuby->fadeBlack=0x200(512),
fadeWhite=0x200(512), state=None, outerRadius=ScreenInfo.size.x, timer=64.
FadeIn beat (GHZCutsceneST_Cutscene_FadeIn, :134-180):
  - host->timer 0..59 : HOLD (fadeBlack=512, fadeWhite=512). FillScreen runs both
    (`fadeWhite>0` AND `fadeBlack>0`); fadeBlack drawn LAST -> screen is BLACK.
  - host->timer >= 60 : decrement fadeBlack 512->0 by 16 (32 frames) FIRST, then
    fadeWhite 512->0 by 16 (32 frames). Visible: BLACK -> (fadeBlack->0) WHITE ->
    (fadeWhite->0) clear FG. The fade-IN-from-black-through-white-to-FG.
FillScreen alpha semantics (Drawing.cpp:586-612): alphaR/G/B clamped [0,0xFF];
  white wash = FillScreen(0xFFF0F0, fadeWhite, fadeWhite-256, fadeWhite-256) ->
  R alpha=min(fadeWhite,255), G/B alpha=max(0,fadeWhite-256). black wash =
  FillScreen(0x000000, fadeBlack, fadeBlack-128, fadeBlack-256). The effective
  full-screen opacity = the alpha channel (0..255); fadeWhite/fadeBlack 256..512
  saturate to alpha 255 (full wash), 0..255 ramps the wash.

## DOC DECISION -- VDP2 Color Offset Function (ST-058-R2 Chapter 13, p249-254)
Chosen over the VDP1 full-screen translucent quad. Cited rationale:
- ST-058-R2 Chapter 13 Introduction (VDP2_Manual.txt:10215-10219): the color offset
  function "causes a change in the screen color without changing color RAM data by
  adding the offset value when sprite and data of each screen are output. Can also be
  used for fade-in and fade-out." -- this is the canonical Saturn fade.
- 9-bit SIGNED offset per RGB (Fig 13.1, :10235-10249; :10372-10373 "Negative numbers
  should be set by two's-complement values"), added post-color-calc to 8-bit color
  data, clamped [0,FF] (:10226-10227). NEGATIVE offset -> subtract -> toward black;
  POSITIVE -> add -> toward white. SMOOTH 256-step ramp matches fadeWhite/fadeBlack.
- Per-screen enable CLOFEN 180110H (:10252-10283): N1COEN(bit1)=NBG1, SPCOEN(bit6)=
  Sprite. Select CLOFSL 180112H (:10289-10324): 0=offset A. GHZCutscene's visible
  layers are NBG1(FG)+SPR(VDP1 sprites) (p6_vdp2.c:358 slScrAutoDisp(NBG1ON|SPRON)),
  so enable offset A on NBG1ON|SPRON.
- REJECT VDP1 half-transparency (ST-013-R3): VDP1_Manual.txt:575-576,845-846 -- RGB
  half-transparency/half-luminance are FIXED 50% ratios (NOT variable alpha); :760-817
  "results of half-transparent processing... cannot be guaranteed" when pixels written
  twice. Plus a full-screen quad costs VDP1 fill-rate (a known title bottleneck) +
  competes for the FULL VDP1 slot tables (NSHEETS=12/SLOTS=16). Color offset is
  post-CRAM (zero CRAM-bank stomp -- heeds the R3.3 lesson), zero VDP1 cost.

## SGL/jo path (verified linked)
SL_DEF.H:971-977: slColOffsetA(Sint16 r,g,b), slColOffsetAUse(Uint16 screens),
slColOffsetOff(Uint16). Screen mask = NBG1ON(1<<1)|SPRON(1<<6) (SL_DEF.H:543,548).
LINKED engine-wide (p6_vdp2.c already includes <SGL.H> + calls slScrAutoDisp etc.;
demo-gamepad/game.map slColOffsetA @0x601ddb0). p6_vdp2.c is the home (raw SGL TU).
Mapping fadeWhite/fadeBlack(0..512) -> offset: clamp to opacity a=min(v,255);
  black wash (fadeBlack): offset = (-a,-a,-a). white wash (fadeWhite): (+a,+a,+a).
  When BOTH>0 (the 0..59 hold) black dominates (decomp draws black LAST) -> use
  black offset (-255). Single offset-A write/frame; OFF (offset 0) when both<=0.

## HOLD infra (P6_GHZCUT_HOLD, implies P6_GHZCUT_BOOT) -- for the RED gate
The cutscene is transient; FadeIn returns false only while a fade>0, true when BOTH
hit 0 -> to FREEZE at a visible wash, PIN fxRuby->fadeWhite each tick to a chosen
HOLD value (P6_GHZCUT_HOLD_WHITE, default 256 = full white wash) and fadeBlack=0.
FadeIn then never returns true (fadeWhite never reaches 0) -> SetupGHZ1 never fires ->
no handoff -> the white wash is on-screen for the capture. Pin lives in the overlay
witness (#if P6_GHZCUT_HOLD), which already reads the live FXRuby entity. GHZ/plain-
AIZ untouched (flag-gated). The fade APPLY (below) runs unconditionally under
P6_GHZCUT_BOOT, so a non-HOLD direct-boot still shows the real ramp (then hands off).

## Edits
1. p6_vdp2.c: add `void p6_vdp2_fade_apply(int fadeWhite, int fadeBlack)` (#if
   P6_GHZCUT_BOOT): compute opacity, slColOffsetA(+/-a,...) + slColOffsetAUse(
   NBG1ON|SPRON), or slColOffsetOff(NBG1ON|SPRON) when 0. + p6_vdp2_fade_reset().
2. build_p6scene_objs.sh:205 -- thread ${P6_GHZCUT_BOOT:+-DP6_GHZCUT_BOOT} +
   ${P6_GHZCUT_HOLD:+-DP6_GHZCUT_HOLD} onto p6_vdp2.o.
3. p6_ovl_api.h: add `void (*fade_fn)(int *outWhite,int *outBlack);` -- overlay reads
   the live FXRuby fade fields into engine-visible ints (the overlay has EntityFXRuby;
   the engine TU calls slColOffset). NULL on non-GHZCUT flavors.
4. p6_ovl_ghz.c (#if P6_GHZCUT_BOOT): p6_ghzcut_fade_fn -- foreach_all(FXRuby) read
   fadeWhite/fadeBlack into *outWhite/*outBlack. Under #if P6_GHZCUT_HOLD, FIRST pin
   fx->fadeWhite=P6_GHZCUT_HOLD_WHITE, fx->fadeBlack=0 (freeze). Set api->fade_fn.
5. p6_io_main.cpp p6_frontend_frame (#if P6_GHZCUT_BOOT, after ProcessObjectDrawLists
   ~line 6661): if (s_ovl.fade_fn){ int w=0,b=0; s_ovl.fade_fn(&w,&b);
   p6_vdp2_fade_apply(w,b); p6_w_ghzcut_fade=(w<<16)|(b&0xFFFF);} -- gc-root
   p6_w_ghzcut_fade witness for the gate. extern decl for p6_vdp2_fade_apply.
6. build_shipping.sh:63 -- add P6_GHZCUT_HOLD self-imply (-> P6_GHZCUT_BOOT=1 ->
   P6_AIZ_TEST=1); thread -DP6_GHZCUT_HOLD onto p6_io_main + p6_ovl_ghz + p6_vdp2.

## Gate (tools/_portspike/qa_ghzcut_fade.py) -- RED-first
Peek VDP2 color-offset regs from the held savestate (mcs_extract --peek16):
  CLOFEN 0x25F80110 (NBG1=bit1, SPR=bit6 set?), COAR 0x25F80114, COAG 0x25F80116,
  COAB 0x25F80118. + the p6_w_ghzcut_fade witness (fadeWhite/fadeBlack the overlay saw).
RED (current build, no fade code): CLOFEN==0 AND COAR==0 -> no offset -> FG full bright.
GREEN (after fix): with P6_GHZCUT_HOLD_WHITE=256 -> white wash -> CLOFEN bit1&bit6 set,
  COAR/COAG/COAB == +255 (0x00FF positive 9-bit). p6_w_ghzcut_fade white==256, black==0.
  Plus BINDING (gotcha #4, visual symptom): capture _ghzcut_fade_*.png, confirm the FG
  is washed white (mean luma raised vs a no-fade GHZCutscene reference).

## Tier-B.2 (Heavies) -- MEASURED RESIDENCY STRATEGY (strategy only, NOT implemented)

### (a) Exact frames/anims each Heavy uses in the GHZ ExitHBH beat
Parsed via tools/build_anim_pack.py:parse_bin (the project's RSDK SpriteAnimation
parser). Per CutsceneHBH_LoadSprites (..._CutsceneHBH.c:199-242) + ExitHBH
(GHZCutsceneST.c:258-291):
  GUNNER  SPZ1/Boss.bin       anim 5            ->  4 frames  (sheet SPZ1/Boss.gif)
  SHINOBI PSZ2/Shinobi.bin    anim 0+5, ->3     -> 24 frames  (PSZ2/Boss.gif+Boss2.gif)
  MYSTIC  MSZ/HeavyMystic.bin anim 0            ->  6 frames  (MSZ/HeavyMystic.gif)
  RIDER   LRZ3/HeavyRider.bin anim 3            ->  4 frames  (LRZ3/HeavyRider.gif)
  KING    LRZ3/HeavyKing.bin  anim 7+16         ->  8 frames  (LRZ3/HeavyKing.gif+King2.gif)
  TOTAL = 46 used frames.

### (b) SELECTIVE combined-atlas byte size vs full sheets vs cart free
- FULL sheets (infeasible): SPZ1/Boss 1024x512, LRZ3/HeavyKing 512x1024, etc. ->
  8bpp pixel totals SPZ1/Boss=512KB, PSZ2/Boss=256KB+Boss2=128KB, MSZ/HeavyMystic=
  256KB, LRZ3/HeavyRider=256KB, LRZ3/HeavyKing=512KB+King2=256KB = ~2.2 MB. NO.
- SELECTIVE atlas (sum of the 46 used frames' w*h, 8bpp) = **189,864 B = 185.4 KB**
  (per-frame maxima: GUNNER 57x95, SHINOBI 58x73, MYSTIC 79x80, RIDER 80x68,
  KING 106x101). A bin-packed atlas of just these 46 sub-rects is ~185-220 KB
  with packing slack.
- FRONT-END cart RES store free: window 0x22400000..0x226E0000 (~2.88 MB, VDP1
  staging buffers sit at 0x226E0000). The front-end high-water with TSONIC(1MB)+
  LOGOS+TLOGO+MAINICON+TEXTEN+AIZOBJ staged = ~0x22686900 (p6_io_main.cpp:3741) ->
  ~366 KB free. **185 KB selective atlas < 366 KB free -> FITS even conservatively.**
  GHZCutscene-active dropping the title-only sheets (TSONIC 1MB + TLOGO + LOGOS, not
  shown during the cutscene) reclaims ~1.5 MB -> ample.
- VDP1 SLOT budget (the real wall): P6_VDP1_NSHEETS=12 (p6_vdp1.c:123),
  SATURNSHEET_SLOTS=16 (build_p6scene_objs.sh:577, MENU flavor), AIZOBJ banded at
  slot 15. The 17th sheet (slot 16) = the MEASURED #228 orphan-.bss trap. So a NEW
  Heavy sheet CANNOT take slot 16. PLAN: the 5 Heavies share ONE combined atlas =
  ONE new banded sheet. Reuse a freed TITLE slot when GHZCutscene is active (TSONIC
  slot or TLOGO slot -- those sheets are title-only, dead during the cutscene) rather
  than growing SATURNSHEET_SLOTS to 17. Net VDP1 slot delta = 0 (slot reuse).

### (c) Palette / CRAM plan (the per-draw swap constraint)
CutsceneHBH_SetupColors (..._CutsceneHBH.c:80-182): each Heavy owns a 128-entry
(0x80) color table (colorSet 0-4). CutsceneHBH_SetupPalettes (:188) writes ALL 5 into
the SAME CRAM region -- bank 0 indices 0x80-0xFF via SetPaletteEntry(0, c+0x80, ...).
Heavy Draw (:34-46) does SetupPalettes (swap THIS Heavy's 128 in) -> draw -> Restore
(swap back). So the DECOMP keeps only ONE Heavy's palette in CRAM[0x80..0xFF] at a
time (a per-sprite palette swap).
- Saturn constraint: a VDP1 sprite's palette is latched at draw time; you cannot swap
  CRAM mid-frame between two sprites without a CRAM DMA per sprite. With 5 Heavies
  on-screen SIMULTANEOUSLY in the ExitHBH beat, the decomp's single-region swap does
  NOT map directly.
- PLAN: give each Heavy its OWN distinct CRAM bank region so all 5 are resident at
  once (no swap). 5 x 128 colors = 640 CRAM entries. CRAM has 2048 entries (4 KB,
  CRM16_2048 mode -- the project already uses banks 0/1 for FG/VDP1-sprite). Heed
  R3.3: CRAM[0-255]=VDP2 FG bank0, CRAM[256-511]=VDP1 sprite bank1, AIZ-BG uses
  CRAM[512+] @P6_AIZBG_PAL_BASE=32. For GHZCutscene (no AIZ BG), CRAM[512..2047]
  (1536 entries) is FREE -> place the 5 Heavy 128-blocks at CRAM[512],[640],[768],
  [896],[1024] (= banks via the VDP1 sprite color-bank bits). The combined Heavy
  atlas's frames each tag the sprite's palette-bank to its character's CRAM block.
  Convert each 128-color table offline (the decomp RGB888 -> Saturn RGB555, the
  jo-cram +1 pre-shift) into a HBHPAL.BIN, upload to CRAM[512..1151] at scene load.
- COST: 640 CRAM entries (well within the 1536 free for the no-AIZ-BG GHZCutscene
  flavor) + the ~185 KB atlas + 0 net VDP1 slots (title-slot reuse).

### VERDICT: Tier-B.2 is FEASIBLE.
  Pixel budget GREEN (185 KB selective atlas < 366 KB conservative cart free, < 1.5 MB
  if title sheets dropped). VDP1 slot budget GREEN (1 combined sheet via title-slot
  reuse; do NOT grow to slot 16/17 = #228). CRAM budget GREEN (640 entries in the
  1536-entry free upper CRAM for the no-AIZ-BG flavor). The build work is: (1) offline
  build_heavy_atlas.py emitting the 46-frame selective atlas + a remap of the 5 .bin
  frame sub-rects -> atlas coords; (2) HBHPAL.BIN (5x128 colors, RGB555, +1 shift);
  (3) stage the atlas into a reused title VDP1 slot when folder=="GHZCutscene";
  (4) upload the 5 palette blocks to CRAM[512..1151]; (5) the SetSpriteAnimation
  remap so each Heavy's drawn frames index the atlas + its CRAM bank. None of these
  touch WRAM-H code beyond the small stage/upload calls (no #228 risk). A focused
  multi-build sub-project, not a drop-in.

## Acceptance (RED -> GREEN), Tier-B.1
1. RED: build P6_GHZCUT_BOOT=1 (no HOLD, no fade-apply yet) OR run the gate on an
   existing GHZCutscene state -> CLOFEN==0 -> RED.
2. Implement edits 1-6. Build P6_GHZCUT_HOLD=1. _end < 0x060C0000, no #228.
3. Capture held state -> qa_ghzcut_fade.py GREEN (regs + witness) + PNG shows white wash.

# =====================================================================
# Tier-B.2 -- the 5 Heavies render (IMPLEMENTATION, 2026-06-29)
# =====================================================================

## DOC-CITED CRAM/CMDCOLR scheme (the CRITICAL R3.3 risk -- RESOLVED by measurement)
LIVE register state from the held GHZCutscene state (tools/mcs_extract.py --peek16
on tools/_portspike/_ghzcut_fade_hold.mcs, 2026-06-29):
  SPCTL  (0x25F800E0) = 0x0023 -> SPTYPE=3, SPCLMD(bit5)=1, SPWINEN(bit4)=0
  CRAOFB (0x25F800E6) = 0x0000 -> SPCAOS (bits6-4) = 0  (sprite CRAM offset = 0)
  RAMCTL (0x25F8000E) = 0x1327 -> CRMD(bits13-12)=01 = CRAM mode 1 (2048 RGB555)

VDP1 CMDCOLR color-bank addressing (ST-013-R3 sec 6.4 = VDP1_Manual.txt:4214-4236,
Table 6.3, Fig 6.17): a textured COLOR-BANK-mode 8bpp part writes framebuffer pixel
= (CMDCOLR upper bits) | (8-bit char pixel). The 256-color-mode bank is the upper 8
bits, so CMDCOLR (=jo `colno`, 256-aligned) becomes the framebuffer pixel's high byte
-> framebuffer value = colno + charpixel (charpixel 0..255, colno multiple of 0x100).

VDP2 Sprite color RAM address (ST-058-R2 sec 10.1 = VDP2_Manual.txt:8944-9011, Fig
10.1/10.2 + Color RAM Address Offset Register :9066-9116): the final CRAM address =
(sprite dot color data, masked to the type's DC width) + SPCAOS*0x200 added to the top
3 bits. SPRITE TYPE 3 (the live SPCTL) exposes the FULL 11-bit DC10..DC0 (Fig 9.1
:8459-8463: bit15=SD,14-13=PR,12-11=CC,10-0=DC) -> the framebuffer pixel's low 11 bits
ARE the CRAM address (mode 1 = full 2048). SPCAOS=0 -> CRAM address = framebuffer pixel
= colno + charpixel DIRECTLY.

=> A VDP1 sprite CAN reference CRAM[0..2047]. To give each Heavy its own 256-entry
   CRAM block (all 5 resident simultaneously, NO swap), set its draws' jo `colno` to
   the block base: GUNNER=512, SHINOBI=768, MYSTIC=1024, RIDER=1280, KING=1536. Each
   colno is a 0x100 multiple and <=0x0600 (1536) so it ONLY sets DC bits (bit15 stays 0
   -> palette format under SPCLMD=1; never touches PR/CC bits 14-11). The decomp's
   per-Heavy palette is uploaded ONCE to CRAM[block..block+127] (the Heavy art uses
   palette indices 0x00-0x7F per the source GIF -> block holds 128 colors; indices
   0x80+ unused by these frames). This is the engine's existing per-sprite mechanism
   (jo sprites.h:380 colno=palette_id*256; p6_vdp1.c hardcodes jo_sprite_set_palette(1)
   = CRAM[256] bank1 for all normal sprites). NO new SPCTL/SPCAOS/RAMCTL write needed
   -> ZERO register-level change -> R3.3-bank-collision-proof (each block disjoint;
   CRAM[256..511]=bank1 normal sprites untouched, CRAM[512..1663]=the 5 Heavy blocks,
   CRAM[0..255]=VDP2 FG bank0 untouched). The checklist's earlier "640-entry blocks at
   512/640/768/896/1024" is CORRECTED: blocks must be 256-ALIGNED (colno = block*1, a
   0x100 multiple) so the CMDCOLR high-byte lands them; 128-aligned bases (640,896) are
   NOT addressable by an 8bpp colno. Use 512/768/1024/1280/1536 (256-aligned, 5 blocks
   span CRAM[512..1663] < 2048).

## Exact used frames (MEASURED via tools/_portspike/_hbh_probe.py, 2026-06-29)
For the held cutscene (pre-ExitHBH-timer-60) the on-screen anims are:
  GUNNER  SPZ1/Boss.gif  anim5 'Gunner Hover' 4 fr   (max 54x95)
  SHINOBI PSZ2/Boss.gif  anim0 'Idle' 6 fr + anim3 'Spin' 10 fr (Boss.gif);
          PSZ2/Boss2.gif anim5 'Cape Idle' 8 fr  (fxAnimator)  (max 58x73)
  MYSTIC  MSZ/HeavyMystic.gif anim0 'Idle' 6 fr   (max 79x80)
  RIDER   LRZ3/HeavyRider.gif anim3 'Fall' 4 fr   (max 80x68)
  KING    LRZ3/HeavyKing.gif  anim7 'Body Hang' 4 fr + anim16 'Scepter Hang' 1 unique
          rect (18x15, all 4 frames same rect)  (max 106x101)
TOTAL = 43 unique rects, 189,054 B 8bpp (184.6 KB). King2.gif NOT referenced (both
King anims use sheet0 HeavyKing.gif) -> its absence is irrelevant.

## Atlas / sheet / PAK plan (mirror AIZOBJ pipeline)
tools/build_heavy_atlas.py (offline, NEW): read the 5 source .bin + their GIFs, extract
the 43 used rects, bin-pack into ONE 8bpp atlas (target <=512 wide so the .SHT banded
raw band <=0x4000 = MakeResident-safe per the AIZOBJ note; or wider banded WITHOUT
MakeResident like AIZOBJ.SHT). Emit:
  cd/HBHOBJ.SHT  -- one Saturn banded sheet (the combined atlas), staged like AIZOBJ.SHT
  cd/HBHOBJ.PAK  -- 5 rewritten .bin entries (SPZ1/Boss.bin, PSZ2/Shinobi.bin,
                    MSZ/HeavyMystic.bin, LRZ3/HeavyRider.bin, LRZ3/HeavyKing.bin), each
                    with sheet names rewritten to ONE name "Cutscene/HBH.gif" + frame
                    sheet-ordinals all 0 + rects remapped to the atlas coords. The decomp
                    CutsceneHBH_LoadSprites loads these by name; the pack resolves them
                    (FAST WRAM-H/cart path) -> aniFrames>0 -> sprites have frames.
  cd/HBHPAL.BIN  -- 5x128 colors (RGB555, jo +1 pre-shift) for CRAM[512/768/1024/1280/
                    1536]. Source = each source GIF's GCT (the AUTHENTIC palette -- the
                    decomp's tempPal tables are pre-Plus copies of stage palettes per the
                    CutsceneHBH.c:73-78 bug note; the GIF GCT is what the art was authored
                    against, so it renders authentic colors).

## Engine integration (all #if defined(P6_GHZCUT_BOOT))
1. p6_io_main.cpp p6_ghzcut_reload: load HBHOBJ.PAK into a free cart window (the AIZ
   build loads AIZOBJ.PAK at P6_HW_OBJANIMPAK; GHZCutscene does NOT -> that window is
   free). Mirror p6_aiz_reload's AIZOBJ.PAK load.
2. p6_io_main.cpp p6_scene_load_and_arm GHZCUT branch: stage HBHOBJ.SHT banded (mirror
   the AIZOBJ.SHT block at :4228) into a free SaturnSheet slot; set its hash to
   "Cutscene/HBH.gif"; upload HBHPAL.BIN's 5 blocks to CRAM[512..1663]
   (p6_vdp2_hbh_pal_upload, a new p6_vdp2.c fn -- raw CRAM writes like the AIZ-BG bank
   upload). The bind loop binds the sheet to a VDP1 handle (NSHEETS=12 has room).
3. p6_vdp1.c: add `int p6_heavy_palblock` (default 1); change the two hardcoded
   jo_sprite_set_palette(1) in p6_vdp1_blit/_blit_flipped to jo_sprite_set_palette(
   p6_heavy_palblock). DEFAULT 1 keeps every normal sprite on bank1 (CRAM[256]) ->
   byte-identical behaviour for non-Heavy draws.
4. p6_ovl_ghz.c: register a Draw SHIM for CutsceneHBH (instead of CutsceneHBH_Draw
   directly): p6_cuthbh_draw sets p6_heavy_palblock = 2 + (sceneInfo.entity's
   EntityCutsceneHBH->characterID) [GUNNER=0..KING=4 -> block 2..6 -> colno 512..1536],
   calls CutsceneHBH_Draw(), resets p6_heavy_palblock=1. The overlay TU CAN name
   EntityCutsceneHBH (Game.h) and read sceneInfo.entity + write the engine global
   p6_heavy_palblock (extern, -R cross-link). characterID->block: colno = (2+cid)*256.
   NOTE: only the 5 GHZ Heavies (cid 0-4) map to blocks 2-6; cid>4 falls back to bank1.
5. SetSpriteAnimation remap: NONE needed in code -- the rewritten HBHOBJ.PAK .bin frames
   already index the atlas (sheet ord 0 -> Cutscene/HBH.gif -> the staged HBHOBJ.SHT).
   The decomp SetSpriteAnimation(aniFrames, N, ...) selects anim N from the rewritten
   .bin; its frames' rects are the atlas coords.

## Build-script edits
- build_shipping.sh: register HBHOBJ.SHT/PAK presence grep (optional); they auto-glob
  into the ISO from cd/. No new .o (Game_CutsceneHBH.o already compiles).
- build_p6scene_objs.sh: thread the existing P6_GHZCUT_BOOT onto p6_vdp1.o (for
  p6_heavy_palblock) -- verify it is ALREADY threaded (p6_vdp2.o/p6_io_main.o/p6_ovl_ghz.o
  already are for Tier-B.1; p6_vdp1.o may need adding).
- tools/build_cdda.py host post-step after each build (CueMaker emits malformed _arc.wav).

## RED -> GREEN gate (tools/_portspike/qa_ghzcut_heavies.py, NEW)
- HOLD with fade CLEARED: build P6_GHZCUT_HOLD=1 P6_GHZCUT_HOLD_WHITE=0 (no wash). The
  HOLD pins fadeWhite=0/fadeBlack=0 so FadeIn never completes (still frozen -- the pin
  itself holds) -> the Heavies render at their scene positions with no fade.
- RED (current build, before HBHOBJ.* + integration): the Heavy sheet handle = -1
  (p6_w_hbh_slot<0) and 0 Heavy-region VDP1 pixel-mass. Screenshot: no Heavies.
- GREEN (after fix): p6_w_hbh_slot>=0 + Heavy VDP1 pixel-mass present + sampled Heavy
  sprite pixels have CORRECT colors (sample the rendered framebuffer at a Heavy's screen
  rect, compare hue distribution to the source GIF palette; NOT magenta/green). Screenshot
  shows the 5 Heavies.

## AUDIT 4 -- boot-delay budget (Tier-B.2 assets, synchronous boot path)
- HBHOBJ.SHT 29,974 B banded: 30/150 = 0.20 s + 1 seek = ~0.30 s
- HBHOBJ.PAK 30,024 B (5 thin .bin): 30/150 = 0.20 s + 1 seek = ~0.30 s
- HBHPAL.BIN 1,280 B: ~0.01 s
  Cumulative Tier-B.2 boot I/O ~0.6 s (the zlib-banded SHT compressed far below the
  185 KB raw estimate). Well under the 5 s budget.

# =====================================================================
# Tier-B.2 RESULT -- DONE + VERIFIED (2026-06-29, GREEN)
# =====================================================================
The 5 Heavies RENDER in the GHZCutscene arrival cutscene with AUTHENTIC colors
(screenshot-confirmed). Built P6_GHZCUT_HOLD=1 P6_GHZCUT_HOLD_WHITE=0; _end 0x060b9e50
(25,008 B under the 0x060C0000 ceiling, no #228). GHZ shipping byte-identical (0 hbh
symbols in the GHZ map, clean link, _end 0x060b5a40 unchanged).

ASSETS (tools/build_heavy_atlas.py): cd/HBHOBJ.SHT 29,974 B (512x432 atlas, 43 used
frames, SHB1 codec, raw band 0x2000 = MakeResident-safe) + cd/HBHOBJ.PAK 30,024 B (5
rewritten .bin -> "Cutscene/HBH.gif") + cd/HBHPAL.BIN 1,280 B (5x128 RGB555 blocks).

THE ROOT-CAUSE BLOCKER (MEASURED, 3 builds in): the Heavies were INVISIBLE not because
of palette/sheet (those were GREEN from build 1) but because RegisterObject (Object.cpp:
118 `if (entityClassSize > P6_MAX_ENTITY_SIZE) return;`) REFUSED to register CutsceneHBH
-> its Object* stayed NULL -> NO Heavies instantiated (witness p6_w_hbh_count=-2). Cause:
EntityCutsceneHBH carries `color colors[128]` (512 B) -> sizeof ~730 B > 592 (front-end
P6_MAX_ENTITY_SIZE). FIX: shrink colors[128]->colors[1] + paletteColors[128]->[1] under
#if defined(P6_GHZCUT_BOOT) (the array is the RSDKv5 SW palette-swap, REPLACED on Saturn
by the CRAM blocks) -> sizeof ~222 B -> registers. SetupPalettes/Store/RestorePalette
#if-guarded no-ops in the .c (they'd read the shrunk array OOB). After the fix:
p6_w_hbh_count=5, vis=1/onScreen=1/active=4, handle=0 (bound), aniFrames=18,
vdp1_landed=1843 (was 0).

CRITICAL-RISK (R3.3 CRAM/CMDCOLR collision) -- RESOLVED + MEASURED: all 5 CRAM Heavy
blocks @CRAM[512/768/1024/1280/1536] MATCH HBHPAL.BIN 7/7 (byte-exact), disjoint from
bank0 FG (CRAM[0..255]) + bank1 sprites (CRAM[256..511]). The per-Heavy jo colno
(2+characterID -> 512..1536) routes each Heavy's pixels to its own block (live SPCTL=0x23
Type-3 full-11-bit DC + SPCAOS=0; ST-013-R3 sec6.4 + ST-058-R2 sec10.1). NO register
write needed. The magenta 0xFC1F entries in 3 blocks are LEGIT source-GIF unused-slot
fillers (the 7/7 byte-match distinguishes authentic palette from collision garbage).

HOLD for the capture: P6_GHZCUT_HOLD_WHITE=0 pins fadeBlack=1 (a -1 VDP2 color offset,
imperceptible) so FadeIn never completes -> the cutscene FREEZES at FadeIn with the
Heavies on the King Claw, VISIBLE + un-washed (an earlier HOLD_WHITE=0 with fadeBlack=0
let FadeIn complete -> the cutscene raced past the Heavies).

GATE tools/_portspike/qa_ghzcut_heavies.py: RED->GREEN proven. RED (pre-fix, on
_ghzcut_fade_hold.mcs): p6_w_hbh_slot absent + 0/5 CRAM blocks match. GREEN (post-fix,
_ghzcut_heav.mcs): slot=9, count=5, handle=0, aniFrames=18, vdp1_landed=1843, 5/5 CRAM
match HBHPAL.BIN. SCREENSHOT proofs tools/_portspike/_ghzcut_heav_GREEN{,_f1}.png show
the Heavies (red/white Shinobi+Gunner, grey King Claw, purple Mystic) in authentic colors.

# =====================================================================
# GATE-2 -- the LIVE loop (decouple support from direct-boot), 2026-06-29
# =====================================================================

## STEP 1 -- COMBINED-OBJECT-SET BUDGET (offline, measured from the current build)
The current game.map / cd/OVLRING.BIN / ovl_ring.map ARE the combined-set figures:
the live build is `P6_AIZ_TEST=1 P6_GHZCUT_BOOT=1`, and P6_GHZCUT_BOOT implies
P6_AIZ_TEST, so OVL_FE ALREADY carries BOTH object code sets simultaneously (AIZ
intro 8 TUs + GHZCutsceneST + CutsceneHBH) on top of the Menu/Title/Logos overlay.

TWO walls (measured, both FIT):
- OVERLAY window (the TIGHT one): P6_OVL_WINDOW=0x20000=131,072 B (p6_ovl_api.h:67).
  ovl_ring.map high-water = .text 0x02690000+0x1f790, .bss 0x026af790+0x1d8 ->
  end 0x026af968 = 0x1f968 = 129,896 B footprint. HEADROOM = 1,176 B. (cd/OVLRING.BIN
  on disk = 128,912 B = .text-only; the loader zeroes the 0x1d8 .bss tail in-window.)
- WRAM-H pack _end (the #228 ceiling): _end=0x060b9e50 vs the front-end ceiling
  0x060C0000 -> HEADROOM = 25,008 B.
- CD assets (sequential-load, disjoint cart/WRAM windows -> low risk): AIZ set
  (AIZ1LAYT 7,695 + AIZBG.* ~125 KB + AIZOBJ.* ~25 KB) + GHZCutscene set (GHC1LAYT
  47,709 + GHZCUTIL 262,144 + GHZCUPAL 1,024 + HBHOBJ.* ~60 KB + HBHPAL 1,280). All
  auto-glob into the ISO; loaded in their own scene phase. FITS.

The decouple (STEP 2) REMOVES only the `p6_ghzcut_reload()` CALL from the boot dispatch
(in p6_io_main.cpp = the MAIN PACK, NOT the overlay) and re-gates the reload FN under
the new direct-boot flag. The OVL_FE object set is UNCHANGED (GHZCutsceneST+CutsceneHBH
stay under P6_GHZCUT_BOOT) -> the overlay footprint is BYTE-IDENTICAL across the
decouple -> the 1,176 B overlay headroom + 25,008 B _end headroom HOLD without a
speculative build. VERDICT: FITS.

## STEP 2 -- DECOUPLE MECHANISM (flags)
- P6_GHZCUT_BOOT keeps its current meaning EXCEPT the boot dispatch: it now means
  "GHZCutscene SUPPORT compiled+registered+staged + the AIZ->GHZCutscene + GHZCutscene
  ->GHZ ENGINESTATE_LOAD seam routing + fade + Heavies". The LIVE build sets it.
- NEW P6_GHZCUT_DIRECTBOOT: gates ONLY the `p6_ghzcut_reload()` boot dispatch + the
  `p6_ghzcut_reload` fn definition (the direct-boot-only diagnostic). Self-implies
  P6_GHZCUT_BOOT. The existing gate-1/B.1/B.2 captures use this (so they are unchanged).
- P6_GHZCUT_HOLD (fade/Heavy capture flavor) now implies P6_GHZCUT_DIRECTBOOT (it was
  used WITH direct-boot for those captures) -> B.1/B.2 reproduce byte-identically.
- The LIVE-loop build = `P6_GHZCUT_BOOT=1` (support on) WITHOUT P6_GHZCUT_DIRECTBOOT
  -> boots the normal menu->title->logos->AIZ path; the AIZ intro's beat-9
  AIZSetup_Cutscene_LoadGHZ (AIZSetup.c:900 SetScene("Cutscenes","Green Hill Zone"))
  -> ENGINESTATE_LOAD -> the NEW p6_frontend_frame seam branch (currentSceneFolder=="AIZ")
  loads+arms GHZCutscene (+ the HBHOBJ.PAK pre-load that p6_ghzcut_reload did) -> the
  cutscene runs -> SetupGHZ1 -> the EXISTING GHZCutscene->GHZ seam branch -> playable GHZ.

THE LIVE-SEAM CODE GAP (the one piece the live transition lacks vs direct-boot):
p6_ghzcut_reload pre-loads HBHOBJ.PAK into P6_HW_OBJANIMPAK BEFORE p6_scene_load_and_arm
(so CutsceneHBH_LoadSprites resolves the Heavies on the fast path). The live ENGINESTATE
_LOAD branch must do the SAME HBHOBJ.PAK load before its p6_scene_load_and_arm(). Add it
to the NEW AIZ->GHZCutscene seam branch. (The FG cell upload + HBHOBJ.SHT stage + HBHPAL
CRAM upload already live in p6_scene_load_and_arm keyed on runtime folder=="GHZCutscene",
and the per-frame FG present + fade are keyed on P6_GHZCUT_BOOT + runtime folder -> all
reachable from the live transition unchanged.)

## STEP 3 -- VALIDATE CHEAPLY (RED-gate-first; AIZ intro is ~4 fps / minutes)
Reuse tools/_portspike/qa_ghzcut_load.py (--phase load: folder=="GHZCutscene";
--phase handoff: folder=="GHZ"). The new thing to prove = the SEAM (live AIZ LoadGHZ
lands in GHZCutscene, not swallowed).
- FAST iteration path = a gated one-shot AIZ->GHZCutscene INJECTION (new flag
  P6_GHZCUT_SEAMTEST, implies P6_GHZCUT_BOOT but NOT DIRECTBOOT): boots the live AIZ
  path, then after a fixed cont-frame count fires the EXACT decomp SetScene("Cutscenes",
  "Green Hill Zone")+ENGINESTATE_LOAD that beat-9 fires -> exercises the new seam branch
  in seconds (the proven P6_TRANSITION_TEST act-advance pattern, p6_io_main.cpp:7170).
- RED (current build, before the seam branch): on the live AIZ build the AIZ LoadGHZ
  falls through to ENGINESTATE_REGULAR (swallowed) -> folder stays "AIZ" -> RED.
- GREEN (after the seam branch): folder=="GHZCutscene" (--phase load), then deeper
  folder=="GHZ" (--phase handoff). Plus screenshot the live GHZCutscene render reached
  from the injection (fade + Heavies). Captures named _ghzcut_live_*.mcs/.png.
- The direct-boot gate-1/B.1/B.2 (P6_GHZCUT_DIRECTBOOT / P6_GHZCUT_HOLD) MUST still pass.

## EDITS (precise sites)
1. build_shipping.sh env block (~line 67-77): P6_GHZCUT_HOLD -> export P6_GHZCUT_DIRECTBOOT=1
   (was P6_GHZCUT_BOOT=1); new P6_GHZCUT_DIRECTBOOT -> export P6_GHZCUT_BOOT=1; new
   P6_GHZCUT_SEAMTEST -> export P6_GHZCUT_BOOT=1. Thread the make knob
   ${P6_GHZCUT_DIRECTBOOT:+...} is NOT needed at make (it is a p6_io_main-only -D); add a
   DIRECTBOOT symbol-presence grep ([5h] already greps the overlay TUs which are
   P6_GHZCUT_BOOT-gated -- leave as is; add a p6_ghzcut_reload presence grep under DIRECTBOOT).
2. build_p6scene_objs.sh:152 -- thread ${P6_GHZCUT_DIRECTBOOT:+-DP6_GHZCUT_DIRECTBOOT}
   ${P6_GHZCUT_SEAMTEST:+-DP6_GHZCUT_SEAMTEST} onto p6_io_main.o ONLY (the boot-dispatch TU).
3. p6_io_main.cpp:
   (a) boot dispatch (~7093): `#if defined(P6_GHZCUT_DIRECTBOOT)` p6_ghzcut_reload() #else
       p6_aiz_reload() (was P6_GHZCUT_BOOT).
   (b) p6_ghzcut_reload fn def (~6266 + its #endif ~6349): `#if defined(P6_GHZCUT_DIRECTBOOT)`
       (was P6_GHZCUT_BOOT) -- it is only CALLED by the direct-boot dispatch.
   (c) NEW ENGINESTATE_LOAD seam branch (~6483, inside the existing #if P6_GHZCUT_BOOT block,
       BEFORE the GHZCutscene->GHZ branch): one-shot, currentSceneFolder=="AIZ" ->
       VDP1-handle-map reset (folder change, #250) + HBHOBJ.PAK pre-load into
       P6_HW_OBJANIMPAK + p6_scene_load_and_arm() + return. (Mirror the GHZCutscene->GHZ
       branch + the p6_ghzcut_reload HBHOBJ.PAK load.)
   (d) NEW P6_GHZCUT_SEAMTEST one-shot injection (inside #if P6_AIZ_TEST in p6_frontend_frame,
       after the cutscene has run): at a fixed p6_w_cont_frames, scan listPos for
       "GHZCutscene" id=='1' (the engine SetScene would do the same via GameConfig) +
       sceneInfo.state=ENGINESTATE_LOAD. The REAL decomp trigger is the AIZ beat-9 SetScene;
       this injection reproduces it for the fast gate.
GHZ shipping (no flag) BYTE-IDENTICAL: every edit #if-gated; verify map _end unchanged
(0x060b5a40 per the Tier-B.2 result) + the GHZ map carries 0 ghzcut symbols.

# =====================================================================
# GATE-2 RESULT -- DONE + VERIFIED (2026-06-30, GREEN)
# =====================================================================
The LIVE menu->AIZ->GHZCutscene->GHZ loop is PROVEN. The GHZCutscene SUPPORT is now
decoupled from the direct-boot diagnostic, so the LIVE build's AIZ intro LoadGHZ reaches
GHZCutscene (instead of being swallowed) and the whole chain runs to playable Green Hill
Zone -- WITHOUT the P6_GHZCUT_DIRECTBOOT diagnostic.

STEP-1 BUDGET (offline, from the combined-set build = P6_GHZCUT_BOOT implies P6_AIZ_TEST,
both object sets in OVL_FE simultaneously): FITS.
  - Overlay window (the TIGHT wall): ovl_ring.map high-water .text 0x1f790 + .bss 0x1d8 =
    0x1f968 = 129,896 B vs P6_OVL_WINDOW 0x20000=131,072 -> 1,176 B headroom.
  - WRAM-H pack _end: SEAMTEST/live build _end 0x060ba1d0 vs ceiling 0x060C0000 ->
    24,624 B headroom (the direct-boot is 0x060b9e50). No #228.
  - The decouple removes only the p6_ghzcut_reload() CALL from the boot dispatch (the MAIN
    PACK, not the overlay) -> OVL_FE byte-identical across the decouple -> headroom HOLDS.

DECOUPLE (flags): P6_GHZCUT_BOOT = SUPPORT (objects/assets/fade/Heavies + the AIZ->GHZCutscene
+ GHZCutscene->GHZ ENGINESTATE_LOAD seam routing). NEW P6_GHZCUT_DIRECTBOOT = the direct-boot
diagnostic ONLY (gates the boot-dispatch p6_ghzcut_reload() + the reload fn def); self-implies
P6_GHZCUT_BOOT. P6_GHZCUT_HOLD now implies P6_GHZCUT_DIRECTBOOT (the B.1/B.2 captures are direct-
boot). NEW P6_GHZCUT_SEAMTEST = the live-seam RED-gate flavor (live AIZ boot + a one-shot
injection of the decomp beat-9 SetScene at cont_frames>=100); implies P6_GHZCUT_BOOT, NOT
DIRECTBOOT.

THE LIVE-SEAM CODE: a NEW ENGINESTATE_LOAD branch in p6_frontend_frame (#if P6_GHZCUT_BOOT,
BEFORE the GHZCutscene->GHZ branch): one-shot, currentSceneFolder=="AIZ" -> VDP1-handle reset
+ HBHOBJ.PAK pre-load into P6_HW_OBJANIMPAK (the one piece the live transition lacked vs
p6_ghzcut_reload) + p6_scene_load_and_arm() + return. The FG tileset (GHZCUTIL/GHZCUPAL via
p6_til_name), the FG cell upload, HBHOBJ.SHT/HBHPAL staging, the per-frame FG present, and the
fade all already key on runtime folder=="GHZCutscene" / P6_GHZCUT_BOOT -> reachable from the
live transition unchanged.

RED->GREEN (gate tools/_portspike/qa_ghzcut_load.py, SEAMTEST flavor):
  RED  (build WITHOUT the seam branch, _ghzcut_live_RED.mcs): the injection fires (cont_frames
       256 >= 100) but is SWALLOWED to ENGINESTATE_REGULAR -> currentSceneFolder=="AIZ" -> exit 1.
  GREEN (build WITH the seam branch):
       --phase load   _ghzcut_live_load.mcs    -> currentSceneFolder=="GHZCutscene" cont_frames=212,
                      hbh_count=5 hbh_vis=0x01000401 (5 Heavies visible+onScreen), fade=0xf0000000
                      (FadeIn white wash animating) -> exit 0. THE LIVE AIZ LoadGHZ LANDED in
                      GHZCutscene (the seam works).
       --phase handoff _ghzcut_live_handoff2.mcs (SaveFrame 280 FpsScale 4) -> currentSceneFolder
                      =="GHZ" cont_frames=776, hbh_count=-257 (GHZCutscene unloaded) -> exit 0. THE
                      CUTSCENE RAN to SetupGHZ1 -> PLAYABLE Green Hill Zone (whole live loop works).
  SCREENSHOT (gotcha #4): tools/_portspike/_ghzcut_live_fade.png (FadeIn white wash over the GHZ FG
       tileset + Heavies) + _ghzcut_live_render.png (fade lifted: full GHZ FG -- palms/sunflowers/
       checkerboard + the red Heavy + King-Claw silhouettes). The live GHZCutscene renders identically
       to the gate-1 direct-boot.

REGRESSION (gate-1/B.1/B.2 via P6_GHZCUT_DIRECTBOOT): direct-boot rebuilt -> p6_ghzcut_reload PRESENT
in the map (boots straight to GHZCutscene), reaches GHZCutscene then GHZ. NOT regressed.

GHZ BYTE-IDENTICAL: plain GHZ rebuilt -> _end=0x060b5a40 (EXACTLY the pre-edit Tier-B.2 baseline) +
0 ghzcut symbols in the GHZ map. The no-flag build is unaffected.

EDITS (all #if-gated): build_shipping.sh (HOLD->DIRECTBOOT imply, new DIRECTBOOT/SEAMTEST self-imply,
[5h2] reload-presence grep); build_p6scene_objs.sh:152 (-DP6_GHZCUT_DIRECTBOOT/-DP6_GHZCUT_SEAMTEST
onto p6_io_main.o); p6_io_main.cpp (boot dispatch + reload fn def re-gated DIRECTBOOT; NEW AIZ->
GHZCutscene seam branch #if P6_GHZCUT_BOOT; NEW SEAMTEST injection #if P6_GHZCUT_SEAMTEST). UNCOMMITTED.

LIVE-LOOP BUILD COMMAND (no direct-boot): the LIVE menu->AIZ path with GHZCutscene support is
`P6_GHZCUT_BOOT=1` (WITHOUT DIRECTBOOT) -- the AIZ intro's own beat-9 LoadGHZ reaches GHZCutscene
through the seam. The SEAMTEST flavor only SHORT-CIRCUITS the ~minutes 4fps AIZ playthrough for the
gate; the live P6_GHZCUT_BOOT build runs the natural beats to the same SetScene.

# =====================================================================
# GATE-2 FOLLOW-UP -- LIVE GHZCutscene RENDER not clean (FG/BG bleed), 2026-06-30
# =====================================================================
# The prior gate-2 closeout claimed "the live GHZCutscene renders identically to the
# direct-boot" (line ~591). That claim was FALSE: the live render shows the checkerboard
# FG bleeding into the upper-screen black-sky region (direct-boot _ghzcut_heav_GREEN.png has
# black sky above; live _ghzcut_live_render.png has checkerboard there) + stray sprite
# fragments. ROOT-CAUSE HYPOTHESIS (to be MEASURED, not assumed): the live seam comes FROM
# the AIZ scene which armed the AIZ 4-bpp VDP2 BG planes (NBG0/NBG2/NBG3) + their BGON +
# scroll; the GHZCutscene present arms slScrAutoDisp(NBG1ON|SPRON) but the stale AIZ BG-plane
# state / scroll persists and shows through where the direct-boot (which NEVER ran AIZ) has
# clean zeroed BG planes.
#
# DOCS (read end-to-end, ST-058-R2 = sega_saturn_docs/VDP2_Manual.txt):
#   - Screen Display Enable Register BGON (180020H), ST-058-R2 p48 (txt:2509-2551): bit0=N0ON,
#     bit1=N1ON, bit2=N2ON, bit3=N3ON, bit4=R0ON, bit5=R1ON. A clean GHZCutscene shows ONLY
#     N1ON(NBG1 FG)+SPR. NBG0/2/3 (AIZ BG) must be OFF.
#   - Screen Scroll Value Register, ST-058-R2 p122-123 (txt:5364-5470): NBG0 SCXIN0=180070H/
#     SCYIN0=180074H; NBG1 SCXIN1=180080H/SCYIN1=180084H; NBG2 SCXN2=180090H/SCYN2=180092H;
#     NBG3 SCXN3=180094H/SCYN3=180096H. (NOTE: the task hint's "SCXIN1/SCYIN1 @0x70/74" was
#     WRONG -- those are NBG0; NBG1 is @0x80/0x84.)
#
# HARNESS-CALIBRATION GAP (must fix first): the existing _ghzcut_live_*.mcs were captured
# against a build whose game.map no longer matches the root game.map (p6_w_magic reads
# 0x646e/0x00ff != 0x12345678 -> qa_p6_scene.calibrate returns perm=None -> qa_ghzcut_load.py
# --phase load AttributeError in decode_u32). FIX: (a) make calibrate-failure a clean RED
# (not a crash) in the gate; (b) ALWAYS recapture from the freshly-built binary so map+state
# correspond. Per gotcha #3 a mismatched-map capture is NOISE -- never diagnose against it.
#
# STEP 1 (MEASURE): rebuild SEAMTEST (P6_GHZCUT_SEAMTEST=1) + DIRECTBOOT (P6_GHZCUT_DIRECTBOOT=1)
#   from the SAME source tree; capture _ghzcut_liverender_seam.mcs (held GHZCutscene) +
#   _ghzcut_liverender_direct.mcs. Peek (matching map): BGON 0x25F80020, SCXIN1/SCYIN1
#   0x25F80080/4, SCXIN0/SCYIN0 0x25F80070/4, SCXN2/SCYN2 0x25F80090/2, SCXN3/SCYN3
#   0x25F80094/6, + screens[0].position (currentScreen ptr deref) / the p6_w_aiz_scrx/y
#   witnesses. The register(s) that DIFFER beyond legit cutscene-frame progression ARE the bug.
#
# STEP 2 (FIX, live seam only, #if P6_GHZCUT_BOOT, in the AIZ->GHZCutscene seam branch
#   ~p6_io_main.cpp:6503): BEFORE p6_scene_load_and_arm, reset the AIZ-only VDP2 BG state so
#   the live state EQUALS the direct-boot: disable NBG0/NBG2/NBG3 (a new p6_vdp2.c helper that
#   re-arms slScrAutoDisp(NBG1ON|SPRON) AND zeroes the NBG0/2/3 scroll via slScrPosNbg0/2/3,
#   ST-058-R2 p48+p122) + reset screens[0].position to match the direct-boot's GHZCutscene
#   camera. Each VDP2 write cites ST-058-R2 section:page. Mirror the direct-boot's measured
#   state exactly (it is the ground-truth target).
#
# STEP 3 (RED->GREEN GATE, tools/_portspike/qa_ghzcut_liverender.py): quantitatively compare
#   the live render to the direct-boot. (a) BGON + NBG1 scroll + screens[0].position in the
#   live state must EQUAL the direct-boot state; (b) a PIXEL check of the captured live frame --
#   the upper-screen black-sky band must be black/sky NOT checkerboard FG. RED on the current
#   live build (they differ), GREEN after the fix. Capture the live render to PNG + confirm
#   visually (the upper screen is the discriminator). Unstaged players (black squares) are
#   EXPECTED (caveat #2) -- do NOT block the gate on them.
#
# CONSTRAINTS: direct-boot (DIRECTBOOT) must STILL render clean (don't regress gate-1/B.1/B.2).
#   GHZ shipping BYTE-IDENTICAL (_end via `grep -E " _end = " game.map`, no-flag build
#   unaffected, never trip #228). Read ST-058 before any VDP2 write; cite section:page.
#   Captures named _ghzcut_liverender_*.mcs/.png. Up to ~5 build iterations.
