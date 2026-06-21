// =============================================================================
// p6_ovl_ghz.c -- P6.8 O1 (Task #210/#254): the GHZ zone-overlay ENTRY TU.
//
// Extends the proven P6.7d.3 Ring overlay to a MULTI-CLASS entry. O1 moves the
// resident GHZ objects out of the pack INTO the chain-loaded overlay (so pack
// _end stops being the code wall), registering them via the api thunks (flat-TU
// rule: the overlay names no C++ engine symbols -- pointers only) and writing the
// ld -R-imported main witnesses (the Ring p6_w_obj_* pattern).
//   step 1 (db9dfe4): Ring + Spring.
//   step 2 (this):     + Bridge + PlaneSwitch (their p6_brg/loop witnesses MIGRATE
//                      here too -- the resident pack can no longer name the
//                      Bridge/PlaneSwitch globals).
//
// Compiled -x c with the census Game.h/GameLink.h tree (Spring/Bridge/PlaneSwitch
// verbatim globals + callbacks) and WITH -ffunction-sections so ovl_ring.ld places
// .text.p6_overlay_entry FIRST at the window base (the main calls that constant).
// =============================================================================
#include "Game.h"          /* Object*/Entity* + globals + X_* callbacks + foreach_all */
#include "p6_ovl_api.h"

/* Ring is now the VERBATIM Game_Ring (Global/Ring.c) compiled into the overlay --
 * ObjectRing *Ring + Ring_Create/StageLoad/State_Normal/Draw arrive via Game.h
 * (same as Spring/Bridge). The flat p6_ring2 harness is RETIRED (#W18 ring-arm). */

/* Witnesses -- DEFINED in p6_io_main.cpp (main image; gates read them from
 * game.map); written here via the ld -R import. */
extern int32 p6_w_ring_aniframes, p6_w_ring_classid;
extern int32 p6_w_spring_classid, p6_w_spring_frames;
extern int32 p6_w_brg_classid, p6_w_brg_count, p6_w_brg_posx, p6_w_brg_posy,
             p6_w_brg_onscreen, p6_w_brg_frames;
extern int32 p6_w_loop_regmask, p6_w_loop_pscount;
extern int32 p6_w_spikelog_classid, p6_w_spikelog_frames;
/* RANGE-INDEPENDENT anim-load status read straight off the Object struct
 * (<Obj>->aniFrames = LoadSpriteAnimation() result; (int16)-cast so 0xFFFF
 * reads as -1 == load FAILED). Definitive per-object gate signal -- does not
 * depend on a live entity being near the camera. */
extern int32 p6_w_spikelog_aniframes, p6_w_spring_aniframes, p6_w_brg_aniframes;
extern int32 p6_w_spikes_aniframes;
extern int32 p6_w_b1_registered; /* mass-port Batch 1: count of the 4 clean objects with classID>0 */
extern ObjectDecoration *Decoration;
extern ObjectForceSpin *ForceSpin;
extern ObjectSpinBooster *SpinBooster;
/* ForceUnstick deferred to Batch 3 -- its StageLoad loads the 69-frame
 * Global/ItemBox.bin which overflows DATASET_STG by ~1.3KB here (MEASURED:
 * pool 153600, at_fail 152376). It shares ItemBox's anim, so it ports for free
 * alongside the Monitor. */

/* MASS-PORT BATCH 2 (badnik break chain, 2026-06-18). The 9 objects: the 3 CHAIN
 * TUs (Explosion/Animals registered for live classIDs -- BadnikHelpers_BadnikBreak-
 * Unseeded derefs Explosion->sfxDestroy + Animals->animalTypes[]; BadnikHelpers
 * itself is a registered no-op helper class, matching the decomp object list) and
 * the 6 GHZ1 BADNIKs. ALL 9 are OVERLAY-resident (this link): Animals references the
 * overlay's Bridge_HandleCollisions so it cannot live in the pack. The pack->overlay
 * edge (Game_Player.o calls BadnikHelpers_BadnikBreakUnseeded) is bridged by a
 * p6_closure_edge forward stub routed through api->badnikbreak_unseeded_fn below. */
/* All 9 are defined by their overlay-resident TUs (Game_BadnikHelpers/Explosion/
 * Animals/<6 badniks>.o, in this overlay link); Game.h declares the externs. */
extern ObjectBadnikHelpers *BadnikHelpers;
extern ObjectExplosion *Explosion;
extern ObjectAnimals *Animals;
extern ObjectNewtron *Newtron;
extern ObjectCrabmeat *Crabmeat;
extern ObjectBuzzBomber *BuzzBomber;
extern ObjectChopper *Chopper;
extern ObjectMotobug *Motobug;
extern ObjectBatbrain *Batbrain;
/* CP4 FRONT-END KEYSTONE (Task #265/#266): the Logos splash objects. Registered
 * only in the -DP6_FRONTEND_LOGOS overlay flavor (their Game_*.o are linked into
 * the overlay only in that build) -- guarded so the default GHZ overlay does not
 * reference them. LogoSetup_StageLoad loads "Logos/Logos.bin" (the 4-logo sheet)
 * + spawns itself at slot 0; UIPicture draws each logo via DrawSprite->VDP1. */
#if defined(P6_FRONTEND_LOGOS)
extern ObjectLogoSetup *LogoSetup;
extern ObjectUIPicture *UIPicture;
extern int32 p6_w_logosetup_classid, p6_w_uipicture_classid; /* pack witnesses, ld -R */
extern int32 p6_w_uipicture_aniframes, p6_w_uipicture_framesNN; /* CP4b render diag */
/* CP4c BLUE-SCREEN diag: the first live UIPicture entity's full draw-chain state
 * (mirrors the BD_SCAN badnik witness). Written below in the witness fn. */
extern int32 p6_w_uipic_drawgrp, p6_w_uipic_active, p6_w_uipic_visible,
             p6_w_uipic_onscreen, p6_w_uipic_posx, p6_w_uipic_posy,
             p6_w_uipic_animid, p6_w_uipic_frameid, p6_w_uipic_sheetid,
             p6_w_uipic_handle;
#endif
#if defined(P6_FRONTEND_TITLE)
/* CP5a FRONT-END link 2 (Task #267): the Title scene objects. Registered only in
 * the -DP6_FRONTEND_TITLE overlay flavor (their Game_TitleSetup.o/Game_TitleLogo.o
 * link into the overlay only in that build). TitleSetup self-places at slot 0 +
 * drives the State machine; TitleLogo carries the Scene1.bin logo-piece placements
 * (the T4 objcount evidence). Their classID witnesses are pack globals (ld -R). */
extern ObjectTitleSetup *TitleSetup;
extern ObjectTitleLogo  *TitleLogo;
extern int32 p6_w_titlesetup_classid, p6_w_titlelogo_classid, p6_w_title_objcount;
/* CP5b.1 (Task #268) RENDER diag: the first live TitleLogo entity's draw-chain state
 * (mirrors the CP4c p6_w_uipic_* witnesses). handle = the resolved frame's surface ->
 * p6_vdp1_handle_for_surface; >=0 == Title/Logo.gif bound == the logo blit CAN land. */
extern int32 p6_w_tlogo_drawgrp, p6_w_tlogo_visible, p6_w_tlogo_onscreen,
             p6_w_tlogo_type, p6_w_tlogo_sheetid, p6_w_tlogo_handle, p6_w_tlogo_landed;
extern int32 p6_w_tlogo_existmask, p6_w_tlogo_vismask, p6_w_tlogo_onscrmask,
             p6_w_tlogo_boundmask, p6_w_tsetup_statetag;
extern int p6_w_vdp1_landed; /* global VDP1 landed-blit counter (p6_vdp1.c) */
/* CLOSURE EDGE (overlay-resident, flat-TU rule): symbols TitleSetup.c references
 * that no other linked TU provides, all inert at the CP5a capture frame (90, inside
 * State_Wait -- timer 1024 -> -1024 by 16/frame = ~128 frames before the first
 * foreach_all/SetupFX/advance fires). Stubbed here under the flag so they LINK; none
 * is CALLED before the title would auto-advance, so they are runtime-safe for CP5a.
 *   - TitleSonic: foreach_all(TitleSonic) in State_FlashIn -- never deref'd at f90.
 *   - TitleBG_SetupFX: called in State_FlashIn (TitleBG is CP5b).
 *   - TimeAttackData_Clear: TitleSetup_StageLoad (no .c cached); a clear of save
 *     tables that are unused on Saturn -> inert no-op is correct.
 *   - APICallback_ClearPrerollErrors: TitleSetup_StageLoad (API_ClearPrerollErrors
 *     macro -> this under !PLUS/REV02); the removed-at-REV02 preroll-error surface,
 *     a no-op on Saturn. */
ObjectTitleSonic *TitleSonic = NULL;
void TitleBG_SetupFX(void) {}
void TimeAttackData_Clear(void) {}
void APICallback_ClearPrerollErrors(void) {}
/* TitleSetup_State_WaitForEnter (reached only on a button-press, ~256+ frames in --
 * never at the CP5a f90 capture) calls API_ResetInputSlotAssignments ->
 * APICallback_ResetControllerAssignments (the !PLUS/REV02 macro arm, APICallback.h:69).
 * No .c cached -> inert stub. The sibling AssignControllerID/MostRecentActiveControllerID
 * macros it also uses are already stubbed in p6_closure_edge.c. */
