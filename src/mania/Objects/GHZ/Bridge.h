#ifndef MANIA_OBJECTS_GHZ_BRIDGE_H
#define MANIA_OBJECTS_GHZ_BRIDGE_H

/* Phase 2.4-PLAT (Task #155) — Bridge port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_GHZ_Bridge.c` + `..._Bridge.h`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * RSDK_THIS) like the 2.4g/2.4h precedents (InvisibleBlock / Chopper).
 * Decomp struct field order is preserved 1:1 (Bridge.h:13-28).
 *
 * Bridge is the ONLY in-game-visible class in 2.4-PLAT (decomp Create
 * L86 visible=true). The planks are drawn from GHZ/Bridge.bin via the
 * entity_atlas (cd/BRIDGE.SP2 + cd/BRIDGE.MET, all frames per
 * memory/entity-atlas-must-ship-all-frames.md). The sine-depression
 * draw (decomp Bridge_Draw L50-81) is reproduced in Bridge_draw_only.
 *
 * Saturn substitutions (documented as FIXME in Bridge.c):
 *   - RSDK `Animator` -> Saturn `Animator` (= rsdk_animator_t). Layout
 *     fidelity only; the actual draw is via the shared g_bridge_atlas.
 *   - RSDK `Hitbox`   -> Saturn `hitbox_t` (Player.h; int16 LTRB).
 *   - `void *stoodEntity` kept (decomp uses the (Entity*)-1/-2 sentinel
 *     values to track the stood-on player); on Saturn the single player
 *     maps to the sentinel logic the same way.
 *   - foreach_active(Player) collapses to the single g_ghz_player
 *     (g_ghz_player_addr), gated on mania_is_ghz_active().
 *   - Bridge_HandleCollisions uses Player_CheckCollisionTouch in decomp.
 *     The Saturn collision surface is Player_CheckCollisionBox (Player.h);
 *     the plank step-on is detected by a C_TOP contact + snap to the sine
 *     plank Y (the closest Saturn-fit observable). */

#include "../../Game.h"
#include "../Global/Player.h"

/* Object Class (decomp ObjectBridge, Bridge.h:7-10). */
typedef struct ObjectBridge {
    RSDK_OBJECT
    uint16 aniFrames;
} ObjectBridge;

/* Entity Class (decomp EntityBridge, Bridge.h:13-28). */
typedef struct EntityBridge {
    RSDK_ENTITY
    uint8    length;
    bool32   burnable;
    uint8    burnOffset;
    uint8    stoodEntityCount;
    uint8    timer;
    int32    stoodPos;
    int32    bridgeDepth;
    int32    depression;
    void    *stoodEntity;
    int32    startPos;
    int32    endPos;
    Animator animator;
    int32    unused1;
} EntityBridge;

extern ObjectBridge *Bridge;

/* Standard RSDK class callbacks (per decomp). */
void Bridge_Update(void);
void Bridge_LateUpdate(void);
void Bridge_StaticUpdate(void);
void Bridge_Draw(void);
void Bridge_Create(void *data);
void Bridge_StageLoad(void);

/* Saturn-side asset load + bespoke draw (rsdk_object_draw_all is
 * suppressed in the GHZ path; the actual draw is Bridge_draw_only,
 * called from mania_ghz_draw_only). */
void Bridge_load_assets(void);
void Bridge_draw_only(int cam_x, int cam_y);

#endif /* MANIA_OBJECTS_GHZ_BRIDGE_H */
