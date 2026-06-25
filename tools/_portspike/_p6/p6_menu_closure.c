// =============================================================================
// p6_menu_closure.c -- M1 (engine LoadScene of the MENU scene) CLOSURE BOUNDARY.
//
// The 7 verbatim non-Plus Menu TUs registered by the overlay under
// P6_FRONTEND_MENU (MenuSetup + UIControl/UIBackground/UIButton/UIWidgets/
// UISubHeading/UIButtonPrompt) reference symbols from UI/API/save classes that
// are NOT in that 7-TU set. A trial relocatable link of the 7 (no gc, the exact
// overlay-link surface) measured 111 undefined externals; 43 are PACK-resident
// (resolve via the overlay's -R game.elf import -- engine globals RSDK/SceneInfo/
// ScreenInfo, the wave-1 helpers Music/Localization/Options/DrawHelpers, and the
// closure_edge stubs already covering APICallback/Announcer/FXFade/UIDialog/
// UIWaitSpinner/CompetitionSession/MenuParam). The remaining 68 split:
//   - 8  pack-TU functions gc-DROPPED from the GHZ build (real defs in
//        Game_Localization/MathHelpers/SaveGame/Options.o) -> -u-rooted in the
//        pack build (build_p6scene_objs.sh), NOT stubbed here (would be a
//        multiple-definition).
//   - the rest = genuinely-absent UI*/API*/GameProgress/TimeAttackData/Announcer
//        symbols -> defined here as NULL class pointers + header-signature inert
//        stubs, EXACTLY the p6_closure_edge.c pattern.
//
// WHY THESE STUBS ARE INERT AT THE FRAME-90 CAPTURE (the measurable claim):
//   MenuSetup_StaticUpdate (non-Plus, :88-134) calls MenuSetup_InitAPI first;
//   InitAPI returns false until the (Saturn-stubbed) auth/storage handshake
//   completes, so MenuSetup_Initialize (:242 -- the function that wires the
//   UIControl tree + calls SetupActions/HandleUnlocks, which deref these class
//   pointers) is GATED OUT (`if (!MenuSetup->initializedMenu)`). So at the M1
//   load+run capture every symbol below is LINK-ONLY (referenced, never called).
//   M2 (navigate + start-game) ports UISaveSlot/UIModeButton + the real
//   InitAPI/UIControl tree, deleting the corresponding lines here -- the
//   closure_edge "delete a line == the real TU landed" convention.
//
// Each function stub bumps p6_w_menu_edge_calls so a savestate gate can ASSERT
// the boundary stayed link-only at the capture (a nonzero count names the path
// that must be ported before it runs). Signatures are byte-for-byte the cached
// UI headers (a mismatched generic stub is a compile error against Game.h).
//
// Compiled -x c against the census Game.h tree, only under P6_FRONTEND_MENU
// (build_p6scene_objs.sh / build_shipping.sh), and links into the OVERLAY
// (OVL_FE) alongside the 7 Menu TUs. The default GHZ + Title builds never
// compile it.
// =============================================================================
#include "Game.h"

// ---- witness -----------------------------------------------------------------
__attribute__((used)) int32 p6_w_menu_edge_calls = 0; // total boundary crossings (gate expects 0 @ f90)
__attribute__((used)) int32 p6_w_menu_edge_last  = 0; // ordinal of the last stub hit (1-based, file order)
#define M6_EDGE(n)                                                             \
    do { ++p6_w_menu_edge_calls; p6_w_menu_edge_last = (n); } while (0)