void APICallback_ResetControllerAssignments(void) {}
#endif
extern int32 p6_w_b2_registered;       /* count of the 9 chain+badnik objs with classID>0 */
extern int32 p6_w_explosion_aniframes; /* Explosion->aniFrames (load-status latch) */
extern int32 p6_w_animals_aniframes;   /* Animals->aniFrames */
extern int32 p6_w_newtron_aniframes;   /* Newtron->aniFrames */

/* BADNIK-VIS diag (2026-06-18): the overlay names the badnik globals, so the live
 * draw-state scan lives here. Witnesses DEFINED in p6_io_main.cpp (main image). The
 * handle accessor is also there (the pack static table). bd_* latch the first live
 * badnik entity each frame: pos/onScreen/visible/drawGroup/active + animator.frames
 * (NULL?) + the current frame's sheetID + the resolved VDP1 handle for it. */
extern int32 p6_w_bd_found, p6_w_bd_classid, p6_w_bd_posx, p6_w_bd_posy,
             p6_w_bd_onscreen, p6_w_bd_visible, p6_w_bd_drawgrp, p6_w_bd_active,
             p6_w_bd_framesNN, p6_w_bd_animid, p6_w_bd_frameid, p6_w_bd_sheetid,
             p6_w_bd_handle, p6_w_bd_drawn;
extern int32 p6_vdp1_handle_for_surface(int32 sheetID);

/* Forward decl so p6_overlay_entry is the FIRST function (window base). */
static void p6_ghz_ovl_witness(const void *ringSlot);

/* I3b 2b: the camera-local-pool MATERIALIZE, overlay-resident (new engine code -> cart per the
 * residency rule, freeing WRAM-H for the shrink manager). Forward-declared so p6_overlay_entry stays
 * first; defined at file end. The ENGINE-touching ops are thin extern "C" PACK thunks (the overlay
 * can't name C++-mangled engine syms -- flat-TU rule; ld -R game.elf resolves these). The overlay
 * does the DORM navigation + LE var-replay (raw offset writes -- no engine types). */
static void p6_ovl_materialize(unsigned logical_slot, unsigned dest_slot);
extern int32 p6_eng_classid_resolve(const unsigned char *objhash_le); /* -> classID (0=unregistered) */
extern void  p6_eng_serialize_begin(int32 classID);                   /* rebuild editableVarList (cart scratch) */
extern int32 p6_eng_var_offset(const unsigned char *varhash_le);      /* var-hash -> field offset (-1=none) */
extern void  p6_eng_serialize_end(void);                              /* restore editableVarList */
extern void *p6_eng_entity_prepare(int32 slot);                       /* RSDK_ENTITY_AT(slot) + memset */
extern void  p6_eng_write_placement(void *ent, int32 classID, int32 px, int32 py);
/* the materialize witnesses are PACK globals (extern "C"); the overlay writes them via ld -R game.elf */
extern int32 p6_w_mat_slot, p6_w_mat_classid, p6_w_mat_nvars, p6_w_mat_nmatch;
extern int32 p6_w_mat_posx, p6_w_mat_posy, p6_w_mat_v0, p6_w_mat_v1, p6_w_mat_v2, p6_w_mat_v3;
/* I3b 2b COMPACTION (overlay-resident per the residency rule -- the pack-placed version overflowed
 * WRAM-H/ANIMPAK by 80 B). Relocates every populated scene entity into a dense physical pool via the
 * non-identity remap (byte-plan proven offline by qa_p6_pool_compact_model). Pure byte-math with the
 * pool GEOMETRY from the pack (p6_eng_pool_geom -- no struct/define hardcode); the only engine-touching
 * op is the atomic scene_phys flip (p6_eng_pool_flip). Forward-declared so p6_overlay_entry stays first. */
static void p6_ovl_pool_compact(void);
extern void p6_eng_pool_geom(int32 *out);              /* {classID off, NARROW, R, SCN, TEMP, WIDE, SCENE_PHYS} */
extern void p6_eng_pool_flip(int32 sphys, int32 dummy);/* atomic flip of the RSDK pool ints (LAST) */
extern unsigned char p6_scan_near[];                   /* WRAM-H near bitfield (pack; per-frame near-set) */
extern int32 p6_w_compact_n, p6_w_compact_sphys, p6_w_compact_dummy;
extern int32 p6_w_compact_bij_ok, p6_w_compact_lastL, p6_w_compact_lastP;
/* I3b 2b STREAMING (per-frame manager). */
static void p6_ovl_stream(void);
extern void p6_eng_create(int32 slot);                 /* re-Create a materialized entity (InitObjects mirror) */
extern int32 p6_w_stream_mat, p6_w_stream_dorm, p6_w_stream_free, p6_w_stream_resident, p6_w_stream_starve;
extern int32 p6_w_bt_logical, p6_w_bt_cid, p6_w_bt_life, p6_w_bt_reappear; /* I3b 2b backtrack-proof witnesses */
extern int32 p6_w_pool_inv_bad; /* I3b 2b: sticky free-list-invariant violation latch (resident+free!=SP-1) */
/* cart structures -- the verified-free [0x226BC980,0x226C0000) gap (inv-end -> shadow buffer). */
#define P6_STREAM_FREELIST 0x226BD000u  /* unsigned short[SCENE_PHYS] -- free physical-slot stack */
#define P6_STREAM_FREECNT  0x226BD600u  /* int -- free-list count */
#define P6_STREAM_LIFE     0x226BD700u  /* unsigned char[(SCENEENTITY_COUNT+7)/8] -- destroyed (lifecycle) bits */
/* I3b 2b PERF #2 (scan narrowing): the resident-list -- logical slots currently holding a physical slot.
   The per-frame DORMANT/RETIRE pass iterates THIS ~42-entry list; the MATERIALIZE pass byte-scans the
   existing WRAM-H p6_scan_near bitfield (zero new pack code -> no WRAM-H growth, CART-only). Verified-free
   hole 0x226BDE00 (after the 136-B life bitfield), u16[SCENE_PHYS=640]=1280 B + count, < shadow 0x226C0000. */
#define P6_STREAM_RESIDLIST  0x226BDE00u  /* unsigned short[SCENE_PHYS] -- resident logical slots */
#define P6_STREAM_RESIDCNT   0x226BE300u  /* int -- resident-list count */

