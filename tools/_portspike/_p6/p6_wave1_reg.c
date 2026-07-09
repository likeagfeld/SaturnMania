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
    // O1 step 2: Bridge MOVED to the GHZ overlay (p6_ovl_ghz.c) -- registered there.
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
    // O1 (Task #254): Ring + Spring + Bridge + PlaneSwitch all live in the chain-
    // loaded GHZ zone overlay (p6_ovl_ghz.c, cd/OVLRING.BIN) -- registered there via
    // the api full-callback thunk, NOT here. Moving them out of the resident pack
    // frees _end so the overlay window can grow (the P6.8 way to scale objects past
    // the code wall). The corkscrew force-objects + the rest of the sweep follow into
    // the overlay (O3) after collision relocation (O2). memory/ghz-zone-overlay-o1-design.
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

// O1 step 2 (Task #254): p6_brg_witness + p6_loop_witness MOVED to the GHZ overlay
// (p6_ovl_ghz.c) WITH Bridge + PlaneSwitch -- the resident pack no longer names
// those globals (flat-TU rule). The overlay's combined witness (p6_ghz_ovl_witness)
// writes p6_w_brg_*/p6_w_loop_*/p6_w_spring_* via the ld -R import, called per-tick
// through s_ovl.witness_fn in p6_ghz_frame. Spring witness moved in O1 step 1.

// =============================================================================
// p6_titlecard_atl_restore -- user punch list v2 items 6/7 (camera dead after the
// GHZCutscene->GHZ handoff). VERBATIM EXCERPT of TitleCard_State_ShowTitleCard's
// actionTimer==16 block (TitleCard.c:504-514): the decomp's cutscene detaches the
// camera for manual panning (GHZCutsceneST.c:205 player->camera=NULL) and relies
// on the NEXT stage's TitleCard to re-wire player->camera / camera->target /
// Camera_State_FollowXY and clear Zone->setATLBounds. The Saturn engine build has
// no TitleCard registered, so setATLBounds stayed 1 (MEASURED _v8_land_right.mcs
// Zone+320=1) and Camera_Create skipped its state+bounds init (Camera.c:72-86) ->
// camera state=NULL, screen pinned at x=52 while Sonic ran to x=777.
// Called once from the chain's GHZCutscene->GHZ seam AFTER p6_scene_load_and_arm.
// EXTRA vs the excerpt: the camera's own bounds are seeded from Zone's (the
// Camera_Create branch it WOULD have taken, Camera.c:74-77) because our leftover
// cutscene camera never had valid bounds -- in the decomp the ATL camera KEEPS its
// prior-act bounds (Zone.c:434-443); the cutscene camera here has none.
// FIXME: replace with the verbatim TitleCard port (stage banner + letters); this
// excerpt covers only the ATL camera hand-back the decomp does at TitleCard.c:504.
// =============================================================================
void p6_titlecard_atl_restore(void)
{
    if (!Zone || !Camera || !Player)
        return;
    if (Zone->setATLBounds) {
        EntityCamera *camera = RSDK_GET_ENTITY(SLOT_CAMERA1, Camera);
        EntityPlayer *player = RSDK_GET_ENTITY(SLOT_PLAYER1, Player);
        camera->boundsL      = Zone->cameraBoundsL[0]; // Camera.c:74-77 (skipped
        camera->boundsR      = Zone->cameraBoundsR[0]; // Create branch; see note)
        camera->boundsT      = Zone->cameraBoundsT[0];
        camera->boundsB      = Zone->cameraBoundsB[0];
        player->camera       = camera;                 // TitleCard.c:507
        camera->target       = (Entity *)player;      // TitleCard.c:508
        camera->state        = Camera_State_FollowXY; // TitleCard.c:509
        Camera->centerBounds.x = TO_FIXED(2);          // TitleCard.c:510
        Camera->centerBounds.y = TO_FIXED(2);          // TitleCard.c:511
    }
    Zone->setATLBounds = false;                        // TitleCard.c:514
}

#if defined(P6_GHZCUT_BOOT)
// =============================================================================
// GL1 (2026-07-06): TitleCard act card for the chain GHZ landing.
//
// The frontend_full_chain_parity contract row GL1 (+ scene-6 row): the "GREEN
// HILL ZONE / ACT 1" card must slide in on the GHZCutscene->GHZ arrival. This
// is the CHAIN-NATIVE port -- a self-contained state machine + direct-VDP1
// draw, replacing the FIXME at p6_titlecard_atl_restore above ("replace with
// the verbatim TitleCard port").
//
// SOURCE OF TRUTH: the vertex math below is lifted VERBATIM from the faithful
// Saturn port src/mania/Objects/Global/TitleCard.c (which cites decomp
// SonicMania_Objects_Global_TitleCard.c line-by-line). Only the render calls
// change: instead of the SGL slPutPolygon path (dead under P6_DIRECT_VDP1) the
// draw emits into the direct list via p6_dl_face / p6_dl_rect (p6_vdp1.c).
//
// SATURN-FIT DEVIATIONS (documented, same set the src/mania port already
// carries + one chain-specific):
//   - The card is a POD (no rsdk_entity base): decomp active/visible/drawGroup/
//     scale references are dropped.
//   - Non-Plus: globals->atlEnabled / suppressTitlecard do not exist -> every
//     "!atlEnabled && !suppress" guard is always-taken (the strips/BG path).
//   - Act-number FX_SCALE grow-in omitted (no per-sprite scale in the direct
//     list); the act digit is a glyph anyway = deferred (see below).
//   - CHAIN-SPECIFIC: the decomp SetEngineState(PAUSED) freeze is NOT applied.
//     The chain landing needs ProcessObjects to keep ticking (camera follow +
//     GHZSetup ambient); the card is a pure VDP1 overlay drawn on top. So
//     g_titlecard_active is tracked as a witness only, it does not gate ticks.
//   - ZONE-NAME LETTERS ("GREEN HILL"/"ZONE"/act digit) need Global/TitleCard.
//     gif -> a TITLCARD.SHT banded sheet + seam staging + a glyph draw; that is
//     a measured, separable next increment (parity checklist GL1). THIS
//     iteration draws the colored card (BG bands + 4 diagonal strips + decor
//     boxes + curtains), which is the GL1 primary gate clause.
//
// The colored strips are the visually iconic + gate-checked element; the whole
// slide-in -> show -> slide-away choreography runs so the card animates in and
// out exactly as the decomp times it.
// =============================================================================

