# SaturnMania

Sonic Mania, running on a Sega Saturn.

This isn't a remake or a hand-drawn demake. It's the actual RSDKv5 engine and the
Sonic Mania decompilation, recompiled for the Saturn's SH-2 and reading a retail
`Data.rsdk` at runtime. The decompiled game objects — `Player`, `Ring`, `TitleCard`,
the whole set — run unmodified. A Saturn-side layer stands in for the engine's
platform code and maps its draw/audio/file/input calls onto VDP1 sprites, VDP2
tilemaps, the SCSP, and the CD block.

The target is the real game, frame for frame. It's nowhere near finished, but it
boots, it plays Green Hill Zone, and the entire opening runs end to end: the SEGA
and RSDK logos, the title screen, the menu, the Tornado biplane intro, the warp
into Green Hill, and the act card.

## The part that was supposed to be impossible

For a long time the assumption was that you couldn't put RSDKv5 on a Saturn. It's
C++, the static entity pools reserve tens of megabytes, and the renderer wants a
linear framebuffer the Saturn doesn't have. Each of those turned out to be an
engineering problem, not a wall:

- **C++ on the Saturn.** The `sh-none-elf` GCC 8.2 toolchain that ships with Jo
  Engine compiles the decomp to SH-2. The engine object code links next to the C
  hardware layer.
- **~74 MB of pools.** Retargeted to Saturn sizes, backed by the 4 MB extended-RAM
  cart, and streamed. Entities, tile layers, and sprite sheets live in camera-local
  sliding windows instead of all-resident arrays.
- **No framebuffer.** `DrawSprite` becomes a rect-keyed VDP1 upload cache. Tile
  layers become VDP2 scroll planes refreshed in vblank. The title's rotating island
  is a genuine VDP2 RBG0 mode-7 floor driven by a per-line coefficient table.
- **~180 MB of assets, 2 MB of RAM.** That's a streaming problem, not a fit
  problem: windowed GFS reads straight off the on-disc pack.

## How it's put together

Three layers, bottom to top:

- `jo-engine/` + SGL — the Saturn hardware abstraction (VDP1/VDP2, SCSP, CD block,
  the two SH-2s).
- `rsdkv5-src/`, `src/rsdk/`, `platform/Saturn/` — the RSDKv5 engine plus the Saturn
  backend that replaces its platform layer (rendering, audio, file I/O, input).
- `tools/_decomp_raw/` and `src/mania/` — the verbatim Mania decompilation. These
  are the game logic, and they're meant to read as a mechanical translation of the
  decomp rather than something reinvented.

`tools/_portspike/_p6/` is where the engine gets packed into the Saturn image, and
where most of the glue and the QA harness live.

## What works right now

The full-chain build boots hands-free through the whole front end and into gameplay:

- **The opening, start to finish.** The SEGA/RSDK logos, the title screen (the
  SONIC MANIA logo and the Sonic-in-the-ring animation), the main menu, then the
  Angel Island intro — Sonic riding the top wing of the Tornado over the ocean,
  Tails flying it — the warp, and the drop into Green Hill Zone with the
  "GREEN HILL ZONE — ACT 1" card sliding in.
- **Green Hill Act 1.** The real decompiled `Player` (Mania physics, slopes,
  spindash/peelout/drop dash, rolling), camera follow, foreground tile collision,
  rings, several badniks, the act signpost, death and respawn, CD-DA stage music,
  and SCSP sound effects.

That is the engine's own code executing on hardware. Every claim above was checked
against emulator captures (RetroArch's Beetle Saturn core, which is Mednafen's
Saturn emulation) rather than assumed from the source.

## What doesn't yet

- **Framerate.** The front end currently renders at roughly 8-13 fps, so the
  animations look like a slide show. It's VDP1 fill-bound — too much sprite
  overdraw. Moving the static art onto hardware VDP2 layers and offloading work to
  the second SH-2 is the active work.
- **Coverage.** Only Green Hill is playable. Other zones, the special and bonus
  stages, and part of Act 1's object set aren't in yet.
