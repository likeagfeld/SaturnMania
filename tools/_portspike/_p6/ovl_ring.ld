/* ovl_ring.ld -- P6.7d.3 (Task #210): the Ring overlay's fixed-base layout.
 * Linked with `ld -R game.elf` (main-image symbols imported, nothing pulled)
 * at P6_OVL_BASE inside the region the P6.7d.2 SGL re-contract freed.
 * p6_ovl_ring.o is placed FIRST so p6_overlay_entry (its only function,
 * compiled without -ffunction-sections) sits exactly at the window base --
 * the main image calls through that constant. objcopy -O binary emits
 * .text+.data; the loader zeroes the window tail (.bss). */
OUTPUT_ARCH(sh)
SECTIONS {
    . = 0x060C8800; /* O1 (Task #254): P6_OVL_BASE slid DOWN 0x800 (ANIMPAK floor
                      * now 0x060B7800) so the 6KB window holds Ring + Spring;
                      * GLOBALS at 0x060CA000 unchanged. Keep in lockstep with
                      * p6_ovl_api.h P6_OVL_BASE + Animation.hpp P6_HW_ANIMPAK. */
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
