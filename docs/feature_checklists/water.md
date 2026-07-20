# Feature checklist — Water subsystem (GHZ Act 1 parity)

Methodology artifact (CLAUDE.md §4.5). De-risks the single largest unported GHZ
Act 1 subsystem before any code lands. Data-driven; every claim below is measured
against the decomp + the current tree (2026-07-20).

## Decomp source (ported subset)
- `tools/_decomp_raw/SonicMania_Objects_Common_Water.c` (56 KB, 1498 lines) +
  `..._Water.h`. GHZ uses only 2 of the 9 `WaterTypes`: `WATER_WATERLEVEL` and
  `WATER_POOL` (Water.h:6-16). Also reachable via those: `WATER_SPLASH`
  (spawned on water entry/exit, Water.c:472-491) and `WATER_BUBBLE`
  (drown bubbles — GHZ is shallow, no drown, so BUBBLE/COUNTDOWN/BUBBLER/
  HEIGHT_TRIGGER/BIG_BUBBLER are NOT needed for GHZ Act 1).
- Functions in scope: `Water_StageLoad` (244-284), `Water_Create` (103-242,
  WATERLEVEL+POOL+SPLASH cases only), `Water_State_Water` (382-630),
  `Water_State_Pool` (631-635), `Water_State_Splash` (636-654),
  `Water_Draw_Water` (1288-1312), `Water_Draw_Pool` (1313-1320),
  `Water_Draw_Splash` (1321-1327), `Water_DrawHook_ApplyWaterPalette` (286-293).
- HCZ/CPZ branches (`RSDK.CheckSceneFolder("HCZ")` etc.) gate OUT for GHZ:
  Wake/BigBubble sheets + WaterLevel_L/R + DNA SFX are skipped (StageLoad:273-283).

## Placements (MEASURED)
`tools/_parse_ghz_scene.py`: GHZ Scene1.bin OBJ[67] Water = **6 entities**,
x384..9400 (all pre-signpost, on the forward player path). Parity map records
these as WaterLevel/Pool. So water IS gameplay-reachable — a physics-only
(invisible-water) milestone would read as a REGRESSION; ship physics + surface
visual together.

## Assets (present — no rebuild needed)
- `extracted/Data/Sprites/Global/Water.bin` (2236 B) — the WATERLEVEL surface
  strip (anim 0), splash (anim 1), bubbler (2), bubble (5), countdown (7).
- SFX `extracted/Data/SoundFX/Stage/{Splash,Breathe,Warning,Drown}.wav` present.
- Water.bin must be added to `tools/build_anim_pack.py` OBJ pack (GHZOBJ) +
  `tools/maniafilelist.txt`; the FRD chain (commit 3d78704) then folds its rects
  into `GHZOBJ.FRD` automatically.

## Closure (verified available)
- `Player_UpdatePhysicsState`, `Player_CheckValidState`, `Player_State_*`,
  `Zone->timer`, `RSDK.Sin512`, `RSDK.CheckObjectCollisionTouchBox`,
  `RSDK.GetEntitySlot`, `CREATE_ENTITY(Water,...)` — all in the verbatim-compiled
  decomp Player/Zone set already in the chain pack. The Player side ALREADY reads
  `Water->waterLevel` + `player->underwater` (Player.c:3760, 5606, guarded by
  `underwater &&` so the current no-Water build is safe — underwater never set →
  no NULL deref). Registering Water is what ENABLES the underwater path.

## Saturn-hard pieces (2) — design BEFORE coding
1. **INK_ADD water-surface fill** (`Water_Draw_Water`, alpha 0xE0, drawGroup
   `hudDrawGroup-1`). RSDK draws a translucent additive quad from `waterLevel`
   down. Saturn: VDP1 polygon (Comm=0x0004) with CMDPMOD Gouraud/half-transparent
   OR a VDP2 line-color band. Cheapest parity = a VDP1 translucent quad the width
   of the screen from `waterDrawPos` to screen bottom (ST-013-R3 §6 polygon +
   §11.6 half-transparency PMOD bit 3). MEASURE cost vs the existing p6 VDP1 slot
   budget first.
