/* Phase 3.2.c.1 (Task #148) — UIButton class body.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UIButton.c`
 * (968 LOC; Christian Whitehead/Simon Thomley/Hunter Bridges; decomp
 * by Rubberduckycooly & RMGRich). Per-function decomp line citations
 * appear inline above each ported body.
 *
 * Per-line decomp citations:
 *   .c L12-47   UIButton_Update                       -> UIButton_Update
 *   .c L49      UIButton_LateUpdate                   -> UIButton_LateUpdate
 *   .c L51      UIButton_StaticUpdate                 -> UIButton_StaticUpdate
 *   .c L53-94   UIButton_Draw                         -> UIButton_Draw
 *   .c L96-147  UIButton_Create                       -> UIButton_Create
 *   .c L149     UIButton_StageLoad                    -> UIButton_StageLoad
 *   .c L151-166 UIButton_ManageChoices                -> UIButton_ManageChoices
 *   .c L168-183 UIButton_GetChoicePtr                 -> UIButton_GetChoicePtr
 *   .c L185-238 UIButton_SetChoiceSelectionWithCB     -> idem
 *   .c L240-272 UIButton_SetChoiceSelection           -> idem
 *   .c L274-286 UIButton_GetActionCB                  -> idem
 *   .c L288     UIButton_FailCB                       -> idem
 *   .c L290-414 UIButton_ProcessButtonCB_Scroll       -> idem
 *   .c L416-463 UIButton_ProcessTouchCB_Multi         -> idem (touch stub)
 *   .c L465-538 UIButton_ProcessTouchCB_Single        -> idem (touch stub)
 *   .c L540-710 UIButton_ProcessButtonCB              -> idem
 *   .c L712-717 UIButton_CheckButtonEnterCB           -> idem
 *   .c L719-724 UIButton_CheckSelectedCB              -> idem
 *   .c L726-761 UIButton_ButtonEnterCB                -> idem
 *   .c L763-791 UIButton_ButtonLeaveCB                -> idem
 *   .c L793-832 UIButton_SelectedCB                   -> idem
 *   .c L834-857 UIButton_State_HandleButtonLeave      -> idem
 *   .c L859-878 UIButton_State_HandleButtonEnter      -> idem
 *   .c L880-898 UIButton_State_Selected               -> idem
 *
 * Saturn-side adaptations:
 *   * `MANIA_USE_PLUS` == 0 — UIWidgets->buttonColor cycling is gated
 *     out (decomp L62 reads it; the non-Plus branch uses 0xF0F0F0 from
 *     L66 which is what Saturn ships).
 *   * Touch I/O has no Saturn equivalent; UIButton_ProcessTouchCB_*
 *     return false. The touchCB pointer is still populated for offset
 *     compat with UIControl_ProcessButtonInput.
 *   * UIChoice / UIResPicker / UIVsRoundPicker / UIWinSize class
 *     pointers are NULL until those classes port (Phase 3.2.c.2 / .3).
 *     The decomp gates every cross-class access on the class pointer
 *     being non-NULL; the Saturn shim does the same so these branches
 *     short-circuit cleanly.
 *   * `UITransition_StartTransition` is the wipe-into-menu transition
 *     (Phase 3.2.d port); Saturn-side stub invokes the action callback
 *     immediately.
 *   * `RSDK.SetSpriteAnimation` / `RSDK.DrawSprite` / `RSDK.PlaySfx` /
 *     `RSDK.GetEntitySlot` map to their Saturn-side rsdk_* / mania_*
 *     equivalents per per-call comments below.
 *   * `Music_Stop` -> Game.h Music_Stop (already declared). */

#include "UIButton.h"
#include "UIControl.h"
#include "UIWidgets.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/drawing.h"
#include "../../../rsdk/audio.h"
#include "../../../rsdk/input.h"   /* CONT_P1, CONT_P2 — Phase 3.2.b-post unblock */

ObjectUIButton *UIButton = NULL;

