#ifndef MANIA_OBJECTS_COMMON_PLATFORM_H
#define MANIA_OBJECTS_COMMON_PLATFORM_H

/* Phase 2.4c.2 Task #147 — Platform port (59 GHZ Act 1 instances).
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Common_Platform.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Scope (Phase 2.4c.2 dominant GHZ Act 1 types from Scene1.bin attribute
 * parse via tools/build_phase2_4c2_entity_coords.py):
 *   - PLATFORM_FIXED      (decomp PlatformTypes=0) — static perch
 *   - PLATFORM_FALL       (decomp PlatformTypes=1) — fall on stand
 *   - PLATFORM_LINEAR     (decomp PlatformTypes=2) — sin-wave oscillation
 *   - PLATFORM_CIRCULAR   (decomp PlatformTypes=3) — circular orbit
 *
 * Phase 2.4c.2 deferrals:
 *   - PLATFORM_SWING / TRACK / PATH / PUSH / REACT / CLACKER / DIPROCK
 *     and their _REACT variants. These are non-GHZ-Act-1 dominant types
 *     (the parsed Scene1.bin distribution shows >95% FIXED/FALL/LINEAR).
 *     For non-supported types we fall through to FIXED behaviour so the
 *     sprite is still drawn at its declared position.
 *   - Collision types beyond PLATFORM_C_PLATFORM (top-only stand) and
 *     PLATFORM_C_SOLID (full AABB). Decomp PlatformCollisionTypes enum
 *     has 16 variants; Phase 2.4c.2 supports the two dominant ones.
 *   - Child entity carrying (decomp Update L48-94). Phase 2.5.
 *
 * Asset: cd/PLATFORM.SP2/.MET + cd/GHZ1PLAT.BIN. */

#include <jo/jo.h>
#include "../Global/Player.h"

#ifdef __cplusplus
extern "C" {
#endif

void platform_load_assets(void);
void platform_tick_and_draw(const sms_world_t *w, player_t *p,
                            int cam_x, int cam_y);

#ifdef __cplusplus
}
#endif

#endif /* MANIA_OBJECTS_COMMON_PLATFORM_H */
