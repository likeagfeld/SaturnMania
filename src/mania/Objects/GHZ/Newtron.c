/* Phase 2.4c.2 Task #147 — Newtron port.
 *
 * Decomp source: tools/_decomp_raw/SonicMania_Objects_GHZ_Newtron.c
 *   - Create   L30-72  — type + alpha fade-in + initial anim per type
 *   - StageLoad L74-103 — aniFrames + 4 hitboxes (Shoot/Fly/Projectile/Range)
 *   - GetTargetDir L142-160 — find nearest player, set self->direction
 *   - State_Init   L162-170 — wake -> State_CheckPlayerInRange
 *   - State_CheckPlayerInRange L172-183 — sleeping until player enters
 *     hitboxRange={-128,-64,128,64}
 *   - State_Appear L185-207  — alpha 0->0xFF in steps of 4 (~64 ticks)
 *   - State_Shoot  L250-274  — 90-tick fire/recover cycle (SHOOT type)
 *   - State_StartFly L209-234 + State_Fly L236-248 — glider launch + glide
 *   - State_FadeAway L276-287 — alpha 0xFF->0 then destroyEntity
 *   - State_Projectile L289-307 — orphaned sub-entity (Phase 2.5 deferred)
 *
 * Saturn-side asset layout:
 *   cd/NEWTRON.SP2/.MET — 7 anims, 23 frames (per build_entity_atlas).
 *   cd/GHZ1NEWT.BIN — u16 BE count + count * (u16 BE x, u16 BE y, u8 type,
 *     u8 direction). */

#include "Newtron.h"
#include "../Common/Entities.h"
#include "../../../rsdk/entity_atlas.h"

#include <jo/jo.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define SCR_W           320
#define SCR_H           224
#define SONIC_BODY_H    40

#define NEWTRON_MAX     24

/* Decomp StageLoad L79-82: hitboxShoot = {-12, -14, 12, 14}. */
#define NEWTRON_HIT_L   (-12)
#define NEWTRON_HIT_T   (-14)
#define NEWTRON_HIT_R   ( 12)
#define NEWTRON_HIT_B   ( 14)

/* Decomp StageLoad L97-100: hitboxRange = {-128, -64, 128, 64}. */
#define NEWTRON_RNG_L   (-128)
#define NEWTRON_RNG_T   (-64)
#define NEWTRON_RNG_R   ( 128)
#define NEWTRON_RNG_B   ( 64)

/* NewtronTypes (decomp Newtron.h L6-10). */
#define NEWTRON_T_SHOOT 0
#define NEWTRON_T_FLY   1

/* Anim ids (decomp Create L47-69). */
#define ANIM_SHOOT_IDLE  0
#define ANIM_SHOOT_FIRE  1
#define ANIM_FLY_IDLE    2
#define ANIM_FLY_APPEAR  3
#define ANIM_FLY_GLIDE   4
#define ANIM_FLAME       5

typedef enum {
    NS_SLEEPING,   /* hidden, waiting for range trigger */
    NS_APPEAR,     /* alpha fade-in */
    NS_SHOOT,      /* 90-tick fire cycle (SHOOT type) */
    NS_STARTFLY,   /* gliding-launch transient (FLY type) */
    NS_FLY,        /* horizontal glide (FLY type) */
    NS_FADEAWAY,   /* alpha fade-out -> respawn */
} newtron_state_t;

typedef struct {
    int           x;
    int           y;
    int           start_x;
    int           start_y;
    int16_t       vx;          /* per-tick px step (FLY glide) */
    int16_t       timer;
    uint8_t       alpha;       /* 0..255 (decomp INK_ALPHA) */
    uint8_t       state;       /* newtron_state_t */
    uint8_t       type;        /* NEWTRON_T_SHOOT / NEWTRON_T_FLY */
    uint8_t       direction;   /* 0 = right (FLIP_NONE), 1 = left (FLIP_X) */
    uint8_t       start_dir;
    uint8_t       active;
} newtron_t;

typedef struct {
    newtron_t list[NEWTRON_MAX];
    int       count;
    bool      ready;
} newtron_module_t;

static newtron_module_t g_newtron;

