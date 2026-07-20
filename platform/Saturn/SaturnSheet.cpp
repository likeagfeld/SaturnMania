// =============================================================================
// SaturnSheet.cpp -- P6.7 W12 (Task #227): sprite-sheet ROW-BAND stores in
// VDP2 VRAM + the rect fetch the VDP1 slot-cache miss path consumes.
//
// WHY (W12 declaration, SaturnMemoryMap.h): LoadSpriteSheet's whole-sheet
// decode cannot be resident at gameplay scale (Player set = 786,432 B raw
// vs the 64 KB DATASET_STG pool). The working set lives in VDP1 via the
// P6.5b3 rect-keyed DrawSprite slot cache; a cache MISS fetches the frame's
// rows from these band stores (cd/<NAME>.SHT, built by
// tools/build_sheet_bands.py -- the W11 band codec).
//
// PLACEMENT (MEASURED, task #227): the blobs (155,339 B for SONIC1/2/3)
// fit NEITHER work-RAM bank next to the 102,004 B closure code -- they
// live in VDP2 VRAM B0-tail/B1 (0x25E44000.., ~240 KB free beyond the
// NBG1 cells at A0/A1 + the 16 KB map at B0 head; CPU access to B0 is
// PROVEN live in this build -- the P6.5b1 map writes).
//
// ACCESS CONTRACT (pinned p6_vdp2.c per ST-058): VDP2 VRAM in 16-bit units
// ONLY. All staging copies here are u16 loops; the compressed band is
// copied VDP2 -> WRAM scratch before miniz ever sees it (inflate runs
// WRAM -> WRAM exclusively).
//
// SCRATCH: shares the 32 KB P6_LW_LAYSCRATCH window with SaturnLayout
// (synchronous consumers, never concurrent): low half = compressed band
// copy (largest SHT band zlib < 8 KB), high half = inflated band rows
// (16 rows x 512 px = 8,192 B).
//
// Gate: qa_p6_sheet.py (file-hash staging proof + probe rects replayed
// through SaturnSheet_FetchRect vs the offline p6_sheet_model.json).
// =============================================================================
#include <string.h>

#include "miniz/miniz.h"

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef signed int int32;

