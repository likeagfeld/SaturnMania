/* Phase 2.4c.2 Task #147 — SpikeLog port.
 *
 * Decomp source: tools/_decomp_raw/SonicMania_Objects_GHZ_SpikeLog.c
 *   - StaticUpdate L20             — SpikeLog->timer = Zone->timer/3 & 0x1F
 *   - Create       L28-41          — drawFX|=FX_FLIP, frame*=4 (non-editor),
 *                                    SetSpriteAnimation(anim 0)
 *   - StageLoad    L43-57          — aniFrames + hitboxSpikeLog/hitboxBurnLog
 *                                    hitboxSpikeLog = {-8, -16, 8, 0}
 *   - State_Main   L59-113         — animator.frameID = (frame+timer) & 0x1F
 *                                    hazard when (frameID & ~3) == 8 (frames
 *                                    8..11 = spikes extended)
 *   - State_Burn   L115-132        — fire-shield path; deferred Phase 2.5.
 *
 * Saturn-side asset layout:
 *   cd/SPIKELOG.SP2/.MET — 32-frame single-anim (per build_entity_atlas).
 *   cd/GHZ1LOG.BIN — u16 BE count + count * (u16 BE x, u16 BE y, u8 frame, u8 _pad).
 *     "frame" is the editor-supplied phase offset (0..7) per decomp Create L37
 *     ("self->frame *= 4" -> 0/4/8/12/16/20/24/28). */

#include "SpikeLog.h"
#include "../Common/Entities.h"
#include "../../../rsdk/entity_atlas.h"

#include <jo/jo.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define SCR_W           320
#define SCR_H           224
#define SONIC_BODY_H    40

#define SPIKELOG_MAX    64        /* GHZ1 has 61; trim per BSS-overflow audit */
#define SPIKELOG_W      32
#define SPIKELOG_H      64

/* Decomp StageLoad L48-51: hitboxSpikeLog = {-8, -16, 8, 0}. */
#define SPIKELOG_HL     (-8)
#define SPIKELOG_HT     (-16)
#define SPIKELOG_HR     ( 8)
#define SPIKELOG_HB     ( 0)

typedef struct {
    int           x;            /* world X (pixels) */
    int           y;            /* world Y (pixels) */
    unsigned char frame;        /* editor frame offset * 4 (decomp Create L37) */
    unsigned char active;
} spikelog_t;

typedef struct {
    spikelog_t list[SPIKELOG_MAX];
    int        count;
    uint16_t   global_timer;   /* mirrors decomp SpikeLog->timer */
    uint16_t   zone_timer;     /* mirrors decomp Zone->timer */
    bool       ready;
} spikelog_module_t;

static spikelog_module_t g_spikelog;

void spikelog_load_assets(void)
{
    g_spikelog.ready = false;
    g_spikelog.zone_timer = 0;
    g_spikelog.global_timer = 0;

    if (!entity_atlas_load(&g_spikelog_atlas, "SPIKELOG")) return;
    entity_atlas_play(&g_spikelog_atlas, 0);

    int blen = 0;
    unsigned char *raw = (unsigned char *)jo_fs_read_file("GHZ1LOG.BIN", &blen);
    if (!raw || blen < 2) { if (raw) jo_free(raw); return; }
    unsigned int n = ((unsigned int)raw[0] << 8) | raw[1];
    if (n > SPIKELOG_MAX) n = SPIKELOG_MAX;
    for (unsigned int i = 0; i < n; ++i) {
        const unsigned char *r = raw + 2 + (i * 6);
        unsigned int sx = ((unsigned int)r[0] << 8) | r[1];
        unsigned int sy = ((unsigned int)r[2] << 8) | r[3];
        unsigned char frame = r[4];
        /* Decomp Create L37: self->frame *= 4 outside editor scope. */
        g_spikelog.list[i].x      = (int)sx;
        g_spikelog.list[i].y      = (int)sy;
        g_spikelog.list[i].frame  = (unsigned char)((frame * 4) & 0x1F);
        g_spikelog.list[i].active = 1;
    }
    g_spikelog.count = (int)n;
    jo_free(raw);
    g_spikelog.ready = true;
}

