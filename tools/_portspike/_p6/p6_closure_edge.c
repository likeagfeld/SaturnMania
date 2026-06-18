// =============================================================================
// p6_closure_edge.c -- P6.7 Player wave (Task #227): the CLOSURE BOUNDARY.
//
// The 21-TU verbatim game set (wave-1 trio + APICallback/DrawHelpers/
// MathHelpers/Soundboard + the 17-TU Player depth-2 closure) references
// symbols from classes OUTSIDE the set. Two kinds, both inert by decomp
// construction until their owning wave lands:
//
//   1. CLASS POINTERS (`ObjectX *X`): the decomp NULLs unregistered class
//      pointers (only REGISTERED classes get static vars, Scene.cpp:209-242)
//      and every cross-class touch is guarded (`if (X) ...` / classID
//      compares). Defining them NULL here reproduces exactly the
//      unregistered-class state. Signatures mirror the All.h externs.
//
//   2. CROSS-CLASS FUNCTIONS (states/helpers of out-of-set classes): these
//      only run from gameplay paths that themselves require the owning
//      class's entities to exist. Each stub counts into p6_w_edge_calls so
//      a savestate gate can ASSERT the boundary was never crossed -- a
//      nonzero count names the wave that must land before that path runs.
//      Signatures are byte-for-byte the header prototypes (Game.h already
//      declares them; a mismatched generic stub is a compile error).
//
// KNOWN SEAM (B-step, task #227 metadata): `Ring` here is the PACK's NULL
// pointer while the registered Ring class lives in the OVERLAY (P6.7d.3)
// with its own static pointer. Pack-side Player code that checks `Ring`
// sees NULL even though Ring is registered -- correct for the
// registration-only step; the Ring TU moves into the pack (it is a GLOBAL
// class) when gameplay paths need it.
//
// Replaced wave by wave; deleting a line here is the signal that the real
// TU joined the pack.
// =============================================================================
#include "Game.h"

// ---- witness -----------------------------------------------------------------
int32 p6_w_edge_calls = 0; // total boundary crossings (gate expects 0)
int32 p6_w_edge_last  = 0; // ordinal of the last stub hit (1-based, file order)

#define P6_EDGE(n)                                                             \
    do {                                                                       \
        ++p6_w_edge_calls;                                                     \
        p6_w_edge_last = (n);                                                  \
    } while (0)

