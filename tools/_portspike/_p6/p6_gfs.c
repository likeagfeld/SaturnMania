// =============================================================================
// p6_gfs.c -- Saturn CD/GFS file backend for the unmodified engine Reader.cpp
// (Task #206, P6.2). io-green only (compiled with -DRETRO_SATURN_FILEIO).
//
// The engine's LoadFile/ReadBytes/CloseFile path (RSDK/Core/Reader.cpp) calls the
// fOpen/fRead/fSeek/fTell/fClose/fWrite primitives. Reader.hpp's Saturn branch
// (guarded by RETRO_SATURN_FILEIO) #undefs the newlib-stdio versions and routes
// them to the Saturn_* functions defined here, which open a real on-disc file via
// SBL GFS and serve its bytes out of a whole-file RAM buffer.
//
// WHY WHOLE-FILE-IN-RAM:
//   The engine's non-write-mode LoadFile does fSeek(END)/fTell to learn the size,
//   then ReadBytes streams sequentially. GFS reads in SECTOR units, so the cleanest
//   exact-size contract is: GFS_GetFileSize -> exact byte size (sctsz*(nsct-1)+lstsz),
//   GFS_Fread the whole file once into s_filebuf, then satisfy every fRead/fSeek/fTell
//   from that buffer + a cursor. P6IO.BIN is 256 B; s_filebuf is 64 KB.
//
// CANONICAL CD BRING-UP (P6.2 -- Task #206, DECOUPLED GFSDEMO-style proof):
//   GFS file I/O rides the STANDARD Saturn CD bring-up order, proven identical in
//   three same-toolchain references: slInitSystem -> CDC_CdInit -> GFS_Init.
//     - jo-engine (this EXACT toolchain): core.c slInitSystem, then audio.c:110
//       CDC_CdInit(0x00,0x00,0x05,0x0f), then fs.c:115 GFS_DIRTBL + GFS_Init.
//     - Coup (working NetLink homebrew) + the SMPGFS SBL samples: the same
//       slInitSystem -> CDC_CdInit -> GFS_Init single-region boot.
//   slInitSystem + slInitSynch run FIRST, in their own TU (p6_sgl_boot.c, SGL.H,
//   steps 1-2); THIS file owns the CDC_CdInit + GFS_Init half (steps 3-4).
//   THE DECOUPLE: the image boots through SGL's REAL ___Start SLSTART entry
//   (sets GBR=0x060FFC00 / SR / SP=*MasterStack / CCR cache), so slInitSystem runs
//   in the SAME runtime jo/Coup prove works -- it does NOT drag the 1.2 MB engine
//   entity/scene BSS (that is P6.3); only Reader.cpp's LoadFile path is linked.
//   WHY THE PRIOR crt0 PATH FAULTED (refined root cause): crt0.s skipped SGL's
//   SLSTART, leaving GBR unset. SGL's VBlank ISR is GBR-relative, so when
//   slInitSystem enabled interrupts the first vblank handler took an address error
//   (master to PC=0x06000956). It was NOT "SGL coupling"; it was the missing
//   GBR/cache/interrupt setup SLSTART performs. Booting through SLSTART removes the
//   fault at its source -- the whole point of the decouple decision.
//
//   GFS_Fread self-drives to completion: GFS_Fread (GFS.C:868) -> GFS_NwFread then
//   gfs_waitRead (GFS.C:913) busy-loops GFS_NwExecOne until GFS_SVR_COMPLETED
//   (GFS.C:927-928). No external periodic / VBLANK handler is needed. Default
//   transfer mode is CPU software copy (GFS_TRN.C:41), pinned here by
//   GFS_SetTmode(gfs, GFS_TMODE_CPU) so no SCU DMA / interrupt path is exercised.
// =============================================================================
#include <SEGA_GFS.H> // GFS_*/CDC_* protos + Sint32/Uint32 + GfsDirTbl/GfsDirName + GFS_TMODE_CPU
#include <stdio.h>    // SEEK_SET / SEEK_CUR / SEEK_END -- the whence values the engine's fSeek passes
#include <string.h>   // memcpy