// =============================================================================
// p6_overlay_entry -- MUST be first (window base). Registers the overlay's
// classes through the api thunks; the engine LoadGameConfig hash loop matches
// each by md5(name), so classID assignment is order-independent (P6.7d.3).
// =============================================================================
int p6_overlay_entry(p6_ovl_api *api)
{
    /* Ring -- verbatim FULL-callback form (the real Game_Ring): Ring_StageLoad loads
     * Global/Ring.bin into DATASET_STG + Ring_Create arms each placed ring's animator
     * (ACTIVE_BOUNDS, Ring_State_Normal) so rings RENDER (#W18 ring-arm gap). */
    api->register_object_full((void **)&Ring, "Ring",
                              (unsigned)sizeof(EntityRing), (unsigned)sizeof(ObjectRing),
                              Ring_Update, Ring_LateUpdate, Ring_StaticUpdate,
                              Ring_Draw, Ring_Create, Ring_StageLoad, Ring_Serialize);

    /* Spring / Bridge / PlaneSwitch -- verbatim FULL-callback form (NULL editor
     * matches the resident RSDK_REGISTER_OBJECT REV02/non-REV0U arm, GameLink.h
     * :1799: these objects define no _EditorLoad/_EditorDraw). */
    api->register_object_full((void **)&Spring, "Spring",
                              (unsigned)sizeof(EntitySpring), (unsigned)sizeof(ObjectSpring),
                              Spring_Update, Spring_LateUpdate, Spring_StaticUpdate,
                              Spring_Draw, Spring_Create, Spring_StageLoad, Spring_Serialize);
    api->register_object_full((void **)&Bridge, "Bridge",
                              (unsigned)sizeof(EntityBridge), (unsigned)sizeof(ObjectBridge),
                              Bridge_Update, Bridge_LateUpdate, Bridge_StaticUpdate,
                              Bridge_Draw, Bridge_Create, Bridge_StageLoad, Bridge_Serialize);
    api->register_object_full((void **)&PlaneSwitch, "PlaneSwitch",
                              (unsigned)sizeof(EntityPlaneSwitch), (unsigned)sizeof(ObjectPlaneSwitch),
                              PlaneSwitch_Update, PlaneSwitch_LateUpdate, PlaneSwitch_StaticUpdate,
                              PlaneSwitch_Draw, PlaneSwitch_Create, PlaneSwitch_StageLoad,
                              PlaneSwitch_Serialize);
    /* O3 step 1: SpikeLog (the rolling GHZ spike log, 1023 B -- fits the existing
     * window, no re-budget). Shares GHZ/Objects.gif (GHZOBJ.SHT, already staged). */
    api->register_object_full((void **)&SpikeLog, "SpikeLog",
                              (unsigned)sizeof(EntitySpikeLog), (unsigned)sizeof(ObjectSpikeLog),
                              SpikeLog_Update, SpikeLog_LateUpdate, SpikeLog_StaticUpdate,
                              SpikeLog_Draw, SpikeLog_Create, SpikeLog_StageLoad, SpikeLog_Serialize);
    /* Spikes (Global hazard, 41 GHZ1 placements): verbatim Game_Spikes. Closure
     * fully resolved -- Ice/Shield/MathHelpers/Player are ported TUs; Press is the
     * lone GHZ1-dead NULL (p6_closure_edge.c). Loads Global/Spikes.bin + the Global
     * Spikes sheet (census = staged). No editor callbacks (GAME_INCLUDE_EDITOR off). */
    api->register_object_full((void **)&Spikes, "Spikes",
                              (unsigned)sizeof(EntitySpikes), (unsigned)sizeof(ObjectSpikes),
                              Spikes_Update, Spikes_LateUpdate, Spikes_StaticUpdate,
                              Spikes_Draw, Spikes_Create, Spikes_StageLoad, Spikes_Serialize);
#if defined(P6_FRONTEND_LOGOS)
    /* CP4: the Logos splash objects. Register order is irrelevant (the engine
     * LoadGameConfig matches by md5(name)); these resolve their classIDs the same
     * registration-time way Spring/Ring do. NULL editor callbacks (GAME_INCLUDE_
     * EDITOR off) -- matches the verbatim RSDK_REGISTER_OBJECT non-editor arm. */
    api->register_object_full((void **)&LogoSetup, "LogoSetup",
                              (unsigned)sizeof(EntityLogoSetup), (unsigned)sizeof(ObjectLogoSetup),
                              LogoSetup_Update, LogoSetup_LateUpdate, LogoSetup_StaticUpdate,
                              LogoSetup_Draw, LogoSetup_Create, LogoSetup_StageLoad, LogoSetup_Serialize);
    api->register_object_full((void **)&UIPicture, "UIPicture",
                              (unsigned)sizeof(EntityUIPicture), (unsigned)sizeof(ObjectUIPicture),
                              UIPicture_Update, UIPicture_LateUpdate, UIPicture_StaticUpdate,
                              UIPicture_Draw, UIPicture_Create, UIPicture_StageLoad, UIPicture_Serialize);
#endif
#if defined(P6_FRONTEND_TITLE)
    /* CP5a: the Title scene objects. Register order is irrelevant (the engine
     * LoadGameConfig matches by md5(name)); these resolve their classIDs the same
     * registration-time way Ring/LogoSetup do. NULL editor callbacks (matches the
     * verbatim RSDK_REGISTER_OBJECT non-editor arm). TitleSetup_StageLoad runs
     * RSDK.ResetEntitySlot(0,...) to self-place at slot 0 + loads Title/Electricity.bin;
     * TitleLogo_StageLoad loads Title/Logo.bin and TitleLogo carries the placements. */
    api->register_object_full((void **)&TitleSetup, "TitleSetup",
                              (unsigned)sizeof(EntityTitleSetup), (unsigned)sizeof(ObjectTitleSetup),
                              TitleSetup_Update, TitleSetup_LateUpdate, TitleSetup_StaticUpdate,
                              TitleSetup_Draw, TitleSetup_Create, TitleSetup_StageLoad, TitleSetup_Serialize);
    api->register_object_full((void **)&TitleLogo, "TitleLogo",
                              (unsigned)sizeof(EntityTitleLogo), (unsigned)sizeof(ObjectTitleLogo),
                              TitleLogo_Update, TitleLogo_LateUpdate, TitleLogo_StaticUpdate,
                              TitleLogo_Draw, TitleLogo_Create, TitleLogo_StageLoad, TitleLogo_Serialize);
#endif
    /* MASS-PORT BATCH 1 (verified-CLEAN drop-ins; closure self-confirmed: only
     * Zone/Player/SceneInfo/DebugMode/Zone_RotateOnPivot, all ported). Decoration =
     * GHZ scenery; ForceSpin/ForceUnstick/SpinBooster = player-state trigger regions
     * (reuse PlaneSwitch/ItemBox anims, already staged). */
    api->register_object_full((void **)&Decoration, "Decoration",
                              (unsigned)sizeof(EntityDecoration), (unsigned)sizeof(ObjectDecoration),
                              Decoration_Update, Decoration_LateUpdate, Decoration_StaticUpdate,
                              Decoration_Draw, Decoration_Create, Decoration_StageLoad, Decoration_Serialize);
    api->register_object_full((void **)&ForceSpin, "ForceSpin",
                              (unsigned)sizeof(EntityForceSpin), (unsigned)sizeof(ObjectForceSpin),
                              ForceSpin_Update, ForceSpin_LateUpdate, ForceSpin_StaticUpdate,
                              ForceSpin_Draw, ForceSpin_Create, ForceSpin_StageLoad, ForceSpin_Serialize);
    api->register_object_full((void **)&SpinBooster, "SpinBooster",
                              (unsigned)sizeof(EntitySpinBooster), (unsigned)sizeof(ObjectSpinBooster),
                              SpinBooster_Update, SpinBooster_LateUpdate, SpinBooster_StaticUpdate,
                              SpinBooster_Draw, SpinBooster_Create, SpinBooster_StageLoad, SpinBooster_Serialize);

    /* MASS-PORT BATCH 2 -- the badnik break CHAIN. Register order is irrelevant
     * (engine matches by md5(name)). CHAIN TUs first: BadnikHelpers (no-op helper
     * class), Explosion (StageLoad loads Global/Explosions.bin + the Destroy.wav
     * sfxDestroy -- needs a live classID for the CREATE_ENTITY in the break path),
     * Animals (StageLoad loads Global/Animals.bin -- needs a live classID for the
     * spawnAnimals=TRUE CREATE_ENTITY + animalTypes[] deref). Then the 6 GHZ1
     * badniks. ALL closures verified (adversarial harvest): Player_CheckBadnikTouch
     * + Player_CheckBadnikBreak + Player_ProjectileHurt (pack, -u rooted),
     * Zone/DebugMode/SceneInfo (ported), APICallback_TrackEnemyDefeat (inert stub
     * added), Water/Platform/Press (GHZ1-dead NULLs), Bridge_HandleCollisions
     * (Game_Bridge.o, overlay-internal). Anims slow-path into DATASET_STG
     * (+9.8 KB MEASURED; pool grown 150 to 166 KB). */
    api->register_object_full((void **)&BadnikHelpers, "BadnikHelpers",
                              (unsigned)sizeof(EntityBadnikHelpers), (unsigned)sizeof(ObjectBadnikHelpers),
                              BadnikHelpers_Update, BadnikHelpers_LateUpdate, BadnikHelpers_StaticUpdate,
                              BadnikHelpers_Draw, BadnikHelpers_Create, BadnikHelpers_StageLoad, BadnikHelpers_Serialize);
    api->register_object_full((void **)&Explosion, "Explosion",
                              (unsigned)sizeof(EntityExplosion), (unsigned)sizeof(ObjectExplosion),
                              Explosion_Update, Explosion_LateUpdate, Explosion_StaticUpdate,
                              Explosion_Draw, Explosion_Create, Explosion_StageLoad, Explosion_Serialize);
    api->register_object_full((void **)&Animals, "Animals",
                              (unsigned)sizeof(EntityAnimals), (unsigned)sizeof(ObjectAnimals),
                              Animals_Update, Animals_LateUpdate, Animals_StaticUpdate,
                              Animals_Draw, Animals_Create, Animals_StageLoad, Animals_Serialize);
    api->register_object_full((void **)&Newtron, "Newtron",
                              (unsigned)sizeof(EntityNewtron), (unsigned)sizeof(ObjectNewtron),
                              Newtron_Update, Newtron_LateUpdate, Newtron_StaticUpdate,
                              Newtron_Draw, Newtron_Create, Newtron_StageLoad, Newtron_Serialize);
    api->register_object_full((void **)&Crabmeat, "Crabmeat",
                              (unsigned)sizeof(EntityCrabmeat), (unsigned)sizeof(ObjectCrabmeat),
                              Crabmeat_Update, Crabmeat_LateUpdate, Crabmeat_StaticUpdate,
                              Crabmeat_Draw, Crabmeat_Create, Crabmeat_StageLoad, Crabmeat_Serialize);
    api->register_object_full((void **)&BuzzBomber, "BuzzBomber",
                              (unsigned)sizeof(EntityBuzzBomber), (unsigned)sizeof(ObjectBuzzBomber),
                              BuzzBomber_Update, BuzzBomber_LateUpdate, BuzzBomber_StaticUpdate,
                              BuzzBomber_Draw, BuzzBomber_Create, BuzzBomber_StageLoad, BuzzBomber_Serialize);
    api->register_object_full((void **)&Chopper, "Chopper",
                              (unsigned)sizeof(EntityChopper), (unsigned)sizeof(ObjectChopper),
                              Chopper_Update, Chopper_LateUpdate, Chopper_StaticUpdate,
                              Chopper_Draw, Chopper_Create, Chopper_StageLoad, Chopper_Serialize);
    api->register_object_full((void **)&Motobug, "Motobug",
                              (unsigned)sizeof(EntityMotobug), (unsigned)sizeof(ObjectMotobug),
                              Motobug_Update, Motobug_LateUpdate, Motobug_StaticUpdate,
                              Motobug_Draw, Motobug_Create, Motobug_StageLoad, Motobug_Serialize);
    api->register_object_full((void **)&Batbrain, "Batbrain",
                              (unsigned)sizeof(EntityBatbrain), (unsigned)sizeof(ObjectBatbrain),
                              Batbrain_Update, Batbrain_LateUpdate, Batbrain_StaticUpdate,
                              Batbrain_Draw, Batbrain_Create, Batbrain_StageLoad, Batbrain_Serialize);

    /* Ring vtable; witness_fn is the COMBINED tick witness below. staticvars_slot
     * feeds the F.3 main-image Ring-global rewire (p6_io_main: Ring = *staticvars_slot
     * so SignPost's CREATE_ENTITY(Ring) resolves). arm_fn=0: the real Ring_Create arms
     * placed rings now (the p6_io_main:1767 manual-proof guard skips on NULL). */
    api->staticvars_slot = (void *)&Ring;
    api->entity_size     = (unsigned)sizeof(EntityRing);
    api->arm_fn          = 0;
    api->witness_fn      = (void (*)(const void *))p6_ghz_ovl_witness;
    api->update_fn       = (void *)Ring_Update;
    /* #258b: export the overlay's REAL hurt-ring-scatter entry points so the
     * pack-side Player (which calls Ring_LoseRings on hurt) reaches them via the
     * p6_closure_edge forward instead of the pack stub. */
    api->loserings_fn      = (void *)Ring_LoseRings;
    api->losehyperrings_fn = (void *)Ring_LoseHyperRings;
    /* BATCH 2: export the overlay's REAL BadnikHelpers break fns. Game_Player.o
     * (PACK) calls BadnikHelpers_BadnikBreakUnseeded on every badnik kill; the
     * p6_closure_edge stub forwards pack->overlay to these (the chain TUs are
     * overlay-resident because Animals refs the overlay's Bridge_HandleCollisions). */
    api->badnikbreak_unseeded_fn = (void *)BadnikHelpers_BadnikBreakUnseeded;
    api->badnikbreak_fn          = (void *)BadnikHelpers_BadnikBreak;
    /* BATCH 2: expose &Animals (the overlay's registered Animals object**) so the
     * pack's NULL Animals placeholder (ActClear.c:903 foreach_active) gets rewired
     * to the live object each frame (p6_io_main, the #235 Ring-seam). */
    api->animals_slot = (void *)&Animals;
    /* I3b 2b: the pack drives the materialize one-shot at load (s_ovl.materialize_fn). */
    api->materialize_fn = p6_ovl_materialize;
    /* I3b 2b: the pack drives the COMPACTION one-shot at load (s_ovl.compact_fn) -- relocates all
     * populated scene entities into a dense physical pool (overlay-resident per the residency rule). */
    api->compact_fn = p6_ovl_pool_compact;
    /* I3b 2b: the per-frame STREAMING manager (s_ovl.stream_fn) -- materialize newly-near + dormant
     * newly-far, the camera-local pool's live half. Called from ProcessObjects via p6_stream_tick. */
    api->stream_fn = p6_ovl_stream;
    return 0;
}

