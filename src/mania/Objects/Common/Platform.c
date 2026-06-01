/* Phase 2.4c.2 Task #147 — Platform port.
 *
 * Decomp source: tools/_decomp_raw/SonicMania_Objects_Common_Platform.c
 *   - Update L12-95     — drawPos integration + collision dispatch
 *   - Create L150-396   — type-switched init (we port FIXED/FALL/LINEAR/
 *                          CIRCULAR; rest fall through to FIXED)
 *   - StageLoad L398-474 — aniFrames per zone (we use GHZ branch only)
 *   - State_Fixed   L484-494 — drawPos = centerPos + stoodAngle wobble
 *   - State_Linear  L496-509 — drawPos = centerPos + sin1024(speed*timer)*amp
 *   - State_Circular         — orbit via amp.x*cos + amp.y*sin
 *   - State_Falling L577+    — gravity once stoodAngle reaches threshold
 *
 * Saturn-side asset:
 *   cd/PLATFORM.SP2/.MET (7 frames, 2 anims).
 *   cd/GHZ1PLAT.BIN format (per build_phase2_4c2_entity_coords.py):
 *     u16 BE count
 *     count * (u16 BE x, u16 BE y, u8 type, u8 collision,
 *              i16 BE amp_x, i16 BE amp_y, i16 BE speed, i16 BE angle,
 *              u8 frameID, u8 hasTension) */

#include "Platform.h"
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

#define PLATFORM_MAX    64   /* GHZ1 has 59; trim per BSS-overflow audit */

/* Decomp PlatformTypes (Platform.h L6-24). */
#define PLT_FIXED    0
#define PLT_FALL     1
#define PLT_LINEAR   2
#define PLT_CIRCULAR 3

/* Decomp PlatformCollisionTypes (Platform.h L26-43). */
#define PLC_PLATFORM 0   /* top-only stand */
#define PLC_SOLID    1   /* full AABB push */

/* Tiny sin/cos lookup at 1024-sample resolution to mirror RSDK.Sin1024 /
 * Cos1024 used by decomp State_Linear / State_Circular. Output is signed
 * Q1.15 (range -32768..+32767). Mirrors decomp output range. */
static int16_t sin1024_lut[256];
static bool s_sin_inited = false;

static void platform_init_sin_lut(void)
{
    if (s_sin_inited) return;
    /* Quarter-wave LUT: index 0..255 covers 0..pi/2. */
    /* Use the standard polynomial approximation against a unit circle;
     * sample i corresponds to angle = i * (pi/2) / 256. */
    for (int i = 0; i < 256; ++i) {
        /* Bhaskara's approximation -- sufficient for visual platform
         * motion (decomp's RSDK.Sin1024 is itself a LUT). */
        int x = i * 1024 / 256;        /* x in 0..1024 (1024 = pi/2). */
        /* sin(theta) ~= 16*x*(1024-x) / (5*1024^2 - 4*x*(1024-x))
         * where theta = x * pi/2 / 1024. We map directly via integer. */
        long lx = (long)x;
        long u = lx * (1024 - lx);
        long num = 16L * u * 32767L;
        long den = 5L * 1024L * 1024L - 4L * u;
        if (den == 0) den = 1;
        long r = num / den;
        if (r > 32767) r = 32767;
        sin1024_lut[i] = (int16_t)r;
    }
    s_sin_inited = true;
}

/* Returns Q1.15 sin of (theta * pi/512) where theta is in 0..1023.
 * Decomp's RSDK.Sin1024 returns 16-bit fixed in same range. */
static int sin1024_q(int theta)
{
    theta &= 1023;
    int quad = theta >> 8;        /* 0..3 */
    int phase = theta & 255;
    int v;
    switch (quad) {
    case 0: v =  sin1024_lut[phase]; break;
    case 1: v =  sin1024_lut[255 - phase]; break;
    case 2: v = -sin1024_lut[phase]; break;
    default: v = -sin1024_lut[255 - phase]; break;
    }
    return v;
}

static int cos1024_q(int theta) { return sin1024_q(theta + 256); }

/* Decomp Sin256 (used by State_Fixed L490 for stoodAngle wobble). Same
 * sine but 256 samples per cycle. We just downconvert from sin1024_q.
 * Unused for the Phase 2.4c.2 subset (we ignore stoodAngle wobble); kept
 * for future ports. */
static int sin256_q(int theta) __attribute__((unused));
static int sin256_q(int theta) { return sin1024_q((theta & 0xFF) * 4); }

