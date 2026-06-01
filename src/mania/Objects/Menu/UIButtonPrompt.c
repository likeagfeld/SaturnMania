/* Phase 3.2.c.1 (Task #148) — UIButtonPrompt class body.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UIButtonPrompt.c`
 * (567 LOC; Christian Whitehead/Simon Thomley/Hunter Bridges; decomp
 * by Rubberduckycooly & RMGRich).
 *
 * Per-line decomp citations:
 *   .c L12-62    UIButtonPrompt_Update          -> UIButtonPrompt_Update
 *   .c L64-97    UIButtonPrompt_LateUpdate      -> UIButtonPrompt_LateUpdate
 *   .c L99-112   UIButtonPrompt_StaticUpdate    -> UIButtonPrompt_StaticUpdate
 *   .c L114-128  UIButtonPrompt_Draw            -> UIButtonPrompt_Draw
 *   .c L130-160  UIButtonPrompt_Create          -> UIButtonPrompt_Create
 *   .c L162-168  UIButtonPrompt_StageLoad       -> UIButtonPrompt_StageLoad
 *   .c L170-195  UIButtonPrompt_GetButtonMappings (Saturn: hard-wired)
 *   .c L197-232  UIButtonPrompt_GetGamepadType   (Saturn: SATURN_WHITE)
 *   .c L234-400  UIButtonPrompt_MappingsToFrame  (PC keyboard map; Saturn
 *                                                  collapses to glyph 0)
 *   .c L402-442  UIButtonPrompt_SetButtonSprites
 *   .c L444-480  UIButtonPrompt_CheckTouch       (touch stub on Saturn)
 *   .c L482-497  UIButtonPrompt_State_CheckIfSelected
 *   .c L499-518  UIButtonPrompt_State_Selected
 *
 * Saturn-side adaptations:
 *   * PLATFORM = NTSC-US Saturn — UIButtonPrompt->type pinned to
 *     SATURN_WHITE for 3-button + 6-button + 3D Analog Pad (the SDC
 *     glyph atlas only contains one Saturn variant in UI/Buttons.bin).
 *     The decomp's keyboard/mapping subsystem is bypassed — Saturn
 *     PADs are dedicated controllers, no key remapping.
 *   * Pad-type detection uses rsdk_controller_state()->has_analog to
 *     distinguish 3D Analog Pad from digital pads. Both still use
 *     SATURN_WHITE glyphs for Phase 3.2.c.1; a future 3.2.k extension
 *     can swap to analog-specific glyphs.
 *   * Touch I/O is a no-op stub (no Saturn touchscreen).
 *   * `API_GetConfirmButtonFlip` -> always returns 0 (A=confirm,
 *     B=back per Mega Drive convention per docs/MANIA_MODE_PARITY_PLAN
 *     §9.A). */

#include "UIButtonPrompt.h"
#include "UIControl.h"
#include "UIWidgets.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/drawing.h"
#include "../../../rsdk/input.h"

ObjectUIButtonPrompt *UIButtonPrompt = NULL;

static bool32 _api_get_confirm_button_flip(void) { return 0; }

/* Decomp .c L12-62 — Update. Resyncs prompt animator on text-atlas
 * swap, runs scale tween, runs state machine, refreshes button glyph
 * if prevButton/prevPrompt changed since last frame. */
void UIButtonPrompt_Update(void)
{
    RSDK_THIS(UIButtonPrompt);

    bool32 textChanged = 0;
    if (UIWidgets && self->textSprite != UIWidgets->textFrames) {
        rsdk_set_sprite_animation((int)UIWidgets->textFrames, 0,
                                  &self->promptAnimator, true,
                                  self->promptID);
        textChanged      = 1;
        self->textSprite = UIWidgets->textFrames;
    }

    if (self->scale.x == 0x200 && self->scaleMax == 0x200 && self->scaleSpeed)
        self->scaleSpeed = 0;

    StateMachine_Run(self->state);

    if (self->scale.x >= self->scaleMax) {
        if (self->scale.x > self->scaleMax) {
            self->scale.x -= self->scaleSpeed;
            if (self->scale.x < self->scaleMax)
                self->scale.x = self->scaleMax;
        }
    } else {
        self->scale.x += self->scaleSpeed;
        if (self->scale.x > self->scaleMax)
            self->scale.x = self->scaleMax;
    }
    self->scale.y = self->scale.x;

    if (self->prevPrompt != self->promptID && UIWidgets) {
        rsdk_set_sprite_animation((int)UIWidgets->textFrames, 0,
                                  &self->promptAnimator, true,
                                  self->promptID);
        self->prevPrompt = self->promptID;
    }

    int32 button = self->buttonID;
    if (self->prevButton != button) {
        UIButtonPrompt_SetButtonSprites();
        button           = self->buttonID;
        self->prevButton = button;
    }

    /* Decomp L55-61 — PC keyboard remap. Saturn ships pad-only so this
     * branch never fires (sku_platform is never PLATFORM_PC/DEV). */
    (void)textChanged;
}