// =============================================================================
// Combined per-tick witness (api->witness_fn, called from p6_ghz_frame's
// shipping loop): Ring residency + the Spring/Bridge/PlaneSwitch latches (each
// MIGRATED here from p6_wave1_reg.c -- the resident pack no longer names these
// globals). One-shot latches; gates read p6_w_*.
// =============================================================================
static void p6_ghz_ovl_witness(const void *ringSlot)
{
    (void)ringSlot; /* p6_ring2 harness retired; Ring witnesses are aniFrames-based now */

    /* Ring (#W18 ring-arm): aniFrames>=0 == Ring_StageLoad's LoadSpriteAnimation
     * succeeded (rings can arm + render); classid live == registered + instantiated.
     * (int16)-cast so a -1 (0xFFFF) load failure reads as -1, not 65535. */
    if (Ring) p6_w_ring_aniframes = (int32)(int16)Ring->aniFrames;
    if (Ring && Ring->classID) p6_w_ring_classid = (int32)Ring->classID;
#if defined(P6_FRONTEND_LOGOS)
    /* CP4 (E2/E3): latch the front-end classIDs once they resolve. Called from
     * p6_frontend_frame each tick (the api->witness_fn seam). */
    if (LogoSetup && LogoSetup->classID) p6_w_logosetup_classid = (int32)LogoSetup->classID;
    if (UIPicture && UIPicture->classID) p6_w_uipicture_classid = (int32)UIPicture->classID;
    /* CP4b render diag: did UIPicture's Logos.bin animation load + does a live
     * UIPicture entity have a frame table? (int16-cast so -1 reads as -1.) */
    if (UIPicture) p6_w_uipicture_aniframes = (int32)(int16)UIPicture->aniFrames;
    {
        EntityUIPicture *up = NULL;
        foreach_all(UIPicture, e) { up = e; break; }
        if (up) p6_w_uipicture_framesNN = (up->animator.frames != NULL) ? 1 : 0;
    }
    /* CP4c BLUE-SCREEN diag: latch the first live UIPicture entity's full draw-chain
     * state -- the exact links p6_vdp1_blit needs (mirrors BD_SCAN, :456). Prefer an
     * on-screen entity. The resolved frame's sheetID -> p6_vdp1_handle_for_surface
     * is the load-bearing one: handle<0 == Logos surface UNBOUND == the blit drops. */
    if (UIPicture && UIPicture->classID) {
        foreach_all(UIPicture, up2) {
            int32 onscr = (int32)up2->onScreen;
            if (p6_w_uipic_drawgrp < 0 || (onscr && p6_w_uipic_onscreen <= 0)) {
                p6_w_uipic_drawgrp  = (int32)up2->drawGroup;
                p6_w_uipic_active   = (int32)up2->active;
                p6_w_uipic_visible  = (int32)up2->visible;
                p6_w_uipic_onscreen = onscr;
                p6_w_uipic_posx     = (int32)(up2->position.x >> 16);
                p6_w_uipic_posy     = (int32)(up2->position.y >> 16);
                p6_w_uipic_animid   = (int32)up2->animator.animationID;
                p6_w_uipic_frameid  = (int32)up2->animator.frameID;
                SpriteFrame *ufr = RSDK.GetFrame(UIPicture->aniFrames,
                    up2->animator.animationID, up2->animator.frameID);
                p6_w_uipic_sheetid  = ufr ? (int32)ufr->sheetID : -1;
                p6_w_uipic_handle   = ufr ? p6_vdp1_handle_for_surface(ufr->sheetID) : -4;
            }
        }
    }
#endif
#if defined(P6_FRONTEND_TITLE)
    /* CP5a (T2/T3): latch the Title classIDs once they resolve. Called from
     * p6_frontend_frame each tick (the api->witness_fn seam). T4 (objcount) is
     * latched in InitObjects (p6_io_main); T1/T5 are set in p6_title_reload /
     * p6_frontend_frame. */
    if (TitleSetup && TitleSetup->classID) p6_w_titlesetup_classid = (int32)TitleSetup->classID;
    if (TitleLogo  && TitleLogo->classID)  p6_w_titlelogo_classid  = (int32)TitleLogo->classID;
    /* CP5b.1 (Task #268) RENDER diag: latch the first live VISIBLE TitleLogo entity's
     * draw-chain state -- the exact links p6_vdp1_blit needs (mirrors the CP4c
     * p6_w_uipic_* block). Prefer a visible+on-screen piece (the emblem/ribbon/
     * gametitle become visible at AnimateUntilFlash/FlashIn). The resolved frame's
     * sheetID -> p6_vdp1_handle_for_surface is the load-bearing link: handle<0 ==
     * Title/Logo.gif UNBOUND == the blit drops (the CP5a RED). landed = the global
     * VDP1 landed-blit count snapshot (Title is sprite-only -> corroborates the logo
     * reached the framebuffer; the screenshot + pixel measure are the PRIMARY proof). */
    p6_w_tlogo_landed = p6_w_vdp1_landed;
    if (TitleLogo && TitleLogo->classID) {
        foreach_all(TitleLogo, tl2) {
            int32 onscr = (int32)tl2->onScreen;
            int32 vis   = (int32)tl2->visible;
            /* take the first piece, then UPGRADE to a visible+on-screen one if found */
            if (p6_w_tlogo_drawgrp < -1
                || (vis && onscr && !(p6_w_tlogo_visible > 0 && p6_w_tlogo_onscreen > 0))) {
                p6_w_tlogo_drawgrp  = (int32)tl2->drawGroup;
                p6_w_tlogo_visible  = vis;
                p6_w_tlogo_onscreen = onscr;
                p6_w_tlogo_type     = (int32)tl2->type;
                SpriteFrame *tfr = RSDK.GetFrame(TitleLogo->aniFrames,
                    tl2->mainAnimator.animationID, tl2->mainAnimator.frameID);
                p6_w_tlogo_sheetid  = tfr ? (int32)tfr->sheetID : -1;
                p6_w_tlogo_handle   = tfr ? p6_vdp1_handle_for_surface(tfr->sheetID) : -4;
            }
        }
        /* CP5b.1 per-TYPE diag: which logo pieces are visible / on-screen / bound.
         * Rebuilt every tick (a snapshot, not a latch) so the capture reflects the
         * title's CURRENT state. bit T = type T (0=EMBLEM,1=RIBBON,2=GAMETITLE,
         * 3=POWERLED,4=COPYRIGHT,5=RINGBOTTOM,6=PRESSSTART). */
        int32 em = 0, vm = 0, om = 0, bm = 0;
        foreach_all(TitleLogo, tl3) {
            int32 t = (int32)tl3->type;
            if (t < 0 || t > 15) continue;
            int32 bit = 1 << t;
            em |= bit;
            if (tl3->visible) {
                vm |= bit;
                if (tl3->onScreen) {
                    om |= bit;
                    SpriteFrame *fr = RSDK.GetFrame(TitleLogo->aniFrames,
                        tl3->mainAnimator.animationID, tl3->mainAnimator.frameID);
                    if (fr && p6_vdp1_handle_for_surface(fr->sheetID) >= 0)
                        bm |= bit;
                }
            }
        }
        p6_w_tlogo_existmask = em;
        p6_w_tlogo_vismask   = vm;
        p6_w_tlogo_onscrmask = om;
        p6_w_tlogo_boundmask = bm;
    }
    /* which TitleSetup state: tag = a small int per state fn (compared by ptr). The
     * state field lives on the ENTITY (slot 0, where TitleSetup_StageLoad self-placed
     * it via ResetEntitySlot(0,...)), not the Object. RSDK_GET_ENTITY_GEN(0) gives it. */
    {
        Entity *e0 = RSDK_GET_ENTITY_GEN(0);
        EntityTitleSetup *ts = (EntityTitleSetup *)e0;
        if (e0 && TitleSetup && e0->classID == TitleSetup->classID) {
            void *st = (void *)ts->state;
            extern void TitleSetup_State_Wait(void);
            extern void TitleSetup_State_AnimateUntilFlash(void);
            extern void TitleSetup_State_FlashIn(void);
            extern void TitleSetup_State_WaitForSonic(void);
            extern void TitleSetup_State_SetupLogo(void);
            extern void TitleSetup_State_WaitForEnter(void);
            extern void TitleSetup_State_FadeToMenu(void);
            extern void TitleSetup_State_FadeToVideo(void);
            p6_w_tsetup_statetag =
                st == (void *)TitleSetup_State_Wait              ? 1 :
                st == (void *)TitleSetup_State_AnimateUntilFlash ? 2 :
                st == (void *)TitleSetup_State_FlashIn           ? 3 :
                st == (void *)TitleSetup_State_WaitForSonic      ? 4 :
                st == (void *)TitleSetup_State_SetupLogo         ? 5 :
                st == (void *)TitleSetup_State_WaitForEnter      ? 6 :
                st == (void *)TitleSetup_State_FadeToMenu        ? 7 :
                st == (void *)TitleSetup_State_FadeToVideo       ? 8 : 0;
        }
    }
#endif
    if (Spikes) p6_w_spikes_aniframes = (int32)(int16)Spikes->aniFrames;
    {   /* Batch 1: count how many of the 4 clean objects registered (classID>0). */
        int32 b1 = 0;
        if (Decoration && Decoration->classID) ++b1;
        if (ForceSpin && ForceSpin->classID) ++b1;
        if (SpinBooster && SpinBooster->classID) ++b1;
        p6_w_b1_registered = b1;
    }
    {   /* Batch 2: count how many of the 9 chain+badnik objects registered (classID>0),
         * and latch each one's classID for the per-object diagnostic. */
        extern int32 p6_w_b2_cids[9];
        int32 b2 = 0;
        p6_w_b2_cids[0] = (BadnikHelpers) ? (int32)BadnikHelpers->classID : -1;
        p6_w_b2_cids[1] = (Explosion)     ? (int32)Explosion->classID     : -1;
        p6_w_b2_cids[2] = (Animals)       ? (int32)Animals->classID       : -1;
        p6_w_b2_cids[3] = (Newtron)       ? (int32)Newtron->classID       : -1;
        p6_w_b2_cids[4] = (Crabmeat)      ? (int32)Crabmeat->classID      : -1;
        p6_w_b2_cids[5] = (BuzzBomber)    ? (int32)BuzzBomber->classID    : -1;
        p6_w_b2_cids[6] = (Chopper)       ? (int32)Chopper->classID       : -1;
        p6_w_b2_cids[7] = (Motobug)       ? (int32)Motobug->classID       : -1;
        p6_w_b2_cids[8] = (Batbrain)      ? (int32)Batbrain->classID      : -1;
        for (int i = 0; i < 9; ++i) if (p6_w_b2_cids[i] > 0) ++b2;
        p6_w_b2_registered = b2;
    }
    /* Batch 2 anim-load latches (range-independent; -1 == LoadSpriteAnimation failed,
     * which on STG-overflow is the R13 sentinel for these slow-path anims). */
    if (Explosion) p6_w_explosion_aniframes = (int32)(int16)Explosion->aniFrames;
    if (Animals)   p6_w_animals_aniframes   = (int32)(int16)Animals->aniFrames;
    if (Newtron)   p6_w_newtron_aniframes   = (int32)(int16)Newtron->aniFrames;

    /* RANGE-INDEPENDENT anim-load status, every tick, straight off the Object
     * struct -- the definitive "did StageLoad's LoadSpriteAnimation succeed"
     * signal. (int16)-cast so a -1 (0xFFFF) load failure reads as -1, not 65535
     * (which would falsely pass a >0 gate). This is what makes an unloaded anim
     * impossible to miss regardless of where the camera/player is. */
    if (Spring)   p6_w_spring_aniframes   = (int32)(int16)Spring->aniFrames;
    if (Bridge)   p6_w_brg_aniframes      = (int32)(int16)Bridge->aniFrames;
    if (SpikeLog) p6_w_spikelog_aniframes = (int32)(int16)SpikeLog->aniFrames;

    /* Spring canary (#254 funding): first Spring's animator.frames. */
    static int32 s_spring_latched = 0;
    if (Spring && Spring->classID) {
        p6_w_spring_classid = (int32)Spring->classID;
        if (!s_spring_latched) {
            EntitySpring *sp = NULL;
            foreach_all(Spring, e) { sp = e; break; }
            if (sp) { p6_w_spring_frames = (int32)(size_t)sp->animator.frames; s_spring_latched = 1; }
        }
    }

    /* Bridge (#181): classid live; count/pos/frames one-shot (planks exist from
     * frame 1). brg_frames 0 == Bridge.bin alloc-failed; the regression sentinel. */
    static int32 s_brg_latched = 0;
    if (Bridge && Bridge->classID) {
        p6_w_brg_classid = (int32)Bridge->classID;
        if (!s_brg_latched) {
            int32 cnt = 0; EntityBridge *first = NULL;
            foreach_all(Bridge, b) { ++cnt; if (!first) first = b; }
            if (cnt > 0) {
                p6_w_brg_count    = cnt;
                p6_w_brg_posx     = first->position.x;
                p6_w_brg_posy     = first->position.y;
                p6_w_brg_onscreen = (int32)first->onScreen;
                p6_w_brg_frames   = (int32)(size_t)first->animator.frames;
                s_brg_latched     = 1;
            }
        }
    }

    /* PlaneSwitch (#254 loop): bit 3 = the loop fix; pscount = # placed (expect 106). */
    static int32 s_loop_latched = 0;
    if (!s_loop_latched) {
        p6_w_loop_regmask = (PlaneSwitch && PlaneSwitch->classID) ? 0x08 : 0;
#if defined(P6_STREAM_PROOF)
        /* PERF (2026-06-20, MEASURED): pscount is a foreach_all FULL-POOL scan. Under the camera-local
         * pool a PlaneSwitch may NEVER be near (nearest is x=3352, >2200px past the x~108 spawn window)
         * so this latch NEVER fires -> the scan ran EVERY shipping frame == ~half the 9.65ms "tail"
         * (qa_p6_perf), the perf-diagnostic-in-hotloop regression the streaming introduced. pscount is
         * consumed ONLY by qa_p6_stream_in (the P6_STREAM_PROOF warp build, where PlaneSwitches ARE near
         * and it DOES latch). Shipping uses regmask (above) for R3 / qa_p6_loop, so the scan is pure
         * diagnostic overhead there -> compile it out of shipping. */
        if (PlaneSwitch && PlaneSwitch->classID) {
            int32 cnt = 0;
            foreach_all(PlaneSwitch, ps) { ++cnt; }
            if (cnt > 0) { p6_w_loop_pscount = cnt; s_loop_latched = 1; }
        }
#endif
    }

    /* SpikeLog (O3 step 1): classid live; first instance's animator.frames latched
     * (0 == GHZ/SpikeLog.bin anim failed to load; >0 == the spike log is armed). */
    static int32 s_sl_latched = 0;
    if (SpikeLog && SpikeLog->classID) {
        p6_w_spikelog_classid = (int32)SpikeLog->classID;
#if defined(P6_STREAM_PROOF)
        /* PERF: same regression as PlaneSwitch -- SpikeLog (min x=2632) is never near the x~108 spawn
         * so this latch never fires -> foreach_all scanned EVERY shipping frame (the other ~half of the
         * tail). spikelog_frames is diag-only (qa_p6_ghz_regression checks classid above + the
         * load-status aniframes witness, NOT this latch) -> strip from shipping, keep for diag builds. */
        if (!s_sl_latched) {
            EntitySpikeLog *sl = NULL;
            foreach_all(SpikeLog, e) { sl = e; break; }
            if (sl) { p6_w_spikelog_frames = (int32)(size_t)sl->animator.frames; s_sl_latched = 1; }
        }
#endif
    }

    /* BADNIK-VIS: live draw-state scan. Walk every badnik type; count live entities;
     * latch the FIRST one that is on-screen (or, failing that, the first live one) and
     * record its full draw state so the decision tree resolves in one capture. Each
     * badnik Entity has its `animator` field; GetFrame(aniFrames, animID, frameID) is
     * stride-safe (->frames is opaque here). The handle accessor reads the pack table.
     * Non-sticky every frame so the LATEST on-screen badnik is what the capture sees.
     *
     * PERF (2026-06-18): this 6x-foreach_all scan ran EVERY shipping frame and cost
     * ~19ms in-motion -- it HALVED fps (48->20) until measured (qa_p6_perf M8/M9). It
     * is PURE DIAGNOSTIC (feeds qa_p6_ghz_regression R16 only). Compile-strip it from
     * the shipping build via P6_PERF_NOSCAN (set by build_shipping.sh) -- the SAME flag
     * that strips the census in p6_io_main.cpp. R14/R15 (arm_env bind witnesses) still
     * prove GHZ/Objects.gif binds in shipping; R16 (a LIVE badnik's handle) is diag-
     * only and reads the -1 sentinel below as "skipped". */
#ifndef P6_PERF_NOSCAN
    {
        int32 found = 0;
        /* Macro: scan one badnik type's live entities in its OWN block scope (foreach_all
         * declares Entity##OBJP *_b -- a fresh _b per block avoids redeclaration). Latch
         * the first live entity, preferring an on-screen one. */
        #define BD_SCAN(OBJP, AFW)                                                     \
            if (OBJP && (OBJP)->classID) {                                             \
                foreach_all(OBJP, _b) {                                                \
                    ++found;                                                           \
                    int32 onscr = (int32)_b->onScreen;                                 \
                    if (p6_w_bd_classid < 0 || (onscr && p6_w_bd_onscreen <= 0)) {     \
                        p6_w_bd_classid  = (int32)_b->classID;                         \
                        p6_w_bd_posx     = (int32)(_b->position.x >> 16);              \
                        p6_w_bd_posy     = (int32)(_b->position.y >> 16);              \
                        p6_w_bd_onscreen = onscr;                                      \
                        p6_w_bd_visible  = (int32)_b->visible;                         \
                        p6_w_bd_drawgrp  = (int32)_b->drawGroup;                       \
                        p6_w_bd_active   = (int32)_b->active;                          \
                        p6_w_bd_framesNN = (_b->animator.frames != NULL) ? 1 : 0;      \
                        p6_w_bd_animid   = (int32)_b->animator.animationID;            \
                        p6_w_bd_frameid  = (int32)_b->animator.frameID;                \
                        SpriteFrame *_fr = RSDK.GetFrame((AFW),                        \
                            _b->animator.animationID, _b->animator.frameID);           \
                        p6_w_bd_sheetid  = _fr ? (int32)_fr->sheetID : -1;             \
                        p6_w_bd_handle   = _fr ? p6_vdp1_handle_for_surface(_fr->sheetID) : -4; \
                    }                                                                  \
                }                                                                      \
            }
        /* reset the latch each frame so an on-screen badnik (when one exists) wins */
        p6_w_bd_classid = -1; p6_w_bd_onscreen = -1;
        BD_SCAN(Motobug,    Motobug->aniFrames)
        BD_SCAN(Newtron,    Newtron->aniFrames)
        BD_SCAN(Crabmeat,   Crabmeat->aniFrames)
        BD_SCAN(BuzzBomber, BuzzBomber->aniFrames)
        BD_SCAN(Chopper,    Chopper->aniFrames)
        BD_SCAN(Batbrain,   Batbrain->aniFrames)
        #undef BD_SCAN
        p6_w_bd_found = found;
    }
#else
    /* Shipping (P6_PERF_NOSCAN): the per-frame badnik scan is stripped from the hot
     * loop. The -1 sentinel tells qa_p6_ghz_regression R16 to SKIP (diag-only); the
     * R14/R15 bind-table checks (arm_env witnesses) still prove the surface binds. */
    p6_w_bd_found = -1;
#endif
}