typedef struct {
    int           x;            /* world (pixel) -- decomp centerPos */
    int           y;
    int           draw_x;       /* world (pixel) -- decomp drawPos */
    int           draw_y;
    int16_t       amp_x;
    int16_t       amp_y;
    int16_t       speed;
    int16_t       angle;
    int16_t       timer;
    int16_t       fall_vy;      /* PLT_FALL gravity accumulator */
    int8_t        frame_id;     /* visible frame from atlas anim 0 */
    uint8_t       type;
    uint8_t       collision;
    uint8_t       stood;        /* Sonic standing flag (last frame) */
    uint8_t       fall_state;   /* 0=stable, 1=falling */
    uint8_t       active;
} platform_t;

typedef struct {
    platform_t list[PLATFORM_MAX];
    int        count;
    uint16_t   zone_timer;
    bool       ready;
} platform_module_t;

static platform_module_t g_platform;

void platform_load_assets(void)
{
    g_platform.ready = false;
    g_platform.zone_timer = 0;
    platform_init_sin_lut();

    if (!entity_atlas_load(&g_platform_atlas, "PLATFORM")) return;
    entity_atlas_play(&g_platform_atlas, 0);

    int blen = 0;
    unsigned char *raw = (unsigned char *)jo_fs_read_file("GHZ1PLAT.BIN", &blen);
    if (!raw || blen < 2) { if (raw) jo_free(raw); return; }
    unsigned int n = ((unsigned int)raw[0] << 8) | raw[1];
    if (n > PLATFORM_MAX) n = PLATFORM_MAX;
    const int REC = 14;
    for (unsigned int i = 0; i < n; ++i) {
        const unsigned char *r = raw + 2 + (i * REC);
        unsigned int px = ((unsigned int)r[0] << 8) | r[1];
        unsigned int py = ((unsigned int)r[2] << 8) | r[3];
        uint8_t type = r[4];
        uint8_t coll = r[5];
        int16_t ax = (int16_t)(((uint16_t)r[6] << 8) | r[7]);
        int16_t ay = (int16_t)(((uint16_t)r[8] << 8) | r[9]);
        int16_t sp = (int16_t)(((uint16_t)r[10] << 8) | r[11]);
        int16_t an = (int16_t)(((uint16_t)r[12] << 8) | 0); /* unused trim */
        (void)an;
        uint8_t fid = r[12];
        /* (r[13] = hasTension, not used in Phase 2.4c.2 subset) */

        g_platform.list[i].x         = (int)px;
        g_platform.list[i].y         = (int)py;
        g_platform.list[i].draw_x    = (int)px;
        g_platform.list[i].draw_y    = (int)py;
        g_platform.list[i].amp_x     = ax;
        g_platform.list[i].amp_y     = ay;
        g_platform.list[i].speed     = sp;
        g_platform.list[i].angle     = 0;
        g_platform.list[i].timer     = 0;
        g_platform.list[i].fall_vy   = 0;
        g_platform.list[i].frame_id  = (int8_t)fid;
        g_platform.list[i].type      = type;
        g_platform.list[i].collision = coll;
        g_platform.list[i].stood     = 0;
        g_platform.list[i].fall_state = 0;
        g_platform.list[i].active    = 1;
    }
    g_platform.count = (int)n;
    jo_free(raw);
    g_platform.ready = true;
}

/* Per-platform state advance. Decomp State_* mapped to integer-pixel
 * approximation. */
static void platform_state_tick(platform_t *pl)
{
    switch (pl->type) {
    case PLT_FIXED:
    default: {
        /* Decomp State_Fixed L484-494. */
        pl->draw_x = pl->x;
        pl->draw_y = pl->y;
        break;
    }
    case PLT_FALL: {
        /* Decomp State_Fall: stand-triggered fall. When stood, start
         * a 30-tick countdown, then enter falling (gravity). */
        if (pl->fall_state == 0) {
            pl->draw_x = pl->x;
            pl->draw_y = pl->y;
            if (pl->stood) {
                pl->timer++;
                if (pl->timer >= 30) {
                    pl->fall_state = 1;
                    pl->fall_vy = 0;
                }
            }
        } else {
            /* gravity = 0x3800 in decomp = ~0.22 px/tick^2. Use 1 every
             * 4 ticks in integer space. */
            pl->fall_vy++;
            pl->draw_y += pl->fall_vy / 4;
        }
        break;
    }
    case PLT_LINEAR: {
        /* Decomp State_Linear L496-509:
         *   drawPos.x = centerPos.x + amp.x * Sin1024(speed * timer)
         *   drawPos.y = centerPos.y + amp.y * Sin1024(speed * timer)
         * In integer-pixel space we approximate amp at unit and divide
         * the Q15 sin by 32. */
        pl->timer++;
        int theta = (pl->speed * pl->timer) & 1023;
        int s = sin1024_q(theta);            /* Q15 in [-32767..32767] */
        pl->draw_x = pl->x + ((pl->amp_x * s) >> 15);
        pl->draw_y = pl->y + ((pl->amp_y * s) >> 15);
        break;
    }
    case PLT_CIRCULAR: {
        /* Decomp State_Circular: x = centerPos.x + amp.x*cos(theta)
         *                        y = centerPos.y + amp.y*sin(theta). */
        pl->angle = (int16_t)((pl->angle + (pl->speed ? pl->speed : 1)) & 1023);
        int sv = sin1024_q(pl->angle);
        int cv = cos1024_q(pl->angle);
        pl->draw_x = pl->x + ((pl->amp_x * cv) >> 15);
        pl->draw_y = pl->y + ((pl->amp_y * sv) >> 15);
        break;
    }
    }
}

