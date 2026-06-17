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
extern int32 p6_w_plr_tick_x;
extern int32 p6_w_plr_tick_y;
extern int32 p6_w_plr_state;
extern int32 p6_w_plr_onground;
extern int32 p6_w_plr_animframes;
extern int32 p6_w_plr_animid;
extern int32 p6_w_plr_drawflags;
extern int32 p6_w_plr_sheetid_t;
extern int32 p6_w_cam_static;
extern int32 p6_w_zone_static;
extern int32 p6_w_cam_entclass;
extern int32 p6_w_cam_state;
extern int32 p6_w_cam_target;
extern int32 p6_w_cam_x;
extern int32 p6_w_cam_y;
extern int32 p6_w_scr_x;
extern int32 p6_w_scr_y;
extern int32 p6_w_cam_entclass0;
extern int32 p6_w_api_screencount; // game-side RSDK.GetVideoSetting(SCREENCOUNT) result
extern int32 p6_w_api_credits;     // game-side RSDK.CheckSceneFolder("Credits") result
extern int32 p6_w_zone_boundsR;
extern int32 p6_w_zone_boundsB;
extern int32 p6_w_cam_boundsR;
extern int32 p6_w_cam_boundsB;

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
    // P6.7 W7 (Task #227): the W15 zeroed-statics stopgap is RETIRED --
    // p6_io_main.cpp now passes the ENGINE's own input arrays
    // (RSDK::controller / RSDK::stickL / &RSDK::touchInfo, the
    // RetroEngine.cpp:1287-1292 EngineInfo shape), filled per tick by the
    // verbatim ProcessInput + the Saturn SMPC device backend
    // (platform/Saturn/InputDevice_Saturn.cpp). Gate qa_p6_input.py I3
    // asserts these pointers equal the engine array addresses.
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
    RSDK_REGISTER_OBJECT(ActClear);     // Game.c:~160 (F.2: ActClear -> stageFinishCallback)
    RSDK_REGISTER_OBJECT(BGSwitch);     // Game.c:178 (F.4: GHZSetup BG switch; funded by MAX_WORKS reclaim)
    RSDK_REGISTER_OBJECT(BoundsMarker); // Game.c:188
    RSDK_REGISTER_OBJECT(Bridge);       // Game.c:~195 (#181: GHZ planks; alpha order Bo<Br<Ca)
    RSDK_REGISTER_OBJECT(Camera);       // Game.c:212
    RSDK_REGISTER_OBJECT(DebugMode);    // Game.c:265
    RSDK_REGISTER_OBJECT(DrawHelpers);  // Game.c:277
    RSDK_REGISTER_OBJECT(Dust);         // Game.c:280
    RSDK_REGISTER_OBJECT(ERZStart);     // Game.c:305
    RSDK_REGISTER_OBJECT(GameOver);     // Game.c:352
    RSDK_REGISTER_OBJECT(GHZSetup);     // Game.c:363 (F.4: GHZ1->GHZ2 ATL trigger via stageFinishCallback)
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
    RSDK_REGISTER_OBJECT(SignPost);     // Game.c:642 (F.3: spawns ActClear -> GHZ1->GHZ2)
    RSDK_REGISTER_OBJECT(SizeLaser);    // Game.c:644
    RSDK_REGISTER_OBJECT(Soundboard);   // Game.c:649
    // #254 GHZ1 loop fix: PlaneSwitch = the collision-plane toggle (106 placed in
    // GHZ1) that lets Sonic run the inside of a loop-de-loop. Census drop-in (fits
    // the 344 B slot, no deps/sheets/stub-gates). The corkscrew force-objects
    // (CorkscrewPath/ForceSpin/ForceUnstick/SpinBooster) are DEFERRED: adding all 5
    // pushed _end 1,564 B past the ANIMPAK floor (their ~9.9 KB of .text + the SGL
    // work-area COMMON), so the corkscrew batch needs a code-budget lever first.
    RSDK_REGISTER_OBJECT(PlaneSwitch);
    RSDK_REGISTER_OBJECT(Zone);         // Game.c:854
}

// =============================================================================
// p6_input_witness -- P6.7 W7 (Task #227): called by p6_io_main.cpp right
// after p6_wave1_link; copies the GAME-side input pointers into the main-
// image witnesses so gate qa_p6_input.py I3 can compare them against the
// ENGINE array addresses from the link map (proving the link landed on the
// engine arrays, not the retired W15 statics).
// =============================================================================
extern int32 p6_w_in_ctrlptr;
extern int32 p6_w_in_stickptr;
extern int32 p6_w_in_touchptr;
void p6_input_witness(void)
{
    p6_w_in_ctrlptr  = (int32)(size_t)ControllerInfo;
    p6_w_in_stickptr = (int32)(size_t)AnalogStickInfoL;
    p6_w_in_touchptr = (int32)(size_t)TouchInfo;
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
    // W14b: replay Camera_StageLoad's two gating calls (Camera.c:94-95)
    // through the SAME game-side table slots, right before InitObjects --
    // discriminates a REV-shifted table slot from an engine-state gap.
    p6_w_api_screencount = RSDK.GetVideoSetting(VIDEOSETTING_SCREENCOUNT);
    p6_w_api_credits     = (int32)RSDK.CheckSceneFolder("Credits");
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
    // W14b: SLOT_CAMERA1 classID immediately after InitObjects (Camera_StageLoad
    // ResetEntitySlot'd it during the StageLoad phase, Camera.c:96).
    {
        Entity *cam0 = RSDK.GetEntity(SLOT_CAMERA1);
        p6_w_cam_entclass0 = cam0 ? (int32)cam0->classID : -2;
    }
}

