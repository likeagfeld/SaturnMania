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
#define P6_LW_HEAP_BASE    0x00200000u
#define P6_LW_HEAP_END     0x00252000u // pools 272 KB + inflate ~44 KB transient + slack
#define P6_LW_ENTITYLIST   0x00252000u // 448 * 344         = 0x25A00 -> 0x277A00
#define P6_LW_TILELAYERS   0x00277A00u // 4 * 13384         = 0xD120  -> 0x284B20
#define P6_LW_DATASTORAGE  0x00284B20u // 5 * 32788         = 0x28064 -> 0x2ACB84
#define P6_LW_DATAFILELIST 0x002ACC00u // 0x700 * 32        = 0xE000  -> 0x2BAC00
#define P6_LW_TILESETPX    0x002BAC00u // TILESET_SIZE      = 0x40000 -> 0x2FAC00 (LIVE since P6.5a)
#define P6_LW_GROUPB_BASE  0x002FAC00u // absolute arrays below
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
// P6.4 (Task #225, qa_p6_pack.py): the original-Data.rsdk ingestion witnesses.
__attribute__((used)) int32 p6_w_pack_mounted   = 0; // LoadDataPack("Data.rsdk") return
__attribute__((used)) int32 p6_w_pack_filecount = 0; // dataPacks[0].fileCount (expect 1677)
__attribute__((used)) int32 p6_w_pack_used      = 0; // 1 == scene LoadFile rode the pack (fileOffset > 0)
// P6.5a (Task #208, qa_p6_gif.py): engine GIF-decode witnesses.
__attribute__((used)) int32 p6_w_gif_loaded = 0; // 1 == LoadStageGIF + hash completed
__attribute__((used)) int32 p6_w_gif_hash   = 0; // djb2-xor over tilesetPixels[0x40000]
__attribute__((used)) int32 p6_w_gif_b0     = 0; // tilesetPixels[0] (offline model 0x01)
// P6.5b1 (Task #208, qa_p6_vdp2.py): VDP2 present witness.
__attribute__((used)) int32 p6_w_vdp2_done  = 0; // 1 == engine layer presented on NBG1
// p6_vdp2.c (C TU): presents the engine-decoded Island layer through NBG1.
void p6_vdp2_present(const unsigned char *tilesetPx, const unsigned short *layout,
                     int wshift, const unsigned short *pal565);
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
__attribute__((used)) int32 p6_w_draw_xy      = 0;  // last blit top-left (x&FFFF)<<16|(y&FFFF)
__attribute__((used)) int32 p6_w_draw_rect    = 0;  // last blit (sprX<<16)|sprY
__attribute__((used)) int32 p6_w_draw_sheetid = -1; // last blit frame->sheetID
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
// p6_snd.c: CD-DA start through the proven jo_audio_play_cd_track path.
void p6_cdda_play(int track, int loop);
// p6_vdp1.c (C TU, jo side): slot-cached VDP1 blitter the Saturn DrawSprite
// backend targets. sheet_bind pins the engine surface + mirrors the palette
// to CRAM bank 1 once; blit() draws a sheet rect at an engine TOP-LEFT,
// uploading each DISTINCT rect to VDP1 exactly once (cache keyed on
// (sx,sy,w,h) -- a per-tick jo_sprite_add would be the #189 overflow class).
int  p6_vdp1_sheet_bind(const unsigned char *sheetPixels, int sheetWidth,
                        const unsigned short *pal565);
void p6_vdp1_blit(int x, int y, int w, int h, int sx, int sy);
}