// --- witnesses (defined here; p6_io_main.cpp reads via extern for the gate) --
__attribute__((used)) int32 p6_w_tc_state       = -1; // TC_STATE_* (-1 = never spawned)
__attribute__((used)) int32 p6_w_tc_drawstate   = -1; // TC_DRAW_*
__attribute__((used)) int32 p6_w_tc_draw_faces  = 0;  // direct-list poly cmds the card emitted last draw
__attribute__((used)) int32 p6_w_tc_timer       = 0;  // self->timer (slide progress)
__attribute__((used)) int32 p6_w_tc_active      = 0;  // g_titlecard_active mirror

// TO_FIXED is (x)<<16 (GameLink.h:98). Self-contained fixed Vector2.
typedef struct { int32 x, y; } tcv2;

// Non-Plus TitleCard colors (decomp TitleCard.c L678-682, MANIA_USE_PLUS=0).
#define TCC_ORANGE_R 0xF0
#define TCC_ORANGE_G 0x8C
#define TCC_ORANGE_B 0x18
#define TCC_GREEN_R  0x60
#define TCC_GREEN_G  0xC0
#define TCC_GREEN_B  0xA0
#define TCC_RED_R    0xF0
#define TCC_RED_G    0x50
#define TCC_RED_B    0x30
#define TCC_BLUE_R   0x40
#define TCC_BLUE_G   0x60
#define TCC_BLUE_B   0xB0
#define TCC_YELLOW_R 0xF0
#define TCC_YELLOW_G 0xC8
#define TCC_YELLOW_B 0x00

// Screen-relative-pixel direct-list emitters (p6_vdp1.c, P6_DIRECT_VDP1 gated).
extern void p6_dl_face(const int *px, const int *py, int r8, int g8, int b8);
extern void p6_dl_rect(int x, int y, int w, int h, int r8, int g8, int b8);
// GL1 glyph blit: draw a Display.gif rect (from the staged DISPLAY.SHT slot) as a
// VDP1 sprite into the open direct list, using CRAM block `palblk` (glyphs = 2).
extern void p6_dl_glyph(int sheet, int x, int y, int w, int h, int sx, int sy, int palblk);

// GL1 glyph CRAM block: Display.gif GCT is uploaded to block 2 (CRAM[512]) at the
// landing seam (p6_vdp2_titlecard_pal_upload); the glyph blits select colno=512.
#define TCH_GLYPH_PALBLK 2

// --- Glyph frame-rect tables (MEASURED from extracted Global/TitleCard.bin) ----
// Each glyph is a Display.gif source rect (sx,sy,w,h) + a pivot (px,py). A sprite's
// SCREEN top-left = draw-pos + pivot (RSDKv5 DrawSpriteFlipped, Drawing.cpp:2785).
// All frames FLIP_NONE, speed=0 (static; the motion is the slide state math).
typedef struct { short sx, sy, w, h, px, py; } tc_glyph;

// anim 1 "Name Letters": frame index = ASCII-'A' (0..25), space = 26. Used by the
// ALIGN_CENTER DrawText port for the zone name ("GREEN HILL").
static const tc_glyph TC_NAME[27] = {
    {  1,46,16,32,-8,-16},{ 18,46,16,32,-8,-16},{ 35,46,16,32,-8,-16},{ 52,46,16,32,-8,-16}, // A B C D
    { 69,46,16,32,-8,-16},{ 86,46,16,32,-8,-16},{103,46,16,32,-8,-16},{120,46,16,32,-8,-16}, // E F G H
    {137,46, 6,32,-3,-16},{144,46,16,32,-8,-16},{161,46,16,32,-8,-16},{178,46,16,32,-8,-16}, // I J K L
    {195,46,24,32,-12,-16},{220,46,16,32,-8,-16},{  1,79,24,32,-12,-16},{ 26,79,16,32,-8,-16},// M N O P
    { 43,79,24,32,-12,-16},{ 68,79,16,32,-8,-16},{ 85,79,16,32,-8,-16},{102,79,16,32,-8,-16},// Q R S T
    {119,79,16,32,-8,-16},{136,79,20,32,-10,-16},{157,79,24,32,-12,-16},{182,79,16,32,-8,-16},// U V W X
    {199,79,16,32,-8,-16},{216,79,16,32,-8,-16},{  1,46,12, 0,-6,  0},                        // Y Z space
};
// anim 2 "Zone Letters" (Z,O,N,E) -- the pivots spread them horizontally.
static const tc_glyph TC_ZONE[4] = {
    {  1,112,26,18,-117,-10},{ 28,112,26,18,-90,-10},{ 55,112,26,18,-64,-10},{ 82,112,28,18,-37,-10},
};
// anim 3 "Act Numbers" (frameID = actID: 0="1", 1="2", 2="3").
static const tc_glyph TC_ACTNUM[3] = {
    {173,112,16,48,-8,-24},{190,112,32,48,-16,-24},{223,112,32,48,-16,-24},
};

// The single chain TitleCard instance + its state.
static struct {
    int32 state;      // TC_STATE_* (values match src/mania/TitleCard.h enum)
    int32 stateDraw;  // TC_DRAW_*
    int32 actionTimer;
    int32 timer;
    tcv2  decorationPos;
    int32 stripPos[4];
    tcv2  vertMovePos[2];
    tcv2  vertTargetPos[2];
    tcv2  word1DecorVerts[4];
    tcv2  word2DecorVerts[4];
    tcv2  zoneDecorVerts[4];
    tcv2  stripVertsBlue[4];
    tcv2  stripVertsRed[4];
    tcv2  stripVertsOrange[4];
    tcv2  stripVertsGreen[4];
    tcv2  bgLCurtainVerts[4];
    tcv2  bgRCurtainVerts[4];
    int32 zoneCharPos[4];
    int32 zoneCharVel[4];
    int32 zoneXPos;
    tcv2  charPos[20];
    int32 charVel[20];
    int32 titleCardWord2;
    int32 word1Width;
    int32 word2Width;
    int32 word1XPos;
    int32 word2XPos;
    uint8 actID;
    int32 actNumScale;
    tcv2  actNumPos;
    int32 zoneNameLen;   // glyph count (used by the (deferred) letter draw + zone strip width)
    int32 word2NameLen;  // approx width proxy for word2 (in glyphs) -> word2Width
    int32 displaySlot;   // GL1: DISPLAY.SHT SaturnSheet slot (glyph source); -1 = none
    uint8 nameFrame[20]; // GL1: per-char anim-1 frame index (ASCII-'A', space=26)
} s_tc;

