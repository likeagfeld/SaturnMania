// =============================================================================
// SaturnFrameDir.cpp -- sprite-pipeline rework: PRE-CUT frame-directory
// stores (FRD1, built offline by tools/build_frame_dir.py). Replaces the
// runtime rect-cutting of SaturnSheet_FetchRect for sheets converted to FRD.
//
// WHY: today a VDP1 slot-cache MISS cuts the frame out of the sheet at
// runtime -- banded path = miniz inflate of every intersecting 16-row band
// (8 KB scratch round trip), resident path = per-row memcpy repack from the
// sheet stride to the mult-8 content stride. The FRD blob holds every frame
// the anim .bins reference ALREADY cut, padded to the ST-013-R3 sec 5.1
// 8-pixel width unit, rows contiguous at the padded stride, 4-byte aligned.
// A miss becomes: binary-search the directory (16 B/entry, sorted by
// (sy,sx,w,h)) + ONE linear copy into the slot's VDP1 VRAM. Zero cutting,
// zero inflate, zero scratch.
//
// 4bpp: frames with <= 15 distinct nonzero palette indices are stored as
// packed nibbles (VDP1 16-color LOOKUP TABLE mode -- ST-013-R3 sec 6.3
// color mode bits 5-3 = 001B; sec 5.2 lookup table = 32 B in VDP1 VRAM,
// entries may be COLOR BANK CODE). The FRD carries the 16 ORIGINAL 8-bit
// palette indices per LUT (deduped table); the VDP1-side consumer builds
// each LUT entry as (CRAM bank | index) at bind time, so CRAM palette
// animation / fades keep working and colors are bit-exact (lossless).
//
// PLACEMENT: the blob is copied once into the shared cart resident store
// (SaturnSheet_ResAlloc bump allocator, SaturnSheet.cpp:307-318) -- the
// same region the fully-inflated sheets occupy today, at ~half the bytes
// (frames only, 4bpp where eligible). Cart reads are byte-addressable;
// the u16 copy helpers mirror SaturnSheet.cpp's access pattern.
//
// LTO CONTRACT (W12b root cause, p6_vdp1.c:326-338): the jo-side TU never
// references these pack symbols statically -- p6_io_main hands
// p6_vdp1_set_frd() the SaturnFrameDir_Lookup function pointer, the proven
// direction.
//
// FILE FORMAT (tools/build_frame_dir.py, all BIG-ENDIAN, parsed in place):
//   +0  'FRD1' | u16 frameCount | u16 lutCount | u16 sheetW | u16 sheetH
//   +12 frameCount x 16 B directory, sorted ascending by
//       key = (sy<<48 | sx<<32 | w<<16 | h):
//         u16 sx, sy, w, h | u32 offset | u8 mode | u8 pw8 | u16 lutIdx
//   then lutCount x 16 B LUT source tables (original 8-bit indices);
//   then (4-aligned) pattern data: h rows of pw8*8 (8bpp) or pw8*4 (4bpp)
//   bytes, pad pixels = 0 (the transparent color code, ST-013-R3 sec 6.3).
// =============================================================================

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef signed int int32;

// shared cart resident-store bump allocator (SaturnSheet.cpp)
extern "C" uint32 SaturnSheet_ResAlloc(uint32 bytes);

#define SATURNFRD_SLOTS 16

struct SaturnFrameDirSlot {
    const uint8 *base;   // cart address of the staged blob
    uint32 bytes;
    uint16 frames, luts;
    uint16 sheetW, sheetH;
    uint32 hash[4];      // engine path MD5 (same convention as SaturnSheet)
};

static SaturnFrameDirSlot s_frd[SATURNFRD_SLOTS];
static int32 s_frdCount = 0;

extern "C" {
__attribute__((used)) int32 p6_w_frd_staged  = 0; // blobs staged into the cart (cumulative)
__attribute__((used)) int32 p6_w_frd_active  = 0; // live slot count (post-seam-reset)
__attribute__((used)) int32 p6_w_frd_lookups = 0; // directory lookups served
__attribute__((used)) int32 p6_w_frd_misses  = 0; // rects NOT in the directory
// qa_p6_frd.py F2 identity: per-ACTIVE-slot djb2 over the staged cart bytes
// (computed ONCE at stage time -- load phase, never per-frame) + blob size +
// frame count. The gate matches each (bytes,hash) pair against the offline
// cd/*.FRD file, proving the cart copy is intact end-to-end.
__attribute__((used)) int32 p6_w_frd_hash[SATURNFRD_SLOTS]   = { 0 };
__attribute__((used)) int32 p6_w_frd_bytes[SATURNFRD_SLOTS]  = { 0 };
__attribute__((used)) int32 p6_w_frd_frames[SATURNFRD_SLOTS] = { 0 };
// miss ring (last 4): checklist sec 6.2 classification -- a nonzero miss is
// either a non-anim blit (keep .SHT fallback) or a converter gap.
__attribute__((used)) int32 p6_w_frd_missrect[4] = { 0 }; // sx<<16 | sy
__attribute__((used)) int32 p6_w_frd_misswh[4]   = { 0 }; // w<<16 | h
__attribute__((used)) int32 p6_w_frd_missslot[4] = { 0 };
}