/* Decomp .c L64-97 — LateUpdate. If parent's UIControl has a heading
 * and headingAnchor is set, snap position to one of four heading
 * corners. */
void UIButtonPrompt_LateUpdate(void)
{
    RSDK_THIS(UIButtonPrompt);

    EntityUIControl *control = (EntityUIControl *)self->parent;
    if (!control || !control->heading || !self->headingAnchor) return;

    /* heading position lives at offset 0 of EntityUIHeading per the
     * RSDK_ENTITY layout; Phase 3.2.c.2 lands the full UIHeading
     * struct port. For 3.2.c.1 we treat heading as a Vector2-prefixed
     * opaque pointer (position.x at +0, position.y at +4). */
    Vector2 hpos;
    hpos.x = ((int32 *)control->heading)[0];
    hpos.y = ((int32 *)control->heading)[1];

    switch (self->headingAnchor) {
        default:
        case UIBUTTONPROMPT_ANCHOR_NONE:
            break;
        case UIBUTTONPROMPT_ANCHOR_TOPLEFT_ROW1:
            self->position.x = hpos.x - (188 << 16);
            self->position.y = hpos.y - (8 << 16);
            break;
        case UIBUTTONPROMPT_ANCHOR_TOPRIGHT_ROW1:
            self->position.x = hpos.x + (124 << 16);
            self->position.y = hpos.y - (8 << 16);
            break;
        case UIBUTTONPROMPT_ANCHOR_TOPLEFT_ROW2:
            self->position.x = hpos.x - (188 << 16);
            self->position.y = hpos.y + (16 << 16);
            break;
        case UIBUTTONPROMPT_ANCHOR_TOPRIGHT_ROW2:
            self->position.x = hpos.x + (124 << 16);
            self->position.y = hpos.y + (16 << 16);
            break;
    }
}

/* Decomp .c L99-112 — StaticUpdate. Refreshes ObjectUIButtonPrompt
 * globals (current gamepad type + active input slot). */
void UIButtonPrompt_StaticUpdate(void)
{
    if (!UIButtonPrompt) return;
    UIButtonPrompt->type      = UIButtonPrompt_GetGamepadType();
    UIButtonPrompt->inputSlot = CONT_P1;
}

/* Decomp .c L114-128 — Draw. Three sprite layers: decoration backdrop,
 * scaled button glyph, then the optional text prompt label. */
void UIButtonPrompt_Draw(void)
{
    RSDK_THIS(UIButtonPrompt);

    UIButtonPrompt_SetButtonSprites();

    /* Decoration backdrop (e.g. underlying button frame). */
    rsdk_draw_sprite(&self->decorAnimator, self->position.x,
                     self->position.y, FLIP_NONE, false);

    /* Scaled button glyph. drawFX FX_SCALE telegraphs to the rsdk
     * draw layer that scale.x/y should be applied. The Saturn
     * rsdk_draw_sprite ignores drawFX for now (Phase Z scale path);
     * the glyph still renders at native size. */
    self->drawFX = FX_SCALE;
    rsdk_draw_sprite(&self->buttonAnimator, self->position.x,
                     self->position.y, FLIP_NONE, false);

    self->drawFX = FX_NONE;
    if (self->textVisible) {
        rsdk_draw_sprite(&self->promptAnimator, self->position.x,
                         self->position.y, FLIP_NONE, false);
    }
}

/* Decomp .c L130-160 — Create. */
void UIButtonPrompt_Create(void *data)
{
    (void)data;
    RSDK_THIS(UIButtonPrompt);

    if (!SceneInfo->inEditor) {
        self->startPos      = self->position;
        self->visible       = 1;
        self->drawGroup     = 2;
        self->scaleMax      = 0x200;
        self->scaleSpeed    = 0x10;
        self->scale.x       = 0x200;
        self->scale.y       = 0x200;
        self->disableScale  = 0;
        self->active        = ACTIVE_BOUNDS;
        self->updateRange.x = 0x2000000;
        self->updateRange.y = 0x800000;
        self->textVisible   = 1;

        if (UIButtonPrompt) {
            rsdk_set_sprite_animation((int)UIButtonPrompt->aniFrames, 0,
                                      &self->decorAnimator, true, 0);
        }
        if (UIWidgets) {
            rsdk_set_sprite_animation((int)UIWidgets->textFrames, 0,
                                      &self->promptAnimator, true,
                                      self->promptID);
        }

        UIButtonPrompt_SetButtonSprites();

        if (UIWidgets) self->textSprite = UIWidgets->textFrames;
        self->state       = UIButtonPrompt_State_CheckIfSelected;
        self->parent      = NULL;
        self->touchSize.x = 0x580000;
        self->touchSize.y = 0x100000;
        self->touchPos.x  = 0x200000;
    }
}

