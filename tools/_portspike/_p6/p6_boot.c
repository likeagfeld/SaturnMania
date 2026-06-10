// =============================================================================
// p6_boot.c -- two-bank C boot shim for the P6.2 io-green build (Task #206).
//
// WHY THIS FILE EXISTS (the "Fix bare-metal entry" pivot):
//   The io-green build no longer uses the hand-rolled crt0.s as its boot entry.
//   crt0.s DELIBERATELY omitted VBR/cache/interrupt/GBR setup (its own comment:
//   "no VBR / cache / interrupt setup is required for a boot-to-ENGINESTATE_NONE
//   witness; this stub deliberately omits it"). That omission is exactly why
//   p6_gfs_init()'s slInitSystem faulted (address-error PC=0x06000956): the SGL
//   runtime needs GBR=0x060FFC00 (its GBR-relative system base), the cache
//   enabled, and SR.IMASK=0xF during init -- none of which crt0.s established.
//
//   So io-green now boots through SGL's REAL startup, LIBSGL.A(sglI00.o)'s
//   SLSTART (the same ___Start the working jo build uses). Confirmed by Ghidra
//   headless disassembly of sglI00.o SLSTART (0x28 bytes):
//     mov.l @(IMM_main),r4         ; r4 = &main
//     mov.l @(IMM_MasterStack),r15 ; r15 = &MasterStack (SGLAREA work-area table)
//     mov #-0x10; extu.b; ldc r0,sr; SR = 0xF0  (mask all ints during init)
//     mov.l @r15,r15              ; SP = *MasterStack (collision-free SGL stack)
//     mov #0x11; mov.b r0,@(2,r1) ; CCR(0xFFFFFE92) = 0x11 (cache enable+purge)
//     mov.l @(IMM_SystemCtrl=0x060FFC00),r0; jmp @r4; ldc r0,gbr  ; GBR=0x060FFC00, jmp main
//   sglI00.o references `_main` (UNDEF) and jumps to it DIRECTLY -- it bypasses
//   newlib's __setup_argv_and_call_main / __libc_init_array. So the C runtime
//   init that libc-startup normally performs (zero .bss, run global ctors) is
//   NOT done by the SGL path. THIS shim is `main`: it performs that init for the
//   two-bank layout, then runs the engine proof.
//
// WHAT crt0.s USED TO DO that moves here (BSS-zero + ctors): the engine core is
//   C++ with a ~1.18 MB .bss split across two WRAM banks by p6.linker. We must
//   zero BOTH [__bss_h_start,__bss_h_end) (WRAM-H) and [__bss_l_start,__bss_l_end)
//   (WRAM-L), then run .init_array (forward) + .ctors (backward) before any engine
//   global is touched. SGL's ___Start already set SR/SP/CCR/GBR, so this shim does
//   NOT touch those.
//
// This file is linked ONLY for io-green (FILEIO=1). red/green/io-red keep crt0.s
// as their bare-metal entry and p6_main.cpp's main() as the C entry, unchanged.
//
// SYMBOL UNDERSCORE CONVENTION (sh-none-elf prepends ONE underscore to C idents):
//   p6.linker/p6_io.linker emit ELF `__bss_h_start` (two underscores), so the C
//   reference is `_bss_h_start` (one underscore -> ELF two). Mirrors how
//   p6_syscalls.c's C `__heap_start` references the linker's `___heap_start`.
// =============================================================================

typedef void (*ctor_fn)(void);

// p6_io.linker bounds (ELF two-underscore names -> C one-underscore refs).
extern char _bss_h_start[], _bss_h_end[];   // WRAM-H pinned .bss bank
extern char _bss_l_start[], _bss_l_end[];   // WRAM-L swept  .bss bank
extern ctor_fn _init_array_start[], _init_array_end[]; // C++ global ctors (.init_array)
extern ctor_fn _ctors_start[], _ctors_end[];           // legacy ctors (.ctors)

// The engine proof body (p6_main.cpp main(), renamed under -DP6_BOOT_SHIM).
extern int p6_proof_run(void);

// SGL runtime bring-up (p6_gfs.c): slInitSystem + slInitSynch + slTVOff (io-green).
extern void p6_gfs_video_init(void);

int main(void)
{
    char *p;

    // 0. Bring the SGL runtime up FIRST -- before the BSS-zero + ctors below.
    //    MEASURED DELTA (Task #206, 2026-06-05): io-green's slInitSystem call, args,
    //    and sequence (p6_gfs.c:99-108) are byte-identical to the working Coup build
    //    (examples/coup/saturn/main_saturn.c:198-201), yet it faulted (master halt
    //    PC=0x06000956) INSIDE slInitSystem (witness p6_w_io_step==1) -- but ONLY when
    //    it ran after steps 1-2. SGL's ___Start enters main with SR=0xF0 (all ints
    //    masked); the ~1.18 MB byte-wise BSS-zero then holds that mask for ~12 VBlank
    //    periods, so a VBlank is already pending when slInitSystem lowers the mask
    //    mid-setup and SGL's auto-transfer handler runs against a not-yet-built
    //    descriptor. Coup calls slInitSystem as the first statement of main(), fresh
    //    from reset with no pending-interrupt backlog. Running it here mirrors that.
    //    Inputs are valid pre-BSS-zero: s_sgl_textures is only stored as a base ptr,
    //    and the SGL work area is SGLAREA.O (a LOADED section, not .bss). p6_proof_run
    //    no longer calls it, so there is no double slInitSystem.
    p6_gfs_video_init();

    // 1. Zero both .bss banks. crt0.s did this for the bare-metal builds; the SGL
    //    ___Start->main path does NOT, so the shim must. Uses only locals + the
    //    linker bounds -- touches no engine global before they are zeroed.
    for (p = _bss_h_start; p < _bss_h_end; ++p) *p = 0;
    for (p = _bss_l_start; p < _bss_l_end; ++p) *p = 0;

    // 2. Run static constructors. .init_array forward, then .ctors backward
    //    (the GNU ordering crt0.s used). The .tors section is in the WRAM-H
    //    loaded image (NOT .bss), so these pointers survive the zeroing above.
    {
        ctor_fn *f;
        for (f = _init_array_start; f < _init_array_end; ++f)
            (*f)();
        for (f = _ctors_end; f > _ctors_start; ) {
            --f;
            (*f)();
        }
    }

    // 3. Run the engine proof (SetupFunctionTables + Ring dispatch + P6.2 LoadFile
    //    over the GFS backend). slInitSystem inside it now has the SGL runtime env.
    return p6_proof_run();
}