// W19 (Task #227): 8 slots. Task #241 main: the band store is RELOCATED from
// the 245,760 B VDP2 VRAM window to a 384 KB region in the 4MB Extended-RAM
// cart (cache-through alias 0x227A0000..0x22800000), funded by shrinking
// DATASET_TMP 768->640KB (Storage.cpp). The cart dissolves the VDP2 window wall
// (which fit only 6 sheets at 206,222 B): the full 7-sheet set SONIC1/2/3 +
// ITEMS + DISPLAY + SHIELDS + TAILS1 = 264,865 B now co-resides (no
// SHIELDS<->TAILS trade -- the sidekick body + shield FX both stay resident).
// Cart is plain CPU-addressable memory (A-Bus; wait-stated but read once per
// band-fetch, NOT per frame), so the VDP2 "16-bit only" access contract no
// longer applies -- the existing u16 copy loops are kept (correct + harmless on
// the cart). Map: [[cart-4mb-extram-measured-map]]. The VDP2 0x25E44000 window
// is the non-cart fallback (kept so a hypothetical non-P6_CART build still
// links + boots; it stages only the 6 that fit).
// #247/#181: 9 GHZ content sheets. CP4 (#266): the FRONT-END Logos flavor adds a 10th
// slot for LOGOS.SHT (Logos.gif splash). CP5b.1 (#268): the FRONT-END TITLE flavor adds
// an 11th slot for TLOGO.SHT (Title/Logo.gif). CP5b.2 (#269): the TITLE flavor adds a
// 12th slot for TSONIC.SHT (Title/Sonic.gif, the ring-center head) -- it ALSO stages
// LOGOS + TLOGO (TITLE implies LOGOS, so slot 9 = LOGOS, slot 10 = TLOGO, slot 11 =
// TSONIC). KEPT 9 in the DEFAULT (GHZ) build so its s_sheets[] is byte-identical -- the
// default shipping _end is only ~96 B under the #228 ANIMPAK floor, so the 32 B/slot of
// an unconditional extra slot would breach it. Only the front-end builds (which gc-drop
// the large p6_ghz_frame/reload -> ~1.6 KB of headroom) carry the extra slots.
// NSHEETS=12 (VDP1 bind table) is unchanged either way (the Title bind demand is the 3
// Title surfaces -- logo + sonic + electricity -- + a few engine surfaces << 12).
#if defined(P6_FRONTEND_MENU)
// M1b: the MENU flavor (implies TITLE -> LOGOS) adds 2 slots for the main-menu sheets
// MAINICON.SHT (slot 13, UI/MainIcons.gif) + TEXTEN.SHT (slot 14, UI/TextEN.gif) on
// top of the 13 Title+Logo sheets (slots 0..12). 16 = 13 + 2 + 1 margin. The front-end
// builds gc-drop the large p6_ghz_frame/reload (~1.6 KB headroom), so 7*32 B over the
// GHZ 9-slot table fits under the #228 ANIMPAK floor. (NSHEETS=12 VDP1 bind table is
// unchanged -- the menu bind demand is the 2 UI surfaces + a few engine surfaces << 12.)
// R3.1 (#305): the AIZ_TEST flavor reuses THIS 16-slot table -- AIZOBJ.SHT lands at slot
// 15 (the 1 margin). NO bump (a 17th slot shifted .bss past the front-end heap limit ->
// #228 boot trap, MEASURED _end 0x060BA360 blue-screen). NSHEETS=12 unchanged.
// STEP-3 GHZ CHAIN STAGING (2026-07-03, frontend-cart-map-recarve memory): +9 slots
// for the handoff-seam staging of the 9 GHZ gameplay sheets (SONIC1/2/3, ITEMS,
// DISPLAY, SHIELDS, TAILS1, GLOBJ, GHZOBJ) so the chain's landing renders players/
// HUD/rings. .bss +288 B is SAFE in front-end flavors: with the player pack moved
// to CART the old #228 wall (WRAM ANIMPAK 0x060B6C00) no longer applies here -- the
// real ceiling is the GLOBALS window 0x060C8000, ~52 KB above the chain _end. The
// R3.1 "17th slot trap" note below predates the pack relocation.
// BADNIK-VIS FIX (2026-07-11): 25->27 for EXPLOS.SHT + ANIMALS.SHT staged at the GHZ
// chain-handoff (p6_io_main ghzShtFiles[11]). +2 slots = +64 B .bss; SAFE in the front-end/
// chain flavors (animpak-on-cart -> the #228 WRAM ANIMPAK wall does not apply; ceiling is
// GLOBALS 0x060C8000, ~28 KB above the chain _end -- the SAME reasoning as the 16->25 bump).
// Water M1b: 27->28 for the chain-handoff WATER.SHT (a 12th GHZ-handoff sheet). Same
// certified-safe +64B .bss reserve class as the EXPLOS/ANIMALS 25->27 bump above (chain
// ceiling GLOBALS 0x060C8000, ~22KB _end headroom). Unconditional in the chain flavor (the
// reserve is inert without WATER.SHT staged, which is #if P6_WATER-gated at the handoff).
#define SATURNSHEET_SLOTS     28
#elif defined(P6_FRONTEND_TITLE)
// CP5b.3 (#272): the TITLE flavor adds a 13th slot for TBG.SHT (Title/BG.gif, the
// TitleBG + Title3DSprite mountains/water/billboard sheet) -- slot 12 (slots 9/10/11
// = LOGOS/TLOGO/TSONIC). The front-end builds gc-drop the large p6_ghz_frame/reload
// (~1.6 KB headroom), so the extra slot fits under the #228 ANIMPAK floor.
#define SATURNSHEET_SLOTS     13
#elif defined(P6_FRONTEND_LOGOS)
#define SATURNSHEET_SLOTS     10
#else
#define SATURNSHEET_SLOTS     9
#endif
                                 // BADNIK-VIS: kept at 9 (NOT grown for EXPLODE/ANIMALS) -- growing
                                 // it AND P6_VDP1_NSHEETS together tripped the #228 orphan-.bss
                                 // overlap that corrupts the GFS GfsMng ptr (boot trap 0x06000956).
                                 // Explosions/Animals keep their stock resident-pixel decode path.