static uint16 rd16(const uint8 *p) { return (uint16)((p[0] << 8) | p[1]); }
static uint32 rd32(const uint8 *p)
{
    return ((uint32)p[0] << 24) | ((uint32)p[1] << 16)
         | ((uint32)p[2] << 8) | p[3];
}

// Stage one .FRD blob (already GFS-loaded into WRAM staging) into the cart
// resident store. Returns the slot index, or -1. 16-bit copies (the
// SaturnSheet access pattern; correct + harmless on the cart).
extern "C" int32 SaturnFrameDir_Stage(const void *blob, uint32 bytes)
{
    const uint8 *b = (const uint8 *)blob;
    if (s_frdCount >= SATURNFRD_SLOTS)
        return -1;
    if (!(b[0] == 'F' && b[1] == 'R' && b[2] == 'D' && b[3] == '1'))
        return -1;
    uint32 dst = SaturnSheet_ResAlloc((bytes + 3u) & ~3u);
    if (!dst)
        return -1;
    {
        const uint16 *src = (const uint16 *)b;
        volatile uint16 *d16 = (volatile uint16 *)dst;
        for (uint32 i = 0; i < (bytes + 1) / 2; ++i)
            d16[i] = src[i];
    }
    SaturnFrameDirSlot *s = &s_frd[s_frdCount];
    s->base   = (const uint8 *)dst;
    s->bytes  = bytes;
    s->frames = rd16(b + 4);
    s->luts   = rd16(b + 6);
    s->sheetW = rd16(b + 8);
    s->sheetH = rd16(b + 10);
    for (int32 i = 0; i < 4; ++i)
        s->hash[i] = 0;
    ++p6_w_frd_staged;
    return s_frdCount++;
}

// Stage-1 live path: the .FRD was GFS-loaded DIRECTLY into the cart at the
// ResAlloc cursor (p6_io_main p6_frd_stage_file: ResAlloc(0) peek + the new
// SaturnSheet_ResRemain cap -- blobs up to 262 KB exceed every WRAM bounce
// window). This registers the slot IN PLACE (zero copy) after claiming the
// bytes from the shared bump allocator; the claim MUST return the same
// address the caller loaded to (nothing else allocates in between --
// single-threaded load phase). One-time djb2 read-back = the qa_p6_frd F2
// identity witness (load-phase only; the cart read is wait-stated but runs
// once per stage, never per frame).
extern "C" int32 SaturnFrameDir_StageDirect(const void *blob, uint32 bytes)
{
    const uint8 *b = (const uint8 *)blob;
    if (s_frdCount >= SATURNFRD_SLOTS)
        return -1;
    if (bytes < 12
        || !(b[0] == 'F' && b[1] == 'R' && b[2] == 'D' && b[3] == '1'))
        return -1;
    if (SaturnSheet_ResAlloc((bytes + 3u) & ~3u) != (uint32)blob)
        return -1;
    SaturnFrameDirSlot *s = &s_frd[s_frdCount];
    s->base   = b;
    s->bytes  = bytes;
    s->frames = rd16(b + 4);
    s->luts   = rd16(b + 6);
    s->sheetW = rd16(b + 8);
    s->sheetH = rd16(b + 10);
    for (int32 i = 0; i < 4; ++i)
        s->hash[i] = 0;
    {
        uint32 hh = 5381u;
        for (uint32 k = 0; k < bytes; ++k)
            hh = ((hh << 5) + hh) ^ b[k];
        p6_w_frd_hash[s_frdCount]   = (int32)hh;
        p6_w_frd_bytes[s_frdCount]  = (int32)bytes;
        p6_w_frd_frames[s_frdCount] = (int32)s->frames;
    }
    ++p6_w_frd_staged;
    p6_w_frd_active = s_frdCount + 1;
    return s_frdCount++;
}

