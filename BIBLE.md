# Sonic Mania → Sega Saturn — Project Bible

## 2026-07-01 POSITION UPDATE (audit pass; measured, offline) — the P6 engine track supersedes the plans below

This file is kept as the historical phase record. Two of its load-bearing
claims are measured-stale and are corrected here rather than rewritten in
place:

- **§2.1 "We do not port the RSDKv5 C++ engine. It does not fit." is
  FALSIFIED by the live build.** The shipping track (P6.1-P6.8, tasks
  #194-#310) compiles the UNMODIFIED RSDKv5 engine TUs (Reader.cpp,
  Scene.cpp, Storage.cpp, Object.cpp, Collision.cpp, Animation.cpp,
  Audio.cpp, Input.cpp, Link.cpp, Math.cpp, RetroEngine.cpp, Sprite.cpp,
  Text.cpp) with sh-none-elf-gcc-8.2.0 plus VERBATIM decomp object TUs
  straight from `tools/_decomp_raw/`
  (`tools/_portspike/_p6/build_p6scene_objs.sh`). Green Hill Zone Act 1
  runs continuously on it (massport plan §0; pool-streaming steady fps
  56.56 per commit 44ed63d). `docs/WHOLE_GAME_MASSPORT_PLAN.md` §0
  authority note already recorded this supersession (its G2).
- **The Phase A table (A1-A9 "reimplement each subsystem in src/rsdk/")
  and the §3 phase ladder (Phases 0-21, zone_desc_t/ZONES.IDX asset
  model) describe the RETIRED hand-port track.** The forward plan is
  `docs/WHOLE_GAME_MASSPORT_PLAN.md` §8 (execution sequencing) as
  corrected by its §9 and positioned by its §10.
- "Authoritative references" line "`tools/_decomp_raw/` — 87 mirrored
  upstream files" is stale: the cache holds 1174 files (measured
  2026-07-01; 894 batch-fetched per docs/decomp_port_status.md Phase
  3.0-prep++ note).
- Per-file port status now lives in `docs/decomp_port_status.md` (see
  its 2026-07-01 two-track audit note; 45 classes registered in the
  default Green Hill Zone build, 72 across all front-end flavors).

## 2026-05-26 — ARCHITECTURE LOCKED: Phase A (engine) → Phase B (objects) → Phase Z (Saturn-native rewrites)

After both upstream-decomp research agents landed, the port has a new top-down architecture that supersedes all earlier piecemeal plans.

**Authoritative references** (cite by §section when claiming any feature done):
- `docs/rsdkv5_engine_catalog.md` — RSDKv5 engine subsystems + Saturn mapping
- `docs/mania_decomp_catalog.md` — every Mania scene + per-object Create/Update/Draw + physics tables
- `docs/title_ground_truth.md` — Title scene entity coords + verbatim TitleLogo.c Draw()
- `docs/scene_objects.json` — per-stage object lists for all 42 folders
- `tools/_decomp_raw/` — 87 mirrored upstream files for re-verification

### Phase A — RSDKv5 engine on Saturn

Each subsystem is a clean Saturn-C reimplementation with the SAME public API as upstream so per-object ports compose. Translation order = dependency order:

| # | Module | Upstream source | Saturn target | Status |
|---|---|---|---|---|
| A1 | **Storage** (file IO + format parsers) | `RSDK/Storage/{Storage,StorageAPI,Scene,Sprite,Animation,Bytecode}.cpp` | `src/rsdk/storage.c` + `tools/dump_data_rsdk.py` | not started |
| A2 | **Object/Entity** (slot manager, ACTIVE states, Create/Update/Draw loop) | `RSDK/Scene/Object.cpp` | `src/rsdk/object.c` | not started |
| A3 | **Animation** (ProcessAnimation tick, SetSpriteAnimation, loop modes) | `RSDK/Graphics/Animation.cpp` | `src/rsdk/animation.c` | not started |
| A4 | **Drawing** (layer compositor, DrawSprite, DrawTile, FillScreen, SetClipBounds, FLIP_*) | `RSDK/Graphics/Drawing.cpp` | `src/rsdk/drawing.c` over VDP1/VDP2 | partial (hand-rolled NBG + sprite calls) |
| A5 | **Collision** (6-sensor model: 2 floor + 2 ceiling + 2 wall) | `RSDK/Scene/{TileLayer,Collision}.cpp` | `src/rsdk/collision.c` | partial (4-sensor floor only — Gate 8 fails) |
| A6 | **Audio** (BGM stream + 16 SFX channels w/ ducking) | `RSDK/Audio/Audio.cpp` | `src/rsdk/audio.c` over CDDA + SCSP | partial (CDDA wired, channels not RSDK-modeled) |
| A7 | **Input** (button state machine: down/press/release) | `RSDK/Input/Input.cpp` | `src/rsdk/input.c` over jo's pad polling | not started |
| A8 | **Save** (8 slots in 64 KB backup RAM) | `RSDK/User/SaveData.cpp` | `src/rsdk/save.c` (already partially built — `src/save.c`) | partial |
| A9 | **Scene management** (SceneInfo, LoadScene, frame-tick loop) | `RSDK/Scene/Scene.cpp` + `RSDKv5/main.cpp` | `src/rsdk/scene.c` + `jo_main` re-architecture | not started |

**Phase A gate criterion:** `src/rsdk/*.c` exposes the RSDK public API surface (`RSDKFunctionTable`-shaped); a minimal scene-load test (load Title/Scene1.bin → create entities at scene-data positions → run one frame → call DrawSprite per entity) renders the same output as my current hand-rolled `draw_title()`.

### Phase B — Per-object port

Once Phase A's API stubs are in place, per-object ports become mechanical translations from `SonicMania/Objects/<Cat>/<Obj>.c`. The Mania catalog (`docs/mania_decomp_catalog.md` §9) lists ~30 priority objects (Ring, Monitor, Spring, Spikes, Platform, Bridge, Player, badniks, etc.). Each port file is `src/objects/<Obj>.c` with matching Create/StageLoad/Update/Draw + per-state functions.

**Phase B gate criterion (per object):** Saturn behavior matches upstream verified via either (a) a unit test of the state machine driven by canned inputs, or (b) a visual gate in `verify_done.ps1` that captures a known scene moment and diffs against a golden.

### Phase Z — Saturn-native rewrites (end-of-port)

Per `docs/rsdkv5_engine_catalog.md` §12 + memory/`saturn-native-rewrites-final-phase.md`, three subsystems get Saturn-native implementations as final-phase work:

