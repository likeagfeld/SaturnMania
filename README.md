# SaturnMania

Sonic Mania running on a Sega Saturn.

Not a demake, not a fan remake. This is the actual RSDKv5 engine and the actual
decompiled Sonic Mania game code, compiled for the Saturn's SH-2 processors,
reading a retail `Data.rsdk` off the disc at runtime. The decompiled objects —
Player, Ring, TitleCard, the Angel Island cutscene, all of it — run as-is. What I
wrote is the layer underneath: the code that takes the engine's draw calls, audio
calls, and file reads and maps them onto VDP1 sprites, VDP2 tile planes, the SCSP,
and the CD block.

Right now it boots and plays through the whole opening: SEGA and RSDK logos, the
title screen, the menu, the Tornado biplane intro with Sonic on the wing, the warp
into Green Hill, the "GREEN HILL ZONE ACT 1" card, and then you're playing Green
Hill Act 1 with real Mania physics. It's slow in places and plenty is missing.
But it's the real game.

Here's what it looks like as of July 9, 2026:

https://github.com/likeagfeld/SaturnMania/raw/master/docs/media/progress-2026-07-09.mp4

## Why this wasn't supposed to be possible

The conventional wisdom was that RSDKv5 can't run on a Saturn. It's C++, it
reserves something like 74 MB of static pools, it draws into a linear framebuffer
the Saturn doesn't have, and the game data is ~180 MB against 2 MB of work RAM.

None of those held up:

- The `sh-none-elf` GCC that ships with Jo Engine compiles the C++ fine. The
  engine links right next to the C hardware code.
- The pools got retargeted to Saturn sizes and backed by the 4 MB RAM cart.
  Entities, tile layers, and sprite sheets live in sliding windows that follow the
  camera instead of sitting resident.
- DrawSprite goes through a VDP1 upload cache keyed by sprite rect. Tile layers
  are VDP2 scroll planes refreshed during vblank. The rotating island on the title
  screen is an actual VDP2 mode-7 plane with a per-scanline coefficient table,
  which I'm still a little amazed works.
- 180 MB of data is a streaming problem, not a fitting problem. The disc is right
  there.

## What's in here

The layout takes some getting used to, so here's the short version:

| Where | What |
|---|---|
| `rsdkv5-src/` | the RSDKv5 engine (decompiled), built for SH-2 |
| `tools/_decomp_raw/`, `src/mania/` | the decompiled Mania game objects |
| `platform/Saturn/`, `src/rsdk/` | the Saturn backend replacing the engine's platform layer |
| `tools/_portspike/_p6/` | build scripts, the frame loop, the VDP1/VDP2 code |
| `jo-engine/` | Jo Engine, vendored |
| `tools/qa_*.py`, `tools/qa_*.ps1` | the test harness (reads emulator memory live) |
| `docs/` | plans and notes |

Anything starting with an underscore is scratch from a working session and can be
ignored or deleted.

A note on the test harness, because it's the part I'd actually recommend stealing:
almost nothing here gets called "fixed" from a screenshot. The harness boots the
game headless in an emulator and reads Saturn memory over UDP while it runs —
entity positions, animation state, frame counters, per-frame profiling counters
compiled into the build. Bugs get a failing check written first, then the fix,
then the check goes green. That discipline is most of the reason this project
still moves.

## Building it

You need Docker (the toolchain runs in a container — build the image once with
`docker build -t joengine-saturn:latest .`), your own `Data.rsdk` from a copy of
Sonic Mania you bought, and an emulator with a real Saturn BIOS. Mednafen or
RetroArch's Beetle Saturn core both work. **Set the emulator to the 4 MB extended
RAM cart** or it will hang before the title screen.

```
MSYS_NO_PATHCONV=1 docker run --rm -e P6_FRONTEND_CHAIN=1 -e P6_GHZCUT_BOOT=1 \
    -v "%cd%":/work -w /work joengine-saturn:latest \
    bash tools/_portspike/_p6/build_shipping.sh
python tools/build_cdda.py cd_audio/track02.wav cd_audio/track03.wav \
    --cue-out game.cue --iso game.iso
```

That's the full-chain build — logos through the intro into Green Hill. The second
command muxes the CD audio tracks in (the toolchain only emits a data track).
Drop the two `-e` flags for a build that boots straight into Green Hill.
`pwsh tools/_play.ps1` launches whatever you built in RetroArch at normal speed
with sound.

## What works and what doesn't

Works: the entire opening sequence end to end, and Green Hill Act 1 with the
decompiled Player (slopes, spindash, drop dash, rolling — it's Mania's actual
physics code), rings, monitors, platforms, several badniks, springs, bridges,
the signpost, dying and respawning, stage music on CD audio, sound effects.

Doesn't, yet: the frame rate is the big one — the front end runs around 7 to 20
fps depending on the scene, and making that not true is the current focus. Only
Green Hill exists. A few object types are stuck behind real memory walls
(CollapsingPlatform's entity is bigger than any pool slot; BreakableWall and
Water don't fit the code overlay until I re-carve it). Assorted visual polish.

## Credit where it's due

- [RSDKModding's Sonic Mania decompilation](https://github.com/RSDKModding/Sonic-Mania-Decompilation)
  is the game logic itself. The rule in this repo is that their code is the source
  of truth — everything is translated from it, never reinvented.
- [RSDKModding's RSDKv5 decompilation](https://github.com/RSDKModding/RSDKv5-Decompilation)
  is the engine, and big pieces of it compile straight into this build.
- [Jo Engine](https://jo-engine.org/) by Johannes Fetz is the Saturn hardware
  layer and the toolchain everything builds with.

## Legal

There is no Sonic Mania content in this repo. You bring your own `Data.rsdk`.
The decompilations are community projects with their own licenses; the Saturn
port code is mine. Non-commercial fan project, no affiliation with Sega.