// State/draw tags -- MUST match src/mania/Objects/Global/TitleCard.h.
enum { TCH_STATE_SETUPBG=0, TCH_STATE_OPENINGBG, TCH_STATE_ENTERTITLE,
       TCH_STATE_SHOWING, TCH_STATE_SLIDEAWAY, TCH_STATE_SUPRESSED, TCH_STATE_DONE };
enum { TCH_DRAW_NONE=0, TCH_DRAW_SLIDEIN, TCH_DRAW_SHOWCARD, TCH_DRAW_SLIDEAWAY };
#define TCH_ACT_NONE 3

// SGL default-window safe envelope (src/mania/TitleCard.c TC_SAFE_*, doc-cited
// ST-237-R1 clipping): the direct list is NOT perspective-projected (flat 2D
// commands), so unlike the src/mania SGL path the full-screen fills do NOT get
// dropped -- but we keep the fills inside 0..319 x 0..223 so no command
// overhangs the VDP1 display area. Bands cover the true screen 0..319/0..223.
#define TCH_SX  320
#define TCH_SY  224

// tc_face: draw a 4-vertex tcv2[] quad in FIXED screen-relative coords via the
// direct list (>>16 to pixels). Counts the emit for the witness.
static void tc_face(const tcv2 *v, int r8, int g8, int b8)
{
    int px[4], py[4];
    for (int i = 0; i < 4; ++i) { px[i] = v[i].x >> 16; py[i] = v[i].y >> 16; }
    p6_dl_face(px, py, r8, g8, b8);
    ++p6_w_tc_draw_faces;
}

// tc_rect: axis-aligned fill in pixels (decomp DrawRect), clamped to screen.
static void tc_rect(int x, int y, int w, int h, int r8, int g8, int b8)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TCH_SX) w = TCH_SX - x;
    if (y + h > TCH_SY) h = TCH_SY - y;
    if (w <= 0 || h <= 0) return;
    p6_dl_rect(x, y, w, h, r8, g8, b8);
    ++p6_w_tc_draw_faces;
}

// ---- SetupTitleWords (decomp L346-391 via src/mania/TitleCard.c) ------------
// The glyph-font string-width helpers are unavailable in the chain (no atlas),
// so word/zone widths are proxied from the glyph COUNT (each TitleCard glyph is
// ~24 px wide + 24 px pad, matching the src/mania rsdk_get_string_width path
// "+24"). "GREEN HILL" has no word-2 separator -> single word (titleCardWord2=0).
static void tc_setup_title_words(void)
{
    int center_x = ScreenInfo->center.x;

    int32 offset = TO_FIXED(40);
    for (int32 c = 0; c < s_tc.zoneNameLen && c < 20; ++c) {
        s_tc.charPos[c].y = offset;
        s_tc.charVel[c]   = -TO_FIXED(8);
        offset += TO_FIXED(16);
    }
    for (int32 i = 0; i < 4; ++i) {
        s_tc.zoneCharPos[i] = ((2 - s_tc.zoneNameLen) << 19) - ((i * 2) << 19);
        s_tc.zoneCharVel[i] = TO_FIXED(4);
    }

    s_tc.titleCardWord2 = 0; // "GREEN HILL" = single word (no -1/newline glyph)

    // word2Width = GetStringWidth(name) + 24 (decomp L282). GetStringWidth
    // (Animation.cpp:367-379) = sum(frame->width) + (n-1)*spacing, spacing=1.
    // Using the REAL per-glyph widths (TC_NAME) so the ALIGN_CENTER anchor +
    // the black word plate width match the drawn letters exactly.
    {
        int32 sw = 0;
        for (int32 c = 0; c < s_tc.zoneNameLen && c < 20; ++c) {
            int fr = s_tc.nameFrame[c];
            if (fr < 27) {
                sw += TC_NAME[fr].w;
                if (c + 1 < s_tc.zoneNameLen)
                    sw += 1; // spacing
            }
        }
        s_tc.word2Width = TO_FIXED(sw + 24);
    }

    s_tc.zoneXPos  = TO_FIXED(center_x - ((center_x - 160) >> 3) + 72);
    s_tc.word2XPos = TO_FIXED(center_x - ((center_x - 160) >> 3) + 72);
    if (s_tc.word2Width < TO_FIXED(128))
        s_tc.word2XPos -= TO_FIXED(40);
    s_tc.word1XPos = s_tc.word1Width - s_tc.word2Width + s_tc.word2XPos - TO_FIXED(32);
}

