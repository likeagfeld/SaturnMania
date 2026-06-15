/* breadcrumb.h - #192 WRAM-L forensic breadcrumb ring.
 *
 * WHY THIS EXISTS
 * ---------------
 * A GHZ-gameplay hard crash (#192) fills Work-RAM-H (0x06000000, 1 MB)
 * with the uniform halfword 0x03EF (measured: 99.99% fill, 4 distinct
 * bytes), derailing the master SH-2 (PC=0x00000003 boot-ROM, SP=0x05374040
 * A-bus garbage, last good PR=ghz_fg_vblank+24). The stomp DESTROYS the
 * normal counters and BOTH stacks, so the post-mortem capture cannot name
 * the proximate writer from WRAM-H. Work-RAM-L was measured INTACT through
 * the same crash.
 *
 * This ring lives in the 8 KB WRAM-L slack [0x002FE000,0x00300000) that
 * player_atlas.c:47-52 documents as unused ("No other LWRAM region is
 * touched"). Because WRAM-L survives the stomp, the last records before the
 * derail name the writer in the NEXT crash capture.
 *
 * CACHE-THROUGH (binding): every access uses the |0x20000000 alias
 * (0x002FE000 -> 0x202FE000) so each store reaches physical RAM immediately
 * and is visible in an F5 savestate WITHOUT a cache flush. This mirrors the
 * established project convention for savestate-visible diagnostics
 * (memory/sgl-audio-vs-scroll-cpu-dma-conflict.md: slDMAXCopy is NOT
 * cache-aware; we already alias g_ghz_page through 0x20000000 for the same
 * reason). Citations: ST-097-R5 (SCU User Manual) memory map -- WRAM-L at
 * 0x00200000, cache-through mirror at +0x20000000.
 *
 * This is MEASUREMENT-ENABLING instrumentation, not a fix. It does not
 * change the crash; it makes the next crash self-describing.
 */
#ifndef RSDK_BREADCRUMB_H
#define RSDK_BREADCRUMB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ring base: WRAM-L 0x002FE000 slack, addressed through the cache-through
 * alias so writes are savestate-visible without a flush. */
#define RSDK_BC_BASE_CT   0x202FE000u
#define RSDK_BC_MAGIC     0x43425344u   /* ASCII 'CBSD' -- spottable in a dump */
#define RSDK_BC_SLOTS     256u          /* power of two; ring index masks      */

/* WRAM-H probe addresses (cache-through alias). Read each V-blank into the
 * header; when the 0x03EF stomp reaches them they flip, timestamping the
 * crash to a tick. LO sits in low .text/.data, HI sits near the master
 * stack -- a forward fill flips them at different ticks, hinting direction. */
#define RSDK_BC_WRAMH_LO_CT  0x2600F000u   /* WRAM-H 0x0600F000 (low .text)   */
#define RSDK_BC_WRAMH_HI_CT  0x260FF000u   /* WRAM-H 0x060FF000 (near stack)  */

/* Call-site tags. STABLE ids -- the probe maps these back to source lines. */
enum {
    RSDK_BC_GHZ_VBLANK_DMA = 1,  /* scene_ghz.c ghz_fg_vblank slDMAXCopy     */
    RSDK_BC_GHZ_CELL_DMA   = 2,  /* scene_ghz.c setup slDMACopy cell bank    */
    RSDK_BC_GHZ_PAGE_DMA   = 3,  /* scene_ghz.c setup slDMAXCopy page push   */
};

typedef struct {
    uint32_t tag;   /* RSDK_BC_* call-site id                                */
    uint32_t dest;  /* DMA destination address (flag if in WRAM-H)          */
    uint32_t len;   /* DMA byte length                                       */
    uint32_t val;   /* sample: first source halfword (flag if 0x03EF)       */
} rsdk_bc_rec_t;

typedef struct {
    uint32_t magic;     /* RSDK_BC_MAGIC once initialised                    */
    uint32_t head;      /* next write slot, already masked to [0,SLOTS)      */
    uint32_t count;     /* total records ever written (monotonic)           */
    uint32_t last_sp;   /* most-recent sampled stack pointer                 */
    uint32_t last_tick; /* V-blank frame counter at last SP sample          */
    uint32_t wramh_lo;  /* halfword probed from RSDK_BC_WRAMH_LO_CT          */
    uint32_t wramh_hi;  /* halfword probed from RSDK_BC_WRAMH_HI_CT          */
    uint32_t flags;     /* reserved (0)                                      */
    rsdk_bc_rec_t rec[RSDK_BC_SLOTS];
} rsdk_bc_ring_t;

#define RSDK_BC_RING  ((volatile rsdk_bc_ring_t *)RSDK_BC_BASE_CT)

/* One-time init at GHZ scene load. Zeroes the ring then sets magic LAST so a
 * concurrent reader never sees a live magic over a half-zeroed ring. */
void rsdk_breadcrumb_init(void);

/* Append one record. Inline (no call overhead in the V-blank budget). Safe
 * before init -- no-ops until the magic is set. ~6 uncached stores. */
static inline void rsdk_breadcrumb(uint32_t tag, uint32_t dest,
                                   uint32_t len, uint32_t val)
{
    volatile rsdk_bc_ring_t *r = RSDK_BC_RING;
    uint32_t h;
    if (r->magic != RSDK_BC_MAGIC) return;
    h = r->head & (RSDK_BC_SLOTS - 1u);
    r->rec[h].tag  = tag;
    r->rec[h].dest = dest;
    r->rec[h].len  = len;
    r->rec[h].val  = val;
    r->head  = (h + 1u) & (RSDK_BC_SLOTS - 1u);
    r->count = r->count + 1u;
}

/* Sample the caller's stack pointer + both WRAM-H probe halfwords into the
 * header. Call once per V-blank. `sp` = a current stack address (pass
 * &local), `tick` = a monotonic frame counter. */
static inline void rsdk_breadcrumb_mark_sp(uint32_t sp, uint32_t tick)
{
    volatile rsdk_bc_ring_t *r = RSDK_BC_RING;
    if (r->magic != RSDK_BC_MAGIC) return;
    r->last_sp   = sp;
    r->last_tick = tick;
    r->wramh_lo  = *(volatile uint16_t *)RSDK_BC_WRAMH_LO_CT;
    r->wramh_hi  = *(volatile uint16_t *)RSDK_BC_WRAMH_HI_CT;
}

#ifdef __cplusplus
}
#endif

#endif /* RSDK_BREADCRUMB_H */
