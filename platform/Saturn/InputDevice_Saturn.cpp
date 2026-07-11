// =============================================================================
// InputDevice_Saturn.cpp -- P6.7 W7 (Task #227): the Saturn SMPC pad device
// backend for the VERBATIM engine input core (RSDK/Input/Input.cpp), the
// AudioDevice_Saturn precedent applied to input.
//
// The engine side stays untouched: Input.cpp's controller[]/stickL[]/
// touchInfo arrays + ProcessInput edge logic are the verbatim TU (census
// 2026-06-12: compiles clean at the pack knobs, text 1,332 B / bss 2,064 B,
// undefs = videoSettings + SKU::userCore only). This TU is the platform
// device that fills the device-state side, mirroring the keyboard device
// shape line-for-line:
//
//   registration   = InitKeyboardDevice, KBInputDevice.cpp:681-708
//   UpdateInput    = KBInputDevice.cpp:739-770 (edge detect -> anyPress ->
//                    per-button states -> this->ProcessInput(CONT_ANY))
//   ProcessInput   = KBInputDevice.cpp:773-789 (press |= state)
//   API init       = InitKeyboardInputAPI, KBInputDevice.cpp:791-806
//                    (GenerateHashCRC device id -- Text.cpp:205, a real
//                    pack TU via Storage_Text.o)
//
// Pad source: SGL's Smpc_Peripheral buffer (SL_DEF.H:1499), filled by SGL's
// own per-vblank INTBACK issuer (ST-169-R1 SMPC manual, INTBACK command;
// SGL reference ST-238-R1, peripheral data acquired every frame after
// slInitSystem). PerDigital layout per SL_DEF.H:1404-1411; button word is
// ACTIVE-LOW (0 = pressed -- ST-169 digital pad data format; jo input.h:48
// reads `(data & KEY) == 0`). PER_DGT_* bit positions per SL_DEF.H:1250-1262
// match the ST-169 pad byte layout (R=15..L=3 after the 16-bit assemble).
//
// READ-ONLY inside the masked load phase: this TU only READS the
// Smpc_Peripheral memory snapshot -- it never issues an SMPC command, so the
// ST-169 single-task rule (SMPC_Manual.txt:996-1000; the Phase p6.7
// dual-issue deadlock class) is untouched. p6_input_settle() busy-waits the
// FIRST INTBACK result and runs BEFORE p6_load_phase_enter masks interrupts
// (p6_io_main.cpp) -- never inside the masked region.
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

// ---- Witnesses (defined in p6_io_main.cpp; global C++ variables carry the
// ---- plain symbol name, the established p6_wave1_reg.c extern pattern) -----
extern int32 p6_w_in_ticks;  // device UpdateInput call count (gate I2)
extern int32 p6_w_in_perid;  // Smpc_Peripheral[0].id snapshot (gate I4)

// ---- SGL Smpc_Peripheral surface (SL_DEF.H, do-not-modify COMMON tree) -----
// Mirror declared here because SGL.H is a C header whose unrelated content
// (sprite/event structs) collides with the engine namespace view; the struct
// is byte-for-byte SL_DEF.H:1404-1411 and the extern is SL_DEF.H:1499.
extern "C" {
typedef struct {
    uint8 id;          /* Peripheral ID                */
    uint8 ext;         /* Extended data size           */
    uint16 data;       /* Button current data (active-low) */
    uint16 push;       /* Button press data            */
    uint16 pull;       /* Button pull data             */
    uint32 dummy2[4];  /* Dummy 2                      */
} P6PerDigital;
extern P6PerDigital *Smpc_Peripheral;
}

#define P6_PER_ID_STNPAD     0x02 /* SL_DEF.H:1233 Saturn Standard Pad   */
#define P6_PER_ID_NOTCONNECT 0xFF /* SL_DEF.H:1247 Not connected         */

// PER_DGT_* button bits (SL_DEF.H:1250-1262), in the ACTIVE-HIGH word after
// the ~data invert below.
#define P6_PAD_RIGHT (1 << 15)
#define P6_PAD_LEFT  (1 << 14)
#define P6_PAD_DOWN  (1 << 13)
#define P6_PAD_UP    (1 << 12)
#define P6_PAD_START (1 << 11)
#define P6_PAD_A     (1 << 10)
#define P6_PAD_C     (1 << 9)
#define P6_PAD_B     (1 << 8)
#define P6_PAD_R     (1 << 7)
#define P6_PAD_X     (1 << 6)
#define P6_PAD_Y     (1 << 5)
#define P6_PAD_Z     (1 << 4)
#define P6_PAD_L     (1 << 3)