// =============================================================================
// p6_player_witness_tick -- W14: SLOT_PLAYER1 snapshot after the first two
// engine gameplay ticks at GHZ (qa_p6_player P8). The state fn pointer
// proves the verbatim Player state machine dispatched; animator.frames in
// the ANIMPAK window proves the W13 pack feeds ProcessAnimation.
// =============================================================================
void p6_player_witness_tick(void)
{
    if (!Player)
        return;
    EntityPlayer *p1 = RSDK_GET_ENTITY(SLOT_PLAYER1, Player);
    p6_w_plr_tick_x     = p1->position.x;
    p6_w_plr_tick_y     = p1->position.y;
    p6_w_plr_state      = (int32)(size_t)p1->state;
    p6_w_plr_onground   = (int32)p1->onGround;
    p6_w_plr_animframes = (int32)(size_t)p1->animator.frames;
    p6_w_plr_animid     = (int32)p1->animator.animationID;
    p6_w_plr_drawflags  = ((int32)p1->drawGroup << 16) | ((int32)p1->visible << 8)
                          | (int32)p1->onScreen;
    // The frame the Player's draw would blit: GetFrame strides the ENGINE
    // SpriteFrame correctly (the game-side prefix struct must NOT index
    // engine frame arrays directly).
    {
        SpriteFrame *fr = RSDK.GetFrame(Player->sonicFrames,
                                        p1->animator.animationID,
                                        p1->animator.frameID);
        p6_w_plr_sheetid_t = fr ? (int32)fr->sheetID : -1;
    }
    // W14b camera chain (Camera.c:14-44/103-126): Camera_LateUpdate is the
    // ONLY screen->position writer; these tick-time snapshots discriminate
    // "Camera entity never created" (entclass 0) from "created but stateless"
    // (state 0) from "ran but clamped to (0,0)" (scr pos vs cam pos).
    p6_w_cam_static  = (int32)(size_t)Camera;
    p6_w_zone_static = (int32)(size_t)Zone;
    if (Camera && Camera->classID) {
        EntityCamera *cam = RSDK_GET_ENTITY(SLOT_CAMERA1, Camera);
        p6_w_cam_entclass = (int32)cam->classID;
        p6_w_cam_state    = (int32)(size_t)cam->state;
        p6_w_cam_target   = (int32)(size_t)cam->target;
        p6_w_cam_x        = cam->position.x;
        p6_w_cam_y        = cam->position.y;
        p6_w_cam_boundsR  = cam->boundsR;
        p6_w_cam_boundsB  = cam->boundsB;
    }
    if (Zone && Zone->classID) {
        p6_w_zone_boundsR = Zone->cameraBoundsR[0];
        p6_w_zone_boundsB = Zone->cameraBoundsB[0];
    }
    p6_w_scr_x = ScreenInfo->position.x;
    p6_w_scr_y = ScreenInfo->position.y;
}

// =============================================================================
// p6_cont_witness -- P6.8 Step A (Task #211): snapshot SLOT_PLAYER1 into the
// CONTINUOUS witnesses, called every frame from p6_ghz_frame(). Uses the same
// RSDK_GET_ENTITY(SLOT_PLAYER1, Player) path as p6_player_witness_tick (the
// engine headers + class globals live in this TU). Frame count is bumped on
// the p6_io_main side so the witness stays valid even before Player is live.
// =============================================================================
extern int32 p6_w_cont_plr_x;
extern int32 p6_w_cont_plr_y;
extern int32 p6_w_cont_animid;
extern int32 p6_w_plr_live_rings;   // #P0: SLOT_PLAYER1 self->rings (expect 0 at spawn)
extern int32 p6_w_plr_live_shield;  // #P0: SLOT_PLAYER1 self->shield (expect SHIELD_NONE)
void p6_cont_witness(void)
{
    if (!Player)
        return;
    EntityPlayer *p1 = RSDK_GET_ENTITY(SLOT_PLAYER1, Player);
    p6_w_cont_plr_x  = p1->position.x;
    p6_w_cont_plr_y  = p1->position.y;
    p6_w_cont_animid = (int32)p1->animator.animationID;
    p6_w_plr_live_rings  = (int32)p1->rings;   // #P0 spawn-state parity
    p6_w_plr_live_shield = (int32)p1->shield;
}

