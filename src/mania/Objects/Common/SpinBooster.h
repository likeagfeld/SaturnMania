#ifndef MANIA_OBJECTS_COMMON_SPINBOOSTER_H
#define MANIA_OBJECTS_COMMON_SPINBOOSTER_H

/* Phase 2.4-PLAT (Task #155) — SpinBooster port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Common_SpinBooster.c` + `..._SpinBooster.h`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * RSDK_THIS) like the 2.4g/2.4h precedents. Decomp struct field order is
 * preserved 1:1 (SpinBooster.h:13-26).
 *
 * SpinBooster is an INVISIBLE trigger (decomp Create L95 visible=false;
 * Update L58 sets visible=debugActive). It rolls + boosts the player along
 * a direction. No atlas; no draw.
 *
 * Saturn substitutions (documented as FIXME in SpinBooster.c):
 *   - RSDK `Animator` -> Saturn `Animator` (= rsdk_animator_t). Layout
 *     fidelity only; the trigger is invisible in-game so there is no
 *     atlas and no draw_only.
 *   - foreach_active(Player) collapses to the single g_ghz_player
 *     (g_ghz_player_addr), gated on mania_is_ghz_active().
 *   - activePlayers is a per-player bitmask in decomp (1 << GetEntitySlot);
 *     on Saturn with a single player it collapses to bit 0.
 *   - The Zone_RotateOnPivot bbox test uses the Saturn Zone_RotateOnPivot
 *     (Zone.h:91).
 *   - The Saturn player_t is minimal (Player.h:115-158): no state machine,
 *     no ObjectTileGrip, no collisionMode, no controlLock/tileCollisions.
 *     SpinBooster_HandleForceRoll/ApplyRollVelocity are Saturn-fit: they
 *     force a rolling kinematic and apply the boostPower along the booster
 *     angle using the minimal player_t fields (gsp/xsp/ysp/onGround),
 *     mirroring the decomp intent (force-roll + directional boost). The
 *     auto-grip CMODE wall/roof reattachment (decomp GetRollDir/HandleRollDir)
 *     has no minimal-player_t analogue and is dropped (GHZ Act 1 boosters
 *     are floor-aligned; autoGrip defaults 0 in the scene attrs). */

#include "../../Game.h"
#include "../Global/Player.h"

/* Object Class (decomp ObjectSpinBooster, SpinBooster.h:7-10). */
typedef struct ObjectSpinBooster {
    RSDK_OBJECT
    uint16 aniFrames;
} ObjectSpinBooster;

/* Entity Class (decomp EntitySpinBooster, SpinBooster.h:13-26). */
typedef struct EntitySpinBooster {
    RSDK_ENTITY
    uint8    autoGrip;
    uint8    bias;
    int32    size;
    int32    boostPower;
    bool32   boostAlways;
    bool32   forwardOnly;
    bool32   playSound;
    bool32   allowTubeInput;
    uint8    activePlayers;
    int32    unused;
    Animator animator;
} EntitySpinBooster;

extern ObjectSpinBooster *SpinBooster;

/* Standard RSDK class callbacks (per decomp). */
void SpinBooster_Update(void);
void SpinBooster_LateUpdate(void);
void SpinBooster_StaticUpdate(void);
void SpinBooster_Draw(void);
void SpinBooster_Create(void *data);
void SpinBooster_StageLoad(void);

#endif /* MANIA_OBJECTS_COMMON_SPINBOOSTER_H */
