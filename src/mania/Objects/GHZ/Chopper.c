/* ---------------------------------------------------------------------
 * Phase 2.4h — Chopper badnik.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_GHZ_Chopper.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Translation notes vs decomp (same conventions as InvisibleBlock.c):
 *
 *   - `foreach_active(Player, player)` (decomp L98-102,109-113,197-216):
 *     the Saturn build has one player (g_ghz_player via g_ghz_player_addr,
 *     valid once mania_is_ghz_active()). The loop collapses to a single
 *     guarded reference. No RSDK Player slot group yet.
 *
 *   - Player_CheckBadnikTouch + Player_CheckBadnikBreak (decomp L100-101,
 *     111-112): the Saturn collision surface is Player_CheckCollisionBox
 *     (Player.h) dispatching C_TOP/LEFT/RIGHT/BOTTOM. Stomp (C_TOP while
 *     airborne) destroys the badnik + bounces Sonic; side/bottom hurts.
 *     Mirrors the Motobug precedent (Entities.c:490-546) +
 *     memory/player-checkcollisionbox-required-for-every-object.md.
 *
 *   - RSDK.ObjectTileCollision wall/floor probes (decomp L180-188,246-280):
 *     the Saturn GHZ surface is a single-path height table queried via
 *     Player_SurfaceY(world, x_px). Chopper Jump bounces off its own
 *     startPos.y baseline (decomp L161-164, no tile probe). Chopper Swim's
 *     wall-flip uses the surface probe: a wall ahead is detected when the
 *     surface at the probe X rises sharply (>= 16 px) relative to the
 *     badnik's own Y. This is the Saturn-fit of the LWALL/RWALL CMODE
 *     probe; the timer-driven flip (decomp L190-194) is preserved exactly.
 *
 *   - Water / charge / ChargeDelay (decomp L196-296): the GHZ Act 1 scene
 *     has no Water entity and `charge` defaults false (Serialize L329 is a
 *     scene attribute; scene.c fill leaves it 0 for the GHZ instances), so
 *     the charge branch is inert. Ported for fidelity but the surface
 *     probe stands in for the (absent) Water-pool / roof checks.
 *
 *   - Drawing: rsdk_object_draw_all is suppressed in the GHZ path, so
 *     Chopper_Draw is a no-op and the actual draw is Chopper_draw_only,
 *     which walks the RSDK slots of class Chopper and draws each via the
 *     shared g_chopper_atlas (SPR2+MET, all 24 frames per
 *     memory/entity-atlas-must-ship-all-frames.md).
 * ------------------------------------------------------------------- */

#include "Chopper.h"
#include "../Common/Entities.h"
#include "../../../rsdk/entity_atlas.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/drawing.h"   /* FLIP_X / FLIP_NONE (decomp direction) */

#include <jo/jo.h>
#include <stdlib.h>

#define SCR_W 320
#define SCR_H 224

/* Anim ids (decomp SetSpriteAnimation calls): 0=Jump, 1=Swim, 2=Charge. */
#define ANIM_JUMP   0
#define ANIM_SWIM   1
#define ANIM_CHARGE 2

ObjectChopper *Chopper = NULL;

extern player_t *const g_ghz_player_addr;

/* QA landmark (gate P4): count of Chopper entities spawned into RSDK
 * slots this scene. `used` + non-static so LTO keeps a stable map address
 * the savestate gate can peek. Same precedent as InvisibleBlock.c:69. */
__attribute__((used)) volatile int g_ghz_chopper_spawned = 0;

/* __attribute__((used)) defeats GCC 8.2 whole-program LTO symbol
 * elimination so gate P1 can locate the registered callbacks in game.map
 * (precedent: InvisibleBlock.c:72-78). */

/* === collision dispatch (decomp CheckPlayerCollisions_*; Saturn-fit) === */

static void chopper_check_player(EntityChopper *self, const hitbox_t *hb)
{
    if (!mania_is_ghz_active()) return;
    player_t *p = g_ghz_player_addr;

    int box_x = self->position.x >> 16;
    int box_y = self->position.y >> 16;

    int side = Player_CheckCollisionBox(p, box_x, box_y, hb);
    if (side == C_NONE) return;

    bool attacking = !p->onGround;   /* airborne proxy (Motobug precedent) */
    if (side == C_TOP && attacking) {
        /* Stomp — decomp Player_CheckBadnikBreak. Destroy + bounce. */
        self->active = ACTIVE_NEVER;
        g_hud_score += 100;
        p->ysp = -PLAYER_FIXED(4.0);
        p->jumping = true;
        p->onGround = false;
        return;
    }
    /* Hurt path. */
    if (g_hud_rings > 0) g_hud_rings = 0;
    p->ysp = -PLAYER_FIXED(4.0);
    p->xsp = p->facing_left ? PLAYER_FIXED(2.0) : -PLAYER_FIXED(2.0);
    p->onGround = false;
    p->jumping  = true;
}

/* === Saturn-fit wall probe (decomp ObjectTileCollision CMODE_*WALL) ===
 * Returns true when a wall blocks horizontal progress at the badnik's
 * current band. Mirrors the decomp 3-probe (center/-0xF0000/+0xF0000)
 * by sampling the surface ahead and treating a sharp rise as a wall. */
static bool chopper_hit_wall(const sms_world_t *w, EntityChopper *self)
{
    int probe_x = (self->position.x >> 16) + (self->direction ? 16 : -16);
    int surf_y  = Player_SurfaceY(w, probe_x);
    int self_y  = self->position.y >> 16;
    /* Wall = surface rises >= 16 px above the badnik's own Y. */
    return (surf_y <= self_y - 16);
}

/* === States (decomp Chopper_State_*) ================================== */

