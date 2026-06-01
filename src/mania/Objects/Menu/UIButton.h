#ifndef MANIA_MENU_UIBUTTON_H
#define MANIA_MENU_UIBUTTON_H

/* Phase 3.2.c.1 (Task #148) — UIButton class header.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UIButton.h`
 * (85 LOC). The generic menu button: parallelogram background +
 * sprite-frame label + state-machine-driven hover/select/disabled
 * animation, hosted by UIControl (decomp UIControl_SetupButtons walk
 * adds every UIButton in-bounds to control->buttons[]).
 *
 * Decomp citation: tools/_decomp_raw/SonicMania_Objects_Menu_UIButton.h
 *   L6-10   UIButtonAlignments enum  (LEFT / CENTER / RIGHT)
 *   L12-15  struct ObjectUIButton    (no fields beyond RSDK_OBJECT)
 *   L17-46  struct EntityUIButton    (MANIA_UI_ITEM_BASE + size + listID +
 *                                     frameID + align + choiceCount +
 *                                     choiceDir + invisible + assignsP1 +
 *                                     freeBindP2 + transition + stopMusic +
 *                                     isDisabled + bgEdgeSize +
 *                                     textBounceOffset + buttonBounceOffset +
 *                                     textBounceVelocity + buttonBounceVelocity +
 *                                     textVisible + clearParentState +
 *                                     firstChoicePos + selection +
 *                                     choiceChangeCB + animator + textFrames +
 *                                     startListID + startFrameID)
 *   L48-82  Standard Entity Events + helpers
 *
 * Saturn-side decisions:
 *   * MANIA_USE_PLUS == 0 — UIWidgets->buttonColor cycling is gated out
 *     (decomp UIWidgets.c L22). UIButton_Draw passes the non-Plus
 *     0xF0F0F0 "white" colour for the highlight pass.
 *   * Touch I/O (UIButton_ProcessTouchCB_*) is functionally stubbed
 *     because Saturn has no touch input; the touchCB pointer is still
 *     populated for offset-compat with UIControl_ProcessButtonInput.
 *   * UIChoice / UIResPicker / UIVsRoundPicker / UIWinSize are
 *     forward-declared opaque pointers (Phase 3.2.c.2/3 ports). UIButton
 *     gates their class-pointer comparisons on NULL — the upstream
 *     decomp does the same. */

#include "../../Game.h"
#include "UIControl.h"

/* Decomp .h L6-10 — alignment enum. */
typedef enum {
    UIBUTTON_ALIGN_LEFT,
    UIBUTTON_ALIGN_CENTER,
    UIBUTTON_ALIGN_RIGHT
} UIButtonAlignments;

/* MANIA_UI_ITEM_BASE expanded — decomp GameVariables.h L326-350.
 *
 * Mirrored verbatim so EntityUIButton field offsets match decomp
 * 1:1. RSDK_ENTITY contributes the base 132 B (Phase 1.17 stride).
 * The base-pad (Game.h _rsdk_entity_pad[60]) covers offsets +72..+131
 * so the FIRST MANIA_UI_ITEM_BASE field (`state`) lands at +132.
 *
 * NOTE: ManiaString tag/parentTag from UIControl.h are not used here.
 * Forward-declare opaque Entity pointer alias so the `parent` field
 * compiles before EntityUIControl is fully defined. */
typedef struct EntityUIControl EntityUIControl;
typedef struct EntityUIButton  EntityUIButton;
typedef struct EntityUIChoice  EntityUIChoice;

#define MANIA_UI_ITEM_BASE_FIELDS                                       \
    RSDK_ENTITY                                                         \
    StateMachine(state);                                                \
    void   (*processButtonCB)(void);                                    \
    bool32 (*touchCB)(void);                                            \
    void   (*actionCB)(void);                                           \
    void   (*selectedCB)(void);                                         \
    void   (*failCB)(void);                                             \
    void   (*buttonEnterCB)(void);                                      \
    void   (*buttonLeaveCB)(void);                                      \
    bool32 (*checkButtonEnterCB)(void);                                 \
    bool32 (*checkSelectedCB)(void);                                    \
    int32   timer;                                                      \
    Vector2 startPos;                                                   \
    void   *parent;                                                     \
    Vector2 touchPosSizeS;                                              \
    Vector2 touchPosOffsetS;                                            \
    bool32  touchPressed;                                               \
    Vector2 touchPosSizeM[4];                                           \
    Vector2 touchPosOffsetM[4];                                         \
    void  (*touchPosCallbacks[4])(void);                                \
    int32   touchPosCount;                                              \
    int32   touchPosID;                                                 \
    bool32  isSelected;                                                 \
    bool32  disabled;

typedef struct ObjectUIButton {
    RSDK_OBJECT
} ObjectUIButton;

struct EntityUIButton {
    MANIA_UI_ITEM_BASE_FIELDS

    /* Decomp .h L19-46 — per-button payload. Field order MUST match
     * decomp so Scene1.bin attribute-deserialization (Serialize order
     * is L953-967) writes into the correct slots. */
    Vector2 size;
    int32   listID;
    int32   frameID;
    int32   align;
    int32   choiceCount;
    uint8   choiceDir;
    bool32  invisible;
    bool32  assignsP1;
    bool32  freeBindP2;
    bool32  transition;
    bool32  stopMusic;
    bool32  isDisabled;
    int32   bgEdgeSize;
    int32   textBounceOffset;
    int32   buttonBounceOffset;
    int32   textBounceVelocity;
    int32   buttonBounceVelocity;
    bool32  textVisible;
    bool32  clearParentState;
    Vector2 firstChoicePos;
    int32   selection;
    void  (*choiceChangeCB)(void);
    Animator animator;
    uint16  textFrames;
    int32   startListID;
    int32   startFrameID;
};

extern ObjectUIButton *UIButton;

/* Standard Entity Events — mirror decomp .h L52-57. */
void UIButton_Update(void);
void UIButton_LateUpdate(void);
void UIButton_StaticUpdate(void);
void UIButton_Draw(void);
void UIButton_Create(void *data);
void UIButton_StageLoad(void);

/* Extra Entity Functions — mirror decomp .h L65-82. */
void   UIButton_ManageChoices(EntityUIButton *button);
EntityUIButton *UIButton_GetChoicePtr(EntityUIButton *button, int32 selection);
void   UIButton_SetChoiceSelectionWithCB(EntityUIButton *button, int32 selection);
void   UIButton_SetChoiceSelection(EntityUIButton *button, int32 selection);
void  *UIButton_GetActionCB(void);
void   UIButton_FailCB(void);
void   UIButton_ProcessButtonCB_Scroll(void);
bool32 UIButton_ProcessTouchCB_Multi(void);
bool32 UIButton_ProcessTouchCB_Single(void);
void   UIButton_ProcessButtonCB(void);
bool32 UIButton_CheckButtonEnterCB(void);
bool32 UIButton_CheckSelectedCB(void);
void   UIButton_ButtonEnterCB(void);
void   UIButton_ButtonLeaveCB(void);
void   UIButton_SelectedCB(void);
void   UIButton_State_HandleButtonLeave(void);
void   UIButton_State_HandleButtonEnter(void);
void   UIButton_State_Selected(void);

#endif /* MANIA_MENU_UIBUTTON_H */
