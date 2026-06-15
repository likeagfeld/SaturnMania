/* Task #180 step 2 - decomp-faithful tile 6-sensor collision.
 *
 * Mechanical port of rsdkv5-src/RSDKv5/RSDK/Scene/Collision.cpp (REV0U,
 * !RETRO_USE_ORIGINAL_CODE branch) plus the Scene.cpp:866-957 flip-variant
 * builder applied at LOOKUP time (the Saturn deviation documented in
 * collision.h). Every function below cites its Collision.cpp line range.
 *
 * The decomp pre-bakes 4 flip variants into collisionMasks[plane][TILE*4] and
 * indexes them with `tile & 0xFFF`. We ship only the base 1024 masks/infos per
 * plane and synthesise the FLIP_X/FLIP_Y/FLIP_XY variant from the base record
 * via the m_ and a_ helpers below; that transform is the exact inverse of
 * Scene.cpp:869-949 (cross-checked against tools/qa_ghz_6sensor_gate.py).
 */

#include "collision.h"

/* --- constants (Collision.cpp:10-58) --------------------------------- */
#define COLLISION_OFFSET        0x40000  /* TO_FIXED(4)                   */
#define COLLISION_MIN_DISTANCE  0xE0000  /* TO_FIXED(14)                  */
#define LOW_COLLISION_TOL       8
#define HIGH_COLLISION_TOL      14
#define FLOOR_ANGLE_TOL         0x20
#define WALL_ANGLE_TOL          0x20
#define ROOF_ANGLE_TOL          0x20

static int iabs(int v) { return v < 0 ? -v : v; }

/* inv() from Scene.cpp: a flip mirrors a height column to (0xF - h), but a
 * non-solid 0xFF stays 0xFF. */
static uint8_t inv4(uint8_t h) { return h == 0xFF ? 0xFF : (uint8_t)(0xF - h); }

/* --- decomp sin256/cos256 (Math.cpp:55-104, scale +-256 = 1.0) ------- */
#include "_sincos256.txt"

/* --- module context (mirrors Collision.cpp globals) ------------------ */
static const rsdk_collision_mask_t *s_masks[RSDK_CPATH_COUNT];
static const rsdk_tile_info_t      *s_info[RSDK_CPATH_COUNT];
static rsdk_collision_layer_t       s_layers[RSDK_LAYER_COUNT];
static rsdk_collision_entity_t     *s_entity;
static rsdk_hitbox_t                s_outer, s_inner;
static rsdk_collision_sensor_t      s_sensors[6];
static int32_t                      s_collisionTolerance;
static int32_t                      s_collisionMaskAir;
static bool                         s_useCollisionOffset;

/* GMS2 compacted-table indirection (#180 step 3): tileID -> slot remap and the
 * slot count (slot 0 = blank sentinel). s_masks/s_info are SLOT-indexed into
 * the resident cd/GHZ1MASK.BIN blob; lookups go through s_remap[tile & 0x3FF].
 * SH-2 is big-endian and GHZ1MASK.BIN stores its u16 fields big-endian, so the
 * remap pointer aliases the blob directly with no copy or byteswap. */
static const uint16_t              *s_remap;
static uint16_t                     s_slot_count;

/* --- lookup-time flip transforms (inverse of Scene.cpp:869-949) ------ *
 * (fx,fy) = (FLIP_X,FLIP_Y) decoded from layout bits 10/11. c = column. */

static uint8_t m_floor(const rsdk_collision_mask_t *m, int fx, int fy, int c)
{
    if (!fx && !fy) return m->floorMasks[c];
    if ( fx && !fy) return m->floorMasks[0xF - c];
    if (!fx &&  fy) return inv4(m->roofMasks[c]);
    return inv4(m->roofMasks[0xF - c]);
}
static uint8_t m_lwall(const rsdk_collision_mask_t *m, int fx, int fy, int c)
{
    if (!fx && !fy) return m->lWallMasks[c];
    if ( fx && !fy) return inv4(m->rWallMasks[c]);
    if (!fx &&  fy) return m->lWallMasks[0xF - c];
    return inv4(m->rWallMasks[0xF - c]);
}
static uint8_t m_rwall(const rsdk_collision_mask_t *m, int fx, int fy, int c)
{
    if (!fx && !fy) return m->rWallMasks[c];
    if ( fx && !fy) return inv4(m->lWallMasks[c]);
    if (!fx &&  fy) return m->rWallMasks[0xF - c];
    return inv4(m->lWallMasks[0xF - c]);
}
static uint8_t m_roof(const rsdk_collision_mask_t *m, int fx, int fy, int c)
{
    if (!fx && !fy) return m->roofMasks[c];
    if ( fx && !fy) return m->roofMasks[0xF - c];
    if (!fx &&  fy) return inv4(m->floorMasks[c]);
    return inv4(m->floorMasks[0xF - c]);
}

static uint8_t a_floor(const rsdk_tile_info_t *t, int fx, int fy)
{
    int fa = t->floorAngle, roa = t->roofAngle, v;
    if (!fx && !fy) v = fa;
    else if ( fx && !fy) v = -fa;
    else if (!fx &&  fy) v = -0x80 - roa;
    else v = 0x80 + roa;
    return (uint8_t)v;
}
static uint8_t a_lwall(const rsdk_tile_info_t *t, int fx, int fy)
{
    int la = t->lWallAngle, rwa = t->rWallAngle, v;
    if (!fx && !fy) v = la;
    else if ( fx && !fy) v = -rwa;
    else if (!fx &&  fy) v = -0x80 - la;
    else v = 0x80 + rwa;
    return (uint8_t)v;
}
static uint8_t a_rwall(const rsdk_tile_info_t *t, int fx, int fy)
{
    int la = t->lWallAngle, rwa = t->rWallAngle, v;
    if (!fx && !fy) v = rwa;
    else if ( fx && !fy) v = -la;
    else if (!fx &&  fy) v = -0x80 - rwa;
    else v = 0x80 + la;
    return (uint8_t)v;
}
static uint8_t a_roof(const rsdk_tile_info_t *t, int fx, int fy)
{
    int fa = t->floorAngle, roa = t->roofAngle, v;
    if (!fx && !fy) v = roa;
    else if ( fx && !fy) v = -roa;
    else if (!fx &&  fy) v = -0x80 - fa;
    else v = 0x80 + fa;
    return (uint8_t)v;
}

/* --- binding -------------------------------------------------------- */

/* Bind the GMS2 compacted mask/info/remap blob (cd/GHZ1MASK.BIN, #180 step 3).
 *
 * Layout (all multi-byte fields big-endian; see tools/build_tilemasks.py):
 *   [0..3]  'GMS2'
 *   [4..5]  u16 planes        (RSDK_CPATH_COUNT)
 *   [6..7]  u16 slot_count    (S; slot 0 = blank sentinel)
 *   [8..9]  u16 tile_count    (RSDK_TILE_COUNT, remap length)
 *   [10..11] u16 reserved
 *   remap:  tile_count * u16  (tileID -> slot)
 *   masks:  planes * S * 64 B (rsdk_collision_mask_t stride)
 *   info:   planes * S *  8 B (rsdk_tile_info_t stride)
 *
 * The blob must stay resident; every pointer below aliases into it directly. */
void rsdk_collision_bind_compact(const void *blob)
{
    const uint8_t *b = (const uint8_t *)blob;
    if (!b) return;
    if (b[0] != 'G' || b[1] != 'M' || b[2] != 'S' || b[3] != '2') return;

    uint16_t planes     = (uint16_t)((b[4] << 8) | b[5]);
    uint16_t slot_count = (uint16_t)((b[6] << 8) | b[7]);
    /* b[8..9] tile_count (== RSDK_TILE_COUNT), b[10..11] reserved */

    s_slot_count = slot_count;
    s_remap      = (const uint16_t *)(b + 12);

    const uint8_t *masks0 = b + 12 + RSDK_TILE_COUNT * 2;
    uint32_t mask_block = (uint32_t)slot_count * 64u;
    uint32_t info_block = (uint32_t)slot_count * 8u;
    const uint8_t *info0 = masks0 + (uint32_t)planes * mask_block;

    for (int p = 0; p < RSDK_CPATH_COUNT && p < (int)planes; ++p) {
        s_masks[p] = (const rsdk_collision_mask_t *)(masks0 + (uint32_t)p * mask_block);
        s_info[p]  = (const rsdk_tile_info_t *)(info0 + (uint32_t)p * info_block);
    }
}

void rsdk_collision_clear_layers(void)
{
    for (int i = 0; i < RSDK_LAYER_COUNT; ++i) {
        s_layers[i].layout     = 0;
        s_layers[i].xsize      = 0;
        s_layers[i].ysize      = 0;
        s_layers[i].widthShift = 0;
        s_layers[i].position.x = 0;
        s_layers[i].position.y = 0;
        s_layers[i].active     = false;
        s_layers[i].win        = 0;
        s_layers[i].winW       = 0;
        s_layers[i].winBase    = 0;
    }
}

void rsdk_collision_set_layer(int idx, const uint16_t *layout,
                              int32_t xsize, int32_t ysize, uint8_t widthShift,
                              int32_t pos_x_px, int32_t pos_y_px)
{
    if (idx < 0 || idx >= RSDK_LAYER_COUNT) return;
    s_layers[idx].layout     = layout;
    s_layers[idx].xsize      = xsize;
    s_layers[idx].ysize      = ysize;
    s_layers[idx].widthShift = widthShift;
    s_layers[idx].position.x = pos_x_px;
    s_layers[idx].position.y = pos_y_px;
    s_layers[idx].active     = (layout != 0);
    s_layers[idx].win        = 0;       /* full-grid: no toroidal window */
    s_layers[idx].winW       = 0;
    s_layers[idx].winBase    = 0;
}

/* #180 step 3d - bind a toroidal column-window (src/rsdk/colwindow.c owns the
 * buffer). The full-grid `layout` is left NULL so s_layer_tile() takes the
 * window path. */
