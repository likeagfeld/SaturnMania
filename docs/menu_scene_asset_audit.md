# Menu Scene - Comprehensive Asset Audit

- **Date:** 2026-05-28
- **Phase:** 3.0-prep (second application of the Phase 1.33 binding methodology)
- **Methodology:** enumerate decomp `StageLoad` / `RSDK.Load*` / `RSDK.PlayStream` / `Music_SetMusicTrack` / sprite-sheet calls AND `Scene1.bin` entity-attribute `trackFile` strings -> cross-reference `extracted/Data/`.
- **Gate:** `tools/qa_phase3_0_menu_asset_coverage_gate.py` (GREEN post-extraction)
- **Companion:** mirrors `docs/title_scene_asset_audit.md` exactly.

The Menu scene is the parent of the entire UI sub-system: MainMenu, Save Select, Time Attack, Competition, Options, Extras. It is also the destination of the Title scene's `RSDK.SetScene("Presentation", "Menu")` transition. Its `StageConfig.bin` declares 54 Object classes (the entire UI* family) all of which load assets via `SCOPE_STAGE` in their per-class `StageLoad`. This audit catalogues every one of them.

The `Logos` scene (the very first scene loaded at game boot, `SceneInfo->listPos = 0..2`, sometimes 3..7 for char-select-via-boot-arg) is also audited because LogoSetup is the only object in that scene and is co-located in `SonicMania/Objects/Menu/`.

## Per-object inventory

### LogoSetup (`tools/_decomp_raw/SonicMania_Objects_Menu_LogoSetup.c`)

| Decomp ref | Resolved Data/ path | Status |
|---|---|---|
| line 48 `RSDK.GetSfx("Stage/Sega.wav")` | `Data/SoundFX/Stage/Sega.wav` | needs check |
| line 50 `RSDK.LoadSpriteAnimation("Logos/Logos.bin")` | `Data/Sprites/Logos/Logos.bin` | needs check |
| via Logos.bin sheet 0+ | `Data/Sprites/Logos/*.gif` | parsed at gate-run time |
| line 87 `RSDK.LoadImage("CESA.png", ...)` (Plus, JP region only) | `Data/Image/CESA.png` | INFO (Plus + JP) |
| line 89 `RSDK.LoadImage("CESA.tga", ...)` (pre-Plus, JP region only) | `Data/Image/CESA.tga` | INFO (JP region) |

CESA is the Japanese rating-board logo, shown only on REGION_JP boots. The Saturn port is NTSC-U/EU and skips this; tracking them as INFO.

### MenuSetup (`tools/_decomp_raw/SonicMania_Objects_Menu_MenuSetup.c`)

`MenuSetup_StageLoad` itself (lines 153-208) loads NO direct sprite/SFX assets. It is the menu-state controller: it stops music, clears save buffers, iterates `foreach_all(UIControl, ...)` to bind the 22 UIControl tags ("Main Menu", "Save Select", "Time Attack", ...) into MenuSetup struct fields, and calls `MenuSetup_Initialize`. All sprite/SFX assets the menu uses are loaded by its child Objects (UIWidgets, UIButtonPrompt, UIControl, etc., catalogued below).

| Decomp ref | Resolved Data/ path | Status |
|---|---|---|
| line 902 `Music_PlayTrack(trackID)` where trackID = 0/1/2/3 | trackFile resolved from Menu/Scene1.bin Music entities (see below) | OK |

`MenuSetup_ChangeMenuTrack` (line 890) maps the active UIControl back to one of 4 Music slots via `MenuSetup_GetActiveMenu` (slots 0=MAIN, 1=TIMEATTACK, 2=COMPETITION, 3=SAVESELECT). The trackFile strings for those four slots come from the `Music` entity attributes in `Menu/Scene1.bin` (see "Music entity inventory" below).

### Menu/Scene1.bin Music entities (parsed via `tools/parse_title_entities.py`)

4 Music entities, slot=0..3 in trackID terms (the entity attribute `<unknown 609124e14a31bf3dda74cf9417e4c0a8>` = trackID enum):

| Slot | trackID | trackFile attr | loopPoint | Resolved Data/ path |
|---|---|---|---|---|
| 129 | 0 | `MainMenu.ogg` | 202752 | `Data/Music/MainMenu.ogg` |
| 198 | 1 | `Competition.ogg` | 94500 | `Data/Music/Competition.ogg` |
| 199 | 2 | `Results.ogg` | 88991 | `Data/Music/Results.ogg` |
| 200 | 3 | `SaveSelect.ogg` | 131290 | `Data/Music/SaveSelect.ogg` |

