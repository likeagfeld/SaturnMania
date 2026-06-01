#ifndef MANIA_MENU_UIBUTTONPROMPT_H
#define MANIA_MENU_UIBUTTONPROMPT_H

/* Phase 3.2.c.1 (Task #148) — UIButtonPrompt class header.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UIButtonPrompt.h`
 * (101 LOC). The controller-button-glyph prompt: shows the glyph for
 * a specific controller button beside a text label ("A: confirm",
 * "B: back"). Per-pad-type glyph mapping makes this NTSC-Saturn-shipped
 * with the SATURN_WHITE / SATURN_BLACK glyph set.
 *
 * Decomp citation: tools/_decomp_raw/SonicMania_Objects_Menu_UIButtonPrompt.h
 *   L6-20   UIButtonPromptTypes enum (KEYBOARD/XBOX/PS4/SWITCH/
 *                                     SATURN_BLACK/SATURN_WHITE/...)
 *   L22-28  UIButtonPromptAnchors enum
 *   L30-37  UIButtonPromptButtons enum (A/B/X/Y/Start/Select)
 *   L40-45  struct ObjectUIButtonPrompt
 *   L48-72  struct EntityUIButtonPrompt
 *   L77-98  Standard events + helpers
 *
 * Saturn-side pad-type adaptation:
 *   * 3-button pad / 6-button pad / 3D Analog Pad all use the SATURN_WHITE
 *     glyph set (UIBUTTONPROMPT_SATURN_WHITE = 6 per decomp enum).
 *     The decomp's keyboard-localised glyph variants (KEYBOARD_FR/IT/GE/SP)
 *     are LANGUAGE_EN-only on Saturn NTSC-US so they collapse to
 *     UIBUTTONPROMPT_SATURN_WHITE.
 *   * Pad-type is detected via rsdk_controller_state()->has_analog so
 *     the 3D Analog Pad (NIGHTS pad) can render its variant glyph
 *     when Phase 3.2.k extends the glyph atlas with analog-specific
 *     frames. For Phase 3.2.c.1 all Saturn pad types yield SATURN_WHITE.
 *
 * Phase 3.2.c.1 scope: registered + StageLoad loads UI/Buttons.bin;
 * Update / Draw / Create wire the per-frame glyph rotation. */

#include "../../Game.h"
#include "UIControl.h"

typedef enum {
    UIBUTTONPROMPT_NONE,
    UIBUTTONPROMPT_KEYBOARD,
    UIBUTTONPROMPT_XBOX,
    UIBUTTONPROMPT_PS4,
    UIBUTTONPROMPT_SWITCH,
    UIBUTTONPROMPT_SATURN_BLACK,
    UIBUTTONPROMPT_SATURN_WHITE,
    UIBUTTONPROMPT_JOYCON_L,
    UIBUTTONPROMPT_JOYCON_R,
    UIBUTTONPROMPT_KEYBOARD_FR,
    UIBUTTONPROMPT_KEYBOARD_IT,
    UIBUTTONPROMPT_KEYBOARD_GE,
    UIBUTTONPROMPT_KEYBOARD_SP
} UIButtonPromptTypes;

typedef enum {
    UIBUTTONPROMPT_ANCHOR_NONE,
    UIBUTTONPROMPT_ANCHOR_TOPLEFT_ROW1,
    UIBUTTONPROMPT_ANCHOR_TOPRIGHT_ROW1,
    UIBUTTONPROMPT_ANCHOR_TOPLEFT_ROW2,
    UIBUTTONPROMPT_ANCHOR_TOPRIGHT_ROW2
} UIButtonPromptAnchors;

typedef enum {
    UIBUTTONPROMPT_BUTTON_A,
    UIBUTTONPROMPT_BUTTON_B,
    UIBUTTONPROMPT_BUTTON_X,
    UIBUTTONPROMPT_BUTTON_Y,
    UIBUTTONPROMPT_BUTTON_START,
    UIBUTTONPROMPT_BUTTON_SELECT
} UIButtonPromptButtons;

typedef struct ObjectUIButtonPrompt {
    RSDK_OBJECT
    int32  type;        /* current pad-type (SATURN_WHITE on Saturn NTSC) */
    int32  inputSlot;   /* CONT_P1 on Saturn baseline                     */
    uint16 aniFrames;   /* UI/Buttons.bin atlas                           */
} ObjectUIButtonPrompt;

typedef struct EntityUIButtonPrompt {
    RSDK_ENTITY
    void   *parent;
    StateMachine(state);
    int32   timer;
    Vector2 startPos;
    int32   promptID;
    int32   buttonID;
    uint8   headingAnchor;
    int32   unused;
    int32   prevPrompt;
    int32   prevButton;
    int32   mappings;
    bool32  textVisible;
    int32   scaleMax;
    int32   scaleSpeed;
    bool32  disableScale;
    Vector2 touchSize;
    Vector2 touchPos;
    bool32  touched;
    Animator decorAnimator;
    Animator buttonAnimator;
    Animator promptAnimator;
    uint16  textSprite;
} EntityUIButtonPrompt;

extern ObjectUIButtonPrompt *UIButtonPrompt;

void UIButtonPrompt_Update(void);
void UIButtonPrompt_LateUpdate(void);
void UIButtonPrompt_StaticUpdate(void);
void UIButtonPrompt_Draw(void);
void UIButtonPrompt_Create(void *data);
void UIButtonPrompt_StageLoad(void);

int32 UIButtonPrompt_GetButtonMappings(int32 input, int32 button);
int32 UIButtonPrompt_GetGamepadType(void);
uint8 UIButtonPrompt_MappingsToFrame(int32 mappings);
bool32 UIButtonPrompt_CheckTouch(void);
void UIButtonPrompt_SetButtonSprites(void);
void UIButtonPrompt_State_CheckIfSelected(void);
void UIButtonPrompt_State_Selected(void);

#endif /* MANIA_MENU_UIBUTTONPROMPT_H */
