/* Phase 3.2.a (Task #145) — UIControl class body.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UIControl.c`
 * (876 LOC; Christian Whitehead/Simon Thomley/Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich). Each function below cites the decomp line
 * range it mirrors.
 *
 * Per-line decomp citations:
 *   .c L12-28    UIControl_Update             -> UIControl_Update
 *   .c L30       UIControl_LateUpdate         -> UIControl_LateUpdate
 *   .c L32-46    UIControl_StaticUpdate       -> UIControl_StaticUpdate
 *   .c L48-54    UIControl_Draw               -> UIControl_Draw
 *   .c L56-124   UIControl_Create             -> UIControl_Create
 *   .c L126-132  UIControl_StageLoad          -> UIControl_StageLoad
 *   .c L134-144  UIControl_GetUIControl       -> UIControl_GetUIControl
 *   .c L146-185  UIControl_ClearInputs        -> UIControl_ClearInputs
 *   .c L187-342  UIControl_ProcessInputs      -> UIControl_ProcessInputs
 *   .c L344-352  UIControl_GetButtonID        -> UIControl_GetButtonID
 *   .c L354-417  UIControl_MenuChangeButtonInit-> ditto (Phase 3.2.b stub
 *                                                  for UIButton dispatch)
 *   .c L426-479  UIControl_SetActiveMenu      -> UIControl_SetActiveMenu
 *   .c L481-520  UIControl_SetMenuLostFocus   -> UIControl_SetMenuLostFocus
 *   .c L522-536  UIControl_SetInactiveMenu    -> UIControl_SetInactiveMenu
 *   .c L538-598  UIControl_SetupButtons       -> Phase 3.2.b stub
 *   .c L600-615  UIControl_isMoving           -> UIControl_isMoving
 *   .c L617-630  UIControl_MatchMenuTag       -> UIControl_MatchMenuTag
 *   .c L632-643  UIControl_HandleMenuChange   -> UIControl_HandleMenuChange
 *   .c L645-654  UIControl_HandleMenuLoseFocus-> ditto
 *   .c L656-662  UIControl_ReturnToParentMenu -> ditto
 *   .c L707-735  UIControl_HandlePosition     -> UIControl_HandlePosition
 *   .c L737-823  UIControl_ProcessButtonInput -> Phase 3.2.b stub
 *   .c L825-839  UIControl_ContainsPos        -> UIControl_ContainsPos
 *
 * Saturn-side adaptations:
 *   * `ControllerInfo[CONT_P1+i].keyA.press` etc. (decomp .c L195-224) map
 *     to Saturn-side `rsdk_controller_state(CONT_P1+i)->key_a.press` etc.
 *     The new src/rsdk/input.h field set (Phase 3.2.a extensions) includes
 *     key_l, key_r, analog_stick_x/y so 6-button + 3D Analog Control Pad
 *     compatibility is preserved.
 *   * `AnalogStickInfoL[]` per-player analog stick mirror is absorbed into
 *     the Saturn controller_state's analog_stick_x/y/has_analog fields;
 *     the .ProcessInputs body ORs the stick deflection into the dpad
 *     press bits via the rsdk_input.c shim, so no per-class translation
 *     is needed beyond reading key_up/.../key_right.press as usual.
 *   * `Unknown_pausePress` (decomp .c L247) is a stop-of-game external
 *     trigger; Saturn currently has no equivalent (no pause-menu yet —
 *     3.2.m), so we treat it as zero.
 *   * `API_GetConfirmButtonFlip()` (decomp .c L172, L217, L237) maps to
 *     Saturn-side `rsdk_api_get_confirm_button_flip()` which returns 0
 *     for the NTSC-US Saturn build per docs/MANIA_MODE_PARITY_PLAN.md
 *     §9.A (PS-style "B confirms" is platform-specific; Saturn uses
 *     A=confirm + B=back per Mega Drive convention).
 *   * `foreach_all(Class, var)` (decomp .c L92-99, L135-142, L545-571,
 *     L623-628) is the RSDK entity-table walk macro; we mirror it via
 *     `rsdk_get_all_entity(class_id, &cursor)` from src/rsdk/object.h.
 *   * `RSDK.SetString` / `RSDK.CompareStrings` for the tag-matching path
 *     (decomp .c L622-625): use a minimal ASCII compare on the
 *     ManiaString chars[] (UTF-16-LE on disk, stored as uint16). Until
 *     UIWidgets ApplyLanguage lands the string layer in 3.2.c the
 *     widget chars[] is the per-entity Scene1.bin string attribute
 *     parsed by the scene loader.
 *   * `UITransition_StartTransition` (decomp .c L288) is the wipe-into-
 *     parent-menu transition; Phase 3.2.d ports it. Saturn-side stub
 *     directly invokes the callback (no animated wipe yet).
 *   * `RSDK.AddCamera` / `RSDK.ClearCameras` (decomp .c L444-445) drive
 *     the per-control viewport scroll. Saturn-side stub — the Saturn
 *     viewport is always 320x224 NTSC-US fixed and Phase 3.2.a doesn't
 *     drive sub-menu camera scrolling.
 *   * `UIButton_*` / `UIChoice_*` / `UITAZoneModule_*` / `UIModeButton_*`
 *     / `UIVsZoneButton_*` / `UIHeading_*` dispatch (decomp .c L373-407,
 *     L578-587) all gate on `NULL` class pointers — they're not registered
 *     until Phase 3.2.b/c/h, so the dispatch tables resolve to no-ops
 *     here. Once those classes register the `extern ObjectXXX *XXX`
 *     pointers, this body's NULL-checks will fall through and the
 *     per-widget update calls will route correctly.
 *
 * Phase 3.2.a scope: this class is REGISTERED + INPUT-ROUTED so Menu/
 * Scene1.bin's 27 UIControl entity hashes resolve and the per-control
 * input-processing state machine runs every frame. Button dispatch lands
 * in 3.2.b; transition wipes in 3.2.d. */