| Z# | Upstream system | Saturn implementation |
|---|---|---|
| Z1 | 3D mesh pipeline (Special Stage only) | VDP1 textured quads via `slPutPolygon` |
| Z2 | Scanline FX (water deformation, heat haze) | VDP2 H-scroll IRQ raster callback |
| Z3 | Palette FX (`SetPaletteFade`/`BlendColors`/`RotatePalette`/`SetTintLookupTable`) | CRAM-write helpers per V-blank |

**Phase Z is not skip-work — it is full feature parity scheduled last.** Earlier phases may ship Saturn placeholders with `// TODO Phase Z:` markers.

### Binding rules (memory files this plan rests on)

- `qa-hard-rule-before-claim-done.md` — `pwsh tools/verify_done.ps1` exit 0 required before any "done" claim
- `qa-iterative-improvement.md` — every user-reported bug adds a new gate
- `whole-game-must-be-decomp-validated.md` — feature must cite §section of the catalog
- `rsdkv5-engine-is-the-engine-reference.md` — engine behavior from RSDKv5 repo, game behavior from Mania repo, no hand-rolling format parsers
- `title-positions-must-come-from-scene-data.md` — parse Scene*.bin entities, never guess coords
- `mania-title-decomp-spec.md` — title-scene state machine + per-anim placement reference
- `saturn-native-rewrites-final-phase.md` — Phase Z deliverables, not skip-work

---

## 2026-05-26 — In-flight bug backlog + immediate next steps (PRE-ARCHITECTURE; see above for new plan)

User-reported defects (NOT YET FIXED — current build fails `verify_done.ps1`
gate 6; do NOT claim "done" on any of these without rerunning the gate):

  1. **Audio inaudible** — `qa_boot.ps1` passes `-sound 0` which mutes
     Mednafen during QA captures (silent SFX + silent CD-DA). Also need
     to verify that booting *with* sound actually produces audible
     output, since the user reports zero audio even outside QA.
  2. **Title screen incomplete** — left 28% of viewport is solid sky-
     blue (caught by new gate 6). Only the "MANIA" sprite shows, not
     the full "SONIC THE HEDGEHOG MANIA" composite. No character run
     cycles (Sonic/Tails/Knux). No animated waves. No twinkles. No
     proper PRESS START prompt graphics — only the placeholder.
  3. **Invisible-wall collision** — on GHZ Act 1, walking into a mound
     hits a false-solid that should be either pass-through-under or
     jump-on-top. `build_collision.py` is marking decoration / path
     tiles as ground when it shouldn't.
  4. **Monitor item-box sprites not on ground** — visually overlap the
     mound geometry instead of sitting flush on grass. Sprite anchor
     mismatch or missing y-offset in GHZBOX.BIN conversion.
  5. **Cinepak intro video stuck** — bisect-2 localised to `cpk_lib.o`
     (CPK_Init() alone breaks boot when LIBCPK.A is linked into jo+SGL).
     Three paths forward documented in `src/intro_video.c` header.

### Execution order (locked, in priority order):

  1. **Fix `qa_boot.ps1 -sound 0`** + add Gate 7 (audio) to
     `verify_done.ps1`. Audio is necessary to QA the rest.
  2. **Fix title screen** — re-export Title scene at correct viewport
     covering full 320 width + extract all 9 `Logo.bin` anims + wire
     character run-cycle overlays. Must clear Gate 6 (<25% single-
     color dominance).
  3. **Fix collision** — re-run build_collision.py with path-flag-aware
     scan; add Gate 8 (autorun progress monotonic) to verify_done.ps1.
  4. **Fix monitor ground-snap** — sample GHZSURF.BIN at monitor x-pos
     and align sprite y; add Gate 9 (sprite-on-ground) to verify_done.
  5. **Cinepak Path A: data-driven dive into LIBRARY/SBL6** on the
     DTS96 CD to find CPK_Init's exact prerequisites and the SBL6
     newer-version of LIBCPK if available.

### Rules locked this session:
  - `memory/qa-hard-rule-before-claim-done.md` — never claim "done"
    without `verify_done.ps1` exit 0
  - `memory/test-iteration-cleanup-discipline.md` — clean diagnostic
    captures at every work-segment exit
  - `tools/verify_done.ps1` gates 1-6 active; 7-9 to be added per
    item-3 below

---



> Authoritative engineering plan for completing the port. Read alongside
> `HANDOFF.md` (which captures the current state). This document is the
> contract: every phase below has a single concrete deliverable, an exit
> criterion, and dependencies on earlier phases. Phases are executed in
> order. No work starts on phase N+1 until phase N's exit criterion is met.
>
> Last revised: 2026-05-25.

---

## 0. Honest Scope Statement

Sonic Mania PC is ~600 MB of game content. The full original ships 12 main
zones × 2 acts + Titanic Monarch (3 acts) + Egg Reverie + 13 boss fights +
36 Blue Spheres special stages + 7 UFO 3D special stages + 2 alternate
characters (Tails, Knuckles) with unique abilities + Encore Mode + Time
Attack + Competition + cutscenes + menus + save data. On modern hardware
this is years of one-engineer work. On a 28.6 MHz dual-SH-2 with 1 MB Work
RAM-H + 512 KB VDP1 VRAM + 512 KB VDP2 VRAM + 4 KB CRAM, it is a multi-year
effort done correctly.

**The bible plans the complete journey and partitions it into independently
shippable milestones**, so that every checkpoint is a playable, demonstrable
artifact rather than a dead-end half-feature. We do not promise to land all
phases in one session; we promise that every phase we do land is correct,
robust, and unblocks the next one.

**All of Sonic Mania is in scope** (revised per user instruction
2026-05-25). The previously-deferred items — UFO 3D special stages,
Encore Mode, Competition split-screen, Mighty + Ray, PSZ pinball
minigames, Time Attack — are added back to the plan as Phases 15–20
(see section 11). They land after the core 14 phases because each one
requires the foundations built in Phases 0–14 (entity system, save
data, character abstraction, special-stage shell, etc.). Sequencing
them after the main game is shipped does not remove them from the
final V1 target; it ensures each is built on top of working
foundations rather than against unfinished scaffolding.

---

## 1. Current Reality Inventory (verified)

### 1.1 Engine state (D:\sonicmaniasaturn\src\)

