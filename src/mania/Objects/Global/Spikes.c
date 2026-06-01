/* Phase 2.4c Task #140 — Spikes port.
 *
 * Decomp source: tools/_decomp_raw/SonicMania_Objects_Global_Spikes.c
 *   - Create  L320-386 — type/count/direction/hitbox setup
 *   - Update  L12-308   — move state machine + collision branches
 *     (Phase 2.4c subset: static-only, no Ice/Plus path)
 *   - hitbox vertical    L355-358 = { -8*count, -16, 8*count, 16 }
 *   - hitbox horizontal  L373-376 = { -16, -8*count, 16, 8*count }
 *   - StageLoad L388-411 — Spikes->aniFrames = "Global/Spikes.bin"
 *
 * Saturn-side asset layout:
 *   cd/SPIKES.SPR  - u16 BE frame_count + u16 BE w + u16 BE h
 *                    + frame_count * w*h * u16 BGR1555.
 *                    Frame 0 = vertical 4-spike block (decomp anim 0).
 *                    Frame 1 = horizontal 4-spike block (decomp anim 1).
 *   cd/GHZ1SPIKE.BIN - u16 BE count + count * (u16 BE x, u16 BE y,
 *                                              u8 type, u8 count).
 *                     type encodes (direction<<1 | typeMask) per decomp
 *                     Spikes_Create L333-335. For Phase 2.4c we accept
 *                     0 = vertical-up and 1 = vertical-down and
 *                     2 = horizontal-right and 3 = horizontal-left.
 *                     count = visible spike-section width (decomp default 2). */

#include "Spikes.h"
#include "../Common/Entities.h"   /* g_hud_rings, sfx surrogate */
#include "../../../rsdk/entity_atlas.h"

#include <jo/jo.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define SCR_W            320
#define SCR_H            224

#define SPIKE_MAX        64       /* GHZ1 has 41; pad headroom */
/* Phase 2.4e v2: SPIKES.SP2 ships 2 native-size frames (V 32x16 + H 16x32)
 * per audit row 5. We use the native frame size from the atlas per draw. */
#define SPIKE_CELL_W     32
#define SPIKE_CELL_H     32

/* Spike-type encoding per decomp Spikes.h SpikeTypes enum:
 *   0 = UP    (vertical, spikes point up,    C_TOP)
 *   1 = DOWN  (vertical, spikes point down,  C_BOTTOM)
 *   2 = LEFT  (horizontal, spikes point left, C_LEFT)
 *   3 = RIGHT (horizontal, spikes point right, C_RIGHT) */
#define SPIKE_TYPE_UP    0
#define SPIKE_TYPE_DOWN  1
#define SPIKE_TYPE_LEFT  2
#define SPIKE_TYPE_RIGHT 3

/* SONIC_BODY_H mirrors the same constant used in Common/Entities.c. */
#define SONIC_BODY_H    40

typedef struct {
    int           x;             /* world X (pixels) */
    int           y;             /* world Y (pixels) */
    unsigned char type;          /* SpikeTypes enum */
    unsigned char count;         /* spike-section width (decomp L325-326: min 2) */
    unsigned char active;        /* 1 = drawn / collided */
} spike_t;

typedef struct {
    spike_t list[SPIKE_MAX];
    int     count;
    bool    ready;
} spikes_module_t;

static spikes_module_t g_spikes;

void spikes_load_assets(void)
{
    g_spikes.ready = false;

    /* Phase 2.4e v2 (Task #144) -- SPR2+MET via entity_atlas loader.
     * SPIKES.SP2/.MET ships 2 anims (Spikes V + Spikes H), each
     * single-frame (decomp ships static -- see audit row 5). The
     * walker won't advance frames (frame_count=1) but the atlas
     * exposes per-frame pivots and native sizes. */
    if (!entity_atlas_load(&g_spikes_atlas, "SPIKES")) return;
    entity_atlas_play(&g_spikes_atlas, 0);  /* default Spikes V */

    /* Entity positions: GHZ1SPIKE.BIN format:
     *   u16 BE count
     *   count * (u16 BE x, u16 BE y, u8 type, u8 count) */
    int blen = 0;
    unsigned char *raw = (unsigned char *)jo_fs_read_file("GHZ1SPIKE.BIN", &blen);
    if (!raw || blen < 2) { if (raw) jo_free(raw); return; }
    unsigned int n = ((unsigned int)raw[0] << 8) | raw[1];
    if (n > SPIKE_MAX) n = SPIKE_MAX;
    for (unsigned int i = 0; i < n; ++i) {
        const unsigned char *r = raw + 2 + (i * 6);
        unsigned int sx = ((unsigned int)r[0] << 8) | r[1];
        unsigned int sy = ((unsigned int)r[2] << 8) | r[3];
        unsigned char st = r[4];
        unsigned char sc = r[5];
        if (sc < 2) sc = 2;     /* decomp Spikes_Create L325-326 */
        if (st > SPIKE_TYPE_RIGHT) st = SPIKE_TYPE_UP;
        g_spikes.list[i].x      = (int)sx;
        g_spikes.list[i].y      = (int)sy;
        g_spikes.list[i].type   = st;
        g_spikes.list[i].count  = sc;
        g_spikes.list[i].active = 1;
    }
    g_spikes.count = (int)n;
    jo_free(raw);
    g_spikes.ready = true;
}

