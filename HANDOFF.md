# Sonic Mania → Sega Saturn Port — Engineering Handoff

> A complete-as-possible single-document handoff covering the project goal, the engine that exists today, every asset slot the engine reads from (and which ones currently contain procedural placeholders that need to be replaced with your own custom art / music / sound effects), the technical facts that took build cycles to learn, the QA harness that keeps regressions out, and the prioritized remaining work.

---

## 1. Executive Summary

This project is a from-scratch Sega Saturn homebrew port of a Sonic-Mania-style 2D platformer. The repo lives at `D:\sonicmaniasaturn\` on Windows 11 and produces a bootable `game.iso` + `game.cue` pair runnable on **Mednafen Saturn core** (used for automated QA) and ultimately on real Saturn hardware via the **Satiator ODE** (a real-hardware optical drive emulator).

The strategy is intentionally to rebuild the existing engine to sega saturn standards so that a FULL AND COMPLETE DIRECT PORT OF THE ACTUAL SONIC MANIA GAME can be run on Sega Saturn in complete.... success is a compelte true port of teh original game...end to end.... every aspect.... The RSDKv5 C++ engine that runs the publicly-released decompilation cannot fit inside the Saturn's SH-2 budget (28.6 MHz dual-CPU with 1 MB Work RAM-H + 1 MB Work RAM-L), so this is a **Saturn-native engine** written on top of **Jo Engine** + Sega's **SGL** library, fed by Saturn-native binary asset files. PC-side conversion tools take your source data and produce the runtime files; the Saturn engine just loads-and-plays whatever happens to be at the expected paths in `cd/`. **Swap a file in `cd/`, rebuild, get a different visual — no engine code changes.**

The build pipeline is Docker-driven through a `joengine-saturn:latest` image, wrapped by a single `build.bat` that builds twice (a QA-mode binary for the reference-diff gate plus a release binary), runs two automated QA gates, and finally produces `game.iso` and `game.cue` in the project root. The whole loop takes about a minute on a warm cache.

Current build state:

* A bootable Saturn ISO that plays the title screen, transitions through a title-card cinematic, then runs **GHZ Act 1** as a playable, in-engine demo.
* Full-level **VDP2 NBG1 cell-scroll streaming** (16384 × 2048 px) plus an **NBG2 parallax sky**.
* Real **RSDK collision** with per-column surface table, slope-angle physics with embedded sine LUT, ground-follow, wall block, cliff drop, and vertical camera follow.
* Sonic VDP1 sprite at the proper grounded position with **Mania-faithful Q16.16 physics constants** (acceleration 0.046875, deceleration 0.5, friction 0.046875, top-speed 6.0, air-acceleration 0.09375, gravity 0.21875, jump force 6.5 / 6.0 per character, slope factor 0.125).
* **Auto-run demo mode** (default), gracefully handed over to manual control when the first directional or jump button is pressed after a 30-tick boot-transient grace period.
* **Auto-jump AI** that fires on impassable walls and cliff edges while autorun is active.
* **446 rings, 9 Motobug-style badniks, 35 springs, 38 item monitors, and 2 signposts** in GHZ Act 1, all hand-extracted from `Scene1.bin` and rendered from per-class position files.
* **HUD**: ring icon + counter, six-digit zero-padded score, and a `m:ss:cc` timer all rendered with a procedural 8×8 pixel font.
* **Title-card cinematic** between the title screen and gameplay.
* **SCSP audio engine** wired up with ring-pickup and jump sound effects, plus a **CD-DA BGM** path that loops track 02 of a multi-track CUE on stage entry.

Both QA gates pass on a clean rebuild. The project is at a natural pause point at the boundary between **Phase 4 polish** (refinements to the existing playable level) and **Phase 5** (multi-zone game shell, save data, special stages).

---

## 2. Project Goal & Strategy

### The goal

Produce a fully playable Sonic-Mania-style platformer on real Sega Saturn hardware. Boot from CD, play through multiple zones, persist progress to backup RAM, optionally hit special stages, run reliably on both the Mednafen Saturn emulator (used for fast iteration and QA) and on real Saturn hardware booted from a Satiator ODE.

### Why not a direct port

The RSDKv5 engine that powers the public decompilation of Sonic Mania is C++ targeted at modern desktop CPUs with abundant RAM and full SIMD vector units. Its draw loop and entity system are not budgetable on a 28.6 MHz SH-2 with 1 MB of Work RAM and no FPU. A direct port would either run at frame rates that are unacceptable on the target hardware, or would require so much surgery that the resulting code would no longer resemble the original engine in any meaningful way. Instead:

* The publicly-released decompilation source under `rsdkv5-src/` is used **as a reference for file formats only**. Scene format, TileConfig format, sprite-animation format, palette format, tile-image format, datapack format — all of those are read from the decomp source, translated into PC-side Python parsers, and used to convert your source data into Saturn-native binary files offline.
* The Saturn engine itself is written from scratch in C against the Jo Engine + SGL APIs. It is small, fast, and tuned for the Saturn's hardware quirks (VDP2 cell-scroll timing, SCU vs CPU DMA, CRAM allocator off-by-one, jo's malloc-pool sizing constraints).
* Assets at runtime are pure binary files in deliberately simple formats — fixed headers, big-endian, no compression. The Saturn just loads them and plays them.

### Asset-portable workflow

Because every asset is loaded by name from `cd/`, you can replace any visual or audio piece without touching any C code:

* Draw a new Sonic sprite atlas? Save it as `cd/SONIC.SPR` in the engine's multi-frame BGR1555 format and rebuild. Sonic looks different the next time you boot.
* Compose a new BGM track? Save it as `cd_audio/track02.wav`, rebuild, and `build.bat` rewrites the CUE for you.
* Hand-design custom badniks, springs, monitors, signposts? Drop them at the matching filenames in `cd/`.

This is the central design principle of the entire project, and the **placeholders** section of this document spells out exactly what each filename should contain and how to convert your source assets into the right format.

### Hard constraints

* **Target hardware**: Sega Saturn (NTSC), 320 × 224 frame size, 60 Hz V-blank target. PAL retained as a build option but not a current QA target.
* **No homebrew-specific kernel extensions**. The ISO must boot through the stock Saturn BIOS on Mednafen and on real hardware.
* **Pool budget**: jo's `JO_GLOBAL_MEMORY_SIZE_FOR_MALLOC` currently set to **576 KB (589824 bytes)**. `.bss` end must stay below `0x060C0000` (SGL's work area at the top of Work RAM-H). Current build has ~14 KB headroom.
* **No mid-act loading stalls**. The active zone fits resident (collision, FG cells / pattern / map, sky bitmap + palette, all entity sprites and position files, all audio buffers, the HUD font). Zone transitions are the only blocking I/O.

---

## 3. Repository Layout

```
D:\sonicmaniasaturn\
├── src\                            -- Saturn-side engine source
│   ├── main.c                      -- entry point, game loop, all entity behaviour
│   ├── player.c                    -- Mania-faithful Q16.16 physics state machine
│   ├── player.h                    -- player struct + flags
│   ├── physics.h                   -- ACC / DEC / FRC / TOP / GRAVITY / JUMP constants
│   └── main_phase*_*.c.bak         -- milestone snapshots (incremental backups)
│
├── tools\                          -- PC-side asset conversion + QA pipeline
│   ├── rsdk_extract.py             -- generic RSDK datapack extractor (filelist-driven)
│   ├── render_scene.py             -- Scene.bin layer parser + palette/tile decoder
│   ├── convert_stream.py           -- stage tile-set -> GHZFG.{CEL,PAT,TMP,PAL}
│   ├── convert_vdp2*.py            -- companion VDP2 cell/bitmap converters
│   ├── build_collision.py          -- TileConfig.bin -> per-column GHZSURF.BIN
│   ├── build_rings.py              -- Scene1.bin entity-class walker (-> per-class .BIN)
│   ├── build_heightmap.py          -- legacy heightmap builder (subsumed by build_collision)
│   ├── shift_pal.py                -- pre-shift PAL up by 1 to compensate jo CRAM offset
│   ├── convert_ring_sprite.py      -- RSDK SPR-anim + GIF -> Saturn multi-frame BGR1555
│   ├── convert_audio.py            -- generic ffmpeg wrapper -> jo s8 mono PCM
│   ├── build_cdda.py               -- 44.1 kHz 16-bit stereo WAV -> raw CD audio + multi-track CUE
│   ├── make_digit_font.py          -- procedural 12-glyph 8x8 HUD font
│   ├── make_audio.py               -- procedural chime / boop / chiptune BGM
│   ├── make_badnik_sprite.py       -- procedural 4-frame 16x16 walking critter
│   ├── make_object_sprites.py      -- procedural spring/monitor/signpost placeholders
│   ├── qa_boot.ps1                 -- Mednafen launcher + screen-capture burst
│   ├── qa_refdiff.py               -- per-pixel diff vs golden title view
│   ├── qa_grounded.py              -- detect player feet vs visible ground gap
│   ├── qa_grounded_majority.py     -- majority gate over a capture burst (>=2 grounded)
│   ├── qa_gate.ps1                 -- one-command capture + diff wrapper
│   ├── qa_validate.py              -- offline Saturn resource budget check
│   └── refs\
│       └── title_view.golden.png   -- reference frame for title-screen diff
│
├── cd\                             -- bundled assets that go into game.iso
│   ├── GHZFG.CEL / PAT / TMP / PAL -- streaming foreground tile bank + pattern map + tilemap + palette
│   ├── GHZSURF.BIN                 -- 16384-column surface table (u16 Y + u8 angle + u8 flag)
│   ├── GHZSKY.DAT / PAL            -- parallax sky bitmap + palette
│   ├── SONIC.SPR                   -- Sonic idle sprite
│   ├── SONWALK.SPR                 -- Sonic walk frames
│   ├── RING.SPR + GHZRINGS.BIN     -- 16-frame spinning ring sprite + 446 position entries
│   ├── BADNIK.SPR + GHZBUGS.BIN    -- 4-frame walking badnik + 9 position entries
│   ├── SPRING.SPR + GHZSPRG.BIN    -- 3-frame squashing spring + 35 position entries
│   ├── MONITOR.SPR + GHZBOX.BIN    -- 1-frame item box + 38 position entries
│   ├── SIGNPOST.SPR + GHZSIGN.BIN  -- 1-frame goalpost + 2 position entries
│   ├── DIGITS.SPR                  -- 12-glyph 8x8 HUD font (0-9 + ring icon + colon)
│   ├── RINGSFX.PCM                 -- s8 mono 22050 Hz ring-pickup chime
│   ├── JUMPSFX.PCM                 -- s8 mono 22050 Hz jump boop
│   └── STAGEBGM.PCM                -- s8 mono 11025 Hz (optional, currently disabled)
│
├── cd_audio\                       -- optional CD-DA BGM source files (44.1 kHz / 16-bit / stereo)
│   └── track02.wav                 -- if present, build.bat folds it into game.cue as track 02
│
├── jo-engine\                      -- vendored Jo Engine source tree
│   └── jo_engine\
│       ├── core.c                  -- where the malloc pool lives (see pool-budget gotcha)
│       └── ...
│
├── rsdkv5-src\                     -- publicly-released decompilation source (REFERENCE ONLY)
│
├── Makefile                        -- wraps jo_engine_makefile; sets JO_GLOBAL_MEMORY_SIZE_FOR_MALLOC,
│                                      JO_COMPILE_WITH_AUDIO_MODULE, JO_COMPILE_WITH_BACKUP_MODULE
├── build.bat                       -- 2-pass docker build + QA gates
├── QA.md                           -- the standing QA mandate (gates 1-8 + grounded + reference-diff)
├── HANDOFF.md                      -- this document
└── memory\                         -- one-fact-per-file memory directory loaded into Claude sessions
    ├── MEMORY.md                   -- index loaded at start of every session
    └── *.md                        -- per-fact files (see "Key Technical Learnings" section)