// =============================================================================
// p6_player_newgame_reset -- #P0 (GHZ1 parity). Called from p6_scene_load_and_arm
// AFTER LoadSceneAssets (the ObjectPlayer static exists) and BEFORE InitObjects
// (Player_Create reads these statics). Player_Create copies Player->rings into the
// new player (Player.c:643) and applies Player->powerups as a shield (Player.c:
// 654-656), THEN zeroes them (:645/:658). At a genuine new-game start those statics
// are 0/0 (ringExtraLife 100); the lean engine boot skips the menu/new-game path
// that establishes that, so the first GHZ player inherits uninitialized static
// memory (MEASURED: 100 rings + a fire shield). Set the new-game values explicitly.
// Witnesses the pre-reset garbage so the gate documents the bug it corrects.
// =============================================================================
extern int32 p6_w_plr_newgame_pre_rings;
extern int32 p6_w_plr_newgame_pre_pwr;
void p6_player_newgame_reset(void)
{
    if (!Player)
        return;
    p6_w_plr_newgame_pre_rings = (int32)Player->rings;
    p6_w_plr_newgame_pre_pwr   = (int32)Player->powerups;
    Player->rings         = 0;
    Player->powerups      = 0;
    Player->ringExtraLife = 100;
}

// =============================================================================
// p6_brg_witness -- #181 ("bridges disappear in level"). The GHZ Bridge object
// was never compiled/registered into the pack; now it is (RSDK_REGISTER_OBJECT
// (Bridge) above, Game_Bridge.o). Only this game-side TU carries the Bridge
// class type + global, so the engine-side p6_ghz_frame calls in here to snapshot
// the gate witnesses (defined in p6_io_main.cpp):
//   p6_w_brg_classid -- Bridge->classID (0 == unregistered == the bug; >0 == B1).
//   p6_w_brg_count   -- # Bridge entities the scene instantiated (B2; GHZ1 = 3).
//   posx/posy        -- first bridge world pos (feeds the P6_WARP_BRIDGE pin).
//   onScreen/frames  -- first bridge onScreen + animator.frames (0/-1 frames ==
//                       Bridge.bin alloc-failed the #247 sprite residency).
// PERF: one-shot LATCH (scene-placed bridges exist from frame 1, count/pos are
// fixed) so the shipping frame pays ONE foreach_all scan total, not per-frame
// (the #246 full-table-scan cost rule). The P6_WARP_BRIDGE diag re-scans every
// frame so onScreen tracks the camera settling onto the warped bridge.
// =============================================================================
extern int32 p6_w_brg_classid;
extern int32 p6_w_brg_count;
extern int32 p6_w_brg_posx;
extern int32 p6_w_brg_posy;
extern int32 p6_w_brg_onscreen;
extern int32 p6_w_brg_frames;
void p6_brg_witness(void)
{
    if (!Bridge || !Bridge->classID)
        return;
    p6_w_brg_classid = (int32)Bridge->classID;

    // Latch count + first-bridge pos ONCE (stable -> the P6_WARP_BRIDGE pin holds a
    // fixed target; the earlier per-frame update made it a moving pin). onScreen
    // refreshes live in the warp diag as the camera settles onto the pinned bridge.
    static int32 s_latched = 0;
    if (!s_latched) {
        int32 cnt = 0;
        EntityBridge *first = NULL;
        foreach_all(Bridge, b)
        {
            ++cnt;
            if (!first)
                first = b;
        }
        if (cnt > 0) {
            p6_w_brg_count    = cnt;
            p6_w_brg_posx     = first->position.x;
            p6_w_brg_posy     = first->position.y;
            p6_w_brg_onscreen = (int32)first->onScreen;
            p6_w_brg_frames   = (int32)(size_t)first->animator.frames;
            s_latched         = 1;
        }
        return;
    }
#if defined(P6_WARP_BRIDGE_TEST)
    foreach_all(Bridge, b) { p6_w_brg_onscreen = (int32)b->onScreen; break; }
#endif
}

// =============================================================================
// p6_loop_witness -- #254 GHZ1 loop closure. One-shot latch (like p6_brg_witness)
// proving the 5 loop classes registered + the scene instantiated them:
//   p6_w_loop_regmask -- bit i set if loop-class[i] has a live classID
//                        (CorkscrewPath|ForceSpin|ForceUnstick|PlaneSwitch|
//                        SpinBooster = bits 0..4; 0x1F == all registered).
//   p6_w_loop_pscount -- # PlaneSwitch entities placed in GHZ1 (expect 106;
//                        0 == the class resolved to a blank slot == still broken).
// PlaneSwitch is the marquee: the collision-plane toggle that the loop needs.
// =============================================================================
extern int32 p6_w_loop_regmask;
extern int32 p6_w_loop_pscount;
void p6_loop_witness(void)
{
    static int32 s_latched = 0;
    if (s_latched)
        return;
    // bit 3 = PlaneSwitch (the loop fix); the corkscrew force-objects are deferred.
    p6_w_loop_regmask = (PlaneSwitch && PlaneSwitch->classID) ? 0x08 : 0;

    if (PlaneSwitch && PlaneSwitch->classID) {
        int32 cnt = 0;
        foreach_all(PlaneSwitch, ps) { ++cnt; }
        if (cnt > 0) {
            p6_w_loop_pscount = cnt;
            s_latched         = 1;
        }
    }
}
