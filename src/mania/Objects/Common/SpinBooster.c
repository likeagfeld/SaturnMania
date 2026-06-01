/* ---------------------------------------------------------------------
 * Phase 2.4-PLAT (Task #155) — SpinBooster port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Common_SpinBooster.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * direct g_rsdk_current_entity cast) like the 2.4h Chopper precedent.
 *
 * SpinBooster is an INVISIBLE trigger (decomp Create L95 visible=false;
 * Update L58 sets visible=debugActive). It force-rolls + boosts the
 * player along a direction. No atlas; no draw.
 *
 * Saturn-fit deviations (FIXME, documented vs decomp lines):
 *
 *   - foreach_active(Player) (decomp L18): one player (g_ghz_player via
 *     g_ghz_player_addr), gated on mania_is_ghz_active() (same-TU state
 *     var, LTO-safe). Precedent InvisibleBlock.c. activePlayers
 *     (decomp 1<<GetEntitySlot) collapses to bit 0.
 *
 *   - Zone_RotateOnPivot (decomp L23): the Saturn signature is
 *     Zone_RotateOnPivot(int *px, int *py, int ox, int oy, int angle)
 *     (Zone.h:91; in/out px,py + pivot origin ints) vs the decomp
 *     Vector2* pivot+origin form. We rotate the player's position copy
 *     about the trigger origin by negAngle (the PlaneSwitch.c Saturn
 *     idiom).
 *
 *   - The minimal player_t (Player.h:115-158) has NO state machine
 *     (Player_State_TubeRoll/TubeAirRoll/Roll/Air), NO animator
 *     (animationID), NO nextGroundState/nextAirState/controlLock/
 *     tileCollisions/collisionMode/jumpOffset/pushing. The auto-grip
 *     CMODE wall/roof reattach (decomp GetRollDir/HandleRollDir,
 *     L102-300) has no minimal-player analogue and is DROPPED (GHZ Act 1
 *     boosters are floor-aligned; autoGrip defaults 0 in the scene
 *     attrs). SpinBooster_HandleForceRoll / SpinBooster_ApplyRollVelocity
 *     collapse to a Saturn-fit force-roll: apply a minimum ground speed
 *     in the booster direction and add the boostPower along the booster
 *     angle (decomp ApplyRollVelocity onGround branch L305-314 +
 *     HandleForceRoll groundVel floor L485-486), mirroring the decomp
 *     intent (force-roll + directional boost). */

#include "SpinBooster.h"
#include "../Common/Entities.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/drawing.h" /* FLIP_NONE/X/Y/XY + TO_FIXED */
#include "../../../rsdk/math.h"    /* rsdk_sin256 / rsdk_cos256 */
#include "../Global/Zone.h"        /* Zone_RotateOnPivot */

#include <jo/jo.h>
#include <stdlib.h>

ObjectSpinBooster *SpinBooster = NULL;

extern player_t *const g_ghz_player_addr;

/* QA landmark (gate P4). `used` + non-static so LTO keeps a stable map
 * address the savestate gate can peek. */
__attribute__((used)) volatile int g_ghz_spinbooster_spawned = 0;

/* === Saturn-fit roll velocity (decomp SpinBooster_ApplyRollVelocity) === */
static void spinbooster_apply_roll_velocity(EntitySpinBooster *self, player_t *player)
{
    /* decomp L305-314: onGround branch. entAng/plrAng sign products pick
     * the boost direction along the booster angle; power is boostPower in
     * Q16.16 (boostPower << 15 == boostPower * 0.5 px/frame per unit, but
     * the decomp shifts <<15 because boostPower is a small integer). */
    if (player->onGround) {
        int32 entAng = rsdk_cos256(self->angle) + rsdk_sin256(self->angle);
        int32 plrAng = rsdk_cos256(player->angle) - rsdk_sin256(player->angle);
        int32 power  = (self->boostPower << 15) * ((plrAng > 0) - (plrAng < 0))
                                                * ((entAng > 0) - (entAng < 0));
        if (self->boostPower >= 0)
            player->gsp += power;
        else
            player->gsp = power;
    }
    else {
        /* decomp L316-326: airborne branch adds along the booster angle. */
        int32 x = (0x80 * rsdk_cos256(self->angle)) * self->boostPower;
        int32 y = (-0x80 * rsdk_sin256(self->angle)) * self->boostPower;
        if (self->boostPower >= 0) {
            player->xsp += x;
            player->ysp += y;
        }
        else {
            player->xsp = x;
            player->ysp = y;
        }
    }
}

