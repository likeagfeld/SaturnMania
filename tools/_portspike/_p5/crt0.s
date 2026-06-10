! =============================================================================
! crt0.s -- P3 NOSGL boot stub (engine true-port, #202).
!
! The Saturn IP.BIN (jo-engine/Compiler/COMMON/IP.BIN) loads cd/0.bin to
! 0x06004000 and jumps there. p3.linker places section SLSTART first at
! 0x06004000, so ___Start (the first label here) is the literal first
! instruction the BIOS hands control to. This is the NOSGL path: SGL's SYS_INIT
! is NOT linked, so this stub does the C-runtime bring-up the true-ported core
! needs -- and ONLY that. The core touches no hardware at P3 (RenderDevice /
! AudioDevice / UserCore shims are no-ops), so no VBR / cache / interrupt setup
! is required for a boot-to-ENGINESTATE_NONE witness; this stub deliberately
! omits it (P6 adds it if the true-port becomes the shipping entry).
!
! Sequence:
!   1. SP = 0x06100000 (top of WRAM-H, grows down; p3.linker keeps WRAM-H .bss
!      below 0x060F8000 so the 32 KB stack reserve never overlaps it -- enforced
!      by a hard ASSERT in p3.linker, measured headroom 14,004 B).
!   2. Zero WRAM-H .bss [__bss_h_start, __bss_h_end)  -- engine, sceneInfo, ...
!   3. Zero WRAM-L .bss [__bss_l_start, __bss_l_end)  -- the big arrays.
!   4. Run .init_array forward, then .ctors backward (GCC 8.2 sh-none-elf emits
!      global constructors to .ctors -- verified by -S probe). The trueport set
!      is almost all POD / constexpr-constructible (saturnUserCore included), so
!      these ranges are typically empty; the loops are defensive. Loop cursors
!      live in r8/r9 (callee-saved across the jsr, SH-2 ABI) so a ctor cannot
!      clobber them.
!   5. Call _main (C++ main, ELF _main). main calls RunRetroEngine, which never
!      returns on a good boot; if it ever does, hang so the state is stable.
! =============================================================================

    .section SLSTART,"ax"
    .global ___Start
    .align 1
___Start:
    ! --- 1. stack pointer ----------------------------------------------------
    mov.l   .L_stack_top, r15

    ! --- 2. zero WRAM-H .bss --------------------------------------------------
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

    ! --- 3. zero WRAM-L .bss --------------------------------------------------
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

    ! --- 4a. run .init_array forward -----------------------------------------
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

    ! --- 4b. run .ctors backward ---------------------------------------------
    ! r9 = __ctors_start (low bound), r8 = __ctors_end (cursor). Call entries
    ! from (end-4) down to start. Each entry is a function pointer.
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

    ! --- 5. call main --------------------------------------------------------
    mov.l   .L_main, r0
    jsr     @r0
    nop

.L_hang:
    bra     .L_hang
    nop

    .align 2
.L_stack_top:   .long 0x06100000
.L_bss_h_start: .long __bss_h_start
.L_bss_h_end:   .long __bss_h_end
.L_bss_l_start: .long __bss_l_start
.L_bss_l_end:   .long __bss_l_end
.L_init_start:  .long __init_array_start
.L_init_end:    .long __init_array_end
.L_ctors_start: .long __ctors_start
.L_ctors_end:   .long __ctors_end
.L_main:        .long _main
