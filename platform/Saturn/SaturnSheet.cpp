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
#define SATURNSHEET_SLOTS     8
#if defined(P6_CART)
#define SATURNSHEET_VRAM_BASE 0x227A0000u // 4MB cart, after STG(3MB)+TMP(640KB)
#define SATURNSHEET_VRAM_END  0x22800000u // top of the 4MB cart (384 KB store)
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
};

static SaturnSheetSlot s_sheets[SATURNSHEET_SLOTS];
static uint32 s_cursor = SATURNSHEET_VRAM_BASE;
static int32 s_count = 0;

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
    s_cursor    = vbase + bytes;
    ++p6_w_sht_staged;
    return s_count++;
}

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
