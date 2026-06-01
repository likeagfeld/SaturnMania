/* Phase 3.2 — MenuSetup minimum-viable port.
 *
 * Port reference:
 *   tools/_decomp_raw/SonicMania_Objects_Menu_MenuSetup.c
 *
 * Per-line citations for each behavior ported below:
 *   decomp L12-33    MenuSetup_Update           -> MenuSetup_Update
 *   decomp L35       MenuSetup_LateUpdate       -> MenuSetup_LateUpdate
 *   decomp L37-135   MenuSetup_StaticUpdate     -> stubbed (no UIControl tree)
 *   decomp L137-142  MenuSetup_Draw             -> MenuSetup_Draw
 *   decomp L144-151  MenuSetup_Create           -> MenuSetup_Create
 *   decomp L153-208  MenuSetup_StageLoad        -> MenuSetup_StageLoad
 *
 * The decomp's StaticUpdate (L37-135) does three things per frame:
 *   (a) wire `MenuSetup->mainMenu` to the UIControl with the "Main Menu"
 *       tag — the MVP has no UIControl widget tree, so this is skipped.
 *   (b) one-shot call ManiaModeMenu_Initialize() / MenuSetup_Initialize()
 *       — also skipped (those wire UIControl tags into struct fields).
 *   (c) configure the menu-track via Music_PlayTrack — deferred to Phase
 *       3.3 (need MenuParam-driven trackID lookup).
 *
 * The decomp's StageLoad (L153-208) clears globals->noSaveSlot, stops
 * music, logs the SKU + region.  The Saturn MVP only stops music and
 * clears the initialized flag.
 *
 * Saturn-side simplifications per CLAUDE.md Phase 3.2 brief:
 *   * "MANIA MODE" + "PRESS START" rendered via jo_printf (font is the
 *     jo-default 8x8 ASCII NBG0 font seeded at jo_core_init).
 *   * No FxFade entity; no UI widget tree; no API_SetRichPresence.
 *   * STARTtransition target is the Saturn-side title→GHZ pipeline
 *     (mania_menu_should_advance_to_ghz -> Game.c TS_MENU_ACTIVE branch
 *     pivots to TS_TRANSITION_TO_GHZ).
 *
 * STRICT brief constraints honoured:
 *   * No mania_tick early-return gating (Phase 3.1 LogoSetup failure mode).
 *   * Reuses existing rsdk_controller_state() input API.
 *   * No new sprite atlases — text-only menu via jo_printf placeholder.
 *   * Honest hand-off below for the FULL UI port deferred to Phase 3.3+.
 *
 * Honest hand-off (Phase 3.2 deferred):
 *   * Full UIControl tree (52 UI* widget classes) — Phase 3.3+.
 *   * Save Select / Time Attack / Competition / Options sub-menus — same.
 *   * "Mania Mode" tap-pattern, animated diorama, character previews —
 *     same.
 *   * Replacing jo_printf text with the real UIWidgets.bin "PRESS START"
 *     and MainIcons.bin glyphs — Phase 3.3.
 *   * `rsdk_load_scene("Menu")` parses Menu/Scene1.bin's tile-layer
 *     headers fine (storage.c:186 path), but every entity inside hits
 *     class_id < 0 because UIControl + 53 sibling classes are not yet
 *     registered.  scene.c:238 silently skips unresolved entities so
 *     this is a clean no-op load, NOT a corruption -- but it ALSO
 *     means the scene-load path runs but produces zero visible entities.
 *     The MVP uses the Saturn-side `mania_menu_*` direct-tick path
 *     instead, which IS the canonical Saturn-port pattern (mirrors how
 *     ghz_is_active / mania_ghz_tick_and_draw work today).
 */

#include "MenuSetup.h"
#include "../../../rsdk/input.h"

#include <jo/jo.h>                /* jo_audio_stop_cd                       */
#include <sl_def.h>               /* slPrint, slLocate, slCurColor          */
#include <string.h>

/* ASCII NBG0 text-cell write helper.
 *
 * SGL's slPrint writes 8x8 ASCII glyphs into the NBG0 ASCII work area
 * seeded by jo_core_init.  The `void *workarea` argument is the
 * cell-grid pointer; we pass SGL's NBG0 ASCII work address via the
 * `slLocate(x, y)` macro that returns the cell-grid pointer at (x, y).
 *
 * SL_DEF.H:894:  extern void slPrint(char *, void *);
 * SL_DEF.H:887:  extern void slPrintHex(Uint32, void *);
 *
 * The (x, y) coordinates are character cells (0..39 x 0..27).  The
 * Saturn 320x224 viewport maps to 40 cells wide x 28 tall. */
static void menu_print_at(int cell_x, int cell_y, const char *msg)
{
    /* slLocate is a macro on the ASCII work area baseline; the Saturn-side
     * jo init path enables NBG0 ASCII via jo_core_init by default. */
    slPrint((char *)msg, slLocate(cell_x, cell_y));
}

ObjectMenuSetup *MenuSetup = NULL;