Per `Music.c:92-105 Music_SetMusicTrack`: when a Music entity Create runs it writes its trackFile into `Music->trackNames[trackID]`; `Music_PlayTrack(trackID)` then `RSDK.PlayStream(Music->trackNames[trackID], ...)`. So all four `.ogg` paths must be extracted.

### Per-Object UI sprite + SFX inventory

Every `Menu/UI*.c` and the menu-mode .c files were grepped for `RSDK.LoadSpriteAnimation` / `RSDK.GetSfx` / `RSDK.PlayStream` / `RSDK.LoadStringList` / `RSDK.LoadVideo`. The full citation table is in the gate output; this section summarises the unique resolved paths.

Sprite animations (Data/Sprites/...):

| Path | Loaded by (decomp file:line) |
|---|---|
| `UI/DAGarden.bin` | DAControl.c:93, DAControl.c:113 |
| `Title/DemoMenu.bin` | DemoMenu.c:73, DemoMenu.c:214 |
| `LSelect/Icons.bin` | LevelSelect.c:75, LevelSelect.c:697 |
| `LSelect/Text.bin` | LevelSelect.c:76, LevelSelect.c:700 |
| `Logos/Logos.bin` | LogoSetup.c:50, UIPicture.c:77, UIPicture.c:95 |
| `Thanks/Decorations.bin` | ThanksSetup.c:61, ThanksSetup.c:174 |
| `Editor/EditorIcons.bin` | UIBackground.c:84, UIControl.c:856 (EDITOR-ONLY -- excluded by gate) |
| `UI/ButtonLabel.bin` | UIButtonLabel.c:55, UIButtonLabel.c:75 |
| `UI/Buttons.bin` | UIButtonPrompt.c:167, UIButtonPrompt.c:542, UIKeyBinder.c:187, UIKeyBinder.c:471 |
| `UI/SaveSelect.bin` | UICharButton.c:108/266, UIChoice.c:144/350, UILeaderboard.c:75/533, UIReplayCarousel.c:122/630, UIResPicker.c:124, UISaveSlot.c:287/1317, UITABanner.c:69/239, UITAZoneModule.c:143/1196, UIVsCharSelector.c:110/488, UIVsResults.c:75/305, UIVsRoundPicker.c:120/378, UIVsScoreboard.c:50/124, UIVsZoneButton.c:128/395, UIWidgets.c:36/339, UIWinSize.c:123/386 |
| `UI/CreditsText.bin` | UICreditsText.c:72, UICreditsText.c:266 |
| `UI/Diorama.bin` | UIDiorama.c:103, UIDiorama.c:992 |
| `AIZ/SchrodingersCapsule.bin` | UIDiorama.c:104 |
| `Players/Sonic.bin` | UIDiorama.c:105 (already covered by build_filelist global block) |
| `Players/Tails.bin` | UIDiorama.c:106 (already covered) |
| `Players/KnuxCutsceneAIZ.bin` | UIDiorama.c:107 |
| `Players/KnuxCutsceneHPZ.bin` | UIDiorama.c:108 |
| `Players/Mighty.bin` | UIDiorama.c:109 (Plus DLC) |
| `Players/Ray.bin` | UIDiorama.c:110 (Plus DLC) |
| `Global/Ring.bin` | UIDiorama.c:111 (already covered) |
| `Global/SpeedGate.bin` | UIDiorama.c:112 |
| `SpecialBS/Sonic.bin` | UIDiorama.c:113 |
| `SpecialBS/StageObjects.bin` | UIDiorama.c:114 |
| `UI/HeadingsEN.bin` | UIHeading.c:59, UIHeading.c:94 |
| `UI/HeadingsFR.bin` | UIHeading.c:60 |
| `UI/HeadingsIT.bin` | UIHeading.c:61 |
| `UI/HeadingsGE.bin` | UIHeading.c:62 |
| `UI/HeadingsSP.bin` | UIHeading.c:63 |
| `UI/HeadingsJP.bin` | UIHeading.c:64 |
| `UI/HeadingsKO.bin` | UIHeading.c:66 |
| `UI/HeadingsSC.bin` | UIHeading.c:67 |
| `UI/HeadingsTC.bin` | UIHeading.c:68 |
| `UI/TextEN.bin` | UIInfoLabel.c:89, UIText.c:89, UIVsResults.c:76/306, UIVsZoneButton.c:129/396, UIWidgets.c:76/342 |
| `UI/TextFR.bin` | UIWidgets.c:77 |
| `UI/TextIT.bin` | UIWidgets.c:78 |
| `UI/TextGE.bin` | UIWidgets.c:79 |
| `UI/TextSP.bin` | UIWidgets.c:80 |
| `UI/TextJP.bin` | UIWidgets.c:81 |
| `UI/TextKO.bin` | UIWidgets.c:83 |
| `UI/TextSC.bin` | UIWidgets.c:84 |
| `UI/TextTC.bin` | UIWidgets.c:85 |
| `UI/MedallionPanel.bin` | UIMedallionPanel.c:36, UIMedallionPanel.c:66 |
| `UI/MainIcons.bin` | UIModeButton.c:107, UIModeButton.c:325 |
| `UI/Picture.bin` | UIPicture.c:75, UIPicture.c:93 |
| `UI/WaitSpinner.bin` | UIWaitSpinner.c:73 |
| `UI/UIElements.bin` | UIWidgets.c:34, UIWidgets.c:337 |
| `UI/SmallFont.bin` | UIWidgets.c:38, UIWidgets.c:341 |