/* Saturn-side stubs for the cross-class symbols that Phase 3.2.c.1
 * doesn't port yet. Each is a NULL ObjectXXX * pointer; the decomp
 * gates every cross-class access on `UIChoice && ...` so these
 * short-circuit cleanly until 3.2.c.2/3 populates them. */
static const void *UIChoice         = NULL;
static const void *UIVsRoundPicker  = NULL;
static const void *UIResPicker      = NULL;
static const void *UIWinSize        = NULL;

/* UITransition_StartTransition stub — Phase 3.2.d port wipes via
 * UITransition. For Phase 3.2.c.1 invoke the action immediately
 * (visually wrong: no wipe; functionally the menu still advances). */
static void _uitransition_start_transition(void (*cb)(void), int unused)
{
    (void)unused;
    if (cb) cb();
}

/* API_GetFilteredInputDeviceID / API_ResetInputSlotAssignments /
 * API_AssignInputSlotToDevice — Saturn has fixed P1/P2 hard-wired to
 * native pad ports, so the input-device-id concept doesn't apply.
 * Stubs here are no-ops; decomp L803-814 path that calls these only
 * fires when assignsP1 / freeBindP2 attributes are set on the button,
 * which is a Plus-only "controller assign" UI we don't ship. */
#define INPUT_NONE          0
#define INPUT_AUTOASSIGN    1
static int _api_get_filtered_input_device_id(int unused)
{ (void)unused; return 0; }
static void _api_reset_input_slot_assignments(void) {}
static void _api_assign_input_slot_to_device(int slot, int device)
{ (void)slot; (void)device; }

/* RSDK.GetEntitySlot — derives the slot index of an entity by pointer
 * arithmetic against the global byte buffer. Mirrors rsdk_object.h
 * indexing via RSDK_ENTITY_STRIDE. */
static int32 _entity_slot(const void *ent)
{
    if (!ent || !g_rsdk_entity_buf) return -1;
    intptr_t off = (const uint8_t *)ent - g_rsdk_entity_buf;
    if (off < 0) return -1;
    return (int32)(off / RSDK_ENTITY_STRIDE);
}

/* RSDK_GET_ENTITY(slot, klass) — by-slot typed entity getter. */
static EntityUIButton *_get_uibutton_entity(int32 slot)
{
    return (EntityUIButton *)rsdk_entity_at((uint16_t)slot);
}

/* === Decomp .c L12-47 — Update ====================================== */
void UIButton_Update(void)
{
    RSDK_THIS(UIButton);

    self->touchPosSizeS.x   = self->size.x;
    self->touchPosOffsetS.x = 0;
    self->touchPosOffsetS.y = 0;
    self->touchPosSizeS.x  += 3 * self->size.y;
    self->touchPosSizeS.y   = self->size.y + 0x60000;

    if (!UIWidgets) return;

    if (self->textFrames != UIWidgets->textFrames ||
        self->startListID != self->listID ||
        self->startFrameID != self->frameID ||
        self->isDisabled != self->disabled) {
        if (self->disabled) {
            rsdk_set_sprite_animation((int)UIWidgets->textFrames, 7,
                                      &self->animator, true, 0);
        } else {
            rsdk_set_sprite_animation((int)UIWidgets->textFrames,
                                      self->listID, &self->animator,
                                      true, self->frameID);
        }
        self->textFrames   = UIWidgets->textFrames;
        self->startListID  = self->listID;
        self->startFrameID = self->frameID;
        self->isDisabled   = self->disabled;
    }

    EntityUIButton *choice = UIButton_GetChoicePtr(self, self->selection);
    if (choice) {
        choice->visible = 1;
    }

    StateMachine_Run(self->state);

    EntityUIControl *parent = (EntityUIControl *)self->parent;
    if (parent && self->state == UIButton_State_HandleButtonEnter &&
        (parent->state != UIControl_ProcessInputs ||
         (parent->buttonID >= 0 && parent->buttons[parent->buttonID] != self))) {
        self->isSelected = 0;
        UIButton_ButtonLeaveCB();
    }
}

