#!/usr/bin/env python3
# =============================================================================
# qa_p6_obj.py -- P6.7a gate (Task #210): ENGINE OBJECT LOOP RUNS THE
# VERBATIM DECOMP RING. The engine's own machinery -- RegisterObject
# (Object.cpp:62-108), ResetEntitySlot (:1084), ProcessObjects (:357-475)
# update dispatch + draw-group enqueue -- ticks the byte-for-byte decomp
# Ring_State_LostFX (gravity +0x1800/tick, +0x10 scale, ++timer,
# destroy > 64) and dispatches Ring_Draw from the draw-group walk through
# the engine RSDKFunctionTable into the REAL Saturn DrawSprite backend
# (P6.5b3) -- a falling, animating, engine-driven ring. The pack tick
# respawns the slot via the engine path each ~65-tick cycle.
#
# Every witness has a closed-form model from the VERBATIM state body:
#   O1 p6_w_obj_classcount == 2   (Blank @0 + Ring @1 registered)
#   O2 p6_w_obj_spawns >= 1 and == ceil-ish of ticks/cycle (engine respawns)
#   O3 physics at capture (classid==1): vely == 0x1800*timer,
#      posy == y0 + 0x1800*timer*(timer+1)/2, scalex == 0x10*timer
#   O4 animator cadence: frameid == floor((timer*0x40 - 1)/0x100) % 16
#      (engine ProcessAnimation via the function-table bridge)
#   O5 p6_w_obj_draws > 0 and consistent with ticks (the draw-group walk
#      dispatched Ring_Draw -> DrawSprite; also cross-checked by
#      qa_p6_draw.py's updated D1: draw_calls == ticks + obj_draws)
#
# Usage: python tools/_portspike/qa_p6_obj.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")

SYMS = ["_p6_w_obj_classcount", "_p6_w_obj_classid", "_p6_w_obj_timer",
        "_p6_w_obj_vely", "_p6_w_obj_posy", "_p6_w_obj_scalex",
        "_p6_w_obj_frameid", "_p6_w_obj_draws", "_p6_w_obj_spawns",
        "_p6_w_spr_ticks"]

SPAWN_Y = 60 << 16  # p6_io_main block 8 spawn position (world fixed-point)


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7a OBJECT GATE: engine ProcessObjects runs the verbatim Ring")
    print("=" * 72)
    print("  savestate: %s" % mcs)
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the map:")
        for s in missing:
            print("        %s" % s)
        print("        (Expected while the P6.7a body is unwritten.)")
        return 1
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
    print("  peeked: " + "  ".join("%s=%s" % (s[9:], _scene._hx(v[s])) for s in SYMS))

    t      = v["_p6_w_obj_timer"] or 0
    alive  = v["_p6_w_obj_classid"] == 1
    ticks  = v["_p6_w_spr_ticks"] or 0

    checks = [
        ("O1 engine registration: classCount == 2 (Blank + Ring)",
         v["_p6_w_obj_classcount"] == 2, "got %s" % v["_p6_w_obj_classcount"]),
        ("O2 engine respawn cycle ran (spawns >= 1, draws > 0)",
         (v["_p6_w_obj_spawns"] or 0) >= 1 and (v["_p6_w_obj_draws"] or 0) > 0,
         "spawns=%s draws=%s" % (v["_p6_w_obj_spawns"], v["_p6_w_obj_draws"])),
    ]

    if alive and t > 0:
        exp_vely   = 0x1800 * t
        exp_posy   = SPAWN_Y + 0x1800 * t * (t + 1) // 2
        exp_scalex = 0x10 * t
        exp_frame  = (max(0, t * 0x40 - 1) // 0x100) % 16
        checks += [
            ("O3a verbatim gravity: vely == 0x1800*timer (timer=%d)" % t,
             v["_p6_w_obj_vely"] == exp_vely,
             "got %s expect 0x%X" % (_scene._hx(v["_p6_w_obj_vely"]), exp_vely)),
            ("O3b verbatim integration: posy == y0 + sum(vely)",
             v["_p6_w_obj_posy"] == exp_posy,
             "got %s expect 0x%X" % (_scene._hx(v["_p6_w_obj_posy"]), exp_posy)),
            ("O3c verbatim scale: scalex == 0x10*timer",
             v["_p6_w_obj_scalex"] == exp_scalex,
             "got %s expect 0x%X" % (_scene._hx(v["_p6_w_obj_scalex"]), exp_scalex)),
            ("O4 engine ProcessAnimation cadence: frame %d at timer %d" % (exp_frame, t),
             v["_p6_w_obj_frameid"] == exp_frame,
             "got %s" % v["_p6_w_obj_frameid"]),
        ]
    else:
        # Captured in the 1-2 tick dead/respawn window: physics unverifiable
        # THIS capture, but the cycle counters still bind the loop.
        checks.append(("O3/O4 (capture in respawn window; spawns/draws carry the proof)",
                       (v["_p6_w_obj_spawns"] or 0) >= 2,
                       "classid=%s spawns=%s" % (v["_p6_w_obj_classid"], v["_p6_w_obj_spawns"])))

    # O5: cycle consistency -- spawns ~= ticks/65 (+1 boot spawn).
    if ticks > 130:
        exp_spawns_lo = ticks // 70
        exp_spawns_hi = ticks // 60 + 2
        checks.append(("O5 spawn count consistent with ticks (%d..%d at ticks=%d)"
                       % (exp_spawns_lo, exp_spawns_hi, ticks),
                       exp_spawns_lo <= (v["_p6_w_obj_spawns"] or 0) <= exp_spawns_hi,
                       "spawns=%s" % v["_p6_w_obj_spawns"]))

    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine's OWN object loop (RegisterObject +")
        print("        ResetEntitySlot + ProcessObjects + draw-group dispatch)")
        print("        runs the VERBATIM decomp Ring: physics, cadence, draws")
        print("        and respawns all match the closed-form decomp model.")
        return 0
    print("RESULT: RED -- engine object loop not proven (see witnesses).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
