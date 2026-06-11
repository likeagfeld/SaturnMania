// =============================================================================
// SaturnMemoryMap.h -- the P6.8 SHIPPING-image memory architecture (P6.7b,
// Task #210). THE BINDING LAYOUT CONTRACT for the engine true-port at GHZ
// scale, enforced arithmetically by tools/_portspike/qa_p6_memmap.py (which
// LIVE-parses the GHZ1 scene population every run -- any constant drift or
// scene-data growth that breaks a bank fires the gate).
//
// MEASURED INPUTS (2026-06-10):
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
//
// CONSUMERS: Object.hpp Saturn branch picks up the P68_* entity counts and
// entry caps at the P6.8 flip (the P6.7a diag image keeps Title-scale
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

// ---- Group-list entry caps (inRange-bounded, clamped at write) ---------------
#define P68_TYPEGROUP_ENTRY_CAP (0x100) // camera-culled inRange peak << this
#define P68_DRAWGROUP_ENTRY_CAP (0x100)
#define P68_TYPEGROUP_COUNT     (0x84)  // TYPE_COUNT 0x80 + 4 (Saturn branch)
#define P68_DRAWGROUP_COUNT     (16)
#define P68_OBJECT_COUNT        (0x100)
// typeGroups = 0x84*(2*0x100+4) = 68,112 B   (stock at this scale: 321 KB)
// drawGroups = 16*(2*0x100+40)  =  8,832 B

// ---- WRAM-L (0x00200000, 1 MB) tenants, placement order ----------------------
#define P68_LWRAM_HEAP_BYTES         (0x60000) // storage pools + malloc (STG
                                               // grows for GHZ sheets/layouts)
// objectEntityList                  (440,320 B, derived above)
#define P68_LWRAM_DATAFILELIST_BYTES (0xE000)  // RSDKFileInfo[0x700]
#define P68_LWRAM_GROUPB_BYTES       (0x15000) // palettes/gfxSurface/IDs/rgb
                                               // tables + growth pad
#define P68_LWRAM_MARGIN_MIN         (0x10000) // 64 KB contracted floor

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

#endif // SATURN_MEMORY_MAP_H