void UIButton_LateUpdate(void) {}
void UIButton_StaticUpdate(void) {}

/* === Decomp .c L53-94 — Draw ======================================== */
void UIButton_Draw(void)
{
    RSDK_THIS(UIButton);

    Vector2 drawPos;
    int32 width = (self->size.x + self->size.y) >> 16;

    /* decomp L60-64 — back parallelogram (highlight pass, light grey on
     * non-Plus). */
    drawPos.x = self->position.x - self->buttonBounceOffset;
    drawPos.y = self->position.y - self->buttonBounceOffset;
    UIWidgets_DrawParallelogram(drawPos.x, drawPos.y, width,
                                self->size.y >> 16, self->bgEdgeSize,
                                0xF0, 0xF0, 0xF0);

    /* decomp L69-71 — black foreground parallelogram. */
    drawPos.x = self->position.x + self->buttonBounceOffset;
    drawPos.y = self->position.y + self->buttonBounceOffset;
    UIWidgets_DrawParallelogram(drawPos.x, drawPos.y, width,
                                self->size.y >> 16, self->bgEdgeSize,
                                0x00, 0x00, 0x00);

    if (self->textVisible) {
        drawPos.x = self->buttonBounceOffset + self->position.x;
        drawPos.y = self->buttonBounceOffset + self->position.y;
        drawPos.y += self->textBounceOffset;

        switch (self->align) {
            case UIBUTTON_ALIGN_LEFT:
                drawPos.x += -0x60000 - (self->size.x >> 1);
                break;
            case UIBUTTON_ALIGN_CENTER:
                break;
            case UIBUTTON_ALIGN_RIGHT:
                drawPos.x -= 0x60000;
                drawPos.x += self->size.x >> 1;
                break;
        }

        if (self->disabled && self->align == UIBUTTON_ALIGN_LEFT) {
            drawPos.x += 0x150000;
        }

        rsdk_draw_sprite(&self->animator, drawPos.x, drawPos.y,
                         FLIP_NONE, false);
    }
}

/* === Decomp .c L96-147 — Create ===================================== */
void UIButton_Create(void *data)
{
    (void)data;
    RSDK_THIS(UIButton);

    if (!SceneInfo->inEditor) {
        self->drawGroup     = 2;
        self->visible       = self->invisible ? 0 : 1;
        self->active        = ACTIVE_BOUNDS;
        self->updateRange.x = (int32)(128 << 16);
        self->updateRange.y = (int32)( 64 << 16);
        self->bgEdgeSize    = self->size.y >> 16;
        if (self->size.y < 0) self->size.y = -self->size.y;

        self->processButtonCB    = UIButton_ProcessButtonCB;
        self->touchCB            = UIButton_ProcessTouchCB_Single;
        self->selectedCB         = UIButton_SelectedCB;
        self->failCB             = UIButton_FailCB;
        self->buttonEnterCB      = UIButton_ButtonEnterCB;
        self->buttonLeaveCB      = UIButton_ButtonLeaveCB;
        self->checkButtonEnterCB = UIButton_CheckButtonEnterCB;
        self->checkSelectedCB    = UIButton_CheckSelectedCB;

        self->textVisible = 1;
        if (UIWidgets) {
            rsdk_set_sprite_animation((int)UIWidgets->textFrames,
                                      self->listID, &self->animator,
                                      true, self->frameID);
            self->textFrames = UIWidgets->textFrames;
        }
        self->startListID  = self->listID;
        self->startFrameID = self->frameID;

        /* decomp L124-145 — UIChoice / UIVsRoundPicker / UIResPicker /
         * UIWinSize childing. All four classes are NULL until Phase
         * 3.2.c.2/3 — the loop short-circuits on the class pointer
         * checks. We still mirror the firstChoicePos capture for the
         * UIButton's own choices array semantic. */
        int32 slot = _entity_slot(self) - self->choiceCount;
        for (int32 i = 0; i < self->choiceCount; ++i) {
            EntityUIButton *item = _get_uibutton_entity(slot + i);
            if (!item) continue;

            /* Decomp L128-134 — gated on UIChoice/UIVsRoundPicker class
             * pointers. Saturn-side they're NULL so this is a no-op. */
            (void)UIChoice;
            (void)UIVsRoundPicker;
            (void)UIResPicker;
            (void)UIWinSize;

            if (i) {
                item->position.x = self->firstChoicePos.x;
                item->position.y = self->firstChoicePos.y;
                item->active     = ACTIVE_NEVER;
            } else {
                self->firstChoicePos.x = item->position.x;
                self->firstChoicePos.y = item->position.y;
            }
        }
    }
}