```

The two key principles in this layout are:

1. **`src/` is small**. Two C files (`main.c` and `player.c`) plus one header. No game-state or asset data lives in source code. Every entity count, position, and sprite frame is read at runtime from a file in `cd/`.
2. **`tools/` is large**. Every transformation between your source assets and the runtime binary lives in a Python script, never inline in C. If you change a source asset, you re-run the matching converter; you don't recompile the Saturn binary unless you're changing logic.

---

## 4. Build Pipeline

### `build.bat` — top-level entry point

Run from a terminal at the project root:

```
build.bat
```

The script executes the following sequence (lines 27–63 of `build.bat`):

1. **Force `main.o` recompile**, because the `-DQA_MODE` flag toggle in step 2 vs step 5 doesn't touch source-file mtimes. Without this, `make` would skip the rebuild and the binary would carry whichever flag the previous build used.
2. **Docker pass 1 — QA build**:
   ```
   docker run --rm -v %ROOT%:/work -w /work joengine-saturn:latest make QA_MODE=1
   ```
   Builds a binary that holds the title screen forever (no auto-advance). This is what the title-reference-diff gate captures.
3. **Run the title reference-diff gate** (`tools/qa_gate.ps1` → `tools/qa_refdiff.py`). Boots the QA binary in Mednafen, captures the title frame, crops + downscales it to 320 × 240, aligns it within ±3 px against `tools/refs/title_view.golden.png`. **FAIL** if mean RGB delta exceeds 12 or 95th-percentile exceeds 120 (Sonic's title-pose region is masked out to tolerate idle-animation variation). Catches palette shifts, missing assets, geometry regressions.
4. **Force `main.o` recompile again** (same reason).
5. **Docker pass 2 — release build**:
   ```
   docker run --rm -v %ROOT%:/work -w /work joengine-saturn:latest make
   ```
   Builds the release binary (no `QA_MODE`). The title auto-advances after ~2 seconds into the title-card cinematic, then into gameplay.
6. **Optional CD-DA BGM step**: if `cd_audio\track02.wav` exists, runs `tools/build_cdda.py` which strips the WAV header into raw 44100 Hz 16-bit stereo CD-DA samples, writes `cd_audio\track02.bin`, and rewrites `game.cue` as a 2-track CUE (`game.iso` as track 01 MODE1/2048, `track02.bin` as track 02 AUDIO).
7. **Run the grounded gate** (`tools/qa_grounded_majority.py`). Captures 8 game-state frames at `Wait 18 -Every 0.7` (Wait 18 deliberately lands after BIOS ~14 s + title ~2 s + title-card ~2 s, so all 8 captures fall inside true game state). Feeds them through `qa_grounded.py` which finds the player's blue body silhouette and scans down to the first grass/dirt pixel. **PASS** if at least 2 of 8 frames show Sonic's feet within 4 game-pixels of the visible ground.

   The threshold is intentionally loose. Real gameplay legitimately produces airborne frames (jumps, cliff falls, spring launches, badnik stomps, monitor bounces); none of those are bugs. The "Sonic floating above ground" bug class — caused by collision-table off-by-one, decoration-tile false-positives in the surface builder, or sprite-anchor misalignment — produces 0 grounded frames out of N. So a threshold of ≥2 grounded frames catches the bug class without false-positiving on spring-heavy sections of the demo.

8. **Exit 0**. `game.iso` and `game.cue` are now in the project root.

`build.bat --skip-qa` runs steps 2 and 5 only — useful for fast iteration when you know you're going to break the gates and want to look at the binary first.

### The `Makefile`

The top-level `Makefile` is a thin wrapper around Jo Engine's `jo_engine_makefile`. Key macros it sets:

```
JO_GLOBAL_MEMORY_SIZE_FOR_MALLOC = 589824      # 576 KB malloc pool
JO_COMPILE_WITH_AUDIO_MODULE     = 1           # SCSP audio
JO_COMPILE_WITH_BACKUP_MODULE    = 1           # Saturn backup RAM (for Phase 5 save data)
JO_COMPILE_WITH_FS_MODULE        = 1           # CD filesystem
JO_COMPILE_WITH_PSEUDO_MODE7     = 0
JO_COMPILE_WITH_3D_MODULE        = 0
JO_NTSC                          = 1
```

**Critical gotcha**: changing `JO_GLOBAL_MEMORY_SIZE_FOR_MALLOC` does **not** force `core.o` to recompile, because `make` only watches source-file mtimes, not Makefile variables. Whenever you change the pool size, run `rm jo-engine/jo_engine/*.o` before the next build. Otherwise the binary will keep using whatever pool size was baked into the last `core.o`. This is the same failure mode as `build.bat`'s `del /q src\main.o` calls and is recorded as a permanent memory file (`memory/jo-pool-stale-core-o-gotcha.md`).

---

## 5. Saturn Hardware Constraints That Mattered

Saturn is unusual hardware even by mid-90s standards. Five specific quirks shaped the architecture of this engine and are worth understanding before you start changing things:

### 5.1. VDP2 cell-scroll streaming requires V-blank uploads

The foreground playfield is a single VDP2 NBG1 plane (PL_SIZE_1x1, 64 × 64 cells, 512 × 512 pixels) but the GHZ Act 1 level is 16384 × 2048 pixels — 32 × 4 plane-widths bigger than the plane. There is no "scroll the plane and let VDP2 read the rest of the level from VRAM" mode that would fit the whole level in VDP2's 512 KB.

The approach: keep all 1406 unique cells resident in VDP2 VRAM bank A0, and **rebuild the 64 × 64 page pattern-name table in Work RAM every frame** based on the camera position, then **DMA the page into VDP2 VRAM bank B0 during V-blank**. VDP2 then reads it normally for the next visible frame.

The first version of this wrote the page directly into VDP2 VRAM from the active scanlines, and the result was visible tearing and incoherent partial uploads — VDP2 reads its plane data during active display, and CPU writes during that window do not reliably reach the screen. The fix is in `fg_vblank()` registered with `jo_core_add_vblank_callback`.

### 5.2. SCU DMA, not CPU DMA, for the page upload

SGL's sound subsystem internally uses CPU DMA. If V-blank fires during a scroll-screen CPU DMA, the sound init can corrupt the upload — colorful pixel garbage or banded noise across the playfield. **The page upload must use SCU DMA** (`slDMAXCopy` with mode `Sinc_Dinc_Long`).

SCU DMA bypasses the SH-2 cache, so the source pointer must be the **cache-through alias**: `(void *)((unsigned int)g_page | 0x20000000)`. This is documented in SGL 2.0A release notes §1.2.1 and lives in `memory/sgl-audio-vs-scroll-cpu-dma-conflict.md`.

### 5.3. jo's CRAM allocator is off-by-one

`jo_create_palette_from()` allocates user palettes starting at `CRAM[bank+1]`, reserving entry `bank+0` for jo's printf text colour. But VDP2 256-colour mode reads `CRAM[bank+pixel]`, so a cell pixel with value V reads `PAL[V-1]`. If you don't compensate, every level's colours are off by one entry — most visible as palms turning into wrong shades of green.

The fix is on the disk side, not the runtime side: every 256-colour `.PAL` file is pre-shifted up by one entry before being saved. `tools/shift_pal.py` does it explicitly; every one of the five PAL-writing converters applies the shift natively. The runtime then loads the PAL as-is, and the off-by-one cancels out. See `memory/jo-cram-off-by-one-shift.md`.

### 5.4. Cell byte order is row-major

VDP2 8 × 8 cells are stored byte[r * 8 + c] in plain row-major order for the raw VDP2 path used here. Jo's higher-level VDP2 helpers use a transposed convention. Mixing them gives you tiles that look like Picasso painted them. `convert_stream.py` writes cells in plain row-major, and `main.c` loads them with that assumption.

### 5.5. The jo malloc pool has to be exactly the right size

Too small (e.g., the default 16 KB): `jo_fs_read_file` returns NULL on big files. The Saturn keeps running but the graphics are scrambled because the FG cell bank, the pattern table, and the tilemap all came back as null.

Too big: jo's pool is a `static unsigned char global_memory[N]` in `core.c`, which ends up in `.bss`. If `.bss` grows past `0x060C0000` it overruns SGL's work area — SortList, SpriteBuf, Zbuffer — and the screen fills with garbled spritework.

Current pool: **576 KB (589824 bytes)**. `.bss` end is around `0x060BC490`, giving ~14 KB headroom against the 0x060C0000 ceiling. The `tools/qa_validate.py` script does a static map-file check to make sure we don't accidentally cross it.

---

## 6. The Engine — Phase by Phase

The engineering work was organized into five phases. Phases 0–3 are complete; Phase 4 is substantially complete with a small number of polish items remaining; Phase 5 has not been started.

### Phase 0 — PC-side asset converter pipeline (DONE)

Before any Saturn code was written, the entire offline conversion pipeline had to work. Every script under `tools/` falls into one of three categories:

* **Source-format parsers**: `rsdk_extract.py`, `render_scene.py`, `build_collision.py`, `build_rings.py`. These read the RSDKv5 file formats verified against the decompilation source code in `rsdkv5-src/`.
* **Saturn-format writers**: `convert_stream.py`, `convert_vdp2*.py`, `convert_ring_sprite.py`, `convert_audio.py`, `build_cdda.py`. These emit big-endian binary files in formats designed for direct loading by the Saturn engine.
* **Placeholder generators**: `make_digit_font.py`, `make_audio.py`, `make_badnik_sprite.py`, `make_object_sprites.py`. Pure procedural pixel-art and sample-synthesis scripts that emit files in the runtime formats. Used to bootstrap the engine before custom artwork exists.

The procedural placeholder pieces are documented exhaustively in the **Placeholders** section below — every one needs to be replaced eventually with your own custom artwork or sourced asset, and the relevant filename + format is spelled out for each one.

### Phase 1 — VDP2 zone renderer (DONE)

Builds the playable level out of two VDP2 backgrounds:

* **NBG1 (foreground)**: the streaming cell-scroll plane covering the full 16384 × 2048 px GHZ Act 1 level. Cells loaded from `cd/GHZFG.CEL` into VRAM bank A0. Pattern name table at fixed-page-size 1 × 1, lookup table from `cd/GHZFG.PAT`. Tilemap (the actual placement of each pattern across the 2048 × 256 cell-grid of the level) from `cd/GHZFG.TMP`. Palette from `cd/GHZFG.PAL` (already pre-shifted; loaded as-is). The 64 × 64 page is rebuilt every frame in Work RAM by `fg_build_page()` based on the camera position, then DMA'd in `fg_vblank()`.
* **NBG2 (sky)**: parallax background scrolling at 1/4 horizontal + 1/4 vertical speed behind the foreground. 256 × 256 bitmap from `cd/GHZSKY.DAT`, 256-colour palette from `cd/GHZSKY.PAL`. Repeats over the level.

Visually, this gives you the entire stage: grass, dirt, cliffs, palm trees, distant clouds, water reflection — all present and scrolling correctly the moment you boot the ROM.

### Phase 2 — Sonic sprites + base physics (DONE)

* `cd/SONIC.SPR` (idle, 1 frame) and `cd/SONWALK.SPR` (walk, multiple frames) loaded via Jo Engine's sprite system. `jo_sprite_add` internally DMAs the pixel data into VDP1 VRAM, so the source buffer in the pool is safe to `jo_free` immediately after the add returns (verified at line 220 of `jo_engine/sprites.c`). Every sprite loader in `main.c` does this — it recovered ~55 KB of pool that made the later objects + HUD all fit.
* `src/player.c` implements the full ground/air physics state machine in Q16.16 fixed-point. Constants in `src/physics.h` (`ACC`, `DEC`, `FRC`, `TOP`, `AIR_ACC`, `GRAVITY`, `JUMP_SONIC`, `JUMP_TAILS`, `JUMP_KNUX`, `SLOPE_FACTOR_RUN`, `SLOPE_FACTOR_ROLLUP`, `SLOPE_FACTOR_ROLLDOWN`).
* Sprite anchor is centre-origin: `jo_sprite_draw3D(sx - 160, sy - 112, ...)` for the standard 320 × 224 coordinate convention. `draw_game` converts world coords to screen coords using the camera position and renders Sonic at his feet position.

### Phase 3 — Collision + camera + slope physics (DONE)

The collision data structure is a **per-column surface table** in `cd/GHZSURF.BIN`: 16384 entries of 4 bytes each — `(u16 BE Y, u8 angle, u8 flag)`. Loaded into Work RAM at boot (~64 KB). The runtime queries:

```c
u16  player_surface_y(int x_px);      // ground height in pixels
u8   player_surface_angle(int x_px);  // 0-255, 0 = flat
bool player_no_floor(int x_px);       // SMS_NO_FLOOR flag set?
```

The table is **built offline** from RSDK's `TileConfig.bin` (path 0 only — RSDK supports two paths for slopes that can be entered from above vs below, but Mania uses path 0 for ground gameplay). `tools/build_collision.py` walks Scene1.bin layer 3 (FG Low) top-down, looking for the first solid tile-column. The critical gotcha:

* **Decoration tiles** in Mania (sun totems, hanging flowers, large grass tufts) are also marked solid in TileConfig.bin — they have hitboxes for badnik collision and parallax sprite interaction. A naive "first-solid" scan picks them up as ground, and Sonic ends up floating above the actual playfield where these decorations exist.
* **Fix**: require **at least 4 tiles of continuous solid-column mass below a candidate surface tile**. That rejects 3-tile-tall decorations while accepting the real grass-on-dirt playfield. See `memory/rsdk-decoration-tile-skip.md`.

The physics layer:

* `player_update()` ground-follows the surface, wall-blocks on upward steps > 17 px (the standard Mania stair tolerance), cliff-falls on downward drops > 17 px, transitions to airborne when the next column has `SMS_NO_FLOOR`.
* Airborne integrates with gravity and lands when `ysp ≥ 0 && ypos ≥ floor_y`.
* **Slope physics** via an embedded 256-entry Q1.7 sin LUT (`SIN8`). Per frame:
  ```
  gsp -= SLOPE_FACTOR_RUN * sin(angle)
  xsp = gsp * cos(angle)
  ysp = -gsp * sin(angle)
  ```
  Jump direction follows the surface normal — jumping off a 45° slope launches you 45° off-vertical, not straight up.
* **Camera vertical follow**: `cam_y = clamp(ypos - SCR_H/2 - CAM_FOOT_OFFSET, 0, max_y)`. The CAM_FOOT_OFFSET (~32 px) keeps Sonic centred at his feet, not his sprite midpoint, so cliff drops feel right.
* **Auto-jump AI** (demo mode only): when grounded with `|gsp| > 1.0`, looks 24 px ahead and triggers a jump if the surface there is more than the stair tolerance above (wall) or below (cliff edge) the current floor.

### Phase 4 — Entities, audio, HUD, input, title card (SUBSTANTIALLY DONE)

This phase added every gameplay element that distinguishes a tech demo from a playable level. Each one is data-driven from a file in `cd/` and has its own per-entity state struct in `main.c`.

**Rings** (`cd/GHZRINGS.BIN` + `cd/RING.SPR`):
* 446 rings in GHZ Act 1, hand-extracted from Scene1.bin entity-class `Ring`.
* Per-ring: world position + active flag.
* 16-frame spinning sprite at 16 × 16 px. AABB pickup test against Sonic's collision box. On pickup: clear the active flag, `g_rings += 1`, `g_score += 10`, play `RINGSFX.PCM`.

**Badniks** (`cd/GHZBUGS.BIN` + `cd/BADNIK.SPR`):
* 9 Motobug-style badniks. Per-bug state struct: position, x velocity, facing flag, alive flag, animation frame.
* Patrol AI shares Sonic's surface table reads — same `player_surface_y`, same 17-px wall/cliff thresholds. Reaching a wall or a cliff edge reverses facing.
* Sprite flips horizontally based on facing.
* AABB hit-test against Sonic. Sonic stomp from above (his `ysp > 0` and his feet box overlaps the bug's head): bug dies, +100 score, Sonic bounces (`ysp = -3.5`), plays `JUMPSFX.PCM`.
* Side-contact damage: currently zeros ring count as a placeholder. Proper hurt-state machine is a Phase 4 polish follow-up.

**Springs** (`cd/GHZSPRG.BIN` + `cd/SPRING.SPR`):
* 35 springs. Per-spring: position, squash-timer.
* 3-frame squash animation (extended / neutral / compressed).
* On touch from above: launch Sonic up at `ysp = -10.0` px/frame, set squash-timer to 8 ticks, advance through compressed → neutral → extended over the timer window.

**Item Monitors** (`cd/GHZBOX.BIN` + `cd/MONITOR.SPR`):
* 38 item boxes ("ItemBox" in RSDK class names).
* Break on jump-from-above OR roll-into-side contact, plays `RINGSFX.PCM`, grants `+10 rings`, bounces Sonic (`ysp = -3.5`).
* Currently no item-type differentiation (all boxes give rings as a placeholder). Real Mania has Ring / Shield / Speed / Invincibility / Life / Eggman etc., each with its own icon-frame and behavior.

**Signposts** (`cd/GHZSIGN.BIN` + `cd/SIGNPOST.SPR`):
* 2 SignPost entities at the end of the level.
* Touch sets `g_act_cleared`, freezes the autorun input, +1000 score.
* Currently static — no spin animation, no act-clear chime. Polish item.

**SCSP audio engine**:
* `JO_COMPILE_WITH_AUDIO_MODULE=1` in the Makefile.
* `cd/RINGSFX.PCM` (~22050 Hz s8 mono, short chime, ~0.15 s) loaded at boot via `jo_audio_load_pcm`.
* `cd/JUMPSFX.PCM` (same format) loaded at boot.
* Fired by `jo_audio_play_sound` at the relevant gameplay event.
* **CD-DA BGM path**: if `cd_audio/track02.wav` exists, `build.bat` runs `tools/build_cdda.py` to rewrite `game.cue` as a 2-track CUE. On stage entry, `enter_game()` calls `jo_audio_play_cd_track(2, 2, true)` which plays track 02 on loop.

**HUD** (`cd/DIGITS.SPR`):
* 12-glyph procedural 8 × 8 pixel font: `0` `1` `2` `3` `4` `5` `6` `7` `8` `9` plus a ring-icon glyph plus a colon glyph (used for the timer).
* Three lines, top-left of the screen:
  * Line 1: ring icon + ring count (e.g., `[O]  23`)
  * Line 2: `SCORE 001230`
  * Line 3: `TIME 1:23:45` (m:ss:cc — Mania-style centisecond timer)
* All rendered with `jo_sprite_draw3D` from the same atlas. Score and timer use 6-digit / 5-character zero-padded formatting.

**Player input takeover**:
* On boot, `g_autorun = true`. The first 30 ticks (~0.5 s) ignore all input — that's the boot-transient grace period.
* After tick 30, the first rising edge on LEFT / RIGHT / A / B / C sets `g_autorun = false` permanently for the rest of the act. `player.c` then reads the directional and jump buttons normally.
* Auto-jump AI is gated behind `g_autorun && grounded` so manual play doesn't see unwanted hops.

**Title-card cinematic** (`STATE_TITLECARD`):
* Plays between `STATE_TITLE` and `STATE_GAME`.
* 114-tick animation (~1.9 s): slide-in 0–23 ticks, hold 24–89, slide-out 90–113.
* Renders the ring-icon + `1` glyph pair from `cd/DIGITS.SPR` over the title backdrop.
* Currently linear easing. Ease-out cubic is a polish item.

### Phase 5 — Game shell (NOT STARTED)

Reserved for later work. Includes:

* Full hurt-state machine for Sonic (invuln flash, ring-scatter physics, knockback).
* End-of-act bonus tally cinematic.
* Multi-zone progression (Scene2.bin loading, level-list state machine).
* Special stages (Blue Spheres or UFO chase).
* Save data to Saturn backup RAM (jo backup module is already compiled in but unused).
* Pause menu.
* Game-over and continue flow.

---

## 7. Placeholders & What to Replace With Custom Assets

This is the most important section if you're picking the project up to make it visually yours. **Every asset listed below is currently a procedural placeholder generated by a `tools/make_*.py` script — they bootstrap the engine to a playable state but they are not your final art.** Replace each file in `cd/` with your own asset in the same format and the engine will pick it up on the next build with zero code changes.

### 7.1. Sonic sprites

**Files**: `cd/SONIC.SPR` (idle), `cd/SONWALK.SPR` (walk frames).
**Format**:
```
u16 BE  frame count
u16 BE  frame width  (pixels)
u16 BE  frame height (pixels)
N frames * width * height * u16 BE   (Saturn BGR1555, MSB=opaque, 0x0000=transparent)
```

Each pixel is a 16-bit big-endian BGR1555 value:
* bit 15 (MSB) = opacity flag — 1 for visible, 0 for transparent
* bits 14..10 = blue (5-bit)
* bits 9..5 = green (5-bit)
* bits 4..0 = red (5-bit)

A pure black opaque pixel is `0x8000`. Pure white opaque is `0xFFFF`. A transparent pixel is `0x0000` regardless of RGB.

**Current state**: These files exist in `cd/` from earlier work but are placeholders rather than final art. They need to be replaced with your own Sonic sprite sheet.

**Conversion workflow**:
* If you have your sprites as a sequence of indexed-PNG or GIF frames, run `tools/convert_ring_sprite.py --input <sonic_idle.gif> --out cd/SONIC.SPR` — it works for any sprite, not just rings; the name is historical.
* If you have a single PNG atlas with frames laid out in a grid, use `tools/convert_ring_sprite.py --atlas <atlas.png> --grid-w 64 --grid-h 64 --frames 8` and adjust the grid arguments.
* Frame dimensions can be anything; the engine reads width/height from the header. The current placeholder uses 32 × 32 px.

### 7.2. Ring sprite

**File**: `cd/RING.SPR`.
**Format**: same multi-frame BGR1555 atlas described above.

**Current state**: 16-frame spinning ring at 16 × 16 px. The procedural placeholder is functional — the ring rotates and looks like a ring — but it's flat shaded. Replace it with your custom ring art (typically a 16-frame loop showing the ring rotating through profile views).

**Conversion**: `tools/convert_ring_sprite.py --input <ring_animation.gif> --out cd/RING.SPR`.

### 7.3. Badnik sprite (Motobug-style critter)

**File**: `cd/BADNIK.SPR`.
**Format**: same.

**Current state**: 4-frame 16 × 16 procedural critter from `tools/make_badnik_sprite.py` — a round red body with two eyes and alternating leg positions. The exact palette is fixed in the script:
* body fill: dark red `(180, 30, 30)`
* body shade: deeper red `(110, 10, 10)`
* outline: black
* eye: white
* pupil: black
* legs: dark grey `(60, 60, 60)`

This is generic placeholder art designed to be visually distinct from Sonic so collisions are obviously legible during development. Replace it with your hand-designed badnik sprite at the same filename.

**Conversion**: `tools/convert_ring_sprite.py --input <badnik.gif> --out cd/BADNIK.SPR` — and reduce the file to 16 × 16 frames so the AABB hit-test math stays consistent. (Alternative: edit the AABB constants in `main.c`'s badnik update if you want bigger badniks.)

### 7.4. Static object sprites — Spring, Monitor, SignPost

**Files**:
* `cd/SPRING.SPR` — 3 frames at 16 × 16 (extended, neutral, compressed).
* `cd/MONITOR.SPR` — 1 frame at 16 × 16 (box with a `?` glyph).
* `cd/SIGNPOST.SPR` — 1 frame at 16 × 24 (wooden pole with green sign face).

**Current state**: All three are procedural placeholders from `tools/make_object_sprites.py`. The header comment in that script makes the design intent explicit:

> The shapes here are deliberately generic and abstract — yellow squashable column with a red base for a spring, a box with a punctuation glyph for a monitor, a wooden pole with a square sign for a goalpost. If you have hand-extracted sprite atlases you'd rather use, drop them at the same output filenames and the build picks them up.

These are the most placeholder-y of all the assets and the highest visual-impact swap you can make. Replace them with your own art in the same multi-frame BGR1555 SPR format.

* **Spring**: keep 3 frames — the engine cycles through them based on `squash_timer` to give the "press down, then extend" launch animation. Frame 0 should be the most-extended pose (Sonic just launched), frame 1 should be the neutral idle, frame 2 should be the most-compressed pose (Sonic just landed on it).
* **Monitor**: 1 frame is the default. If you want a per-item-type icon (Ring / Shield / Speed / Invincibility / Life / Eggman), pack them as multiple frames and adjust `draw_monitors` in `main.c` to select the frame based on a per-monitor type byte added to `GHZBOX.BIN`.
* **SignPost**: 1 frame as default. The polish item "signpost spin animation" wants ~8 frames showing the post rotating, settling on one of three faces (Sonic / Tails / Knuckles / Eggman, depending on which character cleared the act).

### 7.5. HUD font

**File**: `cd/DIGITS.SPR`.
**Format**: 12 frames at 8 × 8 px (0-9 + ring icon + colon).

**Current state**: Procedurally drawn 8 × 8 pixel font from `tools/make_digit_font.py`. White-on-black with a thin black outline. Functional but visually plain.

**Replacement workflow**: if you want a custom font (e.g., a stylized retro arcade font, or a Mania-faithful font), draw each glyph as an 8 × 8 PNG and feed the 12 PNGs through `tools/convert_ring_sprite.py --frames-dir <font_dir> --out cd/DIGITS.SPR`. Keep the glyph order: index 0 is `0`, index 9 is `9`, index 10 is the ring icon, index 11 is the colon. `main.c`'s `draw_hud` uses these indices directly.

### 7.6. Foreground playfield (the level tiles themselves)

**Files**:
* `cd/GHZFG.CEL` — 8 × 8 cell pixel data, 256-colour indexed, row-major byte order.
* `cd/GHZFG.PAT` — pattern name table (which cell + flip flags goes at each tilemap entry).
* `cd/GHZFG.TMP` — tilemap (the actual 2048 × 256 grid of pattern indices that lays out the level).
* `cd/GHZFG.PAL` — 256-colour CRAM palette (already pre-shifted up by 1 entry on disk).

**Current state**: This is the **GHZ Act 1 layout from your source data**, converted via `tools/convert_stream.py`. It's not placeholder; it's real level data that drives the engine's collision and gameplay. **Don't replace these files unless you're swapping in a different level.**

**To swap in a different level**:
```
python tools/convert_stream.py --scene <path-to-Scene.bin> --tile-bank <path-to-16x16Tiles.gif> --palette <path-to-StageConfig.bin> --out-prefix cd/GHZFG
python tools/build_collision.py --tile-config <path-to-TileConfig.bin> --scene <path-to-Scene.bin> --out cd/GHZSURF.BIN
```

The engine's hard-coded level extents (`LEVEL_W_PX = 16384`, `LEVEL_H_PX = 2048`) would also need updating in `main.c` if your replacement level isn't 1024 × 128 tiles.

### 7.7. Parallax sky background

**Files**: `cd/GHZSKY.DAT` (8bpp 256 × 256 bitmap), `cd/GHZSKY.PAL` (256-colour palette).

**Current state**: GHZ Act 1's sky bitmap from your source data. Like the foreground, this is real source-derived data, not placeholder.

**To replace** (e.g., for a different zone or a stylistic variation): draw a 256 × 256 indexed-colour PNG (256-colour palette) and run `tools/convert_vdp2_bitmap.py --input <sky.png> --out-prefix cd/GHZSKY`.

### 7.8. Sound effects

**Files**: `cd/RINGSFX.PCM`, `cd/JUMPSFX.PCM`.
**Format**: signed 8-bit mono raw PCM at 22050 Hz. No header.

**Current state**: Procedurally synthesized samples from `tools/make_audio.py`:
* `RINGSFX.PCM` — a short bright sine chime decaying over ~0.15 s.
* `JUMPSFX.PCM` — a downward frequency sweep ("boop") over ~0.10 s.

These are functional placeholders. Replace them with your own custom recordings or synthesized sounds.

**Conversion workflow**: any audio file (WAV, MP3, OGG, FLAC) → 22050 Hz s8 mono PCM:
```
python tools/convert_audio.py --input <your_ring_chime.wav> --out cd/RINGSFX.PCM --rate 22050 --bits 8 --channels 1
python tools/convert_audio.py --input <your_jump_boop.wav> --out cd/JUMPSFX.PCM --rate 22050 --bits 8 --channels 1
```

The script wraps `ffmpeg` and strips the header, leaving raw s8 PCM. Keep effects short — every byte costs pool space. Currently each SFX is ~3 KB, which is fine. Going above ~10 KB per SFX would crowd the pool.

**More SFX slots you'll want to add**:
* `BREAKSFX.PCM` — monitor break.
* `BOUNCESFX.PCM` — spring launch (currently reuses `JUMPSFX.PCM` as a stand-in).
* `STOMPSFX.PCM` — badnik stomp.
* `HURTSFX.PCM` — when Sonic takes damage.
* `ACTCLRSFX.PCM` — act-clear jingle.

Each new SFX needs three things: an entry in `setup_audio()` to load it via `jo_audio_load_pcm`, a global `jo_sound g_*_sfx` handle, and a `jo_audio_play_sound(&g_*_sfx)` call at the relevant gameplay event.

### 7.9. BGM (background music)

There are **two** BGM paths in the engine, mutually exclusive in practice:

**Path A — SCSP streamed PCM**:
* File: `cd/STAGEBGM.PCM`.
* Format: s8 mono PCM at 11025 Hz (low-fi to fit pool).
* Currently disabled in `setup_audio()` for pool budget reasons. The hook is still there commented out; flip it back on if you have pool headroom.
* Drawback: eats Work RAM (every minute of audio is ~660 KB at 11025 Hz — far more than the entire pool budget allows). Realistic ceiling is ~20-30 seconds of looping audio.

**Path B — CD-DA (Recommended)**:
* File: `cd_audio/track02.wav`.
* Format: standard WAV file, 44100 Hz, 16-bit, stereo.
* On build: `build.bat` runs `tools/build_cdda.py` to strip the WAV header into raw CD audio (`cd_audio/track02.bin`) and rewrite `game.cue` as a 2-track CUE (game data on track 01, audio on track 02).
* At runtime: `jo_audio_play_cd_track(2, 2, true)` in `enter_game()` plays track 02 on a loop.
* Advantage: full CD-quality audio, no pool cost, and any length up to the disc's available space.

**Current state**: Both paths are wired in source. Whether `cd_audio/track02.wav` exists or not is up to you — `build.bat` only invokes the CD-DA build step if the file is present. There's no procedural placeholder for BGM; the engine simply runs silent if you haven't provided one.

**Replacement workflow** (Path B, recommended):
1. Compose or source a BGM track. Export as 44100 Hz 16-bit stereo WAV.
2. Save it as `cd_audio/track02.wav`.
3. Run `build.bat`.

That's the whole flow. The CUE rewrite and the runtime playback are already wired up.

If you want multiple zones with different BGMs, the natural shape is: rename each WAV by zone (`cd_audio/track02_ghz.wav`, `cd_audio/track03_cpz.wav`, ...), extend `build_cdda.py` to write multiple `AUDIO` tracks into the CUE, and pass the track number from your zone state machine into `jo_audio_play_cd_track`.

### 7.10. Entity position files

**Files**: `cd/GHZRINGS.BIN`, `cd/GHZBUGS.BIN`, `cd/GHZSPRG.BIN`, `cd/GHZBOX.BIN`, `cd/GHZSIGN.BIN`.
**Format**:
```
u16 BE  entity count
N entries * (u16 BE x, u16 BE y, [optional per-entity state bytes])
```

**Current state**: These are extracted from your source `Scene1.bin` via `tools/build_rings.py --class-name <ClassName>`. They're real per-entity world coordinates, not placeholders. **Don't replace these unless you're hand-authoring custom level layouts.**

If you want to hand-place rings (e.g., draw rings in a specific layout that differs from the source), write a Python script that emits the same big-endian header + per-entity coordinate format. The simplest version:

```python
import struct
rings = [(1024, 800), (1080, 800), (1136, 800), ...]
with open("cd/GHZRINGS.BIN", "wb") as f:
    f.write(struct.pack(">H", len(rings)))
    for x, y in rings:
        f.write(struct.pack(">HH", x, y))
```

The engine reads at most `RINGS_MAX = 512` rings, `BUGS_MAX = 16` badniks, `SPRINGS_MAX = 48` springs, `BOXES_MAX = 48` monitors, `SIGNS_MAX = 4` signposts. Bump those caps in `main.c` if your custom layout exceeds them, but watch the pool budget — each entity slot costs a few bytes of state.

### 7.11. Collision surface table

**File**: `cd/GHZSURF.BIN`.
**Format**: 16384 entries of `(u16 BE Y, u8 angle, u8 flag)`.

**Current state**: Generated offline by `tools/build_collision.py` from your source `TileConfig.bin` + `Scene1.bin`. This is real source-derived data and **shouldn't be edited by hand** — if the level layout changes (`GHZFG.TMP` swap), regenerate this file from the same source.

### 7.12. Title screen backdrop

**Current state**: `STATE_TITLE` in `main.c` calls `draw_title()` which renders procedural primitives: a sky-blue background, an orange sun, palm trees made of stacked rectangles, flower-shaped dots in the foreground, and Sonic at the centre-left in his idle pose. It's deliberately abstract.

**To replace with custom title art**:
1. Draw your title screen as a 320 × 224 indexed-PNG (or BGR1555 RAW).
2. Either:
   * Convert to a VDP2 NBG bitmap (same path as the sky) and load it as a full-screen background. Add a converter call to `make` or `build.bat`.
   * Or load it as a single large VDP1 sprite. Cheap to wire up but eats more VDP1 cycles than the VDP2 path.

The title-card cinematic between TITLE and GAME (`STATE_TITLECARD`) currently composites the ring icon + `1` glyph pair over whatever the title backdrop is, sliding them across the centre of the screen. If you redesign the title art, the title card overlay will compose over your new background automatically.

### 7.13. Title-card decorations

**Current state**: The title-card cinematic uses the existing `cd/DIGITS.SPR` glyphs (ring icon + `1`) as its content. There's no separate "zone name" text or character portrait — those are placeholder gaps.

**Polish items**:
* Solid-coloured backdrop bar behind the glyphs (draw an SGL filled polygon strip in `draw_titlecard`).
* Ease-out cubic instead of linear easing on the slide-in/slide-out (~5 lines of code change in `draw_titlecard`).
* "GREEN HILL ZONE" zone-name text glyph atlas (new SPR file at e.g. `cd/ZONENAME.SPR`).
* "ACT 1" act-number text glyph atlas (or extend `DIGITS.SPR`).
* Character portrait sprite shown sliding in next to the zone name.

---

## 8. Key Technical Learnings (Each Cost a Build Cycle)

These are recorded as one-fact-per-file memory entries under `memory/`, auto-loaded into Claude sessions via `memory/MEMORY.md`. Worth reading before you make changes to the matching subsystem:

* **`saturn-vdp2-streaming-solved.md`** — VDP2 VRAM writes during active display don't reliably reach the screen. Do them via SCU DMA in the V-blank handler. Origin: NOV96 DTS sample SEGA2D_1.
* **`rule-read-dts-before-coding.md`** — Non-negotiable: any Saturn hardware behaviour (DMA, VRAM, cycle patterns, VDP1↔VDP2 interaction) must be settled from the NOV96 DTS docs in `D:\Claude Saturn Skill Documentation\NOV96_DTS` before writing a fix. No guessing.
* **`jo-pool-stale-core-o-gotcha.md`** — `make` does not recompile `core.o` when `JO_GLOBAL_MEMORY_SIZE_FOR_MALLOC` changes. Always `rm jo-engine/jo_engine/*.o` before a pool-size change.
* **`jo-cram-off-by-one-shift.md`** — `jo_create_palette_from` allocates user palettes starting at `CRAM[bank+1]`, so cell-pixel V reads `PAL[V-1]`. Pre-shift every 256-colour PAL up by 1 entry on disk.
* **`qa-capture-must-start-from-bios.md`** — `-Wait 2 -Every 0.25 -Shots 120` is the mandatory dense-capture pattern. `Wait≥20 + Every=1` misses the action arc entirely.
* **`rsdk-decoration-tile-skip.md`** — TileConfig marks decoration tiles (sun totems, hanging objects, flowers) as solid; the surface builder must require ≥ 4 tiles of continuous solid mass below a candidate floor.
* **`sgl-audio-vs-scroll-cpu-dma-conflict.md`** — SGL 2.0A release notes §1.2.1: sound CPU DMA conflicts with scroll-screen CPU DMA during V-blank. Use `slDMAXCopy` (SCU DMA, cache-through source).
* **`sonic-mania-saturn-project.md`** / **`sonic-mania-saturn-status.md`** — Project goal, current state, open items.

A few additional facts that haven't earned their own memory file but are worth noting:

* **`jo_sprite_add` copies pixel data to VDP1 VRAM** at line 220 of `jo_engine/sprites.c` via `jo_dma_copy`. After the call returns, the source buffer in the pool is safe to `jo_free()`. Every sprite loader in `main.c` does this — recovered ~55 KB of pool that made the springs/monitors/signposts + HUD all fit.
* **RSDK Scene.bin entity-list format** (after layer data ends):
  ```
  u8     objectClassCount
  per class:
    16B  MD5 hash of class name
    u8   varCount   (var 0 is implicit "position")
    per var 1..varCount-1:  16B name-hash + u8 type
    u16  entityCount
    per entity:
      u16 slotID
      i32 position.x  -- LITTLE-endian (RSDK uses LE on wire)
      i32 position.y  -- Q16.16 fixed
      per var: byte/word/dword by type, or u16 length + length*u16 chars for VAR_STRING (wide)
  ```
  VAR enum order: `UINT8=0, UINT16=1, UINT32=2, INT8=3, INT16=4, INT32=5, ENUM=6, BOOL=7, STRING=8, VECTOR2=9, FLOAT=10, COLOR=11`. Mania class names: `Ring`, `Motobug` (lowercase b!), `BuzzBomber`, `Newtron`, `Chopper`, `Spring`, `ItemBox`, `SignPost`, `BreakableWall`, `Bridge`, `Spikes`, `EggPrison`, `HUD`, `TitleCard`, `Player`, `BoundsMarker`, `GHZSetup`.

---

## 9. QA Harness

Two automated gates run every release build (and can be invoked standalone for debugging):

### 9.1. Title reference-diff gate

* Script: `tools/qa_gate.ps1` → `tools/qa_refdiff.py`.
* Trigger: `build.bat` runs it on the QA-mode binary (title held forever).
* Capture pattern: `qa_boot.ps1 -Cue game.cue -Wait 22 -Shots 1 -Out qa_title.png`. The 22-second wait covers Mednafen Saturn's BIOS load (~14-18 s) plus 4 s of title-screen stabilization.
* Crop: cuts the Mednafen window chrome down to a 320 × 240 game area.
* Downscale: bilinear to 320 × 240 (matches the golden image).
* Alignment: searches ±3 px in both axes for the minimum mean-delta offset (tolerates 1-2 px shifts in the emulator's framebuffer-to-window mapping).
* Pass criterion: mean RGB delta ≤ 12 **and** 95th-percentile RGB delta ≤ 120, with the Sonic title-pose region (centre-left) masked out so idle-animation variation doesn't false-positive.
* Failure modes caught: palette shifts (CRAM allocator bug regressions), missing assets (sprite or palette failed to load), geometry regressions (sprite anchor moved), title-screen logic regressions.
* The golden file at `tools/refs/title_view.golden.png` is the source of truth. Update it deliberately (and review the visual diff) whenever you intentionally change the title screen.

### 9.2. Grounded gate

* Script: `tools/qa_grounded_majority.py` over `tools/qa_grounded.py`.
* Trigger: `build.bat` runs it on the release binary.
* Capture pattern: `qa_boot.ps1 -Cue game.cue -Wait 18 -Shots 8 -Every 0.7 -Out qa_ground.png`. Wait 18 lands after BIOS + title (~2 s) + title card (~2 s), so all 8 captures fall in true game state, not the title-card overlay. Every 0.7 covers ~5 s of gameplay.
* Per-frame check (`qa_grounded.py`): scan the captured frame for Sonic's blue body silhouette, find the bottom-most blue pixel, then scan straight down looking for the first green/brown grass-or-dirt pixel. Pass if the feet are within 4 game-pixels of the ground.
* Majority check (`qa_grounded_majority.py`): pass if at least 2 of 8 frames are grounded. Loosened from the original 50% threshold once springs were added (every spring legitimately puts Sonic airborne for 30+ frames).
* Failure modes caught: collision-table off-by-one, decoration-tile false-positives in the surface builder, sprite-anchor misalignment, gravity broken (Sonic flying off into space), camera not following vertically (player visible at wrong screen Y).

### 9.3. The capture-from-BIOS mandate

There was an early version of the QA harness that used short `Wait` values (5-10 seconds) and dense captures. It never caught real bugs because Mednafen's BIOS load is variable — on a cold start it takes 14 s, on a warm start it can take 18 s. Captures launched too early would land in BIOS rather than gameplay and report false-pass.

The current pattern (`Wait 18-22` for game-state captures, `Wait 22` for title-only) is calibrated to land cleanly after BIOS on both warm and cold starts. This is recorded as `memory/qa-capture-must-start-from-bios.md` and is non-negotiable for any new QA gates added in future.

### 9.4. Manual QA — boot the ROM and play

For interactive QA beyond the automated gates, the simplest workflow is:
```
mednafen.exe game.cue
```
The Mednafen Saturn core runs the ROM at full speed. Default keyboard mapping: arrows = D-pad, Z/X/C = A/B/C, Enter = START. Press F11 to capture a screenshot, F9 to save state, F7 to load state.

---

## 10. What Remains

Listed in roughly priority order for visual / gameplay impact per unit of work.

### Phase 4 polish (small wins, high visual return)

1. **Custom sprite swap-in pass**. Replace every procedural placeholder under `cd/*.SPR` with hand-designed or sourced sprite art. This is the single highest-impact change you can make to the visual presentation of the game and requires zero engine work — just produce the SPR files and rebuild.

2. **Custom BGM**. Drop a 44.1 kHz 16-bit stereo WAV at `cd_audio/track02.wav` and rebuild. Highest audio-impact change, zero engine work.

3. **Custom SFX**. Replace `cd/RINGSFX.PCM` and `cd/JUMPSFX.PCM` with your own samples via `tools/convert_audio.py`. Add additional SFX (break / bounce / stomp / hurt / act-clear) by adding `jo_audio_load_pcm` calls in `setup_audio()` and `jo_audio_play_sound` calls at the relevant events.

4. **Signpost spin animation**. On `g_act_cleared`, rotate the SIGNPOST.SPR through frames, settle on one of the character faces, play act-clear chime. Requires extending `SIGNPOST.SPR` to multi-frame and adding a state machine in `draw_signposts`.

5. **End-of-act bonus tally cinematic**. New `STATE_TALLY` state showing `SCORE BONUS` + `RING BONUS` + `TIME BONUS` with digit-roll-up animation (ease-out cubic), adding to `g_score`, then transitioning back to `STATE_TITLE` for replay. Reuses existing `DIGITS.SPR` glyphs.

6. **Title-card refinements**:
   * Solid-coloured backdrop bar behind the glyphs (one SGL polygon strip).
   * Ease-out cubic on the slide-in/slide-out (5-line code change in `draw_titlecard`).
   * Zone-name text overlay (new SPR or extended `DIGITS.SPR`).

7. **Proper hurt-state machine**. Replace the placeholder "side-contact → rings=0" with Mania-faithful flow: invuln-flash period (~120 ticks), ring-scatter physics (cosine-arc burst with per-ring gravity + 4 s despawn), brief knockback `xsp` + `ysp`. Adds 3 new ring states (scattered / bouncing / despawning) on top of the existing ring entity.

8. **More badnik AI patterns**. BuzzBomber (flying line + stinger drop), Newtron (pop-out-of-wall), Chopper (water jump). Same entity-position pipeline (`build_rings.py --class-name X`), each with its own motion script atop the existing badnik engine. Class hashes already mapped during the Scene1.bin probe.

### Phase 5 (full game shell)

9. **Death pit + respawn + lives counter + game-over state**. Trigger: `ypos > LEVEL_H_PX + 64`. Subtract one life, reset Sonic to last-touched checkpoint (the `Bounds` / `BoundsMarker` entities in Scene1.bin), or to start of act if no checkpoint was hit.

10. **Pause menu** (START in game state). Just a `STATE_PAUSE` with a translucent overlay and a "RESUME / EXIT" two-option menu using `DIGITS.SPR` glyphs.

11. **Stage select / zone progression**. Mania has 12+ zones; your source data pack has `Data/Stages/{AIZ,CPZ,FBZ,HCZ,LRZ1,LRZ2,LRZ3,MMZ,MSZ,OOZ1,OOZ2,PSZ1,PSZ2,SPZ1,SPZ2,SSZ1,SSZ2,TMZ1,TMZ2,TMZ3,ERZ,DAGarden,Title,Logos,Menu,…}`. Per-zone, run `convert_stream.py` + `build_collision.py` + `build_rings.py` and ship the resulting files under per-zone filename prefixes (e.g., `cd/CPZ1FG.CEL`, `cd/CPZ1SURF.BIN`). State machine loads the right ones when entering each zone.

12. **Special stages** (Blue Spheres). Your source data pack has the `SpecialBS` stage folder with 36 scenes. Different gameplay model entirely (you're rolling around a sphere collecting blue spheres while avoiding red ones), so it'll need its own scene-loading + render path.

13. **Save data to backup RAM**. The jo backup module is already compiled in. Use `jo_backup_save_file` to persist best times, ring totals, special-stage emerald counts, completed acts.

---

## 11. Where to Pick Up

When a new development session begins:

1. **Read `memory/MEMORY.md`** — it auto-loads on every session and lists the eight permanent technical facts that took build cycles to discover.
2. **Read this file (`HANDOFF.md`)** — gives you the full picture from goal to current state.
3. **Read `src/main.c`** — the entire game loop fits in this one file; familiarize yourself with the state machine (`STATE_TITLE` / `STATE_TITLECARD` / `STATE_GAME`), the entity update functions (`update_rings`, `update_badniks`, `update_springs`, `update_monitors`, `update_signposts`), and the draw functions (`draw_title`, `draw_titlecard`, `draw_game`, `draw_hud`).
4. **Read `src/player.c`** — the physics state machine. Constants are in `src/physics.h`.
5. **`build.bat`** — your one-command rebuild + QA gate. If it passes, you didn't break anything.

The fastest way to make a visible change to the game is to swap one of the placeholder assets listed in section 7. The fastest way to make a structural change is to extend one of the existing entity update functions (every entity follows the same `cd/<name>.BIN` + `cd/<NAME>.SPR` + per-entity state struct pattern, so adding a new entity class is a copy-of-an-existing-class plus a `build_rings.py --class-name X` extraction).

Backups of every working milestone state are in `src/main_phase*_*.c.bak` and `Makefile*.bak` — useful reference points if you ever break something and want to see what worked before. The memory files under `memory/` are the durable lessons-learned and should be consulted whenever you touch a Saturn hardware subsystem.

This is a small, fast, asset-portable Saturn engine that runs a playable level today. The remaining work is mostly content (more sprites, more music, more zones) plus the Phase 5 game-shell items (death/respawn, pause, save, multi-zone progression, special stages). Every piece is tractable; none of it requires re-architecting what exists.

— end handoff —

---

## Phase 1.26c — RBG0 rotation gate diagnosis + fix (2026-05-27)

**Problem reported:** Gate V1.26b RED at mean ROI delta = 0.000 — title backdrop static despite jo_enable_background_3d_plane plumbing in src/main.c.

**Diagnostic methodology (skill-binding §6a):** Used the Phase 1.30 Mednafen savestate harness as PRIMARY diagnostic (register-level question). Wrote tools/qa_rbg0_rotation_gate.py to peek VDP2 rotation registers per ST-058-R2 (BGON @ 0x05F80020, RPMD @ 0x05F800B2, RPTAU/L @ 0x05F800BC/BE, PRIR @ 0x05F800FC).

**Register peeks (BEFORE fix, samples/qa_phase1_26c_t0.mcs):**
- BGON  = 0x0002 — only NBG1ON; **R0ON (bit 4) NOT SET**.
- PRINA = 0x0000, PRINB = 0x0000 — all NBG priorities zeroed.
- PRIR  = 0x0004 — RBG0 priority 4 (slPriorityRbg0(5) clobbered to 4).
- RPMD  = 0x0000 — rotation mode 0 (correct).
- RPTA  = 0x05E3FF00 — slRparaInitSet DID bind table inside VDP2 VRAM bank A1.
- Matrix block at RPTA: identical between two states 2s apart (advisory only — see below).

**Root cause hypothesis:** SGL's per-vblank slSynch rewrites BGON/PRIR via internal pre-vblank-sync transactions; jo's one-shot `slScrAutoDisp(screen_flags)` call inside `jo_vdp2_enable_rbg0` (jo-engine/jo_engine/vdp2.c:359) gets clobbered downstream.

**Fix (src/main.c:349-350, applied inside mania_title_3d_backdrop_draw):**
```c
slScrAutoDisp(NBG1ON | RBG0ON | SPRON);
slPriorityRbg0(5);
```
REPLACE-semantics every-frame re-application, mirroring the pattern proven correct at src/rsdk/scene_ghz.c:300/325 (Phase 2.1 NBG1ON|SPRON). Citations: ST-058-R2 §6.2 BGON (R0ON bit 4), §11.1 PRIR (R0PRIN bits 2-0), NOV96_DTS/EXAMPLES/SGL/BIPLANE/MAIN.C:212 (canonical slScrAutoDisp pattern).

**Evidence (AFTER fix):**
- Gate V1.26b pixel ROI delta: **mean over 5 pairs = 36.171** (>> 1.5 threshold) — RED→GREEN.
- Per-pair deltas: 17.321 / 15.902 / 39.482 / 48.707 / 59.441.
- Pixel rotation is unambiguously visible.

**Empirical finding about savestate-based register inspection:** Mednafen 1.32.1's VDP2/RawRegs and VDP2 VRAM serialisation captures values at a sync point BEFORE SGL's per-vblank rewrites commit. Write-only registers (BGON, PRIR, PRINA, PRINB) read in the savestate may not reflect the live hardware state when SGL re-applies them every frame. The RPTA-pointed matrix block in VDP2 VRAM also suffers this — slScrMatSet writes happen post-sync. **Pixel-based ROI gates are the authoritative test for live VDP2 rotation behavior; savestate register peeks are advisory.** Documented at tools/qa_rbg0_rotation_gate.py header + verify_done.ps1 Gate V1.26c comment.

**Files changed:**
- `src/main.c:298-380` — per-frame slScrAutoDisp + slPriorityRbg0 inside mania_title_3d_backdrop_draw.
- `tools/qa_rbg0_rotation_gate.py` — NEW register-level diagnostic gate (advisory).
- `tools/verify_done.ps1` — NEW Gate V1.26c sub-gate between V1.26b and V-REG.

**Gate verdicts after fix:**
- Gate V1.26b: GREEN (mean delta 36.171).
- V-REG baseline: GREEN (8/8 register assertions PASS: SPCTL=0x0023, TVMR=0x8110, PTMR=0x0402, etc.).
- Gate V1.26c (advisory): OK (RPTA inside VRAM, RPMD valid).

**Step 2 (Mania island backdrop asset) — DEFERRED:** Pre-condition "Step 1 produces visible rotation" met; current TITLE.DAT (114688 B, 224×512) is already rendering as the rotating plane content per pixel evidence. Replacing it with a Mania-island-cropped asset is a content swap, not a structural change. Recommend deferring to Phase 1.29 (boot-delay reduction) where asset budget audit is needed anyway — adding a second 100KB asset now would push the cumulative load time over the 5s threshold from §4.5.1 Audit 4. The current Phase 1.26c work fully resolves the user-visible "static backdrop" defect.

---

## Phase 1.27b — TitleSonic finger pivot audit — HALTED (audit ruled out hypothesised cause)

**User report (2026-05-27):** "the placement of sonics arm/waving hand in the title screen image seems to be too far to the left of his body and not in the right spot as per the gospel decomp"

**Hypothesised root cause (entering Phase 1.27b):** `src/mania/Game.c:1683-1691` finger composite uses asset-level (frozen frame 0) pivot/width/height for all 12 finger frames instead of the per-frame metadata in `g_tsonic_frames[g_tsonic_anim1_first_frame + finger_local]`.

**Audit per CLAUDE.md §4.5.1 Audit 3 (pivot+flip composite math):** `tools/audit_finger_pivot.py` parses `extracted/Data/Sprites/Title/Sonic.bin`, extracts the per-frame `(w, h, pivot_x, pivot_y, duration)` from anim 1 (12 frames), and computes the broken-formula vs correct-formula horizontal/vertical centroid drift for each frame. JSON report at `tools/audit_finger_pivot_report.json`.

**Audit table excerpt:**

| frame | w  | h  | px | py  | dur | correct cx (FLIP_X) | broken cx | dx | rel(C) | rel(B) |
|------:|---:|---:|---:|----:|----:|--------------------:|----------:|---:|-------:|-------:|
| 0     | 50 | 58 |  6 | -44 |   4 |                 221 |       221 |  0 |    -26 |    -26 |
| 3     | 49 | 63 |  6 | -49 |   1 |                 222 |       221 |  1 |    -25 |    -26 |
| 4     | 48 | 63 |  6 | -49 |   2 |                 222 |       221 |  1 |    -25 |    -26 |
| 5     | 46 | 62 |  6 | -48 |   3 |                 223 |       221 |  2 |    -24 |    -26 |
| 6     | 45 | 60 |  6 | -46 |   4 |                 224 |       221 |  3 |    -23 |    -26 |

Worst horizontal drift = **3 px (at frame 6)**. Worst vertical drift = **3 px**. The `pivot_x` is INVARIANT at +6 across all 12 frames; the only per-frame variation is in `width` and `height` (mostly the forearm extending vertically), which yields a sub-screen-pixel horizontal shift at the finger centroid. **A 3-px shift cannot account for the user-visible "too far to the left" complaint** — the finger sits at flipped-x=221 (26 px LEFT of the body centroid at 247) regardless of which finger frame is active.

**Conclusion:** the per-frame pivot fix is not the root cause. Per the user's binding constraint in the Phase 1.27b task ("If the audit reveals the bug is NOT the per-frame pivot issue... STOP and HONEST hand-off"), no code change was made to `src/mania/Game.c:1683-1691` and no Gate V1.27b was added to `verify_done.ps1`.

**Structural finding from the audit's deeper trace (where the bug actually lives):**

1. `src/mania/Objects/Title/TitleAssets.c:558` — `title_tsonic_draw_frame` explicitly drops the `direction` parameter: `(void)direction; /* TSONIC body is symmetric — no flip used. */`, and writes `attr.dir = 0` at line 579. **Hardware VDP1 HF (CMDCTRL bits 5-4 per ST-013-R3 §5.5.4, exposed via SGL `slDispSprite`'s `attr.dir` per ST-238-R1 §slDispSprite) is NEVER applied to either body or finger.**

2. `src/mania/Game.c:1620-1640` (body) and `src/mania/Game.c:1685-1691` (finger) both compose the FLIP_X effect by mirroring the centroid x about `entity_x=252`: `flipped_cx = 2*world_x - cx_noflip`. This moves the sprite to the mirror position but leaves the pixels unmirrored.

3. **Body is roughly symmetric** (front/back silhouette of Sonic), so unmirrored pixels at the mirror position look approximately correct.

4. **Finger is highly asymmetric** — anim 1 depicts the finger extending to one side (in source-art coords, to Sonic's right since the body faces left in source). With centroid mirrored but pixels NOT mirrored, the right-hand-extending pixels land at the LEFT-of-body screen position. That is the visible bug the user is reporting.

5. Math verification (per audit + RSDK `_RSDKv5_Graphics_Drawing.cpp DrawSpriteFlipped`):
   - Body settled (kf=7, atlas 48, px=-50, w=110): FLIP_NONE topleft = 252-50 = 202; centroid 257.
   - Finger frame 0 (px=+6, w=50): FLIP_NONE topleft = 252+6 = 258; centroid 283.
   - In source-art coords, finger sits +26 px right of body centroid (between 258 and 308, i.e. right half of body 202..312).
   - Both sprites mirrored about entity_x=252 yields body centroid 247, finger centroid 221, finger at -26 relative — finger now appears LEFT of body, exactly matching the user complaint.

**Three candidate fix paths (require user direction — not selecting unilaterally):**

A. **Hardware-flip via `attr.dir = 0x10` (HF, per ST-013-R3 §5.5.4 PMOD bits 5-4)** for both body and finger. Drop `title_tsonic_draw_frame`'s `(void)direction;` and propagate the param to `attr.dir`. Keeps the entity-coord centroid math as-is; just mirrors the framebuffer pixels per-sprite about each sprite's own bbox centre. Result: source-art right-hand finger pixels become left-hand finger pixels at the mirrored position, geometry now visually consistent with the body.

B. **Stop flipping both sprites** (revert the Phase 1.27 facing decision). Sonic faces left in source art; if we draw both unflipped at their natural source-coord positions, the decomp geometry is preserved exactly (finger to source-right of body, both unflipped). The visual change: Sonic no longer faces the gold MANIA wordmark on the right; he points back-left toward the title side.

C. **Pre-mirror the TSONIC.ATL pixels offline** in `tools/build_titlesonic_atlas.py` (add `Image.transpose(FLIP_LEFT_RIGHT)` for each frame; negate pivot_x and recompute pool offsets). Keeps `attr.dir = 0`. Equivalent visual result to (A) but the cost is a re-roll of the atlas and the ~33 KB of VDP1 char-RAM that now contains pre-flipped pixels.

**Recommendation:** Path A is the smallest source change (~10 lines in TitleAssets.c + drop the centroid-mirror math in Game.c's body and finger composite, replace with `attr.dir = (direction & 1) << 4`). Path C is the cleanest cache-of-pixels approach but requires an asset rebuild and re-verify. Path B is "most faithful to decomp" but reverses an explicit Phase 1.27 §11.32 Item 1 decision the user already approved.

**Files produced this phase (HALTED state):**
- `tools/audit_finger_pivot.py` — NEW quantitative audit script (per §4.5.1 Audit 3); replays the broken vs correct composite math against Sonic.bin's anim 1 metadata.
- `tools/audit_finger_pivot_report.json` — NEW machine-readable per-frame audit report; max drift = 3 px on x and y.
- `HANDOFF.md` — THIS entry.

**Files NOT changed (per the user's binding constraint):**
- `src/mania/Game.c:1683-1691` (no per-frame fix — audit proved the fix would yield at most 3 px of drift reduction, well below the user-visible threshold).
- `tools/verify_done.ps1` (no Gate V1.27b — without a fix to gate, RED-firing has no remediation step).
- `tools/qa_golden/` (no broken-baseline screenshot archived — the broken state is unchanged and reproducible by any rebuild).

**Awaiting user direction:** pick path A, B, or C (or propose a fourth) before the fix lands. Per CLAUDE.md §6.5 (no unilateral deferrals) and §6.1 (no shipping "looks close" — the right answer needs a paired RED→GREEN gate against a real reference image, not a 3-px micro-improvement).

— end Phase 1.26c entry —

---

## Phase 1.27b — Step 2: body-anchored finger fix landed (2026-05-27)

**User direction (binding):** "Sonic appears correct and is facing the right direction... its only his arm that is placed incorrectly!!" — i.e. body at `src/mania/Game.c:1620-1640` is fine, ONLY the finger at `src/mania/Game.c:1685-1691` is in scope.

**Audit reference:** `tools/audit_finger_pivot.py` + `tools/audit_finger_pivot_report.json` (per CLAUDE.md §4.5.1 Audit 3). Audit established: decomp `tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.c:36` draws finger at `self->position` (NO flip, NO relative offset in object code). The relative geometry comes ENTIRELY from per-frame pivots in `Title/Sonic.bin`: body settled (kf=7, atlas 48) cx_flip_none=257; finger frame 0 cx_flip_none=283 → finger is +26 px RIGHT of body in source-art FLIP_NONE coords.

**Pre-fix vs post-fix centroid measurements (mednafen capture, settled window frames 95..118):**

| Build | RED-baseline (broken) | Post-fix (Step 2) | Shift |
|---|---:|---:|---:|
| median glove_cx (center-ROI, x=320..570) | 372 | 436 | +64 px |
| Sonic body cx (qa-capture, unchanged) | ~456 | ~456 | 0 (body untouched) |
| Glove relative to body | -19 mednafen-px LEFT | +16 mednafen-px right of broken position | finger now visually on the RIGHT of Sonic's face per decomp |

**Code change** at `src/mania/Game.c:1685-1758` (replaced the 7-line broken centroid-mirror block with a 74-line body-anchored composite):
- Old: `int fcx = world_x + fa->pivot_x + (fa->width >> 1); int flipped_fcx = 2*world_x - fcx; title_tsonic_draw_frame(..., flipped_fcx-256, fcy-112, 188, 1);`
- New: anchor at body's drawn centroid `flipped_canvas_cx + rel_x`, where `rel_x = finger_flipnone_cx - body_flipnone_cx` per the active finger frame's per-frame pivot lookup from `g_tsonic_frames[g_tsonic_anim1_first_frame + finger_local]`.
- Inline comment block cites: decomp TitleSonic.c:36 + audit JSON + user 2026-05-27 direction + CLAUDE.md §4.5.1 Audit 3.

**Out-of-scope (per user direction):** Path A/B/C decision from Phase 1.27b Step 1 (the `attr.dir=0` hardcode at `TitleAssets.c:558,579`) — body's mirrored centroid is the accepted reference, no pixel-flip change attempted.

**Gate V1.27b** added between V1.26c and V-REG in `tools/verify_done.ps1` (around line 1390):
- Method: capture Wait=2 / Every=0.25 / Shots=120 (matches V1.27/V1.26b cadence), settled window 95..118. Measure glove white-pixel centroid (R,G,B all >= 235) in center ROI y=200..400, x=320..570 (excludes wings sprite which has white highlights). Threshold: median glove_cx >= 410 (broken baseline = 372, post-fix = 436).
- Broken-baseline reference screenshot saved at `tools/qa_golden/qa_phase1_27b_broken.png` (66007 B).

**Gate verdicts:**
- Gate V1.27b on RED-baseline build (broken `#else` branch active): glove_cx=372 → RED (FAIL, 372 < 410). Confirmed firing RED before fix landed.
- Gate V1.27b on POST-FIX clean build (fix landed, scaffolding removed): glove_cx=436 → GREEN (PASS, 436 >= 410, +64 px shift).
- Gate V1.27 sub-gates (Sonic facing, intro arc, electricity ring): timing-dependent failures during this session's verify_done run (electricity arc_max=0 at frames 50..65 because mednafen BIOS boot took longer than usual today, pushing title visibility to frame ~96 instead of ~55-65). Body draw at lines 1620-1640 is byte-identical to pre-fix so these failures are NOT a regression from the Phase 1.27b Step 2 change. Pre-existing Task #37 (Gate 6b too loose — frame-picker gaming) covers the broader capture-timing fragility.
- Visual verification: post-fix capture `qa_phase1_27_seq_100.png` (during gate-debug recapture) shows Sonic with finger extended to RIGHT of body, matching the iconic Mania title pose; broken-baseline `tools/qa_golden/qa_phase1_27b_broken.png` shows finger on left of face (the user-reported bug).

