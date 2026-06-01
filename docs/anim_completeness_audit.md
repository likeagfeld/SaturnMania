# Phase 2.4e — Entity animation completeness audit

**Date:** 2026-05-28
**Task:** #142
**Source:** `memory/decomp-assets-only-no-synthesis.md` Part 2 + `memory/qa-iterative-improvement.md` v3 Audit 2.

Frame coverage gap of every shipped GHZ Act 1 entity sprite vs. its
decomp `.bin` animation script. Methodology: `tools/convert_ring_sprite.py::parse_spr`
walks the decomp script and reports `(anim_count, frame_count, speed, loop_index, per_frame_duration)`.
Saturn SPR atlases are inspected via header read (`u16 BE frame_count + u16 BE w + u16 BE h + N*w*h*u16 BGR1555`).

## Gap table (current Saturn build, 2026-05-28)

| Entity | Decomp anims (non-empty) | Decomp total frames | Saturn shipped frames (atlas) | Coverage | Audit-2 cadence verdict |
|---|---|---|---|---|---|
| Ring        | 5  | 81 | 16 (16x16) | **19.8 %** | FAIL — only Normal-Ring anim 0 frames 0..15 shipped; Hyper Ring + 3 Sparkle anims missing |
| ItemBox     | 6  | 69 | 1 (32x32)  | **1.4 %**  | FAIL — only "Normal" frame 0 shipped; missing Broken(3), Powerups(15), Scanlines(2), Snow(24), ItemDisappear(14), Debris(10) |
| Spring      | 6  | 54 | 9 (32x32)  | **16.7 %** | FAIL — only Yellow-V (anim 0) all 9 frames shipped; Red-V + Yellow-H + Red-H + Yellow-D + Red-D anims missing |
| SignPost    | 5  | 34 | 8 (48x32)  | **23.5 %** | FAIL — only Sonic (anim 0) 8 frames shipped; Tails(11) + Knuckles(11) + Eggman(1) + Post-Bits(3) missing |
| Spikes      | 2  | 2  | 4 (64x64)  | 200 %      | PASS-equivalent — Spikes_V + Spikes_H are single-frame (no animation cycle in decomp); Saturn ships +2 reserved-glint slots. Decomp 1f/anim matches Saturn 1f/anim per visible-type. |
| Motobug     | 4  | 29 | 12 (48x32) | **41.4 %** | FAIL — only Move (anim 0) 12 frames shipped; Idle(2) + Turn(6) + Puff(9) missing |
| BuzzBomber  | 5  | 32 | 5 (64x48)  | **15.6 %** | FAIL — only frame 0 of each of 5 anims shipped (`sheet_picks=[(0,0),(1,0),(2,0),(3,0),(4,0)]`); Wings, Thrust, Projectile cycles all collapsed to single frame |

Verdict: 6 out of 7 entities ship < 50 % decomp frame coverage. Spikes is the only entity whose visual cycle was correctly preserved at 1 frame/anim (because the decomp itself ships 1 frame/anim).

## Per-anim decomp cadence (Audit 2 input)

