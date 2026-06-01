#ifndef MANIA_OBJECTS_COMMON_FORCESPIN_H
#define MANIA_OBJECTS_COMMON_FORCESPIN_H

/* Phase 2.4-PLAT (Task #155) — ForceSpin port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Common_ForceSpin.c` + `..._ForceSpin.h`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * RSDK_THIS) like the 2.4g/2.4h precedents. Decomp struct field order is
 * preserved 1:1 (ForceSpin.h:13-18).
 *
 * ForceSpin is an INVISIBLE trigger (decomp Create L73 visible=false;
 * Update L52 sets visible=debugActive). It forces the player into a
 * tube-roll state when entered. No atlas; no draw.
 *
 * Saturn substitutions (documented as FIXME in ForceSpin.c):
 *   - RSDK `Animator` -> Saturn `Animator` (= rsdk_animator_t). Layout
 *     fidelity only; the trigger is invisible in-game so there is no
 *     atlas and no draw_only.
 *   - foreach_active(Player) collapses to the single g_ghz_player
 *     (g_ghz_player_addr), gated on mania_is_ghz_active().
 *   - The Zone_RotateOnPivot bbox test uses the Saturn Zone_RotateOnPivot
 *     (Zone.h:91; int* px/py + int ox/oy + angle).
 *   - The Saturn player_t is minimal (Player.h:115-158): no state machine
 *     (Player_State_TubeRoll/TubeAirRoll/Roll/Air), no animator.animationID,
 *     no nextGroundState/nextAirState. ForceSpin_SetPlayerState is therefore
 *     a Saturn-fit: it forces the player into a rolling kinematic (onGround
 *     roll velocity + airborne) using the minimal player_t fields that
 *     exist (gsp/xsp/ysp/onGround/jumping), mirroring the decomp intent
 *     (a tube-roll lock with a minimum ground speed). */

#include "../../Game.h"
#include "../Global/Player.h"

/* Object Class (decomp ObjectForceSpin, ForceSpin.h:7-10). */
typedef struct ObjectForceSpin {
    RSDK_OBJECT
    uint16 aniFrames;
} ObjectForceSpin;

/* Entity Class (decomp EntityForceSpin, ForceSpin.h:13-18). */
typedef struct EntityForceSpin {
    RSDK_ENTITY
    int32    size;
    int32    negAngle;
    Animator animator;
} EntityForceSpin;

extern ObjectForceSpin *ForceSpin;

/* Standard RSDK class callbacks (per decomp). */
void ForceSpin_Update(void);
void ForceSpin_LateUpdate(void);
void ForceSpin_StaticUpdate(void);
void ForceSpin_Draw(void);
void ForceSpin_Create(void *data);
void ForceSpin_StageLoad(void);

#endif /* MANIA_OBJECTS_COMMON_FORCESPIN_H */