#if defined(P6_CART)
// CP5b.2 (Task #269): the FRONT-END TITLE flavor stages a 12th sheet TSONIC.SHT
// (Title/Sonic.gif, 121,090 B -- the 1024x1024 head sheet). The 11 prior sheets
// (9 GHZ + LOGOS + TLOGO) already consume 331,032 B of the DEFAULT 384 KB band
// store (0x227A0000..0x22800000), so adding TSONIC needs 452,122 B and OVERFLOWS
// by 58,906 B -> SaturnSheet_Stage returns -1 -> TSONIC never stages -> the head's
// surface stays UNBOUND (handle<0) -> the head drops (MEASURED via the savestate:
// sht_staged=11 not 12, tsonic_shtslot=-1, tsonic_handle=-1, the ring interior
// black). FIX (front-end only -- the GHZ build's cart layout is BYTE-IDENTICAL):
// lower the band-store base to 0x22720000 (the GFS read windows end -- p6_gfs.c
// P6_CART_GFSWIN_BASE 0x22700000 + 2*64 KB = 0x22720000), giving the band store
// 0x22800000-0x22720000 = 0xE0000 = 917,504 B (896 KB >> 452 KB needed). TSONIC is
// staged BANDED here but DELIBERATELY NOT made resident (MEASURED: the 1024-wide
// MakeResident boundary-case -- raw band rsz = 16*1024 = 0x4000 exactly -- HANGS the
// boot; the NORES build boots clean with tsonic_handle=1, the head binds + renders
// via the per-frame banded FetchRect inflate path). So the resident store holds only
// the 11 prior sheets (1.998 MB, ending ~0x225E8000); RES_END at 0x22700000 (the GFS
// windows base) is a 3 MB region with huge room. SAFE: the front-end Title scene
// never mounts SaturnLayout (the only other 0x22600000..0x227A0000 cart user) and
// never loads the GHZ tileset/TMP cart -- so 0x22720000..0x227A0000 is free in this
// flavor. The GFS windows (0x22700000..0x22720000) sit BETWEEN the two stores, used
// only DURING the scene-load reads (no collision: resident bump-alloc reaches
// ~0x225E8000, far below 0x22700000). TSONIC's banded blob (121 KB) lives in the band
// store; FetchRect inflates its head rects per-frame (cheap on the title).
#if defined(P6_FRONTEND_TITLE)
#define SATURNSHEET_VRAM_BASE 0x22720000u // front-end: above the GFS windows (0x22720000); 896 KB band store
#define SATURNSHEET_VRAM_END  0x22800000u // top of the 4MB cart
#define SATURNSHEET_RES_BASE  0x22400000u
// F-LAND-SONIC (2026-07-03): CAP the resident bump-alloc BELOW the re-carved anim
// pack windows (OBJ 0x22640000 + PLAYER 0x22680000..0x22691000, Animation.hpp
// front-end arms). MEASURED: the player pack resolved INTACT at arm time (spr->
// frames captured = pak+0x77C exactly) but Sonic's animation table read -1 later
// -- the chain's cumulative MakeResident traffic (6 scenes vs the title-only
// ~0x225E8000 fill this end was sized against) grew across 0x22680000 and smashed
// the pack head (Tails' table at +0xACB4 survived = the exact symptom split).
// Shortfall degrades gracefully: Stage's resident-fit check falls back to banded.
#define SATURNSHEET_RES_END   0x225A0000u // FINAL CARVE: 1.625 MB resident store; packs at
                                          // 0x225A0000/0x225E0000, SaturnLayout at 0x22600000+
#else
#define SATURNSHEET_VRAM_BASE 0x227A0000u // 4MB cart, after STG(3MB)+TMP(640KB)
#define SATURNSHEET_VRAM_END  0x22800000u // top of the 4MB cart (384 KB store)
// Task #243 Lever 1 (render perf): the per-frame miniz band-inflate in
// SaturnSheet_FetchRect was MEASURED as the render bottleneck (~43 ms/frame,
// 3.2 inflates/frame from the wait-stated cart). Fix: decompress each sheet ONCE
// at load into a RESIDENT buffer in the otherwise-unused 3.64 MB of the cart
// (Storage stays in WRAM in shipping, so 0x22400000..0x227A0000 is free,
// cache-through alias). FetchRect then COPIES the rect from resident pixels with
// NO inflate -> the 43 ms goes to a ~w*h cart->WRAM copy. Bounds-checked; a sheet
// that doesn't fit falls back to the banded inflate path (resident == 0).
#define SATURNSHEET_RES_BASE  0x22400000u
#define SATURNSHEET_RES_END   0x227A0000u
#endif
#else
#define SATURNSHEET_VRAM_BASE 0x25E44000u // VDP2 B0 tail (non-cart fallback)
#define SATURNSHEET_VRAM_END  0x25E80000u // top of B1
#endif