// ---- 1. out-of-set CLASS POINTERS (NULL == unregistered) ---------------------
// These are the Menu UI widget classes NOT registered in the M1 set. The decomp
// NULLs unregistered class pointers (only REGISTERED classes get static vars,
// Scene.cpp:209-242) and every cross-class touch is guarded (`if (X)` / classID
// compares) or lives inside the gated MenuSetup_Initialize. NULL reproduces the
// unregistered-class state exactly. (UIControl/UIBackground/UIButton/UIWidgets/
// UISubHeading/UIButtonPrompt are the REAL registered TUs -> their globals are
// defined by their own .o, NOT here. UIDialog/UIWidgets/FXFade/Announcer/
// APICallback are the closure_edge pack NULLs -> -R import, NOT here.)
ObjectUICharButton     *UICharButton     = NULL;
ObjectUIChoice         *UIChoice         = NULL;
ObjectUIHeading        *UIHeading        = NULL;
ObjectUIInfoLabel      *UIInfoLabel      = NULL;
ObjectUIKeyBinder      *UIKeyBinder      = NULL;
ObjectUILeaderboard    *UILeaderboard    = NULL;
// M1b: UIModeButton is now a REGISTERED, overlay-resident object (its verbatim
// Game_UIModeButton.o links into the overlay) -- its global is provided by that .o,
// no longer a NULL stub here. It carries the 4 MAIN-MENU rows (Mania Mode/Time
// Attack/Competition/Options); foreach_all(UIModeButton,...) at MenuSetup_SetupActions
// :507 wires their actionCB, and UIModeButton_Draw blits each row's icon+text+plates.
ObjectUIResPicker      *UIResPicker      = NULL;
ObjectUISaveSlot       *UISaveSlot       = NULL; // M2: the save-select start-game slot
ObjectUISlider         *UISlider         = NULL;
ObjectUITAZoneModule   *UITAZoneModule   = NULL;
ObjectUIVsCharSelector *UIVsCharSelector = NULL;
ObjectUIVsResults      *UIVsResults      = NULL;
ObjectUIVsRoundPicker  *UIVsRoundPicker  = NULL;
ObjectUIVsScoreboard   *UIVsScoreboard   = NULL;
ObjectUIVsZoneButton   *UIVsZoneButton   = NULL;
ObjectUIWinSize        *UIWinSize        = NULL;

// ---- 1b. M1b AUTH-GATE FLIP --------------------------------------------------
// M1a left the menu BLACK because MenuSetup_StaticUpdate (non-Plus, MenuSetup.c:
// 88-134) gates MenuSetup_Initialize (the fn that wires the UIControl row tree)
// behind MenuSetup_InitAPI()==true, and InitAPI (:412-489) returned false: the
// storage stub below returned 0 (so :425 if(!storageStatus) looped on TryInitStorage)
// and the pack `APICallback` was NULL (p6_closure_edge.c:60, so :419 read garbage).
// The DECOMP-FAITHFUL Saturn state is "offline, no online save" -- the same terminal
// state the real APICallback.c state machine converges to (GetStorageStatus:430
// NULL-fn path -> STATUS_OK; API_GetNoSave=globals->noSave).
//
// p6_menu_apic_init() (below) installs a real zeroed ObjectAPICallback with
// authStatus=STATUS_OK + sets globals->noSave=true. It lives HERE (the overlay has
// the Mania Game.h ObjectAPICallback type, which the pack p6_io_main.cpp -- compiled
// against only the engine C++ headers -- does not). It is exported to the pack via
// the api forward pointer (p6_overlay_entry sets api->menu_apic_init_fn =
// p6_menu_apic_init) and called ONCE at the top of the first p6_frontend_frame, AFTER
// the overlay is loaded (the struct + globals writes are valid) and BEFORE the scene's
// first StaticUpdate runs InitAPI. `APICallback`/`globals` are PACK symbols written
// here via the overlay's -R import. Re-traced (MenuSetup.c:412-489) with the post-init
// state:
//   :417 GetUserAuthStatus() (pack closure_edge stub, ret 0, does NOT write authStatus)
//   :419 if (!APICallback->authStatus) -> !STATUS_OK(200) == false (skip TryAuth)
//   :423 storageStatus = APICallback_GetStorageStatus() -> STATUS_OK(200) (below)
//   :425 if (!storageStatus) -> false; :428 200!=CONTINUE -> true
//   :429 if (!API_GetNoSave() && ...) -> !true==false -> SKIP the return-false block
//   :443 initializedSaves block (UIWaitSpinner/Options_LoadFile/SaveGame_LoadFile
//        stubs, all inert) -> initializedSaves=true
//   :454 globals->optionsLoaded/saveLoaded == 0 (stubs never set them) -> false
//   :467 if (API_GetNoSave()) -> true -> UIWaitSpinner_FinishWait(); return TRUE.
// authStatus is the ONLY struct field InitAPI reads on this path; the rest match the
// decomp's post-converge no-save values. APICallback now points at a REAL struct so
// :415/:419 derefs are valid (no garbage low-memory read). The storage-status function
// below is self-contained (no pack symbols) and returns the decomp's terminal value.
static ObjectAPICallback s_menu_apicallback; /* overlay .bss, zero-initialised */

