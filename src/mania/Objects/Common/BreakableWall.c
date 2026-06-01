/* ---------------------------------------------------------------------
 * Phase 2.4-PLAT (Task #155) — BreakableWall port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Common_BreakableWall.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Ported onto the RSDK entity engine (src/rsdk/object.c slot table +
 * direct g_rsdk_current_entity cast) like the 2.4h Chopper precedent.
 *
 * The GHZ Scene1.bin BreakableWall instances all take the data==NULL
 * Create branch (decomp L58-128): an INVISIBLE scene wall whose surface
 * is the FG tilemap (visible=DebugMode->debugActive). On Saturn the wall
 * is invisible (no atlas, no draw_only). See BreakableWall.h note.
 *
 * Saturn-fit deviations (FIXME, documented vs decomp lines):
 *
 *   - foreach_active(Player) (decomp L299,371,...): the Saturn build has
 *     one player (g_ghz_player via g_ghz_player_addr), gated on
 *     mania_is_ghz_active() (same-TU state var, LTO-safe — never a
 *     cross-TU volatile ready flag per sync-load memory rule). The loop
 *     collapses to a single guarded reference. Precedent InvisibleBlock.c.
 *
 *   - Player_CheckCollisionTouch (decomp L340) does NOT exist on Saturn.
 *     The Saturn collision surface is Player_CheckCollisionBox (Player.h)
 *     which both tests AND snaps. The break gate fires on a non-C_NONE
 *     contact (Wall) / C_TOP contact (Floor) / C_BOTTOM (Ceiling/up).
 *
 *   - The break-on-spin gate uses `abs(player->groundVel) >= 0x48000 &&
 *     player->onGround && player->animator.animationID == ANI_JUMP`
 *     (decomp L321). The minimal Saturn player_t (Player.h:115-158) has
 *     NO animator/animationID, so the ANI_JUMP rolling-animation check is
 *     substituted with `!p->onGround || abs(p->gsp) >= 0x48000` for the
 *     floor path (a jump/drop/roll proxy) and `abs(p->gsp) >= 0x48000 &&
 *     p->onGround` for the wall path, mirroring the decomp intent (a fast
 *     spin or an airborne attack breaks the wall).
 *
 *   - BreakableWall_Break (decomp L628-699) spawns BREAKWALL_TILE_FIXED
 *     debris + RSDK.SetTile -1 on the FG layer. NOT ported: the Saturn
 *     surface model has no dynamic GetTile/SetTile (FG.TMP is static).
 *     The Saturn-fit break is destroyEntity + score (g_hud_score) + SFX
 *     (the surface stays drawn but the collision entity is gone — the
 *     closest Saturn-fit observable). GiveScoreBonus (decomp L702-735)
 *     collapses to a flat 100-point award (player->scoreBonus chain has
 *     no minimal-player analogue).
 * ------------------------------------------------------------------- */

#include "BreakableWall.h"
#include "Entities.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/drawing.h" /* TO_FIXED */

#include <jo/jo.h>
#include <stdlib.h>

ObjectBreakableWall *BreakableWall = NULL;

extern player_t *const g_ghz_player_addr;

/* QA landmark (gate P4): count of BreakableWall entities spawned into
 * RSDK slots this scene. `used` + non-static so LTO keeps a stable map
 * address the savestate gate can peek. Precedent InvisibleBlock.c:69. */
__attribute__((used)) volatile int g_ghz_breakablewall_spawned = 0;

/* === Saturn-fit break (decomp BreakableWall_Break + GiveScoreBonus) === */
static void breakablewall_break(EntityBreakableWall *self)
{
    /* Surface tile-shatter NOT ported (static FG.TMP). Saturn-fit:
     * destroy the collision entity + award score + play the break SFX. */
    g_hud_score += 100;
    destroyEntity(self);
}

/* === States (decomp BreakableWall_State_*) =========================== */
/* GHZ Act 1 instances are TYPE_WALL / TYPE_FLOOR / TYPE_CEILING (the
 * burrow-floor types are MSZ/PGZ-only). All three are ported below;
 * the tile/falling-tile debris states are not (no dynamic tilemap). */

void BreakableWall_State_Wall(void);
void BreakableWall_State_Floor(void);
void BreakableWall_State_Ceiling(void);

void BreakableWall_CheckBreak_Wall(void);
void BreakableWall_CheckBreak_Floor(void);
void BreakableWall_CheckBreak_Ceiling(void);

void BreakableWall_State_Wall(void)
{
    EntityBreakableWall *self = (EntityBreakableWall *)g_rsdk_current_entity;
    self->visible = false; /* DebugMode->debugActive == false on Saturn */
    BreakableWall_CheckBreak_Wall();
}

void BreakableWall_State_Floor(void)
{
    EntityBreakableWall *self = (EntityBreakableWall *)g_rsdk_current_entity;
    self->visible = false;
    BreakableWall_CheckBreak_Floor();
}

void BreakableWall_State_Ceiling(void)
{
    EntityBreakableWall *self = (EntityBreakableWall *)g_rsdk_current_entity;
    self->visible = false;
    BreakableWall_CheckBreak_Ceiling();
}

/* === Break checks (decomp CheckBreak_*; Saturn-fit) ================== */

