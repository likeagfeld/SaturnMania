#ifndef MANIA_OBJECTS_GHZ_NEWTRON_H
#define MANIA_OBJECTS_GHZ_NEWTRON_H

/* Phase 2.4c.2 Task #147 — Newtron port (21 GHZ Act 1 instances).
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_GHZ_Newtron.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Scope:
 *   - Two type variants per decomp NewtronTypes enum (Newtron.h L6-10):
 *       NEWTRON_SHOOT = 0  (idle/shoot turret)
 *       NEWTRON_FLY   = 1  (green glider)
 *     NEWTRON_PROJECTILE = 2 is the spawn-only sub-entity; Phase 2.4c.2
 *     defers projectile spawn until Phase 2.5 (depends on Player_Hurt).
 *   - Anims used (per decomp Create L47-69):
 *       0 = SHOOT idle      (turret stowed)
 *       1 = SHOOT firing    (transient)
 *       2 = FLY idle        (cloaked turret)
 *       3 = FLY appear      (Newtron_State_StartFly)
 *       4 = FLY glide       (Newtron_State_Fly)
 *       5 = flame overlay   (FLY only)
 *       6 = projectile      (deferred)
 *   - Range hitbox: {-128, -64, 128, 64} (decomp StageLoad L97-100).
 *   - Touch hitbox: {-12, -14, 12, 14}   (decomp StageLoad L79-82). */

#include <jo/jo.h>
#include "../Global/Player.h"

#ifdef __cplusplus
extern "C" {
#endif

void newtron_load_assets(void);
void newtron_tick_and_draw(const sms_world_t *w, player_t *p,
                           int cam_x, int cam_y);

#ifdef __cplusplus
}
#endif

#endif /* MANIA_OBJECTS_GHZ_NEWTRON_H */