void rsdk_collision_set_layer_window(int idx, const uint16_t *win,
                                     int32_t xsize, int32_t ysize,
                                     uint8_t widthShift, int32_t winW,
                                     int32_t winBase,
                                     int32_t pos_x_px, int32_t pos_y_px)
{
    if (idx < 0 || idx >= RSDK_LAYER_COUNT) return;
    s_layers[idx].layout     = 0;
    s_layers[idx].xsize      = xsize;
    s_layers[idx].ysize      = ysize;
    s_layers[idx].widthShift = widthShift;
    s_layers[idx].position.x = pos_x_px;
    s_layers[idx].position.y = pos_y_px;
    s_layers[idx].active     = (win != 0);
    s_layers[idx].win        = win;
    s_layers[idx].winW       = winW;
    s_layers[idx].winBase    = winBase;
}

void rsdk_collision_set_window_base(int idx, int32_t winBase)
{
    if (idx < 0 || idx >= RSDK_LAYER_COUNT) return;
    s_layers[idx].winBase = winBase;
}

/* Single tile-fetch helper for all 8 sensor lookup sites (#180 step 3d).
 * Mirrors tools/qa_ghz_colwindow_gate.py ColWindow.fetch + the legacy
 * full-grid index it replaces. Out-of-band / out-of-grid => 0xFFFF (the
 * "no tile" sentinel the sensor loops already test via `tile < 0xFFFF`). */
static uint16_t s_layer_tile(const rsdk_collision_layer_t *layer,
                             int32_t worldCol, int32_t worldRow)
{
    if (layer->win) {
        if (worldCol < layer->winBase || worldCol >= layer->winBase + layer->winW)
            return 0xFFFF;
        return layer->win[(worldCol % layer->winW) * layer->ysize + worldRow];
    }
    return layer->layout[worldCol + (worldRow << layer->widthShift)];
}

void rsdk_collision_set_context(rsdk_collision_entity_t *entity,
                                int32_t tolerance)
{
    s_entity             = entity;
    s_collisionTolerance = tolerance;
}

/* #180 step 4b - per-column topmost solid-floor world-Y (the integrate-then-
 * snap surface query). LINE-FOR-LINE mirror of tools/qa_ghz_fall_through_
 * gate.py oracle_floor, which the fall-through gate proved drops Sonic through
 * 0 of 16384 real-floor columns (vs 2384 for the old GHZ1SURF heightmap):
 *
 *   plane 0; solid = 1<<12 (TILECOLLISION_DOWN plane-A floor). For each active
 *   FG layer, scan rows top-down and take the FIRST solid-floor tile whose
 *   flip-applied floor mask (m_floor) is < 0xFF -> the topmost real surface in
 *   that column. surf = layer->position.y + ty*16 + mask. Minimum across all
 *   active layers. A column with no solid-floor tile (or x outside the resident
 *   window band, where s_layer_tile returns 0xFFFF) is a pit -> SMS_NO_FLOOR.
 *
 * Note (faithfulness): mask==0xFF is `continue` (keep scanning DOWN), not break
 * -- a solid-flagged tile with an empty floor column must not occlude a real
 * floor below it, exactly as oracle_floor's `if fm == 0xFF: continue`. */
int32_t rsdk_collision_column_floor(int32_t x_px, int32_t *out_angle)
{
    const int     plane = 0;
    const int32_t solid = (1 << 12);
    int32_t best       = 0x7FFFFFFF;
    int32_t best_angle = 0;

    for (int l = 0; l < RSDK_LAYER_COUNT; ++l) {
        const rsdk_collision_layer_t *layer = &s_layers[l];
        if (!layer->active) continue;

        int32_t colX = x_px - layer->position.x;
        if (colX < 0 || colX >= RSDK_TILE_SIZE * layer->xsize) continue;

        int32_t tx = colX / RSDK_TILE_SIZE;
        int      c  = colX & 0xF;

        for (int32_t ty = 0; ty < layer->ysize; ++ty) {
            uint16_t tile = s_layer_tile(layer, tx, ty);
            if (tile >= 0xFFFF || !(tile & solid)) continue;
            int     slot = s_remap[tile & 0x3FF];
            int     fx   = (tile >> 10) & 1, fy = (tile >> 11) & 1;
            uint8_t mask = m_floor(&s_masks[plane][slot], fx, fy, c);
            if (mask >= 0xFF) continue;
            int32_t surf = layer->position.y + ty * RSDK_TILE_SIZE + mask;
            if (surf < best) {
                best       = surf;
                best_angle = a_floor(&s_info[plane][slot], fx, fy);
            }
            break;   /* topmost solid floor in this layer -> done with column */
        }
    }

    if (best == 0x7FFFFFFF) {
        if (out_angle) *out_angle = 0;
        return 0xFFFF;   /* SMS_NO_FLOOR */
    }
    if (out_angle) *out_angle = best_angle;
    return best;
}

