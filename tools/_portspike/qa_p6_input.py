#!/usr/bin/env python3
# =============================================================================
# qa_p6_input.py -- P6.7 W7 gate (Task #227): the ENGINE input chain is live
# in the diag pack -- real Saturn pad state flows SMPC -> SGL Smpc_Peripheral
# (ST-169-R1 INTBACK; SGL ST-238 vblank issuer) -> the Saturn InputDevice
# backend (platform/Saturn/InputDevice_Saturn.cpp, AudioDevice_Saturn
# precedent) -> the VERBATIM engine Input.cpp (RSDK::ProcessInput,
# Input.cpp:194-311) -> RSDK::controller[] -> the GAME's ControllerInfo
# (p6_wave1_link receives the ENGINE arrays, the RetroEngine.cpp:1287-1292
# link shape, instead of the W15 zeroed statics).
#
# Headless Mednafen presses NO buttons but a digital pad IS connected on
# port 1 -- so this gate asserts PLUMBING, not presses:
#
#   I1 witness symbols present in game.map (RED while W7 unimplemented).
#   I2 the engine input tick ran during the GHZ ticks: the Saturn device's
#      UpdateInput call counter == 60 (ProcessInput() is called once per
#      tick of the 60-tick GHZ loop, mirroring ENGINESTATE_REGULAR,
#      RetroEngine.cpp:393; ProcessInput -> device->UpdateInput,
#      Input.cpp:199-205).
#   I3 game-side ControllerInfo / AnalogStickInfoL / TouchInfo == the ENGINE
#      array addresses (RSDK::controller / RSDK::stickL / RSDK::touchInfo,
#      demangled in the COFF map) -- NOT the zeroed statics.
#   I4 pad plumbing contract per the Input.cpp semantics:
#      - inputDeviceCount == 1, device id != 0, device active == 1
#        (InitKeyboardDevice registration shape, KBInputDevice.cpp:681-708)
#      - device anyPress == 0 and inputSlots[0] == INPUT_AUTOASSIGN (-1):
#        with zero presses GetAvaliableInputDevice never returns a device
#        (Input.hpp:529-539 anyPress gate), so the autoassign slot state
#        persists (Input.cpp:100, :217-221)
#      - every controller[0..4] button down/press bit == 0 (no input in the
#        headless boot)
#      - SMPC peripheral id witness == 0x02 (PER_ID_StnPad, SL_DEF.H:1233:
#        a digital pad IS connected on port 1)
#
# Usage: python tools/_portspike/qa_p6_input.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "p6_j0.mcs")

SYMS = [
    "_p6_w_in_ticks",     # Saturn device UpdateInput call count
    "_p6_w_in_ctrlptr",   # game-side ControllerInfo after p6_wave1_link
    "_p6_w_in_stickptr",  # game-side AnalogStickInfoL after p6_wave1_link
    "_p6_w_in_touchptr",  # game-side TouchInfo after p6_wave1_link
    "_p6_w_in_devcount",  # RSDK::inputDeviceCount after the ticks
    "_p6_w_in_devid",     # inputDeviceList[0]->id
    "_p6_w_in_devstate",  # (active<<16)|(isAssigned<<8)|anyPress
    "_p6_w_in_slot0",     # RSDK::inputSlots[0]
    "_p6_w_in_btnbits",   # OR of down|press across controller[0..4] x 12 keys
    "_p6_w_in_perid",     # Smpc_Peripheral[0].id snapshot at device update
]
# Engine pad-state arrays (Input.cpp:11-18 definitions). The COFF-SH final
# link DEMANGLES C++ symbols in game.map (MEASURED 2026-06-12: line 3801
# reads "0x...060ab3c4  RSDK::controller", and zero `_ZN4RSDK*` symbol lines
# exist -- only .text._ZN* SECTION names), so the lookup uses the demangled
# spelling. map_symbol's `_?` prefix tolerance is a no-op here.
ENG_SYMS = {
    "controller": "RSDK::controller",
    "stickL":     "RSDK::stickL",
    "touchInfo":  "RSDK::touchInfo",
}

INPUT_AUTOASSIGN = -1   # Input.hpp:13
PER_ID_STNPAD = 0x02    # SL_DEF.H:1233
EXP_TICKS = 60          # the GHZ-pass engine tick loop (p6_io_main.cpp)


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7 W7 INPUT GATE: engine input chain live (SMPC -> ProcessInput "
          "-> game ControllerInfo)")
    print("=" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS + list(ENG_SYMS.values()):
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("  [ RED ] I1 witness + engine-array symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected while W7 is unimplemented.)")
        print("-" * 72)
        print("RESULT: RED -- W7 not proven (I1).")
        return 1
    print("  [GREEN] I1 witness + engine-array symbols present in the link map")
    for k, s in ENG_SYMS.items():
        print("          %-10s @ 0x%08X" % (k, syms[s]))

    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=True)
         for s in SYMS}

    i2 = v["_p6_w_in_ticks"] is not None and v["_p6_w_in_ticks"] >= EXP_TICKS

    ctrl = (v["_p6_w_in_ctrlptr"] or 0) & 0xFFFFFFFF
    stick = (v["_p6_w_in_stickptr"] or 0) & 0xFFFFFFFF
    touch = (v["_p6_w_in_touchptr"] or 0) & 0xFFFFFFFF
    i3 = (ctrl == syms[ENG_SYMS["controller"]]
          and stick == syms[ENG_SYMS["stickL"]]
          and touch == syms[ENG_SYMS["touchInfo"]])

    devstate = v["_p6_w_in_devstate"]
    active = (devstate >> 16) & 0xFF if devstate is not None else None
    assigned = (devstate >> 8) & 0xFF if devstate is not None else None
    anypress = devstate & 0xFF if devstate is not None else None
    i4 = (v["_p6_w_in_devcount"] == 1
          and (v["_p6_w_in_devid"] or 0) != 0
          and active == 1 and anypress == 0
          and v["_p6_w_in_slot0"] == INPUT_AUTOASSIGN
          and v["_p6_w_in_btnbits"] == 0
          and v["_p6_w_in_perid"] == PER_ID_STNPAD)

    checks = [
        ("I2 engine input tick ran during the GHZ ticks "
         "(device UpdateInput count >= %d)" % EXP_TICKS, i2,
         "ticks=%s" % v["_p6_w_in_ticks"]),
        ("I3 game-side ControllerInfo/StickL/TouchInfo == the ENGINE arrays "
         "(not the W15 zeroed statics)", i3,
         "ctrl=0x%08X vs 0x%08X, stick=0x%08X vs 0x%08X, touch=0x%08X vs 0x%08X"
         % (ctrl, syms[ENG_SYMS["controller"]],
            stick, syms[ENG_SYMS["stickL"]],
            touch, syms[ENG_SYMS["touchInfo"]])),
        ("I4 pad plumbing contract (1 device, id!=0, active, no presses, "
         "slot0 AUTOASSIGN, all button bits 0, SMPC id 0x02)", i4,
         "devcount=%s id=%s active=%s assigned=%s anyPress=%s slot0=%s "
         "btnbits=%s perid=%s"
         % (v["_p6_w_in_devcount"], _scene._hx(v["_p6_w_in_devid"]),
            active, assigned, anypress, v["_p6_w_in_slot0"],
            v["_p6_w_in_btnbits"], _scene._hx(v["_p6_w_in_perid"]))),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine input chain is live: SMPC pad state")
        print("        reaches RSDK::controller via the verbatim ProcessInput and")
        print("        the game's ControllerInfo points at the ENGINE arrays.")
        return 0
    print("RESULT: RED -- W7 not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
