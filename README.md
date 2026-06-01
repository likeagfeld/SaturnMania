# Sonic Mania — Sega Saturn (native reimplementation)

A Saturn-native reimplementation of Sonic Mania's engine, built on **Jo Engine**
(C, `sh-elf-gcc`). This is **not** a recompile of the RSDKv5 C++ decompilation —
that is provably impossible on Saturn hardware (see "Why not a direct port"
below). Instead we reproduce Mania's gameplay/physics on Saturn hardware and feed
it level/art data **converted offline** from the assets you legally own.

## Why not a direct port (the hard numbers)

| Constraint | Mania / RSDKv5U | Saturn | Verdict |
|---|---|---|---|
| Engine language | RSDKv5 engine is 96.1% C++ | SGL/Jo are C, no C++ runtime | can't recompile |
| Static memory pool | engine reserves **74 MB** at startup (`Storage.cpp::InitStorage`: 24+32+8+8+2 MB) | **2 MB** main RAM | 37x over budget |
| Framebuffer | linear `uint16` RGB565, 424x240, CPU software-blit | VDP1 sprites + VDP2 tile BGs, 4 KB CRAM | incompatible model |
| Min. proven HW | ~1 GHz ARM, 256 MB+ (mobile/Switch) | 2x SH-2 @ 28.6 MHz, 2 MB | ~35x CPU / ~100x RAM deficit |

The data file (~180 MB) fits on a CD (~650 MB), so **full content is a streaming
problem, not a fit problem** — which Saturn does well (cf. Sonic Jam, Sonic 3D).

## Prerequisites

1. **Docker** (verified building with Docker 29.x on Windows 11).
2. **Jo Engine** — cloned into `jo-engine/` (its bundled `sh-none-elf` GCC 8.2.0
   toolchain is used inside the container; nothing to install on the host).
3. **`joengine-saturn` Docker image** — built once from the included `Dockerfile`:
   ```
   docker build -t joengine-saturn:latest .
   ```
4. **Sonic Mania assets you own** — `Data.rsdk` (present here, ~174 MB, verified
   RSDKv5 datapack: 1677 files). The Phase 0 converter reads it; nothing
   copyrighted ships in this repo.
5. **An emulator** (Mednafen or Kronos) or real hardware / ODE to test the ISO.

## Build (verified working)

```
build.bat            # Windows wrapper, or:
docker run --rm -v "%cd%":/work -w /work joengine-saturn:latest make
```

Produces `game.iso` + `game.cue`. Run in Mednafen/Kronos, or copy to a Satiator
SD card. Last verified build: game.iso 495,616 bytes, clean (0 warnings/errors).

## Roadmap (each phase is independently testable)

- **Phase 0** — `tools/` offline RSDK->Saturn asset converter (Python).
- **Phase 1** — engine core + VDP2 multi-layer parallax zone renderer.   <- this build boots
- **Phase 2** — Sonic physics + VDP1 player sprite + per-character variants.  <- physics core landed
- **Phase 3** — TileConfig collision + entity engine (rings, springs, badniks).
- **Phase 4** — CD streaming + SCSP audio + re-authored music/cutscenes.
- **Phase 5** — game shell, special stages (VDP1 3D / VDP2 RBG0), bosses, and
  full content integration (12 zones / ~32 acts / 5 characters) end-to-end.

## Current status

Phase 1 scaffold + Phase 2 physics core. Boots to a title screen; START enters a
bootstrap flat-ground world running the full Sonic ground/air physics model. The
flat world is real working collision, replaced by converted tile geometry in
Phase 3. HUD prints live physics state for verification.
