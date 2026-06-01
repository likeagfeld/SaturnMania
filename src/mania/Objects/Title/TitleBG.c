/* Phase 1.2 — TitleBG class body.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c`
 * (Christian Whitehead/Simon Thomley/Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Decomp behaviors mirrored:
 *   * 5 sub-types: MOUNTAIN1, MOUNTAIN2 (INK_BLEND), REFLECTION (INK_ADD),
 *     WATERSPARKLE (INK_ADD), WINGSHINE (INK_MASKED + bounce-up timer).
 *   * StaticUpdate rotates palette indices 140-143 every 6 frames
 *     (decomp:42-45 — water shimmer).
 *   * SetupFX masks palette index 55 (clouds bg shimmer).
 *
 * Saturn-side adaptations:
 *   * Scanline FX (TitleBG_Scanline_Clouds / _Island) are deferred to
 *     Phase 2 (require VDP2 H-IRQ raster table programming). The callback
 *     pointers are stored on the layer but not invoked.
 *   * Without the .bin asset shipped the per-sub-type animator stays empty
 *     (frame_count == 0) and Draw early-returns. The palette rotation in
 *     StaticUpdate still fires (it doesn't depend on the asset). */

#include "TitleBG.h"
#include "Title3DSprite.h"
#include "TitleAssets.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/drawing.h"
#include "../../../rsdk/scene.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/palette.h"
#include "../../../rsdk/tilelayer.h"
#include "../../../rsdk/math.h"

#include <stdint.h>

/* Phase 1.23 GAP A — `mania_set_active_list_id` removed: animator-borne
 * list_id replaces the prior global hack. */
extern void mania_set_titlebg_list_id(int v);
extern int  mania_get_titlebg_list_id(void);

/* Decomp TitleBG.c:12-30 — Update. */
void TitleBG_Update(void)
{
    RSDK_THIS(TitleBG);
    if (self->type == TITLEBG_WINGSHINE) {
        self->position.y += 0x10000;
        ++self->timer;
        if (self->timer == 32) {
            self->timer = 0;
            self->position.y -= 0x200000;
        }
    } else {
        self->position.x -= 0x10000;
        if (self->position.x < -0x800000)
            self->position.x += 0x3000000;
    }
}

void TitleBG_LateUpdate(void) {}

/* Decomp TitleBG.c:34-46 — StaticUpdate.
 *
 * `TitleBG->timer += 0x8000` is a global scanline-FX accumulator used by
 * Scanline_Clouds; preserved structurally even though the scanline path
 * isn't wired on Saturn.
 *
 * Palette rotation: every 6 ticks rotate CRAM indices 140..143 (water
 * shimmer band). */
void TitleBG_StaticUpdate(void)
{
    TitleBG->timer += 0x8000;
    TitleBG->timer &= 0x7FFFFFFF;

    ++TitleBG->angle;
    TitleBG->angle &= 0x3FF;

    if (++TitleBG->palTimer == 6) {
        TitleBG->palTimer = 0;
        rsdk_rotate_palette(0, 140, 143, false);
    }
}

/* Decomp TitleBG.c:48-54 — Draw. */
void TitleBG_Draw(void)
{
    RSDK_THIS(TitleBG);
    /* Phase 1.23 GAP A — animator-borne list_id; no global setter needed. */

    rsdk_set_clip_bounds(0, 0, ScreenInfo->size.x, ScreenInfo->size.y);
    rsdk_draw_sprite_ex(&self->animator,
                        self->position.x, self->position.y,
                        self->direction, self->inkEffect,
                        self->alpha, self->drawGroup, false);
}

