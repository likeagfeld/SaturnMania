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
// O1 (Task #254, 2026-06-17): the SAFE re-budget. Spring (3,006 B) moves OUT of
// the resident pack INTO this overlay, freeing ~3 KB of `_end`. The window must
// grow to hold Ring (501) + Spring (3,006) + the multi-class entry, so it goes
// 4 KB -> 6 KB. To keep GLOBALS (= OVL_BASE + OVL_WINDOW = 0x060CA000) and every
// region above it (GROUPWIN/PACKEDCOL collision/LAYOUT/SGL) BYTE-IDENTICAL --
// avoiding the #249 collision-move risk -- ANIMPAK + OVL_BASE slide DOWN by the
// same 0x800 the window grew (Animation.hpp P6_HW_ANIMPAK 0x060B7800; ANIMPAK
// still ends contiguously at OVL_BASE). The freed `_end` space absorbs the slide
// (margin measured after the build; gate qa_p6_ghz_regression R0 catches a
// frozen boot). GLOBALS check: 0x060C8800 + 0x1800 = 0x060CA000 (unchanged).
// O1 step 2 (Task #254): + Bridge + PlaneSwitch (~4.8KB more) -> the overlay holds
// Ring+Spring+Bridge+PlaneSwitch+entry+migrated-witnesses (~9.5KB), window 6KB->
// 0x2A00 (10.75KB). D grew 0x800->0x1A00; ANIMPAK+OVL_BASE slide down 0x1A00 into
// the _end space the 2 objects vacate, GLOBALS stays 0x060CA000 (=OVL_BASE+WINDOW).
//
// #258 RING ARMAMENT (2026-06-18): the FULL verbatim Game_Ring port grows the
// overlay to ~17.5 KB -- 6,744 B over the WRAM-H GLOBALS wall with no fundable
// WRAM-H reclaim (MEASURED ~3.6 KB short even at the maximal ANIMPAK slide). USER
// DECISION: relocate the overlay to the 4MB Extended-RAM CART (code-from-cart
// VERIFIED to execute, p6_cart_code_proof). P6_OVL_BASE moves to the CACHED cart
// alias 0x02690000 (the engine calls + executes the entry via the cached alias;
// cold I-cache reads cart RAM, no purge needed first exec). The loader WRITES the
// blob via the cache-through twin 0x22690000 so bytes land in cart RAM bypassing
// the SH-2 cache (cart-4mb-extram-measured-map cache rule). PLACEMENT PROVEN FREE
// (data-driven, NOT the prior session's incomplete gap): the GHZ1 resident LAYOUT
// store ends at 0x22686900 (=0x22600000 + 5 layers x551,168 B, parsed offline from
// GHZ1LAYT.BIN); resident SHEETS bump from 0x22400000 and end < 0x22600000 (proven
// by the working build -- crossing 0x22600000 would overwrite the live layout ->
// FG/collision corruption, which the shipping default does NOT show); the GFS read
// windows start at 0x22700000. So 0x22690000..0x22698000 (32 KB) sits in a 38.6 KB
// gap above the layout high-water and 32 KB below the GFS windows -- both neighbors
// proven on the far side. GLOBALS DECOUPLES to its fixed WRAM-H 0x060CA000 (Edit 2,
// p6_io_main.cpp) so it -- and every region above it -- stays put when OVL leaves
// WRAM-H (zero #249 risk; the freed 0x2A00 WRAM-H window is left as _end slack).
#define P6_OVL_BASE   0x02690000u   /* CACHED cart alias (exec); cache-through twin 0x22690000 (load) */
#define P6_OVL_WINDOW 0x20000u      /* 128 KB cart window. Mass-port Batch 2 adds the 6 badnik TUs
                                     * (Newtron/Crabmeat/BuzzBomber/Chopper/Motobug/Batbrain, ~40 KB of
                                     * code) on top of Ring+Spring+Bridge+PlaneSwitch+SpikeLog+Spikes+
                                     * Batch1; ends 0x226B0000, still below the GFS windows (0x22700000)
                                     * -- the gap above the GHZ1 layout high-water 0x22686900 is ~485 KB,
                                     * so this is amply clear. (Chain TUs BadnikHelpers/Explosion/Animals
                                     * live in the PACK, not here -- Game_Player.o references them.) */

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

    /* O1 (Task #254): FULL-callback registration for verbatim objects that need
     * Create/StageLoad (Spring_Create spawns the pad, Spring_StageLoad loads the
     * sprite -- the simple register_object above NULLs them). Mirrors the
     * resident RSDK_REGISTER_OBJECT REV02/non-REV0U arm (GameLink.h:1799): the
     * thunk fills NULL editorLoad/editorDraw + the trailing NULL. */
    void (*register_object_full)(void **staticVars, const char *name,
                                 unsigned entityClassSize,
                                 unsigned staticClassSize,
                                 void (*update)(void), void (*lateUpdate)(void),
                                 void (*staticUpdate)(void), void (*draw)(void),
                                 void (*create)(void *), void (*stageLoad)(void),
                                 void (*serialize)(void));

    /* ---- filled by the OVERLAY entry ------------------------------------ */
    void *staticvars_slot;            /* Object** RegisterObject stored      */
    unsigned entity_size;             /* sizeof(EntityRing) (slot-fit check) */
    void (*arm_fn)(void *, void *);   /* p6_ring2_arm (harness spawn init)   */
    void (*witness_fn)(const void *); /* p6_ring2_witness (per-tick copy)    */
    void *update_fn;                  /* Ring_Update -- the V3 residency     */
                                      /* witness (must lie in the window)    */
    void *loserings_fn;               /* #258b: the overlay's REAL Ring_LoseRings.   */
    void *losehyperrings_fn;          /* The verbatim Player lives in the PACK and   */
                                      /* calls Ring_LoseRings on hurt -> binds to    */
                                      /* the pack STUB. The closure-edge forward      */
                                      /* routes that call here (pack->overlay) so     */
                                      /* lost rings actually scatter + re-collect.   */
    void *badnikbreak_unseeded_fn;    /* Batch 2: the overlay's REAL BadnikHelpers_  */
    void *badnikbreak_fn;             /* BadnikBreakUnseeded / BadnikBreak. Game_    */
                                      /* Player.o (PACK) calls _BadnikBreakUnseeded  */
                                      /* on every badnik kill; BadnikHelpers lives in */
                                      /* the overlay (it derefs Animals/Explosion +   */
                                      /* Animals refs Bridge_HandleCollisions, both   */
                                      /* overlay). The closure-edge forward routes    */
                                      /* the pack call here (same #258b pattern).    */
    void *animals_slot;               /* Batch 2: &Animals (overlay's registered     */
                                      /* Animals object**). ActClear.c:903 (PACK) does */
                                      /* foreach_active(Animals,...) -> the pack's    */
                                      /* NULL Animals is rewired to *animals_slot     */
                                      /* every frame (the #235 Ring-seam pattern).   */
    /* I3b 2b: the camera-local-pool MATERIALIZE (overlay-resident per the residency  */
    /* rule -- new engine code goes to cart). Reconstructs scene entity `logical_slot`*/
    /* from the cart DORM store into `dest_slot`; the pack drives it one-shot at load  */
    /* (s_ovl.materialize_fn) + per-frame at the shrink. The overlay does the DORM     */
    /* navigation + var-replay; the ENGINE-touching ops are pack extern "C" thunks.   */
    void (*materialize_fn)(unsigned logical_slot, unsigned dest_slot);
} p6_ovl_api;

/* The entry signature at P6_OVL_BASE: registers the overlay's classes via
 * api->register_object, fills the api outputs, returns the engine
 * objectClassCount as observed by the registration thunk's last call (the
 * thunk-side counter -- the overlay cannot read engine globals directly). */
typedef int (*p6_ovl_entry_t)(p6_ovl_api *api);

#endif /* P6_OVL_API_H */