void newtron_load_assets(void)
{
    g_newtron.ready = false;

    if (!entity_atlas_load(&g_newtron_atlas, "NEWTRON")) return;
    entity_atlas_play(&g_newtron_atlas, ANIM_SHOOT_IDLE);

    int blen = 0;
    unsigned char *raw = (unsigned char *)jo_fs_read_file("GHZ1NEWT.BIN", &blen);
    if (!raw || blen < 2) { if (raw) jo_free(raw); return; }
    unsigned int n = ((unsigned int)raw[0] << 8) | raw[1];
    if (n > NEWTRON_MAX) n = NEWTRON_MAX;
    for (unsigned int i = 0; i < n; ++i) {
        const unsigned char *r = raw + 2 + (i * 6);
        unsigned int nx = ((unsigned int)r[0] << 8) | r[1];
        unsigned int ny = ((unsigned int)r[2] << 8) | r[3];
        unsigned char nt = r[4] & 1;       /* 0=Shoot, 1=Fly */
        unsigned char nd = r[5] & 1;
        g_newtron.list[i].x         = (int)nx;
        g_newtron.list[i].y         = (int)ny;
        g_newtron.list[i].start_x   = (int)nx;
        g_newtron.list[i].start_y   = (int)ny;
        g_newtron.list[i].type      = nt;
        g_newtron.list[i].direction = nd;
        g_newtron.list[i].start_dir = nd;
        g_newtron.list[i].state     = NS_SLEEPING;
        g_newtron.list[i].alpha     = 0;
        g_newtron.list[i].timer     = 0;
        g_newtron.list[i].vx        = 0;
        g_newtron.list[i].active    = 1;
    }
    g_newtron.count = (int)n;
    jo_free(raw);
    g_newtron.ready = true;
}

/* Decomp GetTargetDir L142-160: direction = (player.x < self.x). */
static inline void newtron_face_player(newtron_t *n, int sonic_px)
{
    n->direction = (sonic_px < n->x) ? 1 : 0;
}

static void newtron_respawn(newtron_t *n)
{
    n->x         = n->start_x;
    n->y         = n->start_y;
    n->direction = n->start_dir;
    n->state     = NS_SLEEPING;
    n->alpha     = 0;
    n->timer     = 0;
    n->vx        = 0;
}