namespace RSDK
{
namespace SKU
{

struct InputDeviceSaturn : InputDevice {
    void UpdateInput();
    void ProcessInput(int32 controllerID);

    int32 port = 0; // SMPC port index into Smpc_Peripheral

    uint16 prevButtonMasks = 0;
    uint16 buttonMasks     = 0;

    uint8 stateUp     = 0;
    uint8 stateDown   = 0;
    uint8 stateLeft   = 0;
    uint8 stateRight  = 0;
    uint8 stateA      = 0;
    uint8 stateB      = 0;
    uint8 stateC      = 0;
    uint8 stateX      = 0;
    uint8 stateY      = 0;
    uint8 stateZ      = 0;
    uint8 stateStart  = 0;
    uint8 stateSelect = 0;
};

// Registration mirror of InitKeyboardDevice (KBInputDevice.cpp:681-708).
InputDeviceSaturn *InitSaturnPadDevice(uint32 id)
{
    if (inputDeviceCount == INPUTDEVICE_COUNT)
        return NULL;

    if (inputDeviceList[inputDeviceCount] && inputDeviceList[inputDeviceCount]->active)
        return NULL;

    if (inputDeviceList[inputDeviceCount])
        delete inputDeviceList[inputDeviceCount];

    inputDeviceList[inputDeviceCount] = new InputDeviceSaturn();

    InputDeviceSaturn *device = (InputDeviceSaturn *)inputDeviceList[inputDeviceCount];
    device->gamepadType        = (DEVICE_API_NONE << 16) | (DEVICE_TYPE_CONTROLLER << 8) | (DEVICE_SATURN << 0);
    device->disabled           = false;
    device->id                 = id;
    device->active             = true;

    for (int32 i = 0; i < PLAYER_COUNT; ++i) {
        if (inputSlots[i] == (int32)id) {
            inputSlotDevices[i] = device;
            device->isAssigned  = true;
        }
    }

    inputDeviceCount++;
    return device;
}

// UpdateInput mirror of KBInputDevice.cpp:739-770 with the SMPC pad word as
// the mask source (no cursor half -- Saturn has no mouse backend, the
// RetroEngine.hpp RETRO_SATURN block).
void InputDeviceSaturn::UpdateInput()
{
    if (!Smpc_Peripheral) { // slInitSystem not run -- diagnosable, not a wild read
        if (this->port == 0)
            p6_w_in_perid = -1;
        ++p6_w_in_ticks;
        return;
    }
    volatile P6PerDigital *per = (volatile P6PerDigital *)&Smpc_Peripheral[this->port];

    if (this->port == 0)
        p6_w_in_perid = (int32)per->id;

    uint16 held = 0;
    if (per->id == P6_PER_ID_STNPAD)
        held = (uint16)~per->data; // active-low -> active-high (ST-169 pad format)

    this->prevButtonMasks = this->buttonMasks;
    this->buttonMasks     = held;

#if defined(P6_GHZ_AUTORUN)
    // DIAGNOSTIC (gated, NON-SHIPPING): scripted GHZ1 traversal input so the
    // live-mem QA harness can drive Sonic boot->signpost and measure traversal /
    // physics / render parity (signpost campaign). Live controller[] injection
    // is defeated because the verbatim ProcessInput rewrites controller[] from this
    // backend every frame (MEASURED 2026-07-05: WRITE_CORE_RAM lands but is cleared
    // each tick, Sonic dx=0); forcing bits HERE at the pad source propagates
    // cleanly through the edge logic. currentSceneFolder-gated to GHZ so the chain's
    // title/menu/AIZ auto-nav is untouched. Only present when -DP6_GHZ_AUTORUN is
    // passed (a diagnostic flavor); plain/chain shipping builds are byte-identical.
    //
    // v2 (signpost campaign): hold RIGHT always + a DATA-DRIVEN jump table
    // generated from the GHZ1 scene manifest (tools/_gen_autorun_table.py from
    // extracted/Data/Stages/GHZ/Scene1.bin -- the authored hazard coordinates,
    // Scene.cpp:558-780 entity walk). Rule: hold A (jump; Player.c decomp maps
    // A/B/C to jump via controller keyA/keyB/keyC) while player.x sits in
    // [hazard.x - LEAD, hazard.x - GAP] and the hazard is at-or-below the
    // player's running height (dy = hazard.y - player.y in [-JUMP_DY_UP,
    // +JUMP_DY_DOWN]). One window = one press edge (prev/cur mask diff) with a
    // sustained hold for full jump height; clustered hazards (SpikeLog rows)
    // merge into one longer hold. Plus raw override boxes for live-iterated
    // fixes. Reads slot 0 (SLOT_PLAYER1, reserve region) via RSDK_ENTITY_AT --
    // read-only entity access, no gameplay state is written (behavior-parity
    // binding: the scripted input is indistinguishable from a human pad).
    if (RSDK::currentSceneFolder[0] == 'G' && RSDK::currentSceneFolder[1] == 'H'
        && RSDK::currentSceneFolder[2] == 'Z' && RSDK::currentSceneFolder[3] == 0) {
#include "../../tools/_portspike/_p6/p6_autorun_table.h"
        enum { P6_AR_LEAD = 96, P6_AR_GAP = 8, P6_AR_DY_UP = 48, P6_AR_DY_DOWN = 160 };
        // v3 (signpost campaign r3): PRESS-EDGE PULSE SHAPER. Root cause of the
        // permanent x=2358 stall (MEASURED 18:19 run: state=Player_State_Ground
        // 0x06032A98, anim=17 push, gvel=0, 33 stall detections): the v2 table
        // held A as a LEVEL for the whole [hx-96, hx-8] window. The verbatim
        // engine edge logic (Input.cpp:258-266) converts a held key into exactly
        // ONE press edge (press cleared while down persists), and the decomp
        // Player_State_Ground jump requires jumpPress = a fresh PRESS edge
        // (Player.c Player_Input_P1). If the one jump fails to carry Sonic
        // across the window (landed back inside it against the 32px step face),
        // A stays down forever -> no second edge -> no jump, ever. The v2
        // unstick only ORed A (never released it while the table held it), so
        // it could not manufacture an edge either. FIX: route every scripted
        // jump request through a hold/release pulse: JHOLD=32 ticks asserted
        // (full variable-jump rise, apex ~30 frames at -6.5px/f, 0.21875px/f^2)
        // then JREL=10 ticks forced released -> each cycle is a fresh press
        // edge until the obstacle is actually cleared. Diagnostic flavor only.
        enum { P6_AR_JHOLD = 32, P6_AR_JREL = 10 };
        // override buttons bit0 = SUPPRESS-JUMP: inside such a box, no scripted
        // jump fires (hazard-scan, unstick fallback, or override-A) -- for
        // rolling-hill sections the player must RUN through on momentum, never
        // jump (signpost r4: x5376-5636 GHZ1 hill; spurious spikes/terrain
        // waypoints 136-320px BELOW the deck launched Sonic airborne off the
        // crest -> permanent x~5572 oscillation, MEASURED state=Player_State_
        // Ground+0xD0 anim10 onG=0).
        enum { P6_AR_SUPPRESS_JUMP = 0x0001 };
        uint16 scripted    = P6_PAD_RIGHT;
        int32 wantJump     = 0;
        int32 suppressJump = 0;
        RSDK::Entity *p0   = (RSDK::Entity *)RSDK_ENTITY_AT(0);
        if (p0->classID != 0) { // alive (death->respawn leaves classID 0 for a beat)
            int32 px = p0->position.x >> 16;
            int32 py = p0->position.y >> 16;
            for (int32 i = 0; i < P6_AUTORUN_NHAZ; ++i) {
                int32 hx = (int32)p6_autorun_hazards[i].x;
                if (hx - P6_AR_LEAD > px)
                    break; // table is x-sorted; nothing further can match
                if (px >= hx - P6_AR_LEAD && px < hx - P6_AR_GAP) {
                    int32 dy = (int32)p6_autorun_hazards[i].y - py;
                    if (dy >= -P6_AR_DY_UP && dy <= P6_AR_DY_DOWN) {
                        wantJump = 1;
                        break;
                    }
                }
            }
            for (int32 i = 0; i < P6_AUTORUN_NOVR; ++i) {
                const P6AutorunOverride *o = &p6_autorun_overrides[i];
                if (px >= (int32)o->x0 && px < (int32)o->x1
                    && py >= (int32)o->y0 && py < (int32)o->y1) {
                    if (o->buttons & P6_PAD_RIGHT) // bit15 = SUPPRESS forced RIGHT
                        scripted &= (uint16)~P6_PAD_RIGHT;
                    if (o->buttons & P6_PAD_A) // route override jumps through the shaper
                        wantJump = 1;
                    if (o->buttons & P6_AR_SUPPRESS_JUMP) // bit0 = suppress all jumps here
                        suppressJump = 1;
                    scripted |= (uint16)(o->buttons & ~(P6_PAD_RIGHT | P6_PAD_A | P6_AR_SUPPRESS_JUMP));
                }
            }
            // UNSTICK fallback (run-2 iteration: permanent push-stall against a
            // >16px step). If x stagnates ~2/3 s while alive, request jump for
            // one full pulse cycle; the shaper below guarantees the press edge.
            // Tracked in a SEPARATE flag (unstickJump) so it survives the
            // suppress-jump cancel below -- a hard wedge against a small terrain
            // step INSIDE a suppress box (signpost r6: the x13412 32px step on
            // the low-path approach to the x14540 PlaneSwitch) must still be
            // able to hop out, per this fallback's documented intent. Only the
            // SCRIPTED-HAZARD jumps (wantJump) are cancelled by suppress.
            int32 unstickJump = 0;
            {
                static int32 s_lastx = -1, s_stagnant = 0, s_jumpreq = 0;
                if (s_jumpreq > 0) {
                    unstickJump = 1;
                    --s_jumpreq;
                }
                else if (px >= s_lastx - 2 && px <= s_lastx + 2) {
                    if (++s_stagnant >= 40) {
                        s_jumpreq  = P6_AR_JHOLD + P6_AR_JREL;
                        s_stagnant = 0;
                    }
                }
                else {
                    s_stagnant = 0;
                    s_lastx    = px;
                }
            }
            // suppress-jump boxes win over every SCRIPTED-hazard jump source:
            // the player runs the section on pure momentum (no airborne launch)
            // -- but the last-resort unstick still fires so a small step-wedge
            // inside the box can recover.
            if (suppressJump)
                wantJump = 0;
            if (unstickJump)
                wantJump = 1;

            // === r8 (signpost campaign) SCRIPTED PLANE POKE ===
            // The GHZ1 forward deck toward the SignPost (x14576->15792) is on
            // collision plane B; the runner arrives on plane A (MEASURED live
            // x8471: collisionPlane=0). The PlaneSwitch that would arm plane B
            // (Scene1.bin slot 924 @x14768 flags -- and the earlier x14540 one)
            // sits PAST the leftward horizontal Spring(14516, vel.x=-0xA0000)
            // that launches the autorun runner left before he can reach it
            // (r5-r7 MEASURED: permanent oscillation, max_x 14502). So mimic the
            // switch he cannot reach: force collisionPlane=1 while he is in the
            // approach window [14440,14520), BEFORE the FG-Low plane-A hole at
            // x14464 (tools/_floor_profile.py: plane A is None x14464-14572, plane
            // B carries the y960 deck from x14496). PlaneSwitch.c:94 only writes
            // collisionPlane (NOT collisionLayers), so leave collisionLayers
            // untouched (runner carries 0x18). Re-assert every frame in-window so
            // a mid-window Player_State reset can't drop it. Diagnostic flavor
            // only (P6_GHZ_AUTORUN); the plain/chain shipping paths never define
            // it -> byte-identical, no shipping physics change. The gate's
            // player() reader samples collisionPlane (@+81) so cplane flipping
            // 0->1 in the live trace is the poke's proof.
            if (px >= 14440 && px < 14520)
                p0->collisionPlane = 1;
        }
        // pulse shaper: while a jump is requested, assert A for JHOLD ticks then
        // force-release JREL ticks, repeating -- one press edge per cycle.
        {
            static int32 s_phase = -1;
            if (wantJump) {
                if (s_phase < 0)
                    s_phase = 0;
                if (s_phase < P6_AR_JHOLD)
                    scripted |= P6_PAD_A;
                if (++s_phase >= P6_AR_JHOLD + P6_AR_JREL)
                    s_phase = 0;
            }
            else {
                s_phase = -1;
            }
        }
        this->buttonMasks |= scripted;
    }
#endif

    int32 changedKeys = (int32)(uint16)(~this->prevButtonMasks & (this->buttonMasks ^ this->prevButtonMasks));
    if (changedKeys) {
        this->inactiveTimer[0] = 0;
        this->anyPress         = true;
    }
    else {
        ++this->inactiveTimer[0];
        this->anyPress = 0;
    }

    if ((changedKeys & P6_PAD_A) || (changedKeys & P6_PAD_START))
        this->inactiveTimer[1] = 0;
    else
        ++this->inactiveTimer[1];

    this->stateUp     = (this->buttonMasks & P6_PAD_UP) != 0;
    this->stateDown   = (this->buttonMasks & P6_PAD_DOWN) != 0;
    this->stateLeft   = (this->buttonMasks & P6_PAD_LEFT) != 0;
    this->stateRight  = (this->buttonMasks & P6_PAD_RIGHT) != 0;
    this->stateA      = (this->buttonMasks & P6_PAD_A) != 0;
    this->stateB      = (this->buttonMasks & P6_PAD_B) != 0;
    this->stateC      = (this->buttonMasks & P6_PAD_C) != 0;
    this->stateX      = (this->buttonMasks & P6_PAD_X) != 0;
    this->stateY      = (this->buttonMasks & P6_PAD_Y) != 0;
    this->stateZ      = (this->buttonMasks & P6_PAD_Z) != 0;
    this->stateStart  = (this->buttonMasks & P6_PAD_START) != 0;
    this->stateSelect = 0; // the Saturn pad has no Select

    this->ProcessInput(CONT_ANY);

    ++p6_w_in_ticks; // gate I2 witness: one completed device update
}

// ProcessInput mirror of KBInputDevice.cpp:773-789.
void InputDeviceSaturn::ProcessInput(int32 controllerID)
{
    ControllerState *cont = &controller[controllerID];

    cont->keyUp.press |= this->stateUp;
    cont->keyDown.press |= this->stateDown;
    cont->keyLeft.press |= this->stateLeft;
    cont->keyRight.press |= this->stateRight;
    cont->keyA.press |= this->stateA;
    cont->keyB.press |= this->stateB;
    cont->keyC.press |= this->stateC;
    cont->keyX.press |= this->stateX;
    cont->keyY.press |= this->stateY;
    cont->keyZ.press |= this->stateZ;
    cont->keyStart.press |= this->stateStart;
    cont->keySelect.press |= this->stateSelect;
}

// API-init mirror of InitKeyboardInputAPI (KBInputDevice.cpp:791-806); ONE
// port-1 pad device (the diag contract; multitap is a later wave).
void InitSaturnInputAPI()
{
    char idBuffer[0x10];
    strcpy(idBuffer, "SaturnDevice0");
    uint32 id = 0;
    GenerateHashCRC(&id, idBuffer);

    InputDeviceSaturn *device = InitSaturnPadDevice(id);
    if (device) {
        device->port = 0;
        // add the pad "device" index to the type as its lowest byte
        // (the KBInputDevice.cpp:803 shape)
        device->gamepadType |= 1;
    }
}

} // namespace SKU
} // namespace RSDK

// =============================================================================
// p6_input_settle -- bounded busy-wait until SGL's per-vblank INTBACK has
// populated Smpc_Peripheral[0] with the port-1 digital pad (PER_ID_StnPad).
// MUST run with interrupts ENABLED (before p6_load_phase_enter) -- the data
// arrives via SGL's vblank handler. Memory-read only: no SMPC command issue,
// so the ST-169 single-task rule is respected. Bound ~30M iterations (a few
// seconds at 26.8 MHz) so a disconnected pad cannot hang the diag; returns
// the final id byte for the witness.
// =============================================================================
extern "C" int32 p6_input_settle(void)
{
    if (!Smpc_Peripheral)
        return -1;
    volatile P6PerDigital *per = (volatile P6PerDigital *)&Smpc_Peripheral[0];
    for (volatile int32 i = 0; i < 30000000; ++i) {
        if (per->id == P6_PER_ID_STNPAD)
            break;
    }
    return (int32)per->id;
}
