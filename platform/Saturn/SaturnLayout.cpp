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
#define SATURNLAYOUT_WIN_COLS 64
#define SATURNLAYOUT_WIN_ROWS 32
// P6.8 F.2: band height is now PER-ZONE, read from the store header at Mount
// (u16 @ off 6), not a fixed #define. build_layout_bands.py picks the largest
// bandRows<=16 whose widest raw band (maxWidth*bandRows*2) fits the 0x8000
// inflate scratch: GHZ1 (1024w)->16, GHZ2 (1280w)->12. A fixed 16 overflowed
// the scratch for GHZ2 (1280*16*2=40,960 > 32,768) -> Refill aborted -> empty
// windows -> the player fell through GHZ2.
static int32 s_bandRows = 16; // set from the mounted store header
// Phase 2g (Task #229): N-WAY window cache per slot. MEASURED root cause of
// the GHZ collision frame-time hog (583 ms / 3 Player entities): the Player's
// per-frame sensor reads hit TWO fixed vertical bands on each collidable layer
// (the ring-of-origins diagnostic showed a STABLE 2-value ping-pong: slot
// origins alternated wy=51 <-> wy=95 every frame -- feet at ty~59 and a deep
// read at ty~103, 44 rows apart, exceeding the 32-row window). A single
// window evicted+re-inflated both bands EVERY frame (4 refills x ~3 bands).
// A 2-deep window cache lets the two stable positions co-reside and SWAP
// (active-index flip) instead of re-inflating -> 0 steady-state inflates.
// Backing budget unchanged: SLOTS*WAYS = 4 windows x 64*32*2 = 4,096 B each
// = 16,384 B = the exact 0x060F0000..0x060F4000 hole (was 2 x 8,192 B).
#define SATURNLAYOUT_WAYS     2

// Window backing: the WRAM-H freed-region slack 0x060F0000..0x060F4000
// (exactly SLOTS*WAYS=4 x 4,096 B windows; SaturnMemoryMap.h W11 ledger).
#define SATURNLAYOUT_WINBASE 0x060F0000u

struct SaturnLayoutLayer {
    uint16 xsize, ysize, bandCount;
    const uint8 *dir; // 12 B per band: u32 offset, u32 zsize, u32 rawsize
};

struct SaturnLayoutWindow {
    int32 wx, wy;  // window origin in tile coords (-1 = empty)
    uint16 *win;   // SATURNLAYOUT_WIN_COLS * SATURNLAYOUT_WIN_ROWS
};

struct SaturnLayoutSlot {
    int32 layer;    // -1 = unbound
    int32 active;   // index of the active way [0..SATURNLAYOUT_WAYS)
    SaturnLayoutWindow way[SATURNLAYOUT_WAYS];
};

static const uint8 *s_blob = 0;
static SaturnLayoutLayer s_layers[8];
static int32 s_layerCount = 0;
static SaturnLayoutSlot s_slots[SATURNLAYOUT_SLOTS];

// witnesses (read by qa_p6_layout via game.map)
extern "C" {
__attribute__((used)) int32 p6_w_lay_refills = 0;
// Phase 2g diagnostic: per-slot refill count + a ring of the last 8 refill
// origins per slot. Read post-hoc at a steady-state (player standing still)
// capture to discriminate the inflate-thrash mechanism: all-same-origin =
// frame-persistent waste (skip-if-unchanged fixes it); 2 alternating origins
// = ping-pong (a 2-band decode cache fixes it); all-distinct = genuine wide
// scan (needs a larger window or multi-band cache). NOT in any ship gate --
// pure measurement before the Phase 2g fix selection.
__attribute__((used)) int32 p6_w_lay_slot_refills[2] = {0, 0};
__attribute__((used)) int32 p6_w_lay_ring_wx[2][8] = {{0}};
__attribute__((used)) int32 p6_w_lay_ring_wy[2][8] = {{0}};
__attribute__((used)) int32 p6_w_lay_ring_pos[2] = {0, 0};
// #249 band-crossing fix: count of layers made resident (diagnostic).
__attribute__((used)) int32 p6_w_lay_resident = 0;
// #249 ROOT-CAUSE self-check (cart resident store byte-exactness, FG-Low).
__attribute__((used)) int32 p6_w_lay_resaddr  = 0;  // diagnostic cart address
__attribute__((used)) int32 p6_w_lay_resmiss  = -1; // mismatching bytes (cart vs source)
__attribute__((used)) int32 p6_w_lay_resfirst = -1; // first mismatching byte offset
__attribute__((used)) int32 p6_w_lay_res_rv   = -1; // cart byte read at first mismatch
__attribute__((used)) int32 p6_w_lay_res_bv   = -1; // source byte at first mismatch
}

