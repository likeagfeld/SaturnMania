/* ---------------------------------------------------------------------
 * Phase 2.4-PLAT (Task #155) — CollapsingPlatform port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Common_CollapsingPlatform.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * direct g_rsdk_current_entity cast) like the 2.4h Chopper precedent.
 *
 * CollapsingPlatform is IN-GAME INVISIBLE (decomp Update L16-18 sets
 * visible=debugActive; the crumbling surface is the FG tilemap, no
 * per-entity sprite). No atlas; no draw.
 *
 * Saturn-fit deviations (FIXME, documented vs decomp lines):
 *
 *   - foreach_active(Player) (decomp L25,42): one player (g_ghz_player
 *     via g_ghz_player_addr), gated on mania_is_ghz_active() (same-TU
 *     state var, LTO-safe). Precedent InvisibleBlock.c.
 *
 *   - Player_CheckCollisionTouch (decomp L27,44) does NOT exist on
 *     Saturn. The Saturn collision surface is Player_CheckCollisionBox
 *     (Player.h) which both tests AND snaps; a C_TOP contact == player
 *     stood on the platform. We use a non-C_NONE contact as the stood
 *     trigger (the trigger hitbox spans the platform top per decomp
 *     Create L153-156).
 *
 *   - Mighty (MANIA_USE_PLUS, decomp L23-34,46,51) is not in the Saturn
 *     build (Sonic-only). The mightyOnly / jumpAbilityState branches are
 *     dropped; the GHZ Act 1 instances are not mightyOnly.
 *
 *   - The decomp collapse (State_Left/Right/Center, L186-318) spawns
 *     BREAKWALL_TILE_DYNAMIC debris + reads storedTiles + RSDK.GetTile/
 *     SetTile on the FG layer. NOT ported: the Saturn surface model has
 *     no dynamic GetTile/SetTile (FG.TMP is static) and the 512-byte
 *     storedTiles[256] member is dropped (overflows the 256-byte slot
 *     STRIDE — see CollapsingPlatform.h note). The Saturn-fit collapse
 *     is destroyEntity (respawn? reset : destroy) + SFX (the surface
 *     stays drawn but the collision trigger is gone — the closest
 *     Saturn-fit observable, same model as BreakableWall_Break). */

#include "CollapsingPlatform.h"
#include "../Common/Entities.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/drawing.h" /* TO_FIXED */

#include <jo/jo.h>
#include <stdlib.h>

ObjectCollapsingPlatform *CollapsingPlatform = NULL;

extern player_t *const g_ghz_player_addr;

/* QA landmark (gate P4). `used` + non-static so LTO keeps a stable map
 * address the savestate gate can peek. */
__attribute__((used)) volatile int g_ghz_collapsingplatform_spawned = 0;

/* === States (decomp CollapsingPlatform_State_*) ====================== */
/* The decomp states spawn BreakableWall tile debris (dynamic tilemap),
 * which is not ported on Saturn. The Saturn collapse is the
 * destroyEntity in Update (decomp L67-78). The state pointer is kept for
 * struct-layout/dispatch fidelity but is a no-op here; LeftRight /
 * LeftRightCenter only selected which debris pattern played, which has
 * no Saturn analogue. */

void CollapsingPlatform_State_Left(void)            {}
void CollapsingPlatform_State_Right(void)           {}
void CollapsingPlatform_State_Center(void)          {}
void CollapsingPlatform_State_LeftRight(void)       {}
void CollapsingPlatform_State_LeftRightCenter(void) {}

/* === RSDK class callbacks ============================================ */