// ---- SetupVertices (decomp L244-343 via src/mania/TitleCard.c) --------------
static void tc_setup_vertices(void)
{
    s_tc.vertMovePos[0].x = TO_FIXED(240);
    s_tc.vertMovePos[0].y = TO_FIXED(496);
    s_tc.vertMovePos[1].x = TO_FIXED(752);
    s_tc.vertMovePos[1].y = TO_FIXED(1008);

    s_tc.vertTargetPos[0].x = TO_FIXED(0);
    s_tc.vertTargetPos[0].y = TO_FIXED(138);
    s_tc.vertTargetPos[1].x = TO_FIXED(74);
    s_tc.vertTargetPos[1].y = TO_FIXED(112);

    s_tc.word2DecorVerts[0].x = -s_tc.word2Width; s_tc.word2DecorVerts[0].y = TO_FIXED(186);
    s_tc.word2DecorVerts[1].x = TO_FIXED(0);      s_tc.word2DecorVerts[1].y = TO_FIXED(186);
    s_tc.word2DecorVerts[2].x = TO_FIXED(0);      s_tc.word2DecorVerts[2].y = TO_FIXED(202);
    s_tc.word2DecorVerts[3].x = -s_tc.word2Width; s_tc.word2DecorVerts[3].y = TO_FIXED(202);

    s_tc.zoneDecorVerts[0].x = TO_FIXED(TCH_SX);              s_tc.zoneDecorVerts[0].y = TO_FIXED(154);
    s_tc.zoneDecorVerts[1].x = TO_FIXED(120) + s_tc.zoneDecorVerts[0].x; s_tc.zoneDecorVerts[1].y = TO_FIXED(154);
    s_tc.zoneDecorVerts[2].x = TO_FIXED(120) + s_tc.zoneDecorVerts[0].x; s_tc.zoneDecorVerts[2].y = TO_FIXED(162);
    s_tc.zoneDecorVerts[3].x = s_tc.zoneDecorVerts[0].x;      s_tc.zoneDecorVerts[3].y = TO_FIXED(162);

    s_tc.stripVertsBlue[0].x = s_tc.stripPos[0];                    s_tc.stripVertsBlue[0].y = TO_FIXED(240);
    s_tc.stripVertsBlue[1].x = TO_FIXED(64)  + s_tc.stripVertsBlue[0].x; s_tc.stripVertsBlue[1].y = TO_FIXED(240);
    s_tc.stripVertsBlue[2].x = TO_FIXED(304) + s_tc.stripVertsBlue[0].x; s_tc.stripVertsBlue[2].y = TO_FIXED(240);
    s_tc.stripVertsBlue[3].x = TO_FIXED(240) + s_tc.stripVertsBlue[0].x; s_tc.stripVertsBlue[3].y = TO_FIXED(240);

    s_tc.stripVertsRed[0].x = s_tc.stripPos[1];                    s_tc.stripVertsRed[0].y = TO_FIXED(240);
    s_tc.stripVertsRed[1].x = TO_FIXED(128) + s_tc.stripVertsRed[0].x; s_tc.stripVertsRed[1].y = TO_FIXED(240);
    s_tc.stripVertsRed[2].x = TO_FIXED(230) + s_tc.stripVertsRed[0].x; s_tc.stripVertsRed[2].y = TO_FIXED(240);
    s_tc.stripVertsRed[3].x = TO_FIXED(102) + s_tc.stripVertsRed[0].x; s_tc.stripVertsRed[3].y = TO_FIXED(240);

    s_tc.stripVertsOrange[0].x = s_tc.stripPos[2];                      s_tc.stripVertsOrange[0].y = TO_FIXED(240);
    s_tc.stripVertsOrange[1].x = TO_FIXED(240) + s_tc.stripVertsOrange[0].x; s_tc.stripVertsOrange[1].y = TO_FIXED(240);
    s_tc.stripVertsOrange[2].x = TO_FIXED(262) + s_tc.stripVertsOrange[0].x; s_tc.stripVertsOrange[2].y = TO_FIXED(240);
    s_tc.stripVertsOrange[3].x = TO_FIXED(166) + s_tc.stripVertsOrange[0].x; s_tc.stripVertsOrange[3].y = TO_FIXED(240);

    s_tc.stripVertsGreen[0].x = s_tc.stripPos[3];                     s_tc.stripVertsGreen[0].y = TO_FIXED(240);
    s_tc.stripVertsGreen[1].x = TO_FIXED(32)  + s_tc.stripVertsGreen[0].x; s_tc.stripVertsGreen[1].y = TO_FIXED(240);
    s_tc.stripVertsGreen[2].x = TO_FIXED(160) + s_tc.stripVertsGreen[0].x; s_tc.stripVertsGreen[2].y = TO_FIXED(240);
    s_tc.stripVertsGreen[3].x = TO_FIXED(128) + s_tc.stripVertsGreen[0].x; s_tc.stripVertsGreen[3].y = TO_FIXED(240);

    // Curtains (decomp bgL/RCurtainVerts; outer edges to the true screen so
    // they cover fully; inner edges follow the blue-strip midpoints).
    s_tc.bgLCurtainVerts[0].x = TO_FIXED(0);   s_tc.bgLCurtainVerts[0].y = TO_FIXED(0);
    s_tc.bgLCurtainVerts[1].x = (s_tc.stripVertsBlue[1].x + s_tc.stripVertsBlue[0].x) >> 1; s_tc.bgLCurtainVerts[1].y = TO_FIXED(0);
    s_tc.bgLCurtainVerts[2].x = (s_tc.stripVertsBlue[3].x + s_tc.stripVertsBlue[2].x) >> 1; s_tc.bgLCurtainVerts[2].y = TO_FIXED(TCH_SY);
    s_tc.bgLCurtainVerts[3].x = TO_FIXED(0);   s_tc.bgLCurtainVerts[3].y = TO_FIXED(TCH_SY);

    s_tc.bgRCurtainVerts[0].x = (s_tc.stripVertsBlue[1].x + s_tc.stripVertsBlue[0].x) >> 1; s_tc.bgRCurtainVerts[0].y = TO_FIXED(0);
    s_tc.bgRCurtainVerts[1].x = TO_FIXED(TCH_SX); s_tc.bgRCurtainVerts[1].y = TO_FIXED(0);
    s_tc.bgRCurtainVerts[2].x = TO_FIXED(TCH_SX); s_tc.bgRCurtainVerts[2].y = TO_FIXED(TCH_SY);
    s_tc.bgRCurtainVerts[3].x = (s_tc.stripVertsBlue[3].x + s_tc.stripVertsBlue[2].x) >> 1; s_tc.bgRCurtainVerts[3].y = TO_FIXED(TCH_SY);
}

// ---- HandleWordMovement (decomp L393-433) -----------------------------------
static void tc_handle_word_movement(void)
{
    s_tc.word2DecorVerts[1].x -= TO_FIXED(32);
    if (s_tc.word2DecorVerts[1].x < s_tc.word2XPos - TO_FIXED(16))
        s_tc.word2DecorVerts[1].x = s_tc.word2XPos - TO_FIXED(16);
    s_tc.word2DecorVerts[0].x = s_tc.word2DecorVerts[1].x - s_tc.word2Width;

    s_tc.word2DecorVerts[2].x -= TO_FIXED(32);
    if (s_tc.word2DecorVerts[2].x < s_tc.word2XPos)
        s_tc.word2DecorVerts[2].x = s_tc.word2XPos;
    s_tc.word2DecorVerts[3].x = s_tc.word2DecorVerts[2].x - s_tc.word2Width;

    s_tc.zoneDecorVerts[1].x += TO_FIXED(32);
    if (s_tc.zoneDecorVerts[1].x > s_tc.zoneXPos - TO_FIXED(8))
        s_tc.zoneDecorVerts[1].x = s_tc.zoneXPos - TO_FIXED(8);
    s_tc.zoneDecorVerts[0].x = s_tc.zoneDecorVerts[1].x - TO_FIXED(120);

    s_tc.zoneDecorVerts[2].x += TO_FIXED(32);
    if (s_tc.zoneDecorVerts[2].x > s_tc.zoneXPos)
        s_tc.zoneDecorVerts[2].x = s_tc.zoneXPos;
    s_tc.zoneDecorVerts[3].x = s_tc.zoneDecorVerts[2].x - TO_FIXED(120);

    if (s_tc.decorationPos.y < TO_FIXED(12)) {
        s_tc.decorationPos.x += TO_FIXED(2);
        s_tc.decorationPos.y += TO_FIXED(2);
    }
}

