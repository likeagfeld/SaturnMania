/* Phase A5 — Collision (sensor-based), Saturn port of RSDKv5/RSDK/Scene/
 * Collision.cpp. See src/rsdk/collision.h header for design rationale.
 *
 * Authoritative source paths in upstream:
 *   FindFloorPosition  Collision.cpp ≈L1822-1875
 *   FindRoofPosition   Collision.cpp ≈L1922-1973
 *   FindLWallPosition  Collision.cpp ≈L1877-1920
 *   FindRWallPosition  Collision.cpp ≈L1975-2018
 *   ProcessPathGrip    Collision.cpp ≈L1454-1818
 *   SetPathGripSensors Collision.cpp ≈L1820-1890
 *
 * Test plan (Phase A5 gate criterion):
 *   1. Loading GHZ Scene1.bin TileConfig + Scene1.bin layout, the
 *      6-sensor probe at the GHZ mound x-position reports a LEFT wall
 *      collision (sensor[0].collided = true) — NOT a stuck-floor as
 *      the old surf_y model would.
 *   2. ProcessPathGrip on a flat ground patch sets on_ground = true,
 *      angle = 0, ground_vel preserved.
 *   3. ProcessPathGrip on a steep 45° slope sets on_ground = true,
 *      angle = 0x20 (≈45°), and re-evaluates collision_mode to
 *      CMODE_FLOOR (no transition for moderate slopes).
 *   4. ProcessPathGrip when both floor sensors miss + entity was
 *      on_ground last frame clears on_ground (entity falls off cliff). */

#include "collision.h"
#include <string.h>

/* --- Helpers: layout-entry decode ------------------------------------- */

#define LAYOUT_TILE(e)   ((uint16_t)((e) & 0x3FFu))
#define LAYOUT_FLIP_H(e) (((e) & 0x0400u) != 0)
#define LAYOUT_FLIP_V(e) (((e) & 0x0800u) != 0)
#define LAYOUT_BLANK     0xFFFFu

static const rsdk_tile_t *_tile_at(const rsdk_tile_layer_t *L,
                                   int tx, int ty)
{
    if (tx < 0 || ty < 0) return NULL;
    if ((uint32_t)tx >= L->xs || (uint32_t)ty >= L->ys) return NULL;
    if (!L->layout || !L->tile_cfg) return NULL;
    uint16_t e = L->layout[ty * L->xs + tx];
    if (e == LAYOUT_BLANK) return NULL;
    uint16_t id = LAYOUT_TILE(e);
    if (id >= RSDK_TILE_COUNT) return NULL;
    uint8_t path = L->collision_path & 1;
    return &L->tile_cfg->paths[path][id];
}

int rsdk_tile_floor_at(const rsdk_tile_layer_t *layer,
                       int tile_x, int tile_y, int col_in_tile)
{
    if (col_in_tile < 0 || col_in_tile > 15) return -1;
    const rsdk_tile_t *T = _tile_at(layer, tile_x, tile_y);
    if (!T) return -1;
    uint16_t e = layer->layout[tile_y * layer->xs + tile_x];
    int c = LAYOUT_FLIP_H(e) ? (15 - col_in_tile) : col_in_tile;
    if (!T->mask_active[c]) return -1;
    uint8_t h = T->mask_heights[c];
    if (h == 0xFF) return -1;
    /* yFlip: per §5.6 + storage.h, yFlip=1 marks a ceiling tile whose
     * solidity profile is mirrored vertically. For a floor query on a
     * ceiling tile, we return non-solid -- the player can't stand on
     * the underside of a ceiling. (Roof queries use the same data
     * with inverted scan direction.) */
    if (T->y_flip) return -1;
    return (int)h;
}

uint8_t rsdk_tile_floor_angle(const rsdk_tile_layer_t *layer,
                              int tile_x, int tile_y)
{
    const rsdk_tile_t *T = _tile_at(layer, tile_x, tile_y);
    return T ? T->floor_angle : 0xFF;
}

/* --- FindFloorPosition (§5.5) ----------------------------------------- *
 * Starting at (sx, sy), scan downward at most MAX_FLOOR_SCAN_TILES tiles.
 * Returns via sensor->position.y the absolute pixel Y of the floor
 * surface and via sensor->angle the floor angle, setting collided. */

#define MAX_FLOOR_SCAN_TILES 3

