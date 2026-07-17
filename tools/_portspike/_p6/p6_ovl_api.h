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
#define P6_OVL_WINDOW 0x30000u      /* 192 KB cart window (grown 0x28000->0x30000 2026-07-11 for the
                                     * DDWrecker boss TU, ~9.8 KB, that overflowed the 3.3 KB free at
                                     * 0x28000). The +0x8000 growth reclaims 0x226B8000..0x226C0000;
                                     * the 5 cart blocks that lived there (p6_pool_remap/scan_sorted/
                                     * scan_always/pool_remap_inv/P6_STREAM_FREELIST) were RELOCATED to
                                     * the 0x226DC000..0x226E0000 slack below the VDP1 s_stage
                                     * 0x226E0000 (GHZ resident cursor stops 0x22686900, never reaches
                                     * there). Window end now 0x226C0000 == s_p6_shadow_inrange (a
                                     * static array, not a cart pointer -- exclusive end, no overrun).
                                     * DO NOT grow past 0x226C0000. Original note follows: */
                                    /* 160 KB cart window. Mass-port Batch 2 added the 6 badnik TUs
                                     * (~40 KB) on top of Ring+Spring+Bridge+PlaneSwitch+SpikeLog+Spikes+
                                     * Batch1. Batch 3 (2026-07-09, GHZ gameplay-parity sweep: ItemBox+
                                     * Debris+InvincibleStars+Platform+InvisibleBlock) grew the window
                                     * 0x20000->0x28000: the CHAIN-flavor overlay measured 130,120 B used
                                     * of 131,072 (ovl_ring.map .text 0x1fa70 + .bss 0x1d8) = 952 B free,
                                     * and the batch adds ~31 KB of code. The +32 KB extension
                                     * [0x226B0000,0x226B8000) is the VERIFIED-FREE cart gap cited at
                                     * p6_io_main.cpp:1978-1980 ("32 KB past the overlay window end
                                     * 0x226B0000") -- the next occupant above is p6_pool_remap at
                                     * 0x226B8000 (p6_io_main.cpp:1986), then p6_scan_sorted 0x226B9000,
                                     * p6_scan_always 0x226BB000, p6_pool_remap_inv 0x226BC000, the
                                     * stream free-list block 0x226BD000..0x226BE304, the shadow buffer
                                     * 0x226C0000 and the GFS windows at 0x22700000. The ONLY consumers
                                     * of P6_OVL_WINDOW are the loader's read cap + window-tail zero +
                                     * cache purge (p6_io_main.cpp:3949-3958) -- GLOBALS decoupled to
                                     * fixed WRAM-H 0x060CA000 long ago (W17/#258), so nothing else
                                     * moves. Window end 0x226B8000 == p6_pool_remap home: DO NOT grow
                                     * further without relocating the pool-remap block. (Chain TUs
                                     * BadnikHelpers/Explosion/Animals live in the PACK, not here --
                                     * Game_Player.o references them.) */

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
    /* Batch 3 (2026-07-09, GHZ gameplay parity): pack-global rewire slots + pack->    */
    /* overlay call forwards for the ItemBox/Debris ports (both objects are OVERLAY-   */
    /* resident; the PACK keeps NULL placeholders in p6_closure_edge.c that pack TUs    */
    /* read: Zone.c:380 foreach_active(ItemBox), SaveGame.c:133/295 (broken-box ATL     */
    /* recall), Ice.c:235/593 (PGZ-dead), Shield.c:194+ CREATE_ENTITY(Debris,           */
    /* Debris_State_Move) -- the lightning-shield spark FX). Same #235 Ring-seam +      */
    /* #258b forward patterns as animals_slot / loserings_fn above.                     */
    void *itembox_slot;               /* &ItemBox  (overlay's registered object**)   */
    void *debris_slot;                /* &Debris   (overlay's registered object**)   */
    void *itembox_break_fn;           /* real ItemBox_Break        (Ice.c:600 caller) */
    void *itembox_state_broken_fn;    /* real ItemBox_State_Broken (SaveGame.c:138    */
                                      /* assigns it -> StateMachine_Run hits the pack */
                                      /* stub -> forward keeps the broken box inert-  */
                                      /* correct. NOTE: SaveGame.c:300's POINTER      */
                                      /* compare (state == ItemBox_State_Broken) still */
                                      /* sees the PACK stub address vs the overlay-set */
                                      /* address -> broken boxes are NOT re-recorded   */
                                      /* on ATL store; documented divergence.)         */
    void *itembox_state_falling_fn;   /* real ItemBox_State_Falling (uniform forward) */
    void *itembox_state_idle_fn;      /* real ItemBox_State_Idle    (uniform forward) */
    void *debris_state_move_fn;       /* real Debris_State_Move -- Shield.c (PACK)     */
                                      /* assigns the pack stub as the spark's state;   */
                                      /* the forward runs the real mover so the spark  */
                                      /* animates + self-destroys (no entity leak).    */
    /* I3b 2b: the camera-local-pool MATERIALIZE (overlay-resident per the residency  */
    /* rule -- new engine code goes to cart). Reconstructs scene entity `logical_slot`*/
    /* from the cart DORM store into `dest_slot`; the pack drives it one-shot at load  */
    /* (s_ovl.materialize_fn) + per-frame at the shrink. The overlay does the DORM     */
    /* navigation + var-replay; the ENGINE-touching ops are pack extern "C" thunks.   */
    void (*materialize_fn)(unsigned logical_slot, unsigned dest_slot);
    /* I3b 2b COMPACTION (overlay-resident per the residency rule -- new engine code   */
    /* goes to cart, NOT WRAM-H). Relocates every populated scene entity into a dense  */
    /* physical pool via the non-identity remap (byte-plan proven by                   */
    /* qa_p6_pool_compact_model), reserves a classID=0 dummy, flips p6_pool_scene_phys  */
    /* (via the pack thunk p6_eng_pool_flip). The pack drives it one-shot at load.      */
    void (*compact_fn)(void);
    /* I3b 2b STREAMING per-frame manager (overlay-resident). Called every frame from ProcessObjects     */
    /* (after p6_scan_update_near, before loop1): materialize newly-near scene entities from the DORM     */
    /* store + dormant newly-far ones, maintaining the free-list + lifecycle bit. The fps + WRAM-L win.   */
    void (*stream_fn)(void);
    /* M1b (qa_engine_menu_render): the Menu AUTH-GATE FLIP. The overlay (which has the   */
    /* Mania Game.h type ObjectAPICallback) installs a real zeroed APICallback struct +   */
    /* sets globals->noSave=true so MenuSetup_InitAPI returns true (offline no-save path,  */
    /* MenuSetup.c:467) -> MenuSetup_Initialize wires the UIControl row tree. APICallback/ */
    /* globals are PACK symbols the overlay writes via -R; the struct is overlay .bss.     */
    /* The pack calls this ONCE at the top of the first p6_frontend_frame (BEFORE the      */
    /* scene's first StaticUpdate runs InitAPI) -- it cannot run pre-overlay-load, so the  */
    /* call site is the per-frame frontend tick (one-shot), NOT p6_menu_reload. NULL on    */
    /* the GHZ/Logos/Title flavors (the overlay only sets it under P6_FRONTEND_MENU).      */
    void (*menu_apic_init_fn)(void);
    /* M2b/M3 (Task #294/#295): the Menu LAYOUT apply, called by the pack EACH FRAME    */
    /* after ProcessObjects + BEFORE ProcessObjectDrawLists. The overlay (which has the  */
    /* Mania Game.h EntityUIModeButton type) (1) overrides the 4 UIModeButton world      */
    /* positions to the Saturn-native 320-fit 2x2 grid keyed on buttonID, and (2) writes */
    /* the active "Main Menu" UIControl's scroll origin (position>>16 - center) into the  */
    /* pack witnesses p6_w_menu_force_scrx/scry. The pack then forces currentScreen->     */
    /* position to that origin (currentScreen is a C++-mangled pack symbol the flat-C     */
    /* overlay cannot name, so the pack does the actual write). Together: the world->     */
    /* screen transform (UIControl_Draw:52-53) lands every row + plate on-screen, fitting */
    /* 320 cleanly. NULL on GHZ/Logos/Title (overlay sets it only under P6_FRONTEND_MENU).*/
    void (*menu_layout_fn)(void);
    /* StarPost port (2026-07-17): the checkpoint class is OVERLAY-resident        */
    /* (Game_StarPost.o) but its STATIC state (postIDs/playerPositions/stored*)    */
    /* is read+written by PACK TUs on the death-respawn chain: SaveGame.c:96-158   */
    /* (recallEntities restore + new-act reset), Zone.c:883 (State_ReloadScene     */
    /* stores the clock), GameOver.c:319, PauseMenu.c:476-501, Player.c:2224.      */
    /* Those bind to the pack `StarPost` placeholder (p6_closure_edge.c), so the   */
    /* pack global is REWIRED per-frame to this slot (the #235 Ring-seam pattern,  */
    /* same as animals_slot/itembox_slot) -> one shared ObjectStarPost instance.   */
    void *starpost_slot;              /* &StarPost (overlay's registered object**) */
    /* ActClear.c:766/790 (PACK) calls StarPost_ResetStarPosts -> binds to the     */
    /* pack stub (P6_EDGE(55)); the stub forwards through this to the overlay's    */
    /* REAL reset (the #258b p6_ovl_loserings_raw pattern).                        */
    void *starpost_reset_fn;          /* real StarPost_ResetStarPosts              */
