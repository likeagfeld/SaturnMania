# GHZ Act 1 — Entity Parity Gap Table (Phase 2.4c / Task #140)

**Generated:** 2026-05-28
**Source of truth (decomp):** `tools/_decomp_raw/SonicMania_Objects_GHZ_*.c` and
`tools/_decomp_raw/SonicMania_Objects_Global_*.c`
**Source of truth (Scene.bin instance counts):**
`extracted/Data/Stages/GHZ/Scene1.bin`, parsed via
`tools/parse_title_entities.py` (with the KNOWN_NAMES set extended to all
GHZ/Global object classes for hash-resolution).

## Methodology

The Scene1.bin entity table was parsed end-to-end (85,191 / 85,191 bytes
consumed). 75 object classes are present; 53 resolve to known names with
the extended hash dictionary. The remaining 22 "<unknown HASH>" entries
have zero entities in 17 cases (slot-pre-reservation only); 5 have ≥1
entity and are tracked in §"Unhashed".

For each entity class:
- **Decomp file** = upstream RSDKModding canonical .c file.
- **Saturn port** = `src/mania/Objects/.../` C file (status: none /
  partial / shipped).
- **Cd asset** = the converted Saturn-side .SPR/.BIN slot, or empty.
- **Scene1 count** = entity instances in GHZ Act 1.

## Gap table (sorted by Scene1 instance count, desc)