| Item                              | Status                                          |
|-----------------------------------|-------------------------------------------------|
| Title → titlecard → game flow     | Working, single-shot (no return path)           |
| GHZ Act 1 streaming FG (NBG1)     | Working, full 16384×2048                        |
| GHZ parallax sky (NBG2)           | Working                                         |
| Per-column collision surface      | Working, 16384 columns                          |
| Sonic physics (Q16.16)            | Working, Mania-faithful constants               |
| Autorun + auto-jump AI            | Working (demo mode)                             |
| Rings (446) / Badniks (9) / etc.  | Working, GHZ-only positions                     |
| HUD (rings/score/timer)           | Working                                         |
| SCSP SFX (ring/jump)              | Working, placeholder samples                    |
| CD-DA BGM path                    | Wired (track 02), no source WAV present         |
| Save data / pause / death / hurt  | **NOT IMPLEMENTED**                             |
| Multi-zone loader                 | **NOT IMPLEMENTED** (GHZ filenames hard-coded)  |
| Second/third character            | **NOT IMPLEMENTED**                             |
| Boss fights                       | **NOT IMPLEMENTED**                             |
| Blue Spheres                      | **NOT IMPLEMENTED**                             |
| Menu / save-select / level-select | **NOT IMPLEMENTED**                             |

### 1.2 Asset pack inventory (D:\sonicmaniasaturn\extracted\Data\)

| Folder                  | Status                                              |
|-------------------------|-----------------------------------------------------|
| `Data/Game/GameConfig.bin` | Decrypted (4008 B) — defines stages, objects, SFX |
| `Data/Stages/<42 folders>` | All present (Scene*.bin + StageConfig + tiles)    |
| `Data/Sprites/GHZ/Objects.gif`            | Present                              |
| `Data/Sprites/Global/Items.gif + Ring.bin`| Present                              |
| `Data/Sprites/Players/Sonic1..3.gif + Sonic.bin` | Present                       |
| `Data/Music/*`          | **MISSING — encrypted, paths not in filelist**      |
| `Data/SoundFX/*`        | **MISSING — encrypted, paths not in filelist**      |
| Per-class sprites for non-GHZ zones (Players2/3, AIZ, CPZ, …) | **MISSING — same reason** |

**61 unnamed encrypted blobs** remain in `extracted/_unnamed/`, totalling
**~73 MB**. The 60 blobs over 100 KB are almost certainly the music tracks
(typical Mania OGG sizes range 1–4 MB each, and Mania has ~50 tracks). The
single sub-100 KB blob is most likely the missing SFX bundle or one of the
per-character sprite metadata files.

### 1.3 Hard constraints (verified from current build's `game.map`)

| Constraint                                    | Value                  |
|-----------------------------------------------|------------------------|
| Work RAM-H total                              | 1 MB (0x06000000)      |
| `JO_GLOBAL_MEMORY_SIZE_FOR_MALLOC`            | 576 KB (589824 B)      |
| `.bss` end (current)                          | ~0x060BC490            |
| SGL work-area ceiling                         | 0x060C0000             |
| Headroom remaining                            | ~14 KB                 |
| VDP2 VRAM                                     | 512 KB (banks A0/A1/B0/B1) |
| VDP2 CRAM                                     | 4 KB (1024 colors @16b) |
| Available NBG planes                          | 4 (NBG0/1/2/3); using 1 (FG) + 2 (sky) currently |
| VDP1 VRAM                                     | 512 KB (sprites + framebuffers) |
| CD capacity                                   | ~540 MB available      |
| Frame budget @ NTSC                           | 16.67 ms / 60 Hz       |

### 1.4 Build pipeline (verified working)

* `build.bat` produces `game.iso` + `game.cue` in ~60 s via Docker image
  `joengine-saturn:latest`.
* QA gates: title reference-diff (mean ΔRGB ≤ 12, p95 ≤ 120) +
  grounded-majority (≥ 2 of 8 captures show Sonic on the ground).
* Both gates currently PASS.

### 1.5 Standing rules carried from prior sessions (`memory/`)

* **Never guess Saturn hardware behaviour.** Read NOV96 DTS docs first.
* **VDP2 page upload uses SCU DMA in V-blank**, cache-through source alias.
* **CRAM is off-by-one;** every 256-color PAL is pre-shifted up by 1 entry
  on disk. New zone PALs MUST go through `tools/shift_pal.py` (or be written
  shifted by the converter).
* **Changing the jo malloc pool requires `rm jo-engine/jo_engine/*.o`** —
  `make` does not track Makefile-variable changes.
* **QA captures must start from BIOS** with `-Wait 18-22`. Shorter waits
  land in the BIOS load and report false-pass.
* **RSDK decoration tiles are flagged solid** in TileConfig.bin — surface
  builder requires ≥ 4 tiles of continuous solid mass below a candidate
  surface tile, or Sonic floats on flowers / sun totems.
* **SGL sound vs scroll CPU-DMA conflict** — use `slDMAXCopy`
  (SCU DMA, not CPU DMA) for the V-blank page upload.

---

## 2. Architectural Decisions

### 2.1 The engine remains "Saturn-native, asset-driven"

We do not port the RSDKv5 C++ engine. It does not fit. We continue the
established pattern: PC-side Python converts source data into Saturn-native
binary files; runtime loads files at known paths and plays them. This
constrains the runtime to what fits in a 28.6 MHz SH-2 with 576 KB pool
and is the only path that has been shown to work.

### 2.2 One generic zone runtime, parameterised by a descriptor

The current `main.c` hard-codes filenames (`GHZFG.CEL`, `GHZSKY.DAT`,
`GHZRINGS.BIN`, …) and dimensions (`LEVEL_W_PX = 16384`). The next major
refactor (Phase 2) replaces this with a `zone_desc_t` struct loaded from a
single descriptor file (`cd/ZONES.IDX`). Each zone gets exactly one entry:

```c
typedef struct {
    char    prefix[8];       /* e.g. "GHZ1", "CPZ2", "SSZ1"          */
    u16     level_w_tiles;   /* level width in 16-px tiles            */
    u16     level_h_tiles;   /* level height in 16-px tiles           */
    u16     spawn_x;         /* player spawn world-x in pixels        */
    u16     spawn_y;         /* player spawn world-y in pixels        */
    u8      music_track;     /* CD-DA track number for BGM            */
    u8      next_zone;       /* index of next zone on act-clear       */
    u8      flags;           /* WATER | UNDERGROUND | NIGHT | ...     */
    u8      boss_class;      /* BOSS_NONE, BOSS_DDWRECKER, ...        */
} zone_desc_t;
```

All file paths derive from `prefix`: `GHZ1FG.CEL`, `GHZ1SKY.DAT`,
`GHZ1RINGS.BIN`, etc. The existing GHZ Act 1 files get renamed at build
time from `GHZ*` to `GHZ1*` so the new pattern is uniform.