struct SaturnSheetSlot {
    uint16 width, height, bandRows, bandCount;
    uint32 vbase;   // VDP2 address of this sheet's blob ('SHB1' header)
    uint32 hash[4]; // engine path MD5 (GEN_HASH_MD5 of e.g. "Players/Sonic1.gif")
                    // -- set by the harness after staging; LoadSpriteSheet's
                    // Saturn arm resolves its banded slot through this.
    uint32 resident; // Task #243: cart address of the fully-inflated sheet (0 = banded)
};

static SaturnSheetSlot s_sheets[SATURNSHEET_SLOTS];
static uint32 s_cursor = SATURNSHEET_VRAM_BASE;
static int32 s_count = 0;
#if defined(P6_CART)
static uint32 s_resCursor = SATURNSHEET_RES_BASE; // Task #243 resident-sheet bump alloc
#endif

// scratch: caller-provided WRAM window (pointer-indirect, the W11b pattern)
static uint8 **s_scratchPtr = 0;
static uint32 s_scratchCap = 0;
extern "C" void SaturnSheet_SetScratch(void **bufp, uint32 cap)
{
    s_scratchPtr = (uint8 **)bufp;
    s_scratchCap = cap;
}

// witnesses (qa_p6_sheet via game.map)
extern "C" {
__attribute__((used)) int32 p6_w_sht_staged  = 0; // sheets staged into VDP2
__attribute__((used)) int32 p6_w_sht_fetches = 0; // FetchRect band inflates
#if defined(P6_FRONTEND_MENU)
// #243 attribution (2026-07-16): per-STORE-SLOT banded-inflate histogram +
// each slot's engine-path hash word0 so the offline reader NAMES the fetching
// sheets (store slots are stable across VDP1 handles -- the #321/#328 dup-
// handle hazard does not apply here). Existing witnesses cannot attribute:
// p6_w_sht_fetches is a single global sum. Gated on P6_FRONTEND_MENU (the
// flag threaded to this TU that the chain build sets and plain GHZ does not
// -- build_p6scene_objs.sh :601) so plain GHZ stays byte-identical. .bss
// cost 256 B, chain-only (chain _end ceiling GLOBALS 0x060C8000, ~28 KB
// headroom per the frontend-cart-map-recarve memory). Gate: qa_ghz_fetch.py.
__attribute__((used)) int32 p6_w_fetch_hist[32]    = { 0 }; // banded inflates per store slot
__attribute__((used)) int32 p6_w_sht_slothash0[32] = { 0 }; // GEN_HASH_MD5 word0 per slot
#endif
}

// fixed-window inflate (SaturnLayout.cpp, Task #227 STG sizing)
extern "C" int p6_mz_uncompress(unsigned char *, unsigned long *,
                                const unsigned char *, unsigned long);

#if defined(P6_FRAMEDIR)
// C1 identification (2026-07-11): p6_io_main records the currently-drawing
// entity's classID at the slot-19 banded-inflate site (defined in p6_io_main.cpp).
extern "C" void p6_frd_note_fetch(int32 slot);
#endif

static uint16 v16(uint32 addr) { return *(volatile uint16 *)addr; }
static uint32 v32be(uint32 addr) { return ((uint32)v16(addr) << 16) | v16(addr + 2); }

// Stage one .SHT blob (already GFS-loaded into WRAM staging) into VDP2.
// Returns the slot index, or -1. 16-bit copies only (access contract).
extern "C" int32 SaturnSheet_Stage(const void *blob, uint32 bytes)
{
    const uint8 *b = (const uint8 *)blob;
    if (s_count >= SATURNSHEET_SLOTS)
        return -1;
    if (!(b[0] == 'S' && b[1] == 'H' && b[2] == 'B' && b[3] == '1'))
        return -1;
    uint32 vbase = (s_cursor + 1u) & ~1u;
#if defined(P6_FRONTEND_TITLE)
    // #2a band-store vs OBJANIMPAK overlap: the front-end TITLE band store base was
    // lowered to 0x22720000 (for TSONIC) so it OVERLAPS the shared OBJANIMPAK cart
    // window (Animation.hpp P6_HW_OBJANIMPAK 0x22760000 + CAP 0x40000 = 0x227A0000). A
    // sheet staged in [0x22760000,0x227A0000) is CLOBBERED when HBHOBJ.PAK loads there
    // -> SaturnSheet_FetchRect reads a garbage band dir -> the cutscene sprites drop
    // (MEASURED dropreason=4 bucket-fetch, fetchret=0, shtSlot=9 HBHOBJ). GHZ's band
    // store starts AT 0x227A0000 (adjacent, no overlap) so this is FRONT-END ONLY ->
    // GHZ byte-identical. FIX: the bump-allocator SKIPS the pak window; sheets use
    // [0x22720000,0x22760000) + [0x227A0000,0x22800000).
    if (vbase < 0x227A0000u && vbase + bytes > 0x22760000u)
        vbase = 0x227A0000u;
#endif
    if (vbase + bytes > SATURNSHEET_VRAM_END)
        return -1;

    const uint16 *src = (const uint16 *)b; // builder pads streams; +1 over-read safe
    volatile uint16 *dst = (volatile uint16 *)vbase;
    for (uint32 i = 0; i < (bytes + 1) / 2; ++i)
        dst[i] = src[i];

    SaturnSheetSlot *s = &s_sheets[s_count];
    s->width    = (uint16)((b[4] << 8) | b[5]);
    s->height   = (uint16)((b[6] << 8) | b[7]);
    s->bandRows = (uint16)((b[8] << 8) | b[9]);
    s->bandCount = (uint16)((b[10] << 8) | b[11]);
    s->vbase    = vbase;
    s->resident = 0; /* Task #243: banded until SaturnSheet_MakeResident runs */
    s_cursor    = vbase + bytes;
    ++p6_w_sht_staged;
    return s_count++;
}

