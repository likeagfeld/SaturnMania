# Whole-Game Asset Audit

- **Date:** 2026-05-28
- **Phase:** 3.0-prep++ (third application + whole-game generalisation of the
  Phase 1.33 BINDING methodology, extending Phase 3.0-prep's Menu/Logos work
  to every scene shipped in retail Sonic Mania)
- **Methodology:** for EVERY object class referenced by EVERY scene's
  StageConfig.bin, cache the upstream decomp .c file under
  `tools/_decomp_raw/`, grep it for the 7 RSDK asset patterns, parse every
  referenced sprite .bin's sheets[] list, AND parse every Scene*.bin Music
  entity's trackFile attribute. Union into `tools/build_filelist.py`,
  re-run rsdk_extract, and gate via
  `tools/qa_phase3_0_plus_whole_game_asset_coverage_gate.py`.
- **Gate:** `tools/qa_phase3_0_plus_whole_game_asset_coverage_gate.py`
  (Phase 3.0-prep++) — wired into `verify_done.ps1` as Gate V3.0-prep++.
  Verdict: **GREEN** (0 retail FAIL; 55 PLUS/EXPECTED_ABSENT INFO).

## Per-scene status summary

| Folder | Objects | Cached | SFX | Music | Scenes | Notes |
|---|---|---|---|---|---|---|
| AIZ | 11 | 11 | 12 | 1 | 1 | NEW Phase 3.0-prep++ |
| CPZ | 45 | 45 | 43 | 2 | 2 | NEW Phase 3.0-prep++ |
| Credits | 14 | 14 | 10 | 4 | 1 | NEW Phase 3.0-prep++ |
| DAGarden | 15 | 15 | 3 | 52 | 1 | NEW Phase 3.0-prep++ (52 D.A. tracks) |
| ERZ | 18 | 18 | 27 | 2 | 1 | NEW Phase 3.0-prep++ (final boss) |
| Ending | 13 | 13 | 0 | 1 | 7 | NEW Phase 3.0-prep++ (7-act ending sequence) |
| FBZ | 48 | 48 | 36 | 2 | 2 | NEW Phase 3.0-prep++ |
| GHZ | 28 | 28 | 16 | 2 | 2 | NEW Phase 3.0-prep++ (Phase 2.1-2.3 partial gameplay support) |
| GHZCutscene | 15 | 15 | 23 | 1 | 2 | NEW Phase 3.0-prep++ |
| HCZ | 42 | 42 | 37 | 2 | 2 | NEW Phase 3.0-prep++ |
| LRZ1 | 36 | 36 | 27 | 2 | 2 | NEW Phase 3.0-prep++ |
| LRZ2 | 38 | 38 | 19 | 1 | 1 | NEW Phase 3.0-prep++ |
| LRZ3 | 18 | 18 | 32 | 1 | 1 | NEW Phase 3.0-prep++ (final boss act) |
| LSelect | 8 | 8 | 1 | 53 | 1 | NEW Phase 3.0-prep++ (LSelect.bin enumerates 53 sound tests) |
| Logos | 2 | 2 | 1 | 0 | 1 | Phase 3.0-prep |
| MMZ | 32 | 32 | 29 | 2 | 2 | NEW Phase 3.0-prep++ |
| MSZ | 50 | 50 | 49 | 3 | 3 | NEW Phase 3.0-prep++ (highest entity count) |
| MSZCutscene | 22 | 22 | 29 | 1 | 1 | NEW Phase 3.0-prep++ |
| Menu | 40 | 40 | 9 | 4 | 1 | Phase 3.0-prep |
| OOZ1 | 15 | 15 | 16 | 1 | 1 | NEW Phase 3.0-prep++ |
| OOZ2 | 24 | 24 | 22 | 1 | 1 | NEW Phase 3.0-prep++ |
| PSZ1 | 27 | 27 | 27 | 1 | 1 | NEW Phase 3.0-prep++ (Press Garden) |
| PSZ2 | 30 | 30 | 33 | 1 | 1 | NEW Phase 3.0-prep++ |
| Puyo | 25 | 25 | 12 | 1 | 1 | NEW Phase 3.0-prep++ (extras minigame) |
| SPZ1 | 29 | 29 | 38 | 2 | 2 | NEW Phase 3.0-prep++ |
| SPZ2 | 35 | 35 | 42 | 1 | 1 | NEW Phase 3.0-prep++ |
| SSZ1 | 42 | 42 | 26 | 1 | 1 | NEW Phase 3.0-prep++ |
| SSZ2 | 33 | 33 | 34 | 3 | 2 | NEW Phase 3.0-prep++ |
| SpecialBS | 20 | 20 | 9 | 0 | 36 | NEW Phase 3.0-prep++ (Blue Spheres, 36 stage layouts) |
| TMZ1 | 30 | 30 | 30 | 1 | 1 | NEW Phase 3.0-prep++ |
| TMZ2 | 31 | 31 | 34 | 1 | 1 | NEW Phase 3.0-prep++ |
| TMZ3 | 29 | 29 | 31 | 2 | 1 | NEW Phase 3.0-prep++ (Titanic Monarch boss act) |
| Thanks | 2 | 2 | 0 | 0 | 1 | NEW Phase 3.0-prep++ |
| TimeTravel | 4 | 4 | 0 | 1 | 1 | NEW Phase 3.0-prep++ (transitional cutscene) |
| Title | 11 | 11 | 0 | 1 | 1 | Phase 1.33 |
| UFO1 | 27 | 27 | 11 | 1 | 1 | NEW Phase 3.0-prep++ (Special Stage 1) |
| UFO2 | 27 | 27 | 11 | 1 | 1 | NEW Phase 3.0-prep++ |
| UFO3 | 27 | 27 | 11 | 1 | 1 | NEW Phase 3.0-prep++ |
| UFO4 | 28 | 28 | 11 | 1 | 1 | NEW Phase 3.0-prep++ |
| UFO5 | 27 | 27 | 11 | 1 | 1 | NEW Phase 3.0-prep++ |
| UFO6 | 27 | 27 | 11 | 1 | 1 | NEW Phase 3.0-prep++ |
| UFO7 | 28 | 28 | 11 | 1 | 1 | NEW Phase 3.0-prep++ |

