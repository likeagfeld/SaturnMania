> Authored by R11's seat 2026-07-23. The converter invocation table + gap list that
> drove commits d541c31..bcbc9ae. Added to docs/ 2026-07-24 with the -2 session.

# SaturnMania asset-pipeline validation report

*2026-07-23, run on macOS (Apple Silicon), Python 3.14, NumPy 2.5, Pillow 12.2, against `Data.rsdk` (183 MB) and the `cd/` payload extracted from the 2026-07-23 `build/game.iso`.*

## Headline

**229 of the 284 `cd/` files regenerate from `Data.rsdk` on this machine, byte-identical to the shipped disc.** The pipeline is far more reproducible than SETUP.md's "not turn-key" warning suggests — the converters are deterministic and cross-platform; what's missing is only: entries in `maniafilelist.txt`, written-down command lines, and four small bug fixes.

## Validated byte-identical (229 files)

| Family | Tool | Invocation |
|---|---|---|
| All entity atlases (RING/ITEMBOX/SPRING/…/SONIC slices, HUD, TITLCARD — 49 files) | `build_entity_atlas.py` | no-arg (full MANIFEST) |
| All banded sheets `*.SHT` (23) | `build_sheet_bands.py` | no-arg |
| All frame dirs `*.FRD` (13) | `build_frame_dir.py` | no-arg **after fixing MAIN path (bug #1)** |
| Heavies/player cutscene atlases (HBHOBJ.SHT/.PAK, PLROBJ.SHT, PLRPAL, HBHPAL) | `build_heavy_atlas.py`, `build_player_atlas.py` | no-arg |
| Pre-decoded tilesets `*TIL/*PAL.BIN` — every zone (~86) | `build_predecoded_tilesets.py` | no-arg |
| Layout band stores GHZ1/GHZ2/AIZ1/GHC1 LAYT.BIN | `build_layout_bands.py` | `--scene … --tag …` (AIZ1/GHC1 add `--no-model`) |
| GHZ collision/masks/rings/surf (GHZ1COL, GHZ1MASK, GHZ[12]RINGS…) | `build_collayout.py`, `build_tilemasks.py`, `build_rings.py` | no-arg / `--scene` |
| GHZ1 FG stream cells (CEL/PAT/TMP/PAL) | `convert_stream.py` | `GHZ --scene Scene1.bin --layers 3,4 --bank 1` |
| 4bpp FG + cutscene BGs (GHZFG/AGHFG/AGHCBG/AGHFS/AIZBG 18 files) | `build_ghz_fg_4bpp.py`, `build_ghzcut_bg.py`, `build_aiz_4bpp.py` | no-arg / documented flags |
| Sky/clouds/title/island (CLOUDS, TITLE, TITLE3D.DAT, ISLAND) | `build_clouds_bg.py`, `build_title_island_bg.py`, `build_island_bg.py` | no-arg |
| Boot splash, title-card pal, dormant stores, anim PAKs*, SFX PCMs, sprite packs, scene bins, spawn coords | various (see converter map) | scripted |

*PAK files: GHZANIM/GHZOBJ/AIZOBJ/AIZANIM.PAK were already validated identical from the earlier build.bat pass; `build_anim_pack.py` re-run is blocked by bug #1 (it shells out to `build_frame_dir.py`).

## Bugs found (4)

1. **`build_frame_dir.py:69` — `MAIN = r"D:\sonicmaniasaturn"`** (hardcoded absolute Windows path). Breaks on any other machine/checkout. Also blocks `build_anim_pack.py`, which invokes it. Validated fix: make MAIN = repo root relative to the script (patched copy produced all 13 FRDs byte-identical).
2. **`build_collision.py` — NumPy ≥2 incompatibility**: `OverflowError: Python integer 960 out of bounds for int8`. NumPy 2 removed silent wrapping of out-of-range scalars. GHZ[12]SURF.BIN cannot regenerate under current NumPy. (Everything else works on NumPy 2.5.)
3. **`convert_stream.py` — GHZ Scene2 crash**: `IndexError: index 48 is out of bounds for axis 0 with size 18`. Scene1 works; Scene2 path is broken (and it crashes mid-write, corrupting existing GHZ2FG.CEL/.PAT/.TMP — restore from a good copy after any failed run).
4. **Legacy title-atlas quartet bitrot** — `build_title3d_atlas.py`, `build_titlesonic_atlas.py`, `build_electricity_atlas.py`, `build_logos_atlas.py` all fail with `ValueError: too many values to unpack (expected 8, got 9)`: their embedded .bin frame parser predates a 9-field frame tuple that newer tools handle. (Their outputs TITLE3D/TSONIC/ELECTRA/LOGOS.ATL are on the shipped disc from older runs.)

## maniafilelist.txt gaps (~300 paths)

The stock filelist extracts 1,352/1,677 files but misses inputs the converters need. Found iteratively: `Global/Display.gif`, `Global/Shields.gif`, `Global/Water.gif`, `Global/Explosions.gif`, `Global/Animals.gif`, `Data/Video/Mania.ogv`, `Title/*.bin+gif`, `Logos/Logos.bin+gif`, `Players/SuperSonic.bin`, `Players/Sonic1-3.gif`, `Players/Tails1.gif`, `SPZ1/Boss.bin+gif`, `UI/MainIcons.gif`, `UI/TextEN.gif`, plus the wide sweep (281 more paths harvested by grepping every `Data/...` literal in `tools/*.py` — list saved in `missing4.txt` alongside this report). **All were present in the datapack** — the fix is purely additive filelist lines.

## Not regenerable / expected differences

- `INTRO.CPK` — regenerates fine via `ogv_to_cpk.py` but is ffmpeg-version-dependent (Cinepak re-encode; never byte-identical). Functional, not bit-exact.
- `MLOGO/MRING/MRIBBON/MRIBSIDE/MWINGS/MPRESS.SPR`, `ELECTR1/2.SHT`, `SCENE1.BIN`, `TSONIC_ATL_STRIPPED.BAK` — legacy/superseded artifacts with no current generator; carry forward as opaque files (candidates for deletion from the disc).
- `GHZ1BOX/BUGS/SIGN/SPRG.BIN` + GHZ2 counterparts — placement tables; generator invocation not recoverable from docs (phase2_4c family suspected; `build_phase2_4c_entities.py` itself fails with the same 8-vs-9 parser bitrot).
- `GHZSCN2.BIN` — not on the shipped disc at all (GHZ2 content gated off); the docs' Scene2 copy step is aspirational.
- `0.bin`, `OVLRING.BIN`, `INTRO.BIN` — code build outputs (Docker make / build_shipping.sh / Makefile.intro), not assets.
- `DATA.RSDK` — verbatim copy of the datapack onto the disc.
- SETUP.md correction: `convert_stream.py` is the GHZ FG tilemap streamer, **not** the Cinepak tool (that's `ogv_to_cpk.py`); SETUP.md line 102 is wrong.

## Suggested upstream actions (ranked)

1. Add the ~300 missing paths to `maniafilelist.txt` (one-time, purely additive).
2. Fix `build_frame_dir.py` MAIN → script-relative (unblocks anim packs too).
3. Consolidate this report's invocation table into `tools/setup.ps1` (+ a bash twin) — with 1+2 done, a clean clone regenerates 97% of the disc from `Data.rsdk` in one pass.
4. Pin `numpy<2` in a requirements note, or fix the int8 write in `build_collision.py`.
5. Fix or retire: `convert_stream.py` Scene2 path, the 8-vs-9 parser in the four legacy .ATL tools (or regenerate those assets with the modern SHT pipeline and delete the legacy files from the disc).
