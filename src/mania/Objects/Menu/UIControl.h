#ifndef MANIA_MENU_UICONTROL_H
#define MANIA_MENU_UICONTROL_H

/* Phase 3.2.a (Task #145) — UIControl class header.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UIControl.h`
 * (152 LOC). The focus manager + input router that hosts every Menu
 * widget class. Per Menu/Scene1.bin: 27 instances (one per logical sub-menu
 * tag: "Main Menu", "Save Select", "Time Attack", "Competition", ...).
 *
 * Decomp citation: tools/_decomp_raw/SonicMania_Objects_Menu_UIControl.h
 *   L7              UICONTROL_BUTTON_COUNT = 64
 *   L12-44          struct ObjectUIControl  (input mirror + per-player
 *                                            press bits + globals)
 *   L47-105         struct EntityUIControl  (per-control focus / buttons[]
 *                                            / callbacks)
 *   L111-148        Standard entity events + extras
 *
 * Saturn-side decisions:
 *   * PLAYER_COUNT shrunk to 2 (Saturn 2-port baseline). Decomp uses
 *     PLAYER_COUNT (4) — the diff matters only for the per-player press[]
 *     arrays which we size at 2. Multitap support lands in 3.2.i.
 *   * MANIA_USE_PLUS == 0 -- promptCount + carousel + UICarousel +
 *     popoverHasFocus + noClamp/noWrap fields are kept (so the offset
 *     layout matches the decomp 1:1 and per-class scene-bin attribute
 *     fills land at the correct field) but Plus-only logic paths get
 *     stubbed in the .c body. The non-Plus #else branch is what the
 *     Saturn build executes.
 *   * Forward declarations for EntityUIButton / EntityUIHeading /
 *     EntityUIShifter / EntityUICarousel / EntityUIButtonPrompt: those
 *     widget classes land in Phase 3.2.b/c. The UIControl struct holds
 *     pointers to them — that's safe with forward decls since the .c
 *     body's foreach_all() walks resolve those pointers as NULL until
 *     the widget classes register. */

#include "../../Game.h"

/* Decomp .h L6 -- max buttons per control. */
#define UICONTROL_BUTTON_COUNT  64

/* Saturn-side PLAYER_COUNT alias (decomp expects 4, Saturn ships 2). */
#ifndef MANIA_MENU_PLAYER_COUNT
#define MANIA_MENU_PLAYER_COUNT  2
#endif

/* Forward declarations for sibling widget classes ported in 3.2.b/c.
 * Decomp struct UIControl_h L86-92 stores pointers to these. */
typedef struct EntityUIButton EntityUIButton;
typedef struct EntityUIHeading EntityUIHeading;
typedef struct EntityUIShifter EntityUIShifter;
typedef struct EntityUICarousel EntityUICarousel;
typedef struct EntityUIButtonPrompt EntityUIButtonPrompt;

/* RSDK String type — referenced via decomp L51-52 (`String tag`,
 * `String parentTag`). Phase 3.2.a stubs as a minimal pair of fields
 * mirroring the decomp's String layout (`uint16 *chars; uint16 length;
 * uint16 size`). The real RSDK String type lives in
 * tools/_decomp_raw/_RSDKv5_String.cpp; full port lands in 3.2.c when
 * UIWidgets needs it for ApplyLanguage. */
typedef struct {
    uint16 *chars;
    uint16  length;
    uint16  size;
} ManiaString;

/* Object Class -- mirrors decomp .h L12-44 verbatim. The per-player
 * upPress[] / downPress[] etc. arrays use MANIA_MENU_PLAYER_COUNT (2) so
 * we don't bloat BSS. */