// #249 band-crossing fix: each layer's FULL uncompressed layout, pre-inflated ONCE
// into the cart resident store at Mount (via SaturnSheet's shared bump allocator),
// so Refill copies the window slice DIRECTLY from resident pixels with NO
// per-crossing mz_uncompress (the user-felt ~50ms stall on forward progression).
// 0 = that layer did not fit the cart -> the band-inflate fallback still serves it
// (byte-exact, just slower). Resident pixels are row-major (row r at r*xsize*2).
extern "C" uint32 SaturnSheet_ResAlloc(uint32 bytes);
static uint32 s_layer_res[8] = {0, 0, 0, 0, 0, 0, 0, 0};
// #249: the resident-cart stall fix corrupts the WRAM-H packed collision geometry
// at 0x060E0000 (qa_p6_collgeom RED -> Sonic/Tails fall through). MEASURED so far:
// the resident READ is byte-exact (self-check resmiss=0); band store intact; the
// SHEET-SHIFT is RULED OUT (a dedicated bank-1 cart store, no shared-cursor bump,
// was STILL RED). The corruption is some other interaction of "resident pre-inflate
// active" with LoadTileConfig's 0x060E0000 packing -- patchy, not uniform. Until it
// is cracked (t1 hash-after-LoadTileConfig + a write-watch), ship the proven
// band-inflate path. 0 = OFF (collision correct, stall present). The dedicated bank-1
// allocator below stays for the eventual re-enable.
static int s_layResEnable = 1; // #249/#250 MEASUREMENT: resident ON to pin the 0x060E0000 corruption (t1hash/badframe)
// NOTE (2026-07-05): DISPROVEN as the cause of the empty-tile fall-through -- with
// resident OFF (p6_w_lay_resident=0) the 26 corrupt empty tiles at 0x060E0000
// PERSIST identically, and t1hash==golden 0x643A3A5D, so the corruption is BAKED
// INTO LoadTileConfig's pack (Scene.cpp packedCollisionMasks write), NOT the
// resident pre-inflate. Real fix is in the pack seam; this flag is irrelevant.

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