| Class | Scene1 count | Decomp file | Saturn port | Cd asset | Status |
|---|---:|---|---|---|---|
| Ring | 446 | `SonicMania_Objects_Global_Ring.c` | `Common/Entities.c::rings_*` | `RING.SPR` + `GHZ1RINGS.BIN` | PARTIAL — hand-rolled subset of Ring_State_Normal + AABB collect. Decomp animations 0 (Ring) + 1 (Sparkle) for loss; only spin frames active. Hitbox correct (-8..8 / -8..8 per Ring.c:122). Loss-on-hurt scatter (Ring_LoseRings) NOT ported. |
| PlaneSwitch | 106 | `SonicMania_Objects_Common_PlaneSwitch.c` | none | none | **MISSING** — but PlaneSwitch is collision-plane gating (A/B-plane swap), not visible. Phase 2.5+ when multi-plane geometry lands. |
| SpikeLog | 61 | `SonicMania_Objects_GHZ_SpikeLog.c` | `GHZ/SpikeLog.c` | `SPIKELOG.SP2/.MET` + `GHZ1LOG.BIN` | PARTIAL — Phase 2.4c.2 (Task #147) ported State_Main with full 32-frame anim cycle + hazard window (frames 8-11) + decomp hitbox `{-8,-16,8,0}`. State_Burn (fire-shield path + BurningLog spawn) deferred to Phase 2.5. |
| Platform | 59 | `SonicMania_Objects_Common_Platform.c` | `Common/Platform.c` | `PLATFORM.SP2/.MET` + `GHZ1PLAT.BIN` | PARTIAL — Phase 2.4c.2 ported FIXED/FALL/LINEAR/CIRCULAR types + PLATFORM_C_PLATFORM/SOLID collision (top-only stand). 13 remaining type variants (SWING/TRACK/PATH/PUSH/REACT/...) fall through to FIXED draw. Child carrying + non-GHZ collisions deferred Phase 2.5. |
| Ring (dup row above) |  |  |  |  |  |
| Spikes | 41 | `SonicMania_Objects_Global_Spikes.c` | none | none | **MISSING** — 41 instances, hazard. Spikes_State_Static + hitbox -16..16/-16..16. Priority. |
| ItemBox | 38 | `SonicMania_Objects_Global_ItemBox.c` | `Common/Entities.c::itembox_*` | `MONITOR.SPR` + `GHZ1BOX.BIN` | PARTIAL — break-on-airborne only. No powerup table (rings/shield/invuln/super). 1 frame loaded (decomp has 6 contents-icon frames). |
| Spring | 35 | `SonicMania_Objects_Global_Spring.c` | `Common/Entities.c::spring_*` | `SPRING.SPR` + `GHZ1SPRG.BIN` | PARTIAL — vertical-up only (3 frames). Horizontal + diagonal variants + 8 colors (yellow/red) NOT ported. |
| BreakableWall | 23 | `SonicMania_Objects_Common_BreakableWall.c` | none | none | MISSING — destruction wall block. |
| BoundsMarker | 22 | `SonicMania_Objects_Global_BoundsMarker.c` (or Common) | none | none | MISSING — camera/respawn bounds; invisible. Phase 2.5+ when respawn lands. |
| Newtron | 21 | `SonicMania_Objects_GHZ_Newtron.c` | `GHZ/Newtron.c` | `NEWTRON.SP2/.MET` + `GHZ1NEWT.BIN` | PARTIAL — Phase 2.4c.2 ported both NEWTRON_SHOOT + NEWTRON_FLY variants with range-trigger (hitboxRange `{-128,-64,128,64}`), alpha fade-in (State_Appear), 90-tick Shoot cycle, 16-tick StartFly + horizontal glide, FadeAway respawn. Projectile sub-entity spawn deferred Phase 2.5 (depends on Player_Hurt). Flame overlay drawn from anim 5 during NS_FLY. |
| Decoration | 21 | `SonicMania_Objects_Common_Decoration.c` | none | none | **MISSING** — 21 instances, visible flowers/totems/etc. Pure visual; defers to Phase 2.5. |
| BuzzBomber | 18 | `SonicMania_Objects_GHZ_BuzzBomber.c` | none | none | **MISSING** — 18 instances, flying badnik with projectile drop. Priority. |
| InvisibleBlock | 18 | `SonicMania_Objects_Common_InvisibleBlock.c` | none | none | MISSING — invisible solid; Phase 2.5+. |
| CollapsingPlatform | 15 | `SonicMania_Objects_Common_CollapsingPlatform.c` | none | none | MISSING — pixel-trigger platform crumbles. |
| Bridge | 13 | `SonicMania_Objects_GHZ_Bridge.c` | none | none | **MISSING** — 13 GHZ rope bridges; visible + Sonic-bows-it-down physics. Phase 2.5+. |
| Chopper | 13 | `SonicMania_Objects_GHZ_Chopper.c` | none | none | **MISSING** — jumping-fish badnik (water-spawn). 13 instances. |
| ForceSpin | 13 | `SonicMania_Objects_Common_ForceSpin.c` | none | none | MISSING — slope force-ball trigger. |
| BGSwitch | 12 | `SonicMania_Objects_Common_BGSwitch.c` | none | none | MISSING — palette/parallax swap zones; visible only as side-effect. |
| Crabmeat | 11 | `SonicMania_Objects_GHZ_Crabmeat.c` | none | none | **MISSING** — 11 instances, ranged badnik (pincer projectile). |
| Motobug | 9 | `SonicMania_Objects_GHZ_Motobug.c` | `Common/Entities.c::motobug_*` | `BADNIK.SPR` + `GHZ1BUGS.BIN` | PARTIAL — patrol-only port (State_Move L122-156). Missing: Smoke trail (State_Smoke), turn anim (State_Turn), proper 30-tick turn pose. Cull margin matches sprite size only. |
| Batbrain | 7 | `SonicMania_Objects_GHZ_Batbrain.c` | none | none | MISSING — bat-badnik (hangs from ceiling, swoops). 7 instances. |
| Water | 6 | `SonicMania_Objects_Common_Water.c` | none | none | MISSING — water region; Phase Z (palette FX). |
| SpinBooster | 4 | `SonicMania_Objects_Common_SpinBooster.c` | none | none | MISSING — directional speed-pad. |
| StarPost | 4 | `SonicMania_Objects_Global_StarPost.c` | none | none | MISSING — checkpoint marker; Phase 2.5+ when respawn lands. |
| SignPost | 2 | `SonicMania_Objects_Global_SignPost.c` | `Common/Entities.c::signpost_*` | `SIGNPOST.SPR` + `GHZ1SIGN.BIN` | PARTIAL — touch -> +1000 + cleared flag. Spin frames defined but no real Mania 4-face cycle; act-clear cinematic skipped. |
| Player | 2 | `SonicMania_Objects_Global_Player.c` | `Global/Player.c` | `SONIC.SPR` etc | PARTIAL — Phase 2.2 port (Sonic 1/2-style physics + Mania constants). Spindash + peelout + drop-dash NOT ported. |
| CorkscrewPath | 2 | `SonicMania_Objects_GHZ_CorkscrewPath.c` | none | none | MISSING — loop-de-loop path. |
| ForceUnstick | 2 | `SonicMania_Objects_Common_ForceUnstick.c` | none | none | MISSING — Phase 2.5+. |
| HUD | 1 | `SonicMania_Objects_Global_HUD.c` | `Common/Entities.c::hud_*` | `DIGITS.SPR` | PARTIAL — score/rings/timer drawn; life icon + life count NOT ported. |
| TitleCard | 1 | `SonicMania_Objects_Global_TitleCard.c` | none | none | MISSING — act-name reveal cinematic. Phase 2.5+. |
| Music | 1 | `SonicMania_Objects_Global_Music.c` | `src/rsdk/audio.c` | CD-DA track 2 | PARTIAL — entity wraps CD-DA start; current Saturn wire is direct call from GHZSetup_StageLoad. Functionally equivalent. |
| DDWrecker | 1 | `SonicMania_Objects_GHZ_DDWrecker.c` | none | none | MISSING — GHZ Act 1 boss (one instance — boss arena). Phase 2.6 (GHZ boss). |
| Animals | 0 | `SonicMania_Objects_Global_Animals.c` | none | none | Reserved (spawns from broken ItemBox). |
| Debris | 0 | `SonicMania_Objects_Global_Debris.c` | none | none | Reserved (spawns from badnik defeat). |
| ScoreBonus | 0 | `SonicMania_Objects_Global_ScoreBonus.c` | none | none | Reserved (popup on badnik/itembox hit). |
| Shield | 0 | `SonicMania_Objects_Global_Shield.c` | none | none | Reserved (spawned when ItemBox=shield). |
| SuperSparkle | 0 | none | none | none | Reserved (Super state visual). |
| GHZSetup | 0 | `SonicMania_Objects_GHZ_GHZSetup.c` | `GHZ/GHZSetup.c` | (palette + tile layer config) | PARTIAL — StageLoad triggers Saturn engine bring-up (NBG1 FG + NBG2 sky + BGM kick). StaticUpdate palette rotation + DrawAniTiles waterfall/flowers NOT ported. |
| (Splats, BurningLog, ZipLine, DERobot, Eggman, CheckerBall, CutsceneSeq, GHZ2Outro) | 0 each | various | none | none | Reserved or Act-2-only. |

## "Unhashed" entries (5 with entities, hash-unmatched in our known table)

| Hash | Entity count | Likely class (deduced from hash convention) |
|---|---:|---|
| `875e224b9f42431b139ffb14ee92afd6` | 7 | First-in-table after engine globals; correlates with `Dust`-class or unnamed scene marker |
| `1111239cec4364df9e22f8dd8fc447af` | 3 | Likely GHZ-set decoration variant (small count) |
| `82ca2f1d7fe89871d6c87940e8190af8` | 3 | Likely GHZ-set decoration variant (small count) |

These do not move gameplay-critical visuals; they can be resolved by
adding their string names to `parse_title_entities.KNOWN_NAMES` once
identified from the full GameObjects.h list.

## Priority list (highest-impact, gameplay-critical, missing or
weakly-ported)

