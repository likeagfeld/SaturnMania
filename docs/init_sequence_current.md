# Current build — init sequence (Phase 1.3+1.4 build)

Source: `src/main.c` (229 lines) + `src/rsdk/*` + `src/mania/Game.c`.
This build renders ONLY the NBG2 TITLE.DAT backdrop. VDP1 sprites invisible.
Phase 1.4 proved sprite invisibility holds even for probe sprites drawn
directly via `jo_sprite_draw3D` from `mania_tick` — bug is below app layer.

## jo_main (src/main.c:111..228) — ordered Saturn-hardware-state-mutating calls

| # | file:line | call | args / notes |
|---|------|------|--------------|
| 1 | main.c:119 | `jo_core_init` | `JO_COLOR_RGB(96,128,224)` — back-color = title sky-blue |
| 2 | main.c:124 | `slPriorityNbg0` | `0` |
| 3 | main.c:125 | `slPriorityNbg1` | `0` |
| 4 | main.c:126 | `slPriorityNbg2` | `0` |
| 5 | main.c:127 | `slPriorityNbg3` | `0` |
| 6 | main.c:130 | `rsdk_object_init()` | pure data init (memset). NO hardware writes. |
| 7 | main.c:131 | `rsdk_drawing_init()` | pure data init (memset g_rsdk_screen). NO hardware writes (drawing.c:20-31). |
| 8 | main.c:132 | `rsdk_input_init()` | pure data init. NO hardware writes. |
| 9 | main.c:133 | `rsdk_audio_init()` | **memset on sfx_table ONLY** (audio.c:31-35). **DOES NOT call `jo_audio_init()`** — unlike archived setup_audio at line 457. |
| 10 | main.c:134 | `rsdk_save_init()` | pure data init. |
| 11 | main.c:135 | `mania_engine_init()` | calls rsdk_palette_init, rsdk_scene_init, registers 5 Title classes, **setup_title_assets()** (jo_sprite_add calls). NO hardware register writes (no sl* / jo_audio_init). |
| 12 | main.c:167 | `jo_create_palette_from` | dummy 16-entry zero palette to seed CRAM slot for NBG1 |
| 13 | main.c:171 | `jo_vdp2_set_nbg1_8bits_image` | 8x8 dummy 8-bit image (Phase 1.3 "trigger NBG1 cycle-pattern config") |
| 14 | main.c:173 | `slPriorityNbg1` | `0` (hide NBG1 — config done) |
| 15 | main.c:196 | `setup_title_bg()` | TITLE.PAL→jo_create_palette_from, TITLE.DAT→jo_vdp2_set_nbg2_8bits_image, slScrPosNbg2(0,0). |
| 16 | main.c:200..207 | `slPrioritySpr0..7` | **all 8 = 6** — Phase 1.3 ADDED THIS (archived does NOT) |
| 17 | main.c:209 | `slPriorityNbg2` | `5` (below sprite priority) — same as archived. |
| 18 | main.c:219 | `jo_audio_play_cd_track` | track 3 (title BGM) — same as archived. |
| 19 | main.c:223 | `rsdk_load_scene_by_name("Title")` | parses SCENE1.BIN, **fires per-class StageLoad** (each class's `RSDK.LoadSpriteAnimation` etc.). NO hardware register writes (verified — grepped src/rsdk/ + src/mania/ for sl* and jo_audio_init). |
| 20 | main.c:226 | `jo_core_add_callback(mania_tick)` | per-frame tick |
| 21 | main.c:227 | `jo_core_add_vblank_callback(fg_vblank)` | CRAM dirty-range drain via 16-bit short writes to JO_VDP2_CRAM (NOT slDMAXCopy). |
| 22 | main.c:228 | `jo_core_run()` | enter main loop |

## Comparison-relevant absences vs archived

The current build is MISSING (vs archived):
- (A) `setup_ghz_foreground()` — the archived calls this BEFORE setup_title_bg. It does `jo_vdp2_set_nbg1_8bits_image(...g_fg_num_cells*8 bytes)` (line 308) + `slPriorityNbg1(6)` (line 309) + `slDMACopy(cel, nbg1_cell, n*64)` (line 318). Current builds substitute a `jo_vdp2_set_nbg1_8bits_image` with an 8x8 dummy and hides with priority 0.
- (B) `jo_audio_init()` call. Archived setup_audio at line 457 calls it. Current `rsdk_audio_init` does NOT.
- (C) `intro_video_play("INTRO.CPK")` between hide-NBGs and setup_ghz_foreground. Skipped on this build (no INTRO.CPK shipped). Marginal — its job is to take VDP2 and release it.

The current build ADDS (vs archived):
- (D) `slPrioritySpr0..7 = 6` (8 sprite-priority writes). Archived NEVER writes sprite priority — defaults are relied on.
- (E) `fg_vblank()` does CRAM short writes via `JO_VDP2_CRAM`, NOT `slDMAXCopy`. The archived `fg_vblank` does `slDMAXCopy` to NBG1 map (different purpose — there's no CRAM drain because palettes are written directly via jo_create_palette_from before jo_core_run).

## Key observation re Phase 1.4 probe-sprite invisibility

The archived build calls `jo_sprite_add` MANY times BEFORE `jo_core_run`, then draws via `jo_sprite_draw3D` from `game_tick` — and sprites render. The Phase 1.4 probe did the same thing (`jo_sprite_add` in `jo_main`, draw via `jo_sprite_draw3D` from `mania_tick`) and sprites did NOT render. The difference must be a hardware-state delta between the two builds, NOT an application-layer logic delta.

The candidate hardware-state deltas are A, B, D above.