// Seam reclaim companion (p6_io_main): every SaturnSheet_ResReset() KILLS the
// staged blobs' cart backing, so the registry must die with it -- the caller
// then re-stages the incoming leg's FRDs and p6_vdp1_frd_detach_all() clears
// the per-sheet attachments (a stale frdSlot against a re-staged different
// blob would serve wrong pixels -- the #250 stale-binding class).
extern "C" void SaturnFrameDir_Reset(void)
{
    s_frdCount      = 0;
    p6_w_frd_active = 0;
}

extern "C" void SaturnFrameDir_SetHash(int32 slot, const uint32 *hash)
{
    if (slot < 0 || slot >= s_frdCount)
        return;
    for (int32 i = 0; i < 4; ++i)
        s_frd[slot].hash[i] = hash[i];
}

extern "C" int32 SaturnFrameDir_FindSlot(const uint32 *hash)
{
    for (int32 s = 0; s < s_frdCount; ++s) {
        SaturnFrameDirSlot *S = &s_frd[s];
        if (S->hash[0] == hash[0] && S->hash[1] == hash[1]
            && S->hash[2] == hash[2] && S->hash[3] == hash[3])
            return s;
    }
    return -1;
}

// Result of a directory hit. pattern rows are CONTIGUOUS at the padded
// stride (pw pixels; 8bpp = pw bytes/row, 4bpp = pw/2 bytes/row), pad
// pixels already 0, base 4-aligned -- the VDP1-side restage copy is one
// linear aligned block.
typedef struct {
    const uint8 *pattern;
    const uint8 *lutSrc; // 16 original 8-bit palette indices (4bpp), or 0
    int32 pw;            // padded width, PIXELS (multiple of 8)
    int32 mode;          // 0 = 8bpp (VDP1 color mode 4), 1 = 4bpp LUT (mode 1)
    int32 lutIdx;        // index into this sheet's deduped LUT table, or -1
} P6FrameInfo;

// Binary-search the (sheet-slot) directory for the exact anim rect. The key
// order matches the offline sort (build_frame_dir.py: sy, sx, w, h).
// Returns 1 on hit (out filled), 0 on miss.
extern "C" int32 SaturnFrameDir_Lookup(int32 slot, int32 sx, int32 sy,
                                       int32 w, int32 h, P6FrameInfo *out)
{
    if (slot < 0 || slot >= s_frdCount)
        return 0;
    const SaturnFrameDirSlot *S = &s_frd[slot];
    const uint8 *dir = S->base + 12;
    int32 lo = 0, hi = (int32)S->frames - 1;
    while (lo <= hi) {
        int32 mid = (lo + hi) >> 1;
        const uint8 *e = dir + mid * 16;
        int32 esx = rd16(e), esy = rd16(e + 2);
        int32 ew = rd16(e + 4), eh = rd16(e + 6);
        int32 c;
        if (esy != sy)      c = esy - sy;
        else if (esx != sx) c = esx - sx;
        else if (ew != w)   c = ew - w;
        else                c = eh - h;
        if (c == 0) {
            uint32 off  = rd32(e + 8);
            int32 mode  = e[12];
            int32 pw8   = e[13];
            int32 lutIx = rd16(e + 14);
            out->pattern = S->base + off;
            out->pw      = pw8 * 8;
            out->mode    = mode;
            out->lutIdx  = (mode == 1) ? lutIx : -1;
            out->lutSrc  = (mode == 1)
                ? S->base + 12 + (uint32)S->frames * 16 + (uint32)lutIx * 16
                : 0;
            ++p6_w_frd_lookups;
            return 1;
        }
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    p6_w_frd_missslot[p6_w_frd_misses & 3] = slot;
    p6_w_frd_missrect[p6_w_frd_misses & 3] = (sx << 16) | (sy & 0xFFFF);
    p6_w_frd_misswh[p6_w_frd_misses & 3]   = (w << 16) | (h & 0xFFFF);
    ++p6_w_frd_misses;
    return 0;
}

extern "C" void SaturnFrameDir_Dims(int32 slot, int32 *w, int32 *h)
{
    if (slot < 0 || slot >= s_frdCount) { *w = *h = 0; return; }
    *w = s_frd[slot].sheetW;
    *h = s_frd[slot].sheetH;
}

extern "C" int32 SaturnFrameDir_LutCount(int32 slot)
{
    return (slot >= 0 && slot < s_frdCount) ? s_frd[slot].luts : 0;
}