### 2.3 Asset memory model — one zone resident, others on CD

* **At rest in pool**: a single zone's resident asset set (≈ 470 KB peak
  for GHZ-class zones). Player sprites + global sprites + HUD + audio
  add ~80 KB on top, with ~26 KB headroom.
* **On zone transition**: full teardown of zone-specific buffers, blocking
  CD I/O for the next zone's assets, then resume. Player + global buffers
  persist across transitions (free at game-over / return to title only).

This is **already** the implicit model; we are only formalising it.

### 2.4 Audio routing

* **Music**: CD-DA. Every zone act gets a dedicated CD-DA track. Total
  music budget: 27 tracks for V1 (24 act themes + title + invincibility +
  Blue Spheres). Each track ~3 minutes × 10 MB/min = ~30 MB; comfortably
  under the 540 MB CD ceiling.
* **SFX**: SCSP via `jo_audio_play_sound`. Per-SFX limit ~10 KB to keep
  pool overhead bounded. Long ambient samples that exceed this become
  CD-DA tracks instead.

### 2.5 VDP2 plane allocation

| Plane | Use                                         |
|-------|---------------------------------------------|
| NBG0  | UI / HUD overlay (currently unused; will host the pause overlay + tally screen) |
| NBG1  | Foreground playfield (streaming, current)   |
| NBG2  | Mid-distance parallax sky / background art  |
| NBG3  | Far parallax band (clouds, distant mountains) — used by zones that need it |
| RBG0  | Reserved; not enabled in V1                 |

Two-layer-only zones (GHZ, SSZ) leave NBG3 disabled. Three-layer zones
(CPZ, HCZ, MMZ) enable NBG3 with a per-zone sky+far split.

### 2.6 Boss & gimmick code as drop-in modules

Each boss is a single C file (`src/bosses/<name>.c`) implementing four
functions: `init`, `tick`, `draw`, `teardown`. The zone runtime calls
`boss_table[zone_desc->boss_class]->tick()` once per frame when the
player has reached the boss arena bounds. Same pattern for zone-specific
gimmicks (`src/gimmicks/<name>.c`).

This keeps `main.c` small (< 2000 lines) and lets each subsystem be
worked on, tested, and reverted independently.

---

## 3. Phase Plan — Vertical Slice First, Breadth Second

Phases land in this strict order. Each phase ends with a working, QA-gated
build. The intent is that **after every phase you can boot a Saturn ISO
and see real progress**, not a half-rotted feature.

Estimated session counts are conservative — each row is one
"phase deliverable", and the line items inside it are not necessarily
one session each.

| # | Phase                                       | Sessions | Exit criterion                                |
|---|---------------------------------------------|----------|-----------------------------------------------|
| 0 | Complete asset extraction from Data.rsdk    | 1        | All 61 unnamed blobs decrypted + filed        |
| 1 | Asset-replacement pass (custom sprites/music/SFX swapped in) | 1 | New SPRs/PCMs/CD-DA in cd/, ISO boots, both QA gates pass |
| 2 | Zone-descriptor refactor + GHZ1/GHZ2 dual   | 2        | GHZ Act 1 → Act 2 transition works in-engine  |
| 3 | Act-clear → tally → save flow               | 2        | Signpost spin, tally screen, BUP-RAM save     |
| 4 | Hurt / death / lives / game-over            | 1        | Mania-faithful damage + pit death + GAME OVER |
| 5 | Pause + start menu + zone-select skeleton   | 1        | START in-game pauses; main menu offers select |
| 6 | GHZ boss (Death Egg Robot drop-in)          | 2        | Boss arena loads, defeat advances next zone   |
| 7 | Per-zone conversion: CPZ, SPZ, FBZ, PSZ     | 4        | Four additional zones ship as playable acts   |
| 8 | Per-zone conversion: SSZ, HCZ, MSZ, OOZ     | 4        | Eight zones total                             |
| 9 | Per-zone conversion: LRZ, MMZ, TMZ          | 4        | All 12 main story zones playable              |
|10 | Per-zone boss implementations               | 6–8      | One boss per zone wired to its arena          |
|11 | Tails + Knuckles sprite + ability swap-ins  | 3        | Character-select on main menu; all 3 playable |
|12 | Blue Spheres special stage                  | 3        | Big-ring collect → BS stage → emerald award   |
|13 | Continue / ending / cutscene shell          | 2        | Beating Zone 12 boss triggers ending state    |
|14 | Core polish + balance + audio mastering     | 2        | Core 14-phase QA gate + manual playtest pass  |
|15 | UFO 3D special-stage engine + 7 stages      | 8–10     | Big-ring → UFO chase → emerald award          |
|16 | Mighty + Ray (Mania Plus characters)        | 3        | All 5 characters selectable; abilities work   |
|17 | Encore Mode (palette + layout variants)     | 4        | Encore unlocked after main; 12 zones replay   |
|18 | PSZ full pinball gimmick mode               | 2        | PSZ pinball machines fully playable           |
|19 | Time Attack mode + ghost replays            | 3        | Per-act timer leaderboard saved to BUP-RAM    |
|20 | Competition split-screen mode               | 4        | 2P versus on chosen zones, half-height view   |
|21 | Final V1 ship: polish, mastering, manual    | 3        | Real-hardware Satiator boot pass; gold master |

Total ballpark: **62–73 sessions** of focused work for the full V1 target
(everything Mania has, on Saturn). The first **14 phases** ship the core
single-player main-story experience as a complete, self-contained
"V1-core" milestone; phases 15–21 layer the remaining modes and
characters on top of that foundation.

The remainder of this document specifies Phases 0–6 in implementation
detail. Phases 7–21 follow the same patterns established by 0–6 and will
be specified at the start of each phase rather than guessed in advance.

---

## 4. Phase 0 — Complete Asset Extraction from Data.rsdk

### 4.1 What we have, what we don't

`Data.rsdk` (182,962,115 bytes) contains 61 files we have not been able to
decrypt because their on-disk hashes (`MD5(lowercase(path))` with the
word-swap dance) don't match any path in the prior filelist. The RSDK
cipher requires the real path as the key, so we cannot decrypt without
knowing the path.

### 4.2 Path-discovery strategy

Three RSDK path conventions cover almost everything we are missing
(verified against `rsdkv5-src/RSDKv5/RSDK/Audio/Audio.cpp` and
`Graphics/Sprite.cpp` / `Animation.cpp`):

