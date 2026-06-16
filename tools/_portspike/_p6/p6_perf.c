/* =============================================================================
 * p6_perf.c -- Perf Phase 1 (Task #211): jo-side timing primitives for the
 * continuous engine GHZ loop. Compiled inside the jo make (SRCS +=, beside
 * p6_vdp1.c/p6_snd.c) so it sees the project's jo/SGL config; the pack TU
 * p6_io_main.cpp (no SGL headers) calls these extern "C".
 *
 * Two measurements, doc-cited:
 *   1. TRUE 60 Hz vblank counter -- p6_perf_vblank() is registered via
 *      jo_core_add_vblank_callback (jo-engine/jo_engine/core.c:416-434), which
 *      installs the dispatcher through SGL slIntFunction (core.c:432). Per
 *      ST-238-R1 p184 slIntFunction "registers a function to be executed during
 *      blanking" -> it fires every hardware V-blank (60 Hz, 320 NTSC non-
 *      interlace), NOT frame-locked. So p6_perf_vbl_count advances at true
 *      60 Hz and a per-rendered-frame delta yields fps = 60 * frames / vblanks
 *      (overflow-immune, FpsScale-invariant -- it is all emulated time).
 *   2. SH-2 FRT free-running counter (cost attribution). FRC_H 0xfffffe12 /
 *      FRC_L 0xfffffe13 / TCR 0xfffffe16 (SH7604 hardware manual section 11,
 *      p295; the exact addresses SBL SEGA_TIM.H:63-67 defines -- we use the
 *      literals directly because SEGA_TIM.H drags in SEGA_SYS.H, which uses
 *      Hitachi-SHC `#pragma interrupt` syntax GCC cannot parse). Per the SH7604
 *      16-bit FRC read protocol the FRC_H read LATCHES FRC_L into a temp
 *      register; an ISR that reads the FRT between our two byte reads would
 *      re-latch and corrupt the value. We therefore MASK interrupts (SR.I3-I0 =
 *      15) for ONLY the paired read -- the p6_load_phase_enter pattern
 *      (p6_io_main.cpp:1395-1402), ~6 instructions, negligible. READ-ONLY: we
 *      never write TCR (no divider re-init), so SGL/BIOS timer state is
 *      untouched; the recorded divider (TCR CKS bits) lets the gate convert
 *      ticks->us. On-chip module registers (0xfffffe1x) are accessed directly,
 *      NOT through the 0x20000000 cache-through alias (they are not cached).
 * ============================================================================= */

/* SH-2 on-chip FRT registers (SH7604 manual section 11; == SEGA_TIM.H:63-67). */
#define P6_FRC_H    ((volatile unsigned char *)0xfffffe12u)  /* free-run counter H */
#define P6_FRC_L    ((volatile unsigned char *)0xfffffe13u)  /* free-run counter L */
#define P6_TCR      ((volatile unsigned char *)0xfffffe16u)  /* timer control (CKS) */
#define P6_TCR_CKS  0x03u                                    /* CKS1:0 divider select */

/* True-60 Hz vblank tally. volatile: written in the vblank ISR, read in the
 * frame callback. */
volatile unsigned int p6_perf_vbl_count = 0;

/* Registered via jo_core_add_vblank_callback (main.c, both engine boot
 * branches). Increments once per hardware V-blank. */
void p6_perf_vblank(void)
{
    ++p6_perf_vbl_count;
}

/* Coherent 16-bit FRC read with interrupts masked for the paired byte read
 * (eliminates the SH7604 temp-register latch hazard, validated against SH7604
 * manual section 11). Returns the raw FRC tick value. */
unsigned short p6_perf_frt_get(void)
{
    unsigned short v;
    int sr;
    __asm__ volatile("stc sr, %0" : "=r"(sr));
    {
        int m = (sr & ~0xF0) | 0xF0;            /* mask all maskable interrupts */
        __asm__ volatile("ldc %0, sr" : : "r"(m));
    }
    v = (unsigned short)(((unsigned)(*P6_FRC_H) << 8) | (unsigned)(*P6_FRC_L));
    __asm__ volatile("ldc %0, sr" : : "r"(sr)); /* restore prior interrupt mask */
    return v;
}

/* The FRT divider select (TCR CKS bits): 0=/8, 1=/32, 2=/128. The gate converts
 * FRC ticks -> microseconds via us = ticks * (8 << (2*cks)) / clock_MHz. Pure
 * read. */
int p6_perf_frt_cks(void)
{
    return (int)((*P6_TCR) & P6_TCR_CKS);
}

/* ---------------------------------------------------------------------------
 * Phase 1b (#243): VDP1 draw-completion peek -- the 2-VBLANK-LOCK discriminator.
 * The 30fps is MEASURED to be a 2-vblank structural lock (a 4ms CPU cut moved
 * fps 29.91->29.91, zero frames flipped to 1 vbl), so the frame is NOT CPU-
 * throughput-bound. The remaining unknown is WHY slSynch's swap spans a second
 * vblank: is VDP1 still rasterizing the prior frame's sprite command list when
 * the CPU finishes (DRAW-BOUND -> slSynch waits on VDP1), or is VDP1 already
 * idle and the swap is merely waiting on the vblank boundary (SWAP CADENCE)?
 *
 * VDP1 status registers (ST-013-R3 VDP1 user manual; vdp1-reference.md:20-22,
 * 151-153). Base 0x05D00000:
 *   0x10 EDSR (R) -- transfer end status. bit1 CEF "current frame End Flag" =1
 *        when VDP1 has reached the End command / finished drawing the current
 *        frame; bit0 BEF = previous frame. "Drawing must complete before V-blank
 *        for tear-free display" -- CEF=0 at compute-done == VDP1 overrun.
 *   0x12 LOPR (R) -- last operation command address (the command-list END).
 *   0x14 COPR (R) -- current operation command address (how far VDP1 has reached;
 *        COPR<LOPR => still drawing). COPR/LOPR sizes the overrun severity.
 * These A-Bus I/O registers are NOT cached -- a plain volatile read is coherent
 * (no 0x20000000 alias). READ-ONLY: we never write VDP1 state. The pack TU
 * (p6_io_main.cpp) calls these extern "C" at the end of p6_ghz_frame, the latest
 * point before the implicit slSynch, and accumulates CEF done/busy counters into
 * WRAM-H witnesses (the registers are live state, not serialized into the
 * savestate). */
#define P6_VDP1_EDSR ((volatile unsigned short *)0x05d00010u)
#define P6_VDP1_LOPR ((volatile unsigned short *)0x05d00012u)
#define P6_VDP1_COPR ((volatile unsigned short *)0x05d00014u)

unsigned short p6_perf_vdp1_edsr(void) { return *P6_VDP1_EDSR; }
unsigned short p6_perf_vdp1_lopr(void) { return *P6_VDP1_LOPR; }
unsigned short p6_perf_vdp1_copr(void) { return *P6_VDP1_COPR; }
