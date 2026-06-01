#ifndef MANIA_OBJECTS_COMMON_ENTITIES_H
#define MANIA_OBJECTS_COMMON_ENTITIES_H

/* Phase 2.3 — minimal entity ports for GHZ Act 1 gameplay.
 *
 * Bundles five decomp-port entity classes plus the HUD overlay into a
 * single Saturn-side module. Each class mechanically translates the
 * decomp's state machine + hitbox + draw call using Phase 2.2-style
 * Saturn-port deviations (per-column surface table, AABB hit-test,
 * one-shot active flag).
 *
 * Decomp source files (cached in tools/_decomp_raw/):
 *   - SonicMania_Objects_GHZ_Motobug.c    (~255 lines, full port)
 *   - SonicMania_Objects_Global_Ring.{c,h} (~1300 lines, minimal port)
 *   - SonicMania_Objects_Global_Spring.{c,h}
 *   - SonicMania_Objects_Global_ItemBox.{c,h}
 *   - SonicMania_Objects_Global_SignPost.{c,h}
 *   - SonicMania_Objects_Global_HUD.{c,h}
 *
 * Archived counterpart (verified-working entity loops):
 *   - src/_archived/main.c.v01-handrolled:494-1209
 *     (load_rings, load_badniks, load_objects, draw_rings, draw_badniks,
 *      draw_springs, draw_monitors, draw_signpost, draw_hud,
 *      hud_glyph, hud_number, load_hud_digits)
 *
 * Phase 2.3 scope (mechanical-port subset; each item cites decomp
 * line range it implements):
 *   - Motobug: patrol + stomp + side-hurt (decomp Motobug_State_Move
 *     L122-156 + Player_CheckBadnikBreak archived-arch L702-757).
 *   - Ring: 16-frame spin + AABB pickup + +10 score (Ring_State_Normal
 *     L283-300 + Ring_Collect L335-378).
 *   - Spring: 12-tick squash + bounce launch (Spring_State_Vertical
 *     L233-273 + ysp=-10.0 launch via Spring_Bounce).
 *   - ItemBox: break-on-roll/jump + +10 rings (ItemBox_State_Idle +
 *     ItemBox_Break L488-538 — simplified: no powerup table).
 *   - SignPost: touch sets g_act_cleared + +1000 score (SignPost_State_
 *     Setup + SignPost_State_Spin — Phase 2.4 polish: spin anim).
 *   - HUD: 12-glyph 8x8 font, line 1 = ring icon + count, line 2 =
 *     6-digit score, line 3 = m:ss:cc timer (HUD_DrawNumbers archived
 *     L1183-1209).
 *
 * Phase 2.3 deferrals (cited inline at the relevant point):
 *   - Motobug smoke trail (Motobug_State_Smoke) — Phase 2.4 polish.
 *   - Motobug fall/turn (Motobug_State_Fall/Turn) — Phase 2.4.
 *   - Ring loss-on-hurt scatter (Ring_LoseRings) — Phase 2.5 hurt path.
 *   - Spring horizontal/diagonal variants — Phase 2.4 (Act 2 needs).
 *   - ItemBox powerup table (rings/shield/invuln/super) — Phase 2.5.
 *   - SignPost spin anim + bonus-tally cinematic — Phase 2.4.
 *
 * Integration: each class exposes a *_load_assets / *_tick_and_draw
 * pair. mania_ghz_tick_and_draw in Game.c calls them in order after
 * Player_Tick. */

#include <jo/jo.h>
#include <stdint.h>
#include <stdbool.h>

#include "../Global/Player.h"

