#ifndef RSDK_COLLISION_H
#define RSDK_COLLISION_H

/* Task #180 step 2 - decomp-faithful tile 6-sensor collision.
 *
 * Mechanical port of rsdkv5-src/RSDKv5/RSDK/Scene/Collision.cpp (REV0U) plus
 * the FLIP_X/FLIP_Y/FLIP_XY tile-variant builder in Scene.cpp:866-955. The
 * Python reference oracle tools/qa_ghz_6sensor_gate.py validates the exact
 * same primitives; tools/qa_ghz_collision_cparity_gate.py cross-checks THIS C
 * code against that oracle column-by-column over the real shipped GHZ data.
 *
 * Saturn deviation (the whole reason this header exists): the decomp pre-bakes
 * 4 flip variants per tile into collisionMasks[plane][TILE_COUNT*4] /
 * tileInfo[plane][TILE_COUNT*4] and indexes them with `tile & 0xFFF` (low 10
 * bits = tileID, bits 10/11 = FLIP_X/FLIP_Y -> variant*1024 + tileID). That is
 * 512 KB of masks - far past the LWRAM carve. We ship ONLY the base 1024
 * masks/infos per plane (cd/GHZ1MASK.BIN) and apply the flip transform at
 * lookup time (flip_*_mask / flip_info below). The transform is the exact
 * inverse of Scene.cpp:869-949.
 *
 * This module is intentionally free of any Saturn (jo/SGL) dependency: it is
 * plain stdint C so the host parity harness compiles it verbatim. The runtime
 * binds the LWRAM-resident mask/info tables and the streamed layout window via
 * rsdk_collision_bind_compact / rsdk_collision_set_layer; src/rsdk/storage.c owns
 * those buffers.
 */

#include <stdint.h>
#include <stdbool.h>

/* CollisionSides (Collision.hpp:13-22). Shared sentinel so a TU that pulls in
 * both this header and Player.h (Game.c, Player.c, Entities.c) does not
 * redeclare the C_* enumerators. */
#ifndef RSDK_COLLISION_SIDES_DEFINED
#define RSDK_COLLISION_SIDES_DEFINED
enum {
    C_NONE   = 0,
    C_TOP    = 1,
    C_LEFT   = 2,
    C_RIGHT  = 3,
    C_BOTTOM = 4
};
#endif

/* TileCollisionModes (Collision.hpp) */
enum {
    TILECOLLISION_NONE = 0, /* no tile collisions            */
    TILECOLLISION_DOWN = 1, /* downward gravity (normal)     */
    TILECOLLISION_UP   = 2  /* upward gravity (flipped)      */
};

/* CollisionModes (Collision.hpp) */
enum {
    CMODE_FLOOR = 0,
    CMODE_LWALL = 1,
    CMODE_ROOF  = 2,
    CMODE_RWALL = 3
};

#define RSDK_LAYER_COUNT 8     /* LAYER_COUNT (Scene.hpp)               */
/* RSDK_TILE_SIZE / RSDK_CPATH_COUNT also live in storage.h with identical
 * token text, so no #ifndef guard is needed for them. RSDK_TILE_COUNT differs
 * only in spelling (storage.h "1024" vs "0x400"), which trips GCC's
 * redefinition warning -> guard it. */
#define RSDK_TILE_SIZE   16    /* TILE_SIZE                              */
#ifndef RSDK_TILE_COUNT
#define RSDK_TILE_COUNT  0x400 /* 1024 base tiles per plane             */
#endif
#define RSDK_CPATH_COUNT 2     /* collision planes A/B                  */

/* TO_FIXED / FROM_FIXED (RetroEngine.hpp) */
#define RSDK_TO_FIXED(x)   ((int32_t)(x) << 16)
#define RSDK_FROM_FIXED(x) ((int32_t)(x) >> 16)

/* rsdk_vec2_t / rsdk_hitbox_t are duplicated (identical layout) in object.h /
 * storage.h. In the Saturn build those headers are included first; skip our
 * copies when their include guards are already set. In the standalone host
 * parity harness neither guard is defined, so we provide the types. */
#ifndef RSDK_OBJECT_H
typedef struct {
    int32_t x, y;          /* Q16.16 fixed-point world coords           */
} rsdk_vec2_t;
#endif

#ifndef RSDK_STORAGE_H
typedef struct {
    int16_t left, top, right, bottom;
} rsdk_hitbox_t;
#endif

/* Resident collision-mask record (Scene.hpp:134-139). Field ORDER and the
 * 64-byte size match cd/GHZ1MASK.BIN exactly, so the loaded buffer can be
 * pointer-cast to (const rsdk_collision_mask_t *) with no copy. */
typedef struct {
    uint8_t floorMasks[16];
    uint8_t lWallMasks[16];
    uint8_t rWallMasks[16];
    uint8_t roofMasks[16];
} rsdk_collision_mask_t;

/* Resident tile-info record (Scene.hpp:141-147). Padded to 8 bytes to match
 * the cd/GHZ1MASK.BIN info-block stride (fa,la,ra,roa,flag,0,0,0). */
typedef struct {
    uint8_t floorAngle;
    uint8_t lWallAngle;
    uint8_t rWallAngle;
    uint8_t roofAngle;
    uint8_t flag;
    uint8_t _pad0, _pad1, _pad2;
} rsdk_tile_info_t;

/* CollisionSensor (Collision.hpp) */
typedef struct {
    rsdk_vec2_t position;  /* Q16.16                                    */
    bool        collided;
    uint8_t     angle;
} rsdk_collision_sensor_t;

/* TileLayer view (Scene.hpp). position is in INTEGER pixels (decomp
 * layer->position is a non-fixed Vector2 in the collision math). */