// W13 note (Task #227): the brief "fixed WRAM-H inflate window" experiment
// (commit d040d6b) is REVERTED -- with the anim working set moved to
// cd/GHZANIM.PAK the pools shrink to 220 KB and the ~44 KB inflate_state
// fits the WRAM-L heap again (the original proven path); the freed WRAM-H
// gap hosts the ANIMPAK fixed window instead (p6_io_main.cpp).
extern "C" int p6_mz_uncompress(unsigned char *dest, unsigned long *destLen,
                                const unsigned char *source, unsigned long sourceLen)
{
    return mz_uncompress(dest, (mz_ulong *)destLen, source, (mz_ulong)sourceLen);
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
    // F.2: per-zone band height from the header (u16 @ off 6).
    s_bandRows = (int32)be16(b + 6);
    if (s_bandRows < 1)
        s_bandRows = 16;
    const uint8 *hdr = b + 8;
    const uint8 *dir = hdr + s_layerCount * 8;
    for (int32 i = 0; i < s_layerCount; ++i) {
        s_layers[i].xsize = (uint16)be16(hdr + i * 8 + 0);
        s_layers[i].ysize = (uint16)be16(hdr + i * 8 + 2);
        s_layers[i].bandCount = (uint16)be16(hdr + i * 8 + 4);
        s_layers[i].dir = dir;
        dir += s_layers[i].bandCount * 12;
    }
    // #249/#250: the resident pre-inflate is DEFERRED out of Mount --
    // SaturnLayout_PreInflateResident() runs it AFTER LoadTileConfig has packed the
    // collision geometry at 0x060E0000 (p6_scene_load_and_arm). MEASURED ROOT CAUSE
    // (qa_p6_collpin on p6_collpin.mcs: t1hash=0xFC30CD79 != golden 0x643A3A5D,
    // badframe=1, player y=2079 fell-through): running the pre-inflate BEFORE
    // LoadTileConfig corrupts the pack -- every real collision cell mis-encoded
    // (10731/10746) -> no floor. Mount only RESETS residency here; the proven
    // band-inflate Refill serves every layer until the deferred pre-inflate
    // populates s_layer_res[].
    p6_w_lay_resident = 0;
    for (int32 i = 0; i < 8; ++i)
        s_layer_res[i] = 0;
    for (int32 s = 0; s < SATURNLAYOUT_SLOTS; ++s) {
        s_slots[s].layer = -1;
        s_slots[s].active = 0;
        for (int32 w = 0; w < SATURNLAYOUT_WAYS; ++w) {
            s_slots[s].way[w].wx = s_slots[s].way[w].wy = -1;
            s_slots[s].way[w].win = (uint16 *)(SATURNLAYOUT_WINBASE
                + ((uint32)s * SATURNLAYOUT_WAYS + (uint32)w)
                      * SATURNLAYOUT_WIN_COLS * SATURNLAYOUT_WIN_ROWS * 2);
        }
    }
    return s_layerCount;
}

// #249/#250: DEFERRED resident pre-inflate. Runs AFTER LoadTileConfig packs the
// collision geometry (called at the END of p6_scene_load_and_arm) so the pack is
// finalized before this touches the shared inflate heap / cart store -- the
// MEASURED fix for the t1hash=0xFC30CD79 corruption. Pre-inflates each layer's
// FULL row-major layout ONCE into the cart resident store so Refill copies window
// slices with NO per-crossing mz_uncompress (the band-crossing stall fix). Any
// layer that does not fit stays band-inflate. Requires Mount + SaturnLayout_Set-
// Scratch to have run (reads s_layers[]/s_blob/s_scratchPtr/s_bandRows).
extern "C" void SaturnLayout_PreInflateResident(void)
{
    uint8 *scratch = (s_layResEnable && s_scratchPtr) ? *s_scratchPtr : 0;
    if (!scratch || !s_blob)
        return;
    // #250: SKIP on a SAME-ZONE reload. Mount tag-matches the live zone and does
    // NOT reset s_layer_res, so the cart resident layout still holds the first
    // load's pixels -- re-inflating here would re-write cart 0x22600000.. AFTER
    // LoadSceneAssets re-staged the resident sheets into the SAME 4MB cart,
    // conflicting -> garbled FG after a death reload. A cross-zone reload resets
    // s_layer_res via Mount, so it correctly re-inflates the new zone's layout.
    if (s_layer_res[0] != 0)
        return;
    // Dedicated bank-1 cart region 0x22600000..0x227A0000 (above the SaturnSheet
    // residency, below the sheet staging). Local cursor -> resets each call.
    uint32 layCartCur = 0x22600000u;
    p6_w_lay_resident = 0;
    for (int32 i = 0; i < s_layerCount; ++i) {
        s_layer_res[i] = 0;
        uint32 full = (uint32)s_layers[i].xsize * (uint32)s_layers[i].ysize * 2u;
        if (layCartCur + full > 0x227A0000u)
            continue; // dedicated region full -> band-inflate fallback
        uint32 res = layCartCur;
        layCartCur += (full + 3u) & ~3u;
        uint8 *dst = (uint8 *)res;
        int32 ok = 1;
        for (int32 bnd = 0; bnd < (int32)s_layers[i].bandCount; ++bnd) {
            const uint8 *e = s_layers[i].dir + bnd * 12;
            uint32 off = be32(e), zsz = be32(e + 4), rsz = be32(e + 8);
            if (rsz > s_scratchCap) { ok = 0; break; }
            mz_ulong dl = rsz;
            if (p6_mz_uncompress(scratch, &dl, s_blob + off, zsz) != MZ_OK) { ok = 0; break; }
            uint32 bandOff = (uint32)bnd * (uint32)s_bandRows
                           * (uint32)s_layers[i].xsize * 2u;
            if (bandOff + rsz > full) { ok = 0; break; }
            memcpy(dst + bandOff, scratch, rsz);
        }
        if (ok) {
            s_layer_res[i] = res;
            ++p6_w_lay_resident;
        }
    }
}