void UIButton_StageLoad(void) {}

/* === Decomp .c L151-166 — ManageChoices ============================= */
void UIButton_ManageChoices(EntityUIButton *button)
{
    if (!button) return;
    /* decomp gates every choice-class access on UIChoice / UIVsRoundPicker
     * pointers; both are NULL on Saturn until 3.2.c.2/3 ports them.
     * The Saturn-side body short-circuits to a no-op. */
    (void)button;
}

/* === Decomp .c L168-183 — GetChoicePtr ============================== */
EntityUIButton *UIButton_GetChoicePtr(EntityUIButton *button, int32 selection)
{
    if (!button || button->choiceCount <= 0) return NULL;
    /* decomp L171-180 returns the choice entity at
     * slot = GetEntitySlot(button) - choiceCount + selection%choiceCount,
     * but ONLY when its classID matches one of the choice classes.
     * Saturn-side: all choice classes are NULL until 3.2.c.2/3 — return
     * NULL so callers (UIButton_Update / SetChoiceSelection / etc.)
     * skip their choice-aware branches. */
    (void)selection;
    return NULL;
}

/* === Decomp .c L185-238 — SetChoiceSelectionWithCB ================= */
void UIButton_SetChoiceSelectionWithCB(EntityUIButton *button, int32 selection)
{
    if (!button || button->choiceCount <= 0) return;
    button->selection = selection;
    /* Saturn-side choice classes are NULL until 3.2.c.2 — skip the
     * choice-state-update + active/visible toggling that decomp L189-
     * 226 performs. The change callback still fires so menus that
     * watch selection-change react. */
    if (button->choiceChangeCB) {
        rsdk_entity_t *prev = g_rsdk_current_entity;
        g_rsdk_current_entity = (rsdk_entity_t *)button;
        button->choiceChangeCB();
        g_rsdk_current_entity = prev;
    }
}

/* === Decomp .c L240-272 — SetChoiceSelection ======================= */
void UIButton_SetChoiceSelection(EntityUIButton *button, int32 selection)
{
    if (!button || button->choiceCount <= 0) return;
    button->selection = selection;
    /* Saturn-side: no choice activation/deactivation until 3.2.c.2. */
}

/* === Decomp .c L274-286 — GetActionCB ============================== */
void *UIButton_GetActionCB(void)
{
    RSDK_THIS(UIButton);
    EntityUIButton *choice = UIButton_GetChoicePtr(self, self->selection);
    if (!choice) return (void *)self->actionCB;
    /* Decomp L282-285 — choice class can override actionCB. UIChoice
     * port (3.2.c.2) wires this. Saturn-side returns the button's own
     * cb since choice is NULL until then. */
    return (void *)self->actionCB;
}

/* === Decomp .c L288 — FailCB ======================================= */
void UIButton_FailCB(void)
{
    if (UIWidgets) {
        rsdk_play_sfx((int)UIWidgets->sfxFail, 0xFFFFFFFFu, 255);
    }
}

