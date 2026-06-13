/* ovl_ring.ld -- P6.7d.3 (Task #210): the Ring overlay's fixed-base layout.
 * Linked with `ld -R game.elf` (main-image symbols imported, nothing pulled)
 * at P6_OVL_BASE inside the region the P6.7d.2 SGL re-contract freed.
 * p6_ovl_ring.o is placed FIRST so p6_overlay_entry (its only function,
 * compiled without -ffunction-sections) sits exactly at the window base --
 * the main image calls through that constant. objcopy -O binary emits
 * .text+.data; the loader zeroes the window tail (.bss). */
OUTPUT_ARCH(sh)
SECTIONS {
    . = 0x060C9000; /* W17: P6_OVL_BASE slid UP by the WRAM-H re-budget
                      * (p6_ovl_api.h; ANIMPAK floor now 0x060B8000) */
    .text : {
        p6_ovl_ring.o(.text*)
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
