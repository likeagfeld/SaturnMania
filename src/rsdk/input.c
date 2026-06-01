/* Phase A7 — Input system, Saturn port of RSDKv5/RSDK/Input.
 *
 * Thin wrapper over jo's pad polling (`jo_is_pad1_key_pressed` /
 * `jo_is_pad2_key_pressed`). The RSDK-shaped press/down state machine
 * (§7.4) is computed each tick by diffing the current sample against
 * the prior tick's `down` flag. */

#include "input.h"

#include <jo/jo.h>
#include <string.h>

rsdk_controller_state_t g_rsdk_controllers[RSDK_PLAYER_COUNT + 1];

/* Phase 3.2.a (Task #145) — analog-stick dead-zone matching decomp
 * UIControl_Create L102-105 (AnalogStickInfoL[1..4].deadzone = 0.75f).
 * The Saturn 3D Analog Control Pad reports axes as unsigned 0..255
 * with rest at 128; we convert to signed (-128..+127) and OR a
 * dpad-like press when the deflection exceeds the deadzone. */
#define INPUT_ANALOG_DEADZONE  64   /* ~0.50 of full deflection; UIControl */
                                    /* decomp uses 0.75f for AnalogStickL  */
                                    /* but Saturn 3D pad reports rest=128  */
                                    /* with full deflection at ~95..160 in */
                                    /* practice; 0.50 is conservative. */

/* Per-frame raw sample helpers. jo's API is per-key boolean queries;
 * we collect them into a packed bitfield once per tick so the state-
 * machine code below is generic across players. */
static uint16_t _sample_pad(int port, int8_t *out_stick_x,
                            int8_t *out_stick_y, uint8_t *out_trig_l,
                            uint8_t *out_trig_r, uint8_t *out_has_analog)
{
    uint16_t b = 0;
    *out_stick_x = 0;
    *out_stick_y = 0;
    *out_trig_l = 0;
    *out_trig_r = 0;
    *out_has_analog = 0;

    /* Port 0 = P1, port 1 = P2 — both go through the generic
     * jo_is_input_* surface so 3-button, 6-button, and 3D Analog Control
     * Pads all decode the same JO_KEY_* bits. The 3D pad's analog stick
     * + analog triggers are exposed via jo_get_input_axis(). */
    if (port == 1 && !jo_is_input_available(1)) {
        return 0;
    }
    if (jo_is_input_key_pressed(port, JO_KEY_UP))     b |= KEYMASK_UP;
    if (jo_is_input_key_pressed(port, JO_KEY_DOWN))   b |= KEYMASK_DOWN;
    if (jo_is_input_key_pressed(port, JO_KEY_LEFT))   b |= KEYMASK_LEFT;
    if (jo_is_input_key_pressed(port, JO_KEY_RIGHT))  b |= KEYMASK_RIGHT;
    if (jo_is_input_key_pressed(port, JO_KEY_A))      b |= KEYMASK_A;
    if (jo_is_input_key_pressed(port, JO_KEY_B))      b |= KEYMASK_B;
    if (jo_is_input_key_pressed(port, JO_KEY_C))      b |= KEYMASK_C;
    if (jo_is_input_key_pressed(port, JO_KEY_X))      b |= KEYMASK_X;
    if (jo_is_input_key_pressed(port, JO_KEY_Y))      b |= KEYMASK_Y;
    if (jo_is_input_key_pressed(port, JO_KEY_Z))      b |= KEYMASK_Z;
    if (jo_is_input_key_pressed(port, JO_KEY_START))  b |= KEYMASK_START;
    /* Phase 3.2.a — 6-button + 3D pad shoulder triggers (digital).
     * jo's JO_KEY_L = (1<<15), JO_KEY_R = (1<<3); 3-button pad reports
     * both as not-pressed so 3-button menu code that ignores keyL/keyR
     * still works. */
    if (jo_is_input_key_pressed(port, JO_KEY_L))      b |= KEYMASK_L;
    if (jo_is_input_key_pressed(port, JO_KEY_R))      b |= KEYMASK_R;

    /* Phase 3.2.a — 3D Analog Control Pad axes. jo_is_input_analog()
     * returns true only for PER_ID_NightsPad (the 3D pad / Twin Stick).
     * Axis1/2 = stick X/Y; Axis3 = right trigger; Axis4 = left trigger
     * per jo/input.h L132-135 docs. Raw axis values are 0..255 with
     * rest=128 for stick (signed semantics) and rest=0 for triggers. */
    if (jo_is_input_analog(port)) {
        *out_has_analog = 1;
        int ax = (int)jo_get_input_axis(port, JoAxis1) - 128;
        int ay = (int)jo_get_input_axis(port, JoAxis2) - 128;
        if (ax < -128) ax = -128;
        if (ax >  127) ax =  127;
        if (ay < -128) ay = -128;
        if (ay >  127) ay =  127;
        *out_stick_x = (int8_t)ax;
        *out_stick_y = (int8_t)ay;
        *out_trig_r  = jo_get_input_axis(port, JoAxis3);
        *out_trig_l  = jo_get_input_axis(port, JoAxis4);

        /* Decomp UIControl_ProcessInputs L195-198 ORs AnalogStickInfoL
         * deflections into the dpad press bits. Mirror that here so
         * menu navigation on the 3D pad's stick "just works" via the
         * same key_up/.../key_right .press fields. */
        if (ay < -INPUT_ANALOG_DEADZONE) b |= KEYMASK_UP;
        if (ay >  INPUT_ANALOG_DEADZONE) b |= KEYMASK_DOWN;
        if (ax < -INPUT_ANALOG_DEADZONE) b |= KEYMASK_LEFT;
        if (ax >  INPUT_ANALOG_DEADZONE) b |= KEYMASK_RIGHT;
    }
    return b;
}

