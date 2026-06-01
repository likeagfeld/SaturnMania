#ifndef RSDK_COLLISION_H
#define RSDK_COLLISION_H

/* Phase A5 — Collision (sensor-based, per RSDKv5).
 *
 * Per docs/rsdkv5_engine_catalog.md §5 + BIBLE.md Phase A row A5.
 *
 * Authoritative source (cite by §section when touching this code):
 *   §5.2 Sensor placement — 6 sensors per ProcessObjectMovement:
 *        [0] left wall, [1] right wall, [2,3] floor inner L/R,
 *        [4,5] ceiling inner L/R.
 *   §5.3 Sides enum (C_TOP/LEFT/RIGHT/BOTTOM).
 *   §5.4 Sensor struct (Vector2 position; bool collided; uint8 angle).
 *   §5.5 Algorithms (FindFloorPosition / FindRoofPosition /
 *        FindLWallPosition / FindRWallPosition / ProcessPathGrip).
 *   §5.6 Tile properties: per-tile floor[16], roof[16], lWall[16],
 *        rWall[16] height arrays + four angles + flag byte.
 *
 * Saturn-port deviations from upstream:
 *   * We do NOT pre-build flipX/flipY/flipXY variants of every tile at
 *     load (4× memory). Instead, the lookup helpers honour flip bits on
 *     the fly. With 1024 tiles × 4 paths × 16 columns × 1 byte = 64 KB
 *     per mask × 4 masks = 256 KB if pre-built — that's prohibitive for
 *     Saturn (1 MB Work RAM-H budget). Runtime fix-up is ~5% slower
 *     per query but fits the memory envelope.
 *   * Path index (0 = primary, 1 = alt route) is selected per-entity via
 *     `entity->collisionPlane`; we expose it as a parameter.
 *   * `entity->collisionLayers` bitmask of which layer indices to probe
 *     is supported, but most callers use a single FG layer.
 *
 * This module exposes the SENSOR API only. Caller code (player.c, badnik
 * controllers, monitor ground-snap) wires sensors into per-object update
 * loops. The TileConfig is loaded via rsdk_tile_config_load in
 * src/rsdk/storage.c. */

#include <stdint.h>
#include <stdbool.h>
#include "storage.h"

/* --- §5.3 sides + §5.4 sensor struct ---------------------------------- */

enum {
    C_NONE   = 0,
    C_TOP    = 1,
    C_LEFT   = 2,
    C_RIGHT  = 3,
    C_BOTTOM = 4
};

enum {
    TILECOLLISION_NONE = 0,
    TILECOLLISION_DOWN = 1,
    TILECOLLISION_UP   = 2
};

enum {
    CMODE_FLOOR = 0,
    CMODE_LWALL = 1,
    CMODE_ROOF  = 2,
    CMODE_RWALL = 3
};

typedef struct {
    int32_t x, y;             /* 16.16 fixed pixel coords (sensor uses    */
                              /* integer pixels but caller often supplies */
                              /* fixed input; we read int from x>>16)     */
    bool    collided;
    uint8_t angle;            /* Q0.8 floor angle (256 = full circle)     */
} rsdk_sensor_t;

/* --- TileLayer view (Saturn-side; the engine's layer + layout)  ------- */

typedef struct {
    const uint16_t           *layout;          /* xs*ys u16 entries       */
    uint32_t                  xs, ys;          /* in tiles                */
    const rsdk_tile_config_t *tile_cfg;        /* shared tile properties  */
    uint8_t                   collision_path;  /* 0 = primary, 1 = alt    */
} rsdk_tile_layer_t;

/* --- Sensor probe API ------------------------------------------------- */

/* Scan downward from `sensor.{x,y}` looking for a floor surface in this
 * column. On hit, writes `sensor.position.y` (the surface pixel Y) and
 * `sensor.angle` (the floor angle of the tile), and sets `sensor.collided
 * = true`. Maximum scan distance is 3 tiles (48 px) per §5.5.
 *
 * If `sensor.collided` is already false on entry, the function leaves it
 * false on miss. Pass-through is safe to chain. */
void rsdk_find_floor(const rsdk_tile_layer_t *layer, rsdk_sensor_t *sensor);

/* Scan upward from `sensor.{x,y}` looking for a ceiling surface. */
void rsdk_find_roof(const rsdk_tile_layer_t *layer, rsdk_sensor_t *sensor);

/* Scan rightward from `sensor.{x,y}` looking for a left-wall surface. */
void rsdk_find_lwall(const rsdk_tile_layer_t *layer, rsdk_sensor_t *sensor);

/* Scan leftward from `sensor.{x,y}` looking for a right-wall surface. */
void rsdk_find_rwall(const rsdk_tile_layer_t *layer, rsdk_sensor_t *sensor);

/* Lower-level: query the tile-config mask at one specific column of the
 * tile at (tile_x, tile_y) in the given layer. Returns the floor height
 * (0..15) for the floor mask, or -1 if non-solid. Handles flip bits in
 * the layout entry automatically. */
int rsdk_tile_floor_at(const rsdk_tile_layer_t *layer,
                       int tile_x, int tile_y, int col_in_tile);

/* Return the floor angle of the tile at (tile_x, tile_y), 0xFF if no
 * solid tile. */
uint8_t rsdk_tile_floor_angle(const rsdk_tile_layer_t *layer,
                              int tile_x, int tile_y);

/* --- 6-sensor box probe (drop-in replacement for the current 4-sensor
 *     custom logic in src/player.c) ----------------------------------- */

typedef struct {
    int32_t pos_x, pos_y;            /* entity position (integer pixels) */
    int32_t inner_left, inner_right; /* inner sensor X-offsets (negative L)*/
    int32_t outer_top, outer_bottom; /* outer sensor Y-offsets (negative U)*/
    int32_t outer_left, outer_right; /* outer sensor X-offsets (wall probe)*/
} rsdk_hitbox_offsets_t;

typedef struct {
    rsdk_sensor_t sensors[6];        /* [0]lw [1]rw [2]flL [3]flR [4]ceL [5]ceR */
    bool          on_ground;
    bool          edge_balance_left;
    bool          edge_balance_right;
    uint8_t       collision_mode;    /* CMODE_FLOOR / LWALL / ROOF / RWALL */
    int16_t       ground_vel;        /* sonic-style "speed along the path" */
    uint8_t       angle;             /* current ground angle (Q0.8)        */
} rsdk_grip_state_t;

/* Spawn the 6 sensors around `pos`+`box` and probe the layer. Mirrors
 * the engine's ProcessObjectMovement when entity->tileCollisions ==
 * TILECOLLISION_DOWN. Writes results into `state`. Saturn-side player
 * code calls this once per frame instead of the per-column surf_y
 * shortcut. */
void rsdk_process_path_grip(const rsdk_tile_layer_t *layer,
                            const rsdk_hitbox_offsets_t *box,
                            rsdk_grip_state_t *state);

#endif /* RSDK_COLLISION_H */
