/* breadcrumb.c - #192 WRAM-L forensic breadcrumb ring (init).
 *
 * See breadcrumb.h for the full rationale. This file holds ONLY the one-time
 * initialiser; the append + SP-sample paths are inline in the header so they
 * cost ~6 uncached stores at each GHZ DMA site with no call overhead in the
 * V-blank budget. */
#include "breadcrumb.h"

/* Zero the ring through the cache-through alias, then stamp the magic LAST.
 * Ordering matters: a V-blank breadcrumb that fires mid-init must see EITHER
 * no magic (and no-op) OR a fully zeroed header with the magic set -- never a
 * live magic over stale head/count. We therefore clear magic first, wipe the
 * body, and write magic last. */
void rsdk_breadcrumb_init(void)
{
    volatile rsdk_bc_ring_t *r = RSDK_BC_RING;
    uint32_t i;

    r->magic     = 0u;            /* disarm: any concurrent append no-ops    */
    r->head      = 0u;
    r->count     = 0u;
    r->last_sp   = 0u;
    r->last_tick = 0u;
    r->wramh_lo  = 0u;
    r->wramh_hi  = 0u;
    r->flags     = 0u;
    for (i = 0u; i < RSDK_BC_SLOTS; ++i) {
        r->rec[i].tag  = 0u;
        r->rec[i].dest = 0u;
        r->rec[i].len  = 0u;
        r->rec[i].val  = 0u;
    }
    r->magic = RSDK_BC_MAGIC;     /* arm LAST                                */
}
