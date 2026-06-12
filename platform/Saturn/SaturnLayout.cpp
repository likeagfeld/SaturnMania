// =============================================================================
// SaturnLayout.cpp -- P6.7 W11a (Task #210): the layer-layout BAND STORE +
// CAMERA-LOCAL SLIDING WINDOWS.
//
// WHY (W11, SaturnMemoryMap.h): tile-layer layouts cannot be RAM-resident at
// scale (GHZ1 551,168 B raw; FBZ/Scene2 3,006,976 B). Every consumer is
// camera-local (Collision.cpp sensor fetches run only for inRange entities;
// the VDP2 present streams the visible region), so reads are served from
// per-collidable-layer sliding windows (128 cols x 32 rows x 2 B = 8,192 B
// each, two slots) refilled from a per-zone band store built at asset time
// by tools/build_layout_bands.py (16-row bands, zlib-deflated; decoder =
// the SAME miniz inflate proven on SH-2 since P6.3 -- runtime re-encoding
// is infeasible: tdefl needs ~320 KB state, and the cheap codecs measured
// insufficient: RLE16 49% / row-delta+RLE 41% on GHZ FG Low vs zlib 13%).
//
// FILE FORMAT (cd/<TAG>LAYT.BIN, all BIG-ENDIAN -- SH-2-native, parsed in
// place): see build_layout_bands.py header. MEASURED: GHZ1 = 51,094 B
// (worst band 5,354 B deflated / 32,768 B raw).
//
// Gate: qa_p6_layout L1-L4 (file hash on SH-2 + 110 probes spanning window
// crossings vs the offline model + refill-count sanity).
// =============================================================================
#include <string.h>

#include "miniz/miniz.h"

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef signed int int32;

#define SATURNLAYOUT_SLOTS    2
#define SATURNLAYOUT_WIN_COLS 128
#define SATURNLAYOUT_WIN_ROWS 32
#define SATURNLAYOUT_BANDROWS 16

// Window backing: the WRAM-H freed-region slack 0x060F0000..0x060F4000
// (exactly 2 x 8,192 B windows; SaturnMemoryMap.h W11 ledger).
#define SATURNLAYOUT_WINBASE 0x060F0000u

struct SaturnLayoutLayer {
    uint16 xsize, ysize, bandCount;
    const uint8 *dir; // 12 B per band: u32 offset, u32 zsize, u32 rawsize
};

struct SaturnLayoutSlot {
    int32 layer;   // -1 = unbound
    int32 wx, wy;  // window origin in tile coords (-1 = empty)
    uint16 *win;   // SATURNLAYOUT_WIN_COLS * SATURNLAYOUT_WIN_ROWS
};

static const uint8 *s_blob = 0;
static SaturnLayoutLayer s_layers[8];
static int32 s_layerCount = 0;
static SaturnLayoutSlot s_slots[SATURNLAYOUT_SLOTS];

// witnesses (read by qa_p6_layout via game.map)
extern "C" {
__attribute__((used)) int32 p6_w_lay_refills = 0;
}

// band-inflate scratch: caller-provided (DATASET_TMP; the zone's largest raw
// band -- GHZ 32,768 B). W11b: POINTER-INDIRECT -- the engine's storage GC
// (DefragmentAndGarbageCollectStorage) MOVES tracked allocations and updates
// the registered uint32** location, so the refill must re-read the caller's
// pointer variable every time instead of caching the raw address (a cached
// pointer goes stale on the first defrag -- the jo-pool-stale class).
static uint8 **s_scratchPtr = 0;
static uint32 s_scratchCap = 0;
extern "C" void SaturnLayout_SetScratch(void **bufp, uint32 cap)
{
    s_scratchPtr = (uint8 **)bufp;
    s_scratchCap = cap;
}

static uint32 be32(const uint8 *p) { return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3]; }
static uint32 be16(const uint8 *p) { return ((uint32)p[0] << 8) | p[1]; }

