// =============================================================================
// p6_ovl_ring.cpp -- P6.7d.3 (Task #210): the Ring overlay's ENTRY TU.
//
// Compiled WITHOUT -ffunction-sections and linked FIRST (ovl_ring.ld places
// this TU's .text at the window base), so p6_overlay_entry -- the only
// function here -- sits exactly at P6_OVL_BASE: the main image calls it
// through that constant, never through a named overlay symbol.
//
// The entry mirrors what every per-zone pack entry does at the P6.8 flip:
// register the pack's classes through the engine (via the api registration
// thunk -- the flat-TU rule forbids naming C++ engine symbols from overlay
// code; pointers only, the decomp-GameAPI shape) and hand the harness vtable
// back. The Ring registration arguments are VERBATIM the P6.7c in-image
// preamble's (p6_io_main step 2b): ordering is the caller's -- DefaultObject
// @0 + DevOutput @1 are already registered, so Ring lands at classID 2 and
// the LoadGameConfig hash loop matches it identically.
// =============================================================================
#include "p6_ovl_api.h"

// Sibling-TU surface (overlay-local p6_ring2.o):
extern "C" {
extern void *p6_ring2_staticvars_slot;
extern unsigned p6_ring2_entity_size;
void Ring_Update(void);
void Ring_Draw(void);
void p6_ring2_arm(void *slot, void *frames);
void p6_ring2_witness(const void *slot);
}

extern "C" int p6_overlay_entry(p6_ovl_api *api)
{
    api->register_object((void **)&p6_ring2_staticvars_slot, "Ring",
                         p6_ring2_entity_size, 20, Ring_Update, Ring_Draw);

    api->staticvars_slot = p6_ring2_staticvars_slot;
    api->entity_size     = p6_ring2_entity_size;
    api->arm_fn          = p6_ring2_arm;
    api->witness_fn      = p6_ring2_witness;
    api->update_fn       = (void *)Ring_Update;
    return 0;
}
