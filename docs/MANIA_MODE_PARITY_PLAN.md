# Mania Mode Parity — Master Execution Plan

**Date:** 2026-05-28
**Mandate (user, verbatim, 2026-05-28):**

> "EVERYTHING has to be EXACTLY A REPLICA of the original game"
>
> "make sure you are using actual game assets.... dont make your own....
> this needs to be a decomp accurate recompliation!!! it should be
> exactly like the original game"
>
> "this doesnt seem like the canonical first level of the game once
> someone selects mania mode"

**Status:** Draft. User must review §3 (Saturn-fit deviations requiring
explicit approval) + §5 (sequence + agent budget) before any new sub-agent
dispatch.

This document supersedes `docs/FRAME_TO_FRAME_PARITY_PLAN.md`
(2026-05-26) as the active execution plan. The older plan's scope analysis
is still valid; this plan focuses on the next-iteration sequence with
current-state grounding.

---

## §0 — Brutally honest current state (post Phase 2.4a + 2.4c partial)

### What works
- **Boot stub** → title scene state machine → autoadvance → GHZ Act 1 NBG/sprite rendering chain end-to-end
- **Title scene**: TitleSonic compound (body anim 0 + finger anim 1 overlay at frame 48), TitleLogo, electricity arc, press-start prompt, ribbon wave, cloud parallax (Phase 1.34b)
- **Title backdrop**: SUPPRESSED per Phase 1.40 user-explicit override — clouds-only, no Title3DSprite billboards, no TitleBG entities
- **GHZ Act 1 rendering**: NBG1 cell-mode foreground (FG.CEL+PAT+TMP per Phase 2.3k buffer fix), NBG2 sky (Phase 2.3l LWRAM bypass), Sonic sprite (idle + walk subset), 5 SFX (sample_rate fix Phase 2.4a), CD-DA BGM track 2
- **GHZ entities**: 5 entity types ported partially — Ring (1 frame), ItemBox (1 frame), Spring (1 frame), Motobug (decomp-cited hitbox), SignPost (1 frame), Spikes (frame 0 only, Phase 2.4c), BuzzBomber (frame 0 only, Phase 2.4c)
- **Player physics**: TileLayer collision via rsdk_process_path_grip (Phase A5-wire)
- **Tooling**: Mednafen savestate harness (Phase 1.30), hardened deep-frame capture (Phase 2.3i), build pipeline reliability (Phase 2.3k-pre full src/*.o purge), fail-loud propagation (Phase 2.3k-mid g_ghz_load_error_code)

### What's missing for Mania Mode parity
Documented per-subsystem in §2.

### What's a Saturn-fit deviation requiring user approval
Documented per-item in §3.

---

## §1 — Canonical Mania Mode boot flow (the target)

What the actual Mania PC Steam build does, in order:

| Step | Decomp source | Saturn current state |
|---|---|---|
| 1. Sega splash | `tools/_decomp_raw/SonicMania_Objects_Logos_SegaSplash.c` (pull) | Missing entirely |
| 2. RSDK logo | `Objects/Logos/RSDKLogo.c` (pull) | Missing entirely |
| 3. Mania intro Cinepak (Mania.bik/Mania.ogv) | `Objects/Title/TitleSetup.c` `State_FadeToVideo` (cached, lines TBD) + RSDK Video API | Partial: Phase 1.21 two-binary chain-load exists (`cd/INTRO.BIN`) but disconnected from main flow |
| 4. Title scene | `Objects/Title/TitleSetup.c` (cached) | Active. Phase 1.40 deviation: backdrop suppressed |
| 5. Menu select (Mania Mode / Encore Mode / Time Attack / Competition / Options / Extras) | `Objects/Menu/MenuSetup.c` + `MenuParam.c` + `UIControl.c` + dozens of UI* objects | Missing entirely (Phase 3.2 reverted) |
| 6. Save-file select | `Objects/Menu/UISaveSlot.c` | Missing entirely |
| 7. ManiaModeMenu confirm → ZoneSelect (or direct GHZ on new-game) | `Objects/Menu/UIZoneModule.c` + `UIZoneButton.c` | Missing entirely |
| 8. TitleCard splash ("GREEN HILL ZONE / ACT 1") | `Objects/Global/TitleCard.c` (pull) | Missing entirely |
| 9. GHZ Act 1 gameplay | `Objects/GHZ/*.c` + `Objects/Global/Player.c` | Partial. Spawn at world col 0 + autorun=true (Saturn-fit; see §3) |
| 10. Signpost end-of-act | `Objects/Global/SignPost.c` (cached, partial port) | Partial port, no end-of-act tally trigger |
| 11. Act tally | `Objects/Global/ActClear.c` (pull) | Missing entirely |
| 12. GHZ Act 2 → … | `extracted/Data/Stages/GHZ/Scene2.bin` | Stage data exists; engine doesn't transition |
| 13. GHZ→CPZ cutscene | `extracted/Data/Stages/GHZCutscene/Scene1.bin` | Stage data exists; engine doesn't transition |

**Conclusion:** Steps 1-3 + 5-8 + 11-13 are entirely missing. Step 4 has a
user-explicit deviation. Step 9 has Saturn-fit deviations needing approval.
Steps 9-10 are partial. So the "Mania Mode flow" from boot to end-of-Act-1
is approximately 30% complete in functional terms, much less in parity
terms.

---

## §2 — Per-subsystem gap analysis (decomp file:line cites required)

### 2.A — Boot flow (Sega + RSDK + Mania intro)

**Authoritative sources:**
- `tools/_decomp_raw/SonicMania_Objects_Logos_SegaSplash.c` — pull
- `tools/_decomp_raw/SonicMania_Objects_Logos_RSDKLogo.c` — pull
- `extracted/Data/Sprites/Logos/Sega.bin` + `RSDK.bin` (sprites)
- `extracted/Data/Stages/Logos/Scene1.bin` (Sega), `Scene2.bin` (RSDK), `Scene3.bin` (Mania)
- Cinepak: `cd/INTRO.BIN` from Phase 1.21 + RSDK Video API; PC source = `Data/Videos/Mania.bik`

**Current state:** 0% — `LogoSetup` port abandoned mid-Phase 3.1, MenuSetup reverted Phase 3.2.

### 2.B — Title scene

**Authoritative sources:**
- `tools/_decomp_raw/SonicMania_Objects_Title_TitleSetup.c` (cached) — state machine
- `tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.c` (cached) — finger wave
- `tools/_decomp_raw/SonicMania_Objects_Title_TitleLogo.c` (cached) — entrance + slide
- `tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c` (cached) — Mountain1/2, Reflection, WaterSparkle, WingShine
- `tools/_decomp_raw/SonicMania_Objects_Title_Title3DSprite.c` (cached) — Mania island billboards
- `extracted/Data/Stages/Title/Scene1.bin` — entity coords
- `extracted/Data/Sprites/Title/*.bin` — animation scripts

**Current state:** ~70% visible chrome. Title scene state machine ports + TitleSonic + TitleLogo + Electricity ring done. Backdrop SUPPRESSED per Phase 1.40 (user override).

### 2.C — Menu flow

**Authoritative sources:**
- `tools/_decomp_raw/SonicMania_Objects_Menu_MenuSetup.c` — pull
- `Objects/Menu/UIControl.c`, `UIButton.c`, `UIButtonPrompt.c`, `UIChoice.c`, `UISaveSlot.c`, `UIZoneModule.c`, `UIZoneButton.c`, `UIWidgets.c` — pull
- `extracted/Data/Stages/Menu/Scene1.bin` + `Scene2.bin`
- `extracted/Data/Sprites/UI/*.bin`

**Current state:** 0% — Phase 3.2 MenuSetup port reverted.

### 2.D — GHZ Act 1 gameplay

**Authoritative sources:**
- `tools/_decomp_raw/SonicMania_Objects_GHZ_GHZSetup.c` (cached, partial port)
- `tools/_decomp_raw/SonicMania_Objects_Global_Player.c` (cached) — full Player state machine
- `tools/_decomp_raw/SonicMania_Objects_Global_TitleCard.c` — pull (act-start splash)
- `tools/_decomp_raw/SonicMania_Objects_Global_ActClear.c` — pull (end-of-act tally)
- `extracted/Data/Stages/GHZ/Scene1.bin` — Mania Mode Act 1 entity table
- `extracted/Data/Stages/GHZ/Scene2.bin` — Mania Mode Act 2 entity table
- `extracted/Data/Stages/GHZ/StageConfig.bin` — palette + per-stage SFX/music config

**Current state — GHZ rendering:** ~80% NBG1 + NBG2 + Player visible
**Current state — GHZ entities ported:** ~5 of ~25+ unique types

### 2.E — Per-entity GHZ Act 1 decomp ports

Pull/cache + port for each (gap table at `docs/ghz_act1_entity_gap.md`):

| Entity | Decomp file | Scene1.bin count | Port status |
|---|---|---|---|
| Ring | `Objects/Global/Ring.c` (cached) | 446 | Partial: 1 frame, no spin anim |
| ItemBox | `Objects/Global/ItemBox.c` (cached) | 38 | Partial: 1 frame, no anim |
| Spring | `Objects/Global/Spring.c` (cached) | 35 | Partial: 1 frame |
| Motobug | `Objects/GHZ/Motobug.c` (cached) | 9 | Partial: hitbox cited, missing smoke trail + turn anim |
| SignPost | `Objects/Global/SignPost.c` (cached) | 2 | Partial: 1 frame, no spin |
| Spikes | `Objects/Global/Spikes.c` (Phase 2.4c) | 41 | Frame 0 only |
| BuzzBomber | `Objects/GHZ/BuzzBomber.c` (Phase 2.4c) | 18 | Frame 0 only, no projectile |
| Platform | `Objects/Common/Platform.c` — pull | 59 | Not ported |
| SpikeLog | `Objects/GHZ/SpikeLog.c` — pull | 61 | Not ported |
| BreakableWall | `Objects/Global/BreakableWall.c` — pull | 23 | Not ported |
| BoundsMarker | `Objects/Global/BoundsMarker.c` — pull | 22 | Not ported |
| Newtron | `Objects/GHZ/Newtron.c` — pull | 21 | Not ported |
| Decoration | `Objects/Common/Decoration.c` — pull | 21 | Not ported |
| Bridge | `Objects/GHZ/Bridge.c` — pull | 13 | Not ported |
| Chopper | `Objects/GHZ/Chopper.c` — pull | 13 | Not ported |
| CollapsingPlatform | `Objects/Common/CollapsingPlatform.c` — pull | 15 | Not ported |
| Crabmeat | `Objects/GHZ/Crabmeat.c` — pull | 11 | Not ported |
| BatBrain | `Objects/GHZ/Batbrain.c` — pull | 7 | Not ported |
| Water | `Objects/Common/Water.c` — pull | 6 | Phase Z (palette FX) |
| StarPost | `Objects/Global/StarPost.c` — pull | 4 | Not ported (checkpoint) |
| Music | `Objects/Global/Music.c` (cached) | 4 | Partial: SFX list cited in Phase 2.4a |
| TitleCard | `Objects/Global/TitleCard.c` — pull | 1 | Not ported (act-start splash) |
| DDWrecker (boss) | `Objects/GHZ/DDWrecker.c` — pull | 1 | Phase 2.6 boss |
| Other (~10 minor) | various | low | Not surveyed |

### 2.F — Player physics + collision

**Decomp source:** `tools/_decomp_raw/SonicMania_Objects_Global_Player.c` (cached, 5000+ lines)

**Current state:** Phase 2.2 ported state machine + jump/walk subset. Phase 2.4b COMPLETED 2026-05-28 (Task #139): `Player_CheckCollisionBox` ported per decomp `Player.c:2267-2341` + `Collision.cpp:276-487 setValues=true` at `src/mania/Objects/Global/Player.c`. Three Entities.c call sites wired (itembox, spring, motobug). The "jump in/out of objects" symptom — caused by ad-hoc AABB hit-tests with no position snap-back — is resolved. RED-firing gate `tools/qa_phase2_4b_collision_gate.py` (P1-P4) GREEN on post-fix build. Memory entry: `player-checkcollisionbox-required-for-every-object.md`.

**Missing actions:** spindash, peelout, drop dash, push (against wall), grab (ledge), hurt-bounce, death-fall, breathe-underwater, super-transform.

**Missing power-up shields:** standard, fire, water, lightning, invincibility, speed shoes — Phase 2.5.

### 2.G — Animation completeness

**Decomp source:** `extracted/Data/Sprites/<Cat>/<Name>.bin` for each Anim.

**Current state — TitleSonic:** ~8 keyframes shipped (Phase 1.23). Decomp has 49 body frames + 12 finger frames.
**Current state — GHZ Player:** idle 1 frame, walk subset. Decomp Player.bin: idle 6, bored 2x sequences, walk 8, jog 8, run 8, dash 4, skid 4, roll 4, spindash 6, push 4 — 50+ frames across 16+ animations.
**Current state — Ring:** 1 frame. Decomp: 8-frame spin cycle.
**Current state — Other entities:** see §2.E.

This is per-entity work that maps 1:1 with §2.E ports.

### 2.H — Audio

**Decomp source:**
- `tools/_decomp_raw/SonicMania_Objects_Global_Music.c` (cached) — track switching
- Per-Object `RSDK.PlaySfx` cites in Player/Ring/ItemBox/Spring/SignPost/etc.
- `extracted/Data/Music/` (OGG → CD-DA conversion)
- `extracted/Data/SoundFX/` (WAV → PCM)

**Current state — BGM:** Phase 2.4a fixed — track 2 plays in GHZ, track 3 plays in title. ✓
**Current state — SFX:** Phase 2.4a fixed sample_rate + wired jump SFX. 5 SFX in pipeline (Ring/Stomp/ItemBox/Spring/SignPost/Jump). Decomp has ~30+ SFX in Mania Mode GHZ Act 1 (death, hurt, monitor-power-up, ring-loss, drown, breathe, spindash-charge, spindash-release, skid, roll, peelout-charge, peelout-release, drop-dash-charge, drop-dash-release, etc.).

### 2.I — HUD

**Decomp source:** `Objects/Global/HUD.c` — pull

**Current state:** Partial. Score/time/rings digits visible in Phase 2.4l captures (1100 frames shown). Needs decomp audit for layout, font, life icon, ring icon, blink-on-low-rings.

### 2.J — Transitions + ActClear + tally

**Decomp source:**
- `Objects/Global/SignPost.c` (partial port)
- `Objects/Global/ActClear.c` — pull (rolling tally + score bonus + time bonus)
- `tools/_decomp_raw/SonicMania_Objects_Global_TitleCard.c` — pull (act intro splash)

**Current state:** 0% — Phase 3 deferred from earlier session.

### 2.K — Save / continue / lives / game-over

**Decomp source:** `Objects/Global/SaveGame.c` (cached) + `Objects/Global/GameOver.c` (pull)

**Current state:** Phase A8 save shim landed; per-slot save_slot_t pending (Task #23 in_progress from earlier session).

### 2.L — Scanline / palette / background FX

**Decomp source:** `Objects/GHZ/PaletteFX.c` (if exists in decomp), `Objects/Common/Water.c`, `Objects/GHZ/SunFlower.c`, etc.

**Current state:** 0% — Phase Z (Saturn-native rewrites) per `memory/saturn-native-rewrites-final-phase.md`.

---

## §3 — Saturn-fit deviations requiring user explicit approval

**USER SCOPE DECISION (2026-05-28, binding):** "just keep it all canon from
the second we press any button at the title menu.... anything that already
exists before that is fine".

This means:
- **Pre-button-press chrome (Sega splash, RSDK splash, Mania intro Cinepak,
  title scene itself with current Phase 1.40 backdrop suppression) is
  EXPLICITLY OUT OF SCOPE.** Whatever state we have today is fine.
- **From the button-press onward EVERYTHING must be decomp-canon.** Menu
  flow, save select, zone select (if any), TitleCard, gameplay spawn,
  Player physics, entities, animations, audio, transitions, ActClear,
  next-stage chain.

Apply this to §3.1-§3.6 below:

### §3.1 — Title backdrop suppression (Phase 1.40) — **RESOLVED: KEEP**

Pre-button-press = out of scope per user directive. Phase 1.40 stays.
Sega splash / RSDK splash / intro Cinepak (Tasks #123, #141, #31) likewise
stay deferred indefinitely.

### §3.2 — GHZ spawn at world col 0 + autorun mode — **RESOLVED: FIX**

Post-button-press = in scope. Task #143 ports Scene1.bin Player entity
spawn + drops `g_ghz_autorun` / `g_ghz_input_grace`. RED gate at §5.

### §3.3 — Entity SPR atlases ship frame-0 only — **RESOLVED: FIX**

Post-button-press = in scope. Task #142 anim completeness audit covers
every Object's per-Anim K-frame coverage + per-frame duration table.

### §3.4 — Skip menu, jump straight to GHZ — **RESOLVED: FIX**

Post-button-press = in scope. Phase 3.2 MenuSetup (Task #124) is the
canonical Mania Mode entry path. UIControl + UIButton + UIChoice +
UISaveSlot + UIZoneModule (or direct-to-GHZ on new game) MUST land.

### §3.5 — `g_ghz_load_error_code` BSS instrumentation — **DEFAULT: KEEP**

Near-zero cost (4 bytes + 1 store per failure path). Saturn-fit
instrumentation that mirrors the decomp's runtime error semantics. No
user objection observed; keeping unless flagged.

### §3.6 — `g_ghz_active_tick_counter` BSS gate evidence — **DEFAULT: KEEP**

Same class as §3.5. Keeping unless flagged.

---

## §4 — Sequenced execution plan

Phases ordered by dependency, not by impact. Within a phase, sub-tasks
can be parallel if file boundaries don't overlap.

### Phase A — Honest cleanup + canonical spawn

- **A.1** ✓ DONE 2026-05-28 (Task #139). Phase 2.4b landed Player_CheckCollisionBox. No conflict with Phase 2.4c (2.4c added Spikes/BuzzBomber + Motobug hitbox tokens; 2.4b rewrote the motobug hit-test block to use the new symbol, preserving the MOTOBUG_HITBOX_* token sentinels Phase 2.4c gate depends on).
- **A.2** Task #143: parse Scene1.bin Player entity; replace `Player_Init(..., 0, start_y)` with the canonical coord; drop `g_ghz_autorun` + `g_ghz_input_grace`. RED gate: Player.xpos at TS_GHZ_ACTIVE entry within 16px of Scene1.bin Player entity x.
- **A.3** Task #142 sprite-character animation completeness audit. RED gate: per-Object anim cycle math matches decomp within 5%. **Scope is broad** — recommend separate sub-agent per entity class group (Player, Ring/ItemBox/Spring, Badniks).
- **Budget:** 2-3 sub-agents.

### Phase B — Menu + intro flow

- **B.1** Phase 3.1: LogoSetup (Sega + RSDK splash). Per `tools/_decomp_raw/SonicMania_Objects_Logos_*.c` + `extracted/Data/Stages/Logos/Scene1.bin` + `Scene2.bin`.
- **B.2** Phase 3.2: MenuSetup (Mania Mode menu). Per `Objects/Menu/MenuSetup.c` + `Scene1.bin`/`Scene2.bin`. **HIGH SCOPE** — UIControl + UIButton + UIChoice + UISaveSlot + UIZoneModule — likely needs to be broken into B.2a/B.2b/B.2c sub-tasks per UI primitive.
- **B.3** Phase 1.21: connect intro Cinepak chain-load (already built; needs wiring into post-title flow).
- **B.4** Wire B.1 → B.2 → B.3 → B.4 (Title) flow per decomp `RetroEngine.cpp` scene-load chain.
- **Budget:** 4-6 sub-agents.

### Phase C — GHZ entity completeness

- **C.1** Port remaining priority entities by Scene1.bin instance count (Platform 59, SpikeLog 61, Newtron 21, BreakableWall 23, BoundsMarker 22, Decoration 21, Bridge 13, Chopper 13, CollapsingPlatform 15, Crabmeat 11, BatBrain 7). Each is a mechanical decomp port + asset extraction + animation completeness.
- **C.2** Upgrade existing partial ports to full anim cycles (Ring spin, ItemBox idle + break, Spring compress + bounce, Motobug walk + smoke + turn, SignPost spin sequence).
- **C.3** TitleCard splash per `Objects/Global/TitleCard.c`. ActClear tally per `Objects/Global/ActClear.c`.
- **Budget:** 5-8 sub-agents (parallelizable by entity).

### Phase D — Player completeness

- **D.1** Spindash, peelout, drop dash actions per `Objects/Global/Player.c` Player_Action_*.
- **D.2** Push, grab-ledge, hurt-bounce, death-fall animations + state machine.
- **D.3** Power-up shields (Phase 2.5).
- **Budget:** 3-4 sub-agents.

### Phase E — Transitions + ActClear

- **E.1** SignPost end-of-act trigger → TitleCard out → ActClear tally.
- **E.2** Stage-transition: GHZ Act 1 → Act 2 → GHZCutscene → CPZ Act 1.
- **Budget:** 2-3 sub-agents.

### Phase F — Audio completeness

- **F.1** SFX coverage audit: every Player_Action_* SFX, every UI menu SFX, every entity interaction SFX.
- **F.2** BGM transitions (act-clear jingle, hurt jingle, invincibility jingle, drowning jingle, etc.).
- **Budget:** 2 sub-agents.

### Phase G — HUD + UI polish

- **G.1** Full HUD port per `Objects/Global/HUD.c`.
- **G.2** UIButton/UIChoice/UISaveSlot completeness for menu (chained from Phase B).
- **Budget:** 2-3 sub-agents.

### Phase H — Multi-zone expansion

Subsequent zones (CPZ, SPZ, FBZ, ...) repeat Phase C+D+E patterns per-zone.

### Phase Z — Saturn-native rewrites

Scanline FX, palette cycling, water reflection, etc. — already documented as
final-phase per `memory/saturn-native-rewrites-final-phase.md`.

---

## §5 — RED-firing gate definitions

Each phase's acceptance requires gates BEFORE the fix. Examples:

| Phase | RED-fire predicate | GREEN target |
|---|---|---|
| A.2 spawn | Player.xpos != Scene1.bin Player entity x | within 16 px |
| A.3 anim | Saturn anim cycle ticks != decomp anim cycle ticks | within 5% |
| B.1 Sega splash | Scene1.bin Logos parse + visible Sega logo at correct coord | pixel-mass + position match |
| B.2 menu | UIControl entity registered + menu items visible + B-button navigation | savestate evidence + visual capture |
| C.* entity | each Object's sprite visible at Scene1.bin coord + anim cycling | per-entity pixel-mass + cadence |
| D.1 spindash | Player.state == Action_Spindash on charge | savestate Player.state peek |
| F.1 SFX | every Player_Action SFX heard during gameplay | wav-capture RMS check |

Per-gate gates live at `tools/qa_phase<N><alpha>_<name>_gate.py` per existing
convention.

---

## §6 — Sub-agent budget summary

| Phase | Sub-agents | Rough wall-clock |
|---|---|---|
| A | 2-3 | 1 day |
| B | 4-6 | 2-3 days |
| C | 5-8 | 3-4 days |
| D | 3-4 | 2 days |
| E | 2-3 | 1 day |
| F | 2 | 1 day |
| G | 2-3 | 1 day |
| H | 5+ per zone × 11 remaining zones | 4-8 weeks |
| Z | 3-5 | 2-3 weeks |

**Mania-Mode-GHZ-Act-1-only target (Phases A through F + relevant entities):**
**~18-25 sub-agents over ~7-10 days of focused work.**

**Full game (Phases A through Z):**
**~80-120 sub-agents over ~3-6 months.**

This is the actual scope. The previous celebratory framings ("Phase 2.3l
shipped GHZ Act 1!") were premature — they were milestones in the rendering
chain, not parity milestones.

---

## §7 — Decisions status

1. ~~§3.1 Phase 1.40 title backdrop~~ — RESOLVED: keep (pre-button)
2. ~~§3.4 menu flow~~ — RESOLVED: MenuSetup required (canonical post-button)
3. ~~§3.5/§3.6 BSS instrumentation~~ — RESOLVED default: keep
4. **Phase H scope (all 13 zones canonical, or GHZ-only?)** — open. Default
   assumption per "EXACTLY A REPLICA": all zones, but that's ~80-120
   sub-agents over months. Will dispatch GHZ Act 1 first; flag at GHZ end-
   of-act for explicit scope confirmation before CPZ.
5. **Sub-agent parallelism cap** — open. Defaulting to max 3 in-flight
   (the level we've been operating at). Adjust if user wants
   different.

## §7.1 — Next dispatch sequence (in order)

Phase 2.4b returned 2026-05-28 (Task #139, gate GREEN). Next:

| # | Task | Scope | Type |
|---|---|---|---|
| 1 | #143 Phase 2.4f canonical spawn + drop autorun | small, well-bounded | ✓ DONE 2026-05-28 (Approach B build-time extract; cd/GHZ1SPWN.BIN=(108,947); autorun + input_grace dropped; gate `qa_phase2_4f_canonical_spawn_gate.py` P1-P4 GREEN; visual confirms Sonic stationary at canonical spawn) |
| 2 | #142 Phase 2.4e anim completeness for GHZ entities | medium | sub-agent: audit + extend `build_phase2_4c_entities.py` and equivalents to ship ALL frames per anim |
| 3 | #124 Phase 3.2 MenuSetup port | LARGE — needs decomposition | planning sub-agent first (break MenuSetup into UIControl + UIButton + UIChoice + UISaveSlot + UIZoneModule sub-tasks with decomp file:line + asset paths) |
| 4 | Remaining priority GHZ entities per `docs/ghz_act1_entity_gap.md` | Platform 59, SpikeLog 61, Newtron 21, BreakableWall 23, BoundsMarker 22, Decoration 21, Bridge 13, Chopper 13, CollapsingPlatform 15, Crabmeat 11, BatBrain 7 | sub-agents per entity group |
| 5 | TitleCard splash + ActClear tally | medium | sub-agent |
| 6 | Player spindash / peelout / drop dash + power-up shields | medium | sub-agent |
| 7 | HUD per `Objects/Global/HUD.c` | small-medium | sub-agent |
| 8 | Audio SFX coverage audit (full Player_Action SFX + jingles) | medium | sub-agent |

Steps 1-3 must complete before step 4+ for sequence integrity (canonical
post-button-press flow lands first; then we build out entities on top of
correctly-running Mania Mode gameplay).

---

## §8 — In-flight work to wait for

- **Phase 2.4b** ✓ DONE 2026-05-28 (Task #139). Player_CheckCollisionBox ported, 3 Entities.c call sites wired, gate `tools/qa_phase2_4b_collision_gate.py` GREEN, memory entry `player-checkcollisionbox-required-for-every-object.md` indexed.

---

## §9 — Comprehensive port-developer audit (categories I missed)

User direction 2026-05-28: "think comprehensively like a developer who is porting a game from one system to another platform — think of anything else you may have missed". This section catalogues categories not explicitly covered in §1-§8. Each is tagged with a §4 phase or new phase letter.

### 9.A — Input system completeness

**Decomp:** `rsdkv5-src/RSDKv5/RSDK/Input/Input.cpp` + per-Object `RSDK.GetControllerInfo` calls.
- Saturn 3-button (A/B/C/Start) vs 6-button (+ X/Y/Z/L/R) pad detection
- 3D Analog Control Pad (analog stick → directional input + L/R triggers)
- Multi-controller (decomp supports 4; Saturn = 2 native + 6 via multitap)
- Button-EDGE vs button-HELD semantics for menu navigation
- ControllerState API surface (Phase A7 partial port — needs review)
- "Press any button" semantics (any button on any pad, not just Start)
- Pause toggle (Start mid-game), restart-from-pause
- Key remap UI (Options menu)
- DEFAULT: 3-button standard pad mapping; 6-button optional; no analog.

**Phase tag:** A.4 (precedes Phase B menu).

### 9.B — Saturn memory + resource budget per stage

**Authorities:** ST-013-R3 (VDP1), ST-058-R2 (VDP2), ST-077-R2 (SCSP), ST-097-R5 (SCU/LWRAM)
- VDP1 VRAM 512 KB — sprite cells + sprite cmd tables
- VDP1 framebuffer 256 KB × 2 (double-buffered)
- VDP2 VRAM 512 KB — NBG/RBG cell + pattern + map; 4 banks A0/A1/B0/B1 of 128 KB each
- VDP2 CRAM 4 KB = 2048×16-bit OR 1024×32-bit entries
- Sound RAM 512 KB — SCSP PCM samples + sequencer
- Work RAM-H 1 MB — BSS + heap + SGL work area (0x060C0000+ reserved)
- Work RAM-L 1 MB — currently 480 KB used (Phase 2.3l); 544 KB free
- Backup RAM 64 KB (8 KB usable per Saturn unit for game saves)
- Cartridge RAM optional (1 MB / 4 MB Action Replay expansion)

**Per-stage budget table needed:** 26 acts (13 zones × 2) + bosses + cutscenes + special stages. For each, catalogue: resident bytes per VRAM type, streaming bytes, CRAM slots, Sound RAM PCM bank, BSS entity-state arrays.

**Currently:** only GHZ Act 1 reactively budgeted. Need `tools/audit_stage_budget.py` to run proactively per-stage.

**Phase tag:** A.5 (new tool) + ongoing per-stage application.

### 9.C — CD-ROM file system + streaming

**Authorities:** ST-136-R2 (GFS), `tools/build_filelist.py`, Phase 2.2b raw-slCdRead precedent
- Every shipped asset MUST be in IP.BIN file table OR loaded via raw `slCdRead` (Phase 2.2b/2.2c/2.3l precedent for assets jo's GFS can't size correctly)
- GFS_Seek ~100ms cost → boot-path budget <5s per §4.5.1 Audit 4
- Per-zone asset hot-swap during transitions (load CPZ residents while GHZ act-clear tally plays)
- Asset interleaving on disk for CD seek minimization (group per-stage residents physically nearby)
- ISO 9660 file table size limit; Joliet extensions for long names not supported by jo's GFS by default
- CD seek time on real Saturn ≠ Mednafen 1× emulation — test on hw near end

**Phase tag:** B.4 (asset hot-swap) + R (release packaging optimization).

### 9.D — Audio completeness beyond per-action SFX

Beyond Phase 2.4a baseline:
- BGM jingles: 1-up, invincibility, drowning-warning, drowning-beat (5-4-3-2-1), hurry-up, act-clear, life-bonus, time-bonus, game-over, continue, super-transform
- Stage-specific BGM (~30 zone BGMs + bosses + special stages + menus + credits = ~40+ tracks)
- BGM cross-fade for boss intros (decomp `Music_FadeOut` + `Music_PlayTrack`)
- Stinger SFX layering (act-clear stinger over BGM)
- 3D positional SFX (decomp pans some SFX by screen-X)
- Voice clips ("Heyyy!" taunt — verify if base Mania or Plus only)

**Saturn-side challenges:**
- 99 CD-DA track max on a data CD
- PCM Sound RAM budget per active SFX set
- BGM cross-fade needs pre-mixed stingers OR Saturn-side dual-track playback (not stock CD-DA)

**Phase tag:** F.1 (existing audit) + F.2 (jingles + transitions).

### 9.E — Scene-transition + cutscene system

Beyond SignPost → ActClear:
- Pre-act TitleCard slide-in (zone name + act number + character portrait)
- Pre-boss act-transition (camera pan, BGM cross-fade, player auto-stop at arena)
- Post-boss act-transition (boss explosion, capsule break, victory pose, animals released)
- Inter-zone cutscene (GHZ→CPZ has Eggman-rocket cinematic per `extracted/Data/Stages/GHZCutscene/Scene1.bin`)
- Mid-act plot cutscenes (some zones have story beats)
- End-credits scrolling text + character portraits + ending art
- True-ending unlock (all 7 Emeralds → special boss → true credits)

**Decomp source:** Per-cutscene scene/object pair, e.g. `Objects/GHZ/HotaruHiWatt.c` per zone.

**Phase tag:** E.1 (TitleCard + ActClear, existing) + E.3 (inter-zone cutscenes) + R.3 (end credits).

### 9.F — Save / persistence completeness

Beyond Task #23 backup save_slot_t:
- Per-zone progression flags (act-1-complete, act-2-complete, special-stage-N-collected)
- Emerald collection state (7 Emeralds)
- Time Attack records per act per character
- Save-data CRC/checksum on load
- Save-failure UI ("backup not detected" / "out of space")
- Multiple save slots (3 in Mania PC; Saturn budget = 3 × ~2-2.5 KB)
- Option settings persistence (BGM volume, SFX volume, controls)
- Region/language preference persistence

**Phase tag:** Task #23 expanded; new save-format spec doc needed.

### 9.G — Special game modes + bonus stages

Beyond Mania Mode main story:
- Time Attack (single-act time trial + leaderboard)
- Competition Mode (2-player split-screen race — Saturn split-screen feasibility study needed)
- Mean Bean Machine (Puyo Puyo minigame in CPZ Act 2)
- Special Stage (UFO chase — collect blue spheres for Chaos Emerald)
- Pinball / Slots bonus stages (verify: base Mania or Plus-only?)
- Stage Select debug menu
- Sound Test
- Options

**Plus-exclusive (DEFAULT OUT OF SCOPE unless user confirms):**
- Encore Mode (alt pink palette + character swap)
- Mighty + Ray player characters
- Bonus stage UFO variants

**Phase tag:** I (post-main-story modes).

### 9.H — Tile / level mechanics depth

Beyond simple tile collision:
- TileConfig.bin per-tile slope-angle byte (decomp `TileLayer.cpp` slope physics)
- Multiple TileLayers per stage (bg-decoration + fg-collision + path-1 + path-2)
- DrawAniTiles for animated tiles (sun flowers, flower stems, water rims)
- Loop physics (player runs full 360° loop with z-order swap mid-loop)
- Conveyor belts (per-tile force vector)
- Water (palette swap below water-line + reduced physics + breath countdown)
- Crusher floors (tile region collapses)
- Slope-acceleration physics (downhill speed-up)
- Push-block tiles
- Spike/instant-death tiles

**Decomp source:** `rsdkv5-src/RSDKv5/RSDK/Scene/TileLayer.cpp` + per-stage AnimTiles + per-zone setup objects.

**Phase tag:** C.4 (after entity ports) + Phase Z (palette FX subset).

### 9.I — Camera system completeness

**Decomp source:** `Objects/Global/Camera.c`
- X/Y follow with screen-relative offset (`Camera->offset`)
- Lookup (up-held: camera pans down)
- Lookdown (down-held: camera pans up)
- Boundary clamping (BoundsMarker entities define limits)
- Cutscene camera (independent of player position)
- Shake X/Y (per-source decomp `Camera_ShakeX/Y`)
- Multi-player camera (Tails follow, Knuckles split-screen)
- Boss-arena lock (camera stops at arena edges)
- Death camera (continues following Sonic into pit)
- Goal-sign camera (follows past goal post on act-clear)

**Phase tag:** C.4 + D (player abilities).

### 9.J — Player abilities expansion

Beyond Phase D actions in §4:
- Sonic actions per decomp `Player.c::Player_Action_*`:
  - Action_Jump (basic)
  - Action_Spindash (down-button charge + release)
  - Action_Peelout (up-button charge + release)
  - Action_DropDash (jump + hold-jump-mid-air)
  - Action_GroundRoll
  - Action_AirRoll
  - Action_Super (all 7 Emeralds + 50 rings → invincible + speed-boost)
- Damage system: ring loss, invuln frames, knockback direction
- Power-up shields: none / fire (lava immune + dash) / water (water immune + bounce) / lightning (auto-ring-collect + double-jump)
- Invincibility: BGM swap + sparkle particles + immune
- Speed shoes: 2× max speed + BGM speedup
- 1-up: lives +1 + jingle freeze
- Death sequence: fall-off-screen-bottom, lives -1, respawn at last checkpoint
- Game over: zero lives → "GAME OVER" + countdown to title

**Decomp source:** `Objects/Global/Player.c` (8000+ lines) + `Objects/Global/Shield.c` + per-shield sub-class.

**Phase tag:** D.1-D.3 (drafted) + D.4 power-ups + D.5 super.

### 9.K — All-zone bosses

Per-zone act-1 mini-boss + act-2 main boss (or single boss per act).
- GHZ Act 1: minor mini-boss; Act 2: Death Egg Robot
- CPZ Act 1: mini-boss; Act 2: main boss
- ... (13 zones × 2 acts ≈ 22-26 bosses total)
- ERZ Act 2: Phantom King + Phantom Rider + Phantom Eggman + final boss

Each boss = decomp `.c` port: state machine + projectile spawn + hitbox sequence + damage states + defeat animation.

**Phase tag:** new Phase L (per-zone boss work) parallel with §4 Phase H zone work.

### 9.L — Visual effects (most Phase Z)

- Palette rotation per-tile-layer (GHZ flower stems, MMZ machinery)
- Scanline effects (water-line refraction, heat haze, RBG0 distortion)
- Sprite trails (Super Sonic, spindash dust)
- Particle systems (explosion, dust puffs, sparkles, smoke)
- Mode-7-ish background warping (SSZ tornado, special stages)
- Lens flare (TimeTravel)
- INK_BLEND / INK_ADD additive (Saturn = half-transparency + color calc per ST-058-R2)
- White-flash hit effect
- Death-fade-to-black
- Stage transition wipes (iris, slide, dissolve)
- Multi-line parallax (cloud layers at different per-line speeds)

**Phase tag:** Z (Saturn-native rewrites).

### 9.M — Performance budget enforcement

60 FPS NTSC = 16.67 ms/frame. Rough per-frame split (target):
- Input read: <1 ms
- Entity tick (50-200 entities): 4-6 ms
- Collision (tile + per-object): 2-3 ms
- Camera + cutscene: <1 ms
- Render submit (VDP1 cmd list + VDP2 reg + DMA): 3-5 ms
- Audio update: 1-2 ms
- Vblank wait: 1-2 ms
- Headroom: 1-2 ms

Slowdown handling: decomp Mania PC drops to 30 fps on heavy zones (FBZ ash). Saturn matches "best-effort frame" model. Need per-subsystem profile harness (`slGetSystemTime` deltas around tick blocks).

**Phase tag:** new Phase M (perf audit) after entities + bosses land.

### 9.N — Region / localization + IP.BIN

- NTSC (60 Hz, 224 lines) vs PAL (50 Hz, 240 lines) — frame rate + render area both differ
- IP.BIN per-region: NTSC-US, NTSC-J, PAL
- Game text English-only initial; Japanese stretch goal
- Title screen art per region (decomp Mania has US/JP variants)

**Phase tag:** new Phase N (region) at release polish stage.

### 9.O — Pause + restart + game-over flow

- Pause overlay (dim screen + menu: resume / restart / exit-to-title / options)
- Restart (re-load current act from beginning)
- Exit-to-title (tear down stage, re-load title scene)
- Game-over (zero lives → "GAME OVER" + jingle + countdown → title)
- Continue (where supported — Time Attack continues prompt before game-over)
- 1-up jingle (freezes BGM, plays jingle, resumes — needs music state machine)
- Save-and-quit (only at checkpoints — saves progression)

**Phase tag:** new Phase P (post-button flow polish) parallel with Phase E.

### 9.P — Build / packaging / distribution

- Final ISO budget: Saturn CD = 681 MB; Mania PC data ~250 MB base + Plus content
- Asset interleaving for CD seek minimization (group per-stage residents)
- Backup-RAM save format
- Real-hardware test (Saturn model 1/2/3 — different BIOS + cache behavior)
- Action Replay / Pro Action Replay loader compat
- CD-R burn for real-hw testing
- Multi-region IP.BIN customization

**Phase tag:** new Phase R (release packaging) final pre-ship.

### 9.Q — Decomp Plus vs base Mania disambiguation

- Decomp at GitHub `RSDKModding/Sonic-Mania-Decompilation` is from Mania **Plus** (later revision)
- Per-file `#ifdef PLUS` blocks distinguish base from Plus
- DEFAULT: base Mania only (matches user's "Sonic Mania" reference)
- Per-port audit must check `#ifdef PLUS` — skip if Plus-exclusive

**Phase tag:** ongoing per-port discipline.

### 9.R — End-of-game + credits

- ERZ Phantom sequence (King → Rider → Eggman → final boss)
- Good ending (all Emeralds → special final boss + true cutscene + true credits)
- Bad ending (no Emeralds → standard final boss + credits-only)
- Credits roll (scrolling text + character portraits + final art reveal)
- "Press start to retry" on game-over OR ending → title

**Phase tag:** Phase R.3 (end credits) at very end.

### 9.S — Dev-tooling that needs to be built

- `tools/audit_stage_budget.py` — per-stage VRAM/CRAM/Sound-RAM/LWRAM budget audit
- `tools/extract_entity_anim_frames.py` — for every Object's referenced `.bin`, dump all frames + duration table (kills the frame-0-only-atlas violation)
- `tools/build_bgm_track_table.py` — emit cd_audio CUE for all ~40 BGM tracks
- `tools/perf_profile_capture.py` — runtime per-subsystem tick profiler
- `tools/qa_zone_smoke_test.ps1` — boot to each zone via debug stage-select, capture 5 frames, gate
- `tools/release_iso_assembler.py` — final ISO build with optimized file ordering

**Phase tag:** these tools land alongside the phases that need them.

### 9.T — Risk register (Saturn-specific blockers I anticipate)

- **VDP1 sortlist overflow** when many entities on-screen (Phase 2.3e precedent) — needs per-frame entity culling beyond visibility test
- **VDP2 VRAM bank exhaustion** when stages use 2+ NBGs with large tilemaps + sky + clouds — may need per-stage NBG demotion (RBG → NBG cell)
- **CRAM 256-entry limit shared across all NBGs + VDP1** — must per-stage budget palette slots; some zones may need palette swapping mid-stage
- **Sound RAM 512 KB ceiling** — full Player SFX set + zone ambient + boss SFX may exceed; needs per-stage SFX bank rotation
- **Backup RAM 8 KB save budget** for 3 slots × emeralds + flags + records + options — needs compact serialization
- **GFS file table 256-entry limit** per IP.BIN (per memory rule) — Mania has thousands of assets; need per-stage subdir + raw `slCdRead` for overflow assets
- **SH-2 28.6 MHz dual-CPU coordination** — slave SH-2 currently underused; entity tick parallelization possible but adds bug surface
- **Decomp uses C++ classes / templates** — every port must be hand-translated to C (no auto-mangling); Phase A engine port is the foundation
- **Saturn split-screen feasibility for 2P Competition** — VDP2 vertical scroll-mode coexistence with line-scroll = unknown; may need to defer/cut

**Phase tag:** ongoing risk review at end of each phase.

---

## §10 — Realistic scope acknowledgement

A full canonical Sonic Mania → Saturn port is a **multi-year engineering effort**. Revised budget:

| Scope tier | Sub-agents | Wall-clock |
|---|---|---|
| GHZ Act 1 canonical (button-press to act-clear) | 25-35 | 2-3 focused weeks |
| GHZ both acts + boss + cutscene | 35-45 | 3-4 weeks |
| First 3 zones (GHZ + CPZ + SPZ) + transitions | 70-90 | 2-3 months |
| All 13 zones + bosses + cutscenes (no special stages, no Plus) | 150-200 | 8-12 months |
| Full Mania including special stages + bonus games | 200-250 | 12-18 months |
| Mania Plus parity (Encore + Mighty + Ray + bonus) | +50-80 | +3-4 months |

These are honest professional-port-team estimates. The user's binding "EXACTLY A REPLICA" mandate implies the top tier eventually; the practical sequencing is bottom-up: ship GHZ Act 1 canonical first, validate frame-for-frame against PC reference, then sequence the rest.

---

## §11 — Decision answers (locked 2026-05-28)

All 12 questions resolved:

1. ~~Phase 1.40 backdrop~~ — KEEP (pre-button-press scope)
2. ~~Menu flow~~ — REQUIRED (canonical post-button)
3. ~~BSS instrumentation~~ — KEEP
4. **Phase H scope** — **ALL 13 ZONES** (full main game canonical)
5. **Sub-agent parallelism cap** — **3 in-flight**
6. **§9.A input system** — **3-button + 6-button + 3D Analog Control Pad** (all three) + **multitap** if Competition Mode requires it
7. **§9.G Plus content** — **INCLUDE PLUS** (Encore Mode + Mighty + Ray + Plus bonus stages) — full Mania Plus parity
8. **§9.G special modes** — Mania Mode + **Time Attack + Competition (2P split-screen, requires feasibility study) + Mean Bean Machine + Pinball + Special Stage UFO chase**
9. **§9.J shields** — **All four** (none/fire/water/lightning, decomp-canon)
10. **§9.N region** — **NTSC-US only** (no PAL, no NTSC-J localization)
11. **§9.P real-hw testing** — **Saturn hardware available** — Phase R includes real-hardware verification pass
12. **§9.T risk-policy** — **Auto-propose lowest-impact mitigation, then brief-report** — agent picks the smallest-impact mitigation when a Saturn blocker fires (VDP1 sortlist, CRAM exhaustion, Sound RAM, GFS file table, etc.), continues, surfaces a brief report. User pinged ONLY if auto-mitigation fails OR has visible cost.

### Effective scope per these answers

Top-tier per §10 estimate: **~250-330 sub-agents over 12-22 months** for full
Mania + Plus parity on Saturn with real-hardware verification.

The plan from §4 + §9 + §12 holds. The dispatch sequence in §7.1 (Phase A
through GHZ Act 1) is the first deliverable; Phase H per-zone expansion
continues bottom-up after GHZ act-clear.

---

## §12 — Phase letter map (consolidated)

After §9 expansion:

| Phase | Scope | Status |
|---|---|---|
| A.1 | Wait for 2.4b merge + verify | ✓ DONE 2026-05-28 |
| A.2 | Task #143 canonical spawn + drop autorun | NEXT |
| A.3 | Task #142 anim completeness | queued |
| A.4 | Input system 9.A | queued |
| A.5 | Stage budget tool 9.B | queued |
| B.1 | (skipped — pre-button-press) | OUT OF SCOPE per user |
| B.2 | Phase 3.2 MenuSetup decomposition + port | queued (needs planning sub-agent first) |
| B.3 | (skipped — pre-button-press) | OUT OF SCOPE |
| B.4 | CD asset hot-swap framework 9.C | queued |
| C.1-3 | Remaining GHZ entity ports + animation completeness | queued |
| C.4 | Tile mechanics 9.H + Camera 9.I | queued |
| D.1-3 | Player spindash/peelout/drop dash | queued |
| D.4 | Power-up shields 9.J | queued |
| D.5 | Super transform 9.J | queued |
| E.1 | TitleCard + ActClear | queued |
| E.2 | Stage transition GHZ Act 2 + GHZCutscene | queued |
| E.3 | Inter-zone cutscenes | queued (per-zone) |
| F.1 | Full SFX coverage 9.D | queued |
| F.2 | BGM jingles + transitions 9.D | queued |
| G | HUD completeness | queued |
| H | Per-zone expansion (CPZ → ERZ) | post GHZ act-clear |
| I | Time Attack + (deferred modes) | post Phase H |
| L | All-zone bosses 9.K | parallel with Phase H |
| M | Perf budget audit 9.M | post entities + bosses |
| N | Region/localization 9.N | release polish |
| P | Pause + restart + game-over 9.O | parallel with Phase E |
| R.1 | Final ISO assembly 9.P | pre-ship |
| R.2 | Multi-region IP.BIN 9.N | pre-ship |
| R.3 | End credits + true ending 9.R | pre-ship |
| Z | Saturn-native VFX rewrites 9.L | post-main-game |

---

HOLD pending §11 answers. The dispatch sequence in §7.1 holds for the GHZ Act 1 scope; everything past Phase H needs explicit user confirmation on scope.