Sorted by (Scene1 instance count × gameplay impact ÷ port effort):

1. **Spikes (41 instances)** — currently 0 visible/active on Saturn.
   Hazard. Required for any walk-into-spike scenario. PORT NOW.
2. **BuzzBomber (18 instances)** — currently 0. Ranged badnik;
   players see them flying overhead immediately on autorun. Has
   projectile sub-entity (Phase 2.4c covers patrol; projectile defer
   1 iteration). PORT NOW.
3. **Motobug** — currently shipped patrol-only. Upgrade to add cadence
   audit (per-frame duration table from Motobug.bin) and proper
   28-px hitbox per decomp (`Motobug->hitboxBadnik = {-14,-14,14,14}`
   per Motobug.c:140-143). UPGRADE NOW.
4. **Newtron (21 instances)** — ranged glider/turret badnik. Two
   variants (Glider + Shooter). PORT NEXT.
5. **Crabmeat (11 instances)** — pincer-projectile badnik. PORT NEXT.
6. **Chopper (13 instances)** — water-jumping fish. Self-contained
   pattern. PORT NEXT.

After 1-6: Bridge (13, gameplay platform), Platform (59, mostly visual
on Act 1), CollapsingPlatform (15), Batbrain (7).

## Asset coverage

| Class to port now | Source atlas | Source animation .bin | Decomp aniFrames spec |
|---|---|---|---|
| Spikes | `Sprites/Global/Items.gif` | `Sprites/Global/Spikes.bin` | `LoadSpriteAnimation("Global/Spikes.bin")` — animation 0 = horizontal, 1 = vertical (16x16 cell × 4 spikes = 64 px) |
| BuzzBomber | `Sprites/GHZ/Objects.gif` | `Sprites/GHZ/BuzzBomber.bin` | animation 0 = body, 2 = wings, 3 = thrust, 4 = projectile |
| Newtron | `Sprites/GHZ/Objects.gif` | `Sprites/GHZ/Newtron.bin` | animation 0 = idle/shoot, 1 = glide |
| Crabmeat | `Sprites/GHZ/Objects.gif` | `Sprites/GHZ/Crabmeat.bin` | animation 0 = walk, 1 = attack |
| Chopper | `Sprites/GHZ/Objects.gif` | `Sprites/GHZ/Chopper.bin` | animation 0 = jump cycle |
| Batbrain | `Sprites/GHZ/Objects.gif` | `Sprites/GHZ/Batbrain.bin` | animation 0 = hang, 1 = swoop |
| Bridge | `Sprites/GHZ/Objects.gif` | `Sprites/GHZ/Bridge.bin` | animation 0 = stake, 1 = log |

