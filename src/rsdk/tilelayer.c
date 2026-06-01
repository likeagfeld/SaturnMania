/* Phase 1.1 — TileLayer module (Saturn implementation).
 *
 * Port reference (NOT reproduced verbatim — API surface re-implemented):
 *   tools/_decomp_raw/_RSDKv5_Scene.cpp:461-665   Scene-file layer load
 *   docs/RSDK_TO_SATURN_API_MAP.md §7             TileLayer API map
 *
 * Phase 1.1 scope: provide GetTileLayer / GetTileLayerID accessors so
 * decomp call sites link. The actual render path is still owned by the
 * existing main.c VDP2 NBG streaming code — that gets refactored to read
 * out of g_rsdk_tilelayers[] when GHZSetup lands. */

#include "tilelayer.h"

#include <string.h>

rsdk_tilelayer_t g_rsdk_tilelayers[RSDK_TILELAYER_COUNT];
uint8_t          g_rsdk_tilelayer_count = 0;

void rsdk_tilelayer_init(void)
{
    memset(g_rsdk_tilelayers, 0, sizeof(g_rsdk_tilelayers));
    g_rsdk_tilelayer_count = 0;
}

int rsdk_get_tilelayer_id(const char *name)
{
    if (!name) return -1;
    for (uint8_t i = 0; i < g_rsdk_tilelayer_count; ++i) {
        const char *ln = g_rsdk_tilelayers[i].name;
        int eq = 1;
        for (int c = 0; c < (int)sizeof(g_rsdk_tilelayers[i].name); ++c) {
            if (ln[c] != name[c]) { eq = 0; break; }
            if (!ln[c]) break;
        }
        if (eq) return (int)i;
    }
    return -1;
}

rsdk_tilelayer_t *rsdk_get_tilelayer(int layer_id)
{
    if (layer_id < 0 || layer_id >= (int)g_rsdk_tilelayer_count) return NULL;
    if (g_rsdk_tilelayers[layer_id].layout == NULL) return NULL;
    return &g_rsdk_tilelayers[layer_id];
}

uint16_t rsdk_get_tile(int layer_id, int tx, int ty)
{
    rsdk_tilelayer_t *L = rsdk_get_tilelayer(layer_id);
    if (!L) return 0;
    if (tx < 0 || ty < 0 || tx >= (int)L->xsize || ty >= (int)L->ysize) return 0;
    return L->layout[ty * L->xsize + tx];
}

void rsdk_set_tile(int layer_id, int tx, int ty, uint16_t tile)
{
    rsdk_tilelayer_t *L = rsdk_get_tilelayer(layer_id);
    if (!L) return;
    if (tx < 0 || ty < 0 || tx >= (int)L->xsize || ty >= (int)L->ysize) return;
    L->layout[ty * L->xsize + tx] = tile;
    /* FIXME Phase 2: queue a VDP2 pattern-name DMA upload for this cell
     * via the V-blank tile-streaming path in main.c. */
}

int rsdk_tilelayer_register(const rsdk_scene_layer_t *src)
{
    if (!src) return -1;
    if (g_rsdk_tilelayer_count >= RSDK_TILELAYER_COUNT) return -1;
    int idx = g_rsdk_tilelayer_count++;
    rsdk_tilelayer_t *L = &g_rsdk_tilelayers[idx];
    memset(L, 0, sizeof(*L));
    int n;
    for (n = 0; n < (int)sizeof(L->name) - 1 && src->name[n]; ++n)
        L->name[n] = src->name[n];
    L->name[n] = 0;
    L->type            = src->type;
    L->draw_group      = src->draw_group;
    L->width_shift     = src->width_shift;
    L->height_shift    = src->height_shift;
    L->xsize           = src->xsize;
    L->ysize           = src->ysize;
    L->parallax_factor = src->parallax_factor;
    L->scroll_speed    = src->scroll_speed;
    L->deformation     = src->deform;
    L->visible         = 1;
    L->layout          = src->layout;
    L->layout_size     = src->layout_len;
    L->scanline_callback = NULL;
    return idx;
}