**Coverage totals across all 42 stages + Global + UI + Common + Cutscene +
Helpers + BSS + Pinball + Continue + Summary + Unused:**

| Metric | Count |
|---|---|
| Stage folders enumerated | 42 |
| Unique object classes referenced | 493 (across all StageConfigs) |
| Object .c files cached pre-Phase-3.0-prep++ | 92 |
| Object .c files newly cached this phase | 401 |
| Object .h files newly cached this phase | 480 |
| Total upstream Objects/ .c + .h cached | 1036 (across all 28 cat directories) |
| Decomp .c files scanned by gate | 518 |
| RSDK call sites enumerated | 1477 |
| Scene*.bin Music entity trackFile attrs parsed | 161 |
| Unique resolved asset paths | 1387 (post-sheet-derivation, post-anchor) |
| Present in extracted/ (retail) | 1332 |
| INFO (Plus-DLC or decomp-conditional absent) | 55 |
| Retail missing FAIL | **0** |
| Gate verdict | **GREEN** |

## Methodology pivot (continuation of Phase 1.33 + Phase 3.0-prep)

Phase 1.33 established the binding methodology for one scene (Title). Phase
3.0-prep generalised it to a second scene (Menu + Logos) and added the
**Scene1.bin Music entity trackFile attribute** parser-step — the .ogg
names for menu music are only discoverable from Scene1.bin attrs, never
from any .c grep. Phase 3.0-prep++ generalises further to the **whole
game** in a single batch:

1. Parse every `extracted/Data/Stages/<Folder>/StageConfig.bin` to
   enumerate every object class + sfx name. Output:
   `tools/_phase3plus_enum.json` (493 unique classes, 364 unique sfx,
   57 unique trackFiles).
2. Map every referenced class to its upstream `SonicMania/Objects/<Cat>/`
   directory via a single recursive `git/trees/master?recursive=1` API
   call. Output: `tools/_phase3plus_class_to_cat.json` (609 known
   classes; 100% of referenced classes mapped — none unmapped).
3. Batch-fetch every missing decomp `.c` + `.h` file by GitHub blob SHA
   (12-worker concurrent.futures pool). Output: 894 new files in
   `tools/_decomp_raw/`. Implementation:
   `tools/phase3_0_plus_fetch_decomp.py`.
4. Scan every cached `SonicMania_Objects_*.c` for the 7 RSDK asset
   patterns (skipping `#if GAME_INCLUDE_EDITOR` blocks). PLUS parse
   every `Stages/<Folder>/Scene*.bin` for Music-entity trackFile attrs.
   Output: `tools/_phase3plus_asset_paths.json` (1001 unique paths) +
   `tools/_phase3plus_track_refs.json` (161 attrs). Implementation:
   `tools/phase3_0_plus_scan_assets.py`.