/* Decomp TitleBG.c:56-85 — Create. */
void TitleBG_Create(void *data)
{
    (void)data;
    RSDK_THIS(TitleBG);
    if (!SceneInfo->inEditor) {
        rsdk_set_sprite_animation(TitleBG->aniFrames, (int)self->type,
                                  &self->animator, true, 0);
        self->active    = ACTIVE_NORMAL;
        self->visible   = false;
        self->drawGroup = 1;
        self->alpha     = 0xFF;
        self->drawFX    = FX_FLIP;

        switch (self->type) {
            case TITLEBG_MOUNTAIN2:
                self->inkEffect = INK_BLEND;
                break;
            case TITLEBG_REFLECTION:
            case TITLEBG_WATERSPARKLE:
                self->inkEffect = INK_ADD;
                self->alpha     = 0x80;
                break;
            case TITLEBG_WINGSHINE:
                self->drawGroup = 4;
                self->inkEffect = INK_MASKED;
                break;
            default: break;
        }
    }
}

/* Decomp TitleBG.c:87-92 — StageLoad.
 *
 * Saturn-side adaptation: rsdk_load_sprite_animation returns -1 (no .bin
 * shipped); the NBG2 TITLE.DAT backdrop serves as the visible BG. Fall
 * back to the pre-seeded list_id so the per-sub-type animator stays valid. */
void TitleBG_StageLoad(void)
{
    int aniFrames = rsdk_load_sprite_animation("Title/Background.bin", -1);
    if (aniFrames < 0) aniFrames = mania_get_titlebg_list_id();
    TitleBG->aniFrames = (uint16)(aniFrames >= 0 ? aniFrames : 0);
    mania_set_titlebg_list_id(aniFrames);
    /* Decomp:91 — palette[0][55] = 0x202030 (cloud-shimmer mask anchor). */
    rsdk_set_palette_entry(0, 55, 0x202030u);
}

/* Decomp TitleBG.c:94-113 — SetupFX (called from TitleSetup_State_FlashIn). */
void TitleBG_SetupFX(void)
{
    rsdk_tilelayer_t *L;
    if ((L = rsdk_get_tilelayer(0)) != NULL) L->draw_group = DRAWGROUP_COUNT;
    if ((L = rsdk_get_tilelayer(1)) != NULL) L->draw_group = 0;
    if ((L = rsdk_get_tilelayer(2)) != NULL) {
        L->draw_group        = 0;
        /* FIXME Phase 2: scanline_callback wiring requires VDP2 H-IRQ
         * raster-table programming; stored but not invoked. */
    }
    if ((L = rsdk_get_tilelayer(3)) != NULL) {
        L->draw_group        = 1;
    }

    EntityTitleBG *titleBG;
    foreach_all(TitleBG, titleBG) {
        titleBG->visible = true;
    }
    EntityTitle3DSprite *title3DSprite;
    foreach_all(Title3DSprite, title3DSprite) {
        title3DSprite->visible = true;
    }

    /* Decomp:110-112 — flip the cloud-mask palette entry; this is the
     * "magenta key" reveal the upstream uses to chroma-key the cloud
     * layer. */
    rsdk_set_palette_entry(0, 55, 0x00FF00u);
    rsdk_set_palette_mask(0x00FF00u);
}

