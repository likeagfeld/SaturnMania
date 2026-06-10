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
__attribute__((used)) int32 p6_w_io_fid        = 0;
__attribute__((used)) int32 p6_w_io_step       = 0;
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

// ---- WRAM-L layout constants (P6.4 revision, Task #225) -----------------------
// The P6.3 map shifted: InitStorage's MUS/SFX pools are proof-trimmed 144 KB
// (Storage.cpp P6_SCENE_TEST branch, measured basis in its comment) to make
// room for the relocated Data.rsdk registry dataFileList[0x700] (57,344 B,
// Reader.hpp:77 -- MEASURED pack fileCount = 1677).
#define P6_LW_HEAP_BASE    0x00200000u
#define P6_LW_HEAP_END     0x00276000u // pools 464 KB (0x74000) + nano-malloc headers, 8 KB slack
#define P6_LW_ENTITYLIST   0x00276000u // 448 * 344         = 0x25A00 -> 0x29BA00
#define P6_LW_TILELAYERS   0x0029BA00u // 4 * 13384         = 0xD120  -> 0x2A8B20
#define P6_LW_DATASTORAGE  0x002A8B20u // 5 * 32788         = 0x28064 -> 0x2D0B84
#define P6_LW_DATAFILELIST 0x002D0C00u // 0x700 * 32        = 0xE000  -> 0x2DEC00
#define P6_LW_GROUPB_BASE  0x002DEC00u // absolute arrays below
#define P6_LW_GROUPB_END   0x002E42C0u
#define P6_LW_DEAD         0x002E4300u // shared dummy for measured-DEAD pointers
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
// P6.4 (Task #225, qa_p6_pack.py): the original-Data.rsdk ingestion witnesses.
__attribute__((used)) int32 p6_w_pack_mounted   = 0; // LoadDataPack("Data.rsdk") return
__attribute__((used)) int32 p6_w_pack_filecount = 0; // dataPacks[0].fileCount (expect 1677)
__attribute__((used)) int32 p6_w_pack_used      = 0; // 1 == scene LoadFile rode the pack (fileOffset > 0)
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
TileLayer *tileLayers        = (TileLayer *)P6_LW_TILELAYERS;
DataStorage *dataStorage     = (DataStorage *)P6_LW_DATASTORAGE;
ObjectClass *objectClassList = (ObjectClass *)P6_LW_DEAD;
TypeGroupList *typeGroups    = (TypeGroupList *)P6_LW_DEAD;
DrawList *drawGroups         = (DrawList *)P6_LW_DEAD;
SpriteAnimation *spriteAnimationList = (SpriteAnimation *)P6_LW_DEAD;
Model *modelList             = (Model *)P6_LW_DEAD;
uint8 *tilesetPixels         = (uint8 *)P6_LW_DEAD;
CollisionMask (*collisionMasks)[TILE_COUNT * COLLISION_FLIPCOUNT] =
    (CollisionMask (*)[TILE_COUNT * COLLISION_FLIPCOUNT])P6_LW_DEAD;
TileInfo (*tileInfo)[TILE_COUNT * COLLISION_FLIPCOUNT] =
    (TileInfo (*)[TILE_COUNT * COLLISION_FLIPCOUNT])P6_LW_DEAD;

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
void ClearStageSfx() {}
void ClearStageObjects() {}
void LoadStaticVariables(uint8 *classPtr, uint32 *hash, int32 readOffset)
{
    (void)classPtr;
    (void)hash;
    (void)readOffset;
}
void LoadSfx(char *filePath, uint8 plays, uint8 scope)
{
    (void)filePath;
    (void)plays;
    (void)scope;
}
// Key function of ImageGIF (Sprite.hpp:67, only virtual not defined in-class):
// this out-of-line definition emits _ZTVN4RSDK8ImageGIFE here (Itanium ABI key-
// function rule), satisfying both undefined symbols. Returning false makes
// LoadStageGIF's guard (Scene.cpp:984) skip its whole body -- tilesetPixels
// stays untouched, matching its DEAD dummy backing.
bool32 ImageGIF::Load(const char *fileName, bool32 loadHeader)
{
    (void)fileName;
    (void)loadHeader;
    return false;
}
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
// P6.4: dataFileList[0x700] -- the Data.rsdk registry LoadDataPack fills
// (Reader.cpp:140-154) and OpenDataFile hash-scans (Reader.cpp:192-196).
P6_GROUPB_ABS("__ZN4RSDK12dataFileListE",    "0x002D0C00"); // RSDKFileInfo[0x700] = 0xE000
P6_GROUPB_ABS("__ZN4RSDK11fullPaletteE",     "0x002DEC00"); // uint16[8][256] = 0x1000
P6_GROUPB_ABS("__ZN4RSDK13globalPaletteE",   "0x002DFC00"); // uint16[8][256] = 0x1000
P6_GROUPB_ABS("__ZN4RSDK12stagePaletteE",    "0x002E0C00"); // uint16[8][256] = 0x1000
P6_GROUPB_ABS("__ZN4RSDK11scene3DListE",     "0x002E1C00"); // Scene3D[32]    = 0xA80
P6_GROUPB_ABS("__ZN4RSDK10gfxSurfaceE",      "0x002E2680"); // GFXSurface[64] = 0x900
// textBuffer[0x400] is NOT an absolute here: Storage_Text.o (linked since P6.4
// for the real GenerateHashMD5) DEFINES it -- 1 KB rides WRAM-H .bss instead.
P6_GROUPB_ABS("__ZN4RSDK14stageObjectIDsE",  "0x002E3380"); // int32[256]     = 0x400
P6_GROUPB_ABS("__ZN4RSDK15globalObjectIDsE", "0x002E3780"); // int32[256]     = 0x400
P6_GROUPB_ABS("__ZN4RSDK11rgb32To16_RE",     "0x002E3B80"); // uint16[256]    = 0x200
P6_GROUPB_ABS("__ZN4RSDK11rgb32To16_GE",     "0x002E3D80"); // uint16[256]    = 0x200
P6_GROUPB_ABS("__ZN4RSDK11rgb32To16_BE",     "0x002E3F80"); // uint16[256]    = 0x200
P6_GROUPB_ABS("__ZN4RSDK13gfxLineBufferE",   "0x002E4180"); // uint8[224]     = 0xE0
P6_GROUPB_ABS("__ZN4RSDK16activeGlobalRowsE","0x002E4260"); // uint16[8]      = 0x10
P6_GROUPB_ABS("__ZN4RSDK15activeStageRowsE", "0x002E4270"); // uint16[8]      = 0x10
P6_GROUPB_ABS("__ZN4RSDK7screensE",          "0x002E4280"); // ScreenInfo[1]  = 0x34 -> end 0x2E42B4 < 0x2E42C0

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