// P6.6c: Audio.cpp's file-scope stream state lives at GLOBAL scope (NOT in
// namespace RSDK -- Audio.cpp:20,24): PlayStream sprintf-s the request path
// into streamFilePath and stashes the loop point; the Saturn
// AudioDevice::HandleStreamLoad below reads both to resolve the CD track.
extern char streamFilePath[0x40];
extern int32 streamLoopPoint;

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
// P6.7a: objectClassList/typeGroups/drawGroups are LIVE (ProcessObjects,
// Object.cpp:357-475, reads/writes all three every tick). Real WRAM-H .bss
// backings, sized by the P6.7a Title-scale data retarget (Object.hpp Saturn
// branch: ENTITY_COUNT 0xC0): classes 0x100*~68B = 17.4 KB, typeGroups
// 0x84*(0xC0*2+4) = 32 KB, drawGroups 16*~420B = 6.6 KB -- measured against
// the 171 KB diag-image margin (GHZ-scale memory map = P6.7b deliverable).
static ObjectClass   p6_objClassBacking[OBJECT_COUNT];
static TypeGroupList p6_typeGroupsBacking[TYPEGROUP_COUNT];
static DrawList      p6_drawGroupsBacking[DRAWGROUP_COUNT];
ObjectClass *objectClassList = p6_objClassBacking;
TypeGroupList *typeGroups    = p6_typeGroupsBacking;
DrawList *drawGroups         = p6_drawGroupsBacking;
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
static SpriteAnimation p6_sprAnimBacking[SPRFILE_COUNT];
SpriteAnimation *spriteAnimationList = p6_sprAnimBacking;
Model *modelList             = (Model *)P6_LW_DEAD;
// LIVE since P6.5a: LoadStageGIF points the decoder at this backing and the
// engine's ReadGifPictureData writes all 262,144 indexed pixels into it.
uint8 *tilesetPixels         = (uint8 *)P6_LW_TILESETPX;
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
void DrawSprite(Animator *animator, Vector2 *position, bool32 screenRelative)
{
    if (animator && animator->frames) {
        SpriteFrame *frame = &animator->frames[animator->frameID]; // Drawing.cpp:2673
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
        }

        // Drawing.cpp:2687-2688 reads sceneInfo.entity->rotation/drawFX
        // UNCONDITIONALLY -- callers must keep sceneInfo.entity valid.
        switch (sceneInfo.entity->drawFX) {
            case FX_NONE:
                // Drawing.cpp:2785. inkEffect/alpha: the proof entity is
                // zeroed (INK_NONE -> opaque), matching VDP1 CLUT-mode draw.
                // FIXME Phase Z: INK_ALPHA/ADD/SUB via VDP1 color calculation.
                p6_vdp1_blit(pos.x + frame->pivotX, pos.y + frame->pivotY,
                             frame->width, frame->height, frame->sprX, frame->sprY);
                p6_w_draw_xy = (((pos.x + frame->pivotX) & 0xFFFF) << 16)
                             | ((pos.y + frame->pivotY) & 0xFFFF);
                p6_w_draw_rect    = ((int32)frame->sprX << 16) | (int32)frame->sprY;
                p6_w_draw_sheetid = (int32)frame->sheetID;
                ++p6_w_draw_calls;
                break;

            default:
                // FIXME P6.5b4+: FX_FLIP via VDP1 HF/VF (CMDCTRL Dir bits,
                // ST-013-R3 sec 5.5.4 -- position math per Drawing.cpp:
                // 2789-2812), FX_ROTATE/FX_SCALE via scaled/distorted parts.
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
    // FIXME P6.7+: table-ize from tools/loops.json as zones come online
    // (bgm-loops-hand-curated: every shipped BGM needs an acknowledged entry).

    p6_w_str_track = track;
    if (track > 0) {
        p6_cdda_play(track, streamLoopPoint != 0 || channel->loop);
        channel->state = CHANNEL_STREAM; // mirror LoadStream:239
    }
}
} // namespace RSDK