// ---- HandleZoneCharMovement (decomp L435-456) -------------------------------
static void tc_handle_zone_char_movement(void)
{
    for (int32 c = 0; c < s_tc.zoneNameLen && c < 20; ++c) {
        if (s_tc.charPos[c].y < 0)
            s_tc.charVel[c] += 0x28000;
        s_tc.charPos[c].y += s_tc.charVel[c];
        if (s_tc.charPos[c].y > 0 && s_tc.charVel[c] > 0)
            s_tc.charPos[c].y = 0;
    }
    for (int32 i = 0; i < 4; ++i) {
        if (s_tc.zoneCharPos[i] > 0)
            s_tc.zoneCharVel[i] -= 0x14000;
        s_tc.zoneCharPos[i] += s_tc.zoneCharVel[i];
        if (s_tc.zoneCharPos[i] < 0 && s_tc.zoneCharVel[i] < 0)
            s_tc.zoneCharPos[i] = 0;
    }
}

// ---- States (decomp TitleCard_State_*) --------------------------------------
static void tc_state_setup_bg(void)
{
    // (decomp SetEngineState(PAUSED) NOT applied on chain -- see header note.)
    p6_w_tc_active = 1;

    s_tc.timer += 24;
    if (s_tc.timer >= 512) {
        s_tc.word2DecorVerts[0].y -= TO_FIXED(32);
        s_tc.word2DecorVerts[1].y -= TO_FIXED(32);
        s_tc.word2DecorVerts[2].y -= TO_FIXED(32);
        s_tc.word2DecorVerts[3].y -= TO_FIXED(32);
        s_tc.zoneDecorVerts[0].y += TO_FIXED(32);
        s_tc.zoneDecorVerts[1].y += TO_FIXED(32);
        s_tc.zoneDecorVerts[2].y += TO_FIXED(32);
        s_tc.zoneDecorVerts[3].y += TO_FIXED(32);
        s_tc.state = TCH_STATE_OPENINGBG;
    }
    s_tc.word2DecorVerts[0].x += TO_FIXED(40);
    s_tc.word2DecorVerts[1].x += TO_FIXED(40);
    s_tc.word2DecorVerts[2].x += TO_FIXED(40);
    s_tc.word2DecorVerts[3].x += TO_FIXED(40);
    s_tc.zoneDecorVerts[0].x -= TO_FIXED(40);
    s_tc.zoneDecorVerts[1].x -= TO_FIXED(40);
    s_tc.zoneDecorVerts[2].x -= TO_FIXED(40);
    s_tc.zoneDecorVerts[3].x -= TO_FIXED(40);
}

static void tc_state_opening_bg(void)
{
    if (s_tc.timer >= 1024) {
        s_tc.state     = TCH_STATE_ENTERTITLE;
        s_tc.stateDraw = TCH_DRAW_SHOWCARD;
    } else {
        s_tc.timer += 32;
    }
    tc_handle_word_movement();
}

static void tc_state_enter_title(void)
{
    s_tc.vertMovePos[0].x += (s_tc.vertTargetPos[0].x - s_tc.vertMovePos[0].x - TO_FIXED(16)) / 6;
    if (s_tc.vertMovePos[0].x < s_tc.vertTargetPos[0].x) s_tc.vertMovePos[0].x = s_tc.vertTargetPos[0].x;
    s_tc.vertMovePos[0].y += (s_tc.vertTargetPos[0].y - s_tc.vertMovePos[0].y - TO_FIXED(16)) / 6;
    if (s_tc.vertMovePos[0].y < s_tc.vertTargetPos[0].y) s_tc.vertMovePos[0].y = s_tc.vertTargetPos[0].y;
    s_tc.vertMovePos[1].x += (s_tc.vertTargetPos[1].x - s_tc.vertMovePos[1].x - TO_FIXED(16)) / 6;
    if (s_tc.vertMovePos[1].x < s_tc.vertTargetPos[1].x) s_tc.vertMovePos[1].x = s_tc.vertTargetPos[1].x;
    s_tc.vertMovePos[1].y += (s_tc.vertTargetPos[1].y - s_tc.vertMovePos[1].y - TO_FIXED(16)) / 6;
    if (s_tc.vertMovePos[1].y < s_tc.vertTargetPos[1].y) s_tc.vertMovePos[1].y = s_tc.vertTargetPos[1].y;

    s_tc.stripVertsBlue[0].x = (s_tc.vertMovePos[0].x - TO_FIXED(240)) + s_tc.stripVertsBlue[3].x;
    s_tc.stripVertsBlue[0].y = s_tc.vertMovePos[0].x;
    s_tc.stripVertsBlue[1].x = (s_tc.vertMovePos[0].x - TO_FIXED(240)) + s_tc.stripVertsBlue[2].x;
    s_tc.stripVertsBlue[1].y = s_tc.vertMovePos[0].x;

    s_tc.stripVertsRed[0].x = (s_tc.vertMovePos[0].y - TO_FIXED(240)) + s_tc.stripVertsRed[3].x;
    s_tc.stripVertsRed[0].y = s_tc.vertMovePos[0].y;
    s_tc.stripVertsRed[1].x = (s_tc.vertMovePos[0].y - TO_FIXED(240)) + s_tc.stripVertsRed[2].x;
    s_tc.stripVertsRed[1].y = s_tc.vertMovePos[0].y;

    s_tc.stripVertsOrange[0].x = (s_tc.vertMovePos[1].x - TO_FIXED(240)) + s_tc.stripVertsOrange[3].x;
    s_tc.stripVertsOrange[0].y = s_tc.vertMovePos[1].x;
    s_tc.stripVertsOrange[1].x = (s_tc.vertMovePos[1].x - TO_FIXED(240)) + s_tc.stripVertsOrange[2].x;
    s_tc.stripVertsOrange[1].y = s_tc.vertMovePos[1].x;

    s_tc.stripVertsGreen[0].x = (s_tc.vertMovePos[1].y - TO_FIXED(240)) + s_tc.stripVertsGreen[3].x;
    s_tc.stripVertsGreen[0].y = s_tc.vertMovePos[1].y;
    s_tc.stripVertsGreen[1].x = (s_tc.vertMovePos[1].y - TO_FIXED(240)) + s_tc.stripVertsGreen[2].x;
    s_tc.stripVertsGreen[1].y = s_tc.vertMovePos[1].y;

    tc_handle_word_movement();
    tc_handle_zone_char_movement();

    if (s_tc.actNumScale < 0x300)
        s_tc.actNumScale += 0x40;

    if (!s_tc.zoneCharPos[3] && s_tc.zoneCharVel[3] < 0)
        s_tc.state = TCH_STATE_SHOWING;
}

