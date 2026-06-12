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
// P6.7 Player wave step B (Task #227): TEMP halves to fund the DUAL-STRIDE
// pool -- reserve+temp slots are WIDE (556 B >= EntityPlayer; oversize
// entities are reserve/temp-resident by decomp construction), scene slots
// stay 344. Mirrors Object.hpp ENTITY_WIDE_SIZE / TEMPENTITY_COUNT.
#define P68_TEMP_ENTITIES       (0x40)
#define P68_ENTITY_WIDE_BYTES   (556)
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
#define P68_LWRAM_HEAP_BYTES         (0x43000) // C2+C5 LANDED: pools 224 KB
                                               // (TMP 80K -- TileConfig's
                                               // verbatim inflate is 77,824 B
                                               // decompressed and bounds it;
                                               // a 64K trim fired the
                                               // collision gate RED) + miniz
                                               // ~44 KB transient + slack
// objectEntityList                  (440,320 B, derived above)
#define P68_LWRAM_DATASTORAGE_BYTES  (0x6100)  // C1 LANDED: 5 x 32 B structs
                                               // + per-dataset entry backings
                                               // (STG 0x800, others 0x100) x
                                               // 8 B = 24,736 (Storage.hpp/
                                               // .cpp C1 arms; was 82,176 at
                                               // the uniform 0x800)
#define P68_LWRAM_TILELAYERS_BYTES   (0x1A300) // 8 x 13,384 = 107,072 + pad --
                                               // P6.7 W11 census: stages use
                                               // up to 8 layers (FBZ/TMZ1);
                                               // the P4 "<=4" trim falsified
                                               // (GHZ1 itself uses 5)
#define P68_LWRAM_DATAFILELIST_BYTES (0xA000)  // C3 LANDED: RSDKFileInfo
                                               // packed to 24 B (raw size
                                               // word keeps bit31=encrypted;
                                               // packID/useFileBuffer =
                                               // constant-0 macros, single
                                               // pack) x DATAFILE_COUNT
                                               // 0x6A0 = 40,704 (1.03 pack
                                               // = 1677 files measured)
#define P68_LWRAM_GROUPB_BYTES       (0x8000)  // C4 LANDED: palettes/
                                               // gfxSurface/IDs/rgb tables --
                                               // diag actual 21,184 B + pad
                                               // (was 0xC000)
#define P68_LWRAM_MARGIN_MIN         (0x7000)  // 28,672 B contracted floor
                                               // (actual 30,176 B -- see gate)

// ---- WRAM-H (0x06000000, 1 MB) tenants ---------------------------------------
// P6.7d.2 RE-CONTRACT (MEASURED): the stock SGL work area (0x060C0000+,
// 256 KB, SGLAREA.O sized for MaxPolygons=1761 3D scenes) is REPLACED by the
// engine-sized platform/Saturn/SaturnSGLArea.c block (MAX_POLYGONS=144 /
// MAX_VERTICES=384 at 0x060F4000, 28,472 B used; formulas per SGL302
// WORKAREA.TXT secs 1-7; PROVEN live: all 13 P6 gates GREEN on the new area,
// per-frame command count 5 vs the 144 ceiling, gate qa_p6_sglarea G1-G3).
// The freed 212,992 B fund the code window growth + collision placement.
#define P68_HWRAM_CODE_BYTES    (0x7A000) // 499,712: engine core ~100-117K +
                                          // retained jo/SGL ~60K + hot-resident
                                          // objects 204K (-Os census) + the
                                          // SPZ-sized overlay window 124,029
#define P68_HWRAM_SGL_RESERVE   (0xC000)  // 0x060F4000..0x06100000: the
                                          // engine-sized area + TransList +
                                          // SystemWork/stack headroom
#define P68_HWRAM_TILESET_BYTES (0x40000) // engine tilesetPixels (aniTiles)
// typeGroups + drawGroups + objectClassList (94,352 B, derived above)
#define P68_HWRAM_MISC_BYTES    (0x8000)  // screens/scanlines/sfxList/channels/
                                          // witnesses/audio device buffers
#define P68_HWRAM_COLL_BYTES    (0x12800) // collisionMasks+tileInfo, 4-bit-
                                          // PACKED masks (75,776 B; the W2
                                          // closer -- raw 141,312 stays the
                                          // qa model until the packer lands)
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
#define P68_COLLISION_PLACED  (1)       // P6.7d.2: WRAM-H tenant (PACKED form;
                                        // P68_HWRAM_COLL_BYTES above), funded
                                        // by the SGL work-area re-contract.
// P6.7 PACKER LANDED (Task #210): EXACT 16-bit/column packed masks at the
// diag window 0x060E0000 (65,536 B; ends 0x060F0000, 16 KB under the SGL
// floor); tileInfo (10,240 B) stays at its WRAM-L window for now -- the
// 75,776 contract total = 65,536 + 10,240 across the two banks. The
// accessors derive FLIP_X/Y/XY masks AND angles on the fly (Task #203
// approximation RETIRED; byte-exact + 128-probe proof = qa_p6_collision).
// The raw x1 collisionMasks WRAM-L window (0x20000 B) is now DEAD -- a
// future WRAM-L reclaim of 131,072 B.

