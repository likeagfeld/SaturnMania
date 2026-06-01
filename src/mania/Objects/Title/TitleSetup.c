/* Phase 1.2 — TitleSetup class body.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Title_TitleSetup.c`
 * (Christian Whitehead/Simon Thomley/Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich). Each function below cites the decomp line
 * range it mirrors. Plus-only cheat-code paths are removed (Saturn build
 * is non-Plus).
 *
 * Saturn-side adaptations:
 *   * `RSDK.LoadSpriteAnimation("Title/Electricity.bin", SCOPE_STAGE)` may
 *     return -1 because the .bin isn't shipped in cd/. The state machine
 *     still progresses because `self->animator.frame_count` will be 0,
 *     and the State_AnimateUntilFlash + State_FlashIn callbacks check
 *     `frameID == 31` / `frameCount - 1` against the (possibly zero)
 *     count — when count==0 they fast-forward past the electricity arc.
 *   * `API_*` and `Localization_*` calls map to no-op stubs in rsdk/api.c.
 *   * `Unknown_anyKeyPress` and `TouchInfo->count` are absent — we read
 *     from the input controller's any-press flag instead. */

#include "TitleSetup.h"
#include "TitleLogo.h"
#include "TitleSonic.h"
#include "TitleBG.h"
#include "Title3DSprite.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/drawing.h"
#include "../../../rsdk/audio.h"
#include "../../../rsdk/input.h"
#include "../../../rsdk/scene.h"
#include "../../../rsdk/api.h"
#include "../../../rsdk/string.h"
#include "../../../rsdk/storage.h"
#include "../../../rsdk/palette.h"
#include <jo/jo.h>     /* Phase 1.25: jo_set_default_background_color for
                        * State_FlashIn sentinel measurement (docs/
                        * COMPREHENSIVE_PLAN.md §11.31). */

/* Saturn-side helpers from Game.c.
 *
 * Phase 1.23 GAP A — `mania_set_active_list_id` removed: animator-borne
 * list_id replaces the prior global hack. */
extern void mania_set_titlesetup_list_id(int v);
extern int  mania_get_titlesetup_list_id(void);

/* Decomp TitleSetup.c:12-19 — Update. */
void TitleSetup_Update(void)
{
    RSDK_THIS(TitleSetup);
    StateMachine_Run(self->state);
    ScreenInfo->position.x = 0x100 - ScreenInfo->center.x;
}

/* Decomp TitleSetup.c:21. */
void TitleSetup_LateUpdate(void) {}

/* Decomp TitleSetup.c:23. */
void TitleSetup_StaticUpdate(void) {}

/* Decomp TitleSetup.c:25-30 — Draw. */
void TitleSetup_Draw(void)
{
    RSDK_THIS(TitleSetup);
    StateMachine_Run(self->stateDraw);
}

/* Decomp TitleSetup.c:32-49 — Create. */
void TitleSetup_Create(void *data)
{
    (void)data;
    RSDK_THIS(TitleSetup);
    if (!SceneInfo->inEditor) {
        rsdk_set_sprite_animation(TitleSetup->aniFrames, 0, &self->animator, true, 0);
        self->active    = ACTIVE_ALWAYS;
        self->visible   = true;
        self->drawGroup = 12;
        self->drawFX    = FX_FLIP;
        self->state     = TitleSetup_State_Wait;
        self->stateDraw = TitleSetup_Draw_FadeBlack;
        self->timer     = 1024;
        self->drawPos.x = 256 << 16;
        self->drawPos.y = 108 << 16;
    }
}