static void tc_state_showing(void)
{
    // decomp TitleCard_State_ShowingTitle (TitleCard.c:504-514): re-wire the
    // camera off the detached cutscene camera + clear Zone->setATLBounds. This
    // is the SAME work p6_titlecard_atl_restore does; run it here at the
    // decomp's actionTimer==16 site so a chain camera that was still detached
    // gets fixed even without the seam call.
    if (s_tc.actionTimer == 16)
        p6_titlecard_atl_restore();

    if (s_tc.actionTimer >= 60) {
        s_tc.actionTimer = 0;
        s_tc.state       = TCH_STATE_SLIDEAWAY;
        s_tc.stateDraw   = TCH_DRAW_SLIDEAWAY;
        p6_w_tc_active   = 0;
    } else {
        s_tc.actionTimer++;
    }
}

static void tc_state_slide_away(void)
{
    int32 speed = ++s_tc.actionTimer << 18;
    for (int i = 0; i < 4; ++i) { s_tc.stripVertsGreen[i].x -= speed; s_tc.stripVertsGreen[i].y -= speed; }

    if (s_tc.actionTimer > 6) {
        speed = (s_tc.actionTimer - 6) << 18;
        for (int i = 0; i < 4; ++i) { s_tc.stripVertsOrange[i].x -= speed; s_tc.stripVertsOrange[i].y -= speed; }
        s_tc.decorationPos.x += speed;
        s_tc.decorationPos.y += speed;
    }
    if (s_tc.actionTimer > 12) {
        speed = (s_tc.actionTimer - 12) << 18;
        for (int i = 0; i < 4; ++i) { s_tc.stripVertsRed[i].x -= speed; s_tc.stripVertsRed[i].y -= speed; }
    }
    if (s_tc.actionTimer > 18) {
        speed = (s_tc.actionTimer - 12) << 18;
        for (int i = 0; i < 4; ++i) { s_tc.stripVertsBlue[i].x -= speed; s_tc.stripVertsBlue[i].y -= speed; }
    }
    if (s_tc.actionTimer > 4) {
        speed = (s_tc.actionTimer - 4) << 17;
        for (int i = 0; i < 4; ++i) s_tc.bgLCurtainVerts[i].x -= speed;
        for (int i = 0; i < 4; ++i) s_tc.bgRCurtainVerts[i].x += speed;
    }
    if (s_tc.actionTimer > 60) {
        speed = TO_FIXED(32);
        s_tc.zoneXPos  -= speed;
        s_tc.word1XPos -= speed;
        s_tc.word2XPos += speed;
        s_tc.actNumPos.x += speed;
        s_tc.actNumPos.y += speed;
        s_tc.word2DecorVerts[0].x += speed; s_tc.word2DecorVerts[1].x += speed;
        s_tc.word2DecorVerts[2].x += speed; s_tc.word2DecorVerts[3].x += speed;
        s_tc.zoneDecorVerts[0].x -= speed;  s_tc.zoneDecorVerts[1].x -= speed;
        s_tc.zoneDecorVerts[2].x -= speed;  s_tc.zoneDecorVerts[3].x -= speed;
    }
    if (s_tc.actionTimer > 80) {
        s_tc.state     = TCH_STATE_DONE;
        s_tc.stateDraw = TCH_DRAW_NONE;
    }
}

// ---- Draw states (decomp TitleCard_Draw_*) ----------------------------------
static void tc_draw_slidein(void)
{
    int cy = ScreenInfo->center.y;
    int sx = TCH_SX;
    int sy = TCH_SY;

    if (s_tc.timer < 256)
        tc_rect(0, 0, sx, sy, 0x00, 0x00, 0x00);

    int height = s_tc.timer;      // Blue
    if (s_tc.timer < 512)
        tc_rect(0, cy - (height >> 1), sx, height, TCC_BLUE_R, TCC_BLUE_G, TCC_BLUE_B);
    height = s_tc.timer - 128;    // Red
    if (s_tc.timer > 128 && s_tc.timer < 640)
        tc_rect(0, cy - (height >> 1), sx, height, TCC_RED_R, TCC_RED_G, TCC_RED_B);
    height = s_tc.timer - 256;    // Orange
    if (s_tc.timer > 256 && s_tc.timer < 768)
        tc_rect(0, cy - (height >> 1), sx, height, TCC_ORANGE_R, TCC_ORANGE_G, TCC_ORANGE_B);
    height = s_tc.timer - 384;    // Green
    if (s_tc.timer > 384 && s_tc.timer < 896)
        tc_rect(0, cy - (height >> 1), sx, height, TCC_GREEN_R, TCC_GREEN_G, TCC_GREEN_B);
    height = s_tc.timer - 512;    // Yellow
    if (s_tc.timer > 512)
        tc_rect(0, cy - (height >> 1), sx, height, TCC_YELLOW_R, TCC_YELLOW_G, TCC_YELLOW_B);

    tc_face(s_tc.word2DecorVerts, 0x00, 0x00, 0x00);
    tc_face(s_tc.zoneDecorVerts, 0xF0, 0xF0, 0xF0);
}