/* §7.4 state-machine evaluator — given the new raw-down bit and the
 * previous down flag, compute the new (press, down) pair. */
static void _step(rsdk_input_state_t *st, bool down_now)
{
    bool was_down = (st->down != 0);
    st->down  = down_now ? 1 : 0;
    st->press = (down_now && !was_down) ? 1 : 0;
}

void rsdk_input_init(void)
{
    memset(g_rsdk_controllers, 0, sizeof(g_rsdk_controllers));
}

void rsdk_input_tick(void)
{
    int8_t sx1 = 0, sy1 = 0, sx2 = 0, sy2 = 0;
    uint8_t tl1 = 0, tr1 = 0, tl2 = 0, tr2 = 0;
    uint8_t ha1 = 0, ha2 = 0;
    uint16_t p1 = _sample_pad(0, &sx1, &sy1, &tl1, &tr1, &ha1);
    uint16_t p2 = _sample_pad(1, &sx2, &sy2, &tl2, &tr2, &ha2);

    /* CONT_ANY is the OR of all controllers — used by code that doesn't
     * care which player pressed the button. */
    uint16_t any = p1 | p2;

#define APPLY(slot_idx, raw, sx, sy, tl, tr, ha) do {                          \
        rsdk_controller_state_t *c = &g_rsdk_controllers[(slot_idx)];          \
        _step(&c->key_up,     ((raw) & KEYMASK_UP    ) != 0);                  \
        _step(&c->key_down,   ((raw) & KEYMASK_DOWN  ) != 0);                  \
        _step(&c->key_left,   ((raw) & KEYMASK_LEFT  ) != 0);                  \
        _step(&c->key_right,  ((raw) & KEYMASK_RIGHT ) != 0);                  \
        _step(&c->key_a,      ((raw) & KEYMASK_A     ) != 0);                  \
        _step(&c->key_b,      ((raw) & KEYMASK_B     ) != 0);                  \
        _step(&c->key_c,      ((raw) & KEYMASK_C     ) != 0);                  \
        _step(&c->key_x,      ((raw) & KEYMASK_X     ) != 0);                  \
        _step(&c->key_y,      ((raw) & KEYMASK_Y     ) != 0);                  \
        _step(&c->key_z,      ((raw) & KEYMASK_Z     ) != 0);                  \
        _step(&c->key_start,  ((raw) & KEYMASK_START ) != 0);                  \
        _step(&c->key_select, ((raw) & KEYMASK_SELECT) != 0);                  \
        _step(&c->key_l,      ((raw) & KEYMASK_L     ) != 0);                  \
        _step(&c->key_r,      ((raw) & KEYMASK_R     ) != 0);                  \
        c->analog_stick_x = (sx);                                              \
        c->analog_stick_y = (sy);                                              \
        c->analog_trigger_l = (tl);                                            \
        c->analog_trigger_r = (tr);                                            \
        c->has_analog = (ha);                                                  \
    } while (0)

    /* For CONT_ANY: keep the OR semantics on buttons, propagate the
     * stronger-magnitude analog axis (whichever pad is producing
     * stick deflection) so any-controller menu code sees a consistent
     * stick reading. */
    int8_t any_sx = (ha1 ? sx1 : 0);
    int8_t any_sy = (ha1 ? sy1 : 0);
    if (ha2) {
        if (!ha1 || (int)(sx2 < 0 ? -sx2 : sx2) > (int)(any_sx < 0 ? -any_sx : any_sx)) any_sx = sx2;
        if (!ha1 || (int)(sy2 < 0 ? -sy2 : sy2) > (int)(any_sy < 0 ? -any_sy : any_sy)) any_sy = sy2;
    }
    uint8_t any_tl = (tl1 > tl2) ? tl1 : tl2;
    uint8_t any_tr = (tr1 > tr2) ? tr1 : tr2;
    uint8_t any_ha = (uint8_t)(ha1 || ha2);

    APPLY(CONT_ANY, any, any_sx, any_sy, any_tl, any_tr, any_ha);
    APPLY(CONT_P1,  p1,  sx1, sy1, tl1, tr1, ha1);
    APPLY(CONT_P2,  p2,  sx2, sy2, tl2, tr2, ha2);

#undef APPLY
}

const rsdk_controller_state_t *rsdk_controller_state(int slot)
{
    if (slot < 0 || slot > RSDK_PLAYER_COUNT) return NULL;
    return &g_rsdk_controllers[slot];
}