// =============================================================================
// p6_mz_uncompress -- mz_uncompress with the inflate_state served from a
// FIXED WRAM-H window instead of the WRAM-L _sbrk heap (Task #227 STG
// sizing). miniz's inflate_state is ~43,712 B (tinfl_decompressor ~10,920 +
// the 32,768 B LZ dict); keeping that transient out of the heap funds the
// DATASET_STG growth to 112 KB (Storage.cpp) inside the unchanged 0x43000
// heap window. The window sits in the measured free WRAM-H gap between
// _end and the 0x060C0000 overlay floor (qa_p6_mapoverlap asserts
// _end <= 0x060B4000). All inflate consumers are SYNCHRONOUS and
// single-threaded (SaturnLayout/SaturnSheet refills + the engine
// ReadCompressed seam) so one bump window, reset per call, is sound.
// =============================================================================
#define P6_MZ_WIN_BASE 0x060B4000u
#define P6_MZ_WIN_CAP  0x0000B000u // 45,056 B >= inflate_state ~43,712 B
static uint32 p6_mz_used = 0;
static void *p6_mz_alloc(void *opaque, size_t items, size_t size)
{
    (void)opaque;
    uint32 need = (uint32)(items * size + 7u) & ~7u;
    if (p6_mz_used + need > P6_MZ_WIN_CAP)
        return 0;
    void *p = (void *)(P6_MZ_WIN_BASE + p6_mz_used);
    p6_mz_used += need;
    return p;
}
static void p6_mz_free(void *opaque, void *address)
{
    (void)opaque;
    (void)address; // bump window: freed wholesale by the per-call reset
}
extern "C" int p6_mz_uncompress(unsigned char *dest, unsigned long *destLen,
                                const unsigned char *source, unsigned long sourceLen)
{
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));
    p6_mz_used     = 0; // reset the bump window (synchronous consumers only)
    stream.zalloc  = p6_mz_alloc;
    stream.zfree   = p6_mz_free;
    stream.next_in  = source;
    stream.avail_in = (unsigned int)sourceLen;
    stream.next_out  = dest;
    stream.avail_out = (unsigned int)*destLen;
    int status = mz_inflateInit(&stream);
    if (status != MZ_OK)
        return status;
    status = mz_inflate(&stream, MZ_FINISH);
    if (status != MZ_STREAM_END) {
        mz_inflateEnd(&stream);
        return status == MZ_BUF_ERROR ? MZ_DATA_ERROR : status;
    }
    *destLen = stream.total_out;
    return mz_inflateEnd(&stream);
}

extern "C" int32 SaturnLayout_Mount(const void *blob)
{
    const uint8 *b = (const uint8 *)blob;
    if (!(b[0] == 'L' && b[1] == 'Y' && b[2] == 'T' && b[3] == '1'))
        return 0;
    s_blob = b;
    s_layerCount = (int32)be16(b + 4);
    if (s_layerCount > 8)
        s_layerCount = 8;
    const uint8 *hdr = b + 8;
    const uint8 *dir = hdr + s_layerCount * 8;
    for (int32 i = 0; i < s_layerCount; ++i) {
        s_layers[i].xsize = (uint16)be16(hdr + i * 8 + 0);
        s_layers[i].ysize = (uint16)be16(hdr + i * 8 + 2);
        s_layers[i].bandCount = (uint16)be16(hdr + i * 8 + 4);
        s_layers[i].dir = dir;
        dir += s_layers[i].bandCount * 12;
    }
    for (int32 s = 0; s < SATURNLAYOUT_SLOTS; ++s) {
        s_slots[s].layer = -1;
        s_slots[s].wx = s_slots[s].wy = -1;
        s_slots[s].win = (uint16 *)(SATURNLAYOUT_WINBASE
                                    + (uint32)s * SATURNLAYOUT_WIN_COLS * SATURNLAYOUT_WIN_ROWS * 2);
    }
    return s_layerCount;
}

extern "C" void SaturnLayout_Bind(int32 slot, int32 layer)
{
    if (slot < 0 || slot >= SATURNLAYOUT_SLOTS)
        return;
    s_slots[slot].layer = layer;
    s_slots[slot].wx = s_slots[slot].wy = -1; // force refill
}