#include "UIControl.h"
#include "UIBackground.h"
#include "UIButton.h"
#include "../../../rsdk/input.h"
#include "../../../rsdk/object.h"
#include <string.h>

ObjectUIControl *UIControl = NULL;

/* Saturn-side stub for API_GetConfirmButtonFlip (NTSC-US uses A=confirm). */
static bool32 _api_get_confirm_button_flip(void) { return 0; }

/* Saturn-side stub for Unknown_pausePress (no pause-menu yet — 3.2.m). */
#define UICONTROL_UNKNOWN_PAUSEPRESS  ((bool32)0)

/* Saturn-side stub for UITransition_StartTransition (Phase 3.2.d port).
 * Falls back to invoking the callback immediately — visually wrong but
 * functionally complete (no wipe animation). */
static void _uitransition_start_transition(void (*cb)(void), int unused)
{
    (void)unused;
    if (cb) cb();
}

/* Saturn-side helper: mirror decomp's `RSDK.GetEntitySlot(self)`.
 * For Phase 3.2.a we compute slot by pointer arithmetic against the
 * entity table base. Returns -1 if ent is null or out of range.
 * Currently unused at top-level — referenced from MenuChangeButtonInit
 * + SetupButtons stubs in Phase 3.2.b when the foreach_all UIButton
 * walks land. Mark as ATTR_UNUSED for clean -Wall pass. */
#ifdef __GNUC__
__attribute__((unused))
#endif
static int32 _entity_slot(rsdk_entity_t *ent)
{
    if (!ent || !g_rsdk_entity_buf) return -1;
    intptr_t off = (uint8_t *)ent - g_rsdk_entity_buf;
    if (off < 0) return -1;
    return (int32)(off / RSDK_ENTITY_STRIDE);
}

/* foreach_all(UIControl, var) is supplied by mania/Game.h via the
 * generic decomp-idiom macro (Game.h L290-294). We use it directly
 * throughout this file to mirror decomp call sites. */

/* Decomp .c L12-28 — Update. Routes self->state + self->menuUpdateCB. */
void UIControl_Update(void)
{
    RSDK_THIS(UIControl);

    if (self->buttonID >= 0 && self->buttonID != self->lastButtonID)
        self->lastButtonID = self->buttonID;

    if (!UIControl->hasTouchInput && self->buttonID == -1)
        self->buttonID = self->lastButtonID;

    StateMachine_Run(self->state);

    if (self->backoutTimer > 0)
        self->backoutTimer--;

    StateMachine_Run(self->menuUpdateCB);
}

/* Decomp .c L30. */
void UIControl_LateUpdate(void) {}

/* Decomp .c L32-46 — StaticUpdate. Latches inputLocked per-frame. */
void UIControl_StaticUpdate(void)
{
    if (!UIControl) return;

    if (UIControl->lockInput) {
        UIControl->lockInput   = 0;
        UIControl->inputLocked = 1;
    } else {
        UIControl->inputLocked = 0;
    }

    UIControl->forceBackPress = 0;

    ++UIControl->timer;
    UIControl->timer &= 0x7FFF;
}

