/* Phase 1.2 — TitleLogo class body.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Title_TitleLogo.c`
 * (Christian Whitehead/Simon Thomley/Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich). Plus-only paths are removed (Saturn build
 * is non-Plus).
 *
 * Per-class behaviors:
 *   * EMBLEM     — drawn FLIP_NONE + FLIP_X (mirrored wings) after
 *                  SetClipBounds full viewport.
 *   * RIBBON     — drawn FLIP_X + FLIP_NONE; ribbonCenterAnimator drawn
 *                  once if showRibbonCenter is set.
 *   * PRESSSTART — blink on `!(timer & 0x10)` (16-frames on / 16-off square
 *                  wave, per decomp:62-64).
 *   * GAMETITLE / COPYRIGHT / RINGBOTTOM — default branch, single draw.
 *   * POWERLED   — destroyed in TitleSetup_State_AnimateUntilFlash before
 *                  ever reaching here; assert-friendly no-op fallthrough.
 */

#include "TitleLogo.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/drawing.h"
#include "../../../rsdk/scene.h"
#include "../../../rsdk/object.h"

/* Phase 1.23 GAP A — `mania_set_active_list_id` removed: list_id now
 * arrives via the animator (set in rsdk_set_sprite_animation) and
 * flows through the draw callback signature directly. */
extern void mania_set_titlelogo_list_id(int v);
extern int  mania_get_titlelogo_list_id(void);

/* Decomp TitleLogo.c:12-29 — Update (non-Plus branch). */
void TitleLogo_Update(void)
{
    RSDK_THIS(TitleLogo);
    switch (self->type) {
        case TITLELOGO_RIBBON:
            rsdk_process_animation(&self->mainAnimator);
            if (self->showRibbonCenter)
                rsdk_process_animation(&self->ribbonCenterAnimator);
            break;
        case TITLELOGO_PRESSSTART:
            ++self->timer;
            break;
        default: break;
    }
}

void TitleLogo_LateUpdate(void)   {}
void TitleLogo_StaticUpdate(void) {}

/* Decomp TitleLogo.c:35-75 — Draw. */
void TitleLogo_Draw(void)
{
    RSDK_THIS(TitleLogo);
    /* Phase 1.23 GAP A — list_id no longer needs to be set globally; the
     * animator carries it through rsdk_draw_sprite_ex to title_sprite_cb. */

    switch (self->type) {
        case TITLELOGO_EMBLEM:
            rsdk_set_clip_bounds(0, 0, ScreenInfo->size.x, ScreenInfo->size.y);
            self->direction = FLIP_NONE;
            rsdk_draw_sprite_ex(&self->mainAnimator,
                                self->position.x, self->position.y,
                                self->direction, self->inkEffect,
                                self->alpha, self->drawGroup, false);
            self->direction = FLIP_X;
            rsdk_draw_sprite_ex(&self->mainAnimator,
                                self->position.x, self->position.y,
                                self->direction, self->inkEffect,
                                self->alpha, self->drawGroup, false);
            break;

        case TITLELOGO_RIBBON:
            self->direction = FLIP_X;
            rsdk_draw_sprite_ex(&self->mainAnimator,
                                self->position.x, self->position.y,
                                self->direction, self->inkEffect,
                                self->alpha, self->drawGroup, false);
            self->direction = FLIP_NONE;
            rsdk_draw_sprite_ex(&self->mainAnimator,
                                self->position.x, self->position.y,
                                self->direction, self->inkEffect,
                                self->alpha, self->drawGroup, false);
            if (self->showRibbonCenter) {
                rsdk_draw_sprite_ex(&self->ribbonCenterAnimator,
                                    self->position.x, self->position.y,
                                    FLIP_NONE, self->inkEffect,
                                    self->alpha, self->drawGroup, false);
            }
            break;

        case TITLELOGO_PRESSSTART:
            /* Decomp:62-64 — 16 on / 16 off square wave. */
            if (!(self->timer & 0x10)) {
                rsdk_draw_sprite_ex(&self->mainAnimator,
                                    self->position.x, self->position.y,
                                    self->direction, self->inkEffect,
                                    self->alpha, self->drawGroup, false);
            }
            break;

        case TITLELOGO_POWERLED:
            /* Should never reach — destroyed in
             * TitleSetup_State_AnimateUntilFlash. */
            break;

        default:
            rsdk_draw_sprite_ex(&self->mainAnimator,
                                self->position.x, self->position.y,
                                self->direction, self->inkEffect,
                                self->alpha, self->drawGroup, false);
            break;
    }
}