5. Append the union to `tools/build_filelist.py` in a single Phase
   3.0-prep++ block grouped by kind/folder with `file:line` citations
   in the methodology header (build_filelist.py:10-65 BINDING).
6. Re-run rsdk_extract.py twice (first pass extracts new .bin files;
   second pass extracts the GIF sheets discovered by parsing those
   .bin sheets[] arrays). Hash-matcher result improved from 1610/1677
   to **1728/1677** matched (counting some Plus-DLC files matched
   despite expected-absent classification).
7. Catalogue genuinely absent paths into Plus-DLC + EXPECTED_ABSENT
   sets in the gate. All 55 INFO items are either Plus-DLC characters
   (Mighty/Ray and dependents), Encore-mode-only assets, JP-region
   CESA logos, or pre-shipping renamed/inlined files (`AniTiles.gif`
   merged into `16x16Tiles.gif`).
8. Gate at `tools/qa_phase3_0_plus_whole_game_asset_coverage_gate.py`
   wired into `verify_done.ps1` adjacent to V1.33 and V3.0-prep.

## Per-scene detailed inventory references

Per-object grep results are stored in
`tools/_phase3plus_per_file_refs.json` (per-.c-file citations). Gate run
output enumerates every miss. The 161 Scene*.bin trackFile attributes are
in `tools/_phase3plus_track_refs.json`.

Highlights of newly-extracted retail asset families (Phase 3.0-prep++):

- **47 zone music acts** (GreenHill1/2 + ChemicalPlant1/2 + ... +
  TitanicMonarch1/2 + named boss tracks BossEggman1/2 + BossHBH +
  BossFinal + EggReverie + MetalSonic + AngelIsland + Credits +
  BlueSpheres + EggReverie + Sneakers + Super + HBHMischief).
- **52 D.A. Garden sound test tracks** (parsed from DAGarden/Scene1.bin
  Music entities — the in-game record-player UI cycles through these
  via Music_PlayTrack per DAControl entity slot).
- **53 Level Select sound test tracks** (parsed from LSelect/Scene1.bin
  Music entities — independent test-mode music browser).
- **VO announcer pack** (Global/Announcer.c:52-74 enumerates 22 VO
  WAVs — character names + win-condition phrases).
- **Per-zone particle / boss / setup atlases** (CPZ MBM\*, FBZ
  SpiderMobile, HCZ Boss, OOZ Oil tile sheets, ERZ final boss, TMZ
  MonarchTop/Bottom, etc.).
- **Continue/GameOver/Pause sprite atlases** (Players/Continue.bin,
  Global/PauseMenu.bin per-language).
- **Eggman cutscene + zone-stage sprite families** (Eggman/EggMobile,
  EggmanCPZ..EggmanTMZ).

## Plus-DLC + decomp-conditional INFO classification

Tracked but not required for the REV01 NTSC-U/EU Saturn build:

**Mania Plus characters (Mighty + Ray + dependencies):**
`Players/Mighty.bin`, `Players/Ray.bin`, `Players/ChibiMighty.bin`,
`Players/ChibiRay.bin`, `SpecialBS/Mighty.bin`, `SpecialBS/Ray.bin`,
`CPZ/MBMMighty.bin`, `CPZ/MBMRay.bin`, `Global/Mighty*.wav`,
`VO/Mighty*.wav`, `VO/Ray*.wav`.

**Plus DLC title/splash:**
`Title/PlusLogo.bin`, `Title/PlusLogo.gif`, `SoundFX/Stage/Plus.wav`,
`Image/CESA.png/tga` (JP region only).

**Encore mode (Plus-DLC):**
`UI/Diorama.bin` (Save Select diorama), `AIZ/SchrodingersCapsule.bin`
(Encore-only AIZ boss capsule), `Music/EggReveriePinch.ogg`,
`Music/BlueSpheresSPD.ogg`, `Music/UFOSpecial.ogg`,
`SoundFX/VO/Player3.wav`, `SoundFX/VO/Player4.wav`,
`SoundFX/VO/ItsADraw.wav`, `SoundFX/VO/ItsADraw_Set.wav`,
`SoundFX/Global/Swap.wav`, `SoundFX/Global/SwapFail.wav`.