// ---- Glyph draw (decomp TitleCard_Draw_ShowTitleCard L790-835) --------------
// Draws the zone-name LETTERS ("GREEN HILL"), the fixed "ZONE" word, and the act
// digit as VDP1 sprites from the staged DISPLAY.SHT (Global/Display.gif rects),
// routed to CRAM block 2 (Display GCT). Every glyph's screen TOP-LEFT = draw-pos
// + frame pivot (DrawSpriteFlipped, Drawing.cpp:2785). No-op if no display slot.
static void tc_draw_glyphs(void)
{
    if (s_tc.displaySlot < 0)
        return;

    // "ZONE" (decomp L793-799): 4 anim-2 frames, all at x=zoneXPos; the frame
    // pivots (-117/-90/-64/-37) spread Z/O/N/E; y = 186 + zoneCharPos[i] (slide).
    {
        int zx = s_tc.zoneXPos >> 16;
        for (int i = 0; i < 4; ++i) {
            const tc_glyph *g = &TC_ZONE[i];
            int dy = 186 + (s_tc.zoneCharPos[i] >> 16);
            p6_dl_glyph(s_tc.displaySlot, zx + g->px, dy + g->py, g->w, g->h,
                        g->sx, g->sy, TCH_GLYPH_PALBLK);
            ++p6_w_tc_draw_faces;
        }
    }

    // Zone name (decomp L810-813): DrawText(nameLetterAnimator[anim1], word2XPos-20,
    // 154, ALIGN_CENTER, spacing=1, charPos). Replicate DrawString ALIGN_CENTER
    // (Drawing.cpp:4362-4387, charOffsets arm): iterate chars RIGHT->LEFT from the
    // anchor; each char draws at (x - width + charOffset.x, y + pivotY + charOffset.y)
    // then x = (x - width) - spacing. charOffset.x is 0 here (charPos[c].x unused);
    // charPos[c].y is the per-letter drop-in bounce.
    {
        int x = (s_tc.word2XPos - TO_FIXED(20)) >> 16;
        int ybase = 154;
        for (int c = s_tc.zoneNameLen - 1; c >= 0; --c) {
            int fr = s_tc.nameFrame[c];
            if (fr >= 27) continue;
            const tc_glyph *g = &TC_NAME[fr];
            if (g->w > 0 && g->h > 0) {
                int gx = x - g->w;
                int gy = ybase + g->py + (s_tc.charPos[c].y >> 16);
                p6_dl_glyph(s_tc.displaySlot, gx, gy, g->w, g->h,
                            g->sx, g->sy, TCH_GLYPH_PALBLK);
                ++p6_w_tc_draw_faces;
            }
            x = (x - g->w) - 1; // spacing
        }
    }

    // Act digit (decomp L818-832): actID != 3 -> the act-number decoration box
    // (anim0 f0) then the digit (anim3 frameID=actID) at actNumPos. The decomp's
    // FX_SCALE grow-in is omitted (no per-sprite scale in the direct list -- header
    // note); the digit draws at 1:1 once actNumScale has ramped >0.
    if (s_tc.actID != TCH_ACT_NONE && s_tc.actNumScale > 0) {
        int ax = s_tc.actNumPos.x >> 16;
        int ay = s_tc.actNumPos.y >> 16;
        // Decoration box behind the digit (anim0 f0 @ (191,161,64,64) piv(-32,-32)).
        p6_dl_glyph(s_tc.displaySlot, ax - 32, ay - 32, 64, 64, 191, 161, TCH_GLYPH_PALBLK);
        ++p6_w_tc_draw_faces;
        // The digit itself.
        int d = (int)s_tc.actID;
        if (d >= 0 && d < 3) {
            const tc_glyph *g = &TC_ACTNUM[d];
            p6_dl_glyph(s_tc.displaySlot, ax + g->px, ay + g->py, g->w, g->h,
                        g->sx, g->sy, TCH_GLYPH_PALBLK);
            ++p6_w_tc_draw_faces;
        }
    }
}

static void tc_draw_showcard(void)
{
    // Yellow BG (decomp L753-755). Full screen behind the strips.
    tc_rect(0, 0, TCH_SX, TCH_SY, TCC_YELLOW_R, TCC_YELLOW_G, TCC_YELLOW_B);

    if (s_tc.vertMovePos[1].x < TO_FIXED(240)) tc_face(s_tc.stripVertsOrange, TCC_ORANGE_R, TCC_ORANGE_G, TCC_ORANGE_B);
    if (s_tc.vertMovePos[1].y < TO_FIXED(240)) tc_face(s_tc.stripVertsGreen,  TCC_GREEN_R,  TCC_GREEN_G,  TCC_GREEN_B);
    if (s_tc.vertMovePos[0].y < TO_FIXED(240)) tc_face(s_tc.stripVertsRed,    TCC_RED_R,    TCC_RED_G,    TCC_RED_B);
    if (s_tc.vertMovePos[0].x < TO_FIXED(240)) tc_face(s_tc.stripVertsBlue,   TCC_BLUE_R,   TCC_BLUE_G,   TCC_BLUE_B);

    tc_face(s_tc.word2DecorVerts, 0x00, 0x00, 0x00);
    tc_face(s_tc.zoneDecorVerts, 0xF0, 0xF0, 0xF0);
    // Zone name / ZONE word / act digit glyphs ON TOP of the plates (decomp
    // TitleCard_Draw_ShowTitleCard L790-832: the plates are drawn, THEN the
    // zone-name letters + "ZONE" word + act digit are DrawText/DrawSprite'd on
    // top). Re-enabled after the glyph pipeline was verified ready at the hold
    // state (MEASURED live: p6_w_tc_disp_slot=17 resolved, p6_w_tc_pal_bytes=512
    // uploaded, CRAM[512+33]=0xA0C7 real color) -- the prior "#if 0" isolation
    // (magenta bisect) predated the dedicated non-evicting glyph cache +
    // slDMAWait + content-exact staging fixes now in p6_vdp1.c p6_tcglyph_slot,
    // and left p6_w_tcg_n=0 (glyphs never staged) = the empty black/white plates.
    tc_draw_glyphs();
}

