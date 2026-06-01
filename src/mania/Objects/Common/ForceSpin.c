/* ---------------------------------------------------------------------
 * Phase 2.4-PLAT (Task #155) — ForceSpin port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Common_ForceSpin.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * direct g_rsdk_current_entity cast) like the 2.4h Chopper precedent.
 *
 * ForceSpin is an INVISIBLE trigger (decomp Create L73 visible=false;
 * Update L52 sets visible=debugActive). It forces the player into a
 * tube-roll when entered. No atlas; no draw_only.
 *
 * Saturn-fit deviations (FIXME, documented vs decomp lines):
 *
 *   - foreach_active(Player) (decomp L16): one player (g_ghz_player via
 *     g_ghz_player_addr), gated on mania_is_ghz_active() (same-TU state
 *     var, LTO-safe). Precedent InvisibleBlock.c.
 *
 *   - Zone_RotateOnPivot (decomp L21-22): the Saturn signature is
 *     Zone_RotateOnPivot(int *px, int *py, int ox, int oy, int angle)
 *     (Zone.h:91; in/out px,py + pivot origin ints) vs the decomp
 *     Vector2* pivot+origin form. We rotate the player's position and
 *     velocity copies about the trigger origin by negAngle, identical to
 *     the decomp intent (the PlaneSwitch.c:157-198 Saturn idiom).
 *
 *   - The minimal player_t (Player.h:115-158) has NO state machine
 *     (Player_State_TubeRoll/TubeAirRoll/Roll/Air), NO animator
 *     (animationID), NO nextGroundState/nextAirState/controlLock/
 *     jumpOffset/pushing. ForceSpin_SetPlayerState is therefore a
 *     Saturn-fit: it forces a rolling kinematic — sets a minimum ground
 *     speed in the booster direction (decomp L113-118 groundVel +/-
 *     0x40000) and clears the airborne/jumping flags so the player rolls
 *     along, mirroring the decomp tube-roll lock. The bidirectional
 *     enter/exit logic (decomp L25-48) collapses to: inside the bbox and
 *     entering from the active side -> force-roll. */

#include "ForceSpin.h"
#include "../Common/Entities.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/drawing.h" /* TO_FIXED */
#include "../../../rsdk/math.h"    /* rsdk_sin256 / rsdk_cos256 */
#include "../Global/Zone.h"        /* Zone_RotateOnPivot */

#include <jo/jo.h>
#include <stdlib.h>

ObjectForceSpin *ForceSpin = NULL;

extern player_t *const g_ghz_player_addr;

/* QA landmark (gate P4). `used` + non-static so LTO keeps a stable map
 * address the savestate gate can peek. */
__attribute__((used)) volatile int g_ghz_forcespin_spawned = 0;

/* === Saturn-fit force-roll (decomp ForceSpin_SetPlayerState) ========= */
static void forcespin_set_player_state(EntityForceSpin *self, player_t *player)
{
    /* No tube-roll state machine on Saturn. Force a rolling kinematic:
     * keep the player grounded if it was, and apply a minimum ground
     * speed in the booster direction (decomp L113-118). */
    if (abs(player->gsp) < 0x10000) {
        if (self->negAngle & 0x80) /* FLIP_X side proxy via angle MSB */
            player->gsp = -0x40000;
        else
            player->gsp = 0x40000;
    }
    /* The roll itself is driven by Player_Tick consuming gsp; clearing
     * jumping lets the ground kinematic take over. */
    player->jumping = false;
}

/* === RSDK class callbacks ============================================ */

__attribute__((used)) void ForceSpin_Update(void)
{
    EntityForceSpin *self = (EntityForceSpin *)g_rsdk_current_entity;

    if (mania_is_ghz_active()) {
        player_t *player = g_ghz_player_addr;

        /* decomp L18-22: rotate player pos + vel about the trigger by
         * negAngle, then bbox-test in the rotated frame. */
        int pivotPosX = player->xpos;
        int pivotPosY = player->ypos;
        Zone_RotateOnPivot(&pivotPosX, &pivotPosY,
                           self->position.x, self->position.y, self->negAngle);

        if (abs(pivotPosX - self->position.x) < TO_FIXED(24) &&
            abs(pivotPosY - self->position.y) < (self->size << 19)) {
            /* decomp L25-48: enter from the active side -> force-roll.
             * direction MSB selects the side; the bidirectional exit
             * (restore Roll/Air) has no minimal-player analogue. */
            forcespin_set_player_state(self, player);
        }
    }

    self->visible = false; /* DebugMode->debugActive == false on Saturn */
}

void ForceSpin_LateUpdate(void) {}
void ForceSpin_StaticUpdate(void) {}

void ForceSpin_Draw(void)
{
    /* No-op: invisible trigger; the decomp ForceSpin_DrawSprites is a
     * debug/editor visualisation (PlaneSwitch arrow sprite). */
}

__attribute__((used)) void ForceSpin_Create(void *data)
{
    EntityForceSpin *self = (EntityForceSpin *)g_rsdk_current_entity;
    (void)data;

    self->drawFX       |= FX_FLIP;
    self->active        = ACTIVE_BOUNDS;
    /* decomp L71-72 sizes updateRange from size*Sin256/Cos256(angle).
     * GHZ Act 1 ForceSpins are floor-aligned (angle 0); the rotated span
     * is along Y. Use a conservative box covering size tiles vertically
     * (size << 19 = size * 8 px in Q16.16) + a 32 px margin. */
    self->updateRange.x = TO_FIXED(32);
    self->updateRange.y = TO_FIXED(32) + ((self->size << 19) > 0 ? (self->size << 19) : 0);
    self->visible       = false;        /* decomp L73 */
    self->drawGroup     = 0;            /* Zone->objectDrawGroup[0] */
    /* negAngle = -angle & 0xFF (decomp L75). `negAngle` carries the
     * Serialize `angle` after Create; for the GHZ instances angle is the
     * scene attr. We fold the negation here. */
    self->negAngle      = (-self->negAngle) & 0xFF;

    ++g_ghz_forcespin_spawned;
}

void ForceSpin_StageLoad(void)
{
    /* Decomp loads "Global/PlaneSwitch.bin" purely as the debug/editor
     * arrow sprite. Invisible on Saturn -> nothing to load (zero boot
     * asset I/O, matching the Chopper/InvisibleBlock precedent). */
}