#if defined(P6_GHZCUT_BOOT)
    /* Task #309 Tier-B.1 (GHZCutscene FXRuby fade): the overlay reads the live   */
    /* FXRuby entity's fade fields into engine-visible ints. The overlay has the  */
    /* Mania Game.h EntityFXRuby type; the engine TU (p6_io_main) cannot name it, */
    /* so the overlay does the foreach_all(FXRuby) + (under P6_GHZCUT_HOLD) the    */
    /* freeze-pin, and hands fadeWhite/fadeBlack back so the engine applies the    */
    /* VDP2 color offset (p6_vdp2_fade_apply). LAST field + gated so non-GHZCUT    */
    /* builds keep sizeof(p6_ovl_api) byte-identical (the field grows .bss 4 B).   */
    /* Both the overlay (p6_ovl_ghz.o) and engine (p6_io_main.o) compile with the  */
    /* SAME P6_GHZCUT_BOOT state, so the struct layout is consistent per build.    */
    void (*fade_fn)(int *outWhite, int *outBlack);
#endif
} p6_ovl_api;

/* The entry signature at P6_OVL_BASE: registers the overlay's classes via
 * api->register_object, fills the api outputs, returns the engine
 * objectClassCount as observed by the registration thunk's last call (the
 * thunk-side counter -- the overlay cannot read engine globals directly). */
typedef int (*p6_ovl_entry_t)(p6_ovl_api *api);

#endif /* P6_OVL_API_H */
