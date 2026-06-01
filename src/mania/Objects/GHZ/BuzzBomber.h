#ifndef MANIA_OBJECTS_GHZ_BUZZBOMBER_H
#define MANIA_OBJECTS_GHZ_BUZZBOMBER_H

/* Phase 2.4c Task #140 — BuzzBomber port (priority #2, 18 GHZ Act 1 instances).
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_GHZ_BuzzBomber.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Phase 2.4c scope:
 *   - State_Init   (decomp L155-167) — set velocity.x = +/-0x40000 = +/-0.25 px/fr.
 *   - State_Flying (decomp L169-190) — patrol with timer + wing/thrust anim.
 *   - State_Idle   (decomp L192-207) — pause after each leg of patrol.
 *   - State_DetectedPlayer (decomp L209-247) — projectile-shot timer (Phase 2.4c
 *     defers the actual projectile spawn — single sprite + countdown only).
 *   - Hitbox per decomp StageLoad L91-94: hitboxBadnik = { -24, -12, 24, 12 }.
 *
 * Phase 2.4c deferrals:
 *   - Projectile sub-entity (BuzzBomber_State_ProjectileShot + Charge):
 *     Phase 2.4d. The detect-player branch just plays the charge anim
 *     and reverts after 128 ticks; no projectile is spawned.
 *   - Detect-player range hitbox shotRange (decomp L52-59) — for the
 *     deferred projectile path; not used by the Phase 2.4c subset.
 *
 * Asset: cd/BUZZ.SPR (multi-frame BGR1555) + cd/GHZ1BUZZ.BIN
 * (u16 BE count + N * (u16 BE x, u16 BE y, u8 direction)). */

#include <jo/jo.h>
#include "../Global/Player.h"

#ifdef __cplusplus
extern "C" {
#endif

void buzzbomber_load_assets(void);
void buzzbomber_tick_and_draw(const sms_world_t *w, player_t *p,
                              int cam_x, int cam_y);

#ifdef __cplusplus
}
#endif

#endif /* MANIA_OBJECTS_GHZ_BUZZBOMBER_H */
