#ifndef MANIA_OBJECTS_GLOBAL_ZONE_H
#define MANIA_OBJECTS_GLOBAL_ZONE_H

/* Phase 2.4g.2 (Task #153) — Zone object, camera-bounds + death-boundary
 * SUBSET only.
 *
 * The full RSDK Zone object (tools/_decomp_raw/SonicMania_Objects_Global_
 * Zone.c, 53 KB) is the zone-wide state machine: act transitions, fades,
 * player-swap, layer masks, draw groups, the random seed, etc. This file
 * ports ONLY the per-screen camera/player/death bounds arrays
 * (Zone.h:65-73) and their default initialisation (Zone.c:221-235), which
 * is the slice BoundsMarker writes and the GHZ camera clamp reads. The
 * rest of Zone lands in a later increment.
 *
 * Field set mirrors struct ObjectZone (Zone.h:65-73) verbatim:
 *   int32 cameraBoundsL/R/T/B[PLAYER_COUNT];   // pixel-space camera region
 *   int32 playerBoundsL/R/T/B[PLAYER_COUNT];   // Q16.16 player clamp
 *   int32 deathBoundary[PLAYER_COUNT];         // Q16.16 fatal Y line
 *
 * Saturn substitution: the decomp's `Zone->cameraBoundsB[playerID]` member
 * accesses become free-standing global arrays `g_zone_cameraBoundsB[...]`
 * etc. (there is no live `ObjectZone *Zone` instance on Saturn yet). Index
 * is the playerID; the Saturn build is single-player so only [0] is ever
 * written/read, but the arrays carry PLAYER_COUNT entries for decomp
 * struct-layout fidelity (a later 2-player or full-Zone port reuses them
 * unchanged). PLAYER_COUNT == 4 matches the decomp (Game.h upstream). */

#include "../../Game.h"

/* Decomp PLAYER_COUNT (Game.h upstream RSDK_GAME_PLAYER_COUNT). Saturn
 * only uses index 0 but keeps the 4-entry layout for parity. */
#ifndef MANIA_ZONE_PLAYER_COUNT
#define MANIA_ZONE_PLAYER_COUNT 4
#endif

/* Camera-region bounds in PIXELS (decomp Zone->cameraBounds*; written from
 * FROM_FIXED(playerBounds*) — see BoundsMarker.c:60,67,75,81 and the
 * default init Zone.c:222-225). The GHZ camera clamp (scene_ghz.c
 * ghz_set_camera) reads these. */
extern volatile int g_zone_cameraBoundsL[MANIA_ZONE_PLAYER_COUNT];
extern volatile int g_zone_cameraBoundsR[MANIA_ZONE_PLAYER_COUNT];
extern volatile int g_zone_cameraBoundsT[MANIA_ZONE_PLAYER_COUNT];
extern volatile int g_zone_cameraBoundsB[MANIA_ZONE_PLAYER_COUNT];

/* Player-clamp bounds in Q16.16 (decomp Zone->playerBounds*; init
 * Zone.c:227-230 = TO_FIXED(cameraBounds*)). */
extern volatile int g_zone_playerBoundsL[MANIA_ZONE_PLAYER_COUNT];
extern volatile int g_zone_playerBoundsR[MANIA_ZONE_PLAYER_COUNT];
extern volatile int g_zone_playerBoundsT[MANIA_ZONE_PLAYER_COUNT];
extern volatile int g_zone_playerBoundsB[MANIA_ZONE_PLAYER_COUNT];

/* Fatal-fall Y line in Q16.16 (decomp Zone->deathBoundary; init
 * Zone.c:232 = TO_FIXED(cameraBoundsB)). */
extern volatile int g_zone_deathBoundary[MANIA_ZONE_PLAYER_COUNT];

/* Tracks whether ANY BoundsMarker / the default init has populated the
 * camera bounds this scene. The GHZ camera clamp reads the Zone bounds
 * only when this is set; otherwise it falls back to the legacy world-size
 * clamp (defense-in-depth so a non-GHZ / pre-load frame never clamps to
 * a zeroed bound). Set by zone_init_default_bounds() and BoundsMarker. */
extern volatile int g_zone_bounds_valid;

/* Phase 2.4g.2 — seed the default bounds from the GHZ world size
 * (mirrors Zone.c:221-235). Called once when the GHZ scene's foreground
 * size is known (mania_ghz_player_init_on_transition). world_w_px /
 * world_h_px are the layer-0 pixel dimensions. */
void zone_init_default_bounds(int world_w_px, int world_h_px);

/* Phase 2.4g.2 — clear all bounds + the valid flag (scene teardown so a
 * stale GHZ bound never leaks into a re-load). */
void zone_reset_bounds(void);

/* Phase 2.4g.3 (Task #153) — Zone_RotateOnPivot.
 *
 * Mechanical port of tools/_decomp_raw/SonicMania_Objects_Global_Zone.c:
 * 506-512:
 *   void Zone_RotateOnPivot(Vector2 *pivotPos, Vector2 *originPos, int32 angle)
 *   {
 *       int32 x     = (pivotPos->x - originPos->x) >> 8;
 *       int32 y     = (pivotPos->y - originPos->y) >> 8;
 *       pivotPos->x = originPos->x + y * RSDK.Sin256(angle) + x * RSDK.Cos256(angle);
 *       pivotPos->y = originPos->y + y * RSDK.Cos256(angle) - x * RSDK.Sin256(angle);
 *   }
 *
 * Saturn substitution: the decomp `Vector2 *` (Q16.16 x,y) becomes two
 * in/out int32 pointers (px/py) plus the pivot origin (ox/oy). RSDK.Sin256
 * / Cos256 map to rsdk_sin256 / rsdk_cos256 (src/rsdk/math.h:85-86), which
 * return the same Q8.8 [-256..256] scale as the decomp LUT. PlaneSwitch is
 * the sole 2.4g.3 caller; the rotation runs on the player's Q16.16
 * position/velocity copies. (px,py) and (ox,oy) are world Q16.16. */
void Zone_RotateOnPivot(int *px, int *py, int ox, int oy, int angle);

#endif /* MANIA_OBJECTS_GLOBAL_ZONE_H */