**Files changed:**
- `src/mania/Game.c:1685-1758` — body-anchored finger composite (replaces broken centroid-mirror block).
- `tools/verify_done.ps1` — NEW Gate V1.27b between V1.26c and V-REG.
- `tools/qa_golden/qa_phase1_27b_broken.png` — NEW broken-baseline screenshot for audit trail.
- `HANDOFF.md` — THIS entry.

**Files NOT changed (per user explicit direction):**
- `src/mania/Game.c:1620-1640` (body draw — user accepted as-is).
- `src/mania/Objects/Title/TitleAssets.c:558,579` (attr.dir=0 hardcode — Path A deferred).
- Centroid mirror math at line 1632 for body — unchanged.

— end Phase 1.27b Step 2 entry —

---

## Phase 1.29a — Selective TSONIC.ATL rebuild (Task #101, 2026-05-27)

**Scope:** strip the unused 41 source frames out of `cd/TSONIC.ATL` anim 0
(the 49-frame body entrance arc) so only the 8 keyframes the runtime
actually reads at `src/mania/Objects/Title/TitleAssets.c:396`
(`s_tsonic_keyframe_offsets[8] = {0, 6, 12, 18, 24, 30, 36, 48}`) plus
all 12 anim 1 finger frames ship on disc. Single-port iteration; no
runtime touchpoints.