#if defined(P6_CART)
// Task #243 Lever 1: decompress the WHOLE sheet ONCE into the resident cart
// region. Per band: copy the compressed stream out of the store (16-bit, the
// VDP2/cart access contract) into the WRAM scratch low half, inflate into the
// scratch high half (WRAM->WRAM, fast -- not into the wait-stated cart), then
// copy the raw band into the resident cart buffer. After this, FetchRect serves
// every rect of this sheet by a direct copy with NO miniz inflate. Returns 0 ok,
// -1 if it doesn't fit the resident region (the sheet stays banded -> the old
// inflate path still serves it, just slower). Witness: p6_w_sht_resident counts
// sheets made resident.
__attribute__((used)) int32 p6_w_sht_resident = 0;
#if defined(P6_FRONTEND_MENU)
// #317 draw/inflate hog: precise residency witnesses (front-end only -> plain GHZ
// SaturnSheet.o byte-identical). resfill = s_resCursor - RES_BASE (true RES bytes
// in use); resmask = bitmask of which slots are resident; rescap = store size.
// Updated by MakeResident + ResReset so the live harness reads the exact fill.
__attribute__((used)) int32 p6_w_sht_resfill = 0;
__attribute__((used)) int32 p6_w_sht_resmask = 0;
__attribute__((used)) int32 p6_w_sht_rescap  = (int32)(SATURNSHEET_RES_END - SATURNSHEET_RES_BASE);
#endif
extern "C" int32 SaturnSheet_MakeResident(int32 slot)
{
    if (slot < 0 || slot >= s_count)
        return -1;
    SaturnSheetSlot *S = &s_sheets[slot];
    uint32 sheetBytes = (uint32)S->width * (uint32)S->height;
    uint32 resbase = (s_resCursor + 3u) & ~3u;
    if (resbase + sheetBytes > SATURNSHEET_RES_END)
        return -1;
    uint8 *scratch = s_scratchPtr ? *s_scratchPtr : 0;
    if (!scratch || s_scratchCap < 0x8000)
        return -1;
    uint8 *zbuf = scratch;          /* low half: compressed band copy (< 16 KB) */
    uint8 *raw  = scratch + 0x4000; /* high half: inflated band (<= 8,192 B) */
    uint32 dirBase = S->vbase + 12;
    for (int32 b = 0; b < (int32)S->bandCount; ++b) {
        uint32 e   = dirBase + (uint32)b * 12;
        uint32 off = v32be(e);
        uint32 zsz = v32be(e + 4);
        uint32 rsz = v32be(e + 8);
        if (zsz > 0x4000 || rsz > 0x4000)
            return -1;
        uint32 srcStart   = S->vbase + off;
        uint32 srcAligned = srcStart & ~1u;
        uint32 lead       = srcStart - srcAligned;
        uint32 words      = (lead + zsz + 1) / 2;
        volatile uint16 *vsrc = (volatile uint16 *)srcAligned;
        uint16 *zdst = (uint16 *)zbuf;
        for (uint32 i = 0; i < words; ++i)
            zdst[i] = vsrc[i];
        mz_ulong dlen = rsz;
        if (p6_mz_uncompress(raw, &dlen, zbuf + lead, zsz) != MZ_OK)
            return -1;
        memcpy((void *)(resbase + (uint32)b * S->bandRows * S->width), raw, rsz);
    }
    S->resident = resbase;
    s_resCursor = resbase + sheetBytes;
    ++p6_w_sht_resident;
#if defined(P6_FRONTEND_MENU)
    p6_w_sht_resfill = (int32)(s_resCursor - SATURNSHEET_RES_BASE);
    p6_w_sht_resmask |= (1 << slot);
#endif
    return 0;
}