- **Polish.** Odd color washes, the occasional missing background layer, and a
  handful of cosmetic gaps. The running lists live in `docs/`.

This is multi-year work, and it advances one measured, gated port at a time.

## Building it

You need:

1. **Docker.** The toolchain runs inside the image; build it once with
   `docker build -t joengine-saturn:latest .`
2. **Your own `Data.rsdk`** from a legitimate copy of Sonic Mania. Nothing
   copyrighted is in this repo.
3. **An emulator with a real Saturn BIOS** — Mednafen, or RetroArch with Beetle
   Saturn — configured for the **4 MB extended-RAM cart**. Without that cart the
   game stalls before the title screen.

The full-chain build (logos through the intro into Green Hill):

```
MSYS_NO_PATHCONV=1 docker run --rm -e P6_FRONTEND_CHAIN=1 -e P6_GHZCUT_BOOT=1 \
    -v "%cd%":/work -w /work joengine-saturn:latest \
    bash tools/_portspike/_p6/build_shipping.sh
python tools/build_cdda.py cd_audio/track02.wav cd_audio/track03.wav \
    --cue-out game.cue --iso game.iso
```

The second step muxes the CD-DA music in: the toolchain emits a data-only cue, so
the audio is a host post-step. Drop the `P6_*` flags for the plain
continuous-Green-Hill build. `pwsh tools/_play.ps1` launches whatever you built in
RetroArch at real speed with sound.

## Finding your way around

The repo has accumulated a lot of history. Only a handful of places actually
matter:

| Where | What it is |
|---|---|
| `rsdkv5-src/` | the RSDKv5 engine (decompiled), compiled for SH-2 |
| `tools/_decomp_raw/` + `src/mania/` | the decompiled Sonic Mania game objects |
| `platform/Saturn/`, `src/rsdk/` | the Saturn backend that replaces the engine's platform layer |
| `tools/_portspike/_p6/` | the Saturn integration: build scripts, the frame loop glue, VDP1/VDP2 code |
| `jo-engine/` | Jo Engine, the Saturn HAL (treated as a vendored dependency) |
| `tools/qa_*.py`, `tools/qa_*.ps1` | the test harness (live memory reads against the emulator) |
| `docs/` | plans, per-feature checklists, research notes |

Everything else — the `_`-prefixed scratch files, old phase scripts, savestates —
is working residue from development sessions. If a file starts with `_`, it's
disposable.

## Sources of truth

- **Game logic** — the Sonic Mania decompilation (RSDKModding on GitHub), cached
  under `tools/_decomp_raw/`.
- **Engine behavior** — the RSDKv5 decompilation in `rsdkv5-src/`.
- **Assets** — your retail `Data.rsdk`, read at runtime.
- **Hardware** — the Sega Saturn technical documents (the VDP1, VDP2, SCSP, SMPC,
  and SCU manuals). Every register-level decision traces back to them.

`CLAUDE.md` is the working manual for anyone touching this repo; `BIBLE.md` holds
the phase plan.

## Thanks and credits

This project exists because of other people's work, and it's worth being explicit
about that:

- **[RSDKModding's Sonic Mania decompilation](https://github.com/RSDKModding/Sonic-Mania-Decompilation)**
  — the game logic itself. Every object this port runs (`Player`, `Ring`,
  `TitleCard`, the AIZ cutscene, all of it) is their decompilation, translated
  mechanically, not rewritten. This port's rule is that their code is the source
  of truth.
- **[RSDKModding's RSDKv5 decompilation](https://github.com/RSDKModding/RSDKv5-Decompilation)**
  — the engine. It defines the contract the Saturn backend implements, and large
  parts of it compile straight into this build.
- **[Jo Engine](https://jo-engine.org/) by Johannes Fetz** — the Saturn hardware
  layer and the bundled `sh-none-elf` GCC toolchain this whole thing builds with.

If you're an author of any of the above and want something credited differently,
open an issue.

## Legal

No Sonic Mania content is committed here. Bring your own legally obtained
`Data.rsdk`. The decompilations are community-authored projects with their own
licenses; the Saturn port code is original work. This is a non-commercial fan
technical project, unaffiliated with Sega.
