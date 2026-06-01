#ifndef MANIA_OBJECTS_GLOBAL_SPIKES_H
#define MANIA_OBJECTS_GLOBAL_SPIKES_H

/* Phase 2.4c Task #140 — Spikes port (priority #1, 41 GHZ Act 1 instances).
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Global_Spikes.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Phase 2.4c scope:
 *   - Static (SPIKES_MOVE_STATIC) only — the moving-spike sub-state
 *     machine (SPIKES_MOVE_HIDDEN -> APPEAR -> SHOWN -> DISAPPEAR) is
 *     visible-only in non-GHZ-Act1 stages (Spikes.c:14-72).
 *   - Type 0 (vertical) + type 1 (horizontal) per decomp Spikes_Create
 *     L342-378.
 *   - Hitbox per decomp Spikes_Create L355-358 (vertical) and L373-376
 *     (horizontal): { -8*count..8*count } x { -16..16 } and rotated.
 *   - Saturn-side simplification: no Spikes_Shatter (Ice/PSZ2 path) and
 *     no MANIA_USE_PLUS Mighty Hammer Drop path — both are non-GHZ.
 *   - Hurt on contact: zero rings + small knockback. The full
 *     Player_Hurt port lands in Phase 2.5.
 *
 * Asset: cd/SPIKES.SPR (multi-frame BGR1555) + cd/GHZ1SPIKE.BIN
 * (u16 BE count + N * (u16 BE x, u16 BE y, u8 type, u8 count)).
 *
 * The Saturn-side BIN payload differs from the decomp's RSDK
 * editor-attribute format because we extract entity-table records
 * offline (per the existing tools/build_*_atlas.py pattern). The
 * runtime only sees the four-field fixed record.
 *
 * Integration: spikes_load_assets() registered in entities_load_assets
 * after motobug_load (largest atlas loads last per existing convention).
 * spikes_tick_and_draw() called in mania_ghz_tick_and_draw after
 * itembox_tick_and_draw and before signpost_tick_and_draw (drawGroup
 * matches per decomp Spikes_Create L337-339). */

#include <jo/jo.h>
#include "../Global/Player.h"

#ifdef __cplusplus
extern "C" {
#endif

void spikes_load_assets(void);
void spikes_tick_and_draw(const sms_world_t *w, player_t *p,
                          int cam_x, int cam_y);

#ifdef __cplusplus
}
#endif

#endif /* MANIA_OBJECTS_GLOBAL_SPIKES_H */