// #249 (band-crossing fix): shared resident-cart allocator. SaturnLayout reuses
// the SAME bump cursor as the resident sheets so the FG layout co-resides in the
// cart RES store with NO fixed split or collision (one cursor; whoever allocs
// first gets the lower addresses). Returns a 4-aligned cart address, or 0 if the
// RES store is full (caller falls back to the per-crossing inflate). The resident
// sheets (~1 MB) + the GHZ layout (~551 KB) fit the 3.6 MB RES store with room.
__attribute__((used)) int32 p6_w_sht_resbytes = 0; // resident-store bytes used (sheets+layout)
extern "C" uint32 SaturnSheet_ResAlloc(uint32 bytes)
{
    uint32 a = (s_resCursor + 3u) & ~3u;
    if (a + bytes > SATURNSHEET_RES_END)
        return 0;
    s_resCursor = a + bytes;
    p6_w_sht_resbytes = (int32)(s_resCursor - SATURNSHEET_RES_BASE);
#if defined(P6_FRONTEND_MENU)
    p6_w_sht_resfill = (int32)(s_resCursor - SATURNSHEET_RES_BASE);
#endif
    return a;
}

// Band-store (VDP2/cart) allocator introspection for the title-Sonic TSONIC
// overflow diagnosis (2026-07-12): the band store bump cursor + its base/end.
// UNGATED (referenced by the P6_FRONTEND_TITLE TSONIC band diag in p6_io_main.cpp
// regardless of P6_FRAMEDIR; moving them out of the FRAMEDIR block unbreaks the
// no-FRAMEDIR chain link + keeps the P6_FRAMEDIR="" A/B override buildable).
extern "C" uint32 SaturnSheet_BandCursor(void) { return s_cursor; }
extern "C" uint32 SaturnSheet_BandBase(void)   { return SATURNSHEET_VRAM_BASE; }
extern "C" uint32 SaturnSheet_BandEnd(void)    { return SATURNSHEET_VRAM_END; }

#if defined(P6_FRAMEDIR)
// Sprite-pipeline rework (feature checklist sec 7): the .FRD blobs are
// GFS-loaded DIRECTLY into the cart at the (aligned) bump cursor -- the
// caller peeks the destination via ResAlloc(0) and needs the remaining
// capacity as the GFS load cap so an oversize read can never run past
// RES_END into the anim-pack windows. Flag-gated: the default build is
// byte-identical without -DP6_FRAMEDIR.
extern "C" uint32 SaturnSheet_ResRemain(void)
{
    uint32 a = (s_resCursor + 3u) & ~3u;
    return (a < SATURNSHEET_RES_END) ? (SATURNSHEET_RES_END - a) : 0;
}

// MEASURED live clobber (qa_p6_frd F2, 2026-07-10): the front-end arms the
// engine `scanlines` backing at 0x22400000 == SATURNSHEET_RES_BASE and
// DrawLayer's ungated layer->scanlineCallback(scanlines) WRITES deform data
// there EVERY FRAME (224 * 16 B = 3,584 B). A blob staged at the base gets
// its header+directory garbled after the stage-time djb2 -- observed as
// per-frame slot-0 lookup misses on rects PROVEN present offline (SONIC1
// walk frames, ~11 misses/s at the landed GHZ). Ratchet the bump cursor
// above the window before FRD staging; <= 4 KB of the >= 285 KB worst-seam
// headroom. Flag-gated: byte-identical without -DP6_FRAMEDIR.
extern "C" void SaturnSheet_ResFloor(uint32 guardBytes)
{
    uint32 floorAddr = SATURNSHEET_RES_BASE + guardBytes;
    if (s_resCursor < floorAddr)
        s_resCursor = floorAddr;
}
#endif