SFX (Data/SoundFX/...):

| Path | Loaded by |
|---|---|
| `Stage/Sega.wav` | LogoSetup.c:48, ThanksSetup.c:57 |
| `Special/Emerald.wav` | DASetup.c:69, LevelSelect.c:68 |
| `Special/Medal.wav` | DASetup.c:70 |
| `Special/SSExit.wav` | DASetup.c:71 |
| `Global/ScoreTotal.wav` | DASetup.c:72 |
| `Stage/Fail.wav` | LevelSelect.c:65, UIKeyBinder.c:189, UIVsZoneButton.c:131, UIWidgets.c:49 |
| `Global/Ring.wav` | LevelSelect.c:67 (already covered by Title-scene audit) |
| `Special/Continue.wav` | LevelSelect.c:69 |
| `Special/MedalCaught.wav` | LevelSelect.c:70 |
| `Global/MenuBleep.wav` | UIWidgets.c:44 (already covered by Title-scene audit) |
| `Global/MenuAccept.wav` | UIWidgets.c:45 (already covered) |
| `Global/SpecialWarp.wav` | UIWidgets.c:46 |
| `Special/Event.wav` | UIWidgets.c:47 |
| `Global/MenuWoosh.wav` | UIWidgets.c:48 |

## Scenes that the Menu transitions to (cross-reference for follow-on audits)

From every `RSDK.SetScene(category, name)` in the cached Menu sources, paired against `GameConfig.bin` category/name -> folder resolution. The "Mania Mode" category resolves at runtime via `MenuParam->levelID` to a per-zone scene (GHZ, CPZ, ..., ERZ).

| `RSDK.SetScene(...)` call site | Category -> Folder | Scene name -> id | Notes |
|---|---|---|---|
| `MenuSetup.c:435,476,973`, `ManiaModeMenu.c:63,108,216`, `UIVideo.c:108` | "Presentation" -> Title | "Title Screen" | Already audited (Phase 1.33) |
| `MenuSetup.c:1119`, `UISubHeading.c:390` | "Presentation" -> Title | "Level Select" | LevelSelect = same Title/Menu folder reuse with different scene id |
| `MenuSetup.c:1121,1245,1400`, `CompetitionMenu.c:499`, `E3MenuSetup.c:113`, `TimeAttackMenu.c:703`, `UISubHeading.c:404` | "Mania Mode" -> resolves at runtime | empty / `MenuParam->levelID` | Per-zone gameplay scenes (GHZ/CPZ/...) -- Phase 7+ audits |
| `CompetitionMenu.c:497`, `TimeAttackMenu.c:701`, `UISubHeading.c:400` | "Encore Mode" | empty | Plus DLC -- Phase 17 audit |
| `MenuSetup.c:1813,2206,2222`, `CompetitionMenu.c:931`, `ExtrasMenu.c:169,185` | "Extras" -> ??? | "Puyo Puyo" | Pinball/Puyo extras scene -- Phase 18 audit |
| `MenuSetup.c:2239`, `ExtrasMenu.c:202` | "Presentation" -> Credits | "Credits" | Phase 13 audit |
| `MenuSetup.c:2254`, `ExtrasMenu.c:219,222` | "Extras" | "D.A. Garden" / "D.A. Garden Plus" | DAControl/DASetup scenes -- minor Phase TBD |
| `MenuSetup.c:2270`, `ExtrasMenu.c:239` | "Blue Spheres" | "Random" | Phase 12 audit |
| `MenuSetup.c:2286`, `ExtrasMenu.c:255` | "Blue Spheres" | "Random 2" | Phase 12 audit |
| `UISubHeading.c:386,393` | "Cutscenes" | "Angel Island Zone" / "Angel Island Zone Encore" | AIZ cutscene -- Phase TBD |
| `DASetup.c:26` | "Presentation" -> Menu | "Menu" | Back-to-Menu |
| `DemoMenu.c:174,176` | "Media Demo" | "Green Hill Zone 1" / "Studiopolis Zone 1" | Attract-mode demo recordings -- minor Phase TBD |
| `LevelSelect.c:655` | dynamic `buffer` | empty | LevelSelect-driven jump |

