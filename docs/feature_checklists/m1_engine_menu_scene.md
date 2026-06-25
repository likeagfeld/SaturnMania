# M1 — engine LoadScene of the MENU scene (front-end link 3)

Mirror of the PROVEN CP4 (Logos) / CP5a (Title) front-end ports. Boot the
engine directly into category "Presentation" folder "Menu", register the
verbatim non-Plus MenuSetup + the minimum UI set, and make the RED gate
`tools/_portspike/qa_engine_menu.py` go GREEN (M1-M5), then SCREENSHOT.

Build is NON-Plus: `GAME_DEFS -DGAME_VERSION=3` (< VER_105=5) so
`MANIA_USE_PLUS = (GAME_VERSION>=VER_105) = 0` (census Game.h:83,
build_p6scene_objs.sh:253). MenuSetup is the live director; MainMenu /
ManiaModeMenu are `#if MANIA_USE_PLUS` = inert.

## Authoritative decomp + line ranges

- `tools/_decomp_raw/SonicMania_Objects_Menu_MenuSetup.c` (2301 L). Non-Plus
  branches ONLY:
  - `MenuSetup_Update`           :12-33   (StateMachine_Run state/callback)
  - `MenuSetup_StaticUpdate`     :88-134  (the non-Plus director: InitAPI ->
                                            Initialize -> HandleMenuReturn ->
                                            SetBGColors -> ChangeMenuTrack)
  - `MenuSetup_Draw`             :137-142 (FillScreen fade)
  - `MenuSetup_Create`           :144-151 (active/visible/drawGroup=14)
  - `MenuSetup_StageLoad`        :169-208 (non-Plus: Music_Stop + sku logs +
                                            memset noSaveSlot + SetVideoSetting
                                            + foreach FXFade)
  - `MenuSetup_Initialize`       :242-410 (wire ~25 UIControl tags +
                                            HandleUnlocks + SetupActions)
  - `MenuSetup_InitAPI`          :412-489 (derefs APICallback->authStatus)
  - `MenuSetup_SetupActions`     :491-672 (touches ~25 UI classes)
  - `MenuSetup_HandleUnlocks`    :674-741 (GameProgress + UIButton derefs)
  - `MenuSetup_SaveSlot_ActionCB`:1046-1132 (the StartGame -> M2 scope, DEFER)
- UI core (placed in Menu Scene1.bin, must register + instantiate):
  - `Menu_UIControl.c`     (876 L) -- Create:56-110 non-Plus is clean
  - `Menu_UIBackground.c`  (91 L)  -- Create/StageLoad/DrawNormal clean
  - `Menu_UIButton.c`      (967 L)
  - `Menu_UIWidgets.c`     (353 L)
  - `Menu_UISubHeading.c`  (450 L)
  - `Menu_UIButtonPrompt.c`(566 L)

## Menu Scene1.bin / StageConfig class list (40 classes, authoritative)

From `tools/dump_all_scenes.py parse_stage_config` on
`extracted/Data/Stages/Menu/StageConfig.bin`:

APICallback, Options, SaveGame, Localization, UIControl, UIBackground,
UIWidgets, UIHeading, UISubHeading, UIPicture, UIButton, UIModeButton,
UISaveSlot, UIButtonPrompt, FXFade, Music, MenuSetup, UIChoice, UICharButton,
UITAZoneModule, UIDialog, UILeaderboard, UIVsCharSelector, UIVsRoundPicker,
UIVsZoneButton, UIVsResults, UIInfoLabel, UIVsScoreboard, UIOptionPanel,
UISlider, UIButtonLabel, UIKeyBinder, UIWaitSpinner, GameProgress,
UIMedallionPanel, Announcer, UITransition, UIUsernamePopup, UIResPicker,
UIWinSize.

## CLOSURE CONE FINDING (the central risk, MEASURED)

The verbatim non-Plus `MenuSetup_StaticUpdate` runs EVERY frame from frame 0
(unlike TitleSetup's State_WaitForEnter which is only reached on a button
press ~256 frames in, so CP5a could link-only-stub its closure). Its path:

  StaticUpdate(:88) -> MenuSetup_InitAPI(:103)
    -> APICallback_GetUserAuthStatus()  [closure_edge stub returns 0]
    -> `if (!APICallback->authStatus)`  [APICallback is NULL -> WILD DEREF]

