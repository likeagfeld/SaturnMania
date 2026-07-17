# Splats -- GHZ ink badnik (source-only port batch 2026-07-17)

## Decomp source (ported VERBATIM, zero Saturn code deltas)
- `tools/_decomp_raw/SonicMania_Objects_GHZ_Splats.c` (440 L, whole file)
- `tools/_decomp_raw/SonicMania_Objects_GHZ_Splats.h` (79 L)
- Sister context: the GHZ arm only is live (Splats_StageLoad
  CheckSceneFolder("GHZ") -> GHZ/Splats.bin + initialState =
  Splats_State_BounceAround, Splats.c:84-87). The PSZ1 ink-jar arm
  (SPLATS_SPAWNER/INKSPLATS/SPLAT states) compiles but is folder-dead.

## Placement census (MEASURED, tools/_parse_ghz_scene.py over Scene1+Scene2)
- GHZ1: `OBJ[59] Splats 0 entities`; GHZ2: `OBJ[59] Splats 0 entities`.
- Splats is in the GHZ stage_config manifest (docs/scene_objects.json) but has
  ZERO authored placements in either act -> the port is manifest/DebugMode
  closure (DEBUGMODE_ADD_OBJ(Splats), Splats.c:110), not a placed-entity fix.
  The qa_registered_vs_placed RED row still requires the class to LINK.

## Entity size (computed from the census include tree, no clamp needed)
- EntitySplats = RSDK_ENTITY(88) + state(4) + 3xu8+pad + minDelay(2+pad) +
  delay(4) + isOnScreen(4) + parent(4) + startPos(8) + startDir(4) +
  2x Animator(24) = 172 B <= 344 narrow stride. No Saturn shrink.
- ObjectSplats ~ 40 B <= 592 static cap.

## Assets
- Anim `GHZ/Splats.bin` (parsed: 1 anim "Bounce", 2 frames, sheet
  `GHZ/Objects.gif` == GHZOBJ.SHT already staged at the GHZ landing) -- ADDED
  to tools/build_anim_pack.py OBJ_BINS (primary rebuilds GHZOBJ.PAK).
- Sfx `PSZ/SplatsSpawn.wav` + `PSZ/SplatsLand.wav`: GetSfx -1 -> PlaySfx no-op
  if unstaged (same declared gap as BreakableWall's LedgeBreak.wav). GHZ arm
  only reaches sfxSplatsLand from the PSZ-only bounce states -- GHZ-dead.

## Closure edges (read whole file; decomp graph)
- Player_CheckBadnikTouch / Player_CheckBadnikBreak (Splats.c:138-143),
  Player_CheckCollisionBox / Player_CheckCollisionTouch (:229-230) -- all pack
  symbols already -u rooted for the 6-badnik set (build_p6scene_objs.sh).
- Zone->objectDrawGroup/collisionLayers, DebugMode, Player -- pack via -R.
- No pack TU references Splats (grep-verified) -> no rewire seam, no stub.

## Wiring (the f0d0f30 CollapsingPlatform recipe)
- w4 compile: `GHZ_Splats:Game_Splats` (build_p6scene_objs.sh)
- Overlay link: `Game_Splats.o` after Game_Batbrain.o (build_shipping.sh)
- Register: p6_ovl_ghz.c register_object_full after Batbrain (unconditional --
  Splats is a GHZ manifest class in plain + chain flavors, like the badniks)
- Witnesses: p6_w_splats_classid / p6_w_splats_aniframes (p6_io_main.cpp pack
  globals, -u rooted)

## Audits (CLAUDE.md 4.5.1)
- Audit 1 layering: Draw = verbatim decomp (splash under main, drawGroup
  objectDrawGroup[0] GHZ arm). No placements -> no live Z interaction to map.
- Audit 2 cadence: GHZ arm sets frameID directly from velocity.y sign
  (Splats.c:196), no ProcessAnimation walker on the live path.
- Audit 3 pivot/flip: FX_FLIP via direction, verbatim decomp DrawSprite.
- Audit 4 boot budget: GHZ/Splats.bin ~90 B inside the existing GHZOBJ.PAK
  read; Game_Splats.o ~3 KB inside the existing OVLRING.BIN read. Zero new
  synchronous CD opens.

## Gates
- RED (verified 2026-07-17 pre-fix): `py -3 tools/qa_registered_vs_placed.py
  --scenes GHZ` -> `MISSING: Splats` (11 missing, exit 1).
- GREEN required (post primary batch build): the Splats row disappears
  (11 -> 10); ovl_ring.map contains Splats_StageLoad; live capture
  p6_w_splats_classid > 0 and p6_w_splats_aniframes >= 0; chain _end <
  0x060C8000; plain-GHZ regression suite unchanged.

## Memory walls
- Overlay window P6_OVL_WINDOW 0x30000; adds ~3 KB TU.
- Pack .bss: +8 B (2 witnesses). Entity pool: +0 (no placements).
