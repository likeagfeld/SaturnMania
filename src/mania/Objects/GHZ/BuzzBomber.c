/* Phase 2.4c Task #140 — BuzzBomber port.
 *
 * Decomp source: tools/_decomp_raw/SonicMania_Objects_GHZ_BuzzBomber.c
 *   - Create   L40-82   — initial state + drawFX + hitboxRange
 *   - StageLoad L84-102 — aniFrames + hitboxBadnik {-24,-12,24,12}
 *   - State_Init    L155-167 — velocity.x = -0x40000 (left) / +0x40000 (right)
 *   - State_Flying  L169-190 — position step + 128-tick patrol timer
 *   - State_Idle    L192-207 — 60-tick pause (timer hardcoded L196-200)
 *   - State_DetectedPlayer L209-247 — 128-tick charge-and-shoot countdown
 *   - CheckPlayerCollisions L132-153 — badnik-touch -> hurt/stomp
 *
 * Saturn-side asset layout:
 *   cd/BUZZ.SPR     - u16 BE frame_count + u16 BE w + u16 BE h
 *                     + frame_count * w*h * u16 BGR1555.
 *                     Frame 0 = body idle (decomp anim 0)
 *                     Frame 1 = body charge (decomp anim 1)
 *                     Frame 2 = wings flap (decomp anim 2; single-frame
 *                                            in Phase 2.4c)
 *                     Frame 3 = thrust (decomp anim 3; single-frame)
 *                     Frame 4 = projectile (decomp anim 4; reserved, not drawn
 *                                           in Phase 2.4c).
 *   cd/GHZ1BUZZ.BIN - u16 BE count + count * (u16 BE x, u16 BE y, u8 direction).
 *                     direction: 0 = FLIP_NONE (faces left, decomp default),
 *                                1 = FLIP_X    (faces right). */

#include "BuzzBomber.h"
#include "../Common/Entities.h"   /* g_hud_score */
#include "../../../rsdk/entity_atlas.h"

#include <jo/jo.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define SCR_W            320
#define SCR_H            224
#define SONIC_BODY_H     40

#define BUZZ_MAX         24       /* GHZ1 has 18; pad headroom */
#define BUZZ_W           48
#define BUZZ_H           32

/* Decomp StageLoad L91-94: hitboxBadnik = { -24, -12, 24, 12 }. */
#define BUZZ_HITBOX_LEFT     (-24)
#define BUZZ_HITBOX_TOP      (-12)
#define BUZZ_HITBOX_RIGHT    ( 24)
#define BUZZ_HITBOX_BOTTOM   ( 12)

/* Decomp State_Init L161-163: velocity.x = +/- 0x40000 = +/- 0.25 fx px/fr.
 * In Saturn integer-pixel approximation we step by 1 px every 4 ticks. */
#define BUZZ_STEP_TICKS  4

/* Decomp State_Flying L178: timer reload after each leg = 60. State_Idle
 * L197: timer reload to leave idle = 128. State_DetectedPlayer entry
 * (L147): timer = 90; checks at 82 (anim swap), 45 (shot), 0 (reset). */
#define BUZZ_FLY_TICKS       128   /* decomp Create L49 + Flying initial */
#define BUZZ_IDLE_TICKS      60    /* decomp State_Flying L178 */
#define BUZZ_DETECT_TICKS    90    /* decomp State_Flying detect branch L147 */
#define BUZZ_DETECT_CHARGE   82    /* decomp State_DetectedPlayer L219 */
#define BUZZ_DETECT_SHOT     45    /* decomp State_DetectedPlayer L222 */

typedef enum {
    BUZZ_STATE_FLYING,   /* patrol step + 128-tick countdown */
    BUZZ_STATE_IDLE,     /* 60-tick pause */
    BUZZ_STATE_DETECT,   /* 90-tick charge anim (no projectile spawn yet) */
} buzz_state_t;

