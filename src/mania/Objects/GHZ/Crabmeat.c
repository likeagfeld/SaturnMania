/* ---------------------------------------------------------------------
 * Phase 2.4h — Crabmeat badnik.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_GHZ_Crabmeat.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Translation notes vs decomp (same conventions as InvisibleBlock.c +
 * Chopper.c):
 *
 *   - foreach_active(Player, player) -> single bespoke g_ghz_player
 *     (g_ghz_player_addr), gated on mania_is_ghz_active().
 *
 *   - Player_CheckBadnikTouch + Player_CheckBadnikBreak (decomp L107-108)
 *     -> Player_CheckCollisionBox dispatch (stomp C_TOP+airborne / hurt),
 *     per Motobug precedent (Entities.c:490-546) +
 *     memory/player-checkcollisionbox-required-for-every-object.md.
 *
 *   - RSDK.ObjectTileGrip floor check (decomp L130) -> Saturn surface
 *     probe: Crabmeat walks the floor; it turns when its timer hits 128
 *     OR when there is no floor ahead (a ledge), detected via
 *     Player_SurfaceY(world, probe_x) returning SMS_NO_FLOOR. This is the
 *     Saturn-fit of the ObjectTileGrip "no floor at the forward step"
 *     condition (decomp inverts the grip result).
 *
 *   - Projectile sub-entity (decomp State_Shoot L171-177, State_Projectile
 *     L187-207): the decomp CREATE_ENTITYs two falling projectiles. On
 *     Saturn the GHZ draw path suppresses rsdk_object_draw_all and there
 *     is no projectile draw plumbing yet, so projectile SPAWN is a
 *     Saturn-fit no-op (FIXME Phase 2.5 — same deferral as Newtron.c:14).
 *     The shoot CYCLE (anim swap, direction flip, timer) is ported in
 *     full so the badnik's visible behavior matches.
 *
 *   - Drawing: Crabmeat_Draw is a no-op; Crabmeat_draw_only walks the
 *     RSDK Crabmeat slots via the shared g_crabmeat_atlas (all 22 frames).
 * ------------------------------------------------------------------- */

#include "Crabmeat.h"
#include "../Common/Entities.h"
#include "../../../rsdk/entity_atlas.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/drawing.h"   /* FLIP_X / FLIP_NONE (decomp direction) */

#include <jo/jo.h>
#include <stdlib.h>

#define SCR_W 320
#define SCR_H 224

/* Anim ids (decomp): 0=Stand, 1=Walk, 2=Shoot, 3=Projectile. */
#define ANIM_STAND 0
#define ANIM_WALK  1
#define ANIM_SHOOT 2

/* SMS_NO_FLOOR (0xFFFF) is defined in Player.h. */

ObjectCrabmeat *Crabmeat = NULL;

extern player_t *const g_ghz_player_addr;

/* QA landmark (gate P4). */
__attribute__((used)) volatile int g_ghz_crabmeat_spawned = 0;

/* === collision (decomp CheckPlayerCollisions; Saturn-fit) ============= */

static void crabmeat_check_player(EntityCrabmeat *self)
{
    if (!mania_is_ghz_active()) return;
    player_t *p = g_ghz_player_addr;

    int box_x = self->position.x >> 16;
    int box_y = self->position.y >> 16;

    int side = Player_CheckCollisionBox(p, box_x, box_y, &Crabmeat->hitboxBadnik);
    if (side == C_NONE) return;

    bool attacking = !p->onGround;
    if (side == C_TOP && attacking) {
        self->active = ACTIVE_NEVER;
        g_hud_score += 100;
        p->ysp = -PLAYER_FIXED(4.0);
        p->jumping = true;
        p->onGround = false;
        return;
    }
    if (g_hud_rings > 0) g_hud_rings = 0;
    p->ysp = -PLAYER_FIXED(4.0);
    p->xsp = p->facing_left ? PLAYER_FIXED(2.0) : -PLAYER_FIXED(2.0);
    p->onGround = false;
    p->jumping  = true;
}

/* === Saturn-fit ledge probe (decomp !ObjectTileGrip => turn) =========
 * decomp turns when there is no floor at the forward step (grip fails). */
static bool crabmeat_no_floor_ahead(const sms_world_t *w, EntityCrabmeat *self)
{
    int probe_x = (self->position.x >> 16) + (self->velocity.x > 0 ? 14 : -14);
    int surf_y  = Player_SurfaceY(w, probe_x);
    return (surf_y == SMS_NO_FLOOR);
}

/* === States ========================================================== */

void Crabmeat_State_Init(void);
void Crabmeat_State_Moving(void);
void Crabmeat_State_Shoot(void);

void Crabmeat_State_Moving(void)
{
    EntityCrabmeat *self = (EntityCrabmeat *)g_rsdk_current_entity;

    self->position.x += self->velocity.x;

    const sms_world_t *w = mania_ghz_world();
    bool ledge = (w != NULL) && crabmeat_no_floor_ahead(w, self);

    if (self->timer >= 128 || ledge) {
        self->timer = 0;
        self->state = Crabmeat_State_Shoot;
    } else {
        self->timer++;
    }

    crabmeat_check_player(self);
}