#if defined(P6_FRONTEND_MENU)
// #317 draw/inflate hog: at the GHZCutscene->Green Hill Zone handoff the RES store is
// full of resident TITLE/menu sheets (TSONIC 1 MB!) that are NEVER drawn again in
// gameplay, forcing the landing sheets banded (MEASURED 8.6 inflations/frame -> 4.1
// fps). The bump allocator never frees, so reclaim it explicitly here: reset the
// cursor to the base and clear every slot's resident flag so its FetchRect falls back
// to its (still-staged, still-intact) banded blob in the separate band store. SAFE
// because the caller invokes this ONLY at the handoff seam, where none of the pre-GHZ
// resident sheets are drawn again; the caller then re-promotes the Green Hill Zone
// gameplay sheets from the reclaimed store. Front-end only -> plain GHZ byte-identical.
__attribute__((used)) int32 p6_w_sht_resreset_n = 0; /* task #326: ResReset call count */
extern "C" void SaturnSheet_ResReset(void)
{
    for (int32 s = 0; s < s_count; ++s)
        s_sheets[s].resident = 0;
    s_resCursor      = SATURNSHEET_RES_BASE;
    p6_w_sht_resfill = 0;
    p6_w_sht_resmask = 0;
    p6_w_sht_resident = 0;
    ++p6_w_sht_resreset_n;
}
// Water M1b (2026-07-20, MEASURED band-store overflow): the 384 KB chain band store
// [0x227A0000,0x22800000) is NEVER reset across the Logos->Title->Menu->AIZ->
// GHZCutscene->GHZ chain (s_cursor/s_count monotonic), so at the GHZ handoff it is
// 372,713/384 KB FULL of dead front-end debris (TSONIC 121 KB, HBHOBJ 185 KB, Menu/
// AIZOBJ/Logos) that GHZ never draws. WATER.SHT (18,274 B) then overflows by 6,475 B
// -> SaturnSheet_Stage returns -1 -> the water surface never binds (shtslot -3,
// bandcur 0x227FD1E9 vs bandend 0x22800000, slotcnt 24<28 -- NOT slot-exhaustion).
// The store CANNOT be enlarged here: SaturnLayout owns [0x22600000,0x227A0000) below
// the band base (title-precedent base-lower would collide). SYMMETRIC FIX (mirrors
// SaturnSheet_ResReset above, called at the SAME GHZ-handoff seam for the RES store):
// rewind the band cursor/count to empty BEFORE the GHZ ghzShtFiles loop re-stages all
// GHZ sheets fresh (FindSlot returns -1 post-reset -> clean re-stage) + WATER fits.
// p6_frd_attach_bound re-routes persisted surfaces by STORE slot (task #328). Only
// pure front-end debris is dropped (GHZ draws none of it). Chain-only (P6_FRONTEND_MENU)
// -> plain GHZ byte-identical. Witness: p6_w_sht_bandreset_n counts the rewinds.
__attribute__((used)) int32 p6_w_sht_bandreset_n = 0;
extern "C" void SaturnSheet_BandReset(void)
{
    s_cursor = SATURNSHEET_VRAM_BASE;
    s_count  = 0;
    ++p6_w_sht_bandreset_n;
}
#endif
#endif

extern "C" void SaturnSheet_SetHash(int32 slot, const uint32 *hash)
{
    if (slot < 0 || slot >= s_count)
        return;
    for (int32 i = 0; i < 4; ++i)
        s_sheets[slot].hash[i] = hash[i];
#if defined(P6_FRONTEND_MENU)
    // #243 attribution: mirror hash word0 so qa_ghz_fetch.py names the slot.
    if (slot < 32)
        p6_w_sht_slothash0[slot] = (int32)hash[0];
#endif
}

extern "C" int32 SaturnSheet_FindSlot(const uint32 *hash)
{
    for (int32 s = 0; s < s_count; ++s) {
        SaturnSheetSlot *S = &s_sheets[s];
        if (S->hash[0] == hash[0] && S->hash[1] == hash[1]
            && S->hash[2] == hash[2] && S->hash[3] == hash[3])
            return s;
    }
    return -1;
}

extern "C" void SaturnSheet_Dims(int32 slot, int32 *w, int32 *h)
{
    if (slot < 0 || slot >= s_count) { *w = *h = 0; return; }
    *w = s_sheets[slot].width;
    *h = s_sheets[slot].height;
}