/* Decomp TitleLogo.c:77-141 — Create (non-Plus branch). */
void TitleLogo_Create(void *data)
{
    (void)data;
    RSDK_THIS(TitleLogo);
    self->drawFX = FX_FLIP;
    self->alpha  = 0xFF;

    if (!SceneInfo->inEditor) {
        switch (self->type) {
            case TITLELOGO_EMBLEM:
                rsdk_set_sprite_animation(TitleLogo->aniFrames, 0,
                                          &self->mainAnimator, true, 0);
                break;
            case TITLELOGO_RIBBON:
                rsdk_set_sprite_animation(TitleLogo->aniFrames, 1,
                                          &self->mainAnimator, true, 0);
                rsdk_set_sprite_animation(TitleLogo->aniFrames, 3,
                                          &self->ribbonCenterAnimator, true, 0);
                break;
            case TITLELOGO_PRESSSTART:
                TitleLogo_SetupPressStart();
                break;
            default:
                /* Per decomp:116 — `type + 2` slot (so GAMETITLE=2 → anim
                 * id 4; COPYRIGHT=4 → 6; RINGBOTTOM=5 → 7). The Saturn-
                 * side asset bridge looks up the appropriate Saturn sprite
                 * via resolve_asset on (list_id, type+2). */
                rsdk_set_sprite_animation(TitleLogo->aniFrames,
                                          (int)self->type + 2,
                                          &self->mainAnimator, true, 0);
                break;
        }

        /* Decomp:119-139 — all known types are initially hidden +
         * ACTIVE_NEVER; TitleSetup_State_AnimateUntilFlash / _FlashIn /
         * _SetupLogo activate them. */
        switch (self->type) {
            case TITLELOGO_EMBLEM:
            case TITLELOGO_RIBBON:
            case TITLELOGO_GAMETITLE:
            case TITLELOGO_COPYRIGHT:
            case TITLELOGO_RINGBOTTOM:
            case TITLELOGO_PRESSSTART:
                self->visible   = false;
                self->active    = ACTIVE_NEVER;
                self->drawGroup = 4;
                break;
            default:
                self->active    = ACTIVE_NORMAL;
                self->visible   = true;
                self->drawGroup = 4;
                break;
        }
    }
}

/* Decomp TitleLogo.c:143-153 — StageLoad.
 *
 * Saturn-side adaptation: rsdk_load_sprite_animation will return -1 because
 * the .bin asset isn't shipped (the Saturn build uses .SPR atlases loaded
 * via the asset bridge). We fall back to the pre-seeded list_id from
 * mania_engine_init::setup_title_assets which manufactures a synthetic
 * sprite-animation list pointing at the .SPR-derived frame stubs. */
void TitleLogo_StageLoad(void)
{
    int aniFrames = rsdk_load_sprite_animation("Title/Logo.bin", -1);
    if (aniFrames < 0) aniFrames = mania_get_titlelogo_list_id();
    TitleLogo->aniFrames = (uint16)(aniFrames >= 0 ? aniFrames : 0);
    mania_set_titlelogo_list_id(aniFrames);
}

/* Decomp TitleLogo.c:155-194 — SetupPressStart (Saturn: English default). */
void TitleLogo_SetupPressStart(void)
{
    RSDK_THIS(TitleLogo);
    rsdk_set_sprite_animation(TitleLogo->aniFrames, 8,
                              &self->mainAnimator, true, 0);
}
