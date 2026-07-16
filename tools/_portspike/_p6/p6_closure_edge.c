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
// PER-ORDINAL HISTOGRAM (the "map EVERYTHING" upgrade): every P6_EDGE(n) bumps
// p6_w_edge_hits[n], so a SINGLE broad-gameplay capture reveals EVERY shadowed
// boundary function that fired (and how often) -- not just the last one. The
// audit gate (qa_p6_edge_audit.py) maps each nonzero ordinal -> its decomp fn
// and classifies dead-cosmetic vs broken-gameplay. SIZE must exceed the highest
// P6_EDGE ordinal used below (currently <80).
#define P6_EDGE_MAX 96
__attribute__((used)) int32 p6_w_edge_hits[P6_EDGE_MAX] = {0};

#define P6_EDGE(n)                                                             \
    do {                                                                       \
        ++p6_w_edge_calls;                                                     \
        p6_w_edge_last = (n);                                                  \
        if ((unsigned)(n) < P6_EDGE_MAX) ++p6_w_edge_hits[(n)];               \
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
// BATCH 2 (badnik break chain, 2026-06-18): `Animals` STAYS a PACK NULL placeholder
// because ActClear.c:903 (a PACK TU, reached on act-clear) does
// foreach_active(Animals, ...) -> derefs Animals->classID. The REAL registered
// Animals lives in the OVERLAY (Game_Animals.o -- it had to, because
// Animals_CheckGroundCollision refs the overlay's Bridge_HandleCollisions). This NULL
// is REWIRED to the overlay's registered Animals every frame (p6_io_main, the #235
// Ring-seam pattern) so the pack's foreach_active sees the live classID. The badnik
// break path itself never touches THIS pointer -- it runs inside the overlay's
// BadnikHelpers (reached via the closure-edge forward), where `Animals` is the real one.
ObjectAnimals *Animals           = NULL; // PACK placeholder; rewired to the overlay's (p6_io_main)
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
ObjectPlatform *Platform         = NULL; // Batch 3 step 2: the REAL Platform is now
                                         // OVERLAY-resident (Game_Platform.o defines its
                                         // own global; Ring/ItemBox/AIZSetup bind to it
                                         // intra-overlay). This PACK placeholder stays
                                         // NULL -- NO pack TU reads it (grep-verified).
// Batch 3 step 2 (full Platform port): Platform.c's remaining cross-class globals.
// HangPoint (Platform.c:363, childCount loop -- NULL-guarded); PlatformNode/
// PlatformControl (:1064/:1180-1190 -- reached ONLY from State_Path/State_PathReact,
// i.e. PLATFORM_PATH/PATH_REACT types; GHZ1 authors types 0/1/2/4/6 only, and the
// 2 level-wide PlatformNode circuits in the whole game are elsewhere (camera-local
// pool audit)). NULL == unregistered.
ObjectHangPoint *HangPoint             = NULL;
ObjectPlatformNode *PlatformNode       = NULL;
ObjectPlatformControl *PlatformControl = NULL;
ObjectTurboTurtle *TurboTurtle         = NULL; // Platform.c:84 child-fan check, NULL-guarded
                                               // (TurboTurtle && ...); AIZ/TMZ fan badnik,
                                               // unregistered here -> NULL == unregistered.
ObjectPress *Press               = NULL; // Spikes_Update NULL-guards it (PGZ press hazard, GHZ1-dead)
ObjectCrate *Crate               = NULL;
ObjectIce *Ice                   = NULL;
ObjectBigSqueeze *BigSqueeze     = NULL;
ObjectSpikeCorridor *SpikeCorridor = NULL;
ObjectDebris *Debris             = NULL; // Batch 3: rewired per-frame to the OVERLAY's
                                         // registered Debris (s_ovl.debris_slot) so
                                         // Shield.c:194+'s CREATE_ENTITY(Debris,
                                         // Debris_State_Move,...) sparks spawn live.
ObjectERZOutro *ERZOutro         = NULL;
ObjectFXFade *FXFade             = NULL;
ObjectFXRuby *FXRuby             = NULL;
#if defined(P6_GHZCUT_BOOT)
// Task #309: CutsceneHBH references FXTrail (the global + CREATE_ENTITY(FXTrail,...) in
// CutsceneHBH_ShinobiJumpSetup, CutsceneHBH.c:322). ShinobiJumpSetup is OFF the GHZ
// ExitHBH beat path (GHZCutsceneST_Cutscene_ExitHBH manipulates the Shinobi hbh fields
// directly, never calling *Setup), so FXTrail is never spawned on the Tier-A handoff.
// NULL == unregistered: CREATE_ENTITY on a NULL class is inert (the foreach/spawn just
// no-ops), the same pattern as BurningLog/Platform above. GHZ/AIZ/menu leave
// P6_GHZCUT_BOOT unset -> the symbol is absent there (no FXTrail reference) -> byte-identical.
ObjectFXTrail *FXTrail           = NULL;
#endif
ObjectFarPlane *FarPlane         = NULL;
ObjectHCZSetup *HCZSetup         = NULL;
ObjectIceSpring *IceSpring       = NULL;
ObjectItemBox *ItemBox           = NULL; // Batch 3: rewired per-frame to the OVERLAY's
                                         // registered ItemBox (s_ovl.itembox_slot, the
                                         // #235 Ring-seam) -- pack readers Zone.c:380 /
                                         // SaveGame.c:133,295 / Ice.c:235 then see the
                                         // live classID. NULL only until the overlay loads.
// Batch 3 (ItemBox port): the verbatim ItemBox references the LRZ conveyor-physics
// object (State_Broken/Break/Conveyor, ItemBox.c:241/250/365). LRZ-only; GHZ1-dead.
// NULL == unregistered; the HandleLRZConvPhys stub below covers the link-time fn ref
// (runtime-guarded: every call site requires LRZConvItem non-NULL or a state only
// entered when it is non-NULL).
ObjectLRZConvItem *LRZConvItem   = NULL;
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
#if defined(P6_AIZ_TEST)
// M3.1 (qa_p6_aiz_cutscene): the AIZ overlay TUs AIZTornado_Create (AIZTornado.c:63)
// and AIZTornadoPath_Create (AIZTornadoPath.c:30,75) deref StarPost->postIDs[0] -- a
// fresh-boot 0 (== arm the tornado fly-in + grab SLOT_CAMERA1). StarPost is NOT
// registered in this flavor, so a NULL StarPost would crash that deref. Point the pack
// `StarPost` global (which the overlay resolves via ld -R) at a real ZEROED ObjectStarPost
// instance -> postIDs[0] reads 0 -> the tornado/path arm. This mirrors the menu's zeroed
// APICallback/UIDialog instance pattern (p6_menu_apic_init). The fresh-boot semantics are
// EXACTLY what M3.1 wants (no StarPost touched -> the intro cutscene plays). GHZ/menu leave
// P6_AIZ_TEST unset -> StarPost stays NULL -> byte-identical.
static ObjectStarPost p6_aiz_starpost_instance; // zero-init (.bss)
ObjectStarPost *StarPost         = &p6_aiz_starpost_instance;
#else
ObjectStarPost *StarPost         = NULL;
#endif
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
// BATCH 2 (badnik break chain, 2026-06-18): Player_CheckBadnikBreak (Player.c:2539,
// the #else / non-PLUS branch -- ACTIVE because MANIA_USE_PLUS=(GAME_VERSION>=5)=
// (3>=5)=FALSE) calls APICallback_TrackEnemyDefeat on every badnik kill. The sibling
// TrackTAClear/TrackActClear are stubbed above but TrackEnemyDefeat was NOT (it has
// NO definition anywhere in the pack -- a hard undefined-ref the instant the break
// path links). Inert no-op on Saturn (the real impl is the removed-at-REV02
// RSDK.GetAPIFunction surface). Signature is byte-for-byte APICallback.h:201.
void APICallback_TrackEnemyDefeat(uint8 zoneID, uint8 actID, uint8 playerID, int32 x, int32 y)
{
    (void)zoneID; (void)actID; (void)playerID; (void)x; (void)y; P6_EDGE(65);
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
#if defined(P6_GHZCUT_BOOT)
// Task #309: CutsceneRules_SetupEntity is CRITICAL-PATH for GHZCutsceneST (and every
// collision-triggered cutscene). GHZCutsceneST_Create calls it to compute self->hitbox
// from the cutscene size; GHZCutsceneST_Update's Player_CheckCollisionTouch(self,
// &self->hitbox) trigger NEVER fires with a zero hitbox -> the cutscene never starts ->
// no playable-GHZ handoff. The no-op #else stub (used by the working AIZ build, which
// never calls this -- AIZSetup auto-starts via StaticUpdate) is therefore a SILENT
// blocker here. This body is the VERBATIM decomp impl (CutsceneRules.c:94-111, pulled
// from RSDKModding/Sonic-Mania-Decompilation) -- a mechanical port, not hand-rolled
// logic. WIDE_SCR_XSIZE/SCREEN_YSIZE/TO_FIXED from GameVariables.h/GameLink.h;
// EntityCutsceneRules (MANIA_CUTSCENE_BASE: updateRange via RSDK_ENTITY + Hitbox) via
// All.h. GHZ/AIZ/menu builds leave P6_GHZCUT_BOOT unset -> the no-op #else links ->
// byte-identical.
void CutsceneRules_SetupEntity(void *e, Vector2 *size, Hitbox *hitbox)
{
    EntityCutsceneRules *entity = (EntityCutsceneRules *)e;

    if (!size->x)
        size->x = WIDE_SCR_XSIZE << 16;

    if (!size->y)
        size->y = SCREEN_YSIZE << 16;

    entity->updateRange.x = TO_FIXED(128) + size->x;
    entity->updateRange.y = TO_FIXED(128) + size->y;

    hitbox->left   = -size->x >> 17;
    hitbox->top    = -size->y >> 17;
    hitbox->right  = size->x >> 17;
    hitbox->bottom = size->y >> 17;
}
#else
void CutsceneRules_SetupEntity(void *e, Vector2 *size, Hitbox *hitbox)
{
    (void)e; (void)size; (void)hitbox;
    P6_EDGE(6);
}
#endif
void CutsceneSeq_LockAllPlayerControl(void) { P6_EDGE(7); }
void CutsceneSeq_StartSequence(void *manager, ...)
{
    (void)manager;
    P6_EDGE(8);
}
// Batch 3 (Debris port): Shield.c (PACK) assigns Debris_State_Move as the lightning-
// shield spark's state -> the spark entity's StateMachine_Run lands HERE (the pack
// symbol), not on the overlay's real impl. Forward pack->overlay via the runtime
// pointer (set by p6_io_main from s_ovl.debris_state_move_fn once the overlay entry
// runs) -- the #258b Ring_LoseRings pattern. Until then the stub no-ops as before.
extern void *p6_ovl_debris_state_move_raw;
void Debris_State_Move(void)
{
    if (p6_ovl_debris_state_move_raw) {
        ((void (*)(void))p6_ovl_debris_state_move_raw)();
        return;
    }
    P6_EDGE(9);
}
void FXRuby_State_Shrinking(void) { P6_EDGE(10); }
// Batch 3 (ItemBox port): the real ItemBox is OVERLAY-resident. PACK callers bind
// here: Ice.c:600 calls ItemBox_Break (PGZ-dead in GHZ1 but forwarded for closure);
// SaveGame.c:138 ASSIGNS ItemBox_State_Broken to recalled broken boxes -> their
// StateMachine_Run lands on the pack symbol -> forward runs the real broken-state
// body. KNOWN DIVERGENCE (documented in p6_ovl_api.h): SaveGame.c:300's POINTER
// compare (state == ItemBox_State_Broken) sees this pack address while live broken
// boxes carry the OVERLAY address -> broken boxes are not re-recorded on ATL store.
// Forward pointers set by p6_io_main from s_ovl.itembox_*_fn (the #258b pattern).
extern void *p6_ovl_itembox_break_raw;
extern void *p6_ovl_itembox_state_broken_raw;
extern void *p6_ovl_itembox_state_falling_raw;
extern void *p6_ovl_itembox_state_idle_raw;
void ItemBox_Break(EntityItemBox *itemBox, EntityPlayer *player)
{
    if (p6_ovl_itembox_break_raw) {
        ((void (*)(EntityItemBox *, EntityPlayer *))p6_ovl_itembox_break_raw)(itemBox, player);
        return;
    }
    (void)itemBox; (void)player;
    P6_EDGE(11);
}
void ItemBox_State_Broken(void)
{
    if (p6_ovl_itembox_state_broken_raw) {
        ((void (*)(void))p6_ovl_itembox_state_broken_raw)();
        return;
    }
    P6_EDGE(12);
}
void ItemBox_State_Falling(void)
{
    if (p6_ovl_itembox_state_falling_raw) {
        ((void (*)(void))p6_ovl_itembox_state_falling_raw)();
        return;
    }
    P6_EDGE(13);
}
void ItemBox_State_Idle(void)
{
    if (p6_ovl_itembox_state_idle_raw) {
        ((void (*)(void))p6_ovl_itembox_state_idle_raw)();
        return;
    }
    P6_EDGE(14);
}
// Batch 3 (ItemBox port): the LRZ conveyor-physics helper (LRZConvItem.h:48
// `Vector2 LRZConvItem_HandleLRZConvPhys(void *e)`). Every ItemBox call site is
// runtime-dead while LRZConvItem is NULL (State_Broken/Break guard on the global;
// State_Conveyor is only entered when it is non-NULL, ItemBox.c:168-171). Inert
// zero-offset stub; the histogram flags any unexpected crossing.
Vector2 LRZConvItem_HandleLRZConvPhys(void *e)
{
    Vector2 none;
    none.x = 0;
    none.y = 0;
    (void)e;
    P6_EDGE(68);
    return none;
}
void KleptoMobile_StateArm_Cutscene(void) { P6_EDGE(15); }
void KleptoMobile_StateArm_Idle(void) { P6_EDGE(16); }
void KleptoMobile_StateHand_Boss(void) { P6_EDGE(17); }
void KleptoMobile_StateHand_Cutscene(void) { P6_EDGE(18); }
void KleptoMobile_State_CutsceneControlled(void) { P6_EDGE(19); }
#if defined(P6_AIZ_TEST)
// #302 batch fix: the 14 _MenuSetup_rev02.c call sites deref the returned param
// UNGUARDED (decomp MenuParam.c:16 returns the reserved SLOT_MENUPARAM entity,
// never NULL) -- a NULL here made every deref read Saturn address 0x0.. = BIOS
// ROM = garbage menu params. Same pattern as p6_aiz_starpost_instance above: a
// ZEROED static instance == the decomp's fresh-boot globals->menuParam
// semantics (the reserved slot is zero until a mode writes it; writes persist
// across calls, mirroring the reserved-slot lifetime). P6_EDGE(20) kept so the
// crossing stays visible in the edge histogram. Chain/menu flavors only
// (P6_AIZ_TEST); plain GHZ keeps NULL -> byte-identical.
static EntityMenuParam p6_menuparam_instance; // zero-init (.bss)
EntityMenuParam *MenuParam_GetParam(void)
{
    P6_EDGE(20);
    return &p6_menuparam_instance;
}
#else
EntityMenuParam *MenuParam_GetParam(void)
{
    P6_EDGE(20);
    return NULL;
}
#endif
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
// #258b: forward the pack-side Player's hurt-ring calls to the OVERLAY's REAL
// Ring_LoseRings/LoseHyperRings (set by p6_io_main after the overlay entry runs).
// Without this the pack binds to these stubs and lost rings never scatter --
// the exact bug the user hit (collect worked because the overlay's Ring_Create
// wires the FIXED-ring draw inside the overlay, but loss crosses pack->overlay).
extern void *p6_ovl_loserings_raw;
extern void *p6_ovl_losehyperrings_raw;
void Ring_LoseHyperRings(EntityPlayer *player, int32 rings, uint8 cPlane)
{
    if (p6_ovl_losehyperrings_raw) {
        ((void (*)(EntityPlayer *, int32, uint8))p6_ovl_losehyperrings_raw)(player, rings, cPlane);
        return;
    }
    (void)player; (void)rings; (void)cPlane;
    P6_EDGE(29);
}
void Ring_LoseRings(EntityPlayer *player, int32 rings, uint8 cPlane)
{
    if (p6_ovl_loserings_raw) {
        ((void (*)(EntityPlayer *, int32, uint8))p6_ovl_loserings_raw)(player, rings, cPlane);
        return;
    }
    (void)player; (void)rings; (void)cPlane;
    P6_EDGE(30);
}
void Ring_State_Lost(void) { P6_EDGE(31); }
// BATCH 2 (badnik break chain): the pack-side Player (Player_CheckBadnikBreak,
// Player.c:2514) calls BadnikHelpers_BadnikBreakUnseeded on every badnik kill. The
// real BadnikHelpers (+ Explosion/Animals it derefs, + the overlay's Bridge_Handle-
// Collisions Animals refs) is OVERLAY-resident, so the pack binds to these stubs
// which forward pack->overlay via the runtime pointers (set by p6_io_main once the
// overlay entry runs). Identical to the #258b Ring_LoseRings forward. Until the
// overlay loads the pointer is 0 and the call no-ops (the break path is gameplay-
// only -- never reached before the overlay is live). Signatures = BadnikHelpers.h.
extern void *p6_ovl_badnikbreak_unseeded_raw;
extern void *p6_ovl_badnikbreak_raw;
void BadnikHelpers_BadnikBreakUnseeded(void *badnik, bool32 destroy, bool32 spawnAnimals)
{
    if (p6_ovl_badnikbreak_unseeded_raw) {
        ((void (*)(void *, bool32, bool32))p6_ovl_badnikbreak_unseeded_raw)(badnik, destroy, spawnAnimals);
        return;
    }
    (void)badnik; (void)destroy; (void)spawnAnimals;
    P6_EDGE(66);
}
void BadnikHelpers_BadnikBreak(void *badnik, bool32 destroy, bool32 spawnAnimals)
{
    if (p6_ovl_badnikbreak_raw) {
        ((void (*)(void *, bool32, bool32))p6_ovl_badnikbreak_raw)(badnik, destroy, spawnAnimals);
        return;
    }
    (void)badnik; (void)destroy; (void)spawnAnimals;
    P6_EDGE(67);
}
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
// Batch 3 (ItemBox port): ItemBox_HandlePlatformCollision (ItemBox.c:998) also
// references Platform_State_Fall. Inert stub until the full Game_Platform.o joins
// the overlay (Batch 3 step 2) -- the overlay's own definition then wins intra-
// overlay and this pack stub goes dead (kept only by its -u root, ~8 B).
void Platform_State_Fall(void) { P6_EDGE(69); }
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
