/* Phase 3.2.c.1 (Task #148) — UISubHeading class body.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UISubHeading.c`
 * (451 LOC; Christian Whitehead/Simon Thomley/Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich). The Saturn ship targets the
 * non-Plus / NTSC-US scope, so the per-line decomp citations cover
 * .c L12-76 only (lifecycle + Draw). The Plus-only SaveSelect helper
 * block (decomp .c L82-413) is OUT OF SCOPE for the Saturn ship — see
 * project-policy lock "Mania-only Saturn" in docs/MANIA_MODE_PARITY_PLAN.md.
 *
 * Per-line decomp citations (non-Plus surface):
 *   .c L12-24    UISubHeading_Update        -> UISubHeading_Update
 *   .c L26       UISubHeading_LateUpdate    -> UISubHeading_LateUpdate
 *   .c L28       UISubHeading_StaticUpdate  -> UISubHeading_StaticUpdate
 *   .c L30-55    UISubHeading_Draw          -> UISubHeading_Draw
 *   .c L57-74    UISubHeading_Create        -> UISubHeading_Create
 *   .c L76       UISubHeading_StageLoad     -> UISubHeading_StageLoad
 *
 * Saturn-side adaptations:
 *   * `UIWidgets_DrawParallelogram` is the decomp call at L38 — Phase
 *     3.2.c.1 fills that helper in UIWidgets.c via the new VDP1 polygon
 *     emitter (rsdk_draw_face 4-vertex path).
 *   * `RSDK.SetSpriteAnimation` -> rsdk_set_sprite_animation (Phase A3).
 *   * `RSDK.DrawSprite` -> rsdk_draw_sprite (Phase A4) — the label glyph
 *     is rendered from UIWidgets->textFrames at the per-align position.
 *   * `align` enum (UIBUTTON_ALIGN_LEFT/CENTER/RIGHT) shares decomp's
 *     UIButton align values (Phase 3.2.c.1 UIButton.h port).
 *
 * Phase 3.2.c.1 scope: registered + drawing-active so Save Select +
 * Secrets menus' "section heading" callouts paint at run-time. */

#include "UISubHeading.h"
#include "UIWidgets.h"
#include "UIButton.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/drawing.h"

ObjectUISubHeading *UISubHeading = NULL;

/* Decomp .c L12-24 — Update. Re-syncs the animator if UIWidgets's
 * textFrames swapped (language change) or if the per-entity
 * listID/frameID changed since the last syncing pass. Then ticks the
 * state machine. */
void UISubHeading_Update(void)
{
    RSDK_THIS(UISubHeading);

    if (!UIWidgets) return;

    if (self->textFrames != UIWidgets->textFrames ||
        self->storedListID != self->listID ||
        self->storedFrameID != self->frameID) {
        rsdk_set_sprite_animation((int)UIWidgets->textFrames, self->listID,
                                  &self->animator, true, self->frameID);
        self->textFrames    = UIWidgets->textFrames;
        self->storedListID  = self->listID;
        self->storedFrameID = self->frameID;
    }

    StateMachine_Run(self->state);
}

/* Decomp .c L26. */
void UISubHeading_LateUpdate(void) {}

/* Decomp .c L28. */
void UISubHeading_StaticUpdate(void) {}

/* Decomp .c L30-55 — Draw. Paints a parallelogram background then the
 * per-align label sprite from the textFrames animator. */
void UISubHeading_Draw(void)
{
    RSDK_THIS(UISubHeading);

    Vector2 drawPos;
    int32 size = (self->size.x + self->size.y) >> 16;

    /* decomp L38 — black parallelogram backdrop. */
    UIWidgets_DrawParallelogram(self->position.x, self->position.y,
                                size, self->size.y >> 16,
                                self->bgEdgeSize, 0x00, 0x00, 0x00);

    drawPos = self->position;
    switch (self->align) {
        case UIBUTTON_ALIGN_LEFT:
            drawPos.x += -0x60000 - (self->size.x >> 1);
            break;
        default:
        case UIBUTTON_ALIGN_CENTER:
            break;
        case UIBUTTON_ALIGN_RIGHT:
            drawPos.x -= 0x60000;
            drawPos.x += self->size.x >> 1;
            break;
    }

    drawPos.x += self->offset;
    rsdk_draw_sprite(&self->animator, drawPos.x, drawPos.y,
                     FLIP_NONE, false);
}

/* Decomp .c L57-74 — Create. */
void UISubHeading_Create(void *data)
{
    (void)data;
    RSDK_THIS(UISubHeading);

    if (!SceneInfo->inEditor) {
        self->offset <<= 16;
        self->visible       = 1;
        self->drawGroup     = 2;
        self->active        = ACTIVE_BOUNDS;
        self->updateRange.x = 0x800000;
        self->updateRange.y = 0x400000;
        self->bgEdgeSize    = self->size.y >> 16;
        if (self->size.y < 0) self->size.y = -self->size.y;

        if (UIWidgets) {
            rsdk_set_sprite_animation((int)UIWidgets->textFrames,
                                      self->listID, &self->animator,
                                      true, self->frameID);
            self->textFrames = UIWidgets->textFrames;
        }
    }
}

/* Decomp .c L76 — StageLoad (empty body in decomp). */
void UISubHeading_StageLoad(void) {}