typedef struct {
    int           x;             /* world X (pixels) */
    int           y;             /* world Y (pixels) */
    int           start_x;       /* respawn X */
    int           start_y;       /* respawn Y */
    int16_t       timer;
    unsigned char state;         /* buzz_state_t */
    unsigned char direction;     /* 0 = FLIP_NONE (left), 1 = FLIP_X (right) */
    unsigned char start_dir;
    unsigned char active;        /* 1 = drawn / ticked */
    unsigned char step_tick;     /* per-instance step accumulator */
} buzz_t;

typedef struct {
    buzz_t list[BUZZ_MAX];
    int    count;
    bool   ready;
} buzzbomber_module_t;

static buzzbomber_module_t g_buzz;

/* BuzzBomber atlas anim layout (post Phase 2.4e build):
 *   anim 0 = Fly        (1 frame,  static body sprite, speed=1)
 *   anim 1 = Shoot      (11 frames, charge sequence, one-shot)
 *   anim 2 = Wings      (4 frames, speed=128, 4-tick fast flap)
 *   anim 3 = Thrust     (4 frames, speed=128, 8-tick pulse)
 *   anim 4 = Projectile (12 frames, speed=1, looped)
 * Per audit row 7 + tools/_decomp_raw/SonicMania_Objects_GHZ_BuzzBomber.c
 * Create L40-82. */
#define BUZZ_ANIM_FLY    0
#define BUZZ_ANIM_SHOOT  1
#define BUZZ_ANIM_WINGS  2
#define BUZZ_ANIM_THRUST 3

/* Auxiliary atlas state for the wings/thrust overlays. We can't run two
 * atlases against the same g_buzz_atlas (single animator), so we manually
 * tick a secondary frame index via the atlas's per-anim cadence table. */
static int16_t s_buzz_wings_frame_id = 0;
static int16_t s_buzz_wings_timer    = 0;
static int16_t s_buzz_thrust_frame_id = 0;
static int16_t s_buzz_thrust_timer    = 0;

void buzzbomber_load_assets(void)
{
    g_buzz.ready = false;

    if (!entity_atlas_load(&g_buzz_atlas, "BUZZ")) return;
    entity_atlas_play(&g_buzz_atlas, BUZZ_ANIM_FLY);

    /* Entity positions */
    int blen = 0;
    unsigned char *raw = (unsigned char *)jo_fs_read_file("GHZ1BUZZ.BIN", &blen);
    if (!raw || blen < 2) { if (raw) jo_free(raw); return; }
    unsigned int n = ((unsigned int)raw[0] << 8) | raw[1];
    if (n > BUZZ_MAX) n = BUZZ_MAX;
    for (unsigned int i = 0; i < n; ++i) {
        const unsigned char *r = raw + 2 + (i * 5);
        unsigned int bx = ((unsigned int)r[0] << 8) | r[1];
        unsigned int by = ((unsigned int)r[2] << 8) | r[3];
        unsigned char dir = r[4] & 1;
        g_buzz.list[i].x         = (int)bx;
        g_buzz.list[i].y         = (int)by;
        g_buzz.list[i].start_x   = (int)bx;
        g_buzz.list[i].start_y   = (int)by;
        g_buzz.list[i].direction = dir;
        g_buzz.list[i].start_dir = dir;
        g_buzz.list[i].state     = BUZZ_STATE_FLYING;
        g_buzz.list[i].timer     = BUZZ_FLY_TICKS;
        g_buzz.list[i].active    = 1;
        g_buzz.list[i].step_tick = 0;
    }
    g_buzz.count = (int)n;
    jo_free(raw);
    g_buzz.ready = true;
}

/* Advance the wings/thrust overlay frame ids using the atlas's per-anim
 * cadence table -- speed=128 walker, per-frame duration from MET. */
static void buzz_overlay_advance(int anim_id, int16_t *fid, int16_t *timer)
{
    if (anim_id < 0 || anim_id >= g_buzz_atlas.anim_count) return;
    int fc  = g_buzz_atlas.anims[anim_id].frame_count;
    int spd = g_buzz_atlas.anims[anim_id].speed;
    int lp  = g_buzz_atlas.anims[anim_id].loop_index;
    if (fc <= 0 || spd <= 0) return;
    int first = g_buzz_atlas.anims[anim_id].first;
    *timer = (int16_t)(*timer + spd);
    int dur = (int)g_buzz_atlas.duration[first + *fid];
    while (dur > 0 && *timer >= dur) {
        *timer = (int16_t)(*timer - dur);
        *fid   = (int16_t)(*fid + 1);
        if (*fid >= fc) *fid = (int16_t)lp;
        if (*fid >= fc) *fid = (int16_t)(fc - 1);
        dur = (int)g_buzz_atlas.duration[first + *fid];
    }
}

