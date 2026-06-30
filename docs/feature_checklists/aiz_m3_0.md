# Feature checklist — M3.0: engine LoadScene + render the AIZ intro-cutscene background

Faithful M3 (user-chosen 2026-06-26): port the Cutscenes/"Angel Island Zone" intro
scene the menu start-game targets (`MenuSetup.c:1121` `SetScene("Cutscenes","Angel
Island Zone")` -> stage folder **`AIZ/Scene1.bin`**). M3.0 = the first rung: the
engine LOADS the non-GHZ AIZ folder and renders its FG tilemap; cutscene MOTION
(Tornado/claw/ruby) is M3.2/M3.3.

## Decomp source (read top-to-bottom, sister-file rule satisfied)

- `tools/_decomp_raw/SonicMania_Objects_AIZ_AIZSetup.{c,h}` — scene driver (StageLoad
  sets up layers/palette/sfx + spawns the cutscene; the cutscene state machine is M3.2).
- `..._Cutscene_CutsceneSeq.{c,h}` + `CutsceneRules.h` — the sequencer (M3.2 driver).
- `..._AIZ_AIZTornado.{c,h}`, `AIZTornadoPath.{c,h}`, `AIZKingClaw.{c,h}`,
  `AIZEggRobo.{c,h}` — cutscene actors (registered M3.0; bodies tick M3.2).
- `..._ERZ_PhantomRuby.{c,h}`, `..._Cutscene_FXRuby` (M3.3), `Decoration`.

## Engine sites (read, file:line) — the keystone is NARROW, all mechanical mirrors

| Edit | Site (p6_io_main.cpp) | Mirror of |
|---|---|---|
| G1 menu->AIZ load handler + `P6_AIZ_TEST` boot inject | the `ENGINESTATE_LOAD` block 5858-5941 (the swallow at 5941) | the `P6_FRONTEND_CHAIN` block 5878-5939 (residency resets + `p6_title_reload()` + `return`) |
| `p6_aiz_reload()` (scan "AIZ", arm, load) | new fn after `p6_menu_reload` (~5760) | `p6_title_reload` 5717-5757 (gameplay variant: route through `p6_isAIZ` arm) |
| G3 `p6_isAIZ` cell-upload | `p6_scene_load_and_arm` 5311 + the `p6_isTitle` block 5313-5331 | `p6_isTitle` (folder strcmp + `p6_vdp2_upload_cells` on real folder change) |
| Witnesses | the front-end witness block | the existing `_p6_w_*` ints |

## Overlay (additive — NO new file)

`p6_ovl_ghz.c::p6_overlay_entry` already registers GHZ + front-end classes via
`api->register_object_full(&Class, "Name", sizeof(Entity), sizeof(Object), update,
lateUpdate, staticUpdate, draw, create, stageLoad, serialize)` (ABI `p6_ovl_api.h:92`).
Add the 14 AIZ classes gated `#if defined(P6_FRONTEND_MENU)`. The decomp object bodies
compile to `Game_<Obj>.o` (the GHZ pattern) added to `build_p6scene_objs.sh` overlay list.
M3.0: register all 14 so LoadScene resolves the types; cutscene Update bodies tick from
M3.2 (M3.0 only needs Create/StageLoad + the background to render).

## ANIMPAK invariant (#228 — the #1 risk, MEASURED-SAFE)

Menu flavor `_end=0x060B95E0` is 10.6 KB ABOVE `P6_HW_ANIMPAK=0x060B6C00` but boots
fine BECAUSE `p6_io_main.cpp:3589/3665` gates the `GHZANIM.PAK`-at-ANIMPAK load behind
`#if !defined(P6_FRONTEND_TITLE)` — the menu/AIZ flavor loads NO WRAM-H anim pack.
**Binding M3.0 rule:** AIZ anims (M3.2) stage to CART (`AIZOBJ.PAK`@cart, mirror
`GHZOBJ.PAK`); NEVER re-introduce a WRAM-H pack in this flavor. M3.0 (no object draw)
loads no anim pack at all. Gate: `qa_p6_mapoverlap` must not regress.

## Assets — M3.0 set ALREADY STAGED

- `cd/AIZTIL.BIN` 222,208 B (tileset cells) — staged.
- `cd/AIZPAL.BIN` 1,024 B — staged.
- `cd/AIZ1LAYT.BIN` 7,695 B (band store; fits 0xC800, 43.5 KB margin) — built 2026-06-26.
- `AIZOBJ.SHT` (object sprite bands) = **M3.2** asset (cutscene actors draw), not M3.0.

## Proactive-detection audits (CLAUDE.md §4.5.1)

- **Audit 1 — Z/layering:** M3.0 renders only FG tilemap (no cutscene sprites). Layer
  order from `AIZ1LAYT.BIN` header (6 layers): Background 1-4 (M3.1) behind FG Low/High
  (M3.0). FG-Low index read from the band-store header (G8); GHZ present reused.
- **Audit 2 — animation cadence:** N/A for M3.0 (no animated sprite drawn). M3.2 parses
  AIZTornado/Claw/EggRobo `.bin` durations via `convert_ring_sprite.parse_spr` before
  the walker; engine `ProcessAnimation` is the verbatim GHZ cadence (no new walker).
- **Audit 3 — pivot+flip:** N/A for M3.0. M3.2: FX_FLIP actors use the engine
  `p6_draw_flipped` (GHZ-proven RSDK `entity_x - pivotX - width`).
- **Audit 4 — boot-delay:** AIZ sync-path add = AIZTIL cells (217 KB / 150 = 1.55 s) +
  AIZ1LAYT (7.7 KB, 0.05 s) + Scene1.bin (12 KB, 0.18 s) = ~1.8 s. Under 5 s. Player
  sheets reuse GHZ resident.

## RED gate — DONE, RED-confirmed

`tools/_portspike/qa_p6_aiz_scene.py` (forces the AIZ load via `P6_AIZ_TEST`, decoupled
from the menu confirm — mirrors F.1/F.2 `P6_TRANSITION_TEST`):
- A1 `p6_w_aiz_loaded==1` (LoadScene of folder AIZ finished)
- A2 `p6_w_aiz_fg_hash!=0` (FG cells uploaded with content -> tilemap renders)
- A3 `p6_w_aiz_objcount>0` (overlay registration closure resolved)
RED on current build (`--static`): 3/4 witnesses absent. GREEN target after M3.0.

## Build / verify

- Flavor: existing `P6_FRONTEND_MENU` (the build that reaches AIZ; no new flavor).
- One Docker build at a time. After: `qa_p6_aiz_scene.py` (capture, GREEN) + AIZ-background
  screenshot (user pixel-verify) + `qa_p6_ghz_regression.py` (GHZ byte-identical, gated)
  + `qa_p6_mapoverlap` (_end not regressed) + `pwsh tools/verify_done.ps1` exit 0.

## Files to create/modify (M3.0)

- `tools/_portspike/_p6/p6_io_main.cpp` — witnesses + `p6_aiz_reload` + `p6_isAIZ` + G1/inject.
- `tools/_portspike/_p6/p6_ovl_ghz.c` — 14 AIZ `register_object_full` calls (`#if P6_FRONTEND_MENU`).
- `tools/_portspike/_p6/build_p6scene_objs.sh` — compile the AIZ/Cutscene `Game_*.o` + `-u` witness roots.
- `tools/_decomp_raw/...` AIZ/Cutscene `.c` bodies -> the port tree (faithful copies, compiled).
- (done) `tools/_portspike/qa_p6_aiz_scene.py`, `cd/AIZ1LAYT.BIN`.