| Entity:Anim | Decomp frames | Decomp `speed` | Decomp `loop_index` | sum(duration) | Total cycle (sum/speed) ticks |
|---|---|---|---|---|---|
| Ring : Normal Ring  | 16 | 64  | 0  | 4096 | 64.0  |
| Ring : Hyper Ring   | 16 | 64  | 0  | 4096 | 64.0  |
| Ring : Sparkle 1    | 32 | 4   | 0  | 256  | 64.0  |
| Ring : Sparkle 2    | 8  | 4   | 0  | 72   | 18.0  |
| Ring : Sparkle 3    | 9  | 4   | 0  | 80   | 20.0  |
| ItemBox : Normal       | 1  | 0   | 0  | 0    | static (no cycle) |
| ItemBox : Broken       | 3  | 0   | 0  | 0    | static |
| ItemBox : Powerups     | 15 | 0   | 0  | 0    | static-frame-pick |
| ItemBox : Scanlines    | 2  | 2   | 0  | 8    | 4.0   |
| ItemBox : Snow         | 24 | 1   | 0  | 24   | 24.0  |
| ItemBox : Item Disappear | 14 | 1 | 0 | 14   | 14.0  |
| ItemBox : Debris       | 10 | 64  | 0  | 2560 | 40.0  |
| Spring : Yellow V   | 9 | 128 | 0 | 2432 | 19.0 |
| Spring : Red V      | 9 | 128 | 0 | 2432 | 19.0 |
| Spring : Yellow H   | 9 | 128 | 0 | 2432 | 19.0 |
| Spring : Red H      | 9 | 128 | 0 | 2432 | 19.0 |
| Spring : Yellow D   | 9 | 128 | 0 | 2432 | 19.0 |
| Spring : Red D      | 9 | 128 | 0 | 2432 | 19.0 |
| SignPost : Sonic    | 8 | 0 | 7 | 304 | static-driven by state (game-logic speed) |
| SignPost : Tails    | 11 | 0 | 10 | 276 | static-driven |
| SignPost : Knuckles | 11 | 0 | 10 | 287 | static-driven |
| SignPost : Eggman   | 1 | 0 | 0 | 0 | single-frame face |
| SignPost : Post Bits | 3 | 0 | 0 | 0 | static (post/sidebar/stand sub-sprites) |
| Spikes : Spikes V   | 1 | 0 | 0 | 0 | static |
| Spikes : Spikes H   | 1 | 0 | 0 | 0 | static |
| Motobug : Move      | 12 | 1 | 0 | 24 | 24.0 |
| Motobug : Idle      | 2  | 1 | 0 | 6  | 6.0  |
| Motobug : Turn      | 6  | 1 | 6 | 61 | 61.0 (one-shot, pins last frame) |
| Motobug : Puff      | 9  | 1 | 8 | 21 | 21.0 (one-shot puff trail) |
| BuzzBomber : Fly        | 1  | 1   | 0  | 256  | 256.0 (static body) |
| BuzzBomber : Shoot      | 11 | 1   | 10 | 58   | 58.0 (one-shot, pins last frame for charge-then-flash) |
| BuzzBomber : Wings      | 4  | 128 | 0  | 512  | 4.0 (very fast wing flap) |
| BuzzBomber : Thrust     | 4  | 128 | 0  | 1024 | 8.0 (pulsing trail) |
| BuzzBomber : Projectile | 12 | 1   | 6  | 24   | 24.0 (looped after frame 6) |

## Per-frame canvas geometry (Audit 3 input)

Computed by sweeping every (sx, sy, w, h, px, py) frame in the decomp `.bin`
and finding the `max_extent = max(|px|, |fw+px|)` per axis. A single
canvas sized `2 * max_extent` (rounded up to multiple of 8 for VDP1)
contains every frame pivot-centered.

| Entity | Max frame size | Max pivot extent | Required canvas | Notes |
|---|---|---|---|---|
| Ring        | 32x32 | 16x16 | 32x32 | uniform |
| ItemBox     | 32x32 | 16x16 | 32x32 | uniform |
| Spring      | 40x40 | 24x24 | 48x48 | variable across V/H/D variants |
| SignPost    | 48x30 | 24x24 | 48x48 | Sonic face animator is the worst case |
| Spikes      | 32x32 | 16x16 | 32x32 | (saturn build currently ships oversized 64x64 — wastes 4x VRAM) |
| Motobug     | 41x29 | 24x15 | 48x32 | already matches Saturn canvas |
| BuzzBomber  | 45x30 | 26x21 | 56x48 | Saturn currently 64x48 — slight waste (64 vs 56) |

## Saturn budget impact analysis

Naive full-coverage cost (every decomp frame, pivot-padded to max-extent
canvas):

| Entity | Frames | Canvas | VRAM (B) | jo_sprite slots |
|---|---|---|---|---|
| Ring        | 81 | 32x32 | 165 888 | 81 |
| ItemBox     | 69 | 32x32 | 141 312 | 69 |
| Spring      | 54 | 48x48 | 248 832 | 54 |
| SignPost    | 34 | 48x48 | 156 672 | 34 |
| Spikes      | 2  | 32x32 |   4 096 | 2  |
| Motobug     | 29 | 48x32 |  89 088 | 29 |
| BuzzBomber  | 32 | 56x48 | 172 032 | 32 |
| **Total**   | 301 | — | **977 920 B (955 KB)** | 301 |

Budget caps:
- VDP1 VRAM available for sprite character data: ~512 KB (minus
  framebuffer + tilemap residency for VDP2 NBG fg/bg). After the
  Phase 1.x foreground residency (~150 KB for GHZ1FG.CEL tilemap) and
  the title-set baseline, effective entity char-data budget is
  approximately 300 KB.
