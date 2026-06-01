#ifndef MANIA_MENU_MENUSETUP_H
#define MANIA_MENU_MENUSETUP_H

/* Phase 3.2 — MenuSetup class header (minimum-viable port).
 *
 * Mirrors the decomp `tools/_decomp_raw/SonicMania_Objects_Menu_MenuSetup.h`
 * struct shape:
 *
 *   struct ObjectMenuSetup       — decomp .h L17-68
 *   struct EntityMenuSetup       — decomp .h L70-80
 *     RSDK_ENTITY
 *     StateMachine(state)
 *     StateMachine(callback)
 *     int32 timer
 *     int32 delay
 *     int32 fadeShift
 *     int32 fadeTimer
 *     int32 fadeColor
 *
 * The Plus-only mainMenu / saveSelect / etc. UIControl* pointers (decomp
 * lines 28-66) are NOT ported.  The MVP path does not instantiate any
 * UIControl widget tree — see CLAUDE.md Phase 3.2 brief constraint set
 * for the rationale.
 *
 * Saturn-side simplifications (mirror MVP scope from the brief):
 *   * Only one state callback wired:  MenuSetup_State_Idle (renders the
 *     menu UI text + polls START).
 *   * Only one external trigger:      MenuSetup_StartTransitionToGHZ()
 *     (called from `title_state_tick` when the user presses START).
 *
 * Larger ports (UI button tree, save-select sub-menu, Options) are
 * Phase 3.3+. */

#include "../../Game.h"

typedef struct ObjectMenuSetup {
    RSDK_OBJECT
    /* Phase 3.2 MVP — no per-class globals are required.  Reserved fields
     * here mirror the decomp .h offset layout so adding fields in Phase
     * 3.3 doesn't shift entity offsets. */
    int32 initialized;
    int32 reserved1;
    int32 reserved2;
    int32 reserved3;
} ObjectMenuSetup;

typedef struct EntityMenuSetup {
    RSDK_ENTITY
    StateMachine(state);
    StateMachine(callback);
    int32 timer;
    int32 delay;
    int32 fadeShift;
    int32 fadeTimer;
    int32 fadeColor;
} EntityMenuSetup;

extern ObjectMenuSetup *MenuSetup;

/* Standard Entity Events — mirror decomp .h L86-91. */
void MenuSetup_Update(void);
void MenuSetup_LateUpdate(void);
void MenuSetup_StaticUpdate(void);
void MenuSetup_Draw(void);
void MenuSetup_Create(void *data);
void MenuSetup_StageLoad(void);

/* MVP state callbacks (Saturn-side; minimal replacements for the decomp's
 * fade-handling state machine). */
void MenuSetup_State_Idle(void);

/* === Saturn-side direct-tick interface ===============================
 *
 * The brief constrains the MVP NOT to use mania_tick early-return gating
 * (Phase 3.1 LogoSetup pattern failed that way).  Instead, the Game.c
 * `title_state_tick` machine adds a TS_MENU_ACTIVE branch that calls
 * these two functions directly — mirroring how `ghz_is_active()` →
 * `mania_ghz_tick_and_draw()` is wired today.
 *
 * Lifecycle:
 *   mania_menu_enter()      one-shot init when transitioning into menu
 *   mania_menu_tick()       per-frame; polls START + drives state timer
 *   mania_menu_draw()       per-frame; renders text overlay
 *   mania_menu_should_advance_to_ghz()
 *                           returns true ONCE the user has pressed START
 *                           inside the menu — Game.c then drops back into
 *                           TS_TRANSITION_TO_GHZ. */
void mania_menu_enter(void);
void mania_menu_tick(void);
void mania_menu_draw(void);
int  mania_menu_should_advance_to_ghz(void);

/* Phase 3.2b (Task #125) — back-exit + active-predicate accessors.
 *
 * mania_menu_should_exit_to_title()
 *   Returns 1 once the user pressed B inside MENU_STATE_DISPLAY.  Game.c
 *   polls this every frame in the TS_MENU_ACTIVE branch (mirrors the
 *   should_advance_to_ghz poll) and pivots s_ts_state back to
 *   TS_WAIT_FOR_ENTER on a positive result.
 *
 * mania_menu_exit_to_title()
 *   Resets menu runtime state (clears s_menu_started, restarts title
 *   BGM).  Called by Game.c immediately after pivoting state, so the
 *   next frame draws the title scene with the backdrop re-animating
 *   (Game.c gating block flips back since s_ts_state != TS_MENU_ACTIVE).
 *
 * mania_menu_is_active()
 *   Read-only public predicate so main.c's per-frame REPLACE block can
 *   include NBG0ON in slScrAutoDisp + raise slPriorityNbg0 while the
 *   menu overlay is the visible state.  Keeps the title-state coupling
 *   one-way: main.c sees "is the menu active?" without including
 *   Game.c's internal TS_MENU_ACTIVE enum. */
int  mania_menu_should_exit_to_title(void);
void mania_menu_exit_to_title(void);
int  mania_menu_is_active(void);

#endif /* MANIA_MENU_MENUSETUP_H */