/* Decomp .c L48-54 — Draw. Per-control viewport scroll origin.
 * The Saturn-side ScreenInfo->position is currently unused by the menu
 * rendering pipeline (no per-control camera scroll until 3.2.d) so this
 * remains a write-only assignment that the Saturn drawing layer doesn't
 * read. Keep the line to preserve decomp parity for when 3.2.d wires
 * the camera. */
void UIControl_Draw(void)
{
    RSDK_THIS(UIControl);
    /* decomp .c L52-53:
     *   ScreenInfo->position.x = FROM_FIXED(self->position.x) -
     *                            ScreenInfo->center.x;
     *   ScreenInfo->position.y = FROM_FIXED(self->position.y) -
     *                            ScreenInfo->center.y;
     * FROM_FIXED = (>> 16). */
    if (ScreenInfo) {
        ScreenInfo->position.x = (self->position.x >> 16) -
                                 ScreenInfo->center.x;
        ScreenInfo->position.y = (self->position.y >> 16) -
                                 ScreenInfo->center.y;
    }
}

/* Decomp .c L56-124 — Create.
 * Sized-by-data variant: the scene-bin attribute payload for UIControl
 * includes a size Vector2 that this body reads when `data != NULL`. */
void UIControl_Create(void *data)
{
    RSDK_THIS(UIControl);

    if (!SceneInfo->inEditor) {
        if (data) {
            Vector2 *size = (Vector2 *)data;
            self->size.x = size->x;
            self->size.y = size->y;
        }

        self->updateRange.x = self->size.x >> 1;
        self->updateRange.y = self->size.y >> 1;

        if (self->rowCount <= 1)    self->rowCount    = 1;
        if (self->columnCount <= 1) self->columnCount = 1;

        if (!self->hasStoredButton)
            self->buttonID = self->startingID;
        else
            self->buttonID = self->storedButtonID;

        self->position.x += self->cameraOffset.x;
        self->position.y += self->cameraOffset.y;
        self->startPos.x  = self->position.x;
        self->startPos.y  = self->position.y;

        /* decomp .c L91-99 — UIButtonPrompt childing. UIButtonPrompt
         * lands in Phase 3.2.c; Saturn-side this loop is a no-op until
         * the class registers. */

        /* decomp .c L102-105 — analog-stick deadzone setup. Saturn-
         * side the deadzone is hardcoded in rsdk_input.c (Phase
         * 3.2.a extensions). Skip. */

        UIControl_SetupButtons();

        if (self->noWidgets) {
            self->active  = ACTIVE_NORMAL;
            self->visible = 1;
        } else {
            if (self->activeOnLoad)
                UIControl_SetActiveMenu(self);
            else
                self->active = ACTIVE_NEVER;
        }
    }
}

/* Decomp .c L126-132 — StageLoad. */
void UIControl_StageLoad(void)
{
    if (!UIControl) return;
    UIControl->inputLocked       = 0;
    UIControl->lockInput         = 0;
    UIControl->active            = ACTIVE_ALWAYS;
    UIControl->isProcessingInput = 0;
}

/* Decomp .c L134-144 — GetUIControl. Walks the UIControl class for the
 * single entity whose active flag is ACTIVE_ALWAYS. */
EntityUIControl *UIControl_GetUIControl(void)
{
    int cid = rsdk_object_find_class("UIControl");
    if (cid < 0) return NULL;
    int cursor = -1;
    EntityUIControl *control;
    while ((control = (EntityUIControl *)rsdk_get_all_entity(
                          (uint16_t)cid, &cursor)) != NULL) {
        if (control->active == ACTIVE_ALWAYS) return control;
    }
    return NULL;
}

/* Decomp .c L146-185 — ClearInputs. Wipes the per-player press[] arrays
 * and forces a specific button's any*Press latch on. */