/* === Phase 1.34 Saturn-batched API =====================================
 *
 * The 9 TitleBG entities from Title/Scene1.bin (verified via
 * `python tools/parse_title_entities.py` 2026-05-27):
 *
 *   slot=  0  x=104  y=144  type=0 MOUNTAIN1
 *   slot=  1  x=520  y=144  type=1 MOUNTAIN2
 *   slot=  2  x=104  y=144  type=2 REFLECTION
 *   slot=  3  x=112  y=144  type=3 WATERSPARKLE
 *   slot=  4  x=520  y=144  type=3 WATERSPARKLE
 *   slot= 11  x=144  y= 64  type=4 WINGSHINE
 *   slot= 12  x=208  y= 64  type=4 WINGSHINE
 *   slot= 13  x=308  y= 64  type=4 WINGSHINE
 *   slot= 14  x=372  y= 64  type=4 WINGSHINE
 *
 * Mirrors the Title3DSprite Saturn-batched precedent (Phase 1.32):
 * static seed table -> per-tick Update applies decomp motion math ->
 * Draw_All emits one slDispSprite per entity via the 4-bpp Color Bank 16
 * path (title3d_bg_draw_frame in TitleAssets.c).
 *
 * Saturn deviations (documented):
 *   1. The ECS pass (TitleBG_Update/_Draw above) and rsdk_object_tick
 *      still cycle for the 9 entities created from Scene1.bin, but
 *      title_direct_draw mode (the only mode currently enabled per
 *      s_title_direct_draw_enabled in Game.c:1895) bypasses
 *      rsdk_object_draw_all. The batched API below is the visible draw
 *      path. The position state is owned by s_titlebg[] independent of
 *      the entity-driven path.
 *   2. INK_BLEND/INK_ADD/INK_MASKED (decomp Create:69-83) are deferred to
 *      a follow-on iteration. VDP1 PMOD §5.5.4 supports half-transparency
 *      via CL_Half (PMOD bit 5) which would map to INK_BLEND, but the
 *      first-cut priority is making the actual island silhouette overlay
 *      strips visible at all. Opaque draws for all 5 sub-types this pass.
 *   3. Palette rotation (decomp StaticUpdate:42-45) is already wired in
 *      the ECS callback above (TitleBG_StaticUpdate) and continues to
 *      fire via rsdk_object_tick; no change required here.
 *
 * Per-class Z layout (Saturn perspective sort, smaller Z = closer):
 *   * MOUNTAIN1/MOUNTAIN2 z=210 (drawGroup 1 — back)
 *   * REFLECTION/WATERSPARKLE z=208 (drawGroup 1, slightly in front of
 *     the mountain tops to match decomp slot order 2,3,4 > slot 0,1)
 *   * WINGSHINE z=170 (drawGroup 4 per decomp Create:79; in front of
 *     the title3DSprite formation but behind the title logo at z=200).
 * Per the decomp, drawGroup increases towards the front; on Saturn we
 * convert via inverse z (drawGroup 1 -> larger Z, drawGroup 4 -> smaller Z).
 */

/* Phase 1.39 REMOVED 2026-05-28 (Task #122) — TITLEBG_COUNT reduced
 * from 9 to 5. The 4 WINGSHINE entries (Scene1.bin slots 11-14) have
 * been removed per the user directive 2026-05-28:
 *   "get rid of the purple and blue striped boxes ..."
 *
 * Static-inspect of the WINGSHINE source asset (BG.gif region (1,1)
 * 64x128, anim 4 of Background.bin) confirms the asset IS a diagonal
 * purple+blue stripe sheet (7 unique colors all purple/blue/cyan
 * stripes per tools/qa_golden/wingshine_anim4_frame0.png). The
 * decomp expects the asset to composite via INK_MASKED (a chroma-key
 * reveal of the underlying NBG2 cloud layer, per TitleBG_SetupFX
 * setting palette[55] = 0x00FF00 + SetPaletteMask) which is a
 * scanline-FX pipeline the Saturn port has not implemented (deferred
 * to Phase Z). On Saturn the opaque + CL_Trans variants both render
 * the stripe boxes literally — visually wrong vs the PC reference.
 * The decomp deviation is accepted per the user direction.
 *
 * Restoration path: re-enable the four lines below + restore
 * TITLEBG_COUNT to 9 once the INK_MASKED scanline-FX pipeline (VDP2
 * line-window / per-line palette mask) lands in Phase Z. */
#define TITLEBG_COUNT  5

typedef struct {
    int32_t pos_x_q16;    /* Q16.16 world X (matches RSDK fixed-point) */
    int32_t pos_y_q16;
    uint8_t type;         /* 0..4 */
    uint8_t timer;        /* WINGSHINE 32-frame reset counter         */
    uint8_t _pad[2];
} entity_titlebg_t;

