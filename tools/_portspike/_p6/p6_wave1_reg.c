// =============================================================================
// p6_wave1_reg.c -- P6.7 wave-1 (Task #210): the GAME-SIDE link + registration
// TU, playing SonicMania_Game.c's role for the first verbatim Global-TU wave
// (Localization, LogHelpers, Options -- Game.c registration order
// :427/:429/:517). Compiled as C against the REAL Game.h/GameLink.h surface
// with the CENSUS knob set (-DRETRO_REVISION=2 -DGAME_VERSION=3): the P6
// engine builds RETRO_REVISION=2 (build_p6scene_objs.sh CORE_DEFS beats
// RetroEngine.hpp:227's #ifndef default), so the game side MUST match --
// at REV0U the table gains entries and RegisterObject/RegisterGlobalVariables
// grow extra args (garbage through the table otherwise). Plus
// -DSATURN_GLOBALS_RETARGET (shrunk GlobalVariables layout) and
// -DRETRO_USE_MOD_LOADER=0 (the engine builds without the mod loader).
//
// FLAT-TU RULE: this TU never sees an engine header. The engine surface
// arrives as the raw function-table pointer + info-struct pointers passed
// into p6_wave1_link() by p6_io_main.cpp, exactly the LinkGameLogicDLL shape
// (SonicMania_Game.c:111-136 pre-Plus). The memcpy table fill is sound per
// gate qa_p6_globals G1 (slot-for-slot ABI check vs the engine's
// FunctionTable_ enum + Link.cpp bindings, run offline every gate pass).
// =============================================================================
#include "Game.h"

// ---- ENGINE VARIABLES (SonicMania_Game.c:3-41, pre-Plus set) ----------------
RSDKFunctionTable RSDK;                       // Game.c:7
int32 RSDKRevision = RETRO_REVISION;          // Game.c:19
RSDKSceneInfo *SceneInfo = NULL;              // Game.c:21
RSDKGameInfo *GameInfo = NULL;                // Game.c:23
RSDKControllerState *ControllerInfo = NULL;   // Game.c:28
RSDKAnalogState *AnalogStickInfoL = NULL;     // Game.c:29
RSDKTouchInfo *TouchInfo = NULL;              // Game.c:35
RSDKScreenInfo *ScreenInfo = NULL;            // Game.c:41
RSDKUnknownInfo *UnknownInfo = NULL;          // Game.c:38 (REV02; PauseMenu's
                                              // Unknown_pausePress macro)

// SATURN 1.03-on-v5U compat (see SonicMania_Objects_Global_APICallback.h
// sku_* arm): pre-Plus code reaches platform/language/region through SKU,
// defined here from EngineInfo->currentSKU exactly as Game.c:95 does under
// Plus. The engine fills SKU::curSKU before the link call (p6_io_main).
RSDKSKUInfo *SKU = NULL;

// ---- GAME VARIABLES (SonicMania_Game.c:43-68) -------------------------------
GlobalVariables *globals;                     // Game.c:47

// VERBATIM GlobalVariables_InitCB (Game.c:50-67). COMPILED OUT at this
// build's RETRO_REVISION=2 (it is the v5U seeding mechanism); kept verbatim
// for the day the engine flips to REV0U. At REV02 the equivalent state
// arrives canonically: the seam memsets the window (AllocateStorage
// clearMemory=true mirror) and the engine's REV02 seed loop writes the
// GameConfig var seeds -- whose nonzero payload gen_globals_map.py
// self-test S6 proves EQUALS these writes ({saveSlotID=NO_SAVE_SLOT,
// presenceID=-1}).
#if RETRO_REV0U
void GlobalVariables_InitCB(GlobalVariables *globals)
{
    memset(globals, 0, sizeof(GlobalVariables));

    globals->saveSlotID = NO_SAVE_SLOT;

    globals->presenceID = -1;

#if MANIA_USE_PLUS
    globals->replayTableID = (uint16)-1;
    globals->taTableID     = (uint16)-1;

    globals->stock          = (ID_RAY << 16) | (ID_KNUCKLES << 8) | ID_TAILS;
    globals->characterFlags = ID_SONIC | ID_TAILS | ID_KNUCKLES | ID_MIGHTY | ID_RAY;

    globals->superMusicEnabled = true;
#endif
}
#endif