void UIControl_ClearInputs(uint8 buttonID)
{
    if (!UIControl) return;
    for (int32 i = 0; i < MANIA_MENU_PLAYER_COUNT; ++i) {
        UIControl->upPress[i]      = 0;
        UIControl->downPress[i]    = 0;
        UIControl->leftPress[i]    = 0;
        UIControl->rightPress[i]   = 0;
        UIControl->yPress[i]       = 0;
        UIControl->xPress[i]       = 0;
        UIControl->backPress[i]    = 0;
        UIControl->confirmPress[i] = 0;
    }

    UIControl->anyUpPress    = 0;
    UIControl->anyDownPress  = 0;
    UIControl->anyLeftPress  = 0;
    UIControl->anyRightPress = 0;
    /* decomp UIBUTTONPROMPT_BUTTON_Y / _X / _A / _B constants used as
     * button-IDs; Phase 3.2.c UIButtonPrompt port defines them. For
     * now use literal values: A=4, B=5, X=8, Y=9 mirroring decomp
     * UIButtonPrompt.h enum. */
    enum { _PB_A = 4, _PB_B = 5, _PB_X = 8, _PB_Y = 9, _PB_SELECT = 11 };
    UIControl->anyYPress = (buttonID == _PB_Y) ? 1 : 0;
    UIControl->anyXPress = (buttonID == _PB_X) ? 1 : 0;

    if (_api_get_confirm_button_flip()) {
        UIControl->anyConfirmPress = (buttonID == _PB_B) ? 1 : 0;
        UIControl->anyBackPress    = (buttonID == _PB_A) ? 1 : 0;
        UIControl->forceBackPress  = (buttonID == _PB_A) ? 1 : 0;
    } else {
        UIControl->anyConfirmPress = (buttonID == _PB_A) ? 1 : 0;
        UIControl->anyBackPress    = (buttonID == _PB_B) ? 1 : 0;
        UIControl->forceBackPress  = (buttonID == _PB_B) ? 1 : 0;
    }

    UIControl->lockInput   = 1;
    UIControl->inputLocked = 1;
}

/* Decomp .c L187-342 — ProcessInputs. The core input-routing state.
 *
 * Reads per-player rsdk_controller_state(CONT_P1+i) and broadcasts the
 * press bits across UIControl's globals. Then dispatches the active
 * button's processButtonCB OR the per-control's processButtonInputCB
 * override. */
void UIControl_ProcessInputs(void)
{
    RSDK_THIS(UIControl);

    UIControl_HandlePosition();

    if (!UIControl->inputLocked) {
        for (int32 i = 0; i < MANIA_MENU_PLAYER_COUNT; ++i) {
            const rsdk_controller_state_t *c =
                rsdk_controller_state(CONT_P1 + i);
            if (!c) continue;

            /* decomp .c L195-198 OR's AnalogStickInfoL into dpad press
             * bits; the rsdk_input.c shim already does this so we just
             * read key_up/down/left/right.press as the merged result. */
            UIControl->upPress[i]    = c->key_up.press;
            UIControl->downPress[i]  = c->key_down.press;
            UIControl->leftPress[i]  = c->key_left.press;
            UIControl->rightPress[i] = c->key_right.press;

            if (UIControl->upPress[i] && UIControl->downPress[i]) {
                UIControl->upPress[i]   = 0;
                UIControl->downPress[i] = 0;
            }
            if (UIControl->leftPress[i] && UIControl->rightPress[i]) {
                UIControl->leftPress[i]  = 0;
                UIControl->rightPress[i] = 0;
            }

            UIControl->yPress[i] = c->key_y.press;
            UIControl->xPress[i] = c->key_x.press;

            UIControl->confirmPress[i] = c->key_start.press;
            if (_api_get_confirm_button_flip()) {
                UIControl->confirmPress[i] |= c->key_b.press;
                UIControl->backPress[i]     = c->key_a.press;
            } else {
                UIControl->confirmPress[i] |= c->key_a.press;
                UIControl->backPress[i]     = c->key_b.press;
            }
        }

        /* decomp .c L227-245 — any-controller (CONT_ANY) press latches. */
        const rsdk_controller_state_t *any =
            rsdk_controller_state(CONT_ANY);
        if (any) {
            UIControl->anyUpPress    = any->key_up.press;
            UIControl->anyDownPress  = any->key_down.press;
            UIControl->anyLeftPress  = any->key_left.press;
            UIControl->anyRightPress = any->key_right.press;
            UIControl->anyYPress     = any->key_y.press;
            UIControl->anyXPress     = any->key_x.press;

            UIControl->anyConfirmPress = any->key_start.press;
            if (_api_get_confirm_button_flip()) {
                UIControl->anyConfirmPress |= any->key_b.press;
                UIControl->anyBackPress     = any->key_a.press;
            } else {
                UIControl->anyConfirmPress |= any->key_a.press;
                UIControl->anyBackPress     = any->key_b.press;
            }

            UIControl->anyBackPress |= UICONTROL_UNKNOWN_PAUSEPRESS;
            UIControl->anyBackPress |= UIControl->forceBackPress;

            if (UIControl->anyBackPress) {
                UIControl->anyConfirmPress = 0;
                UIControl->anyYPress       = 0;
            }
            if (UIControl->anyConfirmPress) {
                UIControl->anyYPress = 0;
            }
        }

        UIControl->inputLocked = 1;
    }

    if (!self->selectionDisabled) {
        bool32 backPressed = 0;

        if (UIControl->anyBackPress) {
            if (!self->childHasFocus && !self->dialogHasFocus &&
                self->backoutTimer <= 0) {
                if (self->backPressCB) {
                    backPressed = self->backPressCB();
                    if (!backPressed) {
                        UIControl->anyBackPress = 0;
                    } else {
                        /* decomp .c L278-280 — clear selected on
                         * active button. UIButton class isn't ported
                         * in 3.2.a (Phase 3.2.b); the buttons[] array
                         * holds NULL pointers so this is a no-op. */
                        if (self->buttons[self->buttonID]) {
                            /* FIXME 3.2.b: button->isSelected = false; */
                        }
                    }
                } else {
                    if (self->parentTag.length <= 0) {
                        UIControl->anyBackPress = 0;
                    } else {
                        self->selectionDisabled = 1;
                        _uitransition_start_transition(
                            UIControl_ReturnToParentMenu, 0);
                        backPressed = 0;
                        if (self->buttons[self->buttonID]) {
                            /* FIXME 3.2.b: button->isSelected = false; */
                        }
                    }
                }
                if (backPressed) return;
            }
        }

        if (self->processButtonInputCB) {
            StateMachine_Run(self->processButtonInputCB);
        } else {
            UIControl_ProcessButtonInput();
        }

        if (!self->selectionDisabled) {
            if (UIControl->anyYPress) {
                if (!self->childHasFocus && !self->dialogHasFocus &&
                    self->backoutTimer <= 0) {
                    StateMachine_Run(self->yPressCB);
                }
                UIControl->anyYPress = 0;
            }
            if (UIControl->anyXPress) {
                if (!self->childHasFocus && !self->dialogHasFocus &&
                    self->backoutTimer <= 0) {
                    StateMachine_Run(self->xPressCB);
                }
                UIControl->anyXPress = 0;
            }
        }
    }
}

