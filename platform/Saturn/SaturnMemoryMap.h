// =============================================================================
// SaturnMemoryMap.h -- the P6.8 SHIPPING-image memory architecture (P6.7b,
// Task #210; AMENDED P6.7c). THE BINDING LAYOUT CONTRACT for the engine
// true-port at GHZ scale, enforced arithmetically by
// tools/_portspike/qa_p6_memmap.py (which LIVE-parses the GHZ1 scene
// population every run -- any constant drift or scene-data growth that breaks
// a bank fires the gate).
//
// MEASURED INPUTS (2026-06-10, amended 2026-06-11):
//   - GHZ1 Scene1.bin places 1,041 entities (largest class 446 = Ring).
//   - EntityBase = 344 B (P4 data retarget, Task #203).
//   - The PC flat layout at this scale: entityList 430 KB + typeGroups
//     321 KB -- does NOT fit alongside pools/tileset in 2 MB. Two design
//     moves close it:
//       1. typeGroups/drawGroups hold only INRANGE entities (the engine
//          writes them inside the inRange branch, Object.cpp:462-493), so
//          their entry arrays are capped at P68_*_ENTRY_CAP (camera-culled
//          peak ~0x80-0x100, NOT scene population) with a Saturn-gated
//          CLAMP at the write sites making overflow safe-and-witnessed
//          instead of the silent .bss-corruption class (Phase 1.4-1.15).
//       2. tilesetPixels + the group/class tables live in WRAM-H; the
//          entityList is the major WRAM-L tenant beside the storage heap.
//   - P6.7c AMENDMENT (adversarial verification findings): the P6.7b
//     contract OMITTED three real tenants --
//       (a) dataStorage bookkeeping: 5 datasets x sizeof(DataStorage).
//           HARD COUPLING: the 16,404 B stride is valid ONLY with the
//           Saturn STORAGE_ENTRY_COUNT 0x800 branch (Storage.hpp, landed
//           in the same P6.7c commit); at the stock 0x1000 the stride is
//           32,788 B and the declared window overruns its neighbors.
//       (b) tileLayers: LAYER_COUNT 4 x sizeof(TileLayer) 13,384 =
//           53,536 B (a WRAM-L tenant in every diag map since P6.3).
//       (c) collisionMasks + tileInfo: 141,312 B raw -- the DISCOVERED
//           GAP below; NOT yet placed in either bank.
//     Funded by heap 0x60000 -> 0x52000 (the diag-PROVEN window: pools
//     272 KB + ~44 KB inflate transient + slack) and GROUPB 0x15000 ->
//     0xC000 (diag actual 21.2 KB + pad).
//
// CONSUMERS: Object.hpp Saturn branch picks up the P68_* entity counts and
// entry caps at the P6.8 flip (the P6.7a/c diag image keeps Title-scale
// 0xC0); the WRAM-L .equ absolute map in the P6.8 boot TU mirrors the
// placement order below.
// =============================================================================
#ifndef SATURN_MEMORY_MAP_H
#define SATURN_MEMORY_MAP_H

// ---- Entity population (Object.hpp consumers at P6.8) -----------------------
#define P68_RESERVE_ENTITIES    (0x40)  // engine-reserved slots (Player etc.)
#define P68_SCENE_ENTITIES      (0x440) // 1088 >= 1041 measured + 47 headroom
#define P68_TEMP_ENTITIES       (0x80)
// ENTITY_COUNT = 0x500 (1280) -> objectEntityList = 1280 * 344 = 440,320 B
// NOTE (P6.7c verification): LoadSceneAssets' REV02 branch routes raw scene
// slotIDs >= SCENEENTITY_COUNT into the throwaway tempEntityList with NO
// clamp (Scene.cpp:545-548) -- at diag Title scale (SCENE 0x40) entities with
// slotID 0x40..0x7F are silently discarded and >= 0x80 would OOB the TMP
// pool. The P68_SCENE_ENTITIES retarget above is what retires that class for
// GHZ (max raw slotID < 0x440); the Saturn clamp at the write site is the
// P6.8-flip companion change.

