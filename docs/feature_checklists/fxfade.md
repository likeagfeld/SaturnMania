# FXFade -- scene-seam fade transitions (source-only port batch 2026-07-17)

## Decomp source (ported verbatim; ZERO edits to the cached .c/.h)
- `tools/_decomp_raw/SonicMania_Objects_Cutscene_FXFade.c` (159 L, whole file)
- `tools/_decomp_raw/SonicMania_Objects_Cutscene_FXFade.h` (53 L)
- MANIA_USE_PLUS=0 (GAME_VERSION=3): the transitionScene field + LoadScene arm
  preprocess out (FXFade.h:24-26, FXFade.c:55-57,78-81) -- the simple arm ships.
- Sister reads: RubyPortal.c:352 (CREATE_ENTITY(FXFade, 0xF0F0F0) -- white),
  MenuSetup.c (menu transition fades -- overlay TU in the chain flavor),
  ERZStart (pack TU, GHZ-dead, the reason p6_closure_edge.c:118 has the NULL
  placeholder).

## Placement census (MEASURED, scene-bin parse 2026-07-17)
- AIZ Scene1: ONE placed entity, slot 2 pos (-28,60), vars timer=512
  speedIn=32 wait=0 speedOut=0(->32 default) color=0x000000 oneWay=1
  eventOnly=0 -> Create arms FXFade_State_FadeIn = a 16-frame fade-FROM-BLACK
  at AIZ intro start (the missing seam polish).
- GHZCutscene Scene1: 0 placed; manifest class (runtime CREATE_ENTITY sites).
- GHZ: not in the manifest (registration is P6_AIZ_TEST-gated; plain GHZ
  overlay byte-identical).

## Entity size
- EntityFXFade = 88 + state 4 + 4x int32 + color 4 + 4x bool32 = 128 B <= 344
  narrow stride (PLUS field compiled out). ObjectFXFade = bare RSDK_OBJECT.

## The Saturn render path (the ONLY non-mechanical piece; doc-cited)
- Decomp Draw = RSDK.FillScreen(color, timer, timer-128, timer-256)
  (FXFade.c:23-28) into the RSDKv5 SW framebuffer, which the Saturn true-port
  NEVER presents (the #309 Tier-B.1 finding). The established Saturn present
  for full-screen washes is the VDP2 Color Offset (p6_vdp2_fade_apply,
  ST-058-R2 Ch.13 p249-254; CLOFEN/COAx via SGL slColOffsetA -- the shipped
  FXRuby mechanism).
- FIX (the p6_cuthbh_draw shim precedent): FXFade registers with the
  p6_fxfade_draw SHIM, which latches live color/timer whenever the entity
  actually draws (zero pool scans -- the #324 foreach_all tail-hog lesson,
  7.8 ms/frame measured) then calls the verbatim FXFade_Draw. The api->fade_fn
  wrapper (p6_ghzcut_fade_fn) max-combines the latch with the FXRuby scan and
  feeds the one VDP2 offset. Scalar mapping: timer 0..512 == the FXRuby
  fadeWhite/fadeBlack convention (clamped 255 in p6_vdp2_fade_apply);
  mean-luma >= 128 -> white wash else black (shipped colors are 0x000000 and
  0xF0F0F0 only).
- Declared gap: in an AIZ-only flavor WITHOUT P6_GHZCUT_BOOT the bridge
  (engine call site + fade_fn export) is compiled out -> the latch is written
  but unread (fade invisible). The chain build defines both flags.

## Closure edges
- Zone (drawGroup pick, NULL-guarded FXFade.c:40), SceneInfo, CheckSceneFolder,
  FillScreen -- all pack/engine via -R. FXFade_StopAll: no caller in the
  shipped TU set (grep) -- gc'd or inert. No pack TU reads FXFade on shipped
  scenes -> the pack NULL placeholder stays, NO rewire seam.
- MenuSetup (overlay, chain flavor) now binds intra-overlay to the REAL FXFade
  -> menu transition fades become live entities driven through the same bridge
  (decomp-faithful improvement; previously CREATE_ENTITY on NULL = inert).

## Wiring
- Compile: `Cutscene_FXFade:Game_FXFade` in the P6_AIZ_TEST waiz loop
  (build_p6scene_objs.sh); link: Game_FXFade.o in the P6_AIZ_TEST OVL_FE
  section (build_shipping.sh:288).
- Register: p6_ovl_ghz.c AIZ block after FXRuby, Draw = p6_fxfade_draw shim.
- Witness: p6_w_fxfade_classid (pack global; ${P6_AIZ_TEST:+-u} rooted);
  written in the AIZ witness block.

## Audits (CLAUDE.md 4.5.1)
- Audit 1 layering: drawGroup = hudDrawGroup-1 (overHUD=0, verbatim Create) --
  the wash sits under the HUD exactly as PC; on Saturn the VDP2 offset applies
  post-composition (documented approximation, same as the FXRuby fade).
- Audit 2 cadence: timer ramp 512->0 at speedOut=32 = 16 frames (decomp
  State_FadeIn verbatim; the bridge samples per frame).
- Audit 3 pivot/flip: none (full-screen fill).
- Audit 4 boot budget: zero new loads (empty StageLoad; TU ~1.5 KB inside
  OVLRING.BIN).

## Gates
- RED (verified 2026-07-17 pre-fix): `py -3 tools/qa_registered_vs_placed.py`
  -> `MISSING: FXFade` under BOTH AIZ (10+1) and GHZCutscene (6 rows).
- GREEN required (post primary batch build): both FXFade rows gone
  (FXFade_StageLoad lands in ovl_ring.map); live chain capture
  p6_w_fxfade_classid > 0 on the AIZ leg; VISUAL: first AIZ frames ramp from
  black over ~16 frames (screenshot sequence -- compare against the previous
  abrupt cut); p6_w_ghzcut_fade witness shows a nonzero black term during
  those frames; chain _end < 0x060C8000; plain GHZ byte-identical (all
  additions #if P6_AIZ_TEST / OVL_FE-gated).

## Memory walls
- Pack .bss: +4 B witness. Overlay (AIZ flavor only): ~1.5 KB TU + 8 B latches.