static entity_titlebg_t s_titlebg[TITLEBG_COUNT] = {
    /* slot 0  Mountain1 LEFT  */ { (int32_t)104 << 16, (int32_t)144 << 16, TITLEBG_MOUNTAIN1,    0, {0,0} },
    /* slot 1  Mountain2 RIGHT */ { (int32_t)520 << 16, (int32_t)144 << 16, TITLEBG_MOUNTAIN2,    0, {0,0} },
    /* slot 2  Reflection LEFT */ { (int32_t)104 << 16, (int32_t)144 << 16, TITLEBG_REFLECTION,   0, {0,0} },
    /* slot 3  WaterSpark LEFT */ { (int32_t)112 << 16, (int32_t)144 << 16, TITLEBG_WATERSPARKLE, 0, {0,0} },
    /* slot 4  WaterSpark RIGHT*/ { (int32_t)520 << 16, (int32_t)144 << 16, TITLEBG_WATERSPARKLE, 0, {0,0} },
    /* Phase 1.39 REMOVED 2026-05-28 (Task #122) — WingShine slots 11-14:
     *   { (int32_t)144 << 16, (int32_t) 64 << 16, TITLEBG_WINGSHINE,    0, {0,0} },
     *   { (int32_t)208 << 16, (int32_t) 64 << 16, TITLEBG_WINGSHINE,    0, {0,0} },
     *   { (int32_t)308 << 16, (int32_t) 64 << 16, TITLEBG_WINGSHINE,    0, {0,0} },
     *   { (int32_t)372 << 16, (int32_t) 64 << 16, TITLEBG_WINGSHINE,    0, {0,0} },
     * — produce visible diagonal-stripe rectangles on Saturn without
     * the INK_MASKED scanline-FX pipeline. Restore in Phase Z when
     * VDP2 line-window palette mask is implemented. */
};

/* Decomp TitleBG_Update (TitleBG.c:12-29): per-entity motion. */
void TitleBG_Tick_All(void)
{
    for (int i = 0; i < TITLEBG_COUNT; ++i) {
        entity_titlebg_t *e = &s_titlebg[i];
        if (e->type == TITLEBG_WINGSHINE) {
            /* WINGSHINE drifts down 1px/frame and resets up 32px every 32 frames. */
            e->pos_y_q16 += 0x10000;
            if (++e->timer == 32) {
                e->timer = 0;
                e->pos_y_q16 -= 0x200000;
            }
        } else {
            /* Mountain1/Mountain2/Reflection/WaterSparkle slide LEFT
             * 1px/frame; wrap horizontally when x < -128 (Q16.16
             * -0x800000 = -128) by adding 0x3000000 = +768 px. */
            e->pos_x_q16 -= 0x10000;
            if (e->pos_x_q16 < -0x800000)
                e->pos_x_q16 += 0x3000000;
        }
    }
}

/* Decomp TitleBG_Draw (TitleBG.c:48-54): self-draw at current position.
 *
 * The decomp uses RSDK.DrawSprite(&animator, NULL, false) which passes the
 * entity's current position to the underlying drawing primitive (per
 * _RSDKv5_Graphics_Drawing.cpp DrawSprite). The frame's pivot (px, py)
 * defines the canvas-top-left offset from entity position. On Saturn
 * jo_sprite_draw3D-style API anchors the sprite CENTRE at (jo_x, jo_y),
 * so we convert: canvas_centre = (px + w/2, py + h/2) relative to the
 * entity origin, then world (X, Y) + canvas_centre -> screen coords,
 * then -> jo (origin-centred) coords by subtracting (160, 112).
 *
 * Per parse_title_entities.py the world coords are already RSDK
 * "screen-space" for the title scene (decomp does not apply a camera
 * offset to TitleBG entities — they're absolute screen positions
 * 0..640 horizontally with the orbit centre at x=256 = ScreenInfo->center.x).
 * Saturn screen is 320x240; the world span 0..640 covers the
 * scroll-wrap range. */
