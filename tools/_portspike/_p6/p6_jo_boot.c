// =============================================================================
// p6_jo_boot.c -- Path 2 (io-jo) boot TU: relink the engine file-I/O proof body
// into jo-engine's EXISTING, PROVEN boot (Task #206, P6.2).
//
// THE DECOUPLE DECISION (user-selected "Boot via jo HAL"): instead of the bare-
// SLSTART crt0/asm-shim path (io-green, p6_io_boot.s) -- whose slInitSystem
// faulted with an unset GBR -> SGL VBlank ISR address error -- this image boots
// through jo's own main()/jo_core_init(), the SAME bring-up the shipping Saturn
// build proves works with this LIBCD.A / LIBSGL.A. jo only boots + owns the CD
// ring; the ENGINE is NOT hosted on jo. p6_io_run() (p6_io_main.cpp) runs the
// UNMODIFIED engine RSDK::LoadFile path over the p6_gfs.c GFS backend.
//
// BOOT CHAIN (all measured, not guessed):
//   SGL SLSTART (LIBSGL.A sglI00.o, ___Start @0x06004000) sets SR/SP/CCR/GBR,
//     then `jmp @main`.
//   jo main() (jo-engine/jo_engine/core.c:665-671) zeroes .bss itself
//     (jo_memset(&_bstart,0,&_bend-&_bstart) + the system work area), then calls
//     jo_main() -- THIS function. No asm shim / no ctor run is needed: Path 2's
//     C++ objects are all POD/zero-init (.bss, zeroed by jo's main) + p6_w_magic
//     (const, .rodata), so there are no C++ ctors to run.
//   jo_core_init() brings up CD/GFS in jo's PROVEN order: slInitSystem
//     (jo_core_init_vdp) -> jo_audio_init -> CDC_CdInit(0x00,0x00,0x05,0x0f)
//     (audio.c:110) -> jo_fs_init -> GFS_Init(JO_OPEN_MAX,__jo_fs_work,
//     &__jo_fs_dirtbl) with the root dir loaded (fs.c:115). So by the time
//     jo_core_init() RETURNS, GFS_NameToId resolves on-disc root names and the CD
//     is spun up -- exactly the state Saturn_fOpen (p6_gfs.c) needs.
//
// WHY p6_io_run() RUNS HERE (synchronously, before jo_core_run): the project's
// own proven pattern loads assets synchronously after jo_core_init and before
// jo_core_run (memory rule sync-load-eliminates-cross-TU-volatile). p6_io_run()
// does ONLY the engine LoadFile witness body -- it does NOT boot SGL/CD/GFS (jo
// owns that) and does NOT call p6_gfs_init() (jo's GFS_Init already ran). With
// JO_OPEN_MAX==1 the single GFS handle slot is free at this point (no jo file op
// is in flight between jo_core_init returning and jo_core_run starting).
//
// PARK: jo_core_run() is jo's proven park (the shipping build's main loop). A
// no-op callback is registered so jo_core_run has a frame callback. The savestate
// gate (tools/_portspike/qa_p6_io.py) captures at frame ~50 -- well after the
// frame-0 LoadFile completed -- and reads the witnesses + asserts SH2-M PC is in
// [__text_start,__text_end). jo_core_run's idle loop (and every lib it spins in)
// is collected into this image's single .text, so PC stays in that range.
// =============================================================================
#include <jo/jo.h>

// p6_io_main.cpp: the engine RSDK::LoadFile("P6IO.BIN") witness body. extern "C"
// there, so it links as the C symbol jo_main references here.
extern void p6_io_run(void);

static void p6_noop(void)
{
}

void jo_main(void)
{
    jo_core_init(JO_COLOR_Black);

    // Engine file-I/O proof: unmodified RSDK::LoadFile over jo's live GFS ring.
    p6_io_run();

    jo_core_add_callback(p6_noop);
    jo_core_run();
}