void p6_menu_apic_init(void)
{
    memset(&s_menu_apicallback, 0, sizeof(s_menu_apicallback));
    s_menu_apicallback.authStatus    = STATUS_OK; /* InitAPI:419 reads this field */
    s_menu_apicallback.storageStatus = STATUS_OK; /* decomp post-converge (GetStorageStatus) */
    s_menu_apicallback.saveStatus    = STATUS_OK; /* decomp post-converge (GetStorageStatus:465) */
    s_menu_apicallback.active        = ACTIVE_ALWAYS;
    APICallback     = &s_menu_apicallback; /* pack NULL -> real struct (writable via -R) */
    globals->noSave = true;                /* API_GetNoSave() (MenuSetup.c:467) -> no-save path */
}

// ---- 2. out-of-set FUNCTIONS (witnessed inert stubs, header signatures) ------
// APICallback auth/storage/leaderboard surface (the removed-at-REV02
// RSDK.GetAPIFunction members p6_closure_edge.c did NOT already cover). All inert
// on Saturn -- the real impl is the desktop store/online handshake.
bool32 APICallback_CheckUnreadNotifs(void) { M6_EDGE(1); return false; }
void   APICallback_ExitGame(void) { M6_EDGE(2); }
int32  APICallback_GetControllerType(int32 id) { (void)id; M6_EDGE(3); return 0; }
// M1b: return the decomp's terminal storage status for the Saturn offline no-save
// case (the real GetStorageStatus:430 NULL-fn path converges to STATUS_OK). This is
// the load-bearing change that lets MenuSetup_InitAPI (:423-428) advance past the
// storage handshake to the API_GetNoSave() return-true (:467). NOT a witnessed inert
// stub anymore -- it returns a real value, like the decomp.
int32  APICallback_GetStorageStatus(void) { return STATUS_OK; }
void   APICallback_LaunchManual(void) { M6_EDGE(5); }
int32  APICallback_LeaderboardStatus(void) { M6_EDGE(6); return 0; }
bool32 APICallback_NotifyAutosave(void) { M6_EDGE(7); return false; }
void   APICallback_PromptSavePreference(int32 status) { (void)status; M6_EDGE(8); }
// ordinal 9 (APICallback_ResetControllerAssignments) is ALREADY defined by the
// GHZ overlay closure (p6_ovl_ghz.c:145, empty stub) -- both .o link into
// cd/OVLRING.BIN, so defining it here too is a multiple-definition (MEASURED ld
// error). Defer to that one (same intent: inert no-op); ordinal 9 retired here.
int32  APICallback_TryAuth(void) { M6_EDGE(10); return 0; }
void   APICallback_TryInitStorage(void) { M6_EDGE(11); }

// Announcer (competition winner SFX states) -- reached only from the competition
// Round/Total MenuSetupCB, which run inside MenuSetup_Initialize/SetupActions.
void Announcer_State_AnnounceWinPlayer(void) { M6_EDGE(12); }
void Announcer_State_AnnounceWinner(void)    { M6_EDGE(13); }

