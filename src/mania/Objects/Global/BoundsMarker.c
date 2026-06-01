/* ---------------------------------------------------------------------
 * Phase 2.4g.2 (Task #153) — BoundsMarker object.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Global_BoundsMarker.c`.
 * Original: Christian Whitehead / Simon Thomley / Hunter Bridges;
 * decomp by Rubberduckycooly & RMGRich.
 *
 * Translation notes vs decomp:
 *
 *   - `for (p < Player->playerCount) { player = RSDK_GET_ENTITY(p, Player);
 *      BoundsMarker_ApplyBounds(player, self, ...) }` (decomp _Update L16-19,
 *     _Create L41-44): the Saturn build has exactly one player (the bespoke
 *     `player_t g_ghz_player`, pinned via `g_ghz_player_addr`). The loop
 *     collapses to a single guarded reference with playerID = 0. Same
 *     precedent as InvisibleBlock.c (2.4g.1).
 *
 *   - `Player_CheckValidState(player) || player->classID==DebugMode->classID`
 *     (decomp _ApplyBounds L55): no DebugMode on Saturn; the bespoke player
 *     is valid for the whole GHZ-active window. The gate becomes
 *     `mania_is_ghz_active()` (Game.h:250, the same-TU state-machine signal,
 *     LTO-safe per memory/sync-load-eliminates-cross-tu-volatile.md).
 *
 *   - `Zone->cameraBoundsB[playerID] = ...` (decomp _ApplyBounds L59-81):
 *     Saturn has no live ObjectZone instance, so member accesses become the
 *     free-standing `g_zone_cameraBoundsB[playerID]` arrays (Zone.h subset).
 *     FROM_FIXED(x) = (x) >> 16 (RSDK Definitions.hpp).
 *
 *   - `EntityCamera *camera = player->camera; camera->boundsL = ...`
 *     (decomp _ApplyBounds L88-95, the setPos=true branch): the Saturn
 *     camera is the GHZ scroll camera in scene_ghz.c, clamped from the same
 *     g_zone_cameraBounds* globals (scene_ghz.c ghz_set_camera). Writing the
 *     Zone globals IS writing the camera bounds on Saturn — the decomp's
 *     copy into EntityCamera fields is therefore implicit. We mark the
 *     bounds valid so the clamp uses them.
 *
 *   - `SceneInfo->inEditor` / `globals->gameMode == MODE_COMPETITION`
 *     (decomp _Create L32-34): always false / single-player on Saturn; the
 *     vsDisable destroy path never fires.
 *
 *   - `self->updateRange.x += self->width << 15` (decomp _Create L39):
 *     ported verbatim; updateRange gates the ACTIVE_XBOUNDS culling.
 * ------------------------------------------------------------------- */

#include "BoundsMarker.h"
#include "Zone.h"

ObjectBoundsMarker *BoundsMarker = NULL;

/* Bespoke single-player handle (Game.c), link-time-constant pointer — same
 * as InvisibleBlock.c. */
extern player_t *const g_ghz_player_addr;

/* QA landmark (2.4g.2 gate). Count of BoundsMarker entities created into
 * RSDK slots this scene; diagnostics only, never read by gameplay. */
__attribute__((used)) volatile int g_ghz_boundsmarker_spawned = 0;

/* FROM_FIXED (RSDK Definitions.hpp): Q16.16 -> integer pixels. */
#define BM_FROM_FIXED(x)  ((x) >> 16)

/* decomp _ApplyBounds BoundsMarker.c:51-98. Saturn single-player so
 * playerID is always 0; `setPos` writes the camera bounds (implicit via
 * the Zone globals on Saturn). */
void BoundsMarker_ApplyBounds(player_t *player, EntityBoundsMarker *marker, bool32 setPos)
{
    const int playerID = 0;

    /* Player_CheckValidState || DebugMode -> GHZ-active window on Saturn. */
    if (!mania_is_ghz_active())
        return;

    /* abs(marker->position.x - player->position.x) < marker->width.
     * marker->width is already the (<<15) value set in _Create; player
     * x is Q16.16 (xpos). */
    int32 dx = (int32)marker->position.x - player->xpos;
    if (dx < 0) dx = -dx;
    if (dx < marker->width) {
        switch (marker->type) {
            case BOUNDSMARKER_ANY_Y:
                g_zone_playerBoundsB[playerID] = marker->position.y;
                g_zone_cameraBoundsB[playerID] = BM_FROM_FIXED(g_zone_playerBoundsB[playerID]);
                g_zone_deathBoundary[playerID] = marker->position.y;
                break;

            case BOUNDSMARKER_ABOVE_Y:
                if (player->ypos < marker->position.y - (marker->offset << 16)) {
                    g_zone_playerBoundsB[playerID] = marker->position.y;
                    g_zone_cameraBoundsB[playerID] = BM_FROM_FIXED(g_zone_playerBoundsB[playerID]);
                    g_zone_deathBoundary[playerID] = marker->position.y;
                }
                break;

            case BOUNDSMARKER_BELOW_Y:
                if (player->ypos > marker->position.y + (marker->offset << 16)) {
                    g_zone_playerBoundsT[playerID] = marker->position.y;
                    g_zone_cameraBoundsT[playerID] = BM_FROM_FIXED(g_zone_playerBoundsT[playerID]);
                }
                break;

            case BOUNDSMARKER_BELOW_Y_ANY:
                g_zone_playerBoundsT[playerID] = marker->position.y;
                g_zone_cameraBoundsT[playerID] = BM_FROM_FIXED(g_zone_playerBoundsT[playerID]);
                break;

            default: break;
        }
        /* Any write makes the bounds usable by the GHZ camera clamp. */
        g_zone_bounds_valid = 1;
    }

    if (setPos) {
        /* decomp copies Zone->cameraBounds* into player->camera->bounds*.
         * On Saturn the GHZ camera reads the Zone globals directly, so this
         * is the marker-of-validity rather than a copy. */
        g_zone_bounds_valid = 1;
    }
}

__attribute__((used)) void BoundsMarker_Update(void)
{
    RSDK_THIS(BoundsMarker);

    /* foreach (p < playerCount) ApplyBounds(player, self, false). */
    BoundsMarker_ApplyBounds(g_ghz_player_addr, self, false);
}

void BoundsMarker_LateUpdate(void) {}

void BoundsMarker_StaticUpdate(void) {}

void BoundsMarker_Draw(void) {}

__attribute__((used)) void BoundsMarker_Create(void *data)
{
    RSDK_THIS(BoundsMarker);
    (void)data;

    /* SceneInfo->inEditor always false; vsDisable+competition path never
     * fires (single-player Saturn). decomp _Create L37-44. */
    self->active = ACTIVE_XBOUNDS;
    self->width  = self->width ? (self->width << 15) : (48 << 15);
    self->updateRange.x += self->width << 15;

    /* foreach (p < playerCount) ApplyBounds(player, self, true). */
    BoundsMarker_ApplyBounds(g_ghz_player_addr, self, true);

    ++g_ghz_boundsmarker_spawned;
}

void BoundsMarker_StageLoad(void)
{
    /* decomp _StageLoad is empty (BoundsMarker.c:49). Keeping it a no-op
     * means the all-class StageLoad pass triggers zero asset I/O for this
     * class (2.4g.1 zero-boot-cost guarantee). */
}