// =============================================================================
// I3b 2b: the camera-local-pool MATERIALIZE (overlay-resident). Reconstructs scene entity
// `logical_slot` from the cart DORM store (0x226C8000; big-endian header/index/records + raw LE
// Scene.bin var-bytes) into `dest_slot`. Mirrors the proven pack logic (qa_p6_materialize_write
// GREEN @ 55c77e5) EXACTLY -- only the home moved (pack -> cart, residency rule). The overlay does
// the DORM navigation + LE var-replay (raw offset writes); the ENGINE-touching ops are pack thunks.
// =============================================================================
static unsigned int   p6m_be32(const unsigned char *p) { return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3]; }
static unsigned short p6m_be16(const unsigned char *p) { return (unsigned short)(((unsigned int)p[0] << 8) | p[1]); }
static unsigned int   p6m_le32(const unsigned char *p) { return ((unsigned int)p[3] << 24) | ((unsigned int)p[2] << 16) | ((unsigned int)p[1] << 8) | p[0]; }
static unsigned short p6m_le16(const unsigned char *p) { return (unsigned short)(((unsigned int)p[1] << 8) | p[0]); }

// I3b 2b STREAMING (load phase, overlay-resident). Camera-local NEAR-shrink: relocate the camera-NEAR
// populated scene entities (p6_scan_near, seeded for the spawn camera) into a dense pool [R,R+nNear),
// DORMANT the FAR ones (remap -> the reserved dummy at R+SCENE_PHYS-1), make the free slots
// [R+nNear,R+SCENE_PHYS-1) inert (materialize-targets for the per-frame stream, Build 2), shift temp
// down, flip p6_pool_scene_phys to SCENE_PHYS=640. Relocation reuses the PROVEN compaction byte-plan
// (near is a subset; qa_p6_pool_compact_model). LOAD-ONLY (no per-frame stream yet) -> this SHRINKS the
// pool (qa_p6_pool_shrink GREEN, maxslot<768) but DROPS entities the camera later reaches (R0-R16 RED
// baseline); Build 2's per-frame materialize/dormant turns R0-R16 GREEN. The pack seeds p6_scan_near via
// p6_scan_update_near(spawn camX) before calling this.
static void p6_ovl_pool_compact(void)
{
    int32 g[7];
    unsigned char  *base, *src, *dst, *e, *pe;
    unsigned short *remap, *inv;
    unsigned int    SCN_BASE;
    int CIDOFF, NARROW, R, SCN, TEMP, WIDE, SP;
    int n, lastL, dummy, L, k, t, i, ok, P, isnear;

    p6_eng_pool_geom(g);
    CIDOFF = (int)g[0]; NARROW = (int)g[1]; R = (int)g[2]; SCN = (int)g[3]; TEMP = (int)g[4]; WIDE = (int)g[5];
    SP = (int)g[6];     /* P6_POOL_SCENE_PHYS = 640 (the shrunk physical scene-slot count) */
    base  = (unsigned char *)0x00243000u;        /* P6_LW_ENTITYLIST (WRAM-L, cached, master SH-2) */
    remap = (unsigned short *)0x226B8000u;        /* p6_pool_remap (cache-through cart) */
    inv   = (unsigned short *)0x226BC000u;        /* p6_pool_remap_inv */
    SCN_BASE = (unsigned int)R * (unsigned int)WIDE;
    dummy = R + SP - 1; /* reserved classID=0 dummy = the LAST scene-physical slot (kept OUT of the free
                           pool so a future materialize never overwrites it) */
    n = 0; lastL = -1;

    /* Pass A -- classify NEAR-and-populated scene slots ascending into a dense pool. p6_scan_near already
       includes the always-iterate set (NORMAL/managers), so they are kept resident. */
    for (L = R; L < R + SCN; ++L) {
        e = base + SCN_BASE + (unsigned int)(L - R) * (unsigned int)NARROW;
        isnear = (p6_scan_near[L >> 3] >> (L & 7)) & 1;
        if (isnear && *(unsigned short *)(e + CIDOFF)) {
            remap[L]   = (unsigned short)(R + n);
            inv[R + n] = (unsigned short)L;
            lastL = L; ++n;
        } else {
            remap[L] = (unsigned short)0xFFFFu;    /* far or empty -> dummy in A2 */
        }
    }
    /* A2 -- far/empty logical slots -> the reserved dummy. */
    for (L = R; L < R + SCN; ++L)
        if (remap[L] == (unsigned short)0xFFFFu) remap[L] = (unsigned short)dummy;
    /* Pass B -- relocate near ascending (dst<=src, per-entity non-overlap -> forward byte copy; proven). */
    for (k = 0; k < n; ++k) {
        L   = (int)inv[R + k];
        src = base + SCN_BASE + (unsigned int)(L - R) * (unsigned int)NARROW;
        dst = base + SCN_BASE + (unsigned int)k * (unsigned int)NARROW;
        if (dst != src) for (i = 0; i < NARROW; ++i) dst[i] = src[i];
    }
    /* Pass C -- make every NON-near scene-physical slot inert: [R+n, R+SP) = the free pool + the dummy.
       loop1 iterates the whole physical scene region, so a STALE classID here would be a phantom entity
       -> zero the classID (2 B) + set inv=self (a safe in-bounds _L for loop1's near-cull index). */
    for (P = R + n; P < R + SP; ++P) {
        pe = base + SCN_BASE + (unsigned int)(P - R) * (unsigned int)NARROW;
        *(unsigned short *)(pe + CIDOFF) = 0;
        inv[P] = (unsigned short)P;
    }
    /* Pass D -- shift temp 1:1 DOWN to [R+SP, R+SP+TEMP). */
    for (t = 0; t < TEMP; ++t) {
        src = base + SCN_BASE + (unsigned int)SCN * (unsigned int)NARROW + (unsigned int)t * (unsigned int)WIDE;
        dst = base + SCN_BASE + (unsigned int)SP  * (unsigned int)NARROW + (unsigned int)t * (unsigned int)WIDE;
        if (dst != src) for (i = 0; i < WIDE; ++i) dst[i] = src[i];
        remap[R + SCN + t] = (unsigned short)(R + SP + t);
        inv[R + SP + t]    = (unsigned short)(R + SCN + t);
    }
    /* STREAMING init: the free pool = [R+n, R+SP-1) (the inert slots Pass C made, EXCLUDING the reserved
       dummy at R+SP-1). Push them onto the free-list stack; zero the lifecycle (destroyed) bitfield. The
       resident set is the spawn-near [R,R+n) (already placed). The per-frame p6_ovl_stream uses these. */
    {
        unsigned short *fl   = (unsigned short *)P6_STREAM_FREELIST;
        int            *fc   = (int *)P6_STREAM_FREECNT;
        unsigned char  *life = (unsigned char *)P6_STREAM_LIFE;
        unsigned short *rl   = (unsigned short *)P6_STREAM_RESIDLIST;  /* I3b 2b PERF #2: resident logical-slot list */
        int            *rc   = (int *)P6_STREAM_RESIDCNT;
        int f = 0, q;
        for (q = R + n; q < R + SP - 1; ++q) fl[f++] = (unsigned short)q;
        *fc = f;
        for (q = 0; q < (SCN + 7) / 8; ++q) life[q] = 0;
        /* I3b 2b PERF #2 (scan narrowing): seed the resident-list with the n spawn-near residents [R,R+n).
           Their logical slots are inv[R+0..R+n-1] (Pass A set inv[R+k]=L). The per-frame stream's DORMANT/
           RETIRE pass iterates THIS ~42-entry list instead of all 1088 scene slots. */
        for (q = 0; q < n; ++q) rl[q] = inv[R + q];
        *rc = n;
    }
    /* witnesses (compact_n is now the spawn NEAR count; sphys is the shrunk 640). */
    p6_w_compact_n     = n;
    p6_w_compact_sphys = SP;
    p6_w_compact_dummy = dummy;
    p6_w_compact_lastL = lastL;
    p6_w_compact_lastP = (lastL >= 0) ? (int32)remap[lastL] : -1;
    /* light bij self-check: dummy inert + the highest-near entity at the last dense slot. The real
       gameplay catch is qa_p6_ghz_regression R0-R16 (RED here without the per-frame stream). */
    ok = 1;
    if (*(unsigned short *)(base + SCN_BASE + (unsigned int)(dummy - R) * (unsigned int)NARROW + CIDOFF) != 0) ok = 0;
    if (lastL >= 0 && (int)remap[lastL] != R + n - 1) ok = 0;
    p6_w_compact_bij_ok = ok;
    /* ATOMIC FLIP (pack thunk, LAST) -- scene_phys = SCENE_PHYS (640). */
    p6_eng_pool_flip(SP, dummy);
}