* `Data/Music/<name>.ogg` — every music track. Filename comes from
  per-stage SetMusicTrack / PlayStream calls. We do not have the per-stage
  game-side C source, so we enumerate **two candidate sources**:
  * (a) The decompilation-community's documented Mania music filename list
    (~50 well-known names: stage themes, jingles, ambients).
  * (b) Brute-force common patterns (`<ZoneFolder>.ogg`,
    `<ZoneFolder>Act1.ogg`, `<ZoneFolder>Act2.ogg`,
    `MiniBoss.ogg`, `Boss.ogg`, `Invincible.ogg`, `Speed.ogg`,
    `Drowning.ogg`, `1up.ogg`, `Stage.ogg`, `Bonus.ogg`, `Special.ogg`,
    `BlueSphere.ogg`, `Continue.ogg`, `GameOver.ogg`, `ActClear.ogg`,
    `Title.ogg`, `Menu.ogg`, `Credits.ogg`, …).
* `Data/SoundFX/<name>.wav` — every short SFX. Names are listed in
  `GameConfig.bin → sfx[]` (already parseable by
  `tools/parse_gameconfig.py`). Per-stage SFX additions are in each
  `StageConfig.bin`'s SFX section (same record format, parseable by
  extending the existing parser).
* `Data/Sprites/<folder>/<class>.bin` — per-object animation file. Folder
  is one of `Global`, `Players`, `<ZoneFolder>` (e.g. `GHZ`, `CPZ`). Class
  name comes from `GameConfig.bin → objects[]` and each
  `StageConfig.bin → objects[]`.

### 4.3 Deliverables — Phase 0

* **`tools/build_filelist.py`**: parses GameConfig.bin and every
  `extracted/Data/Stages/*/StageConfig.bin`, emits a candidate path list
  combining all four conventions above plus a community-sourced music
  name list. Writes to `tools/maniafilelist.txt`.
* **Re-run `tools/rsdk_extract.py Data.rsdk --filelist tools/maniafilelist.txt --out extracted/`**.
* **`tools/audit_extract.py`**: report what's still unnamed; if any audio
  blobs remain, the report includes their MD5 + size so a manual
  filename lookup can finish the job.

### 4.4 Exit criterion — Phase 0

Either:
1. All 61 unnamed blobs decrypted and filed under `extracted/Data/`, OR
2. The audit report explicitly documents each remaining blob with our
   best evidence on what it likely is (size class, encryption flag), so
   the user can supply the missing name or accept the omission knowingly.

No engine changes are made in Phase 0 — only the asset pack is completed.

---

## 5. Phase 1 — Asset-Replacement Pass

This is the "swap placeholders for real assets" pass the user explicitly
asked for. The engine code does not change; only the files in `cd/` are
replaced.

### 5.1 Sprite swaps

| `cd/` slot      | Source asset                                    | Conversion tool                                        |
|-----------------|-------------------------------------------------|--------------------------------------------------------|
| `SONIC.SPR`     | `Data/Sprites/Players/Sonic1.gif` (idle frames) | `tools/convert_player_sprite.py` (new — frame-list-driven extraction from Sonic.bin animation script) |
| `SONWALK.SPR`   | `Data/Sprites/Players/Sonic1.gif` (walk frames) | same                                                   |
| `RING.SPR`      | `Data/Sprites/Global/Ring.bin` + frames in `Items.gif` | `tools/convert_ring_sprite.py --animfile Ring.bin --atlas Items.gif` |
| `BADNIK.SPR`    | `Data/Sprites/GHZ/Objects.gif` (Motobug frames) | `tools/convert_object_sprite.py --animfile <Motobug.bin> --atlas Objects.gif --animation walking` |
| `SPRING.SPR`    | `Data/Sprites/Global/Items.gif` (Spring frames) | same                                                   |
| `MONITOR.SPR`   | `Data/Sprites/Global/Items.gif` (Monitor frame) | same                                                   |
| `SIGNPOST.SPR`  | `Data/Sprites/Global/Items.gif` (SignPost frames) | same                                                 |
| `DIGITS.SPR`    | `Data/Sprites/Global/HUD.bin` + atlas           | new converter (frame-table driven)                     |

The new converters are thin specialisations of `convert_ring_sprite.py`,
which already knows how to read a GIF atlas and emit the Saturn BGR1555
multi-frame format. They add an animation-script reader that consumes the
`<Class>.bin` files (RSDK animation format: per-animation frame indices,
durations, hitboxes — well documented in
`rsdkv5-src/RSDKv5/RSDK/Graphics/Animation.cpp`).

### 5.2 Audio swaps

| `cd/` slot or CD track | Source asset                              | Conversion tool                              |
|------------------------|-------------------------------------------|----------------------------------------------|
| `cd_audio/track02.wav` | `Data/Music/<GHZ Act 1 theme>.ogg`         | `ffmpeg -i in.ogg -ar 44100 -ac 2 -sample_fmt s16 out.wav` (already in `tools/build_cdda.py`) |
| `cd/RINGSFX.PCM`       | `Data/SoundFX/Ring.wav`                    | `tools/convert_audio.py`                    |
| `cd/JUMPSFX.PCM`       | `Data/SoundFX/Jump.wav`                    | same                                         |
| (new) `cd/BREAKSFX.PCM`| `Data/SoundFX/Break.wav` (or `Destroy.wav`)| same                                         |
| (new) `cd/BOUNCESFX.PCM`| `Data/SoundFX/Spring.wav`                 | same                                         |
| (new) `cd/STOMPSFX.PCM`| `Data/SoundFX/Stomp.wav`                   | same                                         |
| (new) `cd/HURTSFX.PCM` | `Data/SoundFX/Hurt.wav`                    | same                                         |
| (new) `cd/LOSEFX.PCM`  | `Data/SoundFX/LoseRings.wav`               | same                                         |

The five new SFX slots are wired up in `setup_audio()` and triggered at
the relevant game events (break/bounce/stomp/hurt already have call
sites; the wrappers just point at real sources now).

### 5.3 Title-screen backdrop swap

`Data/Stages/Title/16x16Tiles.gif` + `Scene1.bin` define the Mania title
screen layout. We convert it via the existing `convert_stream.py` flow
to a single static `TITLE.CEL`/`PAL`/`TMP` and load it into NBG2 (where
the procedural placeholder currently draws). The procedural
`draw_title()` body is replaced with a one-time NBG2 setup + identical
title-card cinematic on top.

### 5.4 Exit criterion — Phase 1

