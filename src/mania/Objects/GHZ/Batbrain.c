/* ---------------------------------------------------------------------
 * Phase 2.4h — Batbrain badnik.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_GHZ_Batbrain.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Translation notes vs decomp (same conventions as InvisibleBlock.c +
 * Chopper.c):
 *
 *   - foreach_active(Player, player) (decomp L75-79,112-127) -> single
 *     bespoke g_ghz_player (g_ghz_player_addr), gated on
 *     mania_is_ghz_active(). self->target is that single player.
 *
 *   - Player_CheckBadnikTouch + Player_CheckBadnikBreak (decomp L77-78)
 *     -> Player_CheckCollisionBox dispatch (stomp C_TOP+airborne / hurt),
 *     per Motobug precedent + memory rule.
 *
 *   - RSDK.Rand(0,8) (decomp L133,178) -> a deterministic Saturn-fit
 *     pseudo-random: a per-entity tick accumulator hashed to 0..7; the
 *     drop / fly-to-ceiling transition fires when it lands on 0. Keeps
 *     the "occasionally" cadence without an engine RNG.
 *
 *   - RSDK.ObjectTileCollision CMODE_RWALL ceiling probe (decomp L199)
 *     -> Player_SurfaceY(world, x) ceiling check: FlyToCeiling stops
 *     rising when it reaches the badnik's own startPos.y (its ceiling
 *     anchor). Saturn-fit of the (commented-as-quirky) RWALL probe.
 *
 *   - PlaySfx(sfxFlap) (decomp L182,207) -> Saturn no-op (no per-entity
 *     SFX plumbing in the GHZ badnik path yet; FIXME Phase 2.5).
 *
 *   - Drawing: Batbrain_Draw is a no-op; Batbrain_draw_only walks the
 *     RSDK Batbrain slots via the shared g_batbrain_atlas (all 11 frames).
 * ------------------------------------------------------------------- */

#include "Batbrain.h"
#include "../Common/Entities.h"
#include "../../../rsdk/entity_atlas.h"
#include "../../../rsdk/object.h"

#include <jo/jo.h>
#include <stdlib.h>

#define SCR_W 320
#define SCR_H 224

/* Anim ids (decomp): 0=Hang/idle, 1=Fall/drop, 2=Fly. */
#define ANIM_HANG 0
#define ANIM_FALL 1
#define ANIM_FLY  2

ObjectBatbrain *Batbrain = NULL;

extern player_t *const g_ghz_player_addr;

/* QA landmark (gate P4). */
__attribute__((used)) volatile int g_ghz_batbrain_spawned = 0;

/* Deterministic Saturn-fit RNG (decomp RSDK.Rand(0,8) — fire on 0). */
static uint32_t s_batbrain_rng = 0x12345;
static inline int batbrain_rand8(void)
{
    s_batbrain_rng = s_batbrain_rng * 1103515245u + 12345u;
    return (int)((s_batbrain_rng >> 16) & 7);
}

/* === collision (decomp CheckPlayerCollisions; Saturn-fit) ============= */

