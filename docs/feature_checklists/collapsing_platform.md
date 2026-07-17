# CollapsingPlatform — GHZ ground-break (task 2026-07-16)

## Decomp source (ported verbatim, Saturn-gated deltas cited below)
- `tools/_decomp_raw/SonicMania_Objects_Common_CollapsingPlatform.c` (375 L, whole file)
- `tools/_decomp_raw/SonicMania_Objects_Common_CollapsingPlatform.h` (70 L)
- Sister files read: `Common/BreakableWall.c` (the crumble spawns
  `CREATE_ENTITY(BreakableWall, BREAKWALL_TILE_DYNAMIC, ...)`;
  `BreakableWall_State_FallingTile` removes the source tile via
  `RSDK.SetTile(-1)` only after `timer >= 3` frames — BreakableWall.c:160-175)

## Placement census (MEASURED, tools/_cplat_census.py over every Scene*.bin)
- 203 placements game-wide; whole-game worst tile count = 176 (GHZ2 slot 45, 16x11)
- GHZ1 = 15 placements, worst 121 tiles (slot 828, 11x11); all respawn=False
- AIZ / GHZCutscene / Menu / Title: 0 placements

## Saturn constraint + delta (the ONLY non-mechanical translations)
1. `EntityCollapsingPlatform.storedTiles[256]` (512 B) makes the entity ~656 B.
   Scene slots are NARROW = sizeof(EntityBase) = 344 (Object.hpp:88,114);
   RegisterObject refuses > P6_MAX_ENTITY_SIZE and ResetEntitySlot refuses an
   oversize class in a narrow slot (Object.hpp:92-94). Budget for extras =
   344-88(RSDK_ENTITY)-56(scalars) = 200 B = 100 tiles < the 121/176 measured
   ceilings -> NO clamped array can be correct.
   FIX (precedent: CutsceneHBH colors[128]->[1], Cutscene/CutsceneHBH.h:50-62):
   shrink `storedTiles` to [1] under SATURN_GLOBALS_RETARGET and read the tiles
   LIVE via `RSDK.GetTile(self->targetLayer, x+startTX, y+startTY)` inside
   State_Left/Right/Center. Equivalence proof: the layer is untouched between
   Create and the crumble loop for every shipped placement (removal happens
   >= 3 frames later per BreakableWall.c:164-166; all GHZ placements
   respawn=False so no second crumble reads an already-emptied region).
   Entity drops to ~148 B <= 344.
2. VDP2 FG present cache: SaturnLayout_SetTile mutates the layout store but the
   static FG map rebuilds only on `p6_vdp2_present_dirty || camera tile cross`
   (p6_vdp2.c:2023) -> a crumble under a STANDING player stays drawn (declared
   BreakableWall ghost gap, SaturnLayout.cpp:417-421). FIX: SetTile marks
   `p6_vdp2_present_dirty = 1` (extern "C"; p6_vdp2.o and SaturnLayout.o are
   both unconditional pack members, build_p6scene_objs.sh:841,858). Cost = the
   same rebuild already paid on every 16-px camera crossing.

## Assets
- Anim `Global/TicMark.bin`: ALREADY in GHZOBJ.PAK (build_anim_pack.py:144,
  BreakableWall) -> no pack rebuild. Debug-only visual (visible=false in play).
- Sfx `Stage/LedgeBreak.wav`: not staged as PCM -> GetSfx -1 -> PlaySfx no-op
  (same declared gap as BreakableWall's break sfx).

## Wiring (the BreakableWall recipe)
- Compile verbatim TU: `Common_CollapsingPlatform:Game_CollapsingPlatform` in
  the w4 list (build_p6scene_objs.sh:400-425)
- Overlay-resident: `Game_CollapsingPlatform.o` after Game_BreakableWall.o in
  the ovl_ring $LD list (build_shipping.sh:318) — same link as BreakableWall
  (whose object pointer + EntityBreakableWall the crumble states reference)
- Register in p6_ovl_ghz.c after the BreakableWall block via
  api->register_object_full; witness p6_w_cplat_classid (pack global,
  p6_io_main.cpp; -u rooted in build_p6scene_objs.sh like p6_w_breakwall_classid)
- Registers in BOTH plain GHZ and chain flavors (the overlay list is
  unconditional) — CollapsingPlatform is a GHZ manifest class in both.

## Audits (§4.5.1)
- Audit 1 layering: no new draw path in play (visible=false unless
  DebugMode->debugActive; Draw = decomp DrawLine/DrawSprite verbatim).
  Crumble visuals ride the already-shipped BreakableWall dynamic-tile path.
- Audit 2 cadence: no animation walker (single TicMark frame, debug only).
- Audit 3 pivot/flip: FLIP_X/XY/Y DrawSprites are debug-only; verbatim decomp.
- Audit 4 boot budget: zero new synchronous loads (TicMark already in
  GHZOBJ.PAK; overlay grows ~2-4 KB inside the existing OVLRING.BIN read).

## Gates
- RED (verified before fix): `py -3 tools/qa_registered_vs_placed.py --scenes GHZ`
  -> `MISSING: CollapsingPlatform`, 12 missing, exit 1.
- GREEN required: missing count 12 -> 11 (row gone); runtime
  p6_w_cplat_classid > 0 in a live GHZ capture; crumble observable (FG tiles at
  a placed platform change after standing on it; screenshot VIEWED);
  edge audit p6_w_edge_hits: no CollapsingPlatform-own-stub hit (it has no
  closure stub — all callees are pack/overlay-resident);
  no speed regression (emulated-vblank anchor), chain `_end` < 0x060C8000.

## Memory walls
- Overlay window P6_OVL_WINDOW = 0x30000 (196,608); OVLRING.BIN currently
  166,240 B -> ~30 KB headroom for the ~2-4 KB TU.
- Pack .bss growth: +4 B witness. Entity pool: +15 created entities in GHZ1
  (narrow slots, 148 B class in 344 B stride).