void rsdk_find_floor(const rsdk_tile_layer_t *layer, rsdk_sensor_t *sensor)
{
    int sx = (int)(sensor->x >> 16);
    int sy = (int)(sensor->y >> 16);
    sensor->collided = false;

    int tx = sx >> 4;
    int col = sx & 0xF;
    int start_ty = sy >> 4;

    for (int step = 0; step <= MAX_FLOOR_SCAN_TILES; ++step) {
        int ty = start_ty + step;
        int h = rsdk_tile_floor_at(layer, tx, ty, col);
        if (h >= 0) {
            /* Tile top = ty*16; floor pixel = top + (15 - h) per RSDK
             * convention (mask_heights stores height of solid ABOVE
             * the bottom of the tile, so floor surface y = top + 15 - h). */
            int floor_y = ty * 16 + (15 - h);
            /* Reject hits that are ABOVE the starting Y -- only count
             * floors at or below the sensor's starting y. */
            if (floor_y >= sy - 1) {
                sensor->y = (int32_t)floor_y << 16;
                sensor->angle = rsdk_tile_floor_angle(layer, tx, ty);
                sensor->collided = true;
                return;
            }
        }
    }
}

/* --- FindRoofPosition ------------------------------------------------- *
 * Scan upward up to MAX_FLOOR_SCAN_TILES tiles. Same masks but accept
 * roof surfaces (yFlip == 1 tiles count as ceilings here). */

void rsdk_find_roof(const rsdk_tile_layer_t *layer, rsdk_sensor_t *sensor)
{
    int sx = (int)(sensor->x >> 16);
    int sy = (int)(sensor->y >> 16);
    sensor->collided = false;

    int tx = sx >> 4;
    int col = sx & 0xF;
    int start_ty = sy >> 4;

    for (int step = 0; step <= MAX_FLOOR_SCAN_TILES; ++step) {
        int ty = start_ty - step;
        const rsdk_tile_t *T = _tile_at(layer, tx, ty);
        if (!T) continue;
        if (!T->y_flip) continue;  /* only ceiling tiles in roof scan    */
        uint16_t e = layer->layout[ty * layer->xs + tx];
        int c = LAYOUT_FLIP_H(e) ? (15 - col) : col;
        if (!T->mask_active[c]) continue;
        uint8_t h = T->mask_heights[c];
        if (h == 0xFF) continue;
        int roof_y = ty * 16 + h;
        if (roof_y <= sy + 1) {
            sensor->y = (int32_t)roof_y << 16;
            sensor->angle = T->roof_angle;
            sensor->collided = true;
            return;
        }
    }
}

/* --- FindLWallPosition ------------------------------------------------ *
 * Scan rightward (entity expects to hit a wall to its LEFT) one column at
 * a time. Uses the per-column wall masks. Up to MAX_WALL_SCAN_TILES tiles
 * horizontally. */

#define MAX_WALL_SCAN_TILES 3

void rsdk_find_lwall(const rsdk_tile_layer_t *layer, rsdk_sensor_t *sensor)
{
    int sx = (int)(sensor->x >> 16);
    int sy = (int)(sensor->y >> 16);
    sensor->collided = false;

    int ty = sy >> 4;
    int row_in_tile = sy & 0xF;
    int start_tx = sx >> 4;

    for (int step = 0; step <= MAX_WALL_SCAN_TILES; ++step) {
        int tx = start_tx + step;
        const rsdk_tile_t *T = _tile_at(layer, tx, ty);
        if (!T) continue;
        if (!T->mask_active[row_in_tile]) continue;
        /* For wall mask we use mask_heights[row] as the column where the
         * wall surface sits within the tile (0..15). The "L-wall" surface
         * is at tile_x*16 + mask_heights[row]. */
        uint8_t h = T->mask_heights[row_in_tile];
        if (h == 0xFF) continue;
        int wall_x = tx * 16 + h;
        if (wall_x >= sx - 1) {
            sensor->x = (int32_t)wall_x << 16;
            sensor->angle = T->left_wall_angle;
            sensor->collided = true;
            return;
        }
    }
}

void rsdk_find_rwall(const rsdk_tile_layer_t *layer, rsdk_sensor_t *sensor)
{
    int sx = (int)(sensor->x >> 16);
    int sy = (int)(sensor->y >> 16);
    sensor->collided = false;

    int ty = sy >> 4;
    int row_in_tile = sy & 0xF;
    int start_tx = sx >> 4;

    for (int step = 0; step <= MAX_WALL_SCAN_TILES; ++step) {
        int tx = start_tx - step;
        const rsdk_tile_t *T = _tile_at(layer, tx, ty);
        if (!T) continue;
        if (!T->mask_active[row_in_tile]) continue;
        uint8_t h = T->mask_heights[row_in_tile];
        if (h == 0xFF) continue;
        int wall_x = tx * 16 + (15 - h);
        if (wall_x <= sx + 1) {
            sensor->x = (int32_t)wall_x << 16;
            sensor->angle = T->right_wall_angle;
            sensor->collided = true;
            return;
        }
    }
}

/* --- ProcessPathGrip (§5.5) ------------------------------------------- *
 * Once per frame for a grounded entity: probe all 6 sensors around the
 * entity's hitbox, pick the higher floor for the new ground Y + angle,
 * detect cliff edges (both floor sensors miss), wall blocks (either
 * wall sensor hit), and ceiling hits. */