#if defined(P6_FRAMEDIR)
// DRAW-WALL FIX (task #328): the live SaturnSheet store-slot count + a full-hash
// copy, so io_main can route the FRD dispatch by STORE slot for EVERY live store
// slot that has a staged frame directory (the #321 AIZ-reuse leaves the Player's
// sheet handles with a per-handle frdSlot=-1; keying by the stable store slot in
// p6_vdp1.c s_frdByStore serves the pre-cut pattern regardless). Pure accessors.
extern "C" int32 SaturnSheet_SlotCount(void) { return s_count; }
extern "C" void  SaturnSheet_SlotHashCopy(int32 slot, uint32 *out)
{
    if (slot < 0 || slot >= s_count) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    out[0] = s_sheets[slot].hash[0]; out[1] = s_sheets[slot].hash[1];
    out[2] = s_sheets[slot].hash[2]; out[3] = s_sheets[slot].hash[3];
}
#endif

// Fetch the rect [sx, sx+w) x [sy, sy+h) into dst (w*h bytes, row-major).
// Per intersecting band: copy the compressed stream VDP2 -> scratch low half
// (16-bit reads), inflate into the scratch high half (WRAM->WRAM), copy the
// row slices into dst. Returns 1 on success.
extern "C" int32 SaturnSheet_FetchRect(int32 slot, int32 sx, int32 sy,
                                       int32 w, int32 h, uint8 *dst)
{
    if (slot < 0 || slot >= s_count)
        return 0;
    SaturnSheetSlot *S = &s_sheets[slot];
    if (sx < 0 || sy < 0 || w <= 0 || h <= 0
        || sx + w > (int32)S->width || sy + h > (int32)S->height)
        return 0;
#if defined(P6_CART)
    if (S->resident) {
        /* Task #243: direct copy from the resident (pre-inflated) cart buffer --
         * NO per-frame miniz inflate (was the ~43 ms/frame render bottleneck). */
        const uint8 *res = (const uint8 *)S->resident;
        for (int32 r = 0; r < h; ++r)
            memcpy(dst + (uint32)r * w,
                   res + (uint32)(sy + r) * S->width + sx, (uint32)w);
        return 1;
    }
#endif
    uint8 *scratch = s_scratchPtr ? *s_scratchPtr : 0;
    if (!scratch || s_scratchCap < 0x8000)
        return 0;
    uint8 *zbuf = scratch;            // low half: compressed copy (< 8 KB)
    uint8 *raw  = scratch + 0x4000;   // high half: inflated band (<= 8,192 B)

    uint32 dirBase = S->vbase + 12;
    int32 firstBand = sy / S->bandRows;
    int32 lastBand  = (sy + h - 1) / S->bandRows;
    for (int32 bnd = firstBand; bnd <= lastBand; ++bnd) {
        uint32 e   = dirBase + (uint32)bnd * 12;
        uint32 off = v32be(e);
        uint32 zsz = v32be(e + 4);
        uint32 rsz = v32be(e + 8);
        if (zsz > 0x4000 || rsz > 0x4000)
            return 0; // outside the W12 contract (largest band 8,192 raw)

        // 16-bit copy of the compressed stream out of VDP2 (vbase + off may
        // be ODD relative to the blob start -- copy from the aligned word
        // below and inflate from the byte offset inside zbuf).
        uint32 srcStart = S->vbase + off;
        uint32 srcAligned = srcStart & ~1u;
        uint32 lead = srcStart - srcAligned;
        uint32 words = (lead + zsz + 1) / 2;
        volatile uint16 *vsrc = (volatile uint16 *)srcAligned;
        uint16 *zdst = (uint16 *)zbuf;
        for (uint32 i = 0; i < words; ++i)
            zdst[i] = vsrc[i];

        mz_ulong dlen = rsz;
        if (p6_mz_uncompress(raw, &dlen, zbuf + lead, zsz) != MZ_OK)
            return 0;
        ++p6_w_sht_fetches;
#if defined(P6_FRONTEND_MENU)
        // #243 attribution: bump this store slot's banded-inflate tally.
        if (slot < 32)
            ++p6_w_fetch_hist[slot];
#endif
#if defined(P6_FRAMEDIR)
        // C1 identification (2026-07-11): attribute this banded inflate to the
        // currently-drawing entity (sceneInfo.entity) so a live read names the
        // object drawing store slot 19 (Global/Shields.gif blue sparkle).
        p6_frd_note_fetch(slot);
#endif

        int32 bandTop = bnd * S->bandRows;
        int32 r0 = (sy > bandTop) ? sy : bandTop;
        int32 r1 = bandTop + S->bandRows - 1;
        if (r1 > sy + h - 1)
            r1 = sy + h - 1;
        for (int32 r = r0; r <= r1; ++r)
            memcpy(dst + (uint32)(r - sy) * w,
                   raw + (uint32)(r - bandTop) * S->width + sx, (uint32)w);
    }
    return 1;
}