void Crabmeat_State_Shoot(void)
{
    EntityCrabmeat *self = (EntityCrabmeat *)g_rsdk_current_entity;

    if (++self->timer >= 60) {
        switch (self->shootState) {
            default:
            case 0:
                self->shootState = 1;
                self->direction ^= FLIP_X;
                self->velocity.x = -self->velocity.x;
                self->timer      = 0;
                self->state      = Crabmeat_State_Moving;
                break;

            case 1:
                self->shootState = 2;
                /* decomp L171-177 spawns two falling projectiles. Saturn-fit
                 * no-op (FIXME Phase 2.5; same deferral as Newtron.c:14). */
                break;

            case 2:
                self->shootState = 1;
                self->direction ^= FLIP_X;
                self->velocity.x = -self->velocity.x;
                self->timer      = 0;
                self->state      = Crabmeat_State_Moving;
                break;
        }
    }

    crabmeat_check_player(self);
}

void Crabmeat_State_Init(void)
{
    EntityCrabmeat *self = (EntityCrabmeat *)g_rsdk_current_entity;

    self->active     = ACTIVE_NORMAL;
    self->velocity.x = -0x8000;
    self->state      = Crabmeat_State_Moving;
    Crabmeat_State_Moving();
}

/* === RSDK class callbacks ============================================= */

__attribute__((used)) void Crabmeat_Update(void)
{
    EntityCrabmeat *self = (EntityCrabmeat *)g_rsdk_current_entity;
    StateMachine_Run(self->state);
}

void Crabmeat_LateUpdate(void) {}
void Crabmeat_StaticUpdate(void) {}
void Crabmeat_Draw(void) {}

__attribute__((used)) void Crabmeat_Create(void *data)
{
    EntityCrabmeat *self = (EntityCrabmeat *)g_rsdk_current_entity;
    (void)data;   /* projectile-spawn path deferred (see file header) */

    self->visible       = true;
    self->drawGroup     = 0;            /* Zone->objectDrawGroup[0] */
    self->drawFX        = FX_FLIP;
    self->startPos.x    = self->position.x;
    self->startPos.y    = self->position.y;
    self->startDir      = self->direction;
    self->active        = ACTIVE_BOUNDS;
    self->updateRange.x = 0x800000;
    self->updateRange.y = 0x800000;
    self->state         = Crabmeat_State_Init;

    ++g_ghz_crabmeat_spawned;
}

void Crabmeat_StageLoad(void)
{
    /* Hitbox table (decomp StageLoad L62-70). aniFrames via
     * Crabmeat_load_assets (entity_atlas), not here (zero boot I/O). */
    if (!Crabmeat) return;
    Crabmeat->hitboxBadnik     = (hitbox_t){ -14, -14, 14, 14 };
    Crabmeat->hitboxProjectile = (hitbox_t){  -6,  -6,  6,  6 };
}

/* === Saturn-side asset load + draw ==================================== */

void Crabmeat_load_assets(void)
{
    if (entity_atlas_load(&g_crabmeat_atlas, "CRABMEAT"))
        entity_atlas_play(&g_crabmeat_atlas, ANIM_WALK);
}

void Crabmeat_draw_only(int cam_x, int cam_y)
{
    if (!g_crabmeat_atlas.ready) return;

    entity_atlas_tick(&g_crabmeat_atlas);

    int cls = rsdk_object_find_class("Crabmeat");
    if (cls < 0) return;

    for (uint16_t slot = 0; slot < RSDK_ENTITY_COUNT; ++slot) {
        rsdk_entity_t *base = rsdk_entity_at(slot);
        if (!base) continue;
        if (base->class_id != (uint16_t)cls) continue;
        if (base->active == ACTIVE_NEVER) continue;

        EntityCrabmeat *self = (EntityCrabmeat *)base;

        int wx = self->position.x >> 16;
        int wy = self->position.y >> 16;
        int sx = wx - cam_x;
        int sy = wy - cam_y;

        /* Anim per state. */
        int anim = (self->state == Crabmeat_State_Shoot) ? ANIM_SHOOT : ANIM_WALK;
        if (anim != (int)g_crabmeat_atlas.current_anim &&
            anim < g_crabmeat_atlas.anim_count)
            entity_atlas_play(&g_crabmeat_atlas, anim);

        int sid = entity_atlas_current_sprite(&g_crabmeat_atlas);
        if (sid < 0) continue;
        int fw, fh;
        entity_atlas_size(&g_crabmeat_atlas, &fw, &fh);
        if (fw <= 0) fw = 44;
        if (fh <= 0) fh = 31;

        if (sx < -fw || sx > SCR_W + fw || sy < -fh || sy > SCR_H + fh)
            continue;

        if (self->direction) jo_sprite_enable_horizontal_flip();
        jo_sprite_draw3D(sid, sx - 160, sy - 112, 145);
        if (self->direction) jo_sprite_disable_horizontal_flip();
    }
}