/* Decomp .c L344-352 — GetButtonID. */
int32 UIControl_GetButtonID(EntityUIControl *control, EntityUIButton *entity)
{
    for (int32 i = 0; i < control->buttonCount; ++i) {
        if (entity == control->buttons[i]) return i;
    }
    return -1;
}

/* Decomp .c L354-417 — MenuChangeButtonInit. Walks every UIButton-class
 * entity inside this control's bounding box, dispatches the per-class
 * Update to refresh its visual state, and adds it to the draw list.
 *
 * Phase 3.2.c.1 (Task #148) — UIButton is now ported. The other
 * cross-class dispatch branches (UIChoice / UITAZoneModule /
 * UIModeButton / UIVsZoneButton / UIHeading) gate on NULL class
 * pointers which is the decomp-canonical short-circuit until those
 * classes land in 3.2.c.2/3 / .h. */
void UIControl_MenuChangeButtonInit(EntityUIControl *control)
{
    if (!control) return;

    rsdk_entity_t *storeEntity = g_rsdk_current_entity;
    int uibutton_cid = rsdk_object_find_class("UIButton");

    /* decomp L361-364 — half-screen bounds. */
    int32 left   = -(ScreenInfo->size.x << 16) >> 1;
    int32 right  =  (ScreenInfo->size.x << 16) >> 1;
    int32 top    = -(ScreenInfo->size.y << 16) >> 1;
    int32 bottom =  (ScreenInfo->size.y << 16) >> 1;

    /* decomp L357-415 — walks every entity slot looking for UIButton-
     * class entries inside the control's bbox. We mirror the slot walk
     * directly (no foreach_all macro since the decomp iterates over
     * ALL slots, not the active-entity ring). */
    for (uint16_t i = 0; i < RSDK_RESERVE_ENTITY_COUNT + RSDK_SCENEENTITY_COUNT; ++i) {
        rsdk_entity_t *ent = rsdk_entity_at(i);
        if (!ent || ent->class_id == 0) continue;

        if (ent->position.x >= control->position.x + left &&
            ent->position.x <= control->position.x + right &&
            ent->position.y >= control->position.y + top &&
            ent->position.y <= control->position.y + bottom) {

            g_rsdk_current_entity = ent;

            if (uibutton_cid >= 0 && (int)ent->class_id == uibutton_cid) {
                EntityUIButton *button = (EntityUIButton *)ent;
                UIButton_ManageChoices(button);
                UIButton_Update();
            }
            /* FIXME Phase 3.2.c.2+: UIChoice / UITAZoneModule /
             * UIModeButton / UIVsZoneButton / UIHeading dispatch
             * branches (decomp L375-407) — gated on NULL class
             * pointers until those classes register. */

            g_rsdk_current_entity = storeEntity;
        }
    }
    g_rsdk_current_entity = storeEntity;
}

