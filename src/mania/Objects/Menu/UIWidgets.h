#ifndef MANIA_MENU_UIWIDGETS_H
#define MANIA_MENU_UIWIDGETS_H

/* Phase 3.2.b (Task #146) — UIWidgets class header.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UIWidgets.h`
 * (75 LOC). The shared widget service: owns the per-stage font + UI-
 * element atlases, the per-language text atlas (LANGUAGE_EN only on
 * Saturn NTSC-US), the per-menu SFX globals (Bleep/Accept/Warp/Event/
 * Woosh/Fail), and the drawing helpers (DrawRectOutline + DrawPara-
 * llelogram + DrawUpDownArrows + DrawLeftRightArrows + DrawTriJoinRect)
 * that every child widget (UIButton, UISaveSlot, UIShifter, ...)
 * calls.
 *
 * Decomp citation: tools/_decomp_raw/SonicMania_Objects_Menu_UIWidgets.h
 *   L7-35   struct ObjectUIWidgets  (non-Plus 16-entry buttonColors[],
 *                                    timer, frameAnimator, arrowsAnimator,
 *                                    uiFrames/textFrames/fontFrames atlases,
 *                                    sfxBleep/sfxAccept/sfxWarp/sfxEvent/
 *                                    sfxWoosh/sfxFail)
 *   L37-41  struct EntityUIWidgets  (state machine + RSDK_ENTITY only —
 *                                    UIWidgets is a service singleton with
 *                                    no per-entity payload beyond the
 *                                    base entity record)
 *   L46-57  Standard Entity Events + Serialize
 *   L60-72  Extra Entity Functions (ApplyLanguage + draw helpers)
 *
 * Saturn-side decisions:
 *   * MANIA_USE_PLUS == 0 — buttonColors[] is the 16-entry non-Plus
 *     table; saveSelFrames / buttonColor / unusedAnimator1/2 fields
 *     are skipped per the #else branch of decomp UIWidgets.h.
 *   * Only LANGUAGE_EN is loaded for the NTSC-US Saturn build per
 *     docs/MANIA_MODE_PARITY_PLAN.md §9.A. The ApplyLanguage switch
 *     still ports the full set; the Saturn-side asset loader only
 *     bundles UI/TextEN.bin so non-EN branches return -1 silently.
 *   * InkEffects enum mirrors src/rsdk/drawing.h INK_NONE/.../INK_TINT.
 *   * `color` type comes from UIBackground.h (Phase 3.2.a). */

#include "../../Game.h"

/* InkEffects alias — decomp draw signatures use this name; we map to
 * the enum already in src/rsdk/drawing.h. */
#ifndef MANIA_INKEFFECTS_TYPEDEF
#define MANIA_INKEFFECTS_TYPEDEF 1
typedef uint8 InkEffects;
#endif

/* `color` type per decomp RetroEngine.hpp — packed RGB888 quantity. */
#ifndef MANIA_COLOR_TYPEDEF
#define MANIA_COLOR_TYPEDEF 1
typedef uint32 color;
#endif

/* Language enum — decomp UIWidgets_ApplyLanguage uses these. */
#ifndef LANGUAGE_EN
enum {
    LANGUAGE_EN = 0,
    LANGUAGE_FR = 1,
    LANGUAGE_IT = 2,
    LANGUAGE_GE = 3,
    LANGUAGE_SP = 4,
    LANGUAGE_JP = 5,
    LANGUAGE_KO = 6,
    LANGUAGE_SC = 7,
    LANGUAGE_TC = 8
};
#endif

/* Object Class — mirrors decomp .h L7-35 non-Plus branch. */
typedef struct ObjectUIWidgets {
    RSDK_OBJECT
    int32     timer;                  /* L15: non-Plus layout puts timer  */
                                      /*      BEFORE buttonColors[]       */
    int32     buttonColors[16];       /* L16                              */
    Animator  frameAnimator;          /* L18                              */
    Animator  arrowsAnimator;         /* L19                              */
    uint16    uiFrames;               /* L20  UI/UIElements.bin           */
    uint16    textFrames;             /* L24  UI/TextEN.bin (NTSC-US)     */
    uint16    fontFrames;             /* L25  UI/SmallFont.bin            */
    uint16    sfxBleep;               /* L26-31  per-menu SFX globals     */
    uint16    sfxAccept;
    uint16    sfxWarp;
    uint16    sfxEvent;
    uint16    sfxWoosh;
    uint16    sfxFail;
} ObjectUIWidgets;

/* Entity Class — mirrors decomp .h L37-41. The entity carries only the
 * base RSDK_ENTITY plus an unused state machine. UIWidgets is invoked
 * as a service via the ObjectUIWidgets singleton, NOT via per-entity
 * instances (Menu/Scene1.bin doesn't even include a UIWidgets entity
 * slot; the StageLoad callback alone populates the singleton). */
typedef struct EntityUIWidgets {
    RSDK_ENTITY
    StateMachine(state);              /* unused */
} EntityUIWidgets;

extern ObjectUIWidgets *UIWidgets;

/* Standard Entity Events — mirror decomp .h L46-52. */
void UIWidgets_Update(void);
void UIWidgets_LateUpdate(void);
void UIWidgets_StaticUpdate(void);
void UIWidgets_Draw(void);
void UIWidgets_Create(void *data);
void UIWidgets_StageLoad(void);

/* Extra Entity Functions — mirror decomp .h L60-70. */
void UIWidgets_ApplyLanguage(void);
void UIWidgets_DrawRectOutline_Black  (int32 x, int32 y, int32 width, int32 height);
void UIWidgets_DrawRectOutline_Blended(int32 x, int32 y, int32 width, int32 height);
void UIWidgets_DrawRectOutline_Flash  (int32 x, int32 y, int32 width, int32 height);
void UIWidgets_DrawRightTriangle      (int32 x, int32 y, int32 size, int32 red, int32 green, int32 blue);
void UIWidgets_DrawEquilateralTriangle(int32 x, int32 y, int32 size, uint8 sizeMode,
                                       int32 red, int32 green, int32 blue, InkEffects ink);
void UIWidgets_DrawParallelogram      (int32 x, int32 y, int32 width, int32 height,
                                       int32 edgeSize, int32 red, int32 green, int32 blue);
void UIWidgets_DrawUpDownArrows       (int32 x, int32 y, int32 arrowDist);
void UIWidgets_DrawLeftRightArrows    (int32 x, int32 y, int32 arrowDist);
Vector2 UIWidgets_DrawTriJoinRect     (int32 x, int32 y, color leftColor, color rightColor);

#endif /* MANIA_MENU_UIWIDGETS_H */