/* Top-only platform collision (decomp Collision_Platform). Sonic's feet
 * land on the platform top when descending and within the bbox X range. */
static void platform_collide_top(platform_t *pl, player_t *p,
                                 int half_w, int half_h)
{
    int sonic_px = p->xpos >> 16;
    int sonic_py = p->ypos >> 16;
    int sonic_feet = sonic_py;
    int top    = pl->draw_y - half_h;
    int left   = pl->draw_x - half_w;
    int right  = pl->draw_x + half_w;
    if (sonic_px < left - 4 || sonic_px > right + 4) {
        pl->stood = 0;
        return;
    }
    /* Top hit: only when descending or grounded and within tolerance. */
    int dy = sonic_feet - top;
    bool descending = (p->ysp >= 0);
    if (descending && dy >= -4 && dy <= 12) {
        /* Snap feet to top, zero ysp, set ground state. */
        int new_y_top = top;
        p->ypos = (long)new_y_top << 16;
        p->ysp = 0;
        p->onGround = true;
        p->jumping  = false;
        pl->stood = 1;
    } else if (sonic_feet > top + 16) {
        pl->stood = 0;
    }
}

void platform_tick_and_draw(const sms_world_t *w, player_t *p,
                            int cam_x, int cam_y)
{
    (void)w;
    if (!g_platform.ready || !g_platform_atlas.ready) return;

    g_platform.zone_timer++;

    for (int i = 0; i < g_platform.count; ++i) {
        platform_t *pl = &g_platform.list[i];
        if (!pl->active) continue;

        platform_state_tick(pl);

        int sx = pl->draw_x - cam_x;
        int sy = pl->draw_y - cam_y;
        if (sx < -96 || sx > SCR_W + 96 ||
            sy < -96 || sy > SCR_H + 96) {
            /* If a PLT_FALL has fallen far enough off-screen, deactivate. */
            if (pl->type == PLT_FALL && pl->fall_state == 1 &&
                sy > SCR_H + 256) {
                pl->active = 0;
            }
            continue;
        }

        /* Pick sprite: frame_id from editor (decomp Create L162). The
         * atlas first frame is index 0; we clamp to atlas range. */
        int frame_id = (int)pl->frame_id;
        if (frame_id < 0) frame_id = 0;
        if (frame_id >= g_platform_atlas.frame_total) {
            frame_id = g_platform_atlas.frame_total - 1;
        }
        int sid = (int)g_platform_atlas.sprite_id[frame_id];
        if (sid < 0) continue;
        int fw = (int)g_platform_atlas.width[frame_id];
        int fh = (int)g_platform_atlas.height[frame_id];
        if (fw == 0) fw = 64;
        if (fh == 0) fh = 32;

        /* Collision (top-only by default; PLT_FALL still top-only). */
        if (pl->collision == PLC_PLATFORM || pl->collision == PLC_SOLID) {
            platform_collide_top(pl, p, fw / 2, fh / 2);
            /* Carry Sonic with the platform (decomp Update L33-66
             * collisionOffset). When Sonic is stood, mirror the
             * delta-x onto the player. */
            if (pl->stood) {
                /* The previous draw_x is stored implicitly via integration;
                 * since platform_state_tick re-derives draw_x from amp/sin
                 * each call, we approximate carry by adding amp_x/8 step
                 * when LINEAR/CIRCULAR. */
                if (pl->type == PLT_LINEAR || pl->type == PLT_CIRCULAR) {
                    int theta_now = (pl->speed * pl->timer) & 1023;
                    int theta_prev = (pl->speed * (pl->timer - 1)) & 1023;
                    int dx_carry = 0;
                    if (pl->type == PLT_LINEAR) {
                        dx_carry = (pl->amp_x *
                                    (sin1024_q(theta_now) -
                                     sin1024_q(theta_prev))) >> 15;
                    } else {
                        dx_carry = (pl->amp_x *
                                    (cos1024_q(pl->angle) -
                                     cos1024_q((pl->angle - pl->speed) & 1023))) >> 15;
                    }
                    p->xpos += ((long)dx_carry) << 16;
                }
            }
        }

        jo_sprite_draw3D(sid,
                         (sx + fw / 2) - 160,
                         (sy + fh / 2) - 112,
                         144);     /* below standard z=145 entities */
    }
}