// Refill the slot's window to origin (wx, wy): inflate each intersecting
// 16-row band into a stack-less scratch and copy the window's column slice.
// Scratch: the band raw image (<= 1024 cols * 16 rows * 2 = 32,768 B) lives
// in a static buffer inside the band-store region tail? NO -- it borrows
// the WRAM-L heap via miniz's own allocator (mz_uncompress mallocs through
// the pack _sbrk; the heap window holds the proven ~44 KB inflate
// transient) and a static 32 KB raw buffer in .bss would blow WRAM-H. We
// inflate the band DIRECTLY to a heap buffer via mz_uncompress.
static void SaturnLayout_Refill(SaturnLayoutSlot *slot, int32 wx, int32 wy)
{
    const SaturnLayoutLayer *L = &s_layers[slot->layer];
    // clamp origin into the layer
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;
    if (wx > (int32)L->xsize - SATURNLAYOUT_WIN_COLS)
        wx = (int32)L->xsize - SATURNLAYOUT_WIN_COLS;
    if (wy > (int32)L->ysize - SATURNLAYOUT_WIN_ROWS)
        wy = (int32)L->ysize - SATURNLAYOUT_WIN_ROWS;
    if (wx < 0) wx = 0; // layer narrower than the window
    if (wy < 0) wy = 0;

    int32 cols = (int32)L->xsize - wx;
    if (cols > SATURNLAYOUT_WIN_COLS) cols = SATURNLAYOUT_WIN_COLS;

    int32 firstBand = wy / SATURNLAYOUT_BANDROWS;
    int32 lastRow = wy + SATURNLAYOUT_WIN_ROWS - 1;
    if (lastRow >= (int32)L->ysize) lastRow = (int32)L->ysize - 1;
    int32 lastBand = lastRow / SATURNLAYOUT_BANDROWS;

    for (int32 b = firstBand; b <= lastBand; ++b) {
        const uint8 *e = L->dir + b * 12;
        uint32 off = be32(e), zsz = be32(e + 4), rsz = be32(e + 8);
        // band raw image goes to the caller-provided scratch (DATASET_TMP,
        // injected via SaturnLayout_SetScratch -- a heap malloc here MEASURED
        // NULL: the pack heap slack is ~19 KB after pools and miniz's own
        // ~44 KB inflate transient, qa_p6_layout first-RED 2026-06-11).
        // W11b: deref the caller's variable per refill (GC-safe, see above).
        uint8 *scratch = s_scratchPtr ? *s_scratchPtr : 0;
        if (!scratch || rsz > s_scratchCap)
            return;
        mz_ulong dlen = rsz;
        if (p6_mz_uncompress(scratch, &dlen, s_blob + off, zsz) != MZ_OK)
            return;
        // copy the window's column slice of every window row in this band
        int32 bandTop = b * SATURNLAYOUT_BANDROWS;
        int32 r0 = (wy > bandTop) ? wy : bandTop;
        int32 r1 = bandTop + SATURNLAYOUT_BANDROWS - 1;
        if (r1 > lastRow) r1 = lastRow;
        for (int32 r = r0; r <= r1; ++r) {
            const uint8 *src = scratch + ((uint32)(r - bandTop) * L->xsize + wx) * 2;
            uint8 *dst = (uint8 *)(slot->win + (uint32)(r - wy) * SATURNLAYOUT_WIN_COLS);
            memcpy(dst, src, (uint32)cols * 2);
        }
    }
    slot->wx = wx;
    slot->wy = wy;
    ++p6_w_lay_refills;
}

extern "C" uint16 SaturnLayout_GetTile(int32 slot, int32 tx, int32 ty)
{
    SaturnLayoutSlot *S = &s_slots[slot];
    const SaturnLayoutLayer *L = &s_layers[S->layer];
    if (tx < 0 || ty < 0 || tx >= (int32)L->xsize || ty >= (int32)L->ysize)
        return 0xFFFF; // outside the layer = empty (engine memset default)
    if (S->wx < 0 || tx < S->wx || tx >= S->wx + SATURNLAYOUT_WIN_COLS
        || ty < S->wy || ty >= S->wy + SATURNLAYOUT_WIN_ROWS) {
        // recentre with hysteresis: origin so the probe sits one quarter in
        SaturnLayout_Refill(S, tx - SATURNLAYOUT_WIN_COLS / 4,
                            ty - SATURNLAYOUT_WIN_ROWS / 4);
    }
    // the file stores layout words LITTLE-endian (RSDK scene layout order,
    // copied raw into bands); swap on read -- SH-2 is big-endian
    uint16 w = S->win[(uint32)(ty - S->wy) * SATURNLAYOUT_WIN_COLS + (tx - S->wx)];
    return (uint16)((w >> 8) | (w << 8));
}