// ---- 1. out-of-set CLASS POINTERS (NULL == unregistered) ---------------------
// APICallback: the verbatim TU is built around pre-REV02's
// RSDK.GetAPIFunction (removed at REV02 -- the API surface moved to the
// split APIFunctionTable). On the 1.03-on-v5U build its 9 referenced
// entry points stub here instead; the SaveGame/UserStorage wave brings the
// REV02-canonical replacement (engine User.cpp surface).
ObjectAPICallback *APICallback   = NULL;
// ActClear: now a REGISTERED pack object (Game_ActClear.o defines the global) --
// F.2 act-clear: ActClear.c:782 ++listPos is the GHZ1->GHZ2 advance. Its closure
// edge (Animals/TimeAttackData/StarPost/GameProgress/APICallback_Track*) stubs
// below -- all leaderboard/save/progress, none on the listPos-advance path.
ObjectAnimals *Animals           = NULL;
ObjectAnnouncer *Announcer       = NULL;
// #181: Bridge (now a registered pack object) spawns BurningLog from Bridge_Burn,
// which is reachable ONLY via SHIELD_FIRE on a burnable bridge. SHIELD_FIRE is
// unobtainable until ItemBox is ported (also NULL below) -> Bridge_Burn is dead
// code in this build, so the NULL is safe. Port BurningLog with its own gate
// (GHZ/Fireball.bin residency) when ItemBox + the fire shield land.
ObjectBurningLog *BurningLog     = NULL;
ObjectCompetition *Competition   = NULL; // F.3: SignPost competition branch (DEAD in Mania mode)
ObjectDecoration *Decoration     = NULL; // F.4: GHZSetup editor-only ref
ObjectGHZ2Outro *GHZ2Outro       = NULL; // F.4: GHZSetup_StageFinish_EndAct2 (Act2-only) ref
ObjectCPZSetup *CPZSetup         = NULL;
ObjectChaosEmerald *ChaosEmerald = NULL;
// #W18 ring-arm: Ring_CheckObjectCollisions (Ring.c:430) foreach_active's these
// platform-class objects -- reached ONLY from the Track/Big/Path ring states; GHZ1
// has none (all rings are RING_MOVE_FIXED -> Ring_State_Normal -> Ring_Collect only),
// so the path is GHZ1-DEAD. NULL == unregistered (matches Spikes/etc. below).
ObjectPlatform *Platform         = NULL;
ObjectCrate *Crate               = NULL;
ObjectIce *Ice                   = NULL;
ObjectBigSqueeze *BigSqueeze     = NULL;
ObjectSpikeCorridor *SpikeCorridor = NULL;
ObjectDebris *Debris             = NULL;
ObjectERZOutro *ERZOutro         = NULL;
ObjectFXFade *FXFade             = NULL;
ObjectFXRuby *FXRuby             = NULL;
ObjectFarPlane *FarPlane         = NULL;
ObjectHCZSetup *HCZSetup         = NULL;
ObjectIceSpring *IceSpring       = NULL;
ObjectItemBox *ItemBox           = NULL;
ObjectKleptoMobile *KleptoMobile = NULL;
ObjectLottoMachine *LottoMachine = NULL;
ObjectOOZSetup *OOZSetup         = NULL;
ObjectPhantomEgg *PhantomEgg     = NULL;
ObjectPhantomKing *PhantomKing   = NULL;
ObjectPhantomRuby *PhantomRuby   = NULL;
ObjectRing *Ring                 = NULL; // SEE KNOWN SEAM ABOVE
ObjectRingField *RingField       = NULL;
ObjectSpikes *Spikes             = NULL;
ObjectSpring *Spring             = NULL;
ObjectStarPost *StarPost         = NULL;
ObjectSuperSparkle *SuperSparkle = NULL;
ObjectTitleCard *TitleCard       = NULL;
ObjectUIButton *UIButton         = NULL;
ObjectUIControl *UIControl       = NULL;
ObjectUIDialog *UIDialog         = NULL;
ObjectUIWidgets *UIWidgets       = NULL;
ObjectWater *Water               = NULL;
// F.3: GameProgress's achievement table. SignPost reads &achievementList[ACH_
// SIGNPOST]=idx 5; sized to 6 (idx 0-5) to stay under the WRAM-H floor.
// The unlock API is a no-op on Saturn, so the .id field value is irrelevant.
AchievementID achievementList[6];

