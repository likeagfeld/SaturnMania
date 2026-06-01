/* ---------------------------------------------------------------------
 * Phase 2.4g.3 (Task #153) — PlaneSwitch object.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Global_PlaneSwitch.c`.
 * Original: Christian Whitehead / Simon Thomley / Hunter Bridges;
 * decomp by Rubberduckycooly & RMGRich.
 *
 * THIRD (last + largest) GHZ entity ported onto the RSDK entity engine
 * (after InvisibleBlock 2.4g.1 + BoundsMarker 2.4g.2). 106 GHZ Act 1
 * instances spawn into RSDK slots via the class-agnostic scene-spawn
 * pipeline. PlaneSwitch writes player->collisionPlane (A/B path select)
 * when the player crosses the marker's X inside its size window; the
 * two-plane tile-collision bridge (Player.h sms_world_t raw_alt +
 * active_path; Player_Tick selects the path from collisionPlane) makes
 * that write choose which collision path the surface probe reads.
 *
 * Translation notes vs decomp:
 *
 *   - `foreach_active(Player, player)` (decomp _Update L16-19): the Saturn
 *     build has exactly one player (bespoke `player_t g_ghz_player`, pinned
 *     via `g_ghz_player_addr`). The loop collapses to a single guarded
 *     reference gated on `mania_is_ghz_active()` (the same-TU state signal,
 *     LTO-safe per memory/sync-load-eliminates-cross-tu-volatile.md). Same
 *     precedent as InvisibleBlock.c / BoundsMarker.c.
 *
 *   - `Entity *other` fields (decomp _CheckCollisions L83-110): the Saturn
 *     player_t mirrors EntityPlayer field-by-field —
 *       other->position.x/y -> player->xpos / player->ypos   (Q16.16)
 *       other->velocity.x/y -> player->xsp  / player->ysp    (Q16.16)
 *       other->onGround     -> player->onGround
 *       other->collisionPlane-> player->collisionPlane (0=A, 1=B)
 *     The entity self->position / self->velocity are RSDK base fields
 *     (Game.h RSDK_ENTITY: position@+0, velocity@+16) — read as int32 x,y.
 *
 *   - `Zone->playerDrawGroup[0/1]` + the `switchdrawGroup` branch (decomp
 *     L18, L95-100/104-109): the Saturn player is NOT an RSDK-drawn entity
 *     yet (it renders through the bespoke GHZ sprite path), so it has no
 *     drawGroup field. We pass `switchdrawGroup = false`; the load-bearing
 *     side effect — the `other->collisionPlane` write — is preserved
 *     exactly. The drawGroup swap lands when the full EntityPlayer
 *     migration arrives (later phase).
 *
 *   - `self->visible = DebugMode->debugActive` (decomp _Update L21): no
 *     debug overlay on Saturn -> visible stays false. PlaneSwitch is
 *     invisible in normal play, so _Draw / _DrawSprites are no-ops.
 *
 *   - `RSDK.SetSpriteAnimation(... PlaneSwitch.bin ...)` (decomp _Create
 *     L34 / _StageLoad L47): the sprite is only ever drawn in DebugMode,
 *     which doesn't exist on Saturn, so we load no asset (zero boot cost)
 *     and skip the animator setup. `negAngle` / `updateRange` math is
 *     ported verbatim so the ACTIVE_BOUNDS cull window matches the decomp.
 *
 *   - `Zone_RotateOnPivot(Vector2*, Vector2*, angle)` (decomp _CheckColl
 *     L88-89): ported to the int-pointer / int-pointer Saturn signature
 *     (Zone.c:92-98).
 * ------------------------------------------------------------------- */

#include "PlaneSwitch.h"
#include "Zone.h"                 /* Zone_RotateOnPivot */
#include "../../../rsdk/math.h"   /* rsdk_sin256 / rsdk_cos256 (Q8.8) */

ObjectPlaneSwitch *PlaneSwitch = NULL;

/* Bespoke single-player handle (Game.c), link-time-constant pointer —
 * same as InvisibleBlock.c / BoundsMarker.c. */
extern player_t *const g_ghz_player_addr;

/* QA landmarks (Phase 2.4g.3 gate P4). `used` + non-static so LTO keeps a
 * stable linker-map address the savestate gate can peek:
 *   g_ghz_planeswitch_spawned   = count of PlaneSwitch entities created
 *                                 into RSDK slots this scene.
 *   g_ghz_planeswitch_lastplane = the collisionPlane value (0 or 1) the
 *                                 last frame any PlaneSwitch wrote it.
 * Never read by gameplay — diagnostics only. */
__attribute__((used)) volatile int g_ghz_planeswitch_spawned   = 0;
__attribute__((used)) volatile int g_ghz_planeswitch_lastplane = 0;

/* TO_FIXED (RSDK Definitions.hpp): integer -> Q16.16. */
#define PS_TO_FIXED(x) ((x) << 16)

/* int abs without pulling <stdlib.h> into the entity TU. */
static inline int ps_abs(int v) { return v < 0 ? -v : v; }