/* Decomp .c L426-479 — SetActiveMenu. Promotes a single UIControl to
 * ACTIVE_ALWAYS and routes it through the state-machine init. */
void UIControl_SetActiveMenu(EntityUIControl *entity)
{
    if (!entity) return;

    entity->active  = ACTIVE_ALWAYS;
    entity->visible = 1;

    if (entity->hasStoredButton) {
        entity->buttonID        = entity->storedButtonID;
        entity->storedButtonID  = 0;
        entity->hasStoredButton = 0;
    } else if (entity->resetSelection) {
        entity->buttonID = entity->startingID;
    }

    /* decomp .c L444-445: RSDK.ClearCameras() + RSDK.AddCamera().
     * Saturn-side no-op until Phase 3.2.d adds the camera-driven
     * per-control scroll. */

    UIControl_MenuChangeButtonInit(entity);

    if (!entity->childHasFocus) {
        entity->position.x  = entity->startPos.x;
        entity->position.y  = entity->startPos.y;
        entity->targetPos.x = entity->startPos.x;
        entity->targetPos.y = entity->startPos.y;
    }

    entity->state         = UIControl_ProcessInputs;
    entity->childHasFocus = 0;

    if (entity->menuSetupCB) {
        /* decomp .c L470-477: SceneInfo->entity swap so the callback's
         * RSDK_THIS resolves to `entity`. Saturn-side we swap
         * g_rsdk_current_entity for the duration of the call. */
        rsdk_entity_t *prev = g_rsdk_current_entity;
        g_rsdk_current_entity = (rsdk_entity_t *)entity;
        entity->menuSetupCB();
        g_rsdk_current_entity = prev;
    }
}

/* Decomp .c L481-520 — SetMenuLostFocus. Used when a sub-menu opens on
 * top of this control. */
void UIControl_SetMenuLostFocus(EntityUIControl *entity)
{
    if (!entity) return;
    entity->active  = ACTIVE_ALWAYS;
    entity->visible = 1;

    if (!entity->dialogHasFocus && !entity->childHasFocus) {
        if (entity->hasStoredButton) {
            entity->buttonID        = entity->storedButtonID;
            entity->storedButtonID  = 0;
            entity->hasStoredButton = 0;
        } else if (entity->resetSelection) {
            entity->buttonID = entity->startingID;
            /* decomp non-Plus branch L498-503 — reset startPos/target. */
            entity->position.x  = entity->startPos.x;
            entity->position.y  = entity->startPos.y;
            entity->targetPos.x = entity->startPos.x;
            entity->targetPos.y = entity->startPos.y;
            entity->state       = UIControl_ProcessInputs;
        }
        entity->state = UIControl_ProcessInputs;
    }
}

/* Decomp .c L522-536 — SetInactiveMenu. */
void UIControl_SetInactiveMenu(EntityUIControl *control)
{
    if (!UIControl || !control) return;
    UIControl->hasTouchInput = 0;
    control->active  = ACTIVE_NEVER;
    control->visible = 0;
    control->state   = StateMachine_None;
}

/* Decomp .c L538-598 — SetupButtons. Walks UIHeading + UIShifter +
 * UICarousel + UIButton-class entities inside this control's bounds,
 * builds the buttons[] array, and records heading/shifter pointers.
 *
 * Phase 3.2.c.1 (Task #148) — UIButton class is registered; this
 * function now populates self->buttons[] with the UIButton-class
 * entities inside the control's bbox. UIHeading / UIShifter /
 * UICarousel walks remain stubbed pending 3.2.c.2/3 ports. */