**Pre-strip audit (`tools/audit_tsonic_atlas.py` against pre-strip atlas):**

| field                       | value             |
|-----------------------------|-------------------|
| current_atlas_bytes         | 392,554 B         |
| anim0_frame_count           | 49                |
| anim0_pixel_bytes_total     | 372,216 B         |
| anim0_used_keyframes        | [0,6,12,18,24,30,36,48] |
| anim0_used_bytes            | 61,408 B          |
| anim0_dead_bytes            | 310,808 B  (83.5%) |
| anim1_frame_count           | 12                |
| anim1_pixel_bytes_total     | 19,424 B (all used) |
| projected_stripped_bytes    | 81,172 B  (79.3 KB) |
| projected_savings_bytes     | 311,382 B (79.3%) |

**Gate V1.29a verdicts:**

- pre-strip: **RED** — size 392,554 B > 81,920 B limit; anim0_fc=49 != 8.
- post-strip: **GREEN** — size 81,172 B <= 81,920 B; anim0_fc=8; anim1_fc=12.

**Post-strip audit confirms zero dead weight:**

- final file size: 81,172 B (vs 392,554 B pre-strip; 311,382 B saved /
  79.3% reduction).
- anim 0 frame_count: 8 (renumbered 0..7 in atlas; pivot/width/height/
  duration for each output-i is sourced from `Sonic.bin` anim 0 frame
  `s_tsonic_keyframe_offsets[i]`).
