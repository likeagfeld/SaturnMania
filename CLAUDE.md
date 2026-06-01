# CLAUDE.md — Entry-point context for any agent working in this repo

**READ THIS FIRST.** This file is the project-level operating manual. Every
session begins here. It supersedes the prior `HANDOFF.md` "what's next"
section and is the binding contract for how to work on this codebase.

Last revised: 2026-05-26

---

## 1. The mission

Port **Sonic Mania** to the **Sega Saturn**. Frame-for-frame parity with
the PC Steam build. Every visible sprite, every animation, every palette
cycle, every state machine, every physics constant must come from the
authoritative Sonic Mania decompilation — not hand-rolled.

The user's binding directive (2026-05-26):

> "PULL THE DECOMP AND USE THAT AS THE AUTHORITATIVE SOURCE OF TRUTH FOR
> EVERYTHING!!! ALSO REPLACE ANYTHING ELSE WE HAVE CUSTOM BUILD WITH PORT
> FROM DECOMP IF THAT MAKES MORE SENSE."

This is the binding rule. Any agent that hand-rolls game logic that has a
direct decomp counterpart is wrong.

---

## 2. Authoritative sources of truth

| Layer | Source | Role |
|---|---|---|
| Game logic (per-entity behavior) | **Sonic-Mania-Decompilation** repo (RSDKModding on GitHub) — selectively cached at `tools/_decomp_raw/` (currently 41 files: Title set, GHZ set, Global subsystems including Player/Camera/Music/SaveGame) | Every `Object_*.c` we port comes from here. Pull additional files via `gh api repos/RSDKModding/Sonic-Mania-Decompilation/contents/<path>?ref=master` when needed. |
| Engine behavior (RSDK APIs the game calls) | **RSDKv5-Decompilation** repo — locally at `rsdkv5-src/RSDKv5/RSDK/` | Source of truth for what each `RSDK.*` call actually does. The Saturn-side `src/rsdk/` layer translates these to jo/SGL/direct VDP. |
| Game assets | `extracted/Data/` (Sprites, Stages, Music, SoundFX, Video) | Original asset blobs from `Data.rsdk`. Saturn-side asset conversion runs offline via `tools/build_*.py` and emits Saturn-native formats into `cd/`. |
| Hardware contract | `D:\Claude Saturn Skill Documentation\` (DTS96 manuals, SGL/SBL samples, VDP1/VDP2 user manuals) | The Saturn-side `src/rsdk/*` implementations are bounded by these. Read DTS docs BEFORE writing register-level code (binding rule from `memory/rule-read-dts-before-coding.md`). |

---

## 3. Architecture

The codebase is a three-tier port:

```
src/mania/Objects/<Cat>/<Obj>.c   <- DECOMP PORTS. Mirrors decomp layout.
                                     Every file cites its decomp counterpart
                                     and reads as a mechanical translation.
                                     Calls into src/rsdk/ APIs only.
       |
       v
src/rsdk/<subsystem>.c            <- ENGINE COMPAT LAYER. Translates the
                                     RSDK API surface (DrawSprite, Process-
                                     Animation, SetClipBounds, etc.) into
                                     Saturn-native jo/SGL/VDP calls.
       |
       v
jo-engine/ + SGL                  <- Saturn hardware abstraction (VDP1
                                     sprites, VDP2 NBG scrolls, CRAM,
                                     SCSP audio, CD-DA, GFS files).
```

**The line between game-logic and engine-compat:**
- If decomp uses `RSDK.*` to call it → engine API → lives in `src/rsdk/`
- If decomp file is `Objects/<Cat>/<Obj>.c` → game logic → ported to
  `src/mania/Objects/<Cat>/<Obj>.c`

Phase A (engine) builds the `src/rsdk/` surface. Phase B (objects) ports
each Mania `Object_*.c` mechanically. Phase Z (Saturn-native) replaces
the Saturn-incompatible RSDK APIs (3D mesh, scanline FX, full palette FX)
with platform-native implementations.

See `BIBLE.md` for the formal phase plan.

---

## 4. The methodology — non-negotiable for every feature

Every time we port a feature, follow these steps in this order. **Do not
skip steps. Do not start coding before steps 1-3 are visible in the chat.**

### 4.PRE — PER-RESPONSE SKILL-COMPLIANCE CHECK (BINDING, 2026-05-27)

**Before EVERY response in this project — every single one, including
status updates, screenshot deliveries, build invocations, plan
discussions, and code changes — the agent MUST silently run this
checklist:**

1. **Is the `sega-saturn-developer` skill active in this session?**
   If not, invoke it now. The skill at
   `C:\Users\gary\.claude\skills\sega-saturn-developer\skill.md`
   (v2.5.0+) is the canonical methodology for any work touching this
   repo. Every CLAUDE.md rule below mirrors a skill rule.

2. **Am I about to produce output that follows the skill's discipline?**
   Specifically:
   - **Data-driven, not impressionistic.** No "looks better", no
     "should work", no "probably". Only measurements (centroid,
     pixel-mass, SSIM, byte-diff, RMS, BSS-margin, build exit-code).
   - **Catalogue first.** If the response involves Saturn hardware or
     subsystem reasoning, the `complete-doc-index.md` shortlist has
     been consulted.
   - **Citation-backed.** Code change cites decomp file:line + DTS doc
     §:page + memory rule. Plan/status references prior phase task IDs
     + §-numbers in `docs/COMPREHENSIVE_PLAN.md`.
   - **RED gate before fix.** Any user-reported bug gets a measurable
     gate added that fires RED on current build, BEFORE any fix
     attempt. Watching RED→GREEN is the only "fixed" evidence.

3. **If dispatching a sub-agent via `Agent` tool:** the prompt MUST
   include the verbatim skill-binding pre-amble per §4.0.1. Self-check
   that the strings `complete-doc-index.md`, `RED-firing gate`, and
   subsystem-specific docs/samples are present in the prompt before
   calling `Agent`.

4. **If this is a status/screenshot/relay response (no code change):**
   the discipline still applies. State concrete findings (file:line,
   pixel coordinates, byte counts, gate verdicts). Don't paraphrase or
   editorialize. Don't say "should look like" or "appears to" — say
   "frame N shows X pixels at coordinates (a,b) per
   `tools/qa_visual_diff.py` output."

5. **If the response would skip any of 1-4, RESTART.** Don't ship the
   response. Compose it again, properly. The user has standing
   authority to call out skipped checks.

**Why this rule exists:** Phase 1.4-1.15 trial-and-error, Phase 1.23
coord-guess, Phase 1.24 worse-than-baseline — every drift was a primary
agent (or sub-agent) producing output without running this compliance
loop. The check is internal/silent (no need to dump the checklist into
every response) but must precede every reply.

**Companion files:**
- `C:\Users\gary\.claude\skills\sega-saturn-developer\skill.md` v2.5.0
  (the skill being checked against)
- `C:\Users\gary\.claude\projects\D--sonicmaniasaturn\memory\binding-session-methodology.md`
  v3 (project-level mirror — adds the sub-agent dispatch rule)

### 4.0 BINDING SESSION STARTUP (v2 — reinforced 2026-05-27)

Every new agent session for THIS project MUST, before touching any code:

1. **Read the complete doc index end-to-end FIRST.** Open
   `C:\Users\gary\.claude\skills\sega-saturn-developer\references\complete-doc-index.md`.
   It catalogues EVERY doc, sample, header, and tool in
   `D:\Claude Saturn Skill Documentation\` (DTS96, SBL601, SBL6, HTML
   docs, sega_saturn_docs txt extracts, NOV96 examples, Tools and
   Utilities, Saturn Video Tools, NetLink refs). The file is bounded
   (~600 lines) — read it whole, never the first matching row.
2. **From the user request, identify EVERY applicable doc + sample.**
   The lookup table at the bottom of complete-doc-index.md maps the
   subsystem to ALL applicable refs. Read every one. Not just the
   first. If two docs disagree, the PDF wins over the HTML extraction;
   note the disagreement explicitly. Authoritative-by-subsystem
   shortlist for fast triage:
   - VDP1 = `ST-013-R3-061694.pdf` + `ST-013-SP1-052794.pdf` +
     `TUTORIAL.pdf` + `SMPSPR2/7/8` samples + `vdp1-reference.md`
   - VDP2 = `ST-058-R2-060194.pdf` + `ST-157-R1-092994.pdf` +
     `SAMPLE/SCROLL/` + `SAMPLE2/SEGA2D_1/SCROLL.C` + `vdp2-reference.md`
   - SGL = `ST-238-R1-051795.pdf` + `ST-237-R1-051795.pdf` +
     every numbered `SGL302/SAMPLE/S_*` sample
   - SCSP = `ST-077-R2-052594.pdf` + `ST-166-R4-012395.pdf` +
     `SOUND/CODEDEMO/*` + `SBL6/SEGASMP/SND/`
   - SMPC = `ST-169-R1-072694.pdf` + `EXAMPLES/PERIPHS/*`
   - SCU = `ST-097-R5-072694.pdf` + `ST-210` + `EXAMPLES/DEVCON96/DEMODSP/`
   - Dual CPU = `ST-202-R1-120994.pdf` + `EXAMPLES/DUALCPU/SLVSAMP.C`
   - CD/GFS = `ST-136-R2-093094.pdf` + `EXAMPLES/GFSDEMO/` +
     `SBL6/SEGASMP/GFS/`
3. **Read the archived working sibling** (`src/_archived/main.c.v01-
   handrolled` for any visible-rendering question) end-to-end. The
   archived build SHIPPED a working title state once — its call
   sequence is ground truth.
4. **Read the decomp end-to-end** (sister-file rule, §4.1/4.2).
5. **Inspect the asset binaries** (§4.3). View .gif/.SPR/.ATL via the
   Read tool BEFORE writing loader/draw code.
6. **Update the plan + status docs FIRST**. Code follows the plan.
7. **Add the data-driven failing gate FIRST** (§4.7 + v2 QA-iterative
   rule). The gate uses quantitative measurement — centroid coords,
   pixel-mass, SSIM, byte-diff, RMS — never impressionistic "looks
   right". Watch the gate fire RED on the current build. Then
   implement. Then watch it turn GREEN. Only then claim done.
8. **Implement.** Mechanical port; cite source file+line for every
   translation decision.
9. **Run `pwsh tools/verify_done.ps1`** before claiming done. Exit-zero
   mandatory.

If steps 1-5 aren't visible in the transcript before the first code
change, the agent is guessing. The user has explicit standing authority
to call this out and the agent MUST restart with the methodology.

This rule was binding-mandated 2026-05-26 (Phase 1.3 trial-and-error)
and reinforced 2026-05-27 after Phase 1.23 again shipped guesses
instead of doc-catalogue-driven, measurement-first work. The "read
EVERY applicable doc, not the first match" rule and the "data-driven
measurement before hypothesis" rule are now binding skill-wide and
project-wide.

### 4.0.1 — Sub-agent dispatch requirement (v3, BINDING, 2026-05-27)

**EVERY `Agent` tool call dispatched during a Sonic-Mania-Saturn
session — whether for title work, GHZ work, build pipeline, QA, or
anything else touching this project — MUST embed the
`sega-saturn-developer` skill-invocation directive in its prompt.** No
exceptions. This rule was added because Phase 1.4-1.15 (15 trial-and-
error BSS-overflow iterations), Phase 1.23 (coord-system guess that
turned out wrong), and Phase 1.24 (entity-driven path produced worse
visuals than direct-draw) all involved sub-agents working in isolation
from the skill's complete-doc-index + RED-gate-first discipline.

**Mandatory pre-amble for every Agent prompt (paste verbatim):**

> **Skill binding:** Before any other work, invoke the
> `sega-saturn-developer` skill (or follow its
> `binding-session-methodology.md` directly if you can't activate the
> skill yourself). Read
> `C:\Users\gary\.claude\skills\sega-saturn-developer\references\complete-doc-index.md`
> end-to-end. Catalogue every applicable doc + sample for your
> subsystem. Read all of them, not the first match. Measure
> quantitatively before hypothesizing. Write the RED-firing gate BEFORE
> the fix.

**Primary-agent self-check before any Agent dispatch:**

Verify the prompt-being-constructed contains:
- The literal string `complete-doc-index.md`
- The literal string `RED-firing gate`
- A scope-specific reference to the docs/samples the sub-agent will need

If any of those three is missing, the primary agent has skipped this
requirement and must add them before calling `Agent`.

Mirrored at:
- `C:\Users\gary\.claude\skills\sega-saturn-developer\skill.md` §"SUB-AGENT DISPATCH REQUIREMENT" (v2.5.0+)
- `C:\Users\gary\.claude\projects\D--sonicmaniasaturn\memory\binding-session-methodology.md` v3

### 4.1 Read the decomp file you're porting

Cache it under `tools/_decomp_raw/` if it isn't already. Read the file
top to bottom. Identify:
- The struct fields the entity owns (its `Entity*` and `Object*` types)
- Every function (Update, LateUpdate, StaticUpdate, Draw, Create, StageLoad)
- Every `RSDK.*` call the file makes
- Every constant / table the file defines

### 4.2 Read the sister files

If the file is in `Objects/Title/`, read EVERY file in `Objects/Title/`.
If it's `GHZ/`, read every file in `GHZ/`. If it references another
object (e.g. `Music_PlayTrack`), cache and read `Objects/Global/Music.c`
too.

This rule comes from a 2026-05-26 failure where I read one file in
isolation and shipped the wrong sprite. The decomp is a graph; read the
neighborhood, not just one node.

### 4.3 Inspect the assets

If the feature draws sprites, **view the source GIF/atlas as an image
via the `Read` tool**. If it loads a palette, dump the palette to RGB
and view it. If it loads a scene, run `tools/dump_all_scenes.py` to
inspect the entity table.

Asset inspection is mandatory because the decomp tells you what
behaviors run, not what pixels look like.

### 4.4 Find or create a PC reference image

`tools/refs/mania_pc/` is the reference image library. For the feature
being ported, there must be at least one PC Steam Mania screenshot the
Saturn output will be diffed against. If one doesn't exist, source it
(authoritative sources: Internet Archive, Spriters Resource, Sonic Wiki
Zone, official Sega press kits) and check it in.

Current refs:
- `tools/refs/mania_pc/title_full_archiveorg.jpg` — settled title screen
  (Internet Archive, Sonic Team)

### 4.5 Write the feature checklist

Create `docs/feature_checklists/<feature>.md` with:
- Decomp file + line ranges being ported
- Asset files used
- Reference image path
- Visual gate threshold (SSIM target, mask region)
- List of `src/rsdk/` APIs required (call out any that don't exist yet —
  must be implemented before the feature port)
- Files to be created/modified
- **PROACTIVE-DETECTION-CHECKLIST outputs (mandatory per §4.5.1):**
  - Audit 1 — Layering/Z-order table
  - Audit 2 — Animation cadence table
  - Audit 3 — Pivot+flip composite math (per flipped sprite)
  - Audit 4 — Boot-delay budget delta

### 4.5.1 PROACTIVE-DETECTION-CHECKLIST (v1, BINDING, 2026-05-27)

Per `memory/qa-iterative-improvement.md` v3 (user-mandated after the
Phase 1.27→1.28 hand-back). Every visible-sprite feature port MUST
include these four audit outputs in its feature checklist BEFORE any
draw call lands in `src/mania/Game.c` or the entity-driven Draw
callbacks. If the checklist doesn't contain audit 1-4 results, the
port is unplanned — do not proceed.

**Audit 1 — Layering/Z-order audit (BEFORE writing draw calls):**

- Parse the decomp scene .bin via `tools/parse_title_entities.py`
  (or equivalent for non-title scenes — write the parser if it
  doesn't exist).
- Output the slot order of every entity in the feature's drawGroup.
- Smaller slot index = drawn first = drawn BEHIND. Build a Z-table
  where smaller Z = closer to viewer (Saturn perspective projection
  per SGL ST-238-R1 §slDispSprite). Each entity's Z must follow
  its slot rank inverted: back entities get LARGER Z.
- Document the slot-to-Z mapping in the checklist BEFORE the draw
  call lands. Cite the parser output.

**Audit 2 — Animation cadence audit (BEFORE writing walker):**

- For every animated sprite, run `convert_ring_sprite.parse_spr` on
  the source .bin.
- Extract `speed`, `loop_index`, AND per-frame `duration` values.
- Compute total cycle length: sum(duration) / speed = ticks per loop
  at 60 Hz.
- The Saturn walker MUST iterate by accumulating per-frame durations
  from the loaded metadata table (`g_tsonic_frames[].duration` or
  equivalent). NOT a uniform tick-per-frame counter.
- Verify the Saturn walker total cycle matches the decomp within 5%.
- Document the math in the checklist.

**Audit 3 — Pivot+flip composite audit (BEFORE composing flipped sprites):**

- For any parent-with-child sprite pair where the parent is flipped,
  parse BOTH pivots from the .bin.
- Verify the RSDK FLIP_X position formula is applied PER SPRITE:
  `world_topleft_x = entity_x - pivot_x - width`
  (per `_RSDKv5_Graphics_Drawing.cpp` `DrawSpriteFlipped`).
- VDP1 HF (PMOD bit 4 per ST-013-R3 §5.5.4) mirrors PIXELS around
  the sprite's framebuffer bbox center. The sprite's WORLD POSITION
  still comes from the RSDK formula above.
- Two flipped sprites at the same entity DO NOT necessarily mirror
  around a common axis — each mirrors around the entity origin via
  its own pivot. The composite changes their relative geometry.
- Document the per-sprite RSDK position math in the checklist.

**Audit 4 — Boot-delay budget audit (BEFORE adding asset to engine_init):**

- For each asset added to the synchronous boot path
  (`mania_engine_init`, `setup_title_assets`, `entities_load_assets`,
  etc.), record: `file_size_KB`, `gfs_seek_count`,
  `expected_load_time_sec`.
- Compute `expected_load_time_sec = file_size_KB / 150 + gfs_seek_count
  * 0.1`. (Mednafen 1x emulated CD = 150 KB/s; GFS_Seek = ~100ms each.)
- Cumulative boot-path total must stay UNDER 5 seconds.
- If over: defer the asset to async load (during State_Wait, scene
  transition, or background fill) OR rebuild the asset to strip
  unused frames/pixels (e.g. selective-frame atlas rebuild).
- Document the budget table in the checklist BEFORE the load call
  lands.

**Why this exists:** the Phase 1.27 ship had four bug classes the
user had to flag (finger overlay misaligned, banner Z reversed,
animation cadence flat/too-fast, ~10s boot delay) that the prior
methodology did NOT catch proactively. v3 of
`memory/qa-iterative-improvement.md` makes this the binding
proactive-detection layer.

### 4.6 Implement

Port the decomp file line-by-line into `src/mania/Objects/<Cat>/<Obj>.c`.
The Saturn file's function signatures MIRROR the decomp's (e.g.
`void TitleSonic_Update(void)`). The body of each function is a
mechanical translation using `src/rsdk/` APIs. Add `// FIXME:` markers
ONLY where the translation isn't fully mechanical (e.g. an RSDK API
isn't fully implemented yet).

### 4.7 Add the gate FIRST, watch it FAIL, then fix

The visual-fidelity gate gets added to `verify_done.ps1` BEFORE the
implementation lands cleanly. Watch it fire RED on the current build.
Then implement. Then watch it turn GREEN. This is the
`memory/qa-iterative-improvement.md` rule applied to visual fidelity.

### 4.8 Run verify_done

`pwsh tools/verify_done.ps1` must exit 0 before declaring the feature
done. Per `memory/qa-hard-rule-before-claim-done.md`, this is binding.

---

## 5. What to throw away when porting

The codebase has lots of pre-decomp hand-rolled code. Per the user
directive, replace it with decomp ports wherever a decomp counterpart
exists.

| Custom file | Decomp counterpart | Action |
|---|---|---|
| `src/main.c` (1965 lines: hand-rolled title state, GHZ stage loop, signpost/tally state machines) | `Game.c` + `TitleSetup.c` + `GHZSetup.c` + `SignPost.c` + `TitleCard.c` (all cached or available via gh api) | **REPLACE.** New `main.c` becomes a minimal boot stub that calls `mania_engine_init() → mania_load_scene("Title") → jo_core_run()`. |
| `src/player.c` + `src/physics.h` (hand-rolled Sonic 1/2-style physics) | `Objects/Global/Player.c` (already cached) | **REPLACE.** Decomp Player has spindash, peelout, drop dash, proper jump/roll/air states, slope physics. |
| `src/save.c` (custom) | `Objects/Global/SaveGame.c` (cached) | **REPLACE.** |
| `src/title_sonic.c` (hand-rolled compound sprite; current build draws only the body, no finger wave — user's "wrong sonic" complaint) | `Objects/Title/TitleSonic.c` (cached) | **REPLACE.** Decomp drives body anim + finger overlay correctly. |
| `src/intro_video.c` (empty stub) | `TitleSetup_State_FadeToVideo` (in cached TitleSetup.c) | **REPLACE** when SBL Cinepak audio path lands. |
| `cd/MSONIC.SPR`, `cd/MLOGO.SPR`, etc. (static single-frame extracts) | `Title/Logo.bin`, `Title/Sonic.bin` (animations) | **REPLACE** with full atlases built by `tools/build_*_atlas.py` (TSONIC.ATL already exists; TLOGO.ATL needs building). |

**Keep:**
- `src/rsdk/*` — this is the engine-compat layer, the Saturn-side
  translation of RSDK APIs. The decomp-ported game code targets this
  surface.
- `jo-engine/` — Saturn HAL. Modify minimally (only when an extension
  is strictly necessary — e.g. `jo_sprite_add_4bits_image` was added
  for the 4-bpp atlas; mirrors `jo_sprite_add_8bits_image` exactly).

---

## 6. Failure modes I (the agent) demonstrated and must not repeat

These come from the 2026-05-26 session where I shipped the wrong Sonic
sprite. The user's pushback is itself a QA mechanism this project
shouldn't depend on. The mitigations below are what's now binding.

### 6.1 Shipping "looks close" instead of "matches reference"

**Symptom:** I declared TitleSonic done because qa_gate.png "showed
Sonic in the ring." The reference shows a large drawn Sonic with a
waving finger; I shipped a small head sprite with no finger.

**Mitigation:** Step 4.4 (require a PC reference image). The visual
gate diffs Saturn output against the reference. If SSIM is below
threshold, the feature is NOT done regardless of how it "looks."

### 6.2 Reading one decomp file in isolation

**Symptom:** I read `TitleSonic.c` and skipped `TitleLogo.c`,
`TitleSetup.c`, `TitleBG.c`. Result: I implemented one of four required
behaviors and called it done.

**Mitigation:** Step 4.2 (sister-file rule). For any feature in
`Objects/<Cat>/`, every file in that directory gets read first.

### 6.3 Code-reading without asset-inspection

**Symptom:** I read `TitleSonic.c` lines saying "anim 0 frame 48 is
the loop target" and shipped frame 48 as "the iconic pose" without
ever opening `Title/Sonic.gif` to see what frame 48 actually depicts
(a small head crop, not the iconic large drawn pose).

**Mitigation:** Step 4.3 (asset inspection mandatory). View source
GIFs/atlases via Read tool before writing extraction or loader code.

### 6.4 Treating verify_done.ps1 as a correctness proof

**Symptom:** All 9-11 file-structure gates green. Build "works."
But the visible output does not match Mania.

**Mitigation:** verify_done.ps1 file-structure gates check for the
ABSENCE of regressions. Visual fidelity gates (`Gate Vn`) check for
the PRESENCE of correct game features. Every feature gets its own
`Vn` gate with SSIM threshold against the PC reference image.

### 6.5 Unilateral deferrals

**Symptom:** I deferred Cinepak as "Phase Z" without consulting the
user, after the user explicitly said the intro is part of the PC
Steam experience they want.

**Mitigation:** No more unilateral deferrals. Anything I can't ship
in one iteration becomes a one-page brief with three paths + effort
estimates, presented for user direction.

### 6.6 Hand-rolling instead of porting

**Symptom:** I built `src/main.c` with a hand-coded `STATE_TITLE`
linear timer instead of porting `TitleSetup.c`'s state machine.
Result: title state diverged from the decomp in 50 small ways
(timing, layer order, missing flash, missing electricity arc,
missing animated BG, no auto-advance-to-video).

**Mitigation:** This file's §5 — replace custom with decomp port
wherever a decomp counterpart exists. The user has full authority
to call out remaining hand-rolled regions; I'll port them.

---

## 7. Working pattern across sessions

Each session should:

1. Read `CLAUDE.md` (this file) and `BIBLE.md`
2. Read `docs/decomp_port_status.md` (the running progress table)
3. Identify the next port from the priority order in `BIBLE.md`
4. Execute the §4 methodology for that one port
5. Update `docs/decomp_port_status.md`
6. Commit with a message like `port(title-sonic): finger-wave overlay
   per decomp TitleSonic.c:18-19,35-36`

**Do not ship multiple ports in one iteration.** One port → one gate
→ one user checkpoint. The frame-to-frame parity goal is multi-year
work; piecemeal shipping with the user as architect is the only
sustainable cadence.

---

## 8. Key project documents

| Document | Purpose |
|---|---|
| `CLAUDE.md` (this) | Entry-point methodology; binding rules |
| `BIBLE.md` | Phase A/B/Z architecture; complete-game scope |
| `HANDOFF.md` | Per-session state (what just shipped, what's next) |
| `QA.md` | verify_done.ps1 gate catalog and rationale |
| `docs/RSDK_TO_SATURN_API_MAP.md` | Every `RSDK.*` API → Saturn equivalent |
| `docs/FRAME_TO_FRAME_PARITY_PLAN.md` | Full-scope plan; phasing |
| `docs/WHAT_I_GOT_WRONG.md` | 2026-05-26 honest accounting of methodology failures |
| `docs/mania_decomp_catalog.md` | Per-scene, per-object decomp inventory |
| `docs/rsdkv5_engine_catalog.md` | Engine subsystems + Saturn mapping |
| `docs/title_ground_truth.md` | Title scene entity coords (from Scene.bin) |
| `docs/scene_objects.json` | Per-stage entity tables (Scene.bin dumps) |
| `tools/_decomp_raw/` | Cached decomp source files |
| `tools/refs/mania_pc/` | PC Steam Mania reference images |
| `tools/verify_done.ps1` | Mandatory pre-claim-done gate |
| `tools/README_debugger.md` | Mednafen savestate QA harness reference card (Phase 1.30) |
| `tools/mcs_extract.py` | Parse `.mc0`/`.mcs` Mednafen savestate; peek any Saturn address |
| `tools/qa_savestate.ps1` | Boot + F5-save + retrieve `.mc0` + pipe through `mcs_extract` |
| `tools/qa_register_gate.py` | JSON-driven register/memory contract gate (verify_done sub-gate) |
| `tools/qa_register_baseline.json` | Don't-regress register contract (title_settled checkpoint) |
| `tools/qa_watch.py` | Cross-state byte delta watcher |
| `samples/qa_title_settled.mcs` | Reference settled-title savestate (baseline anchor) |

---

## 8.5 Mednafen savestate QA harness (Phase 1.30 — BINDING)

For every register-level or memory-level diagnostic question (SPCTL,
TVMR, RAMCTL, VDP1 PTMR/EDSR/LOPR, SCU IST/IMS, BSS overflow, SH-2 PC,
slave-CPU started, VRAM/CRAM contents, "did this value change between
frame A and B"), the **Mednafen savestate harness is the PRIMARY
diagnostic.** Pixel-based capture / SSIM QA is the secondary sanity
check. Tools live in `tools/` and are documented in
`tools/README_debugger.md`.

Quick-reference:

| Tool | Use when |
|---|---|
| `python tools/mcs_extract.py state.mcs --peek16 0x25F800E0` | Peek any Saturn address from a captured state. |
| `pwsh tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 18 -Out state.mcs` | Capture a fresh state from the just-built ISO. |
| `python tools/qa_register_gate.py state.mcs tools/qa_register_baseline.json` | Run the don't-regress register contract gate. |
| `python tools/qa_watch.py a.mcs b.mcs --region vdp2-regs --start 0x25F800E0 --len 0x20` | Diff two states to localise which bytes changed. |

`verify_done.ps1` Gate V-REG runs the register contract gate against a
fresh capture at the end of every build. New register-clobber-class
regressions MUST add an assertion to `tools/qa_register_baseline.json`
(with a `_comment` rationale) so the same bug class is caught
automatically thereafter — mirrors the existing `qa-iterative-improvement`
rule for pixel gates.

Historical record this harness exists to prevent:

- Phase 1.4-1.15: 15-iteration BSS-overflow bisect via screenshot-only QA.
- Phase 2.3b-e: 5-iteration SPCTL hypothesis chain via screenshot-only QA.
- Phase 1.23-1.25: 3-iteration entity-draw goose chase via screenshot-only QA.

All three were 1-2-iteration problems with savestate inspection.

**Mednafen 1.32.1 CLI-debug-flag gap (Phase 1.30 finding):** Mednafen
1.32.1 does NOT support `-ss.dbg_mask` or `-ss.dbg_break_on_unknown_io`
CLI flags. The interactive debugger (Alt-D) is available but requires
UI automation; user approval needed before scheduling that. Savestate
inspection covers every register/memory diagnostic in the meantime —
see `tools/README_debugger.md` § "Documented gap".

---

## 9. Binding memory rules (already in user's auto-memory)

These apply unchanged:

- `qa-hard-rule-before-claim-done.md` — `verify_done.ps1` exit 0 mandatory
- `qa-iterative-improvement.md` — every user-reported bug adds a new gate
- `qa-capture-must-start-from-bios.md` — Wait 2, Every 0.25, Shots 120
- `title-needs-15s-load-before-capture.md` — BINDING: the title needs
  >=15s of emulated boot before content renders. NEVER capture/QA the
  title below Wait=20 (default 24). A blue/SEGA-splash capture means
  "captured too early," NOT "title regressed." `tools/qa_gate.ps1`
  now HARD-ERRORS (exit 2) below the 20s floor so this cannot be
  forgotten.
- `rule-read-dts-before-coding.md` — DTS docs before any HW-register code
- `test-iteration-cleanup-discipline.md` — clean diagnostic files at exit
- `jo-pool-stale-core-o-gotcha.md` — `rm jo *.o` after pool-size change
- `jo-cram-off-by-one-shift.md` — pre-shift palettes UP by 1 on disk
- `sgl-audio-vs-scroll-cpu-dma-conflict.md` — slDMAXCopy for vblank pages
- `saturn-vdp2-streaming-solved.md` — slDMACopy in vblank for cell scroll
- `rsdk-decoration-tile-skip.md` — TileConfig decoration handling
- `qa-gate-stale-golden-trap.md` — refresh golden when title visuals change
- `bgm-loops-hand-curated.md` — tools/loops.json must cover every BGM
- `saturn-cdda-cue-format.md` — MODE1/2048 in multi-track CUE
- `rsdk-extract-mania-naming-conventions.md` — hash-matching .rsdk paths
- `sync-load-eliminates-cross-tu-volatile.md` — Phase 2.3j (2026-05-28) BINDING: NEVER introduce a cross-translation-unit `volatile bool g_xxx_ready` flag that gates per-frame ticks behind a "load complete" condition. GCC 8.2 LTO whole-program internalization breaks volatile semantics for cross-TU readers (`*.lto_priv.NNN`). Mirror the decomp's synchronous LoadScene chain instead. Before introducing any "deferred load" temptation: FIRST verify the decomp doesn't run it synchronously. The bias is sync. Phase 2.3f/g/h/i bisected this bug class 4 times before Phase 2.3j retired it architecturally.

---

## 10. Hard "do not" list

- Do not ship a feature without its `Vn` visual-fidelity gate
- Do not invent game logic — if the decomp has it, port it
- Do not modify `jo-engine/Compiler/COMMON/` (SGL toolchain)
- Do not modify `jo-engine/jo_engine/*.c` unless adding a strictly
  necessary extension (and document it inline with comment + cite)
- Do not bulk-clone decomp repos into the build; cache selectively
- Do not commit secrets / credentials (none expected on this project)
- Do not skip the sister-file rule when reading decomp
- Do not declare done without `verify_done.ps1` exit 0
- Do not use emoji in code, file contents, or documentation
- Do not bypass user approval for unilateral deferrals
- Do not write report/summary/findings/analysis .md files unless the
  user explicitly asks (return findings as chat output, not files)
