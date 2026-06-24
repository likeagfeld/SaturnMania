# SaturnMania — Sonic Mania, true RSDKv5 engine port for the Sega Saturn

A **native port of the RSDKv5 engine and the Sonic Mania decompilation to the
Sega Saturn's SH-2** — the verbatim decomp runs on hardware, ingesting the
**retail `Data.rsdk` at runtime**. North star: frame-for-frame parity with the
PC Steam build.

> This repo previously described a hand-rolled Jo-Engine reimplementation under
> the premise that recompiling RSDKv5 on Saturn was "provably impossible." That
> premise was wrong. The current build (phase **P6**) compiles the byte-for-byte
> decompilation to SH-2 and runs the engine's own scene/object pipeline on
> hardware. The hand-port survives only as a reversible fallback (`make` default).

## How it actually works

The Saturn boots straight into a **continuous Green Hill Zone Act 1** running the
engine's verbatim loop: `LoadGameConfig` → `LoadSceneFolder` → `LoadSceneAssets`
→ `InitObjects` → `ProcessObjects` / `ProcessObjectDrawLists`. The decomp objects
(`Player`, `Ring`, `Bridge`, …) execute unmodified; a thin Saturn backend
translates the RSDK API surface to the hardware.

```
src/mania/Objects/<Cat>/<Obj>.c  +  tools/_decomp_raw/  (verbatim decomp)
        |   compiled to SH-2 in the engine pack (p6_scene_pack.o)
        v
src/rsdk/  +  platform/Saturn/   ENGINE COMPAT LAYER
        |   RSDK.DrawSprite -> VDP1 sprite slot cache
        |   tile layers      -> VDP2 NBG (camera-windowed)
        |   LoadSfx/PlayStream-> SCSP PCM + CD-DA (Red Book audio tracks)
        |   file I/O          -> GFS windowed reads over the real Data.rsdk pack
        v
jo-engine/ + SGL                 Saturn HAL (VDP1/VDP2, SCSP, CD block, SH-2)
```

## The constraints that "made it impossible" — and how they're solved

| Constraint | Mania / RSDKv5U | Saturn | Resolution |
|---|---|---|---|
| Engine language | C++ | SGL/Jo are C | `sh-none-elf` g++ 8.2 compiles the decomp to SH-2; pack linked beside the C HAL |
| Static pool | ~74 MB reserved | 2 MB main RAM | Saturn-retargeted pools + a **4 MB extended-RAM cart** + **windowed residency** (camera-local sliding windows over band-compressed layouts/sheets) |
| Entity table | flat PC arrays (321 KB+) | tight RAM | group lists written only on the in-range branch; 344 B entity slot; per-class Saturn shrink |
| Framebuffer | linear RGB565 software-blit | VDP1 sprites + VDP2 tiles | DrawSprite → rect-keyed VDP1 upload cache; tile layers → VDP2 NBG streamed in VBLANK |
| Full content | ~180 MB data | 650 MB CD | streaming problem, not a fit problem — windowed GFS over the on-disc pack |

## Authoritative sources of truth

- **Game logic** — the Sonic Mania decompilation (RSDKModding), cached selectively
  in `tools/_decomp_raw/` (all 544 object TUs present).
- **Engine behavior** — the RSDKv5 decompilation in `rsdkv5-src/` (the contract
  the `src/rsdk/` + `platform/Saturn/` backend implements).
- **Assets** — your own retail `Data.rsdk` (~174 MB, 1677 files). Nothing
  copyrighted is committed; the build reads the pack you provide.
- **Hardware** — the Sega DTS technical docs (VDP1/VDP2/SCSP/SCU/SMPC manuals)
  bound every register-level decision.

`CLAUDE.md` is the binding operating manual; `BIBLE.md` is the phase plan.

## The drop-in census (port-planning toolchain)

The whole decomp corpus is pre-measured so any object/zone is "drop-in ready"
without re-reading the decomp per object. Re-runnable, deterministic:

- `tools/build_object_census.py` — every object's draw-kind, StageLoad residency
  (anim `.bin` → sheet resolved), dependency closure, RSDK-APIs-used vs the
  Saturn draw-stub set.
- `tools/build_scene_census.py` — a real `Scene.bin` entity parser → per-act
  PLACED objects + counts + coords (the authority for "what's in Act N").
- `tools/build_dropin_census.py` — fuses all dimensions + the 344 B entity-slot
  wall + the 384 KB sheet store into a per-scene drop-in verdict.

## Prerequisites

1. **Docker** (Jo Engine's bundled `sh-none-elf` GCC 8.2 toolchain runs inside).
2. **Jo Engine** in `jo-engine/`.
3. The build image: `docker build -t joengine-saturn:latest .`
4. **Your own `Data.rsdk`** (retail Sonic Mania, RSDKv5 datapack).
5. **Mednafen** (Saturn core, real BIOS) to test — see `tools/qa_*.ps1`.

## Build

**Engine-shipping build (the real target — continuous GHZ):**
```
MSYS_NO_PATHCONV=1 docker run --rm -v "%cd%":/work -w /work \
    joengine-saturn:latest bash tools/_portspike/_p6/build_shipping.sh
# CD-DA music is a host post-step (the build's CUE is data-only):
python tools/build_cdda.py cd_audio/track02.wav cd_audio/track03.wav \
    --cue-out game.cue --iso game.iso
```
**Hand-port fallback** (reversible default): `build.bat` / `docker ... make`.

Produces `game.iso` + `game.cue`. The engine GHZ load is compute-bound (~50 s of
blue/magenta load screens in the emulator before GHZ1 renders — that is the scene
load, not a hang).

## Current status (measured, honest)

**Plays now (engine-shipping build):** boots continuously into GHZ Act 1 with the
verbatim decomp `Player` (physics/animation byte-exact, spindash/peelout/drop-dash,
slope physics), camera follow, the FG tile layer + tile collision, CD-DA stage
music + SCSP SFX, the act signpost → GHZ2 advance, and rendered bridges. Spawn
state correct (0 rings, no shield, ticking timer).

**Front-end (title build, `-e P6_FRONTEND_TITLE=1`):** the engine also runs the
verbatim **Title** scene — the full SONIC MANIA logo, the animated Sonic head +
finger-wave + ribbon, the intro electricity-ring build → white flash, and
TitleScreen CD-DA music. The rotating **Mania island** backdrop is rendered as a
VDP2 **RBG0 Mode-7** perspective floor driven by a per-line coefficient table —
the decomp `TitleBG_Scanline_Island` deform/position math reproduced on hardware
(proven byte-exact vs an offline sim, then gated on the full four-quadrant head).

**In progress / known gaps:** the GHZ object sweep (the census measures 26 GHZ1
objects still to register — loops via `PlaneSwitch`, collapsing platforms,
badniks); BG parallax / sky; realtime 60 fps (currently ~30–49 fps; a dual-SH2
render split is underway); the title cloud layer flickers (VDP2 cycle-pattern
churn); the menu → Mania-Mode select + the biplane intro → GHZ hand-off; other
zones and special stages.

This is multi-year work; progress is one measured, gated port at a time.

## Layout

| Path | Role |
|---|---|
| `tools/_decomp_raw/` | cached verbatim Sonic Mania decomp object TUs |
| `rsdkv5-src/` | the RSDKv5 engine decompilation (Saturn-gated) |
| `src/rsdk/`, `platform/Saturn/` | the RSDK-API → Saturn hardware backend |
| `tools/_portspike/_p6/` | the engine pack build + I/O glue + QA gates |
| `jo-engine/` | Saturn HAL (do not modify except documented extensions) |
| `tools/build_*_census.py` | the drop-in census toolchain |
| `tools/qa_*.ps1`, `tools/qa_p6_*.py` | Mednafen QA harness + per-phase gates |

## Legal

This repo contains **no copyrighted Sonic Mania content**. You must supply your
own legally-obtained `Data.rsdk`. The decompilation is community-authored; the
Saturn port code here is original work.