/* === Saturn-side direct-tick state =================================== */

/* Menu lifecycle phases (Saturn-side; mirrors the decomp's intent
 * MAIN_STATE_DISPLAY + MAIN_STATE_LOAD_GHZ without spinning up the full
 * UIControl tree).  Phase 3.3 will replace this with the full
 * MenuSetup_State_HandleTransition / fxFade-driven entity state machine
 * from decomp .c:625-660. */
typedef enum {
    MENU_STATE_INIT     = 0,      /* one-tick init: clear globals          */
    MENU_STATE_DISPLAY  = 1,      /* poll START; blink PRESS START text    */
    MENU_STATE_ADVANCE  = 2,      /* user pressed START; flag for caller   */
    MENU_STATE_EXIT     = 3,      /* Phase 3.2b: user pressed B; flag      */
                                  /*   caller to return to TS_WAIT_FOR_   */
                                  /*   ENTER.                              */
} menu_state_t;

static volatile menu_state_t s_menu_state = MENU_STATE_INIT;
static volatile uint32_t     s_menu_timer = 0;       /* 60Hz tick counter  */
static volatile int          s_menu_started = 0;     /* 1 once mania_menu_enter */
static volatile int          s_menu_advance = 0;     /* 1 = caller should TRANSITION_TO_GHZ */
static volatile int          s_menu_exit    = 0;     /* Phase 3.2b: 1 = caller should drop back to TS_WAIT_FOR_ENTER */

/* === RSDK-style class callbacks ====================================== *
 *
 * These are registered via rsdk_object_register_ex in mania_engine_init
 * so the class is present at boot and `rsdk_load_scene("Menu")` resolves
 * the MenuSetup entity in Menu/Scene1.bin to this registered slot.
 * Phase 3.3 will flesh these out with the real fxFade-driven body. */

/* Decomp .c:144-151 — Create. */
void MenuSetup_Create(void *data)
{
    (void)data;
    RSDK_THIS(MenuSetup);
    self->active    = ACTIVE_ALWAYS;
    self->visible   = true;
    self->drawGroup = 14;          /* decomp L150 */
}

/* Decomp .c:12-33 — Update.  MVP drops the fxFade-timer block; the state
 * pointer is wired only when the full Phase 3.3 port lands. */
void MenuSetup_Update(void)
{
    RSDK_THIS(MenuSetup);
    StateMachine_Run(self->state);
}

/* Decomp .c:35. */
void MenuSetup_LateUpdate(void) {}

/* Decomp .c:37-135 — StaticUpdate.  See file header for what's deferred. */
void MenuSetup_StaticUpdate(void) {}

/* Decomp .c:137-142 — Draw.  MVP uses jo_printf overlay (in
 * mania_menu_draw) instead of the FillScreen fade, so the per-entity Draw
 * is a no-op. */
void MenuSetup_Draw(void) {}

/* Decomp .c:153-208 — StageLoad. */
void MenuSetup_StageLoad(void)
{
    if (!MenuSetup) return;
    /* Decomp .c:170-171 (non-Plus branch):
     *   Music_Stop();
     *   Music->activeTrack = TRACK_NONE; */
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    jo_audio_stop_cd();
#endif
    /* Decomp .c:196:
     *   memset(globals->noSaveSlot, 0, 0x400);
     * No equivalent Saturn-side global yet; intentionally skipped. */
    MenuSetup->initialized = 1;
}

/* Decomp-style state callback used when the per-entity machine is
 * exercised (Phase 3.3+).  Not wired in Phase 3.2 MVP. */
void MenuSetup_State_Idle(void) {}

/* === Saturn-side direct-tick implementation ========================== *
 *
 * These functions are the production path for Phase 3.2.  Game.c's
 * title_state_tick TS_MENU_ACTIVE branch calls them once per frame.
 * Rationale: matches how mania_ghz_tick_and_draw + ghz_is_active route
 * GHZ drawing today, which is the only proven Saturn-side pattern for
 * mid-game state changes in this codebase. */

void mania_menu_enter(void)
{
    s_menu_state   = MENU_STATE_DISPLAY;
    s_menu_timer   = 0;
    s_menu_started = 1;
    s_menu_advance = 0;
    s_menu_exit    = 0;
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    /* Decomp .c:170 Music_Stop equivalent — the title BGM (CD-DA track 3)
     * is replaced.  Phase 3.3 will start MainMenu.ogg (Music->trackFile[0])
     * via jo_audio_play_cd_track; MVP just stops the title music. */
    jo_audio_stop_cd();
#endif
}

/* Phase 3.2b (Task #125) — back-out path.  When Game.c's TS_MENU_ACTIVE
 * branch sees mania_menu_should_exit_to_title() return 1, it pivots
 * s_ts_state back to TS_WAIT_FOR_ENTER and calls this to clear menu
 * runtime state cleanly.  The title BGM is restarted so the user gets
 * back the exact pre-menu audio state, matching the visible state
 * (title backdrop now re-animates since the Game.c gate flips back). */