extern "C" void SaturnLayout_Bind(int32 slot, int32 layer)
{
    if (slot < 0 || slot >= SATURNLAYOUT_SLOTS)
        return;
    s_slots[slot].layer = layer;
    s_slots[slot].active = 0;
    for (int32 w = 0; w < SATURNLAYOUT_WAYS; ++w)
        s_slots[slot].way[w].wx = s_slots[slot].way[w].wy = -1; // force refill
}

// Refill the slot's window to origin (wx, wy): inflate each intersecting
// 16-row band into a stack-less scratch and copy the window's column slice.
// Scratch: the band raw image (<= 1024 cols * 16 rows * 2 = 32,768 B) lives
// in a static buffer inside the band-store region tail? NO -- it borrows
// the WRAM-L heap via miniz's own allocator (mz_uncompress mallocs through
// the pack _sbrk; the heap window holds the proven ~44 KB inflate
// transient) and a static 32 KB raw buffer in .bss would blow WRAM-H. We
// inflate the band DIRECTLY to a heap buffer via mz_uncompress.
static void SaturnLayout_Refill(int32 slotIdx, int32 layerIdx,
                                SaturnLayoutWindow *slot, int32 wx, int32 wy)
{
    const SaturnLayoutLayer *L = &s_layers[layerIdx];
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

    // #249: resident fast path -- copy the window slice DIRECTLY from the
    // pre-inflated full layout in the cart with NO per-crossing mz_uncompress.
    // Byte-exact with the band path (identical uncompressed row-major data); a
    // ~4 KB copy instead of a ~50 ms 3-band inflate -> the forward-progression
    // stall dissolves (collision AND the FG present both re-window through here).
    if (s_layer_res[layerIdx]) {
        const uint8 *res = (const uint8 *)s_layer_res[layerIdx];
        int32 rlast = wy + SATURNLAYOUT_WIN_ROWS;
        if (rlast > (int32)L->ysize) rlast = (int32)L->ysize;
        for (int32 r = wy; r < rlast; ++r) {
            const uint8 *src = res + ((uint32)r * L->xsize + wx) * 2;
            uint8 *dst = (uint8 *)(slot->win + (uint32)(r - wy) * SATURNLAYOUT_WIN_COLS);
            memcpy(dst, src, (uint32)cols * 2);
        }
        slot->wx = wx;
        slot->wy = wy;
        ++p6_w_lay_refills;
        if (slotIdx >= 0 && slotIdx < SATURNLAYOUT_SLOTS) {
            int32 p = p6_w_lay_ring_pos[slotIdx] & 7;
            p6_w_lay_ring_wx[slotIdx][p] = wx;
            p6_w_lay_ring_wy[slotIdx][p] = wy;
            p6_w_lay_ring_pos[slotIdx] = p + 1;
            ++p6_w_lay_slot_refills[slotIdx];
        }
        return;
    }

    int32 firstBand = wy / s_bandRows;
    int32 lastRow = wy + SATURNLAYOUT_WIN_ROWS - 1;
    if (lastRow >= (int32)L->ysize) lastRow = (int32)L->ysize - 1;
    int32 lastBand = lastRow / s_bandRows;

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
        int32 bandTop = b * s_bandRows;
        int32 r0 = (wy > bandTop) ? wy : bandTop;
        int32 r1 = bandTop + s_bandRows - 1;
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
    // Phase 2g diagnostic: record this (clamped) origin in the per-slot ring.
    if (slotIdx >= 0 && slotIdx < SATURNLAYOUT_SLOTS) {
        int32 p = p6_w_lay_ring_pos[slotIdx] & 7;
        p6_w_lay_ring_wx[slotIdx][p] = wx;
        p6_w_lay_ring_wy[slotIdx][p] = wy;
        p6_w_lay_ring_pos[slotIdx] = p + 1;
        ++p6_w_lay_slot_refills[slotIdx];
    }
}