/* Decomp hitbox per Spikes_Create L355-358 + L373-376. */
static inline void spike_hitbox(const spike_t *s,
                                int *out_left, int *out_top,
                                int *out_right, int *out_bottom)
{
    int half = 8 * s->count;
    if (s->type == SPIKE_TYPE_UP || s->type == SPIKE_TYPE_DOWN) {
        *out_left   = -half;
        *out_right  =  half;
        *out_top    = -16;
        *out_bottom =  16;
    } else {
        *out_left   = -16;
        *out_right  =  16;
        *out_top    = -half;
        *out_bottom =  half;
    }
}

void spikes_tick_and_draw(const sms_world_t *w, player_t *p,
                          int cam_x, int cam_y)
{
    (void)w;
    if (!g_spikes.ready || !g_spikes_atlas.ready) return;

    /* Spikes anims are decomp-static (1 frame/anim per audit row 5);
     * the walker is still called for symmetry but won't advance. */
    entity_atlas_tick(&g_spikes_atlas);

    int sonic_px = p->xpos >> 16;
    int sonic_py = p->ypos >> 16;

    for (int i = 0; i < g_spikes.count; ++i) {
        spike_t *s = &g_spikes.list[i];
        if (!s->active) continue;

        int sx = s->x - cam_x;
        int sy = s->y - cam_y;
        if (sx < -SPIKE_CELL_W || sx > SCR_W + SPIKE_CELL_W ||
            sy < -SPIKE_CELL_H || sy > SCR_H + SPIKE_CELL_H) continue;

        /* === Collision (decomp Spikes_Update L78-191; non-Plus branch) ===
         *
         * Spike contact: Sonic AABB-overlaps the spike hitbox. The
         * decomp non-Plus path (L163-191) calls Player_Hurt only on the
         * face that matches self->type, and only if the player's
         * velocity on that axis is zero (which on the Saturn side
         * approximates to "Sonic is grounded and stopped" — but we
         * relax to "any contact" because Phase 2.2 Player doesn't
         * track per-frame velocity-zero precisely). */
        int hl, ht, hr, hb;
        spike_hitbox(s, &hl, &ht, &hr, &hb);
        int dx = sonic_px - s->x;
        int dy = (sonic_py - SONIC_BODY_H / 2) - s->y;
        if (dx > hl - 12 && dx < hr + 12 &&
            dy > ht - 24 && dy < hb + 12) {
            /* Phase 2.4c hurt: zero rings + knockback (matches
             * existing badnik-side-hit behavior in motobug_tick_and_draw). */
            if (g_hud_rings > 0) {
                g_hud_rings = 0;
            }
            /* Knockback: small reversal away from the spike axis. */
            if (s->type == SPIKE_TYPE_UP) {
                p->ysp = -PLAYER_FIXED(4.0);
            } else if (s->type == SPIKE_TYPE_DOWN) {
                p->ysp =  PLAYER_FIXED(2.0);
            } else if (s->type == SPIKE_TYPE_LEFT) {
                p->xsp =  PLAYER_FIXED(2.0);
            } else {
                p->xsp = -PLAYER_FIXED(2.0);
            }
            p->onGround = false;
            p->jumping  = true;
            /* fall through to draw — spikes don't despawn on hit. */
        }

        /* === Draw ===
         *
         * Anim 0 = Spikes V (vertical), Anim 1 = Spikes H (horizontal).
         * The atlas was built with the decomp's per-frame pivot so the
         * draw anchors at the entity center. */
        int anim = (s->type <= SPIKE_TYPE_DOWN) ? 0 : 1;
        if (g_spikes_atlas.current_anim != anim &&
            anim < g_spikes_atlas.anim_count) {
            entity_atlas_play(&g_spikes_atlas, anim);
        }
        int sprite_id = entity_atlas_current_sprite(&g_spikes_atlas);
        if (sprite_id < 0) continue;
        int fw = 0, fh = 0;
        entity_atlas_size(&g_spikes_atlas, &fw, &fh);
        if (fw == 0) fw = SPIKE_CELL_W;
        if (fh == 0) fh = SPIKE_CELL_H;

        /* Flip for DOWN / RIGHT variants (FLIP_Y / FLIP_X per decomp
         * Spikes_Create L346 + L364). */
        bool vflip = (s->type == SPIKE_TYPE_DOWN);
        bool hflip = (s->type == SPIKE_TYPE_RIGHT);
        if (vflip) jo_sprite_enable_vertical_flip();
        if (hflip) jo_sprite_enable_horizontal_flip();
        jo_sprite_draw3D(sprite_id,
                         (sx + fw / 2) - 160,
                         (sy + fh / 2) - 112,
                         145);
        if (hflip) jo_sprite_disable_horizontal_flip();
        if (vflip) jo_sprite_disable_vertical_flip();
    }
}