typedef struct {
    const uint16_t *layout;   /* xsize*ysize entries, row-major native u16  */
    int32_t         xsize;    /* tiles                                      */
    int32_t         ysize;    /* tiles                                      */
    uint8_t         widthShift;
    rsdk_vec2_t     position;  /* integer-pixel layer offset (x,y)          */
    bool            active;
    /* #180 step 3d toroidal column-window. When `win` is non-NULL the tile
     * fetch ignores `layout` and reads the resident W-column band instead:
     * column-major, win[slot*ysize + row] with slot = worldCol % winW, band
     * = [winBase, winBase+winW). A worldCol outside the band fetches 0xFFFF
     * (no resident tile). NULL/0 => full-grid `layout` (non-windowed). This
     * mirrors tools/qa_ghz_colwindow_gate.py ColWindow.fetch line-for-line;
     * the band is filled/slid by src/rsdk/colwindow.c. */
    const uint16_t *win;
    int32_t         winW;
    int32_t         winBase;
} rsdk_collision_layer_t;

/* Moving entity (the subset of EntityPlayer the collision core touches). */
typedef struct {
    rsdk_vec2_t position;       /* Q16.16                                  */
    rsdk_vec2_t velocity;       /* Q16.16                                  */
    int32_t     groundVel;      /* Q16.16                                  */
    uint8_t     angle;          /* Q0.8                                    */
    bool        onGround;
    uint8_t     collisionMode;  /* CMODE_*                                 */
    uint8_t     collisionPlane; /* 0 = A, 1 = B                            */
    uint8_t     tileCollisions; /* TILECOLLISION_*                         */
    uint16_t    collisionLayers;/* bitmask over RSDK_LAYER_COUNT layers    */
} rsdk_collision_entity_t;

/* --- binding (runtime + harness) ------------------------------------- */

/* Bind the GMS2 compacted mask/info/remap blob (cd/GHZ1MASK.BIN). The collision
 * core indexes into the blob directly (slot = remap[tile & 0x3FF]; slot 0 is the
 * blank sentinel), so the blob must stay resident for the scene's lifetime.
 * Replaces the old per-plane full-1024 bind (#180 step 3 compaction). */
void rsdk_collision_bind_compact(const void *blob);

/* Reset all RSDK_LAYER_COUNT layers to inactive. */
void rsdk_collision_clear_layers(void);

/* Install a full-grid layout as tile layer `idx` (0..RSDK_LAYER_COUNT-1).
 * Clears any toroidal window previously bound to the layer. */
void rsdk_collision_set_layer(int idx, const uint16_t *layout,
                              int32_t xsize, int32_t ysize, uint8_t widthShift,
                              int32_t pos_x_px, int32_t pos_y_px);

/* #180 step 3d: install a toroidal column-window as tile layer `idx`. The
 * window buffer (winW*ysize entries, column-major) is owned by the colwindow
 * streamer (src/rsdk/colwindow.c); the collision core reads it through the
 * single s_layer_tile() helper. xsize is the FULL level width in tiles (band
 * guard uses it), ysize the full height. Pass win=NULL to revert to a
 * full-grid layout via rsdk_collision_set_layer. */
void rsdk_collision_set_layer_window(int idx, const uint16_t *win,
                                     int32_t xsize, int32_t ysize,
                                     uint8_t widthShift, int32_t winW,
                                     int32_t winBase,
                                     int32_t pos_x_px, int32_t pos_y_px);

/* Cheap per-advance update of the resident band's left edge for layer idx
 * (called once per camera advance after colwindow_ensure_band streams the
 * newly-entered columns). No-op if the layer has no window bound. */
void rsdk_collision_set_window_base(int idx, int32_t winBase);

/* #180 step 4b - per-column topmost solid-floor query (the data source
 * Player_SurfaceY/Angle read instead of the retired cd/GHZ1SURF.BIN
 * heightmap). x_px is a WORLD pixel x. Mirrors tools/qa_ghz_fall_through_
 * gate.py oracle_floor: plane 0, floor solidity 1<<12; for each active FG
 * layer scan rows top-down for the FIRST solid-floor tile whose flip-applied
 * floor mask is < 0xFF (the topmost real surface), surface world-Y =
 * layer->position.y + ty*16 + mask; take the minimum (highest) across all
 * active layers. Returns 0xFFFF (SMS_NO_FLOOR) for a real pit or an x outside
 * the resident window band. out_angle (nullable) receives the flip-applied
 * floor angle of the chosen surface. */
int32_t rsdk_collision_column_floor(int32_t x_px, int32_t *out_angle);

/* --- single entry (ProcessObjectMovement, Collision.cpp:924) --------- */

void rsdk_process_object_movement(rsdk_collision_entity_t *entity,
                                  const rsdk_hitbox_t *outerBox,
                                  const rsdk_hitbox_t *innerBox);

/* --- exposed primitives (for the host parity harness + diagnostics) -- */

/* Set the per-call collision context the Find and Collision primitives read
 * (mirrors the globals ProcessObjectMovement sets). */
void rsdk_collision_set_context(rsdk_collision_entity_t *entity,
                                int32_t tolerance);

void rsdk_find_floor_position(rsdk_collision_sensor_t *sensor);
void rsdk_find_lwall_position(rsdk_collision_sensor_t *sensor);
void rsdk_find_roof_position(rsdk_collision_sensor_t *sensor);
void rsdk_find_rwall_position(rsdk_collision_sensor_t *sensor);
void rsdk_floor_collision(rsdk_collision_sensor_t *sensor);
void rsdk_lwall_collision(rsdk_collision_sensor_t *sensor);
void rsdk_roof_collision(rsdk_collision_sensor_t *sensor);
void rsdk_rwall_collision(rsdk_collision_sensor_t *sensor);

#endif /* RSDK_COLLISION_H */
