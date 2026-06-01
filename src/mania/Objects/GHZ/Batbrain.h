#ifndef MANIA_OBJECTS_GHZ_BATBRAIN_H
#define MANIA_OBJECTS_GHZ_BATBRAIN_H

/* Phase 2.4h — Batbrain badnik port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_GHZ_Batbrain.c` + `..._Batbrain.h`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich). Ported onto the RSDK entity engine like the
 * 2.4g precedents. Decomp struct field order preserved 1:1 (Batbrain.h:
 * 7-22). Saturn substitutions per InvisibleBlock.c: Hitbox -> hitbox_t,
 * Animator -> Animator, EntityPlayer* target -> player_t* (single bespoke
 * player), mania_is_ghz_active gate. */

#include "../../Game.h"
#include "../Global/Player.h"

/* Object Class (decomp ObjectBatbrain, Batbrain.h:7-12). */
typedef struct ObjectBatbrain {
    RSDK_OBJECT
    hitbox_t hitboxBadnik;
    uint16   aniFrames;
    uint16   sfxFlap;
} ObjectBatbrain;

/* Entity Class (decomp EntityBatbrain, Batbrain.h:15-22). */
typedef struct EntityBatbrain {
    RSDK_ENTITY
    StateMachine(state);
    int32     targetY;
    player_t *target;       /* decomp EntityPlayer* */
    Vector2   startPos;
    Animator  animator;
} EntityBatbrain;

extern ObjectBatbrain *Batbrain;

void Batbrain_Update(void);
void Batbrain_LateUpdate(void);
void Batbrain_StaticUpdate(void);
void Batbrain_Draw(void);
void Batbrain_Create(void *data);
void Batbrain_StageLoad(void);

void Batbrain_load_assets(void);
void Batbrain_draw_only(int cam_x, int cam_y);

#endif /* MANIA_OBJECTS_GHZ_BATBRAIN_H */