void Chopper_State_Init(void);
void Chopper_State_Jump(void);
void Chopper_State_Swim(void);

void Chopper_State_Jump(void)
{
    EntityChopper *self = (EntityChopper *)g_rsdk_current_entity;

    self->position.y += self->velocity.y;
    self->velocity.y += 0x1800;

    /* decomp L161-164: bounce off the startPos.y baseline. */
    if (self->position.y > self->startPos.y) {
        self->position.y = self->startPos.y;
        self->velocity.y = -0x70000;
    }

    chopper_check_player(self, &Chopper->hitboxJump);
}

void Chopper_State_Swim(void)
{
    EntityChopper *self = (EntityChopper *)g_rsdk_current_entity;

    self->position.x += self->velocity.x;

    const sms_world_t *w = mania_ghz_world();
    bool hitWall = (w != NULL) && chopper_hit_wall(w, self);

    /* decomp L190-194: timer-driven (or wall) flip. */
    if (self->timer == 0 || hitWall) {
        self->direction ^= FLIP_X;
        self->velocity.x = -self->velocity.x;
        self->timer      = 512;
    } else {
        --self->timer;
    }

    chopper_check_player(self, &Chopper->hitboxSwim);
}

void Chopper_State_Init(void)
{
    EntityChopper *self = (EntityChopper *)g_rsdk_current_entity;

    self->active     = ACTIVE_NORMAL;
    self->velocity.x = -0x10000;

    if (self->type == CHOPPER_JUMP) {
        self->state = Chopper_State_Jump;
        Chopper_State_Jump();
    } else {
        self->state = Chopper_State_Swim;
        self->timer = 512;
        self->velocity.x = self->direction ? 0x4000 : -0x4000;
        Chopper_State_Swim();
    }
}

/* === RSDK class callbacks ============================================= */

__attribute__((used)) void Chopper_Update(void)
{
    EntityChopper *self = (EntityChopper *)g_rsdk_current_entity;
    StateMachine_Run(self->state);
}

void Chopper_LateUpdate(void) {}
void Chopper_StaticUpdate(void) {}

void Chopper_Draw(void)
{
    /* No-op: rsdk_object_draw_all is suppressed in the GHZ path; the
     * actual draw is Chopper_draw_only (called from mania_ghz_draw_only). */
}

__attribute__((used)) void Chopper_Create(void *data)
{
    EntityChopper *self = (EntityChopper *)g_rsdk_current_entity;
    (void)data;

    self->visible       = true;
    self->drawGroup     = 0;            /* Zone->objectDrawGroup[0] */
    self->startPos      = self->position;
    self->startDir      = self->direction;
    self->active        = ACTIVE_BOUNDS;
    self->updateRange.x = 0x800000;
    self->updateRange.y = 0x1200000;
    self->drawFX        = FX_FLIP;
    self->state         = Chopper_State_Init;

    ++g_ghz_chopper_spawned;
}

void Chopper_StageLoad(void)
{
    /* Hitbox table (decomp StageLoad L47-65). aniFrames load is handled
     * by Chopper_load_assets (Saturn entity_atlas), not here, so the
     * all-class StageLoad pass at title boot does zero asset I/O. */
    if (!Chopper) return;
    Chopper->hitboxJump  = (hitbox_t){ -10, -20,  6, 20 };
    Chopper->hitboxSwim  = (hitbox_t){ -20,  -6, 20, 10 };
    Chopper->hitboxRange = (hitbox_t){ -160,-32, 16, 32 };
    Chopper->hitboxWater = (hitbox_t){ -20, -24, 20,-16 };
}

/* === Saturn-side asset load + draw ==================================== */

void Chopper_load_assets(void)
{
    if (entity_atlas_load(&g_chopper_atlas, "CHOPPER"))
        entity_atlas_play(&g_chopper_atlas, ANIM_JUMP);
}

void Chopper_draw_only(int cam_x, int cam_y)
{
    if (!g_chopper_atlas.ready) return;

    /* Advance the shared atlas once per frame (decomp drives a per-entity
     * animator; on Saturn one shared walker covers all visible Choppers
     * — adequate since every Chopper shares the same anim cadence). */
    entity_atlas_tick(&g_chopper_atlas);

    int cls = rsdk_object_find_class("Chopper");
    if (cls < 0) return;

    for (uint16_t slot = 0; slot < RSDK_ENTITY_COUNT; ++slot) {
        rsdk_entity_t *base = rsdk_entity_at(slot);
        if (!base) continue;
        if (base->class_id != (uint16_t)cls) continue;
        if (base->active == ACTIVE_NEVER) continue;

        EntityChopper *self = (EntityChopper *)base;

        int wx = self->position.x >> 16;
        int wy = self->position.y >> 16;
        int sx = wx - cam_x;
        int sy = wy - cam_y;

        /* Anim select per state -> atlas anim. */
        int anim = ANIM_JUMP;
        if (self->type == CHOPPER_SWIM) anim = ANIM_SWIM;
        if (anim != (int)g_chopper_atlas.current_anim &&
            anim < g_chopper_atlas.anim_count)
            entity_atlas_play(&g_chopper_atlas, anim);

        int sid = entity_atlas_current_sprite(&g_chopper_atlas);
        if (sid < 0) continue;
        int fw, fh;
        entity_atlas_size(&g_chopper_atlas, &fw, &fh);
        if (fw <= 0) fw = 32;
        if (fh <= 0) fh = 32;

        if (sx < -fw || sx > SCR_W + fw || sy < -fh || sy > SCR_H + fh)
            continue;

        if (self->direction) jo_sprite_enable_horizontal_flip();
        jo_sprite_draw3D(sid, sx - 160, sy - 112, 145);
        if (self->direction) jo_sprite_disable_horizontal_flip();
    }
}
