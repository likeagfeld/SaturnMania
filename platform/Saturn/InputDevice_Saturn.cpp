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
        uint16 scripted    = P6_PAD_RIGHT;
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
                        scripted |= P6_PAD_A;
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
                    scripted |= (uint16)(o->buttons & ~P6_PAD_RIGHT);
                }
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