// ---- P6.7 W11 (Task #210, 2026-06-11): LAYER-LAYOUT RESIDENCY -- DECLARED
// OPEN GAP (the P6.7c-collision pattern: measured, declared, gated; design
// of record below, implementation = its own iteration).
// MEASURED (whole-game Scene.bin census via the parse_title_entities walk):
//   GHZ1: 5 layers, layouts 551,168 B (FG Low/High 1024x128 = 262,144 each
//         + BG Outside 24,576 + BG Caves 2x1,152)
//   worst zone: FBZ/Scene2 = 8 layers, layouts 3,006,976 B
//   distinct layout words: GHZ FG Low 1,149 / FG High 765 (8-bit indirection
//   does NOT fit; 12-bit would, but saves only 128 KB and cannot touch FBZ)
// The engine allocates layouts from DATASET_STG (Scene.cpp:421, xsize*ysize*2)
// and reads them at 27 sites (16 Collision.cpp sensor fetches + GetTile/
// SetTile/CopyTileLayer + the VDP2 present). FULL residency is IMPOSSIBLE at
// FBZ scale on any packing. DESIGN OF RECORD (forced -- every consumer is
// camera-local: sensors run only for inRange entities, the VDP2 present
// streams the visible region): a CAMERA-LOCAL SLIDING WINDOW per layer over
// band-recompressed layouts -- LoadSceneAssets streams each layer's inflate
// into fixed row-band chunks in a budgeted store; a windowed accessor seam
// (the RSDK_*_MASK macro pattern) serves reads from per-layer windows
// refilled on camera crossings; SetTile writes through to the band store.
// Declared budgets (enforced by qa_p6_memmap; the full-ledger arithmetic
// CLOSES only with the W11 CLOSER SET below -- each a real, mechanical,
// individually-gateable change; PLANNED until its line flips to LANDED):
#define P68_LAYOUT_WINDOW_BYTES (0x8000)  // COLLIDABLE layers only (FG
                                          // Low/High): 2 x 64-col x 128-row
                                          // x 2 B windows; render-only BG
                                          // layers decode bands straight to
                                          // VDP2 pages at crossings (no
                                          // resident window)
#define P68_LAYOUT_BANDSTORE_BYTES (0xE000)  // deflated band store: GHZ1
                                          // MEASURED 51,094 B + slack
                                          // (funds the C2 TMP correction;
                                          // big zones trade against their
                                          // overlay slack per-zone)
#define P68_W11_LAYOUTS_OPEN  (1)         // flips to 0 when the window seam
                                          // lands with its byte-exact gate
//
// W11 CLOSER SET (ledger deltas vs the pre-W11 contract; PLANNED):
//   C1 [LANDED 2026-06-11] per-dataset STORAGE_ENTRY_COUNT (STG 0x800,
//      others 0x100): dataStorage bookkeeping 82,176 -> 24,832
//   C2 [LANDED 2026-06-11] DATASET_TMP 128K -> 80K (largest TMP
//      transient after the capped tempEntityList is the GIF decoder ~25 KB;
//      layout inflate streams in 16 KB chunks into the band packer)
//   C3 [LANDED 2026-06-11] dataFileList 24-byte packed records (raw size
//      word, accessor macros RSDKFILE_* in Reader.hpp; DATAFILE_COUNT
//      0x700 -> 0x6A0): 57,344 -> 40,960
//   C4 [LANDED 2026-06-11] GROUPB pad trim 49,152 -> 32,768
//   C5 [LANDED 2026-06-11] heap window pools-exact: 0x52000 -> 0x3F000
// Post-closer WRAM-L: 258,048 heap + 440,320 entityList + 24,832 ds +
// 107,072 tileLayers + 40,960 dfl + 32,768 groupB + 32,768 window +
// 81,920 bands = 1,018,688 -> margin 29,888 >= the 28,672 floor.