void TitleBG_Draw_All(void)
{
    if (!g_title3d_loaded) return;
    if (g_title3d_bg_first_sid < 0) return;

    for (int i = 0; i < TITLEBG_COUNT; ++i) {
        entity_titlebg_t *e = &s_titlebg[i];

        int world_x = (int)(e->pos_x_q16 >> 16);
        int world_y = (int)(e->pos_y_q16 >> 16);

        int anim_id = (int)e->type;
        if (anim_id < 0) anim_id = 0;
        if (anim_id >= TITLE3D_BG_MAX_FRAMES) anim_id = TITLE3D_BG_MAX_FRAMES - 1;

        int fw = g_title3d_bg_frames[anim_id].width;
        int fh = g_title3d_bg_frames[anim_id].height;
        int px = g_title3d_bg_frames[anim_id].pivot_x;
        int py = g_title3d_bg_frames[anim_id].pivot_y;
        int canvas_cx = px + (fw >> 1);
        int canvas_cy = py + (fh >> 1);

        /* World-to-Saturn-screen: the decomp's RSDK ScreenInfo->center.x
         * = 160 (320/2) on Saturn; the world x ranges 0..640 with the
         * island centre at world x=256 + camera-pos.x (0 in title state).
         * To map world (256, 0) -> Saturn screen centre (160, 0) we
         * subtract the RSDK draw origin offset = ScreenInfo->center.x +
         * scroll = 256 (no scroll in title). For Y the decomp adds
         * ScreenInfo->center.y when not in YParallax mode — for
         * TitleBG that's the same 112 we use everywhere else.
         *
         * Per Phase 1.32 Title3DSprite_Draw_All (line 286-287):
         *   jo_x = (screen_x + canvas_cx) - 160;
         *   jo_y = (screen_y + canvas_cy) - 112;
         * with screen_x = world_dx + center_x (center_x=160). Same
         * convention here: jo_x = (world_x - 256) + canvas_cx;
         * jo_y = (world_y - 112) + canvas_cy. */
        int jo_x = (world_x - 256) + canvas_cx;
        int jo_y = (world_y - 112) + canvas_cy;

        /* Cull entities that are off-screen by more than half-width
         * margin (saves VDP1 cycles + sortlist slots). The wrap-around
         * keeps x in roughly [-128, 768] world = jo_x in [-384, 512]
         * accounting for the largest canvas offset. Saturn screen jo_x
         * range = [-160, 160] for the visible area; we drop anything
         * with abs(jo_x) > 320. */
        if (jo_x < -320 || jo_x > 320) continue;
        if (jo_y < -240 || jo_y > 240) continue;

        /* Per-type Z layout — see header comment above. */
        int z;
        if (e->type == TITLEBG_WINGSHINE) {
            z = 170;        /* drawGroup 4 — in front of 3DSprite formation */
        } else if (e->type == TITLEBG_MOUNTAIN1 ||
                   e->type == TITLEBG_MOUNTAIN2) {
            z = 210;        /* back row */
        } else {
            z = 208;        /* reflection/water-sparkle slightly in front */
        }

        /* Phase 1.34c — per-type half-transparency selection per decomp
         * TitleBG_Create (TitleBG.c:67-83). MOUNTAIN1 stays opaque
         * (decomp default INK_NONE). MOUNTAIN2 (INK_BLEND), REFLECTION
         * + WATERSPARKLE (INK_ADD alpha=0x80), WINGSHINE (INK_MASKED)
         * all map to VDP1 CL_Trans (PMOD bits 2:0 = 3 per
         * ST-013-R3 §5.5.4 + SGL SL_DEF.H:194) on Saturn. */
        int half_alpha;
        switch (e->type) {
            case TITLEBG_MOUNTAIN2:
            case TITLEBG_REFLECTION:
            case TITLEBG_WATERSPARKLE:
            case TITLEBG_WINGSHINE:
                half_alpha = 1;
                break;
            case TITLEBG_MOUNTAIN1:
            default:
                half_alpha = 0;     /* opaque (decomp INK_NONE default) */
                break;
        }

        /* direction = FLIP_NONE for every type per decomp Create:67
         * (drawFX = FX_FLIP but direction never set anywhere in
         * TitleBG_Create / TitleBG_Update). */
        title3d_bg_draw_frame(anim_id, jo_x, jo_y, z, 0, half_alpha);
    }
}