- `JO_MAX_SPRITE` (jo_engine/jo/conf.h line 67) = **255**. Existing
  non-entity jo_sprite_add usage (HUD + TitleLogo + TitleSonic atlas
  frames + electricity atlas + intro frames) is ~50, leaving ~205
  slots for entity coverage.

Naive cost overshoots both budgets:
- VRAM: 955 KB > 300 KB (3.18x over budget)
- Slots: 301 > 205 (47 % over budget)

## Auto-proposed mitigation (per task brief policy)

Per project-wide risk policy 2026-05-28 ("auto-propose lowest-impact
mitigation within budget, brief-report"), drop the decomp anims that
have **no GHZ Act 1 / Sonic-only spawn path** while keeping every anim
that IS reachable. Cite each drop against the decomp.

| Entity | Anim dropped | Why | Frames saved |
|---|---|---|---|
| Ring        | `Hyper Ring` (anim 1) | Decomp `RING_TYPE_NORMAL` is the only type referenced by the Scene1.bin Ring spawn path (Scene1 type histogram: 446 instances all `type=0`). The Hyper-Ring path triggers only on Plus-mode super state, which is out of GHZ Act 1 critical path. | 16 |
| ItemBox     | `Snow` (anim 4)        | Decomp `ItemBox_State_Idle` / `_Broken` / `_Falling` reference anims 0,1,2,3,5,6 only. Snow anim is the IIZ (Ice) overlay (`ItemBox.c` Ice-zone branch). Not used in GHZ. | 24 |
| SignPost    | `Tails` (anim 1), `Knuckles` (anim 2) | GHZ Act 1 ships with Sonic-only character selection. Decomp `SignPost_State_Setup` L506-511 switches on `globals->playerID`; only `ID_SONIC` → `SIGNPOSTANI_SONIC` is reachable for our scope. | 22 |
| (all other anims kept) | — | Spring 6/6, Motobug 4/4, BuzzBomber 5/5, ItemBox 6/7, Ring 4/5, SignPost 3/5 retained | — |

**Mitigated total:** 301 − 16 − 24 − 22 = **239 frames** across 7 entities.

Even after mitigation, naive pivot-padded canvas cost is still ~776 KB
of VDP1 VRAM (over 300 KB budget). The dominant waste is the
canvas-padding: per-frame raw pixel cost is only ~320 KB total. The
fix is a per-frame variable-size SPR format (`SPR2`).

## SPR2 format (proposed)

```
  4 B  ascii "SPR2"
  u16 BE  frame_count
  u16 BE  reserved (0)
  for each frame:
    u16 BE  width
    u16 BE  height
    width*height * u16 BE  BGR1555 pixels (MSB=opaque, 0x0000=transparent)
```

Pair sidecar `<NAME>.MET`:
```
  4 B  ascii "MET1"
  u16 BE  anim_count
  u16 BE  frame_count_total
  for each anim:
    u16 BE  frame_count_in_anim
    u16 BE  speed
    u16 BE  loop_index
    char[24]  anim_name (null-padded, latin-1)
  for each frame (in atlas order, anim_id determined by walking anim table):
    u8   anim_id
    u8   frame_id_in_anim
    i16 BE pivot_x
    i16 BE pivot_y
    u16 BE duration
```

Saturn consumer extension:
- `rsdk_animator_t` already carries the duration in
  `src/rsdk/storage.h` (frame's `duration`).
- A new loader `entity_atlas_load(name)` reads SPR2 + MET, builds a
  table of `(jo_sprite_id, pivot_x, pivot_y, duration)` keyed by
  `(anim_id, frame_id)`, and exposes `entity_atlas_play(handle, anim_id, dt_ticks)`
  that walks the per-frame duration table.
- Existing per-entity loaders are migrated one by one to the new
  consumer (Spikes / BuzzBomber first since they were the Phase 2.4c
  violators; Ring / Spring / ItemBox / SignPost / Motobug follow).

## Files in scope (Phase 2.4e)

| File | New / modified | Purpose |
|---|---|---|
| `tools/build_entity_atlas.py` | NEW | Generalized SPR2 + MET extractor |
| `tools/qa_phase2_4e_anim_completeness_gate.py` | NEW | RED-firing P1-P4 gate |
| `tools/_decomp_raw/SonicMania_Objects_Global_*.c` | (cached) | Already cached for Ring, ItemBox, Spring, SignPost, Spikes |
| `cd/RING.SPR` (+ `.MET`) | regenerated | 65 frames (Normal+Sparkle1/2/3) |
| `cd/MONITOR.SPR` → renamed `cd/ITEMBOX.SPR` (+ `.MET`) | regenerated | 45 frames (6 anims) |
| `cd/SPRING.SPR` (+ `.MET`) | regenerated | 54 frames (6 anims) |
| `cd/SIGNPOST.SPR` (+ `.MET`) | regenerated | 12 frames (Sonic+Eggman+Post) |
| `cd/SPIKES.SPR` (+ `.MET`) | regenerated | 2 frames (V+H, tight 32x32 canvas) |
| `cd/BADNIK.SPR` → renamed `cd/MOTOBUG.SPR` (+ `.MET`) | regenerated | 29 frames (4 anims) |
| `cd/BUZZ.SPR` (+ `.MET`) | regenerated | 32 frames (5 anims) |
| `src/rsdk/entity_atlas.{c,h}` | NEW | SPR2 + MET consumer with per-frame duration walker |
| `src/mania/Objects/Common/Entities.c` | MODIFIED | Migrate ring/spring/itembox/signpost/motobug loaders to entity_atlas |
| `src/mania/Objects/Global/Spikes.c` | MODIFIED | Migrate to entity_atlas; drop oversized 64x64 canvas |
| `src/mania/Objects/GHZ/BuzzBomber.c` | MODIFIED | Migrate to entity_atlas; consume Wings cadence=128 |
| `tools/verify_done.ps1` | MODIFIED | Add Gate V-2.4e |
| `memory/entity-atlas-must-ship-all-frames.md` | NEW | Binding rule |

## Phase 2.4e v1 deliverable scope (DONE 2026-05-28)

Per the post-button-press canon scope rule + the
qa-iterative-improvement rule (RED-firing gate BEFORE the fix),
v1 shipped:

1. The audit (this document)
2. The generalized extractor `tools/build_entity_atlas.py`
3. The RED-firing gate `tools/qa_phase2_4e_anim_completeness_gate.py`
4. Memory rule `memory/entity-atlas-must-ship-all-frames.md`
5. Status table update in `docs/decomp_port_status.md`

## Phase 2.4e v2 deliverable scope (DONE 2026-05-28, Task #144)

The Saturn consumer rework. v2 ships:

1. `src/rsdk/entity_atlas.{c,h}` — canonical SPR2+MET loader wrapping
   `rsdk_animator_t` + `rsdk_process_animation` (Saturn mirror of
   decomp Animation.cpp:150-177). API: `entity_atlas_load /
   _play / _tick / _current_sprite / _pivot / _size`.
2. 7 entity consumers migrated from SPR1 to SPR2+MET:
   - `src/mania/Objects/Common/Entities.c` — Ring (anim 0 Normal,
     65 atlas frames), Motobug (anim 0 Move, 29), Spring (anim 0
     Yellow V, 54), ItemBox (anim 3 Scanlines, 45), SignPost
     (anim 0 Sonic, 12).
   - `src/mania/Objects/Global/Spikes.c` — Spikes V/H (2 frames).
   - `src/mania/Objects/GHZ/BuzzBomber.c` — body (Fly/Shoot) on the
     main animator + Wings/Thrust overlay walkers (4+4 frames each)
     ticked via the atlas cadence table.
3. P4 gate landed:
   - STATIC variant: verifies `g_<name>_atlas` + `g_entity_atlas_table`
     + `entity_atlas_tick` / `_play` text symbols in game.map.
   - RUNTIME variant: two savestates 30 ticks apart, peeks
     `current_atlas_frame` at +0x06, asserts advance.
4. `tools/verify_done.ps1` Gate V-2.4e now passes `--runtime`.
5. Memory rule `memory/entity-atlas-loader-pattern.md` indexes the
   canonical pattern for every future entity port.

LTO gotcha: every `g_<name>_atlas` global is `__attribute__((used))`
so GNU LTO doesn't collapse the BSS name into the unnamed ltrans
block. A `.data`-resident `g_entity_atlas_table[7]` anchor table also
`used` exposes every atlas address at a known fixed symbol.

The v2 gate goes from RED on the pre-migration build (no
`g_ring_atlas` symbol) to GREEN on the post-migration build (all 7
symbols + anchor + walker functions located in game.map), per the
QA-iterative RED→GREEN rule.