So MenuSetup is the project's hardest front-end scene: its director is
entangled with the APICallback/SaveGame/Options/GameProgress save+auth
subsystem AND, past InitAPI, with the full UIControl widget tree
(Initialize/SetupActions/HandleUnlocks deref ~25 UI-class globals that are
NULL unless every class is registered + placed + Create'd).

## Gate witness dependency chain (qa_engine_menu.py, capture @ frame 90)

- M1 folder_tag == 0x4D65 ('Me'): set in p6_menu_reload (boot-select "Menu").
- M2 menusetup_classid > 0: registration resolves classID -> trivially GREEN
  once MenuSetup is registered (independent of entities/crashes; written by
  the overlay witness fn each tick).
- M3 uicontrol_classid > 0: same, once UIControl registered.
- M4 menu_objcount > 0: latched post-InitObjects -- requires InitObjects to
  RUN every registered class's StageLoad + instantiate the placements WITHOUT
  FAULTING.
- M5 cont_frames > 0: requires p6_frontend_frame to tick without faulting ->
  MenuSetup_StaticUpdate (+ UIControl_Update etc.) must not crash.

## PLAN (this build, additive-only behind P6_FRONTEND_MENU)

P6_FRONTEND_MENU IMPLIES P6_FRONTEND_TITLE (=> LOGOS), reusing ALL shared
front-end machinery (frontend_frame, VDP1 box, arm-sprites, SaturnSheet slots,
witnesses). Boot dispatch: a `p6_menu_reload` mirror of p6_title_reload that
scans for folder "Menu" + tags 0x4D65, then p6_scene_load_and_arm.

1. p6_io_main.cpp: add `_p6_w_menusetup_classid`/`_p6_w_uicontrol_classid`/
   `_p6_w_menu_objcount` witnesses (under P6_FRONTEND_TITLE guard like the
   Title ones). Add `p6_menu_reload`. Add the T4-style objcount latch on the
   "Menu" folder. Add the `#elif defined(P6_FRONTEND_MENU)` boot branch.
2. p6_ovl_ghz.c: under P6_FRONTEND_MENU, register MenuSetup + UIControl +
   UIBackground + UIButton + UIWidgets + UISubHeading + UIButtonPrompt via
   register_object_full; add the classID latch in the witness fn.
3. build_p6scene_objs.sh: under P6_FRONTEND_MENU compile Game_MenuSetup.o +
   the 6 UI Game_*.o; -u-root the 3 new witnesses. Self-imply TITLE/LOGOS.
4. build_shipping.sh: add the Menu Game_*.o to OVL_FE; add P6_FRONTEND_MENU
   make/grep plumbing; self-imply TITLE.
5. CLOSURE: resolve MenuSetup's link cone. Most class pointers + UI*/API stubs
   already exist in p6_closure_edge.c (APICallback, UIControl, UIButton,
   UIDialog, UIWidgets, FXFade, Announcer, MenuParam_GetParam,
   CompetitionSession_GetSession, UIWaitSpinner_*, UIDialog_*, UIWidgets_*).
   The remaining undefined externs of the 7 verbatim Menu TUs get -u-rooted
   and/or stubbed in a new p6_menu_closure.c compiled under P6_FRONTEND_MENU,
   following the closure_edge pattern (NULL class ptrs + header-signature
   stubs). DO NOT port the save/auth subsystem (M2 scope).

## ACCEPTANCE (measurable)

- RED baseline: `python tools/_portspike/qa_engine_menu.py --static` shows 3/5
  witnesses absent (CONFIRMED).
- M1a (load+run): M1-M5 GREEN in `python tools/_portspike/qa_engine_menu.py`.
- M1b (render): SCREENSHOT shows UIBackground + UIControl rows. If load+run is
  GREEN but render incomplete, report honestly as M1a-only with the leading
  cause.
- Non-regression: default GHZ build byte-identical (only flag-guarded code
  touched); `_end < ANIMPAK` re-measured from the Menu-flavor build log.

## AUDITS (per CLAUDE.md 4.5.1)

- Audit 1 (Z-order): not applicable to M1 load+run; the UI drawGroups are
  decomp-native (UIBackground=0, MenuSetup=14) -- no Saturn Z remap needed.
- Audit 2 (anim cadence): no Saturn walker added; ProcessAnimation is engine.
- Audit 3 (pivot+flip): no flipped composite added in M1.
- Audit 4 (boot-delay): the Menu scene is sprite-only/UI (Scene1.bin 14,813 B,
  no tilemap/collision arm); load budget << the title's. Re-measure load step
  in the build log.