static int buzz_overlay_sprite_id(int anim_id, int fid)
{
    if (anim_id < 0 || anim_id >= g_buzz_atlas.anim_count) return -1;
    int first = g_buzz_atlas.anims[anim_id].first;
    int idx = first + fid;
    if (idx < 0 || idx >= g_buzz_atlas.frame_total) return -1;
    return (int)g_buzz_atlas.sprite_id[idx];
}

void buzzbomber_tick_and_draw(const sms_world_t *w, player_t *p,
                              int cam_x, int cam_y)
{
    (void)w;
    if (!g_buzz.ready || !g_buzz_atlas.ready) return;

    int sonic_px = p->xpos >> 16;
    int sonic_py = p->ypos >> 16;

    /* Body walker (Fly / Shoot anim). */
    entity_atlas_tick(&g_buzz_atlas);
    /* Wings + thrust overlay walkers, ticked via the atlas cadence
     * table -- not the main animator (which is busy with the body). */
    buzz_overlay_advance(BUZZ_ANIM_WINGS,
                         &s_buzz_wings_frame_id, &s_buzz_wings_timer);
    buzz_overlay_advance(BUZZ_ANIM_THRUST,
                         &s_buzz_thrust_frame_id, &s_buzz_thrust_timer);

    for (int i = 0; i < g_buzz.count; ++i) {
        buzz_t *b = &g_buzz.list[i];
        if (!b->active) continue;

        /* === State machine (decomp State_Flying / State_Idle / DetectedPlayer) === */
        switch (b->state) {
        case BUZZ_STATE_FLYING:
            /* Step 1 px every BUZZ_STEP_TICKS (decomp velocity.x = +/-0x40000). */
            if (++b->step_tick >= BUZZ_STEP_TICKS) {
                b->step_tick = 0;
                b->x += b->direction ? 1 : -1;
            }
            if (--b->timer <= 0) {
                /* Decomp L177-182: flip direction, enter idle, reload timer. */
                b->direction ^= 1;
                b->timer = BUZZ_IDLE_TICKS;
                b->state = BUZZ_STATE_IDLE;
            } else {
                /* Detect: Sonic within shotRange = 96 px horizontally
                 * (decomp Create L52-54 + hitboxRange L56-59). Vertical
                 * range is +/- 256 px so we approximate large. */
                int rdx = sonic_px - b->x;
                int rdy = sonic_py - b->y;
                if (rdx > -96 && rdx < 96 && rdy > -16 && rdy < 256) {
                    b->state = BUZZ_STATE_DETECT;
                    b->timer = BUZZ_DETECT_TICKS;
                }
            }
            break;
        case BUZZ_STATE_IDLE:
            if (--b->timer <= 0) {
                b->timer = BUZZ_FLY_TICKS;
                b->state = BUZZ_STATE_FLYING;
            }
            break;
        case BUZZ_STATE_DETECT:
            /* Decomp State_DetectedPlayer L218-247 countdown.
             * Phase 2.4c stub: just play the charge anim (frame swap at
             * timer == 82) and revert at timer == 0. Projectile spawn
             * at timer == 45 deferred. */
            --b->timer;
            if (b->timer <= 0) {
                b->timer = BUZZ_FLY_TICKS;
                b->state = BUZZ_STATE_FLYING;
            }
            break;
        }

        /* === Cull === */
        int sx = b->x - cam_x;
        int sy = b->y - cam_y;
        if (sx < -BUZZ_W || sx > SCR_W + BUZZ_W ||
            sy < -BUZZ_H || sy > SCR_H + BUZZ_H) {
            /* Decomp CheckOffScreen L120-130: respawn from start when
             * far off. Approximate: when entity drifts > 512 px from
             * start, snap back. */
            if (b->x < b->start_x - 512 || b->x > b->start_x + 512) {
                b->x         = b->start_x;
                b->y         = b->start_y;
                b->direction = b->start_dir;
                b->state     = BUZZ_STATE_FLYING;
                b->timer     = BUZZ_FLY_TICKS;
            }
            continue;
        }

        /* === Hit-test (decomp CheckPlayerCollisions L132-153) ===
         * hitboxBadnik = { -24, -12, 24, 12 } around body center. */
        int dx = sonic_px - b->x;
        int dy = (sonic_py - SONIC_BODY_H / 2) - b->y;
        if (dx > BUZZ_HITBOX_LEFT - 8 && dx < BUZZ_HITBOX_RIGHT + 8 &&
            dy > BUZZ_HITBOX_TOP - 12 && dy < BUZZ_HITBOX_BOTTOM + 8) {
            int sonic_feet = sonic_py;
            int body_center = b->y;
            if (sonic_feet <= body_center + 4 &&
                p->ysp >= 0 && !p->onGround) {
                /* Stomp */
                b->active = 0;
                g_hud_score += 100;
                p->ysp = -PLAYER_FIXED(4.0);
                p->jumping = true;
                continue;
            } else {
                /* Side-hurt: zero rings + knockback (Phase 2.4c
                 * placeholder until Player_Hurt lands in Phase 2.5). */
                if (g_hud_rings > 0) g_hud_rings = 0;
                p->ysp = -PLAYER_FIXED(4.0);
                p->xsp = p->facing_left ? PLAYER_FIXED(2.0)
                                        : -PLAYER_FIXED(2.0);
                p->onGround = false;
                p->jumping  = true;
                continue;
            }
        }

        /* === Draw ===
         * Decomp Draw L22-38:
         *   - Body anim (frame 0 Fly or 1 Shoot depending on charge).
         *   - Wings (INK_ALPHA) -- drawn behind body except in projectile mode.
         *   - Thrust -- drawn only during flying.
         * Phase 2.4e v2: body sprite comes from the animator (current
         * anim driven by state); wings + thrust come from the overlay
         * walkers above. */
        int desired_body_anim = (b->state == BUZZ_STATE_DETECT &&
                                 b->timer < BUZZ_DETECT_CHARGE)
                                ? BUZZ_ANIM_SHOOT : BUZZ_ANIM_FLY;
        if (g_buzz_atlas.current_anim != desired_body_anim &&
            desired_body_anim < g_buzz_atlas.anim_count) {
            entity_atlas_play(&g_buzz_atlas, desired_body_anim);
        }
        int body_sprite = entity_atlas_current_sprite(&g_buzz_atlas);
        if (body_sprite < 0) continue;

        /* Flip when direction = FLIP_X. Decomp Create L44 sets drawFX |= FX_FLIP. */
        if (b->direction) jo_sprite_enable_horizontal_flip();

        /* Thrust (overlay walker) -- only during flying. */
        if (b->state == BUZZ_STATE_FLYING) {
            int thrust_sprite = buzz_overlay_sprite_id(
                BUZZ_ANIM_THRUST, s_buzz_thrust_frame_id);
            if (thrust_sprite >= 0) {
                int tx = b->direction ? b->x - 16 : b->x + 16;
                int tsx = tx - cam_x;
                jo_sprite_draw3D(thrust_sprite,
                                 (tsx + BUZZ_W / 2) - 160,
                                 (sy + BUZZ_H / 2) - 112,
                                 146);
            }
        }

        /* Body */
        jo_sprite_draw3D(body_sprite,
                         (sx + BUZZ_W / 2) - 160,
                         (sy + BUZZ_H / 2) - 112,
                         145);

        /* Wings (overlay walker). */
        int wings_sprite = buzz_overlay_sprite_id(
            BUZZ_ANIM_WINGS, s_buzz_wings_frame_id);
        if (wings_sprite >= 0) {
            jo_sprite_draw3D(wings_sprite,
                             (sx + BUZZ_W / 2) - 160,
                             (sy + BUZZ_H / 2) - 112,
                             144);
        }

        if (b->direction) jo_sprite_disable_horizontal_flip();
    }
}