static void batbrain_check_player(EntityBatbrain *self)
{
    if (!mania_is_ghz_active()) return;
    player_t *p = g_ghz_player_addr;

    int box_x = self->position.x >> 16;
    int box_y = self->position.y >> 16;

    int side = Player_CheckCollisionBox(p, box_x, box_y, &Batbrain->hitboxBadnik);
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

/* === States ========================================================== */

void Batbrain_State_Init(void);
void Batbrain_State_CheckPlayerInRange(void);
void Batbrain_State_DropToPlayer(void);
void Batbrain_State_Fly(void);
void Batbrain_State_FlyToCeiling(void);

void Batbrain_State_CheckPlayerInRange(void)
{
    EntityBatbrain *self = (EntityBatbrain *)g_rsdk_current_entity;
    if (!mania_is_ghz_active()) return;
    player_t *p = g_ghz_player_addr;

    int player_x = p->xpos;
    int player_y = p->ypos;
    int distance = abs(player_x - self->position.x);

    /* decomp L123-138: player within 0x800000 and below us -> maybe drop. */
    if (distance < 0x800000 && player_y >= self->position.y) {
        int dy = player_y - self->position.y;
        if (dy >= 0 && dy <= 0x800000 && batbrain_rand8() == 0) {
            self->state   = Batbrain_State_DropToPlayer;
            self->targetY = player_y;
            self->target  = p;
        }
    }

    self->direction = (player_x >= self->position.x);

    batbrain_check_player(self);
}

void Batbrain_State_DropToPlayer(void)
{
    EntityBatbrain *self = (EntityBatbrain *)g_rsdk_current_entity;

    self->position.y += self->velocity.y;
    self->velocity.y += 0x1800;
    if (self->target)
        self->direction = (self->target->xpos >= self->position.x);

    if (self->targetY - self->position.y < 0x100000) {
        self->velocity.y = 0;
        self->velocity.x = self->direction ? 0x10000 : -0x10000;
        self->state = Batbrain_State_Fly;
    }

    batbrain_check_player(self);
}

void Batbrain_State_Fly(void)
{
    EntityBatbrain *self = (EntityBatbrain *)g_rsdk_current_entity;

    self->position.x += self->velocity.x;

    if (self->target &&
        abs(self->target->xpos - self->position.x) >= 0x800000 &&
        batbrain_rand8() == 0)
        self->state = Batbrain_State_FlyToCeiling;

    /* PlaySfx(sfxFlap): no-op (FIXME Phase 2.5). */
    batbrain_check_player(self);
}

void Batbrain_State_FlyToCeiling(void)
{
    EntityBatbrain *self = (EntityBatbrain *)g_rsdk_current_entity;

    self->position.x += self->velocity.x;
    self->position.y += self->velocity.y;
    self->velocity.y -= 0x1800;

    /* decomp L199 CMODE_RWALL ceiling probe -> Saturn-fit: stop rising at
     * the startPos.y ceiling anchor. */
    if (self->position.y <= self->startPos.y) {
        self->position.y = self->startPos.y;
        self->velocity.x = 0;
        self->velocity.y = 0;
        self->state      = Batbrain_State_CheckPlayerInRange;
    }

    batbrain_check_player(self);
}

void Batbrain_State_Init(void)
{
    EntityBatbrain *self = (EntityBatbrain *)g_rsdk_current_entity;

    self->active     = ACTIVE_NORMAL;
    self->velocity.x = 0;
    self->velocity.y = 0;
    self->state      = Batbrain_State_CheckPlayerInRange;
    Batbrain_State_CheckPlayerInRange();
}

/* === RSDK class callbacks ============================================= */

__attribute__((used)) void Batbrain_Update(void)
{
    EntityBatbrain *self = (EntityBatbrain *)g_rsdk_current_entity;
    StateMachine_Run(self->state);
}

void Batbrain_LateUpdate(void) {}
void Batbrain_StaticUpdate(void) {}
void Batbrain_Draw(void) {}

__attribute__((used)) void Batbrain_Create(void *data)
{
    EntityBatbrain *self = (EntityBatbrain *)g_rsdk_current_entity;
    (void)data;

    self->visible       = true;
    self->drawFX        = FX_FLIP;
    self->drawGroup     = 0;            /* Zone->objectDrawGroup[0] */
    self->startPos.x    = self->position.x;
    self->startPos.y    = self->position.y;
    self->active        = ACTIVE_BOUNDS;
    self->updateRange.x = 0x800000;
    self->updateRange.y = 0x800000;
    self->state         = Batbrain_State_Init;

    ++g_ghz_batbrain_spawned;
}

void Batbrain_StageLoad(void)
{
    /* Hitbox (decomp StageLoad L48-51). aniFrames + sfxFlap via
     * Batbrain_load_assets (entity_atlas), not here (zero boot I/O). */
    if (!Batbrain) return;
    Batbrain->hitboxBadnik = (hitbox_t){ -12, -18, 12, 18 };
}

/* === Saturn-side asset load + draw ==================================== */

void Batbrain_load_assets(void)
{
    if (entity_atlas_load(&g_batbrain_atlas, "BATBRAIN"))
        entity_atlas_play(&g_batbrain_atlas, ANIM_HANG);
}

void Batbrain_draw_only(int cam_x, int cam_y)
{
    if (!g_batbrain_atlas.ready) return;

    entity_atlas_tick(&g_batbrain_atlas);

    int cls = rsdk_object_find_class("Batbrain");
    if (cls < 0) return;

    for (uint16_t slot = 0; slot < RSDK_ENTITY_COUNT; ++slot) {
        rsdk_entity_t *base = rsdk_entity_at(slot);
        if (!base) continue;
        if (base->class_id != (uint16_t)cls) continue;
        if (base->active == ACTIVE_NEVER) continue;

        EntityBatbrain *self = (EntityBatbrain *)base;

        int wx = self->position.x >> 16;
        int wy = self->position.y >> 16;
        int sx = wx - cam_x;
        int sy = wy - cam_y;

        /* Anim per state. */
        int anim = ANIM_HANG;
        if (self->state == Batbrain_State_DropToPlayer)      anim = ANIM_FALL;
        else if (self->state == Batbrain_State_Fly ||
                 self->state == Batbrain_State_FlyToCeiling) anim = ANIM_FLY;
        if (anim != (int)g_batbrain_atlas.current_anim &&
            anim < g_batbrain_atlas.anim_count)
            entity_atlas_play(&g_batbrain_atlas, anim);

        int sid = entity_atlas_current_sprite(&g_batbrain_atlas);
        if (sid < 0) continue;
        int fw, fh;
        entity_atlas_size(&g_batbrain_atlas, &fw, &fh);
        if (fw <= 0) fw = 32;
        if (fh <= 0) fh = 32;

        if (sx < -fw || sx > SCR_W + fw || sy < -fh || sy > SCR_H + fh)
            continue;

        if (self->direction) jo_sprite_enable_horizontal_flip();
        jo_sprite_draw3D(sid, sx - 160, sy - 112, 145);
        if (self->direction) jo_sprite_disable_horizontal_flip();
    }
}
