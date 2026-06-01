/* ---------------------------------------------------------------------
 * Phase 2.4-PLAT (Task #155) — Bridge port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_GHZ_Bridge.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * direct g_rsdk_current_entity cast) like the 2.4h Chopper precedent.
 *
 * Bridge is the ONLY in-game-VISIBLE class in 2.4-PLAT (decomp Create
 * L86 visible=true). The planks draw from cd/BRIDGE.SP2 + cd/BRIDGE.MET
 * via the shared g_bridge_atlas (all frames per
 * memory/entity-atlas-must-ship-all-frames.md). rsdk_object_draw_all is
 * suppressed in the GHZ path, so Bridge_Draw is a no-op and the actual
 * draw is Bridge_draw_only (called from mania_ghz_draw_only) which
 * reproduces the decomp sine-depression layout (decomp Bridge_Draw
 * L50-81).
 *
 * Saturn-fit deviations (FIXME, documented vs decomp lines):
 *
 *   - foreach_active(Player) (decomp L33-40): the Saturn build has one
 *     player (g_ghz_player via g_ghz_player_addr), gated on
 *     mania_is_ghz_active() (same-TU state var, LTO-safe). The decomp
 *     uses player1 == stoodEntity identity; the single Saturn player is
 *     always "player1", so the stoodEntity sentinel logic collapses to:
 *     STOOD_NONE (-1) / STOOD_LEFT (-2) / STOOD_PLAYER (a stable
 *     non-sentinel marker). We DO NOT store an Entity* (there is no
 *     Player entity); we store the marker constant.
 *
 *   - Player_GetHitbox / Player_State_KnuxLedgePullUp (decomp L35-36):
 *     minimal player_t has no animator/state. The hitbox is the
 *     Player_FallbackHitbox (Player.h:233, {-10,-20,10,20}); there is no
 *     Knuckles ledge-pull-up on the Sonic-only Saturn build.
 *
 *   - Player_CheckCollisionTouch (decomp L186) does not exist. The
 *     Saturn collision surface is Player_CheckCollisionBox. The plank
 *     step-on is a C_TOP contact + snap to the sine plank Y (the closest
 *     Saturn-fit observable; the decomp explicitly snaps position.y to
 *     the sine surface at L191/251).
 *
 *   - RSDK.Sin512 (decomp L60,76,...) does not exist on Saturn. The
 *     plank depression uses Player_Sin8 (Player.h:207, Q1.7 signed
 *     256-entry sine). The argument is rescaled from the decomp's 512-
 *     entry index (ang range 0..512 -> 0..255) and the >>9 (divide by
 *     512) becomes >>7 (divide by 128, the Player_Sin8 amplitude).
 *
 *   - Bridge_Burn / BurningLog (decomp L42-43,110-141): fire-shield
 *     burnable planks. No fire shield on Saturn (Sonic-only, no shield
 *     state in player_t); burnOffset stays 0xFF so the burn branch is
 *     inert and BurningLog is not ported.
 * ------------------------------------------------------------------- */

#include "Bridge.h"
#include "../Common/Entities.h"
#include "../../../rsdk/entity_atlas.h"
#include "../../../rsdk/object.h"

#include <jo/jo.h>
#include <stdlib.h>

#define SCR_W 320
#define SCR_H 224

/* stoodEntity sentinel markers (decomp uses (Entity*)-1 / (Entity*)-2 +
 * the player1 pointer). Saturn has no Player entity, so we encode the
 * three states the decomp distinguishes as small constant markers. */
#define STOOD_NONE   ((void *)-1)  /* decomp (Entity*)-1 */
#define STOOD_LEFT   ((void *)-2)  /* decomp (Entity*)-2 */
#define STOOD_PLAYER ((void *)1)   /* the single Saturn player ("player1") */

ObjectBridge *Bridge = NULL;

extern player_t *const g_ghz_player_addr;

/* QA landmark (gate P4): count of Bridge entities spawned this scene.
 * `used` + non-static so LTO keeps a stable map address. */
__attribute__((used)) volatile int g_ghz_bridge_spawned = 0;

/* === Sine helper (decomp RSDK.Sin512 -> Player_Sin8 Saturn-fit) ======
 * Returns (Q1.7) * amplitude already folded; caller multiplies by depth
 * and shifts. ang_512 is the decomp 0..512 phase index. */