#ifdef __cplusplus
extern "C" {
#endif

/* === Shared HUD score / ring / timer state ============================
 *
 * Owned by the HUD module but mutated by Ring/Motobug/ItemBox/SignPost
 * pickup branches. Accessed via extern reads for the HUD draw. */

extern int g_hud_rings;     /* uncollected ring count                 */
extern int g_hud_score;     /* 6-digit zero-padded                    */
extern int g_hud_ticks;     /* 60 Hz tick counter for the m:ss:cc      */

/* === Asset / table load entry points ================================= */

/* Bring up every Phase 2.3 entity sprite + table. Called once from
 * mania_engine_init AFTER the title/Player assets have settled (so the
 * cumulative pool draw can be measured under the title-baseline +
 * GHZ-resident peak together).
 *
 * Soft-fails on a per-class basis. If e.g. BADNIK.SPR is missing, the
 * other classes still load and the game still runs. */
void entities_load_assets(void);

/* === Per-class tick + draw entry points ==============================
 *
 * Each takes the camera origin + a non-const Player pointer so the
 * collision/stomp branches can mutate Sonic's velocity / state. The
 * sms_world_t pointer is borrowed (Phase 2.2 collision table). */

void rings_tick_and_draw   (const sms_world_t *w, player_t *p,
                            int cam_x, int cam_y);
void motobug_tick_and_draw (const sms_world_t *w, player_t *p,
                            int cam_x, int cam_y);
void spring_tick_and_draw  (const sms_world_t *w, player_t *p,
                            int cam_x, int cam_y);
void itembox_tick_and_draw (const sms_world_t *w, player_t *p,
                            int cam_x, int cam_y);
void signpost_tick_and_draw(const sms_world_t *w, player_t *p,
                            int cam_x, int cam_y);

/* Phase 2.4c Task #140 — priority entity ports (Spikes + BuzzBomber).
 * See docs/ghz_act1_entity_gap.md "Priority list". Each *_load_assets is
 * soft-fail and entities_load_assets calls them at the tail of the
 * existing load chain. *_tick_and_draw is called from
 * mania_ghz_tick_and_draw after the existing entity ticks. */
void spikes_load_assets    (void);
void spikes_tick_and_draw  (const sms_world_t *w, player_t *p,
                            int cam_x, int cam_y);
void buzzbomber_load_assets(void);
void buzzbomber_tick_and_draw(const sms_world_t *w, player_t *p,
                              int cam_x, int cam_y);

/* Phase 2.4c.2 Task #147 — SpikeLog (61) + Platform (59) + Newtron (21).
 * Each *_load_assets is soft-fail; *_tick_and_draw is invoked from
 * mania_ghz_draw_only after the Phase 2.4c entities. */
void spikelog_load_assets   (void);
void spikelog_tick_and_draw (const sms_world_t *w, player_t *p,
                             int cam_x, int cam_y);
void platform_load_assets   (void);
void platform_tick_and_draw (const sms_world_t *w, player_t *p,
                             int cam_x, int cam_y);
void newtron_load_assets    (void);
void newtron_tick_and_draw  (const sms_world_t *w, player_t *p,
                             int cam_x, int cam_y);

/* HUD draws every frame, independent of camera. Reads the shared
 * counters above. */
void hud_draw(void);

/* Tick the HUD timer (called once per game frame from mania_ghz_tick_
 * and_draw, gated by Player ready + not act-cleared). */
void hud_tick(void);

/* === Test hooks ====================================================== */

/* Has the act been cleared (signpost touched)? */
bool entities_act_cleared(void);

/* Phase 2.4a (Task #138) — Player jump SFX dispatch.
 *
 * Decomp cite: tools/_decomp_raw/SonicMania_Objects_Global_Player.c:3327
 *   RSDK.PlaySfx(Player->sfxJump, false, 255);
 *
 * Called from src/mania/Game.c::mania_ghz_tick_and_draw at the
 * jump-press edge (same edge that triggers player_action_jump in
 * Player.c) so the SFX site lives next to the per-entity asset slot
 * (g_sfx_jump) it dispatches. No-op if SFX failed to load. */
void entities_play_sfx_jump(void);

#ifdef QA_SFX_PROBE
/* QA SFX-probe force-load (load PCM at boot, GHZ-independent). */
void entities_qa_force_load_sfx(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MANIA_OBJECTS_COMMON_ENTITIES_H */