// ---- P6.2 file-I/O witnesses (defined int32 in p6_ring.cpp; int == 32-bit SH-2) -
extern int p6_w_io_gfsinit;  // GFS_Init return (ndir): >2 == backend up, <=2 == fail
extern int p6_w_io_fid;      // GFS_NameToId(P6IO.BIN): >=0 == file found on disc
extern int p6_w_io_step;     // p6_gfs_init progress 1..2 (localises a hang in one capture)
extern int p6_w_io_openfail; // LAST fOpen failure site: 0 none, 1 no free slot,
                             // 2 NameToId<0, 3 GFS_Open NULL, 4 size<0, 5 write mode
extern int p6_w_io_nopen;    // concurrently-open handle count (P6.5b2 nested-open proof)
extern int p6_w_io_nopen_hw; // high-water of p6_w_io_nopen

// ---- GFS state (BSS) --------------------------------------------------------
#define P6_GFS_OPEN_MAX  2                 // max simultaneously-open GFS handles (nested .bin+GIF opens need 2)
#define P6_GFS_MAX_DIR   16                // root-dir entries the dirtbl can hold (disc has ~7)
#define P6_GFS_SECTOR    2048              // CD-ROM Mode 1 user-data bytes per sector
// #251 load-time: 32 sectors = 64 KB read-ahead window (was 2 = 4 KB). MEASURED
// root cause -- the emulated CD charges ~135 ms of access latency PER GFS_Fread
// CALL (the 4 KB transfer is trivial), and the engine's field-walk / registry /
// ReadCompressed parses refilled the 4 KB window ~145 times => ~15-20 s of the
// ~52 s load was the SH-2 spinning on the CD (io_vbl=10.3 s phase-2 alone). A
// 64 KB window cuts the GFS_Fread calls ~16x -> the per-call latency collapses.
#define P6_GFS_WIN_SECTS 32
#define P6_GFS_WIN_BYTES (P6_GFS_SECTOR * P6_GFS_WIN_SECTS) // 64 KB window (max)
// ADAPTIVE read-ahead: a fill that CONTINUES sequentially reads up to 64 KB (the
// engine is streaming a file -> amortize the per-call CD latency); a fill at a
// SEEK reads only P6_GFS_SEEK_SECTS (a new/scattered file -- a 64 KB read there is
// mostly wasted, and the masked load opens ~50 tiny SFX/config files: a fixed
// 64 KB there read 16x the bytes = MEASURED masked load 8s->21s regression).
#define P6_GFS_SEEK_SECTS 2 // 4 KB on a seek (don't over-read a new region)
// The two 64 KB windows live in the 4MB cart (cache-through), NOT inline in the
// pack .bss (WRAM-H margin is only ~3.6 KB; 2x64 KB would overflow it). Placed at
// 0x22700000-0x22720000: above GHZ's resident sheet+layout high-water (~0x22687000,
// SaturnLayout caps layout at 0x227A0000) and below the FG page (0x227F0000) /
// sheet store (0x227A0000). SAFE for GHZ because the layout resident pre-inflate
// writes 0x22600000.. only at the END of the scene-load (AFTER all GFS reads), so
// this region is free DURING the reads. (Moving the windows OUT of the struct also
// frees 8 KB of WRAM-H .bss.) GHZ-scoped: a zone whose layout exceeds 0x22700000
// would need the guard -- the only shipping zone is GHZ. Cache-through so the
// GFS_TMODE_CPU write + the engine read are coherent without a cache purge.
#define P6_CART_GFSWIN_BASE 0x22700000u
// P6.4 (Task #225): the whole-file-in-RAM model (64 KB -> 4 KB s_filebuf) is
// REPLACED by a sector-aligned sliding window so the engine can mount the
// 182,962,115-byte DATA.RSDK pack and serve LoadDataPack's registry walk +
// OpenDataFile's fSeek(offset)/fRead pattern (Reader.cpp:200-218) from ONE
// open GFS handle. GFS_Seek moves the access pointer in SECTOR units
// (ST-136-R2 sec 2.3: "off: amount access point is moved (unit: sector)";
// TB#20 erratum only forbids out-of-range positions), and GFS_Fread with
// GFS_TMODE_CPU busy-polls to completion (GFS.C:927-928), so each window
// refill is: GFS_Seek(sector, GFS_SEEK_SET) -> GFS_Fread(nsct, window).

