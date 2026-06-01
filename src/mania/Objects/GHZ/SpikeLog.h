#ifndef MANIA_OBJECTS_GHZ_SPIKELOG_H
#define MANIA_OBJECTS_GHZ_SPIKELOG_H

/* Phase 2.4c.2 Task #147 — SpikeLog port (61 GHZ Act 1 instances).
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_GHZ_SpikeLog.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Scope:
 *   - SpikeLog_StaticUpdate (decomp L20)  -> SpikeLog->timer = Zone->timer/3 & 0x1F
 *   - SpikeLog_State_Main (decomp L59-113) animator.frameID = (frame+timer)&0x1F
 *     hazard window = animator.frameID & ~3 == 8 (frames 8..11 = spikes
 *     extended -> hurt).
 *   - hitboxSpikeLog (decomp L48-52) = {-8,-16,8,0} around entity.
 *   - Fire-shield path + Mighty Plus path: Phase 2.5 / Plus.
 *   - State_Burn: Phase 2.5 (depends on BurningLog spawn, which is
 *     deferred). The Saturn-side just stays in State_Main and accepts
 *     fire-shield contact as a passive no-op for now.
 *
 * Asset: cd/SPIKELOG.SP2/.MET (32-frame single-anim cycle from
 *   GHZ/SpikeLog.bin) + cd/GHZ1LOG.BIN (u16 BE count + 6-byte tuples). */

#include <jo/jo.h>
#include "../Global/Player.h"

#ifdef __cplusplus
extern "C" {
#endif

void spikelog_load_assets(void);
void spikelog_tick_and_draw(const sms_world_t *w, player_t *p,
                            int cam_x, int cam_y);

#ifdef __cplusplus
}
#endif

#endif /* MANIA_OBJECTS_GHZ_SPIKELOG_H */
