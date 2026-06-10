// =============================================================================
// p6_sgl_boot.c -- SGL single-region bring-up for the decoupled file-I/O proof
// (Task #206, P6.2). io-green only.
//
// The DECOUPLE decision: boot the engine's file-I/O proof through SGL's STANDARD
// single-region runtime -- the exact layout where jo-engine and Coup prove
// slInitSystem works -- instead of the bare crt0.s path that faulted. SGL's
// SLSTART entry (sglI00.o, pulled into the link by referencing slInitSystem;
// placed first by p6_io.linker `.slstart { ___Start = .; *(SLSTART) }`) sets
// SR=0xF0 / SP=*MasterStack / CCR=0x11 (cache enable+purge) / GBR=0x060FFC00,
// then jmp's the ELF `_main` (the asm shim) DIRECTLY. The shim zeroes BSS + runs
// ctors, then calls p6_io_proof, which calls THIS before any GFS work.
//
// WHY GBR MATTERS (the refined root cause of the prior crt0 fault): SGL's VBlank
// ISR uses GBR-relative addressing. crt0.s left GBR unset, so when slInitSystem
// enabled interrupts the first vblank handler faulted (master crashed to
// PC=0x06000956). SLSTART sets GBR=0x060FFC00, so booting through it removes the
// fault at its source -- this is the whole point of the decouple.
//
// slInitSystem ARGS (verified against Coup's working call + SL_DEF.H):
//   TV_320x224 (== 0, SL_DEF.H) -- the resolution Coup boots.
//   (TEXTURE *)0               -- NULL texture table is SAFE (Coup passes NULL).
//   1                          -- framerate divisor.
// slInitSynch() takes no args (SGL signature index 1835); jo does NOT call it at
// boot (it slSynch's each frame) but the GFSDEMO/Coup single-shot pattern does,
// and the gate docstring's boot contract names it -- harmless either way.
//
// The C SGL decls here cannot share a TU with the engine's C++ `namespace RSDK`
// headers, so this is its own TU bridged via extern "C" (p6_io_main.cpp).
// =============================================================================
#include <SGL.H> // slInitSystem / slInitSynch / TV_320x224 / TEXTURE

// Witness defined int32 in p6_io_main.cpp (int == 32-bit SH-2). Step values
// bracket each SGL call so a hang lands on a known boundary in one capture.
extern int p6_w_io_step;

void p6_sgl_boot(void)
{
    slInitSystem(TV_320x224, (TEXTURE *)0, 1);
    p6_w_io_step = 1; // slInitSystem returned (SGL work-area + VDP up)

    slInitSynch();
    p6_w_io_step = 2; // slInitSynch returned (frame sync primed)
}
