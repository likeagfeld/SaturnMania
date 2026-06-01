/* ---------------------------------------------------------------------
 * Phase 2.4g.2 (Task #153) — Zone object: camera-bounds + death-boundary
 * SUBSET.
 *
 * Mechanical port of the bounds slice of
 * `tools/_decomp_raw/SonicMania_Objects_Global_Zone.c`.
 * Original: Christian Whitehead / Simon Thomley / Hunter Bridges;
 * decomp by Rubberduckycooly & RMGRich.
 *
 * Ports ONLY:
 *   - the camera/player/death bounds arrays (Zone.h:65-73), and
 *   - their default init (Zone.c:221-235, the non-swap branch).
 *
 * The decomp seeds the bounds from the layer-0 pixel size:
 *   cameraBoundsL = 0;            cameraBoundsR = layerSize.x;
 *   cameraBoundsT = 0;            cameraBoundsB = layerSize.y;
 *   playerBounds* = TO_FIXED(cameraBounds*);
 *   deathBoundary = TO_FIXED(cameraBoundsB);
 * BoundsMarker_ApplyBounds then overrides cameraBoundsB/T (and the paired
 * playerBoundsB/T + deathBoundary) as the player crosses each marker's X.
 * ------------------------------------------------------------------- */

#include "Zone.h"
#include "../../../rsdk/math.h"   /* rsdk_sin256 / rsdk_cos256 (Q8.8 LUT) */

/* Q16.16 fixed-point helpers (decomp TO_FIXED / FROM_FIXED, RSDK
 * Definitions.hpp). Local because Game.c does not export them. */
#define ZONE_TO_FIXED(x)   ((x) << 16)

/* __attribute__((used)) so GCC 8.2 whole-program LTO keeps these symbols
 * in game.map (the 2.4g.2 gate P2/P4 resolve them by name) — same
 * precedent as InvisibleBlock's diag globals + the entity-atlas globals
 * (memory/entity-atlas-loader-pattern.md). volatile because BoundsMarker
 * (a different TU) writes them and the GHZ camera clamp (scene_ghz.c, a
 * third TU) reads them every frame; the value must round-trip through
 * memory, not be CSE'd. */
__attribute__((used)) volatile int g_zone_cameraBoundsL[MANIA_ZONE_PLAYER_COUNT];
__attribute__((used)) volatile int g_zone_cameraBoundsR[MANIA_ZONE_PLAYER_COUNT];
__attribute__((used)) volatile int g_zone_cameraBoundsT[MANIA_ZONE_PLAYER_COUNT];
__attribute__((used)) volatile int g_zone_cameraBoundsB[MANIA_ZONE_PLAYER_COUNT];

__attribute__((used)) volatile int g_zone_playerBoundsL[MANIA_ZONE_PLAYER_COUNT];
__attribute__((used)) volatile int g_zone_playerBoundsR[MANIA_ZONE_PLAYER_COUNT];
__attribute__((used)) volatile int g_zone_playerBoundsT[MANIA_ZONE_PLAYER_COUNT];
__attribute__((used)) volatile int g_zone_playerBoundsB[MANIA_ZONE_PLAYER_COUNT];

__attribute__((used)) volatile int g_zone_deathBoundary[MANIA_ZONE_PLAYER_COUNT];

__attribute__((used)) volatile int g_zone_bounds_valid = 0;

/* Decomp Zone.c:221-235 (non-swap branch). */
void zone_init_default_bounds(int world_w_px, int world_h_px)
{
    for (int s = 0; s < MANIA_ZONE_PLAYER_COUNT; ++s) {
        g_zone_cameraBoundsL[s] = 0;
        g_zone_cameraBoundsR[s] = world_w_px;
        g_zone_cameraBoundsT[s] = 0;
        g_zone_cameraBoundsB[s] = world_h_px;

        g_zone_playerBoundsL[s] = ZONE_TO_FIXED(g_zone_cameraBoundsL[s]);
        g_zone_playerBoundsR[s] = ZONE_TO_FIXED(g_zone_cameraBoundsR[s]);
        g_zone_playerBoundsT[s] = ZONE_TO_FIXED(g_zone_cameraBoundsT[s]);
        g_zone_playerBoundsB[s] = ZONE_TO_FIXED(g_zone_cameraBoundsB[s]);

        g_zone_deathBoundary[s] = ZONE_TO_FIXED(g_zone_cameraBoundsB[s]);
    }
    /* Mark bounds populated so the GHZ camera clamp switches off its
     * legacy world-size fallback. */
    g_zone_bounds_valid = 1;
}

void zone_reset_bounds(void)
{
    for (int s = 0; s < MANIA_ZONE_PLAYER_COUNT; ++s) {
        g_zone_cameraBoundsL[s] = 0;
        g_zone_cameraBoundsR[s] = 0;
        g_zone_cameraBoundsT[s] = 0;
        g_zone_cameraBoundsB[s] = 0;
        g_zone_playerBoundsL[s] = 0;
        g_zone_playerBoundsR[s] = 0;
        g_zone_playerBoundsT[s] = 0;
        g_zone_playerBoundsB[s] = 0;
        g_zone_deathBoundary[s] = 0;
    }
    g_zone_bounds_valid = 0;
}

/* Decomp Zone.c:506-512 (mechanical; see Zone.h header note for the
 * 1:1 source). The two reads of (px-ox)>>8 / (py-oy)>>8 mirror the
 * decomp's `x`/`y` locals; the products use rsdk_sin256/cos256 (Q8.8)
 * exactly as RSDK.Sin256/Cos256 do upstream. */
void Zone_RotateOnPivot(int *px, int *py, int ox, int oy, int angle)
{
    int x = (*px - ox) >> 8;
    int y = (*py - oy) >> 8;
    *px = ox + y * rsdk_sin256(angle) + x * rsdk_cos256(angle);
    *py = oy + y * rsdk_cos256(angle) - x * rsdk_sin256(angle);
}