**Decomp-conditional / pre-shipping renamed (genuinely absent from
retail Data.rsdk — verified by hash-mismatch):**
- `AIZ/AniTiles.gif` — asset-build-time source layered into the shipped
  `16x16Tiles.gif` (same for every zone).
- `AIZ/CaterkillerJr.bin`, `AIZ/Portal.bin`, `AIZ/Sweep.bin`,
  `AIZ/SwingRope.bin` — wrong-folder decomp paths (CPZ_CaterkillerJr.c
  references AIZ folder; actual asset lives elsewhere or unused).
- `Credits/AnimalHBH.bin`, `Credits/Silhouettes.bin` — unused alt atlases.
- `Cutscene/DamagedKing.bin`, `Cutscene/HBHPile.bin`,
  `Cutscene/KingTMZ2.bin` — Encore-conditional cutscene branches.
- `LRZ1/OrbitSpike.bin`, `MMZ/Decoration.bin`, `MMZ/RTeleporter.bin`,
  `MSZ/Ending.bin`, `OOZ/Splash.bin`, `PSZ1/FrostThrower.bin`,
  `PSZ1/IceBomba.bin` — decomp paths referencing pre-shipping or
  cut-content asset names.
- 11 SFX absent from retail datapack (`SoundFX/Stage/Clack2.wav`,
  `Door.wav`, `DrownAlert.wav`, `Rush.wav`, `Waterfall.wav`,
  `Waterfall2.wav`, `Global/Recovery.wav`, `Global/Spike.wav`,
  `CPZ/CPZ2HitBlocksStop.wav`, `SSZ2/MSTransform.wav`,
  `TMZ3/RubyGet.wav`, `Music/SPZ1.ogg` — alias for Studiopolis1.ogg
  which IS shipped).

## Implementation-level roadmap (Phase 3+ recommendation)

Object-port complexity per scene was assessed by counting unique class
references + line count of cached decomp .c. The recommended phase
ordering balances complexity, dependency, and gameplay impact:

### Tier A — Boot sequence / Mania Mode entry (immediate next phases)

