# SaturnMania — Setup

This repository contains the **source code, build tooling, and the Saturn
toolchain** for a from-scratch port of Sonic Mania to the Sega Saturn. It does
**not** contain any Sonic Mania game data — no sprites, audio, levels, or the
`Data.rsdk` datapack — because that content is copyrighted by SEGA. You supply
your own legally-obtained copy of `Data.rsdk`; the build pipeline extracts and
converts it locally into Saturn-native assets that never leave your machine.

You also supply your own **Sega Saturn BIOS** for the emulator (also
copyrighted; not redistributed here).

---

## What is and isn't in this repo

| In the repo (tracked) | NOT in the repo (you provide / regenerated locally) |
|---|---|
| `src/` — the Saturn port (engine-compat layer + decomp object ports) | `Data.rsdk` — your own Sonic Mania PC datapack |
| `tools/` — extraction, asset converters, QA harness | `extracted/` — files unpacked from your `Data.rsdk` |
| `jo-engine/` sources + bundled SGL toolchain (`Compiler/`) | `cd/` payload — Saturn assets converted from `Data.rsdk` |
| `rsdkv5-src/` — RSDKv5 engine reference sources | `cd_audio/` — CD-DA music converted from `Data.rsdk` |
| `docs/`, `*.md`, `Makefile*`, `build.bat`, `Dockerfile` | Sega Saturn BIOS (`.mednafen/firmware/`) |
| `.mednafen/mednafen.cfg` — emulator pad map / QA settings | build outputs (`game.iso`, `game.elf`, savestates, captures) |

Only the three hand-authored Saturn ISO volume-descriptor text files
(`cd/ABS.TXT`, `cd/BIB.TXT`, `cd/CPY.TXT`) are tracked under `cd/`; every other
`cd/` file is regenerated from your `Data.rsdk`.

---

## Prerequisites

- **Windows 11** with **PowerShell 7+** (`pwsh`).
- **Docker Desktop** — the Saturn cross-compile runs in a small Debian image
  that wraps the bundled `sh-none-elf` GCC 8.2.0 toolchain (see `Dockerfile`).
- **Python 3.10+** on `PATH` (asset extraction + converters).
- **Mednafen 1.32.1** (for running/QA; install via WinGet:
  `winget install MednafenTeam.Mednafen`) plus your own Saturn BIOS placed where
  Mednafen expects it (`.mednafen/firmware/` — `sega_101.bin` /
  `mpr-17933.bin`).
- Your own **`Data.rsdk`** from a PC (Steam) copy of Sonic Mania.

---

## Quick start

1. Place your `Data.rsdk` at the repository root (`D:\...\SaturnMania\Data.rsdk`).
2. Build the Docker image once:
   ```
   docker build -t joengine-saturn:latest .
   ```
3. Run setup:
   ```
   pwsh tools/setup.ps1
   ```
   This (a) verifies prerequisites and your `Data.rsdk`, (b) extracts the
   datapack into `extracted/`, then (c) invokes `build.bat`, which regenerates
   the runtime asset set and produces `game.iso` / `game.cue`.
4. Run it:
   ```
   mednafen game.cue
   ```

---

## Honest limitation — asset regeneration (read this)

`build.bat` deterministically regenerates the **runtime-critical** asset subset
from `extracted/Data/` on every build: the HUD atlas, the act-intro TitleCard
atlas, the global sound-effect PCMs, the canonical GHZ player-spawn coord, and
the GHZ scene table.

The remaining **static visual assets** (entity sprite atlases, GHZ
tile/collision/sky layers, the title backdrops, and the Cinepak intro) were
authored incrementally over the project's history through per-asset converter
invocations that are **not yet consolidated into a single script**. The
converter *programs* are all in `tools/` (tracked) — see the map below — but
their exact command lines are not all captured in `build.bat`.

**Consequence:** a from-zero clean-clone build will regenerate the runtime
subset but is **not yet guaranteed** to repopulate every static `cd/` asset
without running the remaining converters. Consolidating all converters into
`tools/setup.ps1` so a clean clone is fully turn-key is the next hardening step
(tracked). This note exists because the asset pipeline has not been validated
end-to-end from an empty `cd/` in a single pass — rather than claim turn-key
that hasn't been verified.

### Asset family -> generator (reference for completing the pipeline)

| `cd/` asset family | Generator (`tools/`) |
|---|---|
| `HUD.SP2/.MET`, `TITLCARD.SP2/.MET`, entity atlases (`RING`, `ITEMBOX`, `SPRING`, `SIGNPOST`, `SPIKES`, `MOTOBUG`, `BUZZ`, `CHOPPER`, `CRABMEAT`, `BATBRAIN`, `NEWTRON`, `PLATFORM`, `SPIKELOG`, `BRIDGE`) | `build_entity_atlas.py` |
| `*SFX.PCM` | `convert_audio.py` |
| `GHZ*SURF.BIN` (collision/heightmap) | `build_collision.py`, `build_heightmap.py` |
| `GHZ*FG.CEL/.PAL/.PAT/.TMP` (foreground cells) | `convert_vdp2.py`, `convert_vdp2_cells8.py`, `convert_vdp2_tilemap.py` |
| `GHZ*SKY.DAT/.PAL`, `CLOUDS.DAT/.PAL` | `build_clouds_bg.py` |
| `TITLE.DAT/.PAL`, `TITLE3D.DAT/.PAL/.ATL`, `ISLAND.DAT/.PAL` | `build_title3d_atlas.py`, `build_island_bg.py`, `build_title_island_bg.py`, `extract_title_island.py` |
| `TSONIC.ATL` | `build_titlesonic_atlas.py` |
| `ELECTRA.ATL` | `build_electricity_atlas.py` |
| `LOGOS.ATL`, `MLOGO.SPR` | `build_logos_atlas.py` |
| `INTRO.CPK/.BIN` (Cinepak intro) | `convert_stream.py` |
| `GHZSCN1.BIN`, `GHZ1SPWN.BIN` | copied / `extract_ghz_spawn.py` (done by `build.bat`) |

---

## Extraction details

`tools/setup.ps1` runs:

```
python tools/rsdk_extract.py Data.rsdk --filelist tools/maniafilelist.txt --out extracted
```

The cipher key for each file in an RSDKv5 datapack is `MD5(UPPERCASE(path))`, so
extraction is driven by the candidate path list in `tools/maniafilelist.txt`.