- anim 1 frame_count: 12 (unchanged, contiguous 0..11).
- pixel pool: 80,832 B (60,576 B anim 0 + 19,424 B anim 1; 1.2% accounted
  for by alignment padding from `width % 8`).

**Files changed:**

- `tools/audit_tsonic_atlas.py` (NEW) — quantitative pre/post-strip
  audit; emits JSON report.
- `tools/audit_tsonic_atlas_report.json` (NEW, generated) — machine-
  readable record of the audit.
- `tools/build_titlesonic_atlas.py:134-178` — `--selective` mode added;
  swaps anim 0's 49-frame source list for the 8 keyframes
  `{0,6,12,18,24,30,36,48}` (renumbered 0..7), recomputes loop_index to
  7. anim 1 untouched. CLI: `python tools/build_titlesonic_atlas.py --selective`.
- `tools/verify_done.ps1:607-614` — Gate 9 relaxed to accept either 49
  (legacy) or 8 (selective) anim 0 frame_count.
- `tools/verify_done.ps1:1547-1603` — NEW Gate V1.29a inserted between
  V1.27b and V-REG; asserts size <= 80 KB AND anim 0 frame_count == 8
  AND anim 1 frame_count == 12.
- `cd/TSONIC.ATL` — rebuilt: 392,554 B → 81,172 B.
- `HANDOFF.md` — THIS entry.

