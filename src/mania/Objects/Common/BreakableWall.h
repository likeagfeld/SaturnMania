#ifndef MANIA_OBJECTS_COMMON_BREAKABLEWALL_H
#define MANIA_OBJECTS_COMMON_BREAKABLEWALL_H

/* Phase 2.4-PLAT (Task #155) — BreakableWall port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Common_BreakableWall.c` + `..._BreakableWall.h`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * RSDK_THIS) like the 2.4g/2.4h precedents (InvisibleBlock / Chopper).
 * Decomp struct field order is preserved 1:1 (BreakableWall.h:44-60).
 *
 * Saturn substitutions (documented as FIXME in BreakableWall.c):
 *   - RSDK `Hitbox`   -> Saturn `hitbox_t` (Player.h; int16 LTRB).
 *   - RSDK `Animator` -> Saturn `Animator` (= rsdk_animator_t). Layout
 *     fidelity only; the in-game wall is INVISIBLE (decomp visible=
 *     debugActive — the wall surface is the FG tilemap, no per-entity
 *     sprite), so there is NO atlas and NO draw_only.
 *   - foreach_active(Player) collapses to the single g_ghz_player
 *     (g_ghz_player_addr), gated on mania_is_ghz_active().
 *   - The decomp BreakableWall_Break tile-shatter (CREATE_ENTITY of
 *     BREAKWALL_TILE_FIXED debris + RSDK.SetTile -1 on the FG layer) is
 *     NOT ported: the Saturn surface model has no RSDK GetTile/SetTile
 *     dynamic-tile path (the FG.TMP tilemap is static). The break is a
 *     destroyEntity + score + SFX (the surface stays drawn but the
 *     collision entity is gone — the closest Saturn-fit observable). */

#include "../../Game.h"
#include "../Global/Player.h"

typedef enum {
    BREAKWALL_TYPE_WALL,
    BREAKWALL_TYPE_FLOOR,
    BREAKWALL_TYPE_BURROWFLOOR,
    BREAKWALL_TYPE_BURROWFLOOR_B,
    BREAKWALL_TYPE_BURROWFLOORUP,
    BREAKWALL_TYPE_CEILING
} BreakableWallTypes;

typedef enum {
    BREAKWALL_PRIO_HIGH,
    BREAKWALL_PRIO_LOW,
} BreakableWallPriorities;

typedef enum {
    BREAKWALL_TILE_FIXED = 1,
    BREAKWALL_TILE_DYNAMIC,
} BreakableWallTileTypes;

/* Object Class (decomp ObjectBreakableWall, BreakableWall.h:26-41 — the
 * velocity TABLEs are debris-spawn data unused by the Saturn-fit break;
 * dropped). */
typedef struct ObjectBreakableWall {
    RSDK_OBJECT
    Animator animator;
    uint16   aniFrames;
    uint16   sfxBreak;
    uint16   farPlaneLayer;
} ObjectBreakableWall;

/* Entity Class (decomp EntityBreakableWall, BreakableWall.h:44-60). */
typedef struct EntityBreakableWall {
    RSDK_ENTITY
    StateMachine(state);
    StateMachine(stateDraw);
    uint8    type;
    bool32   onlyKnux;
    bool32   onlyMighty;
    int32    priority;
    Vector2  size;
    uint16   tileInfo;
    uint16   targetLayer;
    int32    timer;
    Vector2  tilePos;
    int32    tileRotation;
    int32    gravityStrength;
    hitbox_t hitbox;
} EntityBreakableWall;

extern ObjectBreakableWall *BreakableWall;

/* Standard RSDK class callbacks (per decomp). */
void BreakableWall_Update(void);
void BreakableWall_LateUpdate(void);
void BreakableWall_StaticUpdate(void);
void BreakableWall_Draw(void);
void BreakableWall_Create(void *data);
void BreakableWall_StageLoad(void);

#endif /* MANIA_OBJECTS_COMMON_BREAKABLEWALL_H */