void newtron_tick_and_draw(const sms_world_t *w, player_t *p,
                           int cam_x, int cam_y)
{
    (void)w;
    if (!g_newtron.ready || !g_newtron_atlas.ready) return;

    int sonic_px = p->xpos >> 16;
    int sonic_py = p->ypos >> 16;

    for (int i = 0; i < g_newtron.count; ++i) {
        newtron_t *n = &g_newtron.list[i];
        if (!n->active) continue;

        /* === Off-screen reset (decomp CheckOffScreen L129-140). ====== */
        int sx = n->x - cam_x;
        int sy = n->y - cam_y;
        bool off = (sx < -96 || sx > SCR_W + 96 ||
                    sy < -96 || sy > SCR_H + 96);

        /* === State machine (decomp State_* transitions). ============= */
        switch (n->state) {
        case NS_SLEEPING: {
            /* Decomp State_CheckPlayerInRange L172-183 */
            int rdx = sonic_px - n->x;
            int rdy = sonic_py - n->y;
            if (rdx > NEWTRON_RNG_L && rdx < NEWTRON_RNG_R &&
                rdy > NEWTRON_RNG_T && rdy < NEWTRON_RNG_B) {
                n->state = NS_APPEAR;
                n->alpha = 0;
            }
            /* When player leaves screen entirely, snap back to start
             * (decomp resets via Newtron_Create on far off-screen). */
            if (off &&
                (abs(n->x - n->start_x) > 256 ||
                 abs(n->y - n->start_y) > 256)) {
                newtron_respawn(n);
            }
            break;
        }
        case NS_APPEAR: {
            /* Decomp State_Appear L185-207. alpha += 4 per tick;
             * once >= 0xF8 transition to next state. */
            if (n->type == NEWTRON_T_FLY) newtron_face_player(n, sonic_px);
            if (n->alpha >= 0xF8) {
                n->alpha = 0xFF;
                if (n->type == NEWTRON_T_FLY) {
                    n->state = NS_STARTFLY;
                    n->timer = 0;
                } else {
                    n->state = NS_SHOOT;
                    n->timer = 0;
                }
            } else {
                n->alpha = (uint8_t)(n->alpha + 4);
            }
            break;
        }
        case NS_SHOOT: {
            /* Decomp State_Shoot L250-274. 90-tick cycle:
             *   30 -> swap to firing anim (we don't spawn projectile)
             *   45 -> revert to idle
             *   90 -> FadeAway. */
            n->timer++;
            if (n->timer >= 90) {
                n->state = NS_FADEAWAY;
            }
            break;
        }
        case NS_STARTFLY: {
            /* Decomp State_StartFly L209-234. Approximate: 16-tick
             * appear hold, then commit to glide direction. */
            newtron_face_player(n, sonic_px);
            n->timer++;
            if (n->timer >= 16) {
                /* Decomp L221-224: velocity.x = +/- 0x20000 = +/- 2 px/fr
                 * fixed; in integer px we use 2 px/tick. */
                n->vx = n->direction ? -2 : 2;
                n->state = NS_FLY;
            }
            break;
        }
        case NS_FLY: {
            /* Decomp State_Fly L236-248. Glide horizontally until
             * off-screen, then fade. */
            n->x += n->vx;
            if (off) {
                n->state = NS_FADEAWAY;
            }
            break;
        }
        case NS_FADEAWAY: {
            /* Decomp State_FadeAway L276-287. alpha -= 4 -> respawn. */
            if (n->alpha <= 4) {
                newtron_respawn(n);
            } else {
                n->alpha = (uint8_t)(n->alpha - 4);
            }
            break;
        }
        }

        /* === Hit-test (decomp CheckPlayerCollisions L118-127). =======
         * Only active while visible (alpha > 0). */
        if (n->state != NS_SLEEPING && n->alpha > 0x40) {
            int dx = sonic_px - n->x;
            int dy = (sonic_py - SONIC_BODY_H / 2) - n->y;
            if (dx > NEWTRON_HIT_L - 8 && dx < NEWTRON_HIT_R + 8 &&
                dy > NEWTRON_HIT_T - 12 && dy < NEWTRON_HIT_B + 8) {
                int sonic_feet = sonic_py;
                int body_center = n->y;
                if (sonic_feet <= body_center + 4 &&
                    p->ysp >= 0 && !p->onGround) {
                    /* Stomp -> respawn (badnik defeat). */
                    g_hud_score += 100;
                    p->ysp = -PLAYER_FIXED(4.0);
                    p->jumping = true;
                    newtron_respawn(n);
                    continue;
                } else {
                    /* Side-hurt: zero rings + knockback (Phase 2.4c placeholder). */
                    if (g_hud_rings > 0) g_hud_rings = 0;
                    p->ysp = -PLAYER_FIXED(4.0);
                    p->xsp = (sonic_px < n->x) ? -PLAYER_FIXED(2.0)
                                               :  PLAYER_FIXED(2.0);
                    p->onGround = false;
                    p->jumping  = true;
                    continue;
                }
            }
        }

        /* === Draw (decomp Draw L22-28). ============================== */
        if (n->state == NS_SLEEPING) continue;
        if (n->alpha < 0x20) continue;   /* essentially invisible */

        /* Select body anim per type + state. */
        int body_anim;
        if (n->type == NEWTRON_T_FLY) {
            if (n->state == NS_APPEAR)       body_anim = ANIM_FLY_IDLE;
            else if (n->state == NS_STARTFLY) body_anim = ANIM_FLY_APPEAR;
            else if (n->state == NS_FLY)      body_anim = ANIM_FLY_GLIDE;
            else                              body_anim = ANIM_FLY_IDLE;
        } else {
            body_anim = (n->state == NS_SHOOT && n->timer >= 30 &&
                         n->timer < 45) ? ANIM_SHOOT_FIRE : ANIM_SHOOT_IDLE;
        }
        if (body_anim >= g_newtron_atlas.anim_count) body_anim = 0;

        int first = g_newtron_atlas.anims[body_anim].first;
        int fc    = g_newtron_atlas.anims[body_anim].frame_count;
        if (fc <= 0) continue;
        /* Pick a representative frame: for static-feeling anims we use
         * frame 0; for cycled ones we drive by global tick. */
        int frame_in_anim = 0;
        if (fc > 1) {
            frame_in_anim = ((int)(g_newtron.list[i].timer) >> 2) % fc;
        }
        int idx = first + frame_in_anim;
        if (idx < 0 || idx >= g_newtron_atlas.frame_total) continue;
        int sid = (int)g_newtron_atlas.sprite_id[idx];
        if (sid < 0) continue;
        int fw = (int)g_newtron_atlas.width[idx];
        int fh = (int)g_newtron_atlas.height[idx];
        if (fw == 0) fw = 32;
        if (fh == 0) fh = 32;

        if (n->direction) jo_sprite_enable_horizontal_flip();
        jo_sprite_draw3D(sid,
                         (sx + fw / 2) - 160,
                         (sy + fh / 2) - 112,
                         145);

        /* Flame overlay only during NS_FLY (decomp Draw L26-27). */
        if (n->state == NS_FLY && g_newtron_atlas.anim_count > ANIM_FLAME) {
            int f_first = g_newtron_atlas.anims[ANIM_FLAME].first;
            int f_fc    = g_newtron_atlas.anims[ANIM_FLAME].frame_count;
            if (f_fc > 0) {
                int fframe = ((int)(g_newtron.list[i].timer) >> 1) % f_fc;
                int fidx = f_first + fframe;
                if (fidx >= 0 && fidx < g_newtron_atlas.frame_total) {
                    int fsid = (int)g_newtron_atlas.sprite_id[fidx];
                    if (fsid >= 0) {
                        int ffw = (int)g_newtron_atlas.width[fidx];
                        int ffh = (int)g_newtron_atlas.height[fidx];
                        if (ffw == 0) ffw = 32;
                        if (ffh == 0) ffh = 16;
                        int ftx = n->direction ? n->x + 12 : n->x - 12;
                        int ftsx = ftx - cam_x;
                        jo_sprite_draw3D(fsid,
                                         (ftsx + ffw / 2) - 160,
                                         (sy + ffh / 2) - 112,
                                         146);
                    }
                }
            }
        }
        if (n->direction) jo_sprite_disable_horizontal_flip();
    }
}