// ---- (d) The proof body main.c calls right after jo_core_init -----------------
// Pre-state per the measured LoadSceneAssets contract (Scene.cpp:295-297 reads
// sceneInfo.listData[listPos].id + currentSceneFolder; :693 keeps an entity iff
// sceneInfo.filter & entity->filter, entity->filter = 0xFF from :555 -- a zero
// sceneInfo.filter silently wipes EVERY parsed entity at :702).
extern "C" void p6_scene_run(void)
{
    // 0) Zero the WRAM-L windows (SLSTART zeroes only WRAM-H __bstart..__bend).
    memset((void *)P6_LW_ZERO_BASE, 0, P6_LW_ZERO_END - P6_LW_ZERO_BASE);

    // 1) Engine storage pools (5 mallocs through OUR _sbrk -> WRAM-L heap).
    p6_w_scene_initstorage = (int32)InitStorage();
    if (!p6_w_scene_initstorage)
        return; // loaded stays 0 -> gate RED with initstorage diagnosis
    p6_w_scene_step = 1;

    // 2) Scene pre-state: a one-entry scene list ("Title"/"1") + filter.
    static SceneListEntry p6_entry; // zero .bss; hash/name irrelevant to the parse
    strcpy(p6_entry.folder, "Title");
    strcpy(p6_entry.id, "1");
    p6_entry.filter    = 0xFF;
    sceneInfo.listData = &p6_entry;
    sceneInfo.listPos  = 0;
    sceneInfo.filter   = 0xFF;
    strcpy(currentSceneFolder, "Title");
    p6_w_scene_step = 2;

    // 2.5) P6.4 (Task #225): mount the ORIGINAL Data.rsdk pack (cd/DATA.RSDK,
    //      182,962,115 B). LoadDataPack opens it through the windowed GFS
    //      backend, walks the 1677-entry registry into the relocated
    //      dataFileList (WRAM-L 0x2D0C00), and flips useDataPack -- from then
    //      on EVERY non-external FMODE_RB LoadFile resolves by MD5 hash INSIDE
    //      the pack (Reader.cpp:312-314 returns OpenDataFile unconditionally;
    //      there is no loose-file fallback, so the scene parse below is a
    //      pack-routed proof by construction).
    p6_w_pack_mounted   = (int32)LoadDataPack("Data.rsdk", 0, false);
    p6_w_pack_filecount = (int32)dataPacks[0].fileCount;
    if (!p6_w_pack_mounted)
        return; // loaded stays 0 -> gate RED with the mount diagnosis

    // 3) THE CALL UNDER TEST: unmodified engine scene parse of
    //    "Data/Stages/Title/Scene1.bin", now resolved INSIDE Data.rsdk.
    LoadSceneAssets();
    p6_w_scene_step = 3;

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

    // 4) Witness copy (WRAM-H .bss survives the later title boot's WRAM-L use).
    {
        EntityBase *e;
        e = &objectEntityList[5 + RESERVE_ENTITY_COUNT];
        p6_w_scene_emblem_x = e->position.x;
        p6_w_scene_emblem_y = e->position.y;
        e = &objectEntityList[8 + RESERVE_ENTITY_COUNT];
        p6_w_scene_sonic_x = e->position.x;
        p6_w_scene_sonic_y = e->position.y;
        e = &objectEntityList[16 + RESERVE_ENTITY_COUNT];
        p6_w_scene_t3d_x = e->position.x;
        p6_w_scene_t3d_y = e->position.y;
    }
    p6_w_scene_loaded = 1;
    p6_w_scene_step   = 4;
}
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