/* Decomp TitleSetup.c:51-90 — StageLoad. */
void TitleSetup_StageLoad(void)
{
    rsdk_string_t presence;
    rsdk_init_string(&presence, NULL, 0);
    rsdk_localization_get_string(&presence, 0 /* STR_RPC_TITLE */);
    rsdk_api_set_rich_presence(0 /* PRESENCE_TITLE */, &presence);

    rsdk_api_set_no_save(false);

    /* globals->blueSpheresInit / saveLoaded / optionsLoaded / saveRAM etc.
     * are not yet ported to Saturn (Phase 2+ globals.c). Skipped. */

    rsdk_api_clear_preroll_errors();

    /* `LoadSpriteAnimation("Title/Electricity.bin", SCOPE_STAGE)` —
     * Saturn-side: returns -1 if the .bin isn't in cd/. Fall back to the
     * pre-seeded synthetic list_id so the animator references valid frame
     * storage even though the visible electric arc isn't drawn (no Saturn-
     * side ELECTRA atlas yet). The state machine completes the arc by
     * checking frame_count==0 / frame_id==31 fallthroughs. */
    int aniFrames = rsdk_load_sprite_animation("Title/Electricity.bin", -1);
    if (aniFrames < 0) aniFrames = mania_get_titlesetup_list_id();
    TitleSetup->aniFrames = (uint16)(aniFrames >= 0 ? aniFrames : 0);
    mania_set_titlesetup_list_id(aniFrames);

    /* SFX cache — Saturn-side maps these to slot ids. The PlaySfx call
     * sites will tolerate -1 (no-op). */
    TitleSetup->sfxMenuBleep  = 0;     /* FIXME Phase 2: GetSfx wrapper */
    TitleSetup->sfxMenuAccept = 1;
    TitleSetup->sfxRing       = 2;

    /* ResetEntitySlot(0, TitleSetup classID, NULL) — create a slot-0
     * TitleSetup entity. Mirrors decomp:89. */
    int cid = rsdk_object_find_class("TitleSetup");
    if (cid >= 0)
        rsdk_reset_entity_slot(0, (uint16_t)cid, NULL);
}

/* Decomp TitleSetup.c:127-135 — VideoSkipCB. */
bool32 TitleSetup_VideoSkipCB(void)
{
    const rsdk_controller_state_t *c = rsdk_controller_state(CONT_ANY);
    if (!c) return false;
    if (c->key_a.press || c->key_b.press || c->key_start.press) {
        Music_Stop();
        return true;
    }
    return false;
}

/* Decomp TitleSetup.c:137-150 — State_Wait. */
void TitleSetup_State_Wait(void)
{
    RSDK_THIS(TitleSetup);
    if (self->timer <= -0x400) {
        self->timer     = 0;
        self->state     = TitleSetup_State_AnimateUntilFlash;
        self->stateDraw = TitleSetup_Draw_DrawRing;
        Music_PlayTrack(TRACK_STAGE);
    } else {
        self->timer -= 16;
    }
}

/* Decomp TitleSetup.c:152-174 — State_AnimateUntilFlash.
 *
 * Saturn-side note: the decomp's Electricity.bin animation has 32 frames
 * (the state machine ends the arc at `frame_id == 31` = the last frame).
 * On Saturn we ship a synthetic anim list (TitleAssets.c::
 * build_titlesetup_anims) which currently has only 1 stub frame because
 * the Saturn-side Electricity asset isn't built yet. Any anim with
 * fewer than 32 frames cannot reach frame_id == 31, so the original
 * fallback (`frame_count == 0` only) is too narrow and the state machine
 * stalls forever, blocking TitleLogo visibility (Phase 1.4 diagnosis).
 *
 * Fix: broaden the fallback to ANY frame_count < 32. This is the correct
 * "Saturn-side asset not present" sentinel — when the real 32-frame
 * Electricity asset lands (Phase 2+), `frame_count` will be 32 and the
 * decomp-mirror `frame_id == 31` path activates naturally. */
void TitleSetup_State_AnimateUntilFlash(void)
{
    RSDK_THIS(TitleSetup);
    rsdk_process_animation(&self->animator);

    bool progress = (self->animator.frame_id == 31);
    if (!progress && self->animator.frame_count < 32) {
        /* Saturn-side fallback: synthetic/missing electricity asset
         * (frame_count < 32) -> auto-advance through the arc. */
        progress = true;
    }

    if (progress) {
        EntityTitleLogo *titleLogo;
        foreach_all(TitleLogo, titleLogo)
        {
            if (titleLogo->type >= TITLELOGO_EMBLEM) {
                if (titleLogo->type <= TITLELOGO_RIBBON) {
                    titleLogo->active  = ACTIVE_NORMAL;
                    titleLogo->visible = true;
                }
                else if (titleLogo->type == TITLELOGO_POWERLED) {
                    destroyEntity(titleLogo);
                }
            }
        }
        self->state = TitleSetup_State_FlashIn;
    }
}