// GFS work area MUST be 4-byte aligned (GFS.C:226 -> GFS_ERR_ALIGN otherwise).
// A Uint32 array guarantees that; size = GFS_WORK_SIZE(open_max) rounded up to 4.
static Uint32     s_gfs_work[(GFS_WORK_SIZE(P6_GFS_OPEN_MAX) + 3) / 4];
static GfsDirName s_gfs_dirname[P6_GFS_MAX_DIR];
static GfsDirTbl  s_gfs_dirtbl;

// ---- The opaque file handle the engine holds (FileInfo.file) ----------------
// Reader.hpp forward-declares `typedef struct Saturn_FileIO Saturn_FileIO;` and only
// ever passes around a pointer; the full layout is private to this TU.
// TWO-HANDLE CONTRACT (P6.5b2): the engine's REAL flow nests opens --
// LoadSpriteAnimation holds the .bin open while LoadSpriteSheet (called
// mid-parse, Animation.cpp:60) opens the sheet GIF through OpenDataFile,
// which re-opens the pack. MEASURED failure under the old single handle:
// the nested fOpen returned NULL, ImageGIF::Load failed, sheetID went 0xFF
// through the uint8 sheetIDs[] (Animation.cpp:39/62), and the
// gfxSurface[0xFF] deref crashed the proof (qa_p6_sprite W7/W8 RED, ticks=0).
//
// SHARED-UNDERLYING-HANDLE DESIGN (measured 2026-06-10): a second concurrent
// GFS_Open of the SAME fid fails in this environment -- witnessed
// p6_w_io_openfail=3 / nopen_hw=1 (GFS_Open returned NULL; per SBL source the
// failure sits in GFCB_Setup's CD-block command chain, GFS_CDB.C:84-129, NOT
// in slot exhaustion: jo's GFS_Init open_max=4, GFS_CDBBUF_NR=24; note SBL
// also LEAKS the GfsFile group on a failed open, GFS.C:493+507). Both engine
// handles are byte-windows over the same on-disc DATA.RSDK, so the backend
// keeps ONE underlying GFS handle, refcounted by fid, and serves any number
// of virtual handles from it: every p6_window_fill does GFS_Seek(sector)
// FIRST and GFS_TMODE_CPU reads busy-poll to completion (GFS.C:927-928), so
// interleaved per-slot refills cannot corrupt each other -- the same
// semantics the PC engine gets from multiple stdio opens of the pack file.
#define P6_GFS_NHANDLES 2
struct Saturn_FileIO {
    int   used;    // 1 between fOpen and fClose
    int   size;    // exact file size in bytes
    int   cursor;  // current read position (byte offset)
    GfsHn gfs;     // the SHARED underlying GFS handle (s_pack_gfs)
    int   win_off; // file offset of window start (sector-aligned); -1 = invalid
    int   win_len; // valid bytes in this handle's window
    unsigned char *win; // #251: per-handle 64 KB window in the cart (set at fOpen),
                        // NOT inline -- keeps the struct tiny + WRAM-H .bss low.
};
typedef struct Saturn_FileIO Saturn_FileIO;

static Saturn_FileIO s_handles[P6_GFS_NHANDLES];

// One refcounted GFS open shared by every virtual handle on the same fid.
static GfsHn  s_pack_gfs  = (GfsHn)0;
static Sint32 s_pack_fid  = -1;
static int    s_pack_refs = 0;
static int    s_pack_size = -1; // byte size of the underlying file (cached)
// #251 load-time: the SHARED underlying GFS access-pointer sector. GFS_Fread
// auto-advances it by nsct on success (GFS_CDB.C:411 GFCB_RtnPk ->
// GFCB_Seek(...,GFS_SEEK_CUR)), so a fill that continues sequentially is ALREADY
// positioned and the GFS_Seek is redundant -- skipping it avoids a CD re-seek
// (MEASURED ~100-178 ms/4 KB fill; 145 fills dominated the ~40 s engine load).
// -1 = unknown (force a seek): set on open/close/error.
static Sint32 s_pack_gfs_pos = -1;