// GameProgress (unlock predicates) -- reached only via MenuSetup_HandleUnlocks
// (inside the gated Initialize). Return "unlocked" so a future ported menu shows
// every mode; inert at the M1 capture. Helpers_GameProgress.c exists but is not a
// pack TU here, so stub rather than drag its closure.
bool32 GameProgress_CheckUnlock(uint8 id) { (void)id; M6_EDGE(14); return true; }
bool32 GameProgress_GetZoneUnlocked(int32 zoneID) { (void)zoneID; M6_EDGE(15); return true; }

// TimeAttackData -- NOT present in the cached decomp corpus (a save-table helper
// not on the GHZ/front-end path). Clear is a no-op; GetManiaListPos returns 0
// (listPos offset; only used by the start-game/TA paths = M2). Inert at M1.
// ordinal 16 (TimeAttackData_Clear) is ALREADY defined by the GHZ overlay
// closure (p6_ovl_ghz.c:138, empty stub); both .o link into cd/OVLRING.BIN, so a
// second definition here is a multiple-definition (MEASURED ld error). Defer to
// that one (same intent: no-op clear); ordinal 16 retired here.
int32 TimeAttackData_GetManiaListPos(int32 zoneID, int32 act, int32 characterID)
{
    (void)zoneID; (void)act; (void)characterID; M6_EDGE(17); return 0;
}

// UIChoice / UIResPicker / UIVsRoundPicker / UIWinSize choice-widget state +
// setters -- all reached only from SetupActions/MenuSetupCB choice wiring (gated
// inside Initialize). Header signatures.
void UIChoice_SetChoiceActive(EntityUIChoice *choice) { (void)choice; M6_EDGE(18); }
void UIChoice_SetChoiceInactive(EntityUIChoice *choice) { (void)choice; M6_EDGE(19); }
void UIChoice_State_HandleButtonLeave(void) { M6_EDGE(20); }
void UIChoice_Update(void) { M6_EDGE(21); }
void UIResPicker_GetDisplayInfo(EntityUIResPicker *entity) { (void)entity; M6_EDGE(22); }
void UIResPicker_SetChoiceActive(EntityUIResPicker *entity) { (void)entity; M6_EDGE(23); }
void UIResPicker_SetChoiceInactive(EntityUIResPicker *entity) { (void)entity; M6_EDGE(24); }
void UIVsRoundPicker_SetChoiceActive(EntityUIVsRoundPicker *entity) { (void)entity; M6_EDGE(25); }
void UIVsRoundPicker_SetChoiceInactive(EntityUIVsRoundPicker *entity) { (void)entity; M6_EDGE(26); }
void UIWinSize_SetChoiceActive(EntityUIWinSize *entity) { (void)entity; M6_EDGE(27); }
void UIWinSize_SetChoiceInactive(EntityUIWinSize *entity) { (void)entity; M6_EDGE(28); }

// UIDialog text setter (the other UIDialog_* are closure_edge stubs).
void UIDialog_SetupText(EntityUIDialog *dialog, String *text) { (void)dialog; (void)text; M6_EDGE(29); }

// UIHeading / UIInfoLabel / UILeaderboard / UIModeButton -- heading + label +
// leaderboard + mode-button helpers reached inside Initialize/SetupActions or the
// competition CBs. Header signatures.
void UIHeading_LoadSprites(void) { M6_EDGE(30); }
void UIHeading_Update(void) { M6_EDGE(31); }
void UIInfoLabel_SetString(EntityUIInfoLabel *self, String *text) { (void)self; (void)text; M6_EDGE(32); }
void UIInfoLabel_SetText(EntityUIInfoLabel *label, char *text) { (void)label; (void)text; M6_EDGE(33); }
void UILeaderboard_InitLeaderboard(EntityUILeaderboard *leaderboard) { (void)leaderboard; M6_EDGE(34); }
// M1b: UIModeButton_SetupSprites is now provided by the REAL Game_UIModeButton.o
// (UIModeButton is registered) -- defining it here too is a multiple-definition
// (MEASURED ld error). Retired (the real one builds each row's icon/text animators).