void rsdk_process_path_grip(const rsdk_tile_layer_t *layer,
                            const rsdk_hitbox_offsets_t *box,
                            rsdk_grip_state_t *state)
{
    if (!layer || !box || !state) return;

    /* Spawn the 6 sensors at the box-relative offsets. The offsets are
     * specified in §5.2 sign convention: outerLeft/outerRight = horizontal
     * probe positions (±X from entity); outerTop/outerBottom = vertical
     * probe positions (±Y from entity). */
    int32_t px = box->pos_x;
    int32_t py = box->pos_y;

    /* Sensors [0]/[1]: side-wall probes at entity Y (sy = entity center).*/
    state->sensors[0].x = (int32_t)(px + box->outer_left)  << 16;
    state->sensors[0].y = (int32_t)(py)                    << 16;
    state->sensors[0].angle = 0; state->sensors[0].collided = false;
    state->sensors[1].x = (int32_t)(px + box->outer_right) << 16;
    state->sensors[1].y = (int32_t)(py)                    << 16;
    state->sensors[1].angle = 0; state->sensors[1].collided = false;

    /* Sensors [2]/[3]: floor probes at inner-L / inner-R, scanning DOWN
     * from outer-bottom (typically py + sprite_h/2 + 1). */
    state->sensors[2].x = (int32_t)(px + box->inner_left)  << 16;
    state->sensors[2].y = (int32_t)(py + box->outer_bottom)<< 16;
    state->sensors[2].angle = 0; state->sensors[2].collided = false;
    state->sensors[3].x = (int32_t)(px + box->inner_right) << 16;
    state->sensors[3].y = (int32_t)(py + box->outer_bottom)<< 16;
    state->sensors[3].angle = 0; state->sensors[3].collided = false;

    /* Sensors [4]/[5]: ceiling probes at inner-L / inner-R, scanning UP
     * from outer-top. */
    state->sensors[4].x = (int32_t)(px + box->inner_left)  << 16;
    state->sensors[4].y = (int32_t)(py + box->outer_top)   << 16;
    state->sensors[4].angle = 0; state->sensors[4].collided = false;
    state->sensors[5].x = (int32_t)(px + box->inner_right) << 16;
    state->sensors[5].y = (int32_t)(py + box->outer_top)   << 16;
    state->sensors[5].angle = 0; state->sensors[5].collided = false;

    /* Probe each sensor. */
    rsdk_find_lwall(layer, &state->sensors[0]);
    rsdk_find_rwall(layer, &state->sensors[1]);
    rsdk_find_floor(layer, &state->sensors[2]);
    rsdk_find_floor(layer, &state->sensors[3]);
    rsdk_find_roof (layer, &state->sensors[4]);
    rsdk_find_roof (layer, &state->sensors[5]);

    /* Floor result: pick the HIGHER (lower y) of the two floor hits per
     * §5.5 ProcessPathGrip. */
    bool both_floors_hit = state->sensors[2].collided && state->sensors[3].collided;
    bool any_floor_hit   = state->sensors[2].collided || state->sensors[3].collided;
    if (any_floor_hit) {
        int32_t y2 = state->sensors[2].collided ? state->sensors[2].y : 0x7FFFFFFF;
        int32_t y3 = state->sensors[3].collided ? state->sensors[3].y : 0x7FFFFFFF;
        int32_t pick_y; uint8_t pick_angle;
        if (y2 <= y3) { pick_y = y2; pick_angle = state->sensors[2].angle; }
        else          { pick_y = y3; pick_angle = state->sensors[3].angle; }
        state->on_ground = true;
        state->angle     = pick_angle;
        /* Caller integrates pick_y into entity->pos_y; we don't mutate it
         * here so the caller can apply gravity / projection rules. */
        (void)pick_y;
    } else if (state->on_ground) {
        /* Edge/cliff: both floor sensors missed, was previously grounded
         * → fall off the ledge. */
        state->on_ground = false;
    }

    /* Edge-balance signal (Mania-style "balancing on a ledge" animation
     * trigger per §5.5): exactly ONE of the two floor sensors empty
     * while the other still hits. */
    state->edge_balance_left  = !state->sensors[2].collided &&  state->sensors[3].collided;
    state->edge_balance_right =  state->sensors[2].collided && !state->sensors[3].collided;
    (void)both_floors_hit;

    /* collision_mode re-evaluation per §5.5 — angles in [0..0x20) or
     * [0xE0..0x100) = floor; [0x20..0x60) = R-wall; [0x60..0xA0) = roof;
     * [0xA0..0xE0) = L-wall. */
    if (state->on_ground) {
        uint8_t a = state->angle;
        if      (a < 0x20 || a >= 0xE0) state->collision_mode = CMODE_FLOOR;
        else if (a < 0x60)              state->collision_mode = CMODE_RWALL;
        else if (a < 0xA0)              state->collision_mode = CMODE_ROOF;
        else                            state->collision_mode = CMODE_LWALL;
    }
}
