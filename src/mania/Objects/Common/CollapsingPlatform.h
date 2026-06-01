#ifndef MANIA_OBJECTS_COMMON_COLLAPSINGPLATFORM_H
#define MANIA_OBJECTS_COMMON_COLLAPSINGPLATFORM_H

/* Phase 2.4-PLAT (Task #155) — CollapsingPlatform port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Common_CollapsingPlatform.c` +
 * `..._CollapsingPlatform.h` (Christian Whitehead / Simon Thomley /
 * Hunter Bridges; decomp by Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * RSDK_THIS) like the 2.4g/2.4h precedents. Decomp struct field order is
 * preserved (CollapsingPlatform.h:29-44) WITH ONE SATURN-FIT DEVIATION:
 *
 *   - decomp `uint16 storedTiles[256]` (512 bytes) is DROPPED. The RSDK
 *     entity slot STRIDE on Saturn is 256 bytes (src/rsdk/object.h); a
 *     512-byte member alone overflows the slot. storedTiles only feeds
 *     the BreakableWall tile-shatter spawn (decomp State_Left/Right/Center
 *     CREATE_ENTITY(BreakableWall, BREAKWALL_TILE_DYNAMIC, ...)), which is
 *     NOT ported on Saturn (the FG.TMP tilemap is static — same reason
 *     BreakableWall_Break is a destroyEntity-only Saturn-fit). Without the
 *     tile spawn there is nothing to store, so the member is unused.
 *
 * CollapsingPlatform is IN-GAME INVISIBLE (decomp Update L16 sets
 * visible=false / debugActive; the crumbling surface is the FG tilemap,
 * no per-entity sprite). No atlas; no draw.
 *
 * Saturn substitutions (documented as FIXME in CollapsingPlatform.c):
 *   - RSDK `Hitbox`   -> Saturn `hitbox_t` (Player.h; int16 LTRB).
 *   - RSDK `Animator` -> Saturn `Animator` (= rsdk_animator_t). Layout
 *     fidelity only (Object class); invisible in-game.
 *   - foreach_active(Player) collapses to the single g_ghz_player
 *     (g_ghz_player_addr), gated on mania_is_ghz_active().
 *   - Player_CheckCollisionTouch trigger -> Player_CheckCollisionBox
 *     (the Saturn collision surface; a C_TOP contact = player stood on
 *     the platform).
 *   - The decomp collapse spawns BreakableWall debris tiles + SetTile -1
 *     on the FG layer (State_Left/Right/Center). NOT ported (static
 *     tilemap). The Saturn-fit collapse is destroyEntity + SFX (the
 *     surface stays drawn but the trigger entity is gone). */

#include "../../Game.h"
#include "../Global/Player.h"

typedef enum {
    COLLAPSEPLAT_LEFT,
    COLLAPSEPLAT_RIGHT,
    COLLAPSEPLAT_CENTER,
    COLLAPSEPLAT_LR,
    COLLAPSEPLAT_LRC,
} CollapsingPlatformTypes;

typedef enum {
    COLLAPSEPLAT_TARGET_LOW,
    COLLAPSEPLAT_TARGET_HIGH,
} CollapsingPlatformTargetLayers;

/* Object Class (decomp ObjectCollapsingPlatform, CollapsingPlatform.h:20-26). */
typedef struct ObjectCollapsingPlatform {
    RSDK_OBJECT
    uint8    shift;
    Animator animator;
    uint16   aniFrames;
    uint16   sfxCrumble;
} ObjectCollapsingPlatform;

/* Entity Class (decomp EntityCollapsingPlatform, CollapsingPlatform.h:29-44;
 * storedTiles[256] dropped — see header note). */
typedef struct EntityCollapsingPlatform {
    RSDK_ENTITY
    StateMachine(state);
    Vector2  size;
    bool32   respawn;
    uint16   targetLayer;
    uint8    type;
    int32    delay;
    bool32   eventOnly;
    bool32   mightyOnly;
    int32    unused1;
    int32    collapseDelay;
    hitbox_t hitboxTrigger;
    Vector2  stoodPos;
} EntityCollapsingPlatform;

extern ObjectCollapsingPlatform *CollapsingPlatform;

/* Standard RSDK class callbacks (per decomp). */
void CollapsingPlatform_Update(void);
void CollapsingPlatform_LateUpdate(void);
void CollapsingPlatform_StaticUpdate(void);
void CollapsingPlatform_Draw(void);
void CollapsingPlatform_Create(void *data);
void CollapsingPlatform_StageLoad(void);

#endif /* MANIA_OBJECTS_COMMON_COLLAPSINGPLATFORM_H */