// ---- P6.7 WAVE-1 (Task #210): GAME GLOBALS WINDOW ----------------------------
// GlobalVariables lives at a FIXED WRAM-H window inside the P6.7d.2-freed
// region (the engine's RegisterGlobalVariables would back it from the 64 KB
// DATASET_STG pool, which already carries ~13 KB of scene lists -- the seam
// in p6_io_main.cpp overrides that one table slot). Sizes are generated +
// self-tested by tools/_portspike/_p6/gen_globals_map.py (S1-S6):
//   verbatim pre-Plus sizeof = 268,148 B; SATURN_GLOBALS_RETARGET = 56,180 B
//   (atlEntityData 0x4000->0x800, saveRAM 0x4000->0x2000 [32 KB = the Saturn
//   backup-RAM ceiling], menuParam 0x4000->0x800, competitionSession
//   0x4000->0x100; all GameConfig seeds scalar-targeted; the v5U engine
//   discards seeds and drives the by-name GlobalVariables_InitCB instead).
// BUDGET SQUEEZE (P6.8 watch item): proof overlay window 0x8000 + globals
// 56,180 + SPZ zone window 124,029 = 212,977 of the 212,992 B freed region
// -- 15 B of slack if the zone window replaces the proof window at
// 0x060C0000 and globals move to its tail. Closers if SPZ growth eats it:
// menuParam 0x800->0x400 (-4 KB) or zone-window split.
#define P68_HWRAM_GLOBALS_BASE  (0x060C8000) // = P6_OVL_BASE + P6_OVL_WINDOW
#define P68_HWRAM_GLOBALS_BYTES (56180)      // gen_globals_map.py sat_sizeof

// ---- P6.7d SIZING RECORD (MEASURED 2026-06-11, tools/_portspike/_p67d_sizing)
// The COMPLETE verbatim decomp object set (540 TUs) compiled to SH-2 in the
// SHIPPING configuration (-Os, -DGAME_VERSION=3 = 1.03 pre-Plus, REVISION=2;
// 21 Plus-only TUs correctly absent). text+rodata:
//   TOTAL all objects                1,494,886 B
//   resident cand. (Glob+Com+Help)    224,018 B  (hot subset after TA/
//                                                 Competition demotion ~204 KB)
//   overlay window = max zone (SPZ)    124,029 B  (LRZ 120,983; GHZ 57,241;
//                                                 Menu pre-Plus 96,669)
// Entity slots (compiler-truth probe, entity_sizes_103.json): 68 of 554
// classes exceed the 344 B slot; gameplay-hot offenders are ONLY Player
// (556 B) and TitleCard (864 B) -> stride STAYS 344, per-class Saturn shrink
// behind the static_assert wall (the +287 KB stride-raise path is dead).
// W4 CODE-BUDGET GAP (declared open, decided with the overlay design):
// hot-resident + window + engine core + retained jo/SGL exceeds the
// 0x58000 code reserve by roughly 115-155 KB before collision placement.
// Candidate closers, in checkpoint order: SGL work-area 0x40000 -> 0x20000
// (+131 KB; requires ST-238 work-area verification), TMP pool trim
// (+32 KB), further cold-Global demotion into overlays. The formal
// re-contract of P68_HWRAM_CODE_BYTES lands WITH the P6.7d overlay design.

// ---- P6.7 W12 (Task #227, 2026-06-11): SPRITE-SHEET RESIDENCY -- DECLARED
// OPEN GAP (the W2/W11 pattern: measured, declared, gated; design of record
// below; implementation = its own gated iteration, PREREQUISITE of the
// Player wave -- Player_StageLoad cannot run without it).
// MEASURED (PIL over extracted/Data/Sprites, decoded 8bpp bytes):
//   Players/Sonic1+2+3.gif   3 x 512x512 = 786,432 B  (Player_StageLoad set)
//   Global/ShieldS|s.gif     512x512     = 262,144 B each
//   GHZ/Objects.gif          512x256     = 131,072 B
//   Global/Items.gif         256x128     =  32,768 B (the P6.5b2 proof fit)
// LoadSpriteSheet decodes whole sheets into DATASET_STG (64 KB pool) --
// residency IMPOSSIBLE at gameplay scale; the FR-1/FR-2 hand-port lessons
// (lazy VDP1 residency + CD-streamed frame packs) recur in the engine port.
// DESIGN OF RECORD: the WORKING SET already lives in VDP1 VRAM via the
// P6.5b3 rect-keyed DrawSprite slot cache (each DISTINCT frame rect
// uploaded exactly once). The seam is the slot-cache MISS path: gfxSurface
// keeps its header (size/lineSize/hash) but large sheets keep NO resident
// pixel backing -- a miss fetches the frame's rows from an offline
// row-band store (cd/<SHEET>SHT.BIN, the W11 band codec + builder pattern)
// through a bounded scratch window, then uploads to VDP1 as today. Small
// sheets (<= a residency threshold, e.g. Items.gif) stay resident verbatim.
#define P68_SHEET_RESIDENT_MAX  (0x10000) // sheets <= 64 KB decoded stay
                                          // DATASET_STG-resident (Items.gif
                                          // class); larger = banded
#define P68_SHEET_SCRATCH_BYTES (0x4000)  // miss-path band scratch (largest
                                          // gameplay frame rect rows; exact
                                          // bound measured at the W12
                                          // builder, contract ceiling here)
#define P68_W12_SHEETS_OPEN     (1)       // flips to 0 when the seam lands
                                          // with its byte-exact gate

#endif // SATURN_MEMORY_MAP_H