The `extracted/Data/Sprites/GHZ/` and `extracted/Data/Sprites/Global/`
directories already contain every required `.bin` + `.gif`. The Saturn-
side converter at `tools/convert_ring_sprite.py` is the pattern to
replicate per-class (frame-list-driven multi-frame BGR1555 extraction).

## Phase 2.4c scope vs deferral

**This iteration ports (in priority order):**
1. Spikes (Global) — 41 instances, hazard. New `Spikes.c` under
   `src/mania/Objects/Global/`. Asset `SPIKES.SPR` + `GHZ1SPIKE.BIN`.
2. BuzzBomber (GHZ) — 18 instances, ranged badnik patrol+shoot.
   `BuzzBomber.c` under `src/mania/Objects/GHZ/`. Asset
   `BUZZ.SPR` + `GHZ1BUZZ.BIN`. Projectile sub-entity stubbed (Phase
   2.4d follow-up).
3. Motobug cadence + hitbox audit — modify existing Motobug code in
   `Common/Entities.c` to read per-frame duration from Motobug.bin
   and match decomp's exact `Motobug->hitboxBadnik` rectangle.

**Defers cleanly (not in this iteration):**
- Newtron, Crabmeat, Chopper, Batbrain, Bridge, Platform,
  CollapsingPlatform, BreakableWall, SpikeLog, DDWrecker (boss),
  BoundsMarker (respawn), StarPost (checkpoint), Decoration (visual),
  TitleCard, Water, BGSwitch, PlaneSwitch, ForceSpin, ForceUnstick,
  SpinBooster, InvisibleBlock, CorkscrewPath, Animals, Debris,
  ScoreBonus, Shield, SuperSparkle, Splats, BurningLog, ZipLine,
  DERobot, Eggman, CheckerBall, CutsceneSeq, GHZ2Outro.

Each deferred class has a row in this table with its decomp file +
Scene1 count so the next iteration can pick up at the top of the
priority list mechanically.