## Editor-only references (excluded from port requirements)

The gate skips any reference inside `#if GAME_INCLUDE_EDITOR` blocks AND additionally whitelists the two `Editor/EditorIcons.bin` references in UIBackground.c:84 + UIControl.c:856 (both are at gate-discovered call sites where the surrounding function is `*_EditorLoad` or `*_EditorDraw`, but the simple regex picks them up):

- `UIBackground.c:84` - inside `UIBackground_EditorDraw`
- `UIControl.c:856` - inside `UIControl_EditorLoad`

`Editor/EditorIcons.bin` is also referenced by the Title-scene Music.c:831 audit; the existing gate skip-rule (`PLUS_ONLY` / `EXPECTED_ABSENT`) is extended here for the EDITOR_ONLY class.

## Plus-DLC + region-conditional refs (INFO not FAIL)

These are tracked but not required for the REV01 retail Saturn port:

- `Data/Sprites/Players/Mighty.bin` - Plus DLC character (already in build_filelist global block)
- `Data/Sprites/Players/Ray.bin` - Plus DLC character (already in global block)
- `Data/Image/CESA.png`, `Data/Image/CESA.tga` - REGION_JP only (Saturn port is non-JP)

## Summary

| Metric | Count |
|---|---|
| Decomp .c files scanned (Menu directory) | 56 (54 Menu + LogoSetup + MenuSetup) |
| Files newly fetched from upstream (Phase 3.0-prep) | 52 |
| RSDK call sites enumerated by gate | 83 (LoadSpriteAnim, GetSfx, LoadVideo, PlayStream after editor-block + EditorIcons exclusion) |
| Scene1.bin trackFile attrs extracted | 4 (Menu/Scene1.bin Music entities 129/198/199/200) |
| Unique asset paths (post-extraction, gate-time including sheet GIFs) | 125 |
| Present pre-audit (retail) | 35 |
| Newly-extracted by Phase 3.0-prep (retail) | 86 |
| Total present post-audit (retail) | 121 |
| Plus-DLC + region-conditional INFO | 4 (UI/Diorama.bin, AIZ/SchrodingersCapsule.bin, Players/Mighty.bin, Players/Ray.bin) |
| Retail missing FAIL | **0** |
| Gate verdict | **GREEN** |

## Newly-extracted file list (Phase 3.0-prep)

Total new bytes: ~4.73 MB. Largest additions: SaveSelect.ogg (864 KB), MainMenu.ogg (725 KB), Competition.ogg (650 KB), Results.ogg (343 KB), Special/Medal.wav (679 KB), Special/MedalCaught.wav (326 KB), Special/Continue.wav (191 KB), SmallFont.bin (125 KB).

Key newly-extracted asset families:

- **4 menu music tracks** (MainMenu.ogg, Competition.ogg, Results.ogg, SaveSelect.ogg) -- only discoverable from Menu/Scene1.bin Music entity trackFile attrs.
- **24 SaveSelect-language sprite atlases** (SaveSelectEN..TC, TextEN..TC, HeadingsEN..TC -- 9 languages each).
- **Boot-sequence assets** (Logos/Logos.bin + Logos.gif + Stage/Sega.wav).
- **D.A. Garden assets** (UI/DAGarden.bin + .gif).
- **Diorama character cutscene anims** (Players/KnuxCutsceneAIZ.bin/.gif + KnuxCutsceneHPZ.bin/.gif).
- **Blue Spheres character preview** (SpecialBS/Sonic.bin/.gif + StageObjects.bin).
- **Common UI widget atlases** (Buttons, ButtonLabel, SaveSelect, UIElements, SmallFont, MainIcons, MedallionPanel, Picture, WaitSpinner, CreditsText).
- **5 menu SFX** (Stage/Fail, Special/Emerald, Special/Continue, Special/MedalCaught, Special/SSExit, Special/Medal, Special/Event, Global/ScoreTotal, Global/MenuWoosh, Global/SpecialWarp).

