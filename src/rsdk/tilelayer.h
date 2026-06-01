#ifndef RSDK_TILELAYER_H
#define RSDK_TILELAYER_H

/* Phase 1.1 — TileLayer module, Saturn port of RSDKv5/RSDK/Scene/TileLayer
 * (and the layer subset of RSDKv5/RSDK/Scene/Scene).
 *
 * Source contracts (read but not reproduced):
 *   tools/_decomp_raw/_RSDKv5_Scene.cpp:461-665   LoadSceneFile layer parser
 *   tools/_decomp_raw/_RSDKv5_Scene.cpp           GetTileLayer/ID accessors
 *   docs/RSDK_TO_SATURN_API_MAP.md §7             TileLayer API mapping
 *
 * Phase 1.1 scope: data-access API only (no per-tile rendering — that's
 * deferred to the streaming pipeline in main.c when GHZSetup ports land
 * in Phase 2+). The Title scene doesn't actually USE TileLayers (it
 * draws sprites only), so for Phase 1.1 this module is mostly link-time
 * scaffolding that decomp ports can write `RSDK.GetTileLayer(0)` against
 * without the linker breaking.
 *
 * Saturn deviations:
 *   * scanlineCallback is stored but not invoked. Phase 2+ wires the
 *     VDP2 H-scroll IRQ raster table to this callback list.
 *   * `DrawLayerHScroll` / `DrawLayerVScroll` / `DrawLayerBasic` /
 *     `DrawLayerRotozoom` are NOT exposed here — the existing main.c
 *     handles VDP2 NBG cell-scroll for the FG/BG/sky planes directly.
 *     Phase 2 will replace that with the canonical RSDK draw-layer API. */

#include <stdint.h>
#include <stdbool.h>
#include "storage.h"      /* rsdk_scene_layer_t for layout backing       */

#define RSDK_TILELAYER_COUNT 8

/* Forward declaration so the scanline-callback typedef can name it. */
struct rsdk_tilelayer_s;

/* Per-scanline callback signature (mirrors upstream layer->scanlineCallback).
 * Called once per visible scanline; the callback writes the layer's
 * scrollPos for that line. */
typedef void (*rsdk_scanline_cb_fn)(struct rsdk_tilelayer_s *layer,
                                     int scanline_index,
                                     int32_t *out_scroll_x_fixed,
                                     int32_t *out_scroll_y_fixed);

/* Tile layer struct. Layout chosen for compatibility with decomp call
 * sites (`layer->scanlineCallback = X`, `layer->scrollPos[i]`, ...). */
typedef struct rsdk_tilelayer_s {
    char     name[32];
    uint8_t  type;                  /* LAYER_HSCROLL / VSCROLL / ROTOZOOM / BASIC */
    uint8_t  draw_group;
    uint8_t  width_shift;
    uint8_t  height_shift;
    uint16_t xsize, ysize;          /* tile dimensions                   */
    int16_t  parallax_factor;
    int16_t  scroll_speed;
    uint8_t  deformation;
    uint8_t  visible;
    int32_t  scroll_pos_x;          /* current scroll position (16.16)   */
    int32_t  scroll_pos_y;
    uint16_t *layout;               /* xsize * ysize uint16 cells        */
    uint32_t  layout_size;
    rsdk_scanline_cb_fn scanline_callback;
} rsdk_tilelayer_t;

extern rsdk_tilelayer_t g_rsdk_tilelayers[RSDK_TILELAYER_COUNT];
extern uint8_t          g_rsdk_tilelayer_count;

/* === Public API ===================================================== */

/* Init: zero every layer slot. Idempotent. */
void rsdk_tilelayer_init(void);

/* Lookup by name (decomp `GetTileLayerID`). Returns -1 if absent. */
int rsdk_get_tilelayer_id(const char *name);

/* Lookup by id (decomp `GetTileLayer`). Returns NULL if id is out of
 * range OR the layer slot is empty. */
rsdk_tilelayer_t *rsdk_get_tilelayer(int layer_id);

/* Single-tile read/write (decomp `GetTile`, `SetTile`). The cell encoding
 * is `(flip << 10) | tile_id` in the layer->layout entries. */
uint16_t rsdk_get_tile(int layer_id, int tx, int ty);
void     rsdk_set_tile(int layer_id, int tx, int ty, uint16_t tile);

/* Saturn-port helper: register a layer slot from a parsed
 * rsdk_scene_layer_t (called by rsdk_load_scene_by_name when it walks
 * the Scene.bin layer array). Returns the slot index or -1 if full. */
int rsdk_tilelayer_register(const rsdk_scene_layer_t *src);

#endif /* RSDK_TILELAYER_H */