void UIControl_SetupButtons(void)
{
    RSDK_THIS(UIControl);
    if (!self) return;

    int uibutton_cid = rsdk_object_find_class("UIButton");
    if (uibutton_cid < 0) return;

    /* decomp L574-597 — walk the scene-entity slot range looking for
     * UIButton-class entities (and its registered subclasses; on
     * Saturn only base UIButton is ported in 3.2.c.1) inside the
     * control's bounding box. */
    for (uint16_t i = 0; i < RSDK_RESERVE_ENTITY_COUNT + RSDK_SCENEENTITY_COUNT; ++i) {
        if (self->buttonCount >= UICONTROL_BUTTON_COUNT) break;

        rsdk_entity_t *ent = rsdk_entity_at(i);
        if (!ent || ent->class_id == 0) continue;
        if ((int)ent->class_id != uibutton_cid) continue;

        EntityUIButton *button = (EntityUIButton *)ent;
        Vector2 pos = { button->position.x, button->position.y };
        if (!UIControl_ContainsPos(self, &pos)) continue;

        if (!button->parent) button->parent = self;
        self->buttons[self->buttonCount++] = button;
    }
    /* FIXME Phase 3.2.c.2: UIHeading / UIShifter / UICarousel walks
     * (decomp L544-572) — those classes aren't registered yet. */
}

/* Decomp .c L600-615 — isMoving. */
bool32 UIControl_isMoving(EntityUIControl *entity)
{
    if (entity->scrollSpeed.x && entity->scrollSpeed.y) {
        return entity->position.x != entity->targetPos.x ||
               entity->position.y != entity->targetPos.y;
    } else if (entity->scrollSpeed.x) {
        return entity->position.x != entity->targetPos.x;
    } else if (entity->scrollSpeed.y) {
        return entity->position.y != entity->targetPos.y;
    }
    return 0;
}

/* Mini ManiaString ASCII compare. Decomp uses RSDK.CompareStrings on
 * UTF-16-LE buffers; for Phase 3.2.a we ASCII-decode the chars[] (Mania
 * tags are pure ASCII) for the byte-compare. Returns 0 on equal (matches
 * RSDK.CompareStrings semantics). */
static int _string_compare_to_cstr(const ManiaString *s, const char *cstr)
{
    if (!s || !cstr) return -1;
    size_t clen = strlen(cstr);
    if ((size_t)s->length != clen) return 1;
    for (uint16 i = 0; i < s->length; ++i) {
        if ((char)s->chars[i] != cstr[i]) return 1;
    }
    return 0;
}

static int _string_compare(const ManiaString *a, const ManiaString *b)
{
    if (!a || !b) return -1;
    if (a->length != b->length) return 1;
    for (uint16 i = 0; i < a->length; ++i) {
        if (a->chars[i] != b->chars[i]) return 1;
    }
    return 0;
}

/* Decomp .c L617-630 — MatchMenuTag. Activates the UIControl whose tag
 * equals `text`; deactivates all others. */
void UIControl_MatchMenuTag(const char *text)
{
    int cid = rsdk_object_find_class("UIControl");
    if (cid < 0) return;
    int cursor = -1;
    EntityUIControl *entity;
    while ((entity = (EntityUIControl *)rsdk_get_all_entity(
                         (uint16_t)cid, &cursor)) != NULL) {
        if (entity->active == ACTIVE_ALWAYS ||
            _string_compare_to_cstr(&entity->tag, text) != 0) {
            UIControl_SetInactiveMenu(entity);
        } else {
            UIControl_SetActiveMenu(entity);
        }
    }
}

/* Decomp .c L632-643 — HandleMenuChange. */
void UIControl_HandleMenuChange(ManiaString *newMenuTag)
{
    if (!newMenuTag || newMenuTag->length == 0) return;
    int cid = rsdk_object_find_class("UIControl");
    if (cid < 0) return;
    int cursor = -1;
    EntityUIControl *entity;
    while ((entity = (EntityUIControl *)rsdk_get_all_entity(
                         (uint16_t)cid, &cursor)) != NULL) {
        if (entity->active == ACTIVE_ALWAYS ||
            _string_compare(newMenuTag, &entity->tag) != 0) {
            UIControl_SetInactiveMenu(entity);
        } else {
            UIControl_SetActiveMenu(entity);
        }
    }
}

/* Decomp .c L645-654 — HandleMenuLoseFocus. */
void UIControl_HandleMenuLoseFocus(EntityUIControl *parent)
{
    int cid = rsdk_object_find_class("UIControl");
    if (cid < 0) return;
    int cursor = -1;
    EntityUIControl *entity;
    while ((entity = (EntityUIControl *)rsdk_get_all_entity(
                         (uint16_t)cid, &cursor)) != NULL) {
        if (entity->active == ACTIVE_ALWAYS || entity != parent) {
            UIControl_SetInactiveMenu(entity);
        } else {
            UIControl_SetMenuLostFocus(entity);
        }
    }
}

/* Decomp .c L656-662 — ReturnToParentMenu. */
void UIControl_ReturnToParentMenu(void)
{
    EntityUIControl *entity = UIControl_GetUIControl();
    if (!entity) return;
    entity->selectionDisabled = 0;
    UIControl_HandleMenuChange(&entity->parentTag);
}

