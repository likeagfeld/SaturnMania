#ifndef MANIA_OBJECTS_GLOBAL_INVISIBLEBLOCK_H
#define MANIA_OBJECTS_GLOBAL_INVISIBLEBLOCK_H

/* Phase 2.4g.1 (Task #153) — InvisibleBlock port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Global_InvisibleBlock.c` +
 * `..._InvisibleBlock.h` (Christian Whitehead / Simon Thomley /
 * Hunter Bridges; decomp by Rubberduckycooly & RMGRich).
 *
 * This is the FIRST GHZ entity ported onto the RSDK entity engine
 * (src/rsdk/object.c slot table + foreach_active + RSDK_THIS) instead
 * of the legacy bespoke `<class>_tick_and_draw(world,player,...)`
 * pattern — see memory/ghz-pivot-to-rsdk-engine.md (BINDING 2026-05-28).
 *
 * Decomp struct field order is preserved 1:1 so future ports compile
 * against the same layout. Saturn type substitutions:
 *   - RSDK `Hitbox`  -> Saturn `hitbox_t` (src/mania/Objects/Global/Player.h,
 *     int16 left/top/right/bottom — bit-identical to RSDK Hitbox).
 *   - RSDK `Animator` -> Saturn `Animator` (= rsdk_animator_t, Game.h:35).
 *   - `PlaneFilterTypes` enum mirrors the decomp header verbatim. */

#include "../../Game.h"
#include "Player.h"

typedef enum {
    PLANEFILTER_NONE,
    PLANEFILTER_AL,
    PLANEFILTER_BL,
    PLANEFILTER_AH,
    PLANEFILTER_BH,
} PlaneFilterTypes;

/* Object Class (decomp ObjectInvisibleBlock). */
typedef struct ObjectInvisibleBlock {
    RSDK_OBJECT
    uint16   aniFrames;
    Animator animator;
} ObjectInvisibleBlock;

/* Entity Class (decomp EntityInvisibleBlock). */
typedef struct EntityInvisibleBlock {
    RSDK_ENTITY
    uint8   width;
    uint8   height;
    int32   planeFilter;     /* PlaneFilterTypes (VAR_ENUM = int32 on-wire) */
    bool32  noCrush;
    bool32  activeNormal;
    bool32  timeAttackOnly;
    bool32  noChibi;
    hitbox_t hitbox;
} EntityInvisibleBlock;

extern ObjectInvisibleBlock *InvisibleBlock;

/* Standard RSDK class callbacks (per decomp). */
void InvisibleBlock_Update(void);
void InvisibleBlock_LateUpdate(void);
void InvisibleBlock_StaticUpdate(void);
void InvisibleBlock_Draw(void);
void InvisibleBlock_Create(void *data);
void InvisibleBlock_StageLoad(void);

#endif /* MANIA_OBJECTS_GLOBAL_INVISIBLEBLOCK_H */
