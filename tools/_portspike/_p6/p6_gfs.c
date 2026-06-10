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
extern int p6_w_io_gfsinit; // GFS_Init return (ndir): >2 == backend up, <=2 == fail
extern int p6_w_io_fid;     // GFS_NameToId(P6IO.BIN): >=0 == file found on disc
extern int p6_w_io_step;    // p6_gfs_init progress 1..2 (localises a hang in one capture)

// ---- GFS state (BSS) --------------------------------------------------------
#define P6_GFS_OPEN_MAX  2                 // max simultaneously-open GFS handles (1 is enough)
#define P6_GFS_MAX_DIR   16                // root-dir entries the dirtbl can hold (disc has ~7)
#define P6_GFS_SECTOR    2048              // CD-ROM Mode 1 user-data bytes per sector
#define P6_GFS_WIN_SECTS 2                 // sliding-window size in sectors. 4 -> 2
                                           // (P6.5b1): frees 4 KB of pack BSS to pay
                                           // for the VDP2 present code against the
                                           // WRAM-H floor; loads are boot-time only.
#define P6_GFS_WIN_BYTES (P6_GFS_SECTOR * P6_GFS_WIN_SECTS) // 4 KB window
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
// SINGLE-HANDLE CONTRACT: the engine's Reader opens are strictly sequential in
// the P6 proofs (LoadDataPack CloseFile's the pack before OpenDataFile re-opens
// it per file; LoadSceneAssets holds exactly one file open at a time), so one
// handle + one window suffice; a second concurrent fOpen returns NULL loudly
// rather than silently corrupting the window.
struct Saturn_FileIO {
    int   used;    // 1 between fOpen and fClose
    int   size;    // exact file size in bytes
    int   cursor;  // current read position (byte offset)
    GfsHn gfs;     // OPEN GFS handle, held until fClose (windowed access)
    int   win_off; // file offset of window start (sector-aligned); -1 = invalid
    int   win_len; // valid bytes in s_window
};
typedef struct Saturn_FileIO Saturn_FileIO;

static Saturn_FileIO s_handle;
static unsigned char s_window[P6_GFS_WIN_BYTES];

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
            if (*m == 'w' || *m == 'a' || *m == '+')
                return (Saturn_FileIO *)0;
        }
    }
    if (!path)
        return (Saturn_FileIO *)0;
    if (s_handle.used)
        return (Saturn_FileIO *)0; // single-handle contract (see above)

    const char *base = p6_basename(path);

    Sint32 fid = GFS_NameToId((Sint8 *)base);
    p6_w_io_fid = (int)fid;
    if (fid < 0)
        return (Saturn_FileIO *)0; // not on disc

    GfsHn gfs = GFS_Open(fid);
    if (gfs == (GfsHn)0)
        return (Saturn_FileIO *)0;

    // Force CPU software transfer (no SCU DMA) so no interrupt path is exercised.
    GFS_SetTmode(gfs, GFS_TMODE_CPU);

    Sint32 sctsz = 0, nsct = 0, lstsz = 0;
    GFS_GetFileSize(gfs, &sctsz, &nsct, &lstsz);
    int size = (int)(sctsz * (nsct - 1) + lstsz); // exact byte size
    if (size < 0) {
        GFS_Close(gfs);
        return (Saturn_FileIO *)0;
    }

    // Handle stays OPEN; bytes are served through the sliding window on demand.
    s_handle.used    = 1;
    s_handle.size    = size;
    s_handle.cursor  = 0;
    s_handle.gfs     = gfs;
    s_handle.win_off = -1; // window invalid until the first read
    s_handle.win_len = 0;
    return &s_handle;
}

int Saturn_fClose(Saturn_FileIO *file)
{
    if (file && file->used) {
        GFS_Close(file->gfs);
        file->gfs  = (GfsHn)0;
        file->used = 0;
    }
    return 0;
}

// Refill s_window so it covers `offset`: seek the access pointer to the
// containing sector (GFS_Seek unit = sector, ST-136-R2 sec 2.3) and read up to
// P6_GFS_WIN_SECTS sectors. GFS_TMODE_CPU busy-polls to completion, so the
// refill is synchronous and needs no interrupt plumbing.
static int p6_window_fill(Saturn_FileIO *file, int offset)
{
    Sint32 sector      = offset / P6_GFS_SECTOR;
    Sint32 file_sects  = (file->size + P6_GFS_SECTOR - 1) / P6_GFS_SECTOR;
    Sint32 nsct        = file_sects - sector;
    if (nsct <= 0)
        return 0;
    if (nsct > P6_GFS_WIN_SECTS)
        nsct = P6_GFS_WIN_SECTS;

    if (GFS_Seek(file->gfs, sector, GFS_SEEK_SET) < 0)
        return 0;
    if (GFS_Fread(file->gfs, nsct, s_window, nsct * P6_GFS_SECTOR) <= 0)
        return 0;

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
        memcpy(dst + done, s_window + (file->cursor - file->win_off), (size_t)chunk);
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
