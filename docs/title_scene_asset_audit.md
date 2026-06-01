# Title Scene - Comprehensive Asset Audit

- **Date:** 2026-05-27
- **Phase:** 1.33
- **Methodology:** enumerate decomp `StageLoad` / `RSDK.Load*` / `RSDK.PlayStream` / `Music_SetMusicTrack` / sprite-sheet calls -> cross-reference `extracted/Data/`.
- **Gate:** `tools/qa_phase1_33_asset_coverage_gate.py` (GREEN post-extraction)

This audit replaces the prior piecemeal `build_filelist.py` add cadence. Every Title-scene asset reference in the decomp is now systematically catalogued with `file:line` citation and a present/missing status.

## Per-object inventory

### TitleSetup (`tools/_decomp_raw/SonicMania_Objects_Title_TitleSetup.c`)

| Decomp ref | Resolved Data/ path | Status |
|---|---|---|
| line 83 `RSDK.LoadSpriteAnimation("Title/Electricity.bin")` | `Data/Sprites/Title/Electricity.bin` | OK (759 B) |
| via Electricity.bin sheet 0 | `Data/Sprites/Title/Electricity1.gif` | OK (45 767 B) |
| via Electricity.bin sheet 1 | `Data/Sprites/Title/Electricity2.gif` | OK (7 267 B) |
| line 85 `RSDK.GetSfx("Global/MenuBleep.wav")` | `Data/SoundFX/Global/MenuBleep.wav` | OK (7 018 B) |
| line 86 `RSDK.GetSfx("Global/MenuAccept.wav")` | `Data/SoundFX/Global/MenuAccept.wav` | OK (92 060 B) |
| line 87 `RSDK.GetSfx("Global/Ring.wav")` | `Data/SoundFX/Global/Ring.wav` | OK (59 132 B) |
| line 371 `RSDK.PlayStream("IntroTee.ogg", ...)` | `Data/Music/IntroTee.ogg` | OK (1 594 604 B) **NEW** |
| line 372 `RSDK.LoadVideo("Mania.ogv", ...)` | `Data/Video/Mania.ogv` | OK (30 279 487 B) |
| line 376 `RSDK.PlayStream("IntroHP.ogg", ...)` | `Data/Music/IntroHP.ogg` | OK (1 146 482 B) **NEW** |
| line 331 `RSDK.SetScene("Presentation", "Menu")` | resolves via GameConfig category "Presentation" -> folder `Data/Stages/Menu/` (already extracted) | OK |

### TitleLogo (`tools/_decomp_raw/SonicMania_Objects_Title_TitleLogo.c`)

| Decomp ref | Resolved Data/ path | Status |
|---|---|---|
| line 145 `RSDK.LoadSpriteAnimation("Title/Logo.bin")` | `Data/Sprites/Title/Logo.bin` | OK (861 B) |
| via Logo.bin sheet 0 | `Data/Sprites/Title/Logo.gif` | OK (29 804 B) |
| line 149 `RSDK.LoadSpriteAnimation("Title/PlusLogo.bin")` (Plus DLC) | `Data/Sprites/Title/PlusLogo.bin` | INFO (absent, REV01 non-Plus retail) |
| line 149 implied sheet | `Data/Sprites/Title/PlusLogo.gif` | INFO (absent, Plus DLC) |
| line 151 `RSDK.GetSfx("Stage/Plus.wav")` (Plus DLC) | `Data/SoundFX/Stage/Plus.wav` | INFO (absent, Plus DLC) |

### TitleSonic (`tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.c`)

| Decomp ref | Resolved Data/ path | Status |
|---|---|---|
| line 53 `RSDK.LoadSpriteAnimation("Title/Sonic.bin")` | `Data/Sprites/Title/Sonic.bin` | OK (1 098 B) |
| via Sonic.bin sheet 0 | `Data/Sprites/Title/Sonic.gif` | OK (144 951 B) |

### TitleBG (`tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c`)

| Decomp ref | Resolved Data/ path | Status |
|---|---|---|
| line 89 `RSDK.LoadSpriteAnimation("Title/Background.bin")` | `Data/Sprites/Title/Background.bin` | OK (321 B) |
| via Background.bin sheet 0 | `Data/Sprites/Title/BG.gif` | OK (6 066 B) |