/* === Saturn-fit force-roll (decomp SpinBooster_HandleForceRoll) ====== */
static void spinbooster_handle_force_roll(EntitySpinBooster *self, player_t *player)
{
    /* No tube-roll state machine on Saturn. Force a rolling kinematic:
     * clear jumping so the ground kinematic takes over, give a minimum
     * ground speed in the booster direction (decomp L485-486), then add
     * the directional boost. */
    player->jumping = false;

    if (abs(player->gsp) < TO_FIXED(1))
        player->gsp = (self->direction & FLIP_X) ? -TO_FIXED(4) : TO_FIXED(4);

    spinbooster_apply_roll_velocity(self, player);
}

/* === RSDK class callbacks ============================================ */

__attribute__((used)) void SpinBooster_Update(void)
{
    EntitySpinBooster *self = (EntitySpinBooster *)g_rsdk_current_entity;

    int32 negAngle = -self->angle & 0xFF;

    if (mania_is_ghz_active()) {
        player_t *player = g_ghz_player_addr;
        uint8 playerID   = 1; /* single player -> bit 0 (decomp 1<<slot) */

        /* decomp L22-23: rotate player pos about the trigger by negAngle,
         * then bbox-test in the rotated frame. */
        int pivotPosX = player->xpos;
        int pivotPosY = player->ypos;
        Zone_RotateOnPivot(&pivotPosX, &pivotPosY,
                           self->position.x, self->position.y, negAngle);

        if (abs(pivotPosX - self->position.x) < TO_FIXED(24) &&
            abs(pivotPosY - self->position.y) < (self->size << 19)) {
            /* decomp L26-48: enter from the active side -> force-roll;
             * the exit side restores Roll/Air (no minimal-player
             * analogue, so the exit branch only clears the active bit). */
            if (pivotPosX >= self->position.x) {
                if (!(playerID & self->activePlayers)) {
                    spinbooster_handle_force_roll(self, player);
                    self->activePlayers |= playerID;
                }
            }
            else {
                self->activePlayers &= ~playerID;
            }
        }
        else {
            /* decomp L51-54: outside the bbox, latch the side bit. */
            if (pivotPosX >= self->position.x)
                self->activePlayers |= playerID;
            else
                self->activePlayers &= ~playerID;
        }
    }

    self->visible = false; /* DebugMode->debugActive == false on Saturn */
}

void SpinBooster_LateUpdate(void) {}
void SpinBooster_StaticUpdate(void) {}

void SpinBooster_Draw(void)
{
    /* No-op: invisible trigger; the decomp SpinBooster_DrawSprites is a
     * debug/editor visualisation (PlaneSwitch arrow sprite). */
}

__attribute__((used)) void SpinBooster_Create(void *data)
{
    EntitySpinBooster *self = (EntitySpinBooster *)g_rsdk_current_entity;
    (void)data;

    self->drawFX       |= FX_FLIP;
    self->activePlayers = 0;

    /* decomp L81-86: angle from the direction flip enum. */
    switch (self->direction) {
        case FLIP_NONE: self->angle = 0x00; break;
        case FLIP_X:    self->angle = 0x40; break;
        case FLIP_Y:    self->angle = 0x80; break;
        case FLIP_XY:   self->angle = 0xC0; break;
        default:        self->angle = 0x00; break;
    }

    self->active = ACTIVE_BOUNDS;

    /* decomp L90-94: updateRange from size*|Sin256/Cos256(angle)| + a
     * 0x200000 (32 px) margin. rsdk_sin256/cos256 return Q8.8 [-256..256],
     * matching the decomp LUT scale. */
    self->updateRange.x = (self->size * abs(rsdk_sin256(self->angle))) << 11;
    self->updateRange.x += 0x200000;
    self->updateRange.y = (self->size * abs(rsdk_cos256(self->angle))) << 11;
    self->updateRange.y += 0x200000;

    self->visible   = false;        /* decomp L95 */
    self->drawGroup = 0;            /* Zone->objectDrawGroup[0] */

    ++g_ghz_spinbooster_spawned;
}

void SpinBooster_StageLoad(void)
{
    /* Decomp loads "Global/PlaneSwitch.bin" purely as the debug/editor
     * arrow sprite. Invisible on Saturn -> nothing to load (zero boot
     * asset I/O, matching the Chopper/InvisibleBlock precedent). */
}