// ---- Group-list entry caps (inRange-bounded, clamped at write) ---------------
#define P68_TYPEGROUP_ENTRY_CAP (0x100) // camera-culled inRange peak << this
#define P68_DRAWGROUP_ENTRY_CAP (0x100)
#define P68_TYPEGROUP_COUNT     (0x84)  // TYPE_COUNT 0x80 + 4 (Saturn branch)
#define P68_DRAWGROUP_COUNT     (16)
#define P68_OBJECT_COUNT        (0x100)
// typeGroups = 0x84*(2*0x100+4) = 68,112 B   (stock at this scale: 321 KB)
// drawGroups = 16*(2*0x100+40)  =  8,832 B

// ---- WRAM-L (0x00200000, 1 MB) tenants, placement order ----------------------
#define P68_LWRAM_HEAP_BYTES         (0x52000) // storage pools 272 KB + miniz
                                               // inflate ~44 KB transient +
                                               // slack (diag-PROVEN since
                                               // P6.5b1; was 0x60000 spec)
// objectEntityList                  (440,320 B, derived above)
#define P68_LWRAM_DATASTORAGE_BYTES  (0x14100) // 5 x 16,404 (DataStorage at
                                               // STORAGE_ENTRY_COUNT 0x800 --
                                               // Saturn branch, Storage.hpp;
                                               // P6.7c discovered tenant)
#define P68_LWRAM_TILELAYERS_BYTES   (0xD200)  // 4 x 13,384 = 53,536 + pad
                                               // (P6.7c discovered tenant)
#define P68_LWRAM_DATAFILELIST_BYTES (0xE000)  // RSDKFileInfo[0x700]
#define P68_LWRAM_GROUPB_BYTES       (0xC000)  // palettes/gfxSurface/IDs/rgb
                                               // tables (diag actual 21.2 KB)
                                               // + pad (was 0x15000 spec)
#define P68_LWRAM_MARGIN_MIN         (0x7000)  // 28,672 B contracted floor
                                               // (actual 30,176 B -- see gate)

// ---- WRAM-H (0x06000000, 1 MB) tenants ---------------------------------------
#define P68_HWRAM_CODE_BYTES    (0x58000) // engine core text 117 KB (spike) +
                                          // Global object set + jo/SGL libs +
                                          // the per-zone code-overlay window
#define P68_HWRAM_SGL_RESERVE   (0x40000) // 0x060C0000..0x06100000 work area
#define P68_HWRAM_TILESET_BYTES (0x40000) // engine tilesetPixels (aniTiles)
// typeGroups + drawGroups + objectClassList (94,352 B, derived above)
#define P68_HWRAM_MISC_BYTES    (0x8000)  // screens/scanlines/sfxList/channels/
                                          // witnesses/audio device buffers
#define P68_HWRAM_MARGIN_MIN    (0x8000)  // 32 KB contracted floor

// ---- DISCOVERED GAP (P6.7c): collision-data residency -----------------------
// LoadTileConfig fills collisionMasks[CPATH 2][TILE 1024] (64 B/tile) +
// tileInfo (5 B/tile) = 141,312 B raw -- read EVERY frame by the sensor hot
// path at GHZ. NEITHER bank holds it under the contract above (WRAM-L margin
// 30,176 B; WRAM-H margin 36,720 B). NOT YET PLACED -- the P6.7d design
// decision, user-checkpointed. Candidate paths (exact byte deltas):
//   1. 4-bit-packed masks (heights are 0x0..0xF + 0xFF sentinel; two columns
//      per byte behind the Task #203 COLLISION_TILE_MASK accessor seam):
//      65,536 + 10,240 = 75,776 B -- still exceeds either margin alone;
//      needs a companion trim (e.g. TEMP_ENTITIES 0x80 -> 0x60 frees
//      11,008 B; heap/GROUPB have no proven slack left).
//   2. WRAM-H placement + code-window trim: 141,312 raw (or 75,776 packed)
//      vs the 0x58000 CODE reserve -- decidable only after the P6.7d
//      overlay-window measurement.
//   3. Per-zone overlay co-residency: collision tables ride the zone code
//      overlay (they are per-stage constants, loaded at the same boundary).
#define P68_COLL_RAW_BYTES    (0x22800) // 141,312
#define P68_COLL_PACKED_BYTES (0x12800) // 75,776
#define P68_COLLISION_PLACED  (0)       // gate prints the open-gap WARNING

#endif // SATURN_MEMORY_MAP_H