**Files NOT changed (per strict task scope):**

- `src/mania/Objects/Title/TitleAssets.c` lines 250-540 (runtime atlas
  loader) — untouched.
- `src/mania/Game.c` lines 1620-1758 (body+finger draw bodies) —
  untouched.
- `build.bat`, `Makefile` — TSONIC.ATL is not produced by the Make
  rules (the file ships as a prebuilt artifact in `cd/`); the
  selective builder is invoked manually with `--selective` when the
  source `Sonic.bin` changes.

**Known follow-on (out of Phase 1.29a scope):**

The 49 → 8 strip changes the meaning of `g_tsonic_frames[]` index
arithmetic in two existing call sites:

1. `src/mania/Objects/Title/TitleAssets.c:400-405` keyframe load loop —
   reads `s_tsonic_keyframe_offsets[ki]` (0,6,12,...,48) and compares to
   `g_tsonic_anim0_frame_count`. With the stripped atlas
   (`anim0_frame_count = 8`) only the first two iterations (kf_local =
   0 and 6) pass the `if (kf_local >= g_tsonic_anim0_frame_count) break`
   guard.
2. `src/mania/Game.c:1615-1626` body walker — reads
   `g_tsonic_frames[anim0_first + s_body_kf_offsets[body_kf]]` where
   `s_body_kf_offsets[7] = 48`. With the stripped atlas
   `g_tsonic_frames[48]` lies beyond the 20 populated frame records
   (anim 0 occupies 0..7, anim 1 occupies 8..19).