/* Decomp .c L162-168 — StageLoad. Loads UI/Buttons.bin (the glyph
 * atlas) and pins the per-stage defaults. */
void UIButtonPrompt_StageLoad(void)
{
    if (!UIButtonPrompt) return;
    UIButtonPrompt->type      = UIBUTTONPROMPT_SATURN_WHITE;
    UIButtonPrompt->inputSlot = CONT_P1;
    UIButtonPrompt->aniFrames =
        (uint16)rsdk_load_sprite_animation("UI/Buttons.bin", -1);
}

/* Decomp .c L170-195 — GetButtonMappings. Saturn has no key remap;
 * always return the canonical button-id constant matching the prompt's
 * button. The PC keyboard mappings are unused on Saturn. */
int32 UIButtonPrompt_GetButtonMappings(int32 input, int32 button)
{
    (void)input;
    return button;
}

/* Decomp .c L197-232 — GetGamepadType. Saturn always reports
 * SATURN_WHITE (the only Saturn glyph variant shipped in UI/Buttons.bin).
 * The 3-button / 6-button / 3D Analog detection is exposed via the
 * rsdk_controller_state has_analog flag — currently all three render
 * the same SATURN_WHITE glyphs, but the differentiation is preserved
 * for Phase 3.2.k follow-up. */
int32 UIButtonPrompt_GetGamepadType(void)
{
    const rsdk_controller_state_t *c = rsdk_controller_state(CONT_P1);
    if (!c) return UIBUTTONPROMPT_SATURN_WHITE;

    /* 3-button vs 6-button vs 3D Analog Pad — Saturn glyph atlas
     * currently has one variant (SATURN_WHITE). The has_analog branch
     * could route to a future SATURN_ANALOG variant; for now collapse
     * to SATURN_WHITE so the glyph map call below resolves cleanly. */
    if (c->has_analog) {
        /* 3D Analog Pad — same glyph atlas, but per-pad-type code
         * surface is preserved for 3.2.k. */
        return UIBUTTONPROMPT_SATURN_WHITE;
    }
    /* 3-button + 6-button both render SATURN_WHITE. */
    return UIBUTTONPROMPT_SATURN_WHITE;
}

/* Decomp .c L234-400 — MappingsToFrame. PC keyboard scancode -> glyph
 * frame. Saturn has no keyboard remap so the mapping table collapses:
 * we return the button-id directly which maps 1:1 to the SATURN_WHITE
 * glyph atlas's per-button frame index. */
uint8 UIButtonPrompt_MappingsToFrame(int32 mappings)
{
    if (mappings < 0 || mappings > 255) return 0;
    return (uint8)mappings;
}

/* Decomp .c L402-442 — SetButtonSprites. */
void UIButtonPrompt_SetButtonSprites(void)
{
    RSDK_THIS(UIButtonPrompt);
    if (!UIButtonPrompt) return;

    int32 buttonID = self->buttonID;
    if (_api_get_confirm_button_flip() && buttonID <= 1) {
        buttonID ^= 1;
    }

    /* Saturn-side: SATURN_WHITE glyph set, frame index = buttonID. */
    rsdk_set_sprite_animation((int)UIButtonPrompt->aniFrames,
                              UIButtonPrompt->type,
                              &self->buttonAnimator, true, buttonID);
}

/* Decomp .c L444-480 — CheckTouch (no Saturn touchscreen — stub). */
bool32 UIButtonPrompt_CheckTouch(void)
{
    return 0;
}

/* Decomp .c L482-497 — State_CheckIfSelected. */
void UIButtonPrompt_State_CheckIfSelected(void)
{
    RSDK_THIS(UIButtonPrompt);
    if (self->visible) {
        if (UIButtonPrompt_CheckTouch()) {
            self->scaleMax = 0x280;
            if (self->scaleSpeed < 0x10) self->scaleSpeed = 0x10;
        } else if (!self->disableScale) {
            self->scaleMax = 0x200;
        }
    }
}

/* Decomp .c L499-518 — State_Selected. */
void UIButtonPrompt_State_Selected(void)
{
    RSDK_THIS(UIButtonPrompt);
    self->scaleMax = 0x280;
    if (++self->timer == 16) {
        self->timer       = 0;
        self->textVisible = 1;
        self->state       = UIButtonPrompt_State_CheckIfSelected;
        int32 buttonID = self->buttonID;
        if (_api_get_confirm_button_flip() && buttonID <= 1) buttonID ^= 1;
        UIControl_ClearInputs((uint8)buttonID);
    }
    self->textVisible = !((self->timer >> 1) & 1);
}
