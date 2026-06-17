// =============================================================================
// p6_ovl_ghz.c -- P6.8 O1 (Task #210/#254): the GHZ zone-overlay ENTRY TU.
//
// Extends the proven P6.7d.3 Ring overlay to a MULTI-CLASS entry. O1 step 1
// moves ONE verbatim object (Spring) out of the resident pack into the
// chain-loaded overlay alongside Ring -- proving the multi-class + full-callback
// ABI + the witness migration WITHOUT touching Bridge/PlaneSwitch (they stay
// resident, so they cannot regress). Bridge/PlaneSwitch follow in O1 step 2 once
// this is GREEN.
//
// Compiled -x c with the census Game.h/GameLink.h tree (like the w4 decomp
// objects) so it names Spring's verbatim globals + callbacks directly, and
// WITHOUT -ffunction-sections so p6_overlay_entry lands FIRST at P6_OVL_BASE
// (the main calls it by that constant address). Registration routes through the
// api thunks (flat-TU rule: the overlay names no C++ engine symbols -- pointers
// only). Witnesses write the ld -R-imported main-image globals, the Ring
// p6_w_obj_* pattern.
// =============================================================================
#include "Game.h"          /* ObjectSpring/EntitySpring + Spring + Spring_* + foreach_all */
#include "p6_ovl_api.h"

/* Ring harness (sibling overlay TU p6_ring2.o) -- the P6.7d.3 surface. */
extern void *p6_ring2_staticvars_slot;
extern unsigned p6_ring2_entity_size;
void Ring_Update(void);
void Ring_Draw(void);
void p6_ring2_arm(void *slot, void *frames);
void p6_ring2_witness(const void *slot);

/* Spring canary witnesses -- DEFINED in p6_io_main.cpp (main image; gates read
 * them from game.map); written here via the ld -R symbol import. */
extern int32 p6_w_spring_classid;
extern int32 p6_w_spring_frames;

/* Forward decl so p6_overlay_entry can be defined FIRST (at the window base). */
static void p6_ghz_ovl_witness(const void *ringSlot);

// =============================================================================
// p6_overlay_entry -- MUST be the first function (window base). Registers the
// overlay's classes through the api thunks; ordering is the caller's
// (DefaultObject@0 + DevOutput@1 already registered -> Ring@2, Spring@3; the
// engine LoadGameConfig hash loop matches each by md5(name), so classID
// assignment is order-independent -- the P6.7d.3 guarantee).
// =============================================================================
int p6_overlay_entry(p6_ovl_api *api)
{
    /* Ring -- verbatim P6.7d.3 simple form (Update+Draw via the harness). */
    api->register_object((void **)&p6_ring2_staticvars_slot, "Ring",
                         p6_ring2_entity_size, 20, Ring_Update, Ring_Draw);

    /* Spring -- verbatim FULL-callback form. NULL editor callbacks match the
     * resident RSDK_REGISTER_OBJECT arm active at REV02/non-REV0U (GameLink.h
     * :1799 -- Bridge/Spring define no _EditorLoad/_EditorDraw). */
    api->register_object_full((void **)&Spring, "Spring",
                              (unsigned)sizeof(EntitySpring),
                              (unsigned)sizeof(ObjectSpring),
                              Spring_Update, Spring_LateUpdate, Spring_StaticUpdate,
                              Spring_Draw, Spring_Create, Spring_StageLoad,
                              Spring_Serialize);

    /* Ring harness vtable (verbatim P6.7d.3). witness_fn is the COMBINED tick
     * witness below (Ring residency + Spring canary latch). */
    api->staticvars_slot = p6_ring2_staticvars_slot;
    api->entity_size     = p6_ring2_entity_size;
    api->arm_fn          = p6_ring2_arm;
    api->witness_fn      = (void (*)(const void *))p6_ghz_ovl_witness;
    api->update_fn       = (void *)Ring_Update;
    return 0;
}

// Combined per-tick witness (api->witness_fn, called from p6_ghz_frame): Ring's
// verbatim residency witness + the Spring canary latch (first Spring's
// animator.frames -- 0 == STG starved, the #254 funding regression). The arg is
// the Ring slot entity (Ring's witness uses it; the Spring latch uses foreach_all).
static int32 s_spring_latched = 0;
static void p6_ghz_ovl_witness(const void *ringSlot)
{
    p6_ring2_witness(ringSlot);
    if (Spring && Spring->classID) {
        p6_w_spring_classid = (int32)Spring->classID;
        if (!s_spring_latched) {
            EntitySpring *first = NULL;
            foreach_all(Spring, sp) { first = sp; break; }
            if (first) {
                p6_w_spring_frames = (int32)(size_t)first->animator.frames;
                s_spring_latched   = 1;
            }
        }
    }
}