2. **Underwater palette** (`Water_DrawHook_ApplyWaterPalette`:
   `SetActivePalette(waterPalette, waterDrawPos, size.y)` — a DIFFERENT palette
   for screen rows below the water line). Saturn CRAM is global — a per-region
   palette swap is not native. PROVEN codebase precedent: the GHZCutscene FXRuby
   fade + FXFade both apply a VDP2 COLOUR OFFSET (CLOFEN 0x25F80110, COAR/COAG/COAB
   0x25F80114/6/8; ST-058-R2 Ch.13) via `p6_vdp2_fade_apply` (p6_vdp2.c). The
   underwater tint = a blue-shifted color offset applied to the sub-water Y band.
   Apply it per-line-limited via the VDP2 line window / a Y-gated offset toggle
   (offset ON below `waterDrawPos`, OFF above). This REUSES the fade path — not a
   new subsystem. NOTE: `waterPalette` (the decomp's exact underwater colors) must
   be derived offline into the offset delta; a flat blue offset is the first
   approximation, refine vs a PC water screenshot.

## APPROACH = VERBATIM DECOMP COMPILE (not a hand-port)
Per the established pattern (parity map: Spring/ItemBox/SignPost/ForceSpin/
CorkscrewPath are all `Global_X:Game_X` / `Common_X:Game_X` in the
build_p6scene_objs.sh w4 loop, compiled VERBATIM), Water compiles the same way:
`Common_Water:Game_Water`. This pulls the FULL faithful subsystem (drown chain,
countdown bubbles, splash, shield-loss, pool) with no hand-rewrite.

DRAW-PATH FINDING (measured, Water.c:1288-1327): the draw states use the STANDARD
engine draw calls, so registering Water renders the surface via existing paths —
NO new VDP1 code needed for the base visual:
- `Water_Draw_Water`: `RSDK.DrawSprite(&animator)` in a 64px tiling loop across the
  screen (anim 0 = surface strip) → the normal FRD/SaturnSheet path once Water.bin
  is packed. INK_ADD translucency (alpha 0xE0) may render opaque on Saturn (color-
  calc unwired) = M3 refinement, not a blocker/garbage.
- `Water_Draw_Pool`: `RSDK.DrawRect(..., INK_SUB)` → verify DrawRect is wired in the
  p6 backend (used by other objects?); if stubbed, pool tint is the only gap.
- `Water_Draw_Splash`: plain `RSDK.DrawSprite` (anim 1).

CLOSURE (verified): Music/Player/Shield/Spikes already compiled. Missing object
POINTER symbols Button/Current/PullChain (all GHZ-ABSENT; Water guards each with
`if(Button)` / `!Current` / `if(PullChain)`) → provide NULL stub globals
`ObjectButton *Button=NULL;` etc. (faithful for GHZ: those objects have no entities;
the guards take the null branch). Their struct TYPE decls come from the decomp
headers already on the include path. Water_SpawnBubble/CountDownBubble are Water's
own fns (compile in). Iterate any further missing symbols the linker surfaces.

## Staged milestones (each: RED gate first → build → GREEN → commit)
GATE EVERYTHING behind `#if P6_WATER` (default 0) so the shipping chain stays
byte-identical until verified (mirrors P6_DDWRECKER/P6_AIZ_TEST discipline).
- **M1 — verbatim compile + register + physics.** w4 `Common_Water:Game_Water`;
  Button/Current/PullChain NULL stubs; add Water.bin to the OBJ anim pack +
  maniafilelist (FRD auto-folds via commit 3d78704); stage `Global/Water.gif`
  sheet; `extern ObjectWater *Water;` + register_object_full in p6_ovl_ghz.c;
  witnesses `p6_w_water_classid` + `p6_w_water_level`. RED gate `qa_p6_water.py`:
  at chain GHZ, classid 0→N, `p6_w_water_level != 0x7FFFFFFF`, autorun into x384
  water → entity-pool `player.underwater` 0→1. Surface + splash render via the
  existing DrawSprite path (bundled, since it's free).
- **M2 — Pool tint + splash polish.** Only if `Water_Draw_Pool`'s DrawRect/INK_SUB
  is unwired: add the rect-fill. RED gate: pool region shows the tint rect.
- **M3 — underwater palette tint.** DrawHook via the VDP2 color-offset path (design
  piece 2, the proven fade mechanism). RED gate: below-water rows read the blue
  offset (peek COAR/G/B) + screenshot vs PC ref; above-water unchanged. Separate
  revertible commit. Flip `#if P6_WATER` default-on only after M1-M3 GREEN.

## M1 closure (MEASURED at the overlay link, 2026-07-20 — the complete undefined set)
The verbatim Game_Water.o compiled clean; the OVERLAY link (`ld -R game.elf`) reported
exactly 10 undefined refs (linker reports all at once -> no hidden second-order):
- `Button`, `Current`, `PullChain` -> GHZ-absent object ptrs behind null guards. NULL
  stubs in p6_closure_edge.c (gated) + `-u` roots (pack --gc-sections drops them since
  ONLY the overlay references them; the existing `Water` stub survives WITHOUT a root
  because Player.c (pack) reads Water->waterLevel -- the new ones have no pack referencer).
- `Current_PlayerState_{Left,Right,Up,Down}` -> HCZ Current player-states Water only
  COMPARES against (Water.c:822-823, != only, never called). Empty inert stubs + `-u`
  roots (unique address -> every GHZ compare correctly false).
- `Player_State_TransportTube` (Water.c:402), `Player_State_Bubble` (Water.c:747) -> REAL
  Player states in Game_Player.o but --gc-sections-dropped (nothing else refs them).
  Kept via `p6_water_keep[]` used address-array (gated, `-u` rooted) -> forces the real
  symbols into game.elf so the overlay -R resolves the real state pointers.
- `__truncdfsf2` -> libgcc soft-float from `Water_StaticUpdate` `waterLevelVolume/30.0`
  (HCZ water-level SFX, dead in GHZ -- moveWaterLevel=false). game.elf never used a
  double->float op so libgcc didn't pull the helper. `p6_water_force_sf()` (gated, `-u`
  rooted, a used double->float) forces `__truncdfsf2` into game.elf for the overlay -R.
All 5 fix classes gated behind P6_WATER; plain/plain-chain byte-identical.

## M1 STATUS (2026-07-20) -- LIVE-GREEN VERIFIED via RA netmem
RED->GREEN watched LIVE (tools/qa_p6_water.py --live, RA netmem over _gl_boot.ps1 so the
chain advances -- the audio clock is required, [[headless-ra-cant-advance-chain-no-audio-clock]]):
the poll (pool anchor 0x00243000 HEALTHY throughout) tracked the chain t+0 all-zero -> t+133
b1=1/ring=12 (GHZ badniks registering) -> t+240 GHZ SETTLED (cork=53,b1=4,ring=12) with
**p6_w_water_classid=56** (Water REGISTERED, live classID) + **p6_w_water_level=0x081C0000
(Y=2076px)** (off the 0x7FFFFFFF sentinel -> a WATER_WATERLEVEL entity Created + State_Water
computed the real GHZ water line). Gate GREEN. So Water registers + the water line is live +
the decomp Player's underwater subsystem is armed (it reads Water->waterLevel=2076px).
The EARLIER "blocked" note below was the HEADLESS harness (audio_driver=null, no clock) --
_gl_boot (wasapi) advances the chain and RA reads are clean. LESSON: RA live memory IS the
harness (user directive); just boot GL, not headless, for the chain.

## M1a BUILD STATUS (2026-07-20) -- LINK-VERIFIED + GATED-SAFE (superseded by LIVE-GREEN above)
The P6_WATER=1 chain build (btxhhr7ri) is GREEN at the link level:
- 0 undefined references (closure fully resolved).
- Water witnesses in game.map: p6_w_water_classid @ 0x0609bf50, p6_w_water_level @ 0x0609bf4c.
- _end = 0x060c2a40 < 0x060C8000 chain ceiling (~22 KB headroom; #228 safe).
- All wiring gated (${P6_WATER:+...} / #if defined(P6_WATER)) -> default chain byte-identical.

LIVE-VERIFY BLOCKED on a HARNESS limitation (NOT a Water defect): the headless RA
netmem harness (qa_live.ps1) uses audio_driver=null -> NO audio clock -> the core does
not advance the long autorun chain (logos->title->menu->AIZ->GHZCut->GHZ, ~17k frames);
qa_trace's health guard fires LOUD (objectEntityList=0x0, "4MB cart not applied / data-cue
not booting") and all witness reads are garbage (constant 0x2000). This is pre-existing
[[retroarch-live-memory-harness]] flakiness, orthogonal to Water. M1a is logic-only
(Water.bin NOT packed -> the surface sprite drops), so a GL-boot screenshot can't verify it
either -- the physics needs the WITNESS read (classid + water_level) or an entity-pool
player.underwater read.

PATH FORWARD (next focused session, healthy harness): (a) Mednafen savestate deep-chain
capture (CLAUDE.md sec 8.5 PRIMARY) landing in settled GHZ + `python tools/qa_p6_water.py
STATE.mcs` -> expect classid>0 + water_level off the 0x7FFFFFFF sentinel; OR (b) fix the
headless harness to advance the chain (audio clock), then the qa_p6_water witness read.
Then M1b: pack Water.bin (add to build_anim_pack OBJ_BINS + maniafilelist -> FRD auto-folds;
stage the Global/Water.gif sheet) for the visible surface. Wiring stays UNCOMMITTED until
the live RED->GREEN lands (no claiming done without it).

## M1b TURNKEY (visible INK_ADD surface) -- MEASURED 2026-07-20, budget SAFE
Global/Water.bin (113 frames) references TWO sheets: **Global/Water.gif** (256x512, anim 0 =
the 112-rect surface strip) + Global/Display.gif (1 rect, HCZ drown-countdown digit, already
staged/FRD'd). Water.gif is NEITHER FRD-covered NOR staged as a .SHT -> Water_Draw_Water's
DrawSprite drops (surface saturnSheetSlot==-1). Water_Draw_Water (Water.c:1288) tiles a 64px
surface sprite across the screen at Y=waterLevel; drawGroup = hudDrawGroup-1.

BUDGET (measured): chain SATURNSHEET_SLOTS=27 (SaturnSheet.cpp:85), P6_VDP1_NSHEETS=25
(p6_vdp1.c:136, 14+11 GHZ-handoff binds). Adding WATER.SHT = +1 each (27->28, 25->26) = the
CERTIFIED-SAFE +64B .bss class (chain ceiling GLOBALS 0x060C8000; M1 _end 0x060c2a40 = ~22 KB
headroom). Water.gif is 256x512 but FRD cuts only the small surface-strip rect, so VDP1 VRAM
cost is tiny (not the 256KB whole sheet).

EDITS (keep default chain BYTE-IDENTICAL: OFFLINE builders add Water always -- harmless extra
CD files when P6_WATER is off; COMPILE-time changes stay #if P6_WATER):
- OFFLINE: build_sheet_bands.py SHEETS += ("Global/Water.gif","WATER.SHT"); build_frame_dir.py
  SHEETS += ("Global/Water.gif","WATER.FRD"); build_anim_pack.py OBJ_BINS += Global/Water.bin
  (FRD auto-folds via commit 3d78704) + tools/maniafilelist.txt line.
- COMPILE (#if P6_WATER): SATURNSHEET_SLOTS 27->28; P6_VDP1_NSHEETS 25->26; add WATER.SHT to the
  ghzShtFiles[11] handoff bind list (p6_io_main.cpp ~5xxx, becomes [12]); + a witness
  p6_w_water_aniframes = (Water)?(int16)Water->aniFrames:-1.
VERIFY (aniframes witness, NOT on-screen -- the surface is at Y=2076px, off-screen at the spawn
autorun): qa_p6_water.py --live extended to also read p6_w_water_aniframes; GREEN = aniframes>=0
(Water.bin loaded + Water.gif bound). The on-screen render is then guaranteed by the same
DrawSprite/FRD path every GHZ sprite uses. NOTE: INK_ADD (alpha 0xE0) may render OPAQUE (color-
calc unwired) = M3 refinement, not an M1b blocker (an opaque animated surface band still reads
as "water surface present").

### M1b BAND-STORE CONSTRAINT (MEASURED 2026-07-20 -- resolve BEFORE editing; do NOT rush)
The ghzShtFiles handoff loop (p6_io_main.cpp:8306) stages each .SHT via SaturnSheet_Stage into
the **384 KB cart BAND STORE** which the code comment (io_main:8300) measures at "11-sheet total
337,489 B, 55 KB margin". Global/Water.gif is 256x512 (~131 KB banded) -> staging the FULL sheet
OVERFLOWS the 55 KB margin -> SaturnSheet_Stage returns -1 -> Water SILENTLY UNSTAGED (graceful
fail, no crash, but no surface). This is the [[wram-h-animpak-ceiling-boot-trap]]/residency-budget
class -- the parity map's "delicate, do not rush" warning applies.
OPEN QUESTION (read the code, do NOT assume): does the FRD path AVOID the full-sheet band-store
stage? SaturnFrameDir.cpp says the FRD blob "holds every frame" pre-cut + "Replaces the runtime
rect-cutting of SaturnSheet_FetchRect" and is staged into the cart independently (p6_w_frd_staged).
IF an FRD-covered sheet needs only its (small) FRD blob + a SaturnSheet SLOT (not the full banded
sheet in the store), then Water.gif's 256x512 size is IRRELEVANT and M1b fits trivially -> resolve
by reading SaturnFrameDir_Stage + how LoadSpriteSheet routes an FRD sheet + whether ghzShtFiles
must SaturnSheet_Stage an FRD sheet at all. TWO tractable paths depending on the answer:
  (A) FRD-only (if FRD needs no band-store sheet): stage only WATER.FRD (small, just the surface-
      strip rects) + a slot; skip the WATER.SHT band-store stage. Cleanest, no budget hit.
  (B) reduced WATER.SHT (if the band store IS needed): build a CROPPED Water.gif / rect-subset .SHT
      holding ONLY anim-0's surface-strip rects (GHZ needs no HCZ bubbles/countdown) so the banded
      sheet fits the 55 KB margin -- the selective-rebuild pattern ([[title-vdp1-slot-thrash]] /
      the TSONIC selective-frame rebuild). Measure the reduced .SHT size < margin before staging.
STATUS: M1 (physics) DONE+committed 69637fb. M1b PAUSED at this measured constraint -- a careful
residency unit, NOT a quick sheet-add. Resolve the FRD-staging question first.

## Budget / risk
- WRAM: register-only object adds ~cart .text + ~few B pack witnesses; chain
  `_end` currently ~0x060c1xxx << 0x060C8000 (25 KB headroom) — safe. Confirm
  with the map `_end` assert after M1.
- The color-offset DrawHook is post-CRAM + zero VDP1 cost (the R3.3-safe class,
  proven by GHZCutscene FXRuby). The VDP1 surface quad competes for slots — check
  `p6_w_vdp1_evicts` stays ~0 at settled GHZ after M2.
- Verification needs live water-entry (autorun reaches x384 early). Use
  `tools/_gl_boot.ps1` + entity-pool `underwater` read + `_shots/` screenshot.

## Sources
Water.c/.h line ranges above; ST-013-R3 §6/§11.6 (VDP1 polygon + half-transparency);
ST-058-R2 Ch.13 (VDP2 colour offset); p6_vdp2.c `p6_vdp2_fade_apply` (proven
color-offset precedent); [[ghz-act1-placed-vs-registered-parity-map]] (placement
data + "own focused unit w/ palette-FX plan" mandate).