/* Decomp .c L707-735 — HandlePosition. Interpolates position toward
 * targetPos by scrollSpeed each tick. */
void UIControl_HandlePosition(void)
{
    RSDK_THIS(UIControl);

    if (self->position.x < self->targetPos.x) {
        self->position.x += self->scrollSpeed.x;
        if (self->position.x > self->targetPos.x)
            self->position.x = self->targetPos.x;
    } else if (self->position.x > self->targetPos.x) {
        self->position.x -= self->scrollSpeed.x;
        if (self->position.x < self->targetPos.x)
            self->position.x = self->targetPos.x;
    }

    if (self->position.y < self->targetPos.y) {
        self->position.y += self->scrollSpeed.y;
        if (self->position.y > self->targetPos.y)
            self->position.y = self->targetPos.y;
    } else if (self->position.y > self->targetPos.y) {
        self->position.y -= self->scrollSpeed.y;
        if (self->position.y < self->targetPos.y)
            self->position.y = self->targetPos.y;
    }

    if (self->heading) {
        /* decomp .c L734: heading->position.x = self->position.x. The
         * EntityUIHeading struct is forward-declared in UIControl.h
         * (Phase 3.2.c ports the full struct). Until then we treat the
         * pointer as opaque; the assignment is the FIRST field of the
         * struct (position.x at offset 0 per RSDK_ENTITY layout) so it
         * resolves correctly as soon as Phase 3.2.c links the real
         * EntityUIHeading. For Phase 3.2.a we cast safely. */
        *(int32 *)self->heading = self->position.x;
    }
}

/* Decomp .c L737-823 — ProcessButtonInput. Drives per-button touch +
 * keyboard dispatch.
 *
 * Phase 3.2.c.1 (Task #148) — UIButton is ported. We now dispatch
 * the focused button's processButtonCB. Touch I/O is a Saturn no-op
 * (TouchInfo->count = 0) so the upper touch-handling block from
 * decomp L742-801 short-circuits cleanly. */
void UIControl_ProcessButtonInput(void)
{
    RSDK_THIS(UIControl);
    if (!self || !UIControl) return;

    /* decomp L742-801 — touch path. Saturn TouchInfo->count = 0 so
     * this entire block is skipped (no virtual touch input). */
    UIControl->hasTouchInput = 0;

    /* decomp L803-822 — keyboard / pad path: dispatch the focused
     * button's processButtonCB if it's set. */
    if (self->buttonID >= 0 && self->buttonID < self->buttonCount) {
        EntityUIButton *button = self->buttons[self->buttonID];
        if (button) {
            rsdk_entity_t *prev = g_rsdk_current_entity;
            g_rsdk_current_entity = (rsdk_entity_t *)button;

            if (button->processButtonCB) {
                bool32 skip = 0;
                if (button->checkSelectedCB) {
                    skip = button->checkSelectedCB();
                }
                if (!skip) {
                    button->processButtonCB();
                }
            }
            g_rsdk_current_entity = prev;
        }
    }
}

/* Decomp .c L825-839 — ContainsPos. Point-in-rect test for
 * positioning UIWidgets-class entities into this control's array. */
bool32 UIControl_ContainsPos(EntityUIControl *control, Vector2 *pos)
{
    if (!control || !pos) return 0;
    int32 x = control->startPos.x - control->cameraOffset.x;
    int32 y = control->startPos.y - control->cameraOffset.y;

    /* decomp .c L831-834 — hitbox derived from control->size (in
     * fixed-point; >>17 strips the 16.16 fixed AND halves it). The
     * point-in-hitbox check is: pos in [(x+left, y+top), (x+right,
     * y+bottom)]. */
    int32 left   = -(control->size.x >> 17);
    int32 right  =  (control->size.x >> 17);
    int32 top    = -(control->size.y >> 17);
    int32 bottom =  (control->size.y >> 17);

    /* decomp MathHelpers_PointInHitbox: hitbox edges are added to (x, y)
     * before comparing pos. Hitbox coords are signed pixels, control
     * pos is 16.16 fixed; the >>17 above already produces pixel
     * coords centered on control->position. The decomp uses the
     * fixed-point control position directly in the inclusion test —
     * we mirror that. */
    if (pos->x >= x + (left << 16) && pos->x <= x + (right << 16) &&
        pos->y >= y + (top  << 16) && pos->y <= y + (bottom << 16)) {
        return 1;
    }
    return 0;
}