// True if the (clamped, non-empty) window `w` contains tile (tx,ty).
static inline int32 SaturnLayout_WindowHas(const SaturnLayoutWindow *w,
                                           int32 tx, int32 ty)
{
    return w->wx >= 0 && tx >= w->wx && tx < w->wx + SATURNLAYOUT_WIN_COLS
        && ty >= w->wy && ty < w->wy + SATURNLAYOUT_WIN_ROWS;
}

extern "C" uint16 SaturnLayout_GetTile(int32 slot, int32 tx, int32 ty)
{
    SaturnLayoutSlot *S = &s_slots[slot];
    const SaturnLayoutLayer *L = &s_layers[S->layer];
    if (tx < 0 || ty < 0 || tx >= (int32)L->xsize || ty >= (int32)L->ysize)
        return 0xFFFF; // outside the layer = empty (engine memset default)

    SaturnLayoutWindow *W = &S->way[S->active];
    if (!SaturnLayout_WindowHas(W, tx, ty)) {
        // Phase 2g: probe missed the active way. Search the OTHER ways before
        // re-inflating -- the Player's two stable sensor bands each live in a
        // way, so the steady-state miss is a SWAP (no inflate), not a refill.
        int32 hit = -1;
        for (int32 w = 0; w < SATURNLAYOUT_WAYS; ++w) {
            if (w != S->active && SaturnLayout_WindowHas(&S->way[w], tx, ty)) {
                hit = w;
                break;
            }
        }
        if (hit < 0) {
            // genuine miss: evict the non-active way (keep the just-used one),
            // refill it centred on the probe (probe sits one quarter in).
            hit = S->active ^ 1;
            if (hit >= SATURNLAYOUT_WAYS) hit = 0;
            SaturnLayout_Refill(slot, S->layer, &S->way[hit],
                                tx - SATURNLAYOUT_WIN_COLS / 4,
                                ty - SATURNLAYOUT_WIN_ROWS / 4);
        }
        S->active = hit;
        W = &S->way[hit];
    }
    // the file stores layout words LITTLE-endian (RSDK scene layout order,
    // copied raw into bands); swap on read -- SH-2 is big-endian
    uint16 w = W->win[(uint32)(ty - W->wy) * SATURNLAYOUT_WIN_COLS + (tx - W->wx)];
    return (uint16)((w >> 8) | (w << 8));
}