// ---- Witnesses (DEFINED in p6_io_main.cpp, the main image) ------------------
extern int32 p6_w_w1_locale;
extern int32 p6_w_plr_classid;
extern int32 p6_w_plr_stageload;
extern int32 p6_w_plr_slot;
extern int32 p6_w_plr_x;
extern int32 p6_w_plr_y;
extern int32 p6_w_plr_entclass;
extern int32 p6_w_plr_staticsize;
extern int32 p6_w_plr_sonicframes;

// =============================================================================
// p6_wave1_link -- the LinkGameLogicDLL role (Game.c:111-136 pre-Plus shape,
// REV02 EngineInfo field set per GameLink.h:417-443) + the wave-1 subset of
// InitGameLogic (Game.c:140-147 + the three RSDK_REGISTER_OBJECT lines).
// Called by p6_io_main.cpp AFTER SetupFunctionTables + the overlay entry
// (so the overlay Ring keeps classID 2) and BEFORE LoadGameConfig (which
// hash-matches GameConfig's object names against the registered classes and
// then drives GlobalVariables_InitCB).
// =============================================================================
void p6_wave1_link(void *functionTable, void *gameInfo, void *currentSKU,
                   void *sceneInfo, void *controllerInfo, void *stickInfoL,
                   void *touchInfo, void *screenInfo, void *unknownInfo)
{
    memset(&RSDK, 0, sizeof(RSDKFunctionTable));   // Game.c:118

    if (functionTable)                              // Game.c:120-121
        memcpy(&RSDK, functionTable, sizeof(RSDKFunctionTable));

    GameInfo         = (RSDKGameInfo *)gameInfo;         // Game.c:128
    SKU              = (RSDKSKUInfo *)currentSKU;        // Game.c:95 (Plus)
    SceneInfo        = (RSDKSceneInfo *)sceneInfo;       // Game.c:129
    ControllerInfo   = (RSDKControllerState *)controllerInfo; // Game.c:130
    AnalogStickInfoL = (RSDKAnalogState *)stickInfoL;    // Game.c:131
    TouchInfo        = (RSDKTouchInfo *)touchInfo;       // Game.c:132
    ScreenInfo       = (RSDKScreenInfo *)screenInfo;     // Game.c:133
    UnknownInfo      = (RSDKUnknownInfo *)unknownInfo;   // Game.c:105 (REV02)

    // InitGameLogic wave-1 subset (Game.c:140-147):
#if RETRO_REV0U
    RSDK.RegisterGlobalVariables((void **)&globals, sizeof(GlobalVariables),
                                 (void (*)(void *))GlobalVariables_InitCB); // Game.c:143
#else
    RSDK.RegisterGlobalVariables((void **)&globals, sizeof(GlobalVariables)); // Game.c:145
#endif

    // P6.7 Player wave (Task #227): the 17-TU depth-2 closure joins the
    // wave-1 trio. ONE list in STRICT Game.c line order (relative order is
    // the decomp's registration contract; classIDs follow objectClassCount).
    // APICallback (Game.c:166) NOT registered: the verbatim TU needs
    // pre-REV02 RSDK.GetAPIFunction; its referenced entry points stub at
    // the closure edge until the SaveGame/UserStorage wave (p6_closure_edge.c).
    RSDK_REGISTER_OBJECT(BoundsMarker); // Game.c:188
    RSDK_REGISTER_OBJECT(Camera);       // Game.c:212
    RSDK_REGISTER_OBJECT(DebugMode);    // Game.c:265
    RSDK_REGISTER_OBJECT(DrawHelpers);  // Game.c:277
    RSDK_REGISTER_OBJECT(Dust);         // Game.c:280
    RSDK_REGISTER_OBJECT(ERZStart);     // Game.c:305
    RSDK_REGISTER_OBJECT(GameOver);     // Game.c:352
    RSDK_REGISTER_OBJECT(HUD);          // Game.c:395
    RSDK_REGISTER_OBJECT(Ice);          // Game.c:396
    RSDK_REGISTER_OBJECT(ImageTrail);   // Game.c:399
    RSDK_REGISTER_OBJECT(Localization); // Game.c:427
    RSDK_REGISTER_OBJECT(LogHelpers);   // Game.c:429
    RSDK_REGISTER_OBJECT(MathHelpers);  // Game.c:462
    RSDK_REGISTER_OBJECT(Music);        // Game.c:499
    RSDK_REGISTER_OBJECT(Options);      // Game.c:517
    RSDK_REGISTER_OBJECT(PauseMenu);    // Game.c:529
    RSDK_REGISTER_OBJECT(Player);       // Game.c:563
    RSDK_REGISTER_OBJECT(SaveGame);     // Game.c:623
    RSDK_REGISTER_OBJECT(ScoreBonus);   // Game.c:629
    RSDK_REGISTER_OBJECT(Shield);       // Game.c:636
    RSDK_REGISTER_OBJECT(SizeLaser);    // Game.c:644
    RSDK_REGISTER_OBJECT(Soundboard);   // Game.c:649
    RSDK_REGISTER_OBJECT(Zone);         // Game.c:854
}