static void tc_draw_slideaway(void)
{
    tc_face(s_tc.bgLCurtainVerts, TCC_YELLOW_R, TCC_YELLOW_G, TCC_YELLOW_B);
    tc_face(s_tc.bgRCurtainVerts, TCC_YELLOW_R, TCC_YELLOW_G, TCC_YELLOW_B);

    if (s_tc.vertMovePos[1].x < TO_FIXED(240)) tc_face(s_tc.stripVertsOrange, TCC_ORANGE_R, TCC_ORANGE_G, TCC_ORANGE_B);
    if (s_tc.vertMovePos[1].y < TO_FIXED(240)) tc_face(s_tc.stripVertsGreen,  TCC_GREEN_R,  TCC_GREEN_G,  TCC_GREEN_B);
    if (s_tc.vertMovePos[0].y < TO_FIXED(240)) tc_face(s_tc.stripVertsRed,    TCC_RED_R,    TCC_RED_G,    TCC_RED_B);
    if (s_tc.vertMovePos[0].x < TO_FIXED(240)) tc_face(s_tc.stripVertsBlue,   TCC_BLUE_R,   TCC_BLUE_G,   TCC_BLUE_B);

    tc_face(s_tc.word2DecorVerts, 0x00, 0x00, 0x00);
    tc_face(s_tc.zoneDecorVerts, 0xF0, 0xF0, 0xF0);
    // Glyphs slide out WITH the plates (decomp SlideAway L907-926 keeps drawing
    // ZONE + name; zoneXPos/word2XPos are advanced by tc_state_slide_away).
    tc_draw_glyphs();
}

// ---- Public chain entry points (called from p6_io_main.cpp) -----------------

// Spawn the GHZ act card. zone_name glyph count is derived from strlen; actID
// is TCH_ACT_* (0 = Act 1). display_slot = the SaturnSheet slot of the staged
// DISPLAY.SHT (Global/Display.gif) -- the glyph source; -1 = glyphs skipped.
// Mirrors decomp Create + the src/mania titlecard_spawn (+ SetSpriteString: map
// each name char to its anim-1 frame index, ASCII-'A', space=26).
void p6_titlecard_spawn(const char *zone_name, int actID, int display_slot)
{
    for (unsigned i = 0; i < sizeof(s_tc); ++i) ((uint8 *)&s_tc)[i] = 0;

    s_tc.actID = (uint8)actID;
    if (s_tc.actID > TCH_ACT_NONE) s_tc.actID = TCH_ACT_NONE;
    s_tc.displaySlot = display_slot;

    s_tc.zoneNameLen = 0;
    if (zone_name) {
        while (zone_name[s_tc.zoneNameLen] && s_tc.zoneNameLen < 19) {
            // SetSpriteString char->frame map (anim 1 "Name Letters"): 'A'..'Z'
            // -> 0..25, ' ' -> 26. Any other char clamps to space (blank 12px).
            char ch = zone_name[s_tc.zoneNameLen];
            uint8 fr;
            if (ch >= 'A' && ch <= 'Z')      fr = (uint8)(ch - 'A');
            else if (ch >= 'a' && ch <= 'z') fr = (uint8)(ch - 'a');
            else                             fr = 26; // space / unknown
            s_tc.nameFrame[s_tc.zoneNameLen] = fr;
            ++s_tc.zoneNameLen;
        }
    }

    s_tc.state     = TCH_STATE_SETUPBG;
    s_tc.stateDraw = TCH_DRAW_SLIDEIN;

    int center_x = ScreenInfo->center.x;
    s_tc.stripPos[0] = TO_FIXED(center_x - 152);
    s_tc.stripPos[1] = TO_FIXED(center_x - 152);
    s_tc.stripPos[2] = TO_FIXED(center_x - 160);
    s_tc.stripPos[3] = TO_FIXED(center_x + 20);

    tc_setup_title_words();
    tc_setup_vertices();

    s_tc.decorationPos.y = -TO_FIXED(52);
    s_tc.decorationPos.x = TO_FIXED(TCH_SX - 160);

    s_tc.actNumPos.y = TO_FIXED(168);
    s_tc.actNumPos.x = TO_FIXED(center_x + 106);
    s_tc.actNumScale = -0x400;

    if (s_tc.word2XPos - s_tc.word2Width < TO_FIXED(16)) {
        int32 dist = (s_tc.word2XPos - s_tc.word2Width) - TO_FIXED(16);
        s_tc.word1XPos   -= dist;
        s_tc.zoneXPos    -= dist;
        s_tc.actNumPos.x -= dist;
        s_tc.word2XPos    = s_tc.word2XPos - dist;
    }

    p6_w_tc_active = 1;
    p6_w_tc_state  = s_tc.state;
}

// Advance the card one game tick (state machine). No-op once DONE.
void p6_titlecard_tick(void)
{
    switch (s_tc.state) {
        case TCH_STATE_SETUPBG:    tc_state_setup_bg();    break;
        case TCH_STATE_OPENINGBG:  tc_state_opening_bg();  break;
        case TCH_STATE_ENTERTITLE: tc_state_enter_title(); break;
        case TCH_STATE_SHOWING:    tc_state_showing();     break;
        case TCH_STATE_SLIDEAWAY:  tc_state_slide_away();  break;
        case TCH_STATE_DONE:
        default:                                           break;
    }
    p6_w_tc_state     = s_tc.state;
    p6_w_tc_drawstate = s_tc.stateDraw;
    p6_w_tc_timer     = s_tc.timer;
}

// Draw the card into the CURRENTLY-OPEN direct list (call between p6_dl_begin
// and p6_dl_end). No-op once DONE. Resets the per-draw face count first.
void p6_titlecard_draw(void)
{
    p6_w_tc_draw_faces = 0;
    if (s_tc.state == TCH_STATE_DONE)
        return;
    switch (s_tc.stateDraw) {
        case TCH_DRAW_SLIDEIN:   tc_draw_slidein();   break;
        case TCH_DRAW_SHOWCARD:  tc_draw_showcard();  break;
        case TCH_DRAW_SLIDEAWAY: tc_draw_slideaway(); break;
        case TCH_DRAW_NONE:
        default:                                      break;
    }
}

// Is the card still on-screen (state != DONE)? Lets the seam avoid re-spawning.
int p6_titlecard_is_active(void)
{
    return (s_tc.state != TCH_STATE_DONE && p6_w_tc_state >= 0) ? 1 : 0;
}
#endif /* P6_GHZCUT_BOOT */
