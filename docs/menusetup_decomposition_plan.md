# Phase 3.2 — MenuSetup Decomposition Plan

- **Date:** 2026-05-28
- **Author:** decomposition planning task (Phase 3.2 / Task #124)
- **Status:** planning artifact only — no source-code changes
- **Scope:** decompose the Sonic Mania MenuSetup subsystem (52+ Menu/UI*
  decomp .c files + PauseMenu) into bounded sub-tasks the Phase 3.3+
  dispatcher will TaskCreate in dependency order.
- **Authoritative sources:**
  - Decomp cache: `tools/_decomp_raw/SonicMania_Objects_Menu_*.c`
    (52 Menu/* files + `SonicMania_Objects_Global_PauseMenu.c`)
  - Asset audit: `docs/menu_scene_asset_audit.md` (Phase 3.0-prep)
  - Scene entity table: `extracted/Data/Stages/Menu/Scene1.bin`
    (parsed via `tools/parse_title_entities.py`)
  - Existing Saturn-side stub: `src/mania/Objects/Menu/MenuSetup.{c,h}`
    (Phase 3.2 MVP — direct-tick text overlay; this plan supersedes it)
  - Existing project plan section: `docs/COMPREHENSIVE_PLAN.md` §3.2,
    §7758 ("Phase 3.2 — MenuSetup + UIControl/UIWidgets/UIBackground
    skeleton")

Project policy locks honoured: NTSC-U only; 3-button + 6-button + 3D pad
+ multitap; full 13 zones + Plus + special modes scope; auto-propose
lowest-impact mitigations.

---

## Executive summary

| Metric | Value |
|---|---|
| Decomp Menu/* .c files cataloged | 52 |
| + Global PauseMenu | 1 (53 total) |
| Total Menu/Global LOC parsed | ~24,123 (Menu 23,124 + PauseMenu 999) |
| Sub-tasks proposed | 14 (3.2.a … 3.2.n) |
| Foundation tier (blocks all) | 2 (3.2.a + 3.2.b) |
| Critical path to "title -> MainMenu -> Mania Mode -> Save Select -> GHZ" | 3.2.a -> 3.2.b -> 3.2.c -> 3.2.f -> 3.2.h -> 3.2.l |
| Parallel-eligible after critical path | 6 (3.2.d, 3.2.e, 3.2.g, 3.2.i, 3.2.j, 3.2.k) |
| Sub-agent budget total (estimate) | 32 agents (cap 3 in parallel per policy) |
| New assets to extract beyond Phase 3.0-prep | 0 (Phase 3.0-prep already covers Menu Scene1) |
| Menu/Scene1.bin distinct entity classes | 41 |
| Menu/Scene1.bin total entity instances | ~300 (27 UIControl + 27 UIBackground + 45 UIButton + 31 UIWidgets+ ...) |

The "Mania Mode -> Save Select -> Zone Select -> GHZ" flow is gated on
five sub-tasks completing: 3.2.a (UIControl), 3.2.b (UIButton +
UIWidgets), 3.2.c (UIBackground + UISubHeading + UITransition), 3.2.f
(UISaveSlot + ManiaModeMenu integration), 3.2.l (MenuParam +
MenuSetup integration). The rest (Options, Time Attack, Competition,
Pause) are parallel branches off the same foundation.

---

## Section 1 — Decomp file inventory

All 52 Menu/* .c files plus `SonicMania_Objects_Global_PauseMenu.c` are
cached at `tools/_decomp_raw/SonicMania_Objects_*` (verified via
directory listing). No additional fetches required for the
decomposition step itself.

### 1.1 — Foundation widgets (used by every other widget)

| File | LOC | Functions (key state machines) | Assets loaded (via StageLoad or referenced) | Notes |
|---|---|---|---|---|
| `Menu_UIControl.c` | 876 | Update / StaticUpdate / Create / StageLoad + ProcessInputs + MoveSelection + HandlePosition + MatchMenuTag + SetActiveMenu + ContainsPos + ChangeMenuTrack + ClearInputs | (Editor only: `Editor/EditorIcons.bin`) | Input router; **gateway for everything else**. Owns `buttonID`, `lastButtonID`, `state`, `menuUpdateCB`, `backPressCB`. Per-control tag string ("Main Menu", "Save Select", etc.). |
| `Menu_UIControl.h` | (header) | EntityUIControl struct: ~50 fields | — | RSDK_OBJECT. Critical struct. |
| `Menu_UIWidgets.c` | 353 | Update / StaticUpdate / StageLoad + ApplyLanguage + DrawParallelogram + DrawRectOutline + DrawUpDownArrows + DrawRightTriangle | `UI/UIElements.bin`, `UI/SaveSelect.bin` (Plus), `UI/SmallFont.bin`, `UI/TextEN..TC.bin` (per-language), `UI/Buttons.bin`; SFX Bleep/Accept/Warp/Event/Woosh/Fail | Globals: `textFrames` (per-language), `uiFrames`, `fontFrames`, `saveSelFrames`. All widgets read `UIWidgets->textFrames`. |
| `Menu_UIBackground.c` | 91 | Update / Draw (calls stateDraw) / Create / StageLoad + DrawNormal | colors table only | Animated circles backdrop. **Phase 1 Saturn port can stub Draw to FillScreen + Phase 2 add the circle outlines.** Mania Mode menu uses different `stateDraw`. |
| `Menu_UIBackground.h` | (header) | EntityUIBackground struct | — | Lightweight. |

### 1.2 — Generic button + label widgets

| File | LOC | Key behavior | Assets | Notes |
|---|---|---|---|---|
| `Menu_UIButton.c` | 967 | Update / Draw / Create / StageLoad + ButtonEnterCB + ButtonLeaveCB + State_HandleButtonEnter / Leave / ProcessButtonCB + GetChoicePtr | `UIWidgets->textFrames` (listID-driven) | Heart of widget tree. Owns `actionCB`, `selectedCB`, `processButtonCB`. Reads from `UIWidgets->textFrames`. |
| `Menu_UIButton.h` | (header) | EntityUIButton struct: many fields | — | Parallelogram-drawn buttons. |
| `Menu_UIButtonPrompt.c` | 566 | Update / Draw / Create / StageLoad + SetButtonSprites + GetButtonMappings + per-platform mappings | `UI/Buttons.bin` (sheet 0), `UIWidgets->textFrames` (prompt list) | Controller-button glyph display. Saturn port: 3-button/6-button glyph SHEET subset. |
| `Menu_UIButtonPrompt.h` | (header) | EntityUIButtonPrompt struct | — | |
| `Menu_UIButtonLabel.c` | 83 | Update / Draw / Create / StageLoad | `UI/ButtonLabel.bin` | Static label widget. |
| `Menu_UIButtonLabel.h` | (header) | — | — | |
| `Menu_UISubHeading.c` | 450 | Update / Draw / Create + ButtonActionCB family + scene-transition action callbacks | `UIWidgets->textFrames` | Section headers + the "back" transition handler entry points. Decomp comment line 79 notes "Why is this all here / SaveSelectMenu would make more sense". |
| `Menu_UISubHeading.h` | (header) | — | — | |
| `Menu_UIText.c` | 110 | Update / Draw / Create / StageLoad + SetText | `UIWidgets->textFrames` | Static text rendering. |
| `Menu_UIText.h` | (header) | — | — | |
| `Menu_UIInfoLabel.c` | 96 | Update / Draw / Create / StageLoad + SetText | `UI/TextEN.bin` | Info pop label (e.g. competition total). |
| `Menu_UIInfoLabel.h` | (header) | — | — | |
| `Menu_UIHeading.c` | 113 | Update / Draw / Create / StageLoad | `UI/HeadingsEN..TC.bin` (per-language) | Big section headers. |
| `Menu_UIHeading.h` | (header) | — | — | |
| `Menu_UIShifter.c` | 84 | Update / Draw + scroll-offset helpers | none (composes child positions) | Vertical scroll-offset behavior for menu pages. |
| `Menu_UIPicture.c` | 114 | Update / Draw / Create / StageLoad | `UI/Picture.bin`, `Logos/Logos.bin` | Static picture widget; Thanks/Credits use it. |
| `Menu_UIPicture.h` | (header) | — | — | |
| `Menu_UIWaitSpinner.c` | 153 | Update / Draw + StartWait + FinishWait | `UI/WaitSpinner.bin` | Loading spinner. |

### 1.3 — Choice / toggle widgets

| File | LOC | Key behavior | Assets | Notes |
|---|---|---|---|---|
| `Menu_UIChoice.c` | 371 | Update / Draw / Create / StageLoad + ProcessButtonCB + State_HandleButtonEnter/Leave | `UI/SaveSelect.bin`, `UIWidgets->textFrames` | Left/right arrow toggle (e.g. Mania/Encore mode). |
| `Menu_UIChoice.h` | (header) | — | — | |
| `Menu_UIOptionPanel.c` | 149 | Update / Draw / Create + ButtonAction helpers | `UIWidgets->textFrames` | Container for an option group (Video / Sound / Controls). |
| `Menu_UIOptionPanel.h` | (header) | — | — | |
| `Menu_UIResPicker.c` | 386 | Update / Draw / Create + resolution-cycle helpers | `UI/SaveSelect.bin` | Resolution choice; **Saturn-stub: NTSC-U 320x224 fixed.** |
| `Menu_UIResPicker.h` | (header) | — | — | |
| `Menu_UIWinSize.c` | 401 | Update / Draw / Create + windowed-scale helpers | `UI/SaveSelect.bin` | Window-size choice; **Saturn-stub: not applicable, hide widget.** |
| `Menu_UIWinSize.h` | (header) | — | — | |
| `Menu_UISlider.c` | 382 | Update / Draw / Create + value-slide state | `UIWidgets->textFrames` | Continuous-value slider (volumes). |
| `Menu_UISlider.h` | (header) | — | — | |

### 1.4 — Character + save-slot widgets (Mania Mode)

| File | LOC | Key behavior | Assets | Notes |
|---|---|---|---|---|
| `Menu_UICharButton.c` | 283 | Update / Draw / Create / StageLoad + select callbacks | `UI/SaveSelect.bin` | Sonic / Tails / Knuckles / Mighty (Plus) / Ray (Plus). |
| `Menu_UICharButton.h` | (header) | — | — | |
| `Menu_UISaveSlot.c` | 1381 | Update / Draw / Create / StageLoad + HandleSaveIcons + SetupAnimators + SetupButtonElements + LoadSaveData + State_NotSelected / Selected / NewGame / DeleteSelected + lots of glyph cycles | `UI/SaveSelect.bin` | **Largest single Menu file**. Drives the 3-slot save-select UI. Reads SaveGame slots via `SaveGame_LoadFile` (`Objects/Global/SaveGame.c`). Saturn backup-RAM coupling here. |
| `Menu_UISaveSlot.h` | (header) | EntityUISaveSlot struct: 50+ fields | — | |
| `Menu_UIDiorama.c` | 996 | Update / Draw / Create / StageLoad + per-diorama states (MANIAMODE / TIMEATTACK / COMPETITION / OPTIONS / EXTRAS / ENCOREMODE / PLUSUPSELL / EXIT) + animated character previews | `UI/Diorama.bin`, `AIZ/SchrodingersCapsule.bin`, `Players/Sonic.bin`, `Players/Tails.bin`, `Players/KnuxCutsceneAIZ.bin`, `Players/KnuxCutsceneHPZ.bin`, `Players/Mighty.bin` (Plus), `Players/Ray.bin` (Plus), `Global/Ring.bin`, `Global/SpeedGate.bin`, `SpecialBS/Sonic.bin`, `SpecialBS/StageObjects.bin` | Mania Mode menu backdrop (animated GHZ diorama with character preview). **Saturn complexity hotspot**: many sprite atlases composited. Lowest-impact Saturn variant: static still-frame per diorama ID. |
| `Menu_UIMedallionPanel.c` | 69 | Draw + StageLoad | `UI/MedallionPanel.bin` | Silver/gold medallion counter. |
| `Menu_UIMedallionPanel.h` | (header) | — | — | |

### 1.5 — Zone-select widgets (Mania Mode, Time Attack, Competition)

| File | LOC | Key behavior | Assets | Notes |
|---|---|---|---|---|
| `Menu_UITAZoneModule.c` | 1208 | Update / Draw / Create / StageLoad + State_Idle / Spin / Selected / Confirmed + character-banner + medal-bar + ghost-record fetch | `UI/SaveSelect.bin` | Time Attack zone-select tile. Each TA scene has 12 instances (one per zone). Cross-couples with UITABanner + UILeaderboard. |
| `Menu_UITAZoneModule.h` | (header) | — | — | |
| `Menu_UITABanner.c` | 243 | Update / Draw / Create / StageLoad | `UI/SaveSelect.bin` | Character banner overlay for TA zone select. |
| `Menu_UIVsZoneButton.c` | 422 | Update / Draw / Create / StageLoad + zone-spin behavior | `UI/SaveSelect.bin`, `UI/TextEN.bin` | Competition Mode zone tile. |
| `Menu_UIVsZoneButton.h` | (header) | — | — | |
| `Menu_UIVsRoundPicker.c` | 395 | Update / Draw / Create / StageLoad | `UI/SaveSelect.bin` | Round-count picker for Competition. |
| `Menu_UIVsCharSelector.c` | 504 | Update / Draw / Create / StageLoad | `UI/SaveSelect.bin` | 2P character selector for Competition. |
| `Menu_UIVsCharSelector.h` | (header) | — | — | |
| `Menu_UIVsResults.c` | 352 | Update / Draw / Create / StageLoad | `UI/SaveSelect.bin`, `UI/TextEN.bin` | Competition Mode round-result panel. |
| `Menu_UIVsResults.h` | (header) | — | — | |
| `Menu_UIVsScoreboard.c` | 140 | Update / Draw / Create / StageLoad | `UI/SaveSelect.bin` | Competition Mode total scoreboard. |
| `Menu_UIVsScoreboard.h` | (header) | — | — | |
| `Menu_UIScoreboard` | — | (not a separate decomp .c; functionality folded into UITAZoneModule via UILeaderboard) | — | Per Phase 3.0-prep audit, no `UIScoreboard.c` exists upstream. |
| `Menu_UILeaderboard.c` | 536 | Update / Draw / Create / StageLoad + ranking-display + network-stub | `UI/SaveSelect.bin` | TA records display. Online portion **Saturn-stub: local-only.** |
| `Menu_UILeaderboard.h` | (header) | — | — | |
| `Menu_UIRankButton.c` | 406 | Update / Draw / Create / StageLoad | `UI/SaveSelect.bin` | Per-rank ranking row. |
| `Menu_UIReplayCarousel.c` | 634 | Update / Draw / Create / StageLoad + replay-carousel scrolling | `UI/SaveSelect.bin` | Plus DLC replay browser. **Saturn-stub: hide widget (no replay infrastructure).** |
| `Menu_UICarousel.c` | 185 | Update / Draw / Create + scroll helpers | — | Generic horizontal-scroll carousel. |
| `Menu_UIModeButton.c` | 340 | Update / Draw / Create / StageLoad + action callbacks | `UI/MainIcons.bin` | The 5/6/7 large icons on MainMenu (Mania / Encore / Time Attack / Competition / Options / Extras / Exit). |
| `Menu_UIModeButton.h` | (header) | — | — | |

### 1.6 — Transition + dialog + popup widgets

| File | LOC | Key behavior | Assets | Notes |
|---|---|---|---|---|
| `Menu_UITransition.c` | 322 | Update / Draw / Create / StageLoad + StartTransition + MatchNewTag + SetNewTag + State_Init / TransitionIn / TransitionOut + DrawShapes (parallelogram wipe) | none (vector-drawn) | Menu-to-menu wipe. Wraps three drawPos[] coords. Each menu transition routes here. |
| `Menu_UITransition.h` | (header) | — | — | |
| `Menu_UIDialog.c` | (cached) | — | `UIWidgets->textFrames` | YES/NO + OK/CANCEL dialog. Used by "exit game?" confirmations + save-overwrite prompts. |
| `Menu_UIDialog.h` | (cached) | — | — | |
| `Menu_UIPopover.c` | 313 | Update / Draw / Create + Open / Close helpers | `UIWidgets->textFrames` | Floating panel (Plus DLC info, etc.). |
| `Menu_UIUsernamePopup.c` | 163 | Update / Draw / Create / StageLoad | `UIWidgets->textFrames` | Replay-author popup. **Saturn-stub: hide widget.** |
| `Menu_UIUsernamePopup.h` | (header) | — | — | |
| `Menu_UIKeyBinder.c` | 495 | Update / Draw / Create / StageLoad + State_HandleButtonEnter / WaitForKey / Confirmed | `UI/Buttons.bin` | KB-binding widget. **Saturn-stub: not applicable (no KB).** |
| `Menu_UIKeyBinder.h` | (header) | — | — | |

### 1.7 — Menu-mode entry points

| File | LOC | Key behavior | Notes |
|---|---|---|---|
| `Menu_MenuSetup.c` | 2300 | Update / StaticUpdate / Draw / Create / StageLoad + Initialize + HandleUnlocks + SetupActions + InitAPI + ChangeMenuTrack + GetActiveMenu + 30+ action callbacks (MenuButton_ActionCB for each main-menu button) + State_HandleTransition + StartTransition / StartTransitionLB + BackPressCB family + transition-target callbacks (TA/VS/Save/Extras/...) | The orchestrator. **Largest decomp file** in this set. Wires every "Main Menu", "Save Select", "Time Attack", etc. UIControl tag to its corresponding action callback, plus transition routing. Plus uses ManiaModeMenu instead. |
| `Menu_MenuSetup.h` | (header) | ObjectMenuSetup with 30+ UIControl* pointer fields + state-callback typedefs | — |
| `Menu_MenuParam.c` | 32 | All Update/Static/etc empty + GetParam accessor | Global pass-state holder (`globals->menuParam`). Stores `selectionFlag`, `menuTag`, `levelID`, `puyoSelection`, `bssSelection`, etc. that survive scene transitions. |
| `Menu_MenuParam.h` | (header) | EntityMenuParam struct: 20+ fields | The cross-scene state hand-off vessel. |
| `Menu_MainMenu.c` | 323 | StaticUpdate + Create + Initialize + MenuButton_ActionCB + SetupActions + HandleUnlocks + BackPressCB_ReturnToTitle + ExitGame paths | Plus-DLC variant of MainMenu (different button order). Implements the 5/6/7 ModeButton actions. |
| `Menu_ManiaModeMenu.c` | 318 | Initialize + InitAPI + HandleMenuReturn + SetBGColors + ChangeMenuTrack + StartReturnToTitle + State_HandleTransition + HandleUnlocks + SetupActions | Plus version of MenuSetup_Initialize. Wires every sub-menu's button-action callbacks. |
| `Menu_TimeAttackMenu.c` | 1003 | StaticUpdate + Create + StageLoad + Initialize + Action callbacks + State machine | Owns TimeAttack zone-select + leaderboard wiring. |
| `Menu_CompetitionMenu.c` | 942 | StaticUpdate + Create + StageLoad + Initialize + State machine + Action callbacks | Owns Competition mode flow (2P rules, zone-select, results). |
| `Menu_OptionsMenu.c` | 882 | StaticUpdate + Create + Initialize + per-toggle ActionCB + apply-settings helpers | Options state machine. Mostly platform-coupled (KB/PS4/XB1/NX); Saturn-only subset. |
| `Menu_ExtrasMenu.c` | 268 | Initialize + Puyo/BSS/Credits/DAGarden ActionCBs | Extras-mode launcher. Each entry SetScene(). |

### 1.8 — Boot / sequencing entry points (not the main menu but co-located)

| File | LOC | Notes |
|---|---|---|
| `Menu_LogoSetup.c` | 158 | Phase 3.1 — already ported (in `src/mania/Objects/Menu/LogoSetup.{c,h}`). |
| `Menu_LogoSetup.h` | (header) | — |
| `Menu_ThanksSetup.c` | 177 | "Thanks for playing" end-of-game scene. Phase 13. |
| `Menu_ThanksSetup.h` | (header) | — |
| `Menu_DemoMenu.c` | 217 | Attract-mode demo recordings launcher. Phase 14 (Media Demo support). |
| `Menu_DemoMenu.h` | (header) | — |
| `Menu_DAControl.c` | 116 | D.A. Garden (sound test) control. Phase TBD (Extras). |
| `Menu_DAControl.h` | (header) | — |
| `Menu_DASetup.c` | 218 | D.A. Garden scene setup. Phase TBD (Extras). |
| `Menu_DASetup.h` | (header) | — |
| `Menu_LevelSelect.c` | 704 | Debug Level Select (cheats menu). Phase TBD. |
| `Menu_LevelSelect.h` | (header) | — |
| `Menu_E3MenuSetup.c` | 203 | E3-build alternate menu (`MANIA_USE_PLUS` branch). Phase 17 (Plus DLC). |
| `Menu_UICreditsText.c` | (cached) | Credits scrolling text. Phase 13. |
| `Menu_UICreditsText.h` | (cached) | — |
| `Menu_UIVideo.c` | 131 | UI-side intro-video player wrapper. Already handled by Cinepak path (Phase 1). |
| `Menu_UIVideo.h` | (header) | — |

### 1.9 — Pause menu (in-game; lives under Global/)

| File | LOC | Notes |
|---|---|---|
| `Global_PauseMenu.c` | 999 | Update / LateUpdate / StaticUpdate / Draw / Create / StageLoad + many state callbacks + SetupTintTable + HandleButtonPositions + per-button action CBs | Lives outside Menu/ but uses identical UIControl/UIButton/UIWidgets surface. Saturn coupling: needs `RSDK.SetEngineState(ENGINESTATE_FROZEN)` equivalent for gameplay freeze. |
| `Global_PauseMenu.h` | (header) | — | — |

### 1.10 — Menu/Scene1.bin entity table

Parsed via `tools/parse_title_entities.py extracted/Data/Stages/Menu/Scene1.bin`.

41 entity classes, 5 with known names (APICallback, Options, SaveGame,
Localization, Music) plus 36 unknown-hash class slots. Cross-reference
with the `tools/_decomp_raw/SonicMania_GameObjects.h` hash table will
identify each unknown class.

Recognised by entity count + behaviour pattern (visible-instance count
in the scene file):

| Hash (truncated) | Inferred class | Entity count | Notes |
|---|---|---|---|
| `14ba1dd5d21b88bcd9422764dc92b2d6` | UIControl | 27 | Matches the 27 sub-menu tags ("Main Menu", "Save Select", ...). |
| `65b25ae9fc8db4ac7380e68d1281e872` | UIBackground | 27 | Matches one backdrop per UIControl. |
| `a4f6654a7772842b0198bb6bfec1c1c5` | UIButton | 44 | Main menu icons + sub-menu rows. |
| `6300b745350b336df3a44e063fb7627c` | UIButton (variant) or UISubHeading | 45 | One of these classes shares column-count pattern. |
| `c34a9184a31ead1ba407dd8b0316b544` | UIButtonPrompt | 30 | Per-menu prompt glyphs. |
| `107a7e3d865052769726427c57013d35` | UIWidgets-decorated widget | 31 | High count = widgets w/ text. |
| `887fb65e65d184250f6f989a04dbac5c` | UIChoice | 12 | Toggle controls. |
| `65b8e5e352dfdb4a182064a42b165d23` | UIInfoLabel | 12 | |
| `589545e37fdfffc0e1e2a8f59431e240` | UIHeading | 11 | Per-page heading. |
| `7f453397be67d03f331bf3cddab1dbb8` | UICharButton | 9 | Char-select widgets. |
| `6c627fca0501d3f2307da3cde45aceb0` | UISubHeading | 8 | |
| `5f5e700113b61277757c5514e02b181a` | UIRankButton | 10 | TA rankings. |
| `d03305ef55b89c24a9b23607b987fb3a` | UIDiorama | 4 | Per-mode backdrop. |
| `3ec61db52de61026814cec9acbd79ee3` | UISaveSlot | 3 | THREE save slots. |
| `Music` | Music | 4 | trackID 0..3 — already in Phase 3.0-prep audit. |
| `f84b0cdab021b38e603769f86536bcfa` | UITransition (likely) | 2 | wipe entities. |
| `d7131e10fe5ea601fa79e5a57fafed57` | UITAZoneModule (TA zone tiles) | 2 | |
| `54f4625f0b5581c07c3420b7b8cabe26` | UIVsZoneButton | 4 | Competition zones. |
| `132d4e0cef4d08432a9c79d2ce32fd2f` | UIVsRoundPicker | 2 | |
| `589545e3...` (above) | — | — | |
| `764157597cb8eccd1998b0b988eaedf0` | UIVsResults | 2 | |
| Remaining 1-entity classes | UIScoreboard / UILeaderboard / UIMedallionPanel / UIVsScoreboard / UIVsCharSelector / UIShifter / UIKeyBinder / UIPicture / UIWaitSpinner / UIUsernamePopup / UICarousel / UIReplayCarousel / UIPopover / UIDialog | 1 each | Singletons. |

Sub-task 3.2.a hash-identification step: run `tools/parse_gameconfig.py`
against `extracted/Data/Game/GameConfig.bin` to dump the full class-name
-> hash map; cross-reference each unknown hash above to its decomp .c
file. This is a 10-minute task (done as part of 3.2.a foundation).

---

## Section 2 — Sub-task partitioning

Each sub-task includes: id, title, scope, decomp source files, asset
deps, Saturn-side files, acceptance gate, sub-agent budget, and
dependencies.

### 3.2.a — Foundation: UIControl + UIBackground + class-hash registry

- **Scope:** Port UIControl + UIBackground; expose the input router so
  later sub-tasks attach action callbacks. Build the Saturn-side
  scene-loader class-name -> registration table so the 41 Menu/Scene1
  classes resolve when `rsdk_load_scene("Menu")` runs. (Phase 3.2 MVP
  already silently skips unresolved classes — this task fixes that.)
- **Decomp sources:**
  - `tools/_decomp_raw/SonicMania_Objects_Menu_UIControl.c` (876 LOC)
  - `tools/_decomp_raw/SonicMania_Objects_Menu_UIControl.h`
  - `tools/_decomp_raw/SonicMania_Objects_Menu_UIBackground.c` (91 LOC)
  - `tools/_decomp_raw/SonicMania_Objects_Menu_UIBackground.h`
  - `tools/_decomp_raw/SonicMania_GameObjects.h` (for hash table)
- **Asset deps:** none (UIControl/UIBackground load no sprite atlases
  from the retail menu path — Editor refs excluded).
- **Saturn-side files to create/modify:**
  - `src/mania/Objects/Menu/UIControl.{c,h}`
  - `src/mania/Objects/Menu/UIBackground.{c,h}`
  - `src/mania/Game.c` (engine-init: register 41 new menu class slots)
  - `src/rsdk/object.c` / `src/rsdk/scene.c` (verify entity-resolve path
    no longer no-ops Menu entities)
  - `tools/parse_gameconfig.py` (extend to dump full Game class table
    if not already)
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2a_uicontrol_gate.py` — load Menu scene at boot;
    parse savestate; assert SLOT_UICONTROL_MAINMENU entity is alive
    (`classID != TYPE_BLANK`) and `entity->buttonID == startingID`.
    Initial build (no UIControl port) fails RED with "class slot unresolved".
- **Sub-agent budget:** 3 (port + class-registry + gate)
- **Dependencies:** none. **Gateway for everything else.**

### 3.2.b — Foundation: UIWidgets + asset bootstrap

- **Scope:** Port UIWidgets including `ApplyLanguage` (EN-only for
  NTSC-US per project policy), the SFX globals, the
  `DrawParallelogram` / `DrawRectOutline` / `DrawUpDownArrows` helpers.
  Wire up the asset loaders so child widgets can `RSDK.SetSpriteAnimation`
  against `UIWidgets->textFrames`, `UIWidgets->uiFrames`, etc.
- **Decomp sources:**
  - `Menu_UIWidgets.c` (353 LOC), `Menu_UIWidgets.h`
- **Asset deps (already extracted by Phase 3.0-prep):**
  - `UI/UIElements.bin` (+ .gif)
  - `UI/SmallFont.bin` (+ .gif)
  - `UI/TextEN.bin` (+ .gif) — only EN per policy (drop FR/IT/GE/SP/JP/KO/SC/TC)
  - `UI/Buttons.bin` (+ .gif)
  - `UI/SaveSelect.bin` (+ .gif)
  - SFX `Global/MenuBleep.wav`, `Global/MenuAccept.wav`,
    `Global/MenuWoosh.wav`, `Global/SpecialWarp.wav`,
    `Special/Event.wav`, `Stage/Fail.wav`
- **Saturn-side files:**
  - `src/mania/Objects/Menu/UIWidgets.{c,h}`
  - `src/rsdk/animation.c` (verify `LoadSpriteAnimation` returns valid
    handle for .bin atlases — already works for TitleSonic)
  - `src/rsdk/drawing.c` (add `rsdk_draw_parallelogram` /
    `rsdk_draw_rect_outline` helpers — Saturn-native VDP1 polygon path)
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2b_uiwidgets_gate.py` — capture frame after Menu
    scene load; assert `UIWidgets->textFrames != -1` (atlas loaded) by
    inspecting the entity's `aniFrames` field through savestate; assert
    `UIWidgets->sfxBleep != -1`.
- **Sub-agent budget:** 3 (port + drawing helpers + gate)
- **Dependencies:** 3.2.a (UIControl as host).

### 3.2.c — Generic-widget shell: UIButton + UIButtonPrompt + UISubHeading + UIHeading + UIText + UIInfoLabel + UIButtonLabel + UIShifter + UIPicture + UIWaitSpinner

- **Scope:** All the "generic widget" classes used by every menu page.
  Each is a thin wrapper around UIWidgets sprite-anim + a state machine.
- **Decomp sources:**
  - `Menu_UIButton.c` (967 LOC) + `.h`
  - `Menu_UIButtonPrompt.c` (566 LOC) + `.h`
  - `Menu_UISubHeading.c` (450 LOC) + `.h`
  - `Menu_UIHeading.c` (113 LOC) + `.h`
  - `Menu_UIText.c` (110 LOC) + `.h`
  - `Menu_UIInfoLabel.c` (96 LOC) + `.h`
  - `Menu_UIButtonLabel.c` (83 LOC) + `.h`
  - `Menu_UIShifter.c` (84 LOC)
  - `Menu_UIPicture.c` (114 LOC) + `.h`
  - `Menu_UIWaitSpinner.c` (153 LOC)
- **Asset deps:** `UI/ButtonLabel.bin`, `UI/Picture.bin`,
  `UI/WaitSpinner.bin`, `UI/HeadingsEN.bin`, `Logos/Logos.bin`
  (UIPicture references) — all present.
- **Saturn-side files:** one per decomp file under
  `src/mania/Objects/Menu/`.
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2c_widgets_gate.py` — Menu scene loaded; capture
    Mednafen frame; SSIM against `tools/refs/mania_pc/menu_main.png`
    (PC ref to capture during this task) for the central button-grid
    region (40x40 cells); threshold 0.40 (first iteration RED expected).
- **Sub-agent budget:** 3 (5 widgets / agent ~3.5 hours each)
- **Dependencies:** 3.2.a + 3.2.b.

### 3.2.d — UITransition + UIDialog + UIPopover

- **Scope:** Menu-to-menu wipes + modal dialogs (YES/NO, OK/CANCEL).
  Required for "exit game?", "delete save?", error dialogs.
- **Decomp sources:**
  - `Menu_UITransition.c` (322 LOC) + `.h`
  - `Menu_UIDialog.c` + `.h`
  - `Menu_UIPopover.c` (313 LOC)
- **Asset deps:** none (vector-drawn parallelograms).
- **Saturn-side files:** three pairs under `src/mania/Objects/Menu/`.
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2d_transition_gate.py` — trigger a tag-change
    (MatchMenuTag from "Main Menu" to "Save Select"); assert wipe-frame
    fires (sample pixel at (160,112) is black during transition window).
- **Sub-agent budget:** 2.
- **Dependencies:** 3.2.a + 3.2.b + 3.2.c.

### 3.2.e — UIChoice + UIOptionPanel + UISlider

- **Scope:** Multi-option toggle widgets + the Options panel container.
  Drives the entire Options sub-menu (Video / Sound / Controls /
  Language).
- **Decomp sources:**
  - `Menu_UIChoice.c` (371 LOC) + `.h`
  - `Menu_UIOptionPanel.c` (149 LOC) + `.h`
  - `Menu_UISlider.c` (382 LOC) + `.h`
- **Asset deps:** UI/SaveSelect.bin (shared widget atlas), already present.
- **Saturn-side files:** three pairs.
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2e_choice_gate.py` — focus a UIChoice widget;
    simulate LEFT-press; assert `selection` field decremented by 1.
- **Sub-agent budget:** 2.
- **Dependencies:** 3.2.a + 3.2.b + 3.2.c.

### 3.2.f — UISaveSlot + ManiaModeMenu integration (3-slot save select)

- **Scope:** The Save Select sub-menu (3-slot save select). Couples to
  Saturn backup-RAM module (Phase 3 / Task #122 SaveGame). Includes
  ManiaModeMenu_Initialize wiring for save-slot button callbacks.
- **Decomp sources:**
  - `Menu_UISaveSlot.c` (1381 LOC) + `.h` — **largest single port**
  - `Menu_ManiaModeMenu.c` (318 LOC) (save-select integration only;
    rest of ManiaModeMenu lands in 3.2.l)
  - `Objects/Global/SaveGame.c` (already cached; couple via existing
    `src/rsdk/save.c`)
- **Asset deps:** `UI/SaveSelect.bin`, `UI/SaveSelectEN.gif` (already
  extracted).
- **Saturn-side files:**
  - `src/mania/Objects/Menu/UISaveSlot.{c,h}`
  - `src/mania/Objects/Menu/ManiaModeMenu.{c,h}` (partial; save-select
    only)
  - `src/rsdk/save.c` extensions for 3-slot read/write
  - `src/mania/Objects/Global/SaveGame.c` (port from decomp)
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2f_saveslot_gate.py` — enter Save Select; render
    3 visible slot panels; cursor LEFT/RIGHT navigates `lastButtonID`
    in [0..2]; A on empty slot enters NEW GAME state, writes a save to
    backup RAM, scene transitions to GHZ Act 1.
- **Sub-agent budget:** 4 (UISaveSlot itself is dense + save-RAM
  coupling).
- **Dependencies:** 3.2.a + 3.2.b + 3.2.c + 3.2.d + Phase 3 SaveGame
  (backup-RAM module Task #122).

### 3.2.g — UICharButton + UIDiorama (Mania Mode char-select + animated backdrop)

- **Scope:** Character-select for Mania Mode (Sonic / Tails / Knuckles
  / Mighty / Ray) + the animated backdrop diorama (GHZ-styled with
  character preview). Saturn-side variant: lowest-impact mitigation is
  static still-frame per diorama ID (no animation), bumped to a 2-state
  cycle as a Phase Z follow-on.
- **Decomp sources:**
  - `Menu_UICharButton.c` (283 LOC) + `.h`
  - `Menu_UIDiorama.c` (996 LOC)
- **Asset deps:**
  - `UI/Diorama.bin`
  - `AIZ/SchrodingersCapsule.bin`
  - `Players/Sonic.bin`, `Players/Tails.bin`, `Players/KnuxCutsceneAIZ.bin`,
    `Players/KnuxCutsceneHPZ.bin`
  - Plus DLC variants (Mighty / Ray) **deferred to Phase 17**.
  - `Global/Ring.bin`, `Global/SpeedGate.bin`
  - `SpecialBS/Sonic.bin`, `SpecialBS/StageObjects.bin`
- **Saturn-side files:**
  - `src/mania/Objects/Menu/UICharButton.{c,h}`
  - `src/mania/Objects/Menu/UIDiorama.{c,h}`
- **Mitigation proposal (lowest-impact):** Phase 1 ports only diorama
  states MANIAMODE + TIMEATTACK + COMPETITION + OPTIONS + EXTRAS as
  STATIC still-frames (one sprite each); ENCOREMODE + PLUSUPSELL +
  EXIT deferred to Phase 17. Animated state cycling deferred to
  Phase Z (Saturn-native). User direction needed on this trade.
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2g_diorama_gate.py` — switch MainMenu cursor
    through 5 buttons; assert diorama->dioramaID transitions match
    expected sequence per `MainMenu.c` lines 27-37 (case-table mapping).
- **Sub-agent budget:** 3 (UIDiorama is dense).
- **Dependencies:** 3.2.a + 3.2.b + 3.2.c.

### 3.2.h — UIZoneModule (TA) + UITABanner + UIVsZoneButton + UIVsRoundPicker (zone-select widgets)

- **Scope:** TA zone-select + Competition zone-select. Coupled because
  they share `UI/SaveSelect.bin` atlas layout and similar State_Spin
  behavior.
- **Decomp sources:**
  - `Menu_UITAZoneModule.c` (1208 LOC) + `.h`
  - `Menu_UITABanner.c` (243 LOC)
  - `Menu_UIVsZoneButton.c` (422 LOC) + `.h`
  - `Menu_UIVsRoundPicker.c` (395 LOC)
- **Asset deps:** `UI/SaveSelect.bin`, `UI/TextEN.bin`,
  `extracted/Data/Sprites/UI/Zones.gif` (zone thumbnails; already
  present in Phase 3.0-prep).
- **Saturn-side files:** four widget pairs under
  `src/mania/Objects/Menu/`.
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2h_zonemodule_gate.py` — focus a TA zone tile;
    simulate LEFT-press; assert tile's `zoneIcon` frame index increments
    (cycle through 12 zones).
- **Sub-agent budget:** 3.
- **Dependencies:** 3.2.a + 3.2.b + 3.2.c.

### 3.2.i — UIVsCharSelector + UIVsResults + UIVsScoreboard (Competition Mode)

- **Scope:** 2P competition mode UI: char-select, mid-round results,
  end-of-Total scoreboard. Requires multitap input (project policy lock).
- **Decomp sources:**
  - `Menu_UIVsCharSelector.c` (504 LOC) + `.h`
  - `Menu_UIVsResults.c` (352 LOC) + `.h`
  - `Menu_UIVsScoreboard.c` (140 LOC) + `.h`
- **Asset deps:** `UI/SaveSelect.bin`, `UI/TextEN.bin`.
- **Saturn-side files:** three widget pairs + multitap input route in
  `src/rsdk/input.c`.
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2i_vscharselect_gate.py` — P1+P2 navigate
    independently; assert `p1CharID != p2CharID` is reachable;
    multitap detection mock passes.
- **Sub-agent budget:** 2.
- **Dependencies:** 3.2.a + 3.2.b + 3.2.c + 3.2.h + multitap-input
  module (Phase 2 / Task #110 verified).

### 3.2.j — UILeaderboard + UIRankButton + UIReplayCarousel + UIUsernamePopup (TA records display)

- **Scope:** TA records display. **Online portion is Saturn-stub:
  local-only records** per project policy (no NetLink leaderboard).
  Replay-carousel hidden (no replay infra). Username popup hidden.
- **Decomp sources:**
  - `Menu_UILeaderboard.c` (536 LOC) + `.h`
  - `Menu_UIRankButton.c` (406 LOC)
  - `Menu_UIReplayCarousel.c` (634 LOC) — **stub only**
  - `Menu_UICarousel.c` (185 LOC)
  - `Menu_UIUsernamePopup.c` (163 LOC) — **stub only**
- **Asset deps:** `UI/SaveSelect.bin` already present.
- **Saturn-side files:** five widget pairs; UIReplayCarousel and
  UIUsernamePopup compile as empty entity slots so scene-load doesn't
  fail on unresolved hashes.
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2j_leaderboard_gate.py` — open Leaderboards from
    TA menu; assert at least one UIRankButton entity has visible
    parallelogram drawn at expected position from `Scene1.bin` slot 60.
- **Sub-agent budget:** 2.
- **Dependencies:** 3.2.a + 3.2.b + 3.2.c + 3.2.h.

### 3.2.k — UIResPicker + UIWinSize + UIKeyBinder + UIMedallionPanel (Options sub-widgets, mostly Saturn-stub)

- **Scope:** Mostly platform-stubbed widgets. Per policy: NTSC-US
  Saturn = 320x224 fixed (UIResPicker has 1 valid choice); no windowed
  mode (UIWinSize hidden); no KB binding (UIKeyBinder hidden);
  UIMedallionPanel is the only widget actively rendered (silver/gold
  medal counter).
- **Decomp sources:**
  - `Menu_UIResPicker.c` (386 LOC) + `.h` — stub
  - `Menu_UIWinSize.c` (401 LOC) + `.h` — stub
  - `Menu_UIKeyBinder.c` (495 LOC) + `.h` — stub
  - `Menu_UIMedallionPanel.c` (69 LOC) + `.h` — port fully
- **Asset deps:** `UI/MedallionPanel.bin` already present.
- **Saturn-side files:** four widget pairs.
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2k_medallion_gate.py` — load Options; assert
    UIMedallionPanel entity visible with rendered counter (default 0/32
    for empty save).
- **Sub-agent budget:** 1.
- **Dependencies:** 3.2.a + 3.2.b + 3.2.c.

### 3.2.l — MenuParam + MenuSetup orchestrator + MainMenu + sub-menu Initialize integration

- **Scope:** **Integration sub-task.** Replace the Phase 3.2 MVP direct-
  tick path with the real MenuSetup_StageLoad + MenuSetup_Initialize +
  per-button action-CB wiring. Routes button presses to scene
  transitions (Mania Mode -> Save Select -> ZoneSelect -> GHZ; Options
  -> Sound; etc.).
- **Decomp sources:**
  - `Menu_MenuSetup.c` (2300 LOC) — the orchestrator
  - `Menu_MenuSetup.h`
  - `Menu_MenuParam.c` (32 LOC) + `.h` — trivial
  - `Menu_MainMenu.c` (323 LOC) — Plus-branch action callbacks
  - `Menu_ManiaModeMenu.c` (318 LOC) — Plus-branch Initialize
  - `Menu_TimeAttackMenu.c` (1003 LOC) — TA action wiring
  - `Menu_CompetitionMenu.c` (942 LOC) — Competition action wiring
  - `Menu_OptionsMenu.c` (882 LOC) — Options action wiring
  - `Menu_ExtrasMenu.c` (268 LOC) — Extras action wiring
- **Asset deps:** Music tracks already extracted: `MainMenu.ogg`,
  `Competition.ogg`, `Results.ogg`, `SaveSelect.ogg`.
- **Saturn-side files:**
  - `src/mania/Objects/Menu/MenuSetup.{c,h}` (replace Phase 3.2 MVP)
  - `src/mania/Objects/Menu/MenuParam.{c,h}`
  - `src/mania/Objects/Menu/MainMenu.{c,h}`
  - `src/mania/Objects/Menu/ManiaModeMenu.{c,h}` (extend from 3.2.f)
  - `src/mania/Objects/Menu/TimeAttackMenu.{c,h}`
  - `src/mania/Objects/Menu/CompetitionMenu.{c,h}`
  - `src/mania/Objects/Menu/OptionsMenu.{c,h}`
  - `src/mania/Objects/Menu/ExtrasMenu.{c,h}`
  - `src/mania/Game.c` (rip the TS_MENU_ACTIVE branch; menu now runs via
    real entity tick path; remove `mania_menu_*` direct-tick API)
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2l_integration_gate.py` — **the canonical flow
    gate.** Boot ISO; Title -> START; assert MainMenu visible (3.2.l
    gate frame 1); cursor DOWN; assert Save Select visible (gate frame
    2); A on slot 1 (NEW GAME); assert GHZ Act 1 begins loading (gate
    frame 3 = TitleCard splash). Three SSIM checks vs PC refs.
- **Sub-agent budget:** 5 (MenuSetup is huge; integration is multi-file).
- **Dependencies:** 3.2.a + 3.2.b + 3.2.c + 3.2.d + 3.2.e + 3.2.f +
  3.2.g + 3.2.h + 3.2.i + 3.2.j + 3.2.k. **The final integration step.**

### 3.2.m — Pause Menu (in-game)

- **Scope:** In-game pause menu via Global/PauseMenu. Coupled to game-
  state freeze + audio pause. Distinct from the MainMenu surface but
  reuses UIControl/UIButton/UIWidgets.
- **Decomp sources:**
  - `Global_PauseMenu.c` (999 LOC) + `.h`
- **Asset deps:** `UI/PauseEN.gif` (already extracted by Phase 3.0-prep).
  Sound: `Global/MenuBleep.wav`, `Global/MenuAccept.wav` (shared).
- **Saturn-side files:**
  - `src/mania/Objects/Global/PauseMenu.{c,h}`
  - `src/rsdk/scene.c` (ENGINESTATE_FROZEN equivalent — pause the game
    update loop without unloading the scene)
- **Acceptance gate (RED predicate):**
  - `tools/qa_phase3_2m_pause_gate.py` — load GHZ Act 1; simulate START
    after 60 ticks; assert Player.position unchanged after another 60
    ticks (game frozen); assert PauseMenu visible.
- **Sub-agent budget:** 2.
- **Dependencies:** 3.2.a + 3.2.b + 3.2.c + 3.2.l. Also needs GHZ Act 1
  fully loadable (existing Phase 1.32+ already there).

### 3.2.n — Polish, gate roll-up, and Phase 3.2 close-out

- **Scope:** Aggregate all per-sub-task gates into a Phase 3.2 master
  gate in `tools/verify_done.ps1`; refresh PC reference images for
  every menu page; commit `docs/decomp_port_status.md` update; close
  out the Phase 3.2 task in `docs/COMPREHENSIVE_PLAN.md`.
- **Decomp sources:** none (project-management task).
- **Saturn-side files:**
  - `tools/verify_done.ps1` (Phase 3.2 master gate aggregator)
  - `docs/decomp_port_status.md` (running progress table)
  - `tools/refs/mania_pc/menu_*.png` (each page)
- **Acceptance gate (RED predicate):** `pwsh tools/verify_done.ps1`
  exits 0 with the Phase 3.2 master gate green.
- **Sub-agent budget:** 1.
- **Dependencies:** all prior.

---

## Section 3 — Asset extraction needs

**Result: 0 new assets required beyond Phase 3.0-prep.** All 125 unique
Menu assets identified in `docs/menu_scene_asset_audit.md` are already
present in `extracted/Data/Sprites/UI/`, `extracted/Data/SoundFX/`,
`extracted/Data/Music/`. Phase 3.0-prep extracted everything
proactively (Phase 1.33 binding methodology).

Per-policy de-scoping (NTSC-US, multitap kept, Plus deferred):

- **Drop the 8 non-EN language atlases** from build_filelist when the
  port is complete (`UI/TextFR.bin`..`UI/TextTC.bin`,
  `UI/HeadingsFR.bin`..`UI/HeadingsTC.bin`,
  `UI/SaveSelectFR.bin`..`UI/SaveSelectTC.bin`,
  `UI/PauseFR.gif`..`UI/PauseTC.gif`) — saves ~1.6 MB ISO.
- **Keep**: `UI/SaveSelectEN.gif`, `UI/TextEN.{bin,gif}`,
  `UI/HeadingsEN.{bin,gif}`, `UI/PauseEN.gif`.
- **Plus-DLC assets** (Mighty.bin, Ray.bin) **kept extracted but
  unused** until Phase 17 (per project policy — Plus content scoped
  in).

---

## Section 4 — Saturn-specific gotchas to anticipate

| Gotcha | Affects | Mitigation |
|---|---|---|
| Backup-RAM 8 KB cap; SaveGame slot size = 1024 bytes (decomp `noSaveSlot` 0x400) | 3.2.f UISaveSlot | 3 slots * 1024 = 3 KB; fits with margin for Options + Time-Attack records + Competition records. Lay out per Phase 3 backup module (Task #122). |
| 3-button pad has no Y/X buttons; UIChoice / UIButton check `keyY.press`, `keyX.press` | 3.2.e UIChoice, 3.2.c UIButton | `src/rsdk/input.c` already abstracts pad type; map Y/X to held-modifier on 3-button (e.g. Start+Up = Y on 3-button) OR mask widgets that need Y/X to be unselectable on 3-button. Lowest-impact mitigation: hide widgets, gate Plus features behind 6-button detection. **User direction needed if this is unacceptable.** |
| VDP1 sortlist budget: menu pages have many simultaneous sprites (5-7 buttons + per-button label + 27 background entities + cursor + button-prompts) | 3.2.a UIControl Draw rotation; 3.2.c widget Draw cull | Use Phase 2.3e visibility-cull pattern: only draw widgets whose UIControl is the active one (`UIControl_GetUIControl() == self->parent`). Decomp already filters via `active == ACTIVE_ALWAYS` check; mirror this. |
| CRAM palette budget = 2048 colors; Menu palette differs per page (Mania mode = GHZ palette; Options = neutral grey) | 3.2.g UIDiorama; 3.2.l ManiaModeMenu SetBGColors | `src/rsdk/palette.c` already supports per-stage palette swaps (Phase 1.13). Wire MenuSetup_SetBGColors to call into existing palette API. Phase Z dynamic CRAM swap for diorama animation may be needed; **defer to Phase Z**. |
| `ENGINESTATE_FROZEN` (pause) needs to suspend physics-tick callbacks AND scene-render loop, but keep PauseMenu drawing | 3.2.m PauseMenu | Saturn-side: introduce `s_engine_frozen` flag in `src/rsdk/scene.c`; skip Player_Update + Camera_Update + Zone_Update when set, but continue UIControl/UIButton/PauseMenu Update. Mirror decomp's per-class `if (SceneInfo->state == ENGINESTATE_REGULAR)` guard. |
| Menu BGM = 4 OGG tracks (`MainMenu.ogg`, `Competition.ogg`, `Results.ogg`, `SaveSelect.ogg`); Saturn-side audio backend is CD-DA, not OGG | 3.2.l MenuSetup_ChangeMenuTrack | Re-encode the four OGGs to CD-DA tracks (extend build_iso.py track table). Phase 3.0-prep already has the OGG files; need a separate transcoding sub-step. **Add to 3.2.b** (asset bootstrap). |
| Per-stage StageConfig.bin defines palette; Menu palette differs from in-game | 3.2.l MenuSetup integration | `Menu/StageConfig.bin` is already extracted (`extracted/Data/Stages/Menu/StageConfig.bin`). Verify parse via `rsdk_load_scene("Menu")` path picks it up. |
| Mednafen-based QA harness: savestate-peek must be able to walk the UIControl entity list to assert structure (Phase 1.30 harness) | every per-sub-task gate | Each `tools/qa_phase3_2*_gate.py` will use `tools/mcs_extract.py` to walk the entity slot table; entity offsets need updating against new Saturn-side struct layouts. Bake offset constants per sub-task. |
| Music_Stop / Music_PlayTrack — Saturn side already has `jo_audio_play_cd_track` + `jo_audio_stop_cd`; MenuSetup_ChangeMenuTrack maps menu -> trackID -> trackFile string | 3.2.l MenuSetup integration | Wire menu trackID -> CD-DA track-number table at engine init. **Lowest-impact mitigation: hardcode the 4 tracks at known CD positions (e.g. CD-DA 8/9/10/11) rather than dynamic lookup**, since the menu trackID space is fixed at 4. |
| Title -> Menu scene transition: the existing Phase 3.2 MVP `mania_menu_*` direct-tick path lives in `src/mania/Game.c` TS_MENU_ACTIVE branch; this must be ripped in 3.2.l once the real entity-tick path is up | 3.2.l MenuSetup integration | Document in CLAUDE.md §5 ("REPLACE custom with decomp port"). The Phase 3.2 MVP is acknowledged interim; replacement is mechanical (delete TS_MENU_ACTIVE branch, replace with `rsdk_set_scene("Presentation", "Menu") + rsdk_load_scene()`). |

---

## Section 5 — Sequence + dependencies (DAG)

```
                3.2.a UIControl + Background + class registry
                                |
                                v
                3.2.b UIWidgets + asset bootstrap (+ CD-DA transcode)
                                |
                                v
            3.2.c Generic widget shell (Button + Prompt + SubHeading + ...)
                                |
              +------+----------+--------+--------+--------+
              v      v          v        v        v        v
        3.2.d    3.2.e     3.2.f      3.2.g    3.2.h    3.2.k
       Transit  Choice    Save      Diorama   Zone     Options
       +Dialog  +Slider   Slot      +Char     widgets  stubs
              \    |        \        /         /        /
               \   |         \      /         /        /
                \  |          \    /         /        /
                 \ +-----+     \  /         /        /
                  \      \      \/         /        /
                   v      v     v         v        v
                       3.2.i Competition (uses 3.2.h)
                       3.2.j Leaderboards (uses 3.2.h)
                                |
                                v
                          3.2.l INTEGRATION
                  (MenuParam + MenuSetup + MainMenu +
                   ManiaModeMenu + TA + Comp + Options + Extras)
                                |
                                v
                          3.2.m Pause Menu
                                |
                                v
                       3.2.n Polish + close-out
```

**Critical-path (gate the post-button -> GHZ flow):**
3.2.a -> 3.2.b -> 3.2.c -> 3.2.f -> 3.2.l (5 sub-tasks)

Other tracks (3.2.d, 3.2.e, 3.2.g, 3.2.h, 3.2.i, 3.2.j, 3.2.k) **can
parallel** with 3.2.f once 3.2.c lands. Cap parallelism at 3 per
project policy.

---

## Section 6 — Sub-agent budget

| Sub-task | Estimated sub-agents |
|---|---|
| 3.2.a | 3 |
| 3.2.b | 3 |
| 3.2.c | 3 |
| 3.2.d | 2 |
| 3.2.e | 2 |
| 3.2.f | 4 |
| 3.2.g | 3 |
| 3.2.h | 3 |
| 3.2.i | 2 |
| 3.2.j | 2 |
| 3.2.k | 1 |
| 3.2.l | 5 |
| 3.2.m | 2 |
| 3.2.n | 1 |
| **Total** | **36** |

Cap parallel = 3 -> ~12 wall-clock waves to ship. Foundation waves
(3.2.a, 3.2.b, 3.2.c) must serialize (each is the input to the next).
After 3.2.c, the parallel-eligible block (3.2.d / 3.2.e / 3.2.f / 3.2.g
/ 3.2.h / 3.2.i / 3.2.j / 3.2.k) ships in ceil(8 / 3) = 3 waves =
~24-sub-agent budget consumed by then. 3.2.l (5 agents) + 3.2.m (2) +
3.2.n (1) ship serially.

---

## Section 7 — RED-firing gate per sub-task

Recap of each gate (matches §2 individual entries):

| Sub-task | Gate file | Predicate (one-liner) |
|---|---|---|
| 3.2.a | `tools/qa_phase3_2a_uicontrol_gate.py` | SLOT_UICONTROL_MAINMENU entity alive after `rsdk_load_scene("Menu")` |
| 3.2.b | `tools/qa_phase3_2b_uiwidgets_gate.py` | `UIWidgets->textFrames != -1` after StageLoad |
| 3.2.c | `tools/qa_phase3_2c_widgets_gate.py` | SSIM >= 0.40 against `tools/refs/mania_pc/menu_main.png` central 40x40-cell region |
| 3.2.d | `tools/qa_phase3_2d_transition_gate.py` | Tag-change fires UITransition; black pixel at (160,112) during wipe |
| 3.2.e | `tools/qa_phase3_2e_choice_gate.py` | LEFT-press on UIChoice decrements `selection` |
| 3.2.f | `tools/qa_phase3_2f_saveslot_gate.py` | 3 slot panels visible; A on slot N writes save & transitions to GHZ |
| 3.2.g | `tools/qa_phase3_2g_diorama_gate.py` | dioramaID transitions match decomp `MainMenu.c:27-37` case-table |
| 3.2.h | `tools/qa_phase3_2h_zonemodule_gate.py` | LEFT-press cycles `zoneIcon` frame index through 12 zones |
| 3.2.i | `tools/qa_phase3_2i_vscharselect_gate.py` | P1+P2 independent cursors; `p1CharID != p2CharID` reachable |
| 3.2.j | `tools/qa_phase3_2j_leaderboard_gate.py` | UIRankButton visible at Scene1.bin slot 60 |
| 3.2.k | `tools/qa_phase3_2k_medallion_gate.py` | UIMedallionPanel renders 0/32 counter on empty save |
| 3.2.l | `tools/qa_phase3_2l_integration_gate.py` | **Canonical flow: Title -> START -> MainMenu visible; cursor DOWN -> Save Select; A on slot 1 -> GHZ Act 1 TitleCard.** 3 SSIM checks. |
| 3.2.m | `tools/qa_phase3_2m_pause_gate.py` | START in GHZ -> Player.position frozen; PauseMenu visible |
| 3.2.n | `pwsh tools/verify_done.ps1` (master) | Phase 3.2 master gate green |

Each gate MUST fire RED on its prior build (no UIControl, no
UIWidgets, etc.) BEFORE the implementation lands — per binding
`memory/qa-iterative-improvement.md` rule.

---

## Section 8 — Per-sub-task TaskCreate calls (for follow-up dispatcher)

The post-this-iteration dispatcher should TaskCreate each sub-task with
the following structure. **DO NOT TaskCreate any of these now** — this
plan is a planning artifact only.

```text
TaskCreate #3.2.a — Phase 3.2.a UIControl + UIBackground + class registry
  scope: §2.3.2.a above
  decomp sources: Menu_UIControl.{c,h}, Menu_UIBackground.{c,h}
  acceptance: tools/qa_phase3_2a_uicontrol_gate.py exits 0
  sub-agent budget: 3
  depends on: (none — gateway)

TaskCreate #3.2.b — Phase 3.2.b UIWidgets + asset bootstrap
  scope: §2.3.2.b
  decomp sources: Menu_UIWidgets.{c,h}
  acceptance: tools/qa_phase3_2b_uiwidgets_gate.py exits 0
  sub-agent budget: 3
  depends on: 3.2.a

TaskCreate #3.2.c — Phase 3.2.c Generic widget shell (Button family)
  scope: §2.3.2.c
  decomp sources: Menu_UIButton/{Prompt,Label,SubHeading,Heading,Text,
                  InfoLabel,Shifter,Picture,WaitSpinner}.{c,h}
  acceptance: tools/qa_phase3_2c_widgets_gate.py exits 0
  sub-agent budget: 3
  depends on: 3.2.a, 3.2.b

TaskCreate #3.2.d — Phase 3.2.d UITransition + UIDialog + UIPopover
  decomp sources: Menu_UI{Transition,Dialog,Popover}.{c,h}
  acceptance: tools/qa_phase3_2d_transition_gate.py exits 0
  sub-agent budget: 2
  depends on: 3.2.a, 3.2.b, 3.2.c

TaskCreate #3.2.e — Phase 3.2.e UIChoice + UIOptionPanel + UISlider
  decomp sources: Menu_UI{Choice,OptionPanel,Slider}.{c,h}
  acceptance: tools/qa_phase3_2e_choice_gate.py exits 0
  sub-agent budget: 2
  depends on: 3.2.a, 3.2.b, 3.2.c

TaskCreate #3.2.f — Phase 3.2.f UISaveSlot + ManiaModeMenu (save-select)
  decomp sources: Menu_UISaveSlot.{c,h}, Menu_ManiaModeMenu.c (partial),
                  Global/SaveGame.c
  acceptance: tools/qa_phase3_2f_saveslot_gate.py exits 0
  sub-agent budget: 4
  depends on: 3.2.a, 3.2.b, 3.2.c, 3.2.d, Phase 3 SaveGame backup-RAM (Task #122)

TaskCreate #3.2.g — Phase 3.2.g UICharButton + UIDiorama (static-frame)
  decomp sources: Menu_UICharButton.{c,h}, Menu_UIDiorama.c
  acceptance: tools/qa_phase3_2g_diorama_gate.py exits 0
  sub-agent budget: 3
  depends on: 3.2.a, 3.2.b, 3.2.c
  NOTE: lowest-impact mitigation (static frames, animation deferred
        to Phase Z) needs user approval before dispatch.

TaskCreate #3.2.h — Phase 3.2.h UITAZoneModule + UITABanner + UIVsZoneButton + UIVsRoundPicker
  decomp sources: Menu_UI{TAZoneModule,TABanner,VsZoneButton,VsRoundPicker}.{c,h}
  acceptance: tools/qa_phase3_2h_zonemodule_gate.py exits 0
  sub-agent budget: 3
  depends on: 3.2.a, 3.2.b, 3.2.c

TaskCreate #3.2.i — Phase 3.2.i UIVsCharSelector + UIVsResults + UIVsScoreboard
  decomp sources: Menu_UI{VsCharSelector,VsResults,VsScoreboard}.{c,h}
  acceptance: tools/qa_phase3_2i_vscharselect_gate.py exits 0
  sub-agent budget: 2
  depends on: 3.2.a, 3.2.b, 3.2.c, 3.2.h, Phase 2 multitap (Task #110)

TaskCreate #3.2.j — Phase 3.2.j UILeaderboard + UIRankButton + UIReplayCarousel(stub) + UIUsernamePopup(stub)
  decomp sources: Menu_UI{Leaderboard,RankButton,ReplayCarousel,Carousel,UsernamePopup}.{c,h}
  acceptance: tools/qa_phase3_2j_leaderboard_gate.py exits 0
  sub-agent budget: 2
  depends on: 3.2.a, 3.2.b, 3.2.c, 3.2.h

TaskCreate #3.2.k — Phase 3.2.k UIResPicker + UIWinSize + UIKeyBinder + UIMedallionPanel
  decomp sources: Menu_UI{ResPicker,WinSize,KeyBinder,MedallionPanel}.{c,h}
  acceptance: tools/qa_phase3_2k_medallion_gate.py exits 0
  sub-agent budget: 1
  depends on: 3.2.a, 3.2.b, 3.2.c

TaskCreate #3.2.l — Phase 3.2.l MenuParam + MenuSetup + MainMenu + ManiaModeMenu + sub-menu integration
  decomp sources: Menu_{MenuSetup,MenuParam,MainMenu,ManiaModeMenu,
                       TimeAttackMenu,CompetitionMenu,OptionsMenu,ExtrasMenu}.{c,h}
  acceptance: tools/qa_phase3_2l_integration_gate.py exits 0
  sub-agent budget: 5
  depends on: 3.2.a + 3.2.b + 3.2.c + 3.2.d + 3.2.e + 3.2.f + 3.2.g + 3.2.h + 3.2.i + 3.2.j + 3.2.k

TaskCreate #3.2.m — Phase 3.2.m Pause Menu
  decomp sources: Global_PauseMenu.{c,h}
  acceptance: tools/qa_phase3_2m_pause_gate.py exits 0
  sub-agent budget: 2
  depends on: 3.2.a, 3.2.b, 3.2.c, 3.2.l + GHZ Act 1 (already in Phase 1.32+)

TaskCreate #3.2.n — Phase 3.2.n Polish + master gate + close-out
  decomp sources: (none)
  acceptance: pwsh tools/verify_done.ps1 exit 0 with Phase 3.2 master green
  sub-agent budget: 1
  depends on: all prior
```

---

## Section 9 — Honest hand-back: open questions for user direction

Items needing user clarification BEFORE 3.2.* sub-task dispatch:

1. **UIDiorama animation scope.** Decomp UIDiorama is 996 LOC of
   animated GHZ-styled backdrop with character previews per mode. The
   lowest-impact Saturn mitigation is static-still-frame per mode (no
   animation). Phase Z follow-on for Saturn-native animated diorama.
   **Question:** is static-frame acceptable for Phase 3.2.g, or must
   full animation ship in Phase 3.2? Either is feasible; static is
   ~4x lower agent budget.

2. **3-button pad UIChoice / UIButton with Y/X-press dependencies.**
   Decomp widgets read `keyY.press` and `keyX.press`. 3-button pad has
   neither. Two options: (a) gate widgets needing Y/X behind 6-button
   detection (hide them on 3-button), (b) remap Y/X to chord (e.g.
   Start+Up). Per project policy (3-button + 6-button + 3D pad all
   first-class), **question:** which option? Default
   recommendation: (a) hide widgets — lowest user-confusion risk.

3. **Menu BGM CD-DA encoding.** Phase 3.0-prep extracted 4 OGGs
   (MainMenu / Competition / Results / SaveSelect). Saturn audio path
   is CD-DA only. Need to transcode + extend `tools/build_iso.py` track
   table. Proposed: CD-DA tracks 8/9/10/11. **Question:** approve CD-DA
   track-number assignment 8-11? Or leave choice to 3.2.b sub-task?

4. **Phase 3.2 vs Phase 17 (Plus) entanglement.** Decomp MenuSetup.c
   gates ~30% of its code behind `#if MANIA_USE_PLUS`. The same is
   true of MainMenu.c (Plus reorders the 6 buttons) and
   ManiaModeMenu.c (Plus-only entry point). **Project policy lock
   says Plus is in-scope.** Two paths: (a) Phase 3.2 ports the
   non-Plus branch only; Plus folded into Phase 17. (b) Phase 3.2
   ports both branches together. Recommendation: **(a) non-Plus first**
   to ship the critical "Title -> Mania -> Save -> GHZ" flow fastest;
   add Phase 17 sub-tasks for the Plus branches later. **Question:**
   approve (a)?

5. **LevelSelect (debug cheats menu)** scope. `Menu_LevelSelect.c`
   (704 LOC) is the debug zone-jump cheats menu. Not part of the
   normal user flow but cached and referenced by Plus
   `UISubHeading.c:390`. **Question:** include in Phase 3.2 or defer
   to Phase 17 / Phase Z as a "developer extras" feature?

6. **Class-hash identification.** ~36 of the 41 Menu/Scene1.bin entity
   class hashes are currently `<unknown hash>`. They map to the 36
   UI*/Menu*/Mode classes via the `GameConfig.bin` class table. The
   3.2.a foundation task includes this mapping work but it's worth
   confirming the user is OK that Phase 3.2.a spends ~half an
   sub-agent on hash-identification rather than treating it as a
   prerequisite. (No action needed if (a) approved.)

If the user does not direct otherwise, the post-this-iteration
dispatcher should adopt the recommended defaults: **static diorama**,
**hide-on-3-button widgets**, **CD-DA tracks 8-11**, **non-Plus first**,
**LevelSelect deferred**, **hash-id inside 3.2.a**. The dispatcher
should explicitly cite each decision in the per-sub-task TaskCreate
prompt so any sub-agent inherits the binding context.
