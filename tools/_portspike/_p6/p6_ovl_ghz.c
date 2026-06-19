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
        if (PlaneSwitch && PlaneSwitch->classID) {
            int32 cnt = 0;
            foreach_all(PlaneSwitch, ps) { ++cnt; }
            if (cnt > 0) { p6_w_loop_pscount = cnt; s_loop_latched = 1; }
        }
    }

    /* SpikeLog (O3 step 1): classid live; first instance's animator.frames latched
     * (0 == GHZ/SpikeLog.bin anim failed to load; >0 == the spike log is armed). */
    static int32 s_sl_latched = 0;
    if (SpikeLog && SpikeLog->classID) {
        p6_w_spikelog_classid = (int32)SpikeLog->classID;
        if (!s_sl_latched) {
            EntitySpikeLog *sl = NULL;
            foreach_all(SpikeLog, e) { sl = e; break; }
            if (sl) { p6_w_spikelog_frames = (int32)(size_t)sl->animator.frames; s_sl_latched = 1; }
        }
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