// ---- P6.7a: flat-Ring-TU surface + function-table bridges --------------------
// The verbatim p6_ring2.cpp dispatches RSDK.* through these bridges, which
// route through the engine's OWN RSDKFunctionTable[] (populated by the real
// SetupFunctionTables, Core/Link.cpp -- the P6.1-proven path). DrawSprite's
// slot now carries the REAL Saturn backend (P6.5b3), so the engine-looped
// ring is VISIBLE.
extern "C" {
extern void *p6_ring2_staticvars_slot;
extern unsigned p6_ring2_entity_size;
extern int32 p6_w_obj_classcount;
extern int32 p6_w_obj_spawns;
extern int32 p6_w_obj_draws;
void Ring_Update(void);
void Ring_Draw(void);
void p6_ring2_arm(void *slot, void *frames);
void p6_ring2_witness(const void *slot);

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
// videoSettings + showHitboxes are read by ProcessObjectDrawLists/LoadImage;
// the rest are dev/SKU surfaces the function table or REV02 paths reference.
VideoSettings videoSettings;
bool32 showHitboxes = false;
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

// A valid static Object for the blank class @0 (registry slot scene entities
// resolve to through the zeroed stageObjectIDs -- update/draw NULL, skipped).
static Object  p6_blankObject;
static Object *p6_blankStaticVars = &p6_blankObject;

// The proof entity's fixed slot: inside the RESERVE area (0x00..0x3F), below
// every scene-parsed entity -- deterministic for the gate.
#define P6_OBJ_RING_SLOT (RESERVE_ENTITY_COUNT - 1)
#define P6_OBJ_RING_CLASSID 1
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
P6_GROUPB_ABS("__ZN4RSDK12dataFileListE",    "0x002ACC00"); // RSDKFileInfo[0x700] = 0xE000
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
static void p6_obj_spawn_ring(void)
{
    ResetEntitySlot(P6_OBJ_RING_SLOT, P6_OBJ_RING_CLASSID, NULL);
    p6_ring2_arm(&objectEntityList[P6_OBJ_RING_SLOT], (void *)p6_objRingFrames);
    objectEntityList[P6_OBJ_RING_SLOT].position.x = 260 << 16;
    objectEntityList[P6_OBJ_RING_SLOT].position.y = 60 << 16;
    ++p6_w_obj_spawns;
}

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

    // 4) P6.5a (Task #208): THE DECODE UNDER TEST -- the engine's own GIF
    //    decoder pulls the REAL Title tileset out of the pack into the
    //    WRAM-L tilesetPixels backing. LoadStageGIF (Scene.cpp:980-998)
    //    header-opens via the pack hash, points the decoder at tilesetPixels,
    //    LZW-decodes all 16x16384 = 262,144 indexed pixels (the Saturn build
    //    skips the FLIP pre-expansion, Scene.cpp:1000), and merges the GIF
    //    palette into fullPalette[0] (values land zero here: the rgb32To16_*
    //    lookup tables are render-backend init, P6.5b -- not gated).
    //    qa_p6_gif.py compares the on-SH-2 djb2-xor of the WHOLE buffer
    //    against a Pillow decode of the same GIF.
    {
        // Render-backend palette tables FIRST (P6.5b1): the EXACT engine fill
        // from Drawing.cpp:274-276, so LoadStageGIF's palette merge
        // (Scene.cpp:995) lands the ORIGINAL GIF palette in fullPalette[0] as
        // engine-canonical RGB565 instead of zeros.
        for (int32 c = 0; c < 0x100; ++c) {
            rgb32To16_R[c] = (c & 0xFFF8) << 8;
            rgb32To16_G[c] = (c & 0xFFFC) << 3;
            rgb32To16_B[c] = c >> 3;
        }

        char gifPath[0x40];
        strcpy(gifPath, "Data/Stages/Title/16x16Tiles.gif");
        LoadStageGIF(gifPath);

        const uint8 *px = (const uint8 *)P6_LW_TILESETPX;
        uint32 h        = 5381u;
        for (uint32 i = 0; i < 0x40000u; ++i)
            h = ((h << 5) + h) ^ (uint32)px[i];
        p6_w_gif_hash   = (int32)h;
        p6_w_gif_b0     = (int32)px[0];
        p6_w_gif_loaded = 1;
    }

    // 5) P6.5b1: FIRST ENGINE-RENDERED PIXELS -- present the engine-decoded
    //    Island layer (tileLayers[3]) through VDP2 NBG1 from the engine's own
    //    tilesetPixels + layout + fullPalette[0]. main.c then parks the diag
    //    build on jo_core_run so the layer stays on screen for the capture.
    p6_vdp2_present((const unsigned char *)P6_LW_TILESETPX,
                    (const unsigned short *)tileLayers[3].layout,
                    tileLayers[3].widthShift,
                    (const unsigned short *)fullPalette[0]);
    p6_w_vdp2_done = 1;

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
            if (f0->sheetID < SURFACE_COUNT && sheet->scope != SCOPE_NONE && sheet->pixels) {
                uint32 sh     = 5381u;
                uint32 nbytes = (uint32)sheet->width * (uint32)sheet->height;
                for (uint32 i = 0; i < nbytes; ++i)
                    sh = ((sh << 5) + sh) ^ (uint32)sheet->pixels[i];
                p6_w_spr_sheethash = (int32)sh;

                SetSpriteAnimation(sprID, 0, &p6_ringAnimator, true, 0);
                p6_objRingFrames = &spr->frames[spr->animations[0].frameListOffset]; // P6.7a

                // P6.5b3: DrawSprite environment. Drawing.cpp:2683 translates
                // through currentScreen->position; :2676/:2687 dereference
                // sceneInfo.entity unconditionally. screens[0] sits zeroed in
                // WRAM-L (Group-B absolute, memset in step 0) -> position
                // (0,0); entity slot 0 is zeroed -> drawFX=FX_NONE,
                // inkEffect=INK_NONE, FLIP_NONE.
                currentScreen      = &screens[0];
                screens[0].size.x  = SCREEN_XMAX;
                screens[0].size.y  = SCREEN_YSIZE;
                sceneInfo.entity   = &objectEntityList[0];

                p6_vdp1_sheet_bind(sheet->pixels, sheet->width,
                                   (const unsigned short *)fullPalette[0]);
            }
        }
    }

    // 7) P6.6a: ENGINE AUDIO CORE -- the Saturn AudioDevice::Init backend runs
    //    the engine's own InitAudioChannels, the UNMODIFIED engine loads the
    //    REAL Global/ScoreAdd.wav from the pack (LoadSfx -> LoadSfxToSlot WAV
    //    parse + S16->F32 convert, Audio.cpp:305-424), and the engine's own
    //    PlaySfx channel allocator (Audio.cpp:441-507) arms channel 0. The
    //    SCSP-audible half keys off this canonical channels[] state in P6.6b.
    {
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

    // 8) P6.7a: ENGINE OBJECT LOOP -- populate the engine's own function
    //    table (real SetupFunctionTables, Core/Link.cpp; the P6.1-proven
    //    dispatch path -- the flat Ring TU's RSDK.* calls route through it),
    //    register Blank @0 + the VERBATIM decomp Ring @1 through the real
    //    RegisterObject (Object.cpp:62-108), and spawn the proof entity via
    //    ResetEntitySlot. The tick then runs the engine's ProcessObjects +
    //    ProcessObjectDrawLists every frame (mirrors p6_main.cpp:120-150,
    //    the P6.1 standalone template).
    if (p6_objRingFrames) {
        SetupFunctionTables();

        RegisterObject(&p6_blankStaticVars, "Blank Object", sizeof(EntityBase), sizeof(Object),
                       NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        RegisterObject((Object **)p6_ring2_staticvars_slot, "Ring", p6_ring2_entity_size, 20,
                       Ring_Update, NULL, NULL, Ring_Draw, NULL, NULL, NULL, NULL, NULL);
        p6_w_obj_classcount = objectClassCount;

        stageObjectIDs[0]    = 0;
        stageObjectIDs[P6_OBJ_RING_CLASSID] = 1;
        sceneInfo.classCount = 2;

        // ProcessObjectDrawLists environment (Object.cpp:736-752): one
        // screen, the proof draw group visible. NO-CTORS: videoSettings +
        // engine fields set explicitly.
        videoSettings.screenCount  = 1;
        engine.drawGroupVisible[3] = true;
        sceneInfo.state            = ENGINESTATE_REGULAR;

        p6_obj_spawn_ring();
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

// P6.5b2: per-frame tick, registered as a jo callback by main.c (the diag
// build's only callback -- the park keeps the hand-port out). Advances the
// REAL Ring animator with the engine's own ProcessAnimation and re-emits the
// VDP1 sprite (SGL rebuilds the command list every frame, so the draw must
// recur). Witness order matters for qa_p6_sprite.py's W8 cadence formula:
// ProcessAnimation, copy frameID, THEN ticks++ -- ticks counts completed
// ProcessAnimation calls.
extern "C" void p6_scene_tick(void)
{
    if (p6_w_spr_id < 0)
        return;
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
        if (objectEntityList[P6_OBJ_RING_SLOT].classID == 0)
            p6_obj_spawn_ring();
        ProcessObjects();
        ProcessObjectDrawLists();
        p6_ring2_witness(&objectEntityList[P6_OBJ_RING_SLOT]);
    }
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