/* === Decomp .c L290-414 — ProcessButtonCB_Scroll ==================== */
void UIButton_ProcessButtonCB_Scroll(void)
{
    RSDK_THIS(UIButton);
    EntityUIControl *control = (EntityUIControl *)self->parent;
    if (!control || !UIControl) return;

    /* Decomp L297-300: SetTargetPos (MANIA_USE_PLUS) / direct assign
     * (non-Plus). Saturn ships non-Plus. */
    control->targetPos.y = self->position.y;

    if (!UIControl_isMoving(control)) {
        int32 rowID = 0;
        int32 colID = 0;

        if (control->rowCount && control->columnCount)
            rowID = control->buttonID / control->columnCount;
        if (control->columnCount)
            colID = control->buttonID % control->columnCount;

        bool32 changedSelection = 0;
        if (control->rowCount > 1) {
            if (UIControl->anyUpPress)   { --rowID; changedSelection = 1; }
            if (UIControl->anyDownPress) { ++rowID; changedSelection = 1; }
        }
        if (UIControl->anyLeftPress)  { --colID; changedSelection = 1; }
        if (UIControl->anyRightPress) { ++colID; changedSelection = 1; }

        if (changedSelection) {
            /* Non-Plus branch (decomp L364-379): wrap around. */
            if (rowID < 0)                 rowID += control->rowCount;
            if (rowID >= control->rowCount) rowID -= control->rowCount;
            if (colID < 0)                  colID += control->columnCount;
            if (colID >= control->columnCount) colID -= control->columnCount;

            int32 id = control->buttonCount - 1;
            if (colID + rowID * control->columnCount < id)
                id = colID + rowID * control->columnCount;

            if (control->buttonID != id) {
                control->buttonID = id;
                StateMachine_Run(self->buttonLeaveCB);
                if (UIWidgets)
                    rsdk_play_sfx((int)UIWidgets->sfxBleep, 0xFFFFFFFFu, 0xFF);
            }
        } else {
            bool32 hasNoAction = 1;
            if (UIControl->anyConfirmPress) {
                if (self->disabled) {
                    StateMachine_Run(self->failCB);
                } else {
                    hasNoAction = !self->actionCB;
                }
            }

            if (hasNoAction) {
                if (!self->isSelected) {
                    if (control->buttonID == UIControl_GetButtonID(control, self) &&
                        control->state == UIControl_ProcessInputs &&
                        !control->dialogHasFocus) {
                        StateMachine_Run(self->buttonEnterCB);
                    }
                }
            } else {
                StateMachine_Run(self->selectedCB);
            }
        }
    }
}

/* === Decomp .c L416-463 — ProcessTouchCB_Multi (touch stub) ========= */
bool32 UIButton_ProcessTouchCB_Multi(void)
{
    /* Saturn has no touch input — return false so UIControl doesn't
     * treat any button as touched. The touchCB pointer still has to
     * exist so UIControl_ProcessButtonInput's NULL-check passes. */
    return 0;
}

/* === Decomp .c L465-538 — ProcessTouchCB_Single (touch stub) ======== */
bool32 UIButton_ProcessTouchCB_Single(void)
{
    return 0;
}