// FG-tile-mutation piece 1 (data-side): make RSDK.SetTile(-1) actually remove a
// tile from a STREAMED (layout==NULL) FG layer. The engine's SaturnLayerSetTile
// (Scene.hpp:218) drops the write for a streamed layer; this is the band-store-
// aware setter it routes to. Writes the resident store (persistent truth for the
// layer; survives window evict/refill) AND every currently-cached window that
// holds (tx,ty) (so GetTile -- read by BOTH Collision.cpp RSDK_LAYER_TILE and the
// present -- reflects immediately). BYTE ORDER: the window/resident store tiles
// LITTLE-endian raw (band order); GetTile byteswaps on read -> store the swap of
// the big-endian tile value V. -1 removal V=0xFFFF is byteswap-invariant.
//
// NOTE (measured 2026-07-12): Collision.cpp reads tiles via RSDK_LAYER_TILE =
// SaturnLayerGetTile = the same store, so this ALONE makes a broken wall/floor
// passable (GetTile->0xFFFF => empty => no collision). The VDP2 static-map is a
// SEPARATE cache (built once per load, p6_io_main.cpp:558) so the removed tile
// stays DRAWN until a map rebuild -- a non-blocking "ghost", handled by a later
// visual piece. Resident-only layers keep the mutation across refills; a purely
// band-inflate layer would lose it on refill (GHZ FG is resident, so exact).
extern "C" void SaturnLayout_SetTile(int32 slot, int32 tx, int32 ty, uint16 tile)
{
    if (slot < 0 || slot >= SATURNLAYOUT_SLOTS)
        return;
    SaturnLayoutSlot *S = &s_slots[slot];
    if (S->layer < 0 || S->layer >= s_layerCount)
        return;
    const SaturnLayoutLayer *L = &s_layers[S->layer];
    if (tx < 0 || ty < 0 || tx >= (int32)L->xsize || ty >= (int32)L->ysize)
        return;
    uint16 raw = (uint16)((tile >> 8) | (tile << 8)); // store little-endian raw
    // (a) resident store (row-major, 2 B/tile) -- the persistent truth.
    if (s_layer_res[S->layer]) {
        uint16 *res = (uint16 *)(s_layer_res[S->layer]);
        res[(uint32)ty * L->xsize + (uint32)tx] = raw;
    }
    // (b) every cached way that currently holds (tx,ty).
    for (int32 w = 0; w < SATURNLAYOUT_WAYS; ++w) {
        SaturnLayoutWindow *W = &S->way[w];
        if (SaturnLayout_WindowHas(W, tx, ty))
            W->win[(uint32)(ty - W->wy) * SATURNLAYOUT_WIN_COLS + (uint32)(tx - W->wx)] = raw;
    }
    // (c) CollapsingPlatform port (2026-07-16): invalidate the VDP2 FG static-map
    // cache so the mutation is VISIBLE immediately. The present rebuilds only on
    // `p6_vdp2_present_dirty || camera-tile crossing` (p6_vdp2.c:2023) -- without
    // this, a crumble/wall-break under a STANDING player keeps drawing the removed
    // tiles until the camera crosses a 16-px boundary (the declared "ghost" gap
    // above). Cost = one map rebuild on a mutation frame, the same work already
    // paid on every camera tile crossing. p6_vdp2.o and SaturnLayout.o are both
    // unconditional pack members (build_p6scene_objs.sh) -> the extern always links.
    {
        extern int p6_vdp2_present_dirty; // p6_vdp2.c:1284 (C TU; this file is
                                          // compiled with -fno-rtti C++ but the
                                          // symbol is extern "C"-plain either way)
        p6_vdp2_present_dirty = 1;
    }
}

// #237 DIAGNOSTIC (cheap): which layer is a slot CURRENTLY bound to? The GHZ2
// fall-through root cause is that the VDP2 present rebinds the SHARED slot 1 to
// a hardcoded layer index; this lets the gate MEASURE (not assume) that slot 1
// is bound to FG-High (the per-zone index) and not FG-Low.
extern "C" int32 SaturnLayout_SlotLayer(int32 slot)
{
    if (slot < 0 || slot >= SATURNLAYOUT_SLOTS)
        return -2;
    return s_slots[slot].layer;
}