void mania_menu_exit_to_title(void)
{
    s_menu_state   = MENU_STATE_INIT;
    s_menu_timer   = 0;
    s_menu_started = 0;
    s_menu_advance = 0;
    s_menu_exit    = 0;
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    /* Restart title BGM (CD-DA track 3) — mirrors the boot-time
     * jo_audio_play_cd_track call in main.c::jo_main (Phase 1.3 path). */
    jo_audio_play_cd_track(3, 3, true);
#endif
}

void mania_menu_tick(void)
{
    if (!s_menu_started) return;
    ++s_menu_timer;

    switch (s_menu_state) {
        case MENU_STATE_INIT:
            s_menu_state = MENU_STATE_DISPLAY;
            break;

        case MENU_STATE_DISPLAY: {
            /* Phase 3.2 — START press transitions to GHZ.  Reuses the
             * same controller poll as Game.c title_state_tick's
             * TS_WAIT_FOR_ENTER branch (Game.c L1416). */
            const rsdk_controller_state_t *c = rsdk_controller_state(CONT_ANY);
            bool start_press = (c != NULL && c->key_start.press);
            /* Phase 3.2b (Task #125) — B-button back-handler.  Per the
             * user report 2026-05-28: "pressing other buttons doesn't
             * bring me back to title menu to exit back out to title".
             * RSDK convention (decomp UIControl.c:UIControl_Update L228
             * `if (control->backPressCB)` reads input.keyB.press for
             * sub-menu pop / main-menu exit).  Saturn-side we route
             * B-press to a back-exit signal that Game.c consumes to
             * restore TS_WAIT_FOR_ENTER.  Mutually exclusive with the
             * START path; START checked first so simultaneous press
             * advances forward (matches decomp Phase-3.3 UI behaviour
             * where START on the top-level menu confirms instead of
             * popping). */
            bool b_press = (c != NULL && c->key_b.press);
            if (start_press) {
                s_menu_state   = MENU_STATE_ADVANCE;
                s_menu_advance = 1;
            } else if (b_press) {
                s_menu_state = MENU_STATE_EXIT;
                s_menu_exit  = 1;
            }
            break;
        }

        case MENU_STATE_ADVANCE:
            /* Held until Game.c picks up s_menu_advance and pivots state. */
            break;

        case MENU_STATE_EXIT:
            /* Held until Game.c picks up s_menu_exit and pivots state
             * back to TS_WAIT_FOR_ENTER, which calls
             * mania_menu_exit_to_title() to clear s_menu_started. */
            break;
    }
}

void mania_menu_draw(void)
{
    if (!s_menu_started) return;
    /* MVP overlay via SGL's slPrint into the NBG0 ASCII work area
     * (seeded by jo_core_init's slCurColor + slPrint default-font init).
     * SL_DEF.H:894 declares slPrint(char *, void *); we pass slLocate(x,y)
     * for the cell-grid destination.  Saturn 320x224 = 40 cells x 28 tall.
     *
     * Phase 3.3 replaces these slPrint calls with the real UIWidgets.bin
     * glyph rendering (per docs/menu_scene_asset_audit.md UI/UIElements.bin
     * + UI/SmallFont.bin entries).  For the MVP, the primary purpose of
     * these strings is to make the QA capture frame unambiguously
     * different from a title frame so the gate can detect that the menu
     * state was reached. */
    menu_print_at( 1,  2, "MANIA MODE        ");
    menu_print_at( 1,  4, "SAVE SELECT       ");
    menu_print_at( 1,  6, "OPTIONS           ");
    menu_print_at( 1,  8, "EXIT              ");

    /* PRESS START blink — mirror the decomp's title-side blink predicate
     * `!(timer & 0x10)` (matches Game.c L1942 title PRESSSTART blink). */
    if (!(s_menu_timer & 0x10)) {
        menu_print_at( 12, 22, "PRESS START");
    } else {
        menu_print_at( 12, 22, "           ");
    }
}

int mania_menu_should_advance_to_ghz(void)
{
    return s_menu_advance ? 1 : 0;
}

/* Phase 3.2b (Task #125) — back-exit signal accessor.  Game.c's
 * TS_MENU_ACTIVE branch polls this AFTER mania_menu_tick.  Returning 1
 * tells Game.c to pivot s_ts_state back to TS_WAIT_FOR_ENTER and call
 * mania_menu_exit_to_title() to clear runtime state. */
int mania_menu_should_exit_to_title(void)
{
    return s_menu_exit ? 1 : 0;
}

/* Phase 3.2b (Task #125) — public predicate used by main.c's per-frame
 * REPLACE block to decide whether NBG0ON belongs in the slScrAutoDisp
 * mask AND to know whether NBG0 priority should be raised so slPrint
 * pixels actually composite.  Returns 1 while the menu state machine is
 * driving the visible overlay; 0 otherwise.  Read-only accessor for the
 * volatile s_menu_started flag. */
int mania_menu_is_active(void)
{
    return s_menu_started ? 1 : 0;
}