// =============================================================================
// p6_wave1_witness -- called from the pack tick after the stage-load chain;
// copies game-side state into the main-image witness for qa_p6_globals G8:
// (Localization->loaded << 8) | language, 0xFFFF while staticVars are
// unallocated (before LoadSceneAssets runs the StageLoad callbacks).
// =============================================================================
void p6_wave1_witness(void)
{
    if (Localization)
        p6_w_w1_locale = ((int32)(Localization->loaded ? 1 : 0) << 8)
                         | (int32)Localization->language;
    else
        p6_w_w1_locale = 0xFFFF;
}

// =============================================================================
// p6_player_witness_pre -- called by the GHZ pass (p6_io_main.cpp) AFTER
// LoadSceneAssets and BEFORE InitObjects: Player_StageLoad's
// Player_LoadSprites (Player.c:714 -> :781-815) CopyEntity's the scene
// Player into SLOT_PLAYER1 and memsets the scene slot, so the scene-slot
// evidence (qa_p6_player P4/P5 vs the offline Scene1.bin parse) must be
// captured first. startSlot/sceneCount arrive from the engine TU
// (RESERVE_ENTITY_COUNT / SCENEENTITY_COUNT, Object.hpp Saturn arm) -- this
// TU's Game.h carries the PC entity counts and must not bake them.
// Player->classID is the STAGE classID (Scene.cpp:237 domain); the entity
// walk rides RSDK.GetEntity through the engine's RSDK_ENTITY_AT seam.
// =============================================================================
void p6_player_witness_pre(int32 startSlot, int32 sceneCount)
{
    p6_w_plr_staticsize = (int32)sizeof(ObjectPlayer);
    if (!Player || !Player->classID)
        return;
    p6_w_plr_classid = (int32)Player->classID;
    for (int32 s = 0; s < sceneCount; ++s) {
        Entity *e = RSDK.GetEntity((uint16)(startSlot + s));
        if (e && e->classID == Player->classID) {
            p6_w_plr_slot     = s; // raw scene index (the gate model domain)
            p6_w_plr_x        = e->position.x;
            p6_w_plr_y        = e->position.y;
            p6_w_plr_entclass = (int32)e->classID;
            break;
        }
    }
}

// =============================================================================
// p6_player_witness_post -- called immediately after InitObjects:
// Player_StageLoad sets Player->active = ACTIVE_ALWAYS (Player.c:708) and
// playerCount = GetEntityCount(Player->classID, false) (Player.c:726,
// pre-Plus arm) -- nonzero because Player_LoadSprites just created
// SLOT_PLAYER1. Both together witness "StageLoad ran against real GHZ data".
// =============================================================================
void p6_player_witness_post(void)
{
    if (!Player)
        return;
    p6_w_plr_stageload = (Player->active == ACTIVE_ALWAYS && Player->playerCount > 0) ? 1 : 0;
    // P7 (STG sizing): LoadSpriteAnimation("Players/Sonic.bin") result --
    // 0xFFFF == the alloc-fail refusal (Player.c:795 assigns the uint16 -1).
    p6_w_plr_sonicframes = (int32)Player->sonicFrames;
}