/* __attribute__((used)) defeats GCC 8.2 whole-program LTO symbol
 * elimination so the gate (qa_phase2_4g3_gate.py P1) can locate the
 * registered Update/Create callbacks by name in game.map. Same precedent
 * as InvisibleBlock.c / BoundsMarker.c. */
__attribute__((used)) void PlaneSwitch_Update(void)
{
    RSDK_THIS(PlaneSwitch);

    /* foreach_active(Player, player) -> single bespoke player, gated on the
     * GHZ-active window. decomp _Update L16-19. */
    if (mania_is_ghz_active()) {
        player_t *player = g_ghz_player_addr;
        PlaneSwitch_CheckCollisions(self, player, self->flags, self->size,
                                    false /* switchdrawGroup: no player
                                             drawGroup on Saturn yet */,
                                    0, 0);
    }

    /* DebugMode->debugActive is always false on Saturn. */
    self->visible = false;
}

void PlaneSwitch_LateUpdate(void) {}

void PlaneSwitch_StaticUpdate(void) {}

void PlaneSwitch_Draw(void) { PlaneSwitch_DrawSprites(); }

__attribute__((used)) void PlaneSwitch_Create(void *data)
{
    RSDK_THIS(PlaneSwitch);
    (void)data;

    /* SceneInfo->inEditor always false on Saturn. decomp _Create L36-44.
     * RSDK.SetSpriteAnimation skipped (no DebugMode sprite on Saturn). */
    self->active = ACTIVE_BOUNDS;

    /* updateRange = TO_FIXED(32) + abs(size * Sin/Cos256(angle) << 11).
     * self->angle is the RSDK base entity field (Game.h RSDK_ENTITY
     * angle@+32); RSDK.Sin256/Cos256 -> rsdk_sin256/rsdk_cos256 (Q8.8). */
    self->updateRange.x = PS_TO_FIXED(32) + ps_abs((self->size * rsdk_sin256(self->angle)) << 11);
    self->updateRange.y = PS_TO_FIXED(32) + ps_abs((self->size * rsdk_cos256(self->angle)) << 11);
    self->visible       = false;
    self->drawGroup     = 0; /* Zone->objectDrawGroup[0]; inert (invisible) */
    self->negAngle      = -self->angle & 0xFF;

    ++g_ghz_planeswitch_spawned;
}

void PlaneSwitch_StageLoad(void)
{
    /* decomp loads "Global/PlaneSwitch.bin" purely for the DebugMode
     * visualisation. No debug overlay on Saturn -> no-op (the all-class
     * StageLoad pass triggers zero asset I/O for this class; the 2.4g
     * zero-boot-cost guarantee). */
}

void PlaneSwitch_DrawSprites(void)
{
    /* decomp draws the plane-switch chevrons only when DebugMode is active;
     * PlaneSwitch is invisible in normal play, so this is a no-op. */
}

/* decomp _CheckCollisions PlaneSwitch.c:81-113 (the core). Rotates the
 * player's position+velocity into the switch's local (un-angled) frame via
 * negAngle, AABB-tests the 24px-wide / size-tall window, then selects the
 * collision plane from the flags by which side of the switch the player is
 * approaching from.
 *
 * Saturn substitution: `other` is the bespoke player_t (not an RSDK
 * Entity*); pivotPos/pivotVel are local int x,y pairs (decomp Vector2).
 * Zone_RotateOnPivot takes the in/out coord pair + the pivot origin. */
void PlaneSwitch_CheckCollisions(EntityPlaneSwitch *self, player_t *other,
                                 int32 flags, int32 size, bool32 switchDrawGroup,
                                 uint8 low, uint8 high)
{
    (void)low;
    (void)high;

    /* self->position / self->velocity are the RSDK base entity fields
     * (Game.h RSDK_ENTITY: position@+0, velocity@+16). */
    int sx = self->position.x;
    int sy = self->position.y;
    int svx = self->velocity.x;
    int svy = self->velocity.y;

    int pivotPosX = other->xpos;
    int pivotPosY = other->ypos;
    int pivotVelX = other->xsp;
    int pivotVelY = other->ysp;

    Zone_RotateOnPivot(&pivotPosX, &pivotPosY, sx, sy, self->negAngle);
    Zone_RotateOnPivot(&pivotVelX, &pivotVelY, svx, svy, self->negAngle);

    if (!self->onPath || other->onGround) {
        if (ps_abs(pivotPosX - sx) < PS_TO_FIXED(24)
            && ps_abs(pivotPosY - sy) < (size << 19)) {
            if (pivotPosX + pivotVelX >= sx) {
                other->collisionPlane = (uint8)((flags >> 3) & 1); /* plane bit */
                if (switchDrawGroup) {
                    /* player has no drawGroup on Saturn yet; preserved for
                     * the future EntityPlayer migration. decomp L95-100. */
                }
            }
            else {
                other->collisionPlane = (uint8)((flags >> 1) & 1); /* plane bit */
                if (switchDrawGroup) {
                    /* decomp L104-109 (drawGroup swap) — inert on Saturn. */
                }
            }
            g_ghz_planeswitch_lastplane = other->collisionPlane;
        }
    }
}
