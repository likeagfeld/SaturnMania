#ifndef RSDK_INPUT_H
#define RSDK_INPUT_H

/* Phase A7 — Input system, Saturn port of RSDKv5/RSDK/Input.
 *
 * Per docs/rsdkv5_engine_catalog.md §7 + BIBLE.md Phase A row A7.
 *
 * Authoritative source (cite by §section when touching this code):
 *   §7.1 Constants     (Input.hpp:12-26)
 *   §7.2 KeyMasks      (Input.hpp:99-116)
 *   §7.3 ControllerState (Input.hpp:150-171)
 *   §7.4 Button state machine (paraphrased)
 *
 * Saturn-port deviations from upstream:
 *   * PLAYER_COUNT reduced from 4 to 2 (Saturn has 2 native pad ports;
 *     6-Player Adaptor + Pro Action Replay add more but we ship 2P).
 *   * X/Y/Z + shoulder triggers map to Saturn 3D Control Pad buttons
 *     when present; default 6-button pad leaves them unmapped (down=0).
 *   * STICKL/STICKR + analog deadzone unsupported (digital pad only). */

#include <stdint.h>
#include <stdbool.h>

#define RSDK_PLAYER_COUNT  2

/* Slot enum — CONT_ANY is the "merged any controller" view used by
 * code that doesn't care which pad pressed which button. */
enum {
    CONT_ANY = 0,
    CONT_P1  = 1,
    CONT_P2  = 2
};

/* KeyMasks per §7.2 */
enum {
    KEYMASK_UP       = 1u << 0,
    KEYMASK_DOWN     = 1u << 1,
    KEYMASK_LEFT     = 1u << 2,
    KEYMASK_RIGHT    = 1u << 3,
    KEYMASK_A        = 1u << 4,
    KEYMASK_B        = 1u << 5,
    KEYMASK_C        = 1u << 6,
    KEYMASK_X        = 1u << 7,
    KEYMASK_Y        = 1u << 8,
    KEYMASK_Z        = 1u << 9,
    KEYMASK_START    = 1u << 10,
    KEYMASK_SELECT   = 1u << 11,
    /* Phase 3.2.a (Task #145) — 6-button + 3D Analog Control Pad shoulder
     * triggers. Saturn 6-button pad and 3D Analog Pad both expose
     * JO_KEY_L (1<<15) and JO_KEY_R (1<<3); we publish them via the
     * RSDK ControllerState surface so the menu UI's keyL/keyR (decomp
     * UIControl.c L210-214 keyY/keyX-style fields, and UIButton menu
     * navigation tab-switching) resolves correctly across pad types. */
    KEYMASK_L        = 1u << 12,
    KEYMASK_R        = 1u << 13
};

/* InputState per §7.3 — edge + level, packed as two flags. */
typedef struct {
    uint8_t down;     /* 1 while button is held                */
    uint8_t press;    /* 1 on the frame the button transitions */
                      /* from up -> down. Edge-triggered.      */
} rsdk_input_state_t;

/* ControllerState — full button set + 3D-Analog stick state.
 *
 * Decomp field-name mapping (per RSDKv5 RSDK/Input/Input.hpp:150-171):
 *   keyUp/keyDown/keyLeft/keyRight  -> Saturn d-pad
 *   keyA/keyB/keyC                  -> Saturn A/B/C (3-button + 6-button)
 *   keyX/keyY/keyZ                  -> Saturn X/Y/Z (6-button + 3D pad)
 *   keyStart                        -> Saturn Start
 *   keySelect                       -> not native to Saturn; UIControl
 *                                       (decomp L169) gates this behind
 *                                       MANIA_USE_PLUS so left=0 on
 *                                       Saturn shim is correct
 *
 * 3D Analog Control Pad extensions (Phase 3.2.a):
 *   analog_stick_x, analog_stick_y  -> raw -128..+127 signed (centered 0)
 *   analog_trigger_l, analog_trigger_r -> raw 0..255 (rest 0)
 *   has_analog                      -> 1 if pad-port is JoNightsPad
 *
 * Decomp `AnalogStickInfoL[CONT_P1+i].keyUp.press` etc. (UIControl.c L195)
 * are derived from the analog stick: above a deadzone the stick is
 * OR'd into the dpad. The shim implements that derivation, so UI code
 * that reads `state.key_up.press` automatically inherits stick input. */
typedef struct {
    rsdk_input_state_t key_up;
    rsdk_input_state_t key_down;
    rsdk_input_state_t key_left;
    rsdk_input_state_t key_right;
    rsdk_input_state_t key_a;
    rsdk_input_state_t key_b;
    rsdk_input_state_t key_c;
    rsdk_input_state_t key_x;
    rsdk_input_state_t key_y;
    rsdk_input_state_t key_z;
    rsdk_input_state_t key_start;
    rsdk_input_state_t key_select;
    /* Phase 3.2.a — shoulder triggers (6-button + 3D Analog pads). */
    rsdk_input_state_t key_l;
    rsdk_input_state_t key_r;
    /* Phase 3.2.a — 3D Analog Control Pad stick + analog triggers.
     * Raw axis values from jo_get_input_axis() (0-255 unsigned for
     * triggers, mapped to signed -128..+127 for stick); has_analog is
     * 1 if the pad reports JoNightsPad. */
    int8_t   analog_stick_x;
    int8_t   analog_stick_y;
    uint8_t  analog_trigger_l;
    uint8_t  analog_trigger_r;
    uint8_t  has_analog;
    uint8_t  _pad;
} rsdk_controller_state_t;

/* Global tables — accessed via the rsdk_controller_state() accessor. */
extern rsdk_controller_state_t g_rsdk_controllers[RSDK_PLAYER_COUNT + 1];   /* [0] = CONT_ANY */

/* === Public API ===================================================== */

/* Initialise input subsystem. Idempotent. */
void rsdk_input_init(void);

/* Sample all controllers + advance the press/down state machine per
 * §7.4. Call once per frame BEFORE any update logic that reads input. */
void rsdk_input_tick(void);

/* Convenience accessor for a slot's controller state. Returns a pointer
 * into g_rsdk_controllers[]; slot must be CONT_ANY/P1/P2. */
const rsdk_controller_state_t *rsdk_controller_state(int slot);

#endif /* RSDK_INPUT_H */