/* Decomp TitleSetup.c:176-213 — State_FlashIn. */
void TitleSetup_State_FlashIn(void)
{
    RSDK_THIS(TitleSetup);

    /* Phase 1.25 measurement S0 — one-shot back-color sentinel encoding
     * "State_FlashIn entered." Magenta (255,0,255), distinct from
     * jo_core_init's sky-blue baseline (96,128,224) and the S1 green
     * (0,255,0) emitted post-activation below. Static flag ensures
     * the magenta band only paints once so the rest of the frame
     * sequence keeps the natural backdrop. Per docs/COMPREHENSIVE_PLAN
     * .md §11.31 measurement strategy + memory/qa-iterative-improvement
     * .md v2 (gate fires RED on broken build, GREEN once both sentinels
     * land). */
    static int s_phase1_25_marker_s0_fired = 0;
    if (!s_phase1_25_marker_s0_fired) {
        jo_set_default_background_color(JO_COLOR_RGB(255, 0, 255));
        s_phase1_25_marker_s0_fired = 1;
    }

    rsdk_process_animation(&self->animator);

    bool at_end = (self->animator.frame_count > 0 &&
                   self->animator.frame_id == self->animator.frame_count - 1);
    if (!at_end && self->animator.frame_count == 0) {
        /* Saturn fallback: no electricity asset, fire the flash transition
         * on the very next tick. */
        at_end = true;
    }

    if (at_end) {
        EntityTitleLogo *titleLogo;
        foreach_all(TitleLogo, titleLogo)
        {
            if (titleLogo->type != TITLELOGO_PRESSSTART) {
                titleLogo->active  = ACTIVE_NORMAL;
                titleLogo->visible = true;
            }
            if (titleLogo->type == TITLELOGO_RIBBON) {
                titleLogo->showRibbonCenter = true;
                /* anim 2 = ribbon-wave per decomp:198. The Saturn-side
                 * mapping treats this as a re-Activate of the ribbon. */
                rsdk_set_sprite_animation(TitleLogo->aniFrames, 2,
                                          &titleLogo->mainAnimator, true, 0);
            }
        }

        EntityTitleSonic *titleSonic;
        foreach_all(TitleSonic, titleSonic)
        {
            titleSonic->active  = ACTIVE_NORMAL;
            titleSonic->visible = true;
        }

        /* Phase 1.25 measurement S1 — one-shot back-color sentinel
         * encoding "activation foreach completed." Bright green
         * (0,255,0). Emitted AFTER both TitleLogo + TitleSonic foreach
         * walks finish but BEFORE the state-pointer flip, so its
         * appearance proves the activation loop ran to completion.
         * Per §11.31 decision tree: S0+S1 both observed = branch A
         * (bug is downstream); S0 only = branch B (`at_end` predicate
         * never fires past the first tick); neither = branch C
         * (State_FlashIn never entered). */
        static int s_phase1_25_marker_s1_fired = 0;
        if (!s_phase1_25_marker_s1_fired) {
            jo_set_default_background_color(JO_COLOR_RGB(0, 255, 0));
            s_phase1_25_marker_s1_fired = 1;
        }

        TitleBG_SetupFX();
        self->timer     = 0x300;
        self->state     = TitleSetup_State_WaitForSonic;
        self->stateDraw = TitleSetup_Draw_Flash;
    }
}

/* Decomp TitleSetup.c:215-236 — State_WaitForSonic. */
void TitleSetup_State_WaitForSonic(void)
{
    RSDK_THIS(TitleSetup);
    if (self->timer <= 0) {
        self->stateDraw = StateMachine_None;
        self->state     = TitleSetup_State_SetupLogo;
    } else {
        self->timer -= 16;
    }
}

/* Decomp TitleSetup.c:238-266 — State_SetupLogo. */
void TitleSetup_State_SetupLogo(void)
{
    RSDK_THIS(TitleSetup);
    if (++self->timer == 120) {
        EntityTitleLogo *titleLogo;
        foreach_all(TitleLogo, titleLogo)
        {
            if (titleLogo->type == TITLELOGO_PRESSSTART) {
                titleLogo->active  = ACTIVE_NORMAL;
                titleLogo->visible = true;
            }
        }
        self->timer = 0;
        self->state = TitleSetup_State_WaitForEnter;
    }
}