### Title3DSprite (`tools/_decomp_raw/SonicMania_Objects_Title_Title3DSprite.c`)

| Decomp ref | Resolved Data/ path | Status |
|---|---|---|
| line 59 `RSDK.LoadSpriteAnimation("Title/Background.bin")` | `Data/Sprites/Title/Background.bin` | OK (same as TitleBG) |

Title3DSprite consumes the "Billboard Sprites" anim (5 frames - Mountain L/M/S, Tree, Bush) sliced from `Title/BG.gif`.

### Scene anchors (loaded by RSDK engine when `Title/Scene1.bin` is loaded)

| Path | Status |
|---|---|
| `Data/Stages/Title/Scene1.bin` | OK (2 589 B) |
| `Data/Stages/Title/StageConfig.bin` | OK (614 B) |
| `Data/Stages/Title/16x16Tiles.gif` | OK (23 038 B) |
| `Data/Stages/Title/TileConfig.bin` | INFO (absent in retail Data.rsdk; Title is UI-only with no tile collision; engine handles gracefully) |

### Globals pre-allocated in Scene1.bin (zero entities)

`APICallback`, `Options`, `SaveGame`, `Localization`, `DemoMenu` - slot pre-allocation only, no Title-scene-specific assets beyond what their `StageLoad` does globally.

### Music (`tools/_decomp_raw/SonicMania_Objects_Global_Music.c`)

`Music_StageLoad` runs whenever a scene with Music in its object list loads - the Title scene's StageConfig.bin lists Music, so all 12 `Music_SetMusicTrack` literals are tied to Title load.

| Decomp ref | Resolved Data/ path | Status |
|---|---|---|
| line 51 `Music_SetMusicTrack("Invincible.ogg", ...)` | `Data/Music/Invincible.ogg` | OK (313 056 B) |
| line 52 `Music_SetMusicTrack("Sneakers.ogg", ...)` | `Data/Music/Sneakers.ogg` | OK (504 116 B) **NEW** |
| line 53 `Music_SetMusicTrack("BossMini.ogg", ...)` | `Data/Music/BossMini.ogg` | OK (1 185 703 B) |
| line 54 `Music_SetMusicTrack("BossHBH.ogg", ...)` | `Data/Music/BossHBH.ogg` | OK (1 147 370 B) **NEW** |
| line 55 `Music_SetMusicTrack("BossEggman1.ogg", ...)` | `Data/Music/BossEggman1.ogg` | OK (1 415 900 B) |
| line 56 `Music_SetMusicTrack("BossEggman2.ogg", ...)` | `Data/Music/BossEggman2.ogg` | OK (989 416 B) |
| line 57 `Music_SetMusicTrack("ActClear.ogg", ...)` | `Data/Music/ActClear.ogg` | OK (133 319 B) |
| line 58 `Music_SetMusicTrack("Drowning.ogg", ...)` | `Data/Music/Drowning.ogg` | OK (306 383 B) |
| line 59 `Music_SetMusicTrack("GameOver.ogg", ...)` | `Data/Music/GameOver.ogg` | OK (194 150 B) |
| line 60 `Music_SetMusicTrack("Super.ogg", ...)` | `Data/Music/Super.ogg` | OK (889 690 B) **NEW** |
| line 61 `Music_SetMusicTrack("HBHMischief.ogg", ...)` | `Data/Music/HBHMischief.ogg` | OK (1 083 727 B) **NEW** |
| line 63 `Music_SetMusicTrack("1up.ogg", ...)` | `Data/Music/1up.ogg` | OK (71 980 B) |

### Title Scene1.bin Music entity attribute

Per `python tools/parse_title_entities.py`: the Music entity at slot 75 carries `trackFile="TitleScreen.ogg"`. `Music_Create` reads this via `Music_SetMusicTrack(trackName, self->trackID, self->trackLoop)` -> stream `Data/Music/TitleScreen.ogg`.

| Source | Resolved Data/ path | Status |
|---|---|---|
| Title/Scene1.bin slot 75 attr trackFile | `Data/Music/TitleScreen.ogg` | OK (337 133 B) |

### Localization (`tools/_decomp_raw/SonicMania_Objects_Global_Localization.c`)

Globally pre-allocated in Title/Scene1.bin. `Localization_StageLoad` loads one of nine StringList files based on system language.