// UITAZoneModule (time-attack zone tiles) -- reached only from the TA paths
// (Initialize-gated). Header signatures.
void UITAZoneModule_SetStartupModule(EntityUIControl *control, uint8 characterID, uint32 zoneID, uint8 actID, int32 score)
{
    (void)control; (void)characterID; (void)zoneID; (void)actID; (void)score; M6_EDGE(36);
}
void UITAZoneModule_ShowLeaderboards(int32 player, int32 zone, int32 act, bool32 wasUser, void (*callback)(void))
{
    (void)player; (void)zone; (void)act; (void)wasUser; (void)callback; M6_EDGE(37);
}
void UITAZoneModule_Update(void) { M6_EDGE(38); }

// UITransition / UIUsernamePopup / UIVsCharSelector / UIVsScoreboard / UIVsZoneButton.
void UITransition_StartTransition(void (*callback)(void), int32 delay) { (void)callback; (void)delay; M6_EDGE(39); }
void UIUsernamePopup_ShowPopup(void) { M6_EDGE(40); }
void UIVsCharSelector_ProcessButtonCB(void) { M6_EDGE(41); }
void UIVsScoreboard_SetScores(EntityUIVsScoreboard *scoreboard, uint32 p1Score, uint32 p2Score)
{
    (void)scoreboard; (void)p1Score; (void)p2Score; M6_EDGE(42);
}
void UIVsZoneButton_Update(void) { M6_EDGE(43); }

// ---- 3. M2-scope save/options/localization (STUBBED, not -u-rooted) ----------
// These are pack TUs (Game_SaveGame/Options/Localization.o) but their REAL bodies
// drag the M2/unported UISaveSlot + GameProgress closure:
// SaveGame_SaveLoadedCB (decomp SaveGame.c:228-250) does
// foreach_all(UISaveSlot){ UISaveSlot_LoadSaveInfo(); UISaveSlot_HandleSaveIcons(); }
// + GameProgress_ShuffleBSSID/PrintSaveProgress -- none of which is ported for M1
// (UISaveSlot is the M2 start-game widget; -u-rooting the real SaveGame functions
// dragged UISaveSlot_HandleSaveIcons UNDEFINED into the final jo link, MEASURED).
// All three SaveGame/Options/Localization functions below are called ONLY from
// MenuSetup (the InitAPI save-load branch + the VS round label), which is GATED OUT
// of the M1 frame-90 capture (InitAPI returns false pre-handshake so Initialize never
// runs). So stub them here, inert, exactly like the UI stubs above -- M2 deletes
// these and -u-roots / ports the real save+UISaveSlot subsystem. (SaveGame_GetDataPtr/
// SaveGame_SaveFile/SaveGame_ClearCollectedSpecialRings + Localization_GetString/
// LoadStrings + Options_GetOptionsRAM/GetWinSize are pack-resident in the GHZ map ->
// they resolve via -R, NOT stubbed here. MathHelpers_PointInHitbox is RUNTIME-used by
// UIControl -> -u-rooted to the REAL Game_MathHelpers.o, NOT stubbed.)
void SaveGame_LoadFile(void (*callback)(bool32 success)) { (void)callback; M6_EDGE(44); }
void SaveGame_ResetPlayerState(void) { M6_EDGE(45); }
void SaveGame_SaveLoadedCB(bool32 success) { (void)success; M6_EDGE(46); }
void Options_LoadFile(void (*callback)(bool32 success)) { (void)callback; M6_EDGE(47); }
// non-Plus signature (census Helpers/Options.h:79 -- MANIA_USE_PLUS=0 arm: the
// callback is void(void), NOT void(bool32) as the Plus arm + the cached decomp
// header declare). Matching the ACTIVE census prototype, else "conflicting types".
void Options_SaveFile(void (*callback)(void)) { (void)callback; M6_EDGE(48); }
void Options_LoadCallback(bool32 success) { (void)success; M6_EDGE(49); }
void Localization_GetZoneName(String *string, uint8 zone) { (void)string; (void)zone; M6_EDGE(50); }
