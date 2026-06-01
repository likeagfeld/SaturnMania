#ifndef MANIA_OBJECTS_GHZ_CRABMEAT_H
#define MANIA_OBJECTS_GHZ_CRABMEAT_H

/* Phase 2.4h — Crabmeat badnik port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_GHZ_Crabmeat.c` + `..._Crabmeat.h`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich). Ported onto the RSDK entity engine like the
 * 2.4g precedents. Decomp struct field order preserved 1:1 (Crabmeat.h:
 * 7-23). Saturn substitutions per InvisibleBlock.c (Hitbox -> hitbox_t,
 * Animator -> Animator; single bespoke player; mania_is_ghz_active gate). */

#include "../../Game.h"
#include "../Global/Player.h"

/* Object Class (decomp ObjectCrabmeat, Crabmeat.h:7-12). */
typedef struct ObjectCrabmeat {
    RSDK_OBJECT
    hitbox_t hitboxBadnik;
    hitbox_t hitboxProjectile;
    uint16   aniFrames;
} ObjectCrabmeat;

/* Entity Class (decomp EntityCrabmeat, Crabmeat.h:15-23). */
typedef struct EntityCrabmeat {
    RSDK_ENTITY
    StateMachine(state);
    int32    timer;
    uint8    shootState;
    Vector2  startPos;
    int32    startDir;
    Animator animator;
} EntityCrabmeat;

extern ObjectCrabmeat *Crabmeat;

void Crabmeat_Update(void);
void Crabmeat_LateUpdate(void);
void Crabmeat_StaticUpdate(void);
void Crabmeat_Draw(void);
void Crabmeat_Create(void *data);
void Crabmeat_StageLoad(void);

void Crabmeat_load_assets(void);
void Crabmeat_draw_only(int cam_x, int cam_y);

#endif /* MANIA_OBJECTS_GHZ_CRABMEAT_H */