static void p6_ovl_materialize(unsigned logical_slot, unsigned dest_slot)
{
    const unsigned char *D = (const unsigned char *)0x226C8000u; // cart DORM store
    if (p6m_be32(D) != 0x4D443650u) return;                      // 'P6DM'
    unsigned short slot_count   = p6m_be16(D + 6);
    unsigned short obj_count    = p6m_be16(D + 8);
    unsigned int   slot_idx_off = p6m_be32(D + 12);
    unsigned int   recs_off     = p6m_be32(D + 16);
    if (logical_slot >= slot_count) return;
    unsigned int rec = p6m_be32(D + slot_idx_off + logical_slot * 4u);
    if (rec == 0xFFFFFFFFu) return;
    const unsigned char *R = D + recs_off + rec;
    unsigned short obj_idx = p6m_be16(R + 0);
    int px = (int)p6m_be32(R + 4);
    int py = (int)p6m_be32(R + 8);
    const unsigned char *vb = R + 12;
    if (obj_idx >= obj_count) return;

    // navigate the variable-length object table: each = hash16 | nvars u8 | nvars*(hash16|type u8)
    const unsigned char *O = D + 20;
    for (unsigned short i = 0; i < obj_idx; ++i) { unsigned char nv = O[16]; O += 17u + (unsigned int)nv * 17u; }
    const unsigned char *obj_hash_le = O;
    unsigned char nvars = O[16];
    const unsigned char *attribs = O + 17;

    p6_w_mat_slot    = (int32)logical_slot;
    int classID      = p6_eng_classid_resolve(obj_hash_le); // pack thunk (also latches classcount)
    p6_w_mat_classid = classID;
    p6_w_mat_nvars   = (int32)nvars;
    if (!classID) return;                                   // unregistered -> skip

    p6_eng_serialize_begin(classID);                        // pack thunk: rebuild editableVarList
    unsigned char *eb = (unsigned char *)p6_eng_entity_prepare((int32)dest_slot);
    p6_eng_write_placement((void *)eb, classID, px, py);
    p6_w_mat_posx = px >> 16;
    p6_w_mat_posy = py >> 16;

    // replay var values: per attrib, match hash -> offset (pack thunk), LE-decode the raw bytes
    // (byte-wise = alignment-safe), write into the matched field by offset (no engine types here).
    const unsigned char *vp = vb;
    int nmatch = 0, vi = 0, wv[4] = { 0, 0, 0, 0 };
    for (unsigned short a = 0; a < nvars; ++a) {
        int off = p6_eng_var_offset(attribs + (unsigned int)a * 17u);
        unsigned char vt = attribs[(unsigned int)a * 17u + 16];
        int val = 0, have = 0;
        switch (vt) {
            case 0: case 3: // VAR_UINT8 / VAR_INT8 (1 byte)
                val = (vt == 3) ? (int)(signed char)vp[0] : (int)vp[0]; vp += 1; have = 1;
                if (off >= 0) eb[off] = (unsigned char)val;
                break;
            case 1: case 4: // VAR_UINT16 / VAR_INT16 (2)
                val = (vt == 4) ? (int)(short)p6m_le16(vp) : (int)p6m_le16(vp); vp += 2; have = 1;
                if (off >= 0) *(short *)(eb + off) = (short)val;
                break;
            case 2: case 5: case 6: case 7: case 10: case 11: // U32/I32/ENUM/BOOL/FLOAT/COLOR (4)
                val = (int)p6m_le32(vp); vp += 4; have = 1;
                if (off >= 0) *(int *)(eb + off) = val;
                break;
            case 9: { // VAR_VECTOR2 (8)
                int vx = (int)p6m_le32(vp), vy = (int)p6m_le32(vp + 4); vp += 8; val = vx; have = 1;
                if (off >= 0) { *(int *)(eb + off) = vx; *(int *)(eb + off + 4) = vy; }
                break;
            }
            case 8: { unsigned short ln = p6m_le16(vp); vp += 2u + (unsigned int)ln * 2u; break; } // STRING skip
            default: break;
        }
        if (off >= 0) ++nmatch;
        if (have && vi < 4) { wv[vi] = val; ++vi; }
    }
    p6_eng_serialize_end();                                  // pack thunk: restore editableVarList
    p6_w_mat_nmatch = nmatch;
    p6_w_mat_v0 = wv[0]; p6_w_mat_v1 = wv[1]; p6_w_mat_v2 = wv[2]; p6_w_mat_v3 = wv[3];
    // I3b 2b STREAMING: re-CREATE the entity. Placement alone is INERT (no animator/state machine); Create
    // runs the object's spawn setup so a re-materialized entity is LIVE. Mirrors InitObjects (the gap the
    // qa_p6_materialize_write gate did NOT cover -- it only checked placement M1-M5).
    p6_eng_create((int32)dest_slot);
}