/* Decomp State_Main L62 + StaticUpdate L20:
 *   timer = (zone_timer / 3) & 0x1F
 *   frameID = (frame + timer) & 0x1F. */
static inline unsigned char spikelog_frame_id(const spikelog_t *s,
                                              unsigned char timer)
{
    return (unsigned char)((s->frame + timer) & 0x1F);
}

/* Decomp State_Main L66: hazard window is frameID & 0xFFFFFFFC == 8,
 * i.e. frames 8/9/10/11 (spikes extended). */
static inline bool spikelog_hazard_active(unsigned char fid)
{
    return ((fid & 0xFCu) == 8);
}

void spikelog_tick_and_draw(const sms_world_t *w, player_t *p,
                            int cam_x, int cam_y)
{
    (void)w;
    if (!g_spikelog.ready || !g_spikelog_atlas.ready) return;

    /* Mirror decomp Zone->timer (a free-running u32 incremented every
     * frame; only the low bits matter here because of the &0x1F mask).
     * Decomp StaticUpdate L20: SpikeLog->timer = Zone->timer/3 & 0x1F. */
    g_spikelog.zone_timer++;
    g_spikelog.global_timer = (uint16_t)((g_spikelog.zone_timer / 3) & 0x1F);
    unsigned char timer = (unsigned char)g_spikelog.global_timer;

    int sonic_px = p->xpos >> 16;
    int sonic_py = p->ypos >> 16;

    int sprite_id_base = -1;
    int fw = SPIKELOG_W, fh = SPIKELOG_H;
    /* All 32 frames share the same atlas; we look up the sprite_id and
     * size per-entity inside the loop because the atlas walker only
     * tracks one current_frame slot but we need 32 random-access ids. */

    for (int i = 0; i < g_spikelog.count; ++i) {
        spikelog_t *s = &g_spikelog.list[i];
        if (!s->active) continue;

        int sx = s->x - cam_x;
        int sy = s->y - cam_y;
        if (sx < -SPIKELOG_W || sx > SCR_W + SPIKELOG_W ||
            sy < -SPIKELOG_H || sy > SCR_H + SPIKELOG_H) continue;

        unsigned char fid = spikelog_frame_id(s, timer);

        /* Atlas frame index = fid (single anim 0..31). */
        int idx = fid;
        if (idx >= g_spikelog_atlas.frame_total) {
            idx = g_spikelog_atlas.frame_total - 1;
        }
        int sid = (int)g_spikelog_atlas.sprite_id[idx];
        if (sid < 0) continue;
        fw = (int)g_spikelog_atlas.width[idx];
        fh = (int)g_spikelog_atlas.height[idx];
        if (fw == 0) fw = SPIKELOG_W;
        if (fh == 0) fh = SPIKELOG_H;
        (void)sprite_id_base;

        /* Hazard: spikes-extended phase (decomp L66-99). Non-Plus,
         * non-fire-shield: Player_Hurt -> Saturn-side zero rings +
         * vertical knockback. */
        if (spikelog_hazard_active(fid)) {
            int dx = sonic_px - s->x;
            int dy = (sonic_py - SONIC_BODY_H / 2) - s->y;
            /* Pad hitbox per the existing Spikes-port convention so
             * Sonic's 32-px-wide bbox overlaps the spike's -8..8 strip. */
            if (dx > SPIKELOG_HL - 12 && dx < SPIKELOG_HR + 12 &&
                dy > SPIKELOG_HT - 24 && dy < SPIKELOG_HB + 12) {
                if (g_hud_rings > 0) g_hud_rings = 0;
                p->ysp = -PLAYER_FIXED(4.0);
                p->xsp = (sonic_px < s->x) ? -PLAYER_FIXED(2.0)
                                           :  PLAYER_FIXED(2.0);
                p->onGround = false;
                p->jumping  = true;
            }
        }

        /* Draw. Decomp Create L31: drawFX |= FX_FLIP -- direction unset
         * for the visible-default path so FLIP_NONE is used. */
        jo_sprite_draw3D(sid,
                         (sx + fw / 2) - 160,
                         (sy + fh / 2) - 112,
                         145);
    }
}
