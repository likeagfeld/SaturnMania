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
// an 11th slot for TLOGO.SHT (Title/Logo.gif) -- it ALSO stages LOGOS.SHT (TITLE implies
// LOGOS, so slot 9 = LOGOS, slot 10 = TLOGO). KEPT 9 in the DEFAULT (GHZ) build so its
// s_sheets[] is byte-identical -- the default shipping _end is only ~96 B under the #228
// ANIMPAK floor, so the 32 B/slot of an unconditional extra slot would breach it. Only
// the front-end builds (which gc-drop the large p6_ghz_frame/reload -> ~1.6 KB of
// headroom) carry the extra slots. NSHEETS=12 (VDP1 bind table) is unchanged either way
// (the Title bind demand is the 3 Title surfaces + a few engine surfaces << 12).
#if defined(P6_FRONTEND_TITLE)
#define SATURNSHEET_SLOTS     11
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
}

// fixed-window inflate (SaturnLayout.cpp, Task #227 STG sizing)
extern "C" int p6_mz_uncompress(unsigned char *, unsigned long *,
                                const unsigned char *, unsigned long);

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
    return a;
}
#endif

extern "C" void SaturnSheet_SetHash(int32 slot, const uint32 *hash)
{
    if (slot < 0 || slot >= s_count)
        return;
    for (int32 i = 0; i < 4; ++i)
        s_sheets[slot].hash[i] = hash[i];
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
