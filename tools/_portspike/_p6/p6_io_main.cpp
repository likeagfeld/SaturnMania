// =============================================================================
// p6_io_main.cpp -- engine-headers proof body for the DECOUPLED GFSDEMO-style
// file-I/O proof (Task #206, P6.2).
//
// This is the ONE TU in the decoupled image that includes the REAL engine
// headers (WITH `namespace RSDK`). It owns:
//   (a) every gate witness (p6_w_io_*) + the byte-order calibration magic;
//   (b) the three RSDK symbols the unmodified Core_Reader.o references but that
//       no other linked TU provides (MEASURED -- see below); and
//   (c) `p6_io_proof()`, the proof body the asm `_main` shim (p6_io_boot.s)
//       jsr's into after BSS-zero + ctors.
//
// WHY A SEPARATE TU FROM THE SGL/GFS BRING-UP: the engine headers
// (RetroEngine.hpp -> ... -> the whole RSDK:: surface) cannot share a
// translation unit with the SGL C headers (SGL.H / SEGA_GFS.H pull SL_DEF.H
// C-linkage decls that collide with the engine's C++ namespace). So SGL
// bring-up lives in p6_sgl_boot.c and the GFS backend in p6_gfs.c; this TU
// reaches them through the extern "C" bridge below.
//
// THE THREE ENGINE SYMBOLS Core_Reader.o NEEDS (measured by linking and reading
// the unresolved set; all three are in the RETRO_SATURN_FILEIO LoadFile path):
//   - RSDK::PrintLog            -- Reader.cpp logs the open; a no-op satisfies it.
//   - RSDK::GenerateHashMD5     -- referenced for the data-pack path; NEVER
//                                  called here (useDataPack stays false), only
//                                  needs to resolve at link time.
//   - RSDK::SKU::userFileDir    -- char[0x100] read at Reader.cpp:307 to prefix
//                                  the path; zero-filled -> empty prefix -> the
//                                  filename passed to fOpen stays "P6IO.BIN".
//   Everything else LoadFile/ReadBytes/InitFileInfo/CloseFile needs is either
//   inline in Reader.hpp or comes from libc (strcpy/strlen/memset) + the
//   Saturn_* GFS backend (p6_gfs.c). EXCEPTION: snprintf is defined LOCALLY below
//   (see the "local snprintf" block) -- not pulled from newlib -- to keep the
//   image under the 0x060C0000 SGL BSS floor.
//
// NO main() HERE: the asm shim p6_io_boot.s provides `_main` (the symbol SGL's
// SLSTART jmp's to) and performs the C-runtime init, then calls p6_io_proof.
//
// Witnesses (read by tools/_portspike/qa_p6_io.py from the savestate):
//   p6_w_magic       0x12345678 const -> .rodata (loaded image, WRAM-H); the
//                    gate calibrates WRAM byte order from it.
//   p6_w_io_loaded   1 once RSDK::LoadFile("P6IO.BIN") returns true.
//   p6_w_io_filesize finfo.fileSize after the open (expect 256).
//   p6_w_io_firstbytes first 4 file bytes big-endian packed (expect 0xDEADBEEF).
//   p6_w_io_gfsinit  GFS_Init return (ndir): >2 == backend up.
//   p6_w_io_fid      GFS_NameToId(P6IO.BIN): >=0 == file found on disc.
//   p6_w_io_step     boot progress (localises a fault in one capture), bracketing
//                    jo's PROVEN bring-up order: 1 slInitSystem done, 2 slInitSynch
//                    done, 3 CDC_CdInit done, 4 GFS_Init done.
// Compiled with -DRETRO_SATURN_FILEIO so this TU's FileInfo layout + the
// fOpen/fRead routing match Core_Reader.o exactly.
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

// #317: GLOBAL-scope forward decl of the vblank counter (defined in p6_perf.c, C
// linkage). Declared here -- BEFORE the `namespace RSDK {` blocks below -- so the
// draw sub-brackets in p6_draw_flipped (which lives inside namespace RSDK, ~1963)
// resolve to ::p6_perf_vbl_count, NOT a namespaced RSDK::p6_perf_vbl_count (which
// would be a distinct, undefined symbol AND collide with the global decl at ~2589
// under `using namespace RSDK`). A duplicate global extern is a legal redeclaration.
extern volatile unsigned int p6_perf_vbl_count;

// ---- Gate witnesses + byte-order calibration magic --------------------------
// `used` defeats any dead-strip; magic is const so it lands in .rodata (loaded),
// the int32 witnesses are zero-init .bss pinned to WRAM-H by the linker so they
// share magic's byte order in the savestate.
extern "C" {
__attribute__((used)) extern const int32 p6_w_magic = 0x12345678;
__attribute__((used)) int32 p6_w_io_loaded     = 0;
__attribute__((used)) int32 p6_w_io_filesize   = 0;
__attribute__((used)) int32 p6_w_io_firstbytes = 0;
__attribute__((used)) int32 p6_w_io_gfsinit    = 0;
// P6.8 I1 (S1+7.3 camera-local pool): manifest-enumeration witnesses. The engine
// LoadScene per-entity loop (Scene.cpp) folds EVERY placed scene entity -- incl.
// the ones currently DROPPED at slotID>=1152 -- into these, proving the
// "enumerate every placed entity with correct class/slot/pos" path the I3 STORED
// manifest builds on, with ZERO new allocation. Reset at the top of each
// LoadScene; gate qa_p6_manifest asserts n/maxslot vs the census (GHZ1 1041/1040).
__attribute__((used)) int32 p6_w_manifest_n       = 0;
__attribute__((used)) int32 p6_w_manifest_maxslot = 0;
__attribute__((used)) int32 p6_w_manifest_csum    = 0;
__attribute__((used)) int32 p6_w_io_fid        = 0;
__attribute__((used)) int32 p6_w_io_step       = 0;
// P6.5b2 nested-open diagnostics (written by p6_gfs.c):
//   p6_w_io_openfail  LAST fOpen failure site: 0 none, 1 no free slot,
//                     2 NameToId<0, 3 GFS_Open NULL, 4 size<0, 5 write mode.
//   p6_w_io_nopen     current count of concurrently-open handles (high-water
//                     in p6_w_io_nopen_hw): proves whether the nested second
//                     open ever actually happened.
__attribute__((used)) int32 p6_w_io_openfail   = 0;
__attribute__((used)) int32 p6_w_io_nopen      = 0;
__attribute__((used)) int32 p6_w_io_nopen_hw   = 0;
}

// ---- The 3 engine symbols Core_Reader.o references (exact namespaces) --------
// Mangling MUST match Core_Reader.o, so these are defined inside the engine's
// own namespaces (NOT extern "C"). Verified against the engine source:
//   PrintLog        rsdkv5-src/.../RSDK/Dev/Debug.hpp:36          (namespace RSDK)
//   GenerateHashMD5 rsdkv5-src/.../RSDK/Storage/Text.hpp:135      (namespace RSDK)
//   userFileDir     rsdkv5-src/.../RSDK/User/Core/UserStorage.hpp:402 (RSDK::SKU)
namespace RSDK {
void PrintLog(int32 mode, const char *message, ...)
{
    (void)mode;
    (void)message;
}
#if !defined(P6_SCENE_TEST)
// P6IO builds only: a link-satisfying no-op (the data-pack path never runs
// there). The P6SCENE build compiles the REAL RSDK::GenerateHashMD5 from
// Storage/Text.cpp instead -- OpenDataFile's hash lookup (Reader.cpp:188-196)
// needs true MD5 to resolve filenames inside Data.rsdk.
void GenerateHashMD5(uint32 *buffer, char *textBuffer, int32 textBufferLen)
{
    (void)buffer;
    (void)textBuffer;
    (void)textBufferLen;
}
#endif
namespace SKU {
char userFileDir[0x100];
}
} // namespace RSDK

// ---- local snprintf (kills the newlib float-printf pull) ---------------------
// The engine's UNMODIFIED Reader.cpp is the ONLY snprintf caller in the whole
// P6IO=1 program (verified: nothing in src/ or jo-engine/ references snprintf),
// and `sprintf_s(x,_,...)` expands to `snprintf(x,_,__VA_ARGS__)`
// (RetroEngine.hpp:102). Every format string it reaches is %s / %d only
// (Reader.cpp:114,307,308,371 -- no %f/%g/%e). newlib's snprintf nonetheless
// drags in _svfprintf_r -> _dtoa_r -> soft-float (~24 KB .text + ~1 KB BSS),
// which pushes _end past the 0x060C0000 SGL work-area floor (the Phase 1.15
// BSS-overflow class). Defining snprintf HERE -- this object links before -lc,
// so the linker resolves the engine's reference to ours and never opens newlib's
// member -- reclaims that space. Handles exactly %s/%d/%c/%% (the engine's full
// format-spec set in this build); unknown specs are emitted literally so nothing
// silently vanishes. Built with -fno-builtin, so snprintf is not a GCC builtin
// and this definition is clean. Compiled into the proof object ONLY; the shipping
// `make` (P6IO unset) never links it and keeps newlib's snprintf.
#include <stdarg.h>
extern "C" int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t pos = 0;
#define P6_PUT(ch)                                                                                  \
    do {                                                                                            \
        if (pos + 1 < size)                                                                         \
            buf[pos] = (char)(ch);                                                                  \
        ++pos;                                                                                      \
    } while (0)
    for (const char *p = fmt; *p; ++p) {
        if (*p != '%') {
            P6_PUT(*p);
            continue;
        }
        ++p;
        if (*p == 0)
            break;
        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                while (*s) {
                    P6_PUT(*s);
                    ++s;
                }
                break;
            }
            case 'd': {
                int v = va_arg(ap, int);
                unsigned int uv;
                int neg = 0;
                if (v < 0) {
                    neg = 1;
                    uv  = (unsigned int)(-(long)v); // safe for INT_MIN
                }
                else {
                    uv = (unsigned int)v;
                }
                char tmp[12];
                int n = 0;
                if (uv == 0)
                    tmp[n++] = '0';
                while (uv) {
                    tmp[n++] = (char)('0' + (uv % 10u));
                    uv /= 10u;
                }
                if (neg)
                    P6_PUT('-');
                while (n)
                    P6_PUT(tmp[--n]);
                break;
            }
            case 'c': {
                int c = va_arg(ap, int);
                P6_PUT(c);
                break;
            }
            case '%': {
                P6_PUT('%');
                break;
            }
            default: {
                P6_PUT('%');
                P6_PUT(*p);
                break;
            }
        }
    }
    if (size) {
        if (pos < size)
            buf[pos] = 0;
        else
            buf[size - 1] = 0;
    }
#undef P6_PUT
    va_end(ap);
    return (int)pos;
}

// ============================================================================
// P6_IO_TEST (io-green, bare-SLSTART boot): the proof body the asm _main shim
// (p6_io_boot.s) jsr's into. It does the FULL bring-up itself (p6_sgl_boot +
// p6_gfs_init) then LoadFile then parks. The io-jo build (Path 2, jo HAL boot)
// does NOT define P6_IO_TEST: jo owns slInitSystem/CDC_CdInit/GFS_Init and the
// park (jo_core_run), so only the lean p6_io_run() below is compiled there.
// ============================================================================
#ifdef P6_IO_TEST
// ---- Bridge to the SGL bring-up + GFS backend (separate C TUs) --------------
extern "C" {
void p6_sgl_boot(void); // p6_sgl_boot.c: slInitSystem (step1) + slInitSynch (step2)
                        // -- jo's PROVEN pre-CD bring-up; CALLED first (see below).
                        // Its slInitSystem ref also pulls sglI00.o SLSTART into the
                        // link, placed at ___Start=0x06004000 by p6_io.linker.
int  p6_gfs_init(void); // p6_gfs.c:      CDC_CdInit (step3) + GFS_Init (step4)
}

// ---- The proof body the asm shim jsr's into ---------------------------------
extern "C" void p6_io_proof(void)
{
    // 1) SGL + CD/GFS bring-up in jo's PROVEN order: slInitSystem FIRST, THEN
    //    CDC_CdInit + GFS_Init. This mirrors jo-engine's boot -- the EXACT bring-up
    //    the shipping Saturn build proves works with this LIBCD.A / LIBSGL.A:
    //      jo_core_init_vdp -> slInitSystem(JO_TV_RES, __jo_sprite_def, JO_FRAMERATE)
    //      jo_audio_init    -> CDC_CdInit(0x00,0x00,0x05,0x0f)   (audio.c:110)
    //      jo_fs_init       -> GFS_Init                          (fs.c:115)
    //    REFINED ROOT CAUSE (Task #206, MEASURED): with slInitSystem REMOVED,
    //    CDC_CdInit faulted inside CDSUB_CdCmd (the CD-block command/response
    //    handshake) -- master exception, saved PC=0x0600b2c0, SR=0xf0, BIOS spin
    //    handler at 0x06000956. CDC_CdInit needs the SGL system runtime slInitSystem
    //    establishes; jo always calls it first. The image boots through SGL's
    //    ___Start SLSTART (GBR=0x060FFC00 set), so slInitSystem runs in the same
    //    runtime jo proves works (the prior crt0-era GBR-relative VBlank fault is
    //    gone at its source). GFS_TMODE_CPU busy-polls to completion, so the read
    //    path adds no interrupt dependency. The step witness brackets each call so
    //    any residual fault lands on a known boundary (0 slInitSystem / 1 slInitSynch
    //    / 2 CDC_CdInit / 3 GFS_Init faulted; 4 == all init done).
    p6_sgl_boot();   // slInitSystem + slInitSynch (steps 1-2)
    p6_gfs_init();   // CDC_CdInit (step 3) + GFS_Init (step 4)

    // 2) Exercise the UNMODIFIED engine RSDK::LoadFile against the on-disc file.
    //    RETRO_SATURN_FILEIO routes fOpen/fRead/fSeek/fTell/fClose to p6_gfs.c.
    FileInfo finfo;
    InitFileInfo(&finfo);
    if (LoadFile(&finfo, "P6IO.BIN", FMODE_RB)) {
        p6_w_io_loaded   = 1;
        p6_w_io_filesize = finfo.fileSize;
        uint8 hdr[4]     = { 0, 0, 0, 0 };
        ReadBytes(&finfo, hdr, 4);
        p6_w_io_firstbytes = ((int32)hdr[0] << 24) | ((int32)hdr[1] << 16)
                           | ((int32)hdr[2] << 8) | (int32)hdr[3];
        CloseFile(&finfo);
    }

    // Park in .text so the savestate finds PC inside the core code range.
    for (;;) {
    }
}
#endif // P6_IO_TEST

// ============================================================================
// P6_SCENE_TEST (Task #207, P6.3 Path A): the engine LoadScene proof hosted in
// the PROVEN jo image (`make P6SCENE=1`). This block owns:
//   (a) the qa_p6_scene.py witnesses (p6_w_scene_*, WRAM-H .bss, gate reads
//       them from game.map symbol addresses);
//   (b) every engine global the measured link closure of {Core_Reader.o,
//       Scene_Scene.o, Storage_Storage.o, miniz.o, p6_gfs.o} leaves undefined
//       (tools/_portspike/_p6/_closure.py output, 2026-06-09) -- as three
//       groups: relocated pointers (WRAM-L backing), absolute-address arrays
//       (WRAM-L, zero image cost), and small WRAM-H scalars;
//   (c) a WRAM-L _sbrk + the full newlib syscall set (p6_syscalls.c:39-57
//       pattern -- defining ANY one syscall while newlib's lib_a-syscalls.o is
//       still pulled for _exit would multiple-define; defining the WHOLE set
//       keeps that member out of the link, and newlib's own _sbrk would grow
//       from _end=0x060B5C30 into the 0x060C0000 SGL work-area floor with NO
//       ceiling -- the Phase 1.15 BSS-overflow class);
//   (d) p6_scene_run(), the one-shot proof body main.c calls right after
//       jo_core_init (jo's GFS already live, same contract as p6_io_run).
//
// WRAM-L map for the proof (bank 0x00200000..0x002FFFFF; UNTOUCHED at hook
// time -- first game writer is TITLE.DAT staging at 0x210000 AFTER the hook
// returns, and the gate's witnesses are copied to WRAM-H before that, so
// post-proof clobber of this bank is harmless; measured in the Task-E recon,
// src/main.c:92 + src/rsdk/* region map):
//   0x00200000..0x0029A000  _sbrk heap (0x9A000 = 630,784 B; InitStorage's
//                           STOCK P4 pools = 622,592 B + nano-malloc headers)
//   0x0029A000..0x002BFA00  objectEntityList  448 x 344        = 0x25A00
//   0x002BFA00..0x002CCB20  tileLayers        4 x 13384        = 0xD120
//   0x002CCB20..0x002F4B84  dataStorage       5 x 32788        = 0x28064
//   0x002F4C00..0x002FA2C0  Group-B absolute arrays (.equ symbols below)
//   0x002FA300              shared DEAD-dummy target (never dereferenced --
//                           measured: LoadSceneAssets touches NONE of the six
//                           DEAD globals; see per-symbol notes below)
// p6_scene_run() zeroes [0x0029A000, 0x002FA300) before InitStorage -- WRAM-L
// is NOT covered by SLSTART's .bss zeroing, and the parse relies on zero-init
// for stageObjectIDs / screens / sceneInfo-adjacent state.
// ============================================================================
#ifdef P6_SCENE_TEST
#include <string.h>

// ---- WRAM-L layout constants (P6.5a revision, Task #208) ----------------------
// v3 of the map: the STG pool is proof-trimmed 256->64 KB (Storage.cpp
// P6_SCENE_TEST branch, measured Title STG = 13,184 B) so tilesetPixels gets
// REAL 262,144 B backing -- the engine's own GIF decoder (Sprite.cpp
// ReadGifPictureData) now writes the full tileset INTO it via LoadStageGIF.
// dataFileList[0x700] (57,344 B, P6.4, measured pack fileCount = 1677) and
// the MUS/SFX trims carry over from v2.
// v4 (P6.5b1 ROOT-CAUSE FIX): the heap window grows +48 KB. MEASURED: the
// engine's ReadCompressed -> mz_uncompress allocates ~43.7 KB TRANSIENTLY
// (mz_inflate's internal state: tinfl_decompressor ~10.9 KB + 32 KB dict);
// every prior map left only ~8 KB of heap slack beyond the 5 pools, so the
// malloc FAILED, inflateInit returned MZ_MEM_ERROR, the engine ignored the
// return (upstream behavior), and every layer layout silently stayed ZERO
// since P6.3 -- invisible until qa_p6_vdp2.py's layout-derived witnesses.
// P6.7c map v5: LoadSceneFolder goes LIVE, rooting LoadTileConfig (writes
// collisionMasks/tileInfo) and the inline Clear3DScenes (MEM_ZEROs
// modelList[0x100] = 11,264 B -- through the old 256 B DEAD dummy that would
// have zeroed PAST the end of WRAM-L). Three new REAL windows are funded by
// (a) correcting the STALE entityList sizing (448 was pre-P6.7a; ENTITY_COUNT
// is 0xC0 = 192 since the Title-scale retarget, Object.hpp Saturn branch) and
// (b) the STORAGE_ENTRY_COUNT 0x1000->0x800 Saturn retarget (Storage.hpp,
// 32,788 -> 16,404 B per dataset). GROUPB + DEAD addresses are UNCHANGED so
// the .equ absolutes below and every consumer survive verbatim.
// P6.7 W11b map v7.1 (Task #226): the GHZ-SCALE flip. objectEntityList grows
// to the P6.7b contract (ENTITY_COUNT 0x500 x 344 B = 0x6B800 -- GHZ1 places
// 1,041 scene entities), which EVICTS tilesetPixels from WRAM-L (the bank
// cannot hold both: 0x6B800 + 0x40000 + the rest exceeds 1 MB by ~190 KB).
// tilesetPixels becomes a LOAD-PHASE TRANSIENT aliasing the entityList
// window (see its definition below) -- WRAM-H could not host it either
// (a resident 256 KB .bss backing pushed the pack tail to 0x060DE244, over
// the 0x060C0000 overlay floor; an 8 KB jo-pool trim to fund it MEASURED
// FATAL: the boot jo_mallocs >8 KB before the pack mount, bisect
// 2026-06-11). With the transient alias the linked tail sits at 0x0609E234
// (138,700 B under the floor) at the full shipping pool. spriteAnimationList
// moves WRAM-H -> WRAM-L window. Heap shrinks to the C5 pools-exact
// contract. GROUPB absolutes (palettes etc.) keep their v5 addresses.
// Map v8 (P6.7 Player wave step B, Task #227): the DUAL-STRIDE entity pool
// (Object.hpp: 64 wide(556) reserve + 1088 narrow(344) scene + 64 wide temp
// = 0x6CC00) needs +0x1400 over the uniform v7.1 window. LAYOUTBANDS trims
// its window 0xD400 -> 0xC800 (GHZ1LAYT.BIN = 51,094 B, 106 B slack).
// Map v8.1 (step B sweep ROOT CAUSE, 2026-06-12): v8 ALSO trimmed HEAP by
// 0x800 ("measured use 0x41800") -- that measurement was WRONG. miniz's
// mz_inflateInit allocates inflate_state = ~43,712 B (tinfl_decompressor
// ~10,920 + the 32,768 B LZ dict = the documented ~44 KB transient); after
// the 0x380F8 pools the v8 window left 42,760 B, so the FIRST mz_uncompress
// malloc hit the _sbrk ceiling (MEASURED on p6_b1.mcs: p6_brk frozen at
// 0x2380F8, nano free-list sane, lay refills=0, sht fetches=0, K3/K4
// packed garbage from the failed ReadCompressed, H5 tiles 0x0000 from the
// never-filled window pool). HEAP_END restored to the PROVEN v7.1 0x43000;
// the +0x800 is funded by shifting LAYOUTBANDS..LAYSCRATCH up 0x800 into
// the 0x800 free margin at 0x2FA400..0x2FAC00 -- exact fit, GROUPB and
// everything above byte-identical.
#define P6_LW_HEAP_BASE    0x00200000u
#define P6_LW_HEAP_END     0x00243000u // v8.1: pools 0x380F8 + miniz inflate_state ~0xAAC0 + ~1 KB slack (PROVEN v7.1 size)
#define P6_LW_ENTITYLIST   0x00243000u // dual-stride pool 0x6CC00 (ENTITYLIST_SIZE_BYTES) -> 0x2AFC00
#define P6_LW_COLLMASKS    0x002AFC00u // DEAD sink (raw masks unwritten on
                                       // Saturn since the packed arm); the
                                       // extern pointer needs an address.
                                       // W11a: the SAME window carries the
                                       // LIVE band store below -- safe
                                       // because ZERO Saturn code paths
                                       // access collisionMasks (measured;
                                       // macro seam + P6_CM arm)
#define P6_LW_LAYOUTBANDS  0x002AFC00u // cd/GHZ1LAYT.BIN, 51,094 B <= 0xC796 (window 0xC800) -> 0x2BC400
#define P6_LW_DATASTORAGE  0x002BC400u // C1: 5 * 32 + backings = 24,736 <= 0x6100 -> 0x2C2500
#define P6_LW_DATAFILELIST 0x002C2500u // C3: 0x6A0 * 24    = 0x9F00  (window 0xA000) -> 0x2CC500
#define P6_LW_TILELAYERS   0x002CC500u // 8 * sizeof(TileLayer) <= 0x1A300 -> 0x2E6800
#define P6_LW_TILEINFO     0x002E6800u // 2 * 0x400 * 5     = 0x2800  -> 0x2E9000 (LIVE: LoadTileConfig)
#define P6_LW_MODELLIST    0x002E9000u // 0x100 * 44        = 0x2C00  -> 0x2EBC00 (Clear3DScenes MEM_ZERO target)
#define P6_LW_SPRANIM      0x002EBC00u // SPRFILE_COUNT * sizeof(SpriteAnimation) = 0x7000 -> 0x2F2C00
// W11b v7.1: the PERSISTENT band-inflate scratch is a FIXED window, NOT a
// DATASET_TMP tenant -- MEASURED stall (scene step=2): scratch 32K + GIF
// decoder 24,892 + tempEntityList 22K + editableVarList in TMP overcommits
// the 80K pool (the C2 enumerate-every-tenant lesson, third occurrence).
// Funded by the band-window trim 0xE000 -> 0xD400 (GHZ actual 51,094).
#define P6_LW_LAYSCRATCH   0x002F2C00u // 0x8000 (largest raw band 32,768) -> 0x2FAC00 == GROUPB_BASE
                                       // (v8.1: the old 0x800 margin funds the HEAP restore)
#define P6_LW_GROUPB_BASE  0x002FAC00u // absolute arrays below (UNCHANGED)
#define P6_LW_GROUPB_END   0x002FFEC0u
#define P6_LW_DEAD         0x002FFF00u // shared dummy for measured-DEAD pointers (256 B tail)
#define P6_LW_ZERO_BASE    P6_LW_ENTITYLIST
#define P6_LW_ZERO_END     P6_LW_DEAD

// ---- (a) Gate witnesses (qa_p6_scene.py:98-132) ------------------------------
// Copied from objectEntityList[slot + RESERVE_ENTITY_COUNT].position AFTER
// LoadSceneAssets returns; slots from the offline parse of Title/Scene1.bin
// (parse_title_entities.py): TitleLogo/EMBLEM slot 5, TitleSonic slot 8,
// Title3DSprite slot 16. step legend (diagnostic): 1 = InitStorage OK,
// 2 = scene pre-state set, 3 = LoadSceneAssets returned, 4 = witnesses copied.
extern "C" {
__attribute__((used)) int32 p6_w_scene_loaded      = 0;
__attribute__((used)) int32 p6_w_scene_sonic_x     = 0;
__attribute__((used)) int32 p6_w_scene_sonic_y     = 0;
__attribute__((used)) int32 p6_w_scene_emblem_x    = 0;
__attribute__((used)) int32 p6_w_scene_emblem_y    = 0;
__attribute__((used)) int32 p6_w_scene_t3d_x       = 0;
__attribute__((used)) int32 p6_w_scene_t3d_y       = 0;
__attribute__((used)) int32 p6_w_scene_step        = 0;
__attribute__((used)) int32 p6_w_scene_initstorage = 0;
// Task #251 (load-time localization): fine load-phase breadcrumb. Set BEFORE
// each load sub-step so a captured value names the op CURRENTLY executing. The
// existing coarse witnesses (scene_step/pack_mounted/cfg_globalcount/cont_frames)
// partition the load into BIOS/staged/pack/config/phase-2 buckets; this resolves
// the two unresolved buckets internally: the masked-core staged residency loads
// (10..18 = 8 sheets + probes) and phase-2 p6_scene_load_and_arm (30..37). A
// wall-clock SaveFrame sweep reading this names the dominant sub-step. Single
// int store = zero frame-budget cost (skill v2.6.0 lesson 2: no hashing in path).
__attribute__((used)) int32 p6_w_load_step         = 0;
// P6.4 (Task #225, qa_p6_pack.py): the original-Data.rsdk ingestion witnesses.
__attribute__((used)) int32 p6_w_pack_mounted   = 0; // LoadDataPack("Data.rsdk") return
__attribute__((used)) int32 p6_w_pack_filecount = 0; // dataPacks[0].fileCount (expect 1677)
__attribute__((used)) int32 p6_w_pack_used      = 0; // 1 == scene LoadFile rode the pack (fileOffset > 0)
// P6.5a (Task #208, qa_p6_gif.py): engine GIF-decode witnesses.
__attribute__((used)) int32 p6_w_gif_loaded = 0; // 1 == LoadStageGIF + hash completed
__attribute__((used)) int32 p6_w_gif_hash   = 0; // djb2-xor over tilesetPixels[0x40000]
__attribute__((used)) int32 p6_w_gif_b0     = 0; // tilesetPixels[0] (offline model 0x01)
// LOAD-FIX EXPERIMENT (#271): skip the 262,144-px tileset LZW decode (the MEASURED
// ~4.4s S8 cost, cache-thrashing on the SH-2). 1 in the front-end build only ->
// LoadStageGIF early-returns. CONFIRMS the decode is the lever (spike 7.9->~3.5s)
// before the real pre-decode fix replaces the skip. GHZ build = 0 (untouched).
#if defined(P6_FRONTEND_TITLE)
extern "C" { __attribute__((used)) int p6_skip_gif_decode = 1; }
#else
extern "C" { __attribute__((used)) int p6_skip_gif_decode = 0; }
#endif
// P6.5b1 (Task #208, qa_p6_vdp2.py): VDP2 present witness.
__attribute__((used)) int32 p6_w_vdp2_done  = 0; // 1 == engine layer presented on NBG1
// p6_vdp2.c (C TU): presents the engine-decoded Island layer through NBG1.
// W11b SPLIT: cells+CRAM upload runs while the transient tilesetPixels is
// alive (post-LoadSceneFolder); the layout half runs post-LoadSceneAssets.
void p6_vdp2_upload_cells(const unsigned char *tilesetPx);
#if defined(P6_AIZ_TEST)
extern "C" void p6_vdp2_aiz_blank_setup(int charIdx); // R1 (AIZ): clear an unused CEL char transparent for empty FG cells
// Task #309 gate-2 follow-up: the FG present's empty-cell (0xFFFF) blank-char
// index (p6_vdp2.c). AIZ sets it to 64 (its only-transparent CEL char, since AIZ
// tile-0 is opaque orange); GHZ/GHZCutscene need -1 (= tile 0, the GHZ transparent
// convention). It is a PERSISTENT global -> the live AIZ->GHZCutscene seam MUST
// reset it to -1 before the GHZCutscene present runs, else GHZCutscene's empty FG
// cells render as opaque GHZ-tile-64 (the checkerboard bleed into the black sky).
extern "C" int p6_fg_blank_char_override;
// R2.1 (AIZ Background render): the 4-bpp BG plane on NBG0 (p6_vdp2.c). upload =
// once at load (CHR->B1, CRAM[256..303], static map from BG4, NBG0 config);
// frame = per-frame re-assert NBG0ON + scroll + BG-below-FG priority split.
extern "C" void p6_vdp2_aiz_bg_upload(const unsigned short *chr_cart, int chr_words,
                                      const unsigned char *cmp,
                                      const unsigned short *map_cart, int map_words,
                                      const unsigned short *pal565);
extern "C" void p6_vdp2_aiz_bg_frame(int bg4_sx, int bg1_sx, int bg3_sx);
// R2.4/R2.5: camera-streamed BG parallax -- re-window an NBG plane's map from a
// resident full layout as the camera crosses a tile. which: 0=BG4/NBG0(B1 map),
// 1=BG1/NBG2(B0 map). wrap: source x wraps mod l3_xs (BG1 tiles at 80).
extern "C" void p6_vdp2_aiz_bg_stream(int which, unsigned int map_addr,
                                      const unsigned short *l3, int l3_xs, int l3_ys, int wrap,
                                      const unsigned char *rmp, const unsigned char *bnk,
                                      int scroll_x, int scroll_y);
#endif
void p6_vdp2_present_layout(const unsigned short *layout, int wshift,
                            const unsigned short *pal565);
#if defined(P6_FRONTEND_TITLE)
// CP5b.3 (Task #272): the Mania title BACKDROP (green island + clouds) on NBG1.
// Cells uploaded separately (p6_vdp2_upload_cells, in the load gap); this builds
// the composite map + CRAM + display AFTER LoadSceneAssets (palette/layout final).
void p6_vdp2_present_title_backdrop(const unsigned char *tilesetPx,
                                    const unsigned short *islandLay, int islandWShift,
                                    const unsigned short *cloudLay, int cloudWShift,
                                    const unsigned short *pal565);
void p6_vdp2_title_backdrop_scroll(int frame); // per-frame cloud/island drift
// CP5b.6 (Task #276): the title island MODE-7 ROTATION via VDP2 RBG0 + per-line
// coefficient table. arm() builds the RBG0 map/RPT/coeff control ONCE at load;
// frame(angle,sine,cosine) rewrites the per-line coeff + the angle-dependent RPT
// matrix/screen-start each front-end tick (the decomp TitleBG_Scanline_Island
// math; sine/cosine = RSDK::Sin/Cos1024(-angle)>>2 computed engine-side here).
void p6_vdp2_title_island_rbg0_arm(const unsigned short *islandLay, int islandWShift);
void p6_vdp2_title_island_rbg0_frame(int angle, int sine, int cosine);
void p6_vdp2_title_island_map_heal(void); // punch v2 item 1: re-stage B0 map if wiped (chain)
// SUB2 (#276 clouds-coexist): the cloud backdrop on NBG1 reading from bank B1
// (coexists with the RBG0 island; ST-058:1030). arm() copies the cloud cells +
// builds the B1 map ONCE while tilesetPixels is live; config() points NBG1 at B1
// and parks the cloud band each frame (the frame display enables NBG1ON|RBG0ON).
void p6_vdp2_title_clouds_b1_arm(const unsigned char *tilesetPx,
                                 const unsigned short *cloudLay, int cloudWShift);
void p6_vdp2_title_clouds_b1_config(int scroll_x, int scroll_y);
// The arm() sets this to 1 (defined in p6_vdp2.c); p6_frontend_frame gates the
// per-frame rotation call on it. Declare it here so the C++ TU sees the C symbol.
extern "C" int p6_w_title_island_armed;
extern "C" int p6_w_title_clouds_armed; // SUB2: 1 once the B1 clouds are armed
#endif
// P6.7 W16 (Task #228): GHZ FG Low -> NBG1 via the W11a sliding-window
// accessor, scroll registers anchored to screens[0].position.
void p6_vdp2_present_ghz_camera(int layer, int scroll_x, int scroll_y,
                                const unsigned short *pal565,
                                unsigned int *out_pndhash, int *out_nblank);
// A2 (dual-SH2 STEP A, #246): the FG present split across the master frame --
// p6_present_kick() forks p6_present_compute onto the SLAVE (after ProcessObjects),
// the master runs DrawLists in parallel, then p6_present_join_config() joins the
// slave (slCashPurge) + runs the master-only NBG1 register config.
void p6_present_kick(int layer, int sx, int sy, const unsigned short *pal);
void p6_present_join_config(int sx, int sy);
// STEP B (#246): zero the per-frame VDP1 workload accumulators (p6_vdp1.c).
void p6_vdp1_perf_reset(void);
// Boot/load cover: blank all VDP2 scroll+sprite display during the load phase so
// the slow synchronous load shows a clean back-color, not NBG1's half-written VRAM
// (the red/green static). The first GHZ present re-arms NBG1ON|SPRON.
void p6_vdp2_blank(void);
// Cross-flavor: the AIZ/Menu seam re-mirror + reset used by non-CHAIN flavors too;
// the functions live in p6_vdp2.c regardless of the P6_DIRECT_VDP1 command-list path.
extern "C" void p6_vdp2_mirror_apply(void);
extern "C" void p6_vdp2_mirror_reset(void);
#if defined(P6_DIRECT_VDP1)
// #316 F1: the direct VDP1 command list (p6_vdp1.c). begin at frame draw-phase
// start, end after the draw lists; the vblank trampoline (p6_vdp2.c) links the
// published half into SGL's preamble chain.
extern "C" void p6_dl_begin(void);
extern "C" void p6_dl_end(void);
extern "C" void p6_dl_backfill(unsigned short rgb555); /* SEGMENT A #318: Logos black backfill */
#if defined(P6_DIRECT_VDP1)
// F1-R1 race witness: frames where the frame-top re-patch found 0x60 stomped
// back to END (0x8000) by SGL's per-slSynch empty-plan transfer.
__attribute__((used)) int32 p6_w_dl_stomps = 0;
#endif
#endif
#if defined(P6_GHZCUT_BOOT) || defined(P6_AIZ_TEST)
// #302 mechanism-A latch (p6_vdp2.c): 1 once a 4-plane BG frame owns the display
// arm (the FG present then skips its transitional 2-screen slScrAutoDisp). The
// folder-change seams RESET it so the present re-takes ownership across the load
// window (the old BG planes would otherwise display the rewritten VRAM) until the
// destination scene's BG frame re-arms.
extern int p6_vdp2_bg_owns_disp;
#endif
// CP4c BLUE-SCREEN FIX: arm ONLY the VDP1 sprite layer (SPRON) for a non-GHZ UI
// scene -- the Logos splash draws UIPicture VDP1 sprites with no VDP2 FG plane, so
// the GHZ present (which re-arms SPRON) never runs and the sprites stayed dark.
// Flag-gated (CP4c _end-leak FIX): definition (p6_vdp2.c) + its only caller
// (p6_frontend_frame) are both behind P6_FRONTEND_LOGOS; gate the decl to match.
#if defined(P6_FRONTEND_LOGOS)
void p6_vdp2_arm_sprites_only(void);
#endif
// Perf Phase 2c (Task #211): re-arm the present's static-map cache on every GHZ
// (re)load -- the build runs once, then per-frame the present only does the
// hardware scroll (the static map/inflate was ~820ms/frame of waste).
extern "C" int p6_vdp2_present_dirty;
// P6.5b2 (Task #208, qa_p6_sprite.py): engine sprite-animation witnesses.
__attribute__((used)) int32 p6_w_spr_id        = -1; // LoadSpriteAnimation slot
__attribute__((used)) int32 p6_w_spr_animcount = 0;  // expect 5 (Ring.bin)
__attribute__((used)) int32 p6_w_spr_f0xy      = 0;  // (sprX<<16)|sprY of anim0 frame0
__attribute__((used)) int32 p6_w_spr_f0wh      = 0;  // (width<<16)|height
__attribute__((used)) int32 p6_w_spr_f0pv      = 0;  // (pivotX&FFFF)<<16 | pivotY&FFFF
__attribute__((used)) int32 p6_w_spr_f0dur     = 0;  // frame0 duration (model 256)
__attribute__((used)) int32 p6_w_spr_sheethash = 0;  // djb2 over Items.gif surface pixels
__attribute__((used)) int32 p6_w_spr_frame     = 0;  // animator.frameID at capture
__attribute__((used)) int32 p6_w_spr_ticks     = 0;  // ProcessAnimation call count
__attribute__((used)) int32 p6_w_spr_sheetid   = -1; // raw f0->sheetID (0xFF == LoadSpriteSheet
                                                     // returned -1 truncated through the uint8
                                                     // sheetIDs[] at Animation.cpp:39/62)
// P6.5b3 (Task #208, qa_p6_draw.py): engine DrawSprite-slot witnesses.
__attribute__((used)) int32 p6_w_draw_calls   = 0;  // FX_NONE dispatches completed
#if defined(P6_GHZ_AUTORUN)
// Signpost campaign (2026-07-10, diagnostic AUTORUN flavor only): CUMULATIVE
// count of DrawSprite dispatches attributed to SLOT_PLAYER1 (sceneInfo.entity
// == slot 0 during its Draw callback). qa_signpost_run.py takes deltas vs
// p6_w_cont_frames to prove "player never undrawn while alive+on-screen"
// (gate classes 2 and 7 -- disappearing player / incline rotation frames)
// from LIVE MEMORY, no pixels. A pointer compare + increment per draw call
// (counter-cheap, NOSCAN rule respected); flavor-gated so plain/chain
// shipping builds stay byte-identical.
__attribute__((used)) int32 p6_w_plr_draws    = 0;
// Bridge-1 forensics (fall-through at x~1270, run-1 measurement 2026-07-10):
// touch-test counters written by Collision.cpp CheckObjectCollisionTouch
// (bridge classID vs slot 0) + a per-frame bridge-entity liveness scan while
// the player crosses the bridge-1 window (x 800..1500). Discriminates
// "Bridge entity never ran" (streaming/active/cull) from "AABB missed"
// (math/hitbox) -- the two live hypotheses from the tabled #181/#256 chain.
__attribute__((used)) int32 p6_w_btch_calls   = 0;  // bridge-vs-P1 touch tests
__attribute__((used)) int32 p6_w_btch_hits    = 0;  // ... that collided
__attribute__((used)) int32 p6_w_btch_lastdy  = -9999; // last y-sep px
__attribute__((used)) int32 p6_w_btch_lastvy  = 0;  // player vel.y at last test
__attribute__((used)) int32 p6_w_arun_brg_live    = -1; // bridge-1 live this frame?
__attribute__((used)) int32 p6_w_arun_brg_active  = -1; // its ->active
__attribute__((used)) int32 p6_w_arun_brg_firstx  = -1; // player x @ first live sighting
__attribute__((used)) int32 p6_w_arun_brg_gapmiss = 0;  // frames player in-span & bridge NOT live
__attribute__((used)) int32 p6_w_arun_inspan      = 0;  // frames player in-span total
#endif
#if defined(P6_GHZCUT_BOOT)
// #311 mech-6: draws dropped because frame->sheetID wrapped to 255 (unstaged
// sheet at anim load, mech-2 contract) -- each would have OOB-read the handle
// table and sampled GHCOBJ at foreign coords.
__attribute__((used)) int32 p6_w_draw_wrap255 = 0;
#endif
__attribute__((used)) int32 p6_w_draw_xy      = 0;  // last blit top-left (x&FFFF)<<16|(y&FFFF)
__attribute__((used)) int32 p6_w_draw_rect    = 0;  // last blit (sprX<<16)|sprY
__attribute__((used)) int32 p6_w_draw_sheetid = -1; // last blit frame->sheetID
// W18 (Task #227): per-sheetID DROP histogram -- counts DrawSprite blits
// whose surface handle is < 0 (unbound), bucketed by frame->sheetID (0..15).
// Names exactly which entity classes' surfaces are unbound. RED: rings/
// other-classes' buckets nonzero; the fix zeroes the targeted bucket.
__attribute__((used)) int32 p6_w_dropbysheet[16] = { 0 };
// P6.6a (Task #209, qa_p6_sfx.py): engine audio-core witnesses.
__attribute__((used)) int32 p6_w_sfx_inited   = 0;  // AudioDeviceBase::initializedAudioChannels
__attribute__((used)) int32 p6_w_sfx_musbuf   = 0;  // stream-slot buffer alloc'd (DATASET_MUS)
__attribute__((used)) int32 p6_w_sfx_id       = -1; // GetSfx("Global/ScoreAdd.wav") roundtrip
__attribute__((used)) int32 p6_w_sfx_len      = 0;  // sfxList[id].length (model 1469)
__attribute__((used)) int32 p6_w_sfx_hash     = 0;  // djb2 over the F32 buffer bytes
__attribute__((used)) int32 p6_w_sfx_channel  = -1; // PlaySfx return (expect 0)
__attribute__((used)) int32 p6_w_sfx_chstate  = 0;  // (state<<24)|(soundID&0xFFFF)
__attribute__((used)) int32 p6_w_sfx_chspeed  = 0;  // channels[ch].speed (TO_FIXED(1))
__attribute__((used)) int32 p6_w_sfx_chloop   = 0;  // channels[ch].loop (loopPoint 0 -> -1)
// P6.6b (Task #209, qa_p6_scsp.py): SCSP-audible witnesses.
__attribute__((used)) int32 p6_w_snd_plays    = 0;  // engine PlaySfx + backend play count
__attribute__((used)) int32 p6_w_snd_s16hash  = 0;  // djb2 over the F32->S16 device buffer
// p6_snd.c (C TU): DIRECT SCSP slot backend after the Coup reference
// (coup_audio.c:59-316). Upload the S16 buffer to Sound RAM +0x6C000 once,
// then key slots 28-31 (untouched by the SGL driver) per play. NOT jo/SGL
// slPCMOn: measured 2026-06-10, it accepted every play yet moved zero
// sample bytes into SCSP RAM in this build (and Coup documents quality
// degradation through the M68K driver path).
void p6_snd_upload(const void *pcm16, unsigned int bytes);
void p6_snd_play(void);
// P6.6c (Task #209, qa_p6_stream.py): engine PlayStream -> CD-DA witnesses.
__attribute__((used)) int32 p6_w_str_slot  = -2; // PlayStream return
__attribute__((used)) int32 p6_w_str_state = -1; // channels[slot].state after device load
__attribute__((used)) int32 p6_w_str_path  = 0;  // djb2 over streamFilePath (engine sprintf)
__attribute__((used)) int32 p6_w_str_track = -1; // resolved CD-DA track
// P6.7c (Task #210, qa_p6_stagecfg.py): engine LoadGameConfig/LoadSceneFolder
// scene-driven class-registration witnesses. All expectations derive LIVE in
// the gate from the original 1.03 data files.
__attribute__((used)) int32 p6_w_cfg_titlehash   = 0;  // djb2 over gameVerInfo.gameTitle
__attribute__((used)) int32 p6_w_cfg_globalcount = -1; // globalObjectCount after LoadGameConfig
__attribute__((used)) int32 p6_w_cfg_ringgid     = -1; // globalObjectIDs[2] (engine hash-matched Ring)
__attribute__((used)) int32 p6_w_cfg_catcount    = -1; // sceneInfo.categoryCount
__attribute__((used)) int32 p6_w_cfg_startpos    = -1; // engine-set listPos (cat0 offset + startScene)
__attribute__((used)) int32 p6_w_cfg_titlepos    = -1; // harness-discovered Title listPos
__attribute__((used)) int32 p6_w_cfg_classcount0 = -1; // sceneInfo.classCount after LoadSceneFolder
__attribute__((used)) int32 p6_w_cfg_classcount  = -1; // after the witnessed harness Ring append
__attribute__((used)) int32 p6_w_sfx_skips       = -1; // Saturn LoadSfxToSlot alloc-guard skips
__attribute__((used)) int32 p6_w_til_cmhash      = 0;  // djb2 over collisionMasks window (0x20000 B)
__attribute__((used)) int32 p6_w_til_tihash      = 0;  // djb2 over tileInfo window (0x2800 B)
__attribute__((used)) int32 p6_w_pal_hash        = 0;  // djb2 over fullPalette[0] (512 B BE image)
__attribute__((used)) int32 p6_w_createslot      = -1; // sceneInfo.createSlot after InitObjects (TEMPENTITY_START)
// P6.7 W11b (Task #226, qa_p6_ghzlive.py): GHZ1-at-scale witnesses, filled
// by the GHZ pass (step 3a-ghz) BEFORE the Title pass overwrites the lists.
__attribute__((used)) int32 p6_w_ghz_stage    = 0;   // 1 == GHZ1 found + selected in the engine scene list
__attribute__((used)) int32 p6_w_ghz_entcount = -1;  // scene slots with classID != 0 (== Ring census, see gate)
__attribute__((used)) int32 p6_w_ghz_binds    = -1;  // SaturnLayout binds during the GHZ load (expect 2)
__attribute__((used)) int32 p6_w_ghz_clamps   = -1;  // sum of every W11b scale-safety counter (expect 0)
__attribute__((used)) int32 p6_w_ghz_probes[16 * 3] = { 0 }; // {slot, pos.x, pos.y} x 16
__attribute__((used)) int32 p6_w_ghz_tiles[8]       = { 0 }; // GetTile through the engine seam
// P6.7 Player wave (Task #227, qa_p6_player P2-P6): filled by the GAME-side
// p6_player_witness_pre/_post (p6_wave1_reg.c -- the only TU that sees
// Game.h's ObjectPlayer). pre = after LoadSceneAssets, BEFORE InitObjects
// (Player_LoadSprites CopyEntity's the scene Player into SLOT_PLAYER1 and
// memsets the scene slot, Player.c:781-815 -- the scene-slot evidence must
// be captured first); post = after InitObjects (StageLoad ran).
__attribute__((used)) int32 p6_w_plr_classid    = -1; // Player stage classID (Scene.cpp:237 domain)
__attribute__((used)) int32 p6_w_plr_stageload  = -1; // Player->active==ACTIVE_ALWAYS && playerCount>0 (Player.c:708,:726)
__attribute__((used)) int32 p6_w_plr_slot       = -1; // first scene Player slot (raw scene index, model domain)
__attribute__((used)) int32 p6_w_plr_x          = 0;  // spawn position from the scene slot (fixed-point)
__attribute__((used)) int32 p6_w_plr_y          = 0;
__attribute__((used)) int32 p6_w_plr_entclass   = -1; // entity->classID at that slot
__attribute__((used)) int32 p6_w_plr_staticsize = 0;  // sizeof(ObjectPlayer) on SH-2 (pack contract)
__attribute__((used)) int32 p6_w_plr_sonicframes = -1; // Player->sonicFrames after StageLoad (0xFFFF = anim refused)
// W14: first engine gameplay ticks at GHZ (filled around the 2-tick block
// + by the game-side p6_player_witness_tick)
__attribute__((used)) int32 p6_w_plr_ticks      = 0;  // engine tick pairs run at GHZ
__attribute__((used)) int32 p6_w_plr_slotdelta  = -1; // NEW VDP1 slot-cache rects during the ticks
__attribute__((used)) int32 p6_w_plr_tick_x     = 0;  // SLOT_PLAYER1 position after the ticks
__attribute__((used)) int32 p6_w_plr_tick_y     = 0;
// #256 FIX witness: count of phantom scene-placed Player entities purged post-load
// (a 2nd GHZ1 Player marker survives Player_LoadSprites' consumption as a stray
// sidekick at a scene slot -> double-runs Player_Input_P2_Delay -> corrupts the
// shared follow buffers -> Tails can't settle). >0 == the bug existed + was fixed.
__attribute__((used)) int32 p6_w_phantom_purged = 0;
// #256 FIX helper (called post-InitObjects from every gameplay scene load): complete
// Player_LoadSprites' scene-marker consumption. Real Mania's foreach_all(Player) in
// Player_LoadSprites (Player.c:779) CopyEntity(clearSrc)'s the chosen spawn marker
// into SLOT_PLAYER1 and destroyEntity's the rest -> exactly 2 live Players. A 2nd
// GHZ1 marker survives on Saturn as a stray sidekick at a scene slot (slot!=0 ->
// Player_Create sets sidekick + Player_Input_P2_AI), double-running
// Player_Input_P2_Delay -> clobbers the SHARED follow buffers -> Tails can't settle
// (#181). Players belong ONLY at reserve SLOT_PLAYER1/2; classID=0 marks the stray
// slot empty (ProcessObjects' `if (classID)` skips it; loop2 excludes it from
// typeGroups) with NO class-size memset (no narrow-slot overrun). Idempotent.
extern "C" void p6_scan_index_build(void); // P6.8 I3c: defined near the boot, called from the scene-load setup below
extern "C" void p6_scan_update_near(int32 cam_x_world); // I3b 2b: seed the near-set for the spawn camera at load
void p6_purge_scene_players(void)
{
    int32 pcls = (int32)RSDK_ENTITY_AT(0)->classID; // SLOT_PLAYER1 (==0); live Player classID
    if (pcls <= 0)
        return;
    for (int32 s = RESERVE_ENTITY_COUNT; s < ENTITY_COUNT; ++s) {
        EntityBase *pe = RSDK_ENTITY_AT(s);
        if ((int32)pe->classID == pcls) {
            pe->classID     = 0;     // empty slot -> skipped by ProcessObjects + typeGroups
            pe->interaction = false;
            ++p6_w_phantom_purged;
        }
    }
}
// P6.8 I2 (camera-local pool) self-check witness: 1 iff EVERY slot in
// [0,ENTITY_COUNT) resolves through the new SaturnSlotToPoolSlot indirection
// (RSDK_ENTITY_AT) to the SAME address as the pre-I2 direct dual-stride formula
// (p6_i2_direct below). Proves the I2 refactor is byte-identical (gate
// tools/_portspike/qa_p6_i2.py). I3 keeps this witness to re-gate the remap table.
__attribute__((used)) int32 p6_w_i2_resolve_ok = 0;
// P6.8 I2 verification oracle: an INDEPENDENT reimplementation (this TU) of the
// pre-I2 dual-stride mapping using the RAW slot (NO indirection). Mirrors
// SaturnEntityAt's three regions verbatim via the same #define strides + the same
// objectEntityList base -> if SaturnEntityAt(slot) diverges from this for any slot,
// the routing changed the address. Uses macro NAMES (not literals) so the active
// Saturn strides (RESERVE 0x40, SCENEENTITY 0x440, WIDE 556) flow through.
static EntityBase *p6_i2_direct(int32 slot)
{
    uint8 *base = (uint8 *)objectEntityList;
    // P6_POOL_NARROW_STRIDE = sizeof(EntityBase) (dual-stride scene region).
#if defined(P6_FRONTEND_MENU)
    // Task #298: mirror SaturnEntityAt's wide-scene sub-pool routing so p6_i2_selfcheck
    // keeps resolve_ok==1 (slot == pool slot for the menu -- identity remap). 0 == narrow
    // (.bss default); a wide record stores (index+1) -- MUST match SaturnEntityAt's encoding.
    if (slot >= RESERVE_ENTITY_COUNT && slot < TEMPENTITY_START && p6_widescene_map[slot - RESERVE_ENTITY_COUNT] != 0)
        return (EntityBase *)(base + P6_WIDESCENE_OFF
                              + (uint32)(p6_widescene_map[slot - RESERVE_ENTITY_COUNT] - 1) * P6_WIDESCENE_SIZE);
#endif
    if (slot < RESERVE_ENTITY_COUNT)
        return (EntityBase *)(base + (uint32)slot * ENTITY_WIDE_SIZE);
    if (slot < TEMPENTITY_START)
        return (EntityBase *)(base + (uint32)RESERVE_ENTITY_COUNT * ENTITY_WIDE_SIZE
                              + (uint32)(slot - RESERVE_ENTITY_COUNT) * P6_POOL_NARROW_STRIDE);
    return (EntityBase *)(base + (uint32)RESERVE_ENTITY_COUNT * ENTITY_WIDE_SIZE + (uint32)SCENEENTITY_COUNT * P6_POOL_NARROW_STRIDE
                          + (uint32)(slot - TEMPENTITY_START) * ENTITY_WIDE_SIZE);
}
// P6.8 I2 load-time self-check: walk every slot once (1216 slots, NOT per-frame --
// negligible cost) and latch resolve_ok=1 iff the routed SaturnEntityAt matches the
// direct oracle for ALL slots. Called post-InitObjects beside p6_purge_scene_players.
// NOTE (I3, 2026-06-19): the round-trip + non-identity bijection proof that I3 needs
// (SaturnEntitySlot is the exact inverse, and survives a non-identity remap) is a STATIC
// property of the address arithmetic -- it is proven OFFLINE by tools/_portspike/qa_p6_i3.py
// (parses the live pool #defines, replicates SaturnEntityAt/SaturnEntitySlot). A runtime
// self-check is NOT added here: WRAM-H has only ~64 B headroom under P6_HW_ANIMPAK
// (0x060B6600); a ~576 B runtime check pushed _end to 0x060B6800 and the anim-pack load
// clobbered .bss -> #228 boot trap (master PC 0x06000956, blue screen). Re-confirm at
// runtime only AFTER the pool shrink frees WRAM-H.
void p6_i2_selfcheck(void)
{
    int32 ok = 1;
    for (int32 s = 0; s < ENTITY_COUNT; ++s) {
        if ((void *)RSDK_ENTITY_AT(s) != (void *)p6_i2_direct(s)) { ok = 0; break; }
    }
    p6_w_i2_resolve_ok = ok;
}
__attribute__((used)) int32 p6_w_plr_state      = 0;  // state fn ptr (nonzero == state machine live)
__attribute__((used)) int32 p6_w_plr_onground   = -1;
__attribute__((used)) int32 p6_w_plr_animframes = 0;  // animator.frames (expect inside P6_HW_ANIMPAK)
__attribute__((used)) int32 p6_w_plr_animid     = -1; // animator.animationID
/* 2026-07-12 stall diagnostic (autorun-gated, written each GHZ frame): the live
 * player physics state at the X~14400 signpost blocker. High gvel + pinned posx ==
 * a collision/plane WALL (real blocker); ~0 gvel == the autorun input stopped. */
__attribute__((used)) int32 p6_w_pdiag_posx  = 0;   // player position.x >> 16 (px)
__attribute__((used)) int32 p6_w_pdiag_gvel  = 0;   // groundVel (fixed .16)
__attribute__((used)) int32 p6_w_pdiag_velx  = 0;   // velocity.x (fixed .16)
__attribute__((used)) int32 p6_w_pdiag_gnd   = -1;  // onGround (0/1)
__attribute__((used)) int32 p6_w_pdiag_plane = -1;  // collisionPlane (0=A,1=B)
__attribute__((used)) int32 p6_w_pdiag_mode  = -1;  // collisionMode (0=floor,1=Lwall,2=roof,3=Rwall)
__attribute__((used)) int32 p6_w_pdiag_dir   = -1;  // direction (0=R,1=L)
__attribute__((used)) int32 p6_w_pdiag_ang   = -1;  // angle (ground angle)
__attribute__((used)) int32 p6_w_plr_drawdelta  = -1; // DrawSprite calls during the ticks
__attribute__((used)) int32 p6_w_plr_drawflags  = -1; // (drawGroup<<16)|(visible<<8)|onScreen after the ticks
#if defined(P6_FRONTEND_TITLE)
// #313 race witnesses: slave mailbox consumer-vs-producer lag observed at the
// frame-callback entry (immediately after jo's slSynch). Nonzero = the slave
// had NOT consumed the frame's commands when slSynch reset the producer.
// MEASURED (A/B-2 build, _tab2.mcs frame 240): lag_frames=1305, lag_max=336
// = 12 cmds x 28 B = the WHOLE frame's sprite set unconsumed at reset time.
__attribute__((used)) int32 p6_w_slave_lag_last   = 0;
__attribute__((used)) int32 p6_w_slave_lag_frames = 0;
__attribute__((used)) int32 p6_w_slave_lag_max    = 0;
// #313 FIX witness: spins spent draining the mailbox at frame end (max latch).
__attribute__((used)) int32 p6_w_slave_drain_max  = 0;
__attribute__((used)) int32 p6_w_slave_drain_to   = 0; // timeouts (drain gave up)
// #313 RECOVERY witness: slInitSprite re-arms fired on detected dead pipeline.
__attribute__((used)) int32 p6_w_sgl_reinit       = 0;
#endif
__attribute__((used)) int32 p6_w_bind_count     = 0;  // successful VDP1 binds in the GHZ pre-tick loop
__attribute__((used)) int32 p6_w_bind_log[8]    = { 0 }; // (surfaceId<<8)|(handle&0xFF) per bind attempt
__attribute__((used)) int32 p6_w_bind_logn      = 0;
// BADNIK-VIS: shipping arm_env bind-loop instrumentation (the burst bind_log never
// runs in shipping). bind_demand = surfaces that consumed a VDP1 bind slot (vs the
// P6_VDP1_NSHEETS table size); bind_log16[k] = (surfaceID<<16)|(wasPix<<8)|(handle&0xFF).
__attribute__((used)) int32 p6_w_bind_demand    = 0;
__attribute__((used)) int32 p6_w_bind_log16[16] = { 0 };
// W18 (Task #227, qa_p6_entdraw.py): the SURFACE CENSUS taken right after the
// GHZ pre-tick bind loop -- proves WHICH surfaces exist + their bind state.
// surfcensus[i] = (scope<<24)|(hasPixels<<16)|((shtSlot&0xFF)<<8)|(handle&0xFF)
// for surface i; surfpop = count of scope!=NONE surfaces. The Items/ring
// surface is the one with shtSlot>=0,pixels==0 -- RED if its handle == 0xFF.
__attribute__((used)) int32 p6_w_surfcensus[16] = { 0 };
__attribute__((used)) int32 p6_w_surfpop        = 0;
// W18: the Items.gif surface index after the GHZ load (LoadSpriteSheet hash
// resolve) and its bound handle. RED: itemsurf < 0 (never loaded). GREEN:
// itemsurf >= 0 AND itemshandle >= 0 (banded slot bound).
__attribute__((used)) int32 p6_w_itemsurf       = -1;
__attribute__((used)) int32 p6_w_itemshandle    = -2;
// W19 (Task #227, qa_p6_entdraw.py E6): the GHZ HUD/Shield sheet-band closer.
// Display.gif (HUD digits, 960 drops/run) + Shields.gif (59 drops/run) are
// staged as DISPLAY.SHT/SHIELDS.SHT into VDP2 and sheet-only loaded here so
// the bind loop resolves their banded slots. disp*/shld* mirror the Items
// witnesses: surf = LoadSpriteSheet surface id, sheet = resolved
// saturnSheetSlot (>=0 GREEN), handle = bound VDP1 handle (>=0 GREEN).
// Tails1.gif (120 drops) is the DECLARED W19 GAP: its 58,643 B band store
// overflows the 245,760 B VDP2 window by 19,105 B (MEASURED), so it is NOT
// staged -- a funded plan (reclaim NBG1 cell tiles or relocate the window)
// is required. handle_drops therefore falls 1139 -> ~120 (the Tails1 residual).
__attribute__((used)) int32 p6_w_dispsurf       = -1;
__attribute__((used)) int32 p6_w_dispsheet      = -2;
__attribute__((used)) int32 p6_w_disphandle     = -2;
__attribute__((used)) int32 p6_w_shldsurf       = -1;
__attribute__((used)) int32 p6_w_shldsheet      = -2;
__attribute__((used)) int32 p6_w_shldhandle     = -2;
// W18 diag: the first hash word of surfaces 0/5/8 (the dropping classes) so
// the gate can name them offline against the candidate sprite-path MD5s.
__attribute__((used)) int32 p6_w_surfhash[16]   = { 0 };
// W18 FIX (Task #227): GHZ Ring sprite-load + ring-arm witnesses.
// ringspr = the Items.gif surface id from the GHZ sheet-only LoadSpriteSheet;
// ringsheet = its resolved saturnSheetSlot (the staged ITEMS.SHT, >=0 GREEN);
// ringsheethandle = its bound VDP1 handle (>=0 GREEN -- the ring SURFACE binds).
// ringsarmed = count of GHZ scene Ring entities armed with anim frames.
// DECLARED GAP (stays 0): the rings are NOT armed here -- arming needs
// Ring.bin's animation resident, which overflows the DATASET_STG pool
// (p6_saturn_anim_allocfail 0->1, also regresses qa_p6_player P7). The rings
// therefore issue ZERO blits in this pass (Ring_Create NULL -> no frames);
// "ring blits land" is blocked on that separate STG-residency budget item.
__attribute__((used)) int32 p6_w_ringspr        = -1;
__attribute__((used)) int32 p6_w_ringsheet      = -1;
__attribute__((used)) int32 p6_w_ringsheethandle = -2;
__attribute__((used)) int32 p6_w_ringsarmed     = 0;
// #W18 ring-arm (Game_Ring overlay): aniFrames>=0 == Ring_StageLoad's
// LoadSpriteAnimation("Global/Ring.bin") succeeded -> placed rings can arm + render;
// ring_classid live == Ring registered + instantiated. Written by p6_ghz_ovl_witness.
__attribute__((used)) int32 p6_w_ring_aniframes = -2; // Ring->aniFrames (-1=load failed, >=0=armed)
__attribute__((used)) int32 p6_w_ring_classid   = 0;  // Ring->classID (live)
__attribute__((used)) int32 p6_w_spikes_aniframes = -2; // Spikes->aniFrames (-1=load failed, >=0=armed)
__attribute__((used)) int32 p6_w_b1_registered = 0; // mass-port Batch 1: count of the 4 clean objs with classID>0 (Decoration/ForceSpin/ForceUnstick/SpinBooster; target 4 since ForceUnstick landed 9a0792b)
__attribute__((used)) int32 p6_w_corkscrew_classid = 0; // CorkscrewPath (GHZ loop-de-loop path) register gate: 0/-1 RED, classID>0 GREEN
// Water M1 (docs/feature_checklists/water.md): classid = register gate (0/-1 RED, >0 GREEN);
// water_level = Water_StageLoad ran (0x7FFFFFFF seed) then the real GHZ water Y once a WATERLEVEL entity Creates.
__attribute__((used)) int32 p6_w_water_classid = 0;
__attribute__((used)) int32 p6_w_water_level   = 0;
// M1b: WATER.SHT stage slot (>=0 = band-store slot claimed -> surface can bind) + Water->aniFrames
// (>=0 = Water.bin anim loaded + Global/Water.gif sheet resolved -> the surface WILL draw when on-screen).
__attribute__((used)) int32 p6_w_water_shtslot   = -2;
__attribute__((used)) int32 p6_w_water_aniframes = -2;
// M1b staging diagnostics: distinguish GFS-load-fail (loadrc<=0) vs Stage-fail (shtslot==-3)
// vs FRD-fail (frdrc<0). shtslot: -2 block never ran, -1 FindSlot only, -3 Stage failed, >=0 OK.
__attribute__((used)) int32 p6_w_water_loadrc    = -2;
__attribute__((used)) int32 p6_w_water_frdrc     = -2;
// M1b Stage-fail localizer: band cursor + remaining bytes + slot count AT the
// WATER.SHT stage attempt. Overflow => (bandcur + loadrc) > bandend; slot-full
// => slotcnt >= SATURNSHEET_SLOTS. Diagnostic only (throwaway, removed post-fix).
__attribute__((used)) int32 p6_w_water_bandcur   = -2;
__attribute__((used)) int32 p6_w_water_bandend   = -2;
__attribute__((used)) int32 p6_w_water_slotcnt   = -2;
__attribute__((used)) int32 p6_w_b2_registered = 0; // mass-port Batch 2 (badnik break chain): count of the 9 chain+badnik objs with classID>0 (target 9)
// Per-object classID latch (diagnostic for the b2 count): index order =
// {BadnikHelpers,Explosion,Animals,Newtron,Crabmeat,BuzzBomber,Chopper,Motobug,Batbrain}.
__attribute__((used)) int32 p6_w_b2_cids[9] = {0,0,0,0,0,0,0,0,0};
// DDWrecker GHZ1 boss classID witness (2026-07-11, qa_ddwrecker D1). PACK global
// so it lands in game.map for the live gate; the P6_DDWRECKER overlay writes it
// via -R (p6_ovl_ghz.c), the same pattern as p6_w_b2_cids. 0 = never written
// (unregistered), -1 = registered-but-object-NULL, >0 = live classID.
__attribute__((used)) int32 p6_w_ddw_classid = 0;
// DDWrecker arena-warp diagnostic witnesses (2026-07-11): warp_fired sentinel +
// live boss-entity count (p6_w_ddw_seen; set by the arena scan below when
// P6_DDW_ARENA). The warp pin releases once the boss has assembled (seen>=2).
__attribute__((used)) int32 p6_w_ddw_warp_fired = 0;
__attribute__((used)) int32 p6_w_ddw_seen       = 0;
__attribute__((used)) int32 p6_w_ddw_state0      = 0; // slot-317 (boss) state ptr
__attribute__((used)) int32 p6_w_ddw_health_min  = -1; // min BALL health seen
__attribute__((used)) int32 p6_w_ddw_hits_injected = 0; // P6_DDW_KILL: DDWrecker_Hit injections fired
__attribute__((used)) int32 p6_w_ddw_sign_live      = 0; // P6_DDW_KILL: a live SignPost near the arena gates the kill
// Batch 3 (2026-07-09, GHZ gameplay-parity sweep): per-object registration +
// range-independent anim-load witnesses (written by p6_ghz_ovl_witness, ld -R).
// classid>0 == registered + StageLoad ran; aniframes in [0,0x400) == the object's
// LoadSpriteAnimation succeeded ((int16)-cast so 0xFFFF reads -1 == FAILED).
__attribute__((used)) int32 p6_w_itembox_classid    = 0;  // ItemBox->classID
__attribute__((used)) int32 p6_w_itembox_aniframes  = -2; // ItemBox->aniFrames (Global/ItemBox.bin)
__attribute__((used)) int32 p6_w_debris_classid     = 0;  // Debris->classID (StageLoad loads nothing)
__attribute__((used)) int32 p6_w_invstars_classid   = 0;  // InvincibleStars->classID
__attribute__((used)) int32 p6_w_scorebonus_classid   = 0;  // ScoreBonus->classID (combo-score popup; 0=unregistered)
__attribute__((used)) int32 p6_w_scorebonus_aniframes = -2; // ScoreBonus->aniFrames (Global/ScoreBonus.bin; -1=load failed)
__attribute__((used)) int32 p6_w_dust_classid         = 0;  // Dust->classID (spindash/skid/land puffs; 0=unregistered -> pack CREATE_ENTITY NULL-deref)
__attribute__((used)) int32 p6_w_dust_aniframes       = -2; // Dust->aniFrames (Global/Dust.bin -> Explosions.gif=EXPLOS.SHT; -1=load failed)
__attribute__((used)) int32 p6_w_shield_classid       = 0;  // Shield->classID (shield bubbles; 0=unregistered -> pack Shield->classID/sfxInstaShield NULL-deref)
__attribute__((used)) int32 p6_w_shield_aniframes     = -2; // Shield->aniFrames (Global/Shields.bin -> Shields.gif=SHIELDS.SHT; -1=load failed)
__attribute__((used)) int32 p6_w_boundsmarker_classid = 0;  // BoundsMarker->classID (22 camera/death-bound markers; 0=unregistered -> camera+death bounds wrong)
__attribute__((used)) int32 p6_w_breakwall_classid = 0;     // BreakableWall->classID (23 breakable walls/floors; 0=unregistered)
__attribute__((used)) int32 p6_w_cplat_classid     = 0;     // CollapsingPlatform->classID (15 GHZ1 collapsing ledges; 0=unregistered -> ground never breaks)
__attribute__((used)) int32 p6_w_splats_classid    = 0;     // Splats->classID (GHZ manifest badnik; 0 authored GHZ1/GHZ2 placements -- manifest/DebugMode closure; 0=unregistered)
__attribute__((used)) int32 p6_w_splats_aniframes  = -2;    // Splats->aniFrames (GHZ/Splats.bin, sheet GHZ/Objects.gif=GHZOBJ.SHT; -1=load failed)
__attribute__((used)) int32 p6_w_starpost_classid   = 0;    // StarPost->classID (4 GHZ1 + 7 GHZ2 checkpoints; 0=unregistered -> no lampposts, death respawns at act start)
__attribute__((used)) int32 p6_w_starpost_aniframes = -2;   // StarPost->aniFrames (Global/StarPost.bin, sheet Global/Objects.gif=GLOBJ.SHT; -1=load failed)
__attribute__((used)) int32 p6_w_fxfade_classid     = 0;    // FXFade->classID (AIZ slot-2 placed fade-from-black + GHZCutscene manifest; 0=unregistered -> abrupt seams)
// R1 discriminator (2026-07-17, gate qa_starpost_fxfade_gates.py G3): live FXFade
// draw-time state, latched by the p6_fxfade_draw shim (zero pool scans). If the
// menu settles BLACK: timer>0 here == the fade IS the wash (state machine stuck;
// chase MenuSetup_InitAPI/FXFade_Update); timer==0 (or draws not advancing) ==
// the black is NOT the FXFade wash -- look at the seam-stomp class instead.
__attribute__((used)) int32 p6_w_fxfade_timer = -1; // self->timer at the last FXFade draw (-1 = never drew)
__attribute__((used)) int32 p6_w_fxfade_draws = 0;  // total FXFade draw-shim invocations
__attribute__((used)) int32 p6_w_platform_classid   = 0;  // Platform->classID (Batch 3 step 2)
__attribute__((used)) int32 p6_w_platform_aniframes = -2; // Platform->aniFrames (GHZ/Platform.bin)
__attribute__((used)) int32 p6_w_invblock_classid   = 0;  // InvisibleBlock->classID (Batch 3 step 3)
__attribute__((used)) int32 p6_w_batbrain_aniframes = -2; // Batbrain->aniFrames (GHZ/Batbrain.bin, cart pack)
__attribute__((used)) int32 p6_w_explosion_aniframes = -2; // Explosion->aniFrames (-1=load failed, >=0=armed; chain TU)
__attribute__((used)) int32 p6_w_animals_aniframes   = -2; // Animals->aniFrames   (-1=load failed, >=0=armed; chain TU)
__attribute__((used)) int32 p6_w_newtron_aniframes   = -2; // Newtron->aniFrames   (per-badnik load-status latch)
// #258b: pack->overlay forward pointers for the hurt-ring-scatter path. The
// verbatim pack-side Player calls Ring_LoseRings on hurt -> binds to the pack
// STUB; the closure-edge forward routes it here to the overlay's REAL impl
// (set after the overlay entry runs). 0 until then (stub no-ops, as before).
extern "C" void *p6_ovl_loserings_raw = 0;
extern "C" void *p6_ovl_losehyperrings_raw = 0;
// Batch 3: pack->overlay forward pointers for the ItemBox/Debris ports (see
// p6_closure_edge.c -- SaveGame assigns ItemBox_State_Broken, Shield assigns
// Debris_State_Move; both bind to pack stubs that forward through these).
// StarPost port (2026-07-17): pack->overlay forward for StarPost_ResetStarPosts
// (ActClear.c:766/790 callers bind to the p6_closure_edge stub; the stub forwards
// through this once the overlay entry runs -- the #258b pattern).
extern "C" void *p6_ovl_starpost_reset_raw = 0;
extern "C" void *p6_ovl_itembox_break_raw = 0;
extern "C" void *p6_ovl_itembox_state_broken_raw = 0;
extern "C" void *p6_ovl_itembox_state_falling_raw = 0;
extern "C" void *p6_ovl_itembox_state_idle_raw = 0;
extern "C" void *p6_ovl_debris_state_move_raw = 0;
// BATCH 2 (badnik break chain): pack->overlay forward pointers for the badnik-break
// path. Game_Player.o's Player_CheckBadnikBreak calls BadnikHelpers_BadnikBreak-
// Unseeded -> binds to the p6_closure_edge STUB, which forwards here to the overlay's
// REAL impl (BadnikHelpers/Explosion/Animals are overlay-resident -- Animals refs the
// overlay's Bridge_HandleCollisions). 0 until the overlay entry runs (then the break
// path is reachable only during gameplay, well after, so the stub never no-ops live).
extern "C" void *p6_ovl_badnikbreak_unseeded_raw = 0;
extern "C" void *p6_ovl_badnikbreak_raw = 0;
__attribute__((used)) int32 p6_w_plr_sheetid_t  = -1; // Player frame's sheetID after the ticks (GetFrame, stride-safe)
__attribute__((used)) int32 p6_w_plr_handle     = -2; // p6_vdp1HandleBySurface[that sheetID]
// P6.8 Step A (Task #211): CONTINUOUS-tick witnesses. The burst is a fixed
// 60-iteration in-run loop; these are incremented/snapshotted ONLY inside
// p6_ghz_frame() (the per-jo-frame path), so p6_w_cont_frames > 60 can only
// arise from the continuous tick -- the burst-vs-continuous discriminator.
__attribute__((used)) int32 p6_ghz_continuous_armed = 0; // 1 == p6_scene_tick drives GHZ
__attribute__((used)) int32 p6_w_legacy_frames  = 0;  // legacy-Ring tick frames before the GHZ switch
__attribute__((used)) int32 p6_w_cont_frames    = 0;  // ++ per p6_ghz_frame() call
__attribute__((used)) int32 p6_w_cont_plr_x     = 0;  // SLOT_PLAYER1 position, continuous
__attribute__((used)) int32 p6_w_cont_plr_y     = 0;
__attribute__((used)) int32 p6_w_cont_animid    = -1; // SLOT_PLAYER1 animator.animationID
// CP4 FRONT-END KEYSTONE (Task #265/#266, qa_engine_logos.py E1-E4): prove the
// engine LoadScene + run path serves a NON-GHZ UI scene (the Logos splash) via
// the SAME chain that runs GHZ -- behind -DP6_FRONTEND_LOGOS so the default
// shipping build still boots GHZ. frontend_folder_tag = a 2-char tag ('L'<<8|'o')
// of the loaded scene's folder, set in p6_logos_reload where the GHZ select
// otherwise runs (E1: != 0 == a non-GHZ folder is the active scene). logosetup/
// uipicture_classid = the overlay-registered classIDs (written by the overlay
// witness via -R; E2/E3 > 0 == the port linked + resolved). logos_objcount = the
// live-classID scene-entity census after InitObjects for the Logos scene
// (E4: > 0 == the 4 UIPicture placements instantiated through the generic chain).
__attribute__((used)) int32 p6_w_frontend_folder_tag = 0;
__attribute__((used)) int32 p6_w_logosetup_classid   = 0;
__attribute__((used)) int32 p6_w_uipicture_classid   = 0;
__attribute__((used)) int32 p6_w_logos_objcount      = 0;
// CP5a FRONT-END link 2 (Task #267, qa_engine_title.py T1-T5): the TITLE scene.
// Mirrors the CP4 Logos witnesses -- the engine LoadScene + run path now serves the
// Title scene (TitleSetup + TitleLogo ports) through the SAME generic chain GHZ +
// Logos use, behind -DP6_FRONTEND_TITLE (which also defines P6_FRONTEND_LOGOS so the
// shared frontend_frame/VDP1-box/arm-sprites machinery compiles unchanged).
// frontend_folder_tag is REUSED (set to 0x5469 'Ti' in p6_title_reload; T1).
// titlesetup/titlelogo_classid = the overlay-registered classIDs (written by the
// overlay witness via -R; T2/T3 > 0 == ports linked + resolved). title_objcount =
// the live-classID scene-entity census after InitObjects for the Title scene (T4 > 0
// == the TitleLogo Scene1.bin placements instantiated through the generic chain).
// cont_frames is REUSED (T5).
// GUARDED under P6_FRONTEND_TITLE (NOT unconditional like the CP4 Logos witnesses):
// the default GHZ _end (0x060B6BA0) has only 96 B of margin to ANIMPAK (0x060B6C00),
// so 12 B of unconditional .bss would breach the exact-equality regression contract.
// The gate evaluates the TITLE-flavor map (where these are defined + -u rooted); the
// default GHZ map stays byte-identical. The -u roots in build_p6scene_objs.sh are
// harmless no-ops when the symbols are absent (default build).
#if defined(P6_FRONTEND_TITLE)
__attribute__((used)) int32 p6_w_titlesetup_classid  = 0;
__attribute__((used)) int32 p6_w_titlelogo_classid   = 0;
__attribute__((used)) int32 p6_w_title_objcount      = 0;
#endif
// M1 FRONT-END link 3 (qa_engine_menu.py M1-M5): the MENU scene. Mirrors the CP5a
// Title witnesses EXACTLY -- the engine LoadScene + run path now serves the Menu
// scene (the verbatim non-Plus MenuSetup + the min UI set) through the SAME generic
// chain GHZ/Logos/Title use, behind -DP6_FRONTEND_MENU (which also defines
// P6_FRONTEND_TITLE -> LOGOS so the shared frontend_frame/VDP1-box/arm-sprites
// machinery compiles unchanged). frontend_folder_tag is REUSED (set to 0x4D65 'Me'
// in p6_menu_reload; M1). menusetup/uicontrol_classid = the overlay-registered
// classIDs (written by the overlay witness via -R; M2/M3 > 0 == ports linked +
// resolved). menu_objcount = the live-classID scene-entity census after InitObjects
// for the Menu scene (M4 > 0 == the Menu Scene1.bin placements instantiated through
// the generic chain). cont_frames is REUSED (M5). GUARDED under P6_FRONTEND_MENU
// (additive-only; the default GHZ/Title maps stay byte-identical -- the -u roots in
// build_p6scene_objs.sh are harmless no-ops when the symbols are absent).
#if defined(P6_FRONTEND_MENU)
__attribute__((used)) int32 p6_w_menusetup_classid   = 0;
__attribute__((used)) int32 p6_w_uicontrol_classid   = 0;
__attribute__((used)) int32 p6_w_menu_objcount       = 0;
// Saturn-native 320 menu X-compression factor (8-bit fixed: 205/256 = 0.80). The DrawSprite
// world->screen transform squeezes X toward screen-centre 160 by this so the 424-authored
// 2-column menu fits the 320 screen. MEASURED-derived (see the DrawSprite use): f<=0.896
// fits; 0.80 leaves margin. Tunable by the qa_menu_layout RED gate.
#define P6_MENU_XSQUEEZE 205
// M1b (qa_engine_menu_render.py): the RENDER half of the Menu checkpoint.
//   M6 menu_treebuilt: MenuSetup->mainMenu != NULL  -- the auth gate flipped so
//     MenuSetup_InitAPI returned true (offline no-save path) -> MenuSetup_Initialize
//     ran -> the foreach_all(UIControl){match "Main Menu"} wired mainMenu (and every
//     other UIControl). 0 while InitAPI stays in its pre-handshake branch (the M1a
//     RED), 1 once the tree is built. Written by the overlay witness each tick (-R).
//   M6b menu_modebtn_classid: UIModeButton registered + classID resolved (>0) -- the
//     4 main-menu rows (Mania Mode/Time Attack/Competition/Options) need this class
//     live so foreach_all(UIModeButton,...) at SetupActions:507 instantiates+draws.
//   M7 the menu emits VDP1 sprite commands: latch of the global p6_w_vdp1_landed at
//     the witness tick (>0 == at least one UI sprite reached a bound VDP1 slot ==
//     the rows/text are blitting; the M1a RED was landed==0 == black screen).
__attribute__((used)) int32 p6_w_menu_treebuilt      = 0;
__attribute__((used)) int32 p6_w_menu_modebtn_classid = 0;
__attribute__((used)) int32 p6_w_menu_vdp1_landed    = 0;
// M2a LAYOUT witnesses (qa_menu_layout.py): WHY do the menu rows scatter?
//   p6_w_menu_scrx/scry = the LIVE engine scroll origin currentScreen->position (px).
//     The decomp transform is screen = world - ScreenInfo->position; ScreenInfo IS
//     currentScreen (Game ScreenInfo = &screens[0], p6_wave1_link). UIControl_Draw
//     (UIControl.c:52-53) sets it to FROM_FIXED(activeControl->position) - center, so
//     this == (activeMainMenu.position>>16) - (160,112) IFF the active main-menu
//     UIControl is the LAST UIControl whose Draw ran. A wrong/stale value == scatter.
//   p6_w_menu_modebtn_px[i]/py[i] = the 4 UIModeButton entities' world position>>16.
//   p6_w_menu_modebtn_sx[i]/sy[i] = those mapped to screen (world - scrollOrigin) ==
//     the on-screen centroid the gate compares to the measured PNG centroid.
//   p6_w_menu_modebtn_act[i] = each row's `active` (ACTIVE_NEVER=0 row would not draw).
//   p6_w_menu_visctrls = count of UIControl entities with visible==true. The PC main
//     menu has exactly ONE visible UIControl (the active one); >1 == inactive menus
//     leaking their content onto the screen (the scatter hypothesis).
//   p6_w_menu_actctrl_px/py = the ACTIVE (active==ACTIVE_ALWAYS) UIControl position>>16.
//   p6_w_menu_actctrl_tagh = djb2 of the active control's tag (identify which menu).
__attribute__((used)) int32 p6_w_menu_scrx = -999999, p6_w_menu_scry = -999999;
__attribute__((used)) int32 p6_w_menu_modebtn_px[4] = { -999999, -999999, -999999, -999999 };
__attribute__((used)) int32 p6_w_menu_modebtn_py[4] = { -999999, -999999, -999999, -999999 };
__attribute__((used)) int32 p6_w_menu_modebtn_sx[4] = { -999999, -999999, -999999, -999999 };
__attribute__((used)) int32 p6_w_menu_modebtn_sy[4] = { -999999, -999999, -999999, -999999 };
__attribute__((used)) int32 p6_w_menu_modebtn_act[4] = { -9, -9, -9, -9 };
__attribute__((used)) int32 p6_w_menu_modebtn_bid[4] = { -9, -9, -9, -9 };
__attribute__((used)) int32 p6_w_menu_visctrls    = -9;
__attribute__((used)) int32 p6_w_menu_actctrl_px  = -999999;
__attribute__((used)) int32 p6_w_menu_actctrl_py  = -999999;
__attribute__((used)) int32 p6_w_menu_actctrl_tagh = 0;
__attribute__((used)) int32 p6_w_menu_ctrl_count   = -9; // total UIControl entities
// M2a: per-visible-control identity (up to 4) -- to identify the 2nd leaking control.
__attribute__((used)) int32 p6_w_menu_vis_tagh[4] = { 0, 0, 0, 0 };
__attribute__((used)) int32 p6_w_menu_vis_px[4]   = { -999999, -999999, -999999, -999999 };
__attribute__((used)) int32 p6_w_menu_vis_py[4]   = { -999999, -999999, -999999, -999999 };
__attribute__((used)) int32 p6_w_menu_vis_act[4]  = { -9, -9, -9, -9 };
// M2b FIX hand-off: the overlay's witness writes the ACTIVE main-menu control's
// scroll origin (position>>16 - center) here every tick; the pack reads it back
// just before ProcessObjectDrawLists and FORCES currentScreen->position to it.
// This makes the world->screen transform DETERMINISTIC for the whole frame --
// independent of (a) whether the verbatim UIControl_Draw ran, (b) draw order, and
// (c) a stray 2nd visible control re-writing ScreenInfo->position late in the frame
// (the MEASURED cause of the scattered/clipped rows: post-draw currentScreen->
// position latched (0,0) while the active control sat at (852,376) i.e. origin
// (692,264)). Mirrors the decomp UIControl_Draw formula (UIControl.c:52-53) applied
// to the GUARANTEED-correct active control. -999999 sentinel == not yet computed
// (pack skips the force-set until the overlay has found the active control). */
__attribute__((used)) int32 p6_w_menu_force_scrx = -999999;
__attribute__((used)) int32 p6_w_menu_force_scry = -999999;
// M2a tap point: the pack sets these from currentScreen at the witness tick (the
// overlay fills the entity ones). currentScreen lives in THIS TU.
__attribute__((used)) void p6_menu_layout_scroll_latch(void)
{
    if (currentScreen) {
        p6_w_menu_scrx = currentScreen->position.x;
        p6_w_menu_scry = currentScreen->position.y;
    }
    // GC-ROOT the array + entity witnesses from the PACK: they are WRITTEN only by
    // the overlay (p6_ghz_ovl_witness), so without a pack-side reference the gc-pack
    // (-r, -u p6_engine_boot_and_run) drops them before game.elf -> the overlay's
    // -R import then fails to link (undefined reference). A self-touch keeps them
    // defined in game.elf so the overlay binds them. (volatile defeats the no-op
    // optimization without changing the value.)
    {
        volatile int32 *p;
        p = p6_w_menu_modebtn_px; p[0] = p[0];
        p = p6_w_menu_modebtn_py; p[0] = p[0];
        p = p6_w_menu_modebtn_sx; p[0] = p[0];
        p = p6_w_menu_modebtn_sy; p[0] = p[0];
        p = p6_w_menu_modebtn_act; p[0] = p[0];
        p = p6_w_menu_modebtn_bid; p[0] = p[0];
        // SCALARS: a plain `x = x` is LTO-dropped (the array volatile-ptr trick
        // above is what keeps those). Use the same volatile-pointer self-touch so
        // the scalar witnesses are gc-rooted in the pack too.
        { volatile int32 *q;
          q = &p6_w_menu_visctrls;     *q = *q;
          q = &p6_w_menu_ctrl_count;   *q = *q;
          q = &p6_w_menu_actctrl_px;   *q = *q;
          q = &p6_w_menu_actctrl_py;   *q = *q;
          q = &p6_w_menu_actctrl_tagh; *q = *q;
          q = &p6_w_menu_force_scrx;   *q = *q;
          q = &p6_w_menu_force_scry;   *q = *q;
        }
        p = p6_w_menu_vis_tagh; p[0] = p[0];
        p = p6_w_menu_vis_px;   p[0] = p[0];
        p = p6_w_menu_vis_py;   p[0] = p[0];
        p = p6_w_menu_vis_act;  p[0] = p[0];
    }
}
// =============================================================================
// M2 (qa_engine_menu_start.py): the START-GAME path witnesses.
//   S1 p6_w_menu_saveslot_classid : UISaveSlot registered + classID resolved (>0).
//      The save-select slot widget is ported (Game_UISaveSlot.o). Written by the
//      overlay witness (needs the Mania Game.h UISaveSlot type).
//   S2 p6_w_menu_input_seen : a STICKY OR of UIControl->any{Confirm,Down,Up,Left,
//      Right}Press observed across the run -- proves the Saturn pad reaches the live
//      UIControl's input read (UIControl_ProcessInputs reads ControllerInfo). Also
//      written by the overlay witness (UIControl is the overlay-registered class).
//      A NON-ZERO value == an injected press was sampled by the menu tick.
//   S3 p6_w_menu_startscene_tag / p6_w_menu_start_cat : latched by the PACK in the
//      p6_frontend_frame ENGINESTATE_LOAD branch when MenuSetup_SaveSlot_ActionCB's
//      RSDK.SetScene("Cutscenes","Angel Island Zone") fires. SetScene (Scene.cpp:
//      1530-1553) sets sceneInfo.activeCategory + sceneInfo.listPos by md5 match;
//      the AIZ scene's folder is "AIZ" (GameConfig verified) -> tag 'A'<<8|'I' =
//      0x4149. start_cat = sceneInfo.activeCategory at the latch (the Cutscenes idx).
//      sticky: once the AIZ tag is seen it is NOT overwritten by a later GHZ load.
__attribute__((used)) int32 p6_w_menu_saveslot_classid = 0;
__attribute__((used)) int32 p6_w_menu_input_seen       = 0;
// S3 diag (Task #296): localize the Mania Mode -> Save Select UITransition stall.
// present: 1 = UITransition->activeTransition live; 0 = obj's activeTransition NULL (Create
//   never ran); -2 = UITransition obj NULL. state: the entity->state fn ptr (map to
//   UITransition_State_{Init,TransitionIn,TransitionOut} via ovl_ring.map). istrans/timer:
//   is the state machine ticking + how far. active: the entity's RSDK active byte.
__attribute__((used)) int32 p6_w_uitrans_present = -1;
__attribute__((used)) int32 p6_w_uitrans_state   = 0;
__attribute__((used)) int32 p6_w_uitrans_timer   = -1;
__attribute__((used)) int32 p6_w_uitrans_istrans = -1;
__attribute__((used)) int32 p6_w_uitrans_active  = -1;
// S3 root-cause (Task #296): the active UIControl button's actionCB ptr. 0 == NULL == the
// stall (UIButton_ProcessButtonCB_Scroll:397 hasNoAction=!actionCB -> only buttonEnterCB,
// never selectedCB -> StartTransition). Non-0 -> maps to MenuSetup_MenuButton_ActionCB in
// ovl_ring.map (so the stall is elsewhere). id/count = the active control's buttonID/buttonCount.
__attribute__((used)) int32 p6_w_active_btn_actioncb = -1;
__attribute__((used)) int32 p6_w_active_btn_id       = -99;
__attribute__((used)) int32 p6_w_active_btn_count    = -1;
// S3 root-cause test (Task #296): UISaveSlot_ProcessButtonCB:19 gates the confirm on
// `control->position.x == control->targetPos.x` (the control must be SETTLED). If the No-Save
// control is perpetually un-settled (posx != tgtx), every confirm is silently ignored -> no
// SelectedCB -> no fxRadius ramp -> no SetScene AIZ. Both are 16.16 fixed (>>16 for pixels).
__attribute__((used)) int32 p6_w_ctrl_posx = -123456;
__attribute__((used)) int32 p6_w_ctrl_tgtx = -123456;
// S3 dispatch triangulation (Task #296): localize WHY the No-Save confirm never reaches
// UISaveSlot_SelectedCB despite the control being settled + active + actionCB wired. The
// active control's state must be UIControl_ProcessInputs for ProcessButtonCB to run + see
// anyConfirmPress. ctrl_state = snapshot of uc->state (cmp UIControl_ProcessInputs offline).
// nosave_pi/nosave_confirm = sticky latches over the whole run.
__attribute__((used)) int32 p6_w_ctrl_state     = -123456;
__attribute__((used)) int32 p6_w_ctrl_active    = -123456;
__attribute__((used)) int32 p6_w_nosave_pi      = 0;   // sticky: 1-button control reached ProcessInputs
__attribute__((used)) int32 p6_w_nosave_confirm = 0;   // sticky: anyConfirmPress while it was in ProcessInputs
// S3 downstream (Task #296): confirm reaches ProcessButtonCB but SelectedCB body never runs.
// nosave_gate = sticky EXACT SelectedCB-call condition (confirm && posx==tgtx on a No-Save PI
// frame) -- distinguishes settle-gate-fails-on-confirm-frame from SelectedCB-downstream. slot_state
// + slot_fxradius = the active UISaveSlot's own state/ramp (did it reach State_Selected?).
__attribute__((used)) int32 p6_w_nosave_gate    = 0;
__attribute__((used)) int32 p6_w_slot_state     = -123456;
__attribute__((used)) int32 p6_w_slot_fxradius  = -123456;
// S3 ROOT (Task #296): UIControl_ProcessButtonInput (-> UISaveSlot_ProcessButtonCB -> confirm)
// is gated by `if (!self->selectionDisabled)` (UIControl.c:262). gate_seldisabled = the
// control's selectionDisabled AT the gate+confirm frame -- if 1, the dispatch is SKIPPED
// (confirm never reaches ProcessButtonCB) => ROOT CAUSE. ctrl_seldisabled = snapshot at capture.
__attribute__((used)) int32 p6_w_gate_seldisabled = -123456;
__attribute__((used)) int32 p6_w_ctrl_seldisabled = -123456;
// #296 debug-inject progress: 0=waiting, 1=MatchMenuTag fired (No-Save active), 2=actionCB fired.
__attribute__((used)) int32 p6_w_as_stage = -123456;
__attribute__((used)) int32 p6_w_menu_startscene_tag   = 0;      // folder tag at the start-game SetScene
__attribute__((used)) int32 p6_w_menu_start_cat        = -1;     // sceneInfo.activeCategory at that load
__attribute__((used)) int32 p6_w_menu_start_listpos    = -1;     // sceneInfo.listPos at that load (diag)
#if defined(P6_AIZ_TEST)
// M3.0 (qa_p6_aiz_scene): the AIZ intro-cutscene scene load+render witnesses. Written
// by p6_aiz_reload + the p6_isAIZ arm branch in p6_scene_load_and_arm (NOT the overlay),
// so __attribute__((used)) + a -u root in build_p6scene_objs.sh keeps them (no gc-root
// fn needed). Behind -DP6_AIZ_TEST: a P6_FRONTEND_MENU diagnostic build that boots
// straight to AIZ, decoupling the load+render from the menu confirm (the same pattern
// F.1/F.2 used with P6_TRANSITION_TEST).
__attribute__((used)) int32 p6_w_aiz_loaded   = 0;  // A1: currentSceneFolder=="AIZ" after load+arm
__attribute__((used)) int32 p6_w_aiz_fg_hash  = 0;  // A2: djb2 of the uploaded AIZ FG cells (!=0 == content)
__attribute__((used)) int32 p6_w_aiz_objcount = 0;  // A3: sceneInfo.classCount after the AIZ load
#if defined(P6_GHZCUT_BOOT)
// Task #309 Tier-B.1: the FXRuby fade the overlay read off the live entity this
// frame, packed (fadeWhite<<16)|(fadeBlack&0xFFFF). The engine hands fadeWhite/
// fadeBlack to p6_vdp2_fade_apply (the VDP2 Color Offset write). Gate
// qa_ghzcut_fade.py reads this + the CLOFEN/COAR registers.
__attribute__((used)) int32 p6_w_ghzcut_fade = 0;
extern "C" void p6_vdp2_fade_apply(int fadeWhite, int fadeBlack); // p6_vdp2.c
// Task #309 Tier-B.2 (the 5 Heavies render): the HBHOBJ.SHT staging + HBHOBJ.PAK
// anim-load + Heavy-region VDP1 blit witnesses. Gate qa_ghzcut_heavies.py reads
// these + the 5 CRAM palette blocks (CRAM[512..1663]).
//   hbh_slot      = the SaturnSheet slot of the staged HBHOBJ.SHT (>=0 == staged+
//                   hashed; -9 == NOT staged == the RED pre-fix state).
//   hbh_aniframes = the live CutsceneHBH->aniFrames (>=0 == LoadSpriteAnimation
//                   resolved a Heavy .bin from HBHOBJ.PAK; -1 == pack missing). The
//                   overlay writes it (CutsceneHBH is overlay-resident).
//   hbh_landed    = count of Heavy-region VDP1 blits this frame (overlay-written in
//                   the CutsceneHBH Draw shim: ++ per DrawSprite reached).
__attribute__((used)) int32 p6_w_hbh_slot      = -9;
__attribute__((used)) int32 p6_w_hbh_aniframes = -2;
__attribute__((used)) int32 p6_w_hbh_landed    = 0;
// Tier-B.2 DIAG: why don't the Heavies draw? The overlay witness scans
// foreach_all(CutsceneHBH) each tick. count = # live Heavy entities; the first
// Heavy's visible/onScreen/active + posY (px) + handle for "Cutscene/HBH.gif".
// cam/plr Y localise whether the Heavies are off-screen above the camera.
__attribute__((used)) int32 p6_w_hbh_count     = -1;
__attribute__((used)) int32 p6_w_hbh_vis       = -1; // (visible<<16)|(onScreen<<8)|active
__attribute__((used)) int32 p6_w_hbh_posy      = -1; // first Heavy world Y (px)
__attribute__((used)) int32 p6_w_hbh_posx      = -1; // first Heavy world X (px)
__attribute__((used)) int32 p6_w_hbh_handle    = -9; // VDP1 handle of Cutscene/HBH.gif surface
__attribute__((used)) int32 p6_w_hbh_camy      = -1; // camera/screen Y (px)
__attribute__((used)) int32 p6_w_hbh_animid    = -9; // first Heavy mainAnimator.animationID
// Task #309 caveat #2a (cutscene PLAYERS render): the PLROBJ.SHT staging + player
// anim-resolve + palette witnesses (gate qa_ghzcut_players.py reads these + CRAM[1792]).
//   plrsht_slot   = the SaturnSheet slot of the staged PLROBJ.SHT (>=0 == staged+hashed;
//                   -9 == NOT staged == the RED pre-fix state). Set in the staging block.
//   plr_cut_anif  = SLOT_PLAYER1 (Sonic) animator.frames != NULL (1 == Sonic.bin resolved
//                   from HBHOBJ.PAK + a frame loaded). Written by the overlay witness.
//   plr_cut_anif2 = SLOT_PLAYER2 (Tails) animator.frames != NULL (-2 == P2 absent).
//   plr_cut_aniid = SLOT_PLAYER1 animator.animationID (25 == ANI_FAN held).
//   plr_cut_surf  = the gfxSurface idx for "Cutscene/Players.gif" (>=0 == loaded).
//   plr_cut_handle= the bound VDP1 handle for that surface (>=0 == bound).
// p6_plr_sheet_slot (DEFINED in p6_vdp1.c) is the surface-route selector; this file
// sets it to the staged slot so the p6_vdp1.c blit routes the player sheet to CRAM[1792].
__attribute__((used)) int32 p6_w_plrsht_slot   = -9;
// Task #311: the GHCOBJ/RUBYOBJ sheet stages (the 2 GHZCutscene scene-object
// sheets). sn = load_to_lwram return (<=0 == GFS fail); slot = staged slot.
__attribute__((used)) int32 p6_w_ghcobj_sn     = -9;
__attribute__((used)) int32 p6_w_ghcobj_slot   = -9;
__attribute__((used)) int32 p6_w_rubyobj_sn    = -9;
__attribute__((used)) int32 p6_w_rubyobj_slot  = -9;
// Task #312 (#311b): the HUD + placed-Ring sheet stages (same pattern).
__attribute__((used)) int32 p6_w_itemsht_sn    = -9;
__attribute__((used)) int32 p6_w_itemsht_slot  = -9;
__attribute__((used)) int32 p6_w_dispsht_sn    = -9;
__attribute__((used)) int32 p6_w_dispsht_slot  = -9;
__attribute__((used)) int32 p6_w_ghcobj_relink  = -9; // surface id re-linked to the slot
__attribute__((used)) int32 p6_w_rubyobj_relink = -9;
__attribute__((used)) int32 p6_w_ghc_fetch1     = -9; // djb2 of chain-rect FetchRect #1
__attribute__((used)) int32 p6_w_ghc_fetch2     = -9; // djb2 of chain-rect FetchRect #2
__attribute__((used)) int32 p6_w_plr_cut_anif  = -2;
__attribute__((used)) int32 p6_w_plr_cut_anif2 = -2;
__attribute__((used)) int32 p6_w_plr_cut_aniid = -9;
__attribute__((used)) int32 p6_w_plr_cut_surf  = -9;
__attribute__((used)) int32 p6_w_plr_cut_handle = -9;
extern "C" int p6_plr_sheet_slot;                       // p6_vdp1.c (surface-route selector)
extern "C" void p6_vdp2_player_pal_upload(const unsigned short *palData); // p6_vdp2.c
extern "C" void p6_vdp2_titlecard_pal_upload(const unsigned short *palData); // p6_vdp2.c (GL1 glyph pal -> CRAM block 2)
#endif
__attribute__((used)) int32 p6_w_aiz_nlayers  = 0;  // diag: non-null tileLayer count (FG-Low index for the M3.0b present)
__attribute__((used)) int32 p6_w_aiz_bg_filebytes = -1; // R2.1: AIZBG.CHR GFS bytes (62720 expected)
__attribute__((used)) int32 p6_w_aiz_fglow_idx = -1; // M3.0c: the index p6_fglow_layer_index() resolved for the present
__attribute__((used)) int32 p6_w_aiz_slots    = -1; // M3.0c: packed tileLayers[0..5].saturnSlot (nibble per layer)
__attribute__((used)) int32 p6_w_aiz_scrx     = 0;  // M3.0c: screens[0].position.x passed to the present (the scroll)
__attribute__((used)) int32 p6_w_aiz_scry     = 0;  // M3.0c: screens[0].position.y
// M3.1 (qa_p6_aiz_cutscene): the AIZ intro-cutscene DRIVER witnesses. The class-id
// witnesses are written by the overlay's combined witness (p6_ghz_ovl_witness, called
// per-tick via s_ovl.witness_fn) -- the AIZ object globals are overlay-resident, so the
// scan lives there; these are pack globals the overlay writes via the ld -R import.
//   cutscene_state = -1 until CutsceneSeq is instantiated, then the live seq->stateID
//                    (0 == EnterAIZ running). C1: the cutscene state machine ran.
//   setup/seq/tornado/path_classid = the registered+instantiated class IDs. C3.
//   cam_x          = SLOT_CAMERA1's live cam->position.x (the cutscene-driven camera).
__attribute__((used)) int32 p6_w_aiz_cutscene_state  = -1;
__attribute__((used)) int32 p6_w_aiz_setup_classid   = 0;
__attribute__((used)) int32 p6_w_aiz_seq_classid     = 0;
__attribute__((used)) int32 p6_w_aiz_tornado_classid = 0;
__attribute__((used)) int32 p6_w_aiz_path_classid    = 0;
__attribute__((used)) int32 p6_w_aiz_cam_x           = 0;
// R3.1 (Task #305, qa_p6_aiz_tornado.py): the AIZ Tornado sheet-bind witnesses (kept
// MINIMAL -- the front-end build is WRAM-H-tight; a full surface-scan diag pushed _end
// past the heap limit -> #228 boot trap, MEASURED). aizobj_slot = the SaturnSheet slot
// returned by p6_stage_sheet_hash("AIZOBJ.SHT") (>=0 == AIZOBJ.SHT staged+hashed).
// tornado_frames = the live AIZTornado animatorTornado.frameCount (the overlay writes
// it, like tornado_classid; >0 == SetSpriteAnimation resolved the sheet). The actual
// VDP1 bind / render is verified by the SCREENSHOT pixel measure (the binding proof).
__attribute__((used)) int32 p6_w_aizobj_slot         = -9;
__attribute__((used)) int32 p6_w_aiz_tornado_frames  = -1;
// R3.1 (#305) DIAG: AIZTornado->aniFrames (OBJECT-level, set by AIZTornado_StageLoad's
// LoadSpriteAnimation("AIZ/AIZTornado.bin")). -1/0xFFFF == load FAILED or StageLoad
// never ran; >=0 == the anim list loaded (then SetSpriteAnimation/the census is the
// frameCount=0 culprit). animID = the live tornado animatorTornado.animationID (-1 ==
// SetSpriteAnimation never ran on it). torn_count = # live AIZTornado entities.
__attribute__((used)) int32 p6_w_aiz_torn_aniframes  = -2;
__attribute__((used)) int32 p6_w_aiz_torn_animid     = -2;
__attribute__((used)) int32 p6_w_aiz_torn_count      = -2;
__attribute__((used)) int32 p6_w_aiz_sonicsht_slot   = -9; // #321: SaturnSheet slot of Players/Sonic1.gif at AIZ (>=0 = staged -> wing renders)
__attribute__((used)) int32 p6_w_explos_slot         = -9; // BADNIK-VIS: EXPLOS.SHT staged slot at the GHZ handoff (>=0 = explosions render)
__attribute__((used)) int32 p6_w_animals_slot        = -9; // BADNIK-VIS: ANIMALS.SHT staged slot at the GHZ handoff (>=0 = freed-critter renders)
// R3.4 (#306 follow-on) AIZ cutscene actor anim-load latches: AIZKingClaw->aniFrames
// (AIZ/Claw.bin, beat 4 EnterClaw) + AIZEggRobo->aniFrames (AIZ/AIZEggRobo.bin, the
// Heavies). -1/0xFFFF == LoadSpriteAnimation FAILED (.bin not in AIZOBJ.PAK -- the
// pre-R3.4 state, identical to the Tornado before R3.2); >=0 == the anim list loaded.
__attribute__((used)) int32 p6_w_aiz_claw_aniframes    = -2;
__attribute__((used)) int32 p6_w_aiz_eggrobo_aniframes = -2;
// #308 AIZ beat-3 (P2FlyIn) STALL diag: the cutscene advances ONLY when player2(Tails)->
// onGround (AIZSetup.c:425-450); MEASURED stuck at beat 3 (cam frozen). These localise WHY
// Tails(SLOT_PLAYER2) isn't landing: classid 0=not spawned vs >0=a Player; onground=the
// stall condition; posx/posy px (near Sonic+on-floor, in-air, or fell BELOW the level);
// vely 16.16 (falling+); sidekick=1 if P2_AI active. p1_posy = Sonic's y (the ground ref --
// Sonic DID land, the cutscene reached beat 3 via his run). Distinguishes the #256 phantom-
// sidekick "can't settle" class from a fall-through (#180) or a not-spawned/respawn gap.
__attribute__((used)) int32 p6_w_aiz_p2_classid  = -2;
__attribute__((used)) int32 p6_w_aiz_p2_onground = -2;
__attribute__((used)) int32 p6_w_aiz_p2_posx     = -999999;
__attribute__((used)) int32 p6_w_aiz_p2_posy     = -999999;
__attribute__((used)) int32 p6_w_aiz_p2_vely     = -999999;
__attribute__((used)) int32 p6_w_aiz_p2_sidekick = -2;
__attribute__((used)) int32 p6_w_aiz_p1_posy     = -999999;
// #308 phase-2: the EXACT Tails state. stateptr/inputptr = the raw function pointers
// (look up in game.map: AIZSetup_PlayerState_Static / _P2Enter / Player_State_HoldRespawn /
// Player_Input_P2_AI / _P2_Delay) -> identifies WHICH state/input is running (no guessing).
// tilecoll = TILECOLLISION mode (NONE==1 set by HandleSidekickRespawn:3755 -> can't land);
// visible (Static sets 0). Together: Static-stuck vs HoldRespawn-stuck vs flying-no-collide.
__attribute__((used)) int32 p6_w_aiz_p2_stateptr = 0;
__attribute__((used)) int32 p6_w_aiz_p2_inputptr = 0;
__attribute__((used)) int32 p6_w_aiz_p2_tilecoll = -2;
__attribute__((used)) int32 p6_w_aiz_p2_visible  = -2;
// #309 beat-7 (RubyAppear) diag+fix: the PhantomRuby flash. ruby_active = the entity active
// mode (4 ACTIVE_BOUNDS = updates only on-screen -> off-screen this beat -> the 38-frame
// flash timer in PhantomRuby_State_PlaySfx never ticks -> flashFinished never set -> stall;
// the fix forces it to 2 ACTIVE_NORMAL when cutscene_state>=7). timer = the flash counter
// (ticks 0->38); flashfin = ruby->flashFinished (0->1 at timer==38 -> beat 7 advances).
__attribute__((used)) int32 p6_w_aiz_ruby_active  = -2;
__attribute__((used)) int32 p6_w_aiz_ruby_timer   = -2;
__attribute__((used)) int32 p6_w_aiz_ruby_flashfin = -2;
// M3.1 DIAG (camera-progression localiser): the live tornado x + the path moveVel.
__attribute__((used)) int32 p6_w_aiz_torn_x       = -1;
__attribute__((used)) int32 p6_w_aiz_torn_dis     = -1;
__attribute__((used)) int32 p6_w_aiz_torn_active  = -1;
__attribute__((used)) int32 p6_w_aiz_torn_state   = -1;
__attribute__((used)) int32 p6_w_aiz_path_movevel = 0;
__attribute__((used)) int32 p6_w_aiz_pn_type   = -99;
__attribute__((used)) int32 p6_w_aiz_pn_tgtspd = -99;
__attribute__((used)) int32 p6_w_aiz_pn_speed  = -99;
__attribute__((used)) int32 p6_w_aiz_pn_state  = -99;
__attribute__((used)) int32 p6_w_aiz_pn_active = -99;
__attribute__((used)) int32 p6_w_aiz_sp_ptr    = 0;
__attribute__((used)) int32 p6_w_aiz_sp_post0  = -99;
// M3.2 slot-trajectory witnesses: the 8 AIZTornadoPath nodes are scene slots 6-13, all
// filter==0xFF (MEASURED -> all survive the REV02 compaction), no wide-scene routing
// (MEASURED map[6]==0). Yet foreach_all sees only 7 (slots 7-13); the type-0 START at
// scene slot 6 (physical RESERVE_ENTITY_COUNT+6) is gone. Scene slot 5 is a Player marker.
// Latch the classID trio (slot5<<16 | slot6<<8 | slot7) at 3 load phases to pin WHEN slot
// 6 is blanked: post-LoadScene/compaction, post-InitObjects (Player_LoadSprites), post-purge.
__attribute__((used)) int32 p6_w_aiz_trio_load  = -1;
__attribute__((used)) int32 p6_w_aiz_trio_init  = -1;
__attribute__((used)) int32 p6_w_aiz_trio_purge = -1;
__attribute__((used)) int32 p6_w_aiz_player_cls = -1; // RSDK_ENTITY_AT(0)->classID after InitObjects
// M3.2 FG-present probe: the AIZ FG renders as a uniform repeating tile. Band store is
// AIZ1LAYT.BIN (mounted, 7695 B), FG-Low is scene/band layer 4 (216 distinct tiles,
// varied). Probe SaturnLayout_GetTile(slot 0 = FG-Low, bound by the present) at 4
// in-window positions to separate "tile read uniform/broken" (binding/window/refill bug)
// from "tile read varied but page renders uniform" (PND/CEL/VDP2 config bug). gt_ctx =
// the camera tile origin so the offline AIZ FG-Low layout can be compared at the exact coords.
__attribute__((used)) int32 p6_w_aiz_gt_ctx = -1; // (ctx<<16)|cty at the probe
__attribute__((used)) int32 p6_w_aiz_gt_a   = -1; // (GetTile(0,ctx+1,cty+2)<<16)|GetTile(0,ctx+8,cty+6)
__attribute__((used)) int32 p6_w_aiz_gt_b   = -1; // (GetTile(0,ctx+15,cty+10)<<16)|GetTile(0,ctx+20,cty+12)
#endif
// PACK gc-root for the two overlay-written witnesses (same pattern as the M2a block
// above -- the overlay's -R import needs them DEFINED in game.elf).
__attribute__((used)) void p6_menu_start_witness_root(void)
{
    volatile int32 *q;
    q = &p6_w_menu_saveslot_classid; *q = *q;
    q = &p6_w_menu_input_seen;       *q = *q;
    q = &p6_w_menu_startscene_tag;   *q = *q;
    q = &p6_w_menu_start_cat;        *q = *q;
    q = &p6_w_menu_start_listpos;    *q = *q;
}
#endif
#if defined(P6_FRONTEND_LOGOS)
// CP4b render diag (front-end only): UIPicture->aniFrames (LoadSpriteAnimation
// ("Logos/Logos.bin") result; -1 == load FAILED == the anim half of the render
// chain is the gap; >= 0 == anim loaded so the gap is downstream (sheet bind /
// palette / position)). + the first live UIPicture entity's animator.frames!=NULL
// (0 == SetSpriteAnimation found no frame table = sprite can't draw).
__attribute__((used)) int32 p6_w_uipicture_aniframes = -2;
__attribute__((used)) int32 p6_w_uipicture_framesNN  = -1;
// CP4c BLUE-SCREEN diag (front-end only, this session): trace EVERY link of the
// first live UIPicture entity's draw chain to localise why the splash logos do
// not blit (uniform-blue capture). Mirrors the BD_SCAN badnik witness pattern
// (p6_ovl_ghz.c:456). Written by the overlay witness each tick (ld -R import).
//   uipic_drawgrp/active/visible/onscreen = the entity render-gating state
//     (decomp UIPicture_Create: ACTIVE_NORMAL=2, visible=1, drawGroup=2).
//   uipic_posx/posy = the entity world position in px (Scene1.bin placement;
//     0,0 or way off-camera == drawn off-screen == the FillScreen clip rejects it).
//   uipic_animid/frameid/sheetid = the resolved frame's sheetID (the surface index
//     DrawSprite indexes p6_vdp1HandleBySurface with). -1 == GetFrame returned NULL.
//   uipic_handle = p6_vdp1_handle_for_surface(sheetID): -1 == the Logos surface is
//     UNBOUND (== the blit drops at p6_vdp1_blit_flipped's handle<0 check == the bug).
__attribute__((used)) int32 p6_w_uipic_drawgrp  = -9;
__attribute__((used)) int32 p6_w_uipic_active   = -9;
__attribute__((used)) int32 p6_w_uipic_visible  = -9;
__attribute__((used)) int32 p6_w_uipic_onscreen = -9;
__attribute__((used)) int32 p6_w_uipic_posx     = -999999;
__attribute__((used)) int32 p6_w_uipic_posy     = -999999;
__attribute__((used)) int32 p6_w_uipic_animid   = -9;
__attribute__((used)) int32 p6_w_uipic_frameid  = -9;
__attribute__((used)) int32 p6_w_uipic_sheetid  = -9;
__attribute__((used)) int32 p6_w_uipic_handle   = -9;
// SURFACE-side truth for the Logos sheet (mirrors the GHZ/Objects.gif scan,
// p6_io_main.cpp:2148-2177): does LOGOS.SHT stage with hash "Logos/Logos.gif", and
// does a gfxSurface resolve to it with a bound VDP1 handle? Written in p6_ghz_arm_env
// (front-end arm). logos_shtslot = SaturnSheet_FindSlot(hash) (>=0 == staged+hashed).
// logos_surfidx = the gfxSurface[] index whose hash matches (-1 == no surface loaded
// the sheet). logos_surfslot = that surface's saturnSheetSlot. logos_surfh0/h0 = the
// surface's stored hash word0 vs the path hash word0 (equal == same path).
__attribute__((used)) int32 p6_w_logos_shtslot   = -9;
__attribute__((used)) int32 p6_w_logos_surfidx   = -9;
__attribute__((used)) int32 p6_w_logos_surfslot  = -9;
__attribute__((used)) int32 p6_w_logos_surfscope = -9;
__attribute__((used)) int32 p6_w_logos_surfh0    = 0;
__attribute__((used)) int32 p6_w_logos_h0        = 0;
#endif
#if defined(P6_FRONTEND_TITLE)
// CP5b.3 (Task #272) BACKDROP witnesses (qa_title_backdrop V2): the VDP2 island/
// cloud present armed. backdrop_done==1 => p6_vdp2_present_title_backdrop ran
// (NBG1 island+cloud map + CRAM + display). backdrop_armed packs (islandWShift<<8
// | cloudWShift) (=0x0604 for island 64-wide shift6 + cloud 16-wide shift4) ==
// the layouts were present + non-NULL. RED baseline: ABSENT (no backdrop code).
__attribute__((used)) int32 p6_w_title_backdrop_done  = 0;
__attribute__((used)) int32 p6_w_title_backdrop_armed = -9;
// CP5b.3 (Task #272) BIND witnesses for the TitleBG sheet (Title/BG.gif -> TBG.SHT):
// the mountains/water/billboard VDP1 sprites' surface bind chain (the TLOGO/TSONIC
// mirror). tbg_shtslot>=0 == TBG.SHT staged+hashed; tbg_surfidx>=0 == TitleBG_
// StageLoad's LoadSpriteAnimation loaded the surface; tbg_handle>=0 == bound.
__attribute__((used)) int32 p6_w_tbg_shtslot  = -9;
__attribute__((used)) int32 p6_w_tbg_surfidx  = -9;
__attribute__((used)) int32 p6_w_tbg_surfslot = -9;
__attribute__((used)) int32 p6_w_tbg_handle   = -9;
__attribute__((used)) int32 p6_w_titlebg_classid = -9; // TitleBG->classID (registered)
__attribute__((used)) int32 p6_w_title3d_classid = -9; // Title3DSprite->classID
// CP5b.4 (Task #272): count of TitleBG/Title3DSprite entities flipped VISIBLE by
// TitleBG_SetupFX (the V2 RED->GREEN signal). 0 while gated off (bed5bac baseline);
// 9 TitleBG + up to 58 Title3DSprite once SetupFX's foreach_all(...){visible=true}
// runs. Written each tick by the overlay witness fn (p6_ghz_ovl_witness, ld -R).
__attribute__((used)) int32 p6_w_titlebg_vis = 0;
__attribute__((used)) int32 p6_w_title3d_vis = 0;
// CP5b.1 (Task #268) RENDER diag: the SONIC-MANIA logo blit chain. Two layers,
// mirroring the CP4b Logos witnesses exactly:
//   (A) SURFACE-side truth for Title/Logo.gif (written in p6_ghz_arm_env, same
//       hash-scan as the Logos block). tlogo_shtslot = SaturnSheet_FindSlot(
//       "Title/Logo.gif") (>=0 == TLOGO.SHT staged+hashed). tlogo_surfidx = the
//       gfxSurface[] index whose hash matches (>=0 == TitleLogo's LoadSpriteAnimation
//       loaded the surface). tlogo_surfslot = that surface's saturnSheetSlot (>=0 ==
//       resolved the banded slot). tlogo_handle = its bound VDP1 handle (>=0 == the
//       bind loop bound it == the blit can land; <0 == the CP5a bug).
//   (B) the first live TitleLogo entity's draw-chain state (written by the overlay
//       witness each tick, ld -R import; mirrors p6_w_uipic_*). visible/onScreen =
//       render-gating; sheetid = the resolved frame's sheetID; handle =
//       p6_vdp1_handle_for_surface(sheetID) (>=0 == bound == GREEN).
//   (C) tlogo_landed = a snapshot of the GLOBAL p6_w_vdp1_landed at the witness tick
//       (Title is sprite-only: electricity ring + logo are the ONLY blits, so
//       landed>0 corroborates the logo reached the framebuffer). The PRIMARY GREEN
//       evidence remains the screenshot + the pixel measure (field gotcha #4).
__attribute__((used)) int32 p6_w_tlogo_shtslot   = -9;
__attribute__((used)) int32 p6_w_tlogo_surfidx   = -9;
__attribute__((used)) int32 p6_w_tlogo_surfslot  = -9;
__attribute__((used)) int32 p6_w_tlogo_surfscope = -9;
__attribute__((used)) int32 p6_w_tlogo_surfh0    = 0;
__attribute__((used)) int32 p6_w_tlogo_h0        = 0;
__attribute__((used)) int32 p6_w_tlogo_drawgrp   = -9;
__attribute__((used)) int32 p6_w_tlogo_visible   = -9;
__attribute__((used)) int32 p6_w_tlogo_onscreen  = -9;
__attribute__((used)) int32 p6_w_tlogo_type      = -9;
__attribute__((used)) int32 p6_w_tlogo_sheetid   = -9;
__attribute__((used)) int32 p6_w_tlogo_handle    = -9;
__attribute__((used)) int32 p6_w_tlogo_landed    = -9;
// CP5b.1 per-TYPE diag: bit T set == a TitleLogo of type T exists. vismask: bit set ==
// that type's entity has visible!=0. onscrmask: visible AND onScreen. boundmask:
// visible AND onScreen AND its frame's surface handle>=0 (== it CAN blit). This
// pinpoints which logo pieces (emblem/ribbon/gametitle/copyright/ringbottom) render vs
// which are gated off. tsetup_state = (TitleSetup->state low byte proxy via a counter)
// -- which state the title is in at the witness tick.
__attribute__((used)) int32 p6_w_tlogo_existmask = 0;
__attribute__((used)) int32 p6_w_tlogo_vismask   = 0;
__attribute__((used)) int32 p6_w_tlogo_onscrmask = 0;
__attribute__((used)) int32 p6_w_tlogo_boundmask = 0;
__attribute__((used)) int32 p6_w_tsetup_statetag = -9;
// CP5b.2 (Task #269) RENDER diag: the SONIC-MANIA ring-center HEAD blit chain, the
// EXACT mirror of the tlogo block above for Title/Sonic.gif (TitleSonic's sheet).
//   (A) SURFACE-side truth (written in p6_ghz_arm_env, same hash-scan as tlogo).
//       tsonic_shtslot = SaturnSheet_FindSlot("Title/Sonic.gif") (>=0 == TSONIC.SHT
//       staged+hashed). tsonic_surfidx = the gfxSurface[] index whose hash matches
//       (>=0 == TitleSonic_StageLoad's LoadSpriteAnimation loaded the surface).
//       tsonic_surfslot = that surface's saturnSheetSlot (>=0 == resolved the banded
//       slot). tsonic_handle = its bound VDP1 handle (>=0 == the bind loop bound it ==
//       the head CAN blit; <0 == the CP5b.1 bug: the head dropped -> black ring hole).
//   (B) the live TitleSonic entity's draw-chain state (written by the overlay witness
//       each tick, ld -R import). visible/onScreen = render-gating (TitleSetup_State_
//       FlashIn flips TitleSonic visible=true); sheetid = the resolved frame's sheetID;
//       handle = p6_vdp1_handle_for_surface(sheetID) (>=0 == bound == GREEN).
__attribute__((used)) int32 p6_w_tsonic_shtslot   = -9;
// title-Sonic chain-overflow diag (2026-07-12): measure the band-store fill at
// TSONIC's stage attempt. bandpre = bytes used before TSONIC; bandcap = store
// capacity; stageret = SaturnSheet_Stage return (>=0 slot, -1 overflow). Confirms
// whether the total genuinely exceeds capacity (needs resize/defer) or just
// fragments (needs re-order).
__attribute__((used)) int32 p6_w_tsonic_bandpre   = -1;
__attribute__((used)) int32 p6_w_tsonic_bandcap   = -1;
__attribute__((used)) int32 p6_w_tsonic_makeres   = -9; /* task #326: MakeResident ret */
__attribute__((used)) int32 p6_w_tsonic_stageret  = -9;
__attribute__((used)) int32 p6_w_tsonic_surfidx   = -9;
__attribute__((used)) int32 p6_w_tsonic_surfslot  = -9;
__attribute__((used)) int32 p6_w_tsonic_surfscope = -9;
__attribute__((used)) int32 p6_w_tsonic_surfh0    = 0;
__attribute__((used)) int32 p6_w_tsonic_h0        = 0;
__attribute__((used)) int32 p6_w_tsonic_visible   = -9;
__attribute__((used)) int32 p6_w_tsonic_onscreen  = -9;
__attribute__((used)) int32 p6_w_tsonic_sheetid   = -9;
__attribute__((used)) int32 p6_w_tsonic_handle    = -9;
__attribute__((used)) int32 p6_w_tsonic_animid    = -9;
__attribute__((used)) int32 p6_w_tsonic_frameid   = -9;
// RING-SONIC render forensic (task #326, RED-first qa_title_ring_sonic.py): the
// visible/handle witnesses above prove ENTITY STATE (visible=1, surface bound)
// but NOT that TitleSonic_Draw actually ran + emitted a VDP1 command that SURVIVED
// (skill gotcha #4: proxy witness). These count the ACTUAL blit. p6_draw_flipped
// sets sonic_curr=1 when the drawing entity is the TitleSonic global; then:
//   tsonic_drawcalls = times p6_draw_flipped ran for TitleSonic (dispatch proof)
//   tsonic_drawclip  = times it CLIP-rejected (returned before validDraw)
//   tsonic_drawhandle= the handle the blit used (== HandleBySurface[sheetID]);
//                      <0 == unbound-drop at the blit
//   tsonic_drawx/y   = the last screen (x,y) top-left the head blit landed at
__attribute__((used)) int32 p6_w_tsonic_drawcalls  = 0;
__attribute__((used)) int32 p6_w_tsonic_drawclip   = 0;
__attribute__((used)) int32 p6_w_tsonic_drawhandle = -9;
__attribute__((used)) int32 p6_w_tsonic_drawx      = -9999;
__attribute__((used)) int32 p6_w_tsonic_drawy      = -9999;
// TitleSonic->classID, published by the overlay diag (it can dereference the
// TitleSonic global; this main image cannot). p6_draw_flipped reads it to key the
// per-blit forensic above.
__attribute__((used)) int32 p6_w_tsonic_classid    = -9;
// EMIT-ORDER forensic (task #326): the emblem (TitleLogo type 0) black disc vs the
// TitleSonic head are both drawGroup 4; on Saturn VDP1 the LAST-emitted command is
// drawn IN FRONT. tsonic_emitseq = p6_w_draw_calls at the FIRST sonic blit of the
// frame (the head). emblem_emitseq = p6_w_draw_calls at the emblem's blit. If
// emblem_emitseq > tsonic_emitseq, the black disc paints OVER the head (root cause).
// TitleLogo->classID published by the overlay (p6_w_tlogo_classid, already exists).
__attribute__((used)) int32 p6_w_tsonic_emitseq    = -9;
__attribute__((used)) int32 p6_w_emblem_emitseq    = -9;
// HEAD blit geometry (first sonic blit of the frame -- NOT the finger which is
// latched into drawx/y). If (headx,heady)+(headw,headh) does NOT cover the ring
// hole (screen ~x[146..178] y[50..82]), the head is being drawn OFF the hole.
__attribute__((used)) int32 p6_w_tsonic_headx      = -9999;
__attribute__((used)) int32 p6_w_tsonic_heady      = -9999;
__attribute__((used)) int32 p6_w_tsonic_headw      = -9;
__attribute__((used)) int32 p6_w_tsonic_headh      = -9;
// max emit-seq reached this frame (last VDP1 blit) -- anything with emitseq >
// tsonic_emitseq that overlaps the hole draws OVER the head.
__attribute__((used)) int32 p6_w_frame_maxemit     = -9;
#endif
// #181 GHZ Bridge witnesses (set by the game-side p6_brg_witness(),
// p6_wave1_reg.c -- only that TU has the Bridge class type/global). classid>0 +
// count>0 is the RED->GREEN gate (Bridge ported + registered + instantiated).
// posx/posy feed the P6_WARP_BRIDGE pin; frames is the first bridge's
// animator.frames (0/-1 == Bridge.bin alloc-failed the #247 residency budget).
__attribute__((used)) int32 p6_w_brg_classid    = 0;  // Bridge->classID (0 == unregistered == the bug)
// #326 (signpost campaign): count of LOAD-PATH p6_scan_index_build runs (the
// pre-shrink full-pool census at p6_scene load/reload). The chain frame's
// settled-index latch compares this against its build-#1 snapshot to decide
// whether a frame-side rebuild is needed (see the #325/#326 block) -- a
// post-shrink rebuild orphans every dormant entity (MEASURED idx 968 -> 32).
__attribute__((used)) int32 p6_scan_loadbuild_seq = 0;
__attribute__((used)) int32 p6_w_brg_count      = 0;  // # Bridge entities the scene instantiated
// #254 GHZ1 loop closure (qa_p6_loop.py): regmask 0x1F == all 5 loop classes
// registered; pscount == # PlaneSwitch placed in GHZ1 (expect 106; 0 == broken).
__attribute__((used)) int32 p6_w_loop_regmask   = 0;
__attribute__((used)) int32 p6_w_loop_pscount   = 0;
__attribute__((used)) int32 p6_w_brg_posx       = 0;  // first bridge position.x (16.16 fixed)
__attribute__((used)) int32 p6_w_brg_posy       = 0;  // first bridge position.y (16.16 fixed)
__attribute__((used)) int32 p6_w_brg_onscreen   = -1; // first bridge onScreen flag
__attribute__((used)) int32 p6_w_brg_frames     = -1; // first bridge animator.frames ptr (sprite loaded?)
// #254 anim-pool funding (qa_p6_animpool.py): stg_limit = live DATASET_STG
// storageLimit (92 KB unfunded -> 150 KB with P6_CART_TMP, set in Storage.cpp
// InitStorage). spring_classid/frames = the SPRING canary (mirror p6_brg_witness):
// proves the FIRST object registered after funding loads its anim (frames>0) AND
// the bridge still loads (R2) -- the whole-level RED->GREEN for the funding.
__attribute__((used)) int32 p6_w_stg_limit      = 0;  // DATASET_STG storageLimit bytes
__attribute__((used)) int32 p6_w_spring_classid = 0;  // Spring->classID (0 == not registered)
__attribute__((used)) int32 p6_w_spring_frames  = -1; // first Spring animator.frames (0 == STG starved == regression)
// O3 step 1: SpikeLog overlay-object witnesses (written by p6_ghz_ovl_witness via -R).
__attribute__((used)) int32 p6_w_spikelog_classid = 0;  // SpikeLog->classID (0 == not registered)
__attribute__((used)) int32 p6_w_spikelog_frames  = -1; // first SpikeLog animator.frames (anim loaded?)
// RANGE-INDEPENDENT anim-load status: <Obj>->aniFrames = LoadSpriteAnimation() result
// read straight off the Object struct (NOT a live in-range entity, which foreach_all
// only sees near the camera). -1/0xFFFF == the StageLoad anim load FAILED. This is the
// definitive per-object "did the anim load" witness -- the careless-proof gate signal.
__attribute__((used)) int32 p6_w_spikelog_aniframes = -2; // SpikeLog->aniFrames (-1=load failed, >=0=slot)
__attribute__((used)) int32 p6_w_spring_aniframes   = -2; // Spring->aniFrames
__attribute__((used)) int32 p6_w_brg_aniframes      = -2; // Bridge->aniFrames
// #P0 GHZ1-parity spawn-state witnesses. newgame_pre_* = Player->rings/powerups
// captured RIGHT AFTER LoadSceneAssets, BEFORE the reset = the uninitialized static
// the lean boot inherits (THE BUG: 100 rings / fire shield). live_* = the
// SLOT_PLAYER1 player's rings/shield post-spawn (THE FIX: 0 / SHIELD_NONE).
// time_enabled + timer witness the scene clock (Zone_Create sets timeEnabled;
// ProcessSceneTimer ticks it -- was never called in p6_ghz_frame -> frozen 0'00"00).
__attribute__((used)) int32 p6_w_plr_newgame_pre_rings = -1;
__attribute__((used)) int32 p6_w_plr_newgame_pre_pwr   = -1;
__attribute__((used)) int32 p6_w_plr_live_rings        = -1;
__attribute__((used)) int32 p6_w_plr_live_shield       = -1;
__attribute__((used)) int32 p6_w_time_enabled          = -1;
__attribute__((used)) int32 p6_w_timer                 = -1;
// #181 sheet-bind diag: ghzobj_slot = SaturnSheet_FindSlot(hash("GHZ/Objects.gif"))
// (>=0 => GHZOBJ.SHT staged with the path hash the engine computes; -1 => stage/hash
// failed). brg_surfslot/scope/h0 = gfxSurface[14] (the bridge's sheet surface) after
// the arm bind loop. ghzobj_h0 = the path hash word0; brg_surfh0 = the surface's
// stored hash word0 -- equal => the bridge resolved the SAME path the stage hashed.
__attribute__((used)) int32 p6_w_ghzobj_slot   = -9;
__attribute__((used)) int32 p6_w_ghzobj_h0     = 0;
__attribute__((used)) int32 p6_w_brg_surfslot  = -9;
__attribute__((used)) int32 p6_w_brg_surfscope = -9;
__attribute__((used)) int32 p6_w_brg_surfh0    = 0;
// BADNIK-VIS diag (2026-06-18): resolve WHERE the GHZ/Objects.gif surface lives now
// that Batch 2 (Explosion/Animals add Global/Explosions.gif + Animals.gif) may have
// shifted the surface load order off the hardcoded gfxSurface[14]. Scan ALL surfaces
// for the GHZ/Objects.gif hash; report the surface index, its saturnSheetSlot, scope,
// and the BOUND VDP1 handle. ghzobj_surf_idx<0 => the sheet has NO surface (no object
// loaded it); handle<0 => surface exists but UNBOUND (the invisible cause).
__attribute__((used)) int32 p6_w_ghzobj_surf_idx    = -9; // gfxSurface[] index for GHZ/Objects.gif
__attribute__((used)) int32 p6_w_ghzobj_surf_slot   = -9; // its saturnSheetSlot (expect 8)
__attribute__((used)) int32 p6_w_ghzobj_surf_scope  = -9; // its scope (2 == SCOPE_STAGE)
__attribute__((used)) int32 p6_w_ghzobj_surf_handle = -9; // p6_vdp1HandleBySurface[idx] (>=0 GREEN)
// (p6_w_surfpop already defined above at the surfcensus block -- reused here)
// Live-badnik draw-state latch (written from the OVERLAY witness via the api, since
// the pack cannot name Motobug/Newtron). bd_* = the first live badnik entity found:
// classID, position x/y(px), onScreen, visible, drawGroup, active, framesNN(0=NULL
// animator.frames=no sprite), animID, frameID, sheetID(the frame's), and the resolved
// handle for that sheetID (the overlay reads it via the exposed accessor below).
__attribute__((used)) int32 p6_w_bd_found    = 0;   // # live badnik entities scanned this frame
__attribute__((used)) int32 p6_w_bd_classid  = -1;
__attribute__((used)) int32 p6_w_bd_posx     = -1;
__attribute__((used)) int32 p6_w_bd_posy     = -1;
__attribute__((used)) int32 p6_w_bd_onscreen = -1;
__attribute__((used)) int32 p6_w_bd_visible  = -1;
__attribute__((used)) int32 p6_w_bd_drawgrp  = -1;
__attribute__((used)) int32 p6_w_bd_active   = -1;
__attribute__((used)) int32 p6_w_bd_framesNN = -1; // (animator.frames!=NULL)
__attribute__((used)) int32 p6_w_bd_animid   = -1;
__attribute__((used)) int32 p6_w_bd_frameid  = -1;
__attribute__((used)) int32 p6_w_bd_sheetid  = -1;
__attribute__((used)) int32 p6_w_bd_handle   = -2; // p6_vdp1HandleBySurface[bd_sheetid]
__attribute__((used)) int32 p6_w_bd_drawn    = 0;  // Motobug/Newtron Draw reached DrawSprite for an on-screen badnik
// P6.8 F.2-followup debug WARP (declared early -- the signpost-active scan in
// p6_ghz_frame's census reads it). p6_w_warp_plrx = the player x after the warp
// past the GHZ1 signpost (x=15792px); p6_w_warp_signactive = the active field of
// an entity AT the signpost x: ACTIVE_BOUNDS(4) before the cross, ACTIVE_NORMAL
// (2) once SignPost_CheckTouch fires. Diag-only (-DP6_WARP_TEST).
__attribute__((used)) int32 p6_w_warp_plrx       = 0;
__attribute__((used)) int32 p6_w_warp_signactive = -1;
// P6.8 F.2-ActClear debug (Task #234): SLOT_ACTCLEAR(=16) census. ActClear is
// the act-clear tally object SignPost spawns via RSDK.ResetEntitySlot(SLOT_
// ACTCLEAR) after the goalpost spin (SignPost.c:452); its ActClear.c:782
// ++SceneInfo->listPos is what advances GHZ1->GHZ2. p6_w_ac_classid = the slot-16
// entity classID (0 == never spawned). p6_w_ac_state = its StateMachine(state)
// fn pointer (the first field after RSDK_ENTITY in EntityActClear; offset
// sizeof(Entity)) -- map the raw pointer to a game.map ActClear_State_* symbol to
// see WHICH state it stalls in. p6_w_ac_timer = the int32 timer right after state
// (progress within a state). Read read-only in the census; diag witnesses only.
__attribute__((used)) int32 p6_w_ac_classid      = -1;
__attribute__((used)) int32 p6_w_ac_state        = 0;
__attribute__((used)) int32 p6_w_ac_timer        = 0;
__attribute__((used)) int32 p6_w_ac_frames       = 0;  // sticky: frames slot-16 alive
__attribute__((used)) int32 p6_w_ac_laststate    = 0;  // last ActClear state fn ptr while alive (which state)
__attribute__((used)) int32 p6_w_listpos_max     = 0;  // sticky MAX sceneInfo.listPos seen (did ++listPos fire?)
__attribute__((used)) int32 p6_w_mount_tag       = 0;  // last p6_layout_mount_for_scene resolved tag (4 chars packed)
__attribute__((used)) int32 p6_w_mount_listpos   = -1; // sceneInfo.listPos at that mount call
__attribute__((used)) int32 p6_w_ac_objcid       = -3; // global ActClear->classID (registered?)
__attribute__((used)) int32 p6_w_sign_state      = 0;  // latched signpost entity state fn ptr
__attribute__((used)) int32 p6_w_ring_cid        = -1; // pack Ring->classID after overlay wire (F.3)
// F.5 (real-signpost-trigger): sticky =1 once the located SignPost entity's
// `active` flips ACTIVE_BOUNDS(4) -> ACTIVE_NORMAL(2), which SignPost_CheckTouch
// sets (SignPost.c:326) the instant the canonical crossing (player.x>signpost.x)
// fires. Drift-proof (no .text address dependence). Combined with p6_w_ac_frames>0
// + the direct ActClear-spawn REMOVED, this proves ActClear came from the REAL
// SignPost_State_Spin ResetEntitySlot (SignPost.c:452), not a scripted spawn.
__attribute__((used)) int32 p6_w_sign_crossed    = 0;
// F.5 diagnostics: ground-truth the GHZ1 signpost setup (scene_objects.json is a
// broken parse for GHZ). count = SignPost entities present; type/posx = the
// last RUNPAST/DROP one found (RUNPAST=0, DROP=1, COMP=2, DECOR=3 per SignPost.h).
__attribute__((used)) int32 p6_w_sign_count      = 0;
__attribute__((used)) int32 p6_w_sign_type       = -1;
__attribute__((used)) int32 p6_w_sign_posx       = 0;
// 4MB Extended RAM Cart probe (Task #238). A-Bus CS0 cart region (Saturn_Overview
// .txt:688; skill memory map A-Bus CS0 0x02000000). The 4MB cart presents two 2MB
// banks; probe RW at each bank start + last longword via the cache-through alias
// (+0x20000000 -> 0x22400000/0x22600000) to bypass SH-2 cache on the A-Bus. ok
// bitmask: bit0=bank0 RW, bit1=bank1 RW -> 3 == full 4MB confirmed. Measure-first:
// the 1996 DTS docs predate this cart, so its addressability is CONFIRMED here, not
// assumed. Mednafen ss.cart=extram4 maps it; wrong token -> garbage readback -> RED.
__attribute__((used)) int32 p6_w_cart_ok         = -2; // -2 = probe never ran
__attribute__((used)) int32 p6_w_cart_rb0        = 0;  // bank0 readback sentinel
__attribute__((used)) int32 p6_w_cart_rb1        = 0;  // bank1 readback sentinel
// F.3 SignPost-by-state-pointer diagnostic. To avoid adding net WRAM-H .bss (it
// would push _end over the W17 floor), REUSE existing witnesses for the REAL
// SignPost (the entity whose `state` @off sizeof(Entity) lands in SignPost .text):
//   p6_w_sign_state     <- the SignPost current state pointer
//   p6_w_warp_signactive<- the SignPost active field
//   p6_w_warp_plrx stays the player x; p6_w_ac_timer/ac_state are FREE (ActClear
//   never spawns) -> repurpose: ac_timer = MIN spinCount (off 128) ever seen
//   (0 => spawned ActClear); ac_state = 1 if it EVER entered State_Spin.
#define P6_SIGN_TEXT_LO 0x06032780u
#define P6_SIGN_TEXT_HI 0x06033900u
#define P6_SIGN_STATE_SPIN 0x06032dfcu
// The verbatim ActClear TU defines `ObjectActClear *ActClear;` (C linkage). We
// only need its classID (RSDK_OBJECT first field, uint16 @ off 0) to tell whether
// RegisterObject gave it a live classID -- so a void* extern + a uint16 read.
extern void *ActClear;
// F.3: the pack's Ring object pointer (NULL by default; wired to the overlay
// Ring object in p6_ghz_frame so SignPost's sparkle CREATE_ENTITY is safe).
extern void *Ring;
// Batch 2: the pack's Animals object pointer (NULL placeholder in p6_closure_edge;
// rewired to the overlay's registered Animals in p6_ghz_frame so ActClear.c:903's
// foreach_active(Animals,...) -- a PACK ref reached on act-clear -- sees the live
// classID rather than NULL-derefing. Same Ring-seam pattern as `Ring` above.).
extern void *Animals;
// Batch 3: the pack's ItemBox/Debris object pointers (NULL placeholders in
// p6_closure_edge; rewired to the overlay's registered objects in p6_ghz_frame /
// p6_frontend_frame -- pack readers: Zone.c:380 foreach_active(ItemBox) at ATL
// store, SaveGame.c:133/295 broken-box recall, Ice.c:235 (PGZ-dead),
// Shield.c:194+ CREATE_ENTITY(Debris,...). Same Ring-seam pattern as above.).
extern void *ItemBox;
extern void *Debris;
// StarPost port (2026-07-17): the pack's StarPost object pointer (NULL placeholder
// in p6_closure_edge -- or the M3.1 zeroed instance under P6_AIZ_TEST); rewired to
// the overlay's registered StarPost in p6_ghz_frame / p6_frontend_frame so the
// pack death-respawn readers/writers (SaveGame.c:96-158 recall restore + reset,
// Zone.c:883 State_ReloadScene clock store, GameOver.c:319, PauseMenu.c:476-501,
// Player.c:2224) share ONE ObjectStarPost instance with the overlay class.
// Same Ring-seam pattern as above.
extern void *StarPost;
// R2 regression fix (2026-07-17, gate qa_starpost_fxfade_gates.py G1/G2): the
// load-duration detach (p6_closure_edge.c). Called at the top of
// p6_scene_load_and_arm (P6_FRONTEND_MENU only) so the pack's StarPost copy can
// never dangle into the defragged/re-allocated DATASET_STG pool while the
// load-time pack writers run (SaveGame_StageLoad -> SaveGame_LoadSaveData
// fresh-act reset, decomp SaveGame.c:151-163). The per-frame rewire below
// re-attaches on the first post-load frame.
extern "C" void p6_starpost_detach(void);
// F.5: the verbatim SignPost TU defines `ObjectSignPost *SignPost;` (C linkage).
// Its first field is the registered classID (uint16 @ off 0); entities of that
// class carry the same classID -> a drift-proof entity locator.
extern void *SignPost;
// F.5: the verbatim SignPost_State_Falling state fn -- assigning it to a DROP
// signpost's `state` reproduces the canonical GHZ drop (GHZ_DDWrecker.c:921);
// the signpost falls, lands, and transitions to State_Spin -> spawns ActClear.
extern void SignPost_State_Falling(void);
// ROUND-7 Objective A: State_Spin is the end-of-act spin; driving a materialized
// signpost's `state` straight to it (with spinCount preset) fires its ResetEntity
// Slot(SLOT_ACTCLEAR) with no ground/land dependency (SignPost.c:439-452).
extern void SignPost_State_Spin(void);
// P6.8 Step B (Task #211): the LEAN SHIPPING-boot flavor flag. 0 == DIAG
// (P6SCENE: full burst + ~14 proofs + Title reload + legacy Ring + deferred
// frame-260 GHZ switch -- byte-identical to W19/Step A). 1 == lean shipping
// boot (P6_ENGINE_SHIPPING): p6_scene_run stops right after LoadGameConfig
// (the masked load core through the staged sheets/bands/anim-pack + audio is
// complete) and p6_scene_tick's FIRST tick re-loads GHZ live. Set ONLY by
// p6_engine_boot_and_run; the DIAG p6_scene_run/p6_scene_tick lean guards are
// no-ops while it is 0, so the diag flavor is unchanged.
__attribute__((used)) int32 p6_lean_boot        = 0;  // 1 == lean engine shipping boot
// Perf Phase 1 (Task #211): frame-time BASELINE witnesses. Accumulated in
// p6_ghz_frame (the FRT is on-chip + NOT serialized into the savestate -- the
// gate reads these WRAM-H ints post-hoc). vblanks/frames -> true fps; the four
// per-section FRC-tick deltas (wrap-handled, divider in _cks) -> us attribution.
__attribute__((used)) int32 p6_w_perf_vblanks     = 0;  // p6_perf_vbl_count at capture
__attribute__((used)) int32 p6_w_perf_frames      = 0;  // continuous frames measured
__attribute__((used)) int32 p6_w_perf_vbl_max     = 0;  // worst single-frame vblank slip
__attribute__((used)) int32 p6_w_perf_cyc_input   = 0;  // last-frame ProcessInput FRC ticks
__attribute__((used)) int32 p6_w_perf_cyc_obj     = 0;  // last-frame ProcessObjects FRC ticks
__attribute__((used)) int32 p6_w_perf_cyc_draw    = 0;  // last-frame ProcessObjectDrawLists FRC ticks
__attribute__((used)) int32 p6_w_perf_cyc_present = 0;  // last-frame present FRC ticks
__attribute__((used)) int32 p6_w_perf_cyc_total   = 0;  // sum of the four sections
__attribute__((used)) int32 p6_w_perf_cyc_fgbg    = 0;  // #322: frontend FG-present+BG-stream span (was untimed)
__attribute__((used)) int32 p6_w_perf_cks         = -1; // FRT divider select (0=/8,1=/32,2=/128)
#if defined(P6_TICK_CATCHUP)
// #315 game-speed fix witnesses: logic ticks vs presented frames. Gate
// qa_chain_tickrate.py asserts delta(tick_frames)/delta(vblanks) >= 0.9 between
// two captures (60 Hz logic within the P6_TICK_CAP clamp). cont_frames keeps its
// PRESENTED-frames meaning (the perf fps gates are unchanged).
__attribute__((used)) int32 p6_w_tick_last   = 0; // catch-up N this presented frame
__attribute__((used)) int32 p6_w_tick_frames = 0; // cumulative 60Hz-clock logic ticks
#else
// Non-catch-up flavors (AIZ_TEST / GHZCUT_BOOT direct-boot) still reference
// p6_w_tick_frames in the GHZ scan-index tickmark diagnostic below. It advances
// once per presented frame there (no multi-tick clock) -- sufficient for the
// "+2 frames since build #1" settle heuristic. Define it here so those flavors link.
__attribute__((used)) int32 p6_w_tick_frames = 0;
#endif
// Phase 2a attribution: split the per-frame true-vblank slip into the engine
// work INSIDE p6_ghz_frame vs the jo_core_run loop body OUTSIDE it (dominated by
// slSynch -- SGL sort-list build + VDP1 command transfer + VDP1-draw/vblank
// wait; the loop has only jo_vdp1_buffer_reset+slUnitMatrix+the callback besides
// slSynch, core.c:591-641). Measured purely from p6_ghz_frame's own start/end
// vblank reads -- no jo-engine edit. Confirms whether the 91% is slSynch.
__attribute__((used)) int32 p6_w_perf_vbl_frame   = 0;  // vblanks consumed inside p6_ghz_frame
__attribute__((used)) int32 p6_w_perf_vbl_jo      = 0;  // vblanks in the jo loop body (~slSynch)
__attribute__((used)) int32 p6_w_perf_vbl_jo_max  = 0;  // worst jo-loop (slSynch) slip
// Phase 1b (#243): VDP1 draw-completion at compute-done -- the 2-VBLANK-LOCK
// discriminator. A 4ms CPU cut moved fps 29.91->29.91 (0 frames -> 1 vbl) so the
// 30fps is NOT CPU-bound. EDSR.CEF (bit1) read at the END of p6_ghz_frame (the
// latest point before the implicit slSynch) tells whether VDP1 still draws the
// prior sprite list: CEF=0 busy => DRAW-BOUND (slSynch waits on VDP1); CEF=1 done
// => the 2nd vbl is swap cadence. COPR/LOPR (when busy) size the overrun.
__attribute__((used)) int32 p6_w_perf_v1_done = 0;  // frames VDP1 done (CEF=1) at compute-done
__attribute__((used)) int32 p6_w_perf_v1_busy = 0;  // frames VDP1 still drawing (CEF=0)
__attribute__((used)) int32 p6_w_perf_v1_copr = 0;  // last COPR when busy (cmd-list progress)
__attribute__((used)) int32 p6_w_perf_v1_lopr = 0;  // last LOPR when busy (cmd-list end)
__attribute__((used)) int32 p6_w_perf_v1_edsr = 0;  // last raw EDSR (sanity)
// CP5b.7 VDP1 DUTY CYCLE -- the fill-bound-vs-cadence discriminator. The title
// frame is MEASURED 103ms (6 vbl) while master compute is only 8ms, so ~92% of
// the frame is the jo-body/slSynch/VDP1 wait. The compute-done EDSR sample (above)
// says VDP1-draw-bound, but its timing relative to slSynch is subtle. The vblank
// ISR (p6_perf.c) samples EDSR.CEF EVERY vblank while the title ticks (cont_frames
// > 5) -> busyvbl/totvbl = the fraction of ALL vblanks VDP1 is mid-draw, timing-
// independent. ~0.83 (busy 5 of 6 vbl) => VDP1 is genuinely fill-bound (big title
// sprites) -> the lever is DRAW REDUCTION. ~0.2 (mostly idle) => VDP1 is NOT the
// wait -> the 5 vbl is swap-cadence/audio -> a different lever. DECISIVE.
__attribute__((used)) int32 p6_w_perf_v1_busyvbl = 0; // vblanks VDP1 busy (CEF=0) while ticking
__attribute__((used)) int32 p6_w_perf_v1_totvbl  = 0; // total vblanks counted while ticking
// Dual-SH2 phase STEP 1 (#246/#243): slave-CPU liveness witness. The jo-side
// slave callback p6_slave_probe() increments this via the CACHE-THROUGH alias
// (addr | 0x20000000) each frame, so the slave's write reaches WRAM (a cached
// slave write would be invisible to the master + savestate). ticks>0 proves the
// slave SH-2 ran the callback AND the coherency handoff works -- the prerequisite
// for offloading the FG present onto the slave. Master/savestate read it cached
// after jo_core_wait_for_slave's slCashPurge.
__attribute__((used)) int32 p6_w_slave_ticks = 0;   // ++ by the slave each frame
// STEP B / swap-cadence (#246): MEASURE the jo-loop body / slSynch FRT directly
// (was only inferred). p6_ghz_frame runs as a jo callback; the jo loop body
// AROUND it (jo_vdp1_buffer_reset + slUnitMatrix + slSynch -- core.c:591-641) is
// NOT bracketed by the per-section FRT. This cross-frame delta = (this frame's
// start FRT) - (previous frame's end FRT) = the slSynch+body cost. If
// compute-FRT-sum (15ms) + synch_frt EXCEED the 16.7ms vblank, THAT is the 2-vbl
// cause (master work over one vblank), MEASURED -- not the slCashPurge guess.
__attribute__((used)) int32 p6_w_perf_synch_frt = 0; // jo-body/slSynch FRT ticks
__attribute__((used)) int32 p6_w_perf_synch_max = 0; // worst jo-body slSynch ticks
// Phase 2c (#246, DATA-DRIVEN -- stop DERIVING the compute-full). The per-section
// FRT-sum (input+obj+draw+present) measured ~14ms, but the master compute-FULL was
// only DERIVED (= frame 33.3ms - synch 11.3ms = ~22ms); the ~8ms gap is UNBRACKETED
// master work -- the slave-kick dispatch, the head setup, the census/EDSR/witness
// tail. MEASURE the full frame directly (entry->exit FRT) and SUB-ATTRIBUTE that gap
// into head/kick/tail so the 60fps cut targets the REDUCIBLE master work, not a
// derivation. INVARIANT: full_frt + synch_frt = the master's true per-frame time; if
// that sum exceeds the 16.7ms vblank the frame is locked to 2 vbl (MEASURED cause).
__attribute__((used)) int32 p6_w_perf_full_frt  = 0; // p6_ghz_frame entry->exit (compute FULL)
__attribute__((used)) int32 p6_w_perf_full_max  = 0; // worst-case compute-full
__attribute__((used)) int32 p6_w_perf_head_frt  = 0; // entry -> ProcessInput (setup overhead)
__attribute__((used)) int32 p6_w_perf_kick_frt  = 0; // p6_present_kick (slave fork dispatch)
__attribute__((used)) int32 p6_w_perf_tail_frt  = 0; // present-end -> exit (census/EDSR/witness)
#if defined(P6_STREAM_PERF)
// I3b 2b PERF #2 measurement (diag-only, P6_STREAM_PERF): the per-frame p6_ovl_stream scan cost in FRC
// ticks, bracketed in p6_stream_tick (runs INSIDE the cyc_obj bracket, so it isolates the stream's slice
// of ProcessObjects). ZERO shipping cost (gated out). RED baseline for the 1088-slot scan-narrowing lever.
__attribute__((used)) int32 p6_w_perf_stream_frt = 0;
#endif
// #243 band-crossing stall (user-felt: "fps gets really slow as I move forward /
// as it renders the next part"). A crossing = obj_refills>0 (the SaturnLayout FG +
// collision band store synchronously re-inflates for the new section, blocking the
// frame). These isolate the crossing cost from the one-time boot frame so a gate
// fires RED on ANY forward-progression run. Target: the resident-cart-layout fix
// drives xing_max_frt to ~steady (no per-crossing inflate).
__attribute__((used)) int32 p6_w_xing_count   = 0;  // frames with a band crossing (obj_refills>0)
__attribute__((used)) int32 p6_w_xing_max_frt = 0;  // worst compute-full FRT on a crossing frame
__attribute__((used)) int32 p6_w_xing_present_max = 0; // worst present-join FRT (FG band inflate wait)
// Phase 2b: per-section VBLANK deltas (overflow-immune). The FRT /32 wraps at
// 78 ms, so the FRC per-section deltas UNDERCOUNT any section that exceeds that
// (multi-wrap); the vbl_frame=77 vs cyc-sum=7 reconciliation proved one section
// is huge. These vblank counters find WHICH section is the real 1.3s.
__attribute__((used)) int32 p6_w_perf_vbl_input   = 0;  // ProcessInput vblanks
__attribute__((used)) int32 p6_w_perf_vbl_obj     = 0;  // ProcessObjects vblanks
__attribute__((used)) int32 p6_w_perf_vbl_draw    = 0;  // ProcessObjectDrawLists vblanks
__attribute__((used)) int32 p6_w_perf_vbl_present = 0;  // present vblanks
// Phase 2d census (Task #211): the in-range entity POPULATION after
// ProcessObjects (the engine set entity->inRange). NON-INVASIVE -- read from the
// pack, no verbatim-engine edit. ProcessObjects_ms / inrange discriminates "few
// heavy entities (Player collision -> targeted win)" from "many entities (->
// culling / dual-SH2)". topclass = the classID with the most in-range entities.
__attribute__((used)) int32 p6_w_obj_inrange   = 0;  // total inRange entities
__attribute__((used)) int32 p6_w_obj_topclass  = -1; // classID with the most in-range
__attribute__((used)) int32 p6_w_obj_topcount  = 0;  // count of that class
__attribute__((used)) int32 p6_w_obj_classcnt  = 0;  // distinct in-range classes
// Phase 2d per-Update timing (P6_PERF_OBJPROF; Object.cpp accumulates, indexed
// by classID&0x3F). p6_ghz_frame resets per frame + scans for the hog.
#if defined(P6_SPLIT)
// #261 SCAN-SPLIT WRAM RECLAIM: the 3 objprof arrays (768 B .bss) push _end past
// the tight plain-GHZ ANIMPAK ceiling 0x060B6C00 when P6_SHADOW+P6_SPLIT are added
// (MEASURED: _end 0x060b6e40, +576 B -> #228 boot trap, cont_frames==0). Relocate
// them to the VERIFIED-FREE cache-through cart gap [0x226C44C0(split end),0x226C8000
// (DORM)] -- master-only (written+read same cache in p6_ghz_frame/Object.cpp loop1),
// so a plain CACHED cart pointer is correct (same pattern as s_p6_shadow_inrange
// @0x026C0000). Every p6_w_objupd_*[i] indexes the pointer unchanged. Reclaims 768 B
// -> _end fits -> plain GHZ + shadow + split BOOTS so split_ticks can be read. Gated
// P6_SPLIT -> shipping .bss byte-identical.
#define p6_w_objupd_vbl ((volatile int *)0x026C5000u)
#define p6_w_objupd_n   ((volatile int *)0x026C5400u)
#define p6_w_objupd_us  ((volatile int *)0x026C5800u)
#else
__attribute__((used)) int p6_w_objupd_vbl[64];
__attribute__((used)) int p6_w_objupd_n[64];
// Phase 2h (Task #230): per-Update FRT-TICK accumulator. At 30 fps the whole
// frame is ~1 vblank, so the vbl[] profiler reads all-zero (every Update is
// sub-vblank). The FRT (1.19 us/tick @ /32) is now reliable -- ProcessObjects
// is sub-78ms -- so it resolves per-class Update cost. Reset + summed per frame.
__attribute__((used)) int p6_w_objupd_us[64];
#endif
__attribute__((used)) int32 p6_w_objupd_topclass = -1; // classID with the most Update time
__attribute__((used)) int32 p6_w_objupd_topvbl   = 0;  // that class's total Update vbl
__attribute__((used)) int32 p6_w_objupd_topus    = 0;  // that class's total Update FRT ticks
__attribute__((used)) int32 p6_w_objupd_topn     = 0;  // that class's in-range count
__attribute__((used)) int32 p6_w_obj_refills = 0; // SaturnLayout inflates DURING ProcessObjects
// Phase 2i (Task #245): ProcessObjects internal sub-phase FRT timing. The 13.5ms
// section splits into loop1 (the 2368-slot inRange scan + the ~12 in-range
// Update()s + drawgroup append), loop2 (typeGroup build, already in-range-list
// driven by Phase 2h), loop3 (the lateUpdate pass -- STILL a full 2368-slot
// RSDK_ENTITY_AT scan + onScreen clear, NOT yet list-driven). Object.cpp brackets
// each loop under P6_PERF_OBJPROF. The gate isolates the scan cost as
// loop1 - sum(p6_w_objupd_us) (Update dispatch). Measure-first: which loop owns
// the 13.5ms picks the Lever-2 optimization (no guessing).
__attribute__((used)) int32 p6_w_objsec_loop1 = 0; // inRange scan + Update + drawgroup (FRT ticks)
// #261 GO/NO-GO: FRT ticks spent in ONLY the switch(active) inRange classification
// per frame (EXCLUDES the Update dispatch + drawgroup). loop1 - classify = the Update
// cost. If classify is a MINORITY of loop1, the scan-split (offloads classify only) is
// the wrong lever. Gated P6_SPLIT (diagnostic). Object.cpp accumulates per-slot.
__attribute__((used)) int32 p6_w_objsec_classify = 0;
__attribute__((used)) int32 p6_w_objsec_loop2 = 0; // typeGroup build (FRT ticks)
__attribute__((used)) int32 p6_w_objsec_loop3 = 0; // lateUpdate full-scan + onScreen clear (FRT ticks)
// LOCKED-60 (#243, 2026-06-18): DrawLists (7.3ms) sub-attribution -- is the cost the
// O(n^2) zdepth BUBBLE SORT (Object.cpp:953-964, per sorted drawgroup) or the per-
// entity draw() callbacks + VDP1 emission? Decides the lever: a sort fix is a cheap
// single-CPU win (maybe 60fps WITHOUT dual-SH2); callback/emit cost justifies the
// slave render-pipeline. P6_PERF_OBJPROF-gated FRT ticks, summed across drawgroups.
__attribute__((used)) int32 p6_w_draw_sort   = 0; // FRT ticks in the bubble-sort loops (summed)
__attribute__((used)) int32 p6_w_draw_cb     = 0; // FRT ticks in the draw()-callback loops (summed)
__attribute__((used)) int32 p6_w_draw_maxgrp = 0; // max list->entityCount over visible drawgroups
__attribute__((used)) int32 p6_w_draw_nents  = 0; // total entries iterated over visible drawgroups
#if defined(P6_FRONTEND_MENU)
// #317 front-end perf: DrawLists sub-attribution in VBLANKS (draw_sort/draw_cb are FRT
// ticks that WRAP at ~78ms; the front-end draw section is ~200ms so FRT is useless).
// Localizes the ~168ms unaccounted after sort(~0)+cb(~32ms): hookCB vs entity-cb vs
// tile-layer. Gated on P6_FRONTEND_MENU (chain-only) -> plain GHZ .bss byte-identical.
__attribute__((used)) int32 p6_w_draw_hook_v = 0; // vblanks in group hookCB() (summed)
__attribute__((used)) int32 p6_w_draw_cb_v   = 0; // vblanks in the entity draw() loops (summed)
__attribute__((used)) int32 p6_w_draw_tile_v = 0; // vblanks in the tile-layer loop (summed)
// #317 sub-split of draw_cb (entity draws = 98% of the ~200ms draw section): is the
// cost the Saturn blit (p6_vdp1_blit_flipped, incl. the LRU miss-DMA) or the object
// Draw LOGIC around it? blit_v = vblanks inside the blit call (all p6_draw_flipped);
// dma_v = vblanks inside the LRU miss path (slDMAWait + stage-copy + jo_sprite_replace,
// in p6_vdp1.c). blit_v>>rest => blit-bound; dma_v~=blit_v => the eviction re-DMA.
__attribute__((used)) int32 p6_w_draw_blit_v = 0; // vblanks inside p6_vdp1_blit_flipped calls
__attribute__((used)) int32 p6_w_draw_dma_v  = 0; // vblanks inside the LRU miss DMA (p6_pool_for)
// #324: FRT ticks in the draw-bracket TAIL (after ProcessObjectDrawLists+p6_dl_end,
// i.e. the fade/titlecard/diag epilogue up to fe_t1). MEASURED RED (live chain,
// _drawprof.jsonl 2026-07-09): AIZ fly-in cyc_draw = 13.7k ticks (16.3ms) while
// draw_cb = 558 -- the cost sat in this tail, not the entity draw() loops.
__attribute__((used)) int32 p6_w_draw_tail   = 0;
// #325 front-end ProcessObjects profile: the class StaticUpdate loop
// (Object.cpp:550-560, verbatim decomp -- EVERY stage class with staticVars
// active ALWAYS/NORMAL, which Scene.cpp:239 sets for ALL loaded classes) runs
// BEFORE the loop1 bracket, so its cost was UNMEASURED (cyc_obj included it,
// loop1/2/3 did not). Bracket it + track the worst single class per tick.
// Chain-gated -> plain GHZ Scene_Object.o/.bss byte-identical.
__attribute__((used)) int32 p6_w_objsec_static = 0; // FRT ticks, whole StaticUpdate class loop (last tick)
__attribute__((used)) int32 p6_w_stat_max      = 0; // worst single staticUpdate (FRT ticks, last tick)
__attribute__((used)) int32 p6_w_stat_max_cls  = -1; // stage class index (== classID) of the worst
__attribute__((used)) int32 p6_w_stat_n        = 0; // staticUpdates dispatched (last tick)
// #325 profile round 2: per-classID StaticUpdate accumulators (mirror p6_w_objupd_us/n
// for the static loop -- the Menu leg showed ONE class at 548 FRT ticks EVERY tick and
// stat_max_cls alone cannot prove which body line repeats). FE-gated -> plain GHZ .bss
// byte-identical.
__attribute__((used)) int p6_w_statupd_us[64];
__attribute__((used)) int p6_w_statupd_n[64];
// #325 profile round 2: the cyc_obj minus (static+loop1+loop2+loop3) gap measured ~1.7k
// FRT ticks/tick at the GHZ landing (~2ms x 4 ticks = 8ms/frame unattributed). Bracket
// the pre-loop1 block (camera update + p6_scan_update_near + p6_stream_tick) so the gap
// attribution is data, not inference. (The remaining gap = ProcessSceneTimer +
// ProcessParallaxAutoScroll, bracketed OUTSIDE ProcessObjects in the tick group.)
__attribute__((used)) int32 p6_w_objsec_pre    = 0; // FRT ticks, camera+near-set+stream (last tick)
// #325 lever (i): engage the proven I3d far-cull (plain-GHZ shipping, Object.cpp
// non-FE branch) at the CHAIN's playable-GHZ landing. The FE loop1 branch (#298)
// deliberately skipped the near-bit consult for the ~130-slot Menu; the landing
// runs ~1041 scene slots x up to 4 catch-up ticks through full ACTIVE_BOUNDS
// checks = the measured 39.4k-tick cyc_obj. Runtime flag: armed ONLY at
// folder=="GHZ" once the sorted index exists (p6_scan_n>0); 0 on every other
// leg so Menu/AIZ/Title behavior is untouched.
__attribute__((used)) int32 g_p6_fe_ghz_cull   = 0;
// #325 A/B knob: live-pokeable override (-1 = no override). The arm block re-writes
// g_p6_fe_ghz_cull every frontend frame, so a plain live poke would be overwritten;
// the RED/GREEN A/B (qa_objprof_watch --ab) pokes THIS instead. Shipping default -1.
__attribute__((used)) int32 g_p6_fe_cull_override = -1;
// #302 AIZ render-wall attribution (2026-07-16): CUMULATIVE VBLANK sums per
// frontend-frame section, read as deltas over a live window by qa_aiz_speed.py
// (vbl/frame = d(witness)/d(p6_w_cont_frames)). VBLANK-stamped, NOT FRT: the
// 16-bit FRT wraps at ~78 ms and an AIZ fly-in frame is ~105 ms (9.5 fps), so
// FRT per-section deltas undercount (the same reason as the #317 vbl witnesses
// above). Cumulative sums beat the existing last-frame p6_w_perf_vbl_* here:
// a sub-vblank section reads 0 on most single frames (integer vblank counter)
// but its true cost accumulates over an 8 s window. Chain-gated
// (P6_FRONTEND_MENU) -> plain GHZ .bss byte-identical.
__attribute__((used)) int32 p6_w_aiz_vbl_objtick   = 0; // ProcessObjects catch-up tick group
__attribute__((used)) int32 p6_w_aiz_vbl_fgpresent = 0; // FG-Low NBG1 present (p6_vdp2_present_ghz_camera)
__attribute__((used)) int32 p6_w_aiz_vbl_bgstream  = 0; // 3x p6_vdp2_aiz_bg_stream + p6_vdp2_aiz_bg_frame
__attribute__((used)) int32 p6_w_aiz_vbl_vdp1emit  = 0; // p6_dl_begin..ProcessObjectDrawLists..p6_dl_end
__attribute__((used)) int32 p6_w_aiz_vbl_framesum  = 0; // whole p6_frontend_frame (jo-body = total - this)
// #331 sync-floor attribution (Step 1): CUMULATIVE outside-frame vblanks -- the
// sum of every frontend jo_gap (previous frame END -> this frame START = the jo
// loop body INCLUDING the single slSynch, core.c:633). Paired with the jo-side
// p6_w_sync_vbl_sum (p6_perf.c, vblanks INSIDE slSynch itself) the split is
// exact: jo-else = d(jo_vbl_sum) - d(sync_vbl_sum). Same MENU gate as the #302
// sums -> plain GHZ .bss byte-identical. Read by tools/qa_sync_floor.py.
__attribute__((used)) int32 p6_w_jo_vbl_sum        = 0; // cumulative jo-body vblanks (incl. slSynch)
#endif
// LOCKED-60 (#243): loop1 scan occupancy -- sizes the maxOccupiedSlot trim AND
// explains the 5.82->15.95ms scan growth. pop = populated slots (classID!=0);
// maxslot = highest populated slot (the empty-tail boundary); bounds = slots whose
// active is ACTIVE_*BOUNDS (the per-camera distance check, the per-slot HOG). If
// bounds grew with the mass-port, the scan cost is INTRINSIC (registered objects
// run the bounds check) -- not a cheap trim. MEASURED, not guessed.
__attribute__((used)) int32 p6_w_scan_pop     = 0; // slots with classID != 0
__attribute__((used)) int32 p6_w_scan_maxslot = 0; // highest slot with classID != 0
__attribute__((used)) int32 p6_w_scan_bounds  = 0; // populated slots with ACTIVE_*BOUNDS
// I3b SHRINK distribution measurement (read-only, one-shot post-InitObjects). Resolves the LAST
// design unknown for the atomic shrink WITHOUT touching the pool: is GHZ1's live scene set COMPACTED
// to a contiguous [RESERVE, RESERVE+npop) (simple remap) or SPREAD with gaps (relocation needed)?
// npop = total populated SCENE slots (the real N, vs scan_pop which is near-only); maxls = highest
// populated scene slot; firstgap = first EMPTY scene slot at/after RESERVE (== RESERVE+npop iff fully
// compacted, no gaps). p6_w_scan_pop's near-cull does NOT apply here (this is a plain full walk).
__attribute__((used)) int32 p6_w_pool_npop     = -1; // total populated scene slots (the real N)
__attribute__((used)) int32 p6_w_pool_maxls     = -1; // highest populated scene slot
__attribute__((used)) int32 p6_w_pool_firstgap = -1; // first empty scene slot >= RESERVE (compaction test)
// I3b 2b COMPACTION witnesses (the de-risk milestone: relocate all populated scene entities into a
// dense physical pool via the non-identity remap, proven byte-safe offline by qa_p6_pool_compact_model).
__attribute__((used)) int32 p6_w_compact_n      = -1; // populated scene slots relocated (== p6_w_pool_npop)
__attribute__((used)) int32 p6_w_compact_sphys  = -1; // NEW p6_pool_scene_phys after compaction (n+1)
__attribute__((used)) int32 p6_w_compact_dummy  = -1; // reserved classID=0 dummy physical slot (R+n)
__attribute__((used)) int32 p6_w_compact_bij_ok = -1; // 1 iff remap/inv round-trip for all populated + dummy clear
__attribute__((used)) int32 p6_w_compact_lastL  = -1; // highest populated logical slot (pre-compaction)
__attribute__((used)) int32 p6_w_compact_lastP  = -1; // physical slot it mapped to (== R+n-1 iff it was the last)
// I3b 2b STREAMING per-frame manager witnesses (the overlay p6_ovl_stream writes them via ld -R).
__attribute__((used)) int32 p6_w_stream_mat      = 0;  // cumulative materializes (newly-near re-created from DORM)
__attribute__((used)) int32 p6_w_stream_dorm     = 0;  // cumulative dormants (newly-far freed)
__attribute__((used)) int32 p6_w_stream_free     = -1; // current free-list count (slots available for materialize)
__attribute__((used)) int32 p6_w_stream_resident = -1; // current resident scene slots (remap != dummy)
__attribute__((used)) int32 p6_w_stream_starve   = 0;  // times a materialize was wanted but the free-list was empty
// I3b 2b BACKTRACK PROOF witnesses (written ONLY by the overlay's P6_BACKTRACK_PROOF harness; 0 in
// shipping). The harness synthetically destroys a resident scene entity (classID=0, exactly how RSDK
// destroyEntity signals it) and proves the stream RETIRES it (life bit) + never RE-MATERIALIZES it.
__attribute__((used)) int32 p6_w_bt_logical = -1; // logical slot of the destroyed entity (-1 = none yet)
__attribute__((used)) int32 p6_w_bt_cid     = 0;  // its classID BEFORE the destroy (0 = no real entity hit)
__attribute__((used)) int32 p6_w_bt_life    = 0;  // 1 = stream set its lifecycle (destroyed) bit = retired
__attribute__((used)) int32 p6_w_bt_reappear= -1; // 1 = it RE-MATERIALIZED after destroy (THE BUG); 0 = stayed dead
// I3b 2b POOL-INVARIANT guard (ALWAYS-ON, written by the overlay stream): sticky latch == 1 the first
// frame the free-list invariant resident+free==SCENE_PHYS-1 is ever violated (a leak/double-free). 0 ==
// the pool stayed accountable every frame. Permanent self-check; gate qa_p6_stream_in S4.
__attribute__((used)) int32 p6_w_pool_inv_bad = 0;
#if defined(P6_SHADOW_COMPARE)
// LOCKED-60 (#243) SCAN-SPLIT PARITY PROOF: before building the dual-SH2 scan-split
// (master classifies [0,mid), slave [mid,end), all at frame-start), PROVE it matches
// the serial engine. Object.cpp runs a shadow pre-pass (classify-all-at-frame-start
// into s_p6_shadow_inrange[], the EXACT engine bounds checks) then compares to the
// real interleaved loop1's inRange per entity. divergence = an entity a mid-frame
// reposition pushed across its update bound (the ONLY way the split could differ).
// 0 over real gameplay => the scan-split is parity-exact for GHZ1. Gated -> normal
// builds never define this.
// Starts DISABLED -- p6_ghz_frame enables it only AFTER gameplay is live (cont_frames
// > 10). The 1216-byte shadow array is NOT here (it lived in .bss and its placement
// shifted the pack layout -> #228-class boot hang); it now lives in the free 4MB-cart
// gap (Object.cpp macro). Only these tiny witnesses stay in .bss (the savestate peeks
// them; the cart is not serialized into the .mcs).
extern "C" { int g_p6_shadow_enable = 0;
             __attribute__((used)) int32 p6_w_scan_divergence = 0;
             __attribute__((used)) int32 p6_w_scan_divmax = 0; } // worst-frame divergence
#endif
// DUAL-SH2 SCAN-SPLIT (#261) Increment-1 witnesses. Defined UNCONDITIONALLY (12 B
// .bss, harmless when P6_SPLIT is off) so Scene_Object.o's extern "C" refs always
// link. p6_w_split_mismatch(_max) = slave-vs-master classification mismatch over the
// upper scene half (0 == the slave path is bit-correct across the CPU boundary);
// p6_w_split_ticks = slave-entry liveness. Written cache-through by the slave.
extern "C" { __attribute__((used)) int p6_w_split_mismatch     = 0;
             __attribute__((used)) int p6_w_split_mismatch_max = 0;
             __attribute__((used)) int p6_w_split_ticks        = 0; }
__attribute__((used)) int32 p6_w_hog_cid = -1;  // full classID of the hog
__attribute__((used)) int32 p6_w_hog_x   = 0;   // a hog entity's world x (fixed)
__attribute__((used)) int32 p6_w_hog_y   = 0;   // a hog entity's world y (fixed)
static unsigned int p6_perf_vbl_prev = 0;               // vblank tally at the previous frame END
static unsigned short p6_perf_frt_prev_end = 0;         // FRT at the previous frame END (slSynch measure)
// W14b camera-chain witnesses (Task #227): TICK-TIME snapshots only -- the
// post-hoc capture lands after the later Title pass, whose STG dataset clear
// NULLs every tracked staticVars pointer (MEASURED p6_f8: Player/Zone/Camera
// all 0 in the capture while the GHZ-tick witnesses show the Player live).
__attribute__((used)) int32 p6_w_cam_static   = -1; // game Camera staticVars ptr at tick time
__attribute__((used)) int32 p6_w_zone_static  = -1; // game Zone staticVars ptr at tick time
__attribute__((used)) int32 p6_w_cam_entclass = -1; // SLOT_CAMERA1 entity classID after the ticks
__attribute__((used)) int32 p6_w_cam_state    = -1; // EntityCamera state fn ptr
__attribute__((used)) int32 p6_w_cam_target   = -1; // EntityCamera target ptr (expect SLOT_PLAYER1)
__attribute__((used)) int32 p6_w_cam_x        = 0;  // EntityCamera position after the ticks
__attribute__((used)) int32 p6_w_cam_y        = 0;
__attribute__((used)) int32 p6_w_scr_x        = ~0; // screens[0].position after the ticks
__attribute__((used)) int32 p6_w_scr_y        = ~0; // (Camera_SetCameraBounds, Camera.c:105-107)
__attribute__((used)) int32 p6_w_api_screencount = -1; // game RSDK.GetVideoSetting(SCREENCOUNT) pre-InitObjects
__attribute__((used)) int32 p6_w_api_credits     = -1; // game RSDK.CheckSceneFolder("Credits") pre-InitObjects
__attribute__((used)) int32 p6_w_eng_screencount = -1; // engine-side direct GetVideoSetting (table-slot discriminator)
__attribute__((used)) int32 p6_w_eng_vs_count    = -1; // engine-side raw videoSettings.screenCount readback
__attribute__((used)) int32 p6_w_cam_entclass0 = -1; // SLOT_CAMERA1 classID right AFTER InitObjects
__attribute__((used)) int32 p6_w_zone_boundsR = -1; // Zone->cameraBoundsR[0] at tick time (Zone.c:223 = GetLayerSize x)
__attribute__((used)) int32 p6_w_zone_boundsB = -1; // Zone->cameraBoundsB[0] (Zone.c:225 = GetLayerSize y)
__attribute__((used)) int32 p6_w_cam_boundsR  = -1; // EntityCamera boundsR after ticks (HandleHBounds-evolved)
__attribute__((used)) int32 p6_w_cam_boundsB  = -1;
                                                     // (vs entclass after ticks: created-then-clobbered
                                                     // discriminator)
// P6.7 W12 (Task #227, qa_p6_sheet.py): probe-replay witnesses (staged/
// fetches counters live in SaturnSheet.cpp).
__attribute__((used)) int32 p6_w_sht_probes   = -1; // byte-exact rects (model 15)
__attribute__((used)) int32 p6_w_sht_firstbad = -1;
// P6.7 W7 (Task #227, qa_p6_input.py): engine input chain witnesses.
// ticks/perid written by InputDevice_Saturn.cpp (the device UpdateInput);
// ctrl/stick/touch ptrs by p6_input_witness (p6_wave1_reg.c, game side);
// the rest filled after the 60-tick GHZ run below.
__attribute__((used)) int32 p6_w_in_ticks    = 0;  // device UpdateInput count (I2)
__attribute__((used)) int32 p6_w_in_perid    = -2; // Smpc_Peripheral[0].id at update (I4)
__attribute__((used)) int32 p6_w_in_ctrlptr  = 0;  // game ControllerInfo after link (I3)
__attribute__((used)) int32 p6_w_in_stickptr = 0;  // game AnalogStickInfoL after link (I3)
__attribute__((used)) int32 p6_w_in_touchptr = 0;  // game TouchInfo after link (I3)
__attribute__((used)) int32 p6_w_in_devcount = -1; // RSDK::inputDeviceCount (I4)
__attribute__((used)) int32 p6_w_in_devid    = 0;  // inputDeviceList[0]->id (I4)
__attribute__((used)) int32 p6_w_in_devstate = -1; // (active<<16)|(isAssigned<<8)|anyPress (I4)
__attribute__((used)) int32 p6_w_in_slot0    = -3; // RSDK::inputSlots[0] (I4: INPUT_AUTOASSIGN)
__attribute__((used)) int32 p6_w_in_btnbits  = -1; // OR of down|press, controller[0..4] x 12 (I4)
// W12b GFS-table tracer RETIRED (Task #227, 2026-06-12): the corruption was
// the pack's orphan .bss.* output sections overlapping the main .bss (179
// pairs) -- typeGroups[126].entryCount=0 landed on the GfsMng GFCF_Seek
// pointer. Permanent guards: the p6_pack_merge.ld second ld -r pass
// (build_p6scene_objs.sh [8/8]) + qa_p6_mapoverlap.py.
// p6_snd.c: CD-DA start through the proven jo_audio_play_cd_track path.
void p6_cdda_play(int track, int loop);
// p6_vdp1.c (C TU, jo side): slot-cached VDP1 blitter the Saturn DrawSprite
// backend targets. sheet_bind pins the engine surface + mirrors the palette
// to CRAM bank 1 once; blit() draws a sheet rect at an engine TOP-LEFT,
// uploading each DISTINCT rect to VDP1 exactly once (cache keyed on
// (sx,sy,w,h) -- a per-tick jo_sprite_add would be the #189 overflow class).
// W12b: binds return a SHEET HANDLE (multi-sheet cache; banded sheets bind
// their SaturnSheet store slot instead of resident pixels) and blit takes it.
int  p6_vdp1_sheet_bind(const unsigned char *sheetPixels, int sheetWidth,
                        const unsigned short *pal565);
int  p6_vdp1_sheet_bind_banded(int shtSlot, int sheetWidth,
                               const unsigned short *pal565);
void p6_vdp1_blit(int sheet, int x, int y, int w, int h, int sx, int sy);
void p6_vdp1_blit_flipped(int sheet, int x, int y, int w, int h, int sx, int sy,
                          int flipX, int flipY);
#if defined(P6_DIRECT_VDP1)
// Fix 1 ("Sonic invisible on slopes"): rotated draw -- VDP1 DISTORTED SPRITE
// (ST-013-R3 sec 7.6) through the same slot plumbing as blit_flipped. (x,y) is
// the entity screen pos (NOT pos+pivot); sn/cs = Sin512/Cos512(rotation).
void p6_vdp1_blit_rot(int sheet, int x, int y, int w, int h, int sx, int sy,
                      int pivotX, int pivotY, int sn, int cs, int flipX);
#endif
#if defined(P6_FRONTEND_TITLE)
// CP5b.4 (Task #272): set/clear VDP1 half-transparency (CL_Trans) for INK_BLEND/
// INK_ADD title sprites. Defined in p6_vdp1.c (jo-side). Title flavor only.
void p6_vdp1_set_ink(int half);
#endif
#if defined(P6_FRONTEND_CHAIN)
/* CP5c CRAM-palette fix: re-arm the sheet/slot state (s_sheet_count=0) so the next
 * surface bind re-runs p6_pal_mirror with the NEW scene's fullPalette[0] -> CRAM
 * bank 1 carries the destination scene's sprite palette across a front-end folder
 * change. See the definition in p6_vdp1.c for the MEASURED root cause. */
void p6_vdp1_frontend_pal_reset(void);
#endif
#if defined(P6_FRAMEDIR)
/* Water M1b regression fix: update a persisted handle's latched shtSlot after
 * SaturnSheet_BandReset renumbers the band store (see p6_vdp1.c definition). */
void p6_vdp1_sheet_update_slot(int handle, int shtSlot);
#endif
}
// W12b: surfaceID -> vdp1 sheet handle (filled at bind time; -1 = unbound).
static int8 p6_vdp1HandleBySurface[SURFACE_COUNT];
static bool p6_vdp1HandlesInit = false;

// BADNIK-VIS diag (2026-06-18): accessor the OVERLAY witness calls to resolve a
// badnik frame's bound handle (the overlay cannot name this static table). Returns
// -3 for an out-of-range/uninit query, else the int8 handle (>=0 bound, -1 unbound).
extern "C" int32 p6_vdp1_handle_for_surface(int32 sheetID)
{
    if (!p6_vdp1HandlesInit || sheetID < 0 || sheetID >= SURFACE_COUNT)
        return -3;
    return (int32)p6_vdp1HandleBySurface[sheetID];
}

// P6.6c: Audio.cpp's file-scope stream state lives at GLOBAL scope (NOT in
// namespace RSDK -- Audio.cpp:20,24): PlayStream sprintf-s the request path
// into streamFilePath and stashes the loop point; the Saturn
// AudioDevice::HandleStreamLoad below reads both to resolve the CD track.
extern char streamFilePath[0x40];
extern int32 streamLoopPoint;

// P6.7c: the Saturn LoadSfxToSlot pool-exhaustion guard counter. Audio.cpp's
// block-scope `extern int32 p6_saturn_sfx_skips` (inside RSDK::LoadSfxToSlot)
// binds to namespace RSDK under GCC 8.2, so the definition lives there; the
// run body mirrors it into the p6_w_sfx_skips witness.
namespace RSDK {
__attribute__((used)) int32 p6_saturn_sfx_skips = 0;
#if defined(P6_GHZ_AUTORUN)
// Signpost campaign: djb2 of the last pool-skipped sfx name. Defined HERE (in
// namespace RSDK) because Audio.cpp's block-scope `extern int32
// p6_w_sfxskip_hash` binds to RSDK:: under GCC 8.2 -- the exact
// p6_saturn_sfx_skips precedent documented above.
__attribute__((used)) int32 p6_w_sfxskip_hash = 0;
#endif
// Task #271 load-time fix: DATASET_SFX pool-full LATCH + the count of file-opens
// the LoadSfxToSlot early-out SAVED. Same namespace as p6_saturn_sfx_skips so
// Audio.cpp's `using namespace RSDK` block-scope externs bind here under GCC 8.2.
// pool_full latches when an SFX alloc fails (pool only grows mid-load), reset per
// ClearStageSfx; skipped_open counts the ~51 wasted GameConfig SFX seeks removed.
#if defined(P6_FRONTEND_LOGOS)
__attribute__((used)) int32 p6_saturn_sfx_pool_full    = 0; // front-end-only (GHZ WRAM-H ceiling)
__attribute__((used)) int32 p6_saturn_sfx_skipped_open = 0; // front-end witness only
#endif
// P6.7 W11: tempEntityList overflow witness (Scene.cpp clamp; expectation
// zero at every measured 1.03 scene under the GHZ-scale retarget).
__attribute__((used)) int32 p6_saturn_tempentity_skips = 0;
// P6.7 W11b (Task #226): scale-safety counters, all gate-expected ZERO.
__attribute__((used)) int32 p6_saturn_group_skips    = 0; // dropped type/draw group appends (Object.hpp macros)
__attribute__((used)) int32 p6_saturn_layer_unbound  = 0; // GetTile on a windowless non-resident layer (Scene.hpp seam)
__attribute__((used)) int32 p6_saturn_settile_drops  = 0; // SetTile on a non-resident layer (declared band write-through gap)
__attribute__((used)) int32 p6_saturn_layer_binds    = 0; // SaturnLayout_Bind calls from the Scene.cpp load arm
// Task #227 hang bisect: InitObjects breadcrumb (Object.cpp P6_SCENE_TEST
// arm; block-scope extern inside RSDK::InitObjects binds to namespace RSDK
// under GCC 8.2 -- the p6_saturn_sfx_skips precedent above).
__attribute__((used)) int32 p6_w_initobj_step = 0; // 0x1...=StageLoad 0x2...=Create 0x7FFFFFFF=done
__attribute__((used)) int32 p6_w_anim_step    = 0; // LoadSpriteAnimation phase stamp (Animation.cpp P6_ANIM_STAMP)
__attribute__((used)) int32 p6_saturn_anim_allocfail = 0; // STG-full refusals in LoadSpriteAnimation (runaway-read guard)
__attribute__((used)) int32 p6_saturn_hitbox_clamps  = 0; // hitboxes dropped by the FRAMEHITBOX_COUNT(2) retarget (expect 0)
__attribute__((used)) int32 p6_w_anim_lastfail = 0; // (sprfile id << 16) | frameCount (bit15: animCount fail)
__attribute__((used)) int32 p6_w_stg_at_fail   = 0; // dataStorage[STG].usedStorage in BYTES at the last refusal
__attribute__((used)) int32 p6_w_anim_log[48]  = { 0 }; // W13: {hash[0], (result<<16)|frameCount} x 24 loads
__attribute__((used)) int32 p6_w_anim_logn     = 0;
__attribute__((used)) int32 p6_w_apk_bytes     = 0; // W13: GHZANIM.PAK GFS load size (>0 == mounted)
__attribute__((used)) int32 p6_w_apk_hash      = 0; // djb2 over the loaded blob (gate vs cd file)
// #254 residency lever: GHZOBJ.PAK = the CART-resident COLD object anim pack
// (>0 == mounted). Object anims resolve fast-path from the cart -> zero DATASET_STG.
__attribute__((used)) int32 p6_w_objapk_bytes  = 0;
}

// ---- (b1) Relocated engine globals: pointer form + WRAM-L backing ------------
// Headers give the pointer extern under P6_SCENE_TEST (Scene.hpp:193-196,215-218,
// Object.hpp:245-275, Storage.hpp:102-106, Drawing.hpp:291-295, Animation.hpp:80-84,
// Scene3D.hpp:128-132). LIVE backing = real WRAM-L addresses; DEAD ones point at
// the shared dummy -- measured dead in the LoadSceneAssets-only proof:
//   drawGroups          written only by LoadSceneFolder (Scene.cpp:45-48,133-136)
//   typeGroups          written only by LoadSceneFolder (Scene.cpp:51-53)
//   objectClassList     address-of only with classCount==0 (Scene.cpp:497;
//                       hash loop :485-490 bounded by sceneInfo.classCount==0)
//   spriteAnimationList ClearSpriteAnimations only via LoadSceneFolder
//   modelList           Clear3DScenes only via LoadSceneFolder
//   tilesetPixels       LoadStageGIF only via LoadSceneFolder (Scene.cpp:272)
//   collisionMasks/tileInfo  LoadTileConfig only via LoadSceneFolder (:163-164)
namespace RSDK {
EntityBase *objectEntityList = (EntityBase *)P6_LW_ENTITYLIST;
// P6.8 I3b.1 (camera-local pool shrink, RED-first): the logical->physical remap the pool
// SHRINK will populate non-trivially. Home = the VERIFIED-FREE cart slot 0x226B8000 (32 KB
// past the overlay window end 0x226B0000 = P6_OVL_BASE+P6_OVL_WINDOW, and 32 KB before the
// s_p6_shadow_inrange diag buffer 0x226C0000; GFS windows are at 0x22700000). CACHE-THROUGH
// alias so a read-mostly table needs no cache-coherency purge. I3b.1 keeps it IDENTITY
// (byte-identical -- resolve_ok stays 1) to de-risk the cart-read + the indirection at
// RUNTIME before the shrink makes it non-identity. p6_pool_remap_ready gates the read in
// SaturnSlotToPoolSlot -> returns the raw slot until the table is built (crash-safe regardless
// of init order; p6_pool_remap_init() runs first thing in p6_engine_boot_and_run).
#if defined(P6_FRONTEND_MENU)
// #325 lever (ii) -- FRONT-END ONLY: home the remap in CACHED WRAM-H .bss instead of the
// uncached A-bus cart. MEASURED (live _objprof.jsonl 2026-07-09): every RSDK_ENTITY_AT
// pays one uncached 16-bit cart read here; the FE loop1 walks ~460-590 slots/tick via
// RSDK_ENTITY_AT (Task #298 wide-remap contract) and every foreach_all() walks all 1216
// -- the Menu leg's loop1 was 5,958 FRT ticks/tick with only ~1,070 in Updates. Same
// table, same writers (p6_pool_remap_init + p6_ovl_pool_compact via the -R imported
// pointer), same semantics -- ONLY the backing store moves. 2,432 B .bss, FE ceiling
// GLOBALS 0x060C8000 (measured headroom ~39 KB at _end 0x060be670). Plain GHZ keeps the
// cart home (its WRAM-H ceiling is P6_HW_ANIMPAK with ~64 B headroom -> byte-identical).
static uint16 s_p6_pool_remap_wram[ENTITY_COUNT];
uint16 *p6_pool_remap        = s_p6_pool_remap_wram;
// Unmangled alias for the C overlay TU (p6_ovl_ghz.c compact/stream import it via -R;
// RSDK::p6_pool_remap itself is namespace-mangled). The pointer VALUE is set once here
// and never reassigned (grep-verified), so the alias cannot desync.
// __attribute__((used)) + the -u keep-root in build_p6scene_objs.sh (both halves
// required, per the TurboTurtle closure lesson) survive the pack gc so the overlay's
// -R import resolves.
extern "C" { __attribute__((used)) uint16 *p6_pool_remap_c = s_p6_pool_remap_wram; }
#else
uint16 *p6_pool_remap        = (uint16 *)0x226DC000u; // 1216 u16 = 2432 B. RELOCATED (DDWrecker
                                                     // re-budget 2026-07-11): moved 0x226B8000->
                                                     // 0x226DC000 to free 0x226B8000..0x226C0000 for
                                                     // the +0x8000 P6_OVL_WINDOW growth. New home in
                                                     // the 0x226DC000..0x226E0000 slack below the VDP1
                                                     // s_stage 0x226E0000 (GHZ resident cursor stops
                                                     // 0x22686900, never reaches here). MIRROR: the
                                                     // overlay hardcodes this same addr (p6_ovl_ghz.c).
#endif
int32   p6_pool_remap_ready  = 0;
// P6.8 I3b.2 (camera-local pool shrink): the PHYSICAL scene-slot count SaturnEntityAt/SaturnEntitySlot
// (Object.hpp) lay out. == SCENEENTITY_COUNT here -> the accessors stay BYTE-IDENTICAL (p6_i2_selfcheck's
// oracle p6_i2_direct uses the SCENEENTITY_COUNT constant, so resolve_ok stays 1). The pool SHRINK sets
// this < SCENEENTITY_COUNT (e.g. 640) ATOMICALLY with a non-identity p6_pool_remap + a resized backing.
int32   p6_pool_scene_phys   = SCENEENTITY_COUNT;
// P6.8 I3b 2b: the reserved classID=0 DUMMY physical slot every EMPTY logical slot remaps to after
// compaction (so RSDK_ENTITY_AT(empty L) is always a safe classID=0 read). -1 until compaction runs.
int32   p6_pool_dummy_slot   = -1;
// P6.8 I3b.2 (sub-step 2a): the PHYSICAL->logical inverse of p6_pool_remap. loop1 (Object.cpp
// ProcessObjects) iterates the PHYSICAL pool [0,RESERVE+sphys+TEMP) and recovers the LOGICAL slot
// for sceneInfo.entitySlot (-> drawGroups), the near bitfield index, the in-range list (loop2/3
// RSDK_ENTITY_AT it), and scan_always. IDENTITY here (inv[p]==p -> byte-identical); the SHRINK
// fills it non-trivially alongside p6_pool_remap. Cart home 0x226BC000 (1216 u16 = 2432 B, in the
// verified-free gap between p6_scan_always 0x226BB000 and s_p6_shadow_inrange 0x226C0000).
uint16 *p6_pool_remap_inv    = (uint16 *)0x226DCA00u; // RELOCATED 0x226BC000->0x226DCA00 (DDWrecker
                                                     // re-budget 2026-07-11; 2432 B ends 0x226DD380,
                                                     // < s_stage 0x226E0000). MIRROR in p6_ovl_ghz.c.
TileLayer *tileLayers        = (TileLayer *)P6_LW_TILELAYERS;
DataStorage *dataStorage     = (DataStorage *)P6_LW_DATASTORAGE;
// P6.7a: objectClassList/typeGroups/drawGroups are LIVE (ProcessObjects,
// Object.cpp:357-475, reads/writes all three every tick). Real WRAM-H .bss
// backings, sized by the P6.7a Title-scale data retarget (Object.hpp Saturn
// branch: ENTITY_COUNT 0xC0): classes 0x100*~68B = 17.4 KB, typeGroups
// 0x84*(0xC0*2+4) = 32 KB, drawGroups 16*~420B = 6.6 KB -- measured against
// the 171 KB diag-image margin (GHZ-scale memory map = P6.7b deliverable).
// W12b honest-accounting move (Task #227, 2026-06-12): with the orphan-
// section merge (p6_pack_merge.ld) the map finally charges these backings
// against the 0x060C0000 floor for real. objectClassList + drawGroups (and
// p6_shtRect below) move to the FIXED WRAM-H window 0x060D6000..0x060E0000
// (free gap between the wave-1 globals window end 0x060D5B74 and the W2
// packed-collision window 0x060E0000). NOT .bss -- p6_scene_run memsets the
// window before registration (the engine assumes zeroed class/draw lists).
// typeGroups (68,112 B) stays .bss: it exceeds every free fixed gap.
// W17 (Task #227, 2026-06-13): slid UP 0x060D6000 -> 0x060D8000 by the WRAM-H
// re-budget (the 0x8000 window now abuts the FIXED PACKEDCOL at 0x060E0000;
// used 28,224 B + 4,544 B headroom). GLOBALS below it ends at 0x060D7B74
// (1,164 B gap). The static_assert bound (0x060E0000) is unchanged.
#define P6_HW_GROUPWIN 0x060D8000u
#define P6_HW_GROUPWIN_OBJCLASS (P6_HW_GROUPWIN)
#define P6_HW_GROUPWIN_DRAWGRP (P6_HW_GROUPWIN_OBJCLASS + sizeof(ObjectClass) * OBJECT_COUNT)
#define P6_HW_GROUPWIN_SHTRECT (P6_HW_GROUPWIN_DRAWGRP + sizeof(DrawList) * DRAWGROUP_COUNT)
#define P6_HW_GROUPWIN_END (P6_HW_GROUPWIN_SHTRECT + 0x1000)
static TypeGroupList p6_typeGroupsBacking[TYPEGROUP_COUNT];
ObjectClass *objectClassList = (ObjectClass *)P6_HW_GROUPWIN_OBJCLASS;
TypeGroupList *typeGroups    = p6_typeGroupsBacking;
DrawList *drawGroups         = (DrawList *)P6_HW_GROUPWIN_DRAWGRP;
static_assert(P6_HW_GROUPWIN_END <= 0x060E0000u,
              "group window overruns the W2 packed-collision window");
// cameras/cameraCount normally live in Drawing.cpp (not a pack TU);
// ProcessObjects reads both (Object.cpp:377-390, 409-458). Zero cameras =
// every ACTIVE_BOUNDS entity stays out of range; the proof entity runs
// ACTIVE_NORMAL.
CameraInfo cameras[CAMERA_COUNT];
int32 cameraCount = 0;
// LIVE since P6.5b2: LoadSpriteAnimation hash-scans + fills these slots. The
// 28,672 B backing rides WRAM-H .bss -- the diag image has ~208 KB of margin
// since the P6SCENE park let LTO sweep the unreachable hand-port (the old
// 41,936 B pack ceiling is obsolete for this build; binding budget = diag
// _end vs the 0x060C0000 floor, asserted from game.map after every build).
// W11b map v7: moved to a WRAM-L window (was WRAM-H .bss) to keep _end
// under the 0x060C0000 overlay floor after the tilesetPixels move.
SpriteAnimation *spriteAnimationList = (SpriteAnimation *)P6_LW_SPRANIM;
// LIVE since P6.7c: LoadSceneFolder's inline Clear3DScenes (Scene3D.hpp:225)
// MEM_ZEROs all MODEL_COUNT (0x100) Model entries = 11,264 B -- through the
// old 256 B DEAD dummy it would have zeroed past the END of WRAM-L. Real
// window in map v5.
Model *modelList             = (Model *)P6_LW_MODELLIST;
// LIVE since P6.5a: LoadStageGIF points the decoder at this backing and the
// engine's ReadGifPictureData writes all 262,144 indexed pixels into it.
// W11b map v7: LOAD-PHASE TRANSIENT aliasing the entityList window. MEASURED
// (2026-06-11): NEITHER bank can hold a resident 256 KB tileset at the GHZ
// entity scale (WRAM-L over by ~190 KB with the 0x6B800 entityList; WRAM-H
// over by ~81 KB even after the jo-pool trim -- linked .bss tail reached
// 0x060DE244 vs the 0x060C0000 overlay floor). The ONLY runtime consumer on
// Saturn is the one-shot VDP2 cell upload (software DrawLayer is skipped;
// the split p6_vdp2_upload_cells runs BETWEEN LoadSceneFolder's GIF decode
// and LoadSceneAssets' entity placement, which then legally clobbers the
// window). DECLARED GAP: DrawTile-class object draws (BreakableWall debris)
// read tilesetPixels at runtime -- they get a witnessed seam with the FX
// wave (W5); no current diag path calls them.
uint8 *tilesetPixels         = (uint8 *)P6_LW_ENTITYLIST;
// LIVE since P6.7c: LoadSceneFolder calls LoadTileConfig (Scene.cpp:163-164,
// :733-880) which writes both arrays. Title has NO TileConfig.bin in the 1.03
// pack (MEASURED: hash absent; GHZ's is 2,620 B) so at Title the LoadFile
// fails at Scene.cpp:738 and the windows stay ZERO -- the qa_p6_stagecfg C6
// hashes assert exactly that (and flip to the byte-exact parse model the
// moment the diag scene is a stage WITH a TileConfig).
CollisionMask (*collisionMasks)[TILE_COUNT * COLLISION_FLIPCOUNT] =
    (CollisionMask (*)[TILE_COUNT * COLLISION_FLIPCOUNT])P6_LW_COLLMASKS;
TileInfo (*tileInfo)[TILE_COUNT * COLLISION_FLIPCOUNT] =
    (TileInfo (*)[TILE_COUNT * COLLISION_FLIPCOUNT])P6_LW_TILEINFO;
// P6.7 PACKED COLLISION (Task #210): the 16-bit/column packed masks at a
// fixed WRAM-H window inside the P6.7d.2-freed region -- 0x060E0000 +
// 0x10000 ends at 0x060F0000, 16 KB under the SGL-area floor 0x060F4000
// (globals window ends 0x060D5B74). With the Scene.cpp P6_CM arm,
// LoadTileConfig packs HERE and the raw x1 collisionMasks WRAM-L window
// above receives no writes (fully dead -- a future WRAM-L reclaim).
#define P6_HW_PACKEDCOL 0x060E0000u // 2 * 0x400 * 32 = 0x10000 -> 0x060F0000
uint16 (*packedCollisionMasks)[TILE_COUNT * TILE_SIZE] =
    (uint16 (*)[TILE_COUNT * TILE_SIZE])P6_HW_PACKEDCOL;

// ---- (b3) Small engine scalars (WRAM-H .bss, zero-init by SLSTART) -----------
int32 objectClassCount        = 0;
int32 globalObjectCount       = 0;
int32 editableVarCount        = 0;
int32 viewableVarCount        = 0;
EditableVarInfo *editableVarList = NULL; // written via AllocateStorage (Scene.cpp:470)
uint16 *tintLookupTable       = NULL;    // NULLed by LoadSceneFolder only; REV02 pointer form
ScreenInfo *currentScreen     = NULL;

// ---- Function stubs the Scene.cpp closure may still reference ----------------
// All are LoadSceneFolder-path-only (the pack is gc-rooted at p6_scene_run ->
// LoadSceneAssets, so these sections drop when unreferenced; kept for link
// robustness). ClearStageObjects is byte-equivalent to the real body at
// classCount==0 (Object.cpp:1256-1264: loop bound sceneInfo.classCount).
// ClearStageSfx + LoadSfx stubs REMOVED at P6.6a (real Audio.cpp defines
// them); ClearStageObjects + LoadStaticVariables stubs REMOVED at P6.7a
// (real Object.cpp defines them) -- false-stubs multiple-define, the same
// class as the P6.5a ImageGIF false-stub.
// ImageGIF::Load is REAL since P6.5a: Graphics_Sprite.o (the engine's own LZW
// GIF decoder, Sprite.cpp:202-267) is in the pack and provides both the key
// function and the _ZTVN4RSDK8ImageGIFE vtable. The P6.3-era false-stub that
// lived here would have shadowed it with a multiple-definition error.

// ---- P6.5b3: the Saturn render-device implementation of the engine's --------
// DrawSprite slot. Draw* is the DESIGNATED Saturn backend seam (Task #194
// spike: the software DrawSpriteFlipped raster is infeasible on SH-2 -- 11
// instr/px, 1.77x over budget -- so VDP1 IS the rasterizer). The body below
// is a mechanical mirror of the engine's Drawing.cpp:2670-2686 frame/position
// semantics; the FX_NONE arm mirrors :2783-2786's
//   DrawSpriteFlipped(pos+pivot, w, h, sprX, sprY, FLIP_NONE, ink, alpha, id)
// onto the jo-side VDP1 blitter. Object Draw callbacks (e.g. decomp Ring_Draw:
// `RSDK.DrawSprite(&self->animator, NULL, false)`) consume exactly this
// signature, so proving it proves the object-facing draw contract.
// W14c: the shared blit tail every DrawSprite arm lands on. Mirrors
// DrawSpriteFlipped's clip-accept (Drawing.cpp:2882-2905: a fully-off rect
// returns BEFORE validDraw) -- an accepted draw sets validDraw, which
// ProcessObjectDrawLists folds into entity->onScreen (Object.cpp:843).
// Partial overlap draws the full part; VDP1 clips at the framebuffer edge.
static void p6_draw_flipped(int32 x, int32 y, SpriteFrame *frame, int32 dir)
{
#if defined(P6_FRONTEND_TITLE)
    // RING-SONIC forensic (task #326): is the current draw the TitleSonic head?
    // sceneInfo.entity is the entity ProcessObjectDrawLists is dispatching. Match
    // its classID to TitleSonic's classID -- which the OVERLAY publishes into
    // p6_w_tsonic_classid (the TitleSonic global lives in Game_TitleSonic.o, which
    // links into the OVERLAY, NOT this main image; so we cannot dereference it here,
    // only compare the classID the overlay latched). classid<=0 == not yet known.
    int32 p6_sonic_curr = 0;
    if (p6_w_tsonic_classid > 0 && sceneInfo.entity
        && sceneInfo.entity->classID == (uint16)p6_w_tsonic_classid)
        p6_sonic_curr = 1;
    if (p6_sonic_curr) ++p6_w_tsonic_drawcalls;
#endif
    if (x + frame->width <= currentScreen->clipBound_X1 || x >= currentScreen->clipBound_X2
        || y + frame->height <= currentScreen->clipBound_Y1 || y >= currentScreen->clipBound_Y2) {
#if defined(P6_FRONTEND_TITLE)
        if (p6_sonic_curr) ++p6_w_tsonic_drawclip;
#endif
        return;
    }
    validDraw = true;
#if defined(P6_FRONTEND_TITLE)
    if (p6_sonic_curr) {
        p6_w_tsonic_drawhandle = p6_vdp1HandlesInit
                                 ? p6_vdp1HandleBySurface[frame->sheetID] : -1;
        // Latch the HEAD (first sonic blit of the frame; emitseq still -9) coords +
        // its frame w/h; the finger (later) would otherwise overwrite these.
        if (p6_w_tsonic_emitseq < 0) {
            p6_w_tsonic_headx = x;
            p6_w_tsonic_heady = y;
            p6_w_tsonic_headw = frame->width;
            p6_w_tsonic_headh = frame->height;
        }
        p6_w_tsonic_drawx = x;
        p6_w_tsonic_drawy = y;
    }
#endif
#if defined(P6_GHZCUT_BOOT)
    // #311 mechanism 6 (the original "fxAnimator/-1 handling"): an anim whose
    // sheet was UNSTAGED at load carries sheetID -1 (mech-2 contract) -> the
    // uint8 frame field wraps to 255 -> HandleBySurface[255] is an OOB read
    // past the int8[SURFACE_COUNT=64] array that LANDS ON 0 == the GHCOBJ
    // handle -> HUD digits / placed Rings / player Fan frames sampled the claw
    // sheet at foreign coords (MEASURED: every "claw garble" ring rect matched
    // Global/HUD.bin + Global/Ring.bin + Players/*.bin Fan frames, none matched
    // Claw.bin/Platform.bin; the solid-white sheet regions drew the BLACK
    // rects). Saturn-only state (PC LoadSpriteSheet always succeeds) -> drop
    // the draw cleanly + count it. Proper render of those objects = #311b
    // (stage ITEMS.SHT/DISPLAY.SHT + fan-frame coverage).
    if (frame->sheetID >= SURFACE_COUNT) {
        ++p6_w_draw_wrap255;
        return;
    }
#endif
    {
        int32 hh = p6_vdp1HandlesInit ? p6_vdp1HandleBySurface[frame->sheetID] : -1;
        if (hh < 0 && frame->sheetID < 16)
            ++p6_w_dropbysheet[frame->sheetID]; /* W18 unbound-surface drop histogram */
    }
#if defined(P6_FRONTEND_TITLE) && defined(P6_TITLE_INK)
    // CP5b.4 (Task #272): map INK_BLEND (Mountain2) + INK_ADD (Reflection/Water-
    // Sparkle, alpha 0x80) to VDP1 CL_Trans half-transparency. The blit inherits the
    // sticky jo attribute; clear it after so opaque sprites (logo/Sonic) are
    // unaffected. INK_MASKED already early-returns; INK_ALPHA/SUB/TINT unused by the
    // Title objects (opaque fallback). GHZ compiles this out (byte-identical).
    // Behind P6_TITLE_INK while A/B-isolating whether the sticky jo half-transparency
    // attribute leaks onto the FG (the head) -- the measured head regression.
    int32 p6_inkHalf = 0;
    if (sceneInfo.entity) {
        int32 ie = sceneInfo.entity->inkEffect;
        p6_inkHalf = (ie == INK_BLEND || ie == INK_ADD) ? 1 : 0;
    }
    if (p6_inkHalf) p6_vdp1_set_ink(1);
#endif
#if defined(P6_FRONTEND_MENU)
    { unsigned int _dvb0 = p6_perf_vbl_count;
#endif
    p6_vdp1_blit_flipped(p6_vdp1HandlesInit ? p6_vdp1HandleBySurface[frame->sheetID] : -1,
                         x, y, frame->width, frame->height, frame->sprX, frame->sprY,
                         (dir & FLIP_X) ? 1 : 0, (dir & FLIP_Y) ? 1 : 0);
#if defined(P6_FRONTEND_MENU)
      p6_w_draw_blit_v += (int)(p6_perf_vbl_count - _dvb0); }
#endif
#if defined(P6_FRONTEND_TITLE) && defined(P6_TITLE_INK)
    if (p6_inkHalf) p6_vdp1_set_ink(0);
#endif
    p6_w_draw_xy      = ((x & 0xFFFF) << 16) | (y & 0xFFFF);
    p6_w_draw_rect    = ((int32)frame->sprX << 16) | (int32)frame->sprY;
    p6_w_draw_sheetid = (int32)frame->sheetID;
    ++p6_w_draw_calls;
#if defined(P6_FRONTEND_TITLE)
    // EMIT-ORDER forensic (task #326): latch the FIRST sonic-head blit and the FIRST
    // TitleLogo (emblem) blit of THIS frame. p6_frontend_frame resets both to -9 just
    // before ProcessObjectDrawLists, so "the first blit whose witness is still -9"
    // captures the per-frame emit order. On Saturn VDP1 the LAST-emitted command is in
    // FRONT: emblem_emitseq > tsonic_emitseq == the black disc paints OVER the head.
    if (p6_sonic_curr && p6_w_tsonic_emitseq < 0)
        p6_w_tsonic_emitseq = p6_w_draw_calls;
    if (p6_w_titlelogo_classid > 0 && sceneInfo.entity
        && sceneInfo.entity->classID == (uint16)p6_w_titlelogo_classid
        && p6_w_emblem_emitseq < 0)
        p6_w_emblem_emitseq = p6_w_draw_calls;
    p6_w_frame_maxemit = p6_w_draw_calls; /* every blit -> last one wins == frame max */
#endif
#if defined(P6_GHZ_AUTORUN)
    // Signpost campaign: attribute this dispatch to the player when the draw
    // callback's current entity is SLOT_PLAYER1 (sceneInfo.entity is set by
    // ProcessObjectDrawLists per entity, Object.cpp draw loop).
    if (sceneInfo.entity == (Entity *)RSDK_ENTITY_AT(0))
        ++p6_w_plr_draws;
#endif
}

#if defined(P6_FRAMEDIR)
// C1 identification (2026-07-11): the residual GHZ per-frame inflate is a
// blue-sparkle draw from store slot 19 (Global/Shields.gif) whose rects no anim
// .bin declares -> FRD miss -> banded re-inflate. SaturnSheet_FetchRect calls
// this at the slot-19 banded-inflate site; sceneInfo.entity IS the drawing
// entity there (the fetch is synchronous inside its Draw dispatch). Record the
// classID(s) + last position so a live read names the object. Diagnostic only,
// P6_FRAMEDIR-gated (chain flavor) -> plain GHZ .bss byte-identical (#228 safe).
__attribute__((used)) int32 p6_w_slot19_class  = -1; // 1st distinct classID drawing slot 19
__attribute__((used)) int32 p6_w_slot19_class2 = -1; // 2nd distinct classID (if it varies)
__attribute__((used)) int32 p6_w_slot19_x      = 0;  // last drawing entity world x (px)
__attribute__((used)) int32 p6_w_slot19_y      = 0;  // last drawing entity world y (px)
__attribute__((used)) int32 p6_w_slot19_hits   = 0;  // total slot-19 fetches attributed
extern "C" void p6_frd_note_fetch(int32 slot)
{
    if (slot != 19)
        return;
    Entity *e = sceneInfo.entity;
    if (!e)
        return;
    int32 c = (int32)e->classID;
    if (p6_w_slot19_class < 0)
        p6_w_slot19_class = c;
    else if (c != p6_w_slot19_class && p6_w_slot19_class2 < 0)
        p6_w_slot19_class2 = c;
    p6_w_slot19_x = e->position.x >> 16;
    p6_w_slot19_y = e->position.y >> 16;
    ++p6_w_slot19_hits;
}
#endif

void DrawSprite(Animator *animator, Vector2 *position, bool32 screenRelative)
{
    if (animator && animator->frames) {
        SpriteFrame *frame = &animator->frames[animator->frameID]; // Drawing.cpp:2673
#if defined(P6_FRONTEND_TITLE)
        // CP5b.3 (Task #272): INK_MASKED is a chroma-KEY REVEAL (Drawing.cpp:
        // DrawSpriteMasked draws ONLY pixels where the framebuffer already holds
        // maskColor) -- it CANNOT render on the opaque Saturn VDP1 CLUT path, and
        // drawing it OPAQUE produces solid garbage rectangles. The only INK_MASKED
        // user in the Title scene is TitleBG WINGSHINE (the diagonal purple/blue
        // stripe sheet, anim 4) -- which the user explicitly rejected as "the purple
        // and blue striped boxes". SKIPPING it is both correct (an unrenderable
        // effect) AND matches the user's directive. GUARDED to P6_FRONTEND_TITLE so
        // the DEFAULT (GHZ) DrawSprite stays byte-identical (#228 _end budget).
        // (INK_BLEND/INK_ADD still draw opaque -- reported.)
        if (sceneInfo.entity && sceneInfo.entity->inkEffect == INK_MASKED)
            return;
#endif
        Vector2 pos;
        if (!position)
            pos = sceneInfo.entity->position; // Drawing.cpp:2676
        else
            pos = *position;

        pos.x >>= 0x10; // Drawing.cpp:2680-2681 (world fixed-point -> px)
        pos.y >>= 0x10;
        if (!screenRelative) { // Drawing.cpp:2682-2685
            pos.x -= currentScreen->position.x;
            pos.y -= currentScreen->position.y;
#if defined(P6_FRONTEND_MENU)
            // Saturn-native 320 layout (MEASURED, data-driven 2026-06-25): the Mania main
            // menu is authored 424-wide. On the working build the rows land (world-origin
            // 692,264) at screen ManiaMode(64,94)/TimeAttack(256,94)/Competition(64,156) --
            // the right column (x=256) + its ~148px label spills past 320 (qa_menu_layout
            // L2 RED). A uniform shift can't fix it (trades right-clip for left-clip); the
            // x-SPREAD must compress. Squeeze X toward screen-centre 160 (= the active
            // "Main Menu" control's screen origin) by P6_MENU_XSQUEEZE/256. f<=0.896 fits
            // (160+96f+74<=320); 205/256=0.80 leaves margin (TimeAttack 256->236 label end
            // 310<=320; ManiaMode 64->83 label start 9>=0). Y unchanged (no vertical clip).
            // MENU flavor only (boots directly to the Menu scene); NOT the engine scroll
            // force (that fought UIControl_Draw + was bundled with the crashed plate path).
            pos.x = 160 + (((pos.x - 160) * P6_MENU_XSQUEEZE) >> 8);
#endif
        }

        // W14c (Task #227): mirror Drawing.cpp:2687-2781 -- the FX_ROTATE
        // normalization that DEGRADES drawFX to the flip arm when the
        // resolved rotation is 0 (the Player carries FX_ROTATE|FX_FLIP
        // permanently, Player_Create; at rotation 0 the PC build draws it
        // through DrawSpriteRotozoom's identity == the flipped path).
        int32 rotation = sceneInfo.entity->rotation;
        int32 drawFX   = sceneInfo.entity->drawFX;
        if (sceneInfo.entity->drawFX & FX_ROTATE) {
            switch (animator->rotationStyle) {
                case ROTSTYLE_NONE:
                    rotation = 0;
                    if ((sceneInfo.entity->drawFX & FX_ROTATE) != FX_NONE)
                        drawFX ^= FX_ROTATE;
                    break;

                case ROTSTYLE_FULL:
                    rotation = sceneInfo.entity->rotation & 0x1FF;
                    if (rotation == 0)
                        drawFX ^= FX_ROTATE;
                    break;

                case ROTSTYLE_45DEG:
                    rotation = (sceneInfo.entity->rotation + 0x20) & 0x1C0;
                    if (rotation == 0)
                        drawFX ^= FX_ROTATE;
                    break;

                case ROTSTYLE_90DEG:
                    rotation = (sceneInfo.entity->rotation + 0x40) & 0x180;
                    if (rotation == 0)
                        drawFX ^= FX_ROTATE;
                    break;

                case ROTSTYLE_180DEG:
                    rotation = (sceneInfo.entity->rotation + 0x80) & 0x100;
                    if (rotation == 0)
                        drawFX ^= FX_ROTATE;
                    break;

                case ROTSTYLE_STATICFRAMES:
                    // Drawing.cpp:2721-2776 (Player rolling/tube frames).
                    if (sceneInfo.entity->rotation >= 0x100)
                        rotation = 0x08 - ((0x214 - sceneInfo.entity->rotation) >> 6);
                    else
                        rotation = (sceneInfo.entity->rotation + 20) >> 6;

                    switch (rotation) {
                        case 0:
                        case 8:
                            rotation = 0x00;
                            if ((sceneInfo.entity->drawFX & FX_SCALE) != FX_NONE)
                                drawFX ^= FX_ROTATE;
                            break;
                        case 1:
                            rotation = 0x80;
                            frame += animator->frameCount;
                            if (sceneInfo.entity->direction)
                                rotation = 0x00;
                            break;
                        case 2: rotation = 0x80; break;
                        case 3:
                            rotation = 0x100;
                            frame += animator->frameCount;
                            if (sceneInfo.entity->direction)
                                rotation = 0x80;
                            break;
                        case 4: rotation = 0x100; break;
                        case 5:
                            rotation = 0x180;
                            frame += animator->frameCount;
                            if (sceneInfo.entity->direction)
                                rotation = 0x100;
                            break;
                        case 6: rotation = 0x180; break;
                        case 7:
                            rotation = 0x180;
                            frame += animator->frameCount;
                            if (!sceneInfo.entity->direction)
                                rotation = 0;
                            break;
                        default: break;
                    }
                    break;

                default: break;
            }
        }
        (void)rotation; // consumed by the FX_ROTATE arms (Phase Z FIXME below)

        switch (drawFX) {
            case FX_NONE:
                // Drawing.cpp:2785. inkEffect/alpha: INK_NONE -> opaque,
                // matching VDP1 CLUT-mode draw.
                // FIXME Phase Z: INK_ALPHA/ADD/SUB via VDP1 color calculation.
                p6_draw_flipped(pos.x + frame->pivotX, pos.y + frame->pivotY, frame, FLIP_NONE);
                break;

            case FX_FLIP:
                // Drawing.cpp:2789-2812 -- the world top-left is flip-
                // adjusted PER SPRITE (the Audit-3 composite rule):
                // FLIP_X x = pos.x - width - pivotX.
                switch (sceneInfo.entity->direction) {
                    case FLIP_NONE:
                        p6_draw_flipped(pos.x + frame->pivotX, pos.y + frame->pivotY, frame, FLIP_NONE);
                        break;
                    case FLIP_X:
                        p6_draw_flipped(pos.x - frame->width - frame->pivotX, pos.y + frame->pivotY, frame, FLIP_X);
                        break;
                    case FLIP_Y:
                        p6_draw_flipped(pos.x + frame->pivotX, pos.y - frame->height - frame->pivotY, frame, FLIP_Y);
                        break;
                    case FLIP_XY:
                        p6_draw_flipped(pos.x - frame->width - frame->pivotX,
                                        pos.y - frame->height - frame->pivotY, frame, FLIP_XY);
                        break;
                }
                break;

            default:
#if defined(P6_DIRECT_VDP1)
                // Fix 1 (user-symptom-map-v2 "Sonic invisible on slopes/ramps"):
                // FX_ROTATE and FX_ROTATE|FX_FLIP with resolved rotation != 0
                // (ROTSTYLE_FULL slope draws; also the snapped 45/90/180/
                // STATICFRAMES styles). Mirrors Drawing.cpp:2815-2823 --
                // DrawSpriteRotozoom(pos, pivot, ..., 0x200, 0x200,
                // direction & FLIP_X, rotation, ...) at identity scale -- onto
                // the VDP1 DISTORTED SPRITE emit (ST-013-R3 sec 7.6) via
                // p6_vdp1_blit_rot. FX_SCALE combos stay undrawn (FIXME Phase Z:
                // needs the scaled-part path; no GHZ-chain object draws scaled
                // yet -- Drawing.cpp:2826-2848).
                if (!(drawFX & FX_SCALE)) {
                    // Decomp masks direction to FLIP_X for rotated draws
                    // (Drawing.cpp:2822); plain FX_ROTATE draws unflipped (:2817).
                    int32 rotFlipX = (drawFX & FX_FLIP)
                                     ? (sceneInfo.entity->direction & FLIP_X) : 0;
                    int32 sn = Sin512(rotation);  // Math.hpp:72 (baked P6.1-fast tables)
                    int32 cs = Cos512(rotation);  // Math.hpp:73
                    // Conservative clip-accept: the rotated quad's corner radius
                    // from pos is bounded by max|dx| + max|dy| (|cos|,|sin| <= 512
                    // and |dx*cos - dy*sin| <= 512*(|dx|+|dy|), >>9). Mirrors the
                    // DrawSpriteFlipped clip-reject (Drawing.cpp:2882-2905) so a
                    // fully-off entity still skips validDraw; VDP1 pre-clipping
                    // (ST-013-R3 sec 7.6) trims any partial overlap.
                    int32 ax1 = frame->pivotX, ax2 = frame->pivotX + frame->width;
                    int32 ay1 = frame->pivotY, ay2 = frame->pivotY + frame->height;
                    int32 rad = ((ax1 < 0 ? -ax1 : ax1) > (ax2 < 0 ? -ax2 : ax2)
                                     ? (ax1 < 0 ? -ax1 : ax1) : (ax2 < 0 ? -ax2 : ax2))
                              + ((ay1 < 0 ? -ay1 : ay1) > (ay2 < 0 ? -ay2 : ay2)
                                     ? (ay1 < 0 ? -ay1 : ay1) : (ay2 < 0 ? -ay2 : ay2));
                    if (pos.x + rad <= currentScreen->clipBound_X1
                        || pos.x - rad >= currentScreen->clipBound_X2
                        || pos.y + rad <= currentScreen->clipBound_Y1
                        || pos.y - rad >= currentScreen->clipBound_Y2)
                        break;
#if defined(P6_GHZCUT_BOOT)
                    // #311 mech-6 twin: an unstaged sheet's sheetID -1 wraps the
                    // uint8 to 255 -> OOB handle read (see p6_draw_flipped).
                    if (frame->sheetID >= SURFACE_COUNT) {
                        ++p6_w_draw_wrap255;
                        break;
                    }
#endif
                    validDraw = true;
                    p6_vdp1_blit_rot(p6_vdp1HandlesInit
                                         ? p6_vdp1HandleBySurface[frame->sheetID] : -1,
                                     pos.x, pos.y, frame->width, frame->height,
                                     frame->sprX, frame->sprY,
                                     frame->pivotX, frame->pivotY, sn, cs, rotFlipX);
                }
#else
                // FIXME P6.5b4+ (non-direct SGL flavors only -- plain-GHZ QA
                // parity build stays byte-identical): FX_ROTATE via VDP1
                // distorted parts lands with P6_DIRECT_VDP1 (the shipping
                // chain, build_shipping.sh:172); FX_SCALE same.
#endif
                break;
        }
    }
}

// ---- P6.6a: the Saturn audio-device Init (the engine-canonical entry every
// port's device class provides; SaturnAudioDevice.hpp declares it). It runs
// the engine's OWN AudioDeviceBase::InitAudioChannels (Audio.cpp:164-182 --
// channel reset to soundID=-1/IDLE, interpolation lookup fill, stream-slot
// reservation from DATASET_MUS). Protected in the base; this derived static
// member is the designated access path, mirroring the NX/SDL2 device shape.
// The SCSP-audible half (key-on from the engine channels[] state) is P6.6b.
bool32 AudioDevice::Init()
{
    // Qualified: SaturnAudioDevice.hpp:34 re-declares a private
    // InitAudioChannels on the DERIVED class (never defined), which hides
    // the base's -- the unqualified call linked to the phantom (measured
    // undefined-ref on the first P6.6a image link). The ENGINE body is
    // AudioDeviceBase::InitAudioChannels (Audio.cpp:164-182).
    AudioDeviceBase::InitAudioChannels();
    return true;
}

// ---- P6.6c: engine global + the Saturn stream device ------------------------
// PlayStream:250 reads engine.streamsEnabled. NO-CTORS TRAP (Text.cpp class):
// RetroEngine has a user ctor + in-class member inits -> the dynamic
// initializer NEVER RUNS under SLSTART, so every field is ZERO including
// streamsEnabled. The run body sets the fields the linked closure reads
// EXPLICITLY before calling PlayStream.
RetroEngine engine;

// The Saturn body of the device stream seam (SaturnAudioDevice.hpp declares
// it; PlayStream:295 calls it after arming the channel + sprintf-ing
// streamFilePath, Audio.cpp:276-293). Saturn-fit per bgm-loops-hand-curated
// + saturn-cdda-cue-format: OGG decode is not real-time-feasible on SH-2;
// BGM is CD AUDIO. Map the requested stream name to its CUE track (track 2 =
// GreenHill1, track 3 = TitleScreen -- build.bat:175-186 track identities)
// and start hardware CD-DA through the proven jo_audio_play_cd_track path.
// On success, perform the SAME state transition the PC LoadStream does on
// vorbis-open success (Audio.cpp:239: channel->state = CHANNEL_STREAM);
// unknown names fall through to the engine's own failure path (:244-245).
void AudioDevice::HandleStreamLoad(ChannelInfo *channel, bool32 async)
{
    (void)async; // CD-DA start is non-blocking; no async split needed

    // basename of "Data/Music/<name>"
    const char *base = streamFilePath;
    for (const char *p = streamFilePath; *p; ++p) {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }

    int track = -1;
    if (!strcmp(base, "TitleScreen.ogg"))
        track = 3;
    else if (!strcmp(base, "GreenHill1.ogg"))
        track = 2;
    // 2026-07-17 fly-in-audio fix (user symptom "no audio during flyin animation
    // scene"): MEASURED at the AIZ fly-in (_flyin_audio.mcs): streamFilePath ==
    // "Data/Music/AngelIsland.ogg" (the engine requested the CORRECT decomp
    // track) but this map had no entry -> track=-1 -> p6_cdda_play never ran ->
    // SILENCE while the channel still read "armed" (the str_state proxy lie).
    // The four AIZ-cutscene tracks now ship as CUE tracks 4-7 (build_cdda args
    // in the build_shipping.sh header + qa_live.ps1; loop curation in
    // tools/loops.json; decomp cites: AIZSetup.c:188 RubyPresence, :372/:724
    // TRACK_HBHMISCHIEF, :568/:809 TRACK_EGGMAN1 = "BossEggman1.ogg" Music.c:55).
    else if (!strcmp(base, "AngelIsland.ogg"))
        track = 4;
    else if (!strcmp(base, "RubyPresence.ogg"))
        track = 5;
    else if (!strcmp(base, "HBHMischief.ogg"))
        track = 6;
    else if (!strcmp(base, "BossEggman1.ogg"))
        track = 7;
    // FIXME P6.7+: table-ize from tools/loops.json as zones come online
    // (bgm-loops-hand-curated: every shipped BGM needs an acknowledged entry).

    p6_w_str_track = track;
    if (track > 0) {
        p6_cdda_play(track, streamLoopPoint != 0 || channel->loop);
        channel->state = CHANNEL_STREAM; // mirror LoadStream:239
    }
}
} // namespace RSDK

// ---- P6.7a/P6.7d.3: flat-Ring surface + function-table bridges ----------------
// The verbatim p6_ring2.cpp dispatches RSDK.* through these bridges, which
// route through the engine's OWN RSDKFunctionTable[] (populated by the real
// SetupFunctionTables, Core/Link.cpp -- the P6.1-proven path). DrawSprite's
// slot now carries the REAL Saturn backend (P6.5b3), so the engine-looped
// ring is VISIBLE.
//
// P6.7d.3: the Ring TUs are NO LONGER pack members -- they live in the
// fixed-base OVERLAY (cd/OVLRING.BIN, linked at P6_OVL_BASE against this
// image via ld -R), chain-loaded at boot. The main image names ZERO overlay
// symbols: everything routes through the entry pointer + the api vtable.
// The p6_w_obj_* witnesses are therefore DEFINED here (gates read them from
// game.map) and the overlay writes them via the -R import.
#include "p6_ovl_api.h"
extern "C" {
__attribute__((used)) int32 p6_w_obj_classcount = 0;  // engine classCount after registration
__attribute__((used)) int32 p6_w_obj_classid    = 0;  // entity classID at capture
__attribute__((used)) int32 p6_w_obj_timer      = 0;  // verbatim LostFX ++timer
__attribute__((used)) int32 p6_w_obj_vely       = 0;  // velocity.y == 0x1800 * timer
__attribute__((used)) int32 p6_w_obj_posy       = 0;  // y0 + 0x1800 * timer*(timer+1)/2
__attribute__((used)) int32 p6_w_obj_scalex     = 0;  // 0x10 * timer
__attribute__((used)) int32 p6_w_obj_frameid    = 0;  // entity animator frame
__attribute__((used)) int32 p6_w_obj_draws      = 0;  // Ring_Draw dispatches
__attribute__((used)) int32 p6_w_obj_spawns     = 0;  // engine respawns
// P6.7d.3 (qa_p6_overlay.py): overlay chain-load witnesses.
__attribute__((used)) int32 p6_w_ovl_bytes    = -1; // GFS bytes loaded into the window
__attribute__((used)) int32 p6_w_ovl_hash     = 0;  // djb2 over the loaded bytes (on SH-2)
__attribute__((used)) int32 p6_w_ovl_classes  = -1; // objectClassCount after the entry ran
__attribute__((used)) int32 p6_w_ovl_updatefn = 0;  // Ring_Update ptr the entry returned
// P6.8 I3b increment 2a: the OFFLINE dormant placement store (cd/<TAG>DORM.BIN, build_dormant_store.py,
// big-endian) chain-loaded to cart as DATA. The increment-2 materialize reads it to re-create far
// entities. The capture is offline (the runtime form was #228-WRAM-H-blocked; the ANIMPAK reclaim
// 609ce2d freed 1616 B so this load + 2b fit). Cart home 0x226C8000 (verified-free).
__attribute__((used)) int32 p6_w_dorm_bytes   = -1; // GFS bytes of the dormant store (>0 = loaded)
__attribute__((used)) int32 p6_w_dorm_magic   = 0;  // 'P6DM' 0x4D443650 if the store header parsed
__attribute__((used)) int32 p6_w_dorm_slots   = -1; // slot_count from the store header (the index size)
// P6.8 I3b increment 2b: the MATERIALIZE WRITE side -- p6_materialize_one reconstructs a scene
// entity from the cart DORM store into a scratch slot (mirrors Scene.cpp:585-806), proving the
// runtime write mechanism (serialize() offsets + LE var-replay to the right field) on real HW
// BEFORE any pool resize. Witnesses gated by qa_p6_materialize_write.py (read back in-function).
__attribute__((used)) int32 p6_w_mat_slot    = -1; // logical slot materialized (the gate decodes it)
__attribute__((used)) int32 p6_w_mat_classid = -1; // resolved stage classID (!=0 = registered)
__attribute__((used)) int32 p6_w_mat_classcount = -1; // sceneInfo.classCount AT CALL TIME (proves the class tables are populated)
__attribute__((used)) int32 p6_w_mat_posx    = 0;  // materialized position.x >> 16 (tiles)
__attribute__((used)) int32 p6_w_mat_posy    = 0;  // materialized position.y >> 16
__attribute__((used)) int32 p6_w_mat_nvars   = -1; // editable-var count of the object
__attribute__((used)) int32 p6_w_mat_nmatch  = -1; // var-hashes matched in editableVarList
__attribute__((used)) int32 p6_w_mat_v0      = 0;  // first 4 replayed var values (read back via offset)
__attribute__((used)) int32 p6_w_mat_v1      = 0;
__attribute__((used)) int32 p6_w_mat_v2      = 0;
__attribute__((used)) int32 p6_w_mat_v3      = 0;
// P6.7 wave-1 (qa_p6_globals.py): game-globals + link-layer witnesses.
__attribute__((used)) int32 p6_w_glb_size   = -1;  // sizeof(GlobalVariables) the game registered
__attribute__((used)) int32 p6_w_glb_ptr    = 0;   // where globalVarsPtr landed
__attribute__((used)) int32 p6_w_w1_locale  = -1;  // (Localization->loaded << 8) | language
// P6.7 packed collision (qa_p6_collision.py): packer + accessor witnesses.
__attribute__((used)) int32 p6_w_col_loaded     = 0;  // LoadTileConfig(GHZ) returned
__attribute__((used)) int32 p6_w_col_packedhash = 0;  // djb2 over the 65,536 B packed window
__attribute__((used)) int32 p6_w_col_infohash   = 0;  // djb2 over the 10,240 B tileInfo window
__attribute__((used)) int32 p6_w_col_probes     = -1; // accessor probes matched (exp 128)
__attribute__((used)) int32 p6_w_col_firstbad   = -1; // first mismatching probe index
// #249/#250 collision-geometry corruption PIN (qa_p6_collgeom RED). The lean boot
// skips the diag block's step-9 hash, so these are the ONLY 0x060E0000 witnesses
// in the shipping path. t1 = djb2 of packedCollisionMasks RIGHT AFTER the gameplay
// LoadSceneFolder->LoadTileConfig (the pack); nowhash = the live djb2 at the latest
// frame; badframe = the FIRST continuous frame the live hash diverges from the
// GROUNDED golden 0x643A3A5D. t1 != golden => packed wrong at load (resident
// pre-inflate disturbs the pack). t1 == golden && badframe >= 0 => packed RIGHT,
// overwritten at frame `badframe` (a per-frame loop writer, not the packer).
__attribute__((used)) int32 p6_w_col_t1hash   = 0;   // 0x060E0000 djb2 after gameplay LoadTileConfig
__attribute__((used)) int32 p6_w_col_nowhash  = 0;   // 0x060E0000 djb2 at the latest frame
__attribute__((used)) int32 p6_w_col_badframe = -1;  // first frame live hash != golden (-1 = never)
// P6.7 W11a layout band store (qa_p6_layout.py).
__attribute__((used)) int32 p6_w_lay_bytes    = -1;  // GFS bytes of cd/GHZ1LAYT.BIN
__attribute__((used)) int32 p6_w_lay_hash     = 0;   // djb2 over the loaded band store
__attribute__((used)) int32 p6_w_lay_probes   = -1;  // windowed-accessor probes matched
__attribute__((used)) int32 p6_w_lay_firstbad = -1;  // first mismatching probe index
// FG-tile-mutation piece 1 RED gate (qa_p6_fg_settile.py): SetTile->GetTile round
// trip on a streamed FG tile. -1 = not run; 0 = RED (SetTile dropped, GetTile
// unchanged); 1 = GREEN (GetTile reflected the write, then restored). Encodes:
// (orig<<16)|(seenAfterSet&0xFFFF) in p6_w_lay_settile_rt for forensics.
__attribute__((used)) int32 p6_w_lay_settile_ok = -1;
__attribute__((used)) int32 p6_w_lay_settile_rt = 0;
// P6.7 W16 (Task #228, qa_p6_scroll.py): GHZ FG Low on NBG1, camera-anchored.
__attribute__((used)) int32 p6_w_scr2_x       = ~0;  // scroll written (== screens[0].position.x)
__attribute__((used)) int32 p6_w_scr2_y       = ~0;  // scroll written (== screens[0].position.y)
__attribute__((used)) int32 p6_w_scr2_pndhash = 0;   // djb2 over the 16,384 B NBG1 map READ BACK from VDP2 VRAM
__attribute__((used)) int32 p6_w_scr2_nblank  = -1;  // non-empty FG tiles in the visible 320x224 window
__attribute__((used)) int32 p6_w_scr2_done    = 0;   // 1 == GHZ FG present ran (gates the Title NBG1 present off)
}

// The api block + the registration thunk the entry calls back through (the
// flat-TU rule: overlay code names no C++ engine symbols; pointers only).
static p6_ovl_api s_ovl;
extern "C" void p6_ovl_register_object(void **staticVars, const char *name,
                                       unsigned entityClassSize,
                                       unsigned staticClassSize,
                                       void (*update)(void), void (*draw)(void))
{
    RSDK::RegisterObject((RSDK::Object **)staticVars, name, entityClassSize,
                         staticClassSize, update, NULL, NULL, draw,
                         NULL, NULL, NULL, NULL, NULL);
}

// O1 (Task #254): FULL-callback overlay registration thunk for verbatim objects
// that need Create/StageLoad (Spring). Mirrors the resident RSDK_REGISTER_OBJECT
// REV02/non-REV0U arm (GameLink.h:1799): NULL editorLoad/editorDraw, NULL trailing.
extern "C" void p6_ovl_register_object_full(void **staticVars, const char *name,
                                            unsigned entityClassSize,
                                            unsigned staticClassSize,
                                            void (*update)(void), void (*lateUpdate)(void),
                                            void (*staticUpdate)(void), void (*draw)(void),
                                            void (*create)(void *), void (*stageLoad)(void),
                                            void (*serialize)(void))
{
    RSDK::RegisterObject((RSDK::Object **)staticVars, name, entityClassSize,
                         staticClassSize, update, lateUpdate, staticUpdate, draw,
                         create, stageLoad, NULL, NULL, serialize);
}

// =============================================================================
// P6.7 wave-1 (Task #210): the GAME GLOBALS window + RegisterGlobalVariables
// SEAM. The game's InitGameLogic registers GlobalVariables through the
// function table; the engine's own RegisterGlobalVariables (RetroEngine.hpp
// :736-741) backs it with AllocateStorage(DATASET_STG) -- but the Saturn
// DATASET_STG pool is 64 KB (Storage.cpp proof-trim) and already carries
// ~13 KB of scene-list weight, so the 56,180 B SATURN_GLOBALS_RETARGET
// struct cannot live there. The seam (HandleStreamLoad pattern, P6.6c)
// overrides the TABLE SLOT after SetupFunctionTables: same 2-arg REV02
// signature (this engine builds RETRO_REVISION=2 -- build_p6scene_objs.sh
// CORE_DEFS beats RetroEngine.hpp:227's #ifndef default), globals backed by
// the fixed window inside the P6.7d.2-freed region instead (0x060C8000 =
// P6_OVL_BASE + P6_OVL_WINDOW; budget runs to the SGL area floor
// 0x060F4000 -- 56,180 of 180,224 B used; the P6.8 zone-code window shares
// the remainder, see SaturnMemoryMap.h). The memset mirrors
// AllocateStorage's clearMemory=true; globalVarsPtr flows exactly as the
// engine inline would set it, and LoadGameConfig's REV02 seed loop then
// writes through the RETRO_SATURN offset-remap arm (RetroEngine.cpp +
// generated SaturnGlobalsMap.inc).
// =============================================================================
// #258: GLOBALS is DECOUPLED from P6_OVL_BASE. The overlay relocated to the CART
// (P6_OVL_BASE = 0x02690000, p6_ovl_api.h) but the engine globals MUST stay in
// fast WRAM-H -- so this is now the fixed address the OVL+WINDOW math used to yield
// (0x060C7600 + 0x2A00 = 0x060CA000), hardcoded. Every region above it
// (GROUPWIN/PACKEDCOL/LAYOUT/SGL) is unchanged -> zero #249 collision-move risk.
#define P6_GLOBALS_WINDOW 0x060CA000u /* W17/#258: fixed WRAM-H (was P6_OVL_BASE+P6_OVL_WINDOW) */
extern "C" void p6_register_global_variables_saturn(void **globals, int32 size)
{
    p6_w_glb_size = size;
    *globals = (void *)P6_GLOBALS_WINDOW;
    RSDK::globalVarsPtr = (int32 *)P6_GLOBALS_WINDOW;
    memset((void *)P6_GLOBALS_WINDOW, 0, (size_t)size);
    p6_w_glb_ptr = (int32)(uint32)RSDK::globalVarsPtr;
}

// p6_wave1_reg.c (game-side TU): the LinkGameLogicDLL role + wave-1
// registration, and the per-capture witness copier.
extern "C" void p6_wave1_link(void *functionTable, void *gameInfo,
                              void *currentSKU, void *sceneInfo,
                              void *controllerInfo, void *stickInfoL,
                              void *touchInfo, void *screenInfo,
                              void *unknownInfo);
extern "C" void p6_wave1_witness(void);
extern "C" void p6_player_witness_pre(int32 startSlot, int32 sceneCount);
extern "C" void p6_player_witness_post(void);
extern "C" void p6_player_witness_tick(void);
extern "C" void p6_cont_witness(void); // P6.8 Step A: SLOT_PLAYER1 continuous snapshot
#if defined(P6_FRONTEND_MENU)
extern "C" void p6_widescene_reset(void); // Task #298: reset the wide-scene sub-pool maps (Object.cpp)
#endif
// O1 step 2: p6_brg_witness + p6_loop_witness MOVED into the overlay (p6_ovl_ghz.c)
// WITH Bridge/PlaneSwitch -- the resident pack no longer names those globals; the
// overlay writes p6_w_brg_*/p6_w_loop_* via the ld -R import, through s_ovl.witness_fn.
// O1 step 1: p6_spring_witness likewise MOVED into the overlay -- the resident pack
// no longer names Spring (flat-TU rule); the overlay writes p6_w_spring_* via -R.
extern "C" void p6_player_newgame_reset(void); // #P0: zero Player->rings/powerups before InitObjects (game-side)
extern "C" void p6_titlecard_atl_restore(void); // punch v2 items 6/7: TitleCard.c:504-514 ATL camera hand-back
                                                // (chain seam only; no TitleCard object registered yet)
#if defined(P6_GHZCUT_BOOT)
// GL1 (2026-07-06): the chain GHZ-landing act card (p6_wave1_reg.c). Colored
// slide-in "GREEN HILL ZONE" card drawn into the direct VDP1 list. Spawned at
// the GHZCutscene->GHZ handoff, ticked in the frontend frame, drawn in the
// p6_dl_begin/end block. Gated so plain GHZ is byte-identical.
extern "C" void p6_titlecard_spawn(const char *zone_name, int actID, int display_slot);
extern "C" void p6_titlecard_tick(void);
extern "C" void p6_titlecard_draw(void);
extern "C" int32 p6_titlecard_is_active(void);
extern "C" int32 p6_w_tc_state;      // TC_STATE_* (-1 = never spawned)
extern "C" int32 p6_w_tc_draw_faces; // direct-list poly cmds emitted last draw
// GL1 glyph diagnostics (defined here; the seam writes them).
__attribute__((used)) int32 p6_w_tc_pal_bytes = -1; // DISPCARD.BIN load size (>=512 == uploaded)
__attribute__((used)) int32 p6_w_tc_disp_slot = -9; // Display.gif SaturnSheet slot handed to the card
__attribute__((used)) int32 p6_w_tc_clofen   = -1; // (CLOFEN<<16)|COAR read back at the landing (wash diag)
__attribute__((used)) int32 p6_w_tc_cram2    = -1; // (CRAM[512+33]<<16)|CRAM[512+255] -- glyph palette readback
__attribute__((used)) int32 p6_w_tc_cram01   = -1; // (CRAM[512+0]<<16)|CRAM[512+1] -- transparent+outline slots
__attribute__((used)) int32 p6_w_tc_cram2hash= -1; // djb2 of CRAM block 2 (vs DISPCARD)
__attribute__((used)) int32 p6_w_tc_gvramhash= -1; // djb2 of the last glyph's 768 VRAM bytes
__attribute__((used)) int32 p6_w_tc_gvram0   = -1; // first 4 pixel bytes of the glyph VRAM
#endif
extern "C" int32 SaturnSheet_FindSlot(const uint32 *hash); // #181 diag: banded-slot lookup by path hash
// Perf Phase 1 (Task #211): jo-side timing primitives (p6_perf.c). The true-60Hz
// vblank tally (registered via jo_core_add_vblank_callback in main.c) + the
// interrupt-safe SH-2 FRT read for per-section cost attribution.
extern "C" unsigned short p6_perf_frt_get(void);  // coherent 16-bit FRC read
extern "C" int            p6_perf_frt_cks(void);  // FRT divider (TCR CKS bits)
extern volatile unsigned int p6_perf_vbl_count;   // ++ at hardware 60 Hz
extern "C" unsigned short p6_perf_vdp1_edsr(void); // VDP1 EDSR (CEF bit1 = draw done)
extern "C" unsigned short p6_perf_vdp1_lopr(void); // VDP1 cmd-list END address
extern "C" unsigned short p6_perf_vdp1_copr(void); // VDP1 current cmd address
// P6.7 W7: game-side input-pointer witness (p6_wave1_reg.c) + the Saturn
// SMPC device backend (platform/Saturn/InputDevice_Saturn.cpp). The settle
// busy-wait MUST run before p6_load_phase_enter (it waits on SGL's
// per-vblank INTBACK result, ST-169-R1; it never issues an SMPC command).
extern "C" void p6_input_witness(void);
extern "C" int32 p6_input_settle(void);
namespace RSDK { namespace SKU { void InitSaturnInputAPI(); } }
extern "C" int p6_w_vdp1_slots; // p6_vdp1.c slot-cache population counter

#if defined(P6_FRONTEND_LOGOS)
// =============================================================================
// Front-end LOAD-TIMING instrumentation (Task #271) -- FLAG-GATED to the
// front-end flavors ONLY (P6_FRONTEND_LOGOS/TITLE/CHAIN). NEVER compiled in the
// default GHZ image (its _end sits ~64 B under the WRAM-H ANIMPAK ceiling per
// memory/wram-h-animpak-ceiling-boot-trap; these witnesses would breach it).
// The front-end flavor drops the large p6_ghz_frame/Ring-proof code (build_
// shipping.sh:5371) so it has the WRAM-H headroom for the witnesses.
//
// PURPOSE: time each LOAD SUB-STEP so the dominant cost of the ~70 s front-end
// title load is MEASURED (not assumed). Two regimes:
//   MASKED CORE (p6_scene_run, interrupts off, vblank ISR frozen + not yet
//     registered -- main.c:1270 runs the whole core BEFORE the vblank callback):
//     vblanks DON'T advance, so the wrap-immune metric is (fills,bytes) read from
//     the GFS counters (p6_gfs.c); FRT-ticks are EXACT for a compute sub-step
//     (<78 ms, no /32-FRT wrap) and undercount a multi-wrap I/O sub-step (sized
//     by fills instead).
//   PHASE-2 (p6_scene_load_and_arm, UNMASKED): vblanks advance -> EXACT ms AND
//     the ground-truth ms-per-fill (vbl_delta/fills_delta) that converts the
//     masked I/O fills to ms.
// p6_lt_mark(slot) snapshots the deltas since the previous mark into the slot's
// witnesses. Slots: 1..10 (see the checklist). All int32, savestate-peeked.
#define P6_LT_NSLOT 11   // index 0 unused; 1..10
__attribute__((used)) int32 p6_w_lt_vbl[P6_LT_NSLOT]   = {0}; // vblanks elapsed in the sub-step (phase-2 exact)
__attribute__((used)) int32 p6_w_lt_fills[P6_LT_NSLOT] = {0}; // GFS_Fread calls in the sub-step (wrap-immune)
__attribute__((used)) int32 p6_w_lt_kb[P6_LT_NSLOT]    = {0}; // KB read in the sub-step (wrap-immune)
__attribute__((used)) int32 p6_w_lt_frt[P6_LT_NSLOT]   = {0}; // FRT ticks (/32: exact<78ms, undercounts multi-wrap)
__attribute__((used)) int32 p6_w_lt_cks       = -1; // FRT divider select (expect 1 = /32)
__attribute__((used)) int32 p6_w_lt_masked_vbl = 0; // total vblanks across the MASKED core (expect ~0: frozen)
__attribute__((used)) int32 p6_w_lt_ph2_fills  = 0; // total fills in phase-2 (ground-truth ms/fill numerator helper)
__attribute__((used)) int32 p6_w_lt_ph2_vbl    = 0; // total vblanks in phase-2 (-> ms/fill = vbl*16.67/fills)
__attribute__((used)) int32 p6_w_lt_sfx_savedopen = 0; // Task #271 fix: SFX file-opens the early-out SKIPPED (was wasted seeks)
// SEEK-LOCALIZER (#271 load fix): cumulative GFS seek-count at marks 7-10 (index slot-7).
// Per-step seeks = consecutive deltas -> pins which LoadScene sub-step owns the title's ~105
// CD-seeks (the dominant load cost). [0]=end-of-masked(S7), [1]=S8 LoadSceneFolder, [2]=S9
// LoadSceneAssets, [3]=S10 InitObjects. 16 B; a TITLE-only -u root (build_p6scene_objs.sh)
// keeps it. The GHZ shipping build's no-op p6_lt_mark never references it -> gc-sections
// strips it there -> GHZ WRAM-H is UNTOUCHED (title _end +16 B = 80 B trap margin).
__attribute__((used)) int32 p6_w_p2seeks[4] = { 0, 0, 0, 0 };
// Running state for the marker (NOT witnesses).
extern "C" int p6_w_gfs_fills;        // p6_gfs.c windowed + storage.c single-shot fill count
extern "C" int p6_w_gfs_seeks_real;   // p6_gfs.c cumulative GFS_Seek count (seek-localizer)
extern "C" int p6_w_gfs_bytes;   // p6_gfs.c + storage.c total bytes read
static unsigned short p6_lt_prev_frt   = 0;
static unsigned int   p6_lt_prev_vbl   = 0;
static int            p6_lt_prev_fills  = 0;
static int            p6_lt_prev_bytes  = 0;
static int32          p6_lt_frt_acc     = 0; // wrap-accumulated ticks since the last mark
// Call ONCE at the very start of the timed region to zero the running cursors.
static void p6_lt_begin(void)
{
    p6_lt_prev_frt   = p6_perf_frt_get();
    p6_lt_prev_vbl   = p6_perf_vbl_count;
    p6_lt_prev_fills = p6_w_gfs_fills;
    p6_lt_prev_bytes = p6_w_gfs_bytes;
    p6_lt_frt_acc    = 0;
    if (p6_w_lt_cks < 0)
        p6_w_lt_cks = p6_perf_frt_cks();
}
// Record the sub-step that JUST ENDED into witness slot `slot`, then re-baseline.
// FRT wrap handling: a single ldelta < prev means exactly one /32 wrap elapsed
// since the previous frt sample (correct for any sub-step whose FRT span is one
// wrap; a multi-wrap I/O sub-step is sized by its fills, not this value).
static void p6_lt_mark(int slot)
{
    unsigned short frt = p6_perf_frt_get();
    unsigned int   vbl = p6_perf_vbl_count;
    int            fil = p6_w_gfs_fills;
    int            byt = p6_w_gfs_bytes;
    int32 dfrt = (int32)(uint16)((unsigned)frt - (unsigned)p6_lt_prev_frt);
    if (slot > 0 && slot < P6_LT_NSLOT) {
        p6_w_lt_frt[slot]   = dfrt;
        p6_w_lt_vbl[slot]   = (int32)(vbl - p6_lt_prev_vbl);
        p6_w_lt_fills[slot] = fil - p6_lt_prev_fills;
        p6_w_lt_kb[slot]    = (byt - p6_lt_prev_bytes) / 1024;
    }
    if (slot >= 7 && slot <= 10)
        p6_w_p2seeks[slot - 7] = (int32)vbl; // LOAD-localizer: cumulative phase-2 VBL at this mark
                                             // (per-step total time; subtract the seek-I/O for compute).
                                             // Seek split already captured: masked 6 / S8 2 / S9 1 / S10 6.
    p6_lt_prev_frt   = frt;
    p6_lt_prev_vbl   = vbl;
    p6_lt_prev_fills = fil;
    p6_lt_prev_bytes = byt;
}
#define P6_LT_BEGIN()    p6_lt_begin()
#define P6_LT_MARK(slot) p6_lt_mark(slot)
#else
#define P6_LT_BEGIN()    ((void)0)
#define P6_LT_MARK(slot) ((void)0)
#endif

// src/rsdk/storage.c (hand-port TU, linked in this image): generic GFS
// load-to-address -- the overlay loader. Name is historical; any address.
extern "C" int rsdk_storage_load_to_lwram(const char *iso9660_name,
                                          void *dst, uint32 max_bytes);

#if defined(P6_FRAMEDIR)
// Stage-1 FRD declarations (checklist sec 7) -- at THIS file position so the
// p6_ghz_arm_env bind loop (below) can attach; the staging helpers live next
// to p6_stage_sheet_hash further down. W12b LTO contract: the jo-side
// p6_vdp1.c receives SaturnFrameDir_Lookup as a RUNTIME FUNCTION POINTER
// only (p6_vdp1_set_frd); pack->jo references are the proven direction.
extern "C" {
int32  SaturnFrameDir_StageDirect(const void *blob, uint32 bytes);
void   SaturnFrameDir_SetHash(int32 slot, const uint32 *hash);
int32  SaturnFrameDir_FindSlot(const uint32 *hash);
void   SaturnFrameDir_Reset(void);
int32  SaturnFrameDir_Lookup(int32 slot, int32 sx, int32 sy,
                             int32 w, int32 h, void *out);
uint32 SaturnSheet_ResAlloc(uint32 bytes);
uint32 SaturnSheet_ResRemain(void);
void   SaturnSheet_ResFloor(uint32 guardBytes);
void   p6_vdp1_set_frd(int32 (*fn)(int32, int32, int32, int32, int32, void *));
void   p6_vdp1_sheet_set_frd(int handle, int frdSlot);
void   p6_vdp1_frd_detach_all(void);
void   p6_vdp1_frd_set_store(int shtSlot, int frdSlot);
void   p6_vdp1_frd_clear_store(void);
int32  SaturnSheet_SlotCount(void);
void   SaturnSheet_SlotHashCopy(int32 slot, uint32 *out);
}
#endif // P6_FRAMEDIR (declarations)

extern "C" {

void *p6_scene_entity(void) { return (void *)RSDK::sceneInfo.entity; }

void p6_bridge_proc_anim(void *animator)
{
    typedef void (*ProcAnimFn)(RSDK::Animator *);
    ProcAnimFn fn = (ProcAnimFn)RSDK::RSDKFunctionTable[RSDK::FunctionTable_ProcessAnimation];
    if (fn)
        fn((RSDK::Animator *)animator);
}

void p6_bridge_draw_sprite(void *animator, void *position, int32 screenRelative)
{
    // Counts Ring_Draw_Normal dispatches (qa_p6_obj.py O2/O5, and
    // qa_p6_draw.py's updated D1: draw_calls == ticks + obj_draws).
    ++p6_w_obj_draws;
    typedef void (*DrawSpriteFn)(RSDK::Animator *, RSDK::Vector2 *, bool32);
    DrawSpriteFn fn = (DrawSpriteFn)RSDK::RSDKFunctionTable[RSDK::FunctionTable_DrawSprite];
    if (fn)
        fn((RSDK::Animator *)animator, (RSDK::Vector2 *)position, (bool32)screenRelative);
}
} // extern "C"

// libm-free fminf/fmaxf: SetChannelAttributes (Audio.cpp:509, KEPT because
// the function table references it) needs both; the pack links no libm
// (P6.4 decision). Soft-float compares route through libgcc.
extern "C" {
float fminf(float a, float b) { return a < b ? a : b; }
float fmaxf(float a, float b) { return a > b ? a : b; }
}

namespace RSDK {
// P6.7a link closure backings/stubs (measured undefined list, build iter 3):
// videoSettings is read by ProcessObjectDrawLists/LoadImage; the rest are
// dev/SKU surfaces the function table or REV02 paths reference.
// P6.7 W15b: showHitboxes backing RETIRED -- the real Scene/Collision.cpp
// pack TU defines it (Collision.cpp:61).
VideoSettings videoSettings;
void RenderDeviceBase::SetupImageTexture(int32 width, int32 height, uint8 *imagePixels)
{
    (void)width; (void)height; (void)imagePixels; // FIXME P6.8: Cinepak/image path
}
void DrawDevString(const char *string, int32 x, int32 y, int32 align, uint32 color)
{
    (void)string; (void)x; (void)y; (void)align; (void)color;
}
namespace SKU {
void DrawAchievements() {}
} // namespace SKU

// P6.7c: the "Blank Object" @0 placeholder is RETIRED -- the canonical
// InitGameLink preamble (RetroEngine.cpp:1216-1235) registers the REAL
// engine DefaultObject @0 + DevOutput @1 (their TUs are now in the pack).
//
// The proof entity's fixed slot: inside the RESERVE area (0x00..0x3F), below
// every scene-parsed entity -- deterministic for the gate.
// CLASSID: with the engine's own StageConfig path live (Title
// useGlobalObjects=0 -> classCount = TYPE_DEFAULT_COUNT = 2 after
// LoadSceneFolder, Scene.cpp:186-190), the harness appends Ring as stage
// class 2 (the Scene.cpp:199-205 mirror in the run body).
#define P6_OBJ_RING_SLOT (RESERVE_ENTITY_COUNT - 1)
#define P6_OBJ_RING_CLASSID 2
} // namespace RSDK

// ---- (b2) Group-B arrays as absolute WRAM-L symbols ---------------------------
// These are declared as ARRAYS in headers (cannot be pointer-relocated without
// further engine edits), but their mangled-name link symbols can be defined as
// ABSOLUTE addresses -- zero bytes in the loaded image, so they cost nothing
// against the 41,936 B WRAM-H headroom (_end 0x060B5C30 vs SGL floor
// 0x060C0000, game.map 2026-06-06). Mangled names verbatim from the measured
// closure. p6_scene_run() zeroes the whole window before any engine call.
#define P6_GROUPB_ABS(sym, addr) \
    __asm__(".global " sym "\n\t.equ " sym ", " addr)
// P6.4: dataFileList -- the Data.rsdk registry LoadDataPack fills
// (Reader.cpp:140-154) and OpenDataFile hash-scans (Reader.cpp:192-196).
// W11 closer C3: 24 B packed records x DATAFILE_COUNT 0x6A0 = 40,704
// <= the 0xA000 window. W11b map v7: moved with its region (the old
// 0x283700 now falls inside P6_LW_TILELAYERS).
P6_GROUPB_ABS("__ZN4RSDK12dataFileListE",    "0x002C2500"); // RSDKFileInfo[0x6A0] = 40,704 <= 0xA000 (== P6_LW_DATAFILELIST, map v8.1)
P6_GROUPB_ABS("__ZN4RSDK11fullPaletteE",     "0x002FAC00"); // uint16[8][256] = 0x1000
P6_GROUPB_ABS("__ZN4RSDK13globalPaletteE",   "0x002FBC00"); // uint16[8][256] = 0x1000
P6_GROUPB_ABS("__ZN4RSDK12stagePaletteE",    "0x002FCC00"); // uint16[8][256] = 0x1000
P6_GROUPB_ABS("__ZN4RSDK11scene3DListE",     "0x002FDC00"); // Scene3D[32]    = 0xA80
P6_GROUPB_ABS("__ZN4RSDK10gfxSurfaceE",      "0x002FE680"); // GFXSurface[64] = 0x900
// textBuffer[0x400] is NOT an absolute here: Storage_Text.o (linked since P6.4
// for the real GenerateHashMD5) DEFINES it -- 1 KB rides WRAM-H .bss instead.
P6_GROUPB_ABS("__ZN4RSDK14stageObjectIDsE",  "0x002FEF80"); // int32[256]     = 0x400
P6_GROUPB_ABS("__ZN4RSDK15globalObjectIDsE", "0x002FF380"); // int32[256]     = 0x400
P6_GROUPB_ABS("__ZN4RSDK11rgb32To16_RE",     "0x002FF780"); // uint16[256]    = 0x200
P6_GROUPB_ABS("__ZN4RSDK11rgb32To16_GE",     "0x002FF980"); // uint16[256]    = 0x200
P6_GROUPB_ABS("__ZN4RSDK11rgb32To16_BE",     "0x002FFB80"); // uint16[256]    = 0x200
P6_GROUPB_ABS("__ZN4RSDK13gfxLineBufferE",   "0x002FFD80"); // uint8[224]     = 0xE0
P6_GROUPB_ABS("__ZN4RSDK16activeGlobalRowsE","0x002FFE60"); // uint16[8]      = 0x10
P6_GROUPB_ABS("__ZN4RSDK15activeStageRowsE", "0x002FFE70"); // uint16[8]      = 0x10
P6_GROUPB_ABS("__ZN4RSDK7screensE",          "0x002FFE80"); // ScreenInfo[1]  = 0x34 -> end 0x2FFEB4 < 0x2FFEC0

// ---- (c) WRAM-L _sbrk + full newlib syscall set -------------------------------
// Pattern + rationale from p6_syscalls.c:4-11 (that file serves the STANDALONE
// p6.linker image via __heap_start/__heap_end; this one serves the jo image via
// the fixed window above -- the two are never linked together). newlib's
// nano-mallocr -> lib_a-sbrkr.o resolves _sbrk to THIS, so InitStorage's 608 KB
// of pools (Storage.cpp:45-49) land in WRAM-L instead of overrunning the SGL
// work area.
extern "C" {
static unsigned char *p6_brk = (unsigned char *)P6_LW_HEAP_BASE;
void *_sbrk(int incr)
{
    unsigned char *prev = p6_brk;
    if ((unsigned long)p6_brk + (unsigned long)incr > P6_LW_HEAP_END)
        return (void *)-1; // malloc fails cleanly -> InitStorage returns false
    p6_brk += incr;
    return prev;
}
int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, void *st) { (void)fd; (void)st; return -1; }
int _isatty(int fd) { (void)fd; return 0; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return -1; }
int _read(int fd, void *buf, int len) { (void)fd; (void)buf; (void)len; return -1; }
int _write(int fd, const void *buf, int len) { (void)fd; (void)buf; (void)len; return -1; }
int _open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; return -1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
void _exit(int code)
{
    (void)code;
    for (;;) {
    }
}
int _fork(void) { return -1; }
int _wait(int *status) { (void)status; return -1; }
int _link(const char *o, const char *n) { (void)o; (void)n; return -1; }
int _unlink(const char *p) { (void)p; return -1; }
int _stat(const char *p, void *st) { (void)p; (void)st; return -1; }
int _times(void *buf) { (void)buf; return -1; }
int _gettimeofday(void *tv, void *tz) { (void)tv; (void)tz; return -1; }
int _execve(const char *p, char **argv, char **envp) { (void)p; (void)argv; (void)envp; return -1; }
// The sh newlib lib_a-syscalls.o exports MORE than the classic set above
// (measured by parsing libc.a 2026-06-09): _creat, isatty, _raise, _chmod,
// _chown, _utime, _execv, _pipe, and the non-reentrant `errno` global. If ANY
// of those stays undefined when -lc is searched, ld opens the member and every
// symbol above multiple-defines (measured on the first `make P6SCENE=1` link).
// Cover the full export surface; only crt0-only __setup_argv_and_call_main is
// skipped (nothing in a SLSTART-booted image can reference it).
int isatty(int fd) { (void)fd; return 0; }
int _creat(const char *path, int mode) { (void)path; (void)mode; return -1; }
int _raise(int sig) { (void)sig; return -1; }
int _chmod(const char *path, int mode) { (void)path; (void)mode; return -1; }
int _chown(const char *path, int owner, int group) { (void)path; (void)owner; (void)group; return -1; }
int _utime(const char *path, void *times) { (void)path; (void)times; return -1; }
int _execv(const char *path, char **argv) { (void)path; (void)argv; return -1; }
int _pipe(int *fds) { (void)fds; return -1; }
} // extern "C"
// `errno` as a raw asm data symbol: errno may be a macro in C++ TUs, so a
// C-level `int errno;` is not reliably spellable; the 4-byte .bss symbol is.
__asm__(".section .bss._p6_errno,\"aw\",@nobits\n"
        "\t.global _errno\n"
        "\t.align 2\n"
        "_errno:\n"
        "\t.space 4\n"
        "\t.section .text\n");

// P6.5b2: the engine animator the per-tick callback advances with the
// engine's own ProcessAnimation (zero .bss; armed by SetSpriteAnimation in
// the run body once Ring.bin is loaded).
static Animator p6_ringAnimator;

// P6.6b: the device-side S16 buffer (the SCSP consumes integer PCM; the
// engine keeps F32, Audio.cpp:378). Converted ONCE in the run body and
// uploaded to Sound RAM +0x6C000 (p6_snd.c); slot key-ons replay it.
// MenuBleep = 3,487 samples @ 44.1 kHz native (SCSP pitch OCT 0/FNS 0).
#define P6_SND_MAXSAMPLES 4096
static int16  p6_sndPcm16[P6_SND_MAXSAMPLES];
static uint32 p6_sndPcmBytes = 0; // S16 byte count (witness hash + upload extent)
static int32  p6_sndSfxID    = -1;

// P6.7a: the REAL engine-loaded Ring anim-0 frame base (saved in block 6;
// the engine-loop entity's animator points INTO the engine's own frames).
static SpriteFrame *p6_objRingFrames = NULL;

// P6.7a: re-arm the proof entity through the ENGINE path (ResetEntitySlot,
// Object.cpp:1084: memset entityClassSize + create dispatch + classID), then
// apply the spawn-relevant Ring_Create lines (p6_ring2_arm, cited there) and
// the spawn position. Slot 0x3F = top of the RESERVE area, below every
// scene-parsed entity.
// P6.7 wave-1: ResetEntitySlot consumes the STAGE class index (set at the
// harness Ring append; 4 at Title with Options+Localization matched ahead),
// NOT the objectClassList id (still 2).
static int32 s_ring_stage_classid = -1;
static void p6_obj_spawn_ring(void)
{
    ResetEntitySlot(P6_OBJ_RING_SLOT, (uint16)s_ring_stage_classid, NULL);
    if (s_ovl.arm_fn)  // overlay vtable (P6.7d.3) -- same verbatim arm body
        s_ovl.arm_fn(RSDK_ENTITY_AT(P6_OBJ_RING_SLOT), (void *)p6_objRingFrames);
    RSDK_ENTITY_AT(P6_OBJ_RING_SLOT)->position.x = 260 << 16;
    RSDK_ENTITY_AT(P6_OBJ_RING_SLOT)->position.y = 60 << 16;
    ++p6_w_obj_spawns;
}

// ---- (d) The proof body main.c calls right after jo_core_init -----------------
// Pre-state per the measured LoadSceneAssets contract (Scene.cpp:295-297 reads
// sceneInfo.listData[listPos].id + currentSceneFolder; :693 keeps an entity iff
// sceneInfo.filter & entity->filter, entity->filter = 0xFF from :555 -- a zero
// sceneInfo.filter silently wipes EVERY parsed entity at :702).
// ---- SMPC single-task discipline for the synchronous load phase --------------
// ST-169 (SMPC manual, "Command Issue Limitations", SMPC_Manual.txt:996-1000):
// "When conducting command issue processing for the SMPC, a maximum effort
// shall be made to conduct single tasks. If multiple issues are being
// processed, problems, such as dual command issue, could occur, which could
// cause the SMPC error or a deadlock to occur." MEASURED deadlock (Task #227,
// p6_c5.mcs): SMPC SF=1 with PendingCommand == ExecutingCommand == -1 (NO
// command in flight) while SGL's per-frame vblank INTBACK issuer
// (slSmpcCashSend/Blow, sglI00) spins on SF forever -- the INTBACK
// continue handshake raced the multi-frame CPU-polled CDC reads of the GHZ
// InitObjects StageLoads (Player .bin parse, p6_w_anim_step=0x405). The
// load phase is SYNCHRONOUS by decomp contract (ProcessEngine's
// ENGINESTATE_LOAD polls no input), so p6_scene_run runs with all maskable
// interrupts off: SGL issues NO INTBACK, dual-issue cannot exist. Any
// in-flight command is drained (bounded SF wait) before masking; the saved
// SR is restored on every exit path and SGL recovers on the next vblank.
#define P6_SMPC_SF (*(volatile unsigned char *)0x20100063u)
static int p6_saved_sr = 0;
static void p6_load_phase_enter(void)
{
    // Boot/load cover: blank VDP2 scroll+sprite display BEFORE masking interrupts
    // so the multi-second synchronous load shows a clean back-color instead of the
    // red/green static (NBG1 displaying half-written VRAM). Re-armed by the first
    // GHZ present after the load completes.
    p6_vdp2_blank();
    for (volatile int i = 0; i < 2000000 && (P6_SMPC_SF & 1); ++i) {}
    int sr;
    __asm__ volatile("stc sr, %0" : "=r"(sr));
    p6_saved_sr = sr;
    sr = (sr & ~0xF0) | 0xF0; // SR.I3-I0 = 15: all maskable interrupts off
    __asm__ volatile("ldc %0, sr" : : "r"(sr));
}
static void p6_load_phase_exit(void)
{
    __asm__ volatile("ldc %0, sr" : : "r"(p6_saved_sr));
}

// =============================================================================

// P6.8 Step A (Task #211): p6_ghz_arm_env -- factor the GHZ render-environment
// arm out of the W14/W18/W19 burst preamble (p6_scene_run lines ~1889-1905
// screen env + clip bounds, ~1955-2006 Items/Display/Shields SHEET-ONLY nudges
// + the per-surface VDP1 bind loop). Assumes GHZ is the CURRENT loaded scene
// (LoadSceneFolder/LoadSceneAssets/InitObjects already ran). Called from BOTH
// the existing burst site AND the end-of-run continuous arm (no code dup).
// =============================================================================
static void p6_ghz_arm_env(void)
{
    // Perf Phase 2c: a fresh GHZ env means a new static FG map -- re-arm the
    // present cache so it rebuilds once on the first frame, then runs cached.
    p6_vdp2_present_dirty = 1;
    // Screen env (mirror SetScreenSize: center = size/2) -- Camera_Create
    // reads ScreenInfo->center; a zero center parks the viewport off-spawn.
    videoSettings.screenCount = 1;
    screens[0].size.x         = SCREEN_XMAX;
    screens[0].size.y         = SCREEN_YSIZE;
    screens[0].center.x       = SCREEN_XMAX / 2;
    screens[0].center.y       = SCREEN_YSIZE / 2;
    screens[0].pitch          = SCREEN_XMAX;
    screens[0].clipBound_X1   = 0;
    screens[0].clipBound_Y1   = 0;
    screens[0].clipBound_X2   = SCREEN_XMAX;
    screens[0].clipBound_Y2   = SCREEN_YSIZE;
    currentScreen             = &screens[0];
    sceneInfo.entity          = RSDK_ENTITY_AT(0);

    if (!p6_vdp1HandlesInit) {
        for (int32 i = 0; i < SURFACE_COUNT; ++i)
            p6_vdp1HandleBySurface[i] = -1;
        p6_vdp1HandlesInit = true;
    }

    // W18/W19 SHEET-ONLY nudges: allocate the Items/Display/Shields gfxSurface
    // slots + resolve their already-staged banded slots (zero DATASET_STG cost)
    // so the bind loop below can bind them. LoadSpriteSheet returns the
    // EXISTING surface for an already-loaded sheet (idempotent re-arm).
    {
        int32 rsurf =
            (int32)(int16)LoadSpriteSheet("Global/Items.gif", SCOPE_STAGE);
        p6_w_ringspr   = rsurf;
        if (rsurf >= 0 && rsurf < SURFACE_COUNT)
            p6_w_ringsheet = (int32)gfxSurface[rsurf].saturnSheetSlot;
        int32 dsurf =
            (int32)(int16)LoadSpriteSheet("Global/Display.gif", SCOPE_STAGE);
        p6_w_dispsurf = dsurf;
        if (dsurf >= 0 && dsurf < SURFACE_COUNT)
            p6_w_dispsheet = (int32)gfxSurface[dsurf].saturnSheetSlot;
        int32 ssurf =
            (int32)(int16)LoadSpriteSheet("Global/Shields.gif", SCOPE_STAGE);
        p6_w_shldsurf = ssurf;
        if (ssurf >= 0 && ssurf < SURFACE_COUNT)
            p6_w_shldsheet = (int32)gfxSurface[ssurf].saturnSheetSlot;
    }

    // #250: bind only surfaces not already bound. On a same-folder reload the gfx
    // surfaces PERSIST (LoadSceneFolder early-returns before ClearGfxSurfaces),
    // so their VDP1 bindings are still valid -- skipping them here is correct;
    // re-binding them (an earlier misdiagnosis) made the sprites invisible.
    for (int32 i = 0; i < SURFACE_COUNT; ++i) {
        GFXSurface *sf = &gfxSurface[i];
        if (sf->scope == SCOPE_NONE || p6_vdp1HandleBySurface[i] >= 0)
            continue;
        int32 h = -1;
        int32 wasPix = 0;
        if (sf->pixels) {
            wasPix = 1;
            h = p6_vdp1_sheet_bind(sf->pixels, sf->width,
                                   (const unsigned short *)fullPalette[0]);
        } else if (sf->saturnSheetSlot >= 0)
            h = p6_vdp1_sheet_bind_banded(sf->saturnSheetSlot, sf->width,
                                          (const unsigned short *)fullPalette[0]);
        else
            continue;
        p6_vdp1HandleBySurface[i] = (int8)h;
#if defined(P6_FRAMEDIR)
        // Stage-1 FRD attach (checklist sec 7): if this surface's sheet has a
        // staged frame directory (same gif-path MD5), route its slot-cache
        // misses through the pre-cut patterns. Load-phase only (bind loop).
        if (h >= 0) {
            int32 fs = SaturnFrameDir_FindSlot((const uint32 *)sf->hash);
            if (fs >= 0)
                p6_vdp1_sheet_set_frd(h, fs);
        }
#endif
        // BADNIK-VIS: log every bind ATTEMPT in this (shipping) arm_env loop so the
        // exact VDP1 bind-table demand is measured (the burst-path bind_log never
        // runs in shipping -- bind_count was 0). attempt = (surfaceID<<16)|(wasPix<<8)
        // |(handle&0xFF). bind_demand counts every surface that consumed a bind slot.
        if (h >= 0) ++p6_w_bind_count;
        ++p6_w_bind_demand;
        if (p6_w_bind_logn < 16)
            p6_w_bind_log16[p6_w_bind_logn++] =
                (i << 16) | (wasPix << 8) | (h & 0xFF);
    }

    // #181 sheet-bind diag: pinpoint why GHZ/Objects.gif (sheetID 14, the bridge's
    // sheet) is still unbound after staging GHZOBJ.SHT. ghzobj_slot>=0 proves the
    // stage + path-hash are right; brg_surfslot is the bridge surface's resolved
    // banded slot; the h0 pair compares the stage path hash to the surface's hash.
    {
        RETRO_HASH_MD5(gh);
        GEN_HASH_MD5("GHZ/Objects.gif", gh);
        p6_w_ghzobj_h0   = (int32)gh[0];
        p6_w_ghzobj_slot = SaturnSheet_FindSlot((const uint32 *)gh);
        if (14 < SURFACE_COUNT) {
            p6_w_brg_surfslot  = (int32)gfxSurface[14].saturnSheetSlot;
            p6_w_brg_surfscope = (int32)gfxSurface[14].scope;
            p6_w_brg_surfh0    = (int32)gfxSurface[14].hash[0];
        }
        // BADNIK-VIS: find the GHZ/Objects.gif surface by HASH scan (not the stale
        // hardcoded index 14) + its bound handle. This is the surface-side truth for
        // every badnik (all reference GHZ/Objects.gif). idx<0 => no object loaded the
        // sheet; handle<0 => surface exists but UNBOUND (=> every badnik blit drops).
        int32 pop = 0, found = -1;
        for (int32 i = 0; i < SURFACE_COUNT; ++i) {
            if (gfxSurface[i].scope != SCOPE_NONE) ++pop;
            if (found < 0 && gfxSurface[i].scope != SCOPE_NONE
                && gfxSurface[i].hash[0] == gh[0] && gfxSurface[i].hash[1] == gh[1]
                && gfxSurface[i].hash[2] == gh[2] && gfxSurface[i].hash[3] == gh[3])
                found = i;
        }
        p6_w_surfpop = pop;
        p6_w_ghzobj_surf_idx = found;
        if (found >= 0) {
            p6_w_ghzobj_surf_slot   = (int32)gfxSurface[found].saturnSheetSlot;
            p6_w_ghzobj_surf_scope  = (int32)gfxSurface[found].scope;
            p6_w_ghzobj_surf_handle = p6_vdp1_handle_for_surface(found);
        }
    }
#if defined(P6_FRONTEND_LOGOS)
    // CP4c BLUE-SCREEN diag: SURFACE-side truth for the Logos splash sheet. Same
    // hash-scan as the GHZ/Objects.gif block above but for "Logos/Logos.gif". This
    // pinpoints whether the bind loop (above, :2117-2142) bound the Logos surface:
    // shtslot>=0 == LOGOS.SHT staged+hashed; surfidx>=0 == a gfxSurface loaded that
    // sheet (UIPicture's LoadSpriteAnimation); surfslot>=0 == it resolved the banded
    // slot; the bound handle is read separately in the per-frame witness.
    {
        RETRO_HASH_MD5(lh);
        GEN_HASH_MD5("Logos/Logos.gif", lh);
        p6_w_logos_h0      = (int32)lh[0];
        p6_w_logos_shtslot = SaturnSheet_FindSlot((const uint32 *)lh);
        int32 lfound = -1;
        for (int32 i = 0; i < SURFACE_COUNT; ++i) {
            if (gfxSurface[i].scope != SCOPE_NONE
                && gfxSurface[i].hash[0] == lh[0] && gfxSurface[i].hash[1] == lh[1]
                && gfxSurface[i].hash[2] == lh[2] && gfxSurface[i].hash[3] == lh[3]) {
                lfound = i;
                break;
            }
        }
        p6_w_logos_surfidx = lfound;
        if (lfound >= 0) {
            p6_w_logos_surfslot  = (int32)gfxSurface[lfound].saturnSheetSlot;
            p6_w_logos_surfscope = (int32)gfxSurface[lfound].scope;
            p6_w_logos_surfh0    = (int32)gfxSurface[lfound].hash[0];
        }
    }
#endif
#if defined(P6_FRONTEND_TITLE)
    // CP5b.1 (Task #268): SURFACE-side truth for the TITLE logo sheet. Same hash-scan
    // as the Logos block above but for "Title/Logo.gif" -- pinpoints whether the bind
    // loop (:2182-2207) bound the TitleLogo surface. tlogo_shtslot>=0 == TLOGO.SHT
    // staged+hashed; tlogo_surfidx>=0 == TitleLogo's LoadSpriteAnimation loaded the
    // surface; tlogo_surfslot>=0 == it resolved the banded slot; tlogo_handle>=0 == the
    // bind loop bound it == the blit CAN land (CP5a RED had tlogo_handle<0 -> the logo
    // dropped -> uniform-blue title).
    {
        RETRO_HASH_MD5(th);
        GEN_HASH_MD5("Title/Logo.gif", th);
        p6_w_tlogo_h0      = (int32)th[0];
        p6_w_tlogo_shtslot = SaturnSheet_FindSlot((const uint32 *)th);
        int32 tfound = -1;
        for (int32 i = 0; i < SURFACE_COUNT; ++i) {
            if (gfxSurface[i].scope != SCOPE_NONE
                && gfxSurface[i].hash[0] == th[0] && gfxSurface[i].hash[1] == th[1]
                && gfxSurface[i].hash[2] == th[2] && gfxSurface[i].hash[3] == th[3]) {
                tfound = i;
                break;
            }
        }
        p6_w_tlogo_surfidx = tfound;
        if (tfound >= 0) {
            p6_w_tlogo_surfslot  = (int32)gfxSurface[tfound].saturnSheetSlot;
            p6_w_tlogo_surfscope = (int32)gfxSurface[tfound].scope;
            p6_w_tlogo_surfh0    = (int32)gfxSurface[tfound].hash[0];
            p6_w_tlogo_handle    = p6_vdp1_handle_for_surface(tfound);
        }
    }
    // CP5b.2 (Task #269): SURFACE-side truth for the TITLE Sonic sheet -- the EXACT
    // mirror of the tlogo scan above but for "Title/Sonic.gif" (TitleSonic's sheet).
    // tsonic_shtslot>=0 == TSONIC.SHT staged+hashed; tsonic_surfidx>=0 == TitleSonic_
    // StageLoad's LoadSpriteAnimation loaded the surface; tsonic_surfslot>=0 == it
    // resolved the banded slot; tsonic_handle>=0 == the bind loop bound it == the head
    // CAN blit (CP5b.1 RED had tsonic ABSENT -> the head dropped -> black ring hole).
    {
        RETRO_HASH_MD5(sh);
        GEN_HASH_MD5("Title/Sonic.gif", sh);
        p6_w_tsonic_h0      = (int32)sh[0];
        p6_w_tsonic_shtslot = SaturnSheet_FindSlot((const uint32 *)sh);
        int32 sfound = -1;
        for (int32 i = 0; i < SURFACE_COUNT; ++i) {
            if (gfxSurface[i].scope != SCOPE_NONE
                && gfxSurface[i].hash[0] == sh[0] && gfxSurface[i].hash[1] == sh[1]
                && gfxSurface[i].hash[2] == sh[2] && gfxSurface[i].hash[3] == sh[3]) {
                sfound = i;
                break;
            }
        }
        p6_w_tsonic_surfidx = sfound;
        if (sfound >= 0) {
            p6_w_tsonic_surfslot  = (int32)gfxSurface[sfound].saturnSheetSlot;
            p6_w_tsonic_surfscope = (int32)gfxSurface[sfound].scope;
            p6_w_tsonic_surfh0    = (int32)gfxSurface[sfound].hash[0];
            p6_w_tsonic_handle    = p6_vdp1_handle_for_surface(sfound);
        }
    }
    // CP5b.3 (Task #272): SURFACE-side truth for the TitleBG sheet (Title/BG.gif).
    // tbg_shtslot>=0 == TBG.SHT staged+hashed; tbg_surfidx>=0 == TitleBG_StageLoad's
    // LoadSpriteAnimation loaded the surface; tbg_handle>=0 == bound == the mountains/
    // water/billboard sprites CAN blit. Also latch the registered classIDs.
    {
        RETRO_HASH_MD5(bh);
        GEN_HASH_MD5("Title/BG.gif", bh);
        p6_w_tbg_shtslot = SaturnSheet_FindSlot((const uint32 *)bh);
        int32 bfound = -1;
        for (int32 i = 0; i < SURFACE_COUNT; ++i) {
            if (gfxSurface[i].scope != SCOPE_NONE
                && gfxSurface[i].hash[0] == bh[0] && gfxSurface[i].hash[1] == bh[1]
                && gfxSurface[i].hash[2] == bh[2] && gfxSurface[i].hash[3] == bh[3]) {
                bfound = i;
                break;
            }
        }
        p6_w_tbg_surfidx = bfound;
        if (bfound >= 0) {
            p6_w_tbg_surfslot = (int32)gfxSurface[bfound].saturnSheetSlot;
            p6_w_tbg_handle   = p6_vdp1_handle_for_surface(bfound);
        }
        // (TitleBG/Title3DSprite classIDs are latched from the overlay witness fn --
        // their object globals are overlay-resident symbols, not nameable here.)
    }
#endif
}

// =============================================================================
// P6.8 Step A (Task #211): p6_ghz_frame -- ONE continuous engine frame at GHZ.
// Mirrors the ENGINESTATE_REGULAR head (ProcessInput/ProcessObjects/Process-
// ObjectDrawLists, RetroEngine.cpp:392-394) + the W16 camera-anchored FG
// present, then snapshots the continuous witnesses. Called every jo frame from
// p6_scene_tick() when p6_ghz_continuous_armed. p6_w_cont_frames bumps here
// only -- a one-shot burst can never increment it (the burst-vs-cont gate).
// =============================================================================
// Perf Phase 1 (Task #211): wrap-handled FRC tick delta. The FRC is 16-bit free-
// running; a single section delta is recovered across at most one 16-bit wrap
// (end < start => the counter rolled over once). The interrupt-masked
// p6_perf_frt_get() guarantees each endpoint read is itself coherent.
#define P6_FRT_DELTA(a, b) ((int32)(uint16)((unsigned)(b) - (unsigned)(a)))

#if defined(P6_GHZ2_BOOT)
// #237: GHZ2 play-probe witnesses (defined below near the GHZ2 load latch) --
// forward-declared so the per-frame probe in p6_ghz_frame can reach them.
extern int32 p6_w_ghz2_loaded;
extern int32 p6_w_ghz2_listpos;
extern int32 p6_w_ghz2_play_frames;
extern int32 p6_w_ghz2_max_plry;
extern int32 p6_w_ghz2_exit_lp;
extern int32 p6_w_ghz2_collplane;
extern int32 p6_w_ghz2_colllayers;
extern int32 p6_w_ghz2_tilecoll;
extern int32 p6_w_ghz2_floorlayer;
extern int32 p6_w_ghz2_fghigh_idx;
extern int32 p6_w_ghz2_fglow_tile;
extern int32 p6_w_ghz2_fghigh_tile;
extern int32 p6_w_ghz2_feetty;
extern int32 p6_w_ghz2_vely;
extern int32 p6_w_ghz2_tcoff;
extern "C" unsigned short SaturnLayout_GetTile(int, int, int);
extern "C" void SaturnLayout_SetTile(int, int, int, unsigned short); // FG-tile-mutation piece 1
extern "C" int SaturnLayout_SlotLayer(int);
extern int32 p6_w_ghz2_slot1layer;
#endif

// #237/#180 FIX: the per-zone FG-Low tile-layer index -- the layer
// LoadSceneFolder bound to SaturnLayout slot 0 (Scene.cpp:450-452). GHZ1=3,
// GHZ2=4. The VDP2 present (p6_vdp2_present_ghz_camera) rebinds the SHARED
// SaturnLayout slots 0/1 to (layer, layer+1); passing a hardcoded 3 bound slot 1
// to GHZ2's layer 4 (FG-Low) instead of layer 5 (FG-High), clobbering the
// player's FG-High floor reads -> the player tunnelled through the GHZ2 spawn
// floor and died on repeat. Deriving the index from saturnSlot makes the present
// rebind the SAME per-zone FG layers the collision uses (GHZ1 unchanged at 3).
static int p6_fglow_layer_index(void)
{
    for (int l = 0; l < 8; ++l)
        if (tileLayers[l].saturnSlot == 0)
            return l;
    return 3; // GHZ1 default if no slot-0 bind yet
}

static void p6_ghz_frame(void)
{
    unsigned short t0, t1;
    // Phase 2c (#246): compute-FULL bracket START -- FRT at function entry, paired
    // with the exit read at the bottom into p6_w_perf_full_frt. MUST be the first
    // read so the head setup (synch measure, perf_reset, drawgroup, Ring) is inside
    // the bracket. Replaces the DERIVATION (frame - synch) with a direct measure.
    unsigned short frame_t0 = p6_perf_frt_get();
    // Phase 2a: vblanks consumed in the jo loop body since the last frame ended
    // (slSynch + the loop's buffer_reset/unitmatrix) -- read BEFORE any work.
    unsigned int vbl_frame_start = p6_perf_vbl_count;
    {
        int32 jo_gap = (int32)(vbl_frame_start - p6_perf_vbl_prev);
        p6_w_perf_vbl_jo = jo_gap;
        if (jo_gap > p6_w_perf_vbl_jo_max)
            p6_w_perf_vbl_jo_max = jo_gap;
    }
    // swap-cadence MEASURE (#246): FRT elapsed in the jo loop body (slSynch +
    // jo_vdp1_buffer_reset + slUnitMatrix) since the PREVIOUS frame's end. < 1 vbl
    // so single-wrap-safe. compute-FRT-sum + this = the master's per-frame work;
    // if it exceeds 16.7ms the frame is 2 vbl (MEASURED cause, not a guess).
    {
        unsigned short _fs = p6_perf_frt_get();
        int32 _sf = P6_FRT_DELTA(p6_perf_frt_prev_end, _fs);
        p6_w_perf_synch_frt = _sf;
        if (_sf > p6_w_perf_synch_max) p6_w_perf_synch_max = _sf;
    }
    // STEP B (#246): zero the per-frame VDP1 workload accumulators before
    // DrawLists emits this frame's sprite commands (the capture then holds this
    // frame's box-area vs content-area -> the 64x64-overdraw factor).
    p6_vdp1_perf_reset();
    currentScreen = &screens[0];
    for (int32 g = 0; g < DRAWGROUP_COUNT; ++g)
        engine.drawGroupVisible[g] = true;

#if defined(P6_WARP_BRIDGE_TEST)
    // #181 debug WARP: pin SLOT_PLAYER1 onto the first GHZ1 bridge so its planks
    // are centered on-screen for a rendered-plank screenshot. p6_brg_witness()
    // (p6_wave1_reg.c) latches the first bridge's world pos into p6_w_brg_posx/posy
    // by frame 1. Pin X EVERY frame (the camera then stays on the bridge despite
    // the Mania drop-autorun); drop Y ONCE from 32px above so the player falls onto
    // the planks -> Bridge_HandleCollisions stands him at the bridge center -> the
    // bridge visibly depresses. Diag-only (P6_WARP_BRIDGE) -- the shipping build
    // leaves the macro undefined and never warps. Runs before ProcessObjects so the
    // camera + Bridge_Update see the pinned position THIS frame.
    if (p6_w_brg_posx != 0) {
        EntityBase *wplr = RSDK_ENTITY_AT(0);
        wplr->position.x = p6_w_brg_posx;
        static int32 s_brg_dropped = 0;
        if (!s_brg_dropped) {
            wplr->position.y = p6_w_brg_posy - 0x200000; // 32px above (16.16 fixed)
            s_brg_dropped    = 1;
        }
    }
#endif

    // P6.8 F.3 (Task #235): point the pack `Ring` global at the overlay's
    // registered Ring object so SignPost's sparkle CREATE_ENTITY(Ring,...) has a
    // valid classID (the pack Ring is otherwise NULL -- the P6.7 overlay seam).
    // Cheap per-frame; robust across scene reloads. Witness = the wired classID.
    if (s_ovl.staticvars_slot && *(void **)s_ovl.staticvars_slot) {
        Ring = *(void **)s_ovl.staticvars_slot;
        p6_w_ring_cid = (int32)*(uint16 *)Ring;
    }
    // Batch 2: same seam for Animals -- point the pack `Animals` global at the
    // overlay's registered Animals object so ActClear.c:903's foreach_active sees a
    // live classID (the pack Animals is otherwise a NULL placeholder).
    if (s_ovl.animals_slot && *(void **)s_ovl.animals_slot) {
        Animals = *(void **)s_ovl.animals_slot;
    }
    // Batch 3: same seam for ItemBox/Debris -- pack readers (Zone_StoreEntities'
    // foreach_active(ItemBox), SaveGame's broken-box recall, Shield's Debris
    // spark CREATE_ENTITY) see the overlay's live objects instead of NULL.
    if (s_ovl.itembox_slot && *(void **)s_ovl.itembox_slot)
        ItemBox = *(void **)s_ovl.itembox_slot;
    if (s_ovl.debris_slot && *(void **)s_ovl.debris_slot)
        Debris = *(void **)s_ovl.debris_slot;
    // StarPost port (2026-07-17): same seam -- pack death-respawn readers/writers
    // (SaveGame recall restore, Zone_State_ReloadScene clock store, GameOver,
    // PauseMenu restart, Player) share the overlay's live ObjectStarPost.
    if (s_ovl.starpost_slot && *(void **)s_ovl.starpost_slot)
        StarPost = *(void **)s_ovl.starpost_slot;

    // Per-section attribution: FRT (sub-78ms precision) + VBLANK (overflow-immune
    // for >78ms sections -- the real discriminator). vb0/vb1 = true-60Hz tally.
    unsigned int vb0, vb1;
    vb0 = p6_perf_vbl_count; t0 = p6_perf_frt_get();
    // Phase 2c (#246): head = entry -> ProcessInput start (the per-frame setup:
    // synch+vbl bookkeeping, p6_vdp1_perf_reset, drawGroupVisible, Ring rewire).
    p6_w_perf_head_frt = P6_FRT_DELTA(frame_t0, t0);
    ProcessInput();
    t1 = p6_perf_frt_get(); vb1 = p6_perf_vbl_count;
    p6_w_perf_cyc_input = P6_FRT_DELTA(t0, t1); p6_w_perf_vbl_input = (int32)(vb1 - vb0);
    // Phase 2d: reset the per-classID Update timing accumulators before the loop
    // (Object.cpp fills them when built -DP6_PERF_OBJPROF; harmless 0s otherwise).
    for (int32 _z = 0; _z < 64; ++_z) { p6_w_objupd_vbl[_z] = 0; p6_w_objupd_n[_z] = 0; p6_w_objupd_us[_z] = 0; }
    {
        // Phase 2f: SaturnLayout re-inflates DURING ProcessObjects -- if the
        // Player's collision sensors re-window the band store, this is the 194ms
        // (the same root cause as the present's redundant inflate).
        extern int p6_w_lay_refills;
        int32 _r0 = p6_w_lay_refills;
        vb0 = p6_perf_vbl_count; t0 = p6_perf_frt_get(); ProcessObjects();
        t1 = p6_perf_frt_get(); vb1 = p6_perf_vbl_count;
        p6_w_obj_refills = p6_w_lay_refills - _r0;
    }
    p6_w_perf_cyc_obj = P6_FRT_DELTA(t0, t1); p6_w_perf_vbl_obj = (int32)(vb1 - vb0);
    // #P0 (GHZ1 parity): tick the scene clock. ProcessEngine calls ProcessSceneTimer
    // each gameplay frame (RetroEngine.cpp:394,402,481); p6_ghz_frame replaces that
    // loop and omitted it -> the HUD timer was frozen at 0'00"00. Gated internally by
    // sceneInfo.timeEnabled (Zone_Create sets it true, Zone.c:820/857). Negligible cost.
    ProcessSceneTimer();
    p6_w_time_enabled = (int32)sceneInfo.timeEnabled;
    p6_w_timer        = (int32)(sceneInfo.seconds * 100 + sceneInfo.milliseconds);
    // #254 anim-pool funding witness: the live DATASET_STG ceiling (92 KB unfunded ->
    // 150 KB with P6_CART_TMP). dataStorage is in scope here (same as sceneInfo); no
    // cross-TU/namespace write (Storage.cpp is namespace RSDK -> would mis-bind).
    p6_w_stg_limit    = (int32)dataStorage[DATASET_STG].storageLimit;
#if defined(P6_GHZ2_BOOT)
    // #237: while GHZ2 is the live scene, does the player run on solid ground or
    // fall into empty? ProcessObjects just moved the player + refilled the
    // collision windows at its position, so sample the FG-Low tile under its feet
    // (typ+1), track the fall depth, and latch the scene GHZ2 advances to.
    if (p6_w_ghz2_loaded && p6_w_ghz2_exit_lp < 0) {
        EntityBase *plr = RSDK_ENTITY_AT(0);
        int32 yy  = (int32)(plr->position.y >> 16);
        if (yy > p6_w_ghz2_max_plry) p6_w_ghz2_max_plry = yy;
        p6_w_ghz2_collplane  = (int32)plr->collisionPlane;
        p6_w_ghz2_colllayers = (int32)plr->collisionLayers;
        p6_w_ghz2_tilecoll   = (int32)plr->tileCollisions;

        // #237 DECISIVE: faithful FindFloorPosition replica (Collision.cpp:2162-
        // 2230) at the player's FEET, EVERY tick. The old scan above conflated
        // "a tile exists (!=0xFFFF)" with "a floor exists (tile & solid)" -- the
        // GHZ2 spawn FG-Low tile 0x2001 is bit13 (ceiling-A) ONLY, NOT bit12
        // (floor-A), so a falling player passes through it. The actual spawn
        // floor is FG-High 0x7413 (bit12 set). This replica asks the engine's
        // EXACT question: across every collisionLayer the player carries, with
        // the player's plane+tileCollisions, is there a tile under the feet with
        // the floor-solid bit set? floorlayer stays -1 across the whole descent
        // == the engine never sees a floor == fall-through PROVEN (and the raw
        // FG-Low/High tiles + solidmask + colllayers say WHY).
        {
            // solid mask: REV0U down(=1)->plane?bit14:bit12 ; up->plane?bit15:bit13
            int32 plane = (int32)plr->collisionPlane;
            int32 tc    = (int32)plr->tileCollisions;
            int32 solid = (tc == 1) ? (plane ? 0x4000 : 0x1000)
                                    : (plane ? 0x8000 : 0x2000);
            // feet column; FG layer position is 0 so tile coords are absolute.
            int32 fx  = (int32)(plr->position.x >> 16);
            int32 fy  = (int32)(plr->position.y >> 16) + 19; // Sonic box bottom
            int32 ftx = fx / 16;
            int32 fty = fy / 16;
            // Resolve the FG-High layer index once (saturnSlot 1 = the floor at
            // the GHZ2 spawn); the reader checks it against collisionLayers.
            if (p6_w_ghz2_fghigh_idx < 0) {
                for (int32 l = 0; l < 8; ++l)
                    if (tileLayers[l].saturnSlot == 1) p6_w_ghz2_fghigh_idx = l;
            }
            // The engine's exact loop: for each collisionLayer, read the feet
            // tile (and the two rows below, matching the 3-step cy sweep) and
            // test floor-solidity. tc==0 == tileCollisions OFF == the engine
            // never runs FindFloorPosition for this entity (it falls); count
            // those ticks so "collision disabled" is distinguishable from
            // "collision on but no floor under the feet".
            int32 found = -1;
            if (tc == 0) {
                ++p6_w_ghz2_tcoff;
            } else {
                for (int32 l = 0; l < 8 && found < 0; ++l) {
                    if (!((int32)plr->collisionLayers & (1 << l))) continue;
                    int32 slot = tileLayers[l].saturnSlot;
                    int32 lx   = ftx - (int32)(tileLayers[l].position.x / 16);
                    int32 lyt  = fty - (int32)(tileLayers[l].position.y / 16);
                    for (int32 d = -1; d <= 2 && found < 0; ++d) {
                        uint16 tile;
                        if (tileLayers[l].layout)
                            tile = 0xFFFF; // small native layer (memset 0xFF) = empty
                        else if (slot >= 0)
                            tile = SaturnLayout_GetTile(slot, lx, lyt + d);
                        else
                            continue;
                        if (tile != 0xFFFF && (tile & solid)) found = l;
                    }
                }
                if (found >= 0) {
                    if (p6_w_ghz2_floorlayer < 0) p6_w_ghz2_floorlayer = found;
                } else if (p6_w_ghz2_floorlayer == -2) {
                    p6_w_ghz2_floorlayer = -1; // collision ON, but no floor yet
                }
            }
            // Snapshot the raw feet tiles + velocity ONCE, the first tick the
            // feet enter the spawn floor band (ty 84..93) -- the moment a working
            // collision WOULD catch him.
            if (p6_w_ghz2_feetty < 0 && fty >= 84 && fty <= 93) {
                p6_w_ghz2_feetty     = fty;
                p6_w_ghz2_vely       = (int32)(plr->velocity.y >> 8);
                p6_w_ghz2_fglow_tile = (int32)(unsigned)SaturnLayout_GetTile(0, ftx, fty);
                p6_w_ghz2_fghigh_tile= (int32)(unsigned)SaturnLayout_GetTile(1, ftx, fty);
            }
            (void)found;
        }

        // #237: measure which layer slot 1 is CURRENTLY bound to (the present
        // rebinds it). Should be the FG-High index (5 for GHZ2); if it reads 4
        // (FG-Low) the present clobbered the collision binding -> fall-through.
        p6_w_ghz2_slot1layer = (int32)SaturnLayout_SlotLayer(1);

        ++p6_w_ghz2_play_frames;
        if ((int32)sceneInfo.listPos != p6_w_ghz2_listpos)
            p6_w_ghz2_exit_lp = (int32)sceneInfo.listPos;
    }
#endif
    // A2 (dual-SH2): fork the FG present-compute onto the SLAVE now -- after
    // ProcessObjects (collision done -> SaturnLayout slots free) and BEFORE
    // DrawLists, so the slave's ~4.6ms present runs in parallel with the master's
    // ~10.2ms DrawLists (hiding under it) -> master frame 23.4 -> ~18.8ms. Camera
    // is current (ProcessObjects already moved it this frame).
    {
        // Phase 2c (#246): bracket the slave fork dispatch (slSlaveFunc kick) --
        // unbracketed before, a prime suspect for the ~8ms gap (the SGL slave-CPU
        // start cost is paid on the MASTER critical path here, before DrawLists).
        unsigned short _k0 = p6_perf_frt_get();
        p6_present_kick(p6_fglow_layer_index() /* per-zone FG Low */,
                        screens[0].position.x, screens[0].position.y,
                        (const unsigned short *)fullPalette[0]);
        p6_w_perf_kick_frt = P6_FRT_DELTA(_k0, p6_perf_frt_get());
    }

    vb0 = p6_perf_vbl_count; t0 = p6_perf_frt_get(); ProcessObjectDrawLists();
    t1 = p6_perf_frt_get(); vb1 = p6_perf_vbl_count;
    p6_w_perf_cyc_draw = P6_FRT_DELTA(t0, t1); p6_w_perf_vbl_draw = (int32)(vb1 - vb0);

    // A2: join the slave present (slCashPurge -> master sees its WRAM writes) +
    // run the master-only NBG1 register config. The present FRT now measures only
    // the join wait (~0 -- the slave's 4.6ms finished under DrawLists' 10.2ms) +
    // the register tail; the 4.6ms is OFF the master critical path.
    vb0 = p6_perf_vbl_count; t0 = p6_perf_frt_get();
    p6_present_join_config(screens[0].position.x, screens[0].position.y);
    t1 = p6_perf_frt_get(); vb1 = p6_perf_vbl_count;
    p6_w_perf_cyc_present = P6_FRT_DELTA(t0, t1); p6_w_perf_vbl_present = (int32)(vb1 - vb0);
    // #243 band-crossing: the present-join waits on the SLAVE's FG band inflate; it
    // is ~0 normally but spikes on a section crossing -> the worst-join witness.
    if (p6_w_cont_frames > 2 && p6_w_perf_cyc_present > p6_w_xing_present_max)
        p6_w_xing_present_max = p6_w_perf_cyc_present;
    p6_w_perf_cyc_total   = p6_w_perf_cyc_input + p6_w_perf_cyc_obj
                          + p6_w_perf_cyc_draw + p6_w_perf_cyc_present;

#ifndef P6_PERF_NOSCAN
    // Phase 2d census: in-range entity population by class (read-only engine
    // state; the histogram is local + the loop is ~1216 cheap reads << 1 vbl).
    // #246 MEASURED: this whole diagnostic block (census + ActClear latch + hog
    // locator) is TWO MORE full ENTITY_COUNT(2368) scans over a ~12-live table =
    // the 5.08ms TAIL. It has NO render/gameplay side-effect (read-only + witness
    // writes only). Gate it behind P6_PERF_NOSCAN so the shipping frame can skip
    // it; build -e P6_NOSCAN=1 -> compute 19.8->~14.7ms < the 16.67ms vblank, a
    // falsifiable RED->GREEN test of compute-bound vs VDP1-bound.
    {
        static int16 census[256];
        int32 i, total = 0, distinct = 0, topc = -1, topn = 0;
        for (i = 0; i < 256; ++i) census[i] = 0;
        for (i = 0; i < ENTITY_COUNT; ++i) {
            EntityBase *e = RSDK_ENTITY_AT(i);
            if (e->classID && e->inRange) {
                ++total;
                uint16 cid = e->classID & 0xFF;
                if (census[cid] == 0) ++distinct;
                if (++census[cid] > topn) { topn = census[cid]; topc = (int32)cid; }
            }
            // F.2-followup warp probe: record the active state of an entity
            // sitting AT the GHZ1 signpost x (15792px, both signposts) -- not
            // the player (slot 0). ACTIVE_BOUNDS(4) until the warped player
            // crosses it, ACTIVE_NORMAL(2) once SignPost_CheckTouch fires.
            // F.3: identify the REAL SignPost by its state POINTER (the prior
            // x-band-only match caught a different entity). Only record when the
            // entity's state @off sizeof(Entity) lands in the SignPost .text
            // range. Reuse witnesses (no new .bss -> _end stays under the floor):
            //   sign_state=state; warp_signactive=active; ac_state=ever-Spin flag;
            //   ac_timer=min spinCount (off 128) seen (0 => it spawned ActClear).
            // F.5: locate SignPost entities by object classID (drift-proof; the
            // prior state-ptr text-range match broke on rebuild drift). Record the
            // count + the last RUNPAST/DROP one's type/state/active/posx so the
            // gate can ground-truth GHZ1's actual signpost setup.
            if (i != 0 && e->classID && SignPost
                && e->classID == (uint16)*(uint16 *)SignPost) {
                uint8 *body  = (uint8 *)e + sizeof(Entity);
                int32 sstate = *(int32 *)body;
                int32 stype  = (int32)body[4]; // EntitySignPost.type (uint8 @ +4)
                ++p6_w_sign_count;
                if (stype <= 1) { // RUNPAST/DROP = the real end-of-act signpost
                    p6_w_sign_state      = sstate;
                    p6_w_warp_signactive = (int32)e->active;
                    p6_w_sign_type       = stype;
                    p6_w_sign_posx       = (int32)e->position.x;
                }
                // STICKY proof of the canonical crossing: ACTIVE_NORMAL(2) is set
                // by SignPost_CheckTouch (SignPost.c:326) when player.x>signpost.x.
                if ((int32)e->active == 2) p6_w_sign_crossed = 1;
#if defined(P6_WARP_TEST)
                // Canonical trigger by type (one-shot, at cont_frames>=120), so the
                // act-clear chain runs from the REAL entry, not a scripted spawn:
                //   RUNPAST(0): relocate beside the player -> CheckTouch crossing
                //               (player.x>signpost.x) -> State_Spin (SignPost.c:416).
                //   DROP(1):    set state=SignPost_State_Falling + drop 64px above
                //               the player's ground (mirrors GHZ_DDWrecker.c:921) ->
                //               land -> State_Spin (SignPost.c:581).
                // Either path ends in SignPost's OWN ResetEntitySlot(SLOT_ACTCLEAR)
                // (SignPost.c:452).
                static int32 s_sign_reloc = 0;
                if (!s_sign_reloc && p6_w_cont_frames >= 120 && stype <= 1) {
                    EntityBase *plr = RSDK_ENTITY_AT(0);
                    if (stype == 0) {
                        e->position.x = plr->position.x - 0x80000; // -TO_FIXED(8)
                        e->position.y = plr->position.y;
                    } else {
                        e->position.x = plr->position.x;
                        e->position.y = plr->position.y - 0x400000; // 64px above
                        *(void **)body = (void *)SignPost_State_Falling;
                        e->active      = 2; // ACTIVE_NORMAL so it ticks every frame
                    }
                    s_sign_reloc = 1;
                }
#endif
            }
        }
        p6_w_obj_inrange  = total;
        p6_w_obj_topclass = topc;
        p6_w_obj_topcount = topn;
        p6_w_obj_classcnt = distinct;

        // Task #234: did ActClear spawn (SLOT_ACTCLEAR=16), and which state is
        // it stuck in? state is the first field after RSDK_ENTITY in
        // EntityActClear -> offset sizeof(Entity); timer is the int32 after it.
        {
            // STICKY latch: ActClear spawns transiently (it triggers the
            // Zone reload, after which the slot is empty again). A plain
            // per-frame read misses it whenever the snapshot lands in the
            // reloaded phase. So latch the FIRST non-zero spawn + count the
            // frames it was alive, and keep the LAST live state pointer.
            EntityBase *ac = RSDK_ENTITY_AT(16);
            if (ac->classID) {
                if (p6_w_ac_classid <= 0) p6_w_ac_classid = (int32)ac->classID;
                ++p6_w_ac_frames;
                uint8 *acp = (uint8 *)ac + sizeof(Entity);
                p6_w_ac_state = *(int32 *)acp;        // state fn pointer (32-bit)
                p6_w_ac_laststate = p6_w_ac_state;    // latch last live ActClear state
                p6_w_ac_timer = *(int32 *)(acp + 4);  // EntityActClear.timer
            }
            // F.4 diag: did ActClear's ++SceneInfo->listPos ever fire? Sticky max
            // (GHZ1=listPos N, GHZ2=N+1). If listpos_max never advances, ActClear
            // never reached State_SaveGameProgress; if it does but lay_bytes stays
            // GHZ1, the band-store mount ignored the advanced listPos.
            if ((int32)sceneInfo.listPos > p6_w_listpos_max)
                p6_w_listpos_max = (int32)sceneInfo.listPos;
            // Is the ActClear Object registered with a live classID? If 0, the
            // SignPost.c:452 ResetEntitySlot(SLOT_ACTCLEAR, ActClear->classID,..)
            // spawns NOTHING (classID 0 = blank slot) -> the act never advances.
            p6_w_ac_objcid = ActClear ? (int32)*(uint16 *)ActClear : -1;
        }

        // Phase 2h: scan the per-classID Update-timing accumulators for the hog,
        // ranked by FRT TICKS (us[]) -- the vbl[] profiler saturates to 0 once
        // the frame fits ~1 vblank. Keep vbl as a back-compat tie-break readout.
        int32 hc = -1, hu = 0, hn = 0, hv = 0;
        for (i = 0; i < 64; ++i)
            if (p6_w_objupd_us[i] > hu) {
                hu = p6_w_objupd_us[i]; hc = i;
                hn = p6_w_objupd_n[i]; hv = p6_w_objupd_vbl[i];
            }
        p6_w_objupd_topclass = hc;
        p6_w_objupd_topvbl   = hv;
        p6_w_objupd_topus    = hu;
        p6_w_objupd_topn     = hn;

        // Phase 2e: locate the hog -- record the first in-range entity whose
        // classID&0x3F == hog so its full classID + world pos identify it
        // (match against the GHZ Act 1 entity layout).
        p6_w_hog_cid = -1; p6_w_hog_x = 0; p6_w_hog_y = 0;
        if (hc >= 0)
            for (i = 0; i < ENTITY_COUNT; ++i) {
                EntityBase *e = RSDK_ENTITY_AT(i);
                if (e->classID && e->inRange && (e->classID & 0x3F) == hc) {
                    p6_w_hog_cid = (int32)e->classID;
                    p6_w_hog_x = e->position.x; p6_w_hog_y = e->position.y;
                    break;
                }
            }
    }
#endif /* P6_PERF_NOSCAN -- skip the diagnostic census/hog scans in the timed frame */

#if defined(P6_GHZ_AUTORUN)
    // Signpost campaign bridge-1 forensics: while SLOT_PLAYER1 crosses the
    // bridge-1 window (x 800..1500 px), scan the pool each frame for a live
    // Bridge entity at the authored bridge-1 x (1184 px, Scene1.bin) and
    // record liveness/active. Diagnostic flavor only; full-pool scan is the
    // measured ~ms class but bounded to ~120 frames of the crossing.
    {
        EntityBase *p0 = RSDK_ENTITY_AT(0);
        int32 plrx = p0->position.x >> 16;
        if (p0->classID && plrx > 800 && plrx < 1500 && p6_w_brg_classid > 0) {
            int32 live = 0, act = -1;
            for (int32 bi = 0; bi < ENTITY_COUNT; ++bi) {
                EntityBase *be = RSDK_ENTITY_AT(bi);
                if (be->classID == (uint16)p6_w_brg_classid) {
                    int32 bx = be->position.x >> 16;
                    if (bx > 1150 && bx < 1220 && (be->position.y >> 16) < 1200) {
                        live = 1;
                        act  = (int32)be->active;
                        break;
                    }
                }
            }
            p6_w_arun_brg_live   = live;
            p6_w_arun_brg_active = act;
            if (live && p6_w_arun_brg_firstx < 0)
                p6_w_arun_brg_firstx = plrx;
            if (plrx >= 1080 && plrx <= 1290) {
                ++p6_w_arun_inspan;
                if (!live)
                    ++p6_w_arun_brg_gapmiss;
            }
        }
    }
#endif
    ++p6_w_cont_frames;
#if defined(P6_SHADOW_COMPARE)
    // Arm the scan-split parity proof only once gameplay is live (avoids the load-
    // phase hang). If cont_frames freezes at ~10, the shadow itself hangs in-gameplay;
    // if it climbs, the hang was load-phase and the divergence measure is valid.
    if (p6_w_cont_frames > 10) g_p6_shadow_enable = 1;
#endif

    // True-fps tally: snapshot the hardware-60Hz vblank counter + the per-frame
    // slip (vblanks elapsed since the previous rendered frame; 1 == locked 60).
    {
        unsigned int vnow = p6_perf_vbl_count;
        int32 slip = (int32)(vnow - p6_perf_vbl_prev);
        p6_w_perf_vbl_frame = (int32)(vnow - vbl_frame_start); // engine work this frame
        p6_perf_vbl_prev = vnow;
        p6_w_perf_vblanks = (int32)vnow;
        p6_w_perf_frames  = p6_w_cont_frames;
        if (slip > p6_w_perf_vbl_max)
            p6_w_perf_vbl_max = slip;
        if (p6_w_perf_cks < 0)
            p6_w_perf_cks = p6_perf_frt_cks();
    }

    // Phase 1b (#243): VDP1 draw-completion at compute-done -- the 2-VBLANK-LOCK
    // discriminator. This is the LATEST point before the implicit slSynch (the jo
    // loop calls slSynch immediately after this callback returns, core.c:632).
    // EDSR.CEF (bit1) reports whether VDP1 has finished rasterizing the PRIOR
    // frame's sprite command list (kicked by the previous slSynch's PTMR):
    // CEF=0 => still drawing => slSynch's swap waits on VDP1 (DRAW-BOUND); CEF=1
    // => VDP1 idle => the 2nd vblank is swap cadence. COPR/LOPR (when busy) size
    // the overrun. Read-only VDP1 register peek (vdp1-reference.md:20-22,151-153).
    {
        unsigned short edsr = p6_perf_vdp1_edsr();
        p6_w_perf_v1_edsr = (int32)edsr;
        if (edsr & 0x0002u) {            // CEF set => VDP1 finished the prior frame
            ++p6_w_perf_v1_done;
        } else {                         // CEF clear => VDP1 still drawing at slSynch
            ++p6_w_perf_v1_busy;
            p6_w_perf_v1_copr = (int32)p6_perf_vdp1_copr();
            p6_w_perf_v1_lopr = (int32)p6_perf_vdp1_lopr();
        }
    }

    p6_cont_witness(); // SLOT_PLAYER1 pos + animator.animationID
    // O1 (#254): the overlay's combined witness covers Ring + Spring + Bridge +
    // PlaneSwitch -- their p6_brg/loop witnesses MIGRATED into the overlay WITH the
    // objects (step 2), so the resident pack no longer names those globals. It MUST
    // run in the SHIPPING per-frame loop here (the other s_ovl.witness_fn call site
    // is the diag-proof function, which the continuous GHZ loop never enters --
    // MEASURED in step 1). Ring-slot arg is read-safe (classID 0 if empty).
    if (s_ovl.witness_fn)
        s_ovl.witness_fn(RSDK_ENTITY_AT(P6_OBJ_RING_SLOT));
    {
        // Phase 2c (#246): compute-FULL bracket END + tail sub-attribution. frame_t1
        // is the exit FRT (also the swap-cadence prev_end). full = entry->exit = the
        // MEASURED master compute (replaces the derivation). tail = present-end -> exit
        // (t1 still holds p6_present_join_config's end FRT): the census + ActClear
        // latch + hog scan + fps tally + EDSR peek + p6_cont_witness.
        unsigned short frame_t1 = p6_perf_frt_get();
        p6_perf_frt_prev_end = frame_t1; // swap-cadence: FRT at this frame's END
        p6_w_perf_full_frt = P6_FRT_DELTA(frame_t0, frame_t1);
        if (p6_w_perf_full_frt > p6_w_perf_full_max)
            p6_w_perf_full_max = p6_w_perf_full_frt;
        p6_w_perf_tail_frt = P6_FRT_DELTA(t1, frame_t1);
        // #243 band-crossing stall: a crossing = obj_refills>0 (SaturnLayout FG +
        // collision band re-inflate). Track count + worst compute-full on crossing
        // frames (excl. boot) -- the user-felt "slow as I move forward" target.
        if (p6_w_cont_frames > 2 && p6_w_obj_refills > 0) {
            ++p6_w_xing_count;
            if (p6_w_perf_full_frt > p6_w_xing_max_frt)
                p6_w_xing_max_frt = p6_w_perf_full_frt;
        }
    }

    // #249/#250: the per-frame packed-collision PIN hash (a 64 KB djb2 over
    // 0x060E0000 every frame until divergence) is REMOVED -- it cost ~10.7 ms/frame
    // (16.86 fps measured) and only existed to localize WHEN the corruption hit.
    // ROOT CAUSE is fixed (resident pre-inflate deferred AFTER LoadTileConfig);
    // don't-regress is the zero-runtime-cost qa_p6_collgeom capture gate + the
    // one-time load-phase p6_w_col_t1hash. No per-frame collision hashing ships.
}

// Task #238: 4MB Extended RAM Cart probe. Write distinct sentinels to bank0/bank1
// start + last longword via the cache-through A-Bus alias (0x22400000/0x22600000,
// = 0x024xxxxx|0x20000000) so the SH-2 cache is bypassed and we test the physical
// cart RAM. ok bit0/bit1 set when both the start and last-longword readbacks match
// per bank -> p6_w_cart_ok==3 confirms a contiguous, RW-correct 4MB. If the cart is
// absent / Mednafen ss.cart!=extram4, writes hit open bus -> readback mismatch -> 0.
static void p6_cart_probe(void)
{
    volatile uint32 *b0 = (volatile uint32 *)0x22400000u; // bank0, cache-through
    volatile uint32 *b1 = (volatile uint32 *)0x22600000u; // bank1, cache-through
    const uint32 S0 = 0xC0DECAFEu, S1 = 0x5A7064A7u;
    const uint32 LAST = 0x7FFFFu; // last longword index in a 2MB bank (0x200000/4-1)
    b0[0] = S0;       b0[LAST] = S0 ^ 0x0F0F0F0Fu;
    b1[0] = S1;       b1[LAST] = S1 ^ 0xF0F0F0F0u;
    p6_w_cart_rb0 = (int32)b0[0];
    p6_w_cart_rb1 = (int32)b1[0];
    int32 ok = 0;
    if (b0[0] == S0 && b0[LAST] == (S0 ^ 0x0F0F0F0Fu)) ok |= 1;
    if (b1[0] == S1 && b1[LAST] == (S1 ^ 0xF0F0F0F0u)) ok |= 2;
    p6_w_cart_ok = ok;
}

// =============================================================================
// I3b 2b -- the camera-local-pool MATERIALIZE (write side). Reconstructs scene
// entity `logical_slot` from the offline DORM store (cart 0x226C8000; big-endian
// header/index/records + raw LE Scene.bin var-bytes) into `dest_slot`. Mirrors the
// engine placement loop Scene.cpp:585-806 EXACTLY: classID resolve by LE-ASSEMBLED
// hash (Reader.hpp ReadInt32(...,false) == the little-endian value on the big-endian
// SH-2 -- a RAW 16-byte memcmp of the store hash would FAIL), serialize() rebuilds
// editableVarList, var-hash->offset match, var-value LE replay into the matched field.
// NO Create yet (placement state only) -- the smallest write-side proof, before any
// pool resize. Reaches engine symbols via `using namespace RSDK` (line 54). Witnesses
// are read BACK here (scratch entity lifetime = a few us; zero interaction).
// =============================================================================
// I3b 2b -> CART OVERLAY relocation: the materialize's BULK (DORM navigation + var-replay) now
// lives in the GHZ overlay (p6_ovl_ghz.c p6_ovl_materialize) -- new engine code goes to the cart per
// the residency rule, freeing WRAM-H for the pool-shrink streaming manager. The overlay can't name
// C++-mangled engine symbols (flat-TU rule), so the ENGINE-TOUCHING ops stay HERE as thin extern "C"
// thunks the overlay calls directly (ld -R game.elf resolves them; -u-rooted so they survive the pack
// gc). The proven logic (qa_p6_materialize_write GREEN @ 55c77e5) is UNCHANGED -- only its home moved.
// p6_mat_loadhash LE-assembles a 16-B md5 hash into uint32[4] (== 4x ReadInt32(...,false) on BE SH-2).
static __attribute__((noinline)) uint32 p6_mat_le32(const uint8 *p) { return ((uint32)p[3] << 24) | ((uint32)p[2] << 16) | ((uint32)p[1] << 8) | p[0]; }
static __attribute__((noinline)) void p6_mat_loadhash(uint32 *out, const uint8 *le)
{
    out[0] = p6_mat_le32(le + 0); out[1] = p6_mat_le32(le + 4);
    out[2] = p6_mat_le32(le + 8); out[3] = p6_mat_le32(le + 12);
}

// classID resolve (Scene.cpp:585-589) + latch the classCount witness (the timing self-check).
extern "C" __attribute__((used)) int32 p6_eng_classid_resolve(const uint8 *objhash_le)
{
    uint32 h[4];
    p6_mat_loadhash(h, objhash_le);
    p6_w_mat_classcount = (int32)sceneInfo.classCount;
    for (int32 o = 0; o < sceneInfo.classCount; ++o)
        if (memcmp(h, objectClassList[stageObjectIDs[o]].hash, 16) == 0)
            return o;
    return 0;
}

// serialize()-rebuild editableVarList for classID (Scene.cpp:602-614). LoadScene FREES editableVarList
// (-> NULL) at its end (Scene.cpp:855), so point it at a CART scratch (DORM-window free tail, past the
// 42KB store) for the serialize()+match; p6_eng_serialize_end restores. (MEASURED: skipping this gave
// nmatch=0 though classID resolved -- the proven 55c77e5 fix, unchanged.)
static EditableVarInfo *s_mat_saved_evl = NULL;
static int32            s_mat_saved_evc = 0;
extern "C" __attribute__((used)) void p6_eng_serialize_begin(int32 classID)
{
    s_mat_saved_evl  = editableVarList;
    s_mat_saved_evc  = editableVarCount;
    editableVarList  = (EditableVarInfo *)0x226D4000u; // cart scratch (DORM window free tail)
    editableVarCount = 0;
#if RETRO_REV02
    SetEditableVar(VAR_UINT8, "filter", (uint8)classID, offsetof(Entity, filter));
#endif
    ObjectClass *oc = &objectClassList[stageObjectIDs[classID]];
    if (oc->serialize)
        oc->serialize();
}

// match a placement var-hash to its editableVarList offset (Scene.cpp:625-633); -1 = unmatched.
extern "C" __attribute__((used)) int32 p6_eng_var_offset(const uint8 *varhash_le)
{
    uint32 h[4];
    p6_mat_loadhash(h, varhash_le);
    for (int32 v = 0; v < editableVarCount; ++v)
        if (memcmp(h, editableVarList[v].hash, 16) == 0)
            return editableVarList[v].offset;
    return -1;
}

extern "C" __attribute__((used)) void p6_eng_serialize_end(void)
{
    editableVarList  = s_mat_saved_evl;   // restore LoadScene's freed NULL
    editableVarCount = s_mat_saved_evc;
}

// RSDK_ENTITY_AT(slot) + clean it (NOT ResetEntitySlot) -> the scratch dest the overlay fills.
extern "C" __attribute__((used)) void *p6_eng_entity_prepare(int32 slot)
{
    EntityBase *e = RSDK_ENTITY_AT(slot);
    memset(e, 0, sizeof(EntityBase));
    return (void *)e;
}

// write the placement fields (Scene.cpp:666-671) -- the engine EntityBase layout stays pack-side.
extern "C" __attribute__((used)) void p6_eng_write_placement(void *ent, int32 classID, int32 px, int32 py)
{
    EntityBase *e = (EntityBase *)ent;
    e->classID = classID;
#if RETRO_REV02
    e->filter = 0xFF;
#endif
    e->position.x = px;
    e->position.y = py;
}

// R3.1 (#305): factor the IDENTICAL load->Stage->SetHash->MakeResident banded-sheet
// staging blocks (LOGOS/TLOGO/MAINICON/TEXTEN/AIZOBJ -- all 0x10000-buffer sheets) into
// one helper. This NET-REDUCES p6_scene_run .text (4 inline copies collapse to 1 fn +
// calls) -- the WRAM-H reclaim that lets the AIZOBJ.SHT staging fit UNDER the front-end
// heap limit (a naive AIZOBJ inline block + diag pushed _end 0x060BA360 -> #228 boot
// trap, MEASURED blue-screen). Returns the staged SaturnSheet slot (>=0) or -1.
// textBuffer + GEN_HASH_MD5 resolve via the file-scope `using namespace RSDK`.
// ((unused)) so the DEFAULT (GHZ) build -- which compiles this TU but guards out all 5
// callers -- doesn't -Werror on the unused static + gc-sections drops it (GHZ _end
// byte-identical). The front-end builds DO call it, so it is kept + used there.
// SaturnSheet_* are extern "C" (SaturnSheet.cpp:178/217/276); declare them at FILE scope
// (extern "C" is ILLEGAL at block scope -- MEASURED "expected unqualified-id") so this
// plain-C++ helper resolves the UNMANGLED C symbols (else the refs mangle -> undefined).
extern "C" {
int32 SaturnSheet_Stage(const void *blob, uint32 bytes);
uint32 SaturnSheet_BandCursor(void); // title-Sonic overflow diag
uint32 SaturnSheet_BandBase(void);
uint32 SaturnSheet_BandEnd(void);
void SaturnSheet_SetHash(int32 slot, const uint32 *hash);
int32 SaturnSheet_MakeResident(int32 slot);
void  SaturnSheet_ResReset(void); // #317 draw/inflate hog: reclaim dead RES at the seam
void  SaturnSheet_BandReset(void); // Water M1b: reclaim dead front-end band debris at the GHZ seam
void  SaturnSheet_SetScratch(void **bufp, uint32 cap); // #317: inflate scratch
int32 SaturnSheet_FetchRect(int32 slot, int32 sx, int32 sy,
                            int32 w, int32 h, uint8 *dst); // #311 fetch bisect
#if defined(P6_GHZCUT_BOOT)
void p6_vdp1_pal_remirror(const unsigned short *pal565);   // #311 mech-5 live sprite palette
#endif
#if defined(P6_FRONTEND_TITLE)
void slDMAWait(void);   // #313 fix: complete the frame's async slDMACopy before slSynch
void slInitSprite(void);// #313 recovery: re-derive the SGL sprite pipeline when dead
#endif
}
__attribute__((unused)) static int32 p6_stage_sheet_hash(const char *shtFile, const char *gifPath)
{
    int sn = rsdk_storage_load_to_lwram(shtFile, (void *)P6_LW_ENTITYLIST, 0x10000);
    if (sn <= 0)
        return -1;
    int32 slot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)sn);
    if (slot < 0)
        return -1;
    RETRO_HASH_MD5(ph);
    GEN_HASH_MD5(gifPath, ph);
    SaturnSheet_SetHash(slot, (const uint32 *)ph);
#ifndef P6_SHT_NO_RESIDENT
    SaturnSheet_MakeResident(slot);
#endif
    return slot;
}

#if defined(P6_FRAMEDIR)
// Sprite-pipeline rework stage 1 (docs/feature_checklists/
// sprite_frame_directory.md sec 7): pre-cut FRD1 frame-directory staging.
// The .FRD is GFS-loaded DIRECTLY into the cart resident store at the
// ResAlloc cursor (peek via ResAlloc(0), cap via SaturnSheet_ResRemain) --
// blobs run up to 261,644 B (TAILS1.FRD), far past every WRAM bounce
// window, and P6_LW_ENTITYLIST is the forbidden live entity pool. Cart-
// destination GFS loads are the proven idiom (GHZOBJ.PAK -> 0x2276xxxx,
// this file). SaturnFrameDir_StageDirect claims the bytes in place (zero
// copy) + computes the one-time djb2 identity witness (qa_p6_frd F2).
// W12b LTO contract: the jo-side p6_vdp1.c gets SaturnFrameDir_Lookup as a
// RUNTIME FUNCTION POINTER only (p6_vdp1_set_frd) -- pack->jo references
// are the proven direction, never jo->pack statics. (Declarations live at
// the rsdk_storage_load_to_lwram site above -- the arm_env bind loop needs
// them before this point in the TU.)
// Stage one .FRD (idempotent by gif-path MD5). Returns the FRD slot >= 0,
// or -1 (file absent / store full / header bad) -- the caller keeps the
// sheet's MakeResident fallback in that case.
__attribute__((unused)) static int32 p6_frd_stage_file(const char *frdFile,
                                                       const char *gifPath)
{
    RETRO_HASH_MD5(fh);
    GEN_HASH_MD5(gifPath, fh);
    {
        int32 have = SaturnFrameDir_FindSlot((const uint32 *)fh);
        if (have >= 0)
            return have;
    }
    // MEASURED scanlines clobber guard (SaturnSheet_ResFloor comment): keep
    // every FRD blob above the per-frame scanline-callback window at RES_BASE.
    SaturnSheet_ResFloor(0x1000);
    uint32 dst = SaturnSheet_ResAlloc(0); // aligned-cursor peek (claims 0 B)
    uint32 cap = SaturnSheet_ResRemain();
    if (!dst || !cap)
        return -1;
    int sn = rsdk_storage_load_to_lwram(frdFile, (void *)dst, cap);
    if (sn <= 12)
        return -1;
    int32 slot = SaturnFrameDir_StageDirect((const void *)dst, (uint32)sn);
    if (slot < 0)
        return -1;
    SaturnFrameDir_SetHash(slot, (const uint32 *)fh);
    return slot;
}
// Re-attach every ALREADY-BOUND surface whose sheet has a staged FRD (the
// arm-env bind loops attach newly-bound surfaces; this covers handles that
// persist across a seam where the FRD registry was reset + re-staged).
__attribute__((unused)) static void p6_frd_attach_bound(void)
{
    for (int32 i = 0; i < SURFACE_COUNT; ++i) {
        GFXSurface *sf = &gfxSurface[i];
        if (sf->scope == SCOPE_NONE || p6_vdp1HandleBySurface[i] < 0)
            continue;
        int32 fs = SaturnFrameDir_FindSlot((const uint32 *)sf->hash);
        if (fs >= 0)
            p6_vdp1_sheet_set_frd(p6_vdp1HandleBySurface[i], fs);
    }
}
#endif // P6_FRAMEDIR

extern "C" void p6_scene_run(void)
{
    // Task #238: probe the 4MB cart FIRST (raw A-Bus RW; harmless -- the cart is
    // not yet used by any pool). Result peeked by qa_p6_cart.py before pool retarget.
    p6_cart_probe();

    // P6.7 W7: wait (bounded) for SGL's per-vblank INTBACK to populate
    // Smpc_Peripheral[0] BEFORE interrupts are masked -- the data arrives in
    // SGL's vblank handler (ST-238-R1 peripheral acquisition; ST-169-R1
    // INTBACK), and the masked load phase below freezes it. The device's
    // per-tick UpdateInput then reads the frozen (valid) snapshot memory;
    // no SMPC command is ever issued by this chain (the dual-issue rule).
    p6_input_settle();

    p6_load_phase_enter();
    P6_LT_BEGIN(); // Task #271: start the front-end load-timing cursors (masked core)
    // 0) Zero the WRAM-L windows (SLSTART zeroes only WRAM-H __bstart..__bend).
    memset((void *)P6_LW_ZERO_BASE, 0, P6_LW_ZERO_END - P6_LW_ZERO_BASE);
    // P6.7 W11: zero the WRAM-H packed-collision window in the PRE-STATE --
    // the 3d TileConfig witness hashes it (zero model at Title; step 9's
    // GHZ load fills it afterwards).
    memset((void *)P6_HW_PACKEDCOL, 0, 0x10000);
    // W12b: zero the relocated objectClassList/drawGroups/shtRect window --
    // it is NOT .bss (SLSTART zeroes only __bstart..__bend) and the engine
    // assumes zeroed class + draw lists before RegisterObject at 2b.
    memset((void *)P6_HW_GROUPWIN, 0, P6_HW_GROUPWIN_END - P6_HW_GROUPWIN);

    // 1) Engine storage pools (5 mallocs through OUR _sbrk -> WRAM-L heap).
    p6_w_load_step = 1; // #251
    p6_w_scene_initstorage = (int32)InitStorage();
    if (!p6_w_scene_initstorage) {
        p6_load_phase_exit();
        return; // loaded stays 0 -> gate RED with initstorage diagnosis
    }
    p6_w_scene_step = 1;
    P6_LT_MARK(1); // Task #271 S1: boot pre-load (memsets + InitStorage)

    // 1.5) P6.7d.3: chain-load the Ring OVERLAY -- MUST precede BOTH the
    //      registration preamble at 2b (the entry registers Ring) AND the
    //      pack mount at 2.5 (the P6.5b2 second-GFS_Open trap). Original
    //      placement at 2.4 was AFTER the preamble -- measured: guard
    //      skipped, Ring unregistered, gates V2/V3 + C2 RED.
    //      [chain-load the Ring OVERLAY into the window the P6.7d.2
    //      SGL-area re-contract freed. ORDERING IS BINDING (the P6.5b2 GFS
    //      trap): a second concurrent GFS_Open FAILS while the Data.rsdk
    //      handle is open, so this open/close must complete BEFORE the pack
    //      mounts at 2.5. After the copy: zero the window tail (the overlay's
    //      .bss) and PURGE the SH-2 cache over the window -- instruction
    //      fetches go through the cache and any stale READ line would execute
    //      garbage (SH7604: writing a 16-bit 0 to address|0x40000000
    //      invalidates that 16-byte line -- the documented cache address
    //      array mechanism).
    //      #258 CART RELOCATION: P6_OVL_BASE is now the CACHED cart alias
    //      0x02690000 (the engine executes the entry through it). The blob
    //      bytes + the .bss zero-fill + the verification hash all go through
    //      the CACHE-THROUGH twin (P6_OVL_BASE | 0x20000000u = 0x22690000) so
    //      they land in cart RAM bypassing the SH-2 cache (cart-4mb-extram-
    //      measured-map cache rule). The associative-purge loop below then
    //      clears any stale CACHED lines for 0x02690000 before the entry runs;
    //      the first exec is an I-cache miss that reads the freshly written
    //      cart RAM. Placement 0x22690000..0x22698000 is PROVEN free (layout
    //      high-water 0x22686900, sheets < 0x22600000, GFS >= 0x22700000).]
    {
        p6_w_load_step = 2; // #251 overlay
        unsigned char *w = (unsigned char *)(P6_OVL_BASE | 0x20000000u); // cache-through cart
        int n = rsdk_storage_load_to_lwram("OVLRING.BIN",
                                           (void *)w, P6_OVL_WINDOW);
        p6_w_ovl_bytes = n;
        if (n > 0) {
            for (uint32 i = (uint32)n; i < P6_OVL_WINDOW; ++i)
                w[i] = 0;                       // overlay .bss (cart RAM)
            uint32 h = 5381u;
            for (int32 i = 0; i < n; ++i)
                h = ((h << 5) + h) ^ (uint32)w[i];
            p6_w_ovl_hash = (int32)h;
            for (uint32 a = P6_OVL_BASE; a < P6_OVL_BASE + P6_OVL_WINDOW; a += 16)
                *(volatile uint16 *)(a | 0x40000000u) = 0;  // line invalidate
        }
    }

    // 1.5b) P6.8 I3b increment 2a: chain-load the OFFLINE dormant placement store
    //       (cd/<TAG>DORM.BIN, big-endian, build_dormant_store.py). DATA load -> ZERO
    //       WRAM-H capture code (the runtime capture form was #228-blocked; a cart-overlay
    //       capture is impossible since the overlay loads AFTER LoadSceneAssets). The
    //       increment-2 materialize reads this from cart to re-create far entities. Cart
    //       home 0x226C8000 is VERIFIED-FREE (past s_p6_shadow_inrange 0x226C0000+1216 B,
    //       before GFS 0x22700000); cache-through (cart-4mb rule), read raw at materialize.
#if !defined(P6_FRONTEND_TITLE) || defined(P6_FRONTEND_CHAIN)
    // FRONT-END LOAD CUT (this session, MEASURED #228 + load): the GHZ1 DORMANT
    // placement store is GHZ-gameplay-only -- the TITLE scene never materializes far
    // GHZ entities. Skip its GFS file-open + 42,084 B read on the TITLE flavor (one of
    // the 13 GHZ-gameplay chain loads the title never uses). GHZ flavor BYTE-IDENTICAL.
    //
    // #327 (signpost campaign r2, 2026-07-10, MEASURED live): the CHAIN flavor
    // stacks P6_FRONTEND_TITLE (build_shipping.sh:138 exports it from
    // P6_FRONTEND_CHAIN) so this cut ALSO removed the DORM load from the
    // boot->GHZ chain -- p6_w_dorm_bytes=-1/dorm_magic=0 live; every Pass-B
    // p6_ovl_materialize early-returned at the 'P6DM' magic check (cart
    // 0x226C8000 reads zero; p6_w_mat_slot stayed -1 across stream_mat=1106).
    // NO dormant scene entity ever rematerialized in chain-GHZ: bridge-1
    // (1184,904) + every far badnik/ring/spring existed only if compact-time
    // resident -> the deterministic x~1270 fall-through death loop (gapmiss==
    // inspan==339, 3 lives). Chain re-enables the load: 42,084 B at boot
    // (~0.4 s, same pre-pack-mount GFS slot), cart window 0x226C8000 is
    // chain-SAFE (measured full-arc resident high-water 0x2255B44C, 1.49 MB
    // below). TITLE/MENU-only flavors keep the cut; plain GHZ byte-identical.
    {
        unsigned char *w = (unsigned char *)0x226C8000u; // cache-through cart, verified-free
        int n = rsdk_storage_load_to_lwram("GHZ1DORM.BIN", (void *)w, 0x10000);
        p6_w_dorm_bytes = n;
        if (n >= 12) {
            p6_w_dorm_magic = (int32)(*(volatile uint32 *)w);       // 'P6DM' 0x4D443650 (big-endian)
            p6_w_dorm_slots = (int32)(*(volatile uint16 *)(w + 6)); // slot_count (header offset 6)
        }
    }
#endif

    // 1.5c) P6.8 I3b increment 2b: the MATERIALIZE WRITE-side proof (p6_materialize_one) is NOT
    //       called here -- the cart DORM store is loaded above, but stageObjectIDs + sceneInfo.
    //       classCount are populated LATER by the GHZ LoadScene/InitObjects chain. Calling it now
    //       resolves classID against an EMPTY class table (MEASURED: classid=0, classcount=0).
    //       The call lives at the END of p6_scene_load_and_arm (after InitObjects), the same
    //       "Saturn step AFTER the loader finalizes" discipline as the resident pre-inflate.

    // 1.6) P6.7 W11a: chain-load the GHZ1 layout BAND STORE (built by
    //      tools/build_layout_bands.py; zlib-banded layouts -- W11 design of
    //      record in SaturnMemoryMap.h). Same GFS-ordering rule as the
    //      overlay above: the open/close must precede the pack mount. It
    //      lands in the WRAM-L window the dead raw collisionMasks vacated
    //      (the pointer keeps the base as its address sink; ZERO remaining
    //      Saturn references read or write through it -- the macro seam +
    //      P6_CM arm cover every site, measured).
#if !defined(P6_FRONTEND_TITLE)
    // FRONT-END LOAD CUT (this session): the GHZ1 layout BAND STORE + its Mount are
    // GHZ-gameplay-only (the FG collision/tilemap windows). The TITLE scene has NO
    // gameplay FG layer (its island/clouds are the VDP2 backdrop), and p6_scene_load_
    // and_arm's GHZ-tilemap arm steps are already #if'd off for the Title folder. Skip
    // the 51,094 B GHZ1LAYT.BIN read + Mount on the TITLE flavor. GHZ BYTE-IDENTICAL.
    {
        p6_w_load_step = 3; // #251 layout band store + mount
        int n = rsdk_storage_load_to_lwram("GHZ1LAYT.BIN",
                                           (void *)P6_LW_LAYOUTBANDS, 0xC800);
        p6_w_lay_bytes = n;
        if (n > 0) {
            const unsigned char *w = (const unsigned char *)P6_LW_LAYOUTBANDS;
            uint32 h = 5381u;
            for (int32 i = 0; i < n; ++i)
                h = ((h << 5) + h) ^ (uint32)w[i];
            p6_w_lay_hash = (int32)h;
        }
        // W11b: mount NOW (the GHZ pass at 3a-ghz needs bound windows long
        // before step 10's probe replay) and arm the PERSISTENT inflate
        // scratch at its FIXED window (map v7.1 above -- NOT a TMP tenant;
        // the W11a stack local died after the probes, and a TMP allocation
        // MEASURED an 80K-pool overcommit against the GIF decoder +
        // tempEntityList load-phase peaks).
        if (n > 0) {
            extern int32 SaturnLayout_Mount(const void *blob);
            extern void SaturnLayout_SetScratch(void **bufp, uint32 cap);
            static void *p6_layScratch = (void *)P6_LW_LAYSCRATCH;
            // #249: SetScratch BEFORE Mount so Mount's resident pre-inflate (the
            // band-crossing fix) HAS the inflate scratch; otherwise every layer
            // stays band-inflate (s_layer_res=0) and the per-crossing stall persists.
            SaturnLayout_SetScratch(&p6_layScratch, 0x8000);
            SaturnLayout_Mount((const void *)P6_LW_LAYOUTBANDS);
        }
    }
#endif // !P6_FRONTEND_TITLE (GHZ1LAYT band store)

    // 1.6b) P6.7 W13 (Task #227): chain-load the offline ANIM PACK
    //      (cd/GHZANIM.PAK, build_anim_pack.py -- pre-parsed SH-2-layout
    //      SpriteFrame/SpriteAnimationEntry arrays for the GHZ Player set)
    //      into the fixed WRAM-H window the DEBUG_HITBOX_COUNT retarget
    //      freed. Same pre-pack-mount GFS slot rule as the band store.
    //      LoadSpriteAnimation's Saturn arm resolves pack members by path
    //      hash with zero DATASET_STG cost (Animation.cpp).
#if !defined(P6_FRONTEND_TITLE)
    // FRONT-END LOAD CUT + #228 BOOT-TRAP FIX (this session, MEASURED): GHZANIM.PAK is
    // the GHZ PLAYER anim pack and it loads at the FIXED WRAM-H window P6_HW_ANIMPAK
    // (0x060B6C00). The TITLE flavor's _end sits ABOVE 0x060B6C00 (the front-end
    // objects/witnesses/uncommitted island code grew WRAM-H), so loading GHZANIM.PAK
    // here CLOBBERS live .bss above _end -> the GFS callback pointer is corrupted ->
    // master SH-2 traps at 0x06000956 -> BLUE SCREEN FOREVER, cont_frames=0 (#228,
    // memory/wram-h-animpak-ceiling-boot-trap). MEASURED on the pre-cut TITLE build:
    // SH2-M PC=0x06000956, _end=0x060B6D50 (0x150 over the ceiling), every load witness
    // = its init value. The TITLE never draws GHZ Player sprites, so this pack is pure
    // waste here -- SKIPPING it both (a) removes the .bss clobber = fixes the boot trap
    // and (b) cuts the 68,800 B read + GFS file-open. GHZ flavor BYTE-IDENTICAL (it
    // still loads + uses the pack; its _end is below the ceiling).
    {
        p6_w_load_step = 4; // #251 anim pack
        int n = rsdk_storage_load_to_lwram("GHZANIM.PAK",
                                           (void *)P6_HW_ANIMPAK, P6_HW_ANIMPAK_CAP);
        p6_w_apk_bytes = n;
        if (n > 0) {
            const unsigned char *w = (const unsigned char *)P6_HW_ANIMPAK;
            uint32 h = 5381u;
            for (int32 i = 0; i < n; ++i)
                h = ((h << 5) + h) ^ (uint32)w[i];
            p6_w_apk_hash = (int32)h;
        }
    }

    // 1.6c) #254 residency lever (user-approved 2026-06-17): chain-load the COLD
    //       GHZ OBJECT anim pack into the CART (P6_HW_OBJANIMPAK 0x22760000). The
    //       Animation.cpp fast path checks this pack after the WRAM-H Player pack,
    //       so Ring/Spring/Bridge/SpikeLog (and the rest of the sweep) resolve with
    //       ZERO DATASET_STG cost -- retiring the STG overflow that failed SpikeLog
    //       (qa_p6_ghz_regression R10-R13 + p6_saturn_anim_allocfail). Same loader
    //       + pre-pack-mount GFS slot rule as GHZANIM.PAK.
    // FRONT-END LOAD CUT: GHZ-object anims (Ring/Spring/Bridge/...) are never drawn on
    // the TITLE scene -> skip the 14,176 B read on the TITLE flavor.
    {
        int n = rsdk_storage_load_to_lwram("GHZOBJ.PAK",
                                           (void *)P6_HW_OBJANIMPAK, P6_HW_OBJANIMPAK_CAP);
        p6_w_objapk_bytes = n;
    }
#endif // !P6_FRONTEND_TITLE (GHZANIM.PAK + GHZOBJ.PAK)

    // 1.7) P6.7 W12 (Task #227, qa_p6_sheet.py): stage the Player sheet
    //      band stores into VDP2 VRAM B0-tail/B1 (placement rationale in
    //      SaturnSheet.cpp -- the 155,339 B fit NEITHER work-RAM bank next
    //      to the closure code). GFS loads ride the SAME pre-pack-mount
    //      slot as the overlay/band loads (the P6.5b2 single-handle rule);
    //      staging buffer = the entityList window, free until the GHZ pass
    //      (the W11b transient-alias precedent). Probe rects replay through
    //      the REAL SaturnSheet_FetchRect immediately -- byte-exact vs the
    //      offline model or the gate names the first bad rect.
    {
        extern int32 SaturnSheet_Stage(const void *blob, uint32 bytes);
        extern void SaturnSheet_SetScratch(void **bufp, uint32 cap);
        extern int32 SaturnSheet_FetchRect(int32 slot, int32 sx, int32 sy,
                                           int32 w, int32 h, uint8 *dst);
        static void *p6_shtScratch = (void *)P6_LW_LAYSCRATCH; // shared, synchronous
        SaturnSheet_SetScratch(&p6_shtScratch, 0x8000);

        extern void SaturnSheet_SetHash(int32 slot, const uint32 *hash);
        extern int32 SaturnSheet_MakeResident(int32 slot); /* Task #243 render perf */
        // W12b root-cause fix: hand the PACK-side FetchRect to the jo-side
        // VDP1 cache as a runtime pointer -- a static jo->pack reference
        // re-shapes the mixed LTO/non-LTO link and crashes the GFS pack
        // open (bisect A/A1/A2, task #227).
        extern void p6_vdp1_set_fetch(int32 (*fn)(int32, int32, int32, int32,
                                                  int32, uint8 *));
        p6_vdp1_set_fetch(SaturnSheet_FetchRect);
#if defined(P6_FRAMEDIR)
        // Stage-1 FRD hook (checklist sec 7): hand the jo-side VDP1 cache the
        // frame-directory lookup as a runtime pointer -- the same W12b LTO
        // contract as set_fetch just above. Once per boot; the per-sheet
        // attachments arrive via p6_vdp1_sheet_set_frd at the bind loops.
        p6_vdp1_set_frd(SaturnFrameDir_Lookup);
#endif
        // Task #227 STG sizing: ITEMS.SHT joins the staged set -- banding
        // Items.gif drops its 32,768 B resident decode from DATASET_STG so
        // the GHZ anim working set fits the 80 KB pool (Storage.cpp).
        // W19 (Task #227): DISPLAY.SHT (HUD digits, 960 drops/run) +
        // SHIELDS.SHT (59 drops/run) join the staged set. MEASURED band-store
        // budget (build_sheet_bands.py): SONIC1 60,099 + SONIC2 61,549 +
        // SONIC3 33,691 + ITEMS 7,252 + DISPLAY 11,416 + SHIELDS 32,215 =
        // 206,222 B, inside the 245,760 B VDP2 window (0x25E44000..0x25E80000)
        // with 39,538 B margin. Tails1.gif (58,643 B) is the DECLARED GAP --
        // adding it overflows by 19,105 B (no-shrink guardrail).
        // Task #241 main: the band store is now in the 4MB cart (384 KB region,
        // SaturnSheet.cpp @0x227A0000), so TAILS1 stages ALONGSIDE the full
        // 6-sheet set -- SHIELDS is back (no trade). 7 sheets = 264,865 B inside
        // the 384 KB cart store. SATURNSHEET_SLOTS=8 holds 7.
        // #247: GLOBJ.SHT (Global/Objects.gif) is the 8th sheet -- fills the last
        // free SATURNSHEET_SLOTS slot (8). It is the shared sheet for the GHZ
        // content GLOBAL objects (Spikes 2 frames, Spring, ...). Banded 12,605 B;
        // total banded store now 277,470 B inside the 384 KB cart (fits).
        // #181/#247: GHZOBJ.SHT (GHZ/Objects.gif, 27,665 B banded) is the 9th sheet
        // -- the shared GHZ content-objects sheet the Bridge planks (and the rest of
        // the GHZ object sweep) index. Total banded store 305,135 B inside the 384 KB
        // cart. SATURNSHEET_SLOTS bumped 8->9 to hold it.
        // BADNIK-VIS (2026-06-18): the staged set stays at 9 (EXPLODE/ANIMALS are NOT
        // staged -- growing SATURNSHEET_SLOTS tripped the #228 orphan-.bss overlap).
        // The badnik fix is solely P6_VDP1_NSHEETS 9->12 so GHZ/Objects.gif (surf 16)
        // gets a bind slot. Explosions/Animals render via their stock resident-pixel
        // path (LoadSpriteSheet decode into DATASET_STG, Sprite.cpp:994).
        // R3.1 (#305) WRAM-H reclaim: these 9-sheet arrays + their 18 path strings are
        // GHZ-ONLY (the front-end SKIPS the load loop below) -- guard them out of the
        // front-end build so the bytes don't push _end past the front-end heap limit
        // (#228 boot trap, MEASURED). GHZ flavor BYTE-IDENTICAL (the guard is true there).
#if !defined(P6_FRONTEND_TITLE)
        static const char *shtFiles[9] = { "SONIC1.SHT", "SONIC2.SHT", "SONIC3.SHT",
                                           "ITEMS.SHT", "DISPLAY.SHT", "SHIELDS.SHT",
                                           "TAILS1.SHT", "GLOBJ.SHT", "GHZOBJ.SHT" };
        // Engine PATH hashes (W12b): LoadSpriteSheet hashes the .bin-relative
        // sprite path -- these are what Player_StageLoad will resolve.
        static const char *shtPaths[9] = { "Players/Sonic1.gif",
                                           "Players/Sonic2.gif",
                                           "Players/Sonic3.gif",
                                           "Global/Items.gif",
                                           "Global/Display.gif",
                                           "Global/Shields.gif",
                                           "Players/Tails1.gif",
                                           "Global/Objects.gif",
                                           "GHZ/Objects.gif" };
#endif // !P6_FRONTEND_TITLE (shtFiles/shtPaths GHZ-only -- R3.1 WRAM-H reclaim)
#if defined(P6_FRONTEND_TITLE)
        // FRONT-END LOAD CUT (this session, MEASURED): the TITLE scene draws
        // TitleLogo/TitleSonic/TitleBG/Title3DSprite and NONE of these 9 GHZ gameplay
        // sheets (SONIC1/2/3, ITEMS, DISPLAY, SHIELDS, TAILS1, GLOBJ, GHZOBJ). The old
        // code STAGED them banded (load + SaturnSheet_Stage) "so their hashes resolve"
        // -- but no Title object resolves them, so even the STAGE is dead weight. SKIP
        // the whole 9-sheet load loop on TITLE: removes 9 GFS file-opens + ~231 KB read
        // (the dominant chunk of the masked-core chain-load cost). The title's OWN
        // sheets (LOGOS/TLOGO/TSONIC/TBG) load in their own blocks below, unaffected.
        // GHZ flavor BYTE-IDENTICAL (this #if is compiled out -> the loop runs verbatim).
        // R3.1: the shtFiles/shtPaths arrays are now GHZ-only (guarded above) -> no void
        // casts here (they don't exist in this build).
#else
        for (int32 i = 0; i < 9; ++i) {
            p6_w_load_step = 10 + i; // #251 per-sheet (10..17 = SONIC1/2/3,ITEMS,DISPLAY,SHIELDS,TAILS1,GLOBJ)
            int sn = rsdk_storage_load_to_lwram(shtFiles[i],
                                                (void *)P6_LW_ENTITYLIST, 0x10000);
            if (sn > 0) {
                int32 slot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)sn);
                if (slot >= 0) {
                    RETRO_HASH_MD5(ph);
                    GEN_HASH_MD5(shtPaths[i], ph);
                    SaturnSheet_SetHash(slot, (const uint32 *)ph);
                    /* Task #243 Lever 1: decompress the sheet ONCE into the
                     * resident cart region so SaturnSheet_FetchRect serves rects
                     * with no per-frame miniz inflate (the render cost). The
                     * P6_SHT_NO_RESIDENT toggle builds the A/B RED variant (the
                     * old per-frame inflate path) for measured before/after. */
#ifndef P6_SHT_NO_RESIDENT
#if defined(P6_FRONTEND_TITLE)
                    /* TWIRL-RESIDENT FIX (this session, MEASURED): the FRONT-END
                     * TITLE scene draws TitleLogo/TitleSonic/TitleBG/Title3DSprite
                     * -- it NEVER draws any of these 9 GHZ gameplay sheets
                     * (SONIC1/2/3, ITEMS, DISPLAY, SHIELDS, TAILS1, GLOBJ, GHZOBJ).
                     * They are staged BANDED above only so their hashes resolve
                     * (harmless), but making them RESIDENT here would consume
                     * 1,373,696 B of the 3 MB cart RES store (0x22400000..0x22700000)
                     * on content the title never fetches -- leaving < 947 KB free,
                     * too little for TSONIC's 1,048,576 B (1024x1024) decoded sheet.
                     * That overflow is exactly why the CP5b.2 note had to leave
                     * TSONIC banded (the head's play-once twirl then re-inflated the
                     * body sheet ~3.4x/frame = the user's "slide show", MEASURED via
                     * qa_title_twirl_resident.py sht_fetches delta 231->272 over 12
                     * frames). SKIP the GHZ-sheet resident inflate on the TITLE
                     * flavor so the RES store has room for the title's OWN big
                     * play-once sheet (TSONIC, made resident below). The GHZ flavor
                     * (no P6_FRONTEND_TITLE) is BYTE-IDENTICAL -- it still makes all
                     * 9 resident (essential there: 3.07x in-motion fps,
                     * ghz-resident-sheets-render-perf.md). The skipped sheets stay
                     * banded on the title (never fetched -> zero cost). */
                    (void)slot;
#else
                    SaturnSheet_MakeResident(slot);
#endif
#endif
                }
            }
        }
#if defined(P6_FRAMEDIR)
        // Stage-1 FRD (checklist sec 7, plain-GHZ boot): stage the 9 GHZ
        // sheets' pre-cut frame directories alongside the banded+resident
        // .SHT set. MakeResident above is KEPT (fallback for non-anim
        // rects): the plain-GHZ store is 3,801,088 B -- 1,605,632 resident
        // + ~551 KB layout + 1,418,316 FRD = 3.57 MB fits (measured,
        // checklist sec 7 table). Attach happens in the arm-env bind loop.
        {
            static const char *frdFiles[9] = { "SONIC1.FRD", "SONIC2.FRD",
                                               "SONIC3.FRD", "ITEMS.FRD",
                                               "DISPLAY.FRD", "SHIELDS.FRD",
                                               "TAILS1.FRD", "GLOBJ.FRD",
                                               "GHZOBJ.FRD" };
            for (int32 i = 0; i < 9; ++i)
                (void)p6_frd_stage_file(frdFiles[i], shtPaths[i]);
        }
#endif // P6_FRAMEDIR (plain-GHZ boot FRD staging)
#endif // P6_FRONTEND_TITLE (skip 9 GHZ sheet loads) / else (GHZ verbatim loop)
        P6_LT_MARK(2); // Task #271 S2: chain loads (OVLRING/DORM/LAYT/ANIMPACK/GHZ Player sheets)
#if defined(P6_FRONTEND_TITLE) && defined(P6_FRONTEND_MENU)
        // RING-SONIC FIX (task #326, MEASURED 2026-07-13): in the CHAIN the SaturnSheet
        // resident store is ~1.56 MB full (LIVE p6_w_sht_resfill=1,563,468 at title
        // load) from PRIOR scenes' sheets, so SaturnSheet_MakeResident(TSONIC, 1 MB)
        // overflows -> TSONIC stays BANDED -> the head's banded FetchRect (frame48 rect
        // sx=496,sy=636,110x120) fails at the settled title -> p6_title_pool_for drops
        // the head blit -> the ring interior renders the emblem's opaque black disc
        // (the user-reported "SONIC STILL MISSING FROM RING"). Reclaim the store here,
        // AFTER the scratch is wired (SetScratch @~:4564) and BEFORE the FOUR title
        // sheets (LOGOS/TLOGO/TSONIC/TBG) stage+MakeResident, so TSONIC's 1 MB fits.
        // The stale residents are from scenes the title never draws (safe to reclaim;
        // they fall back to banded). Companion: the cutscene-promote block (~:5058)
        // is gated to NOT run on the front-end so it can't re-clobber this. Title+menu
        // (chain) only -> plain GHZ + direct-boot title byte-behaviour identical.
        SaturnSheet_ResReset();
#endif
#if defined(P6_FRONTEND_LOGOS)
        // CP4 (Task #266): stage LOGOS.SHT (Logos/Logos.gif, the 4-logo splash sheet,
        // 512x256 banded ~6.4 KB) into the 10th SaturnSheet slot so UIPicture's
        // DrawSprite resolves a banded slot + the p6_ghz_arm_env bind loop binds it
        // to VDP1 -> the logos actually blit. Path hash = "Logos/Logos.gif" (what the
        // engine LoadSpriteSheet computes for UIPicture's sheet). Front-end only; the
        // GHZ flavor never reaches this. (Built offline by tools/build_sheet_bands.py
        // build_one("Logos/Logos.gif","LOGOS.SHT").)
        p6_stage_sheet_hash("LOGOS.SHT", "Logos/Logos.gif");
#endif
#if defined(P6_FRONTEND_TITLE)
        // CP5b.1 (Task #268): stage TLOGO.SHT (Title/Logo.gif, the SONIC-MANIA logo
        // sheet, 512x512 banded 19,502 B) into the 11th SaturnSheet slot (slot 10;
        // slot 9 = LOGOS.SHT, staged just above because TITLE implies LOGOS) so
        // TitleLogo's DrawSprite resolves a banded slot + the p6_ghz_arm_env bind loop
        // (p6_io_main.cpp:2182-2207, run unconditionally from p6_scene_load_and_arm)
        // binds Title/Logo.gif's gfxSurface to a VDP1 handle -> the TitleLogo emblem/
        // ribbon/gametitle pieces actually blit. Path hash = "Title/Logo.gif" (what the
        // engine LoadSpriteAnimation("Title/Logo.bin") computes for TitleLogo's sheet).
        // MEASURED RED ROOT CAUSE (CP5a deep capture, frame 90): the Title scene ran
        // 2923 ticks (past FlashIn) with the logos visible=true, but Title/Logo.gif's
        // surface had saturnSheetSlot==-1 (no TLOGO.SHT staged) -> the bind loop skipped
        // it -> handle<0 -> every TitleLogo blit dropped -> uniform-blue title. MIRRORS
        // the CP4b LOGOS.SHT block above exactly. (Built offline by build_sheet_bands.py
        // build_one("Title/Logo.gif","TLOGO.SHT").)
        p6_stage_sheet_hash("TLOGO.SHT", "Title/Logo.gif");
        P6_LT_MARK(3); // Task #271 S3: 512x512 sheets (LOGOS.SHT + TLOGO.SHT load+stage+resident)
        // CP5b.2 (Task #269): stage TSONIC.SHT (Title/Sonic.gif, the ring-center head
        // sheet) into the 12th SaturnSheet slot (slot 11; slots 9/10 = LOGOS/TLOGO,
        // staged above because TITLE implies LOGOS) so TitleSonic's DrawSprite resolves
        // a banded slot + the p6_ghz_arm_env bind loop binds Title/Sonic.gif's
        // gfxSurface to a VDP1 handle -> Sonic's head + finger blit into the gold ring
        // center. Path hash = "Title/Sonic.gif" (what LoadSpriteAnimation("Title/
        // Sonic.bin") computes for TitleSonic's sheet[0]). MEASURED RED ROOT CAUSE
        // (CP5b.1): the logo rendered but Title/Sonic.gif's surface had
        // saturnSheetSlot==-1 (no TSONIC.SHT staged) -> the bind loop skipped it ->
        // handle<0 -> the head dropped -> black ring interior. MIRRORS the TLOGO.SHT
        // block above EXCEPT the load buffer: TSONIC.SHT is 121,090 B (1024x1024, 4x
        // the Logo's area), which EXCEEDS the 0x10000 (64 KB) buffer TLOGO uses -- load
        // with 0x20000 (128 KB) into P6_LW_ENTITYLIST (region 0x6CC00 = 445,440 B ->
        // 128 KB fits with huge margin). MakeResident inflates the 64 bands (raw band
        // 16,384 B = 0x4000, boundary-exact for the rsz>0x4000 reject + the 0x8000
        // LAYSCRATCH window) into 1 MB of the 3.64 MB cart RES store.
        {
            int sn = rsdk_storage_load_to_lwram("TSONIC.SHT",
                                                (void *)P6_LW_ENTITYLIST, 0x20000);
            // title-Sonic overflow diag: band-store fill BEFORE the TSONIC stage +
            // the store capacity (measured, so the fix is sized not guessed).
            p6_w_tsonic_bandpre = (int32)(SaturnSheet_BandCursor() - SaturnSheet_BandBase());
            p6_w_tsonic_bandcap = (int32)(SaturnSheet_BandEnd() - SaturnSheet_BandBase());
            if (sn > 0) {
                int32 slot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)sn);
                p6_w_tsonic_stageret = slot; // >=0 slot, -1 overflow
                if (slot >= 0) {
                    RETRO_HASH_MD5(ph);
                    GEN_HASH_MD5("Title/Sonic.gif", ph);
                    SaturnSheet_SetHash(slot, (const uint32 *)ph);
                    // TWIRL-RESIDENT FIX (this session): MakeResident TSONIC so the
                    // TitleSonic BODY twirl (anim 0, Title/Sonic.bin, 49 frames,
                    // PLAY-ONCE) is served from the pre-inflated cart with ZERO
                    // per-frame miniz inflate -- the user's "slide show" (the play-
                    // once frames each MISS the VDP1 rect cache and re-inflated the
                    // banded body sheet). MEASURED on the banded build during the
                    // ACTUAL twirl (savestate frame 26->31): p6_w_sht_fetches climbed
                    // 231->272 over 12 title frames = ~3.42 inflate/frame on the
                    // wait-stated A-Bus cart (qa_title_twirl_resident.py RED). The GHZ
                    // resident-sheet fix (ghz-resident-sheets-render-perf.md) is the
                    // proven pattern: FetchRect then serves each body rect by a direct
                    // resident memcpy (S->resident != 0 fast path, SaturnSheet.cpp:305).
                    //
                    // WHY THIS NO LONGER HANGS (the CP5b.2 blocker, MEASURED + resolved
                    // above): the original hang was a cart RES-store OVERFLOW, not the
                    // 1024-wide inflate. With the 9 GHZ gameplay sheets already resident
                    // (1,373,696 B) + LOGOS+TLOGO+TBG, the resident bump cursor reached
                    // ~0x225E8000 and TSONIC's 1,048,576 B pushed it to ~0x226E8000 --
                    // colliding with the VDP1 staging buffers at 0x226E0000 (which sit
                    // INSIDE the RES store range 0x22400000..0x22700000) and writing
                    // bands past RES_END into live load memory = the corruption-hang.
                    // The TITLE flavor now SKIPS the 9 GHZ-sheet resident inflate (just
                    // above, #if P6_FRONTEND_TITLE) -- the title never draws them -- so
                    // the cursor before TSONIC is only LOGOS+TLOGO = ~0x22470000 and
                    // TSONIC ends at ~0x22570000, ~1.5 MB clear of the 0x226E0000
                    // staging buffers and far below RES_END 0x22700000. No overflow ->
                    // no corruption -> no hang. The inflate scratch is fine: the 64
                    // bands are rsz=16*1024=0x4000 each, copied to scratch[0x4000..
                    // 0x8000) = exactly the 0x8000 LAYSCRATCH window (not OVER it; the
                    // reject is rsz>0x4000, and 0x4000 is not >). STEP-2 verifies _end
                    // stays under the #228 ANIMPAK ceiling + cont_frames>0 (no hang) +
                    // qa_title_twirl_resident.py RED->GREEN. GHZ flavor byte-identical
                    // (no P6_FRONTEND_TITLE -> this whole TSONIC block is #if'd out).
#ifndef P6_SHT_NO_RESIDENT
                    /* task #326: capture the TSONIC resident result (>=0 resident,
                     * -1 = overflow/band-reject -> the head stays banded). */
                    p6_w_tsonic_makeres = SaturnSheet_MakeResident(slot);
#endif
                }
            }
        }
        P6_LT_MARK(4); // Task #271 S4: TSONIC.SHT (1024x1024, 121,090 B) load+stage -- the CP5b.2 suspect
        // CP5b.3 (Task #272): stage TBG.SHT (Title/BG.gif, the TitleBG +
        // Title3DSprite mountains/water/billboard sheet) into the 13th SaturnSheet
        // slot (slot 12; slots 9/10/11 = LOGOS/TLOGO/TSONIC) so TitleBG's +
        // Title3DSprite's DrawSprite resolve a banded slot + the p6_ghz_arm_env bind
        // loop binds Title/BG.gif's gfxSurface to a VDP1 handle -> the mountains/
        // water/wing-shine + billboard sprites blit. Path hash = "Title/BG.gif"
        // (what LoadSpriteAnimation("Title/Background.bin") computes for the sheet[0]).
        // 256x256 = 65,536 B decoded; the banded .SHT fits the 0x10000 (64 KB) load
        // buffer with margin. MakeResident is SAFE here (256-wide -> raw band 16*256
        // = 0x1000, well under the 0x4000 scratch-split that breaks the 1024-wide
        // TSONIC) so the sprites serve from the resident cart store (no per-frame
        // band inflate). MIRRORS the TLOGO.SHT block.
        {
            int sn = rsdk_storage_load_to_lwram("TBG.SHT",
                                                (void *)P6_LW_ENTITYLIST, 0x10000);
            if (sn > 0) {
                int32 slot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)sn);
                if (slot >= 0) {
                    RETRO_HASH_MD5(ph);
                    GEN_HASH_MD5("Title/BG.gif", ph);
                    SaturnSheet_SetHash(slot, (const uint32 *)ph);
#ifndef P6_SHT_NO_RESIDENT
                    SaturnSheet_MakeResident(slot);
#endif
                }
            }
        }
        // ELECTRICITY-RING FIX (this session, MEASURED -- user-reported "the intro
        // shows random sprite-sheet fragments, not the electric animation"): the decomp
        // TitleSetup_StageLoad does LoadSpriteAnimation("Title/Electricity.bin"), whose
        // anim 0 ("Electricity", 40 frames, PLAY-ONCE) draws frames from BOTH
        // Title/Electricity1.gif (sheet 0, the small->large ring build) AND
        // Title/Electricity2.gif (sheet 1, the final large frames). TitleSetup_Draw_
        // DrawRing blits this animator twice (FLIP_NONE + FLIP_X). NEITHER sheet was
        // staged on Saturn -> Electricity1/2.gif's gfxSurface had saturnSheetSlot==-1 ->
        // the bind loop skipped them -> the per-frame DrawSprite fetch returned STALE
        // VDP1 slot content -> the user saw "random sprite sheets" cycling during the
        // ring-build (MEASURED: parse of Electricity.bin = 40 play-once frames, sheets
        // ['Title/Electricity1.gif','Title/Electricity2.gif']; the staged set was only
        // LOGOS/TLOGO/TSONIC/TBG -- electricity absent). FIX: stage + MakeResident BOTH
        // sheets (the proven TLOGO/TSONIC/TBG pattern) so Draw_DrawRing fetches the real
        // electricity frames by resident memcpy (no per-frame inflate, no slot thrash) --
        // exactly how the decomp/hardware renders the ring. Path hashes are what
        // LoadSpriteAnimation("Title/Electricity.bin") computes for sheets[0]/[1].
        // ELECTR1.SHT 1024x512 banded 46,833 B (fits the 0x10000 load buffer);
        // ELECTR2.SHT 512x512 banded 5,912 B. RES-store budget OK: the TITLE flavor
        // skips the 9 GHZ-sheet resident inflates, so LOGOS+TLOGO+TSONIC(1MB)+TBG +
        // ELECTR1(512KB)+ELECTR2(256KB) ~= 2.1 MB < the 3 MB store (0x22400000.).
        {
            int sn = rsdk_storage_load_to_lwram("ELECTR1.SHT",
                                                (void *)P6_LW_ENTITYLIST, 0x10000);
            if (sn > 0) {
                int32 slot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)sn);
                if (slot >= 0) {
                    RETRO_HASH_MD5(ph);
                    GEN_HASH_MD5("Title/Electricity1.gif", ph);
                    SaturnSheet_SetHash(slot, (const uint32 *)ph);
#ifndef P6_SHT_NO_RESIDENT
                    SaturnSheet_MakeResident(slot);
#endif
                }
            }
        }
        {
            int sn = rsdk_storage_load_to_lwram("ELECTR2.SHT",
                                                (void *)P6_LW_ENTITYLIST, 0x10000);
            if (sn > 0) {
                int32 slot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)sn);
                if (slot >= 0) {
                    RETRO_HASH_MD5(ph);
                    GEN_HASH_MD5("Title/Electricity2.gif", ph);
                    SaturnSheet_SetHash(slot, (const uint32 *)ph);
#ifndef P6_SHT_NO_RESIDENT
                    SaturnSheet_MakeResident(slot);
#endif
                }
            }
        }
        P6_LT_MARK(5); // Task #272 S5: TBG.SHT + ELECTR1/ELECTR2.SHT load+stage
#endif
#if defined(P6_FRONTEND_MENU)
        // M1b: stage the MAIN-MENU sprite sheets so the menu rows' icons + text blit.
        // MENU implies TITLE -> LOGOS, so the Title/Logo sheets above are already staged
        // (slots 0..12, inert on the Menu scene). These add slots 13 (MAINICON) + 14
        // (TEXTEN). MIRRORS the TBG.SHT block exactly (256-class banded, MakeResident
        // SAFE -- both are <=512 wide so raw band <=0x2000 << the 0x4000 scratch-split).
        //   MAINICON.SHT: UI/MainIcons.gif, the UIModeButton mode icons (LoadSprite-
        //     Animation("UI/MainIcons.bin") -> sheet[0]). 6,567 B banded.
        //   TEXTEN.SHT: UI/TextEN.gif, the mode TEXT labels (UIWidgets_ApplyLanguage ->
        //     UIWidgets->textFrames -> LoadSpriteAnimation("UI/TextEN.bin") -> sheet[0]).
        //     24,097 B banded. Path hashes are what those LoadSpriteAnimation calls compute.
        p6_stage_sheet_hash("MAINICON.SHT", "UI/MainIcons.gif");
        p6_stage_sheet_hash("TEXTEN.SHT", "UI/TextEN.gif");
#endif // P6_FRONTEND_MENU
#if defined(P6_AIZ_TEST)
        // R3.1 (Task #305, qa_p6_aiz_tornado.py): stage AIZOBJ.SHT (AIZ/Objects.gif --
        // the AIZ intro Tornado biplane sheet: body+propeller+flame+pilot) into the 17th
        // SaturnSheet slot (slot 16; AIZ_TEST implies MENU so slots 0..15 are the
        // GHZ+Logos+Title+Menu sheets, inert on the AIZ scene). MIRRORS the TEXTEN.SHT
        // block exactly. Path hash = "AIZ/Objects.gif" -- what AIZTornado_StageLoad's
        // LoadSpriteAnimation("AIZ/AIZTornado.bin") computes for its sheet[0]. ROOT CAUSE
        // (same class as CP4b LOGOS / CP5b.1 TLOGO / CP5b.2 TSONIC): the AIZTornado entity
        // is registered + FLYING (M3.2: tornado x 60->14282) but AIZ/Objects.gif's surface
        // had saturnSheetSlot==-1 (no .SHT staged) -> no VDP1 handle -> every AIZTornado_
        // Draw DrawSprite (AIZTornado.c:29-42) dropped -> the biplane was invisible. The
        // bind loop (p6_ghz_arm_env, run unconditionally below) binds it to a VDP1 handle
        // (P6_VDP1_NSHEETS=12 has room -- the AIZ scene's sprite-bind demand is far under
        // GHZ's). 21,947 B banded fits the 0x10000 (64 KB) load buffer. MakeResident SAFE
        // (512-wide -> raw band <=0x4000, the scratch-split boundary).
        // R3.1 (#305): stage AIZOBJ.SHT BANDED -- NO MakeResident. The front-end cart RES
        // store (0x22400000.., ~3 MB) is already filled by the resident LOGOS/TLOGO/TSONIC
        // (1 MB!)/TBG/MAINICON/TEXTEN; making the AIZ Tornado sheet resident too OVERFLOWS
        // it -> the cart inflate clobbers a boot structure -> master traps 0x06000956 in
        // early boot (folder_tag=0, cont_frames=0; MEASURED blue-screen -- the CP5b.2
        // RES-store-overflow class, NOT a WRAM-H _end trap: _end 0x060B9C40 is 1.2 KB UNDER
        // the R2.5-safe line yet still trapped). The biplane is a ONE-TIME cutscene sprite
        // -- banded FetchRect (per-rect miniz inflate) is fine, no per-frame fast-path. So
        // this stages inline WITHOUT the helper's MakeResident.
        {
            int sn = rsdk_storage_load_to_lwram("AIZOBJ.SHT", (void *)P6_LW_ENTITYLIST, 0x10000);
            if (sn > 0) {
                int32 slot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)sn);
                if (slot >= 0) {
                    RETRO_HASH_MD5(ph);
                    GEN_HASH_MD5("AIZ/Objects.gif", ph);
                    SaturnSheet_SetHash(slot, (const uint32 *)ph);
                    p6_w_aizobj_slot = slot;
                }
            }
        }
#if defined(P6_GHZCUT_BOOT)
        // Task #309 Tier-B.2: stage the COMBINED Heavy atlas (cd/HBHOBJ.SHT,
        // built by tools/build_heavy_atlas.py = the 43 used frames of the 5
        // boss sheets packed into ONE 512x432 8bpp atlas). MIRRORS the AIZOBJ.SHT
        // block above exactly. Path hash = "Cutscene/HBH.gif" -- the single sheet
        // name the rewritten HBHOBJ.PAK .bin entries reference -> CutsceneHBH's
        // frames resolve to THIS sheet's gfxSurface. Staged into the NEXT free
        // SaturnSheet slot (slot 7; the front-end uses 0..6 = LOGOS/TLOGO/TSONIC/
        // TBG/MAINICON/TEXTEN/AIZOBJ, leaving 7..15 free in the 16-slot MENU table
        // -> NO growth to slot 16/17 = the #228 trap). Staged BANDED, NO
        // MakeResident -- the front-end cart RES store is already filled by the
        // resident title sheets (TSONIC 1 MB!); making the 221 KB Heavy atlas
        // resident too risks the AIZOBJ-class RES-store-overflow boot trap. The
        // Heavies are a held cutscene -> banded per-rect FetchRect renders fine.
        // The p6_ghz_arm_env bind loop (run unconditionally below) binds the
        // surface to a VDP1 handle (P6_VDP1_NSHEETS=12 has room). 30 KB banded
        // fits the 0x10000 (64 KB) load buffer.
        {
            int sn = rsdk_storage_load_to_lwram("HBHOBJ.SHT", (void *)P6_LW_ENTITYLIST, 0x10000);
            if (sn > 0) {
                int32 slot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)sn);
                if (slot >= 0) {
                    RETRO_HASH_MD5(ph);
                    GEN_HASH_MD5("Cutscene/HBH.gif", ph);
                    SaturnSheet_SetHash(slot, (const uint32 *)ph);
                    p6_w_hbh_slot = slot;
                }
            }
            // Upload the 5 Heavy palette blocks (cd/HBHPAL.BIN) to CRAM[512..1791]
            // (the no-AIZ-BG GHZCutscene flavor leaves CRAM[512+] free; bank0 FG
            // CRAM[0..255] + bank1 VDP1-sprite CRAM[256..511] UNTOUCHED -> R3.3-safe).
            // Each block = the jo colno the Heavy draw shim selects (block*256).
            // Task #311: blocks are now FULL 256 entries -- lower 128 = the Heavy
            // gif GCT, upper 128 = the decomp CutsceneHBH_SetupPalettes tempPal
            // (CutsceneHBH.c:84-163,195). MEASURED: 11.5-28.1% of every Heavy's
            // frame pixels index >=128; with only 128 loaded they read junk CRAM
            // = the garble strips (gate tools/qa_ghzcut_garble.py).
            {
                extern void p6_vdp2_hbh_pal_upload(const unsigned short *palData);
                int pn = rsdk_storage_load_to_lwram("HBHPAL.BIN", (void *)P6_LW_ENTITYLIST, 0xA00);
                if (pn >= 5 * 256 * 2)
                    p6_vdp2_hbh_pal_upload((const unsigned short *)P6_LW_ENTITYLIST);
            }
        }
        // Task #309 caveat #2a (cutscene PLAYERS render): stage the selective player
        // atlas (cd/PLROBJ.SHT, build_player_atlas.py = the Sonic+Tails FAN/Idle frames
        // packed into ONE 512x80 8bpp atlas) into the NEXT free SaturnSheet slot, and
        // upload the merged player palette (cd/PLRPAL.BIN) to CRAM block 7 (CRAM[1792]).
        // MIRRORS the HBHOBJ.SHT block above exactly. Path hash = "Cutscene/Players.gif"
        // -- the single sheet name the rewritten Sonic.bin/Tails.bin (folded into
        // HBHOBJ.PAK by build_player_atlas.py) reference -> Player_Draw's frames resolve
        // to THIS sheet's gfxSurface. Staged BANDED, NO MakeResident (RES-store-overflow-
        // safe, like AIZOBJ/HBH; 6,673 B fits the 0x10000 load buffer, raw band 0x2000).
        // The p6_ghz_arm_env bind loop (run unconditionally below) binds the surface to a
        // VDP1 handle (P6_VDP1_NSHEETS=12 has room). p6_plr_sheet_slot (p6_vdp1.c) is set
        // to the staged slot so the blit routes the player sheet's pixels to CRAM[1792]
        // (block 7) SURFACE-DRIVEN -- the corrected route that NEVER touches the shared
        // ProcessObjectDrawLists loop (the attempt-1 regression path). The player .bin
        // are already in HBHOBJ.PAK (loaded by p6_ghzcut_reload / the live seam), so NO
        // new PAK-load code is needed here -- only the sheet + palette.
        {
            int sn = rsdk_storage_load_to_lwram("PLROBJ.SHT", (void *)P6_LW_ENTITYLIST, 0x10000);
            if (sn > 0) {
                int32 slot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)sn);
                if (slot >= 0) {
                    RETRO_HASH_MD5(ph);
                    GEN_HASH_MD5("Cutscene/Players.gif", ph);
                    SaturnSheet_SetHash(slot, (const uint32 *)ph);
                    p6_w_plrsht_slot = slot;
                    p6_plr_sheet_slot = (int)slot; // p6_vdp1.c surface-route selector
                }
            }
            // Upload the merged 256-color player palette to CRAM block 7 = CRAM[1792].
            // Disjoint from FG bank0[0..255], sprite bank1[256..511], AND the 5 Heavy
            // blocks (CRAM[512..1663]) -> R3.3-collision-proof.
            {
                int pn = rsdk_storage_load_to_lwram("PLRPAL.BIN", (void *)P6_LW_ENTITYLIST, 0x800);
                if (pn >= 256 * 2)
                    p6_vdp2_player_pal_upload((const unsigned short *)P6_LW_ENTITYLIST);
            }
        }
#if defined(P6_FRONTEND_MENU)
        // #317 cutscene diag+fix: wire the inflate scratch (the plain-GHZ stage path at
        // ~:4077 is #if !FRONTEND_TITLE -> the chain skips it), count that this block
        // runs (p6_w_cut_calls), capture MakeResident(HBHOBJ)'s return, then promote the
        // Heavies + players + HUD. The store is empty (Edit A reclaimed pre-FG-mount) so
        // they should all fit. p6_w_mkres_reason (SaturnSheet) names any failure.
        {
            // #317 draw/inflate hog (GHZCutscene): promote the staged cutscene sheets to
            // resident (zero-inflate FetchRect). MEASURED cause of the prior misses
            // (mkres witness): this block runs while the RES store is STILL FULL of the
            // resident TITLE sheets (1576KB, TSONIC 1MB), so HBHOBJ (221KB) failed the
            // MakeResident bounds check. Reclaim HERE, right before the promotes -- this
            // block runs BEFORE the cutscene's p6_scene_load_and_arm FG mount, so the
            // reclaim is clobber-safe and the title sheets (never drawn in the cutscene)
            // free the store. Also wire the inflate scratch (the plain-GHZ path @~:4077
            // is #if !P6_FRONTEND_TITLE, which the chain skips). Result: 14.44 -> 0.00
            // inflations/frame at the GHZCutscene (qa_frontend_inflate_gate GREEN); the
            // GHZ landing (its own seam reclaim, 606fbfb) is unaffected. Front-end only
            // -> plain GHZ byte-identical (this whole seam is chain-gated).
            static void *p6_cutScr = (void *)P6_LW_LAYSCRATCH;
            SaturnSheet_SetScratch(&p6_cutScr, 0x8000);
            // RING-SONIC FIX (task #326, MEASURED root cause 2026-07-13): this
            // cutscene-sheet promote block is #if P6_FRONTEND_MENU (a COMPILE gate),
            // so in the chain it runs on EVERY scene load -- INCLUDING the Title. Its
            // SaturnSheet_ResReset() below WIPES the resident store; at the Title load
            // this clobbers TSONIC.SHT's residency (which the title-staging block just
            // set, MEASURED p6_w_tsonic_makeres=0=success), leaving TSONIC banded. The
            // banded SaturnSheet_FetchRect then FAILS for the head's high rows (frame48
            // rect sx=496,sy=636 -> ret=0, dropreason=4) -> p6_title_pool_for returns
            // -1 -> the TitleSonic head blit is DROPPED every frame -> the ring
            // interior shows the emblem's own opaque black disc (the user-reported
            // "SONIC STILL MISSING FROM RING"). LIVE-MEASURED at the settled title:
            // headslot_ret=-1, headfetch_resident=0, drawcalls=125 (Draw ran), emitseq
            // 2011>emblem 2008 (head ordered in FRONT), headx/y=(106,13) 110x120
            // (over the ring hole) -- every link GREEN except the reclaimed residency.
            // FIX: this reclaim+promote is only meaningful when loading a scene that
            // USES the cutscene sheets (GHZCutscene, and the GHZ handoff). GATE it to
            // skip the Title/Logos/Menu front-end scenes so the title's TSONIC (and
            // LOGOS/TLOGO/TBG) residency SURVIVES. The GHZCutscene load still runs it
            // (its own seam re-stages + promotes the cutscene sheets). Runtime folder
            // guard; plain GHZ (no P6_FRONTEND_MENU) unaffected.
            // Gate on the ACTUAL PRESENCE of cutscene sheets, NOT the folder string
            // (currentSceneFolder is unreliable mid-load: it holds the PREVIOUS folder
            // until LoadSceneFolder re-strcpy's it, and the chain advance made it read
            // "AIZ" at the settled title -- MEASURED). The HBHOBJ/GHCOBJ/PLROBJ slots are
            // staged ONLY on the GHZCutscene (and the GHZ handoff) load; at the Title/
            // Logos/Menu loads they are all -1, so this reclaim+promote (and its
            // TSONIC-clobbering ResReset) must NOT run there. Key off hbh_slot>=0 (the
            // Heavy atlas, staged only for the cutscene) -> the reset runs exactly when
            // the cutscene sheets exist to be promoted.
            // MEASURED (task #326, 2026-07-13): the presence gate (hbh_slot>=0) is
            // DEFEATED -- p6_w_hbh_slot latches 9 and PERSISTS across loads, so it read
            // >=0 at the Title (LIVE) and this ResReset still fired, re-clobbering the
            // title-load TSONIC residency. Gate POSITIVELY on the destination folder
            // being a cutscene/gameplay scene (the SAME currentSceneFolder discriminator
            // the GHZCutscene/GHZ seams at :7896/:8091 already trust for their own
            // ResResets). At the Title/Logos/Menu front-end loads the folder is NOT
            // GHZCutscene/GHZ, so this reclaim+promote (and its ResReset) is skipped and
            // the title's TSONIC residency SURVIVES; the GHZCutscene/GHZ loads still run
            // it. Runtime folder guard; plain GHZ (no P6_FRONTEND_MENU) unaffected.
            const char *p6_csf = currentSceneFolder ? currentSceneFolder : "";
            int p6_cut_promote = (!strcmp(p6_csf, "GHZCutscene") || !strcmp(p6_csf, "GHZ"))
                                 && ((p6_w_hbh_slot >= 0) || (p6_w_ghcobj_slot >= 0)
                                     || (p6_w_dispsht_slot >= 0));
            if (p6_cut_promote) {
            SaturnSheet_ResReset();
            if (p6_w_dispsht_slot >= 0) SaturnSheet_MakeResident(p6_w_dispsht_slot);
            if (p6_w_plrsht_slot  >= 0) SaturnSheet_MakeResident(p6_w_plrsht_slot);
            if (p6_w_hbh_slot     >= 0) SaturnSheet_MakeResident(p6_w_hbh_slot);
            // #324 (GHZCutscene DrawLists hog): the seam staged GHCOBJ (claw+crate)
            // + RUBYOBJ (PhantomRuby, 41-frame anim) + ITEMS but never PROMOTED
            // them -- MEASURED (live chain _drawprof.jsonl 2026-07-09): 23 banded
            // FetchRect inflates/frame at the cutscene (p6_w_sht_fetches), each a
            // 16-bit band copy + miniz inflate, driving the draw cb bracket to
            // 135k FRT ticks (161 ms) and 2.1 fps. Promote them with the same
            // MakeResident pattern (tiny sheets; the store was just reclaimed).
            if (p6_w_ghcobj_slot  >= 0) SaturnSheet_MakeResident(p6_w_ghcobj_slot);
            if (p6_w_rubyobj_slot >= 0) SaturnSheet_MakeResident(p6_w_rubyobj_slot);
            if (p6_w_itemsht_slot >= 0) SaturnSheet_MakeResident(p6_w_itemsht_slot);
            } /* p6_cut_promote (task #326: skip on Title/Logos/Menu) */
        }
#endif
        // Task #311 (residual garble): stage the 2 sheets the scene's OTHER drawing
        // entities reference -- both were UNSTAGED so their DrawSprite rects sampled
        // a WRONG surface (the #181 class: fragmentary sprite parts + magenta filler,
        // VIEWED _hbhpal_16.png). Their .bins load from DATA.RSDK via the normal
        // LoadSpriteAnimation path (NOT GHZCUTIL.BIN, which is the 256 KB pre-decoded
        // tileset) -- only the SURFACE lookups were unresolved. Both sheets draw at
        // the default colno 256 (bank1): GCTs MEASURED == the live stage sprite
        // palette at every used index (claw 30/33 exact, ruby 6/7; the misses read
        // opaque-black top-slots -- cosmetic). MIRRORS the HBHOBJ/PLROBJ blocks.
        //   GHCOBJ.SHT  (4,442 B)  = GHZCutscene/Objects.gif (AIZKingClaw dig-claw
        //                            via GHZCutscene/Claw.bin + the Platform crate).
        //   RUBYOBJ.SHT (2,829 B)  = Global/PhantomRuby.gif (PhantomRuby, 41 frames).
        // #311: GHCOBJ.SHT/RUBYOBJ.SHT are staged in p6_ghzcut_reload BEFORE
        // p6_scene_load_and_arm -- NOT here. MEASURED (builds 15-17): this site runs
        // AFTER the scene load; the claw/ruby/ring LoadSpriteSheet calls during
        // StageLoad FindSlot-MISSED and (Saturn fall-through, Sprite.cpp:983-992)
        // returned -1 WITHOUT creating a surface -> their anims carried sheetID -1 ->
        // the wrong-surface fragments. A post-load stage cannot be repaired by a
        // surface relink (there is no surface to relink).
#endif // P6_GHZCUT_BOOT
#endif // P6_AIZ_TEST

        // R3.1 (#305) WRAM-H reclaim: the sheet-probe table + djb2 loop is GHZ-ONLY. The
        // front-end SKIPS the 9-sheet GHZ load loop, so its runtime SaturnSheet slots
        // (LOGOS=0,TLOGO=1,... AIZOBJ=6) DON'T match the probe table's SHEETS-index slots
        // (LOGOS=9,...) -> every front-end FetchRect already MISMATCHED (dead diagnostic).
        // Guarding it out of the front-end drops ~1 KB of probe-table .rodata + the loop --
        // the reclaim that lets the front-end (AIZ) build fit under the heap limit (#228
        // boot trap, MEASURED blue-screen at _end 0x060BA140). GHZ flavor BYTE-IDENTICAL.
#if !defined(P6_FRONTEND_TITLE)
#include "p6_sheet_probes.inc"
        // W12b: fixed-window scratch (P6_HW_GROUPWIN tail), was 4 KB .bss
        uint8 *p6_shtRect = (uint8 *)P6_HW_GROUPWIN_SHTRECT; // largest probe rect 64x64
        p6_w_load_step = 18; // #251 sheet probes
        int32 good = 0;
        for (int32 i = 0; i < P6_SHEET_PROBE_COUNT; ++i) {
            uint32 hh = 5381u;
            if (SaturnSheet_FetchRect(p6SheetProbes[i].slot, p6SheetProbes[i].sx,
                                      p6SheetProbes[i].sy, p6SheetProbes[i].w,
                                      p6SheetProbes[i].h, p6_shtRect)) {
                uint32 nb = (uint32)(p6SheetProbes[i].w * p6SheetProbes[i].h);
                for (uint32 k = 0; k < nb; ++k)
                    hh = ((hh << 5) + hh) ^ (uint32)p6_shtRect[k];
                if (hh == p6SheetProbes[i].expect) {
                    ++good;
                    continue;
                }
            }
            if (p6_w_sht_firstbad < 0)
                p6_w_sht_firstbad = i;
        }
        p6_w_sht_probes = good;
#endif // !P6_FRONTEND_TITLE (sheet-probe table GHZ-only -- R3.1 WRAM-H reclaim)
    }


    // 2) P6.7c: the hand-built one-entry scene list is RETIRED -- the ENGINE
    //    builds the real 92-entry list from GameConfig.bin below. The render
    //    palette tables fill FIRST (the EXACT engine fill, Drawing.cpp:274-276)
    //    because LoadGameConfig's global-palette read (RetroEngine.cpp:1070)
    //    and every later merge route through them. currentSceneFolder/
    //    currentSceneID stay EMPTY: a non-empty pair flips LoadGameConfig's
    //    "_RSDK_SCENE" override branch (RetroEngine.cpp:1091-1112) and
    //    corrupts the list; emptiness also keeps LoadSceneFolder off its
    //    same-folder reload early-return (Scene.cpp:71).
    for (int32 c = 0; c < 0x100; ++c) {
        rgb32To16_R[c] = (c & 0xFFF8) << 8;
        rgb32To16_G[c] = (c & 0xFFFC) << 3;
        rgb32To16_B[c] = c >> 3;
    }

    // 2b) Registration preamble -- the InitGameLink mirror (RetroEngine.cpp
    //     :1216-1235, REV02 arm): the REAL engine DefaultObject @0 +
    //     DevOutput @1 (their TUs are pack members as of P6.7c), then the
    //     game objects -- here the VERBATIM decomp Ring @2 -- then the
    //     globalObjectIDs preseed LoadGameConfig's hash loop extends.
    //     SetupFunctionTables FIRST: object code dispatches through it.
    SetupFunctionTables();

    // P6.7 wave-1: the RegisterGlobalVariables SEAM (see the thunk above) --
    // the only table slot overridden; every other slot is the engine's own.
    RSDKFunctionTable[FunctionTable_RegisterGlobalVariables] =
        (void *)p6_register_global_variables_saturn;

    RegisterObject((Object **)&DefaultObject, ":DefaultObject:", sizeof(EntityBase), sizeof(ObjectDefaultObject), DefaultObject_Update,
                   DefaultObject_LateUpdate, DefaultObject_StaticUpdate, DefaultObject_Draw, DefaultObject_Create, DefaultObject_StageLoad,
                   DefaultObject_EditorLoad, DefaultObject_EditorDraw, DefaultObject_Serialize);
    RegisterObject((Object **)&DevOutput, ":DevOutput:", sizeof(EntityDevOutput), sizeof(ObjectDevOutput), DevOutput_Update, DevOutput_LateUpdate,
                   DevOutput_StaticUpdate, DevOutput_Draw, DevOutput_Create, DevOutput_StageLoad, DevOutput_EditorLoad, DevOutput_EditorDraw,
                   DevOutput_Serialize);
    // P6.7d.3: Ring registers from the OVERLAY -- the entry at P6_OVL_BASE
    // calls back through the registration thunk (the per-zone pack shape).
    // Verbatim same RegisterObject arguments, same ordering, classID 2.
    if (p6_w_ovl_bytes > 0) {
        s_ovl.register_object      = p6_ovl_register_object;
        s_ovl.register_object_full = p6_ovl_register_object_full; // O1: Spring's Create/StageLoad
        ((p6_ovl_entry_t)P6_OVL_BASE)(&s_ovl);
        p6_w_ovl_classes  = objectClassCount;
        p6_w_ovl_updatefn = (int32)(uint32)s_ovl.update_fn;
        p6_ovl_loserings_raw      = s_ovl.loserings_fn;      // #258b hurt-ring-scatter forward
        p6_ovl_losehyperrings_raw = s_ovl.losehyperrings_fn;
        p6_ovl_badnikbreak_unseeded_raw = s_ovl.badnikbreak_unseeded_fn; // Batch 2 badnik-break forward
        p6_ovl_badnikbreak_raw          = s_ovl.badnikbreak_fn;
        // Batch 3: ItemBox/Debris pack->overlay call forwards (p6_closure_edge stubs).
        p6_ovl_itembox_break_raw         = s_ovl.itembox_break_fn;
        p6_ovl_itembox_state_broken_raw  = s_ovl.itembox_state_broken_fn;
        p6_ovl_itembox_state_falling_raw = s_ovl.itembox_state_falling_fn;
        p6_ovl_itembox_state_idle_raw    = s_ovl.itembox_state_idle_fn;
        p6_ovl_debris_state_move_raw     = s_ovl.debris_state_move_fn;
        // StarPost port (2026-07-17): the ActClear reset forward (stub -> real).
        p6_ovl_starpost_reset_raw        = s_ovl.starpost_reset_fn;
    }

    globalObjectIDs[0] = TYPE_DEFAULTOBJECT; // RetroEngine.cpp:1230
    globalObjectIDs[1] = TYPE_DEVOUTPUT;     // :1232 (REV02)
    globalObjectCount  = TYPE_DEFAULT_COUNT; // :1235

    // 2c) P6.7 wave-1: the GAME-SIDE LINK (the LinkGameLogicDLL call the
    //     engine makes right after the preamble, RetroEngine.cpp:1255-1330
    //     pre-REV02 arm shape :1280-1290). The pack has no input backend yet
    //     (P6.8 W7), so controller/stick/touch pass NULL -- the wave TUs
    //     (Localization/LogHelpers/Options) touch none of them (measured).
    //     curSKU carries the canonical console identity the pre-Plus sku_*
    //     compat arm reads: PLATFORM_SWITCH is the engine's own console
    //     default (UserCore.cpp:238); LANGUAGE_EN drives Localization's
    //     StringsEN.txt branch; region is read by no wave TU.
    SKU::curSKU.platform = PLATFORM_SWITCH;
    SKU::curSKU.language = LANGUAGE_EN;
    SKU::curSKU.region   = 0;
    // P6.7 W7: engine input devices come up BEFORE the game link, the
    // RetroEngine::Init order mirror (InitInputDevices seeds every inputSlot
    // to INPUT_AUTOASSIGN, Input.cpp:94-101; the Saturn SMPC pad device then
    // registers via the InitKeyboardDevice shape, InputDevice_Saturn.cpp).
    // Memory-only inside the masked region (registration `new` rides the
    // pack _sbrk heap; the SMPC snapshot was settled pre-mask above).
    InitInputDevices();
    SKU::InitSaturnInputAPI();
    // Player wave: UnknownInfo joins the link (Game.c:105 REV02 shape --
    // PauseMenu's Unknown_pausePress macro reads it; the engine's own
    // instance is SKU::unknownInfo, Link.cpp:12).
    // P6.7 W7: the ENGINE input arrays ride the link now -- the
    // RetroEngine.cpp:1287-1292 EngineInfo shape (info.controller =
    // controller; info.stickL = stickL; info.touchMouse = &touchInfo). The
    // W15 zeroed-statics stopgap in p6_wave1_reg.c is retired with it.
    p6_wave1_link((void *)RSDKFunctionTable, (void *)&gameVerInfo,
                  (void *)&SKU::curSKU, (void *)&sceneInfo,
                  (void *)controller, (void *)stickL, (void *)&touchInfo,
                  (void *)screens,
                  (void *)&SKU::unknownInfo);
    p6_input_witness(); // game-side ControllerInfo/AnalogStickInfoL/TouchInfo (I3)

    p6_w_obj_classcount = objectClassCount;  // qa_p6_obj O1 / globals G9 (Player wave: 23)
    p6_w_scene_step = 2;

    // 2.5) P6.4 (Task #225): mount the ORIGINAL Data.rsdk pack (cd/DATA.RSDK,
    //      182,962,115 B). LoadDataPack opens it through the windowed GFS
    //      backend, walks the 1677-entry registry into the relocated
    //      dataFileList (WRAM-L 0x2D0C00), and flips useDataPack -- from then
    //      on EVERY non-external FMODE_RB LoadFile resolves by MD5 hash INSIDE
    //      the pack (Reader.cpp:312-314 returns OpenDataFile unconditionally;
    //      there is no loose-file fallback, so the scene parse below is a
    //      pack-routed proof by construction).
    p6_w_load_step      = 19; // #251 LoadDataPack
    p6_w_pack_mounted   = (int32)LoadDataPack("Data.rsdk", 0, false);
    p6_w_pack_filecount = (int32)dataPacks[0].fileCount;
    if (!p6_w_pack_mounted) {
        p6_load_phase_exit();
        return; // loaded stays 0 -> gate RED with the mount diagnosis
    }
    P6_LT_MARK(5); // Task #271 S5: LoadDataPack (DATA.RSDK 182 MB windowed registry walk) -- #251 suspect

    // 3-pre) P6.6 audio proofs run BEFORE LoadGameConfig so the 32 KB
    //    DATASET_SFX pool serves them deterministically: LoadGameConfig's
    //    52-entry global SFX loop lands after, and whatever exceeds the
    //    remaining pool is skipped-and-witnessed by the Saturn LoadSfxToSlot
    //    alloc-guard (Audio.cpp, P6.7c) instead of streaming writes through a
    //    NULL buffer.
    // 7) P6.6a: ENGINE AUDIO CORE -- the Saturn AudioDevice::Init backend runs
    //    the engine's own InitAudioChannels, the UNMODIFIED engine loads the
    //    REAL Global/ScoreAdd.wav from the pack (LoadSfx -> LoadSfxToSlot WAV
    //    parse + S16->F32 convert, Audio.cpp:305-424), and the engine's own
    //    PlaySfx channel allocator (Audio.cpp:441-507) arms channel 0. The
    //    SCSP-audible half keys off this canonical channels[] state in P6.6b.
    {
        p6_w_load_step = 20; // #251 audio proofs (ScoreAdd + MenuBleep)
        AudioDevice::Init();
        p6_w_sfx_inited = (int32)AudioDeviceBase::initializedAudioChannels;
        p6_w_sfx_musbuf = (sfxList[SFX_COUNT - 1].buffer != NULL) ? 1 : 0;

        char sfxPath[0x20];
        strcpy(sfxPath, "Global/ScoreAdd.wav");
        LoadSfx(sfxPath, 1, SCOPE_GLOBAL);

        uint16 sfxID = GetSfx("Global/ScoreAdd.wav");
        p6_w_sfx_id  = (sfxID == (uint16)-1) ? -1 : (int32)sfxID;
        if (p6_w_sfx_id >= 0) {
            SFXInfo *sfx = &sfxList[sfxID];
            p6_w_sfx_len = (int32)sfx->length;

            const uint8 *pb = (const uint8 *)sfx->buffer;
            uint32 sh       = 5381u;
            uint32 nbytes   = (uint32)sfx->length * sizeof(float);
            for (uint32 i = 0; i < nbytes; ++i)
                sh = ((sh << 5) + sh) ^ (uint32)pb[i];
            p6_w_sfx_hash = (int32)sh;

            int32 ch         = PlaySfx(sfxID, 0, 0xFF);
            p6_w_sfx_channel = ch;
            if (ch >= 0) {
                p6_w_sfx_chstate = ((int32)channels[ch].state << 24)
                                 | ((int32)channels[ch].soundID & 0xFFFF);
                p6_w_sfx_chspeed = channels[ch].speed;
                p6_w_sfx_chloop  = (int32)channels[ch].loop;
            }
        }
    }

    // 7b) P6.6b: SCSP-AUDIBLE -- a SECOND engine SFX (MenuBleep: ScoreAdd's
    //     S16 stream measured only 3 distinct byte values, no conclusive
    //     sound-RAM window; MenuBleep windows carry 31-56). The engine loads
    //     + plays it through its own LoadSfx/PlaySfx, the device backend
    //     converts the channel's F32 buffer to S16 once (trunc toward zero,
    //     bit-reproducible: F32 = (s*0.75)/0x8000 exactly), and p6_snd.c
    //     routes it through jo's proven slPCMOn path. The tick re-triggers
    //     every 256 ticks so the bleep is audible and the SCSP ring holds
    //     recent sample bytes for qa_p6_scsp.py A3.
    {
        char sfxPath2[0x20];
        strcpy(sfxPath2, "Global/MenuBleep.wav");
        LoadSfx(sfxPath2, 1, SCOPE_GLOBAL);

        uint16 bleepID = GetSfx("Global/MenuBleep.wav");
        if (bleepID != (uint16)-1) {
            SFXInfo *sfx = &sfxList[bleepID];
            uint32 n     = (uint32)sfx->length;
            if (n > P6_SND_MAXSAMPLES)
                n = P6_SND_MAXSAMPLES;
            for (uint32 i = 0; i < n; ++i)
                p6_sndPcm16[i] = (int16)(sfx->buffer[i] * 32768.0f);
            p6_sndPcmBytes = n * 2;

            uint32 sh       = 5381u;
            const uint8 *pb = (const uint8 *)p6_sndPcm16;
            for (uint32 i = 0; i < p6_sndPcmBytes; ++i)
                sh = ((sh << 5) + sh) ^ (uint32)pb[i];
            p6_w_snd_s16hash = (int32)sh;

            // One-time upload to Sound RAM (+0x6C000), then key slot 28.
            p6_snd_upload(p6_sndPcm16, p6_sndPcmBytes);

            p6_sndSfxID = (int32)bleepID;
            PlaySfx(bleepID, 0, 0xFF);
            p6_snd_play();
            ++p6_w_snd_plays;
        }
    }
    P6_LT_MARK(6); // Task #271 S6: AudioDevice::Init + ScoreAdd + MenuBleep SFX (WAV parse + S16<->F32)


    // 3a) P6.7c THE CALL UNDER TEST #1: the engine's OWN LoadGameConfig
    //     (RetroEngine.cpp:1020-1195) parses the REAL 1.03 GameConfig.bin
    //     from the pack: title strings, activeCategory/startScene, the
    //     46-name global-object hash loop (matches our registered Ring),
    //     the global palette (through the rgb32To16 tables filled in step
    //     2), the 52-entry global SFX list (alloc-guard skips what exceeds
    //     the pool), and the full 92-scene 8-category list into STG storage.
    p6_w_load_step = 21; // #251 LoadGameConfig
    LoadGameConfig();
    {
        uint32 th = 5381u;
        for (const char *p = gameVerInfo.gameTitle; *p; ++p)
            th = ((th << 5) + th) ^ (uint32)(uint8)*p;
        p6_w_cfg_titlehash   = (int32)th;
        p6_w_cfg_globalcount = globalObjectCount;
        p6_w_cfg_ringgid     = globalObjectIDs[2];
        p6_w_cfg_catcount    = sceneInfo.categoryCount;
        p6_w_cfg_startpos    = sceneInfo.listPos;
        p6_w_sfx_skips       = p6_saturn_sfx_skips;
    }
#if defined(P6_FRONTEND_LOGOS)
    p6_w_lt_sfx_savedopen = p6_saturn_sfx_skipped_open; // Task #271: opens the early-out saved
#endif
    P6_LT_MARK(7); // Task #271 S7: LoadGameConfig (GameConfig.bin parse: globals/palette/SFX/92-scene list)
#if defined(P6_FRONTEND_LOGOS)
    // Task #271: total vblanks elapsed across the WHOLE masked core (S1..S7).
    // EXPECT ~0 -- the vblank ISR is masked off AND not yet registered (main.c:1270
    // runs this entire core before jo_core_add_vblank_callback). Proves the masked
    // core cannot be vblank-timed (-> its sub-steps are sized by fills/bytes + FRT).
    p6_w_lt_masked_vbl = (int32)p6_perf_vbl_count;
#endif

    // P6.8 Step B (Task #211): the LEAN SHIPPING boot stops HERE. Everything
    // above is the masked load core BOTH flavors need (InitStorage, the staged
    // overlay/layout-bands/anim-pack/sheets, SetupFunctionTables, RegisterObject
    // set, p6_wave1_link, LoadDataPack, the audio core, LoadGameConfig). Below
    // is DIAG-ONLY scaffolding (the 60-tick GHZ burst, ~14 one-shot proofs, the
    // Title reload, PlayStream's Title CD-DA, the legacy Ring setup). The lean
    // boot skips ALL of it: exit the interrupt mask and return; main.c then
    // registers p6_scene_tick + jo_core_run, and the first lean tick re-loads
    // GHZ live (the proven unmasked, vblank-active p6_ghz_reload path). When
    // p6_lean_boot==0 (DIAG) this is a no-op -- p6_scene_run is byte-for-byte
    // behavior-identical to W19/Step A.
    if (p6_lean_boot) {
        p6_load_phase_exit();
        return;
    }

    // 3a-ghz) P6.7 W11b (Task #226, qa_p6_ghzlive.py): GHZ1 AT SCALE through
    //     the engine's OWN chain, FIRST -- the Title pass below then ALSO
    //     exercises the engine's real scene-CHANGE path (different folder ->
    //     full reload + STG GC, Scene.cpp:139). The band store was mounted
    //     right after its GFS load (step 1.6) and the persistent inflate
    //     scratch armed after InitStorage, so LoadSceneAssets' Saturn layout
    //     arm can bind + refill the FG windows during this pass.
    //     InitObjects is NOT run here: object Create at GHZ scale is the
    //     Player-wave deliverable; this pass proves stage select + entity
    //     PLACEMENT (1,041 slots) + the windowed layout read seam.
    //     Witnesses are copied immediately -- the Title pass overwrites
    //     objectEntityList/tileLayers right after.
    {
#include "p6_ghzlive_probes.inc"
        // GHZ lives in the "Mania Mode" category, NOT category 0 (MEASURED:
        // the cat0-only first cut left the stage witness 0) -- scan every
        // category range the engine built, mirroring the dev menu.
        for (int32 c = 0; c < sceneInfo.categoryCount && p6_w_ghz_stage == 0; ++c) {
            SceneListInfo *cat = &sceneInfo.listCategory[c];
            for (int32 i = cat->sceneOffsetStart; i <= cat->sceneOffsetEnd; ++i) {
                if (!strcmp(sceneInfo.listData[i].folder, "GHZ")
                    && sceneInfo.listData[i].id[0] == '1') {
                    sceneInfo.activeCategory = c;
                    sceneInfo.listPos        = i;
                    p6_w_ghz_stage           = 1;
                    break;
                }
            }
        }
        if (p6_w_ghz_stage == 1) {
            LoadSceneFolder();
            // P6.7 W16 (Task #228): stage the GHZ tile CELLS to NBG1's cell
            // VRAM NOW -- tilesetPixels is the W11b LOAD-PHASE TRANSIENT
            // aliasing the entityList window (see its definition above):
            // LoadSceneFolder's LoadStageGIF just decoded the GHZ
            // 16x16Tiles.gif into it, and LoadSceneAssets' whole-list memset
            // (Scene.cpp:293) + 1,041 entity placements reclaim the window
            // next. Mirrors the Title pass's upload split (the 4-moved
            // block); byte-identical transform (p6_vdp2.c, ST-058 contracts).
            // The camera-anchored PND/scroll half runs AFTER the 60 ticks.
            p6_vdp2_upload_cells((const unsigned char *)tilesetPixels);
            LoadSceneAssets();

            // Census of REGISTERED-class scene entities (the only nonzero
            // classIDs at the current 6-class set = GHZ's 446 rings; the
            // 16 position probes below span ALL 1,041 slots regardless --
            // Scene.cpp writes position for every slot, classID 0 or not).
            int32 n = 0;
            for (int32 s = 0; s < SCENEENTITY_COUNT; ++s)
                if (RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + s)->classID)
                    ++n;
            p6_w_ghz_entcount = n;

            for (int32 i = 0; i < P6_GHZLIVE_PROBE_COUNT; ++i) {
                EntityBase *e = RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + p6_ghzlive_probe_slots[i]);
                p6_w_ghz_probes[i * 3 + 0] = p6_ghzlive_probe_slots[i];
                p6_w_ghz_probes[i * 3 + 1] = e->position.x;
                p6_w_ghz_probes[i * 3 + 2] = e->position.y;
            }
            for (int32 i = 0; i < P6_GHZLIVE_TILEPROBE_COUNT; ++i)
                p6_w_ghz_tiles[i] = (int32)(uint16)GetTile((uint16)p6_ghzlive_tile_probes[i][0],
                                                           p6_ghzlive_tile_probes[i][1],
                                                           p6_ghzlive_tile_probes[i][2]);
            p6_w_ghz_binds  = p6_saturn_layer_binds;
            p6_w_ghz_clamps = p6_saturn_tempentity_skips + p6_saturn_group_skips
                            + p6_saturn_layer_unbound + p6_saturn_settile_drops;

            // P6.7 Player wave (Task #227): InitObjects AT GHZ SCALE -- the
            // ENGINESTATE_LOAD chain's third call (RetroEngine.cpp:359-361),
            // previously Title-only. StageLoad dispatches for all 26
            // registered classes (Player_StageLoad runs Player_LoadSprites,
            // Player.c:714 -- CopyEntity scene Player -> SLOT_PLAYER1, then
            // destroys the scene slot), then Create for every nonzero-class
            // entity (446 Ring_Create via the overlay fns, 2 Player_Create
            // at the reserve slots, ...). Music is CD-safe here: play fires
            // only from Music_State_PlayOnLoad, an Update state the diag
            // never ticks for GHZ. Scene-slot evidence is captured by the
            // game-side pre witness BEFORE the call (Player.c:781-815).
            // W14 fix: the SCREEN env must exist BEFORE InitObjects --
            // Camera_Create (dispatched inside it) reads ScreenInfo->center
            // (Camera.c:63,66) and Camera_SetCameraBounds positions the
            // screen as player_px - center (Camera.c:105-107); a zero
            // center parks the viewport 16 px below the spawn and the
            // Player never goes onScreen (MEASURED p6_f4: drawflags
            // onScreen=0). Mirror the engine SetScreenSize shape:
            // center = size/2.
            videoSettings.screenCount = 1;
            screens[0].size.x         = SCREEN_XMAX;
            screens[0].size.y         = SCREEN_YSIZE;
            screens[0].center.x       = SCREEN_XMAX / 2;
            screens[0].center.y       = SCREEN_YSIZE / 2;
            screens[0].pitch          = SCREEN_XMAX;
            // W14c: clip bounds armed too -- the DrawSprite clip-accept
            // (p6_draw_flipped) rejects against clipBound_*; ProcessObject-
            // DrawLists only normalizes them AFTER each group's entity
            // draws (Object.cpp:880-900), so zeroed bounds would reject
            // every draw in the first pass.
            screens[0].clipBound_X1   = 0;
            screens[0].clipBound_Y1   = 0;
            screens[0].clipBound_X2   = SCREEN_XMAX;
            screens[0].clipBound_Y2   = SCREEN_YSIZE;
            currentScreen             = &screens[0];
            sceneInfo.entity          = RSDK_ENTITY_AT(0);
            // W14b discriminator: read the count back through BOTH paths.
            p6_w_eng_vs_count     = (int32)videoSettings.screenCount;
            p6_w_eng_screencount  = GetVideoSetting(VIDEOSETTING_SCREENCOUNT);

            p6_player_witness_pre(RESERVE_ENTITY_COUNT, SCENEENTITY_COUNT);
            InitObjects();
            p6_purge_scene_players(); // #256: destroy stray scene-placed Player sidekicks
            p6_player_witness_post();

            // W14 (Task #227): FIRST ENGINE GAMEPLAY TICKS at GHZ scale --
            // the exact ENGINESTATE_REGULAR pair (RetroEngine.cpp:365-368,
            // per-tick proven at Title scale since P6.7a): ProcessObjects
            // runs every inRange entity's Update (the verbatim Player state
            // machine included) and ProcessObjectDrawLists dispatches the
            // draw-group walk into the REAL DrawSprite backend
            // (Player_Draw -> ANIMPAK frames -> banded Sonic sheet ->
            // VDP1 slot cache). Two ticks; SLOT_PLAYER1 snapshotted after.
            {
                // Draw groups visible for the ticks (the screen env is
                // armed BEFORE InitObjects above -- Camera_Create needs it).
                for (int32 g = 0; g < DRAWGROUP_COUNT; ++g)
                    engine.drawGroupVisible[g] = true;

                // VDP1 bind table for EVERY live GHZ surface (the step-8
                // Ring env binds only its own sheet, and only later):
                // resident surfaces bind their pixels; banded ones bind
                // their SaturnSheet slot (the W12b fetch path). DrawSprite
                // (this TU) routes blits via p6_vdp1HandleBySurface.
                if (!p6_vdp1HandlesInit) {
                    for (int32 i = 0; i < SURFACE_COUNT; ++i)
                        p6_vdp1HandleBySurface[i] = -1;
                    p6_vdp1HandlesInit = true;
                }

                // W18 FIX (Task #227): the GHZ scene's Ring entities draw the
                // Global/Items.gif sheet, but the overlay Ring registers a NULL
                // Create + StageLoad (p6_ovl_register_object:1047-1049), so the
                // GHZ InitObjects chain never loads it -> Items.gif gets no
                // gfxSurface entry -> the bind loop below cannot bind it.
                // Populate the Items.gif surface HERE with a SHEET-ONLY load
                // (RSDK.LoadSpriteSheet, NOT LoadSpriteAnimation): it allocates
                // a gfxSurface slot and resolves the already-staged ITEMS.SHT
                // banded slot (W12a; SaturnSheet_FindSlot by path hash ->
                // saturnSheetSlot set, Sprite.cpp:949-967) WITHOUT touching the
                // DATASET_STG anim pool. MEASURED-WHY sheet-only: the full
                // LoadSpriteAnimation("Global/Ring.bin") overflows the GHZ anim
                // pool after the Player closure StageLoads (p6_saturn_anim_-
                // allocfail 0 -> 1) -- that both fails to load AND regresses
                // qa_p6_player P7 (allocfail==0). The bind loop below then binds
                // the Items surface (saturnSheetSlot 3 -> banded handle >= 0).
                {
                    int32 rsurf =
                        (int32)(int16)LoadSpriteSheet("Global/Items.gif", SCOPE_STAGE);
                    p6_w_ringspr   = rsurf; // reuse the witness as the Items surf id
                    if (rsurf >= 0 && rsurf < SURFACE_COUNT)
                        p6_w_ringsheet = (int32)gfxSurface[rsurf].saturnSheetSlot;
                }

                // W19 (Task #227): same SHEET-ONLY nudge for the GHZ HUD +
                // Shield surfaces. The HUD (Global/Display.gif, 960 drops/run)
                // and Shields (Global/Shields.gif, 59 drops/run) draw blits but
                // their surfaces had no banded slot -> every blit dropped at the
                // VDP1 handle<0 check (MEASURED 1,139 silent drops total: 960
                // Display + 120 Tails1 + 59 Shields). LoadSpriteSheet (NOT
                // LoadSpriteAnimation -- zero DATASET_STG cost) allocates each
                // surface + resolves its already-staged DISPLAY.SHT/SHIELDS.SHT
                // banded slot by path hash (Sprite.cpp:949-967); the bind loop
                // below then binds them (banded handle >= 0). Tails1.gif is NOT
                // staged (band store overflows the VDP2 window by 19,105 B) so
                // its 120 drops remain -- the DECLARED W19 GAP.
                {
                    int32 dsurf =
                        (int32)(int16)LoadSpriteSheet("Global/Display.gif", SCOPE_STAGE);
                    p6_w_dispsurf = dsurf;
                    if (dsurf >= 0 && dsurf < SURFACE_COUNT)
                        p6_w_dispsheet = (int32)gfxSurface[dsurf].saturnSheetSlot;
                    int32 ssurf =
                        (int32)(int16)LoadSpriteSheet("Global/Shields.gif", SCOPE_STAGE);
                    p6_w_shldsurf = ssurf;
                    if (ssurf >= 0 && ssurf < SURFACE_COUNT)
                        p6_w_shldsheet = (int32)gfxSurface[ssurf].saturnSheetSlot;
                }

                for (int32 i = 0; i < SURFACE_COUNT; ++i) {
                    GFXSurface *sf = &gfxSurface[i];
                    if (sf->scope == SCOPE_NONE || p6_vdp1HandleBySurface[i] >= 0)
                        continue;
                    int32 h = -1;
                    if (sf->pixels)
                        h = p6_vdp1_sheet_bind(sf->pixels, sf->width,
                                               (const unsigned short *)fullPalette[0]);
                    else if (sf->saturnSheetSlot >= 0)
                        h = p6_vdp1_sheet_bind_banded(sf->saturnSheetSlot, sf->width,
                                                      (const unsigned short *)fullPalette[0]);
                    else
                        continue;
                    p6_vdp1HandleBySurface[i] = (int8)h;
#if defined(P6_FRAMEDIR)
                    // Stage-1 FRD attach -- mirror of the arm_env bind loop.
                    if (h >= 0) {
                        int32 fs = SaturnFrameDir_FindSlot((const uint32 *)sf->hash);
                        if (fs >= 0)
                            p6_vdp1_sheet_set_frd(h, fs);
                    }
#endif
                    if (h >= 0)
                        ++p6_w_bind_count;
                    if (p6_w_bind_logn < 8)
                        p6_w_bind_log[p6_w_bind_logn++] = (i << 8) | (h & 0xFF);
                }

                // W18: surface census + Items.gif resolve. LoadSpriteSheet
                // returns the EXISTING surface index for an already-loaded
                // sheet (Sprite.cpp:917-921 hash match) without re-loading --
                // so this is a pure lookup of whether the GHZ load populated
                // an Items.gif surface at all (RED: returns a fresh/none slot).
                {
                    int32 pop = 0;
                    for (int32 i = 0; i < SURFACE_COUNT; ++i) {
                        GFXSurface *sf = &gfxSurface[i];
                        if (sf->scope == SCOPE_NONE)
                            continue;
                        ++pop;
                        if (i < 16) {
                            p6_w_surfcensus[i] =
                                ((int32)sf->scope << 24)
                                | ((sf->pixels ? 1 : 0) << 16)
                                | (((int32)sf->saturnSheetSlot & 0xFF) << 8)
                                | ((int32)p6_vdp1HandleBySurface[i] & 0xFF);
                            p6_w_surfhash[i] = (int32)sf->hash[0];
                        }
                    }
                    p6_w_surfpop = pop;
                    // W19: capture the Display/Shields bound handles (the bind
                    // loop above set p6_vdp1HandleBySurface for surfaces whose
                    // saturnSheetSlot resolved). LoadSpriteSheet returns the
                    // EXISTING surface index for the already-loaded sheet.
                    {
                        int32 dsurf =
                            (int32)(int16)LoadSpriteSheet("Global/Display.gif", SCOPE_STAGE);
                        if (dsurf >= 0 && dsurf < SURFACE_COUNT)
                            p6_w_disphandle = (int32)p6_vdp1HandleBySurface[dsurf];
                        int32 ssurf =
                            (int32)(int16)LoadSpriteSheet("Global/Shields.gif", SCOPE_STAGE);
                        if (ssurf >= 0 && ssurf < SURFACE_COUNT)
                            p6_w_shldhandle = (int32)p6_vdp1HandleBySurface[ssurf];
                    }
                    int32 isurf = (int32)(int16)LoadSpriteSheet("Global/Items.gif", SCOPE_STAGE);
                    p6_w_itemsurf = isurf;
                    p6_w_ringsheethandle =
                        (isurf >= 0 && isurf < SURFACE_COUNT)
                            ? (int32)p6_vdp1HandleBySurface[isurf] : -3;
                    if (isurf >= 0 && isurf < SURFACE_COUNT) {
                        p6_w_itemshandle = (int32)p6_vdp1HandleBySurface[isurf];
                        // (scope<<24)|(hasPx<<16)|(shtSlot&0xFF)<<8|handle: proves
                        // whether the banded ITEMS.SHT slot resolved on load.
                        GFXSurface *isf = &gfxSurface[isurf];
                        p6_w_itemsurf =
                            (isurf << 24)
                            | ((isf->pixels ? 1 : 0) << 16)
                            | (((int32)isf->saturnSheetSlot & 0xFF) << 8)
                            | ((int32)p6_vdp1HandleBySurface[isurf] & 0xFF);
                    }
                }

                int32 slots0 = p6_w_vdp1_slots;
                int32 draws0 = p6_w_draw_calls;
                // W15 (P9): 60 ticks -- enough for Player_State_Air gravity
                // (0x3800/tick) to drop the spawn onto the GHZ1 start ground
                // through the verbatim collision chain and settle onGround.
                for (int32 t = 0; t < 60; ++t) {
                    // P6.7 W7: the verbatim engine input tick joins the loop
                    // -- the exact ENGINESTATE_REGULAR head (RetroEngine.cpp
                    // :392-394: ProcessInput(); ... ProcessObjects();).
                    // ProcessInput -> device UpdateInput (Input.cpp:199-205)
                    // -> the SMPC snapshot -> RSDK::controller, which the
                    // game's ControllerInfo now aliases via the link above.
                    ProcessInput();
                    ProcessObjects();
                    ProcessObjectDrawLists();
                }
                p6_w_plr_ticks     = 60;
                p6_w_plr_slotdelta = p6_w_vdp1_slots - slots0;
                p6_w_plr_drawdelta = p6_w_draw_calls - draws0;
                p6_player_witness_tick();
                // P6.7 W7 post-tick input-contract witnesses (gate I4):
                // device registered + active, autoassign slot persisted
                // (anyPress==0 -> GetAvaliableInputDevice returns
                // INPUT_AUTOASSIGN, Input.hpp:529-539 / Input.cpp:217-221),
                // and every controller down/press bit clear (headless boot).
                p6_w_in_devcount = inputDeviceCount;
                if (inputDeviceCount > 0 && inputDeviceList[0]) {
                    InputDevice *p6dev = inputDeviceList[0];
                    p6_w_in_devid    = (int32)p6dev->id;
                    p6_w_in_devstate = ((int32)p6dev->active << 16)
                                     | ((int32)p6dev->isAssigned << 8)
                                     | (int32)p6dev->anyPress;
                }
                p6_w_in_slot0 = inputSlots[0];
                {
                    int32 bb = 0;
                    for (int32 c = 0; c <= PLAYER_COUNT; ++c) {
                        // ControllerState == 12 consecutive InputState at
                        // REV02 (Input.hpp:387-410) -- keyUp is element 0.
                        InputState *ks = &controller[c].keyUp;
                        for (int32 k = 0; k < 12; ++k)
                            bb |= (ks[k].down ? 1 : 0) | (ks[k].press ? 2 : 0);
                    }
                    p6_w_in_btnbits = bb;
                }
                if (p6_w_plr_sheetid_t >= 0 && p6_w_plr_sheetid_t < SURFACE_COUNT)
                    p6_w_plr_handle = p6_vdp1HandleBySurface[p6_w_plr_sheetid_t];
            }

            // P6.7 W16 (Task #228, qa_p6_scroll.py): the GHZ1 FOREGROUND
            // becomes VISIBLE on NBG1, anchored to the LIVE camera. The 60
            // ticks left screens[0].position bounds-clamped at (0,780)
            // (Camera_SetCameraBounds, Camera.c:105-107; witnessed in
            // p6_w_scr_x/y above). Cells went to VDP2 A0/A1 during the load
            // phase (the upload between LoadSceneFolder/LoadSceneAssets);
            // this builds the camera-local 64x64 PND page from the W11a
            // sliding-window accessor over FG Low (band-store layer 3 --
            // p6_layout_model.json), writes CRAM bank 0 from the GHZ active
            // palette, and arms SCXIN1/SCYIN1 via slScrPosNbg1 (ST-058-R2
            // 0x180080/0x180084). The handful of extra window refills stays
            // far inside qa_p6_layout L4's 4096 bound.
            {
                unsigned int ph = 0;
                int nb = 0;
                p6_vdp2_present_ghz_camera(p6_fglow_layer_index() /* per-zone FG Low */,
                                           screens[0].position.x,
                                           screens[0].position.y,
                                           (const unsigned short *)fullPalette[0],
                                           &ph, &nb);
                p6_w_scr2_x       = screens[0].position.x;
                p6_w_scr2_y       = screens[0].position.y;
                p6_w_scr2_pndhash = (int32)ph;
                p6_w_scr2_nblank  = nb;
                p6_w_scr2_done    = 1;
            }
        }
    }

    // 3b) Harness stage-select (the dev-menu equivalent): find the Title
    //     entry in the ENGINE-built list by folder name and park listPos on
    //     it. The discovery is witnessed; the gate asserts it equals the
    //     offline parse (listPos 1, category 0).
    {
        SceneListInfo *cat0 = &sceneInfo.listCategory[0];
        sceneInfo.activeCategory = 0; // W11b: the GHZ pass parked on Mania Mode
        for (int32 i = cat0->sceneOffsetStart; i <= cat0->sceneOffsetEnd; ++i) {
            if (!strcmp(sceneInfo.listData[i].folder, "Title")) {
                sceneInfo.listPos = i;
                p6_w_cfg_titlepos = i;
                break;
            }
        }
    }

    // 3c) P6.7c THE CALL UNDER TEST #2: the engine's OWN scene-load chain,
    //     mirroring ProcessEngine's ENGINESTATE_LOAD case (RetroEngine.cpp
    //     :359-361) -- LoadSceneFolder(); LoadSceneAssets(); InitObjects();
    //     (SKU::userCore->StageLoad() omitted: userCore is the NULL pack
    //     stub; ProcessInput deferred with the input backend, P6.8 item.)
    //     LoadSceneFolder clears groups/anims/surfaces, parses TileConfig
    //     (absent for Title -> windows stay zero, witnessed), parses the
    //     REAL StageConfig (useGlobalObjects=0 -> classCount=2), loads the
    //     stage palette rows, and LoadStageGIF-decodes the tileset with the
    //     CANONICAL palette merge (GIF colors only into inactive rows,
    //     Scene.cpp:988-998).
    LoadSceneFolder();
    p6_w_cfg_classcount0 = sceneInfo.classCount;

    // 4-moved + 5a) P6.7 W11b: tilesetPixels is a LOAD-PHASE TRANSIENT in
    //     the entityList window (see the alias note at its definition), so
    //     BOTH the GIF witness hash AND the VDP2 cell+CRAM upload must run
    //     HERE -- after LoadSceneFolder's LoadStageGIF decode, before
    //     LoadSceneAssets' whole-list memset (Scene.cpp:293) reclaims the
    //     window. Pixel bytes are unchanged: qa_p6_gif's Pillow model and
    //     qa_p6_vdp2's VRAM byte-checks hold verbatim.
    {
        const uint8 *px = (const uint8 *)tilesetPixels;
        uint32 h        = 5381u;
        for (uint32 i = 0; i < 0x40000u; ++i)
            h = ((h << 5) + h) ^ (uint32)px[i];
        p6_w_gif_hash   = (int32)h;
        p6_w_gif_b0     = (int32)px[0];
        p6_w_gif_loaded = 1;
    }
    // P6.7 W16 (Task #228): the Title island present is DISPLACED when the
    // GHZ FG present ran -- NBG1 has a single 256 KB cell budget (A0+A1;
    // both tilesets are 1024 x 256 B) and a single 16 KB PND map, so the
    // two presents are mutually exclusive by VRAM arithmetic. The W16
    // deliverable is the GHZ FG in the final frame; re-uploading the Title
    // cells here would clobber the GHZ cells staged during the GHZ load
    // phase. MEASURED CONFLICT, reported: qa_p6_vdp2.py (P6.5b1 Title
    // island gate, NOT in the W16 regression sweep) goes RED-by-design on
    // this build; the GIF witness hash above (qa_p6_gif) still runs.
    if (!p6_w_scr2_done)
        p6_vdp2_upload_cells((const unsigned char *)tilesetPixels);

    //     Harness Ring append -- the EXACT engine stage-class append shape
    //     (Scene.cpp:199-205 + the static-vars classID/active lines
    //     :237-239): Title's StageConfig doesn't list Ring, but the proof
    //     entity must stay registered as a stage class. Witnessed; the
    //     engine path does the identical writes for hash-matched classes.
    {
        Object *ringStatic = *(Object **)s_ovl.staticvars_slot; // overlay vtable
        stageObjectIDs[sceneInfo.classCount] = P6_OBJ_RING_CLASSID; // objectClassList id 2
        // P6.7 wave-1: the STAGE class index is no longer numerically equal
        // to the objectClassList id -- Title's StageConfig stage list names
        // Options + Localization (MEASURED, 11-entry list), which now
        // hash-match ahead of this append (cc0 2 -> 4). ResetEntitySlot and
        // the entity-classID witnesses consume the STAGE index.
        s_ring_stage_classid = sceneInfo.classCount;
        ringStatic->classID = sceneInfo.classCount;                 // Scene.cpp:237
        ringStatic->active  = ACTIVE_NORMAL;                        // Scene.cpp:238-239
        sceneInfo.classCount++;                                     // Scene.cpp:203
    }
    p6_w_cfg_classcount = sceneInfo.classCount;

    //     THE CALL UNDER TEST (P6.3, unchanged): unmodified engine scene
    //     parse of "Data/Stages/Title/Scene1.bin" from the pack. Scene
    //     classes hash-resolve against stageObjectIDs[0..classCount): none
    //     of Title's classes are registered -> classID 0 fallback, entity
    //     slots + positions land byte-identically to P6.3 (gate W4-W6).
    LoadSceneAssets();
    p6_w_scene_step = 3;

    //     InitObjects (Object.cpp:327-362): stageLoad dispatch over the 3
    //     stage classes (DefaultObject/DevOutput empty bodies; Ring NULL),
    //     create for classID!=0 entities (none at Title), then the engine
    //     itself sets sceneInfo.state = ENGINESTATE_REGULAR (:358) and adds
    //     camera 0 (:360-361) -- both retired from the hand-wiring.
    //     createSlot: the stock `ENTITY_COUNT - 0x100` literal would wrap the
    //     uint16 to 0xFFC0 at the 0xC0 retarget (a WILD CreateEntity index at
    //     0x017CE800, outside WRAM-L -- P6.7c verification finding); the
    //     engine line now uses TEMPENTITY_START (PC-byte-identical), and the
    //     witness below asserts the value (0x80 at Title scale).
    InitObjects();
    p6_w_createslot = (int32)sceneInfo.createSlot;
    // P6.7 wave-1: Localization_StageLoad ran inside InitObjects' stageLoad
    // dispatch (Title's StageConfig names Options + Localization in its
    // 11-entry stage list, MEASURED; loadGlobalObjects=0 is irrelevant --
    // the stage list itself carries them). Copy the game-side witness.
    p6_wave1_witness();

    // 3d) P6.7c witnesses over the LoadSceneFolder side effects: the
    //     TileConfig windows (zero for Title -- MEASURED absent from the
    //     1.03 pack; any stray write fires the gate) and the canonical
    //     fullPalette[0] (512 B big-endian uint16[256] image).
    {
        // P6.7 W11: the raw-mask window is DEAD (packed arm); the TileConfig
        // witness now hashes the PACKED window -- zero at Title (its
        // TileConfig is absent and step 9's GHZ load runs AFTER this copy).
        const uint8 *cm = (const uint8 *)P6_HW_PACKEDCOL;
        uint32 h = 5381u;
        for (uint32 i = 0; i < 0x10000u; ++i)
            h = ((h << 5) + h) ^ (uint32)cm[i];
        p6_w_til_cmhash = (int32)h;

        const uint8 *ti = (const uint8 *)P6_LW_TILEINFO;
        h = 5381u;
        for (uint32 i = 0; i < 0x2800u; ++i)
            h = ((h << 5) + h) ^ (uint32)ti[i];
        p6_w_til_tihash = (int32)h;

        const uint8 *pal = (const uint8 *)fullPalette[0];
        h = 5381u;
        for (uint32 i = 0; i < 512u; ++i)
            h = ((h << 5) + h) ^ (uint32)pal[i];
        p6_w_pal_hash = (int32)h;
    }

    // 3.5) Pack-routing witness: re-open the scene file exactly as the engine
    //      did and record where it came from. OpenDataFile stamps the in-pack
    //      byte offset into FileInfo.fileOffset (Reader.cpp:218); pack data
    //      always sits past the header+registry, so fileOffset > 0 iff the
    //      open rode the pack (a loose open leaves InitFileInfo's zero).
    {
        FileInfo pfi;
        InitFileInfo(&pfi);
        if (LoadFile(&pfi, "Data/Stages/Title/Scene1.bin", FMODE_RB)) {
            p6_w_pack_used = (pfi.fileOffset > 0) ? 1 : 0;
            CloseFile(&pfi);
        }
    }

    // 4) P6.5a (Task #208): the tileset-decode witness. P6.7c: the hand
    //    LoadStageGIF call is RETIRED -- the engine's own LoadSceneFolder ran
    //    it (Scene.cpp:272-273) with the CANONICAL palette merge (GIF colors
    //    only into rows inactive in both masks, Scene.cpp:988-998; active
    //    rows come from the global/stage palettes via LoadSceneAssets:308-319
    //    -- witnessed by p6_w_pal_hash in step 3d). The rgb32To16 fill moved
    //    to step 2 (LoadGameConfig's global-palette read needs it). The pixel
    //    bytes are merge-independent, so qa_p6_gif.py's Pillow model holds
    //    unchanged.
    // (4: the GIF witness hash + 5a: the VDP2 cell/CRAM upload MOVED to the
    //  Title-pass LoadSceneFolder->LoadSceneAssets gap -- W11b transient
    //  tilesetPixels; see the 4-moved block above.)

    // 5b) P6.5b1: FIRST ENGINE-RENDERED PIXELS, layout half -- blank-char
    //    pick + PND map + NBG1 display from the engine's own layout (cells
    //    + CRAM already in VDP2 VRAM from 5a). main.c then parks the diag
    //    build on jo_core_run so the layer stays on screen for the capture.
    // W16: skipped when the GHZ FG present owns NBG1 (see the displacement
    // note at the cells-upload guard above). p6_w_vdp2_done stays 0 on the
    // W16 build -- honest signal that the Title island was NOT presented.
    if (!p6_w_scr2_done) {
        p6_vdp2_present_layout((const unsigned short *)tileLayers[3].layout,
                               tileLayers[3].widthShift,
                               (const unsigned short *)fullPalette[0]);
        p6_w_vdp2_done = 1;
    }

    // 6) P6.5b2: ENGINE OBJECT SPRITES -- the engine loads the REAL
    //    Global/Ring.bin animation set from the pack (LoadSpriteAnimation ->
    //    LoadSpriteSheet -> ImageGIF, Animation.cpp:11-94 + Sprite.cpp:906),
    //    the proof copies the parsed metadata into witnesses byte-for-byte
    //    (qa_p6_sprite.py model = parse_spr on the same Ring.bin), uploads
    //    anim-0 frame-0's sheet rect to VDP1, and arms the engine animator
    //    that p6_scene_tick() advances with the engine's own ProcessAnimation.
    {
        uint16 sprID = LoadSpriteAnimation("Global/Ring.bin", SCOPE_STAGE);
        p6_w_spr_id  = (sprID == (uint16)-1) ? -1 : (int32)sprID;
        if (p6_w_spr_id >= 0) {
            SpriteAnimation *spr = &spriteAnimationList[sprID];
            p6_w_spr_animcount   = (int32)spr->animCount;
            SpriteFrame *f0      = &spr->frames[spr->animations[0].frameListOffset];
            p6_w_spr_f0xy        = ((int32)f0->sprX << 16) | (int32)f0->sprY;
            p6_w_spr_f0wh        = ((int32)f0->width << 16) | (int32)f0->height;
            p6_w_spr_f0pv = (((int32)f0->pivotX & 0xFFFF) << 16) | ((int32)f0->pivotY & 0xFFFF);
            p6_w_spr_f0dur       = (int32)f0->duration;

            // MEASURED FAILURE MODE (P6.5b2 T2): a failed LoadSpriteSheet
            // returns (uint16)-1 which the uint8 sheetIDs[] truncates to 0xFF
            // (Animation.cpp:39/62) -- gfxSurface[0xFF] is OOB (SURFACE_COUNT
            // slots) and the unguarded hash loop dereferenced a wild pixels
            // pointer, killing the whole run body (scene_step stuck at 3,
            // every witness after block 6 unwritten). Witness the raw id and
            // only touch gfxSurface when it is a real, populated slot.
            p6_w_spr_sheetid = (int32)f0->sheetID;
            GFXSurface *sheet = &gfxSurface[f0->sheetID];
            // Task #227 ITEMS.SHT: Items.gif is BANDED now (saturnSheetSlot
            // >= 0, pixels == NULL) -- the resident-pixel hash and the
            // legacy whole-sheet VDP1 bind below only run for RESIDENT
            // sheets; the banded case still arms the Ring proof (the
            // DrawSprite slot cache serves rects via SaturnSheet_FetchRect,
            // the W12b fetch seam). sheethash = -2 marks the banded arm for
            // the gate model.
            if (f0->sheetID < SURFACE_COUNT && sheet->scope != SCOPE_NONE
                && (sheet->pixels || sheet->saturnSheetSlot >= 0)) {
                if (sheet->pixels) {
                    uint32 sh     = 5381u;
                    uint32 nbytes = (uint32)sheet->width * (uint32)sheet->height;
                    for (uint32 i = 0; i < nbytes; ++i)
                        sh = ((sh << 5) + sh) ^ (uint32)sheet->pixels[i];
                    p6_w_spr_sheethash = (int32)sh;
                }
                else {
                    p6_w_spr_sheethash = -2;
                }

                SetSpriteAnimation(sprID, 0, &p6_ringAnimator, true, 0);
                // #W18 ring-arm: the manual proof-ring (slot P6_OBJ_RING_SLOT) is
                // RETIRED -- the real Game_Ring Ring_Create now arms every placed GHZ
                // ring. Leaving p6_objRingFrames NULL skips all `if (p6_objRingFrames)`
                // proof blocks (spawn + witness). (was &spr->frames[spr->animations[0].frameListOffset])
                p6_objRingFrames = NULL;

                // P6.5b3: DrawSprite environment. Drawing.cpp:2683 translates
                // through currentScreen->position; :2676/:2687 dereference
                // sceneInfo.entity unconditionally. screens[0] sits zeroed in
                // WRAM-L (Group-B absolute, memset in step 0) -> position
                // (0,0); entity slot 0 is zeroed -> drawFX=FX_NONE,
                // inkEffect=INK_NONE, FLIP_NONE.
                currentScreen      = &screens[0];
                screens[0].size.x  = SCREEN_XMAX;
                screens[0].size.y  = SCREEN_YSIZE;
                // W14c: the Title pass repositions the screen too -- the GHZ
                // ticks left it camera-parked at (0,780); the Ring proof
                // entity sits at screen-abs (260,60). Clip bounds armed for
                // the new DrawSprite clip-accept (p6_draw_flipped).
                screens[0].position.x   = 0;
                screens[0].position.y   = 0;
                screens[0].clipBound_X1 = 0;
                screens[0].clipBound_Y1 = 0;
                screens[0].clipBound_X2 = SCREEN_XMAX;
                screens[0].clipBound_Y2 = SCREEN_YSIZE;
                sceneInfo.entity   = RSDK_ENTITY_AT(0);

                // W12b: capture the bind HANDLE into the surfaceID map the
                // DrawSprite backend consults per frame (multi-sheet cache).
                if (!p6_vdp1HandlesInit) {
                    for (int32 i = 0; i < SURFACE_COUNT; ++i)
                        p6_vdp1HandleBySurface[i] = -1;
                    p6_vdp1HandlesInit = true;
                }
                // W14 fix: the banded arm BINDS too -- the prior "keep
                // handle -1" comment was the silent Ring-invisible
                // regression (DrawSprite call counters stayed GREEN while
                // p6_vdp1_blit dropped every handle -1 blit; MEASURED
                // p6_f4: p6_w_vdp1_slots == 0 post-boot).
                if (sheet->pixels)
                    p6_vdp1HandleBySurface[f0->sheetID] =
                        (int8)p6_vdp1_sheet_bind(sheet->pixels, sheet->width,
                                                 (const unsigned short *)fullPalette[0]);
                else if (sheet->saturnSheetSlot >= 0)
                    p6_vdp1HandleBySurface[f0->sheetID] =
                        (int8)p6_vdp1_sheet_bind_banded(sheet->saturnSheetSlot, sheet->width,
                                                        (const unsigned short *)fullPalette[0]);
            }
        }
    }

    // 8) P6.7c: spawn the proof entity through the engine path. Registration
    //    + SetupFunctionTables moved to step 2b (the InitGameLink mirror);
    //    stageObjectIDs/classCount now come from the ENGINE's StageConfig
    //    path + the witnessed harness append (step 3c); ENGINESTATE_REGULAR
    //    is set by the engine's own InitObjects (Object.cpp:358). Remaining
    //    NO-CTORS environment: videoSettings + engine fields, set explicitly
    //    (ProcessObjectDrawLists reads both, Object.cpp:736-752).
    if (p6_objRingFrames) {
        videoSettings.screenCount  = 1;
        engine.drawGroupVisible[3] = true;
        p6_obj_spawn_ring();
    }

    // 9) P6.7 PACKED COLLISION (Task #210, qa_p6_collision K2-K5): the
    //    engine's own LoadTileConfig over the REAL GHZ TileConfig.bin from
    //    the pack -- Title ships no TileConfig, so this is the packer's
    //    data-bearing proof. The Scene.cpp P6_CM arm packs into the
    //    0x060E0000 window; the byte-exact model + the 128 flip-probe
    //    expectations come from gen_collision_model.py (raw/info hashes ==
    //    the P6.7d.2 contract values). MUST run BEFORE PlayStream (pack
    //    read = CD op; the 7c-moved rule below). Runs AFTER the Title til
    //    witnesses (step 3d) so their zero-window model is undisturbed
    //    (tileInfo gets GHZ angles only here).
    {
        // (window zeroed in the pre-state -- the 3d witness reads it first)
        LoadTileConfig((char *)"Data/Stages/GHZ/TileConfig.bin");
        p6_w_col_loaded = 1;

        uint32 h = 5381u;
        const uint8 *pb = (const uint8 *)P6_HW_PACKEDCOL;
        for (int32 i = 0; i < 0x10000; ++i)
            h = ((h << 5) + h) ^ pb[i];
        p6_w_col_packedhash = (int32)h;

        h = 5381u;
        const uint8 *ti = (const uint8 *)P6_LW_TILEINFO;
        for (int32 i = 0; i < 0x2800; ++i)
            h = ((h << 5) + h) ^ ti[i];
        p6_w_col_infohash = (int32)h;

#include "p6_collision_probes.inc"
        int32 good = 0;
        p6_w_col_firstbad = -1;
        for (int32 i = 0; i < (int32)(sizeof(p6CollisionProbes) / sizeof(p6CollisionProbes[0])); ++i) {
            uint8 got;
            if (p6CollisionProbes[i].dir < 4)
                got = PackedCollisionMask(p6CollisionProbes[i].plane, p6CollisionProbes[i].tile,
                                          p6CollisionProbes[i].dir, p6CollisionProbes[i].col);
            else
                got = PackedTileAngle(p6CollisionProbes[i].plane, p6CollisionProbes[i].tile,
                                      (uint8)(p6CollisionProbes[i].dir - 4));
            if (got == p6CollisionProbes[i].expect)
                ++good;
            else if (p6_w_col_firstbad < 0)
                p6_w_col_firstbad = i;
        }
        p6_w_col_probes = good;
    }

    // 10) P6.7 W11a (qa_p6_layout L2-L4): mount the band store loaded at
    //     step 1.6 and replay the offline probe set through the REAL
    //     windowed accessor -- in-window hits, window crossings (the
    //     clustered x-walk in the probe table forces refills), and BG
    //     layers via slot rebinding. No CD I/O here (the store is RAM).
    {
        extern int32 SaturnLayout_Mount(const void *blob);
        extern void SaturnLayout_Bind(int32 slot, int32 layer);
        extern uint16 SaturnLayout_GetTile(int32 slot, int32 tx, int32 ty);

        // W11b: the PERSISTENT scratch + mount armed at step 1.6 serve this
        // replay too (the W11a per-step stack local is retired -- the
        // documented dies-after-probes trap). Re-mount only resets the slot
        // windows, which the probe loop rebinds anyway.
        if (p6_w_lay_bytes > 0 && SaturnLayout_Mount((const void *)P6_LW_LAYOUTBANDS) > 0) {
#include "p6_layout_probes.inc"
            int32 bound0 = -1;
            SaturnLayout_Bind(1, 4); // FG High stays resident on slot 1
            int32 good = 0;
            p6_w_lay_firstbad = -1;
            for (int32 i = 0; i < (int32)(sizeof(p6LayoutProbes) / sizeof(p6LayoutProbes[0])); ++i) {
                int32 ly = p6LayoutProbes[i].layer;
                int32 slot;
                if (ly == 4)
                    slot = 1;
                else {
                    if (bound0 != ly) {
                        SaturnLayout_Bind(0, ly);
                        bound0 = ly;
                    }
                    slot = 0;
                }
                uint16 got = SaturnLayout_GetTile(slot, p6LayoutProbes[i].x,
                                                  p6LayoutProbes[i].y);
                if (got == p6LayoutProbes[i].expect)
                    ++good;
                else if (p6_w_lay_firstbad < 0)
                    p6_w_lay_firstbad = i;
            }
            p6_w_lay_probes = good;
        }
    }

    // 7c-moved) P6.7c ORDERING FIX (measured: gate T5 RED at the old
    //    pre-LoadGameConfig spot): CD-DA playback and GFS data reads share
    //    the CD block -- the 50+ pack reads of the scene chain DISPLACE an
    //    already-armed CD-DA play. PlayStream therefore runs LAST, after
    //    every pack read -- which is ALSO the canonical order (decomp
    //    Music_PlayTrack fires from the scene's own logic AFTER the stage
    //    load completes, TitleSetup.c:145).
    // 7c) P6.6c: ENGINE MUSIC -- the canonical PlayStream call shape (decomp
    //     Music_PlayTrack -> RSDK.PlayStream(track->fileName, 0, 0, loop,
    //     true); the Title scene requests TitleScreen.ogg via TRACK_STAGE,
    //     TitleSetup.c:145). The UNMODIFIED engine arms channel 0 + builds
    //     "Data/Music/TitleScreen.ogg" (Audio.cpp:276-293), then the Saturn
    //     HandleStreamLoad maps it to CUE track 3 and starts REAL CD-DA.
    {
        // NO-CTORS TRAP: RetroEngine's dynamic initializer never runs under
        // SLSTART -- set the field PlayStream:250 reads explicitly.
        engine.streamsEnabled = true;

        int32 strSlot = PlayStream("TitleScreen.ogg", 0, 0, 1, true);
        p6_w_str_slot = strSlot;
        if (strSlot >= 0 && strSlot < CHANNEL_COUNT)
            p6_w_str_state = (int32)channels[strSlot].state;

        uint32 ph = 5381u;
        for (const char *p = streamFilePath; *p; ++p)
            ph = ((ph << 5) + ph) ^ (uint32)(uint8)*p;
        p6_w_str_path = (int32)ph;
    }

    // 4) Witness copy (WRAM-H .bss survives the later title boot's WRAM-L use).
    {
        EntityBase *e;
        e = RSDK_ENTITY_AT(5 + RESERVE_ENTITY_COUNT);
        p6_w_scene_emblem_x = e->position.x;
        p6_w_scene_emblem_y = e->position.y;
        e = RSDK_ENTITY_AT(8 + RESERVE_ENTITY_COUNT);
        p6_w_scene_sonic_x = e->position.x;
        p6_w_scene_sonic_y = e->position.y;
        e = RSDK_ENTITY_AT(16 + RESERVE_ENTITY_COUNT);
        p6_w_scene_t3d_x = e->position.x;
        p6_w_scene_t3d_y = e->position.y;
    }
    p6_w_scene_loaded = 1;
    p6_w_scene_step   = 4;
    p6_load_phase_exit();
}

// =============================================================================
// P6.8 Step F (Task #211): p6_w_transitions -- count of engine-driven scene
// transitions handled by the tick's ENGINESTATE_LOAD dispatch (read by
// qa_p6_transition). A scene-load triggered by an object (Zone sets
// sceneInfo.state = ENGINESTATE_LOAD via RSDK.LoadScene) bumps this.
__attribute__((used)) int32 p6_w_transitions = 0;
#if defined(P6_FRONTEND_CHAIN)
// CP5c (Task #270): the FRONT-END FLOW chain witnesses. p6_w_chain_fired latches 1
// the single time the Logos->Title advance fires in p6_frontend_frame (the engine's
// ENGINESTATE_LOAD from LogoSetup auto-advance, dispatched to p6_title_reload instead
// of swallowed). p6_w_chain_folder_pre records the active folder tag at the moment of
// the fire (should be 0x4C6F 'Lo' -- Logos -- proving the advance fired FROM Logos, not
// some later scene). Guarded under P6_FRONTEND_CHAIN (NOT unconditional) so the default
// GHZ _end stays exactly 0x060B6BA0 (the #228 ANIMPAK budget) -- mirrors the CP5a Title
// witnesses' flag-guard, NOT p6_w_transitions' unconditional decl. Read by qa_title_chain.
__attribute__((used)) int32 p6_w_chain_fired      = 0;
// FULL-CHAIN (task #314): the Title -> Menu seam fired (the second chain hop).
__attribute__((used)) int32 p6_w_chain2_fired     = 0;
__attribute__((used)) int32 p6_w_chain3_fired     = 0; // #314 Menu->AIZ dwell-nudge fired
__attribute__((used)) int32 p6_w_chain_folder_pre = 0;
// CP5c: the MEASURED scene-list order (risk #1 report). At the Logos->Title fire,
// p6_w_chain_listpos_adv = sceneInfo.listPos AFTER LogoSetup's ++ (= the engine's
// adjacency destination, i.e. Logos listPos + 1); p6_w_chain_listpos_title =
// sceneInfo.listPos AFTER p6_title_reload's by-NAME scan resolved "Title". If these
// DIFFER, the explicit-select is doing real work (Title is NOT simply Logos+1); if
// they match, Title happens to be adjacent -- either way the by-name select is correct
// and order-independent. Latched once at the fire. (Logos itself is category0/scene0
// per LogoSetup.c:53, so its listPos is 0 and the adjacency destination is 1.)
__attribute__((used)) int32 p6_w_chain_listpos_adv   = -1;
__attribute__((used)) int32 p6_w_chain_listpos_title = -1;
#endif
// P6.8 Step F.2 diag: post-load runtime GetTile probes at the loaded scene's
// spawn column (x tile 16). For GHZ2 the offline FG Low has solid ground at
// ty=90 (0x2001) and FG High a platform at ty=86 (0x7413); if the windowed
// store serves the new zone these read those values, if not they read 0xFFFF
// (empty) -- the discriminator for "player falls through GHZ2".
__attribute__((used)) int32 p6_w_xtile_lo = -1; // GetTile(slot0 FG Low, 16, 90)
__attribute__((used)) int32 p6_w_xtile_hi = -1; // GetTile(slot1 FG High,16, 86)
// #237: STICKY latch of the GHZ2 LOAD state (set ONCE at the first GHZ2 load).
// The non-sticky probes above get overwritten when the player drop+autoruns off
// GHZ2 and the engine loads the next scene; these survive for a clean read of
// "did GHZ2 load with solid collision + entities + the right spawn?".
__attribute__((used)) int32 p6_w_ghz2_loaded   = 0;  // 1 == a GHZ2 load was latched
__attribute__((used)) int32 p6_w_ghz2_xtile_lo = -1; // GHZ2 FG Low @(16,90): 0x2001 solid / 0xFFFF empty
__attribute__((used)) int32 p6_w_ghz2_xtile_hi = -1; // GHZ2 FG High @(16,86): 0x7413 / 0xFFFF empty
__attribute__((used)) int32 p6_w_ghz2_entcount = -1; // GHZ2 scene entities with classID after InitObjects
__attribute__((used)) int32 p6_w_ghz2_plrx     = 0;  // SLOT_PLAYER1 spawn x at GHZ2 load (fixed)
__attribute__((used)) int32 p6_w_ghz2_plry     = 0;  // SLOT_PLAYER1 spawn y at GHZ2 load (fixed)
// #237 GHZ2 continuous-PLAY probe (per-frame while GHZ2 is the loaded scene):
// does the player run on solid ground (floor tile under its feet) or into empty
// (-> falls -> dies -> game over -> Menu, the observed transition-away)?
__attribute__((used)) int32 p6_w_ghz2_listpos    = -1; // listPos of the loaded GHZ2 scene
__attribute__((used)) int32 p6_w_ghz2_play_frames = 0; // frames GHZ2 was the live scene
__attribute__((used)) int32 p6_w_ghz2_max_plry   = 0;  // MAX player y px during GHZ2 (fall depth; spawn=1358)
__attribute__((used)) int32 p6_w_ghz2_exit_lp    = -1; // listPos GHZ2 advanced TO (set once on exit)
__attribute__((used)) int32 p6_w_ghz2_collplane  = -1; // EntityBase.collisionPlane (0=A,1=B)
__attribute__((used)) int32 p6_w_ghz2_colllayers = -1; // EntityBase.collisionLayers bitmask
__attribute__((used)) int32 p6_w_ghz2_tilecoll   = -1; // EntityBase.tileCollisions (0=OFF,1=DOWN,2=UP)
// #237 DECISIVE FindFloorPosition replica (see p6_ghz_frame). floorlayer is the
// verdict: -2 replica never ran (collision OFF every tick -> see tcoff), -1 ran
// but NO floor under the feet across the whole descent (== fall-through PROVEN),
// >=0 the layer index whose tile had the floor-solid bit (collision SHOULD have
// caught him -> bug is downstream). The reader derives the solid mask from
// collplane+tilecoll; fghigh_idx is checked against colllayers.
__attribute__((used)) int32 p6_w_ghz2_floorlayer = -2; // -2 none-run / -1 no-floor / >=0 layer
__attribute__((used)) int32 p6_w_ghz2_fghigh_idx = -1; // tileLayers index w/ saturnSlot 1 (FG-High)
__attribute__((used)) int32 p6_w_ghz2_fglow_tile = -1; // raw FG-Low tile at spawn-band feet
__attribute__((used)) int32 p6_w_ghz2_fghigh_tile= -1; // raw FG-High tile at spawn-band feet
__attribute__((used)) int32 p6_w_ghz2_feetty     = -1; // feet tile row at the snapshot
__attribute__((used)) int32 p6_w_ghz2_vely       = 0;  // player velocity.y>>8 at snapshot
__attribute__((used)) int32 p6_w_ghz2_tcoff      = 0;  // ticks tileCollisions was OFF (==falls)
__attribute__((used)) int32 p6_w_ghz2_slot1layer = -9; // layer slot1 bound to (5=FG-High ok, 4=FG-Low bug)

// P6.8 Step F.2 (Task #231): swap the windowed layout BAND STORE for the scene
// currently selected by sceneInfo.listPos. The Saturn windowed layout (collision
// + FG present) is per-zone; on a cross-zone transition the store at
// P6_LW_LAYOUTBANDS must be replaced with the new scene's <folder><id>LAYT.BIN
// and re-mounted. MUST run BEFORE LoadSceneFolder -- that is where Scene.cpp
// (:439/444) calls SaturnLayout_Bind, and SaturnLayout_Mount re-inits (unbinds)
// the slots, so the order is Mount-then-Bind (the same order p6_scene_run uses:
// the boot mount at :1790 precedes LoadSceneFolder at :2149). Skipped when the
// scene's tag already matches the mounted store (the GHZ1 boot mount + a GHZ1
// self-reload pay no extra CD read). GHZ1/GHZ2 share the GHZ tileset +
// TileConfig, so ONLY the band store + Scene.bin entities change. GHZ2LAYT.BIN
// = 41,781 B fits the 0xC800 (51,200 B) window built for GHZ1's 51,094 B.
static char s_layout_tag[12] = "GHZ1"; // tag of the mounted store (boot mounts GHZ1)
extern "C" int32 SaturnLayout_Mount(const void *blob); // file-scope (matches the C symbol)
extern "C" uint16 SaturnLayout_GetTile(int32 slot, int32 tx, int32 ty); // F.2 diag probe
extern "C" void SaturnLayout_PreInflateResident(void); // #249/#250 deferred resident pre-inflate
extern "C" void SaturnLayout_SetScratch(void **bufp, uint32 cap); // M3.2: arm the AIZ FG band-inflate scratch

static void p6_layout_mount_for_scene(void)
{
    const char *folder = sceneInfo.listData[sceneInfo.listPos].folder;
    char id            = sceneInfo.listData[sceneInfo.listPos].id[0];
    char tag[12];
    int32 n = 0;
#if defined(P6_FRONTEND_MENU)
    // #309: the LAYT band-store filename is "<tag>LAYT.BIN" and SGL GFS 8.3 caps
    // the name at 8 chars, so the tag must be <= 4. folder[:7]+id gives "GHZ"->
    // "GHZ1" (OK) but the cutscene folder "GHZCutscene"->"GHZCuts1" -> a 16-char
    // "GHZCuts1LAYT.BIN" that GFS rejects. Alias the long cutscene folders to a
    // <=3-char prefix distinct from "GHZ" (built offline as GHC1LAYT.BIN via
    // build_layout_bands.py --tag GHC1 --no-model). Front-end-only branch; the
    // GHZ shipping build takes the #else and is byte-identical.
    const char *prefix = folder;
    if (!strcmp(folder, "GHZCutscene"))
        prefix = "GHC"; // GHC1LAYT.BIN (Scene1) / GHC2LAYT.BIN (Scene2)
    for (const char *p = prefix; *p && n < 7; ++p)
        tag[n++] = *p;
#else
    for (const char *p = folder; *p && n < 7; ++p)
        tag[n++] = *p;
#endif
    tag[n++] = id;
    tag[n]   = 0;
    // F.4 diag: capture what this mount call resolved (tag + listPos) so the gate
    // can see whether ActClear's listPos=8 landed on GHZ2 or fell back to GHZ1.
    p6_w_mount_tag = (int32)((uint32)(uint8)tag[0] | ((uint32)(uint8)tag[1] << 8)
                   | ((uint32)(uint8)tag[2] << 16) | ((uint32)(uint8)tag[3] << 24));
    p6_w_mount_listpos = (int32)sceneInfo.listPos;
    if (!strcmp(tag, s_layout_tag))
        return; // already mounted -- no CD read

    // filename = "<tag>LAYT.BIN" (<= 12 chars: SGL GFS_FNAME_LEN limit; e.g.
    // GHZ2LAYT.BIN = 12). [[sgl-gfs-fname-len-12-limit]]
    char fname[16];
    int32 m = 0;
    for (int32 i = 0; i < n; ++i)
        fname[m++] = tag[i];
    const char *suf = "LAYT.BIN";
    for (const char *p = suf; *p; ++p)
        fname[m++] = *p;
    fname[m] = 0;

    int32 b = rsdk_storage_load_to_lwram(fname, (void *)P6_LW_LAYOUTBANDS, 0xC800);
    if (b > 0 && SaturnLayout_Mount((const void *)P6_LW_LAYOUTBANDS) > 0) {
        p6_w_lay_bytes = b;
        for (int32 i = 0; i <= n; ++i)
            s_layout_tag[i] = tag[i];
    }
}

// P6.8 Step F (Task #211): p6_scene_load_and_arm -- load the scene CURRENTLY
// selected by sceneInfo.activeCategory/listPos and re-arm the Saturn render
// environment. Factored out of p6_ghz_reload so the SAME sequence serves both
// (a) the boot GHZ1 select+load and (b) an engine-driven transition where an
// object already set listPos (Zone act advance / death reload). Mirrors the
// engine ENGINESTATE_LOAD case (RetroEngine.cpp:365-367): LoadSceneFolder ->
// LoadSceneAssets -> InitObjects, with the Saturn-side VDP2 cell upload wedged
// between the GIF decode and the LoadSceneAssets memset (the W11b transient),
// the screen env armed before InitObjects (Camera_Create reads center), and the
// VDP1 sheet binds re-armed after. InitObjects sets sceneInfo.state =
// ENGINESTATE_REGULAR (Object.cpp:385) so the next tick runs the new scene.
// NOTE (F.1 scope): re-uses the layout bands already mounted at p6_scene_run
// (SaturnLayout_Mount) -- valid for a same-zone reload (GHZ1->GHZ1). A cross-
// scene transition with a different layout (GHZ1->GHZ2) additionally needs the
// new band store staged + re-mounted; that is F.2.
#if defined(P6_GHZCUT_BOOT)
// Task #309 #2b: the FG present backdrop color (defined in p6_vdp2.c). The
// GHZCut load path (below) drives it to the Mania sky-blue for the cutscene and
// back to 0x8000 (black) for the playable-GHZ handoff. C linkage (p6_vdp2.c is C).
extern "C" unsigned short p6_present_backcol;
// Task #309 #2b REAL FIX: render the GHZ "BG Outside" sky (scene layer 0) as a VDP2
// 4-bpp NBG0 behind the transparent FG. Asset tools/build_ghzcut_bg.py -> GHCBG.*.
extern "C" void p6_vdp2_ghzcut_bg_upload(const unsigned short *chr_cart, int chr_words,
                                         const unsigned short *pal_cart, int pal_words,
                                         const unsigned short *map_cart, int map_words);
extern "C" void p6_vdp2_ghzcut_bg_frame(int sx);
// #325 stage-1: master-side sky CRAM[64..127] re-assert, called after the slave
// FG-present join (the slave's CRAM bank0 write runs AFTER bg_frame's re-assert
// under the offload -- this restores the proven sync frame-end CRAM order).
extern "C" void p6_vdp2_ghzcut_bg_pal_reassert(void);
#endif
#if defined(P6_FRONTEND_MENU)
// #319 GHZ death->respawn (camera-local pool RELOAD, task #263). On a SAME-FOLDER GHZ
// reload the pool's remap/inv is left NON-IDENTITY from the first compact, so InitObjects
// (which runs BEFORE the compact and places entities via RSDK_ENTITY_AT->remap) mis-places
// the scene entities + the Player marker -> Player_LoadSprites finds nothing -> SLOT_PLAYER1
// stays TYPE_BLANK. FIX = reset the pool to the pre-first-load state (identity remap +
// scene_phys=SCENEENTITY_COUNT + dummy=-1) BEFORE InitObjects, and re-arm the compact one-
// shot, so the reload runs IDENTICALLY to the proven-safe first load (InitObjects creates all
// ~1041 entities -- fits the 445,440B dual-stride backing at NARROW=344 -- then the compact
// re-classifies/re-shrinks + rebuilds ALL streaming state from scratch: remap/inv/free-list/
// resident-list/lifecycle/scene_phys). MEASURED-safe: the first load does exactly this.
// p6_pool_remap_init (:7878) + p6_eng_pool_flip (:7857) are defined later -> forward-declare.
// CHAIN-gated (#if P6_FRONTEND_MENU) -> plain GHZ Scene_*.o byte-identical.
extern "C" void p6_pool_remap_init(void);
extern "C" void p6_eng_pool_flip(int32 sphys, int32 dummy);
static int32 s_ghz_compact_rearm = 0; // set by the reload reset; re-arms the compact one-shot
#endif
static void p6_scene_load_and_arm(void)
{
#if defined(P6_FRONTEND_MENU)
    // R2 regression fix (2026-07-17, gate qa_starpost_fxfade_gates.py G1): the
    // whole load below (ClearStageObjects -> STG defrag -> AllocateStorage ->
    // StageLoads) runs INSIDE this call while the pack `StarPost` copy still
    // holds LAST frame's rewire target -- a dangling pool address the load-time
    // pack writers (SaveGame_LoadSaveData's fresh-act StarPost reset,
    // SaveGame.c:151-163; SaveGame = GameConfig global #2 < StarPost #18)
    // scribbled through at every folder-change seam (Menu/AIZ/GHZCutscene/GHZ).
    // Detach to the safe zeroed instance for the load; the per-frame rewire
    // re-attaches to the fresh zero-allocated statics next frame. Chain-only
    // (plain GHZ never changes folder; its .o stays exactly as the batch built).
    p6_starpost_detach();
#endif
    // #250: MEASURED root cause of the garbled FG after a DEATH reload. A same-
    // FOLDER reload makes LoadSceneFolder early-return (Scene.cpp:71-91) WITHOUT
    // re-decoding the tileset (LoadStageGIF never runs), so tilesetPixels is stale
    // (the entityList window, just defrag-GC'd) -- uploading it as tile cells
    // garbles NBG1. The VDP2 cells from the first load persist, so the upload must
    // be SKIPPED on a same-folder reload. Mirror LoadSceneFolder's own condition
    // (captured BEFORE the call, since the full path strcpy's currentSceneFolder).
    // GHZ1<->GHZ2 share folder "GHZ", so both correctly keep the resident cells;
    // only a real folder change (or forceHardReset) re-decodes + needs the upload.
    int32 p6_folderReload =
        (strcmp(currentSceneFolder, sceneInfo.listData[sceneInfo.listPos].folder) == 0
         && !forceHardReset);
    // CP4 (Task #265): the GHZ-tilemap-specific arm steps (cell upload, the
    // 0x060E0000 collision-pack hash, the camera-local pool scan-index/compact,
    // the resident layout pre-inflate, the stage BGM) only apply to a gameplay
    // tilemap scene. A non-GHZ UI scene (the Logos splash) is UIPicture sprites
    // over a fade with NO gameplay FG layer + NO <TAG>LAYT.BIN band store, so
    // those steps are skipped for it (the band mount below already no-ops safely
    // when the LAYT.BIN is absent). Guard on folder == "GHZ" -- the only tilemap
    // gameplay folder this build loads today. p6_scene_load_and_arm thus serves
    // BOTH the GHZ load+arm and a generic UI scene load.
    // COMPILE-TIME CONSTANT in the DEFAULT (GHZ) build (always 1 -> the runtime
    // strcmp + the !p6_isGHZ branches fold away to ZERO code) so the shipping GHZ
    // image stays byte-identical (the #228 _end<ANIMPAK budget is only ~48 B; a
    // live strcmp + census would breach it). Only the front-end flavor pays the
    // runtime test + the UI-scene skips.
#if defined(P6_FRONTEND_LOGOS)
    int32 p6_isGHZ =
        (strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "GHZ") == 0);
#else
    const int32 p6_isGHZ = 1; // DEFAULT build only ever loads GHZ here
#endif
#if defined(P6_FRONTEND_MENU)
    // #319 GHZ death->respawn (task #263 pool RELOAD): on a SAME-FOLDER GHZ reload, reset the
    // camera-local pool to the pre-first-load state BEFORE InitObjects (:5992) so the reload
    // replicates the proven-safe first load (see the forward-decl comment above). p6_folderReload
    // ==1 == same folder (a death reload OR a GHZ1->GHZ2 act advance); the FIRST GHZ load
    // (GHZCutscene->GHZ folder change) has p6_folderReload==0 so it is UNTOUCHED (byte-identical
    // to today). p6_pool_remap_init resets remap/inv to identity so InitObjects' RSDK_ENTITY_AT
    // placement is correct; p6_eng_pool_flip restores scene_phys=SCENEENTITY_COUNT + dummy=-1
    // (the boot defaults); s_ghz_compact_rearm re-arms the compact one-shot (consumed at :6104)
    // so the compact re-runs after InitObjects and rebuilds ALL streaming state from scratch.
    if (p6_folderReload && p6_isGHZ) {
        p6_pool_remap_init();                    // remap[i]=inv[i]=i (identity), remap_ready=1
        p6_eng_pool_flip(SCENEENTITY_COUNT, -1); // scene_phys=SCENEENTITY_COUNT, dummy=-1 (defaults)
        s_ghz_compact_rearm = 1;                 // re-arm the compact one-shot (consumed below)
    }
#endif
    // F.2: (re)mount the windowed band store for the zone being loaded BEFORE
    // LoadSceneFolder's SaturnLayout_Bind (Scene.cpp:439/444). No-op on a same-
    // zone reload (tag match); swaps GHZ1<->GHZ2 on a cross-zone act advance.
    // Task #271: re-baseline the load-timing cursors for PHASE-2 (UNMASKED -- the
    // vblank ISR is now registered + running, so S8..S10 get EXACT vblank ms AND
    // the ground-truth ms/fill that converts the masked-core I/O fills to ms).
    P6_LT_BEGIN();
    p6_w_load_step = 30; // #251 phase-2: layout mount-for-scene
    p6_layout_mount_for_scene();
    p6_w_load_step = 31; // #251 phase-2: LoadSceneFolder (tileset GIF + TileConfig inflate)
    LoadSceneFolder();
    P6_LT_MARK(8); // Task #271 S8: layout mount + LoadSceneFolder (Title scene + TileConfig)
    // #249/#250: pin the packed-collision geometry RIGHT AFTER the gameplay
    // LoadTileConfig (nested in LoadSceneFolder, Scene.cpp:163-164). The lean boot
    // skips the diag step-9 hash, so this is the ONLY witness of 0x060E0000 in the
    // shipping pack path. t1hash != golden 0x643A3A5D => the resident pre-inflate
    // (p6_layout_mount_for_scene -> SaturnLayout_Mount, run BEFORE this) corrupted
    // the pack; == golden => packed RIGHT, any later corruption is a loop writer.
    {
        uint32 h = 5381u;
        const uint8 *pb = (const uint8 *)P6_HW_PACKEDCOL;
        for (int32 i = 0; i < 0x10000; ++i)
            h = ((h << 5) + h) ^ pb[i];
        p6_w_col_t1hash = (int32)h;
    }
    // Stage the GHZ tile CELLS to NBG1 NOW (tilesetPixels is the W11b
    // load-phase transient aliasing the entityList window -- valid only
    // between LoadSceneFolder's GIF decode and LoadSceneAssets' memset).
    // #250: SKIP on a same-folder reload -- LoadSceneFolder early-returned without
    // re-decoding the tileset, so tilesetPixels is stale and the first load's cells
    // are still resident in NBG1 VRAM. Uploading here would garble the FG.
    p6_w_load_step = 32; // #251 phase-2: VDP2 cell upload
    // CP4: skip for a non-GHZ UI scene -- the Logos placeholder tileset is a
    // 1731 B no-content GIF, and the UI scene draws via UIPicture VDP1 sprites,
    // not NBG1 cells. (The GHZ same-folder reload skip is unchanged.)
    if (!p6_folderReload && p6_isGHZ)
        p6_vdp2_upload_cells((const unsigned char *)tilesetPixels);
#if defined(P6_FRONTEND_TITLE)
    // CP5b.3 (Task #272): the TITLE scene's 16x16Tiles (the green island + cloud
    // tiles) -> NBG1 cells NOW, while tilesetPixels is the live W11b transient
    // (post-LoadSceneFolder GIF decode, pre-LoadSceneAssets memset). The map +
    // CRAM + display half runs AFTER arm_env (palette final). Unlike the Logos
    // placeholder tileset (a 1731 B no-content GIF), the Title tileset has the
    // real island/cloud content, so the GHZ-only guard above must NOT gate it.
    {
        int32 p6_isTitle =
            (strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "Title") == 0);
        if (p6_isTitle && !p6_folderReload) {
            p6_vdp2_upload_cells((const unsigned char *)tilesetPixels);
            // SUB2 (#276 clouds-coexist): build the clouds-only NBG1 on bank B1 NOW,
            // while tilesetPixels is the live W11b transient (the B1 cell copy needs
            // the decoded tileset; A0 upload above does NOT cover B1). tileLayers[2]
            // = Clouds (16x16). NBG1 on B1 coexists with the RBG0 island (A0/A1/B0)
            // per ST-058:1030 (normal + 1 rotation simultaneously).
            if (tileLayers[2].layout)
                p6_vdp2_title_clouds_b1_arm(
                    (const unsigned char *)tilesetPixels,
                    (const unsigned short *)tileLayers[2].layout,
                    (int)tileLayers[2].widthShift);
        }
    }
#endif
#if defined(P6_AIZ_TEST)
    // M3.0: the AIZ FG cells -> NBG1 NOW, while tilesetPixels is the live W11b transient
    // (post-LoadSceneFolder GIF decode, pre-LoadSceneAssets memset) -- the SAME window the
    // GHZ (p6_isGHZ above) and Title (p6_isTitle above) uploads use. AIZ is a gameplay
    // tilemap, so unlike the Logos 1731 B placeholder it has real FG content -> the
    // GHZ-only guard above must NOT gate it. A2 latches a djb2 of the uploaded cells
    // (!=0 proves the AIZ tileset decoded with content -> the FG renders, separate from
    // the per-frame present G8 -- M3.0b).
    {
        int32 p6_isAIZ =
            (strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "AIZ") == 0);
        if (p6_isAIZ && !p6_folderReload) {
            p6_vdp2_upload_cells((const unsigned char *)tilesetPixels);
            // R1 (AIZ render): AIZ tile-0 is OPAQUE orange -> the GHZ "empty cell -> tile-0
            // (transparent)" present assumption fills the screen with orange. Clear an UNUSED
            // tile index (64, MEASURED used by no AIZ layer -> streaming-safe) to transparent
            // and route empty FG cells (0xFFFF) there so the FG is see-through over the BG.
            p6_vdp2_aiz_blank_setup(64);
            uint32 h = 5381u;
            const uint8 *pb = (const uint8 *)tilesetPixels;
            for (int32 i = 0; i < 0x4000; ++i) // 16 KB sample of the decoded tileset
                h = ((h << 5) + h) ^ pb[i];
            p6_w_aiz_fg_hash = (int32)h;
        }
    }
#endif
#if defined(P6_GHZCUT_BOOT)
    // Task #309: the GHZCutscene FG cells -> NBG1 NOW, while tilesetPixels is the live
    // W11b transient (post-LoadStageGIF pre-decoded-tileset memcpy, pre-LoadSceneAssets
    // memset). GHZCutscene's folder ("GHZCutscene") matches NONE of the p6_isGHZ/isTitle/
    // isAIZ guards above, so it needs its own upload. GHZCutscene is a GHZ-THEMED scene:
    // its tile-0 is the GHZ transparent convention (NOT the AIZ opaque-orange tile-0), so
    // it takes the plain GHZ-style upload + the present's "empty cell (0xFFFF) -> tile-0
    // (transparent)" assumption -- NO p6_vdp2_aiz_blank_setup. Reuse the AIZ fg_hash
    // witness (gc-rooted) so the capture proves the tileset decoded with content.
    {
        int32 p6_isGHZCut =
            (strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "GHZCutscene") == 0);
        // Task #309 #2b: the FG present (p6_present_config) hardwired the VDP2
        // backdrop to black (0x8000), so the revealed GHZCutscene showed Sonic/
        // Tails warping in against a BLACK void ("corpse in the ground"). The GHZ
        // sky is a flat clear color in RSDK (BG Outside's sky region is transparent
        // index-0 -- MEASURED from Scene1.bin/StageConfig.bin), so drive the
        // present's backdrop to the Mania sky-blue for the cutscene, and reset it
        // to 0x8000 (black) for the playable-GHZ handoff so GHZ is unchanged. Set
        // unconditionally (not under the !p6_folderReload guard) so a same-folder
        // reload keeps the right color. p6_present_backcol defaults 0x8000; the GHZ
        // shipping build never compiles P6_GHZCUT_BOOT so it stays byte-identical.
        // -DP6_GHZCUT_NOFIX skips it so the RED-proof build keeps the black void.
#ifndef P6_GHZCUT_NOFIX
        p6_present_backcol = p6_isGHZCut ? 0xF180u /* Mania sky-blue */ : 0x8000u;
#endif
        if (p6_isGHZCut && !p6_folderReload) {
            // Task #309 gate-2 FOLLOW-UP: enforce the documented "empty cell -> tile-0
            // transparent" assumption for GHZCutscene by resetting the persistent
            // p6_fg_blank_char_override to -1 here (the load-time twin of the live-seam
            // reset above). On the LIVE path the AIZ scene left it at 64; on the
            // direct-boot it is already -1 (no-op). Without this the present maps
            // GHZCutscene empty FG cells to opaque GHZ-tile-64 -> the checkerboard bleed.
            // (-DP6_GHZCUT_NOFIX disables BOTH this and the live-seam reset for the
            // RED-proof build.)
#ifndef P6_GHZCUT_NOFIX
            p6_fg_blank_char_override = -1;
#endif
            p6_vdp2_upload_cells((const unsigned char *)tilesetPixels);
            uint32 h = 5381u;
            const uint8 *pb = (const uint8 *)tilesetPixels;
            for (int32 i = 0; i < 0x4000; ++i) // 16 KB sample of the decoded tileset
                h = ((h << 5) + h) ^ pb[i];
            p6_w_aiz_fg_hash = (int32)h;
        }
#ifndef P6_GHZCUT_NOFIX
        // Task #309 #2b REAL FIX: load the GHZ "BG Outside" sky (built by
        // tools/build_ghzcut_bg.py) and render it as a VDP2 4-bpp NBG0 behind the
        // transparent FG. MEASURED (render_scene.py + _bgo_probe2.py): GHZCutscene
        // scene layer 0 = 512x24, 100%-populated sky+clouds+hills+water, palette
        // stage-bank 1. This REPLACES the abandoned char-0 fill (which made the FG's
        // empty sky cells OPAQUE and would BLOCK this BG plane). Staged into the AIZ
        // BG scratch window (free in the GHZCut path -- p6_ghzcut_reload loads no AIZ
        // BG). MUST run after p6_vdp2_upload_cells (FG char base settled). The per-
        // frame NBG0 arm is p6_vdp2_ghzcut_bg_frame in p6_frontend_frame.
        // NOTE: the AGHCBG.* BG-Outside load is NOT done here. MEASURED (witnesses
        // p6_w_ghcbg_loaded=0 across 2 builds): rsdk_storage_load_to_lwram returns 0
        // inside p6_scene_load_and_arm because the DATA.RSDK pack + the in-flight
        // scene read already hold both P6_GFS_OPEN_MAX=2 GFS handles, so a loose
        // GFS_Open fails. It is loaded ONE-SHOT from the per-frame GHZCutscene path
        // (p6_frontend_frame) where GFS is idle -- the same place/timing HBHOBJ.SHT
        // (p6_ghz_arm_env) loads successfully.
        (void)p6_isGHZCut;
#endif
    }
#endif
    p6_w_load_step = 33; // #251 phase-2: LoadSceneAssets (sprite sheets)
    LoadSceneAssets();
    P6_LT_MARK(9); // Task #271 S9: LoadSceneAssets (sprite-sheet GIF decode/stage for the scene)
    // Screen env must exist BEFORE InitObjects (Camera_Create reads
    // ScreenInfo->center) -- arm it here, then arm the binds after.
    videoSettings.screenCount = 1;
    screens[0].size.x         = SCREEN_XMAX;
    screens[0].size.y         = SCREEN_YSIZE;
    screens[0].center.x       = SCREEN_XMAX / 2;
    screens[0].center.y       = SCREEN_YSIZE / 2;
    screens[0].pitch          = SCREEN_XMAX;
    screens[0].clipBound_X1   = 0;
    screens[0].clipBound_Y1   = 0;
    screens[0].clipBound_X2   = SCREEN_XMAX;
    screens[0].clipBound_Y2   = SCREEN_YSIZE;
    currentScreen             = &screens[0];
    sceneInfo.entity          = RSDK_ENTITY_AT(0);
    // #P0 (GHZ1 parity): establish the fresh-act carry-over state (Player->rings=0,
    // powerups=0, ringExtraLife=100) BEFORE InitObjects runs Player_Create -- the lean
    // engine boot skips the menu/new-game path that zeroes these statics, so the first
    // GHZ player otherwise inherits uninitialized memory (100 rings + fire shield). The
    // game-side reset (p6_wave1_reg.c) also witnesses the pre-reset garbage.
    p6_player_newgame_reset();
#if defined(P6_FRONTEND_MENU)
    // Task #298 (M2): reset the wide-scene sub-pool maps to all-narrow BEFORE InitObjects
    // populates them (ResetEntitySlot routes oversize scene placements during entity
    // Create). Without this, stale mappings from a prior scene load leak into this one.
    // (extern declared at file scope above -- a block-scope linkage spec is ill-formed C++.)
    p6_widescene_reset();
#endif
    p6_w_load_step = 34; // #251 phase-2: InitObjects (entity Create/StageLoad)
#if defined(P6_AIZ_TEST)
    // M3.2: latch the slot-5/6/7 classID trio BEFORE InitObjects (post-LoadScene+compaction
    // state). slot 6 == the AIZTornadoPath START node, slot 5 == a scene Player marker,
    // slot 7 == AIZTornadoPath SETSPEED. cls==33 expected for an AIZTornadoPath node.
    if (strcmp(currentSceneFolder, "AIZ") == 0)
        p6_w_aiz_trio_load =
            ((int32)RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + 5)->classID << 16)
            | ((int32)RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + 6)->classID << 8)
            | (int32)RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + 7)->classID;
#endif
    // #P0 (GHZ1 parity) MOVED PRE-InitObjects (#312 timeEnabled fix): the lean
    // boot skips Zone_State_FadeIn (Zone.c:820), which is what enables the act
    // clock on PC -- so force it here as the act-load default. Setting it
    // BEFORE InitObjects restores the PC ORDERING: a cutscene's Create can then
    // DISABLE it and win (GHZCutsceneST_Create sets timeEnabled=false,
    // GHZCutsceneST.c:55 -- MEASURED clobbered by the old post-InitObjects
    // placement: the cutscene HUD TIME counted to 0'05"91).
    sceneInfo.timeEnabled = true;
    InitObjects();
#if defined(P6_AIZ_TEST)
    // M3.2: same trio AFTER InitObjects. If slot 6 went 33->0 here, the Saturn Player-marker
    // consumption (Player_LoadSprites / Player StageLoad) blanked the START node adjacent to
    // the slot-5 Player marker. player_cls == Player1's classID for the purge comparison.
    if (strcmp(currentSceneFolder, "AIZ") == 0) {
        p6_w_aiz_trio_init =
            ((int32)RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + 5)->classID << 16)
            | ((int32)RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + 6)->classID << 8)
            | (int32)RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + 7)->classID;
        p6_w_aiz_player_cls = (int32)RSDK_ENTITY_AT(0)->classID;
    }
#endif
    // #256 FIX: real Mania's Player_LoadSprites (Player.c:779 foreach_all(Player))
    // consumes EVERY scene-placed Player marker -- CopyEntity(clearSrc=true) the
    // chosen-character one into SLOT_PLAYER1, destroyEntity the rest; both zero
    // classID. On Saturn GHZ1's 2nd marker (scene slotID 888 -> runtime slot 952 via
    // Scene.cpp:646 slotID+RESERVE) survives as a STRAY SIDEKICK (slot!=0 ->
    // Player_Create sets sidekick + stateInput=Player_Input_P2_AI). It then double-
    // runs Player_Input_P2_Delay every frame, clobbering the SHARED Player->
    // leaderPositionBuffer + input shift-registers -> the real Tails (SLOT_PLAYER2)
    // can't settle and falls through bridges (#181). Players belong ONLY at the
    // reserve SLOT_PLAYER1/2; complete the consumption by destroying any Player-
    // classID entity at a scene/temp slot. Runs once here (post-InitObjects, before
    // the first tick) so the phantom never executes an Update -> shared buffers stay
    // clean. ResetEntity(.,0,.) == destroyEntity (memset + classID=TYPE_BLANK=0).
    p6_purge_scene_players();
#if defined(P6_AIZ_TEST)
    // M3.2: same trio AFTER the purge. If slot 6 went 33->0 here (while player_cls != 33),
    // p6_purge_scene_players blanked a non-Player slot -> a purge bug clobbering the START node.
    if (strcmp(currentSceneFolder, "AIZ") == 0)
        p6_w_aiz_trio_purge =
            ((int32)RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + 5)->classID << 16)
            | ((int32)RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + 6)->classID << 8)
            | (int32)RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + 7)->classID;
#endif
    // CP4 (Task #266, qa_engine_logos E4): census the live-classID scene entities
    // the generic InitObjects chain just instantiated for the LOGOS scene (the 4
    // UIPicture placements). LATCH on the Logos folder + never overwrite -- the
    // decomp LogoSetup auto-advances (RSDK.LoadScene -> ++listPos -> Title) after
    // the logos play, which would re-enter this with the NEXT scene's count (0,
    // since Title objects aren't registered yet) and clobber the measurement. The
    // first Logos load's count is the E4 evidence. > 0 proves the Logos Scene1.bin
    // placements created through the SAME LoadScene path GHZ uses.
#if defined(P6_FRONTEND_LOGOS)
    if (p6_w_logos_objcount == 0
        && !strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "Logos")) {
        int32 nf = 0;
        for (int32 s = 0; s < SCENEENTITY_COUNT; ++s)
            if (RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + s)->classID)
                ++nf;
        p6_w_logos_objcount = nf;
    }
#endif
    // CP5a (Task #267, qa_engine_title T4): census the live-classID scene entities
    // the generic InitObjects chain just instantiated for the TITLE scene (the
    // TitleLogo logo-piece placements). LATCH on the "Title" folder + never
    // overwrite -- TitleSetup auto-advances (RSDK.LoadScene -> "Menu") after the
    // title plays, which would re-enter with the next scene's count (0, since Menu
    // objects aren't registered yet) and clobber the measurement. The first Title
    // load's count is the T4 evidence. > 0 proves the Title Scene1.bin placements
    // created through the SAME LoadScene path GHZ + Logos use.
#if defined(P6_FRONTEND_TITLE)
    if (p6_w_title_objcount == 0
        && !strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "Title")) {
        int32 nf = 0;
        for (int32 s = 0; s < SCENEENTITY_COUNT; ++s)
            if (RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + s)->classID)
                ++nf;
        p6_w_title_objcount = nf;
    }
#endif
    // M1 (qa_engine_menu M4): census the live-classID scene entities the generic
    // InitObjects chain just instantiated for the MENU scene (the UIControl/
    // UIBackground/UIButton/... Scene1.bin placements). LATCH on the "Menu" folder +
    // never overwrite -- a later MenuSetup_SaveSlot_ActionCB advance (M2) would re-
    // enter with the next scene's count and clobber the measurement. The first Menu
    // load's count is the M4 evidence. > 0 proves the Menu Scene1.bin placements were
    // created through the SAME LoadScene path GHZ/Logos/Title use.
#if defined(P6_FRONTEND_MENU)
    if (p6_w_menu_objcount == 0
        && !strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "Menu")) {
        int32 nf = 0;
        for (int32 s = 0; s < SCENEENTITY_COUNT; ++s)
            if (RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + s)->classID)
                ++nf;
        p6_w_menu_objcount = nf;
    }
#endif
    p6_i2_selfcheck(); // P6.8 I2: assert slot->pool indirection is 1:1 (byte-identical)
    p6_scan_index_build(); // P6.8 I3c: sorted-by-x scene-entity index for the loop1 spatial cull
    // Signpost campaign #326 (2026-07-10, MEASURED live _brg_probe.log): mark that the
    // LOAD PATH built the index from the FULL pre-shrink pool (idx_n=968 at t3.9). The
    // chain frame's #325 "settled rebuild" used to fire AFTER the near-shrink below
    // (ticking resumes post-load) and REPLACED this full index with a 32-entry
    // residents-only census -- every dormant scene entity (bridge-1 at (1184,904),
    // all badniks/rings past the spawn window) lost its near bit and could never
    // rematerialize (p6_w_arun_inspan=339 gapmiss=339 btch_calls=0). The frame path
    // now latches settled WITHOUT rebuilding when this counter advanced since the
    // folder flip.
    ++p6_scan_loadbuild_seq;
    // I3b SHRINK measurement (read-only, one-shot): walk the full scene region, count populated +
    // max + first-gap, so the atomic shrink knows the live layout (compacted vs spread) from DATA.
    // RSDK_ENTITY_AT here is the current identity/full accessor -> zero pool perturbation.
    {
        int32 np = 0, mx = -1, fg = -1;
        for (int32 s = RESERVE_ENTITY_COUNT; s < TEMPENTITY_START; ++s) {
            if (RSDK_ENTITY_AT(s)->classID) { ++np; mx = s; }
            else if (fg < 0) fg = s;
        }
        p6_w_pool_npop = np; p6_w_pool_maxls = mx; p6_w_pool_firstgap = fg;
    }
    // P6.8 I3b 2b STREAMING (load phase): camera-local NEAR-shrink to scene_phys=640. Seed p6_scan_near
    // for the SPAWN camera (the per-frame p6_scan_update_near in ProcessObjects has not run yet at load),
    // then the overlay relocates the near-populated set + dormants the far ones. One-shot (.bss-zero
    // guard); first arm = GHZ1. p6_scan_index_build (above) built the sorted index p6_scan_update_near
    // needs. LOAD-ONLY for now -> qa_p6_pool_shrink GREEN (pool shrunk) but R0-R16 RED (far entities the
    // camera later reaches stay dormant); Build 2's per-frame materialize/dormant turns R0-R16 GREEN.
    // CP4: the camera-local pool COMPACT relocates the scene region into a dense
    // physical pool for the GHZ scan/fps win -- it is gameplay-pool machinery and
    // is skipped for a non-GHZ UI scene (the 4 Logos entities don't need it).
    if (p6_isGHZ) {
        static int s_compact_done = 0;
#if defined(P6_FRONTEND_MENU)
        // #319: a GHZ reload (above) re-armed the compact -> re-run it on this reload so the
        // pool re-shrinks + rebuilds streaming state cleanly (mirrors the first load).
        if (s_ghz_compact_rearm) { s_compact_done = 0; s_ghz_compact_rearm = 0; }
#endif
        if (!s_compact_done && s_ovl.compact_fn) {
            s_compact_done = 1;
            p6_scan_update_near(cameraCount > 0 ? (int32)(cameras[0].position.x >> 16) : 0);
            s_ovl.compact_fn();
        }
    }
    // #P0 (GHZ1 parity): the level-clock force MOVED to just BEFORE InitObjects
    // (#312 timeEnabled fix, see the comment there) so a cutscene Create's
    // explicit timeEnabled=false survives -- PC ordering.
    p6_w_load_step = 35; // #251 phase-2: arm VDP1 sheet binds
    p6_ghz_arm_env();
    P6_LT_MARK(10); // Task #271 S10: InitObjects (entity Create/StageLoad) + arm_env VDP1 binds
#if defined(P6_FRONTEND_TITLE)
    // CP5b.3 (Task #272): present the Mania title BACKDROP on NBG1. The cells were
    // uploaded above (load gap); now build the composite map (island layer 3 +
    // clouds layer 2) + CRAM + display. Runs AFTER LoadSceneAssets so fullPalette[0]
    // + the layouts are final. tileLayers[2]=Clouds (16x16), tileLayers[3]=Island
    // (64x64) per the Title/Scene1.bin parse. The VDP1 logo/Sonic composite on top
    // (NBG1 priority 1 < VDP1 sprite priority). MEASURED RED ROOT CAUSE: the front-
    // end frame only armed SPRON (p6_vdp2_arm_sprites_only) -> the entire VDP2
    // backdrop stayed black (no NBG present). This is the fix.
    if (!strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "Title")
        && tileLayers[3].layout && tileLayers[2].layout) {
        p6_w_title_backdrop_armed =
            (int32)(((uint32)tileLayers[3].widthShift << 8) | (uint32)tileLayers[2].widthShift);
        p6_vdp2_present_title_backdrop(
            (const unsigned char *)tilesetPixels,
            (const unsigned short *)tileLayers[3].layout, (int)tileLayers[3].widthShift,
            (const unsigned short *)tileLayers[2].layout, (int)tileLayers[2].widthShift,
            (const unsigned short *)fullPalette[0]);
        p6_w_title_backdrop_done = 1;
        // CP5b.6 (Task #276): arm the RBG0 MODE-7 rotating island over the NBG1
        // backdrop. Uses the SAME A0 island cells (already uploaded) + the SGL
        // default RPT @ 0x05E3FF00 (RPTA already valid -- MEASURED). Per-frame
        // rotation in p6_frontend_frame via p6_vdp2_title_island_rbg0_frame.
        p6_vdp2_title_island_rbg0_arm(
            (const unsigned short *)tileLayers[3].layout, (int)tileLayers[3].widthShift);
        // SUB2 (#276 clouds-coexist) -- ROOT-CAUSE FIX (MEASURED p6_w_title_clouds_armed==0
        // while island_armed==1): the clouds-B1 arm was gated on !p6_folderReload at the
        // upload_cells site (:5051), but the TITLE scene enters via a FOLDER-RELOAD, so it
        // never ran. Arm it HERE instead -- this block has NO reload guard (it runs whenever
        // the backdrop arms, which island_armed==1 proves), tileLayers[2].layout is non-null
        // (checked above), and tilesetPixels is the same live transient the present() above
        // just consumed (the backdrop renders -> it is valid here, reload or not). Clouds
        // NBG1 on bank B1 coexists with the RBG0 island (A0/A1/B0) per ST-058:1030; the
        // per-frame p6_vdp2_title_clouds_b1_config is already gated on clouds_armed in
        // p6_frontend_frame, so arming here lights it up.
        p6_vdp2_title_clouds_b1_arm(
            (const unsigned char *)tilesetPixels,
            (const unsigned short *)tileLayers[2].layout, (int)tileLayers[2].widthShift);
    }
#endif
#if defined(P6_FRONTEND_LOGOS)
    // Task #271: phase-2 totals -> the ground-truth ms-per-fill for converting the
    // masked-core I/O fills (S2/S5) to ms. ms/fill = ph2_vbl*16.67/ph2_fills.
    p6_w_lt_ph2_fills = p6_w_lt_fills[8] + p6_w_lt_fills[9] + p6_w_lt_fills[10];
    p6_w_lt_ph2_vbl   = p6_w_lt_vbl[8]   + p6_w_lt_vbl[9]   + p6_w_lt_vbl[10];
#endif
    // #249/#250: run the resident layout pre-inflate (the band-crossing stall fix)
    // NOW -- AFTER LoadTileConfig packed 0x060E0000 + LoadSceneFolder/Assets/Init-
    // Objects finished every cart/heap allocation. MEASURED ROOT CAUSE: running it
    // at boot (inside Mount, BEFORE LoadTileConfig) corrupted the packed collision
    // (t1hash 0xFC30CD79 != golden, every real cell mis-encoded -> fall-through).
    // Deferring it here finalizes the pack first; the pre-inflate then fills the
    // cart resident store before the first p6_ghz_frame Refill. No-op when the
    // resident path is disabled or scratch is unarmed. (Declared file-scope above.)
    p6_w_load_step = 36; // #251 phase-2: resident layout pre-inflate (538KB into cart)
    // CP4: no band store is mounted for a non-GHZ UI scene (Logos has no
    // <TAG>LAYT.BIN) so the pre-inflate would no-op anyway; skip it explicitly.
    if (p6_isGHZ)
        SaturnLayout_PreInflateResident();
    // FG-tile-mutation piece 1 RED gate (qa_p6_fg_settile.py): prove RSDK.SetTile
    // now REMOVES a tile from the STREAMED FG layer (was a silent drop). Runs once
    // per GHZ load, here (layout mounted + resident). Self-restoring round trip on
    // an FG-Low tile via slot 0: GetTile(orig)->SetTile(sentinel)->GetTile(seen)->
    // restore(orig). GREEN when seen==sentinel. Bind resets slot-0 windows, which
    // the FG present rebinds every frame anyway -> no collision/present impact.
    if (p6_isGHZ) {
        int32 fgl = p6_fglow_layer_index();
        SaturnLayout_Bind(0, fgl);
        int32 tx = 16, ty = 16; // safely in-bounds for the 1024-wide GHZ FG
        uint16 orig = SaturnLayout_GetTile(0, tx, ty);
        uint16 sentinel = (uint16)(orig ^ 0x0ABC);
        SaturnLayout_SetTile(0, tx, ty, sentinel);
        uint16 seen = SaturnLayout_GetTile(0, tx, ty);
        SaturnLayout_SetTile(0, tx, ty, orig); // restore -> cache byte-identical
        p6_w_lay_settile_rt = ((int32)orig << 16) | (int32)seen;
        p6_w_lay_settile_ok = (seen == sentinel) ? 1 : 0;
    }
    p6_w_load_step = 37; // #251 phase-2: done (first p6_ghz_frame imminent)
    // #182: trigger the STAGE BGM now that the scene is live. The continuous-GHZ
    // shipping boot never called PlayStream (p6_w_str_track stayed -1) -> the level
    // was silent / the title CD-DA was never replaced. This mirrors the decomp's
    // stage-load music: Music_PlayTrack(TRACK_STAGE) -> RSDK.PlayStream(stage music
    // name, 0,0, loop, true) (Music.c:259-276). GreenHill1.ogg -> the Saturn
    // AudioDevice::HandleStreamLoad maps it to CUE audio track 2 -> CD-DA. Runs
    // HERE, AFTER every LoadSceneFolder/Assets pack read -- CD-DA and the GFS pack
    // reads share the CD block, so an earlier play would be displaced (the 7c-moved
    // ordering hazard, :3388). Play-once guard: a same-track reload must not restart
    // the already-looping CD-DA.
    if (!strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "GHZ")) {
        static int32 s_ghzBgmArmed = 0;
        if (!s_ghzBgmArmed) {
            engine.streamsEnabled = true; // NO-CTORS TRAP (:1237): set explicitly
            // FIXME #182: GHZ2 (act 2) should play GreenHill2.ogg -- needs its own
            // CD-DA track; only GreenHill1 (track 2) is mastered today. Table-ize
            // per zone/act when more zones ship (mirror Music_SetMusicTrack).
            PlayStream("GreenHill1.ogg", 0, 0, 1, true);
            s_ghzBgmArmed = 1;
        }
    }
#if defined(P6_FRONTEND_TITLE)
    // TITLE BGM (this session): mirror the GHZ stage-BGM wiring [[ghz-engine-bgm-wiring]]
    // for the front-end TITLE scene. The decomp TitleSetup_State_Wait calls
    // Music_PlayTrack(TRACK_STAGE) -> RSDK.PlayStream(trackNames[0], ...) once the
    // black fade-in completes (TitleSetup.c:137-150). On Saturn, mirror the proven GHZ
    // trigger here (scene-load, AFTER every LoadSceneFolder/Assets pack read so the
    // shared CD block is free -- the 7c ordering hazard) with an explicit
    // PlayStream("TitleScreen.ogg"). AudioDevice::HandleStreamLoad (:1866) maps
    // "TitleScreen.ogg" -> CUE audio track 3 -> jo_audio_play_cd_track(3) -> real CD-DA.
    // This does NOT depend on Music_StageLoad having populated trackNames[0] (the GHZ
    // memory measured that auto-drive as unreliable on the shipping boot). The decomp's
    // own Music_PlayTrack may also fire ~1s later when the wait timer expires; the
    // play-once CD-DA + the static guard below keep this trigger from restarting it.
    // 2026-07-17 REMOVED the early scene-load PlayStream arm (user symptom:
    // title music "starts, STOPS, restarts as the ring animation begins").
    // MEASURED mechanism: this arm started CD-DA at title LOAD; post-load GFS
    // reads DISPLACED it (the 7c shared-CD-block hazard above); then the
    // decomp's own TitleSetup FlashIn Music_PlayTrack (TitleSetup.c:137-150,
    // = the ring-anim moment) RESTARTED it -- the audible restart is the proof
    // the canonical path fires in this flavor. PC-canonical = NO music until
    // FlashIn, ONE start. The canonical PlayTrack now performs the single
    // start (post-load, CD block free); p6_cdda_play's same-track idempotence
    // guard (p6_snd.c:119) suppresses any later duplicate PlayTrack.
    if (!strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "Title")) {
        engine.streamsEnabled = true; // NO-CTORS TRAP (:1237): the canonical
                                      // PlayTrack path still needs it armed
    }
#endif
    // F.2 diag: probe the windowed store at the spawn column right after the
    // mount+bind+InitObjects, to witness whether collision reads serve the
    // newly-loaded zone (GHZ2 FG Low ty=90 solid / FG High ty=86) or empty.
    {
        p6_w_xtile_lo = (int32)SaturnLayout_GetTile(0, 16, 90);
        p6_w_xtile_hi = (int32)SaturnLayout_GetTile(1, 16, 86);
    }
#if defined(P6_GHZ2_BOOT)
    // #237: latch THIS load's state ONCE if it is GHZ2 (folder/id == GHZ/2), so
    // it survives the player's subsequent drop+autorun + scene transition. Entity
    // census + spawn read after InitObjects (the same point W11b reads GHZ1).
    if (!p6_w_ghz2_loaded
        && !strcmp(sceneInfo.listData[sceneInfo.listPos].folder, "GHZ")
        && sceneInfo.listData[sceneInfo.listPos].id[0] == '2') {
        p6_w_ghz2_loaded   = 1;
        p6_w_ghz2_listpos  = (int32)sceneInfo.listPos;
        p6_w_ghz2_xtile_lo = p6_w_xtile_lo;
        p6_w_ghz2_xtile_hi = p6_w_xtile_hi;
        int32 n = 0;
        for (int32 s = 0; s < SCENEENTITY_COUNT; ++s)
            if (RSDK_ENTITY_AT(RESERVE_ENTITY_COUNT + s)->classID)
                ++n;
        p6_w_ghz2_entcount = n;
        p6_w_ghz2_plrx     = RSDK_ENTITY_AT(0)->position.x;
        p6_w_ghz2_plry     = RSDK_ENTITY_AT(0)->position.y;
    }
#endif
}

// P6.8 Step A (Task #211): p6_ghz_reload -- RE-LOAD GHZ as the FINAL live
// scene and arm the continuous tick. Deferred to the per-frame tick (NOT run
// inside p6_scene_run) for two measured reasons:
//   (1) ADDITIVE: p6_scene_run returns with EXACTLY the W19 state -- all ~14
//       burst/proof witnesses intact, PlayStream's title CD-DA undisturbed,
//       the legacy Ring set up. The deferred switch lets the legacy Ring
//       continuous tick run first (obj/entdraw/scsp witnesses accumulate),
//       then GHZ takes over -- both capturable from one late savestate.
//   (2) CD-DA ordering (the 7c-moved hazard, :2510): the GHZ pack reads
//       (LoadSceneFolder/Assets) share the CD block with CD-DA, so they must
//       run OUTSIDE the masked load phase, in the live tick.
// Selects GHZ1 the SAME way the W11b GHZ pass did (scan every category range
// for folder "GHZ" id '1' -- GHZ lives in "Mania Mode", NOT category 0).
// =============================================================================
static void p6_ghz_reload(void)
{
    // F.4 FIX (adversarial-QA finding): prefer the GHZ1 that is IMMEDIATELY
    // FOLLOWED BY GHZ2 -- i.e. the "Mania Mode" category, where the act-advance
    // ++SceneInfo->listPos correctly resolves GHZ2. The old "first GHZ1" scan
    // landed in [Media Demo] (GHZ1 then SPZ1), so ++listPos hit SPZ1, not GHZ2
    // (MEASURED: p6_w_mount_tag='SPZ1' at listPos=8). The GHZ1 SCENE content is
    // identical across categories (folder/id == "GHZ"/"1" -> same Scene1.bin),
    // so this changes ONLY the act-advance target -- zero GHZ1-gameplay change.
    // Fallback to any GHZ1 (old behavior) if no adjacent GHZ1->GHZ2 pair exists.
    int32 found = 0, fb_c = -1, fb_i = -1, ghz2_pos = -1;
    for (int32 c = 0; c < sceneInfo.categoryCount && !found; ++c) {
        SceneListInfo *cat = &sceneInfo.listCategory[c];
        for (int32 i = cat->sceneOffsetStart; i <= cat->sceneOffsetEnd; ++i) {
            if (strcmp(sceneInfo.listData[i].folder, "GHZ")
                || sceneInfo.listData[i].id[0] != '1')
                continue;
            if (fb_c < 0) { fb_c = c; fb_i = i; } // first GHZ1 = fallback
            if (i < cat->sceneOffsetEnd
                && !strcmp(sceneInfo.listData[i + 1].folder, "GHZ")
                && sceneInfo.listData[i + 1].id[0] == '2') {
                sceneInfo.activeCategory = c;
                sceneInfo.listPos        = i;
                ghz2_pos                 = i + 1; // #237: the adjacent GHZ2 entry
                found                    = 1;
                break;
            }
        }
    }
    if (!found && fb_c >= 0) {
        sceneInfo.activeCategory = fb_c;
        sceneInfo.listPos        = fb_i;
        found                    = 1;
    }
    if (!found)
        return;
#if defined(P6_GHZ2_BOOT)
    // #237 GHZ2 play measurement: boot GHZ2 DIRECTLY as the continuous scene
    // instead of GHZ1. p6_scene_load_and_arm + p6_layout_mount_for_scene are
    // scene-generic (band store keyed on <folder><id>LAYT.BIN -> GHZ2LAYT.BIN),
    // so this loads the IDENTICAL GHZ2 state the real signpost transition reaches
    // (both route through p6_scene_load_and_arm with listPos -> GHZ2). Diag-only
    // toggle (env P6_GHZ2_BOOT=1); default boots GHZ1.
    if (ghz2_pos >= 0)
        sceneInfo.listPos = ghz2_pos;
#endif
    p6_scene_load_and_arm();
    p6_ghz_continuous_armed = 1;
}

#if defined(P6_FRONTEND_LOGOS)
// =============================================================================
// CP4 (Task #265): p6_logos_reload -- the FRONT-END mirror of p6_ghz_reload.
// Selects the Logos scene (folder "Logos") the SAME way the GHZ reload selects
// GHZ -- scan every category range for the folder name (no listPos assumption;
// the engine GameConfig orders it) -- then load+arm through the generic
// p6_scene_load_and_arm (its GHZ-tilemap steps no-op for the non-GHZ scene).
// Sets p6_w_frontend_folder_tag to a 2-char tag of the loaded folder (gate E1).
// Behind -DP6_FRONTEND_LOGOS: the default shipping build never compiles this and
// boots GHZ unchanged. =============================================================
static void p6_logos_reload(void)
{
    int32 found = 0;
    for (int32 c = 0; c < sceneInfo.categoryCount && !found; ++c) {
        SceneListInfo *cat = &sceneInfo.listCategory[c];
        for (int32 i = cat->sceneOffsetStart; i <= cat->sceneOffsetEnd; ++i) {
            if (!strcmp(sceneInfo.listData[i].folder, "Logos")) {
                sceneInfo.activeCategory = c;
                sceneInfo.listPos        = i;
                found                    = 1;
                break;
            }
        }
    }
    if (!found)
        return;
    // E1: tag the loaded folder's first two chars ('L'<<8 | 'o' = 0x4C6F).
    {
        const char *f = sceneInfo.listData[sceneInfo.listPos].folder;
        p6_w_frontend_folder_tag =
            (int32)(((uint32)(uint8)f[0] << 8) | (uint32)(uint8)(f[0] ? f[1] : 0));
    }
    p6_scene_load_and_arm();
    p6_ghz_continuous_armed = 1; // reuse the continuous-armed flag (drives the tick)
}

#if defined(P6_FRONTEND_TITLE)
// =============================================================================
// CP5a (Task #267): p6_title_reload -- the FRONT-END mirror of p6_logos_reload
// for the TITLE scene. VERBATIM copy of p6_logos_reload (the proven CP4 path)
// except it scans every category range for the folder "Title" and tags it
// 0x5469 ('T'<<8 | 'i'; gate T1). Load+arm through the same generic
// p6_scene_load_and_arm (its GHZ-tilemap steps no-op for the sprite-only Title
// scene). Behind -DP6_FRONTEND_TITLE; the default shipping build never compiles
// this and boots GHZ unchanged. =============================================================================
static void p6_title_reload(void)
{
    int32 found = 0;
    for (int32 c = 0; c < sceneInfo.categoryCount && !found; ++c) {
        SceneListInfo *cat = &sceneInfo.listCategory[c];
        for (int32 i = cat->sceneOffsetStart; i <= cat->sceneOffsetEnd; ++i) {
            if (!strcmp(sceneInfo.listData[i].folder, "Title")) {
                sceneInfo.activeCategory = c;
                sceneInfo.listPos        = i;
                found                    = 1;
                break;
            }
        }
    }
    if (!found)
        return;
    // T1: tag the loaded folder's first two chars ('T'<<8 | 'i' = 0x5469).
    {
        const char *f = sceneInfo.listData[sceneInfo.listPos].folder;
        p6_w_frontend_folder_tag =
            (int32)(((uint32)(uint8)f[0] << 8) | (uint32)(uint8)(f[0] ? f[1] : 0));
    }
    // CP5b.3 (Task #272) PREREQUISITE: allocate `scanlines` on Saturn BEFORE the
    // Title loads. MEASURED FINDING: RSDK::scanlines (Scene.cpp:13) is NULL on
    // Saturn -- it is allocated ONLY by the desktop RenderDevice (DX11/EGL/...),
    // and there is NO Saturn RenderDevice. The engine's ProcessObjectDrawLists
    // (Object.cpp:1198-1199) calls `layer->scanlineCallback(scanlines)` UNGATED
    // on Saturn, so the moment TitleBG_SetupFX registers Scanline_Island/_Clouds
    // on tileLayers 2/3, those callbacks write `scanlines->deform.x` etc. With
    // scanlines==NULL that is a WILD WRITE -> crash. Point it at a front-end-only
    // cart buffer (SCREEN_YSIZE=240 entries x 16 B = 3,840 B) so the callbacks
    // run safely. The callbacks fill scanlines[] but NOTHING on Saturn consumes
    // it into VDP2 (the software DrawLayer* rasterizer that reads it is Saturn-
    // gated off, Object.cpp:1203); the VDP2 backdrop is driven by
    // p6_vdp2_present_title_backdrop instead. Cart 0x22400000 = the front-end
    // free cart head (no SaturnLayout / GHZ tileset / pool in this flavor).
    scanlines = (ScanlineInfo *)0x22400000u;
    memset((void *)scanlines, 0, (size_t)SCREEN_YSIZE * sizeof(ScanlineInfo));
    p6_scene_load_and_arm();
    p6_ghz_continuous_armed = 1; // reuse the continuous-armed flag (drives the tick)
}
#endif // P6_FRONTEND_TITLE

#if defined(P6_FRONTEND_MENU)
// =============================================================================
// M1 (qa_engine_menu.py): p6_menu_reload -- the FRONT-END mirror of p6_title_reload
// for the MENU scene (category "Presentation", folder "Menu"). VERBATIM copy of the
// proven p6_title_reload path except it scans every category range for the folder
// "Menu" and tags it 0x4D65 ('M'<<8 | 'e'; gate M1). Load+arm through the same
// generic p6_scene_load_and_arm (its GHZ-tilemap steps no-op for the sprite-only UI
// scene -- Menu has no FG/collision; the GHZ arm is guarded behind p6_isGHZ). Also
// arms `scanlines` to the front-end cart buffer the same way p6_title_reload does:
// UIBackground_DrawNormal / the engine ProcessObjectDrawLists path is safe regardless,
// but any UI object that installs a scanlineCallback would otherwise wild-write the
// NULL Saturn `scanlines` (there is no Saturn RenderDevice to allocate it). Behind
// -DP6_FRONTEND_MENU; the default GHZ/Title builds never compile this.
// =============================================================================
static void p6_menu_reload(void)
{
    int32 found = 0;
    for (int32 c = 0; c < sceneInfo.categoryCount && !found; ++c) {
        SceneListInfo *cat = &sceneInfo.listCategory[c];
        for (int32 i = cat->sceneOffsetStart; i <= cat->sceneOffsetEnd; ++i) {
            if (!strcmp(sceneInfo.listData[i].folder, "Menu")) {
                sceneInfo.activeCategory = c;
                sceneInfo.listPos        = i;
                found                    = 1;
                break;
            }
        }
    }
    if (!found)
        return;
    // M1: tag the loaded folder's first two chars ('M'<<8 | 'e' = 0x4D65).
    {
        const char *f = sceneInfo.listData[sceneInfo.listPos].folder;
        p6_w_frontend_folder_tag =
            (int32)(((uint32)(uint8)f[0] << 8) | (uint32)(uint8)(f[0] ? f[1] : 0));
    }
    // Safe `scanlines` backing (front-end-only cart buffer) -- see p6_title_reload
    // for the MEASURED rationale (NULL Saturn scanlines + an ungated scanlineCallback
    // == wild write). The Menu UI classes registered for M1 do not install one, but
    // arming it is cheap and matches the Title path's safety contract.
    scanlines = (ScanlineInfo *)0x22400000u;
    memset((void *)scanlines, 0, (size_t)SCREEN_YSIZE * sizeof(ScanlineInfo));
    p6_scene_load_and_arm();
    p6_ghz_continuous_armed = 1; // reuse the continuous-armed flag (drives the tick)
    // M1b AUTH-GATE FLIP is done at the top of the first p6_frontend_frame via
    // s_ovl.menu_apic_init_fn (set by the overlay entry, run inside p6_scene_load_and_arm
    // above) -- it cannot run before the overlay loads, and it must precede the scene's
    // first StaticUpdate (which runs in p6_frontend_frame, AFTER this reload returns).
}

#if defined(P6_AIZ_TEST)
// =============================================================================
// M3.0 (qa_p6_aiz_scene): p6_aiz_reload -- the AIZ intro-cutscene mirror of
// p6_menu_reload. Scans every category range for folder "AIZ" (the decomp
// Cutscenes/"Angel Island Zone" target, MenuSetup.c:1121), then load+arm through
// the generic p6_scene_load_and_arm. AIZ is a GAMEPLAY-tilemap scene (FG Low/High
// like GHZ, NOT a sprite-only UI scene), so the p6_isAIZ branch added to
// p6_scene_load_and_arm uploads its 16x16 cells to NBG1 (the FG render path). The
// AIZ object classes (Tornado/EggRobo/...) are UNREGISTERED in M3.0 -- the engine
// loads them as inert blank entities (classID 0, no Update/Draw -- Scene.cpp:590-610,
// the "Object Class N is unimplemented" path), so the FG tilemap renders with zero
// new object compiles; the actors come in M3.1+. Behind -DP6_AIZ_TEST: a
// P6_FRONTEND_MENU diagnostic build that boots straight to AIZ (the same pattern
// F.1/F.2 used with P6_TRANSITION_TEST to prove a load+arm before wiring the trigger).
// =============================================================================
static void p6_aiz_reload(void)
{
    int32 found = 0;
    for (int32 c = 0; c < sceneInfo.categoryCount && !found; ++c) {
        SceneListInfo *cat = &sceneInfo.listCategory[c];
        for (int32 i = cat->sceneOffsetStart; i <= cat->sceneOffsetEnd; ++i) {
            if (!strcmp(sceneInfo.listData[i].folder, "AIZ")) {
                sceneInfo.activeCategory = c;
                sceneInfo.listPos        = i;
                found                    = 1;
                break;
            }
        }
    }
    if (!found)
        return;
    // tag the loaded folder ('A'<<8 | 'I' = 0x4149).
    {
        const char *f = sceneInfo.listData[sceneInfo.listPos].folder;
        p6_w_frontend_folder_tag =
            (int32)(((uint32)(uint8)f[0] << 8) | (uint32)(uint8)(f[0] ? f[1] : 0));
    }
    // Safe `scanlines` backing (front-end cart buffer) -- AIZSetup is unregistered in
    // M3.0 so no scanlineCallback installs, but arm it for the Title path's safety
    // contract (a NULL Saturn scanlines + an ungated callback == wild write; see
    // p6_title_reload for the MEASURED rationale). 0x22400000 = the front-end free cart.
    scanlines = (ScanlineInfo *)0x22400000u;
    memset((void *)scanlines, 0, (size_t)SCREEN_YSIZE * sizeof(ScanlineInfo));
    // M3.2 FG fix: the AIZ FG tilemap present serves tiles from SaturnLayout's camera-local
    // windows, which inflate from the AIZ1LAYT band store through a scratch buffer. The
    // front-end boot path never effectively armed SaturnLayout_SetScratch (the GHZ1LAYT
    // chain-load block is GHZ-gameplay-only), so s_scratchPtr stays NULL -> PreInflateResident
    // AND the band Refill both bail (MEASURED: p6_w_lay_resident=0, p6_w_lay_refills=0) -> the
    // FG-Low window stays zeroed -> GetTile returns 0 -> AIZ tile 0 (opaque) fills the screen.
    // Arm the scratch HERE (the same fixed P6_LW_LAYSCRATCH GHZ uses) BEFORE the arm mounts
    // AIZ1LAYT + pre-inflates. Idempotent (only sets the s_scratchPtr/cap statics).
    {
        static void *p6_aiz_layScratch = (void *)P6_LW_LAYSCRATCH;
        SaturnLayout_SetScratch(&p6_aiz_layScratch, 0x8000); // file-scope extern "C" decl above
    }
    // R3.2 (#305): load the AIZ object anim pack (cd/AIZOBJ.PAK) into the CART
    // P6_HW_OBJANIMPAK window BEFORE the scene's StageLoad runs (LoadSpriteAnimation
    // resolves from the resident packs first). The front-end SKIPS the GHZ GHZOBJ.PAK,
    // so this window is free -- AIZOBJ.PAK (AIZ/AIZTornado.bin) resolves the Tornado's
    // anim on the FAST path, so AIZTornado_StageLoad's LoadSpriteAnimation returns
    // aniFrames>=0 (was -1: empty packs -> the slow windowed-GFS LoadFile failed,
    // MEASURED -> the biplane drew nothing). Zero engine-logic change: the existing
    // Animation.cpp pack-resolution loop reads paks[1]=P6_HW_OBJANIMPAK / objapk_bytes.
    {
        int n = rsdk_storage_load_to_lwram("AIZOBJ.PAK", (void *)P6_HW_OBJANIMPAK,
                                           P6_HW_OBJANIMPAK_CAP);
        if (n > 0)
            p6_w_objapk_bytes = n;
    }
    // Task #322 (AIZ scene-load CD-seek stall): load the AIZ SCENE anim pack
    // (cd/AIZANIM.PAK -- the GLOBAL object set Ring/ItemBox/Spring/Spikes/Dust/
    // Animals/ScoreBonus/SignPost/PlaneSwitch/Shields/Invincible/PhantomRuby/HUD +
    // Knux + AIZ/decoration) into the front-end-FREE P6_HW_ANIMPAK window (the GHZ
    // GHZANIM.PAK is skipped here). MEASURED live (P6_GFS_OPENTRACE): each of these
    // .bin StageLoad LoadSpriteAnimation slow-pathed into DATA.RSDK = one scattered
    // GFS_Seek EACH (~15 of the 40 AIZ-load seeks / 6.9 s CD). Mounting the pack makes
    // Animation.cpp:70-124's Saturn pack-first arm (paks[0]=P6_HW_ANIMPAK, size
    // p6_w_apk_bytes) resolve them from RAM with ZERO CD seek -- exact GHZANIM.PAK
    // idiom, zero engine-logic change. 33 KB fits the 69,632 B window. Loaded BEFORE
    // the scene StageLoad runs (like AIZOBJ.PAK) so the resolve finds them first.
    {
        int n = rsdk_storage_load_to_lwram("AIZANIM.PAK", (void *)P6_HW_ANIMPAK,
                                           P6_HW_ANIMPAK_CAP);
        if (n > 0)
            p6_w_apk_bytes = n;
    }
    // AIZ-SONIC-VIS FIX (#321, agent-rooted, data-driven): the fly-in Tornado renders but
    // SONIC's top-wing sprite is MISSING. Live-proven: Sonic (slot0) IS correctly riding the
    // Tornado (Player_State_Static, visible=1, x tracks 92->6274) -- NOT a logic/position bug.
    // The RENDER cause: the chain build (#if P6_FRONTEND_TITLE) gates OUT the 9 GHZ gameplay
    // sheets at boot and only stages Players/Sonic*.gif at the far-later GHZ-handoff seam, so
    // during AIZ the Sonic1.gif surface has saturnSheetSlot==-1 -> Player_Draw's DrawSprite finds
    // no VDP1 handle -> the wing is empty. IDENTICAL class to R3.1 (Tornado invisible until
    // AIZOBJ.SHT staged) and #244 (Tails). FIX: stage SONIC1/2/3.SHT HERE (banded, NO
    // MakeResident; idempotent via SaturnSheet_FindSlot). Palette needs no upload -- the arm
    // below runs p6_pal_mirror(fullPalette[0]) (Sonic's global colors -> sprite CRAM bank 1),
    // the same path the Tornado uses. All inside p6_aiz_reload (#if P6_AIZ_TEST reach) -> plain
    // GHZ byte-identical. Gate: tools/qa_aiz_sonic_wing.py (p6_w_aiz_sonicsht_slot>=0 in AIZ).
    {
        // #323 (this session): + TAILS1.SHT. The sidekick Tails is IN the AIZ intro
        // (P2FlyIn beat 3, AIZSetup.c:425-450: Tails flies in + lands beside Sonic) but
        // the chain staged Players/Tails1.gif ONLY at the far-later GHZ-handoff STEP-3 ->
        // Tails' surface had saturnSheetSlot==-1 for the whole AIZ leg -> invisible
        // (user-reported; the IDENTICAL class as the #321 Sonic-on-wing fix above).
        static const char *sonicSht[4]  = { "SONIC1.SHT", "SONIC2.SHT", "SONIC3.SHT",
                                            "TAILS1.SHT" };
        static const char *sonicPath[4] = { "Players/Sonic1.gif", "Players/Sonic2.gif",
                                            "Players/Sonic3.gif", "Players/Tails1.gif" };
        int32 aizPlrSlot[4] = { -1, -1, -1, -1 };
        for (int32 si = 0; si < 4; ++si) {
            RETRO_HASH_MD5(sph);
            GEN_HASH_MD5(sonicPath[si], sph);
            int32 have = SaturnSheet_FindSlot((const uint32 *)sph);
            if (have >= 0) { aizPlrSlot[si] = have; continue; }
            int ssn = rsdk_storage_load_to_lwram(sonicSht[si], (void *)P6_LW_ENTITYLIST, 0x10000);
            if (ssn > 0) {
                int32 sslot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)ssn);
                if (sslot >= 0) {
                    SaturnSheet_SetHash(sslot, (const uint32 *)sph);
                    aizPlrSlot[si] = sslot;
                }
            }
        }
        { RETRO_HASH_MD5(sph1); GEN_HASH_MD5("Players/Sonic1.gif", sph1);
          p6_w_aiz_sonicsht_slot = SaturnSheet_FindSlot((const uint32 *)sph1); }
        // #323 AIZ-leg draw/inflate hog (the #317 recipe at the Menu->AIZ seam):
        // MEASURED (chain, _pan_trace3): the AIZ leg runs 3.5-8 fps; DrawLists is
        // 18-25 ms on the fly-in and 36-46 ms at the claw beats with 680-1340
        // SaturnSheet_FetchRect inflates per beat window -- every AIZOBJ/Sonic/Tails
        // sprite re-inflates its banded rect per draw (and the VDP1 churn blinks
        // sprites: the Tornado pilot flickers, floating fragments -- user-reported
        // "artifacts", the title-vdp1-slot-thrash class). Promote the AIZ leg's hot
        // sheets to resident (zero-inflate memcpy path). NO ResReset here: the boot
        // block (p6_scene_run ~:4545) already reclaimed the title RES and promoted
        // DISPLAY/PLROBJ/HBHOBJ (still needed by the GHZCutscene leg after AIZ) --
        // the store has ~1.3 MB free. MakeResident is bounds-checked: any sheet that
        // does not fit simply stays banded (no corruption). Priority order = drawn
        // every fly-in frame first: AIZOBJ (Tornado+pilot+claw+EggRobos+Ruby),
        // SONIC1, TAILS1, then SONIC2/SONIC3.
        {
            RETRO_HASH_MD5(aph);
            GEN_HASH_MD5("AIZ/Objects.gif", aph);
            int32 aizObjSlot = SaturnSheet_FindSlot((const uint32 *)aph);
#if defined(P6_FRAMEDIR)
            // Stage-1 FRD (checklist sec 7, Menu->AIZ seam): pre-cut frame
            // directories for the AIZ leg's hot sheets REPLACE their
            // MakeResident (a staged FRD serves every anim-rect miss with
            // one aligned linear copy -- no inflate, no repack). MEASURED
            // budget: boot residents 679,776 + FRD {AIZOBJ 108,620 +
            // SONIC1 259,316 + TAILS1 261,644} = 1,309,356 B < the
            // 1,703,936 B store; the all-5-FRD variant would overflow by
            // ~5 KB, so SONIC2/3 keep today's bounds-checked promote.
            // A failed FRD stage (-1) falls back to the promote, verbatim.
            // MEASURED overlap (live s_frd struct trace, 2026-07-10): without a
            // floor the AIZ-leg FRD blobs land at 0x22440000+ and TAILS1's
            // header+directory (staged @0x22499D40) sits INSIDE the AIZ BG
            // fixed staging windows chr/map/l3/l0/l2 = [0x22480000..0x224A3000)
            // (:~6929-6936), which load AFTER this block -> the directory is
            // garbled post-djb2 -> 100% TAILS1 lookup misses all AIZ leg (309/
            // run, ring rects PROVEN present offline). Float the FRD staging
            // above the BG windows: RES_BASE+0xA4000 = 0x224A4000; the 3 blobs
            // end 0x2253D8CC < RES_END 0x225A0000 (402,740 B headroom), the
            // SONIC2 promote below still fits, SONIC3 falls banded (same as
            // pre-FRD). NOTE: the promoted RESIDENT sheets' pixels overlapped
            // these same BG windows PRE-FRD (SONIC1 patterns under 0x22480000
            // chr) -- a pre-existing AIZ artifact class, out of FRD scope.
            SaturnSheet_ResFloor(0xA4000);
            int32 frdAiz = p6_frd_stage_file("AIZOBJ.FRD", "AIZ/Objects.gif");
            int32 frdS1  = p6_frd_stage_file("SONIC1.FRD", "Players/Sonic1.gif");
            int32 frdT1  = p6_frd_stage_file("TAILS1.FRD", "Players/Tails1.gif");
            if (aizObjSlot >= 0 && frdAiz < 0) SaturnSheet_MakeResident(aizObjSlot);
            if (aizPlrSlot[0] >= 0 && frdS1 < 0) SaturnSheet_MakeResident(aizPlrSlot[0]);
            if (aizPlrSlot[3] >= 0 && frdT1 < 0) SaturnSheet_MakeResident(aizPlrSlot[3]);
#else
            if (aizObjSlot >= 0) SaturnSheet_MakeResident(aizObjSlot);
            if (aizPlrSlot[0] >= 0) SaturnSheet_MakeResident(aizPlrSlot[0]);
            if (aizPlrSlot[3] >= 0) SaturnSheet_MakeResident(aizPlrSlot[3]);
#endif
            if (aizPlrSlot[1] >= 0) SaturnSheet_MakeResident(aizPlrSlot[1]);
            if (aizPlrSlot[2] >= 0) SaturnSheet_MakeResident(aizPlrSlot[2]);
        }
    }
    p6_scene_load_and_arm();
#if defined(P6_FRAMEDIR)
    p6_frd_attach_bound(); // FRD attach for surfaces bound before this seam's staging
#endif
    p6_ghz_continuous_armed = 1; // reuse the continuous-armed flag (drives the tick)
    // A1: the engine LoadScene of folder AIZ finished (LoadSceneFolder re-strcpy's
    // currentSceneFolder to the loaded folder during the arm above).
    p6_w_aiz_loaded   = (strcmp(currentSceneFolder, "AIZ") == 0) ? 1 : 0;
    // A3: the scene's resolved object-class count (proves the object table loaded; the
    // AIZ actor classes are unregistered M3.0 blanks, so this counts the global/default
    // classes -- > 0 whenever the scene loaded its useGlobalObjects table).
    p6_w_aiz_objcount = (int32)sceneInfo.classCount;
    // diag: non-null tileLayer count -> pins the FG-Low layer index for the M3.0b
    // present (p6_vdp2_present_ghz_camera needs the per-zone FG-Low index, G8).
    {
        int32 n = 0;
        for (int32 i = 0; i < LAYER_COUNT; ++i)
            if (tileLayers[i].layout) ++n;
        p6_w_aiz_nlayers = n;
    }
    // R2.1: load the 4-bpp AIZ Background data (cd/AIZBG.*) + upload BG4 (the
    // jungle) onto NBG0. CHR (char data) + MAP (precomputed NBG0 PND image) +
    // CMP (custom CRAM bank composition). NOTE: the BG layers are WINDOWED on
    // Saturn (>8192 B -> tileLayers[].layout is NULL, Scene.hpp:175-203), so the
    // map is PRECOMPUTED offline and copied -- NO tileLayers dependency. Cart
    // staging at 0x22480000 = the front-end free cart (p6_io_main.cpp:5874-5875:
    // no SaturnLayout/GHZ tileset/pool in this flavor); MEASURED-disjoint from
    // scanlines@0x22400000 and GFS@0x22700000. CHR/MAP are staged in cart
    // (byte-writable) then copied to B1/B0 VRAM with 16-bit writes (the
    // VDP2-VRAM-16-bit-only rule), mirroring p6_vdp2_upload_cells.
    {
        unsigned char *chr = (unsigned char *)0x22480000u; // 62,720 B
        unsigned char *map = (unsigned char *)0x22490000u; // 16,384 B (frame-0 window)
        unsigned char *cmp = (unsigned char *)0x22494000u; //     49 B
        unsigned char *rmp = (unsigned char *)0x22495000u; //  2,048 B (R2.4 stream)
        unsigned char *bnk = (unsigned char *)0x22496000u; //  1,024 B (R2.4 stream)
        unsigned char *l3  = (unsigned char *)0x22498000u; // 24,576 B full BG4 layout
        unsigned char *l0  = (unsigned char *)0x2249E000u; //  2,560 B full BG1 layout
        unsigned char *l2  = (unsigned char *)0x224A0000u; // 12,288 B full BG3 layout
        int nchr = rsdk_storage_load_to_lwram("AIZBG.CHR", chr, 0x10000);
        int nmap = rsdk_storage_load_to_lwram("AIZBG.MAP", map, 0x4000);
        int ncmp = rsdk_storage_load_to_lwram("AIZBG.CMP", cmp, 0x40);
        rsdk_storage_load_to_lwram("AIZBG.RMP", rmp, 0x800);   // for the streamer
        rsdk_storage_load_to_lwram("AIZBG.BNK", bnk, 0x400);
        rsdk_storage_load_to_lwram("AIZBG.L3",  l3,  0x6000);  // 768x16 BG4 layout
        rsdk_storage_load_to_lwram("AIZBG.L0",  l0,  0xC00);   // 80x16 BG1 layout (NBG2)
        rsdk_storage_load_to_lwram("AIZBG.L2",  l2,  0x3000);  // 384x16 BG3 layout (NBG3)
        p6_w_aiz_bg_filebytes = nchr;
        if (nchr > 0 && nmap > 0 && ncmp > 0) {
            p6_vdp2_aiz_bg_upload((const unsigned short *)chr, nchr / 2, cmp,
                                  (const unsigned short *)map, nmap / 2,
                                  (const unsigned short *)fullPalette[0]);
        }
    }
}

#if defined(P6_GHZCUT_DIRECTBOOT)
// =============================================================================
// Task #309 gate-1: p6_ghzcut_reload -- the AIZ->GHZCutscene arrival-cutscene mirror
// of p6_aiz_reload. The AIZ intro's final beat (CutsceneSonic_LoadGHZ, AIZSetup.c:896)
// does SetScene("Cutscenes","Green Hill Zone") -> GameConfig folder GHZCutscene id 1.
// This diagnostic boots STRAIGHT to that scene to prove the engine LoadScene + FG
// render + the fixed-timer cutscene -> playable-GHZ handoff in ISOLATION (the same
// pattern p6_aiz_reload used for AIZ).
//
// Scans every category range for folder "GHZCutscene" AND id[0]=='1' (both Scene1 and
// Scene2 share the folder; require id=='1' for the arrival cutscene). GHZCutscene uses
// the NORMAL FG tileset path: LoadStageGIF (Scene.cpp:1238-1249) loads the pre-decoded
// cd/GHZCUTIL.BIN + GHZCUPAL.BIN automatically, keyed on upper(folder)[:5]=="GHZCU"
// (p6_til_name) -- ZERO runtime code here for the tileset. So this OMITS the AIZ-specific
// bits (AIZOBJ.PAK load, the 4-bpp AIZBG.* block). The boss-sheet anims for CutsceneHBH's
// Heavies are NOT staged (Tier-A acceptable -> LoadSpriteAnimation returns -1 gracefully
// -> Heavies invisible). The FG cell upload + per-frame FG present are handled by the
// P6_GHZCUT_BOOT branch in p6_scene_load_and_arm + the P6_AIZ_TEST present block in
// p6_frontend_frame (its AIZ BG streaming is runtime-guarded to folder=="AIZ").
// =============================================================================
static void p6_ghzcut_reload(void)
{
    int32 found = 0;
    for (int32 c = 0; c < sceneInfo.categoryCount && !found; ++c) {
        SceneListInfo *cat = &sceneInfo.listCategory[c];
        for (int32 i = cat->sceneOffsetStart; i <= cat->sceneOffsetEnd; ++i) {
            if (!strcmp(sceneInfo.listData[i].folder, "GHZCutscene")
                && sceneInfo.listData[i].id[0] == '1') {
                sceneInfo.activeCategory = c;
                sceneInfo.listPos        = i;
                found                    = 1;
                break;
            }
        }
    }
    if (!found)
        return;
    // tag the loaded folder ('G'<<8 | 'H' = 0x4748) -- the existing front-end folder-tag witness.
    {
        const char *f = sceneInfo.listData[sceneInfo.listPos].folder;
        p6_w_frontend_folder_tag =
            (int32)(((uint32)(uint8)f[0] << 8) | (uint32)(uint8)(f[0] ? f[1] : 0));
    }
    // Safe `scanlines` backing (front-end free cart) -- a scanlineCallback could install
    // (GHZCutsceneST/FXRuby) so arm it for the Title-path safety contract (NULL Saturn
    // scanlines + ungated callback == wild write; see p6_aiz_reload for the rationale).
    scanlines = (ScanlineInfo *)0x22400000u;
    memset((void *)scanlines, 0, (size_t)SCREEN_YSIZE * sizeof(ScanlineInfo));
    // Arm the SaturnLayout band-inflate scratch BEFORE the arm mounts GHC1LAYT + pre-inflates
    // (the same fixed P6_LW_LAYSCRATCH GHZ/AIZ use). Idempotent. Without it the FG-Low window
    // stays zeroed -> GetTile returns 0 (see the AIZ rationale at p6_aiz_reload).
    {
        static void *p6_ghzcut_layScratch = (void *)P6_LW_LAYSCRATCH;
        SaturnLayout_SetScratch(&p6_ghzcut_layScratch, 0x8000);
    }
    // Task #309 Tier-B.2: load the Heavy object anim pack (cd/HBHOBJ.PAK) into the
    // CART P6_HW_OBJANIMPAK window BEFORE the scene's StageLoad runs (the front-end
    // SKIPS the GHZ GHZOBJ.PAK so this window is free, exactly like AIZOBJ.PAK in
    // p6_aiz_reload). HBHOBJ.PAK holds the 5 rewritten Heavy .bin (keyed by the
    // ORIGINAL load names SPZ1/Boss.bin etc.) -> CutsceneHBH_LoadSprites'
    // LoadSpriteAnimation resolves them on the FAST path (aniFrames>=0); their frames
    // index ONE sheet "Cutscene/HBH.gif" = the staged HBHOBJ.SHT (below). Zero
    // engine-logic change: Animation.cpp reads paks[1]=P6_HW_OBJANIMPAK/objapk_bytes.
    {
        int n = rsdk_storage_load_to_lwram("HBHOBJ.PAK", (void *)P6_HW_OBJANIMPAK,
                                           P6_HW_OBJANIMPAK_CAP);
        if (n > 0)
            p6_w_objapk_bytes = n;
    }
    // Task #311: stage the 2 GHZCutscene scene-object sheets BEFORE the scene load.
    // The scene's StageLoad runs LoadSpriteAnimation -> LoadSpriteSheet for
    // AIZKingClaw (GHZCutscene/Objects.gif via GHZCutscene/Claw.bin), Platform
    // (same sheet) and PhantomRuby (Global/PhantomRuby.gif); on Saturn an unstaged
    // sheet's LoadSpriteSheet returns -1 WITHOUT creating a surface
    // (Sprite.cpp:983-992) -> the anims carry sheetID -1 -> wrong-surface
    // fragments (MEASURED builds 14-17: stage witnesses perfect at the OLD
    // post-load site yet zero surfaces carried the hashes; the VDP1 cmd-list
    // decode showed the fragments drawing from slot-cache textures). Staging HERE
    // (GFS idle, same as the HBHOBJ.PAK load above) makes those LoadSpriteSheet
    // calls FindSlot-HIT -> real surfaces with dims+hash+slot -> the arm_env bind
    // loop binds them (P6_VDP1_NSHEETS=14 has room). Scratch = the AIZ-BG LWRAM
    // window (free pre-load; the sky loads reuse it after the arm below).
    {
        unsigned char *sbuf = (unsigned char *)0x22480000u;
        RETRO_HASH_MD5(ph);
        int sn = rsdk_storage_load_to_lwram("GHCOBJ.SHT", sbuf, 0x10000);
        p6_w_ghcobj_sn = sn;
        if (sn > 0) {
            int32 slot = SaturnSheet_Stage((const void *)sbuf, (uint32)sn);
            p6_w_ghcobj_slot = slot;
            if (slot >= 0) {
                GEN_HASH_MD5("GHZCutscene/Objects.gif", ph);
                SaturnSheet_SetHash(slot, (const uint32 *)ph);
            }
        }
        sn = rsdk_storage_load_to_lwram("RUBYOBJ.SHT", sbuf, 0x10000);
        p6_w_rubyobj_sn = sn;
        if (sn > 0) {
            int32 slot = SaturnSheet_Stage((const void *)sbuf, (uint32)sn);
            p6_w_rubyobj_slot = slot;
            if (slot >= 0) {
                GEN_HASH_MD5("Global/PhantomRuby.gif", ph);
                SaturnSheet_SetHash(slot, (const uint32 *)ph);
            }
        }
        // Task #312 (#311b): stage the HUD + placed-Ring sheets too. DECOMP-VERIFIED
        // both should RENDER in this scene: the census places HUD:1 + Ring:6 in
        // GHZCutscene/Scene1.bin; GHZCutsceneST.c suppresses NEITHER (Create only
        // sets SceneInfo->timeEnabled=false -- timer counting, not drawing -- and
        // HUD_Create sets visible=true unconditionally; no HUD_MoveOut caller in
        // CutsceneSeq/CutsceneRules/GHZCutsceneST). Unstaged, their anims carried
        // sheetID -1 -> the mech-6 wrap draws (RED: p6_w_ringsheet/-1,
        // p6_w_dispsurf/-1 in _ring28.mcs; wrap255=1442). Staged HERE, the arm_env
        // nudges (this file ~:2880) FindSlot-HIT -> surfaces -> the bind loop binds
        // them (bind demand ~5 <= P6_VDP1_NSHEETS 14, no bump needed).
        sn = rsdk_storage_load_to_lwram("ITEMS.SHT", sbuf, 0x10000);
        p6_w_itemsht_sn = sn;
        if (sn > 0) {
            int32 slot = SaturnSheet_Stage((const void *)sbuf, (uint32)sn);
            p6_w_itemsht_slot = slot;
            if (slot >= 0) {
                GEN_HASH_MD5("Global/Items.gif", ph);
                SaturnSheet_SetHash(slot, (const uint32 *)ph);
            }
        }
        sn = rsdk_storage_load_to_lwram("DISPLAY.SHT", sbuf, 0x10000);
        p6_w_dispsht_sn = sn;
        if (sn > 0) {
            int32 slot = SaturnSheet_Stage((const void *)sbuf, (uint32)sn);
            p6_w_dispsht_slot = slot;
            if (slot >= 0) {
                GEN_HASH_MD5("Global/Display.gif", ph);
                SaturnSheet_SetHash(slot, (const uint32 *)ph);
            }
        }
    }
    p6_scene_load_and_arm();
#ifndef P6_GHZCUT_NOFIX
    // Task #309 #2b: render the GHZ "BG Outside" sky as VDP2 NBG0 behind the FG.
    // HERE (after p6_scene_load_and_arm returns) GFS is IDLE -- the pack + scene read
    // released -- so the loose load works, exactly like the HBHOBJ.PAK load above.
    // (MEASURED: the same load INSIDE p6_scene_load_and_arm returned 0 -> both
    // P6_GFS_OPEN_MAX=2 handles busy; and the per-frame arm in p6_frontend_frame's AIZ
    // present block NEVER runs for GHZCut -> control-flow markers aizblk/beforeif/
    // ghzbranch all 0, the frame returns at the ENGINESTATE_LOAD handoff ~6718.) The
    // arm just above presented NBG1 (FG, persists); arming NBG0 now makes it the LAST
    // slScrAutoDisp so both planes stick. Scratch = the AIZ-BG LWRAM window (free --
    // direct-boot skips p6_aiz_reload).
    {
        unsigned char *bchr = (unsigned char *)0x22480000u;
        unsigned char *bmap = (unsigned char *)0x22490000u;
        unsigned char *bpal = (unsigned char *)0x22494000u;
        int nchr = rsdk_storage_load_to_lwram("AGHCBG.CHR", bchr, 0x10000);
        int nmap = rsdk_storage_load_to_lwram("AGHCBG.MAP", bmap, 0x4000);
        int npal = rsdk_storage_load_to_lwram("AGHCBG.PAL", bpal, 0x200);
        if (nchr > 0 && nmap > 0 && npal > 0) {
            p6_vdp2_ghzcut_bg_upload((const unsigned short *)bchr, nchr / 2,
                                     (const unsigned short *)bpal, npal / 2,
                                     (const unsigned short *)bmap, nmap / 2);
            p6_vdp2_ghzcut_bg_frame(0);
        }
    }
#endif
    // #311 fetch-vs-cache bisect: FetchRect the claw CHAIN rect (258,492,16,16 --
    // spans the two ODD-offset tail bands 30/31 of GHCOBJ.SHT) TWICE, djb2 each
    // into witnesses. The cart blob is byte-perfect (measured) and the offline
    // band decode is byte-exact -- MATCH vs the offline djb2 = fetch correct on
    // hardware (corruption is in the VDP1 cache/upload layer); MISMATCH = the
    // banded fetch path (odd-lead 16-bit copy / scratch) breaks on hardware.
    {
        uint8 *fb = (uint8 *)0x22498000u; /* past the sky staging buffers */
        if (p6_w_ghcobj_slot >= 0) {
            int32 ok = SaturnSheet_FetchRect(p6_w_ghcobj_slot, 258, 492, 16, 16, fb);
            uint32 hh = 5381;
            for (int32 i = 0; i < 256; ++i) hh = ((hh << 5) + hh) ^ fb[i];
            p6_w_ghc_fetch1 = ok ? (int32)hh : -1;
            ok = SaturnSheet_FetchRect(p6_w_ghcobj_slot, 258, 492, 16, 16, fb);
            hh = 5381;
            for (int32 i = 0; i < 256; ++i) hh = ((hh << 5) + hh) ^ fb[i];
            p6_w_ghc_fetch2 = ok ? (int32)hh : -1;
        }
    }
    p6_ghz_continuous_armed = 1; // reuse the continuous-armed flag (drives the tick)
    // A1-analog: the engine LoadScene of folder GHZCutscene finished (LoadSceneFolder
    // re-strcpy's currentSceneFolder to the loaded folder during the arm). Reuse the AIZ
    // witnesses (gc-rooted under P6_AIZ_TEST) so the capture is measurable.
    p6_w_aiz_loaded   = (strcmp(currentSceneFolder, "GHZCutscene") == 0) ? 1 : 0;
    p6_w_aiz_objcount = (int32)sceneInfo.classCount;
    {
        int32 n = 0;
        for (int32 i = 0; i < LAYER_COUNT; ++i)
            if (tileLayers[i].layout) ++n;
        p6_w_aiz_nlayers = n;
    }
}
#endif // P6_GHZCUT_DIRECTBOOT
#endif // P6_AIZ_TEST
#endif // P6_FRONTEND_MENU

// =============================================================================
// CP4 (Task #265): p6_frontend_frame -- the GENERIC per-frame tick for a non-GHZ
// UI scene. p6_ghz_frame is GHZ-FG-present-specific (p6_present_kick/join the
// VDP2 NBG1 band store, the dual-SH2 fork, the band-crossing logic) -- none of
// which applies to a UI scene with no FG layer. This runs only the engine's own
// per-frame core: ProcessInput -> ProcessObjects -> ProcessObjectDrawLists (the
// latter emits the UIPicture VDP1 sprite commands jo's loop then swaps), plus the
// Ring->overlay global rewire seam and the overlay witness call (so the front-end
// classID witnesses get written). Bumps p6_w_cont_frames (gate E5 == reached
// ENGINESTATE_REGULAR + ticking). The ENGINESTATE_LOAD dispatch is also handled
// so LogoSetup_State_NextLogos' RSDK.LoadScene() (advance to Title) re-arms.
// =============================================================================
// COMPREHENSIVE BACKEND-PARITY WITNESSES (2026-07-20, user mandate "fix the oracle
// to detect every defect blind"). The Saturn runs verbatim decomp game LOGIC, so
// the engine's WRAM state already matches the reference -- EVERY port bug lives in
// the render/audio BACKEND (WRAM->CRAM->VDP for colour, engine->SCSP/CD-DA for
// sound), which netmem CANNOT read (CRAM/VRAM are on the B-bus, not SYSTEM_RAM).
// The SH-2 CAN read them, so we expose a COMPLETE fingerprint of the composited
// CRAM here for the oracle to diff blind: a djb2 hash (temporal flash = hash
// oscillation), a magenta/pink count (the classic wrong-colour / CRAM-collision
// artifact -- pink flashes, ANY bank, ANY scene), and a nonzero count (blank/black
// detector). ~1024 uncached 16-bit CRAM reads ~= 0.05 ms, negligible even in the
// ~10 fps chain. Front-end/chain ONLY (this whole fn is P6_FRONTEND_LOGOS-gated)
// so plain Green Hill Zone is byte-identical. Saturn CRAM = BGR555, MSB set:
// R5=c&0x1F, G5=(c>>5)&0x1F, B5=(c>>10)&0x1F -> magenta = R & B high, G low.
__attribute__((used)) int32 p6_w_cram_hash    = 0;
__attribute__((used)) int32 p6_w_cram_magenta = 0;   // instantaneous magenta count this frame
__attribute__((used)) int32 p6_w_cram_nonzero = 0;
// GAP-5/FN-1 fix (adversarial-QA 2026-07-20): (1) the CRAM is 2048 u16 entries
// (4 KB); the old i<1024 loop covered only the FIRST HALF -> it was blind to the
// VDP1 SPRITE palette banks at CRAM[1024..2047] (exactly where the AIZ Tornado
// pink-wing bank-collision lives). Loop the FULL 2048. (2) The oracle samples at
// ~1 Hz but a pink FLASH can be <1 s -> Nyquist violation, the 1 Hz read misses
// it entirely. Fix with two MONOTONIC accumulators the oracle diffs by DELTA
// (delta>0 over any window == a flash happened in that window, regardless of the
// oracle's sample rate): p6_w_cram_magenta_max (peak magnitude ever seen) and
// p6_w_cram_magenta_frames (count of frames whose magenta exceeded the floor).
__attribute__((used)) int32 p6_w_cram_magenta_max    = 0;  // monotonic peak magenta
__attribute__((used)) int32 p6_w_cram_magenta_frames = 0;  // monotonic #frames mag>MAG_FLOOR
static void p6_cram_witness(void)
{
    volatile unsigned short *cram = (volatile unsigned short *)0x25F00000u;
    unsigned int h = 5381u;
    int mag = 0, nz = 0;
    for (int i = 0; i < 2048; ++i) {
        unsigned short c = cram[i];
        h = ((h << 5) + h) ^ (unsigned int)c;
        if (c & 0x7FFFu)
            ++nz;
        int r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
        if (r >= 24 && b >= 24 && g <= 8)
            ++mag;
    }
    p6_w_cram_hash    = (int32)h;
    p6_w_cram_magenta = mag;
    p6_w_cram_nonzero = nz;
    if (mag > p6_w_cram_magenta_max)
        p6_w_cram_magenta_max = mag;
    if (mag > 4)                              // MAG_FLOOR = 4 (mirrors oracle D9)
        ++p6_w_cram_magenta_frames;
}
// GAP-A1 fix (adversarial-QA 2026-07-20): the port runs the VERBATIM decomp
// audio logic, so gameplay objects DO call PlaySfx -> the engine channels[]
// array shows CHANNEL_SFX-armed slots with real soundIDs. But there is NO Saturn
// mix callback draining channels[] to the SCSP (only p6_snd_play() reaches the
// SCSP, with ONE fixed MenuBleep buffer). So gameplay SFX are effectively silent
// AND, until now, UNDETECTABLE. This witness proves the divergence blind: it
// samples channels[] every ENGINE frame (60 Hz -> Nyquist-proof, unlike the
// oracle's 1 Hz netmem read) and accumulates MONOTONIC counters the oracle diffs
// by delta. p6_w_sfx_armed_frames rising while p6_w_snd_plays stays flat ==
// "gameplay requested SFX the SCSP never played" == dead SFX. p6_w_sfx_last_id
// carries the most-recent gameplay soundID so a spot check can name it.
__attribute__((used)) int32 p6_w_sfx_armed_frames = 0;  // monotonic #frames a non-bleep SFX ch was armed
__attribute__((used)) int32 p6_w_sfx_arm_events   = 0;  // monotonic #new-soundID arm transitions
__attribute__((used)) int32 p6_w_sfx_last_id      = -1;  // most-recent gameplay SFX soundID
__attribute__((used)) int32 p6_w_stream_frames    = 0;  // monotonic #frames a STREAM (BGM) ch was armed
static int16 s_sfx_prev_id[CHANNEL_COUNT];
static void p6_audio_witness(void)
{
    int armed = 0, streamed = 0;
    for (int c = 0; c < CHANNEL_COUNT; ++c) {
        uint8 st = channels[c].state;
        int16 sid = channels[c].soundID;
        if (st == CHANNEL_SFX) {
            ++armed;
            p6_w_sfx_last_id = (int32)sid;
            if (sid != s_sfx_prev_id[c])          // new sound landed on this slot
                ++p6_w_sfx_arm_events;
        }
        if (st == CHANNEL_STREAM)
            ++streamed;
        s_sfx_prev_id[c] = (st == CHANNEL_SFX) ? sid : (int16)-1;
    }
    if (armed > 0)
        ++p6_w_sfx_armed_frames;
    if (streamed > 0)
        ++p6_w_stream_frames;
}
static void p6_frontend_frame(void)
{
    currentScreen = &screens[0];
    for (int32 g = 0; g < DRAWGROUP_COUNT; ++g)
        engine.drawGroupVisible[g] = true;
#if defined(P6_DIRECT_VDP1)
    // F1-R1 (MEASURED, landing leg ~70/30 sprite flap, montage _f1_montage.png +
    // _f1_land2.mcs forensics): in legs where SGL's slave sprite pipeline is ALIVE
    // (post-handoff GHZ; the title's is #313-dead so the title never flaps), slSynch
    // re-transfers the now-permanently-EMPTY sprite plan (preamble + END at 0x60)
    // every ENGINE frame, stomping the vblank trampoline until the next vblank
    // re-patch -- a per-engine-frame race the sprites lose ~30% of vblanks. This
    // callback runs right after slSynch (see the #313 race-witness note below), so
    // the stomp DMA is complete by entry: re-patch 0x60 HERE too, deterministically
    // after the stomp. Latch how often we caught it stomped (the race witness --
    // reads 0x8000 END at entry only when SGL re-transferred since the last patch).
    {
        extern volatile unsigned int p6_dl_link;
        if (p6_dl_link) {
            volatile unsigned short *cmd = (volatile unsigned short *)0x25C00000u;
            if (cmd[0x60 / 2] == 0x8000)
                ++p6_w_dl_stomps;
            cmd[0x60 / 2] = 0x100A;
            cmd[0x62 / 2] = (unsigned short)p6_dl_link;
            cmd[0x64 / 2] = 0; cmd[0x66 / 2] = 0;
            cmd[0x68 / 2] = 0; cmd[0x6A / 2] = 0;
            cmd[0x6C / 2] = 160; cmd[0x6E / 2] = 120;
        }
    }
#endif
#if defined(P6_GHZCUT_BOOT) || defined(P6_AIZ_TEST)
    // F2a tear killer, frame-top half (the F1-R1 double-patch pattern): this
    // callback runs right after slSynch, so any register-image stomp from the
    // just-finished transfer is already down -- replay the published
    // composition set now; the vblank hook replays it again at vblank.
    p6_vdp2_mirror_apply();
#endif
#if defined(P6_FRONTEND_TITLE)
    // #313 race witness: this callback runs right after jo's slSynch (core.c:632
    // loop). At this instant the SGL slave mailbox should be FULLY CONSUMED
    // (consumer GBR+0x44 == producer GBR+0x48, both reset to CommandBuf base by
    // slSynch). If they differ, the slave was still consuming when slSynch reset
    // the producer -- the frame's sprite commands got orphaned/overwritten (the
    // die-off mechanism candidate). Latch the lag + count lagging frames.
    {
        volatile unsigned long *cons = (volatile unsigned long *)0x060FFC44u;
        volatile unsigned long *prod = (volatile unsigned long *)0x060FFC48u;
        long lag = (long)*prod - (long)*cons;
        p6_w_slave_lag_last = (int32)lag;
        if (lag != 0) { ++p6_w_slave_lag_frames; p6_w_slave_lag_max = (lag > p6_w_slave_lag_max || -lag > p6_w_slave_lag_max) ? (lag > 0 ? (int32)lag : (int32)-lag) : p6_w_slave_lag_max; }
    }
    // #313: an slInitSprite dead-state re-arm was tried here (predicate: landed
    // grew while GBR+0xA4 plan count == 0 for 3 frames) -- it NEVER fired
    // (p6_w_sgl_reinit=0): the plan count reads NONZERO at frame start even in
    // the dead state (the count is a mid-frame transient). Removed; see the
    // frame-end forensic note + memory sgl-slave-sprite-pipeline-internals.md.
#endif
#if defined(P6_GHZCUT_BOOT)
    // #311 mech-4 v3: arm the per-frame bucket-demand latch. p6_ghz_frame calls
    // this at its top (STEP B #246) but the front-end frame never did -- MEASURED
    // (_ring24.mcs): p6_w_vdp1_cmds=1135 cumulative + fmax all 0 = the latch never
    // ran here. With it armed, p6_w_buckN_fmax holds max misses/frame per bucket;
    // fmax > the bucket's slot count == intra-frame slot reuse (the M1b garble
    // class, mech-3 recurrence). GHZCUT-gated: menu/AIZ/title binaries unchanged.
    p6_vdp1_perf_reset();
    // #311 mechanism 5: keep the VDP1 sprite palette (CRAM bank1) tracking the
    // LIVE engine palette every frame -- PC sprites see RotatePalette cycles;
    // the one-shot first-bind mirror froze bank1 (MEASURED 65/256 stale entries,
    // frozen-black [255] = the solid-black dig-site strips). Mirrors the
    // engine's own per-frame bank0 flush. Gate: qa_ghzcut_pal255.py.
    p6_vdp1_pal_remirror((const unsigned short *)fullPalette[0]);
#endif
#if defined(P6_FRONTEND_MENU)
    // M1b AUTH-GATE FLIP (one-shot, BEFORE ProcessObjects runs the scene's first
    // MenuSetup StaticUpdate -> InitAPI). The overlay set s_ovl.menu_apic_init_fn at
    // its entry (run inside p6_menu_reload's load_and_arm), so the pointer is valid
    // here. Installs the real APICallback struct + globals->noSave=true so InitAPI
    // returns true -> MenuSetup_Initialize wires the UIControl row tree (M6).
    //
    // R1 FIX (2026-07-17, BLACK MENU regression): gate the one-shot on
    // currentSceneFolder=="Menu". WHY (MEASURED, live qa_netmem Menu window of the
    // CHAIN build): p6_w_fxfade_timer stuck at 500, p6_w_ghzcut_fade=500 -> full
    // black VDP2 wash. The FXFade (Menu/Scene1.bin slot 8: timer=512 speedOut=12)
    // ran exactly one FadeIn tick (512-12=500) then froze because
    // MenuSetup->initializedAPI never latched -> InitAPI (MenuSetup.c:414-415) kept
    // force-writing timer=512. Root: this one-shot is folder-agnostic, and
    // s_ovl.menu_apic_init_fn is non-NULL from overlay entry, so in the CHAIN
    // (boot=Logos, then Logos->Title->Menu) it fired during LOGOS frame 1 and
    // latched s_menu_apic_done; the Logos-time APICallback/noSave writes did not
    // survive the intervening TitleSetup + MenuSetup StageLoad inits, so by Menu
    // time InitAPI read authStatus!=STATUS_OK/noSave!=true -> returned false forever
    // -> initializedAPI never latched -> fade never rolled out -> BLACK. The
    // PLAIN-MENU boot was unaffected only because p6_menu_reload runs FIRST there
    // (p6_io_main.cpp ~:9850) so folder=="Menu" on the first tick. Folder-gating
    // makes the CHAIN match that plain-menu timing: apic_init cannot fire during
    // Logos/Title and DOES fire on the first Menu frame, when MenuSetup's first
    // StaticUpdate -> InitAPI reads the just-installed offline no-save state and
    // latches. Gate qa_menu_fade.py (RED->GREEN). Plain-menu byte-identical: the
    // first Menu tick still has folder=="Menu". (currentSceneFolder is re-strcpy'd
    // to the loaded folder inside LoadSceneFolder, Scene.cpp:148.)
    {
        static int32 s_menu_apic_done = 0;
        if (!s_menu_apic_done && s_ovl.menu_apic_init_fn
            && currentSceneFolder && !strcmp(currentSceneFolder, "Menu")) {
            s_ovl.menu_apic_init_fn();
            s_menu_apic_done = 1;
        }
    }
#endif
    // Point the pack Ring/Animals globals at the overlay's registered objects
    // (the F.3/#235 seam) -- harmless on the UI scene (no Ring/Animals placed).
    if (s_ovl.staticvars_slot && *(void **)s_ovl.staticvars_slot)
        Ring = *(void **)s_ovl.staticvars_slot;
    if (s_ovl.animals_slot && *(void **)s_ovl.animals_slot)
        Animals = *(void **)s_ovl.animals_slot;
    // Batch 3: ItemBox/Debris seams (harmless on front-end scenes -- no entities).
    if (s_ovl.itembox_slot && *(void **)s_ovl.itembox_slot)
        ItemBox = *(void **)s_ovl.itembox_slot;
    if (s_ovl.debris_slot && *(void **)s_ovl.debris_slot)
        Debris = *(void **)s_ovl.debris_slot;
    // StarPost port (2026-07-17): same seam on the front-end tick -- the AIZ
    // overlay TUs (AIZTornado/AIZTornadoPath) and the chain GHZ leg then read the
    // ONE registered instance (fresh-boot postIDs 0 == the M3.1 zeroed-instance
    // semantics; see p6_closure_edge.c).
    if (s_ovl.starpost_slot && *(void **)s_ovl.starpost_slot)
        StarPost = *(void **)s_ovl.starpost_slot;

    // CP4 SCOPE: LogoSetup auto-advances (RSDK.LoadScene -> ++listPos -> Title)
    // once all 4 logos have displayed. The Title scene's objects are NOT ported
    // yet, so advancing there would just blank the screen. For the CP4 keystone +
    // splash screenshot we HOLD on the Logos scene: swallow the transition request
    // (reset state back to REGULAR so the scene keeps ticking + the last logo stays
    // on screen). When Title/Menu are ported, this guard lifts and the front-end
    // chains Logos -> Title by the same p6_scene_load_and_arm path the GHZ
    // transition already uses (proven by F.1/F.2). The witness still records that
    // the advance FIRED (p6_w_transitions) so the chain readiness is measurable.
#if defined(P6_FRONTEND_CHAIN) && defined(P6_AIZ_TEST)
    // FULL-CHAIN (task #314) Menu -> AIZ dwell-nudge: the decomp start-game path is
    // MenuSetup_SaveSlot_ActionCB (MenuSetup.c:1121) -> SetScene("Cutscenes",
    // "Angel Island Zone") + LoadScene, fired by confirming the No-Save slot. The
    // M2 S3 investigation left that confirm chain open (UISaveSlot_ProcessButtonCB
    // does not reach State_Selected on Saturn yet), so the hands-free chain fires
    // the SAME destination hop after a visible menu dwell (~10 s at the menu's
    // ~25 fps): the identical folder-change choreography as the Logos->Title and
    // Title->Menu seams (VDP1 handle map + CRAM bank-1 mirror reset) then
    // p6_aiz_reload() -- the same select+load+arm the AIZ boot flavor uses.
    // CHAIN-only: the plain Menu flavor keeps waiting for real input.
    {
        static int32 s_chain3_fired = 0;
        static int32 s_menu_ticks   = 0;
        if (!s_chain3_fired && !strcmp(currentSceneFolder, "Menu")) {
            if (++s_menu_ticks >= 250) {
                s_chain3_fired = 1;
                for (int32 i = 0; i < SURFACE_COUNT; ++i)
                    p6_vdp1HandleBySurface[i] = -1;
                p6_vdp1HandlesInit = false;
                p6_vdp1_frontend_pal_reset();
                // SEGMENT D / rank23 (#318): reset the TITLE-leg VDP2 latches so the
                // title backdrop/island/cloud code (gated on p6_w_title_backdrop_done,
                // ~:7371) does NOT run during the AIZ intro and fight the AIZ FG present
                // -> the ~30% black flicker (#302). The Title set these; only the GHZ
                // handoff reset island_armed, so they leaked through the whole
                // Menu+AIZ+GHZCutscene arc. Zero all three at this Menu->AIZ seam.
                p6_w_title_backdrop_done = 0;
                p6_w_title_island_armed  = 0;
                p6_w_title_clouds_armed  = 0;
                p6_w_chain3_fired = 1;
                p6_aiz_reload();
                return;
            }
        }
    }
#endif
    if (sceneInfo.state == ENGINESTATE_LOAD) {
        ++p6_w_transitions;
#if defined(P6_FRONTEND_MENU)
        // M2 (qa_engine_menu_start.py) S3: the START-GAME SetScene fired. The select
        // chain (Mania Mode -> new save slot) reaches MenuSetup_SaveSlot_ActionCB
        // (MenuSetup.c:1121) which calls RSDK.SetScene("Cutscenes","Angel Island Zone")
        // + RSDK.LoadScene() -> sceneInfo.state = ENGINESTATE_LOAD. SetScene already
        // updated sceneInfo.activeCategory + listPos (Scene.cpp:1530-1553); latch the
        // loaded scene's folder tag + category HERE (this is the one frame they hold
        // the AIZ target before the frontend swallows the load below). STICKY on the
        // AIZ tag (0x4149 'AI') so a later (non-AIZ) load cannot clear the evidence.
        if (p6_w_menu_startscene_tag != 0x4149) {
            const char *sf = sceneInfo.listData[sceneInfo.listPos].folder;
            int32 tag = (int32)(((uint32)(uint8)sf[0] << 8)
                                | (uint32)(uint8)(sf[0] ? sf[1] : 0));
            p6_w_menu_startscene_tag = tag;
            p6_w_menu_start_cat      = (int32)sceneInfo.activeCategory;
            p6_w_menu_start_listpos  = (int32)sceneInfo.listPos;
        }
#endif
#if defined(P6_FRONTEND_CHAIN)
        // CP5c (Task #270): the FLOW chain. When the auto-advance fires FROM the
        // Logos scene, do NOT swallow -- carry the front-end to the Title screen by
        // an EXPLICIT folder-select + load+arm of "Title" (p6_title_reload), the
        // robust path that does not depend on the Logos/Title scene-list adjacency.
        // Once (static guard) -- a later advance toward the (unported) Menu still
        // swallows (currentSceneFolder != "Logos" below).
        //
        // MEASURED ROOT CAUSE (CP5c first build, savestate _title_chain.mcs): the
        // discriminator MUST be currentSceneFolder, NOT sceneInfo.listData[listPos].
        // LogoSetup_State_NextLogos (LogoSetup.c:130-131) does `++SceneInfo->listPos`
        // BEFORE `RSDK.LoadScene()`, so at this ENGINESTATE_LOAD point listPos already
        // points at the NEXT scene (the destination) -- listData[listPos].folder is the
        // destination, not "Logos". The first build checked listData[listPos] and never
        // fired (transitions=8951, chain_fired=0, folder stayed 'Lo'). currentSceneFolder
        // still holds the CURRENTLY-LOADED scene's folder ("Logos") -- it is only
        // re-strcpy'd inside LoadSceneFolder (Scene.cpp:148), which runs LATER, during the
        // p6_title_reload load. So "we are leaving Logos" == currentSceneFolder=="Logos".
        // This is also the order-independent test the design requires: p6_title_reload
        // then scans for folder "Title" by NAME and OVERWRITES the engine's ++listPos.
        static int32 s_chain_fired = 0;
        const char  *fnow = currentSceneFolder; // the CURRENTLY-LOADED folder (still "Logos")
        if (!s_chain_fired && !strcmp(fnow, "Logos")) {
            s_chain_fired         = 1;
            p6_w_chain_folder_pre = (int32)(((uint32)(uint8)fnow[0] << 8)
                                            | (uint32)(uint8)(fnow[0] ? fnow[1] : 0));
            // RESIDENCY FIX (chain-specific, MEASURED hazard -- see cp5c checklist):
            // the VDP1 surface->handle map is init-once (p6_vdp1HandlesInit) and is
            // NEVER reset on a folder change; the arm_env bind loop SKIPS any surface
            // whose handle is already >=0 (the #250 same-folder-persist guard). On
            // THIS Logos->Title FOLDER CHANGE, LoadSceneFolder runs ClearGfxSurfaces
            // (scope->NONE) and Title's LoadSceneAssets re-populates the SAME surface
            // indices the Logos UIPicture used -- but their stale "bound" handle would
            // make the bind loop skip them -> the Title logo/Sonic surfaces stay
            // UNBOUND -> the title renders blank. Clear the map (and re-arm the
            // init flag) HERE, the one moment the front-end changes folder, so the
            // arm_env loop inside p6_title_reload re-binds Title's surfaces fresh.
            // Front-end CHAIN only: a same-folder GHZ reload never reaches this.
            for (int32 i = 0; i < SURFACE_COUNT; ++i)
                p6_vdp1HandleBySurface[i] = -1;
            p6_vdp1HandlesInit = false; // arm_env re-inits + re-binds every surface
            // CP5c CRAM-PALETTE FIX (MEASURED: chain CRAM bank1 held the Logos
            // palette in 144/256 entries vs the golden direct-boot Title; fullPalette[0]
            // was byte-identical -- the engine palette was right, only the CRAM mirror
            // was stale). p6_pal_mirror (the sole CRAM-bank-1 writer, p6_vdp1.c) runs
            // ONLY on the first-ever bind (s_sheet_count==0); the Logos bind already
            // consumed that, so Title's re-bind never re-mirrored its palette. Re-arm
            // the sheet/slot state HERE so the Title's first re-bind below (forced by
            // the handle-table reset above) re-runs p6_pal_mirror with Title's
            // fullPalette[0] -> CRAM bank 1 carries the correct Title sprite palette.
            // This is the CRAM twin of the VDP1-handle reset (geometry was already
            // fixed; this fixes the COLORS). Front-end CHAIN only.
            p6_vdp1_frontend_pal_reset();
            p6_w_chain_fired       = 1;
            p6_w_chain_listpos_adv = (int32)sceneInfo.listPos; // engine's ++ destination (Logos+1)
            p6_title_reload();          // select + load+arm "Title" (re-binds surfaces)
            p6_w_chain_listpos_title = (int32)sceneInfo.listPos; // Title listPos from the by-name scan
            // p6_title_reload set sceneInfo.state = ENGINESTATE_REGULAR via InitObjects;
            // RETURN so this frame does NOT also run the (now-Title) ProcessObjects
            // pass on a half-swapped state -- the NEXT p6_frontend_frame ticks Title.
            return;
        }
#if defined(P6_FRONTEND_MENU)
        // FULL-CHAIN (task #314): the Title -> Menu seam, the exact mirror of the
        // Logos -> Title fire above. The verbatim decomp TitleSetup fires
        // RSDK.SetScene("Presentation","Menu Select") + LoadScene on the Start
        // press (TitleSetup_State_Title -> ReturnToMenu), landing here at
        // ENGINESTATE_LOAD with currentSceneFolder still "Title" (the same
        // discriminator rationale as the Logos fire: listPos already points at
        // the DESTINATION; currentSceneFolder is only re-strcpy'd inside
        // LoadSceneFolder, which runs during p6_menu_reload below). Same
        // residency choreography as the Logos->Title fire: the VDP1 handle map +
        // the CRAM bank-1 mirror are folder-stale -> reset both so the Menu's
        // arm re-binds + re-mirrors fresh. Once (static guard).
        static int32 s_chain2_fired = 0;
        if (!s_chain2_fired && !strcmp(currentSceneFolder, "Title")) {
            s_chain2_fired = 1;
            for (int32 i = 0; i < SURFACE_COUNT; ++i)
                p6_vdp1HandleBySurface[i] = -1;
            p6_vdp1HandlesInit = false;
            p6_vdp1_frontend_pal_reset();
            p6_w_chain2_fired = 1;
            p6_menu_reload();           // select + load+arm "Menu" (re-binds surfaces)
            return;
        }
#endif
#endif
#if defined(P6_GHZCUT_BOOT)
        // Task #309 gate-2 (LIVE SEAM, AIZ->GHZCutscene): when LEAVING the AIZ intro
        // cutscene, do NOT swallow -- carry the front-end to the GHZCutscene arrival
        // cutscene. The AIZ intro's final beat AIZSetup_Cutscene_LoadGHZ (AIZSetup.c:900)
        // did RSDK.SetScene("Cutscenes","Green Hill Zone") + RSDK.LoadScene(); SetScene
        // resolved (Scene.cpp, via GameConfig) folder GHZCutscene id 1, so sceneInfo.listPos
        // already points at GHZCutscene/Scene1 here. Just load+arm it -- LoadSceneFolder
        // inside re-strcpy's currentSceneFolder to "GHZCutscene" (Scene.cpp:148) -> the FG
        // tileset (GHZCUTIL/GHZCUPAL via p6_til_name) + the GHZCutscene FG cell upload +
        // HBHOBJ.SHT/HBHPAL staging all run from p6_scene_load_and_arm's runtime
        // folder=="GHZCutscene" branches (the SAME code the direct-boot p6_ghzcut_reload
        // reaches via p6_scene_load_and_arm), and the per-frame FG present + fade are keyed
        // on P6_GHZCUT_BOOT + runtime folder -> the live arrival renders identically to the
        // direct-boot. THE ONE EXTRA STEP vs the GHZCutscene->GHZ branch below: pre-load the
        // Heavy anim pack HBHOBJ.PAK into P6_HW_OBJANIMPAK BEFORE the arm runs StageLoad
        // (CutsceneHBH_LoadSprites' LoadSpriteAnimation resolves from the resident packs
        // first), exactly as p6_ghzcut_reload does. Discriminator = currentSceneFolder
        // (still "AIZ"; re-strcpy'd only inside the load below). Once (static guard). RETURN
        // so this frame does not also tick the now-GHZCutscene ProcessObjects on a
        // half-swapped state (mirrors the CHAIN / GHZCutscene->GHZ returns).
        {
            static int32 s_aiz_ghzcut_seam_fired = 0;
            if (!s_aiz_ghzcut_seam_fired && !strcmp(currentSceneFolder, "AIZ")) {
                s_aiz_ghzcut_seam_fired = 1;
                // Task #309 gate-2 FOLLOW-UP (the LIVE render-clean fix): reset the FG
                // present's empty-cell blank-char override the AIZ scene left at 64.
                // ROOT CAUSE of the live-vs-direct-boot mismatch (MEASURED via
                // tools/_portspike/qa_ghzcut_liverender.py): p6_fg_blank_char_override is
                // a PERSISTENT global. The AIZ scene set it to 64 (p6_io_main.cpp:5610
                // p6_vdp2_aiz_blank_setup(64) -- AIZ's only-transparent CEL char, because
                // AIZ tile-0 is opaque orange). GHZCutscene is GHZ-THEMED: its tile-0 is
                // transparent (the GHZ convention) and its FG cell upload deliberately
                // does NOT call p6_vdp2_aiz_blank_setup (p6_io_main.cpp:5624-5631), so the
                // present must map empty FG cells (0xFFFF) to tile 0 (override == -1). The
                // DIRECT-BOOT (P6_GHZCUT_DIRECTBOOT) never ran AIZ -> override stayed -1 ->
                // empties -> char 0 -> BLACK sky (clean). The LIVE seam comes FROM AIZ ->
                // override==64 persisted -> GHZCutscene empties -> opaque GHZ-tile-64 -> the
                // CHECKERBOARD bleeding into the upper-screen black-sky region (the user-
                // reported "FG framed too high"). Resetting it here makes the live FG-plane
                // content BYTE-EQUAL the direct-boot's. (No VDP2 register write: this only
                // changes which CEL char index empty PND cells reference; the present's
                // slScrAutoDisp(NBG1ON|SPRON) + camera scroll are already identical -- the
                // SCYIN1=0x030C NBG1 scroll MATCHED in both states.)
                // Temporary RED/GREEN A-B knob for the qa_ghzcut_liverender RED->GREEN
                // proof: the fix is ON by default; build with -DP6_GHZCUT_NOFIX to
                // reproduce the pre-fix (RED) live render from the SAME source tree.
#ifndef P6_GHZCUT_NOFIX
                p6_fg_blank_char_override = -1;
#endif
                // VDP1 surface->handle map reset for the AIZ->GHZCutscene folder change
                // (the #250 same-folder-persist hazard): GHZCutscene's LoadSceneAssets +
                // the HBHOBJ.SHT stage re-populate surface indices the AIZ scene used;
                // stale "bound" handles would make the arm-env bind loop skip them.
                for (int32 i = 0; i < SURFACE_COUNT; ++i)
                    p6_vdp1HandleBySurface[i] = -1;
                p6_vdp1HandlesInit = false;
                // Pre-load HBHOBJ.PAK into the CART P6_HW_OBJANIMPAK window (the front-end
                // SKIPS GHZ GHZOBJ.PAK so it is free; AIZ left AIZOBJ.PAK there, now
                // overwritten by the Heavy pack for the GHZCutscene phase) BEFORE the arm's
                // StageLoad runs -- mirrors p6_ghzcut_reload. Without it CutsceneHBH's
                // LoadSpriteAnimation falls to the slow windowed-GFS path -> aniFrames=-1 ->
                // Heavies invisible. Zero engine-logic change (Animation.cpp reads
                // paks[1]=P6_HW_OBJANIMPAK / objapk_bytes).
                {
                    int n = rsdk_storage_load_to_lwram("HBHOBJ.PAK", (void *)P6_HW_OBJANIMPAK,
                                                       P6_HW_OBJANIMPAK_CAP);
                    if (n > 0)
                        p6_w_objapk_bytes = n;
                }
                // SEGMENT E / rank20 (#318): the LIVE AIZ->GHZCutscene chain seam staged
                // ONLY HBHOBJ.PAK, never the scene-object sheets -- so the dig-site claw +
                // crate (GHZCutscene/Objects.gif), the PhantomRuby, the HUD and the placed
                // Rings carried sheetID -1 (invisible / wrong-surface: the black frame).
                // Stage them HERE (GFS idle, before load+arm), verbatim from
                // p6_ghzcut_reload:6655-6708 (the direct-boot path the CHAIN never runs).
                {
                    unsigned char *sbuf = (unsigned char *)0x22480000u;
                    RETRO_HASH_MD5(ph);
                    int sn = rsdk_storage_load_to_lwram("GHCOBJ.SHT", sbuf, 0x10000);
                    p6_w_ghcobj_sn = sn;
                    if (sn > 0) {
                        int32 slot = SaturnSheet_Stage((const void *)sbuf, (uint32)sn);
                        p6_w_ghcobj_slot = slot;
                        if (slot >= 0) { GEN_HASH_MD5("GHZCutscene/Objects.gif", ph); SaturnSheet_SetHash(slot, (const uint32 *)ph); }
                    }
                    sn = rsdk_storage_load_to_lwram("RUBYOBJ.SHT", sbuf, 0x10000);
                    p6_w_rubyobj_sn = sn;
                    if (sn > 0) {
                        int32 slot = SaturnSheet_Stage((const void *)sbuf, (uint32)sn);
                        p6_w_rubyobj_slot = slot;
                        if (slot >= 0) { GEN_HASH_MD5("Global/PhantomRuby.gif", ph); SaturnSheet_SetHash(slot, (const uint32 *)ph); }
                    }
                    sn = rsdk_storage_load_to_lwram("ITEMS.SHT", sbuf, 0x10000);
                    p6_w_itemsht_sn = sn;
                    if (sn > 0) {
                        int32 slot = SaturnSheet_Stage((const void *)sbuf, (uint32)sn);
                        p6_w_itemsht_slot = slot;
                        if (slot >= 0) { GEN_HASH_MD5("Global/Items.gif", ph); SaturnSheet_SetHash(slot, (const uint32 *)ph); }
                    }
                    sn = rsdk_storage_load_to_lwram("DISPLAY.SHT", sbuf, 0x10000);
                    p6_w_dispsht_sn = sn;
                    if (sn > 0) {
                        int32 slot = SaturnSheet_Stage((const void *)sbuf, (uint32)sn);
                        p6_w_dispsht_slot = slot;
                        if (slot >= 0) { GEN_HASH_MD5("Global/Display.gif", ph); SaturnSheet_SetHash(slot, (const uint32 *)ph); }
                    }
#if defined(P6_FRONTEND_MENU)
                    // #324 GHZCutscene DrawLists hog (RED-gated, qa_drawcost_gate G3/G4):
                    // the sheets staged above stay BANDED through the whole cutscene --
                    // MEASURED (live chain forensic _drawprof_F1.jsonl 2026-07-09): 22.4
                    // FetchRect inflates/frame (claw + ruby + HUD + rings), draw cb
                    // bracket 124k FRT ticks (148 ms), 2.3 fps. The p6_scene_run promote
                    // block (p6_io_main.cpp:~4580) is BOOT-ONLY -- these slots are staged
                    // at THIS live seam and had no promotion site (resmask read 0x7F00 =
                    // only the boot+AIZ-seam promotes). Mirror the GHZ-landing seam
                    // pattern (:7436): reclaim the AIZ-leg residents (aizobj + player
                    // sheets, never drawn in the cutscene) then promote the cutscene
                    // working set. Budget: PLR(~64K) + HBH(512x432=221K) + GHC(512x512=
                    // 262K) + RUBY(256x128=32K) + ITEMS(256x128=32K) + DISPLAY(256x256=
                    // 64K) = ~675 KB < the 1.625 MB store. MakeResident is bounds-checked
                    // (overflow -> stays banded, no corruption). Scratch is wired since
                    // the boot block. Front-end only -> plain GHZ byte-identical.
                    {
                        SaturnSheet_ResReset();
#if defined(P6_FRAMEDIR)
                        // Stage-1 FRD (checklist sec 7, AIZ->GHZCut seam): the
                        // ResReset just killed the AIZ leg's FRD blobs' cart
                        // backing -- reset the registry + detach every sheet
                        // attachment (a stale frdSlot would serve wrong
                        // pixels, the #250 stale-binding class), then stage
                        // the cutscene leg's FRDs. PLR/HBH atlases have no
                        // FRD -> resident as before. MEASURED budget:
                        // 286,720 resident + 129,584 FRD = 416,304 B fits.
                        SaturnFrameDir_Reset();
                        p6_vdp1_frd_detach_all();
                        if (p6_w_plrsht_slot  >= 0) SaturnSheet_MakeResident(p6_w_plrsht_slot);
                        if (p6_w_hbh_slot     >= 0) SaturnSheet_MakeResident(p6_w_hbh_slot);
                        int32 frdGhc  = p6_frd_stage_file("GHCOBJ.FRD",  "GHZCutscene/Objects.gif");
                        int32 frdRuby = p6_frd_stage_file("RUBYOBJ.FRD", "Global/PhantomRuby.gif");
                        int32 frdItem = p6_frd_stage_file("ITEMS.FRD",   "Global/Items.gif");
                        int32 frdDisp = p6_frd_stage_file("DISPLAY.FRD", "Global/Display.gif");
                        if (p6_w_ghcobj_slot  >= 0 && frdGhc  < 0) SaturnSheet_MakeResident(p6_w_ghcobj_slot);
                        if (p6_w_rubyobj_slot >= 0 && frdRuby < 0) SaturnSheet_MakeResident(p6_w_rubyobj_slot);
                        if (p6_w_itemsht_slot >= 0 && frdItem < 0) SaturnSheet_MakeResident(p6_w_itemsht_slot);
                        if (p6_w_dispsht_slot >= 0 && frdDisp < 0) SaturnSheet_MakeResident(p6_w_dispsht_slot);
                        p6_frd_attach_bound(); // handles persisting across this seam
#else
                        if (p6_w_plrsht_slot  >= 0) SaturnSheet_MakeResident(p6_w_plrsht_slot);
                        if (p6_w_hbh_slot     >= 0) SaturnSheet_MakeResident(p6_w_hbh_slot);
                        if (p6_w_ghcobj_slot  >= 0) SaturnSheet_MakeResident(p6_w_ghcobj_slot);
                        if (p6_w_rubyobj_slot >= 0) SaturnSheet_MakeResident(p6_w_rubyobj_slot);
                        if (p6_w_itemsht_slot >= 0) SaturnSheet_MakeResident(p6_w_itemsht_slot);
                        if (p6_w_dispsht_slot >= 0) SaturnSheet_MakeResident(p6_w_dispsht_slot);
#endif
                    }
#endif
                }
                // #302 mechanism-A latch: the AIZ BG frame owned the display; hand it
                // back to the present across this folder change (the AIZ planes would
                // otherwise keep displaying VRAM the GHZCutscene load rewrites). The
                // GHZCutscene sky frame re-takes it on its first armed frame.
                p6_vdp2_bg_owns_disp = 0;
                p6_vdp2_mirror_reset(); // F2a: stop replaying the old scene's registers across the seam

                p6_scene_load_and_arm(); // load+arm the GHZCutscene scene the AIZ SetScene selected
                return;
            }
        }
        // Task #309 (handoff): when leaving the GHZCutscene arrival cutscene, do NOT
        // swallow -- carry the front-end to the playable Green Hill Zone. The cutscene's
        // final beat GHZCutsceneST_Cutscene_SetupGHZ1 (GHZCutsceneST.c:310-327) did
        // RSDK.SetScene("Mania Mode","") + Zone_StoreEntities + RSDK.LoadScene(); SetScene
        // with an empty scene name resolves to the FIRST Mania Mode scene = Green Hill Zone
        // 1 (Scene.cpp:1543-1565: listPos = category sceneOffsetStart), so sceneInfo.listPos
        // already points at GHZ Scene1 here. Just load+arm it -- LoadSceneFolder inside
        // re-strcpy's currentSceneFolder to "GHZ" (Scene.cpp:148) -> the handoff is live.
        // Discriminator = currentSceneFolder (the CURRENTLY-loaded folder, still
        // "GHZCutscene"; re-strcpy'd only inside the load below), the same order-independent
        // test the CP5c CHAIN uses. Once (static guard). The front-end SKIPS GHZANIM.PAK/
        // GHZOBJ.PAK (line 3834 #if !P6_FRONTEND_TITLE), so the playable GHZ renders its FG
        // tilemap but the Player/object sprites are unbound -- acceptable for the handoff
        // gate (folder=="GHZ"); a faithful playable GHZ from the front-end needs those packs
        // staged, a separate step. p6_scene_load_and_arm handles GHZ via its p6_isGHZ branch
        // (the P6_FRONTEND_LOGOS runtime strcmp). RETURN so this frame does not also tick the
        // now-GHZ ProcessObjects on a half-swapped state (mirrors the CHAIN return).
        {
            static int32 s_ghzcut_handoff_fired = 0;
            if (!s_ghzcut_handoff_fired && !strcmp(currentSceneFolder, "GHZCutscene")) {
                s_ghzcut_handoff_fired = 1;
                // Reset the VDP1 surface->handle map for the folder change (the same #250
                // same-folder-persist hazard the CHAIN handles): GHZ's LoadSceneAssets
                // re-populates surface indices the cutscene used; stale "bound" handles
                // would make the arm-env bind loop skip them.
                for (int32 i = 0; i < SURFACE_COUNT; ++i)
                    p6_vdp1HandleBySurface[i] = -1;
                p6_vdp1HandlesInit = false;
                // #302 mechanism-A latch: same handback as the AIZ->GHZCutscene seam --
                // the present owns the arm across the GHZ load; the landing sky frame
                // (folder=="GHZ" branch) re-takes it on its first armed frame.
                p6_vdp2_bg_owns_disp = 0;
                p6_vdp2_mirror_reset(); // F2a: stop replaying the old scene's registers across the seam
                // punch v2 item 1: clear the title-island latch so the playable
                // GHZ re-enables the FG-page->B0 vblank DMA (p6_fg_vblank gates
                // it on !p6_w_title_island_armed). The title set it to 1 and
                // nothing else resets it; leaving it set would permanently
                // disable the GHZ foreground streaming in the landing leg.
                p6_w_title_island_armed = 0;

                // F-LAND-POSE (user-reported flying-pose carryover; savestate-proven
                // _r1_land.mcs: p6_w_apk_bytes=0, both players' animator.frames stuck in
                // HBHOBJ.PAK 0x2276xxxx at the cutscene flight anim, because aniFrames=-1
                // makes every SetSpriteAnimation a no-op): pre-load the GHZ PLAYER anim
                // pack into the front-end CART base (Animation.hpp P6_FRONTEND_MENU arm,
                // 0x22744000 -- the WRAM-H window would clobber front-end .bss, #228)
                // BEFORE the arm runs Player_StageLoad, so LoadSpriteAnimation
                // ("Players/Sonic.bin") resolves from paks[0] and the carried players'
                // Ground state normalizes to ANI_IDLE on its first tick -- the exact
                // mirror of the AIZ->GHZCutscene seam's HBHOBJ.PAK preload above.
                // Budget: 68,800 B read ~0.5 s at 1x CD + 1 GFS seek, on a seam that
                // already loads the whole GHZ scene. Gate: qa_chain_player_pose.py.
                {
                    int n = rsdk_storage_load_to_lwram("GHZANIM.PAK", (void *)P6_HW_ANIMPAK,
                                                       P6_HW_ANIMPAK_CAP);
                    if (n > 0)
                        p6_w_apk_bytes = n;
                }
                // STEP-3 GHZ CHAIN STAGING (2026-07-03, frontend-cart-map-recarve
                // memory): the chain skipped the 9 GHZ gameplay sheets at boot (the
                // title-load cut) -- stage them HERE, where the playable scene needs
                // them, so Player_StageLoad/HUD/Ring surface hashes resolve and the
                // arm-env bind loop below (inside p6_scene_load_and_arm) binds them
                // to VDP1. Verbatim the plain-GHZ boot loop (shtFiles/shtPaths,
                // p6_io_main.cpp:4084-4156) minus MakeResident (banded-first: the
                // landing draws ~2 players; RES budget untouched). ~231 KB read on a
                // seam that already loads the whole scene. Slot budget: SaturnSheet
                // 25 front-end slots + NSHEETS 23 (both bumped for this).
                {
                    // BADNIK-VIS FIX (2026-07-11, ghz-explosions-animals-invisible memory):
                    // + EXPLOS.SHT (Global/Explosions.gif, badnik-poof) + ANIMALS.SHT
                    // (Global/Animals.gif, freed critter) as the 10th/11th GHZ sheets. They
                    // were INVISIBLE: unstaged -> LoadSpriteSheet returns -1 on Saturn
                    // (Sprite.cpp:992) -> the Explosion/Animals surface never binds a VDP1
                    // handle -> every draw drops. Staged HERE (same seam + auto-slot as the
                    // 9 GHZ sheets) so Explosion_StageLoad/Animals_StageLoad's
                    // LoadSpriteSheet resolves the slot -> the arm-env bind loop binds them.
                    // Banded EXPLOS 28,365 + ANIMALS 3,989 B fit the 384 KB cart band store
                    // (11-sheet total 337,489 B, 55 KB margin). #228-SAFE: chain has
                    // animpak-on-cart (GLOBALS 0x060C8000 ceiling, ~28 KB _end headroom),
                    // SATURNSHEET_SLOTS 25->27 + P6_VDP1_NSHEETS 23->25. Graceful fail:
                    // SaturnSheet_Stage returns -1 on a full store -> sheet stays banded/
                    // unstaged (no crash), explosions just stay invisible. Chain-gated.
                    static const char *ghzShtFiles[11] = {
                        "SONIC1.SHT", "SONIC2.SHT", "SONIC3.SHT",
                        "ITEMS.SHT",  "DISPLAY.SHT", "SHIELDS.SHT",
                        "TAILS1.SHT", "GLOBJ.SHT",  "GHZOBJ.SHT",
                        "EXPLOS.SHT", "ANIMALS.SHT"
                    };
                    static const char *ghzShtPaths[11] = {
                        "Players/Sonic1.gif", "Players/Sonic2.gif", "Players/Sonic3.gif",
                        "Global/Items.gif",   "Global/Display.gif", "Global/Shields.gif",
                        "Players/Tails1.gif", "Global/Objects.gif", "GHZ/Objects.gif",
                        "Global/Explosions.gif", "Global/Animals.gif"
                    };
                    int32 ghzGslot[11] = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 };
                    // Water M1b NOTE (2026-07-20): a SaturnSheet_BandReset() here (dropping the
                    // dead front-end band debris so WATER.SHT fits) DID stage the water sheet
                    // (shtslot 11 GREEN) but REGRESSED the chain: persisted VDP1 handles kept
                    // pre-reset slot numbers -> Tails' draws dropped every frame (frd_misses
                    // +11.3/s == drops +11.1/s) + the autorun bot wedged at x~585; the follow-up
                    // handle-slot remap then DETERMINISTICALLY froze the handoff (cont=1792, two
                    // boots). Both attempts REVERTED -- the store keeps its (full) monotonic
                    // contents and WATER.SHT staging fails soft (shtslot -1, surface pending)
                    // until the reclaim is redesigned with the persisted-handle contract solved.
                    for (int32 gi = 0; gi < 11; ++gi) {
                        // #321: REUSE the AIZ-staged Sonic sheets (SONIC1/2/3 banded earlier in
                        // p6_aiz_reload) instead of double-staging -> keeps the band store under
                        // 640KB (double-stage overflowed -> SaturnSheet_Stage returns -1 -> the
                        // landing sheets fail to bind). Net band-store usage unchanged.
                        RETRO_HASH_MD5(gph);
                        GEN_HASH_MD5(ghzShtPaths[gi], gph);
                        int32 existing = SaturnSheet_FindSlot((const uint32 *)gph);
                        if (existing >= 0) { ghzGslot[gi] = existing; continue; }
                        int gsn = rsdk_storage_load_to_lwram(ghzShtFiles[gi],
                                                             (void *)P6_LW_ENTITYLIST, 0x10000);
                        if (gsn > 0) {
                            int32 gslot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST,
                                                            (uint32)gsn);
                            if (gslot >= 0) {
                                SaturnSheet_SetHash(gslot, (const uint32 *)gph);
                                ghzGslot[gi] = gslot;
                            }
                        }
                    }
                    // BADNIK-VIS gate witnesses (RED->GREEN): EXPLOS/ANIMALS staged slot.
                    // -1 = stage failed (store full) -> explosion still invisible (RED);
                    // >=0 = staged -> Explosion_StageLoad resolves the surface -> renders.
                    p6_w_explos_slot  = ghzGslot[9];
                    p6_w_animals_slot = ghzGslot[10];
#if defined(P6_FRONTEND_MENU) || defined(P6_FRONTEND_CHAIN)
                    // C1 signpost-campaign r3 (2026-07-10, MEASURED): the live
                    // boot->signpost path is the P6_FRONTEND_CHAIN flavor, NOT
                    // P6_FRONTEND_MENU. The whole resident-promote + FRD-stage +
                    // attach block was guarded P6_FRONTEND_MENU-only, so on the
                    // chain the 9 GHZ sheets staged above (slots 9..19, live
                    // table dump 2026-07-10: ALL res=0) stayed banded with NO
                    // FRD attach -> SaturnSheet_FetchRect re-inflated every draw
                    // (p6_w_sht_fetches +9.4/frame steady-motion, banded-fetch
                    // slots {19:GHZObjects, 17:Shields}; FRD misses=87787 = the
                    // dispatch never consulted because frdSlot stayed -1). Widen
                    // the guard to the chain. Plain GHZ (neither flag) byte-
                    // identical (this #if compiles out there).
                    // #317 DRAW/INFLATE HOG FIX: the RES store
                    // is full of the resident TITLE/menu sheets (TSONIC 1 MB) that are
                    // never drawn in gameplay, so the 9 sheets staged above stay banded
                    // -> SaturnSheet_FetchRect re-inflates every draw. MEASURED at the
                    // landed Green Hill Zone: 8.6 inflations/frame -> 4.1 fps
                    // (qa_frontend_inflate_gate.py RED). Reclaim the dead title RES then
                    // promote the landing sheets to resident (zero-inflate memcpy path,
                    // SaturnSheet.cpp:345). Priority = every-frame draws first: Sonic
                    // 1/2/3 (0,1,2), Tails1 (6), Display/HUD (4), GLObjects (7),
                    // GHZObjects (8), Items (3). Shields (5) left banded (rarely drawn)
                    // to leave RES headroom. MakeResident is bounds-checked -> any sheet
                    // that overflows the 1.625 MB store simply stays banded (no
                    // corruption). Budget: 786K(Sonic)+262K(Tails)+65K+65K+131K+32K =
                    // 1.34 MB < 1.66 MB. Front-end only -> plain Green Hill Zone
                    // byte-identical (this whole seam is chain-gated).
                    {
                        SaturnSheet_ResReset();
#if defined(P6_FRAMEDIR)
                        // Stage-1 FRD (checklist sec 7, GHZ landing seam):
                        // the pre-cut frame directories REPLACE the 8-sheet
                        // resident promote -- 9 FRDs = 1,418,316 B fit the
                        // 1,703,936 B store (the 1.34 MB promote + FRDs
                        // would not). Any sheet whose FRD staging fails
                        // falls back to its promoteOrder MakeResident,
                        // bounds-checked as before; the banded .SHT store
                        // (separate cart region) remains the rect-miss
                        // fallback either way. Registry reset first: the
                        // ResReset killed the cutscene leg's blobs.
                        SaturnFrameDir_Reset();
                        p6_vdp1_frd_detach_all();
                        p6_vdp1_frd_clear_store(); // #328: drop stale store->FRD map
                        static const char *ghzFrdFiles[9] = {
                            "SONIC1.FRD", "SONIC2.FRD", "SONIC3.FRD",
                            "ITEMS.FRD",  "DISPLAY.FRD", "SHIELDS.FRD",
                            "TAILS1.FRD", "GLOBJ.FRD",  "GHZOBJ.FRD"
                        };
                        int32 frdOk[9];
                        for (int32 fi = 0; fi < 9; ++fi) {
                            frdOk[fi] = p6_frd_stage_file(ghzFrdFiles[fi],
                                                          ghzShtPaths[fi]);
                            // DRAW-WALL FIX (task #328): route the FRD dispatch by
                            // SaturnSheet STORE slot (dup-handle safe) -- see p6_vdp1.c
                            // s_frdByStore. The Player draws Sonic1/Tails1 through a VDP1
                            // handle whose per-handle frdSlot stays -1 (a duplicate handle
                            // bound in the AIZ/cutscene leg the #321 reuse leaves un-
                            // attached), so the miss re-inflated the band every frame
                            // (MEASURED p6_w_sht_fetches ~8/frame = the ~7 fps chain draw
                            // wall). Keying the dispatch by the STABLE store slot serves
                            // the pre-cut FRD pattern regardless of which handle draws.
                            if (frdOk[fi] >= 0 && ghzGslot[fi] >= 0)
                                p6_vdp1_frd_set_store(ghzGslot[fi], frdOk[fi]);
                        }
#if defined(P6_WATER)
                        // Water M1b (docs/feature_checklists/water.md): stage WATER.SHT (band-
                        // store slot -> the arm_env bind loop binds a VDP1 surface, since the FRD
                        // ATTACHES to an already-bound surface, p6_io_main.cpp:3551-3565) + WATER.FRD
                        // (fast pixels, store-routed like the 9 GHZ FRDs above). Water_StageLoad's
                        // LoadSpriteSheet("Global/Water.gif") in p6_scene_load_and_arm below then
                        // resolves this slot -> Water_Draw_Water tiles the surface strip at the
                        // water line. Self-contained (no ghzShtFiles/ghzFrdFiles array-bound edits).
                        // Staged AFTER SaturnFrameDir_Reset (above) so the FRD is not wiped.
                        // #if P6_WATER -> plain + non-water chain byte-identical.
                        {
                            RETRO_HASH_MD5(wgph);
                            GEN_HASH_MD5("Global/Water.gif", wgph);
                            int32 wslot = SaturnSheet_FindSlot((const uint32 *)wgph);
                            if (wslot < 0) {
                                int wsn = rsdk_storage_load_to_lwram("WATER.SHT",
                                                                     (void *)P6_LW_ENTITYLIST, 0x10000);
                                p6_w_water_loadrc = wsn; // M1b diag: GFS load byte count (<=0 = load FAIL)
                                if (wsn > 0) {
                                    // M1b Stage-fail localizer (throwaway): band state AT the attempt.
                                    p6_w_water_bandcur = (int32)SaturnSheet_BandCursor();
                                    p6_w_water_bandend = (int32)SaturnSheet_BandEnd();
                                    p6_w_water_slotcnt = SaturnSheet_SlotCount();
                                    wslot = SaturnSheet_Stage((const void *)P6_LW_ENTITYLIST, (uint32)wsn);
                                    if (wslot < 0)
                                        p6_w_water_shtslot = -3; // load OK but Stage FAILED (store/slot full)
                                    else
                                        SaturnSheet_SetHash(wslot, (const uint32 *)wgph);
                                }
                            }
                            int32 wfrd = p6_frd_stage_file("WATER.FRD", "Global/Water.gif");
                            p6_w_water_frdrc = wfrd; // M1b diag: FRD stage slot (<0 = FRD FAIL)
                            if (wfrd >= 0 && wslot >= 0)
                                p6_vdp1_frd_set_store(wslot, wfrd);
                            if (wslot >= 0)
                                p6_w_water_shtslot = wslot; // >=0 = WATER.SHT staged (surface can bind)
                        }
                        // (Water M1b handle-slot remap REVERTED here -- see the BandReset
                        // NOTE above: the remap deterministically froze the GHZ handoff.)
#endif
                        static const int32 promoteOrder[8] = { 0,1,2,6,4,7,8,3 };
                        for (int32 pi = 0; pi < 8; ++pi) {
                            int32 gs = ghzGslot[promoteOrder[pi]];
                            if (gs >= 0 && frdOk[promoteOrder[pi]] < 0)
                                SaturnSheet_MakeResident(gs);
                        }
                        // C1 signpost-campaign fix (2026-07-10, MEASURED 9.4 real
                        // miniz inflates/frame + 12.8 evicts/frame at the landed
                        // GHZ, qa_signpost_run 18:19 run): sheets BOUND in earlier
                        // chain legs (Players/Sonic1, Global/Items, Global/Display
                        // ... surfaces + p6_vdp1HandleBySurface persist) are
                        // SKIPPED by the arm-env bind loop (handle>=0 continue,
                        // :3188) so the p6_vdp1_frd_detach_all() above left their
                        // frdSlot=-1 FOREVER -- and the frdOk>=0 result suppressed
                        // their MakeResident fallback, leaving them on the banded
                        // per-draw miniz path. The AIZ (:7022) and cutscene
                        // (:7683) seams both re-attach persisted handles; this
                        // seam was missing the same call. Front-end-chain only
                        // (inside P6_FRONTEND_MENU) -> plain GHZ byte-identical.
                        p6_frd_attach_bound();
#else
                        static const int32 promoteOrder[8] = { 0,1,2,6,4,7,8,3 };
                        for (int32 pi = 0; pi < 8; ++pi) {
                            int32 gs = ghzGslot[promoteOrder[pi]];
                            if (gs >= 0)
                                SaturnSheet_MakeResident(gs);
                        }
#endif
                    }
#endif
                    // Object anim pack for the staged HUD/Ring/GHZ objects (replaces
                    // HBHOBJ in the front-end OBJ window -- the Heavies are done).
                    int on = rsdk_storage_load_to_lwram("GHZOBJ.PAK", (void *)P6_HW_OBJANIMPAK,
                                                        P6_HW_OBJANIMPAK_CAP);
                    if (on > 0)
                        p6_w_objapk_bytes = on;
                }
                p6_scene_load_and_arm(); // load+arm the GHZ scene SetupGHZ1 selected
#if defined(P6_FRAMEDIR)
                // DRAW-WALL FIX (task #328): re-run the per-handle FRD attach AFTER the
                // arm re-binds the scene surfaces. The store-slot dispatch (s_frdByStore,
                // populated at FRD-staging above) is the primary route; this covers any
                // resident (px!=0, shtSlot<0) sheet whose dispatch falls back to the
                // per-handle frdSlot. Front-end-chain only -> plain GHZ byte-identical.
                p6_frd_attach_bound();
#endif
                // F-LAND-SONIC (savestate-proven, _v4_land.mcs): Sonic's Create-time
                // SetSpriteAnimation during the load storm read its anim entry as
                // ALL-ONES (failed A-Bus cart read under concurrent CD/GFS traffic):
                // animator = {frames = base-36, speed/frameDuration/frameCount = -1,
                // loopIndex = 0xFF} while aniFrames(2)/the live slot table/the pack
                // bytes are ALL healthy. The decomp caller-side "already in this
                // anim" guard means no later call heals it (Tails' set landed in a
                // quiet window and is fine). FIX: after the arm returns (bus quiet),
                // FORCE-reapply both players' animators from their (healthy) tables.
                // forceApply=true bypasses the equal-frames guard; animationID is the
                // one the game already chose, so this is a pure refresh, not a logic
                // change. Offsets per the census EntityPlayer: animator @ +104,
                // aniFrames u16 @ +176 (validated by the _v4_land.mcs field scan).
                for (int32 pfix = 0; pfix < 2; ++pfix) {
                    uint8 *pent  = (uint8 *)RSDK_ENTITY_AT(pfix);
                    Animator *pa = (Animator *)(pent + 104);
                    uint16 paf   = *(uint16 *)(pent + 176);
                    if (paf < SPRFILE_COUNT && pa->animationID >= 0
                        && pa->animationID < spriteAnimationList[paf].animCount) {
                        int32 pfid = (pa->frameID >= 0 && pa->frameID < 0x100) ? pa->frameID : 0;
                        SetSpriteAnimation(paf, (uint16)pa->animationID, pa, true, pfid);
                    }
                }
                // Punch v2 items 6/7 (camera dead post-handoff; MEASURED
                // _v8_land_right.mcs: Zone->setATLBounds=1, camera slot 60
                // state/target NULL, screen pinned x=52 while Sonic ran to 777):
                // the decomp clears the ATL flag + re-wires the camera inside
                // TitleCard_State_ShowTitleCard (TitleCard.c:504-514); no
                // TitleCard is registered yet, so run the verbatim excerpt here.
                // Gate: tools/qa_chain_camera_follow.py (RED on pre-fix state).
                p6_titlecard_atl_restore();
#if defined(P6_GHZCUT_BOOT)
                // GL1 (2026-07-06): spawn the "GREEN HILL ZONE / ACT 1" act card on
                // the arrival, exactly as the direct-boot Game.c:2157 does
                // (titlecard_spawn("GREEN HILL", TC_ACT_1)). The card slides in
                // (colored strips + BG + decor), holds, then slides away. Ticked +
                // drawn in p6_frontend_frame (this same one-shot handoff block is the
                // decomp-faithful spawn site: TitleCard is created by the arriving
                // GHZ scene). p6_titlecard_atl_restore above already did the decomp's
                // TitleCard_State_ShowingTitle ATL camera hand-back (the card re-runs
                // it at its own actionTimer==16, harmless if already cleared).
                //
                // GL1 GLYPHS (this iteration): the zone-name LETTERS ("GREEN HILL"/
                // "ZONE"/act digit) are Global/Display.gif rects on the already-staged
                // DISPLAY.SHT. (a) Re-resolve its SaturnSheet slot (staged above in the
                // 9-sheet loop, slot local to that block) and hand it to the card as
                // the glyph source. (b) Upload the Display GCT to CRAM block 2
                // (CRAM[512..767]) -- FREE at the landing (the 5 GHZCutscene Heavies
                // that own blocks 2-6 have exited), disjoint from the FG (0), the GHZ
                // object bank (1), and the players (7). The glyph blits select colno
                // 512 (p6_dl_glyph -> CMDCOLR, ST-013-R3 sec 6.4 + ST-058-R2 sec 10.1).
                {
                    // GL1 FIX (magenta-garble root cause): the glyph draw path
                    // p6_dl_glyph(sheet,...) indexes p6_vdp1.c's s_sheets[] VDP1
                    // registry (the SAME index space p6_vdp1_blit uses via
                    // p6_vdp1HandleBySurface), NOT the storage-layer SaturnSheet
                    // slot. The prior code handed SaturnSheet_FindSlot()'s slot
                    // (a DIFFERENT index space; =17) to p6_dl_glyph, which read
                    // s_sheets[17] = an unrelated bound sheet (Sonic/flame frames)
                    // -> the letters rendered as magenta sprite garbage (MEASURED
                    // _shots/game-260709-075150.png). Resolve Display.gif's engine
                    // surface, then its VDP1 s_sheets handle (== what a HUD
                    // DrawSprite would blit through). p6_vdp1_handle_for_surface is
                    // defined above (p6_io_main.cpp:1906).
                    int32 dsurf    = (int32)(int16)LoadSpriteSheet("Global/Display.gif", SCOPE_STAGE);
                    int32 dispSlot = (dsurf >= 0 && dsurf < SURFACE_COUNT)
                                         ? p6_vdp1_handle_for_surface(dsurf) : -1;
                    // Upload the Display palette to CRAM block 2 for the glyphs.
                    // NOTE: this runs AFTER p6_scene_load_and_arm(), so the entity
                    // list at P6_LW_ENTITYLIST is LIVE -- use P6_LW_LAYSCRATCH (a
                    // transient 0x8000 band-scratch, dead at this point) as the load
                    // buffer, NOT the entity list (which loading 512 B into would
                    // corrupt player entity 0). p6_w_tc_active doubles as the upload
                    // witness; add a dedicated one so a load MISS is visible.
                    int pn = rsdk_storage_load_to_lwram("DISPCARD.BIN",
                                                        (void *)P6_LW_LAYSCRATCH, 0x800);
                    p6_w_tc_pal_bytes = pn;
                    if (pn >= 256 * 2)
                        p6_vdp2_titlecard_pal_upload((const unsigned short *)P6_LW_LAYSCRATCH);
                    p6_titlecard_spawn("GREEN HILL", 0 /* TC_ACT_1 */, (int)dispSlot);
                    p6_w_tc_disp_slot = dispSlot;
                }
#endif
                return;
            }
        }
        // #319 DEATH-RESPAWN (+ GHZ1->GHZ2 act advance): a SAME-FOLDER GHZ reload. Decomp
        // Player_HandleDeath (Player.c:2089-2091) -> Zone_StartFadeOut_MusicFade -> Zone_State_
        // FadeOut (Zone.c:812) -> RSDK.LoadScene() -> ENGINESTATE_LOAD, currentSceneFolder still
        // "GHZ". The forward chain-hop seams above are one-shots to NEW folders, so a GHZ->GHZ
        // reload falls through to the swallow below -> the TYPE_BLANK'd player never respawns.
        // Un-swallow: actually reload. The pool RELOAD reset in p6_scene_load_and_arm (gated
        // p6_folderReload && p6_isGHZ) re-creates the Player at SLOT_PLAYER1; folderReload stays
        // 1 so the tileset/sheets stay resident (#250-safe, no re-decode/garble). Re-apply both
        // players' animators + restore the camera/ATL bounds exactly as the GHZCut->GHZ handoff.
        if (currentSceneFolder && !strcmp(currentSceneFolder, "GHZ")) {
            p6_scene_load_and_arm();
            for (int32 pfix = 0; pfix < 2; ++pfix) {
                uint8    *pent = (uint8 *)RSDK_ENTITY_AT(pfix);
                Animator *pa   = (Animator *)(pent + 104);
                uint16    paf  = *(uint16 *)(pent + 176);
                if (paf < SPRFILE_COUNT && pa->animationID >= 0
                    && pa->animationID < spriteAnimationList[paf].animCount) {
                    int32 pfid = (pa->frameID >= 0 && pa->frameID < 0x100) ? pa->frameID : 0;
                    SetSpriteAnimation(paf, (uint16)pa->animationID, pa, true, pfid);
                }
            }
            p6_titlecard_atl_restore();
            return;
        }
#endif
        sceneInfo.state = ENGINESTATE_REGULAR; // hold the splash; do not load Title (unported)
    }

    // CP5b.7 (Task #271 follow-up): TITLE FRAME-TIME ATTRIBUTION. The user reports the
    // title is "stupid slow" + "VDP2 flickering in and out". p6_frontend_frame (NOT
    // p6_ghz_frame) drives the title, and ONLY the GHZ frame carried the FRT section
    // brackets -- so the title's per-section cost was never measured (the pre-compaction
    // band-inflate hypothesis was a fetch COUNT, not a TIMED cost; 2.32 fetches/frame x
    // GHZ's ~0.02ms/fetch ~= 0.05ms, nowhere near the 148ms/frame measured). Mirror the
    // proven p6_ghz_frame brackets here verbatim (SAME witnesses, read by qa_p6_perf.py):
    //   - vbl_frame vs vbl_jo  -> compute-bound (these 4 sections) vs jo-body/slSynch
    //     (VDP1 still drawing / swap wait) -- the discriminator that picks the fix class.
    //   - the 4 cyc_*          -> the dominant section (Input/Objects/present/DrawLists).
    //   - EDSR.CEF at compute-done -> VDP1 draw-bound (huge sprite list) vs CPU-bound.
    // FRT reads are ~6 instr each (interrupt-masked), negligible vs a 148ms frame.
    // Front-end ONLY (the GHZ shipping flavor never compiles p6_frontend_frame).
    unsigned short fe_frame_t0 = p6_perf_frt_get();
    unsigned int   fe_vbl_start = p6_perf_vbl_count;
    {
        int32 jo_gap = (int32)(fe_vbl_start - p6_perf_vbl_prev);
        p6_w_perf_vbl_jo = jo_gap;
        if (jo_gap > p6_w_perf_vbl_jo_max) p6_w_perf_vbl_jo_max = jo_gap;
#if defined(P6_FRONTEND_MENU)
        p6_w_jo_vbl_sum += jo_gap; // #331: cumulative outside-frame vblank sum
#endif
    }
    unsigned short fe_t0, fe_t1;
    unsigned int   fe_v0, fe_v1;

    fe_v0 = p6_perf_vbl_count; fe_t0 = p6_perf_frt_get();
    ProcessInput();
    fe_t1 = p6_perf_frt_get(); fe_v1 = p6_perf_vbl_count;
    p6_w_perf_cyc_input = P6_FRT_DELTA(fe_t0, fe_t1);
    p6_w_perf_vbl_input = (int32)(fe_v1 - fe_v0);

    // CP5b.1 (Task #268) ROOT-CAUSE FIX: the engine's per-frame "common stuff" reset
    // of the foreach stack pointer (RetroEngine.cpp:179, `foreachStackPtr =
    // foreachStackList`) runs at the top of ProcessEngine BEFORE ProcessObjects. The
    // lean p6_frontend_frame calls ProcessObjects DIRECTLY (it bypasses ProcessEngine),
    // so without this the pointer stays at its NULL static-init -> the FIRST
    // GetAllEntities does `foreachStackPtr++` on NULL (a wild pointer) and EVERY
    // foreach_all in the Title state machine iterates wrong. MEASURED SYMPTOM: only the
    // EMBLEM (the first TitleLogo entity) ever flipped visible in AnimateUntilFlash/
    // FlashIn -- the RIBBON/GAMETITLE/COPYRIGHT/RINGBOTTOM stayed invisible and POWERLED
    // was never destroyed (vismask = EMBLEM|POWERLED only, per qa_title_logo per-type
    // diag). Resetting the stack each frame (verbatim the engine) makes every
    // foreach_all iterate the full TitleLogo set -> all logo pieces flip visible + blit.
    fe_v0 = p6_perf_vbl_count; fe_t0 = p6_perf_frt_get();
    foreachStackPtr = foreachStackList;
    // #318 rank34 PERF: maintain p6_scan_near for the playable GHZ landing so the
    // ProcessObjects loop1 I3d far-cull (Object.cpp, gated s_feIsGHZ) is CORRECT. The
    // chain's p6_frontend_frame (unlike p6_ghz_frame) never seeded p6_scan_near -> the
    // cull skipped EVERY scene slot and FROZE the level (MEASURED 2026-07-06, reverted).
    // Build the sorted-by-x index once on GHZ entry, then update the near-set from the
    // camera x each frame BEFORE ProcessObjects. Indexing MATCHES the cull byte-for-byte:
    // p6_scan_index_build encodes slot=e (pool slot), p6_scan_update_near sets
    // p6_scan_near[e], the cull tests p6_scan_near[_L] with _L=e. GHZ-only (folder gate);
    // Title/Menu/AIZ front-end scenes are untouched (their loop1 still iterates every slot).
    // #327b: per-FRAME load-build-seq edge detector (any folder -- the GHZ block
    // below consumes it; see its comment). "Changed since the previous ticked
    // frame" == a scene load (whose :6457 load-path p6_scan_index_build always
    // runs) completed between frames -> the current sorted index is the load
    // path's full-pool build and is authoritative.
    static int32 s_fe_prev_loadseq = -1;
    const int32 s_fe_loadseq_changed =
        (s_fe_prev_loadseq >= 0 && p6_scan_loadbuild_seq != s_fe_prev_loadseq);
    s_fe_prev_loadseq = p6_scan_loadbuild_seq;
    if (currentSceneFolder && !strcmp(currentSceneFolder, "GHZ")) {
        // #325 STALE-INDEX FIX (MEASURED, live _objprof.jsonl 2026-07-09): the one-shot
        // index build ran MID-HANDOFF -- the staged GHZCutscene->GHZ seam flips
        // currentSceneFolder before the entity pool is final, and the capture showed
        // scancull_n=45 for the whole landing (GHZ1 populates ~1034 scene slots; the Menu
        // leg's load-path build correctly showed 135). A 45-entry index means almost every
        // BOUNDS badnik/object had NO near bit -> wrongly culled the moment the camera
        // moves off spawn (frozen entities downstream). Fix: rebuild the index once >=2
        // tick groups have run since the first folder=="GHZ" sighting (the seam's loads
        // run inside tick groups; 2 ticked frames later the pool is final), and only arm
        // the cull AFTER that settled rebuild. Landing frames before the latch keep the
        // 186898e cull-off behavior (proven).
        static int32 s_ghz_idx_built    = 0;  // 0=never, 1=first (possibly mid-load) build, 2=settled rebuild
        static int32 s_ghz_idx_tickmark = -1;
        static int32 s_ghz_idx_loadseq  = -1; // p6_scan_loadbuild_seq at build #1 (fix #326)
        // #327b (signpost campaign r2, MEASURED live _brg_index_probe): the #326 guard
        // assumed frame build #1 runs MID-HANDOFF (before the load-path build). In the
        // chain flavor the folder flips only at the BLOCKING GHZ load, so the FIRST
        // folder=="GHZ" frame post-dates the load-path 968-entry build AND the
        // near-shrink: build #1 itself censused the SHRUNK pool (t3.6 idx 968->32,
        // loadbuild_seq unchanged at 6) and latched loadseq==current, so the settled
        // branch then CONFIRMED the collapse (seq==loadseq) -- every dormant entity
        // orphaned for the whole first life. Robust rule: s_fe_prev_loadseq is
        // tracked on EVERY p6_frontend_frame tick (any folder, below); if a scene
        // load (which always runs the :6457 load-path p6_scan_index_build) completed
        // since the previous ticked frame, the index in place IS the load path's
        // full-pool build -> authoritative -> skip build #1 AND the settled rebuild.
        // The #325 staged-seam case still works: its early folder flip ticks frames
        // with seq UNCHANGED -> build #1 runs (stale mid-handoff index, cull stays
        // unarmed) -> the blocking load completes -> seq change detected -> latch 2.
        if (s_fe_loadseq_changed)
            s_ghz_idx_built = 2;
        if (s_ghz_idx_built == 0) {
            p6_scan_index_build();
            s_ghz_idx_built    = 1;
            s_ghz_idx_tickmark = p6_w_tick_frames;
            s_ghz_idx_loadseq  = p6_scan_loadbuild_seq;
        }
        else if (s_ghz_idx_built == 1 && p6_w_tick_frames >= s_ghz_idx_tickmark + 2) {
            // #326 (signpost campaign, MEASURED): if the LOAD PATH built the index
            // since build #1 (the staged seam's p6_scene load ran p6_scan_index_build
            // on the FULL pre-shrink pool), that index is authoritative -- rebuilding
            // HERE would census the post-shrink pool (dormant slots read the dummy ->
            // classID 0 -> dropped) and permanently orphan every dormant entity
            // (measured: idx_n 968 -> 32, bridge-1/badniks never rematerialize).
            if (p6_scan_loadbuild_seq == s_ghz_idx_loadseq)
                p6_scan_index_build(); // no load-path build happened -- rebuild here (pre-#325 case)
            s_ghz_idx_built = 2;
        }
#if defined(P6_FRAMEDIR)
        // DRAW-WALL FIX (task #328, 2026-07-13, MEASURED): every GHZ frame, refresh the
        // STORE-slot -> FRD-slot table (p6_vdp1.c s_frdByStore) for every live
        // SaturnSheet store slot that has a staged frame directory. The FRD dispatch
        // keys off this table (by the stable store slot), NOT the per-VDP1-handle
        // frdSlot, so a sheet drawn through a duplicate/late-bound handle (the #321
        // AIZ-reuse leaves the Player's Sonic1/Tails1 handles with frdSlot=-1) still
        // serves from the pre-cut pattern instead of re-inflating the band every frame
        // (MEASURED p6_w_sht_fetches drop). Idempotent + cheap (<=27 store slots x one
        // FindSlot). Front-end-chain only -> plain GHZ byte-identical. Gate:
        // tools/qa_chain_draw.py.
        {
            int32 nsheet = SaturnSheet_SlotCount();
            for (int32 st = 0; st < nsheet; ++st) {
                RETRO_HASH_MD5(shash);
                SaturnSheet_SlotHashCopy(st, shash);
                if (!shash[0] && !shash[1] && !shash[2] && !shash[3])
                    continue;
                int32 fds = SaturnFrameDir_FindSlot((const uint32 *)shash);
                if (fds >= 0)
                    p6_vdp1_frd_set_store(st, fds);
            }
        }
#endif
#if defined(P6_DDW_ARENA)
        // DDWrecker boss bring-up diagnostic (2026-07-11, milestone a/b/c): the boss
        // at (15792,1588) is camera-local DORMANT until the camera reaches it, and the
        // autorun stalls at x14870 (r8) so it never materializes on a natural run. This
        // teleports the player + camera INTO the boss arena and PINS them just PAST the
        // boss x (so DDWrecker_State_InitChildren's `player.x > self->x` gate fires ->
        // children spawn -> Assemble -> the fight begins) and holds so the camera-local
        // streamer materializes the boss. It does NOT arm the signpost (unlike the r7
        // warp) -- the boss defeat must fire DDWrecker_State_SpawnSignpost NATURALLY.
        // Diag-only (build_shipping.sh P6_DDW_ARENA=1); plain/plain-chain leave it
        // undefined -> byte-identical. Runs BEFORE p6_scan_update_near so the camera
        // snap materializes the boss THIS tick.
        {
            static int32 s_ddw_phase = 0; // 0=armed, 1=pinning
            EntityBase *wplr = RSDK_ENTITY_AT(0);
            // arena floor: DDWrecker origin y1588; the boss deck is ~y1588. Stand the
            // player on it just past the boss x (15792) so InitChildren fires.
            const int32 AX = 15860, AY = 1560; // just right of + slightly above the boss
            if (s_ddw_phase == 0 && p6_w_tick_frames >= 60) {
                s_ddw_phase = 1;
                p6_w_ddw_warp_fired = 0x0DDBA55E;
            }
            if (s_ddw_phase == 1) {
                // Pin BOTH player + camera at the arena EVERY tick (not gated on
                // p6_w_ddw_seen). The P6_DDW_KILL defeat injection burns the boss HP
                // from the overlay (no player attack needed), so keeping the player
                // + camera nailed to the arena maximizes the boss's materialization
                // stability (the camera-local streamer keeps the boss live only while
                // the camera sits on it). A drifting camera flickers the boss dormant
                // (MEASURED milestone-b run: boss materialized intermittently). Also
                // pin the camera ENTITY (classID 6) so Camera_Update cannot re-follow
                // the (also-pinned) player away from x15792.
                wplr->position.x = AX << 16;
                wplr->position.y = AY << 16;
                wplr->velocity.x = 0;
                wplr->velocity.y = 0;
                wplr->groundVel  = 0;
                if (cameraCount > 0) {
                    cameras[0].position.x = 15792 << 16; // center on the boss
                    cameras[0].position.y = 1500  << 16;
                }
                // pin the camera ENTITY too (the reserve-region classID-6 slot)
                for (int32 cs = 0; cs < 64; ++cs) {
                    EntityBase *ce = RSDK_ENTITY_AT(cs);
                    if (ce && ce->classID == 6) {
                        ce->position.x = 15792 << 16;
                        ce->position.y = 1500  << 16;
                        break;
                    }
                }
            }
        }
#endif
#if defined(P6_WARP_TEST)
        // ROUND-7 Objective A (signpost-campaign): prove the GHZ1 SignPost SPIN +
        // ActClear fire from the CHAIN build, DECOUPLED from the autorun's x14503
        // stall. The chain runs its OWN GHZ tick HERE (p6_frontend_frame), NOT
        // p6_ghz_frame -- so the teleport MUST live here (MEASURED: a copy in
        // p6_ghz_frame never executed in the chain flavor). Camera-local streaming
        // means the RUNPAST/DROP signposts at x15792 are never materialized while
        // the camera sits at x<15000 (MEASURED live: p6_w_sign_count == 0 across
        // the whole chain-GHZ run). So teleport the player + camera onto the
        // FG-Low.A/B y1088 deck just LEFT of the signpost (SignPost sits at
        // (15792,1208); the deck runs y1088 at x15700-15844 per Scene1.bin/
        // _gaptrace). Snapping cameras[0].position.x BEFORE p6_scan_update_near
        // (next line, keyed off cameras[0].position.x) materializes the signpost
        // this tick; the autorun keeps holding RIGHT so the player crosses x15792
        // and SignPost_CheckTouch (player.x>signpost.x, SignPost.c:326) fires the
        // REAL ACTIVE_BOUNDS->ACTIVE_NORMAL crossing -> State_Spin (SignPost.c:416)
        // -> ResetEntitySlot(SLOT_ACTCLEAR) (SignPost.c:452). No relocate hack --
        // the genuine end-of-act path. Fires ONCE once the engine is ticking
        // (p6_w_tick_frames past the load settle). Diag-only (build_shipping.sh
        // P6_WARP=1); shipping/plain-chain leaves P6_WARP_TEST undefined ->
        // byte-identical. Runs BEFORE ProcessObjects so the tick sees the new pos.
        {
            // Two-phase warp: (1) at tick>=60 teleport the player + camera onto the
            // FG-Low.A/B y1088 deck at x15750 (the deck runs y1088 at x15700-15844;
            // the SignPost sits on it at x15792) and PIN him there for PIN_TICKS so
            // the camera-local streamer materializes the signpost while he stands
            // still (materialize is budgeted ~few slots/tick -- MEASURED the raw
            // teleport-and-run died in ~5 ticks, before sign_count went nonzero).
            // Re-assert zero velocity each pin tick so gravity can't drop him while
            // the deck tiles stream. (2) after the pin, RELEASE -- the autorun keeps
            // holding RIGHT so the now-materialized player runs the last ~40px and
            // crosses x15792, firing the REAL SignPost_CheckTouch -> State_Spin ->
            // ResetEntitySlot(SLOT_ACTCLEAR) chain (SignPost.c:326/416/452).
            static int32 s_r7_warp_phase = 0; // 0=armed, 1=pinning, 2=released
            static int32 s_r7_pin_left   = 0;
            static int32 s_r7_sign_armed = 0;
            #define P6_R7_PIN_TICKS 40
            EntityBase *wplr = RSDK_ENTITY_AT(0);
            if (s_r7_warp_phase == 0 && p6_w_tick_frames >= 60) {
                s_r7_warp_phase  = 1;
                s_r7_pin_left    = P6_R7_PIN_TICKS;
                wplr->position.x = 15750 << 16;
                wplr->position.y = 1180  << 16; // just above the y1208 signpost deck
                wplr->velocity.x = 0;
                wplr->velocity.y = 0;
                wplr->groundVel  = 0;
                p6_w_warp_plrx   = 0x7EED0000;   // sentinel: teleport FIRED
                if (cameraCount > 0) {
                    cameras[0].position.x = 15750 << 16;
                    cameras[0].position.y = 1180  << 16;
                }
            }
            else if (s_r7_warp_phase == 1) {
                // Pin BOTH x AND y so the teleported player cannot fall through the
                // deck (his collision plane may not treat the x15792 deck as solid
                // -- Objective B) while materialization completes (MEASURED: it takes
                // longer than the old 40-tick pin; the player fell + respawned before
                // sign_count went nonzero).
                wplr->position.x = 15750 << 16;
                wplr->position.y = 1180  << 16;
                wplr->velocity.x = 0;
                wplr->velocity.y = 0;
                wplr->groundVel  = 0;
                if (cameraCount > 0) {
                    cameras[0].position.x = 15750 << 16;
                    cameras[0].position.y = 1180  << 16;
                }
                // Once the GHZ1 signpost STREAMS IN near the pinned player, arm it
                // via the PROVEN #236 DROP path: state=SignPost_State_Falling +
                // active=ACTIVE_NORMAL, dropped 64px above the player's ground. When
                // it LANDS it transitions to SignPost_State_Spin (SignPost.c:581),
                // which runs its OWN ResetEntitySlot(SLOT_ACTCLEAR) (SignPost.c:452)
                // -> the REAL end-of-act spin + ActClear chain. GHZ1's x15792
                // signposts are type=DROP (MEASURED live: materialized type=1), so
                // run-past alone never triggers -- the drop is the canonical trigger
                // (mirrors GHZ_DDWrecker.c:921 dropping the sign at the act end).
                if (!s_r7_sign_armed && SignPost) {
                    uint16 spcid = *(uint16 *)SignPost;
                    for (int32 si = 0; si < ENTITY_COUNT; ++si) {
                        EntityBase *se = RSDK_ENTITY_AT(si);
                        if (si != 0 && se->classID == spcid) {
                            // Drive the signpost DIRECTLY into State_Spin (skip the
                            // Falling->land requirement: the warp deck at x15792 is
                            // not solid ground for the falling sign to land on --
                            // Objective B -- and the player fell through before the
                            // sign could land). State_Spin (SignPost.c:439) runs
                            // HandleSpin (needs spinCount preset -- Create sets 8 for
                            // RUNPAST, SignPost.c:130) + when spinCount hits 0 fires
                            // Music_PlayTrack(TRACK_ACTCLEAR) + ResetEntitySlot(
                            // SLOT_ACTCLEAR) (SignPost.c:452) -- the REAL end-of-act
                            // spin + ActClear chain, no ground dependency.
                            // Entity field offsets (gate-verified): state @+88 =
                            // body+0; spinCount @+128 = body+40; maxAngle/angle set
                            // sane so HandleSpin's first angle>=maxAngle branch ticks.
                            uint8 *sb = (uint8 *)se + sizeof(Entity);
                            *(void **)sb            = (void *)SignPost_State_Spin;
                            // spinCount=5: enough spin ticks for the gate (which
                            // samples ~3/s) to observe the ACTIVE_NORMAL crossing +
                            // State_Spin, yet HandleSpin still decrements to 0 within
                            // ~3s so State_Spin fires ResetEntitySlot(SLOT_ACTCLEAR) +
                            // Music_PlayTrack(TRACK_ACTCLEAR) (SignPost.c:448-452)
                            // BEFORE the player falls through the (non-solid) warp
                            // deck -- so ONE run latches crossed+spin+actclear.
                            *(int32 *)(sb + 40)     = 5;        // spinCount
                            se->active              = 2;       // ACTIVE_NORMAL
                            s_r7_sign_armed = 1;
                            p6_w_warp_signactive = 0x51611; // sentinel: sign armed
                            break;
                        }
                    }
                }
                // HOLD the pin until the sign is armed + given ~30 ticks to fall
                // and reach State_Spin. Only then release. No blind timeout -- if the
                // sign never streams in, keep pinning (the gate reads the live spin
                // state; a permanent pin is a clean RED, not a corrupt run).
                --s_r7_pin_left;
                if (s_r7_sign_armed && s_r7_pin_left <= -30)
                    s_r7_warp_phase = 2; // released after the fall->spin settles
            }
        }
#endif
        p6_scan_update_near(cameraCount > 0 ? (int32)(cameras[0].position.x >> 16) : 0);
        // #325 lever (i): arm the FE loop1 far-cull consult (Object.cpp #298 branch)
        // once the SETTLED sorted index + near-set are live. Parity: identical argument
        // to the plain-GHZ I3d shipping cull -- a slot is skipped only when x-cullable
        // (BOUNDS/XBOUNDS, index-build pins everything else always-iterate incl. any
        // updateRange.x+512 > window) AND |spawn_x - cam_x| > 1024 > the full check's
        // updateRange+offset, so the skip-set == the full check's own reject-set. The
        // near-set is ALSO refreshed per catch-up tick inside ProcessObjects
        // (Object.cpp:595), so mid-group camera motion is honored.
        { extern int32 p6_scan_n; g_p6_fe_ghz_cull = (s_ghz_idx_built == 2 && p6_scan_n > 0) ? 1 : 0; }
        if (g_p6_fe_cull_override >= 0) g_p6_fe_ghz_cull = g_p6_fe_cull_override; // #325 A/B knob
    }
    else if (currentSceneFolder && !strcmp(currentSceneFolder, "Menu")) {
        // #325 lever (i) extension -- MENU leg (MEASURED the 11.8fps floor: loop1 5,958
        // FRT ticks/tick with only ~1,070 in Updates; scan_maxslot 362, 106 BOUNDS slots,
        // near-set 95 of 135 -- the scan walks ~460 scene slots through RSDK_ENTITY_AT +
        // full ACTIVE_BOUNDS position reads x4 catch-up ticks). The menu's index is
        // already built by the SHARED scene-load path (p6_scene_run -> p6_scan_index_build,
        // witnessed live: scancull_n=135 on the Menu leg) and p6_scan_update_near already
        // runs per tick (Object.cpp:636, unconditional) -- arming the consult is the only
        // missing piece. Parity: same I3d argument as GHZ -- menu UI entities are
        // scene-authored at fixed positions (page NAVIGATION pans the CAMERA, entities
        // do not travel); non-BOUNDS actives + wide-updateRange slots are pinned
        // always-iterate by the index build; runtime spawns (UIDialog etc.) CreateEntity
        // into the always-scanned TEMP region. The #298 "drop a UISaveSlot" concern is
        // covered by the same pin set (wide-scene slots resolve via RSDK_ENTITY_AT and
        // their spawn-x is indexed like any scene slot). A/B knob applies here too.
        { extern int32 p6_scan_n; g_p6_fe_ghz_cull = (p6_scan_n > 0) ? 1 : 0; }
        if (g_p6_fe_cull_override >= 0) g_p6_fe_ghz_cull = g_p6_fe_cull_override; // #325 A/B knob
    }
    else {
        g_p6_fe_ghz_cull = 0;
    }
#if defined(P6_FRONTEND_MENU)
    unsigned int aizb_v0 = p6_perf_vbl_count; // #302 objtick bracket (vblank-stamped)
#endif
#if defined(P6_TICK_CATCHUP)
    /* #315 GAME-SPEED FIX (audit-1 headline, user "animation jumps / not decomp
     * authoritative"): the frontend ticked logic ONCE per RENDERED frame, so game
     * speed = renderfps/60 (MEASURED: landing 33.5 ticks/s = 56% speed; AIZ fly-in
     * ~4-10 fps = 7-17% speed -- every state machine, cutscene beat, and animation
     * timeline ran that much slow, and the per-leg rate CHANGES made the whole
     * chain read as temporally wrong). The decomp contract is logic at 60 Hz with
     * a multi-tick loop that runs the update group WITHOUT input or draw between
     * presents -- VERBATIM shape from RetroEngine.cpp:392-412 (ENGINESTATE_REGULAR:
     * ProcessInput once; {ProcessSceneTimer; ProcessObjects;
     * ProcessParallaxAutoScroll} x N with a state-change bail (:399); draw lists
     * once). N here = elapsed hardware vblanks since the previous frontend frame
     * (the true 60 Hz clock), clamped to P6_TICK_CAP so a slow leg cannot death-
     * spiral (extra ticks cost obj-time which lowers renderfps which raises N).
     * Tick order inside the group is the DECOMP order (SceneTimer BEFORE Objects,
     * RetroEngine.cpp:394-395) -- the old code ran Objects->SceneTimer, reversed.
     * ProcessParallaxAutoScroll (:396) was previously never called in the
     * frontend -- engine parallax autoscroll positions were frozen. Gated:
     * non-catchup flavors keep the original single-tick block verbatim. */
#ifndef P6_TICK_CAP
#define P6_TICK_CAP 4
#endif
    {
        static unsigned int s_tick_vbl_prev = 0;
        unsigned int now = p6_perf_vbl_count;
        int32 n = (s_tick_vbl_prev == 0) ? 1 : (int32)(now - s_tick_vbl_prev);
        s_tick_vbl_prev = now;
        if (n < 1) n = 1;
        if (n > P6_TICK_CAP) n = P6_TICK_CAP;
        p6_w_tick_last = n;
        ProcessSceneTimer();
        ProcessObjects();
        ProcessParallaxAutoScroll();
        ++p6_w_tick_frames;
        for (int32 ti = 1; ti < n; ++ti) {
            if (sceneInfo.state != ENGINESTATE_REGULAR)
                break; /* decomp :399 -- a mid-tick LoadScene stops the group */
            ProcessSceneTimer();
            ProcessObjects();
            ProcessParallaxAutoScroll();
            ++p6_w_tick_frames;
        }
    }
#else
    ProcessObjects();
    ProcessSceneTimer();
#endif
#if defined(P6_FRONTEND_MENU)
    p6_w_aiz_vbl_objtick += (int32)(p6_perf_vbl_count - aizb_v0); // #302 objtick close
#endif
#if defined(P6_GHZCUT_BOOT)
    // GL1 (2026-07-06): advance the GHZ act card once per frontend frame (its
    // slide timing is the decomp entity Update cadence). Only ticks while the
    // card is live at the GHZ landing; no-op elsewhere (state stays DONE/-1).
    if (currentSceneFolder && !strcmp(currentSceneFolder, "GHZ"))
        p6_titlecard_tick();
#endif
    fe_t1 = p6_perf_frt_get(); fe_v1 = p6_perf_vbl_count;
    p6_w_perf_cyc_obj = P6_FRT_DELTA(fe_t0, fe_t1);
    p6_w_perf_vbl_obj = (int32)(fe_v1 - fe_v0);
    // CP4c BLUE-SCREEN FIX: arm the VDP1 sprite layer (SPRON) for the UI scene.
    // p6_vdp2_blank() (slScrAutoDisp(0)) in the lean load disabled ALL VDP2 layers;
    // a UI scene runs no GHZ present to re-enable SPRON, so without this the
    // UIPicture sprites blit to the framebuffer (p6_w_vdp1_landed>0) but VDP2 never
    // composites them (MEASURED BGON=0 -> uniform-blue splash). Idempotent; armed
    // each tick (cheap, mirrors how the GHZ frame re-arms SPRON via its present).
    fe_v0 = p6_perf_vbl_count; fe_t0 = p6_perf_frt_get();
#if defined(P6_FRONTEND_TITLE)
    // CP5b.3 (Task #272): when the Title BACKDROP is up (NBG1 island+clouds), arm
    // NBG1ON|SPRON + drift the backdrop instead of SPRON-only (which would disable
    // NBG1 and blank the backdrop). Falls back to SPRON-only before the backdrop
    // is presented (the early load frames) so the logo still composites.
    if (p6_w_title_backdrop_done) {
#if !defined(P6_TITLE_ISLAND_OFF) /* #313 A/B-2: also gate the backdrop scroll (title-unique per-frame) */
        p6_vdp2_title_backdrop_scroll(p6_w_cont_frames);
#endif
        // CP5b.6 (Task #276): drive the RBG0 island rotation. The decomp
        // TitleBG_StaticUpdate (:34-46) advances ++angle &= 0x3FF every frame. We
        // mirror that with a local +1/frame counter (the cross-overlay TitleBG->angle
        // is not directly addressable from the pack TU). sine/cosine = the decomp's
        // RSDK.Sin1024(-angle)>>2 / Cos1024(-angle)>>2 -- computed here via the
        // engine's OWN Sin/Cos1024 tables so the per-line coeff byte-matches the gate.
        if (p6_w_title_island_armed) {
            // punch v2 item 1 (MEASURED _v9_title*.mcs: B0 RBG0 map ALL-ZERO in
            // the chain despite island_armed=1): re-stage the map through the
            // proven B0 vblank-DMA path if it reads back empty. Cheap probe
            // (early-out on the first nonzero word); p6_w_title_map_heals counts
            // hits so a savestate localizes any per-frame clobberer. Runs before
            // the frame's rotation so the coeff/matrix operate on a present map.
            p6_vdp2_title_island_map_heal();
            static int32 s_island_angle = 0;
            int32 ang  = s_island_angle & 0x3FF;
            int32 sine   = RSDK::Sin1024((-ang) & 0x3FF) >> 2;
            int32 cosine = RSDK::Cos1024((-ang) & 0x3FF) >> 2;
            // SUB2 (#276 clouds): the island frame runs FIRST (its slScrAutoDisp
            // enables RBG0ON|NBG1ON|SPRON + sets up the RBG0 planes), THEN the cloud
            // NBG1 config runs LAST so NBG1's plane/map registers (MPABN1 etc.) are
            // the final writes that survive into the vblank register DMA. MEASURED
            // ROOT CAUSE of the blank clouds (2026-06-23): config-BEFORE-island left
            // MPABN1=0x0000 (NBG1 plane base resolved off the cloud map in B1 ->
            // sky-blue blank top). The cloud band is parked high (above the island
            // band y[168,240]); a slow horizontal drift mirrors Scanline_Clouds.
#if !defined(P6_TITLE_ISLAND_OFF) /* #313 A/B knob (exonerated: die-off persists with the
                                   * island block off; root cause = the slSynch slave-
                                   * mailbox race, see the drain fix at frame end). */
            p6_vdp2_title_island_rbg0_frame((int)ang, (int)sine, (int)cosine);
#endif
#if defined(P6_TITLE_ISLAND_OFF) /* #313 A/B-2: clouds config gated with the island */
            if (0)
#else
            if (p6_w_title_clouds_armed)
#endif
                /* CLOUD-FLICKER FIX (#276, MEASURED 2026-06-23): the decomp
                 * TitleBG_Scanline_Clouds is HORIZONTALLY STATIC -- Sin256(0)=0 so
                 * its position.x is fixed; only position.y drifts (TitleBG->timer).
                 * The prior per-frame HORIZONTAL scroll (s_island_angle>>1) dragged
                 * the cloud's non-uniform wisps through the 320px window -> the top-
                 * band cloud mass oscillated 40k<->33k (qa_title_clouds_stable RED,
                 * 6/24 dips; per-column measure: clouds blanked at the L/R screen
                 * edges at the dip scroll positions). The cells tile seamlessly (x%16)
                 * so it is NOT a tiling gap -- it is the non-decomp horizontal pan.
                 * FIX: freeze the scroll (decomp-faithful, stable). The subtle vertical
                 * timer-drift needs a vertically-tiling cloud band -> deferred polish. */
                p6_vdp2_title_clouds_b1_config(0, 0);
            ++s_island_angle;
        }
    } else
        p6_vdp2_arm_sprites_only();
#else
    p6_vdp2_arm_sprites_only();
#endif
    fe_t1 = p6_perf_frt_get(); fe_v1 = p6_perf_vbl_count;
    p6_w_perf_cyc_present = P6_FRT_DELTA(fe_t0, fe_t1);
    p6_w_perf_vbl_present = (int32)(fe_v1 - fe_v0);
    // #322 perf attribution: the span from here to the DrawLists prologue (the AIZ/GHZCut
    // FG-Low present + the 3 AIZ BG streams + menu layout force) was UNTIMED -- at the
    // measured 8 fps fly-in, obj+draw+present+input account for only ~40 ms of the ~125 ms
    // frame. Bracket it so qa can attribute the gap (witness p6_w_perf_cyc_fgbg).
    unsigned short fe_fgbg_t0 = fe_t1;
    // #325 stage-1: 1 = this frame forked the FG present compute onto the SLAVE
    // (folder=="GHZ" only); the join+config runs after ProcessObjectDrawLists.
    int fe_present_kicked = 0;
    (void)fe_present_kicked;

#if defined(P6_GHZCUT_SEAMTEST)
    // Task #309 gate-2: the LIVE-SEAM RED-gate injection. The full AIZ intro is ~4 fps
    // and runs MINUTES before its beat-9 AIZSetup_Cutscene_LoadGHZ (AIZSetup.c:900)
    // fires RSDK.SetScene("Cutscenes","Green Hill Zone")+RSDK.LoadScene(). To exercise the
    // ENGINESTATE_LOAD AIZ->GHZCutscene SEAM in seconds (not minutes), inject the EXACT
    // same SetScene at a fixed cont-frame count once the AIZ scene is up -- the proven
    // P6_TRANSITION_TEST one-shot pattern (the GHZ1->GHZ2 act-advance, this file ~:7170).
    // SetScene("Cutscenes","Green Hill Zone") resolves (Scene.cpp) to folder GHZCutscene
    // id 1; replicate that listPos selection by NAME scan (GameConfig-equivalent), then
    // request ENGINESTATE_LOAD. The REAL trigger in the live build is the beat-9 SetScene;
    // this injection only SHORT-CIRCUITS the wait so the seam can be gated cheaply. The
    // seam branch in the ENGINESTATE_LOAD handler above (currentSceneFolder=="AIZ") catches
    // it on the next frame. NOT compiled in the live-loop build (P6_GHZCUT_BOOT alone).
    {
        static int32 s_seamtest_fired = 0;
        if (!s_seamtest_fired && p6_w_cont_frames >= 100
            && strcmp(currentSceneFolder, "AIZ") == 0) {
            s_seamtest_fired = 1;
            for (int32 c = 0; c < sceneInfo.categoryCount; ++c) {
                SceneListInfo *cat = &sceneInfo.listCategory[c];
                int32 hit = 0;
                for (int32 i = cat->sceneOffsetStart; i <= cat->sceneOffsetEnd; ++i) {
                    if (!strcmp(sceneInfo.listData[i].folder, "GHZCutscene")
                        && sceneInfo.listData[i].id[0] == '1') {
                        sceneInfo.activeCategory = c;
                        sceneInfo.listPos        = i;
                        hit                      = 1;
                        break;
                    }
                }
                if (hit)
                    break;
            }
            sceneInfo.state = ENGINESTATE_LOAD;
            p6_w_aiz_cutscene_state = 999; // SEAMTEST witness: injection fired (distinct from real beats)
        }
    }
#endif
#if defined(P6_AIZ_TEST)
    // M3.0b: present the AIZ FG tilemap on NBG1 each frame. Runs AFTER the
    // p6_vdp2_arm_sprites_only() above (which armed SPRON-only -> NBG1 OFF for AIZ,
    // since p6_w_title_backdrop_done==0), so this is the FINAL VDP2 arm -> NBG1ON|SPRON
    // sticks into the vblank register DMA. The menu flavor drops p6_ghz_frame, so this
    // wires the GHZ-style FG present (the analog of the p6_ghz_frame present) into the
    // front-end frame. p6_fglow_layer_index() is DATA-DRIVEN -- it scans tileLayers for
    // saturnSlot==0 (the FG-Low layer LoadSceneFolder bound, Scene.cpp:450-452) -- so the
    // AIZ FG-Low index is DERIVED, not guessed. The single call builds the PND page from
    // the AIZ1LAYT band-store window + arms NBG1ON|SPRON + sets the scroll
    // (p6_vdp2.c:1481-1482). Static camera (screens[0].position = the AIZ load camera);
    // M3.2 drives screens[0].position via the cutscene state machine.
    {
        unsigned int ph = 0; int nb = 0;
        int fgl = p6_fglow_layer_index();
        // M3.0c diag: which layer did the present read + each layer's saturnSlot + the
        // scroll. The repeating pattern points at the wrong FG-Low index (a BG layer
        // with a default saturnSlot==0 winning the scan). Latched every frame (cheap).
        p6_w_aiz_fglow_idx = fgl;
        p6_w_aiz_scrx      = screens[0].position.x;
        p6_w_aiz_scry      = screens[0].position.y;
        {
            int32 s = 0;
            for (int32 i = 0; i < 6; ++i)
                s |= ((int32)((uint32)tileLayers[i].saturnSlot & 0xF)) << (i * 4);
            p6_w_aiz_slots = s;
        }
        // SEGMENT B / rank6 (#318): this AIZ FG-Low present re-arms NBG1ON|SPRON and
        // DROPS RBG0ON. It must run ONLY for the scenes that actually have an FG-Low
        // present (AIZ/GHZCutscene/GHZ) -- NOT Title/Menu/Logos. On the Title leg the
        // island RBG0 arm above (p6_vdp2_title_island_rbg0_frame) then survives to the
        // settled frame instead of being clobbered black (the "title backdrop broken"
        // report). Menu/Logos never needed this present (they run no FG-Low plane).
        unsigned int aizb_fg0 = p6_perf_vbl_count; // #302 fgpresent bracket (vblank-stamped)
        if (currentSceneFolder
            && (!strcmp(currentSceneFolder, "AIZ")
                || !strcmp(currentSceneFolder, "GHZCutscene")
                || !strcmp(currentSceneFolder, "GHZ"))) {
#if defined(P6_FE_SLAVE_PRESENT) && !defined(P6_TITLE_NODRAW)
            // #325 stage-1 (dual-SH2 fgbg offload, GHZ landing only): fork the FG
            // present COMPUTE (cart PND page + SaturnLayout window walk + CRAM
            // bank0) onto the SLAVE via the proven p6_present_kick mechanism
            // (p6_vdp2.c:2249; plain GHZ p6_ghz_frame:3597, adc396b-coherent).
            // The slave's ~4.8ms runs under the master's sky bg_frame + DrawLists
            // + direct-VDP1 emit; p6_present_join_config after p6_dl_end runs the
            // master-only NBG1 register tail (ST-202: VDP registers master-only).
            // AIZ/GHZCutscene stay synchronous (their legs keep the proven order).
            if (!strcmp(currentSceneFolder, "GHZ")) {
                unsigned short _k0 = p6_perf_frt_get();
                p6_present_kick(fgl, screens[0].position.x, screens[0].position.y,
                                (const unsigned short *)fullPalette[0]);
                p6_w_perf_kick_frt = P6_FRT_DELTA(_k0, p6_perf_frt_get());
                fe_present_kicked = 1;
            } else
#endif
            p6_vdp2_present_ghz_camera(fgl,
                                       screens[0].position.x, screens[0].position.y,
                                       (const unsigned short *)fullPalette[0], &ph, &nb);
        }
        p6_w_aiz_vbl_fgpresent += (int32)(p6_perf_vbl_count - aizb_fg0); // #302 fgpresent close
        // M3.2 FG probe: the present just bound SaturnLayout slot 0 -> layer fgl (FG-Low).
        // Read 4 in-window tiles to see if the windowed accessor returns the varied AIZ
        // FG-Low or a uniform constant (binding/window/refill vs page-build discriminator).
        // #325 stage-1: on a kicked frame the SLAVE owns the SaturnLayout slot-0/1
        // windows (GetTile refills mutate them; ST-202 no bus snoop) -- skip the
        // diagnostic probe for that frame (AIZ/GHZCutscene frames keep it).
        if (!fe_present_kicked) {
            extern uint16 SaturnLayout_GetTile(int32 slot, int32 tx, int32 ty);
            int32 ctx = screens[0].position.x >> 4;
            int32 cty = screens[0].position.y >> 4;
            uint16 t0 = SaturnLayout_GetTile(0, ctx + 1,  cty + 2);
            uint16 t1 = SaturnLayout_GetTile(0, ctx + 8,  cty + 6);
            uint16 t2 = SaturnLayout_GetTile(0, ctx + 15, cty + 10);
            uint16 t3 = SaturnLayout_GetTile(0, ctx + 20, cty + 12);
            p6_w_aiz_gt_ctx = (ctx << 16) | (cty & 0xFFFF);
            p6_w_aiz_gt_a   = ((int32)t0 << 16) | t1;
            p6_w_aiz_gt_b   = ((int32)t2 << 16) | t3;
        }
        // R2.4: the 4-bpp BG4 plane (NBG0), DECOMP-FAITHFUL parallax. Scene.cpp:1471
        // scrollInfo->tilePos = scrollInfo->scrollPos + (camera.x * parallaxFactor << 8);
        // BG4 sInfo[0].parallax=0xC0 and AIZSetup sets scrollPos ONLY for bg2/bg3
        // (AIZSetup.c:172-179), NOT bg4 -> bg4 scroll = camera.x * 0xC0 >> 8 = x*0.75,
        // base offset 0. BG4 content is cols 436-767 -> the plane is faithfully EMPTY
        // when the camera maps to tiles <436 (Mania fills that with bg2/bg3 + sky; a lone
        // BG4 plane is correctly sparse -- the other BG layers are the next phase). Stream
        // the window from the resident full BG4 layout (AIZBG.L3) as the camera crosses a
        // tile. Vertical fixed (BG4 is a 16-tile horizontal band).
        // Task #309: this BG streaming reads the AIZBG.* cart staging (0x22498000+) that
        // p6_aiz_reload loads -- p6_ghzcut_reload does NOT (GHZCutscene has no 4-bpp BG
        // assets; its background comes from the FG tilemap layers, presented above). Guard
        // it to folder=="AIZ" so the GHZCutscene boot does not stream UNINITIALIZED cart
        // garbage onto NBG0/NBG2/NBG3. The FG-Low present above runs for both folders.
        unsigned int aizb_bg0 = p6_perf_vbl_count; // #302 bgstream bracket (vblank-stamped)
        if (strcmp(currentSceneFolder, "AIZ") == 0) {
            int cam_x  = screens[0].position.x;
            int bg4_sx = (cam_x * 0xC0) >> 8;   /* BG4 parallax 0.75 (sInfo 0xC0, scrollPos 0) */
            int bg1_sx = (cam_x * 0x40) >> 8;   /* BG1 parallax 0.25 (mid of its 3 bands)      */
            int bg3_sx = (cam_x * 0x80) >> 8;   /* BG3 parallax 0.5 (R2.6 single-scroll approx) */
            /* BG4 -> NBG0 (map B1, no source-wrap; 768 wide). */
            p6_vdp2_aiz_bg_stream(0, 0x25E70000u, (const unsigned short *)0x22498000u,
                                  768, 16, 0,
                                  (const unsigned char *)0x22495000u,
                                  (const unsigned char *)0x22496000u, bg4_sx, 0);
            /* BG1 -> NBG2 (map B0, source-wraps mod 80; the distant backdrop that
             * fills the screen where BG4 is empty). Shares the B1 CHR + CRAM banks. */
            p6_vdp2_aiz_bg_stream(1, 0x25E48000u, (const unsigned short *)0x2249E000u,
                                  80, 16, 1,
                                  (const unsigned char *)0x22495000u,
                                  (const unsigned char *)0x22496000u, bg1_sx, 0);
            /* BG3 -> NBG3 (map B0 @0x25E4C000, no source-wrap; 384 wide -> re-windows
             * like BG4). The distant mountains/island layer. Shares B1 CHR + CRAM. */
            p6_vdp2_aiz_bg_stream(2, 0x25E4C000u, (const unsigned short *)0x224A0000u,
                                  384, 16, 0,
                                  (const unsigned char *)0x22495000u,
                                  (const unsigned char *)0x22496000u, bg3_sx, 0);
            p6_vdp2_aiz_bg_frame(bg4_sx, bg1_sx, bg3_sx);
        }
#if defined(P6_GHZCUT_BOOT)
        // Task #309 #2b REAL FIX: arm the GHZ "BG Outside" sky (NBG0) behind the FG.
        // Supersedes the note above ("GHZCutscene has no 4-bpp BG assets") --
        // build_ghzcut_bg.py provides GHCBG.*, uploaded at load. Light horizontal
        // parallax off the camera (v1; the 109-band line-scroll is later polish).
        else if (strcmp(currentSceneFolder, "GHZCutscene") == 0
                 || strcmp(currentSceneFolder, "GHZ") == 0) {
            // #314 punch-list 2 (landing black sky): the sky NBG0 (GHZ "BG Outside",
            // the SAME decomp layer the playable zone uses) needs its per-frame
            // config + CRAM[64..127] re-assert (the #310 rule: the engine's bank0
            // palette flush + the FG present's slScrAutoDisp(NBG1ON|SPRON) final
            // arm both revert it every frame). Post-handoff currentSceneFolder is
            // "GHZ" -- extend the branch so the landing keeps the sky. The asset
            // load below is one-shot (s_ghcbg_loaded persists across the seam).
            // ONE-SHOT load of the BG Outside sky HERE (GFS idle), NOT in
            // p6_scene_load_and_arm -- MEASURED: the loose GFS_Open fails there
            // (pack + scene read hold both P6_GFS_OPEN_MAX=2 handles). By the
            // front-end frame a handle is free (HBHOBJ.SHT loads the same way in
            // p6_ghz_arm_env). Retries next frame until it succeeds. Scratch =
            // the AIZ-BG LWRAM window (AIZ inactive during GHZCut).
            static int32 s_ghcbg_loaded = 0;
            if (!s_ghcbg_loaded) {
                unsigned char *bchr = (unsigned char *)0x22480000u;
                unsigned char *bmap = (unsigned char *)0x22490000u;
                unsigned char *bpal = (unsigned char *)0x22494000u;
                int nchr = rsdk_storage_load_to_lwram("AGHCBG.CHR", bchr, 0x10000);
                int nmap = rsdk_storage_load_to_lwram("AGHCBG.MAP", bmap, 0x4000);
                int npal = rsdk_storage_load_to_lwram("AGHCBG.PAL", bpal, 0x200);
                if (nchr > 0 && nmap > 0 && npal > 0) {
                    p6_vdp2_ghzcut_bg_upload((const unsigned short *)bchr, nchr / 2,
                                             (const unsigned short *)bpal, npal / 2,
                                             (const unsigned short *)bmap, nmap / 2);
                    s_ghcbg_loaded = 1;
                }
            }
            int cam_x = screens[0].position.x;
            int bg_sx = (cam_x * 0x40) >> 8;   /* ~0.25 parallax */
            p6_vdp2_ghzcut_bg_frame(bg_sx);
        }
#endif
        p6_w_aiz_vbl_bgstream += (int32)(p6_perf_vbl_count - aizb_bg0); // #302 bgstream close
    }
#endif

#if defined(P6_FRONTEND_MENU) && defined(P6_MENU_LAYOUT320)
    // BISECT (recovery): the interrupted-agent 320 layout mechanism regressed the menu to
    // a BLACK SCREEN (M6/M6b/M7=0). Gated OFF behind P6_MENU_LAYOUT320 (undefined by
    // default) to restore the working clean-icons render while the 320 layout is redone
    // correctly. The force-currentScreen path below is the suspect (overrides positions +
    // forces the scroll origin pre-draw); re-enable only with a verified replacement.
    // M2b/M3 (Task #294/#295): apply the Saturn-native 320 menu layout BEFORE the draw.
    //   (1) The overlay's menu_layout_fn overrides the 4 UIModeButton world positions to
    //       the 320-fit grid + writes the active "Main Menu" control's scroll origin
    //       (position>>16 - center) into p6_w_menu_force_scrx/scry.
    //   (2) FORCE currentScreen->position to that origin here. This is the missing M2b
    //       CONSUMPTION -- the decomp UIControl_Draw (UIControl.c:52-53) writes
    //       ScreenInfo->position = FROM_FIXED(activeControl->position) - center, but that
    //       write is order-dependent and a 2nd visible (leaking) UIControl drawn later in
    //       the frame clobbers it back to (0,0) (MEASURED: scroll stayed (0,0), rows at raw
    //       world -> off-screen). Forcing it here makes the world->screen transform
    //       deterministic for the whole frame, independent of draw order. The -999999
    //       sentinel == the overlay has not yet found the active control (skip the force).
    if (s_ovl.menu_layout_fn)
        s_ovl.menu_layout_fn();
    if (currentScreen && p6_w_menu_force_scrx > -900000 && p6_w_menu_force_scry > -900000) {
        currentScreen->position.x = p6_w_menu_force_scrx;
        currentScreen->position.y = p6_w_menu_force_scry;
    }
#endif
#if defined(P6_GHZCUT_BOOT) && !defined(P6_MENU_LAYOUT320)
    // CHAIN MENU CENTERING (task chain-menu-rows-offscreen-scroll-2026-07-17):
    // at the chain Menu leg the menu LOGIC is fully alive (ctrl_count=27, Mania
    // Mode selected) but currentScreen->position stayed (0,0) -> the DrawSprite
    // world->screen subtract (:2587) left the rows at raw world (756,358) = off
    // the 320-wide screen (only 12 VDP1 cmds, no rows). ROOT (MEASURED): the
    // decomp UIControl_Draw (UIControl.c:52-53) writes ScreenInfo->position =
    // FROM_FIXED(activeControl->position) - center, but the Mania-side port
    // (src/mania/Objects/Menu/UIControl.c:183-186) writes the Mania MIRROR
    // ScreenInfo (Game.h:173, a ManiaScreenInfo), which is NOT the RSDK
    // screens[0]/currentScreen that DrawSprite reads -> the centering never
    // reached the engine scroll origin. NOT a seam stomp (p6_frontend_frame only
    // re-points currentScreen=&screens[0]; it does not zero .position).
    //
    // FIX: consume the overlay-computed active-control origin p6_w_menu_force_scrx/
    // scry (p6_ovl_ghz.c:1801-1802 = activeControl->position>>16 - (160,112),
    // written every frame by the P6_FRONTEND_MENU witness -- the chain compiles
    // P6_FRONTEND_MENU via P6_AIZ_TEST) and FORCE it into currentScreen->position
    // BEFORE ProcessObjectDrawLists. This mirrors UIControl_Draw's own formula
    // applied to the GUARANTEED active control, so world 756 -> screen 160 (center).
    // Menu leg only (folder=="Menu"); every other chain leg (AIZ/GHZCutscene/GHZ)
    // drives currentScreen->position from its own camera and is untouched.
    // -999999 sentinel == the overlay has not found the active control yet (skip).
    // Plain-GHZ + menu-boot (P6_FRONTEND_MENU without P6_GHZCUT_BOOT) never compile
    // this block -> byte-identical.
    if (currentScreen && currentSceneFolder && !strcmp(currentSceneFolder, "Menu")
        && p6_w_menu_force_scrx > -900000 && p6_w_menu_force_scry > -900000) {
        currentScreen->position.x = p6_w_menu_force_scrx;
        currentScreen->position.y = p6_w_menu_force_scry;
    }
#endif
    p6_w_perf_cyc_fgbg = P6_FRT_DELTA(fe_fgbg_t0, p6_perf_frt_get()); // #322 (see above)
    fe_v0 = p6_perf_vbl_count; fe_t0 = p6_perf_frt_get();
#if defined(P6_FRONTEND_MENU)
    unsigned short fe_tpod = fe_t0; // #324: tail bracket start (updated post-dl_end)
#endif
#ifndef P6_TITLE_NODRAW
#if defined(P6_FRONTEND_MENU)
    unsigned int aizb_dl0 = p6_perf_vbl_count; // #302 vdp1emit bracket (vblank-stamped)
#endif
#if defined(P6_DIRECT_VDP1)
    // #316 F1: open the direct command list -- every blit inside
    // ProcessObjectDrawLists (and the FillScreen fades) writes a VDP1 command
    // into the inactive half in the decomp's own draw-list order.
    p6_dl_begin();
    // SEGMENT A (#318): the Logos splash arms SPRON-only (no VDP2 backdrop) so the
    // stale VDP1 framebuffer shows through in the bottom rows (the pink #272 noise
    // band, 320x224-vs-320x240 geometry). Emit an opaque black quad FIRST (behind
    // every UIPicture sprite) to cover it. Logos-ONLY: Title/Menu/AIZ/GHZCutscene
    // composite VDP2 backdrops an opaque VDP1 quad would occlude.
    if (currentSceneFolder && !strcmp(currentSceneFolder, "Logos"))
        p6_dl_backfill(0x8000u);
#endif
#if defined(P6_FRONTEND_TITLE)
    p6_w_tsonic_emitseq = -9; /* task #326 emit-order forensic: per-frame reset */
    p6_w_emblem_emitseq = -9;
    p6_w_frame_maxemit  = -9;
#endif
    ProcessObjectDrawLists(); // emits the UIPicture VDP1 sprite list (jo swaps it)
#if defined(P6_FRONTEND_MENU)
    // M2a (qa_menu_layout.py): latch currentScreen->position AFTER the draw lists ran --
    // with the force-set above the latched value should read the forced origin (692,264),
    // confirming the rows used the correct transform (UIControl_Draw can no longer leave
    // it (0,0) because the force-set ran first AND the overlay re-asserts each frame).
    p6_menu_layout_scroll_latch();
    p6_menu_start_witness_root(); // M2: gc-root the start-game witnesses (overlay -R import)
#endif
#if defined(P6_GHZCUT_BOOT) && defined(P6_DIRECT_VDP1)
    // GL1 (2026-07-06): emit the GHZ act card LAST into the open direct list so
    // its colored strips/BG/decor paint OVER every gameplay sprite (VDP1 is
    // painter-order: last command = in front). Only at the GHZ landing; no-op
    // once the card slides away (state DONE). This is the chain analogue of the
    // src/mania titlecard_draw_only, but through p6_dl_face/p6_dl_rect so it
    // lands in the LIVE (displayed) command list rather than the dead SGL one.
    if (currentSceneFolder && !strcmp(currentSceneFolder, "GHZ"))
        p6_titlecard_draw();
#endif
#if defined(P6_DIRECT_VDP1)
    // #316 F1: close + publish the list (END command, half flip, trampoline
    // target). A seam/LOAD frame that returned earlier never reaches this --
    // the previous completed half keeps displaying (persistence contract).
    p6_dl_end();
#endif
#if defined(P6_AIZ_TEST) && defined(P6_FE_SLAVE_PRESENT)
    // #325 stage-1: join the slave FG present (jo_core_wait_for_slave ->
    // slCashPurge so the master sees the slave's WRAM/cart writes, ST-202 §7)
    // + run the master-only NBG1 register config. With p6_vdp2_bg_owns_disp
    // latched at the settled GHZ landing, p6_present_config emits NO
    // slScrAutoDisp (p6_vdp2.c:2158-2171) -- the 4-plane sky arm survives.
    // Then re-assert the sky CRAM banks [64..127]: the slave's CRAM bank0
    // rewrite (p6_present_compute :2117-2123) ran AFTER this frame's
    // p6_vdp2_ghzcut_bg_frame re-assert -- restore the sync frame-end order.
    if (fe_present_kicked) {
        p6_present_join_config(screens[0].position.x, screens[0].position.y);
#if defined(P6_GHZCUT_BOOT)
        p6_vdp2_ghzcut_bg_pal_reassert();
#endif
    }
#endif
#if defined(P6_FRONTEND_MENU)
    p6_w_aiz_vbl_vdp1emit += (int32)(p6_perf_vbl_count - aizb_dl0); // #302 vdp1emit close
    fe_tpod = p6_perf_frt_get(); // #324: DrawLists+emit done; tail (fade/card/diag) follows
#endif
#else
    // CP5b.7 A/B: skip ALL title VDP1 sprite emit (no sprites added to the SGL sort
    // list) to isolate whether slSynch's measured ~90ms/frame jo-body wait is the
    // draw-sort/transfer of the title sprites (fps jumps if so -> fix = cut draw) or
    // a draw-INDEPENDENT cadence/audio stall (fps unchanged -> different lever).
#endif
#if defined(P6_GHZCUT_BOOT)
    // Task #309 Tier-B.1: drive the FXRuby full-screen fade as a VDP2 Color Offset
    // (ST-058-R2 Ch.13) -- the RSDKv5 software-framebuffer FillScreen the decomp
    // FXRuby_Draw emits is never presented by the true-port. The overlay reads the
    // live FXRuby fade fields (and, under P6_GHZCUT_HOLD, pins them to freeze the
    // cutscene at a visible wash); the engine applies the offset to NBG1+SPR here,
    // AFTER ProcessObjectDrawLists so it is the last VDP2 state before slSynch.
    //
    // GL1 SATURATION FIX (2026-07-06): the TitleCard's colored strips are VDP1
    // sprites on the SPR layer, which this fade offsets (slColOffsetAUse includes
    // SPRON). A residual FXRuby WHITE fade lingering at the GHZ landing therefore
    // washes the card pastel (MEASURED _shots/214050: the 0x000000 decor bar
    // rendered GRAY = ~+64 white offset). In the decomp the FXRuby arrival flash
    // COMPLETES before the TitleCard slides in (the card shows on a clean screen;
    // TitleCard_State_SetupBGElements runs after FXRuby's RubyFX/fade), so any
    // offset still set when the card is up is a Saturn artifact of the fade not
    // clearing post-handoff. While the card is active, force the offset OFF
    // (CLOFEN clear, ST-058-R2 Ch.13) so the strips render at SOURCE saturation.
    // Gated to the GHZ landing + card-active window; the arrival flash itself
    // (before the card spawns) is untouched. p6_w_ghzcut_fade records the raw
    // FXRuby value for the RED-first witness even when we override it.
    if (s_ovl.fade_fn) {
        int fade_w = 0, fade_b = 0;
        s_ovl.fade_fn(&fade_w, &fade_b);
#if defined(P6_GHZCUT_BOOT) && defined(P6_DIRECT_VDP1)
        if (currentSceneFolder && !strcmp(currentSceneFolder, "GHZ")
            && p6_titlecard_is_active()) {
            p6_vdp2_fade_apply(0, 0); // clear the wash so the card renders saturated
        } else
#endif
            p6_vdp2_fade_apply(fade_w, fade_b);
        p6_w_ghzcut_fade = (fade_w << 16) | (fade_b & 0xFFFF);
    }
#if defined(P6_GHZCUT_BOOT) && defined(P6_DIRECT_VDP1) && !defined(P6_PERF_NOSCAN)
    // GL1 wash diagnostic: read back the ACTUAL VDP2 colour-offset registers
    // (ST-058-R2 Ch.13: CLOFEN 0x25F80110 enable-mask, CLOFSL 0x25F80112 A/B
    // select, COAR 0x25F80114 red offset) so a persistent wash is measured, not
    // guessed. If CLOFEN!=0 / COAR!=0 while the card is up, the offset is the
    // wash source and slColOffsetOff did not take (SGL per-vblank rewrite, field
    // gotcha #12). Packed: (CLOFEN<<16)|COAR (COAR is a signed 9-bit two's-comp).
    // #324: this whole block (256-entry CRAM djb2 + 768-byte VDP1-VRAM djb2 +
    // CRAM/VDP2 register reads) ran EVERY shipping frame inside the DrawLists
    // FRT bracket -- a per-frame diagnostic in the hot loop (the
    // perf-diagnostic-in-hotloop rule: CPU reads of CRAM/VDP1-VRAM during active
    // display eat bus-priority wait states). NOSCAN-stripped from shipping;
    // rebuild with P6_NOSCAN= (empty) to re-arm the GL1 witnesses.
    {
        volatile uint16 *vr = (volatile uint16 *)0x25F80110u;
        p6_w_tc_clofen = ((int32)vr[0] << 16) | (int32)vr[2]; // [0]=CLOFEN [2]=COAR
        // GL1 glyph-palette diagnostic: read back CRAM block 2 (the Display GCT the
        // glyphs sample). CRAM[512] word address = 0x25F00000 + 512*2 = 0x25F00400.
        // idx 33 (dark gray 0xA0C7) at [33], idx 255 (white 0xFFFF) at [255].
        volatile uint16 *cr = (volatile uint16 *)(0x25F00000u + 512u * 2u);
        p6_w_tc_cram2 = ((int32)cr[33] << 16) | (int32)cr[255]; // [33]=gray [255]=white
        // idx 0/1 (should be 0x8000 black each) + a hash of all 256 block-2 entries
        // (compare to DISPCARD). And read the glyph VRAM byte 0 + a hash: the glyph's
        // CMDSRCA*8 gives the VRAM addr; the last glyph (act "1", jid 59) is at a
        // known addr. Read the FIRST glyph's VRAM (captured addr) to see the pixel
        // indices actually in VRAM (magenta => index 0; letter => 1/33/255).
        p6_w_tc_cram01 = ((int32)cr[0] << 16) | (int32)cr[1];
        {
            unsigned int hh = 5381; int k;
            for (k = 0; k < 256; ++k) hh = ((hh << 5) + hh) ^ cr[k];
            p6_w_tc_cram2hash = (int32)hh;
        }
        // Glyph VRAM readback: p6_w_tcg_cmd low 16 = CMDSRCA (addr/8). VRAM byte addr
        // = JO_VDP1_VRAM(0x25C00000) + CMDSRCA*8. Read the first 4 pixel bytes + hash
        // 768 bytes so we know if the VRAM holds the staged letter indices or garbage.
        extern int p6_w_tcg_cmd;
        {
            unsigned int srca = (unsigned int)(p6_w_tcg_cmd & 0xFFFF);
            volatile uint8 *gv = (volatile uint8 *)(0x25C00000u + srca * 8u);
            unsigned int hh = 5381; int k;
            for (k = 0; k < 768; ++k) hh = ((hh << 5) + hh) ^ gv[k];
            p6_w_tc_gvramhash = (int32)hh;
            p6_w_tc_gvram0 = ((int32)gv[0] << 24) | ((int32)gv[1] << 16)
                           | ((int32)gv[2] << 8) | (int32)gv[3];
        }
    }
#endif
#endif
    fe_t1 = p6_perf_frt_get(); fe_v1 = p6_perf_vbl_count;
    p6_w_perf_cyc_draw = P6_FRT_DELTA(fe_t0, fe_t1);
    p6_w_perf_vbl_draw = (int32)(fe_v1 - fe_v0);
#if defined(P6_FRONTEND_MENU)
    p6_w_draw_tail = P6_FRT_DELTA(fe_tpod, fe_t1); // #324: fade/card/diag epilogue cost
#endif

    p6_w_perf_cyc_total = p6_w_perf_cyc_input + p6_w_perf_cyc_obj
                        + p6_w_perf_cyc_draw + p6_w_perf_cyc_present;

#if defined(P6_GHZ_AUTORUN)
    // Signpost campaign bridge-1 forensics (CHAIN frame path -- the p6_ghz_frame
    // copy at :3800 never runs in the chain flavor; run-2 measured inspan=0).
    // Same logic: while SLOT_PLAYER1 crosses x 800..1500 in GHZ, scan for a live
    // Bridge entity at bridge-1 (authored (1184,904), Scene1.bin).
    if (RSDK::currentSceneFolder[0] == 'G' && RSDK::currentSceneFolder[1] == 'H'
        && RSDK::currentSceneFolder[2] == 'Z' && RSDK::currentSceneFolder[3] == 0) {
        EntityBase *p0 = RSDK_ENTITY_AT(0);
        int32 plrx = p0->position.x >> 16;
        /* 2026-07-12 stall diagnostic: capture the live player physics EVERY GHZ frame
         * (the p6_ghz_frame copy never runs in the chain flavor). Settles the X~14400
         * signpost blocker: high gvel + pinned posx == a collision/plane WALL; ~0 gvel
         * == the autorun input stopped. EntityBase exposes the RSDK_ENTITY base fields. */
        if (p0) {
            p6_w_pdiag_posx  = plrx;
            p6_w_pdiag_gvel  = (int32)p0->groundVel;
            p6_w_pdiag_velx  = (int32)p0->velocity.x;
            p6_w_pdiag_gnd   = (int32)(p0->onGround ? 1 : 0);
            p6_w_pdiag_plane = (int32)p0->collisionPlane;
            p6_w_pdiag_mode  = (int32)p0->collisionMode;
            p6_w_pdiag_dir   = (int32)p0->direction;
            p6_w_pdiag_ang   = (int32)p0->angle;
        }
        if (p0->classID && plrx > 800 && plrx < 1500 && p6_w_brg_classid > 0) {
            int32 live = 0, act = -1;
            for (int32 bi = 0; bi < ENTITY_COUNT; ++bi) {
                EntityBase *be = RSDK_ENTITY_AT(bi);
                if (be->classID == (uint16)p6_w_brg_classid) {
                    int32 bx = be->position.x >> 16;
                    if (bx > 1150 && bx < 1220 && (be->position.y >> 16) < 1200) {
                        live = 1;
                        act  = (int32)be->active;
                        break;
                    }
                }
            }
            p6_w_arun_brg_live   = live;
            p6_w_arun_brg_active = act;
            if (live && p6_w_arun_brg_firstx < 0)
                p6_w_arun_brg_firstx = plrx;
            if (plrx >= 1080 && plrx <= 1290) {
                ++p6_w_arun_inspan;
                if (!live)
                    ++p6_w_arun_brg_gapmiss;
            }
        }
    }
#endif
    ++p6_w_cont_frames; // E5: engine reached ENGINESTATE_REGULAR + is ticking

    // CP5b.7: frame-end true-vblank tally + compute-full bracket (mirrors
    // p6_ghz_frame:3033-3085). vbl_frame = vblanks consumed INSIDE p6_frontend_frame
    // (the master compute cost); fps = 60*frames/vblanks; cks -> ticks->us at the gate.
    {
        unsigned int vnow = p6_perf_vbl_count;
        int32 slip = (int32)(vnow - p6_perf_vbl_prev);
        p6_w_perf_vbl_frame = (int32)(vnow - fe_vbl_start);
#if defined(P6_FRONTEND_MENU)
        p6_w_aiz_vbl_framesum += p6_w_perf_vbl_frame; // #302: jo-body = d(vblanks) - d(this)
#endif
        p6_perf_vbl_prev    = vnow;
        p6_w_perf_vblanks   = (int32)vnow;
        p6_w_perf_frames    = p6_w_cont_frames;
        if (slip > p6_w_perf_vbl_max) p6_w_perf_vbl_max = slip;
        if (p6_w_perf_cks < 0) p6_w_perf_cks = p6_perf_frt_cks();
        unsigned short fe_frame_t1 = p6_perf_frt_get();
        p6_perf_frt_prev_end = fe_frame_t1;
        p6_w_perf_full_frt   = P6_FRT_DELTA(fe_frame_t0, fe_frame_t1);
        if (p6_w_perf_full_frt > p6_w_perf_full_max) p6_w_perf_full_max = p6_w_perf_full_frt;
        // EDSR.CEF (bit1) at compute-done -- the LATEST point before the implicit
        // slSynch. CEF=0 => VDP1 still rasterizing the PRIOR frame's sprite list =>
        // the title is VDP1-DRAW-BOUND (a huge sprite command list -> the fix is
        // draw reduction, NOT resident sheets); CEF=1 => VDP1 idle => CPU/compute-
        // bound (the 4 cyc_* sections name the cut). Read-only VDP1 reg peek.
        unsigned short edsr = p6_perf_vdp1_edsr();
        p6_w_perf_v1_edsr = (int32)edsr;
        if (edsr & 0x0002u) {
            ++p6_w_perf_v1_done;
        } else {
            ++p6_w_perf_v1_busy;
            p6_w_perf_v1_copr = (int32)p6_perf_vdp1_copr();
            p6_w_perf_v1_lopr = (int32)p6_perf_vdp1_lopr();
        }
    }

    // Write the front-end classID witnesses (E2/E3) -- the overlay's witness fn
    // latches LogoSetup/UIPicture->classID via the -R import (p6_ghz_ovl_witness).
    // For the Menu flavor it ALSO writes M6 (MenuSetup->mainMenu!=NULL ->
    // p6_w_menu_treebuilt) + M6b (UIModeButton->classID) -- those need the Mania
    // Game.h types, so they live in the overlay witness, not here.
    if (s_ovl.witness_fn)
        s_ovl.witness_fn(RSDK_ENTITY_AT(P6_OBJ_RING_SLOT));
    // Backend-parity: fingerprint the composited CRAM for the oracle (blind
    // colour/flash/blank detection across EVERY scene the chain visits).
    p6_cram_witness();
    // Backend-parity: sample the audio channels every frame (Nyquist-proof) so
    // the oracle can prove gameplay-SFX / BGM-stream activity blind (GAP-A1).
    p6_audio_witness();
#if defined(P6_FRONTEND_MENU)
    // M7: latch the global VDP1 landed-blit count so the gate can assert the menu
    // emitted >=1 sprite command (the M1a black-screen RED was landed==0). This is a
    // pack global (p6_vdp1.c), latched here in the pack frame.
    {
        extern int p6_w_vdp1_landed; /* p6_vdp1.c */
        p6_w_menu_vdp1_landed = (int32)p6_w_vdp1_landed;
    }
#endif
#if defined(P6_FRONTEND_TITLE)
    // #313 forensic status (2026-07-02, 8 RED builds -- full record in memory
    // sgl-slave-sprite-pipeline-internals.md): the title FG dies ~1.5 s after
    // settle (FlashIn end). REFUTED interventions (each built + gated RED):
    // island/backdrop/clouds A-Bs, frame-end mailbox drain (drain_max=0 -- never
    // needed), frame-end slDMAWait, FillScreen-quad suppression, slInitSprite
    // re-arm (predicate never fired: plan count nonzero at frame start), and
    // forcing GBR+0x72=1. The frame-END drain/slDMAWait variants additionally
    // BROKE the healthy window (11 cmds transferred but FB empty at frame 75)
    // -- do NOT reintroduce frame-end SGL/DMA waits here. Dead-state facts:
    // inserts accepted, SpriteBuf builds frozen at the flash content, DMA plan
    // empty, all workarea/limit/texture state intact. Next instruments: the
    // Mednafen interactive debugger (slave-side breakpoint in the op24/op28
    // build fns) or a LIBSGL dispatch-histogram patch. The full-chain video
    // dwells on the settled title < 1.5 s, inside the healthy window.
#endif
}
#endif // P6_FRONTEND_LOGOS

// =============================================================================
// P6.8 Step B (Task #211): p6_engine_boot_and_run -- the LEAN engine SHIPPING
// boot entry. main.c's -DP6_ENGINE_SHIPPING branch calls this INSTEAD of the
// hand-port boot, then registers p6_scene_tick + jo_core_run (the jo calls stay
// in main.c -- this pack TU is compiled WITHOUT jo/jo.h, per build_p6scene_objs
// note). This sets the lean flag and runs p6_scene_run, which -- with the flag
// set -- executes only the masked load core (InitStorage..LoadGameConfig + the
// staged sheets/bands/anim-pack + audio) and returns at the lean early-return.
// The first p6_scene_tick then re-loads GHZ live and arms the continuous loop.
// No diagnostic burst, no Title reload, no legacy Ring. Reversible: unset
// P6_ENGINE_SHIPPING and main.c boots the hand-port (this entry is unreached).
// =============================================================================
// P6.8 I3b.1: build the IDENTITY remap (de-risk; the SHRINK replaces this body with the real
// logical->physical assignment + free-list). Cache-through cart writes land immediately (no
// purge dance). Self-checks the table is identity + readable -> p6_w_remap_ok (gate
// qa_p6_i3b.py). Runs first thing in the boot so the table is ready before any entity access.
__attribute__((used)) int32 p6_w_remap_ok = 0;
extern "C" void p6_pool_remap_init(void)
{
    for (int32 i = 0; i < ENTITY_COUNT; ++i) {
        RSDK::p6_pool_remap[i]     = (uint16)i;
        RSDK::p6_pool_remap_inv[i] = (uint16)i; // identity inverse (2a); the shrink fills both non-trivially
    }
    RSDK::p6_pool_remap_ready = 1;
    int32 ok = 1;
    for (int32 i = 0; i < ENTITY_COUNT; ++i)
        if ((int32)RSDK::p6_pool_remap[i] != i) { ok = 0; break; }
    p6_w_remap_ok = ok;
}
// P6.8 I3b 2b COMPACTION -- the relocation MANAGER lives in the GHZ CART OVERLAY (p6_ovl_pool_compact,
// p6_ovl_ghz.c) per the residency rule (new engine code -> cart, NOT WRAM-H; the pack-placed version
// overflowed P6_HW_ANIMPAK by 80 B = #228 trap, MEASURED 2026-06-20). The overlay does the relocation
// with LITERAL cart/WRAM-L addresses + writes the p6_w_compact_* witnesses directly (ld -R game.elf).
// The ONE engine-touching op it cannot do itself is the ATOMIC flip of the namespace-RSDK pool ints --
// this thin pack thunk. Called LAST by the overlay (after the remap is fully built + data relocated);
// scene_phys is set AFTER dummy so the accessor never sees a half-built (remap-new, sphys-old) state.
extern "C" __attribute__((used)) void p6_eng_pool_flip(int32 sphys, int32 dummy)
{
    RSDK::p6_pool_dummy_slot = dummy;
    RSDK::p6_pool_scene_phys = sphys;
}
// I3b 2b: hands the overlay the pool GEOMETRY (the pack owns the struct layout + the #defines; the
// flat-TU overlay must not hardcode them -- a struct/define drift would silently misclassify). The
// overlay does pure byte-math with these. out[]= {classID byte offset, sizeof(EntityBase)=NARROW,
// RESERVE, SCENEENTITY_COUNT, TEMPENTITY_COUNT, ENTITY_WIDE_SIZE}.
extern "C" __attribute__((used)) void p6_eng_pool_geom(int32 *out)
{
    out[0] = (int32)__builtin_offsetof(RSDK::EntityBase, classID);
    out[1] = (int32)sizeof(RSDK::EntityBase);
    out[2] = RESERVE_ENTITY_COUNT;
    out[3] = SCENEENTITY_COUNT;
    out[4] = TEMPENTITY_COUNT;
    out[5] = ENTITY_WIDE_SIZE;
    out[6] = P6_POOL_SCENE_PHYS; // the shrunk physical scene-slot count (640)
}
// I3b 2b STREAMING: re-CREATE a materialized entity. p6_ovl_materialize writes PLACEMENT only (classID +
// position + editable vars); the entity is INERT until Create runs its animator/state setup. This mirrors
// InitObjects (Object.cpp:362-374) EXACTLY -- the overlay sets remap[L]=P first, then materialize(L,L)
// writes to physical P (entity_prepare applies remap) and calls this to Create it live. Create runs
// MID-ProcessObjects (after the camera update, before loop1) -- supported (CreateEntity is called mid-frame
// by gameplay); the R3 PlaneSwitch RED->GREEN is the proof.
extern "C" __attribute__((used)) void p6_eng_create(int32 slot)
{
    using namespace RSDK;
    sceneInfo.entitySlot = slot;
    sceneInfo.entity     = RSDK_ENTITY_AT(slot);
    int32 cid = (int32)sceneInfo.entity->classID;
    if (cid && objectClassList[stageObjectIDs[cid]].create) {
        sceneInfo.entity->interaction = true;
        objectClassList[stageObjectIDs[cid]].create(NULL);
    }
}
// I3b 2b STREAMING: per-frame tick forwarder. Object.cpp (ProcessObjects) calls this AFTER
// p6_scan_update_near (the live near-set) + BEFORE loop1; it routes to the overlay-resident stream manager
// (s_ovl.stream_fn) which materializes newly-near + dormants newly-far. NULL-safe pre-overlay-load.
extern "C" void p6_stream_tick(void)
{
#if defined(P6_FRONTEND_LOGOS)
    // #327 companion (chain flavors only; plain GHZ byte-identical): the stream
    // manager is GHZ-gameplay machinery, but after a game-over the Menu reload
    // leaves the compact state armed and this tick kept churning in the Menu
    // (MEASURED: stream_mat 86 -> 1106, starve 0 -> 392 across GHZ->Menu). With
    // the DORM store now chain-loaded (#327), a non-GHZ materialize would
    // resolve GLOBAL classes (ItemBox/Debris) and inject GHZ1 records into the
    // front-end pool. Folder-gate the dispatch: the stream runs only while the
    // GHZ gameplay scene is live (GHZ1/GHZ2 share folder "GHZ").
    if (RSDK::currentSceneFolder[0] != 'G' || RSDK::currentSceneFolder[1] != 'H'
        || RSDK::currentSceneFolder[2] != 'Z' || RSDK::currentSceneFolder[3] != 0)
        return;
#endif
#if defined(P6_STREAM_PERF)
    /* PERF #2 measurement (diag-only): bracket the per-frame stream scan to isolate its cost from the
       cyc_obj (ProcessObjects) total -- same coherent-16-bit-FRC idiom (p6_perf_frt_get + P6_FRT_DELTA)
       p6_ghz_frame uses for its sections. This call already runs inside the cyc_obj bracket. */
    if (s_ovl.stream_fn) {
        unsigned short _s0 = p6_perf_frt_get();
        s_ovl.stream_fn();
        p6_w_perf_stream_frt = P6_FRT_DELTA(_s0, p6_perf_frt_get());
    }
#else
    if (s_ovl.stream_fn)
        s_ovl.stream_fn();
#endif
}
// =============================================================================
// P6.8 I3c (camera-local pool, the FPS WIN): SPATIAL-CULL loop1. The GHZ scan cost is
// the ACTIVE_BOUNDS per-camera position READS (Object.cpp:589-615: ~757 entities read
// position.x/y from slow WRAM-L EVERY frame). A sorted-by-x index of the populated SCENE
// entities (built once per scene load) + a per-frame near-bitfield lets loop1 set
// inRange=false for FAR scene entities WITHOUT the position read -- skipped via a fast
// WRAM-H bit. PROVABLY CONSERVATIVE: a slot is culled only when |spawn_x - cam_x| > WINDOW
// (768 px); the engine's full ACTIVE_BOUNDS/XBOUNDS check needs |cur_x - cam_x| <=
// updateRange.x+offset (~576 px for GHZ), so the 768 window (+192 px mover-drift margin;
// GHZ badniks patrol ~64) can NEVER exclude an entity the full check would keep. Reserve +
// temp + ACTIVE_ALWAYS/NORMAL are always processed (only ACTIVE_BOUNDS/XBOUNDS scene
// entities are culled -- the x-condition makes x-far => out-of-range). Parity-safe for GHZ1
// per ghz-scan-split-parity-audit (no foreign mid-frame reposition; serial, same engine
// order). Full backing + IDENTITY remap kept (SaturnEntityAt byte-identical, no dormant-
// access). The WRAM-L-saving COMPACTION is the separate later step. Gate qa_p6_scancull.py.
__attribute__((used)) unsigned char p6_scan_near[(ENTITY_COUNT + 7) / 8] = { 0 }; // WRAM-H bitfield
static uint32 *p6_scan_sorted = (uint32 *)0x226DDD00u; // RELOCATED 0x226B9000->0x226DDD00 (DDWrecker
                                                       // re-budget 2026-07-11; 4352 B ends 0x226DEE00).
                                                       // cart; SCENEENTITY_COUNT*4 = 4352 B (0x226B9000..
                                                       // 0x226BA100), whole-game-sized (was 800*4=3200 B)
__attribute__((used)) int32 p6_scan_n          = 0;
#if defined(P6_FRONTEND_MENU)
// #325 lever (i) tail-bound: the highest POPULATED scene slot at index-build time. When
// the FE cull is armed the #317 empty-middle trim is disabled (culled slots never raise
// its high-water), so the cull bit-test otherwise walks all 1088 scene slots. Loop1
// early-exits the scene walk past this (+96 spawn margin, the trim's own contract:
// runtime spawns go to TEMP; scene-region ResetEntitySlot above high-water was already
// invisible to the trim). Default = full region (safe until the first index build).
// FE-gated: plain GHZ pack stays byte-identical (its WRAM-H ceiling has ~64 B headroom).
__attribute__((used)) int32 p6_scan_idx_maxslot = RESERVE_ENTITY_COUNT + SCENEENTITY_COUNT;
#endif
__attribute__((used)) int32 p6_w_scancull_n    = 0; // index entries (witness)
__attribute__((used)) int32 p6_w_scancull_near = 0; // near bits set on the last update (witness)
__attribute__((used)) int32 p6_w_scancull_capped = 0; // WHOLE-GAME: 1 if the index hit P6_SCAN_CAP before
                                                      // scanning all scene slots (un-indexed -> skip bug)
// I3d (loop1 LIVE-ITERATION = the 60fps step): I3c iterated all 1216 slots and used the
// near bit to skip only the FAR scene entities' POSITION read; the 1216-slot iteration
// itself (classID/active touches of slow WRAM-L) still cost ~9.88ms -> compute-full
// 19.66ms > the 16.67ms 1-vblank cliff -> 30fps. I3d SKIPS the far scene slots ENTIRELY
// in loop1 (no WRAM-L touch). But the x-window only bounds ACTIVE_BOUNDS/XBOUNDS; ACTIVE_
// NORMAL/ALWAYS/YBOUNDS/RBOUNDS (managers, vertically-tracked, triggered) are NOT x-cullable
// and MUST always be iterated. p6_scan_always marks every populated scene slot whose active
// is NOT BOUNDS/XBOUNDS -- seeded at load from the Create-time active (below) + maintained
// per-frame in loop1 (Object.cpp) for runtime active-type changes -- and is OR'd into
// p6_scan_near each frame so loop1 tests ONE WRAM-H bit. CART-resident (read once/frame in
// the merge -> ZERO WRAM-H data). The skip-set is then IDENTICAL to I3c's proven {scene,
// BOUNDS/XBOUNDS, x-far} cull set (qa_p6_scancull GREEN, R0-R16) -- parity-exact, just no
// WRAM-L re-walk of the far majority. Monotonic always-set (never cleared; bounded ~managers).
unsigned char *p6_scan_always = (unsigned char *)0x226DEF00u; // RELOCATED 0x226BB000->0x226DEF00
                                                              // (DDWrecker re-budget 2026-07-11; 153 B
                                                              // bitmap ends 0x226DEF99, < s_stage 0x226E0000)
__attribute__((used)) int32 p6_w_scan_always = 0; // always-iterate scene slots seeded at load (witness)
#define P6_SCAN_WINDOW 1024 // world px each side of the camera. MUST be >= max(updateRange.x+offset)
                            // + max mover drift, or a near entity is wrongly culled (it freezes).
                            // 1024 covers updateRange.x up to ~700 + offset 320; far-majority still
                            // culled in a ~10000px level. scancull_near witness measures the actual
                            // near-set -> tune down later if it is too generous (less win).
#define P6_SCAN_CAP    SCENEENTITY_COUNT // = 1088 = the FULL scene region (the pool ceiling). WHOLE-GAME
                            // scalable: covers EVERY scene's max populated count -- GHZ1 alone is 1034
                            // (not 765), worst PSZ2/SPZ1 ~2016 dropped to 1088 by the pool's entity-drop
                            // wall. Was 800 = a GHZ1-ism that under-covered even GHZ1 -> populated slots
                            // past the 800th were UN-indexed -> their ACTIVE_BOUNDS entities got no near
                            // bit -> wrongly skipped (silent: R3 counts PlaneSwitch, not whether all tick).
                            // The index insertion-sort is O(n^2) one-time at load (a faster sort = future opt).
// Built once per scene load (post-InitObjects). (spawn_x << 11)|slot for every populated
// scene slot, sorted by x via insertion sort (n<=765, one-time -- ~64ms in the long GHZ load).
extern "C" void p6_scan_index_build(void)
{
    int32 n = 0, e;
    for (e = RESERVE_ENTITY_COUNT; e < TEMPENTITY_START && n < P6_SCAN_CAP; ++e) {
        EntityBase *ent = RSDK_ENTITY_AT(e);
        if (!ent->classID)
            continue;
        int32 xw = (int32)(ent->position.x >> 16);
        if (xw < 0)
            xw = 0;
        p6_scan_sorted[n++] = ((uint32)(xw & 0x1FFFFF) << 11) | (uint32)(e & 0x7FF);
    }
    // WHOLE-GAME truncation witness: the loop stopped on the CAP (n==P6_SCAN_CAP) BEFORE the whole
    // scene region was scanned (e < TEMPENTITY_START) -> some populated scene slots are UN-indexed ->
    // their ACTIVE_BOUNDS entities would be wrongly skipped. With P6_SCAN_CAP == SCENEENTITY_COUNT (the
    // pool ceiling) this can NEVER fire; it RED-gates a future cap-lowering or scene-region growth.
    p6_w_scancull_capped = (e < TEMPENTITY_START) ? 1 : 0;
    for (int32 i = 1; i < n; ++i) {
        uint32 key = p6_scan_sorted[i];
        int32 j    = i - 1;
        while (j >= 0 && p6_scan_sorted[j] > key) { p6_scan_sorted[j + 1] = p6_scan_sorted[j]; --j; }
        p6_scan_sorted[j + 1] = key;
    }
    p6_scan_n       = n;
    p6_w_scancull_n = n;
#if defined(P6_FRONTEND_MENU)
    // #325: record the highest populated scene slot for the cull-armed tail-bound.
    // n==0 keeps the full-region default (the cull never arms with an empty index).
    {
        int32 mx = RESERVE_ENTITY_COUNT;
        for (int32 i = 0; i < n; ++i) {
            int32 s = (int32)(p6_scan_sorted[i] & 0x7FF);
            if (s > mx) mx = s;
        }
        p6_scan_idx_maxslot = (n > 0) ? mx : (RESERVE_ENTITY_COUNT + SCENEENTITY_COUNT);
    }
#endif
    // I3d: seed the always-iterate bitfield from the Create-time active. A populated scene
    // slot whose active is NOT x-cullable (BOUNDS/XBOUNDS) is pinned always-iterate so the
    // camera moving past its spawn-x can never wrongly skip it (it would stop updating).
    for (int32 i = 0; i < (int32)((ENTITY_COUNT + 7) / 8); ++i)
        p6_scan_always[i] = 0;
    int32 alw = 0;
    for (int32 e = RESERVE_ENTITY_COUNT; e < TEMPENTITY_START; ++e) {
        EntityBase *ent = RSDK_ENTITY_AT(e);
        if (!ent->classID)
            continue;
        uint8 av = (uint8)ent->active;
        // x-CULLABLE iff BOUNDS/XBOUNDS AND its x half-extent (+512 px for camera offset + mover
        // drift) fits the window. A WIDER object's true in-range span exceeds the window -> the
        // x-cull would wrongly skip it -> pin always-iterate. This REMOVES the WHOLE-GAME WINDOW >=
        // max(updateRange.x) ASSUMPTION (GHZ1 happens to fit, but un-ported wide objects -- bosses,
        // long platforms -- may not). updateRange.x is fixed-point (>>16 = px), set in Create.
        int32 rngx = (int32)(ent->updateRange.x >> 16);
        if ((av != ACTIVE_BOUNDS && av != ACTIVE_XBOUNDS) || (rngx + 512 > P6_SCAN_WINDOW)) {
            p6_scan_always[e >> 3] |= (unsigned char)(1 << (e & 7));
            ++alw;
        }
    }
    p6_w_scan_always = alw;
}
// Per-frame (ProcessObjects, AFTER the camera update, BEFORE loop1). Clear the bitfield,
// then set the bit for every indexed slot whose spawn_x is within +-WINDOW of the camera x.
extern "C" void p6_scan_update_near(int32 cam_x_world)
{
    // I3d: seed near from the always-iterate set (non-x-cullable scene slots), THEN add the
    // x-window near slots below. loop1 (Object.cpp) then tests ONE combined WRAM-H bit per
    // scene slot -- a clear bit == FAR + x-cullable == safe to skip the slot entirely.
    for (int32 i = 0; i < (int32)sizeof(p6_scan_near); ++i)
        p6_scan_near[i] = p6_scan_always[i];
    int32 n = p6_scan_n;
    if (n <= 0) {
        p6_w_scancull_near = 0;
        return;
    }
    int32 lo = cam_x_world - P6_SCAN_WINDOW, hi = cam_x_world + P6_SCAN_WINDOW;
    int32 a = 0, b = n; // binary-search the first entry with x >= lo
    while (a < b) {
        int32 m = (a + b) >> 1;
        if ((int32)(p6_scan_sorted[m] >> 11) < lo) a = m + 1;
        else b = m;
    }
    int32 cnt = 0;
    for (int32 i = a; i < n; ++i) {
        uint32 en = p6_scan_sorted[i];
        if ((int32)(en >> 11) > hi)
            break;
        int32 slot = (int32)(en & 0x7FF);
        p6_scan_near[slot >> 3] |= (unsigned char)(1 << (slot & 7));
        ++cnt;
    }
    p6_w_scancull_near = cnt;
}
extern "C" void p6_engine_boot_and_run(void)
{
    p6_lean_boot = 1;
    p6_pool_remap_init(); // P6.8 I3b.1: identity remap before any entity access (ready-flag crash-safe)
    p6_scene_run(); // masked load core -> lean early-return (no diag scaffolding)
}

// P6.5b2: per-frame tick, registered as a jo callback by main.c (the diag
// build's only callback -- the park keeps the hand-port out). Advances the
// REAL Ring animator with the engine's own ProcessAnimation and re-emits the
// VDP1 sprite (SGL rebuilds the command list every frame, so the draw must
// recur). Witness order matters for qa_p6_sprite.py's W8 cadence formula:
// ProcessAnimation, copy frameID, THEN ticks++ -- ticks counts completed
// ProcessAnimation calls.
// P6.8 Step A: legacy-Ring tick frames to run BEFORE switching to GHZ. The
// legacy Ring proof (obj/entdraw/scsp/draw gates) accumulates its witnesses
// over these frames (Ring respawn cycle ~65 ticks, scsp re-trigger at 256),
// keeping CD-DA undisturbed; then the GHZ re-load takes over and runs forever.
// 260 frames clears the scsp re-trigger cadence (RETRIGGER at tick 256 ->
// snd_plays reaches 2) before the GHZ switch, so the legacy Ring audio proof
// is whole; the continuous GHZ pass then runs for the rest of the capture.
#define P6_GHZ_SWITCH_FRAME 260

extern "C" void p6_scene_tick(void)
{
    // P6.8 Step B (Task #211): the LEAN shipping boot has no legacy-Ring proof
    // and no deferred 260-frame switch. On its FIRST live tick -- vblank active,
    // interrupts unmasked: the EXACT conditions the diag's deferred reload runs
    // under -- re-load GHZ and arm the continuous loop, then run the first GHZ
    // frame. Subsequent ticks take the armed fast path below. Gated on
    // p6_lean_boot so the diag tick is unchanged.
    if (p6_lean_boot && !p6_ghz_continuous_armed) {
#if defined(P6_AIZ_TEST)
        // M3.0 (qa_p6_aiz_scene): the AIZ-test diagnostic build (P6_FRONTEND_MENU +
        // P6_AIZ_TEST) boots STRAIGHT to the AIZ intro-cutscene scene instead of the
        // Menu, to prove the engine LoadScene + FG render of a non-GHZ gameplay scene
        // in ISOLATION from the menu confirm (the same pattern F.1/F.2 used with
        // P6_TRANSITION_TEST). PRECEDENCE over the plain MENU branch (P6_AIZ_TEST
        // implies P6_FRONTEND_MENU so both macros are defined here). p6_aiz_reload
        // routes through the gameplay-tilemap arm (p6_isAIZ); the inert AIZ blank
        // entities tick harmlessly in the generic p6_frontend_frame.
#if defined(P6_GHZCUT_DIRECTBOOT)
        // Task #309 gate-2: P6_GHZCUT_DIRECTBOOT (the direct-boot DIAGNOSTIC; implies
        // P6_GHZCUT_BOOT -> P6_AIZ_TEST) boots straight to the AIZ->GHZCutscene DESTINATION
        // scene instead of AIZ, to prove its load+render + the fixed-timer cutscene ->
        // playable-GHZ handoff in ISOLATION. The LIVE-loop build (P6_GHZCUT_BOOT WITHOUT
        // DIRECTBOOT) takes the #else -> boots the normal menu->AIZ path so the AIZ intro's
        // OWN beat-9 LoadGHZ reaches GHZCutscene through the ENGINESTATE_LOAD seam branch.
        p6_ghzcut_reload();
#elif defined(P6_FRONTEND_CHAIN)
        // FULL-CHAIN (task #314): P6_GHZCUT_BOOT + P6_FRONTEND_CHAIN boots the WHOLE
        // front-end from the TOP: Logos -> (auto-advance seam) Title -> (Start-press
        // seam) Menu -> (Mania Mode confirm) AIZ intro -> (beat-9 seam) GHZCutscene
        // -> (SetupGHZ1 seam) playable Green Hill Zone. Every hop is an existing
        // ENGINESTATE_LOAD seam in p6_frontend_frame; this branch only changes the
        // BOOT scene from Menu/AIZ to Logos.
        p6_logos_reload();
#else
        p6_aiz_reload();
#endif
        if (p6_ghz_continuous_armed)
            p6_frontend_frame();
        return;
#elif defined(P6_FRONTEND_MENU)
        // M1 (qa_engine_menu): the MENU front-end flavor boots the Menu scene
        // directly on the first live tick (select "Menu" + load+arm, then run the
        // generic UI-scene frame). MENU takes PRECEDENCE over CHAIN/TITLE/LOGOS (it
        // defines all of them so the shared p6_frontend_frame/VDP1-box/arm-sprites
        // machinery compiles) -- without this #if first, the CHAIN/Title-direct boot
        // would steal the boot. Same lean-tick shape as the CP5a Title boot.
        p6_menu_reload();
        if (p6_ghz_continuous_armed)
            p6_frontend_frame();
        return;
#elif defined(P6_FRONTEND_CHAIN)
        // CP5c (Task #270): the front-end FLOW chain boots the LOGOS scene first
        // (NOT Title) -- it plays the SEGA/RSDK logos, then the decomp LogoSetup
        // auto-advance (RSDK.LoadScene) is caught in p6_frontend_frame and routed
        // to p6_title_reload (the explicit Logos->Title fire). PRECEDENCE over the
        // plain P6_FRONTEND_TITLE branch below: P6_FRONTEND_CHAIN implies TITLE
        // (which implies LOGOS) so all three macros are defined here; without this
        // #if first, the Title-direct-boot branch would steal the boot. Same lean-
        // tick shape as the CP4 Logos boot (select + load+arm, then the UI frame).
        p6_logos_reload();
        if (p6_ghz_continuous_armed)
            p6_frontend_frame();
        return;
#elif defined(P6_FRONTEND_TITLE)
        // CP5a (Task #267): the TITLE front-end flavor boots the Title scene on
        // the first live tick (select "Title" + load+arm, then run the generic
        // UI-scene frame). Title takes precedence over Logos (it also defines
        // P6_FRONTEND_LOGOS so the shared p6_frontend_frame compiles).
        p6_title_reload();
        if (p6_ghz_continuous_armed)
            p6_frontend_frame();
        return;
#elif defined(P6_FRONTEND_LOGOS)
        // CP4 (Task #265): the FRONT-END flavor boots the Logos splash scene
        // instead of GHZ -- same lean-tick shape (select + load+arm on the first
        // live tick, then run the scene frame), routed to the front-end fns.
        p6_logos_reload();
        if (p6_ghz_continuous_armed)
            p6_frontend_frame();
        return;
#else
        p6_ghz_reload();
        if (p6_ghz_continuous_armed)
            p6_ghz_frame();
        return;
#endif
    }
    // P6.8 Step A (Task #211): once GHZ is the final live scene, drive the
    // engine GHZ frame every jo frame and RETURN -- the legacy Ring block is
    // skipped while armed (it stays reachable for the pre-switch legacy frames
    // so the Ring-proof gates capture their witnesses first).
    if (p6_ghz_continuous_armed) {
#if defined(P6_FRONTEND_LOGOS)
        // CP4 (Task #265): the front-end flavor runs the generic UI-scene frame
        // every armed tick (it handles the ENGINESTATE_LOAD advance-to-Title
        // internally). The GHZ-specific transition tests + FG-present below + the
        // legacy-Ring tail are NOT COMPILED in this flavor -- that drops the large
        // p6_ghz_frame/p6_ghz_reload + Ring-proof code from the gc, reclaiming the
        // WRAM-H the front-end fns + guards added (the #228 _end<ANIMPAK budget).
        p6_frontend_frame();
        return;
    }
    return; // front-end: the lean-boot first tick already armed; no legacy path.
}
#else
#if defined(P6_TRANSITION_TEST)
        // P6.8 Step F.2 RED gate: inject a ONE-SHOT ACT ADVANCE (GHZ1 -> GHZ2)
        // at a fixed continuous-frame count to exercise the full cross-zone
        // transition (band-store swap + scene re-init) without a multi-minute
        // playthrough to the act signpost. Select GHZ2's listPos the same way
        // p6_ghz_reload selects GHZ1 (scan the category ranges), then request
        // ENGINESTATE_LOAD. The REAL trigger is Zone's RSDK.LoadScene().
        {
            static int32 s_xtest_fired = 0;
            if (!s_xtest_fired && p6_w_cont_frames >= 100) {
                s_xtest_fired = 1;
                for (int32 c = 0; c < sceneInfo.categoryCount; ++c) {
                    SceneListInfo *cat = &sceneInfo.listCategory[c];
                    int32 hit = 0;
                    for (int32 i = cat->sceneOffsetStart; i <= cat->sceneOffsetEnd; ++i) {
                        if (!strcmp(sceneInfo.listData[i].folder, "GHZ")
                            && sceneInfo.listData[i].id[0] == '2') {
                            sceneInfo.activeCategory = c;
                            sceneInfo.listPos        = i;
                            hit                      = 1;
                            break;
                        }
                    }
                    if (hit)
                        break;
                }
                sceneInfo.state = ENGINESTATE_LOAD;
            }
        }
#endif
#if defined(P6_WARP_TEST)
        // P6.8 F.2-followup: ONE-SHOT debug WARP -- teleport SLOT_PLAYER1 just
        // PAST the GHZ1 signpost (x=15792px) so the REAL SignPost_CheckTouch
        // crossing (player.x > signpost.x, SignPost.c:313) fires in gameplay.
        // The camera follows over the next frames, the signpost comes inRange,
        // and SignPost flips ACTIVE_BOUNDS->ACTIVE_NORMAL + spins. This is the
        // enabler for testing the act-clear chain without a full playthrough.
        {
            // F.5 (real-signpost-trigger): the F.4 direct ActClear spawn is REMOVED.
            // The act-clear chain now runs ONLY from the CANONICAL entry: the census
            // relocates the GHZ1 RUNPAST signpost next to the player (above), the
            // player.x>signpost.x crossing fires SignPost_CheckTouch -> State_Spin ->
            // its OWN ResetEntitySlot(SLOT_ACTCLEAR) (SignPost.c:452). Proof =
            // p6_w_sign_crossed (active->ACTIVE_NORMAL) + p6_w_ac_frames>0 + lay_bytes
            // 51094(GHZ1)->43347(GHZ2). Keep the player x witness for the gate.
            p6_w_warp_plrx = RSDK_ENTITY_AT(0)->position.x;
        }
#endif
        // P6.8 Step F (Task #211): ENGINESTATE_LOAD dispatch -- an object (Zone)
        // requested a scene transition (RSDK.LoadScene set state=LOAD + listPos).
        // Mirror RetroEngine ProcessEngine's switch: load the selected scene +
        // re-arm, then RETURN. InitObjects set state=REGULAR, so the next tick
        // runs the new scene's first REGULAR frame.
        if (sceneInfo.state == ENGINESTATE_LOAD) {
            p6_scene_load_and_arm();
            ++p6_w_transitions;
            return;
        }
        p6_ghz_frame();
        return;
    }
    if (p6_w_spr_id < 0)
        return;
    // After the legacy Ring proof has run its frames, RE-LOAD GHZ + arm the
    // continuous tick (deferred out of the masked p6_scene_run load phase so
    // the GHZ pack reads don't displace the title CD-DA prematurely, and the
    // burst/proof witnesses stay byte-identical to W19 at p6_scene_run return).
    if (p6_w_legacy_frames >= P6_GHZ_SWITCH_FRAME) {
        p6_ghz_reload();
        if (p6_ghz_continuous_armed) {
            p6_ghz_frame();
            return;
        }
    }
    ++p6_w_legacy_frames;
    ProcessAnimation(&p6_ringAnimator);
    p6_w_spr_frame = (int32)p6_ringAnimator.frameID;
    ++p6_w_spr_ticks;
    // P6.5b3: draw through the engine's CANONICAL DrawSprite slot -- the
    // exact call shape object Draw callbacks use (decomp Ring.c Ring_Draw).
    // World position (60<<16,112<<16) + pivot (-8,-8) + 16x16 frame puts the
    // visual CENTER at (60,112), the same anchor qa_p6_sprite.py W9 and
    // qa_p6_draw.py D6 gate on. screenRelative=false exercises the
    // currentScreen->position translate (Drawing.cpp:2682-2685). The current
    // frameID's rect is resolved INSIDE DrawSprite -- the on-screen ring
    // animates through all 16 sheet rects.
    {
        Vector2 drawPos;
        drawPos.x = 60 << 16;
        drawPos.y = 112 << 16;
        DrawSprite(&p6_ringAnimator, &drawPos, false);
    }
    // P6.6b: re-trigger the engine SFX every 256 ticks (~4.3 s, audible
    // bleep). The direct-SCSP backend keeps the FULL sample buffer at a
    // FIXED Sound RAM address, so qa_p6_scsp.py A3 is capture-timing
    // independent (exact-region compare, no ring chasing). Engine PlaySfx
    // re-arms the canonical channel state each trigger.
    if (p6_sndSfxID >= 0 && (p6_w_spr_ticks & 0xFF) == 0) {
        PlaySfx((uint16)p6_sndSfxID, 0, 0xFF);
        p6_snd_play();
        ++p6_w_snd_plays;
    }
    // P6.7a: the ENGINE's own object loop, every frame -- update dispatch +
    // draw-group enqueue (ProcessObjects, Object.cpp:357-475) and the
    // draw-group walk (ProcessObjectDrawLists, :734+) dispatching the
    // verbatim Ring_Draw through the function table into the REAL Saturn
    // DrawSprite backend. The LostFX ring destroys itself after 64 updates
    // (verbatim ++timer > 64); the engine respawn keeps the cycle running.
    if (p6_objRingFrames) {
        if (RSDK_ENTITY_AT(P6_OBJ_RING_SLOT)->classID == 0)
            p6_obj_spawn_ring();
        ProcessObjects();
        ProcessObjectDrawLists();
        if (s_ovl.witness_fn)
            s_ovl.witness_fn(RSDK_ENTITY_AT(P6_OBJ_RING_SLOT));
    }
}
#endif // !P6_FRONTEND_LOGOS (the GHZ-armed body + legacy-Ring tail)
#endif // P6_SCENE_TEST

// ---- Path 2 (io-jo): the file-I/O body jo_main() calls AFTER jo_core_init ----
// In the jo-HAL boot, jo (audio + fs modules ON) has ALREADY run the proven CD
// bring-up -- slInitSystem (jo_core_init -> jo_core_init_vdp), CDC_CdInit
// (audio.c:110), then GFS_Init with the root dir loaded into __jo_fs_dirtbl
// (fs.c:115) -- by the time jo_core_init() returns. So GFS_NameToId already
// resolves on-disc names. This body therefore does ONLY the engine LoadFile
// proof: it neither boots SGL/CD/GFS (jo owns that) nor parks (jo_core_run does).
// p6_gfs.c's Saturn_fOpen rides jo's live GFS; p6_gfs_init() is NOT called here.
extern "C" void p6_io_run(void)
{
    FileInfo finfo;
    InitFileInfo(&finfo);
    if (LoadFile(&finfo, "P6IO.BIN", FMODE_RB)) {
        p6_w_io_loaded   = 1;
        p6_w_io_filesize = finfo.fileSize;
        uint8 hdr[4]     = { 0, 0, 0, 0 };
        ReadBytes(&finfo, hdr, 4);
        p6_w_io_firstbytes = ((int32)hdr[0] << 24) | ((int32)hdr[1] << 16)
                           | ((int32)hdr[2] << 8) | (int32)hdr[3];
        CloseFile(&finfo);
    }
}