// ---- 2. out-of-set FUNCTIONS (witnessed inert stubs, header signatures) ------
void APICallback_AssignControllerID(uint8 inputSlot, int32 deviceID)
{
    (void)inputSlot; (void)deviceID;
    P6_EDGE(44);
}
int32 APICallback_ControllerIDForInputID(uint8 inputSlot)
{
    (void)inputSlot;
    P6_EDGE(45);
    return 0;
}
bool32 APICallback_GetConfirmButtonFlip(void)
{
    P6_EDGE(46);
    return false;
}
int32 APICallback_GetUserAuthStatus(void)
{
    P6_EDGE(47);
    return 0;
}
bool32 APICallback_InputIDIsDisconnected(uint8 inputSlot)
{
    (void)inputSlot;
    P6_EDGE(48);
    return false;
}
int32 APICallback_MostRecentActiveControllerID(uint8 inputSlot)
{
    (void)inputSlot;
    P6_EDGE(49);
    return 0;
}
void APICallback_SaveUserFile(const char *name, void *buffer, int32 size, void (*callback)(int32))
{
    (void)name; (void)buffer; (void)size; (void)callback;
    P6_EDGE(50);
}
void APICallback_SetRichPresence(int32 id, String *msg)
{
    (void)id; (void)msg;
    P6_EDGE(51);
}
// F.2 ActClear closure edge (leaderboard/save/progress -- inert no-ops; none on
// the ActClear.c:782 ++listPos act-advance path).
uint16 *TimeAttackData_GetRecordedTime(uint8 z, uint8 a, uint8 c, uint8 r)
{
    (void)z; (void)a; (void)c; (void)r; P6_EDGE(52); return 0;
}
void TimeAttackData_AddRecord(uint8 z, uint8 a, uint8 c, uint8 rank, uint16 score)
{
    (void)z; (void)a; (void)c; (void)rank; (void)score; P6_EDGE(53);
}
uint32 TimeAttackData_GetPackedTime(int32 m, int32 s, int32 ms)
{
    (void)m; (void)s; (void)ms; P6_EDGE(54); return 0;
}
void StarPost_ResetStarPosts(void) { P6_EDGE(55); }
void GameProgress_MarkZoneCompleted(int32 zoneID) { (void)zoneID; P6_EDGE(56); }
void APICallback_TrackTAClear(uint8 z, uint8 a, uint8 p, int32 t)
{
    (void)z; (void)a; (void)p; (void)t; P6_EDGE(57);
}
void APICallback_TrackActClear(uint8 z, uint8 a, uint8 p, int32 t, int32 ri, int32 sc)
{
    (void)z; (void)a; (void)p; (void)t; (void)ri; (void)sc; P6_EDGE(58);
}
void ChaosEmerald_State_Rotate(void) { P6_EDGE(1); }
void CompetitionSession_DeriveWinner(int32 playerID, int32 finishType)
{
    (void)playerID; (void)finishType;
    P6_EDGE(2);
}
EntityCompetitionSession *CompetitionSession_GetSession(void)
{
    P6_EDGE(3);
    return NULL;
}
void CompetitionSession_ResetOptions(void) { P6_EDGE(4); }
void CutsceneRules_DrawCutsceneBounds(void *e, Vector2 *size)
{
    (void)e; (void)size;
    P6_EDGE(5);
}
void CutsceneRules_SetupEntity(void *e, Vector2 *size, Hitbox *hitbox)
{
    (void)e; (void)size; (void)hitbox;
    P6_EDGE(6);
}
void CutsceneSeq_LockAllPlayerControl(void) { P6_EDGE(7); }
void CutsceneSeq_StartSequence(void *manager, ...)
{
    (void)manager;
    P6_EDGE(8);
}
void Debris_State_Move(void) { P6_EDGE(9); }
void FXRuby_State_Shrinking(void) { P6_EDGE(10); }
void ItemBox_Break(EntityItemBox *itemBox, EntityPlayer *player)
{
    (void)itemBox; (void)player;
    P6_EDGE(11);
}
void ItemBox_State_Broken(void) { P6_EDGE(12); }
void ItemBox_State_Falling(void) { P6_EDGE(13); }
void ItemBox_State_Idle(void) { P6_EDGE(14); }
void KleptoMobile_StateArm_Cutscene(void) { P6_EDGE(15); }
void KleptoMobile_StateArm_Idle(void) { P6_EDGE(16); }
void KleptoMobile_StateHand_Boss(void) { P6_EDGE(17); }
void KleptoMobile_StateHand_Cutscene(void) { P6_EDGE(18); }
void KleptoMobile_State_CutsceneControlled(void) { P6_EDGE(19); }
EntityMenuParam *MenuParam_GetParam(void)
{
    P6_EDGE(20);
    return NULL;
}
void PhantomKing_SetupKing(EntityPhantomKing *king)
{
    (void)king;
    P6_EDGE(21);
}
void PhantomKing_StateArm_Idle(void) { P6_EDGE(22); }
void PhantomKing_StateArm_WrestleEggman(void) { P6_EDGE(23); }
void PhantomKing_State_SetupArms(void) { P6_EDGE(24); }
void PhantomKing_State_TakeRubyAway(void) { P6_EDGE(25); }
void PhantomKing_State_WrestleEggman(void) { P6_EDGE(26); }
void PhantomRuby_PlaySfx(uint8 sfxID)
{
    (void)sfxID;
    P6_EDGE(27);
}
void Ring_Draw_Normal(void) { P6_EDGE(28); }
void Ring_LoseHyperRings(EntityPlayer *player, int32 rings, uint8 cPlane)
{
    (void)player; (void)rings; (void)cPlane;
    P6_EDGE(29);
}
void Ring_LoseRings(EntityPlayer *player, int32 rings, uint8 cPlane)
{
    (void)player; (void)rings; (void)cPlane;
    P6_EDGE(30);
}
void Ring_State_Lost(void) { P6_EDGE(31); }
// P6.8 F.3 (Task #235): SignPost closure. The sparkle Ring entities SignPost
// creates use these state/draw fns -- no-op (cosmetic; the act-clear chain
// does NOT depend on the sparkles rendering). The pack `Ring` pointer is wired
// to the overlay's registered Ring object at scene-load so CREATE_ENTITY(Ring)
// is safe (p6_io_main p6_scene_load_and_arm).
void Ring_State_Sparkle(void) { P6_EDGE(59); }
void Ring_Draw_Sparkle(void)  { P6_EDGE(60); }
// #W18 ring-arm: Ring_CheckPlatformCollisions (Ring.c:381) references these Platform
// states for the carry-direction adjust -- GHZ1-DEAD (Track/Big/Path-ring path only;
// see the Platform NULL above). Inert stubs, header signatures void(void).
void Platform_State_Falling2(void) { P6_EDGE(63); }
void Platform_State_Hold(void) { P6_EDGE(64); }
// SignPost competition + achievement edges (competition branches are DEAD in
// Mania mode -- never reached at runtime; the achievement unlock fires on a
// signpost pass but the API call is a no-op on Saturn).
void Announcer_AnnounceGoal(int32 screen) { (void)screen; P6_EDGE(61); }
// NOTE: Zone_StartFadeOut_Competition is provided by the compiled Zone TU
// (Game_Zone.o) -- do NOT stub it here (multiple-definition).
void APICallback_UnlockAchievement(const char *name) { (void)name; P6_EDGE(62); }
// F.4 GHZSetup closure: CutsceneRules act predicates. GHZSetup_StageLoad uses
// IsAct1 to set Zone->stageFinishCallback=EndAct1 (the GHZ1->GHZ2 ATL trigger),
// so these must reflect Zone->actID (0=Act1, 1=Act2). No reload in this build.
bool32 CutsceneRules_CheckStageReload(void) { return false; }
bool32 CutsceneRules_IsAct1(void) { return Zone && Zone->actID == 0; }
bool32 CutsceneRules_IsAct2(void) { return Zone && Zone->actID == 1; }
int32 UIButtonPrompt_GetGamepadType(void)
{
    P6_EDGE(32);
    return 0;
}
uint8 UIButtonPrompt_MappingsToFrame(int32 mappings)
{
    (void)mappings;
    P6_EDGE(33);
    return 0;
}
void UIControl_SetMenuLostFocus(EntityUIControl *entity)
{
    (void)entity;
    P6_EDGE(34);
}
void UIDialog_AddButton(uint8 frame, EntityUIDialog *dialog, void (*callback)(void), bool32 closeOnSelect)
{
    (void)frame; (void)dialog; (void)callback; (void)closeOnSelect;
    P6_EDGE(35);
}
void UIDialog_CloseOnSel_HandleSelection(EntityUIDialog *dialog, void (*callback)(void))
{
    (void)dialog; (void)callback;
    P6_EDGE(36);
}
EntityUIDialog *UIDialog_CreateActiveDialog(String *msg)
{
    (void)msg;
    P6_EDGE(37);
    return NULL;
}
EntityUIDialog *UIDialog_CreateDialogYesNo(String *text, void (*callbackYes)(void), void (*callbackNo)(void), bool32 closeOnSelect_Yes,
                                           bool32 closeOnSelect_No)
{
    (void)text; (void)callbackYes; (void)callbackNo; (void)closeOnSelect_Yes; (void)closeOnSelect_No;
    P6_EDGE(38);
    return NULL;
}
void UIDialog_Setup(EntityUIDialog *dialog)
{
    (void)dialog;
    P6_EDGE(39);
}
void UIWaitSpinner_FinishWait(void) { P6_EDGE(40); }
void UIWaitSpinner_StartWait(void) { P6_EDGE(41); }
void UIWidgets_DrawParallelogram(int32 x, int32 y, int32 width, int32 height, int32 edgeSize, int32 red, int32 green, int32 blue)
{
    (void)x; (void)y; (void)width; (void)height; (void)edgeSize; (void)red; (void)green; (void)blue;
    P6_EDGE(42);
}
void UIWidgets_DrawRightTriangle(int32 x, int32 y, int32 size, int32 red, int32 green, int32 blue)
{
    (void)x; (void)y; (void)size; (void)red; (void)green; (void)blue;
    P6_EDGE(43);
}

// ---- 3. sprintf over the pack's integer-only vsprintf ------------------------
// (HUD/Music format scores/paths; newlib's sprintf would re-drag the float
// closure p6_vsprintf.c exists to avoid.)
int vsprintf(char *buf, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}
