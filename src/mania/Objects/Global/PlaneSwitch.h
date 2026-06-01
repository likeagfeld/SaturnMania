#ifndef MANIA_OBJECTS_GLOBAL_PLANESWITCH_H
#define MANIA_OBJECTS_GLOBAL_PLANESWITCH_H

/* Phase 2.4g.3 (Task #153) — PlaneSwitch port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Global_PlaneSwitch.c` +
 * `..._PlaneSwitch.h` (Christian Whitehead / Simon Thomley /
 * Hunter Bridges; decomp by Rubberduckycooly & RMGRich).
 *
 * THIRD (last + largest) GHZ entity ported onto the RSDK entity engine
 * (after InvisibleBlock 2.4g.1 + BoundsMarker 2.4g.2) per
 * memory/ghz-pivot-to-rsdk-engine.md. 106 GHZ Act 1 instances spawn into
 * RSDK slots through the same class-agnostic scene-spawn pipeline.
 *
 * PlaneSwitch writes player->collisionPlane (A/B path select) when the
 * player crosses the marker's X within the marker's size window. The
 * two-plane tile-collision bridge (Player.h sms_world_t raw_alt +
 * active_path; Player_Tick selects the path from collisionPlane) makes
 * that write select which collision path the player's surface probe reads.
 *
 * Struct field order mirrors the decomp PlaneSwitch.h:18-31 1:1. Saturn
 * type substitutions:
 *   - RSDK `Animator` -> Saturn `Animator` (= rsdk_animator_t, Game.h).
 *   - `PlaneSwitchFlags` enum mirrors the decomp header verbatim. */

#include "../../Game.h"
#include "Player.h"

/* decomp PlaneSwitch.h:6-15 */
typedef enum {
    PLANESWITCH_LOWLAYER_LEFT   = 0,
    PLANESWITCH_PLANEA_LEFT     = 0,
    PLANESWITCH_LOWLAYER_RIGHT  = 0,
    PLANESWITCH_PLANEA_RIGHT    = 0,
    PLANESWITCH_HIGHLAYER_LEFT  = 1,
    PLANESWITCH_PLANEB_LEFT     = 2,
    PLANESWITCH_HIGHLAYER_RIGHT = 4,
    PLANESWITCH_PLANEB_RIGHT    = 8,
} PlaneSwitchFlags;

/* Object Class (decomp ObjectPlaneSwitch, PlaneSwitch.h:18-21). */
typedef struct ObjectPlaneSwitch {
    RSDK_OBJECT
    uint16 aniFrames;
} ObjectPlaneSwitch;

/* Entity Class (decomp EntityPlaneSwitch, PlaneSwitch.h:24-31). */
typedef struct EntityPlaneSwitch {
    RSDK_ENTITY
    int32    flags;
    int32    size;
    bool32   onPath;
    int32    negAngle;
    Animator animator;
} EntityPlaneSwitch;

extern ObjectPlaneSwitch *PlaneSwitch;

/* Standard RSDK class callbacks (per decomp). */
void PlaneSwitch_Update(void);
void PlaneSwitch_LateUpdate(void);
void PlaneSwitch_StaticUpdate(void);
void PlaneSwitch_Draw(void);
void PlaneSwitch_Create(void *data);
void PlaneSwitch_StageLoad(void);

/* Extra Entity Functions (decomp PlaneSwitch.c:49-113). DrawSprites is a
 * no-op on Saturn (the switch is invisible in normal play). */
void PlaneSwitch_DrawSprites(void);
void PlaneSwitch_CheckCollisions(EntityPlaneSwitch *self, player_t *other,
                                 int32 flags, int32 size, bool32 switchDrawGroup,
                                 uint8 low, uint8 high);

#endif /* MANIA_OBJECTS_GLOBAL_PLANESWITCH_H */