/* --- FindFloorPosition (Collision.cpp:2162-2230) -------------------- */
void rsdk_find_floor_position(rsdk_collision_sensor_t *sensor)
{
    int32_t posX = RSDK_FROM_FIXED(sensor->position.x);
    int32_t posY = RSDK_FROM_FIXED(sensor->position.y);
    int plane = s_entity->collisionPlane;

    int32_t solid;
    if (s_entity->tileCollisions == TILECOLLISION_DOWN)
        solid = plane ? (1 << 14) : (1 << 12);
    else
        solid = plane ? (1 << 15) : (1 << 13);

    int32_t startY = posY;

    for (int l = 0, layerID = 1; l < RSDK_LAYER_COUNT; ++l, layerID <<= 1) {
        if (s_entity->collisionLayers & layerID) {
            rsdk_collision_layer_t *layer = &s_layers[l];
            int32_t colX = posX - layer->position.x;
            int32_t colY = posY - layer->position.y;
            int32_t cy   = (colY & -RSDK_TILE_SIZE) - RSDK_TILE_SIZE;
            if ((layer->layout || layer->win) && colX >= 0 && colX < RSDK_TILE_SIZE * layer->xsize) {
                for (int i = 0; i < 3; ++i) {
                    if (cy >= 0 && cy < RSDK_TILE_SIZE * layer->ysize) {
                        uint16_t tile = s_layer_tile(layer, colX / RSDK_TILE_SIZE, cy / RSDK_TILE_SIZE);
                        if (tile < 0xFFFF && (tile & solid)) {
                            int slot = s_remap[tile & 0x3FF], fx = (tile >> 10) & 1, fy = (tile >> 11) & 1;
                            int32_t mask      = m_floor(&s_masks[plane][slot], fx, fy, colX & 0xF);
                            int32_t ty        = cy + mask;
                            int32_t tileAngle = a_floor(&s_info[plane][slot], fx, fy);
                            if (mask < 0xFF) {
                                if (!sensor->collided || startY >= ty) {
                                    if (iabs(colY - ty) <= s_collisionTolerance) {
                                        if (iabs(sensor->angle - tileAngle) <= RSDK_TILE_SIZE * 2
                                            || iabs(sensor->angle - tileAngle + 0x100) <= FLOOR_ANGLE_TOL
                                            || iabs(sensor->angle - tileAngle - 0x100) <= FLOOR_ANGLE_TOL) {
                                            sensor->collided   = true;
                                            sensor->angle      = (uint8_t)tileAngle;
                                            sensor->position.y = RSDK_TO_FIXED(ty + layer->position.y);
                                            startY             = ty;
                                            i                  = 3;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    cy += RSDK_TILE_SIZE;
                }
            }
            posX = layer->position.x + colX;
            posY = layer->position.y + colY;
        }
    }
}

/* --- FindLWallPosition (Collision.cpp:2231-2278) -------------------- */
void rsdk_find_lwall_position(rsdk_collision_sensor_t *sensor)
{
    int32_t posX = RSDK_FROM_FIXED(sensor->position.x);
    int32_t posY = RSDK_FROM_FIXED(sensor->position.y);
    int plane = s_entity->collisionPlane;

    int32_t solid = plane ? ((1 << 14) | (1 << 15)) : ((1 << 12) | (1 << 13));
    int32_t startX = posX;

    for (int l = 0, layerID = 1; l < RSDK_LAYER_COUNT; ++l, layerID <<= 1) {
        if (s_entity->collisionLayers & layerID) {
            rsdk_collision_layer_t *layer = &s_layers[l];
            int32_t colX = posX - layer->position.x;
            int32_t colY = posY - layer->position.y;
            int32_t cx   = (colX & -RSDK_TILE_SIZE) - RSDK_TILE_SIZE;
            if ((layer->layout || layer->win) && colY >= 0 && colY < RSDK_TILE_SIZE * layer->ysize) {
                for (int i = 0; i < 3; ++i) {
                    if (cx >= 0 && cx < RSDK_TILE_SIZE * layer->xsize) {
                        uint16_t tile = s_layer_tile(layer, cx / RSDK_TILE_SIZE, colY / RSDK_TILE_SIZE);
                        if (tile < 0xFFFF && (tile & solid)) {
                            int slot = s_remap[tile & 0x3FF], fx = (tile >> 10) & 1, fy = (tile >> 11) & 1;
                            int32_t mask      = m_lwall(&s_masks[plane][slot], fx, fy, colY & 0xF);
                            int32_t tx        = cx + mask;
                            int32_t tileAngle = a_lwall(&s_info[plane][slot], fx, fy);
                            if (mask < 0xFF) {
                                if (!sensor->collided || startX >= tx) {
                                    if (iabs(colX - tx) <= s_collisionTolerance && iabs(sensor->angle - tileAngle) <= WALL_ANGLE_TOL) {
                                        sensor->collided   = true;
                                        sensor->angle      = (uint8_t)tileAngle;
                                        sensor->position.x = RSDK_TO_FIXED(tx + layer->position.x);
                                        startX             = tx;
                                        i                  = 3;
                                    }
                                }
                            }
                        }
                    }
                    cx += RSDK_TILE_SIZE;
                }
            }
            posX = layer->position.x + colX;
            posY = layer->position.y + colY;
        }
    }
}

/* --- FindRoofPosition (Collision.cpp:2279-2335) -------------------- */
void rsdk_find_roof_position(rsdk_collision_sensor_t *sensor)
{
    int32_t posX = RSDK_FROM_FIXED(sensor->position.x);
    int32_t posY = RSDK_FROM_FIXED(sensor->position.y);
    int plane = s_entity->collisionPlane;

    int32_t solid;
    if (s_entity->tileCollisions == TILECOLLISION_DOWN)
        solid = plane ? (1 << 15) : (1 << 13);
    else
        solid = plane ? (1 << 14) : (1 << 12);

    int32_t startY = posY;

    for (int l = 0, layerID = 1; l < RSDK_LAYER_COUNT; ++l, layerID <<= 1) {
        if (s_entity->collisionLayers & layerID) {
            rsdk_collision_layer_t *layer = &s_layers[l];
            int32_t colX = posX - layer->position.x;
            int32_t colY = posY - layer->position.y;
            int32_t cy   = (colY & -RSDK_TILE_SIZE) + RSDK_TILE_SIZE;
            if ((layer->layout || layer->win) && colX >= 0 && colX < RSDK_TILE_SIZE * layer->xsize) {
                for (int i = 0; i < 3; ++i) {
                    if (cy >= 0 && cy < RSDK_TILE_SIZE * layer->ysize) {
                        uint16_t tile = s_layer_tile(layer, colX / RSDK_TILE_SIZE, cy / RSDK_TILE_SIZE);
                        if (tile < 0xFFFF && (tile & solid)) {
                            int slot = s_remap[tile & 0x3FF], fx = (tile >> 10) & 1, fy = (tile >> 11) & 1;
                            int32_t mask      = m_roof(&s_masks[plane][slot], fx, fy, colX & 0xF);
                            int32_t ty        = cy + mask;
                            int32_t tileAngle = a_roof(&s_info[plane][slot], fx, fy);
                            if (mask < 0xFF) {
                                if (!sensor->collided || startY <= ty) {
                                    if (iabs(colY - ty) <= s_collisionTolerance && iabs(sensor->angle - tileAngle) <= ROOF_ANGLE_TOL) {
                                        sensor->collided   = true;
                                        sensor->angle      = (uint8_t)tileAngle;
                                        sensor->position.y = RSDK_TO_FIXED(ty + layer->position.y);
                                        startY             = ty;
                                        i                  = 3;
                                    }
                                }
                            }
                        }
                    }
                    cy -= RSDK_TILE_SIZE;
                }
            }
            posX = layer->position.x + colX;
            posY = layer->position.y + colY;
        }
    }
}

/* --- FindRWallPosition (Collision.cpp:2336-2384) ------------------- */
void rsdk_find_rwall_position(rsdk_collision_sensor_t *sensor)
{
    int32_t posX = RSDK_FROM_FIXED(sensor->position.x);
    int32_t posY = RSDK_FROM_FIXED(sensor->position.y);
    int plane = s_entity->collisionPlane;

    int32_t solid = plane ? ((1 << 14) | (1 << 15)) : ((1 << 12) | (1 << 13));
    int32_t startX = posX;

    for (int l = 0, layerID = 1; l < RSDK_LAYER_COUNT; ++l, layerID <<= 1) {
        if (s_entity->collisionLayers & layerID) {
            rsdk_collision_layer_t *layer = &s_layers[l];
            int32_t colX = posX - layer->position.x;
            int32_t colY = posY - layer->position.y;
            int32_t cx   = (colX & -RSDK_TILE_SIZE) + RSDK_TILE_SIZE;
            if ((layer->layout || layer->win) && colY >= 0 && colY < RSDK_TILE_SIZE * layer->ysize) {
                for (int i = 0; i < 3; ++i) {
                    if (cx >= 0 && cx < RSDK_TILE_SIZE * layer->xsize) {
                        uint16_t tile = s_layer_tile(layer, cx / RSDK_TILE_SIZE, colY / RSDK_TILE_SIZE);
                        if (tile < 0xFFFF && (tile & solid)) {
                            int slot = s_remap[tile & 0x3FF], fx = (tile >> 10) & 1, fy = (tile >> 11) & 1;
                            int32_t mask      = m_rwall(&s_masks[plane][slot], fx, fy, colY & 0xF);
                            int32_t tx        = cx + mask;
                            int32_t tileAngle = a_rwall(&s_info[plane][slot], fx, fy);
                            if (mask < 0xFF) {
                                if (!sensor->collided || startX <= tx) {
                                    if (iabs(colX - tx) <= s_collisionTolerance && iabs(sensor->angle - tileAngle) <= WALL_ANGLE_TOL) {
                                        sensor->collided   = true;
                                        sensor->angle      = (uint8_t)tileAngle;
                                        sensor->position.x = RSDK_TO_FIXED(tx + layer->position.x);
                                        startX             = tx;
                                        i                  = 3;
                                    }
                                }
                            }
                        }
                    }
                    cx -= RSDK_TILE_SIZE;
                }
            }
            posX = layer->position.x + colX;
            posY = layer->position.y + colY;
        }
    }
}

/* --- FloorCollision (Collision.cpp:2386-2471, REV0U) --------------- */
void rsdk_floor_collision(rsdk_collision_sensor_t *sensor)
{
    int32_t posX = RSDK_FROM_FIXED(sensor->position.x);
    int32_t posY = RSDK_FROM_FIXED(sensor->position.y);
    int plane = s_entity->collisionPlane;

    int32_t solid;
    if (s_entity->tileCollisions == TILECOLLISION_DOWN)
        solid = plane ? (1 << 14) : (1 << 12);
    else
        solid = plane ? (1 << 15) : (1 << 13);

    int32_t collideAngle = 0;
    int32_t collidePos   = 0x7FFFFFFF;

    for (int l = 0, layerID = 1; l < RSDK_LAYER_COUNT; ++l, layerID <<= 1) {
        if (s_entity->collisionLayers & layerID) {
            rsdk_collision_layer_t *layer = &s_layers[l];
            int32_t colX = posX - layer->position.x;
            int32_t colY = posY - layer->position.y;
            int32_t cy   = (colY & -RSDK_TILE_SIZE) - RSDK_TILE_SIZE;
            if ((layer->layout || layer->win) && colX >= 0 && colX < RSDK_TILE_SIZE * layer->xsize) {
                int stepCount = 2;
                for (int i = 0; i < stepCount; ++i) {
                    int32_t step = RSDK_TILE_SIZE;
                    if (cy >= 0 && cy < RSDK_TILE_SIZE * layer->ysize) {
                        uint16_t tile = s_layer_tile(layer, colX / RSDK_TILE_SIZE, cy / RSDK_TILE_SIZE);
                        if (tile < 0xFFFF && (tile & solid)) {
                            int slot = s_remap[tile & 0x3FF], fx = (tile >> 10) & 1, fy = (tile >> 11) & 1;
                            int32_t mask = m_floor(&s_masks[plane][slot], fx, fy, colX & 0xF);
                            int32_t ty   = layer->position.y + cy + mask;
                            if (mask < 0xFF) {
                                step = -RSDK_TILE_SIZE;
                                if (colY < collidePos) {
                                    collideAngle = a_floor(&s_info[plane][slot], fx, fy);
                                    collidePos   = ty;
                                    i            = stepCount;
                                }
                            }
                        }
                    }
                    cy += step;
                }
            }
            posX = layer->position.x + colX;
            posY = layer->position.y + colY;
        }
    }

    if (collidePos != 0x7FFFFFFF) {
        int32_t collideDist = sensor->position.y - RSDK_TO_FIXED(collidePos);
        if (sensor->position.y >= RSDK_TO_FIXED(collidePos) && collideDist <= COLLISION_MIN_DISTANCE) {
            sensor->angle      = (uint8_t)collideAngle;
            sensor->position.y = RSDK_TO_FIXED(collidePos);
            sensor->collided   = true;
        }
    }
}

/* --- LWallCollision (Collision.cpp:2472-2511) --------------------- */
void rsdk_lwall_collision(rsdk_collision_sensor_t *sensor)
{
    int32_t posX = RSDK_FROM_FIXED(sensor->position.x);
    int32_t posY = RSDK_FROM_FIXED(sensor->position.y);
    int plane = s_entity->collisionPlane;

    int32_t solid = plane ? (1 << 15) : (1 << 13);

    for (int l = 0, layerID = 1; l < RSDK_LAYER_COUNT; ++l, layerID <<= 1) {
        if (s_entity->collisionLayers & layerID) {
            rsdk_collision_layer_t *layer = &s_layers[l];
            int32_t colX = posX - layer->position.x;
            int32_t colY = posY - layer->position.y;
            int32_t cx   = (colX & -RSDK_TILE_SIZE) - RSDK_TILE_SIZE;
            if ((layer->layout || layer->win) && colY >= 0 && colY < RSDK_TILE_SIZE * layer->ysize) {
                for (int i = 0; i < 3; ++i) {
                    if (cx >= 0 && cx < RSDK_TILE_SIZE * layer->xsize) {
                        uint16_t tile = s_layer_tile(layer, cx / RSDK_TILE_SIZE, colY / RSDK_TILE_SIZE);
                        if (tile < 0xFFFF && (tile & solid)) {
                            int slot = s_remap[tile & 0x3FF], fx = (tile >> 10) & 1, fy = (tile >> 11) & 1;
                            int32_t mask = m_lwall(&s_masks[plane][slot], fx, fy, colY & 0xF);
                            int32_t tx   = cx + mask;
                            if (mask < 0xFF && colX >= tx && iabs(colX - tx) <= 14) {
                                sensor->collided   = true;
                                sensor->angle      = a_lwall(&s_info[plane][slot], fx, fy);
                                sensor->position.x = RSDK_TO_FIXED(tx + layer->position.x);
                                i                  = 3;
                            }
                        }
                    }
                    cx += RSDK_TILE_SIZE;
                }
            }
            posX = layer->position.x + colX;
            posY = layer->position.y + colY;
        }
    }
}

/* --- RoofCollision (Collision.cpp:2512-2597, REV0U) --------------- */
void rsdk_roof_collision(rsdk_collision_sensor_t *sensor)
{
    int32_t posX = RSDK_FROM_FIXED(sensor->position.x);
    int32_t posY = RSDK_FROM_FIXED(sensor->position.y);
    int plane = s_entity->collisionPlane;

    int32_t solid;
    if (s_entity->tileCollisions == TILECOLLISION_DOWN)
        solid = plane ? (1 << 15) : (1 << 13);
    else
        solid = plane ? (1 << 14) : (1 << 12);

    int32_t collideAngle = 0;
    int32_t collidePos   = -1;

    for (int l = 0, layerID = 1; l < RSDK_LAYER_COUNT; ++l, layerID <<= 1) {
        if (s_entity->collisionLayers & layerID) {
            rsdk_collision_layer_t *layer = &s_layers[l];
            int32_t colX = posX - layer->position.x;
            int32_t colY = posY - layer->position.y;
            int32_t cy   = (colY & -RSDK_TILE_SIZE) + RSDK_TILE_SIZE;
            if ((layer->layout || layer->win) && colX >= 0 && colX < RSDK_TILE_SIZE * layer->xsize) {
                int stepCount = 2;
                for (int i = 0; i < stepCount; ++i) {
                    int32_t step = -RSDK_TILE_SIZE;
                    if (cy >= 0 && cy < RSDK_TILE_SIZE * layer->ysize) {
                        uint16_t tile = s_layer_tile(layer, colX / RSDK_TILE_SIZE, cy / RSDK_TILE_SIZE);
                        if (tile < 0xFFFF && (tile & solid)) {
                            int slot = s_remap[tile & 0x3FF], fx = (tile >> 10) & 1, fy = (tile >> 11) & 1;
                            int32_t mask = m_roof(&s_masks[plane][slot], fx, fy, colX & 0xF);
                            int32_t ty   = layer->position.y + cy + mask;
                            if (mask < 0xFF) {
                                step = RSDK_TILE_SIZE;
                                if (colY > collidePos) {
                                    collideAngle = a_roof(&s_info[plane][slot], fx, fy);
                                    collidePos   = ty;
                                    i            = stepCount;
                                }
                            }
                        }
                    }
                    cy += step;
                }
            }
            posX = layer->position.x + colX;
            posY = layer->position.y + colY;
        }
    }

    if (collidePos >= 0 && sensor->position.y <= RSDK_TO_FIXED(collidePos)
        && sensor->position.y - RSDK_TO_FIXED(collidePos) >= -COLLISION_MIN_DISTANCE) {
        sensor->angle      = (uint8_t)collideAngle;
        sensor->position.y = RSDK_TO_FIXED(collidePos);
        sensor->collided   = true;
    }
}

/* --- RWallCollision (Collision.cpp:2598-2637) --------------------- */
void rsdk_rwall_collision(rsdk_collision_sensor_t *sensor)
{
    int32_t posX = RSDK_FROM_FIXED(sensor->position.x);
    int32_t posY = RSDK_FROM_FIXED(sensor->position.y);
    int plane = s_entity->collisionPlane;

    int32_t solid = plane ? (1 << 15) : (1 << 13);

    for (int l = 0, layerID = 1; l < RSDK_LAYER_COUNT; ++l, layerID <<= 1) {
        if (s_entity->collisionLayers & layerID) {
            rsdk_collision_layer_t *layer = &s_layers[l];
            int32_t colX = posX - layer->position.x;
            int32_t colY = posY - layer->position.y;
            int32_t cx   = (colX & -RSDK_TILE_SIZE) + RSDK_TILE_SIZE;
            if ((layer->layout || layer->win) && colY >= 0 && colY < RSDK_TILE_SIZE * layer->ysize) {
                for (int i = 0; i < 3; ++i) {
                    if (cx >= 0 && cx < RSDK_TILE_SIZE * layer->xsize) {
                        uint16_t tile = s_layer_tile(layer, cx / RSDK_TILE_SIZE, colY / RSDK_TILE_SIZE);
                        if (tile < 0xFFFF && (tile & solid)) {
                            int slot = s_remap[tile & 0x3FF], fx = (tile >> 10) & 1, fy = (tile >> 11) & 1;
                            int32_t mask = m_rwall(&s_masks[plane][slot], fx, fy, colY & 0xF);
                            int32_t tx   = cx + mask;
                            if (mask < 0xFF && colX <= tx && iabs(colX - tx) <= 14) {
                                sensor->collided   = true;
                                sensor->angle      = a_rwall(&s_info[plane][slot], fx, fy);
                                sensor->position.x = RSDK_TO_FIXED(tx + layer->position.x);
                                i                  = 3;
                            }
                        }
                    }
                    cx -= RSDK_TILE_SIZE;
                }
            }
            posX = layer->position.x + colX;
            posY = layer->position.y + colY;
        }
    }
}

/* --- SetPathGripSensors (Collision.cpp:2088-2160) ------------------- */
static void set_path_grip_sensors(rsdk_collision_sensor_t *sensors)
{
    int32_t offset = s_useCollisionOffset ? COLLISION_OFFSET : 0;

    switch (s_entity->collisionMode) {
        case CMODE_FLOOR:
            sensors[0].position.y = sensors[4].position.y + RSDK_TO_FIXED(s_outer.bottom);
            sensors[1].position.y = sensors[4].position.y + RSDK_TO_FIXED(s_outer.bottom);
            sensors[2].position.y = sensors[4].position.y + RSDK_TO_FIXED(s_outer.bottom);
            sensors[3].position.y = sensors[4].position.y + offset;

            sensors[0].position.x = sensors[4].position.x + RSDK_TO_FIXED(s_inner.left) - RSDK_TO_FIXED(1);
            sensors[1].position.x = sensors[4].position.x;
            sensors[2].position.x = sensors[4].position.x + RSDK_TO_FIXED(s_inner.right);
            if (s_entity->groundVel <= 0)
                sensors[3].position.x = sensors[4].position.x + RSDK_TO_FIXED(s_outer.left) - RSDK_TO_FIXED(1);
            else
                sensors[3].position.x = sensors[4].position.x + RSDK_TO_FIXED(s_outer.right);
            break;

        case CMODE_LWALL:
            sensors[0].position.x = sensors[4].position.x + RSDK_TO_FIXED(s_outer.bottom);
            sensors[1].position.x = sensors[4].position.x + RSDK_TO_FIXED(s_outer.bottom);
            sensors[2].position.x = sensors[4].position.x + RSDK_TO_FIXED(s_outer.bottom);
            sensors[3].position.x = sensors[4].position.x;

            sensors[0].position.y = sensors[4].position.y + RSDK_TO_FIXED(s_inner.left) - RSDK_TO_FIXED(1);
            sensors[1].position.y = sensors[4].position.y;
            sensors[2].position.y = sensors[4].position.y + RSDK_TO_FIXED(s_inner.right);
            if (s_entity->groundVel <= 0)
                sensors[3].position.y = sensors[4].position.y - RSDK_TO_FIXED(s_outer.left);
            else
                sensors[3].position.y = sensors[4].position.y - RSDK_TO_FIXED(s_outer.right) - RSDK_TO_FIXED(1);
            break;

        case CMODE_ROOF:
            sensors[0].position.y = sensors[4].position.y - RSDK_TO_FIXED(s_outer.bottom) - RSDK_TO_FIXED(1);
            sensors[1].position.y = sensors[4].position.y - RSDK_TO_FIXED(s_outer.bottom) - RSDK_TO_FIXED(1);
            sensors[2].position.y = sensors[4].position.y - RSDK_TO_FIXED(s_outer.bottom) - RSDK_TO_FIXED(1);
            sensors[3].position.y = sensors[4].position.y - offset;

            sensors[0].position.x = sensors[4].position.x + RSDK_TO_FIXED(s_inner.left) - RSDK_TO_FIXED(1);
            sensors[1].position.x = sensors[4].position.x;
            sensors[2].position.x = sensors[4].position.x + RSDK_TO_FIXED(s_inner.right);
            if (s_entity->groundVel <= 0)
                sensors[3].position.x = sensors[4].position.x - RSDK_TO_FIXED(s_outer.left);
            else
                sensors[3].position.x = sensors[4].position.x - RSDK_TO_FIXED(s_outer.right) - RSDK_TO_FIXED(1);
            break;

        case CMODE_RWALL:
            sensors[0].position.x = sensors[4].position.x - RSDK_TO_FIXED(s_outer.bottom) - RSDK_TO_FIXED(1);
            sensors[1].position.x = sensors[4].position.x - RSDK_TO_FIXED(s_outer.bottom) - RSDK_TO_FIXED(1);
            sensors[2].position.x = sensors[4].position.x - RSDK_TO_FIXED(s_outer.bottom) - RSDK_TO_FIXED(1);
            sensors[3].position.x = sensors[4].position.x;

            sensors[0].position.y = sensors[4].position.y + RSDK_TO_FIXED(s_inner.left) - RSDK_TO_FIXED(1);
            sensors[1].position.y = sensors[4].position.y;
            sensors[2].position.y = sensors[4].position.y + RSDK_TO_FIXED(s_inner.right);
            if (s_entity->groundVel <= 0)
                sensors[3].position.y = sensors[4].position.y + RSDK_TO_FIXED(s_outer.left) - RSDK_TO_FIXED(1);
            else
                sensors[3].position.y = sensors[4].position.y + RSDK_TO_FIXED(s_outer.right);
            break;

        default: break;
    }
}

/* --- ProcessPathGrip (Collision.cpp:1598-2086, REV0U) -------------- */
static void process_path_grip(void)
{
    int32_t xVel = 0;
    int32_t yVel = 0;

    s_sensors[4].position.x = s_entity->position.x;
    s_sensors[4].position.y = s_entity->position.y;
    for (int32_t i = 0; i < 6; ++i) {
        s_sensors[i].angle    = s_entity->angle;
        s_sensors[i].collided = false;
    }
    set_path_grip_sensors(s_sensors);

    int32_t absSpeed  = iabs(s_entity->groundVel);
    int32_t checkDist = absSpeed >> 18;
    absSpeed &= 0x3FFFF;
    while (checkDist > -1) {
        if (checkDist >= 1) {
            xVel = s_cos256[s_entity->angle] << 10;
            yVel = s_sin256[s_entity->angle] << 10;
            checkDist--;
        }
        else {
            xVel      = absSpeed * s_cos256[s_entity->angle] >> 8;
            yVel      = absSpeed * s_sin256[s_entity->angle] >> 8;
            checkDist = -1;
        }

        if (s_entity->groundVel < 0) {
            xVel = -xVel;
            yVel = -yVel;
        }

        s_sensors[0].collided = false;
        s_sensors[1].collided = false;
        s_sensors[2].collided = false;
        s_sensors[4].position.x += xVel;
        s_sensors[4].position.y += yVel;
        int32_t tileDistance = -1;

        switch (s_entity->collisionMode) {
            case CMODE_FLOOR: {
                s_sensors[3].position.x += xVel;
                s_sensors[3].position.y += yVel;

                if (s_entity->groundVel > 0) {
                    rsdk_lwall_collision(&s_sensors[3]);
                    if (s_sensors[3].collided)
                        s_sensors[2].position.x = s_sensors[3].position.x - RSDK_TO_FIXED(2);
                }

                if (s_entity->groundVel < 0) {
                    rsdk_rwall_collision(&s_sensors[3]);
                    if (s_sensors[3].collided)
                        s_sensors[0].position.x = s_sensors[3].position.x + RSDK_TO_FIXED(2);
                }

                if (s_sensors[3].collided) {
                    xVel      = 0;
                    checkDist = -1;
                }

                for (int32_t i = 0; i < 3; i++) {
                    s_sensors[i].position.x += xVel;
                    s_sensors[i].position.y += yVel;
                    rsdk_find_floor_position(&s_sensors[i]);
                }

                tileDistance = -1;
                for (int32_t i = 0; i < 3; i++) {
                    if (tileDistance > -1) {
                        if (s_sensors[i].collided) {
                            if (s_sensors[i].position.y < s_sensors[tileDistance].position.y)
                                tileDistance = i;

                            if (s_sensors[i].position.y == s_sensors[tileDistance].position.y && (s_sensors[i].angle < 0x08 || s_sensors[i].angle > 0xF8))
                                tileDistance = i;
                        }
                    }
                    else if (s_sensors[i].collided)
                        tileDistance = i;
                }

                if (tileDistance <= -1) {
                    checkDist = -1;
                }
                else {
                    s_sensors[0].position.y = s_sensors[tileDistance].position.y;
                    s_sensors[0].angle      = s_sensors[tileDistance].angle;

                    s_sensors[1].position.y = s_sensors[0].position.y;
                    s_sensors[1].angle      = s_sensors[0].angle;

                    s_sensors[2].position.y = s_sensors[0].position.y;
                    s_sensors[2].angle      = s_sensors[0].angle;

                    s_sensors[4].position.x = s_sensors[1].position.x;
                    s_sensors[4].position.y = s_sensors[0].position.y - RSDK_TO_FIXED(s_outer.bottom);
                }

                if (s_sensors[0].angle < 0xDE && s_sensors[0].angle > 0x80)
                    s_entity->collisionMode = CMODE_LWALL;
                if (s_sensors[0].angle > 0x22 && s_sensors[0].angle < 0x80)
                    s_entity->collisionMode = CMODE_RWALL;
                break;
            }

            case CMODE_LWALL: {
                s_sensors[3].position.x += xVel;
                s_sensors[3].position.y += yVel;

                if (s_entity->groundVel > 0)
                    rsdk_roof_collision(&s_sensors[3]);

                if (s_entity->groundVel < 0)
                    rsdk_floor_collision(&s_sensors[3]);

                if (s_sensors[3].collided) {
                    yVel      = 0;
                    checkDist = -1;
                }

                for (int32_t i = 0; i < 3; i++) {
                    s_sensors[i].position.x += xVel;
                    s_sensors[i].position.y += yVel;
                    rsdk_find_lwall_position(&s_sensors[i]);
                }

                tileDistance = -1;
                for (int32_t i = 0; i < 3; i++) {
                    if (tileDistance > -1) {
                        if (s_sensors[i].position.x < s_sensors[tileDistance].position.x && s_sensors[i].collided) {
                            tileDistance = i;
                        }
                    }
                    else if (s_sensors[i].collided) {
                        tileDistance = i;
                    }
                }

                if (tileDistance <= -1) {
                    checkDist = -1;
                }
                else {
                    s_sensors[0].position.x = s_sensors[tileDistance].position.x;
                    s_sensors[0].angle      = s_sensors[tileDistance].angle;

                    s_sensors[1].position.x = s_sensors[0].position.x;
                    s_sensors[1].angle      = s_sensors[0].angle;

                    s_sensors[2].position.x = s_sensors[0].position.x;
                    s_sensors[2].angle      = s_sensors[0].angle;

                    s_sensors[4].position.x = s_sensors[1].position.x - RSDK_TO_FIXED(s_outer.bottom);
                    s_sensors[4].position.y = s_sensors[1].position.y;
                }

                if (s_sensors[0].angle > 0xE2)
                    s_entity->collisionMode = CMODE_FLOOR;

                if (s_sensors[0].angle < 0x9E)
                    s_entity->collisionMode = CMODE_ROOF;
                break;
            }

            case CMODE_ROOF: {
                s_sensors[3].position.x += xVel;
                s_sensors[3].position.y += yVel;

                if (s_entity->groundVel > 0) {
                    rsdk_rwall_collision(&s_sensors[3]);
                    if (s_sensors[3].collided)
                        s_sensors[2].position.x = s_sensors[3].position.x + RSDK_TO_FIXED(2);
                }

                if (s_entity->groundVel < 0) {
                    rsdk_lwall_collision(&s_sensors[3]);
                    if (s_sensors[3].collided)
                        s_sensors[0].position.x = s_sensors[3].position.x - RSDK_TO_FIXED(2);
                }

                if (s_sensors[3].collided) {
                    xVel      = 0;
                    checkDist = -1;
                }

                for (int32_t i = 0; i < 3; i++) {
                    s_sensors[i].position.x += xVel;
                    s_sensors[i].position.y += yVel;
                    rsdk_find_roof_position(&s_sensors[i]);
                }

                tileDistance = -1;
                for (int32_t i = 0; i < 3; i++) {
                    if (tileDistance > -1) {
                        if (s_sensors[i].position.y > s_sensors[tileDistance].position.y && s_sensors[i].collided) {
                            tileDistance = i;
                        }
                    }
                    else if (s_sensors[i].collided) {
                        tileDistance = i;
                    }
                }

                if (tileDistance <= -1) {
                    checkDist = -1;
                }
                else {
                    s_sensors[0].position.y = s_sensors[tileDistance].position.y;
                    s_sensors[0].angle      = s_sensors[tileDistance].angle;

                    s_sensors[1].position.y = s_sensors[0].position.y;
                    s_sensors[1].angle      = s_sensors[0].angle;

                    s_sensors[2].position.y = s_sensors[0].position.y;
                    s_sensors[2].angle      = s_sensors[0].angle;

                    s_sensors[4].position.x = s_sensors[1].position.x;
                    s_sensors[4].position.y = s_sensors[0].position.y + RSDK_TO_FIXED(s_outer.bottom) + RSDK_TO_FIXED(1);
                }

                if (s_sensors[0].angle > 0xA2)
                    s_entity->collisionMode = CMODE_LWALL;
                if (s_sensors[0].angle < 0x5E)
                    s_entity->collisionMode = CMODE_RWALL;
                break;
            }

            case CMODE_RWALL: {
                s_sensors[3].position.x += xVel;
                s_sensors[3].position.y += yVel;

                if (s_entity->groundVel > 0)
                    rsdk_floor_collision(&s_sensors[3]);

                if (s_entity->groundVel < 0)
                    rsdk_roof_collision(&s_sensors[3]);

                if (s_sensors[3].collided) {
                    yVel      = 0;
                    checkDist = -1;
                }

                for (int32_t i = 0; i < 3; i++) {
                    s_sensors[i].position.x += xVel;
                    s_sensors[i].position.y += yVel;
                    rsdk_find_rwall_position(&s_sensors[i]);
                }

                tileDistance = -1;
                for (int32_t i = 0; i < 3; i++) {
                    if (tileDistance > -1) {
                        if (s_sensors[i].position.x > s_sensors[tileDistance].position.x && s_sensors[i].collided) {
                            tileDistance = i;
                        }
                    }
                    else if (s_sensors[i].collided) {
                        tileDistance = i;
                    }
                }

                if (tileDistance <= -1) {
                    checkDist = -1;
                }
                else {
                    s_sensors[0].position.x = s_sensors[tileDistance].position.x;
                    s_sensors[0].angle      = s_sensors[tileDistance].angle;

                    s_sensors[1].position.x = s_sensors[0].position.x;
                    s_sensors[1].angle      = s_sensors[0].angle;

                    s_sensors[2].position.x = s_sensors[0].position.x;
                    s_sensors[2].angle      = s_sensors[0].angle;

                    s_sensors[4].position.x = s_sensors[1].position.x + RSDK_TO_FIXED(s_outer.bottom) + RSDK_TO_FIXED(1);
                    s_sensors[4].position.y = s_sensors[1].position.y;
                }

                if (s_sensors[0].angle < 0x1E)
                    s_entity->collisionMode = CMODE_FLOOR;
                if (s_sensors[0].angle > 0x62)
                    s_entity->collisionMode = CMODE_ROOF;
                break;
            }
        }

        if (tileDistance != -1)
            s_entity->angle = s_sensors[0].angle;

        if (!s_sensors[3].collided)
            set_path_grip_sensors(s_sensors);
        else
            checkDist = -2;
    }

    int32_t newCollisionMode = s_entity->tileCollisions == TILECOLLISION_DOWN ? CMODE_FLOOR : CMODE_ROOF;
    int32_t newAngle         = newCollisionMode << 6;

    switch (s_entity->collisionMode) {
        case CMODE_FLOOR: {
            if (s_sensors[0].collided || s_sensors[1].collided || s_sensors[2].collided) {
                s_entity->angle = s_sensors[0].angle;

                if (!s_sensors[3].collided) {
                    s_entity->position.x = s_sensors[4].position.x;
                }
                else {
                    if (s_entity->groundVel > 0)
                        s_entity->position.x = s_sensors[3].position.x - RSDK_TO_FIXED(s_outer.right);

                    if (s_entity->groundVel < 0)
                        s_entity->position.x = s_sensors[3].position.x - RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);

                    s_entity->groundVel  = 0;
                    s_entity->velocity.x = 0;
                }

                s_entity->position.y = s_sensors[4].position.y;
            }
            else {
                s_entity->onGround      = false;
                s_entity->collisionMode = newCollisionMode;
                s_entity->velocity.x    = s_cos256[s_entity->angle] * s_entity->groundVel >> 8;
                s_entity->velocity.y    = s_sin256[s_entity->angle] * s_entity->groundVel >> 8;
                if (s_entity->velocity.y < -RSDK_TO_FIXED(16))
                    s_entity->velocity.y = -RSDK_TO_FIXED(16);

                if (s_entity->velocity.y > RSDK_TO_FIXED(16))
                    s_entity->velocity.y = RSDK_TO_FIXED(16);

                s_entity->groundVel = s_entity->velocity.x;
                s_entity->angle     = newAngle;
                if (!s_sensors[3].collided) {
                    s_entity->position.x += s_entity->velocity.x;
                }
                else {
                    if (s_entity->groundVel > 0)
                        s_entity->position.x = s_sensors[3].position.x - RSDK_TO_FIXED(s_outer.right);
                    if (s_entity->groundVel < 0)
                        s_entity->position.x = s_sensors[3].position.x - RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);

                    s_entity->groundVel  = 0;
                    s_entity->velocity.x = 0;
                }

                s_entity->position.y += s_entity->velocity.y;
            }
            break;
        }

        case CMODE_LWALL: {
            if (s_sensors[0].collided || s_sensors[1].collided || s_sensors[2].collided) {
                s_entity->angle = s_sensors[0].angle;
            }
            else {
                s_entity->onGround      = false;
                s_entity->collisionMode = newCollisionMode;
                s_entity->velocity.x    = s_cos256[s_entity->angle] * s_entity->groundVel >> 8;
                s_entity->velocity.y    = s_sin256[s_entity->angle] * s_entity->groundVel >> 8;

                if (s_entity->velocity.y < -RSDK_TO_FIXED(16))
                    s_entity->velocity.y = -RSDK_TO_FIXED(16);

                if (s_entity->velocity.y > RSDK_TO_FIXED(16))
                    s_entity->velocity.y = RSDK_TO_FIXED(16);

                s_entity->groundVel = s_entity->velocity.x;
                s_entity->angle     = newAngle;
            }

            if (!s_sensors[3].collided) {
                s_entity->position.x = s_sensors[4].position.x;
                s_entity->position.y = s_sensors[4].position.y;
            }
            else {
                if (s_entity->groundVel > 0)
                    s_entity->position.y = s_sensors[3].position.y + RSDK_TO_FIXED(s_outer.right) + RSDK_TO_FIXED(1);

                if (s_entity->groundVel < 0)
                    s_entity->position.y = s_sensors[3].position.y - RSDK_TO_FIXED(s_outer.left);

                s_entity->groundVel  = 0;
                s_entity->position.x = s_sensors[4].position.x;
            }
            break;
        }

        case CMODE_ROOF: {
            if (s_sensors[0].collided || s_sensors[1].collided || s_sensors[2].collided) {
                s_entity->angle = s_sensors[0].angle;

                if (!s_sensors[3].collided) {
                    s_entity->position.x = s_sensors[4].position.x;
                }
                else {
                    if (s_entity->groundVel > 0)
                        s_entity->position.x = s_sensors[3].position.x + RSDK_TO_FIXED(s_outer.right);

                    if (s_entity->groundVel < 0)
                        s_entity->position.x = s_sensors[3].position.x + RSDK_TO_FIXED(s_outer.left) - RSDK_TO_FIXED(1);

                    s_entity->groundVel = 0;
                }
            }
            else {
                s_entity->onGround      = false;
                s_entity->collisionMode = newCollisionMode;
                s_entity->velocity.x    = s_cos256[s_entity->angle] * s_entity->groundVel >> 8;
                s_entity->velocity.y    = s_sin256[s_entity->angle] * s_entity->groundVel >> 8;

                if (s_entity->velocity.y < -RSDK_TO_FIXED(16))
                    s_entity->velocity.y = -RSDK_TO_FIXED(16);

                if (s_entity->velocity.y > RSDK_TO_FIXED(16))
                    s_entity->velocity.y = RSDK_TO_FIXED(16);

                s_entity->angle     = newAngle;
                s_entity->groundVel = s_entity->velocity.x;

                if (!s_sensors[3].collided) {
                    s_entity->position.x += s_entity->velocity.x;
                }
                else {
                    if (s_entity->groundVel > 0)
                        s_entity->position.x = s_sensors[3].position.x - RSDK_TO_FIXED(s_outer.right);

                    if (s_entity->groundVel < 0)
                        s_entity->position.x = s_sensors[3].position.x - RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);

                    s_entity->groundVel = 0;
                }
            }
            s_entity->position.y = s_sensors[4].position.y;
            break;
        }

        case CMODE_RWALL: {
            if (s_sensors[0].collided || s_sensors[1].collided || s_sensors[2].collided) {
                s_entity->angle = s_sensors[0].angle;
            }
            else {
                s_entity->onGround      = false;
                s_entity->collisionMode = newCollisionMode;
                s_entity->velocity.x    = s_cos256[s_entity->angle] * s_entity->groundVel >> 8;
                s_entity->velocity.y    = s_sin256[s_entity->angle] * s_entity->groundVel >> 8;

                if (s_entity->velocity.y < -RSDK_TO_FIXED(16))
                    s_entity->velocity.y = -RSDK_TO_FIXED(16);

                if (s_entity->velocity.y > RSDK_TO_FIXED(16))
                    s_entity->velocity.y = RSDK_TO_FIXED(16);

                s_entity->groundVel = s_entity->velocity.x;
                s_entity->angle     = newAngle;
            }

            if (!s_sensors[3].collided) {
                s_entity->position.x = s_sensors[4].position.x;
                s_entity->position.y = s_sensors[4].position.y;
            }
            else {
                if (s_entity->groundVel > 0)
                    s_entity->position.y = s_sensors[3].position.y - RSDK_TO_FIXED(s_outer.right);

                if (s_entity->groundVel < 0)
                    s_entity->position.y = s_sensors[3].position.y - RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);

                s_entity->groundVel  = 0;
                s_entity->position.x = s_sensors[4].position.x;
            }
            break;
        }

        default: break;
    }
}

/* --- ProcessAirCollision_Down (Collision.cpp:1005-1302) ------------ */
static void process_air_collision_down(void)
{
    uint8_t movingDown  = 0;
    uint8_t movingUp    = 0;
    uint8_t movingLeft  = 0;
    uint8_t movingRight = 0;

    int32_t offset = s_useCollisionOffset ? COLLISION_OFFSET : 0;

    if (s_entity->velocity.x >= 0) {
        movingRight             = 1;
        s_sensors[0].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.right);
        s_sensors[0].position.y = s_entity->position.y + offset;
    }

    if (s_entity->velocity.x <= 0) {
        movingLeft              = 1;
        s_sensors[1].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.left) - RSDK_TO_FIXED(1);
        s_sensors[1].position.y = s_entity->position.y + offset;
    }

    s_sensors[2].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);
    s_sensors[3].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.right) - RSDK_TO_FIXED(2);
    s_sensors[4].position.x = s_sensors[2].position.x;
    s_sensors[5].position.x = s_sensors[3].position.x;

    s_sensors[0].collided = false;
    s_sensors[1].collided = false;
    s_sensors[2].collided = false;
    s_sensors[3].collided = false;
    s_sensors[4].collided = false;
    s_sensors[5].collided = false;
    if (s_entity->velocity.y >= 0) {
        movingDown              = 1;
        s_sensors[2].position.y = s_entity->position.y + RSDK_TO_FIXED(s_outer.bottom);
        s_sensors[3].position.y = s_entity->position.y + RSDK_TO_FIXED(s_outer.bottom);
    }

    if (iabs(s_entity->velocity.x) > RSDK_TO_FIXED(1) || s_entity->velocity.y < 0) {
        movingUp                = 1;
        s_sensors[4].position.y = s_entity->position.y + RSDK_TO_FIXED(s_outer.top) - RSDK_TO_FIXED(1);
        s_sensors[5].position.y = s_entity->position.y + RSDK_TO_FIXED(s_outer.top) - RSDK_TO_FIXED(1);
    }

    int32_t cnt   = (iabs(s_entity->velocity.x) <= iabs(s_entity->velocity.y) ? ((iabs(s_entity->velocity.y) >> s_collisionMaskAir) + 1)
                                                                              : (iabs(s_entity->velocity.x) >> s_collisionMaskAir) + 1);
    int32_t velX  = s_entity->velocity.x / cnt;
    int32_t velY  = s_entity->velocity.y / cnt;
    int32_t velX2 = s_entity->velocity.x - velX * (cnt - 1);
    int32_t velY2 = s_entity->velocity.y - velY * (cnt - 1);
    while (cnt > 0) {
        if (cnt < 2) {
            velX = velX2;
            velY = velY2;
        }
        cnt--;

        if (movingRight == 1) {
            s_sensors[0].position.x += velX;
            s_sensors[0].position.y += velY;
            rsdk_lwall_collision(&s_sensors[0]);

            if (s_sensors[0].collided) {
                movingRight = 2;
            }
        }

        if (movingLeft == 1) {
            s_sensors[1].position.x += velX;
            s_sensors[1].position.y += velY;
            rsdk_rwall_collision(&s_sensors[1]);

            if (s_sensors[1].collided) {
                movingLeft = 2;
            }
        }

        if (movingRight == 2) {
            s_entity->velocity.x = 0;
            s_entity->groundVel  = 0;
            s_entity->position.x = s_sensors[0].position.x - RSDK_TO_FIXED(s_outer.right);

            s_sensors[2].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);
            s_sensors[3].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.right) - RSDK_TO_FIXED(2);
            s_sensors[4].position.x = s_sensors[2].position.x;
            s_sensors[5].position.x = s_sensors[3].position.x;

            velX        = 0;
            velX2       = 0;
            movingRight = 3;
        }

        if (movingLeft == 2) {
            s_entity->velocity.x = 0;
            s_entity->groundVel  = 0;
            s_entity->position.x = s_sensors[1].position.x - RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);

            s_sensors[2].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);
            s_sensors[3].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.right) - RSDK_TO_FIXED(2);
            s_sensors[4].position.x = s_sensors[2].position.x;
            s_sensors[5].position.x = s_sensors[3].position.x;

            velX       = 0;
            velX2      = 0;
            movingLeft = 3;
        }

        if (movingDown == 1) {
            for (int32_t i = 2; i < 4; i++) {
                if (!s_sensors[i].collided) {
                    s_sensors[i].position.x += velX;
                    s_sensors[i].position.y += velY;
                    rsdk_floor_collision(&s_sensors[i]);
                }
            }

            if (s_sensors[2].collided || s_sensors[3].collided) {
                movingDown = 2;
                cnt        = 0;
            }
        }

        if (movingUp == 1) {
            for (int32_t i = 4; i < 6; i++) {
                if (!s_sensors[i].collided) {
                    s_sensors[i].position.x += velX;
                    s_sensors[i].position.y += velY;
                    rsdk_roof_collision(&s_sensors[i]);
                }
            }

            if (s_sensors[4].collided || s_sensors[5].collided) {
                movingUp = 2;
                cnt      = 0;
            }
        }
    }

    if (movingRight < 2 && movingLeft < 2)
        s_entity->position.x += s_entity->velocity.x;

    if (movingUp < 2 && movingDown < 2) {
        s_entity->position.y += s_entity->velocity.y;
        return;
    }

    if (movingDown == 2) {
        s_entity->onGround = true;

        if (s_sensors[2].collided && s_sensors[3].collided) {
            if (s_sensors[2].position.y >= s_sensors[3].position.y) {
                s_entity->position.y = s_sensors[3].position.y - RSDK_TO_FIXED(s_outer.bottom);
                s_entity->angle      = s_sensors[3].angle;
            }
            else {
                s_entity->position.y = s_sensors[2].position.y - RSDK_TO_FIXED(s_outer.bottom);
                s_entity->angle      = s_sensors[2].angle;
            }
        }
        else if (s_sensors[2].collided) {
            s_entity->position.y = s_sensors[2].position.y - RSDK_TO_FIXED(s_outer.bottom);
            s_entity->angle      = s_sensors[2].angle;
        }
        else if (s_sensors[3].collided) {
            s_entity->position.y = s_sensors[3].position.y - RSDK_TO_FIXED(s_outer.bottom);
            s_entity->angle      = s_sensors[3].angle;
        }

        if (s_entity->angle > 0xA0 && s_entity->angle < 0xDE && s_entity->collisionMode != CMODE_LWALL) {
            s_entity->collisionMode = CMODE_LWALL;
            s_entity->position.x -= RSDK_TO_FIXED(4);
        }

        if (s_entity->angle > 0x22 && s_entity->angle < 0x60 && s_entity->collisionMode != CMODE_RWALL) {
            s_entity->collisionMode = CMODE_RWALL;
            s_entity->position.x += RSDK_TO_FIXED(4);
        }

        int32_t speed = 0;
        if (s_entity->angle < 0x80) {
            if (s_entity->angle < 0x10) {
                speed = s_entity->velocity.x;
            }
            else if (s_entity->angle >= 0x20) {
                speed = (iabs(s_entity->velocity.x) <= iabs(s_entity->velocity.y) ? s_entity->velocity.y : s_entity->velocity.x);
            }
            else {
                speed = (iabs(s_entity->velocity.x) <= iabs(s_entity->velocity.y >> 1) ? (s_entity->velocity.y >> 1) : s_entity->velocity.x);
            }
        }
        else if (s_entity->angle > 0xF0) {
            speed = s_entity->velocity.x;
        }
        else if (s_entity->angle <= 0xE0) {
            speed = (iabs(s_entity->velocity.x) <= iabs(s_entity->velocity.y) ? -s_entity->velocity.y : s_entity->velocity.x);
        }
        else {
            speed = (iabs(s_entity->velocity.x) <= iabs(s_entity->velocity.y >> 1) ? -(s_entity->velocity.y >> 1) : s_entity->velocity.x);
        }

        if (speed < -RSDK_TO_FIXED(24))
            speed = -RSDK_TO_FIXED(24);

        if (speed > RSDK_TO_FIXED(24))
            speed = RSDK_TO_FIXED(24);

        s_entity->groundVel  = speed;
        s_entity->velocity.x = speed;
        s_entity->velocity.y = 0;
    }

    if (movingUp == 2) {
        int32_t sensorAngle = 0;

        if (s_sensors[4].collided && s_sensors[5].collided) {
            if (s_sensors[4].position.y <= s_sensors[5].position.y) {
                s_entity->position.y = s_sensors[5].position.y - RSDK_TO_FIXED(s_outer.top) + RSDK_TO_FIXED(1);
                sensorAngle          = s_sensors[5].angle;
            }
            else {
                s_entity->position.y = s_sensors[4].position.y - RSDK_TO_FIXED(s_outer.top) + RSDK_TO_FIXED(1);
                sensorAngle          = s_sensors[4].angle;
            }
        }
        else if (s_sensors[4].collided) {
            s_entity->position.y = s_sensors[4].position.y - RSDK_TO_FIXED(s_outer.top) + RSDK_TO_FIXED(1);
            sensorAngle          = s_sensors[4].angle;
        }
        else if (s_sensors[5].collided) {
            s_entity->position.y = s_sensors[5].position.y - RSDK_TO_FIXED(s_outer.top) + RSDK_TO_FIXED(1);
            sensorAngle          = s_sensors[5].angle;
        }
        sensorAngle &= 0xFF;

        if (sensorAngle < 0x62) {
            if (s_entity->velocity.y < -iabs(s_entity->velocity.x)) {
                s_entity->onGround      = true;
                s_entity->angle         = sensorAngle;
                s_entity->collisionMode = CMODE_RWALL;
                s_entity->position.x += RSDK_TO_FIXED(4);
                s_entity->position.y -= RSDK_TO_FIXED(2);

                s_entity->groundVel = s_entity->angle <= 0x60 ? s_entity->velocity.y : (s_entity->velocity.y >> 1);
            }
        }

        if (sensorAngle > 0x9E && sensorAngle < 0xC1) {
            if (s_entity->velocity.y < -iabs(s_entity->velocity.x)) {
                s_entity->onGround      = true;
                s_entity->angle         = sensorAngle;
                s_entity->collisionMode = CMODE_LWALL;
                s_entity->position.x -= RSDK_TO_FIXED(4);
                s_entity->position.y -= RSDK_TO_FIXED(2);

                s_entity->groundVel = s_entity->angle >= 0xA0 ? -s_entity->velocity.y : -(s_entity->velocity.y >> 1);
            }
        }

        if (s_entity->velocity.y < 0)
            s_entity->velocity.y = 0;
    }
}