static inline int bridge_sin(int ang_512)
{
    /* 512-entry phase -> 256-entry Player_Sin8 (one full circle). */
    unsigned char a = (unsigned char)((ang_512 >> 1) & 0xFF);
    return Player_Sin8(a); /* Q1.7 signed, [-127..127] */
}

/* === Collision (decomp Bridge_HandleCollisions; Saturn-fit) ==========
 * Snaps the player onto the sine-depressed plank surface on a C_TOP
 * contact and updates stoodPos/depression/timer per decomp. */
static void bridge_handle_collisions(EntityBridge *self, player_t *player)
{
    int px = player->xpos;

    if (px > self->startPos && px < self->endPos) {
        /* Build a thin plank hitbox at the sine surface (decomp L159-182
         * computes hitY then a +/-8 px band). We approximate with the
         * fallback hitbox + a C_TOP test against the plank Y. */
        int distance = self->endPos - self->startPos;
        if (distance <= 0) return;

        self->stoodPos = px - self->startPos;

        /* depression = (distance>>13) * Sin512(stoodPos/distance)
         * (decomp L207/246). Player_Sin8 amplitude is /128 not /512. */
        int phase512 = ((self->stoodPos >> 8) * 512) / (distance >> 16 ? (distance >> 16) : 1);
        self->depression = (distance >> 13) * bridge_sin(phase512);

        self->bridgeDepth = (self->depression * self->timer) >> 7;

        /* Plank surface Y at the player's column (decomp L174 hitY). */
        int local512;
        int divisor;
        if (self->stoodPos <= (self->endPos - self->startPos - self->stoodPos)) {
            divisor = self->stoodPos ? self->stoodPos : 1;
            local512 = ((px - self->startPos) << 7);
        } else {
            divisor = (self->endPos - self->startPos - self->stoodPos);
            if (divisor == 0) divisor = 1;
            local512 = ((self->endPos - px) << 7);
        }
        int phaseB = (local512 / divisor) & 0x1FF;
        int hitY = ((self->bridgeDepth * bridge_sin(phaseB)) >> 7);

        /* Box centred on the plank surface; a C_TOP contact means the
         * player is standing on it. */
        hitbox_t plank;
        plank.left   = -8;
        plank.right  = 8;
        plank.top    = -8;
        plank.bottom = 8;

        int box_x = px >> 16;
        int box_y = (self->position.y + hitY) >> 16;

        if (player->ysp >= 0 &&
            Player_CheckCollisionBox(player, box_x, box_y, &plank) == C_TOP) {
            ++self->stoodEntityCount;
            if (!player->onGround) {
                player->onGround = true;
                player->gsp      = player->xsp;
            }
            player->ysp = 0;

            /* stoodEntity identity tracking (decomp L203-224). */
            self->stoodEntity = STOOD_PLAYER;
            if (player->ysp < 0x10000)
                self->timer = 0x80;
        }
    } else if (self->stoodEntity == STOOD_PLAYER) {
        self->timer       = 32;
        self->stoodEntity = STOOD_LEFT;
    }
}

/* === RSDK class callbacks ============================================ */

__attribute__((used)) void Bridge_Update(void)
{
    EntityBridge *self = (EntityBridge *)g_rsdk_current_entity;

    /* decomp L16-31: timer ramp + depth from depression*timer. */
    if (self->stoodEntityCount) {
        if (self->timer < 0x80)
            self->timer += 8;
    } else {
        if (self->timer) {
            self->stoodEntity = STOOD_NONE;
            self->timer -= 8;
        } else {
            self->depression = 0;
        }
    }

    self->stoodEntityCount = 0;
    self->bridgeDepth      = (self->depression * self->timer) >> 7;

    /* foreach_active(Player) -> single guarded player. */
    if (mania_is_ghz_active())
        bridge_handle_collisions(self, g_ghz_player_addr);

    /* burnOffset stays 0xFF (no fire shield on Saturn) -> no Bridge_Burn. */
}

void Bridge_LateUpdate(void) {}
void Bridge_StaticUpdate(void) {}

void Bridge_Draw(void)
{
    /* No-op: rsdk_object_draw_all is suppressed in the GHZ path; the
     * actual draw is Bridge_draw_only (mania_ghz_draw_only). */
}