| Decomp ref | Resolved Data/ path | Status |
|---|---|---|
| line 42 `RSDK.LoadStringList(_, "StringsEN.txt", 16)` | `Data/Strings/StringsEN.txt` | OK (4 906 B) **NEW** |
| line 47 `_ "StringsFR.txt"` | `Data/Strings/StringsFR.txt` | OK (5 922 B) **NEW** |
| line 52 `_ "StringsIT.txt"` | `Data/Strings/StringsIT.txt` | OK (5 050 B) **NEW** |
| line 57 `_ "StringsGE.txt"` | `Data/Strings/StringsGE.txt` | OK (5 450 B) **NEW** |
| line 62 `_ "StringsSP.txt"` | `Data/Strings/StringsSP.txt` | OK (5 312 B) **NEW** |
| line 67 `_ "StringsJP.txt"` | `Data/Strings/StringsJP.txt` | OK (3 150 B) **NEW** |
| line 73 `_ "StringsKO.txt"` | `Data/Strings/StringsKO.txt` | OK (2 866 B) **NEW** |
| line 78 `_ "StringsSC.txt"` | `Data/Strings/StringsSC.txt` | OK (2 088 B) **NEW** |
| line 83 `_ "StringsTC.txt"` | `Data/Strings/StringsTC.txt` | OK (2 062 B) **NEW** |

## Editor-only references (excluded from port requirements)

The gate skips any reference inside `#if GAME_INCLUDE_EDITOR` blocks. Examples:

- `Music.c:831 RSDK.LoadSpriteAnimation("Editor/EditorIcons.bin")` - editor-only.

The Saturn port does not ship the RSDK Studio editor, so these refs are correctly excluded.

## Summary

| Metric | Count |
|---|---|
| Decomp .c files scanned | 7 |
| RSDK call sites enumerated | 35 |
| Unique asset paths derived | 42 (including 4 scene anchors and 4 sprite-sheet GIFs derived from .bin sheet lists) |
| Present pre-audit (retail) | 24 |
| Newly-extracted by Phase 1.33 (retail) | 15 (6 .ogg music, 9 string lists) |
| Total present post-audit (retail) | 39 |
| Plus-DLC missing (INFO, absent from REV01) | 2 (`PlusLogo.bin`, `Stage/Plus.wav`) |
| Documented-absent scene anchor (INFO) | 1 (`Title/TileConfig.bin` - UI scene without tile collision) |
| Retail missing (FAIL) | **0** |
| Gate verdict | **GREEN** |

## Newly-extracted file list (Phase 1.33)

```
Data/Music/BossHBH.ogg           1 147 370 B
Data/Music/HBHMischief.ogg       1 083 727 B
Data/Music/IntroHP.ogg           1 146 482 B
Data/Music/IntroTee.ogg          1 594 604 B
Data/Music/Sneakers.ogg            504 116 B
Data/Music/Super.ogg               889 690 B
Data/Strings/StringsEN.txt           4 906 B
Data/Strings/StringsFR.txt           5 922 B
Data/Strings/StringsGE.txt           5 450 B
Data/Strings/StringsIT.txt           5 050 B
Data/Strings/StringsJP.txt           3 150 B
Data/Strings/StringsKO.txt           2 866 B
Data/Strings/StringsSC.txt           2 088 B
Data/Strings/StringsSP.txt           5 312 B
Data/Strings/StringsTC.txt           2 062 B
```

Total new bytes: ~7.6 MB (dominated by the two intro stream OGGs and the boss/Super tracks).

## Methodology pivot (BINDING for future scene ports)

Replaces the prior "manual `paths.add(...)` per surprise" cadence:

1. Cache every per-scene decomp `.c` file under `tools/_decomp_raw/` (sister-file rule).
2. Grep each file for the 7 RSDK call patterns (see top of `tools/qa_phase1_33_asset_coverage_gate.py`).
3. Parse each referenced sprite `.bin` to discover its sheet list - add every sheet GIF too.
4. Add the union to `tools/build_filelist.py` in a per-scene block with `file:line` citations.
5. Run `python tools/qa_phase1_33_asset_coverage_gate.py` to confirm GREEN before claiming the scene done.

Recommended follow-on: replicate this audit pattern for the GHZ scene (and every subsequent zone) in a dedicated phase. Each scene gets one audit doc + one gate predicate keyed off its decomp `_Setup.c` plus child objects.
