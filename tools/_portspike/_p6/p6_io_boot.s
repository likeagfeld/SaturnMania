! =============================================================================
! p6_io_boot.s -- decoupled GFSDEMO-style io-green entry shim (Task #206, P6.2).
!
! This is the C-runtime bring-up for the DECOUPLED file-I/O proof. Unlike the
! bare-metal crt0.s (which is ITSELF the SLSTART entry the BIOS jumps to), this
! build boots through SGL's REAL SLSTART (LIBSGL.A sglI00.o -- pulled into the
! link by referencing slInitSystem, and placed first at 0x06004000 by
! p6_io.linker's `.slstart { ___Start = .; *(SLSTART) }`). SGL's SLSTART sets
! SR=0xF0 / SP=*MasterStack (SGLAREA work-area) / CCR=0x11 (cache enable+purge at
! 0xFFFFFE92) / GBR=0x060FFC00, then `jmp @main` (ELF _main) DIRECTLY -- bypassing
! newlib __libc_init_array. So _main (THIS shim) must do the C-runtime init the
! engine's Reader.cpp TU + newlib need: zero both .bss banks and run the global
! constructors, then enter the proof body p6_io_proof(). (SGL SLSTART contract is
! Ghidra-verified; see p6_io.linker header.)
!
! DELIBERATELY DOES NOT set the stack pointer: SGL's SLSTART already set r15 to
! *MasterStack (inside SGL's 0x060C0000+ work area). Overwriting r15 here would
! move the stack out of SGL's managed region and risk colliding with the image
! .bss/heap below 0x060C0000. This is the ONE change from crt0.s besides the
! section/entry name and the proof-body target.
!
! Sequence (identical to crt0.s MINUS the stack-pointer set):
!   1. Zero WRAM-H .bss [__bss_h_start, __bss_h_end)  -- witnesses, GFS buffer, ...
!   2. Zero WRAM-L .bss [__bss_l_start, __bss_l_end)  -- empty here (heap link-bait).
!   3. Run .init_array forward, then .ctors backward (r8/r9 callee-saved cursors so
!      a ctor cannot clobber them across the jsr -- SH-2 ABI).
!   4. Call _p6_io_proof (the engine-LoadFile witness body); hang if it ever returns.
! =============================================================================

    .text
    .global _main
    .align 1
_main:
    ! --- 1. zero WRAM-H .bss --------------------------------------------------
    mov.l   .L_bss_h_start, r0
    mov.l   .L_bss_h_end, r1
    mov     #0, r2
.L_zh:
    cmp/hs  r1, r0              ! T if r0 >= r1 (reached end)
    bt      .L_zh_done
    mov.l   r2, @r0
    add     #4, r0
    bra     .L_zh
    nop
.L_zh_done:

    ! --- 2. zero WRAM-L .bss --------------------------------------------------
    mov.l   .L_bss_l_start, r0
    mov.l   .L_bss_l_end, r1
.L_zl:
    cmp/hs  r1, r0
    bt      .L_zl_done
    mov.l   r2, @r0
    add     #4, r0
    bra     .L_zl
    nop
.L_zl_done:

    ! --- 3a. run .init_array forward -----------------------------------------
    mov.l   .L_init_start, r8
    mov.l   .L_init_end, r9
.L_init:
    cmp/hs  r9, r8              ! T if r8 >= r9 (done)
    bt      .L_init_done
    mov.l   @r8, r0
    jsr     @r0
    nop
    add     #4, r8
    bra     .L_init
    nop
.L_init_done:

    ! --- 3b. run .ctors backward ---------------------------------------------
    ! r9 = __ctors_start (low bound), r8 = __ctors_end (cursor). Call entries from
    ! (end-4) down to start. Each entry is a function pointer.
    mov.l   .L_ctors_start, r9
    mov.l   .L_ctors_end, r8
.L_ctors:
    cmp/hi  r9, r8              ! T if r8 > r9 (still entries below cursor)
    bf      .L_ctors_done
    add     #-4, r8
    mov.l   @r8, r0
    jsr     @r0
    nop
    bra     .L_ctors
    nop
.L_ctors_done:

    ! --- 4. enter the proof body ---------------------------------------------
    mov.l   .L_proof, r0
    jsr     @r0
    nop

.L_hang:
    bra     .L_hang
    nop

    .align 2
.L_bss_h_start: .long __bss_h_start
.L_bss_h_end:   .long __bss_h_end
.L_bss_l_start: .long __bss_l_start
.L_bss_l_end:   .long __bss_l_end
.L_init_start:  .long __init_array_start
.L_init_end:    .long __init_array_end
.L_ctors_start: .long __ctors_start
.L_ctors_end:   .long __ctors_end
.L_proof:       .long _p6_io_proof