* `build.bat` PASSES both QA gates with new assets in place.
* Manual capture from BIOS at `-Wait 18 -Shots 4 -Every 1.0` shows:
  * Title screen displays the real Mania title art.
  * Title card displays the real ring + "1" glyphs.
  * Sonic on the GHZ playfield is recognisably Sonic, walking with the
    real walk cycle.
  * BGM is audible and recognisable.
  * Ring pickup plays the real Mania ring chime.
* `tools/refs/title_view.golden.png` is regenerated from the new title
  art (the diff gate updates as a deliberate baseline change).

---

## 6. Phase 2 — Zone-Descriptor Refactor + GHZ1 / GHZ2 Both Playable

### 6.1 New files

* **`src/zone.h`** — defines `zone_desc_t` (section 2.2), `g_zone_table[]`,
  `g_current_zone`, prototypes `zone_load(u8 idx)`, `zone_teardown(void)`,
  `zone_advance(void)`.
* **`src/zone.c`** — implementations. `zone_load` performs:
  1. `zone_teardown()` (free all zone-specific buffers in reverse alloc
     order — VDP1 sprite slots first, then jo pool buffers).
  2. Block until any pending audio CD seek completes.
  3. `jo_audio_stop_cd()`.
  4. Read `<prefix>FG.{PAL,CEL,PAT,TMP}`, `<prefix>SKY.{DAT,PAL}`,
     `<prefix>SURF.BIN`, per-class entity files.
  5. Allocate per-entity state arrays sized to the count headers.
  6. Initialise camera, player at `spawn_x/spawn_y`, HUD reset.
  7. `jo_audio_play_cd_track(zone_desc->music_track, zone_desc->music_track, true)`.
* **`cd/ZONES.IDX`** — packed big-endian array of `zone_desc_t`. Length
  prefix u16 + entries. For Phase 2 there are exactly two entries: GHZ1
  and GHZ2. The table grows in subsequent phases.

### 6.2 main.c changes

* Remove every `static jo_palette g_fg_pal`-style static at file scope;
  move into `src/zone.c` and expose getters.
* Replace `setup_ghz_foreground()`, `setup_ghz_sky()`,
  `load_rings()`/`load_badniks()`/`load_springs()`/`load_monitors()`/
  `load_signposts()` with `zone_load(g_current_zone)`.
* Replace hard-coded `LEVEL_W_PX = 16384` with
  `g_zone_table[g_current_zone].level_w_tiles * 16`.

### 6.3 GHZ Act 2 conversion

* Rename current `cd/GHZ*` files to `cd/GHZ1*` (build-time `mv`).
* Run the existing converters on `extracted/Data/Stages/GHZ/Scene2.bin`
  with output prefix `cd/GHZ2`:
  * `tools/convert_stream.py --scene extracted/Data/Stages/GHZ/Scene2.bin --tiles extracted/Data/Stages/GHZ/16x16Tiles.gif --stageconfig extracted/Data/Stages/GHZ/StageConfig.bin --out-prefix cd/GHZ2`
  * `tools/build_collision.py --tileconfig extracted/Data/Stages/GHZ/TileConfig.bin --scene extracted/Data/Stages/GHZ/Scene2.bin --out cd/GHZ2SURF.BIN`
  * `tools/build_rings.py --scene extracted/Data/Stages/GHZ/Scene2.bin --classes Ring,Motobug,Spring,ItemBox,SignPost --out-prefix cd/GHZ2`

### 6.4 Transition trigger (interim)