/* --- ProcessAirCollision_Up (Collision.cpp:1304-1596, REV0U) ------- */
static void process_air_collision_up(void)
{
    uint8_t movingDown  = 0;
    uint8_t movingUp    = 0;
    uint8_t movingLeft  = 0;
    uint8_t movingRight = 0;

    int32_t offset = s_useCollisionOffset ? -COLLISION_OFFSET : 0;

    if (s_entity->velocity.x >= 0) {
        movingRight             = 1;
        s_sensors[0].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.right);
        s_sensors[0].position.y = s_entity->position.y + offset;
    }

    if (s_entity->velocity.x <= 0) {
        movingLeft              = 1;
        s_sensors[1].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.left) - RSDK_TO_FIXED(1);
        s_sensors[1].position.y = s_entity->position.y + offset;
    }

    s_sensors[2].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);
    s_sensors[3].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.right) - RSDK_TO_FIXED(2);
    s_sensors[4].position.x = s_sensors[2].position.x;
    s_sensors[5].position.x = s_sensors[3].position.x;

    s_sensors[0].collided = false;
    s_sensors[1].collided = false;
    s_sensors[2].collided = false;
    s_sensors[3].collided = false;
    s_sensors[4].collided = false;
    s_sensors[5].collided = false;
    if (s_entity->velocity.y <= 0) {
        movingDown              = 1;
        s_sensors[4].position.y = s_entity->position.y + RSDK_TO_FIXED(s_outer.top) - RSDK_TO_FIXED(1);
        s_sensors[5].position.y = s_entity->position.y + RSDK_TO_FIXED(s_outer.top) - RSDK_TO_FIXED(1);
    }

    if (iabs(s_entity->velocity.x) > RSDK_TO_FIXED(1) || s_entity->velocity.y > 0) {
        movingUp                = 1;
        s_sensors[2].position.y = s_entity->position.y + RSDK_TO_FIXED(s_outer.bottom);
        s_sensors[3].position.y = s_entity->position.y + RSDK_TO_FIXED(s_outer.bottom);
    }

    int32_t cnt   = (iabs(s_entity->velocity.x) <= iabs(s_entity->velocity.y) ? ((iabs(s_entity->velocity.y) >> s_collisionMaskAir) + 1)
                                                                              : (iabs(s_entity->velocity.x) >> s_collisionMaskAir) + 1);
    int32_t velX  = s_entity->velocity.x / cnt;
    int32_t velY  = s_entity->velocity.y / cnt;
    int32_t velX2 = s_entity->velocity.x - velX * (cnt - 1);
    int32_t velY2 = s_entity->velocity.y - velY * (cnt - 1);
    while (cnt > 0) {
        if (cnt < 2) {
            velX = velX2;
            velY = velY2;
        }
        cnt--;

        if (movingRight == 1) {
            s_sensors[0].position.x += velX;
            s_sensors[0].position.y += velY;
            rsdk_lwall_collision(&s_sensors[0]);

            if (s_sensors[0].collided) {
                movingRight = 2;
            }
        }

        if (movingLeft == 1) {
            s_sensors[1].position.x += velX;
            s_sensors[1].position.y += velY;
            rsdk_rwall_collision(&s_sensors[1]);

            if (s_sensors[1].collided) {
                movingLeft = 2;
            }
        }

        if (movingRight == 2) {
            s_entity->velocity.x = 0;
            s_entity->groundVel  = 0;
            s_entity->position.x = s_sensors[0].position.x - RSDK_TO_FIXED(s_outer.right);

            s_sensors[2].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);
            s_sensors[3].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.right) - RSDK_TO_FIXED(2);
            s_sensors[4].position.x = s_sensors[2].position.x;
            s_sensors[5].position.x = s_sensors[3].position.x;

            velX        = 0;
            velX2       = 0;
            movingRight = 3;
        }

        if (movingLeft == 2) {
            s_entity->velocity.x = 0;
            s_entity->groundVel  = 0;
            s_entity->position.x = s_sensors[1].position.x - RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);

            s_sensors[2].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.left) + RSDK_TO_FIXED(1);
            s_sensors[3].position.x = s_entity->position.x + RSDK_TO_FIXED(s_outer.right) - RSDK_TO_FIXED(2);
            s_sensors[4].position.x = s_sensors[2].position.x;
            s_sensors[5].position.x = s_sensors[3].position.x;

            velX       = 0;
            velX2      = 0;
            movingLeft = 3;
        }

        if (movingUp == 1) {
            for (int32_t i = 2; i < 4; i++) {
                if (!s_sensors[i].collided) {
                    s_sensors[i].position.x += velX;
                    s_sensors[i].position.y += velY;
                    rsdk_floor_collision(&s_sensors[i]);
                }
            }

            if (s_sensors[2].collided || s_sensors[3].collided) {
                movingUp = 2;
                cnt      = 0;
            }
        }

        if (movingDown == 1) {
            for (int32_t i = 4; i < 6; i++) {
                if (!s_sensors[i].collided) {
                    s_sensors[i].position.x += velX;
                    s_sensors[i].position.y += velY;
                    rsdk_roof_collision(&s_sensors[i]);
                }
            }

            if (s_sensors[4].collided || s_sensors[5].collided) {
                movingDown = 2;
                cnt        = 0;
            }
        }
    }

    if (movingRight < 2 && movingLeft < 2)
        s_entity->position.x += s_entity->velocity.x;

    if (movingUp < 2 && movingDown < 2) {
        s_entity->position.y += s_entity->velocity.y;
        return;
    }

    if (movingDown == 2) {
        s_entity->onGround = true;

        if (s_sensors[4].collided && s_sensors[5].collided) {
            if (s_sensors[4].position.y <= s_sensors[5].position.y) {
                s_entity->position.y = s_sensors[5].position.y - RSDK_TO_FIXED(s_outer.top) + RSDK_TO_FIXED(1);
                s_entity->angle      = s_sensors[5].angle;
            }
            else {
                s_entity->position.y = s_sensors[4].position.y - RSDK_TO_FIXED(s_outer.top) + RSDK_TO_FIXED(1);
                s_entity->angle      = s_sensors[4].angle;
            }
        }
        else if (s_sensors[4].collided) {
            s_entity->position.y = s_sensors[4].position.y - RSDK_TO_FIXED(s_outer.top) + RSDK_TO_FIXED(1);
            s_entity->angle      = s_sensors[4].angle;
        }
        else if (s_sensors[5].collided) {
            s_entity->position.y = s_sensors[5].position.y - RSDK_TO_FIXED(s_outer.top) + RSDK_TO_FIXED(1);
            s_entity->angle      = s_sensors[5].angle;
        }

        if (s_entity->angle > 0xA2 && s_entity->angle < 0xE0 && s_entity->collisionMode != CMODE_LWALL) {
            s_entity->collisionMode = CMODE_LWALL;
            s_entity->position.x -= RSDK_TO_FIXED(4);
        }

        if (s_entity->angle > 0x20 && s_entity->angle < 0x5E && s_entity->collisionMode != CMODE_RWALL) {
            s_entity->collisionMode = CMODE_RWALL;
            s_entity->position.x += RSDK_TO_FIXED(4);
        }

        int32_t speed = 0;
        if (s_entity->angle >= 0x80) {
            if (s_entity->angle < 0x90) {
                speed = -s_entity->velocity.x;
            }
            else if (s_entity->angle >= 0xA0) {
                speed = (iabs(s_entity->velocity.x) <= iabs(s_entity->velocity.y) ? s_entity->velocity.y : s_entity->velocity.x);
            }
            else {
                speed = (iabs(s_entity->velocity.x) <= iabs(s_entity->velocity.y >> 1) ? (s_entity->velocity.y >> 1) : s_entity->velocity.x);
            }
        }
        else if (s_entity->angle <= 0x70) {
            speed = s_entity->velocity.x;
        }
        else if (s_entity->angle <= 0x60) {
            speed = (iabs(s_entity->velocity.x) <= iabs(s_entity->velocity.y) ? -s_entity->velocity.y : s_entity->velocity.x);
        }
        else {
            speed = (iabs(s_entity->velocity.x) <= iabs(s_entity->velocity.y >> 1) ? -(s_entity->velocity.y >> 1) : s_entity->velocity.x);
        }

        if (speed < -RSDK_TO_FIXED(24))
            speed = -RSDK_TO_FIXED(24);

        if (speed > RSDK_TO_FIXED(24))
            speed = RSDK_TO_FIXED(24);

        s_entity->groundVel  = speed;
        s_entity->velocity.x = speed;
        s_entity->velocity.y = 0;
    }

    if (movingUp == 2) {
        int32_t sensorAngle = 0;

        if (s_sensors[2].collided && s_sensors[3].collided) {
            if (s_sensors[2].position.y >= s_sensors[3].position.y) {
                s_entity->position.y = s_sensors[3].position.y - RSDK_TO_FIXED(s_outer.bottom);
                sensorAngle          = s_sensors[3].angle;
            }
            else {
                s_entity->position.y = s_sensors[2].position.y - RSDK_TO_FIXED(s_outer.bottom);
                sensorAngle          = s_sensors[2].angle;
            }
        }
        else if (s_sensors[2].collided) {
            s_entity->position.y = s_sensors[2].position.y - RSDK_TO_FIXED(s_outer.bottom);
            sensorAngle          = s_sensors[2].angle;
        }
        else if (s_sensors[3].collided) {
            s_entity->position.y = s_sensors[3].position.y - RSDK_TO_FIXED(s_outer.bottom);
            sensorAngle          = s_sensors[3].angle;
        }
        sensorAngle &= 0xFF;

        if (sensorAngle >= 0x21 && sensorAngle <= 0x40) {
            if (s_entity->velocity.y > -iabs(s_entity->velocity.x)) {
                s_entity->onGround      = true;
                s_entity->angle         = sensorAngle;
                s_entity->collisionMode = CMODE_RWALL;
                s_entity->position.x += RSDK_TO_FIXED(4);
                s_entity->position.y -= RSDK_TO_FIXED(2);

                s_entity->groundVel = s_entity->angle <= 0x20 ? s_entity->velocity.y : (s_entity->velocity.y >> 1);
            }
        }

        if (sensorAngle >= 0xC0 && sensorAngle <= 0xE2) {
            if (s_entity->velocity.y > -iabs(s_entity->velocity.x)) {
                s_entity->onGround      = true;
                s_entity->angle         = sensorAngle;
                s_entity->collisionMode = CMODE_LWALL;
                s_entity->position.x -= RSDK_TO_FIXED(4);
                s_entity->position.y -= RSDK_TO_FIXED(2);

                s_entity->groundVel = s_entity->angle <= 0xE0 ? -s_entity->velocity.y : -(s_entity->velocity.y >> 1);
            }
        }

        if (s_entity->velocity.y > 0)
            s_entity->velocity.y = 0;
    }
}