// ---- CD/GFS bring-up (called once from p6_io_proof, AFTER p6_sgl_boot) -------
// Standard order: CDC_CdInit then GFS_Init (jo audio.c:110 -> fs.c:115). slInitSystem
// + slInitSynch already ran in p6_sgl_boot.c (steps 1-2); this owns steps 3-4. The
// CDC_CdInit proto comes from SEGA_CDC.H (pulled by SEGA_GFS.H:21).
int p6_gfs_init(void)
{
    // CD block init: iflag=0, stnby=0, ecc=5, retry=0xf (jo audio.c:110, proven).
    CDC_CdInit(0x00, 0x00, 0x05, 0x0f);
    p6_w_io_step = 3; // CDC_CdInit returned

    // Name-indexed root directory so GFS_NameToId("P6IO.BIN") resolves (jo fs.c:96-123).
    GFS_DIRTBL_TYPE(&s_gfs_dirtbl)    = GFS_DIR_NAME;
    GFS_DIRTBL_DIRNAME(&s_gfs_dirtbl) = s_gfs_dirname;
    GFS_DIRTBL_NDIR(&s_gfs_dirtbl)    = P6_GFS_MAX_DIR;

    int ndir = GFS_Init(P6_GFS_OPEN_MAX, s_gfs_work, &s_gfs_dirtbl);
    p6_w_io_gfsinit = ndir;
    p6_w_io_step = 4; // GFS_Init returned
    return ndir;
}

