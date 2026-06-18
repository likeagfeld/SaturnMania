/* ovl_ring.ld -- P6.7d.3 (Task #210): the Ring overlay's fixed-base layout.
 * Linked with `ld -R game.elf` (main-image symbols imported, nothing pulled)
 * at P6_OVL_BASE inside the region the P6.7d.2 SGL re-contract freed.
 * p6_ovl_ring.o is placed FIRST so p6_overlay_entry (its only function,
 * compiled without -ffunction-sections) sits exactly at the window base --
 * the main image calls through that constant. objcopy -O binary emits
 * .text+.data; the loader zeroes the window tail (.bss). */
OUTPUT_ARCH(sh)
SECTIONS {
    . = 0x02690000; /* #258 CART RELOCATION: P6_OVL_BASE moved to the 4MB Extended-RAM
                      * CART (cached alias 0x02690000; cache-through twin 0x22690000 for
                      * the loader write). The full verbatim Game_Ring port overflowed the
                      * WRAM-H GLOBALS wall by 6,744 B with no fundable reclaim -> cart.
                      * Placement PROVEN free: GHZ1 resident layout ends 0x22686900,
                      * sheets < 0x22600000, GFS windows >= 0x22700000 (the 32 KB window
                      * 0x22690000..0x22698000 sits in the 38.6 KB gap). Keep in lockstep
                      * with p6_ovl_api.h P6_OVL_BASE/P6_OVL_WINDOW. GLOBALS decoupled to
                      * its fixed WRAM-H 0x060CA000 (p6_io_main.cpp). */
    .text : {
        p6_ovl_ghz.o(.text.p6_overlay_entry)  /* O1: the entry EXACTLY at the window
                                  * base (built -ffunction-sections so its section is
                                  * named + placeable; the call site is the constant
                                  * P6_OVL_BASE). */
        p6_ovl_ghz.o(.text*)                   /* rest of the GHZ entry TU */
        *(.text*)
        *(.rodata*)
    }
    .data ALIGN(0x10) : {
        *(.data*)
    }
    .bss ALIGN(0x10) (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    }
}