## Newly-extracted file targets (Phase 3.0-prep additions to build_filelist.py)

The build_filelist.py Phase 3.0-prep block adds the union of resolved paths above (sprite .bin, SFX .wav, music .ogg). The rsdk_extract.py hash matcher silently drops misses, so over-enumeration is safe.

Counts (post-build_filelist additions, pre-extraction):
- Sprite anims under Data/Sprites/ requested: 39 retail + sheet GIFs (derived at gate-time)
- SFX under Data/SoundFX/: 14
- Music OGGs under Data/Music/: 4 newly-resolved (MainMenu.ogg, Competition.ogg, Results.ogg, SaveSelect.ogg) -- previously absent from the candidate list

## Methodology pivot continuation (Phase 1.33 -> Phase 3.0-prep)

Phase 1.33 established the binding methodology:

1. Cache every per-scene decomp `.c` file under `tools/_decomp_raw/` (sister-file rule).
2. Grep each file for the 7 RSDK call patterns + Music_SetMusicTrack + Scene1.bin Music-entity trackFile attribute.
3. Parse each referenced sprite `.bin` to discover its sheet list -- add every sheet GIF too.
4. Add the union to `tools/build_filelist.py` in a per-scene block with `file:line` citations.
5. Run the per-scene asset coverage gate before claiming the scene done.

Phase 3.0-prep is the second application. The pattern proved cleanly transferrable: the same 54-file batch fetch + regex pass + Scene1.bin music-attribute extraction yields the complete asset graph for the Menu/Logos scenes.

## Recommended follow-on per-scene audits

In dependency order from the Menu scene (each is a separate task):

1. **SaveSelect scene audit** -- UISaveSlot is the dominant user-facing widget, may have additional `Save/*.bin` or `Diorama/*.bin` refs not in MenuSetup parent.
2. **PlayerSelect / CharSelect audit** -- UICharButton + UICharSelector load Players/* sprites; cross-check against Phase 1.33's already-included player anims.
3. **Mania Mode scene audit (per-zone)** -- once one zone is picked (GHZ recommended since it already has runtime support), audit its full StageConfig.bin + per-object .c.
4. **Time Attack scene audit** -- TimeAttackMenu + UITAZoneModule + UITABanner; needs SaveGame integration for ghost replays.
5. **Competition scene audit** -- CompetitionMenu + UIVsCharSelector + UIVsZoneButton + UIVsResults; 2P split-screen.
6. **Options scene audit** -- OptionsMenu + UIKeyBinder + UISlider + UIWinSize + UIResPicker; KB/PS4/XB1/NX control assets (some Saturn-irrelevant).
7. **Extras scene audit** -- ExtrasMenu + (Puyo Puyo, D.A. Garden, Credits, Blue Spheres entry points).
8. **Credits scene audit** -- UICreditsText with `UI/CreditsText.bin` + per-language strings (TextEN already covered).
9. **Thanks scene audit** -- ThanksSetup + UIPicture with `Thanks/Decorations.bin`.
10. **Logos boot-sequence assets** -- only LogoSetup; smaller follow-up.
11. **D.A. Garden audit** -- DAControl + DASetup with `UI/DAGarden.bin`.
12. **Encore Mode audits** (Plus DLC, deferred to Phase 17).
13. **Blue Spheres scene audit** (Phase 12).
14. **UFO Special Stages audit** (Phase 15).
15. **Cutscenes/Presentation audits** (Phase 13).
16. **Per-zone gameplay audits (CPZ, SPZ1, SPZ2, FBZ, PSZ1, PSZ2, SSZ1, SSZ2, HCZ, MSZ, OOZ, LRZ1, LRZ2, LRZ3, MMZ, TMZ1, TMZ2, TMZ3, ERZ, AIZ)** -- one audit per zone per Phase 7-9 roll-out.

Each one is a single-session task following the same template.
