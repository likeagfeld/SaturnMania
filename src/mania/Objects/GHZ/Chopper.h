#ifndef MANIA_OBJECTS_GHZ_CHOPPER_H
#define MANIA_OBJECTS_GHZ_CHOPPER_H

/* Phase 2.4h — Chopper badnik port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_GHZ_Chopper.c` + `..._Chopper.h`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * RSDK_THIS) like the 2.4g precedents (InvisibleBlock/BoundsMarker/
 * PlaneSwitch) — see memory/ghz-pivot-to-rsdk-engine.md. Decomp struct
 * field order is preserved 1:1 (Chopper.h:9-28). Saturn substitutions:
 *   - RSDK `Hitbox`   -> Saturn `hitbox_t` (Player.h; int16 LTRB, bit-
 *     identical to RSDK Hitbox).
 *   - RSDK `Animator` -> Saturn `Animator` (= rsdk_animator_t, Game.h:35).
 *     Kept for layout fidelity; drawing is driven by the shared
 *     g_chopper_atlas (entity_atlas) walked by Chopper_draw_only.
 *   - `foreach_active(Player, player)` collapses to the single bespoke
 *     g_ghz_player (g_ghz_player_addr), gated on mania_is_ghz_active(),
 *     mirroring InvisibleBlock.c. */

#include "../../Game.h"
#include "../Global/Player.h"

typedef enum { CHOPPER_JUMP, CHOPPER_SWIM } ChopperTypes;

/* Object Class (decomp ObjectChopper, Chopper.h:9-16). */
typedef struct ObjectChopper {
    RSDK_OBJECT
    hitbox_t hitboxJump;
    hitbox_t hitboxSwim;
    hitbox_t hitboxRange;
    hitbox_t hitboxWater;
    uint16   aniFrames;
} ObjectChopper;

/* Entity Class (decomp EntityChopper, Chopper.h:19-28). */
typedef struct EntityChopper {
    RSDK_ENTITY
    StateMachine(state);
    uint8    type;
    uint16   timer;
    bool32   charge;
    Vector2  startPos;
    uint8    startDir;
    Animator animator;
} EntityChopper;

extern ObjectChopper *Chopper;

/* Standard RSDK class callbacks (per decomp). */
void Chopper_Update(void);
void Chopper_LateUpdate(void);
void Chopper_StaticUpdate(void);
void Chopper_Draw(void);
void Chopper_Create(void *data);
void Chopper_StageLoad(void);

/* Saturn-side bespoke draw: walks RSDK slots of class Chopper and draws
 * each via the shared g_chopper_atlas. Called from mania_ghz_draw_only
 * (rsdk_object_draw_all is suppressed in the GHZ path). */
void Chopper_load_assets(void);
void Chopper_draw_only(int cam_x, int cam_y);

#endif /* MANIA_OBJECTS_GHZ_CHOPPER_H */