/* === Decomp .c L540-710 — ProcessButtonCB =========================== */
void UIButton_ProcessButtonCB(void)
{
    RSDK_THIS(UIButton);
    EntityUIControl *control = (EntityUIControl *)self->parent;
    if (!control || !UIControl) return;

    int32 columnID = 0, rowID = 0;
    if (control->rowCount && control->columnCount)
        rowID = control->buttonID / control->columnCount;
    if (control->columnCount)
        columnID = control->buttonID % control->columnCount;

    bool32 movedV = 0;
    if (control->rowCount > 1) {
        if (UIControl->anyUpPress)   { movedV = 1; --rowID; }
        if (UIControl->anyDownPress) { movedV = 1; ++rowID; }
    }

    int32 selection = self->selection;
    bool32 movedH   = 0;

    EntityUIButton *choice = UIButton_GetChoicePtr(self, self->selection);
    (void)choice; /* Saturn: always NULL until 3.2.c.2 */

    if (UIControl->anyLeftPress) {
        if (self->choiceCount <= 0 || self->choiceDir || self->disabled) {
            if (control->columnCount > 1) {
                movedV = 1;
                columnID--;
            }
            movedH = 0;
        } else {
            if (--selection < 0) {
                while (selection < 0) selection += self->choiceCount;
            }
            movedH = 1;
        }
    }

    if (UIControl->anyRightPress) {
        if (self->choiceCount <= 0 || self->choiceDir || self->disabled) {
            if (control->columnCount > 1) {
                ++columnID;
                movedV = 1;
            }
            movedH = 0;
        } else {
            selection = (selection + 1) % self->choiceCount;
            movedH = 1;
        }
    }

    if (movedH) {
        if (selection < 0) selection += self->choiceCount;
        if (selection >= self->choiceCount) selection -= self->choiceCount;
        if (selection != self->selection) {
            UIButton_SetChoiceSelectionWithCB(self, selection);
            if (UIWidgets)
                rsdk_play_sfx((int)UIWidgets->sfxBleep, 0xFFFFFFFFu, 255);
        }
    }

    if (movedV) {
        /* Non-Plus wrap path (decomp L668-678). */
        if (rowID < 0) rowID += control->rowCount;
        if (rowID >= control->rowCount) rowID -= control->rowCount;
        if (columnID < 0) columnID += control->columnCount;
        if (columnID >= control->columnCount) columnID -= control->columnCount;

        int32 id = columnID + control->columnCount * rowID;
        if (id >= control->buttonCount - 1) id = control->buttonCount - 1;

        if (control->buttonID != id && self != control->buttons[id]) {
            control->buttonID = id;
            UIButton_ButtonLeaveCB();
            if (UIWidgets)
                rsdk_play_sfx((int)UIWidgets->sfxBleep, 0xFFFFFFFFu, 255);
        }
    } else {
        void (*actionCB)(void) = (void (*)(void))UIButton_GetActionCB();

        if (UIControl->anyConfirmPress && actionCB) {
            if (self->disabled) {
                if (UIWidgets)
                    rsdk_play_sfx((int)UIWidgets->sfxFail, 0xFFFFFFFFu, 255);
            } else {
                UIButton_SelectedCB();
            }
        } else {
            if (self->state != UIButton_State_HandleButtonEnter &&
                self->state != UIButton_State_Selected) {
                if (control->buttonID == columnID + rowID * control->columnCount &&
                    control->state == UIControl_ProcessInputs) {
                    UIButton_ButtonEnterCB();
                }
            }
        }
    }
}

/* === Decomp .c L712-717 — CheckButtonEnterCB ======================= */
bool32 UIButton_CheckButtonEnterCB(void)
{
    RSDK_THIS(UIButton);
    return self->state == UIButton_State_HandleButtonEnter;
}

/* === Decomp .c L719-724 — CheckSelectedCB ========================== */
bool32 UIButton_CheckSelectedCB(void)
{
    RSDK_THIS(UIButton);
    return self->state == UIButton_State_Selected;
}

/* === Decomp .c L726-761 — ButtonEnterCB ============================ */
void UIButton_ButtonEnterCB(void)
{
    RSDK_THIS(UIButton);
    if (self->state != UIButton_State_HandleButtonEnter) {
        self->textBounceOffset     = 0;
        self->buttonBounceOffset   = 0;
        self->textBounceVelocity   = -0x20000;
        self->buttonBounceVelocity = -0x20000;
        self->state                = UIButton_State_HandleButtonEnter;
        /* Decomp L737-758 — activate the current choice via UIChoice/
         * UIVsRoundPicker/UIResPicker/UIWinSize class dispatch. All
         * choice classes are NULL on Saturn until 3.2.c.2; skip. */
    }
}

