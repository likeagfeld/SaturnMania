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