/* --- ProcessObjectMovement (Collision.cpp:924-1003, REV0U) --------- */
void rsdk_process_object_movement(rsdk_collision_entity_t *entity,
                                  const rsdk_hitbox_t *outerBox,
                                  const rsdk_hitbox_t *innerBox)
{
    if (!entity || !outerBox || !innerBox)
        return;

    if (entity->tileCollisions) {
        entity->angle &= 0xFF;

        s_collisionTolerance = HIGH_COLLISION_TOL;
        if (iabs(entity->groundVel) < RSDK_TO_FIXED(6) && entity->angle == 0)
            s_collisionTolerance = LOW_COLLISION_TOL;

        s_outer.left   = outerBox->left;
        s_outer.top    = outerBox->top;
        s_outer.right  = outerBox->right;
        s_outer.bottom = outerBox->bottom;

        s_inner.left   = innerBox->left;
        s_inner.top    = innerBox->top;
        s_inner.right  = innerBox->right;
        s_inner.bottom = innerBox->bottom;

        s_entity = entity;

        s_collisionMaskAir = s_outer.bottom >= 14 ? 19 : 17;

        if (entity->onGround) {
            if (entity->tileCollisions == TILECOLLISION_DOWN)
                s_useCollisionOffset = entity->angle == 0x00;
            else
                s_useCollisionOffset = entity->angle == 0x80;

            if (s_outer.bottom < 14)
                s_useCollisionOffset = false;

            process_path_grip();
        }
        else {
            s_useCollisionOffset = false;
            if (entity->tileCollisions == TILECOLLISION_DOWN)
                process_air_collision_down();
            else
                process_air_collision_up();
        }

        if (entity->onGround) {
            entity->velocity.x = entity->groundVel * s_cos256[entity->angle & 0xFF] >> 8;
            entity->velocity.y = entity->groundVel * s_sin256[entity->angle & 0xFF] >> 8;
        }
        else {
            entity->groundVel = entity->velocity.x;
        }
    }
    else {
        entity->position.x += entity->velocity.x;
        entity->position.y += entity->velocity.y;
    }
}