/* === Decomp .c L763-791 — ButtonLeaveCB ============================ */
void UIButton_ButtonLeaveCB(void)
{
    RSDK_THIS(UIButton);
    self->state = UIButton_State_HandleButtonLeave;
    /* Decomp L769-790 — deactivate the current choice. Saturn: NULL
     * choice classes -> no-op. */
}

/* === Decomp .c L793-832 — SelectedCB =============================== */
void UIButton_SelectedCB(void)
{
    RSDK_THIS(UIButton);

    EntityUIControl *parent = (EntityUIControl *)self->parent;
    EntityUIButton *choice  = UIButton_GetChoicePtr(self, self->selection);
    (void)choice;

    if (self->clearParentState && parent) {
        parent->state = StateMachine_None;
    }

    if (self->assignsP1) {
        int32 id = _api_get_filtered_input_device_id(INPUT_NONE);
        _api_reset_input_slot_assignments();
        _api_assign_input_slot_to_device(CONT_P1, id);
    }
    if (self->freeBindP2) {
        _api_assign_input_slot_to_device(CONT_P2, INPUT_AUTOASSIGN);
    }

    if (parent) parent->backoutTimer = 30;

    if (self->transition) {
        void (*actionCB)(void) = (void (*)(void))self->actionCB;
        _uitransition_start_transition(actionCB, 14);
    }

    if (self->stopMusic) {
        Music_Stop();
    }

    self->timer = 0;
    self->state = UIButton_State_Selected;
    if (UIWidgets) {
        rsdk_play_sfx((int)UIWidgets->sfxAccept, 0xFFFFFFFFu, 255);
    }
}

/* === Decomp .c L834-857 — State_HandleButtonLeave ================== */
void UIButton_State_HandleButtonLeave(void)
{
    RSDK_THIS(UIButton);

    if (self->textBounceOffset) {
        int32 sign = (self->textBounceOffset > 0) ? -1 : 1;
        int32 offset = sign;
        self->textBounceOffset += offset << 16;
        if (sign < 0 && self->textBounceOffset < 0)
            self->textBounceOffset = 0;
        else if (sign > 0 && self->textBounceOffset > 0)
            self->textBounceOffset = 0;
    }

    if (self->buttonBounceOffset) {
        int32 sign = (self->buttonBounceOffset > 0) ? -1 : 1;
        int32 offset = sign;
        self->buttonBounceOffset += offset << 16;
        if (sign < 0 && self->buttonBounceOffset < 0)
            self->buttonBounceOffset = 0;
        else if (sign > 0 && self->buttonBounceOffset > 0)
            self->buttonBounceOffset = 0;
    }
}

/* === Decomp .c L859-878 — State_HandleButtonEnter ================== */
void UIButton_State_HandleButtonEnter(void)
{
    RSDK_THIS(UIButton);

    self->textBounceVelocity += 0x4000;
    self->textBounceOffset   += self->textBounceVelocity;
    if (self->textBounceOffset >= 0 && self->textBounceVelocity > 0) {
        self->textBounceOffset   = 0;
        self->textBounceVelocity = 0;
    }

    self->buttonBounceVelocity += 0x4800;
    self->buttonBounceOffset   += self->buttonBounceVelocity;
    if (self->buttonBounceOffset >= -0x20000 && self->buttonBounceVelocity > 0) {
        self->buttonBounceOffset   = -0x20000;
        self->buttonBounceVelocity = 0;
    }
}

/* === Decomp .c L880-898 — State_Selected =========================== */
void UIButton_State_Selected(void)
{
    RSDK_THIS(UIButton);
    UIButton_State_HandleButtonEnter();
    if (++self->timer == 30) {
        self->timer = 0;
        if (!self->transition) {
            void (*actionCB)(void) = (void (*)(void))UIButton_GetActionCB();
            if (actionCB) actionCB();
        }
        self->state = UIButton_State_HandleButtonEnter;
    }
    self->textVisible = !((self->timer >> 1) & 1);
}