// I3b 2b STREAMING per-frame manager. Runs EVERY frame from ProcessObjects (after p6_scan_update_near
// rebuilt p6_scan_near for the LIVE camera, BEFORE loop1). One pass over the scene logical range:
//   - gameplay-destroyed (resident but classID==0) -> retire: set lifecycle bit, free the slot, dormant.
//   - newly-near + not-resident + not-destroyed -> MATERIALIZE: pop a free physical slot, re-Create from
//     DORM into it (live), remap[L]=P, inv[P]=L.
//   - newly-far + resident -> DORMANT: make the slot inert, push it to the free-list, remap[L]=dummy.
// Guarded by p6_w_compact_n>=0 (the load-near-shrink ran -> pool is shrunk). The materialize + relocate +
// dummy are PROVEN; this diff/free-list/lifecycle + the mid-frame Create are the new code -> R3 RED->GREEN.
static void p6_ovl_stream(void)
{
    int32 g[7];
    unsigned char  *base, *pe;
    unsigned short *remap, *inv, *fl;
    unsigned char  *life;
    int *fc;
    unsigned int SCN_BASE;
    int CIDOFF, NARROW, R, SCN, WIDE, SP, dummy, L, P, isnear, res;

    if (p6_w_compact_n < 0) return;   /* the load-near-shrink has not run -> not shrunk -> no-op */
    p6_eng_pool_geom(g);
    CIDOFF = (int)g[0]; NARROW = (int)g[1]; R = (int)g[2]; SCN = (int)g[3]; WIDE = (int)g[5]; SP = (int)g[6];
    base  = (unsigned char *)0x00243000u;
    remap = (unsigned short *)0x226B8000u;
    inv   = (unsigned short *)0x226BC000u;
    fl    = (unsigned short *)P6_STREAM_FREELIST;
    fc    = (int *)P6_STREAM_FREECNT;
    life  = (unsigned char *)P6_STREAM_LIFE;
    SCN_BASE = (unsigned int)R * (unsigned int)WIDE;
    dummy = R + SP - 1;
    res = 0;

#ifdef P6_BACKTRACK_PROOF
    /* BACKTRACK PROOF (diag-only): once the stream has settled (call 30), synthetically DESTROY the
       first resident scene entity -- set classID=0, EXACTLY how RSDK destroyEntity/ResetEntitySlot
       signals a kill (collected ring, broken badnik). The main loop below then RETIRES it (lifecycle
       bit, free, dummy); on every later frame it stays NEAR (camera fixed) but dormant -> the
       skip-destroyed path must keep it dead. p6_w_bt_logical (set LAST) arms the post-loop recorder.
       RED demo: -DP6_BT_NOSKIP forces materialize -> the dead entity RE-MATERIALIZES (reappear=1). */
    {
        static int s_bt_frame = 0;
        ++s_bt_frame;
        if (p6_w_bt_logical < 0 && s_bt_frame == 30) {
            int tl;
            for (tl = R; tl < R + SCN; ++tl) {
                int tp = (int)remap[tl];
                if (tp != dummy) {
                    unsigned char *te = base + SCN_BASE + (unsigned int)(tp - R) * (unsigned int)NARROW;
                    unsigned short cid = *(unsigned short *)(te + CIDOFF);
                    if (cid != 0) {
                        p6_w_bt_cid = (int32)cid;
                        *(unsigned short *)(te + CIDOFF) = 0;  /* the synthetic gameplay destroy */
                        p6_w_bt_logical = tl;                  /* set LAST -- arms the recorder */
                        break;
                    }
                }
            }
        }
    }
#endif

    /* I3b 2b PERF #2 (scan narrowing) -- the old full scan walked all 1088 scene slots EVERY frame
       (3.512 ms = 32.4% of ProcessObjects, qa_p6_streamscan). Replace it with two passes over only the
       ~83 active slots: DORMANT/RETIRE over the resident-list, then MATERIALIZE by byte-scanning the
       WRAM-H near bitfield. Per-slot logic is BYTE-IDENTICAL to the old scan; only the iteration changed.
       The always-iterate set (managers/NORMAL) is resident from the load-near-shrink + kept by Pass A's
       `isnear` (its p6_scan_near bit is always set); it appears in the byte-scan too but is skipped resident. */
    {
        unsigned short *rl  = (unsigned short *)P6_STREAM_RESIDLIST;
        int            *rc  = (int *)P6_STREAM_RESIDCNT;
        int k, w, b, bit, lo_b = R >> 3, hi_b = (R + SCN + 7) >> 3;
        unsigned int bv;

        /* PASS A -- DORMANT/RETIRE first (frees slots for the materialize below). Walk the resident-list,
           in-place swap-compacting survivors. classID==0 -> gameplay-destroyed -> RETIRE (lifecycle bit +
           free); no longer near -> DORMANT (free); else KEEP. */
        w = 0;
        for (k = 0; k < *rc; ++k) {
            L  = (int)rl[k];
            P  = (int)remap[L];
            pe = base + SCN_BASE + (unsigned int)(P - R) * (unsigned int)NARROW;
            isnear = (p6_scan_near[L >> 3] >> (L & 7)) & 1;
            if (*(unsigned short *)(pe + CIDOFF) == 0) {       /* destroyed -> retire permanently */
                /* lifecycle bitfield indexed by the SCENE slot (L - R), NOT bare L -- sized
                   (SCENEENTITY_COUNT+7)/8; a bare-L index overflows by RESERVE/8 into un-zeroed cart for
                   the top RESERVE slots (qa_p6_lifecycle_index, static, deterministic). */
                life[(L - R) >> 3] |= (unsigned char)(1 << ((L - R) & 7));
                fl[(*fc)++] = (unsigned short)P;
                remap[L] = (unsigned short)dummy;
                ++p6_w_stream_dorm;                            /* retire counts as a dorm for the witness */
            } else if (!isnear) {                              /* newly far -> dormant */
                *(unsigned short *)(pe + CIDOFF) = 0;
#if !defined(P6_POOLINV_LEAK)
                fl[(*fc)++] = (unsigned short)P;               /* return the freed slot to the pool */
#endif                                                         /* P6_POOLINV_LEAK: skip the return -> LEAK (RED demo) */
                remap[L] = (unsigned short)dummy;
                ++p6_w_stream_dorm;
            } else {
                rl[w++] = (unsigned short)L;                   /* still near + live -> keep */
            }
        }
        *rc = w;

        /* PASS B -- MATERIALIZE: byte-scan the WRAM-H near bitfield p6_scan_near (the overlay links it via
           -R; the pack rebuilt it just before this tick). Skip-zero-byte -> ~136 WRAM-H reads for a sparse
           near set instead of 1088 cart remap reads. Each set bit is a near logical slot; materialize it if
           dormant + not destroyed, then append to the resident-list. */
        for (b = lo_b; b < hi_b; ++b) {
            bv = p6_scan_near[b];
            if (!bv) continue;
            for (bit = 0; bit < 8; ++bit) {
                if (!(bv & (1u << bit))) continue;
                L = (b << 3) + bit;
                if (L < R || L >= R + SCN) continue;           /* scene slots only */
                if ((int)remap[L] != dummy) continue;          /* already resident (Pass A handled retire) */
#if defined(P6_BT_NOSKIP)
                if (1) {  /* BACKTRACK RED demo (diag): ignore the lifecycle bit -> a destroyed entity RE-MATERIALIZES */
#else
                if (!(life[(L - R) >> 3] & (1 << ((L - R) & 7)))) {  /* not destroyed (scene-slot L-R index) */
#endif
                    if (*fc > 0) {
                        P = (int)fl[--(*fc)];
                        remap[L] = (unsigned short)P;
                        inv[P]   = (unsigned short)L;
                        /* DORM is indexed by the raw Scene.bin slotID = L-RESERVE; dest_slot=L ->
                           RSDK_ENTITY_AT(L)=remap[L]=P -> the entity is written + Created at physical P. */
                        p6_ovl_materialize((unsigned)(L - R), (unsigned)L);
                        ++p6_w_stream_mat;
                        rl[(*rc)++] = (unsigned short)L;        /* add to the resident-list */
                    } else {
                        ++p6_w_stream_starve;                  /* free-list empty (SP undersized -- gate) */
                    }
                }
            }
        }
        res = *rc;
    }
    p6_w_stream_free     = *fc;
    p6_w_stream_resident = res;

    /* POOL-INVARIANT GUARD (always-on, ~1 compare/frame): every physical scene slot [R,R+SP) is exactly
       one of resident / free / the single dummy, so resident + free == SP - 1 EVERY frame. A free-list
       leak (a dormant that fails to return its slot) or a double-free breaks it. p6_w_pool_inv_bad is a
       STICKY latch -> a transient or multi-cycle violation that self-corrects by capture-time is still
       caught. Permanent self-check: peek p6_w_pool_inv_bad from any savestate if entities ever vanish
       after scrolling. RED demo: -DP6_POOLINV_LEAK (above) skips the dormant return. Gate qa_p6_stream_in S4. */
    if (res + *fc != SP - 1) p6_w_pool_inv_bad = 1;

#ifdef P6_BACKTRACK_PROOF
    /* RECORD (post-loop, every frame once armed): did the destroyed entity get RETIRED (life bit) and
       does it STAY dead (remap == dummy)?  life=1 + reappear=0 == the lifecycle works. Under
       -DP6_BT_NOSKIP the dead entity re-materialized this frame -> remap != dummy -> reappear=1 (RED). */
    if (p6_w_bt_logical >= 0) {
        int tl = (int)p6_w_bt_logical;
        p6_w_bt_life     = (int32)((life[(tl - R) >> 3] >> ((tl - R) & 7)) & 1);
        p6_w_bt_reappear = (remap[tl] != (unsigned short)dummy) ? 1 : 0;
    }
#endif
}