typedef struct ObjectUIControl {
    RSDK_OBJECT
    bool32 isProcessingInput;
    bool32 inputLocked;
    bool32 lockInput;
    bool32 upPress     [MANIA_MENU_PLAYER_COUNT];
    bool32 downPress   [MANIA_MENU_PLAYER_COUNT];
    bool32 leftPress   [MANIA_MENU_PLAYER_COUNT];
    bool32 rightPress  [MANIA_MENU_PLAYER_COUNT];
    bool32 backPress   [MANIA_MENU_PLAYER_COUNT];
    bool32 confirmPress[MANIA_MENU_PLAYER_COUNT];
    bool32 yPress      [MANIA_MENU_PLAYER_COUNT];
    bool32 xPress      [MANIA_MENU_PLAYER_COUNT];
    bool32 anyUpPress;
    bool32 anyDownPress;
    bool32 anyLeftPress;
    bool32 anyRightPress;
    bool32 anyConfirmPress;
    bool32 anyBackPress;
    bool32 anyYPress;
    bool32 anyXPress;
    bool32 forceBackPress;
    bool32 hasTouchInput;
    int32  timer;
    int32  unused1;
    uint16 aniFrames;
} ObjectUIControl;

/* Entity Class -- mirrors decomp .h L47-105. */
typedef struct EntityUIControl {
    RSDK_ENTITY
    StateMachine(state);
    int32       unused1;
    int32       buttonID;
    ManiaString tag;
    ManiaString parentTag;
    bool32      activeOnLoad;
    bool32      noWidgets;
    bool32      resetSelection;
    uint8       buttonCount;
    uint8       rowCount;
    uint8       columnCount;
    uint8       startingID;
    Vector2     size;
    Vector2     cameraOffset;
    Vector2     scrollSpeed;
    Vector2     startPos;
    Vector2     targetPos;
    bool32      childHasFocus;
    bool32      dialogHasFocus;
    bool32      hasStoredButton;
    bool32      selectionDisabled;
    int32       backoutTimer;
    int32       storedButtonID;
    int32       lastButtonID;
    EntityUIHeading      *heading;
    EntityUIShifter      *shifter;
    EntityUIButton       *buttons[UICONTROL_BUTTON_COUNT];
    bool32              (*backPressCB)(void);
    void                (*processButtonInputCB)(void);
    void                (*menuSetupCB)(void);
    void                (*menuUpdateCB)(void);
    void                (*yPressCB)(void);
    void                (*xPressCB)(void);
    int32       unused2;
    int32       unused3;
    int32       unused4;
    int32       unused5;
    int32       unused6;
    int32       unused7;
} EntityUIControl;

extern ObjectUIControl *UIControl;

/* Standard Entity Events -- decomp .h L111-116. */
void UIControl_Update(void);
void UIControl_LateUpdate(void);
void UIControl_StaticUpdate(void);
void UIControl_Draw(void);
void UIControl_Create(void *data);
void UIControl_StageLoad(void);

/* Extra Entity Functions -- decomp .h L124-149. */
int32  UIControl_GetButtonID(EntityUIControl *control, EntityUIButton *entity);
void   UIControl_MenuChangeButtonInit(EntityUIControl *control);
void   UIControl_SetActiveMenu(EntityUIControl *entity);
void   UIControl_SetMenuLostFocus(EntityUIControl *entity);
void   UIControl_SetInactiveMenu(EntityUIControl *entity);
void   UIControl_SetupButtons(void);

EntityUIControl *UIControl_GetUIControl(void);
bool32 UIControl_isMoving(EntityUIControl *entity);
void   UIControl_MatchMenuTag(const char *text);
void   UIControl_HandleMenuChange(ManiaString *newMenuTag);
void   UIControl_HandleMenuLoseFocus(EntityUIControl *parent);
void   UIControl_ReturnToParentMenu(void);

void   UIControl_ClearInputs(uint8 buttonID);
void   UIControl_HandlePosition(void);
void   UIControl_ProcessInputs(void);
void   UIControl_ProcessButtonInput(void);
bool32 UIControl_ContainsPos(EntityUIControl *control, Vector2 *pos);

#endif /* MANIA_MENU_UICONTROL_H */