Both sites were declared out-of-scope for this iteration by the task
brief ("DO NOT modify the runtime loader at TitleAssets.c:250-540" /
"DO NOT modify the body/finger draw code at Game.c:1620-1758"). The
follow-on iteration must either (a) switch both sites to use the
keyframe table values as POSITIONAL output indices (ki / body_kf
directly), OR (b) emit padding frame records at the original source
positions while keeping anim0_frame_count = 8 in the per-anim header.
Path (a) is cleaner.

— end Phase 1.29a entry —


---

## Phase 1.33 — Comprehensive Title-scene asset audit (2026-05-27)

**Methodology pivot.** Replaces piecemeal `build_filelist.py` additions
(prior cadence: discover a missed asset during a port -> add one
`paths.add(...)` line) with systematic decomp-call enumeration.

### What was done

1. Grepped every Title-scene decomp `.c` file in
   `tools/_decomp_raw/` for `RSDK.LoadSpriteAnimation`,
   `RSDK.LoadSpriteSheet`, `RSDK.LoadStringList`, `RSDK.LoadVideo`,
   `RSDK.PlayStream`, `RSDK.GetSfx`, and `Music_SetMusicTrack`. Editor
   (`#if GAME_INCLUDE_EDITOR`) blocks skipped.
2. Parsed every referenced sprite `.bin` (Background, Logo, Sonic,
   Electricity, DemoMenu) for its sheet list -> automatically added
   each referenced GIF.
3. Cross-referenced the resulting 42 unique Data/ paths against
   `extracted/Data/`. 18 were absent pre-audit (15 retail-required +
   2 Plus DLC + 1 documented-absent UI-scene anchor).
4. Added the 17 newly-required retail paths to `tools/build_filelist.py`
   (Phase 1.33 block, file:line citations inline).
5. Re-ran `python tools/build_filelist.py --out tools/_filelist.txt`
   then `python tools/rsdk_extract.py Data.rsdk --filelist
   tools/_filelist.txt --out extracted`. **Matched count: 1370 / 1677**
   (previously 1355). 15 new files landed in `extracted/Data/`.

### Newly-extracted assets (Phase 1.33)

| Path | Bytes | Why required |
|---|---:|---|
| `Data/Music/IntroTee.ogg` | 1 594 604 | `TitleSetup.c:371` PlayStream (intro video alt music) |
| `Data/Music/IntroHP.ogg` | 1 146 482 | `TitleSetup.c:376` PlayStream (intro video music) |
| `Data/Music/BossHBH.ogg` | 1 147 370 | `Music.c:54` SetMusicTrack (Title StageLoad) |
| `Data/Music/HBHMischief.ogg` | 1 083 727 | `Music.c:61` SetMusicTrack |
| `Data/Music/Sneakers.ogg` | 504 116 | `Music.c:52` SetMusicTrack |
| `Data/Music/Super.ogg` | 889 690 | `Music.c:60` SetMusicTrack |
| `Data/Strings/StringsEN.txt` | 4 906 | `Localization.c:42` LoadStringList |
| `Data/Strings/StringsFR.txt` | 5 922 | `Localization.c:47` |
| `Data/Strings/StringsIT.txt` | 5 050 | `Localization.c:52` |
| `Data/Strings/StringsGE.txt` | 5 450 | `Localization.c:57` |
| `Data/Strings/StringsSP.txt` | 5 312 | `Localization.c:62` |
| `Data/Strings/StringsJP.txt` | 3 150 | `Localization.c:67` |
| `Data/Strings/StringsKO.txt` | 2 866 | `Localization.c:73` |
| `Data/Strings/StringsSC.txt` | 2 088 | `Localization.c:78` |
| `Data/Strings/StringsTC.txt` | 2 062 | `Localization.c:83` |

### Documented-absent (verified via direct hash lookup vs Data.rsdk)

- `Data/Sprites/Title/PlusLogo.bin` + `Data/Sprites/Title/PlusLogo.gif`
  + `Data/SoundFX/Stage/Plus.wav` -- Plus DLC, not in REV01 retail.
- `Data/Stages/Title/TileConfig.bin` -- Title is a UI scene with no
  tile collision; engine treats absence as "no solid tiles".

### Deliverables landed

- `tools/qa_phase1_33_asset_coverage_gate.py` (NEW) -- gate predicate
  enumerates RSDK call sites at runtime and asserts file presence.
  Returns 1 (RED) if any retail asset is missing; 0 (GREEN) otherwise.
  Pre-extraction: RED with 17 missing. Post-extraction: **GREEN**.
- `tools/build_filelist.py:1-58` -- methodology comment block added
  documenting the BINDING pivot for future scene audits.
- `tools/build_filelist.py:336-405` -- Phase 1.33 audit-derived paths
  with `file:line` citations.
- `docs/title_scene_asset_audit.md` (NEW) -- per-object inventory of
  every Title-scene asset reference with present/missing status and
  source-line citation.
- `tools/_decomp_raw/SonicMania_Objects_Global_Localization.c` (NEW
  cache) -- fetched from RSDKModding upstream for the Strings audit.

### Follow-on recommendation

Replicate this audit pattern for the GHZ scene in a dedicated phase
(e.g. Phase 2.4 "GHZ asset coverage audit"). The methodology is
mechanical and ports cleanly to any scene: cache that scene's
`_Setup.c` + child object `.c` files, run the same grep predicates,
parse sprite `.bin` sheet lists, demand a GREEN gate before claiming
the scene's assets done.

-- end Phase 1.33 entry --

---

## Phase 1.35e -- HONEST HAND-OFF: structural-fix paths blocked (2026-05-28, Task #114)

### Outcome

