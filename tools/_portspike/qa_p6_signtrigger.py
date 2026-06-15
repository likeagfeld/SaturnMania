#!/usr/bin/env python3
# Task #236 (P6.8 F.5) RED-first gate: prove the GHZ1->GHZ2 act transition fires
# from the CANONICAL signpost-cross trigger, NOT a scripted ActClear spawn.
#
# The diag (P6_WARP_TEST) relocates the GHZ1 RUNPAST signpost next to the player
# so the engine's own SignPost_CheckTouch crossing (player.x > signpost.x,
# SignPost.c:313) fires -> SignPost_State_Spin -> its OWN ResetEntitySlot(
# SLOT_ACTCLEAR) (SignPost.c:452) -> the full ATL chain -> GHZ2.
#
# GREEN assertions (all must hold):
#   1. _p6_w_sign_crossed == 1   -> the signpost's `active` flipped to
#      ACTIVE_NORMAL(2), which ONLY SignPost_CheckTouch sets (SignPost.c:326)
#      on the canonical crossing. Drift-proof (no .text address dependence).
#   2. _p6_w_ac_frames   >  0    -> ActClear went live. With the F.4 direct
#      spawn REMOVED, the ONLY remaining spawner is the real SignPost_State_Spin.
#   3. _p6_w_lay_bytes   == 43347-> the band-store mount resolved GHZ2 (51094 =
#      GHZ1). i.e. ActClear's ++listPos + stageFinishCallback + LoadScene ran.
#
# RED on the current build: the symbol _p6_w_sign_crossed is absent (older map)
# OR, with the diag built but the crossing not firing, sign_crossed==0.
import importlib.util
import os
import pathlib
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

LAY_GHZ2 = 43347
SYMS = ["_p6_w_sign_crossed", "_p6_w_ac_frames", "_p6_w_lay_bytes",
        "_p6_w_warp_signactive", "_p6_w_sign_state", "_p6_w_listpos_max",
        "_p6_w_cont_frames", "_p6_w_warp_plrx",
        "_p6_w_sign_count", "_p6_w_sign_type", "_p6_w_sign_posx"]

SIGN_TYPE = {0: "RUNPAST", 1: "DROP", 2: "COMP", 3: "DECOR", -1: "(none)"}


def main(argv):
    if len(argv) < 2:
        print("usage: qa_p6_signtrigger.py <state.mcs> [game.map]")
        return 2
    mcs = _scene._as_path(argv[1])
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT
    map_text = _scene.read_text(mp)
    syms = {s: _scene.map_symbol(map_text, s)
            for s in [_scene.SYM_MAGIC] + SYMS}

    missing = [s for s in SYMS if syms[s] is None]
    if missing:
        print("RED: witness symbol(s) absent from map (build not updated):")
        for s in missing:
            print("       MISSING %s" % s)
        print("     -> rebuild with the F.5 witnesses + -u roots.")
        return 1

    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    print("calibration: %s" % label)

    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=True)
         for s in SYMS}
    print("-" * 62)
    for s in SYMS:
        print("  %-24s = %d (0x%08x)" % (s, v[s], v[s] & 0xFFFFFFFF))
    print("-" * 62)

    crossed = v["_p6_w_sign_crossed"]
    acf = v["_p6_w_ac_frames"]
    lay = v["_p6_w_lay_bytes"]
    active = v["_p6_w_warp_signactive"]

    scount = v["_p6_w_sign_count"]
    stype = v["_p6_w_sign_type"]
    sposx = v["_p6_w_sign_posx"]
    print("  GHZ1 signpost setup: count=%d  type=%s  posx=%dpx"
          % (scount, SIGN_TYPE.get(stype, "?%d" % stype), sposx >> 16))
    print("  signpost crossed (active->ACTIVE_NORMAL=2)  : %s (active=%d)"
          % ("YES" if crossed == 1 else "NO", active))
    print("  ActClear ever live (sticky frames)          : %s (%d)"
          % ("YES" if acf > 0 else "NO", acf))
    print("  band-store resolved GHZ2 (lay_bytes=%d)   : %s (%d)"
          % (LAY_GHZ2, "YES" if lay == LAY_GHZ2 else "NO", lay))

    ok = (crossed == 1 and acf > 0 and lay == LAY_GHZ2)
    print("=" * 62)
    if ok:
        print("RESULT: GREEN -- canonical signpost crossing drove GHZ1->GHZ2 "
              "(no scripted ActClear spawn).")
        return 0
    print("RESULT: RED -- the real signpost trigger did NOT complete the "
          "GHZ1->GHZ2 transition:")
    if crossed != 1:
        print("        - the crossing never fired (signpost active never "
              "reached ACTIVE_NORMAL). Check relocation / inRange / CheckTouch.")
    if acf <= 0:
        print("        - ActClear never spawned (SignPost_State_Spin "
              "ResetEntitySlot did not run -> spinCount never hit 0).")
    if lay != LAY_GHZ2:
        print("        - band-store still %d (expected GHZ2 %d): listPos/"
              "mount did not advance." % (lay, LAY_GHZ2))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
