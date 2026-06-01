/* Phase 1.2 — TitleSonic class body.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.c`
 * (Christian Whitehead/Simon Thomley/Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Saturn-side adaptation: `LoadSpriteAnimation("Title/Sonic.bin", ...)` is
 * a no-op in Phase 1.2 because the .bin asset isn't shipped (the Saturn
 * build uses TSONIC.ATL which is loaded outside this path by the legacy
 * title_sonic.c — to be re-wired through the asset bridge in Phase 1.3).
 * Without frames the Process/Draw calls early-return cleanly. */

#include "TitleSonic.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/drawing.h"
#include "../../../rsdk/scene.h"
#include "../../../rsdk/object.h"

/* Phase 1.23 GAP A — `mania_set_active_list_id` removed: animator-borne
 * list_id replaces the prior global hack. */
extern void mania_set_titlesonic_list_id(int v);
extern int  mania_get_titlesonic_list_id(void);

/* Decomp TitleSonic.c:12-20 — Update.
 * Advance the body animator; once it lands on its final frame, also tick
 * the finger-wave animator (which loops independently). */
void TitleSonic_Update(void)
{
    RSDK_THIS(TitleSonic);
    rsdk_process_animation(&self->animatorSonic);
    if (self->animatorSonic.frame_count > 0 &&
        self->animatorSonic.frame_id == self->animatorSonic.frame_count - 1) {
        rsdk_process_animation(&self->animatorFinger);
    }
}

void TitleSonic_LateUpdate(void)   {}
void TitleSonic_StaticUpdate(void) {}

/* Decomp TitleSonic.c:26-37 — Draw.
 *   - SetClipBounds top-half (Y < 160), draw body
 *   - SetClipBounds full screen, draw finger overlay IF body is on its
 *     last frame (the iconic finger-wave pose). */
void TitleSonic_Draw(void)
{
    RSDK_THIS(TitleSonic);
    /* Phase 1.23 GAP A — animator-borne list_id; no global setter needed. */

    rsdk_set_clip_bounds(0, 0, ScreenInfo->size.x, 160);
    rsdk_draw_sprite_ex(&self->animatorSonic,
                        self->position.x, self->position.y,
                        self->direction, self->inkEffect,
                        self->alpha, self->drawGroup, false);

    rsdk_set_clip_bounds(0, 0, ScreenInfo->size.x, ScreenInfo->size.y);

    if (self->animatorSonic.frame_count > 0 &&
        self->animatorSonic.frame_id == self->animatorSonic.frame_count - 1) {
        rsdk_draw_sprite_ex(&self->animatorFinger,
                            self->position.x, self->position.y,
                            self->direction, self->inkEffect,
                            self->alpha, self->drawGroup, false);
    }
}

/* Decomp TitleSonic.c:39-51 — Create. */
void TitleSonic_Create(void *data)
{
    (void)data;
    RSDK_THIS(TitleSonic);
    rsdk_set_sprite_animation(TitleSonic->aniFrames, 0, &self->animatorSonic, true, 0);
    rsdk_set_sprite_animation(TitleSonic->aniFrames, 1, &self->animatorFinger, true, 0);
    if (!SceneInfo->inEditor) {
        self->visible   = false;
        self->active    = ACTIVE_NEVER;
        self->drawGroup = 4;
        self->alpha     = 0xFF;
    }
}

/* Decomp TitleSonic.c:53 — StageLoad.
 *
 * Saturn-side adaptation: rsdk_load_sprite_animation returns -1 (no .bin
 * shipped); fall back to the pre-seeded list_id from the asset bridge
 * which manufactures a 49-frame anim 0 + 1-frame anim 1 (finger stub),
 * with per-frame metadata mirroring TSONIC.ATL. */
void TitleSonic_StageLoad(void)
{
    int aniFrames = rsdk_load_sprite_animation("Title/Sonic.bin", -1);
    if (aniFrames < 0) aniFrames = mania_get_titlesonic_list_id();
    TitleSonic->aniFrames = (uint16)(aniFrames >= 0 ? aniFrames : 0);
    mania_set_titlesonic_list_id(aniFrames);
}