Signpost-touch in Phase 2 simply calls `zone_advance()` immediately (no
tally yet — that's Phase 3). The 60-tick visual freeze is replaced by a
solid black fade-out / fade-in around the `zone_load()` call, performed
by ramping CRAM to all-zero and back over 20 ticks each side.

### 6.5 Exit criterion — Phase 2

* Booting `game.iso` plays GHZ Act 1.
* Touching the signpost fades to black, loads GHZ Act 2, fades back in,
  and plays GHZ Act 2 to a second signpost.
* No memory leaks across the transition — running both acts back-to-back
  five times in a row does not exhaust the pool (verified by
  `tools/qa_validate.py` extended to monitor `_end` between loads).
* Both QA gates pass.

---

## 7. Phase 3 — Act-Clear → Tally → Save

### 7.1 Act-clear cinematic

* **Signpost spin** — extend `SIGNPOST.SPR` from 1 frame to 8 frames
  (rotating-pole animation extracted from `Data/Sprites/Global/Items.gif`
  via the new sprite converter). The signpost touch sets
  `g_state = STATE_ACTCLEAR`, starts a 120-tick state machine: spin (60
  ticks) → settle on character face (60 ticks) → transition to tally.
* During `STATE_ACTCLEAR`, player input is frozen, autorun continues at
  the current `gsp` until Sonic walks off-screen right, then he's hidden.
* `cd_audio/track27.wav` (act-clear jingle, ~6 s) plays on touch via
  `jo_audio_play_cd_track(27, 27, false)`.

### 7.2 Tally screen (`STATE_TALLY`)

* New NBG0 overlay (currently unused). Renders:
  * `SONIC GOT THROUGH`
  * `ACT <n>`
  * `TIME BONUS  <n>`  (digits roll up over 60 ticks)
  * `RING BONUS  <n>`
  * `SCORE       <total>`
* Roll-up uses ease-out cubic (`y = 1 - (1-t)^3`).
* Final tally state holds until START pressed, then `zone_advance()`.

### 7.3 Save to backup RAM

* `JO_COMPILE_WITH_BACKUP_MODULE=1` is already set.
* New file `src/save.c` implements:
  ```c
  typedef struct {
      u32 magic;              /* 'SMSV' */
      u8  version;            /* 1 */
      u8  current_zone;       /* index into g_zone_table */
      u32 score;
      u32 best_score;
      u8  lives;
      u8  emeralds;           /* bitfield of collected emeralds */
      u8  char_select;        /* 0 = Sonic; reserved 1/2 = Tails/Knuckles */
      u8  pad;
  } save_slot_t;              /* 16 bytes */
  ```
* `save_write(slot)` / `save_read(slot)` wrap `jo_backup_save_file` /
  `jo_backup_read_file` on the internal device. Three slots supported.
* Save is auto-written at end of every tally screen (between acts) and on
  game over.

### 7.4 Exit criterion — Phase 3

* GHZ1 → signpost → spin → tally with rolling digits → START → GHZ2 → repeat.
* After GHZ2 tally, `current_zone` is written to backup RAM. Power-cycling
  Mednafen (state-load with cleared RAM) preserves the zone progression.
* Both QA gates pass; tally state added as a third capture point.

---

## 8. Phase 4 — Hurt / Death / Lives / Game-Over

### 8.1 Hurt state machine (replaces current "rings=0" placeholder)

* Side-contact with badnik (or any future hazard) when **not invuln**
  and **not jumping/rolling**:
  * If `g_rings > 0`: enter `PSTATE_HURT` for 120 ticks (invuln flash
    every 4 ticks); set `xsp = (facing == LEFT ? +2.0 : -2.0)`, `ysp = -4.0`;
    spawn ring-scatter (see below).
  * If `g_rings == 0`: enter `PSTATE_DEAD` (death animation + sound),
    180-tick despawn, then `respawn_or_gameover()`.
* During `PSTATE_HURT`, all collision is enabled except enemy contact;
  the ground-follow re-engages on landing.

### 8.2 Ring scatter

* New entity class: `scattered_ring_t` (max 32 simultaneous).
* Spawn pattern: 32 rings in a cosine-arc burst around the hurt
  position. Per ring: `xsp = sin(angle) * speed`, `ysp = cos(angle) * speed`,
  gravity = 0.0625 / tick, 256-tick despawn.
* While airborne, rings are non-collectable. After 64 ticks they enter a
  blinking "collectable" sub-state.

### 8.3 Death pit + respawn

* If `ypos > level_h_px + 64` (off-bottom-of-level) regardless of state:
  enter `PSTATE_DEAD`, suppress ring-scatter, force `g_rings = 0`.
* On `PSTATE_DEAD` despawn:
  * `g_lives -= 1`.
  * If `g_lives > 0`: reset to last-touched `BoundsMarker` (or
    `zone_desc->spawn_x/y` if none), reload from quick-state snapshot
    (timer continues; score persists), resume.
  * If `g_lives == 0`: `g_state = STATE_GAMEOVER`.

### 8.4 Game-over state

* New NBG0 overlay: `GAME` / `OVER` (large glyphs, 2× scale via
  `jo_sprite_set_scale`).
* `cd_audio/track28.wav` (game-over jingle) plays once.
* After 240 ticks: return to `STATE_TITLE`. Save is **not** written (the
  last-saved state from the previous tally remains the "checkpoint" for
  the next Continue).

### 8.5 Exit criterion — Phase 4

* Touching a badnik with ≥ 1 ring scatters the ring set and bumps Sonic.
* Touching a badnik with 0 rings dies the player; lives count decrements;
  Sonic respawns at last `BoundsMarker`.
* Walking off the bottom of the level (forced via debug `jo_input_xxx`
  pad-Y trigger or simply jumping at the right edge of a pit) dies the
  player.
* After 3 deaths in a row the GAME OVER screen plays.
* Both QA gates pass; grounded gate threshold may be loosened to ≥ 1 of
  8 if the death-and-respawn cycle puts Sonic airborne in too many
  capture windows.

---

## 9. Phase 5 — Pause + Start Menu + Zone-Select Skeleton

### 9.1 Pause (`STATE_PAUSE`)

* START in `STATE_GAME` enters `STATE_PAUSE`. Game tick is suspended
  (entity updates skipped) but render still runs.
* NBG0 overlay draws a translucent dim layer + centred "PAUSE" + two
  options: `RESUME` / `EXIT`. UP/DOWN toggles selection; A selects.
* `RESUME` returns to `STATE_GAME`. `EXIT` saves and returns to
  `STATE_TITLE`.
* Music continues (CD-DA is unaffected by main-CPU pause); SFX are
  suspended.

### 9.2 Main menu (`STATE_MENU`, replaces `STATE_TITLE` as default boot state)

* New menu rendered on NBG0:
  ```
  SONIC MANIA  (SATURN)
  > NEW GAME
    CONTINUE      (greyed if no save)
    ZONE SELECT
    OPTIONS
  ```
* `NEW GAME`: wipe save slot 1, load zone index 0 (GHZ1).
* `CONTINUE`: load zone from `save_read(0).current_zone`.
* `ZONE SELECT`: lists all zones in the descriptor table; START loads
  the chosen index. Permanently unlocked in V1 for testing convenience;
  can be gated behind a code in V2.
* `OPTIONS`: contains a single option in V1, `SFX VOLUME` (0–7).
  Provides a placeholder for later additions (controller config,
  music volume, language).

### 9.3 Exit criterion — Phase 5

* Booting drops into the menu, not the title cinematic.
* Selecting NEW GAME starts at GHZ1 with cleared save.
* Selecting CONTINUE after the Phase-3 save resumes at the saved zone.
* In-game START pauses; selecting EXIT returns to the menu with save
  intact.
* Zone-select successfully boots into every defined zone (in Phase 5
  the table still only contains GHZ1 / GHZ2).

---

## 10. Phase 6 — GHZ Boss (Death Egg Robot drop-in)

### 10.1 Boss arena

* GHZ2's existing signpost is repurposed: touching it triggers the boss
  fight rather than `zone_advance()`. The signpost re-becomes the
  act-clear trigger after the boss is defeated.
* The boss arena is the existing GHZ2 end-of-level area; camera locks
  horizontally between two boundary X values (defined per-zone in
  `zone_desc_t` as a future extension, hard-coded in Phase 6).

### 10.2 Boss module `src/bosses/eggrobot.c`

* Implements `boss_init`, `boss_tick`, `boss_draw`, `boss_teardown`.
* Boss entity: `x`, `y`, `xsp`, `ysp`, `hp`, `state`, `state_ticks`,
  `facing`.
* AI states (simplified for Phase 6; refinement in later polish):
  * `BSTATE_DROP` — descends from off-screen-top to fixed Y.
  * `BSTATE_HOVER` — sweeps left↔right at constant Y for 240 ticks.
  * `BSTATE_DIVE` — accelerates downward toward player ground level,
    rebounds.
  * `BSTATE_HURT` — invuln-flash for 30 ticks after each hit.
  * `BSTATE_DEFEATED` — explosion sequence (8 sprite frames over
    120 ticks), then act-clear.
* HP = 8. Each successful jump-hit deducts 1 and triggers `BSTATE_HURT`.

### 10.3 Boss sprite extraction

`Data/Sprites/GHZ/EggRobo.bin` + the relevant atlas in `Objects.gif`
feed the new `convert_object_sprite.py --animation` pipeline. Output:
`cd/EGGROBOT.SPR` (multi-frame).

### 10.4 Exit criterion — Phase 6

* GHZ2 to boss-trigger to fight to defeat to act-clear → tally → load
  next zone (which in Phase 6 falls through to a "STAGE NOT IMPLEMENTED"
  placeholder that returns to menu; placeholder stays until Phase 7).
* QA gates pass with a third gate added: "boss-defeated" capture (boot,
  skip to GHZ2 via debug zone-select if needed, walk to boss, defeat).

---

## 11. Phases 7–14 — Per-Zone Roll-Out and Endgame (summary)

Phases 7–9 each ship 3–4 zone conversions using the established
pipeline. The order is chosen to front-load the visually distinctive
zones (CPZ, SPZ, FBZ) and back-load the technically complex ones (LRZ
rising-lava, MMZ scaling, TMZ moving platforms). For each zone the work
is:

1. Run `convert_stream.py` + `build_collision.py` + `build_rings.py`
   with the zone's prefix.
2. Extract zone-specific sprites via `convert_object_sprite.py` for any
   new badnik/object classes that don't appear in earlier zones.
3. Add entry to `cd/ZONES.IDX`.
4. Add any zone-specific gimmick module (loops, water, conveyor) under
   `src/gimmicks/<zone>.c` and wire it into the zone runtime via a
   gimmick-flag bit in `zone_desc_t.flags`.

Phases 10 (per-zone bosses), 11 (Tails + Knuckles), 12 (Blue Spheres),
13 (continue / ending), 14 (polish) each follow the same drop-in module
pattern. Each phase's deliverable spec is written at the start of that
phase, after the preceding phase's exit criterion has been met and the
build state is known.

---

## 12. Risk Register

| Risk                                                          | Severity | Mitigation                                                                                                              |
|---------------------------------------------------------------|----------|-------------------------------------------------------------------------------------------------------------------------|
| Pool exhaustion when a non-GHZ zone exceeds 470 KB of assets  | High     | Per-zone budget audit before conversion. If a zone exceeds budget, downscale tile palette count or split tilemap into two CD-streamed halves. |
| CRAM overflow when a zone needs > 4 palettes resident         | Medium   | Each zone may use at most 4 of the 8 CRAM palettes (256 colors × 4 = 1024 = full CRAM). Player + HUD share the other 4. |
| VDP2 VRAM exhaustion when a zone needs > 4 char banks         | Medium   | Default mapping holds. Zones with > 4 layers (rare) collapse the two parallax bands at conversion time.                 |
| ~317 KB cold-boot binary-size threshold (mmm-netlink memory)  | Medium   | Monitor `game.elf` size every build. If we cross 317 KB, profile what symbols grew and split into multiple compilation units. |
| Music tracks too large for CD (24 × 30 MB = 720 MB)           | Low      | Compress to lower bitrate or shorter loops if it ever bites. 540 MB / 27 tracks = ~20 MB / track = 2 min stereo @ 1411 kbps = entirely fine. |
| Mednafen-only QA misses real-hardware regressions             | Medium   | Manual Satiator burn at every major milestone (every 3 phases). User performs real-hardware boot test; we log results.  |
| Boss animation pixel-perfect to Mania                         | Low      | Boss visuals are best-effort, not pixel-perfect. The plan prioritises gameplay correctness.                              |
| Decryption of remaining 61 blobs fails entirely               | Low      | Even if music decryption fails, the user can place their own `track*.wav` files in `cd_audio/` directly — the build pipeline reads from there, not from extracted/. |

---

## 13. Build & QA Cadence

* Every phase exit runs `build.bat` (both QA gates) before being declared
  complete.
* After Phases 3, 6, 9, 11, 14: full manual playthrough capture (60 s of
  game-state Mednafen play, video-captured for visual review).
* `memory/MEMORY.md` index gets a new entry whenever a phase reveals a
  new permanent fact (e.g. "Zone X requires custom NBG3 priority of 4").
* Backup `src/main.c.bak`-style snapshots taken at every phase exit so a
  bad regression can be reverted in one move.

---

## 14. Deferred-to-later-phase items (all still in V1 scope)

Per user direction 2026-05-25, every item below is in the final V1
target. They are sequenced after the core 14 phases because they each
depend on a foundation built in 0–14. None are cut.

| Item                  | Phase | Why sequenced after core 14                                                  |
|-----------------------|-------|------------------------------------------------------------------------------|
| UFO 3D special stages | 15    | Needs polygon renderer + Mode-7 floor; sits on top of the special-stage shell laid in Phase 12. |
| Mighty + Ray          | 16    | Needs the character-abstraction layer built in Phase 11 for Tails + Knuckles. |
| Encore Mode           | 17    | Needs the full main-game zone library (Phases 7–9) before alternate layouts are meaningful. |
| PSZ pinball gimmick   | 18    | Needs the gimmick-module framework established by the simpler zone gimmicks in Phases 7–9. |
| Time Attack           | 19    | Needs the save-data system (Phase 3) and stable physics across all zones (Phases 7–9). |
| Competition mode      | 20    | Halves frame budget per player; requires the engine to be measured and optimised first (Phase 14). |
| Final ship / mastering | 21   | Real-hardware Satiator boot validation + manual + gold master.               |

---

## 15. Decision Points That Require User Input Before Execution

These are the only places I will pause for explicit user confirmation
before proceeding. Everything else I execute per spec.

1. **Phase 0 audit gap** — if any of the 61 unnamed blobs cannot be
   matched even after exhausting the candidate list. User decides
   whether to supply the missing name(s) or accept the omission.
2. **Phase 1 title art** — confirmation that the converted Mania title
   art is visually acceptable; the QA reference baseline is regenerated
   based on user sign-off.
3. **Phase 5 menu layout** — confirmation on menu styling (single
   character font, or richer art).
4. **Phase 10 boss difficulty calibration** — once one boss is in,
   user feedback drives HP / iframe / sweep-speed tuning.
5. **Phase 14 polish-pass priorities** — final go/no-go on shipping
   without UFO / Encore.

---

## 16. First Action After This Plan Is Approved

Execute **Phase 0**. Concretely:

1. Write `tools/build_filelist.py` (parses GameConfig.bin + every
   StageConfig.bin, emits combined candidate path list to
   `tools/maniafilelist.txt`).
2. Add to the candidate list a community-sourced music name list
   (`Data/Music/<known names>.ogg`) and the SFX list derived from
   GameConfig's `sfx[]`.
3. Re-run `tools/rsdk_extract.py Data.rsdk --filelist tools/maniafilelist.txt --out extracted/` and report counts.
4. Run a new `tools/audit_extract.py` to report any blobs still unmatched.
5. Report results back to the user before proceeding to Phase 1.

— end bible —