// Reduce "Data/Stages/Title/Scene1.bin" (or "P6IO.BIN") to the bare 8.3 name
// GFS_NameToId expects, UPPERCASED: ISO-9660 file identifiers are d-characters
// (upper-case A-Z 0-9 _), so the mkisofs-built disc records "SCENE1.BIN" while
// the engine asks for mixed-case "Scene1.bin" (Scene.cpp:297 sprintf). The
// dirtbl GFS matches against holds the on-disc form. Bare name must fit
// GFS_FNAME_LEN=12 (SEGA_GFS.H:37, memory rule sgl-gfs-fname-len-12-limit);
// longer names are truncated here and simply resolve to not-found, the same
// clean LoadFile-false path as before (e.g. "TileConfig.bin" -> 14 chars).
static char s_gfs_name[13];
static const char *p6_basename(const char *path)
{
    const char *base = path;
    const char *p;
    int i;
    for (p = path; *p; ++p) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    for (i = 0; base[i] && i < 12; ++i) {
        char c = base[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        s_gfs_name[i] = c;
    }
    s_gfs_name[i] = '\0';
    return s_gfs_name;
}

Saturn_FileIO *Saturn_fOpen(const char *path, const char *mode)
{
    // Read-only CD backend: reject any write/append/update mode request.
    if (mode) {
        const char *m;
        for (m = mode; *m; ++m) {
            if (*m == 'w' || *m == 'a' || *m == '+') {
                p6_w_io_openfail = 5;
                return (Saturn_FileIO *)0;
            }
        }
    }
    if (!path)
        return (Saturn_FileIO *)0;

    // Two-handle contract (see above): claim the first free slot.
    Saturn_FileIO *slot = (Saturn_FileIO *)0;
    int i;
    for (i = 0; i < P6_GFS_NHANDLES; ++i) {
        if (!s_handles[i].used) {
            slot = &s_handles[i];
            break;
        }
    }
    if (!slot) {
        p6_w_io_openfail = 1; // both slots busy: deeper nesting than the engine performs
        return (Saturn_FileIO *)0;
    }

    const char *base = p6_basename(path);

    Sint32 fid = GFS_NameToId((Sint8 *)base);
    p6_w_io_fid = (int)fid;
    if (fid < 0) {
        p6_w_io_openfail = 2; // not on disc
        return (Saturn_FileIO *)0;
    }

    int size;
    if (s_pack_refs > 0 && fid == s_pack_fid) {
        // Nested open of the already-open file: SHARE the underlying GFS
        // handle (see the shared-underlying-handle design note above).
        ++s_pack_refs;
        size = s_pack_size;
    }
    else if (s_pack_refs > 0) {
        // A DIFFERENT fid while another file is open never happens in the
        // pack-routed engine flow (every LoadFile resolves to DATA.RSDK);
        // refuse rather than risk the measured GFCB_Setup second-open fault.
        p6_w_io_openfail = 3;
        return (Saturn_FileIO *)0;
    }
    else {
        GfsHn gfs = GFS_Open(fid);
        if (gfs == (GfsHn)0) {
            p6_w_io_openfail = 3;
            return (Saturn_FileIO *)0;
        }

        // Force CPU software transfer (no SCU DMA) so no interrupt path is exercised.
        GFS_SetTmode(gfs, GFS_TMODE_CPU);

        Sint32 sctsz = 0, nsct = 0, lstsz = 0;
        GFS_GetFileSize(gfs, &sctsz, &nsct, &lstsz);
        size = (int)(sctsz * (nsct - 1) + lstsz); // exact byte size
        if (size < 0) {
            p6_w_io_openfail = 4;
            GFS_Close(gfs);
            return (Saturn_FileIO *)0;
        }
        s_pack_gfs  = gfs;
        s_pack_fid  = fid;
        s_pack_size = size;
        s_pack_refs = 1;
        s_pack_gfs_pos = -1; // #251: fresh GFS_Open -> pointer unknown, force a seek
    }

    // Slot is a VIRTUAL handle: own cursor + window over the shared GFS open.
    slot->used    = 1;
    slot->size    = size;
    slot->cursor  = 0;
    slot->gfs     = s_pack_gfs;
    slot->win_off = -1; // window invalid until the first read
    slot->win_len = 0;
    // #251: point this handle's 64 KB read-ahead window at its dedicated cart slot
    // (s_handles[0] -> 0x22700000, s_handles[1] -> 0x22710000).
    slot->win = (unsigned char *)(P6_CART_GFSWIN_BASE
                                  + (unsigned long)(slot - s_handles) * P6_GFS_WIN_BYTES);
    ++p6_w_io_nopen;
    if (p6_w_io_nopen > p6_w_io_nopen_hw)
        p6_w_io_nopen_hw = p6_w_io_nopen;
    return slot;
}

int Saturn_fClose(Saturn_FileIO *file)
{
    if (file && file->used) {
        file->gfs  = (GfsHn)0;
        file->used = 0;
        --p6_w_io_nopen;
        if (--s_pack_refs <= 0) {
            GFS_Close(s_pack_gfs);
            s_pack_gfs  = (GfsHn)0;
            s_pack_fid  = -1;
            s_pack_size = -1;
            s_pack_refs = 0;
            s_pack_gfs_pos = -1; // #251: handle closed -> pointer state gone
        }
    }
    return 0;
}

// Refill file->win so it covers `offset`: seek the access pointer to the
// containing sector (GFS_Seek unit = sector, ST-136-R2 sec 2.3) and read up to
// P6_GFS_WIN_SECTS sectors. GFS_TMODE_CPU busy-polls to completion, so the
// refill is synchronous and needs no interrupt plumbing.
// Task #227 wedge witnesses: the LAST refill's request + return values.
// After a wedge the savestate peek shows whether GFS_Seek failed, what
// sector was asked for, and whether GFS_Fread ever returned (lastfread
// stays the 0x7FFFFFFF sentinel while the wedge is INSIDE GFS_Fread).
int p6_w_gfs_fills    = 0;
int p6_w_gfs_lastsect = -1;
int p6_w_gfs_lastseek = -2;
int p6_w_gfs_lastfread = -2;

// #251 load-time: seeks_real counts the fills that STILL needed a real GFS_Seek
// (a genuine back-seek or a switch between the 2 virtual handles' files);
// seeks_real << fills proves the sequential pack reads went seek-free. The
// s_pack_gfs_pos tracker it keys off is declared up with the shared-handle
// statics (it is used by Saturn_fOpen/fClose, which precede this point).
int p6_w_gfs_seeks_real = 0;
// #251 IO-vs-CPU split: accumulate the vblanks (true 60 Hz, p6_perf.c) elapsed
// INSIDE the GFS_Seek+GFS_Fread of every fill. Vblanks only advance when
// interrupts are unmasked, so this measures the PHASE-2 reload IO (LoadSceneFolder
// /Assets/InitObjects) -- the masked load core (LoadGameConfig) contributes 0
// (frozen). io_vbl/60 = seconds the SH-2 spent spinning on the CD; compare to the
// ~17 s phase-2 wall time: io_vbl ~ 1020 => emulated-CD-bound, io_vbl << that =>
// SH-2 CPU (decode/parse) bound. Picks the fix (reduce GFS calls vs pre-decode).
extern volatile unsigned int p6_perf_vbl_count;
int p6_w_gfs_io_vbl = 0;

static int p6_window_fill(Saturn_FileIO *file, int offset)
{
    Sint32 sector      = offset / P6_GFS_SECTOR;
    Sint32 file_sects  = (file->size + P6_GFS_SECTOR - 1) / P6_GFS_SECTOR;
    Sint32 avail_sects = file_sects - sector;
    if (avail_sects <= 0)
        return 0;
    // #251 ADAPTIVE: stream 64 KB on a sequential continuation, but read only
    // 4 KB on a seek (new/scattered file -- avoid the read-waste regression).
    // Decided BEFORE the seek-skip check below, off the SAME s_pack_gfs_pos.
    Sint32 nsct = (sector == s_pack_gfs_pos) ? P6_GFS_WIN_SECTS : P6_GFS_SEEK_SECTS;
    if (nsct > avail_sects)
        nsct = avail_sects;

    ++p6_w_gfs_fills;
    p6_w_gfs_lastsect = (int)sector;
    p6_w_gfs_lastseek = 0x7FFFFFFF;
    p6_w_gfs_lastfread = 0x7FFFFFFF;
    unsigned int io_v0 = p6_perf_vbl_count; // #251 IO-time bracket (phase-2 only)
    // Only seek when the shared access pointer is NOT already at this sector.
    if (sector != s_pack_gfs_pos) {
        ++p6_w_gfs_seeks_real;
        p6_w_gfs_lastseek = (int)GFS_Seek(file->gfs, sector, GFS_SEEK_SET);
        if (p6_w_gfs_lastseek < 0) {
            s_pack_gfs_pos = -1; // desynced -> force a seek next time
            return 0;
        }
    } else {
        p6_w_gfs_lastseek = 0; // sequential continuation: no seek issued
    }
    p6_w_gfs_lastfread = (int)GFS_Fread(file->gfs, nsct, file->win, nsct * P6_GFS_SECTOR);
    if (p6_w_gfs_lastfread <= 0) {
        s_pack_gfs_pos = -1; // read failed -> pointer state unknown
        return 0;
    }
    s_pack_gfs_pos = sector + nsct; // GFS_Fread advanced the pointer past nsct
    p6_w_gfs_io_vbl += (int)(p6_perf_vbl_count - io_v0); // accumulate IO vblanks

    file->win_off = sector * P6_GFS_SECTOR;
    file->win_len = nsct * P6_GFS_SECTOR;
    if (file->win_off + file->win_len > file->size)
        file->win_len = file->size - file->win_off; // clamp final partial sector
    return 1;
}

unsigned long Saturn_fRead(void *buffer, unsigned long elementSize,
                           unsigned long elementCount, Saturn_FileIO *file)
{
    if (!buffer || !file || !file->used || elementSize == 0)
        return 0;

    unsigned long want  = elementSize * elementCount;
    unsigned long avail = (unsigned long)(file->size - file->cursor);
    if (want > avail)
        want = avail;

    unsigned char *dst  = (unsigned char *)buffer;
    unsigned long  done = 0;
    while (done < want) {
        int in_window = (file->win_off >= 0)
                     && (file->cursor >= file->win_off)
                     && (file->cursor < file->win_off + file->win_len);
        if (!in_window) {
            if (!p6_window_fill(file, file->cursor))
                break; // CD error: return the elements completed so far
        }
        unsigned long win_avail = (unsigned long)(file->win_off + file->win_len - file->cursor);
        unsigned long chunk     = want - done;
        if (chunk > win_avail)
            chunk = win_avail;
        memcpy(dst + done, file->win + (file->cursor - file->win_off), (size_t)chunk);
        file->cursor += (int)chunk;
        done += chunk;
    }
    return done / elementSize; // fread contract: number of full elements read
}

unsigned long Saturn_fWrite(const void *buffer, unsigned long elementSize,
                            unsigned long elementCount, Saturn_FileIO *file)
{
    (void)buffer;
    (void)elementSize;
    (void)elementCount;
    (void)file;
    return 0; // read-only backend
}

int Saturn_fSeek(Saturn_FileIO *file, long offset, int whence)
{
    if (!file || !file->used)
        return -1;

    long base;
    switch (whence) {
        case SEEK_SET: base = 0;             break;
        case SEEK_CUR: base = file->cursor;  break;
        case SEEK_END: base = file->size;    break;
        default: return -1;
    }
    long pos = base + offset;
    if (pos < 0)               pos = 0;
    if (pos > (long)file->size) pos = file->size;
    file->cursor = (int)pos;
    return 0;
}

long Saturn_fTell(Saturn_FileIO *file)
{
    if (!file || !file->used)
        return -1;
    return (long)file->cursor;
}