__attribute__((used)) void Bridge_Create(void *data)
{
    EntityBridge *self = (EntityBridge *)g_rsdk_current_entity;
    (void)data;

    self->visible     = true;             /* decomp L86 */
    ++self->length;                       /* decomp L87 */
    self->drawGroup   = 0;                /* Zone->objectDrawGroup[0] */
    self->active      = ACTIVE_BOUNDS;
    int32 len           = self->length << 19;
    self->startPos      = self->position.x - len;
    self->endPos        = len + self->position.x;
    self->updateRange.x = len;
    self->updateRange.y = 0x800000;
    self->stoodEntity   = STOOD_NONE;     /* decomp (Entity*)-1 */
    self->burnOffset    = 0xFF;

    ++g_ghz_bridge_spawned;
}

void Bridge_StageLoad(void)
{
    /* Asset load is via Bridge_load_assets (Saturn entity_atlas), called
     * from entities_load_assets on GHZ entry, NOT here — so the all-class
     * StageLoad pass at title boot does zero GHZ asset I/O. */
}

/* === Saturn-side asset load + bespoke draw =========================== */

void Bridge_load_assets(void)
{
    if (entity_atlas_load(&g_bridge_atlas, "BRIDGE"))
        entity_atlas_play(&g_bridge_atlas, 0);
}

void Bridge_draw_only(int cam_x, int cam_y)
{
    if (!g_bridge_atlas.ready) return;

    int sid = entity_atlas_current_sprite(&g_bridge_atlas);
    if (sid < 0) return;
    int fw, fh;
    entity_atlas_size(&g_bridge_atlas, &fw, &fh);
    if (fw <= 0) fw = 16;
    if (fh <= 0) fh = 16;

    int cls = rsdk_object_find_class("Bridge");
    if (cls < 0) return;

    for (uint16_t slot = 0; slot < RSDK_ENTITY_COUNT; ++slot) {
        rsdk_entity_t *base = rsdk_entity_at(slot);
        if (!base) continue;
        if (base->class_id != (uint16_t)cls) continue;
        if (base->active == ACTIVE_NEVER) continue;

        EntityBridge *self = (EntityBridge *)base;
        if (self->length <= 0) continue;

        /* Reproduce the decomp two-arc sine plank layout (Bridge_Draw
         * L56-80). All distances in Q16.16; convert each plank centre to
         * screen pixels and emit one sprite. */
        int size    = self->stoodPos >> 20;
        int ang     = 0x80000;
        int posy    = self->position.y;
        int draw_x  = self->startPos + 0x80000;
        int id      = 0;

        /* Left arc up to the stood column. */
        for (int i = 0; i < size; ++i) {
            int phase512 = self->stoodPos ? (((ang >> 8) * 512) / (self->stoodPos >> 8 ? (self->stoodPos >> 8) : 1)) : 0;
            int dy = ((self->bridgeDepth * bridge_sin(phase512 & 0x1FF)) >> 7);
            int sx = (draw_x >> 16) - cam_x;
            int sy = ((dy + posy) >> 16) - cam_y;
            if (!(sx < -fw || sx > SCR_W + fw || sy < -fh || sy > SCR_H + fh))
                jo_sprite_draw3D(sid, sx - 160, sy - 112, 150);
            draw_x += 0x100000;
            ang    += 0x100000;
            ++id;
        }

        /* Centre plank (the stood column). */
        {
            int sx = (draw_x >> 16) - cam_x;
            int sy = (((self->bridgeDepth + posy)) >> 16) - cam_y;
            if (!(sx < -fw || sx > SCR_W + fw || sy < -fh || sy > SCR_H + fh))
                jo_sprite_draw3D(sid, sx - 160, sy - 112, 150);
            draw_x += 0x100000;
            ++id;
        }

        /* Right arc back down to endPos. */
        ang = 0x80000;
        int divisor = self->endPos - self->startPos - self->stoodPos;
        if (divisor <= 0) divisor = 1;
        draw_x = self->endPos - 0x80000;
        for (; id < self->length; ++id) {
            int phase512 = (((ang >> 8) * 512) / (divisor >> 8 ? (divisor >> 8) : 1));
            int dy = ((self->bridgeDepth * bridge_sin(phase512 & 0x1FF)) >> 7);
            int sx = (draw_x >> 16) - cam_x;
            int sy = ((dy + posy) >> 16) - cam_y;
            if (!(sx < -fw || sx > SCR_W + fw || sy < -fh || sy > SCR_H + fh))
                jo_sprite_draw3D(sid, sx - 160, sy - 112, 150);
            draw_x -= 0x100000;
            ang    += 0x100000;
        }
    }
}
