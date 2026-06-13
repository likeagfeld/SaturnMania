// =============================================================================
// p6_ovl_api.h -- P6.7d.3 (Task #210): the overlay <-> main-image ABI.
//
// Direction 1 (main -> overlay): the overlay binary is linked FIXED-BASE at
// P6_OVL_BASE against the main image's symbol table (ld -R game.elf), so
// overlay code can reference extern "C" main-image symbols (the p6_bridge_*
// table shims, the p6_w_obj_* witnesses) directly. C++-mangled ENGINE
// symbols are NOT named from overlay TUs (the flat-TU rule, P5/P6.1): the
// engine surface arrives as FUNCTION POINTERS in this struct -- exactly how
// the decomp GameAPI hands objects the RSDKTable.
//
// Direction 2 (overlay -> main): the entry function at P6_OVL_BASE registers
// the overlay's classes through the passed-in thunk and fills the harness
// vtable. The MAIN image never names an overlay symbol -- everything routes
// through the entry pointer, the shape every per-zone pack uses at P6.8.
//
// PLACEMENT: P6_OVL_BASE sits in the region the P6.7d.2 SGL-area re-contract
// freed (0x060C0000..0x060F4000); the proof window is 32 KB (the P6.8 zone
// window is the SPZ-sized 124 KB inside P68_HWRAM_CODE_BYTES).
// =============================================================================
#ifndef P6_OVL_API_H
#define P6_OVL_API_H

// W15b: base slid 0x060C0000 -> 0x060C4000, window 32K -> 16K.
// W17 (Task #227, 2026-06-13): the WRAM-H re-budget slides the overlay UP to
// 0x060C9000 and shrinks the proof window 16K -> 4K (the Ring overlay is
// 508 B). The reclaimed 12 KB + the GROUPWIN 8 KB trim fund the ANIMPAK
// floor raise 0x060B3000 -> 0x060B8000 (+20 KB of _end headroom). GLOBALS
// derives to base+window = 0x060CA000; it ends at 0x060D7B74, 1,164 B below
// the relocated GROUPWIN (p6_io_main.cpp 0x060D8000). The P6.8 zone-window
// re-budget (124 KB SPZ contract) supersedes this.
#define P6_OVL_BASE   0x060C9000u
#define P6_OVL_WINDOW 0x1000u

typedef struct {
    /* ---- filled by MAIN before calling the entry ------------------------ */
    /* Registration thunk: an extern "C" main-image bridge around the
     * engine's RegisterObject (Object.cpp:62-108, REV02 13-arg form with
     * the unused callbacks NULL).  Returns nothing; classIDs are
     * registration-order (the caller has already registered the engine
     * preamble classes, so the overlay's land deterministically). */
    void (*register_object)(void **staticVars, const char *name,
                            unsigned entityClassSize,
                            unsigned staticClassSize,
                            void (*update)(void), void (*draw)(void));

    /* ---- filled by the OVERLAY entry ------------------------------------ */
    void *staticvars_slot;            /* Object** RegisterObject stored      */
    unsigned entity_size;             /* sizeof(EntityRing) (slot-fit check) */
    void (*arm_fn)(void *, void *);   /* p6_ring2_arm (harness spawn init)   */
    void (*witness_fn)(const void *); /* p6_ring2_witness (per-tick copy)    */
    void *update_fn;                  /* Ring_Update -- the V3 residency     */
                                      /* witness (must lie in the window)    */
} p6_ovl_api;

/* The entry signature at P6_OVL_BASE: registers the overlay's classes via
 * api->register_object, fills the api outputs, returns the engine
 * objectClassCount as observed by the registration thunk's last call (the
 * thunk-side counter -- the overlay cannot read engine globals directly). */
typedef int (*p6_ovl_entry_t)(p6_ovl_api *api);

#endif /* P6_OVL_API_H */