/* Decomp TitleSetup.c:310-350 — State_WaitForEnter. */
void TitleSetup_State_WaitForEnter(void)
{
    RSDK_THIS(TitleSetup);

    const rsdk_controller_state_t *c = rsdk_controller_state(CONT_ANY);
    bool32 anyButton = false;
    if (c) {
        anyButton = c->key_a.press || c->key_b.press || c->key_c.press ||
                    c->key_x.press || c->key_y.press || c->key_z.press ||
                    c->key_start.press || c->key_select.press;
    }

    if (anyButton) {
        rsdk_play_sfx((int)TitleSetup->sfxMenuAccept, 0xFFFFFFFFu, 0xFF);
        self->timer = 0;
        rsdk_set_scene("Presentation", "Menu");
        Music_Stop();
        self->state     = TitleSetup_State_FadeToMenu;
        self->stateDraw = TitleSetup_Draw_FadeBlack;
    }
    else if (++self->timer == 800) {
        self->timer     = 0;
        self->state     = TitleSetup_State_FadeToVideo;
        self->stateDraw = TitleSetup_Draw_FadeBlack;
    }
}

/* Decomp TitleSetup.c:352-360 — State_FadeToMenu. */
void TitleSetup_State_FadeToMenu(void)
{
    RSDK_THIS(TitleSetup);
    if (self->timer >= 1024) {
        rsdk_load_scene();
    } else {
        self->timer += 8;
    }
}

/* Decomp TitleSetup.c:362-384 — State_FadeToVideo.
 *
 * Saturn-side: Cinepak intro playback is deferred (per the cinepak audio
 * mutex issue). For Phase 1.2, the fade completes but the Mania.ogv
 * playback is a no-op (state stays in FadeToVideo). */
void TitleSetup_State_FadeToVideo(void)
{
    RSDK_THIS(TitleSetup);
    if (self->timer >= 1024) {
        /* FIXME Phase Z: PlayStream IntroTee.ogg or IntroHP.ogg + LoadVideo
         * Mania.ogv with TitleSetup_VideoSkipCB callback. */
        Music_Stop();
        /* Re-arm the title for an attract loop: restart state machine. */
        self->timer = 0;
        self->state = TitleSetup_State_Wait;
        self->stateDraw = TitleSetup_Draw_FadeBlack;
    } else {
        self->timer += 8;
    }
}

/* Decomp TitleSetup.c:386-391 — Draw_FadeBlack. */
void TitleSetup_Draw_FadeBlack(void)
{
    RSDK_THIS(TitleSetup);
    rsdk_fill_screen(0x000000, self->timer, self->timer - 128, self->timer - 256);
}

/* Decomp TitleSetup.c:393-402 — Draw_DrawRing.
 *
 * Saturn-side: when the Electricity.bin asset is absent (Phase 1.2), the
 * animator has no frames so rsdk_draw_sprite_ex returns immediately. The
 * mirroring (FLIP_NONE + FLIP_X) is preserved structurally. */
void TitleSetup_Draw_DrawRing(void)
{
    RSDK_THIS(TitleSetup);
    /* Phase 1.23 GAP A — animator-borne list_id; no global setter needed. */

    self->direction = FLIP_NONE;
    rsdk_draw_sprite_ex(&self->animator, self->drawPos.x, self->drawPos.y,
                        self->direction, self->inkEffect, self->alpha,
                        self->drawGroup, false);

    self->direction = FLIP_X;
    rsdk_draw_sprite_ex(&self->animator, self->drawPos.x, self->drawPos.y,
                        self->direction, self->inkEffect, self->alpha,
                        self->drawGroup, false);
}

/* Decomp TitleSetup.c:404-409 — Draw_Flash. */
void TitleSetup_Draw_Flash(void)
{
    RSDK_THIS(TitleSetup);
    rsdk_fill_screen(0xF0F0F0, self->timer, self->timer - 128, self->timer - 256);
}