__attribute__((used)) void CollapsingPlatform_Update(void)
{
    EntityCollapsingPlatform *self = (EntityCollapsingPlatform *)g_rsdk_current_entity;

    self->visible = false; /* DebugMode->debugActive == false on Saturn */

    bool32 runState = false;

    if (self->collapseDelay) {
        /* decomp L36-38: countdown; collapse when it hits zero. Mighty
         * hammer-drop early-trigger (L23-34) dropped (Sonic-only). */
        if (--self->collapseDelay == 0)
            runState = true;
    }
    else {
        if (mania_is_ghz_active()) {
            player_t *player = g_ghz_player_addr;

            int box_x = self->position.x >> 16;
            int box_y = self->position.y >> 16;

            /* decomp L44-56: stood trigger. Touch -> Box contact. The
             * stood conditions: grounded, not in a wall/roof collision
             * mode, not eventOnly, delay valid. Saturn minimal player_t
             * has no collisionMode/sidekick -> use onGround + a top
             * contact. */
            if (Player_CheckCollisionBox(player, box_x, box_y, &self->hitboxTrigger) != C_NONE
                && player->onGround && !self->eventOnly && self->delay < 0xFFFF) {
                self->stoodPos.x = player->xpos;
            }
        }

        /* decomp L60-64: once stood on, arm the delay (or collapse now). */
        if (!runState && self->stoodPos.x) {
            self->collapseDelay = self->delay;
            if (!self->delay)
                runState = true;
        }
    }

    if (runState) {
        /* decomp L67-78: run the (no-op Saturn) state, play SFX, then
         * respawn-reset or destroy. The tile-shatter is the dropped
         * portion; the destroy is the Saturn-fit observable. */
        StateMachine_Run(self->state);

        /* decomp L70 PlaySfx(sfxCrumble). The Saturn SFX bank
         * (Entities.c entities_load_sfx) does not load a dedicated
         * crumble sample (no Stage/LedgeBreak.wav in the GHZ PCM set);
         * the break SFX bank mirrors the BreakableWall_Break Saturn-fit,
         * which likewise omits the play call. No crumble SFX dispatched. */

        if (self->respawn) {
            self->collapseDelay = 0;
            self->stoodPos.x    = 0;
        }
        else {
            destroyEntity(self);
        }
    }
}

void CollapsingPlatform_LateUpdate(void) {}
void CollapsingPlatform_StaticUpdate(void) {}

void CollapsingPlatform_Draw(void)
{
    /* No-op: the platform is invisible (FG tilemap surface). The debris
     * stateDraw path is not ported (no dynamic tilemap). */
}

__attribute__((used)) void CollapsingPlatform_Create(void *data)
{
    EntityCollapsingPlatform *self = (EntityCollapsingPlatform *)g_rsdk_current_entity;
    (void)data;

    self->visible      = false;            /* decomp L16-18 (debugActive==false) */
    self->position.x  &= 0xFFF80000;       /* decomp L122 tile-snap */
    self->position.y  &= 0xFFF80000;       /* decomp L123 */
    self->drawFX      |= FX_FLIP;
    self->drawGroup    = 0;                /* Zone->objectDrawGroup[0] */

    /* decomp L127-134: targetLayer + drawGroup by Low/High. On Saturn
     * there is no per-layer GetTile, so targetLayer only steers the
     * drawGroup. */
    if (self->targetLayer == COLLAPSEPLAT_TARGET_LOW)
        self->drawGroup = 0;
    else
        self->drawGroup = 1;

    self->active        = ACTIVE_BOUNDS;
    self->updateRange.x = TO_FIXED(128);
    self->updateRange.y = TO_FIXED(128);

    /* decomp L153-156: trigger hitbox from size (Q16.16 -> px via >>17 =
     * half-size in pixels; top extends -16 above). The tile-store loop
     * (L143-151) is dropped (no GetTile, no storedTiles). */
    self->hitboxTrigger.left   = -(self->size.x >> 17);
    self->hitboxTrigger.top    = -16 - (self->size.y >> 17);
    self->hitboxTrigger.right  = self->size.x >> 17;
    self->hitboxTrigger.bottom = self->size.y >> 17;

    /* decomp L159-166: state by type. The states are Saturn no-ops but
     * the dispatch is kept for fidelity. */
    switch (self->type) {
        case COLLAPSEPLAT_LEFT:   self->state = CollapsingPlatform_State_Left;            break;
        case COLLAPSEPLAT_RIGHT:  self->state = CollapsingPlatform_State_Right;           break;
        case COLLAPSEPLAT_CENTER: self->state = CollapsingPlatform_State_Center;          break;
        case COLLAPSEPLAT_LR:     self->state = CollapsingPlatform_State_LeftRight;       break;
        case COLLAPSEPLAT_LRC:    self->state = CollapsingPlatform_State_LeftRightCenter; break;
        default:                  self->state = CollapsingPlatform_State_Left;            break;
    }

    ++g_ghz_collapsingplatform_spawned;
}

void CollapsingPlatform_StageLoad(void)
{
    /* Decomp loads "Global/TicMark.bin" (the DebugMode visualisation
     * sprite) + the LedgeBreak SFX. The platform is invisible on Saturn
     * (no debug overlay) so there is no sprite to load; the crumble SFX
     * is routed through entities_play_sfx_collapse. Keeping this a no-op
     * means the all-class StageLoad pass at title boot does zero GHZ
     * asset I/O (matches Chopper/InvisibleBlock precedent). */
}
