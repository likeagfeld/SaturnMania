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
}