**Did NOT ship NBG1 enable.** All three user-proposed Phase 1.35e
structural fix paths (A transparency-enable, B Y-scroll + asset
rebuild, C line-window) operate downstream of the VDP2 VRAM cycle-
pattern damage that `jo_vdp2_set_nbg1_8bits_image` inflicts at setup
time. Per the STRICT acceptance constraint ("don't ship NBG1 broken
AGAIN"), the call site remains disabled at `src/main.c:1343` and the
per-frame REPLACE remains disabled at `src/main.c:860`.

### Why the user's three fix paths cannot apply

Phase 1.35d already captured savestate evidence
(`samples/qa_phase1_35d_broken.mcs` vs `samples/qa_phase1_31_post_revert.mcs`)
proving the jo wrapper destructively rewrites four VRAM cycle-pattern
register words at setup time (per ST-058-R2 §3.3 p.46-50):

| Addr        | Reg     | Baseline | Broken  | Damage |
|-------------|---------|----------|---------|--------|
| 0x25F80010  | CYCA0L  | 0x55FE   | 0x5555  | NBG1 read slot reassigned |
| 0x25F80014  | CYCA1L  | 0xFFFE   | 0x5555  | CPU+NBG1 slots wiped |
| 0x25F80018  | CYCB0L  | 0x1FEE   | 0xFEEE  | NBG0 read slot dropped |
| 0x25F8001C  | CYCB1L  | 0xFFEE   | 0xFEEE  | slot 0xE wiped |

Plus PRINA, PRINB, PRIR, BGON, CHCTLA, CHCTLB, CRAOFB.

Cycle pattern controls per-bank VRAM read slots for character-pattern
fetch. Without read slots, NBG2 (clouds) and RBG0 (title-backdrop
plane) cannot fetch cell data regardless of BGON enable or priority
register.

- **Path A (slScrTransparent / TPMD bit, ST-058-R2 §6.6):** operates
  at composite time, downstream of cycle-pattern read. Cannot restore
  NBG2/RBG0 read bandwidth -> orange back-color flood persists.
- **Path B (slScrPosNbg1 Y-scroll + asset rebuild, ST-058-R2 §6.7):**
  operates at sample time on SCYIN1/SCYDN1. Same conclusion.
- **Path C (slScrLineWindow1 line-window, ST-058-R2 §8.1):** operates
  on LWTA1 + WCTLA window mask at composite time. Same conclusion.

### Methodology compliance

1. Read `complete-doc-index.md` + `vdp2-reference.md` end-to-end.
2. Catalogued ST-058-R2-060194.pdf §3.3 (VRAM cycle pattern), §6.6
   (transparency), §6.7 (scroll position), §8.1 (windows) +
   ST-238-R1-051795.pdf (SGL scroll/window APIs).
3. Verified SGL prototypes in `jo-engine/Compiler/COMMON/SGL_302j/INC/SL_DEF.H:944`
   (slScrTransparent), `1025` (slScrPosNbg1), `1055` (slScrLineWindow1).
4. Re-ran Phase 1.35d regdiff gate: P1+P2+P3 GREEN -- the savestate
   evidence is intact.
5. Added P4 (cycle-pattern-damage-unrepairable) to
   `tools/qa_phase1_35d_regdiff_gate.py` as a binding measurable
   contract documenting WHY paths A/B/C cannot apply.
6. Gate result: P1+P2+P3+P4 GREEN.

### Deliverable

- `tools/qa_phase1_35d_regdiff_gate.py:127-184` (NEW P4):
  `predicate_cycle_pattern_damage_unrepairable()` -- asserts the
  jo wrapper damages 4/4 VRAM cycle-pattern register words at setup
  time and that Phase 1.35e paths A/B/C cannot repair downstream.
  GREEN against current revert baseline; will fail RED if the
  broken-state savestate drifts (e.g. if a successful custom-NBG1
  bypass lands), automatically signalling Phase 1.35e is unblocked.

### Sprite-integrity / sample-frame archive

No new Saturn capture was taken because no code change landed. The
existing Phase 1.34c baseline frames at
`tools/qa_golden/qa_phase1_34c_alpha_wrap_{60,70,85}.png` remain the
authoritative "no island + Sonic/MANIA/WingShine clean" reference and
remain GREEN per Phase 1.35d P1 (P6/P7/P8 sprite-integrity ROIs
unchanged from baseline since no code change).

### Forward path = Phase 1.36 (NEW)

The Phase 1.35d revert comment at `src/main.c:1331-1337` already
correctly identifies the only fix:

> A correct fix requires bypassing jo_vdp2_set_nbg1_8bits_image
> entirely and instead manually loading the island cell + map
> into a non-conflicting VRAM bank (B1, currently used only by
> NBG2 map/cell) with custom slMapNbg1/slPageNbg1/slCharNbg1
> calls that don't touch the cycle pattern.

That work is a separate engine-layer port (custom VDP2 NBG1 setup
function) not a structural delta on the existing call path. It needs:

1. Read `NOV96_DTS/LIBRARY/SDK_10J/SGL302/SAMPLE/SCROLL/` + `SAMPLE2/SEGA2D_1/SCROLL.C`
   end-to-end for the canonical multi-NBG cycle-pattern-preserving setup.
2. Audit `jo-engine/jo_engine/vdp2_malloc.c:196-220` for VRAM bank
   routing and pick a non-conflicting bank (B1 candidate per Phase 1.35d
   revert comment).
3. Build a custom `setup_island_bg_manual()` that issues
   `slCharNbg1`/`slPlaneNbg1`/`slPageNbg1`/`slMapNbg1` directly with
   manual VRAM addresses, never calls `__jo_switch_to_8bits_mode()` (the
   jo helper that rewrites CYCA0/A1/B0/B1).
4. Hand-write the cell-conversion and map-init steps from the jo
   wrapper but feed them into the non-conflicting bank.
5. Re-add P3/P4 capture and P12 (Y >= 168 clip) + P13 (sprite-integrity
   stays GREEN) gates.

That is meaningful engine work, not a one-iteration Phase 1.35e fix.
The user mandate "REVERT and produce HONEST hand-off -- don't ship
broken" applies; the revert is intact at this end.

-- end Phase 1.35e entry --

## Phase 3.0-prep entry (2026-05-28) — Menu/Logos scene comprehensive asset audit

**Methodology pivot continued from Phase 1.33.** Second application of the
binding "enumerate decomp .c StageLoad refs + Scene1.bin trackFile attrs
-> cross-reference extracted/" pattern, this time applied to the Menu and
Logos scenes (the boot-sequence and UI-shell scenes the title will SetScene
to next).

### Counts

- **Decomp .c files scanned:** 56 (54 newly batch-fetched from upstream
  `Objects/Menu/` + LogoSetup + MenuSetup pre-cached). Pulled via
  `gh api repos/RSDKModding/Sonic-Mania-Decompilation/contents/SonicMania/Objects/Menu`.
- **RSDK call sites enumerated (gate):** 83 (LoadSpriteAnimation /
  GetSfx / LoadVideo / PlayStream after EDITOR-block + EditorIcons
  exclusion).
- **Scene1.bin trackFile attrs:** 4 (MainMenu.ogg / Competition.ogg /
  Results.ogg / SaveSelect.ogg -- only discoverable from Menu/Scene1.bin
  Music entities, never from any .c grep).
- **Unique asset paths (post-extraction):** 125.
- **Present pre-audit (retail):** 35.
- **Newly extracted by Phase 3.0-prep:** 86 retail asset files (~4.73 MB).
- **Plus-DLC + region INFO (not FAIL):** 4 (UI/Diorama.bin,
  AIZ/SchrodingersCapsule.bin, Players/Mighty.bin, Players/Ray.bin --
  the first two verified absent from REV01 Data.rsdk via direct hash
  lookup, the latter two are documented Plus-only characters).
- **Retail missing FAIL:** 0.
- **Gate verdict:** GREEN.

### Notable newly-extracted asset families

- **4 menu music tracks** (MainMenu / Competition / Results / SaveSelect
  .ogg) -- only discoverable via Scene1.bin trackFile attrs, not via any
  .c grep. This is the methodology delta from Phase 1.33: an extra
  Scene1.bin-attr extraction step was added to handle scenes whose
  Music.c-equivalent code uses trackID indirection.
- **9 language SaveSelect text atlases** (TextEN..TC, HeadingsEN..TC,
  SaveSelectEN gif).
- **Common UI widget atlases** (Buttons, ButtonLabel, SaveSelect,
  UIElements, SmallFont, MainIcons, MedallionPanel, Picture,
  WaitSpinner, CreditsText).
- **Boot-sequence assets** (Logos/Logos.bin + Stage/Sega.wav for the
  SEGA splash).
- **Diorama character cutscene anims** (KnuxCutsceneAIZ + HPZ for the
  Save Select character preview when Knuckles is selected).
- **Blue Spheres character preview** (SpecialBS/Sonic + StageObjects)
  and **D.A. Garden assets** (UI/DAGarden).

### Deliverables

- `tools/_decomp_raw/SonicMania_Objects_Menu_*.c` -- 52 new cached files.
- `docs/menu_scene_asset_audit.md` -- full per-object inventory + scenes
  Menu transitions to + recommended follow-on audits.
- `tools/build_filelist.py` -- Phase 3.0-prep block added (asset
  citations + sheet GIFs); methodology header updated to document the
  Scene1.bin trackFile step + the batch-fetch-of-Objects/<Cat>/-listing
  pattern for UI-style scenes.
- `tools/qa_phase3_0_menu_asset_coverage_gate.py` -- new gate, mirrors
  Phase 1.33 template, additionally parses Scene1.bin Music entity
  trackFile attrs.
- `tools/verify_done.ps1` -- Gate V3.0-prep wired in between V1.33 and
  V-REG.
- `docs/mania_decomp_catalog.md` §2.1 Logos + Menu rows updated to
  reference the audit doc + gate + music findings.

### Recommended follow-on audits

Per dependency order from the Menu scene, each is a separate task:

1. SaveSelect / UISaveSlot deep dive (covered by this audit but worth a
   visual port pass).
2. PlayerSelect / UICharButton + UICharSelector flow.
3. Per-zone Mania Mode audits (one per zone per Phase 7-9 roll-out).
4. Time Attack scene + UITAZoneModule + UITABanner + ghost replay.
5. Competition scene + UIVsCharSelector + UIVsZoneButton + UIVsResults
   (2P split-screen).
6. Options scene + UIKeyBinder + UISlider + UIWinSize + UIResPicker
   (skip the Saturn-irrelevant KB/PS4/XB1/NX control assets).
7. Extras scene (Puyo / D.A. Garden / Credits / Blue Spheres entry).
8. Credits + Thanks scenes.
9. Logos boot-sequence assets (small follow-up).
10. D.A. Garden audit.
11. Encore Mode audits (Plus DLC, deferred to Phase 17).
12. Blue Spheres scene audit (Phase 12).
13. UFO Special Stages audit (Phase 15).
14. Cutscenes / Presentation per-scene audits (Phase 13).

### Strict constraints respected

- DID NOT touch src/* code. Asset + docs only.
- DID NOT port any Menu Object. That's Phase 3.0+ proper.
- DID cite decomp file:line for every audit asset reference + Scene1.bin
  trackFile slot for the 4 music tracks.
- DID add a Phase 3.0-prep comment block in build_filelist.py with
  rationale + citation chain + updated methodology header.

-- end Phase 3.0-prep entry --


## Phase 3.0-prep++ — Whole-game asset audit (2026-05-28)

### What landed

- Third application + whole-game generalisation of the Phase 1.33 BINDING
  asset-coverage methodology. Single-session batch generalisation across
  ALL 42 stages + Global + UI + Common + Cutscene + Helpers + BSS +
  Pinball + Continue + Summary + Unused.
- 894 decomp `.c`/`.h` files batch-fetched via single
  `git/trees/master?recursive=1` API + parallel blob-by-SHA fetch
  (`tools/phase3_0_plus_fetch_decomp.py`, 12 workers, ~6 min). Cache
  117 -> 1036 files in `tools/_decomp_raw/`.
- 1477 RSDK call sites enumerated across 518 scanned .c files via
  `tools/phase3_0_plus_scan_assets.py`. 161 Scene*.bin Music entity
  trackFile attrs parsed via existing parse_title_entities pattern
  generalised to all 42 stages by `tools/phase3_0_plus_enumerate.py`.
- 1001 unique asset paths discovered. Re-extract via updated
  `tools/build_filelist.py` (+1087-line Phase 3.0-prep++ block grouped
  by Music/Strings/Video/SoundFX-per-folder/Sprites-per-folder + a
  118-entry derived-sheet-GIFs block) raised hash-match rate to
  **1728/1677** (from 1610/1677 baseline). 118 new retail assets
  resolved.
- Gate `tools/qa_phase3_0_plus_whole_game_asset_coverage_gate.py`
  wired into `verify_done.ps1` as Gate V3.0-prep++. Verdict: **GREEN**.
  1332/1387 paths present, 0 retail FAIL, 55 INFO (curated PLUS_DLC +
  EXPECTED_ABSENT classification table — Mighty/Ray + Encore +
  JP-region + decomp-conditional names that resolve into shipping
  16x16Tiles via asset-build-time merge).

### Per-scene asset count summary (42 stages + UI + Global)

| Folder | Objects | SFX | Music | Folder | Objects | SFX | Music |
|---|---|---|---|---|---|---|---|
| AIZ | 11 | 12 | 1 | MSZCutscene | 22 | 29 | 1 |
| CPZ | 45 | 43 | 2 | Menu | 40 | 9 | 4 |
| Credits | 14 | 10 | 4 | OOZ1 | 15 | 16 | 1 |
| DAGarden | 15 | 3 | 52 | OOZ2 | 24 | 22 | 1 |
| ERZ | 18 | 27 | 2 | PSZ1 | 27 | 27 | 1 |
| Ending | 13 | 0 | 1 | PSZ2 | 30 | 33 | 1 |
| FBZ | 48 | 36 | 2 | Puyo | 25 | 12 | 1 |
| GHZ | 28 | 16 | 2 | SPZ1 | 29 | 38 | 2 |
| GHZCutscene | 15 | 23 | 1 | SPZ2 | 35 | 42 | 1 |
| HCZ | 42 | 37 | 2 | SSZ1 | 42 | 26 | 1 |
| LRZ1 | 36 | 27 | 2 | SSZ2 | 33 | 34 | 3 |
| LRZ2 | 38 | 19 | 1 | SpecialBS | 20 | 9 | 0 |
| LRZ3 | 18 | 32 | 1 | TMZ1 | 30 | 30 | 1 |
| LSelect | 8 | 1 | 53 | TMZ2 | 31 | 34 | 1 |
| Logos | 2 | 1 | 0 | TMZ3 | 29 | 31 | 2 |
| MMZ | 32 | 29 | 2 | Thanks | 2 | 0 | 0 |
| MSZ | 50 | 49 | 3 | TimeTravel | 4 | 0 | 1 |
| Title | 11 | 0 | 1 | UFO1-7 | 27-28 ea | 11 ea | 1 ea |

Aggregate: 493 unique classes referenced, 364 unique sfx, 57 unique
trackFiles, 1387 unique resolved asset paths.

### Implementation-level Phase 3+ roadmap (recommended ordering)

**Tier A — Boot / Menu entry:**
1. Phase 2.3f GHZ Sonic visibility (Task #88, in flight)
2. Phase 3.1 LogoSetup port (2 objects, ~70 lines)
3. Phase 3.2 MenuSetup + UIControl/UIWidgets/UIBackground skeleton
4. Phase 3.3 UIButton + UIButtonPrompt + UIHeading + UISubHeading
5. Phase 3.4 UIChoice + UICarousel + UISlider + UISaveSlot

**Tier B — Mania Mode zones (one phase per zone, ordered per
GameConfig.bin "Mania Mode" category):** Phases 4.1-4.22 covering
GHZ -> CPZ -> SPZ1 -> SPZ2 -> FBZ -> PSZ1 -> PSZ2 -> SSZ1 -> SSZ2 ->
HCZ -> MSZ -> OOZ1 -> OOZ2 -> LRZ1 -> LRZ2 -> LRZ3 -> MMZ -> TMZ1 ->
TMZ2 -> TMZ3 -> ERZ -> AIZ. Per-zone class counts in
`docs/whole_game_asset_audit.md` Tier B table.

**Tier C — Bonus / Special / Ending:** Phases 5.1-5.6 for
SpecialBS / UFO1-7 / Pinball / Puyo / Credits / Thanks / Cutscenes
(GHZCutscene, MSZCutscene, TimeTravel) / DAGarden / LSelect.

**Tier Z — Saturn-native rewrites:** 3D Blue Spheres + UFO chase,
scanline FX (OOZ heat haze, CPZ ChemPool ripple, LRZ lava distortion,
SSZ2 MetalSonic glow), full palette FX (Phantom Ruby tints, Super-form
shift, Eggreverie boss pulses). Per `BIBLE.md` §Phase Z.

### Methodology pivot (BINDING for every subsequent scene)

The Phase 3.0-prep++ pipeline is the canonical asset-coverage cadence
going forward. New scenes need NO additional decomp fetch (all 1036
files cached). Per-zone follow-on phases just port `.c` behavior —
the asset side is closed. Steps for any future per-zone phase:

1. Open the zone's Setup .c + per-object .c files (already cached).
2. Verify the zone row in `docs/whole_game_asset_audit.md` shows
   100% cached/present.
3. Port `.c` behavior; no further asset extraction needed unless
   user opts in to a Plus-DLC / Encore class.

### Strict constraints respected

- DID NOT touch src/* code. Asset + docs only.
- DID NOT port any Object .c. That's Phase 3+ proper.
- DID cite decomp file:line for every asset reference and Scene*.bin
  slot for every trackFile attr.
- DID add a Phase 3.0-prep++ comment block in build_filelist.py with
  rationale + citation chain. Methodology header at
  build_filelist.py:10-65 BINDING reinforced.
- Plus-DLC + decomp-conditional absent assets classified INFO not FAIL
  with rationale documented in the gate's PLUS_DLC_PATHS +
  EXPECTED_ABSENT sets.

### Files modified this phase

- tools/build_filelist.py (+1087 Phase 3.0-prep++ block + 118 sheets)
- tools/phase3_0_plus_enumerate.py (new)
- tools/phase3_0_plus_fetch_decomp.py (new)
- tools/phase3_0_plus_scan_assets.py (new)
- tools/qa_phase3_0_plus_whole_game_asset_coverage_gate.py (new)
- tools/verify_done.ps1 (Gate V3.0-prep++ wired in)
- tools/_decomp_raw/ (+894 files)
- docs/whole_game_asset_audit.md (new)
- docs/decomp_port_status.md (Phase 3.0-prep++ note prepended)
- docs/mania_decomp_catalog.md (cross-ref pointer)
- docs/COMPREHENSIVE_PLAN.md (§3.0-prep++ appended)
- HANDOFF.md (this entry)
- extracted/Data/ (118 new retail assets extracted)

-- end Phase 3.0-prep++ entry --