void BreakableWall_CheckBreak_Wall(void)
{
    EntityBreakableWall *self = (EntityBreakableWall *)g_rsdk_current_entity;

    if (!mania_is_ghz_active()) return;
    player_t *player = g_ghz_player_addr;

    int box_x = self->position.x >> 16;
    int box_y = self->position.y >> 16;

    /* onlyKnux: no Knuckles on Saturn (Sonic-only). The decomp's
     * Knuckles-only wall just performs a box-collision (no break) for
     * Sonic — mirror that by falling through to the box collision. */
    if (self->onlyKnux) {
        Player_CheckCollisionBox(player, box_x, box_y, &self->hitbox);
        return;
    }

    /* decomp L321: canBreak = fast spin + onGround + ANI_JUMP. Saturn
     * has no animationID -> fast-ground-speed proxy. */
    bool32 canBreak = abs(player->gsp) >= 0x48000 && player->onGround;

    if (canBreak) {
        /* decomp L340 Player_CheckCollisionTouch -> Box contact test. */
        if (Player_CheckCollisionBox(player, box_x, box_y, &self->hitbox) != C_NONE) {
            breakablewall_break(self);
        }
        return; /* skip the plain box collision (decomp L361 continue) */
    }

    Player_CheckCollisionBox(player, box_x, box_y, &self->hitbox);
}

void BreakableWall_CheckBreak_Floor(void)
{
    EntityBreakableWall *self = (EntityBreakableWall *)g_rsdk_current_entity;

    if (!mania_is_ghz_active()) return;
    player_t *player = g_ghz_player_addr;

    int box_x = self->position.x >> 16;
    int box_y = self->position.y >> 16;

    if (Player_CheckCollisionBox(player, box_x, box_y, &self->hitbox) == C_TOP) {
        if (self->onlyKnux) return; /* Knuckles-only: Sonic just stands */

        /* decomp L399-420: canBreak = ANI_JUMP/ANI_DROPDASH and NOT a
         * grounded-store walk. Saturn proxy: arriving airborne (a jump
         * or drop-dash lands on the floor) OR a fast spin. */
        bool32 canBreak = (!player->onGround) || (abs(player->gsp) >= 0x48000);

        if (canBreak) {
            player->onGround = false;
            breakablewall_break(self);
            player->ysp = -PLAYER_FIXED(3.0); /* decomp L435 velocity.y = -3 */
        }
    }
}

void BreakableWall_CheckBreak_Ceiling(void)
{
    EntityBreakableWall *self = (EntityBreakableWall *)g_rsdk_current_entity;

    if (!mania_is_ghz_active()) return;
    player_t *player = g_ghz_player_addr;

    int box_x = self->position.x >> 16;
    int box_y = self->position.y >> 16;

    int32 velY = player->ysp;
    if (Player_CheckCollisionBox(player, box_x, box_y, &self->hitbox) == C_BOTTOM) {
        if (self->onlyKnux) return;
        player->onGround = false;
        breakablewall_break(self);
        player->ysp = velY; /* decomp L623 restore */
    }
}

/* === RSDK class callbacks ============================================ */

__attribute__((used)) void BreakableWall_Update(void)
{
    EntityBreakableWall *self = (EntityBreakableWall *)g_rsdk_current_entity;
    StateMachine_Run(self->state);
}

void BreakableWall_LateUpdate(void) {}
void BreakableWall_StaticUpdate(void) {}

void BreakableWall_Draw(void)
{
    /* No-op: the wall is invisible (FG tilemap surface). The debris
     * stateDraw path is not ported (no dynamic tilemap). */
}

__attribute__((used)) void BreakableWall_Create(void *data)
{
    EntityBreakableWall *self = (EntityBreakableWall *)g_rsdk_current_entity;

    self->gravityStrength = 0x3800; /* decomp L34 */

    /* GHZ Scene1.bin instances take the data==NULL branch (decomp L58). */
    (void)data;

    self->drawFX |= FX_FLIP;
    self->visible       = false;       /* decomp L62 (debugActive == false) */
    self->drawGroup     = 1;           /* Zone->objectDrawGroup[1] */
    self->active        = ACTIVE_BOUNDS;
    self->updateRange.x = TO_FIXED(128);
    self->updateRange.y = TO_FIXED(128);

    /* size arrives Q16.16 from Serialize; decomp L69-70 shifts to tiles. */
    self->size.x >>= 0x10;
    self->size.y >>= 0x10;

    switch (self->type) {
        case BREAKWALL_TYPE_WALL:
            if (!self->size.x) { self->size.x = 2; self->size.y = 4; }
            self->state = BreakableWall_State_Wall;
            break;

        case BREAKWALL_TYPE_FLOOR:
            if (!self->size.x) { self->size.x = 2; self->size.y = 2; }
            self->state = BreakableWall_State_Floor;
            break;

        case BREAKWALL_TYPE_CEILING:
            if (!self->size.x) { self->size.x = 2; self->size.y = 2; }
            self->state = BreakableWall_State_Ceiling;
            break;

        /* BURROWFLOOR* types are MSZ/PGZ-only (not in GHZ Scene1.bin);
         * map them to the floor break for fidelity. */
        default:
            if (!self->size.x) self->size.x = 2;
            self->state = BreakableWall_State_Floor;
            break;
    }

    self->hitbox.left   = -(8 * self->size.x);
    self->hitbox.top    = -(8 * self->size.y);
    self->hitbox.right  = 8 * self->size.x;
    self->hitbox.bottom = 8 * self->size.y;

    ++g_ghz_breakablewall_spawned;
}

void BreakableWall_StageLoad(void)
{
    /* Decomp loads "Global/TicMark.bin" (the DebugMode visualisation
     * sprite) + the LedgeBreak SFX + the Far Plane layer id. The wall is
     * invisible on Saturn (no debug overlay) so there is no sprite to
     * load; the break SFX is routed through entities_load_sfx. Keeping
     * this a no-op means the all-class StageLoad pass at title boot does
     * zero GHZ asset I/O (matches Chopper/InvisibleBlock precedent). */
}