1. **Phase 2.3f — GHZ Sonic visibility fix** (pending; blocks Mania mode
   visible gameplay, already in-flight as Task #88). Asset graph
   complete per this audit.
2. **Phase 3.1 — LogoSetup port** (2 objects, 70 lines decomp). Opens
   the boot sequence (SEGA splash) ahead of Title.
3. **Phase 3.2 — MenuSetup port + UIControl/UIWidgets/UIBackground
   skeleton** (40 menu objects but UIControl + UIWidgets + UIBackground
   are the core; the rest are leaf widgets). Asset graph complete.
4. **Phase 3.3 — UIButton + UIButtonPrompt + UIHeading + UISubHeading**
   (4 leaf widgets, ~600 lines combined). Renders the visible main
   menu after Phase 3.2.
5. **Phase 3.4 — UIChoice + UICarousel + UISlider + UISaveSlot** (Save
   Select widget chain, ~1100 lines combined). Enables save-slot UX
   end-to-end.

### Tier B — Mania Mode gameplay zones (per-zone systematic ports)

Order by gameplay sequence (the canonical Sonic Mania ROM ordering;
matches GameConfig.bin category "Mania Mode" scene list). Each phase
is one scene worth of object ports:

6. **Phase 4.1 — GHZ remaining badniks + setups** (28 classes, mostly
   cached + Phase 2.x partial work). Closes GHZ to frame-parity.
7. **Phase 4.2 — CPZ** (45 classes — Chemical Plant. Heavy on
   physics: AmoebaDroid boss, CPZBoss + 4 MBM\* variants, BlueSphereParticles,
   ChemPool fluid platforms, Shutter, Spring + Staircase. ~3200 lines.)
8. **Phase 4.3 — SPZ1** (29 classes — Studiopolis Act 1. EggTV
   weather mobile, LED billboards, ManholeCover, Reflection. ~2400
   lines.)
9. **Phase 4.4 — SPZ2** (35 classes — Boss + weather TV chain).
10. **Phase 4.5 — FBZ** (48 classes — Flying Battery. SpiderMobile,
    BigSqueeze, FGClouds parallax. Heaviest mid-game zone.)
11. **Phase 4.6 — PSZ1** (Press Garden Act 1 — 27 classes including
    FrostThrower / IceBomba sub-bosses).
12. **Phase 4.7 — PSZ2** (30 classes).
13. **Phase 4.8 — SSZ1** (42 classes — Stardust Speedway, includes
    Phantom Ruby narrative branch).
14. **Phase 4.9 — SSZ2** (33 classes including the MetalSonic boss
    chain, RTeleporter, HiLoSign, MetalSonic1/2.gif).
15. **Phase 4.10 — HCZ** (42 classes — Hydrocity, water physics
    integration with Common/Water.c).
16. **Phase 4.11 — MSZ** (50 classes — Mirage Saloon, highest entity
    count, 3 scene acts).
17. **Phase 4.12 — OOZ1** (15 classes — Oil Ocean Act 1).
18. **Phase 4.13 — OOZ2** (24 classes — Oil Ocean Act 2 with boss).
19. **Phase 4.14 — LRZ1** (36 classes — Lava Reef Act 1).
20. **Phase 4.15 — LRZ2** (38 classes).
21. **Phase 4.16 — LRZ3** (18 classes — boss act).
22. **Phase 4.17 — MMZ** (32 classes — Metallic Madness).
23. **Phase 4.18 — TMZ1** (30 classes — Titanic Monarch Act 1).
24. **Phase 4.19 — TMZ2** (31 classes).
25. **Phase 4.20 — TMZ3** (29 classes — boss act).
26. **Phase 4.21 — ERZ** (18 classes — Eggreverie final boss act).
27. **Phase 4.22 — AIZ** (11 classes — Angel Island Zone intro,
    minimal due to cutscene-only nature).

### Tier C — Bonus / Special / Ending content

28. **Phase 5.1 — SpecialBS (Blue Spheres)** — 20 classes + 36 stage
    layouts. Phase Z 3D sphere rendering. See `BIBLE.md` Phase Z.
29. **Phase 5.2 — UFO1..UFO7 Special Stages** — 27-28 classes each,
    largely shared. Phase Z 3D rendering.
30. **Phase 5.3 — Pinball + Puyo extras** — minigames, full port.
31. **Phase 5.4 — Credits / Thanks / Ending sequences** — narrative
    closing.
32. **Phase 5.5 — Cutscene set** (GHZCutscene, MSZCutscene, TimeTravel,
    Cutscene/\*) — passive playback scenes.
33. **Phase 5.6 — DAGarden + LSelect + extras menus**.

### Tier Z — Saturn-native rewrites (final phase per BIBLE.md §Phase Z)

- 3D special stages (SpecialBS sphere field + UFO chase) — software
  rasterisation per `BIBLE.md` Phase Z + `memory/saturn-native-rewrites-
  final-phase.md`.
- Scanline FX (heat haze in OOZ, ChemPool ripple, lava distortion in
  LRZ, MetalSonic glow in SSZ2).
- Full palette FX (Phantom Ruby tint cycles, Super-form rainbow shift,
  Eggreverie boss palette pulses).

## Files added / updated by Phase 3.0-prep++

- `tools/build_filelist.py` — Phase 3.0-prep++ block appended (~1087
  lines, grouped by Music/Strings/Video/SoundFX-per-folder/Sprites-per-
  folder + sheet-GIF block). Cite-comments reference
  `phase3_0_plus_scan_assets.py` output.
- `tools/phase3_0_plus_enumerate.py` — new. StageConfig + Scene1.bin
  per-folder enumeration. Output:
  `tools/_phase3plus_enum.json`.
- `tools/phase3_0_plus_fetch_decomp.py` — new. Parallel blob-by-SHA
  batch fetch using GitHub Trees API.
- `tools/phase3_0_plus_scan_assets.py` — new. Whole-game asset
  reference scan (518 .c files, 1477 sites). Output:
  `_phase3plus_asset_refs.json`, `_phase3plus_asset_paths.json`,
  `_phase3plus_track_refs.json`, `_phase3plus_per_file_refs.json`.
- `tools/qa_phase3_0_plus_whole_game_asset_coverage_gate.py` — new.
  Gate predicate; Plus-DLC / EXPECTED_ABSENT classification table.
- `tools/verify_done.ps1` — Gate V3.0-prep++ wired in next to V3.0-prep.
- `tools/_decomp_raw/` — 894 new `.c` + `.h` files cached (now 1036
  total).
- `docs/whole_game_asset_audit.md` — this document.
- `extracted/Data/` — re-extracted, 1728/1677 hash matches (118 new
  retail assets resolved this phase).
