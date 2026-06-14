#!/usr/bin/env python3
# =============================================================================
# qa_p6_transition.py -- P6.8 Step F.1 gate: the engine-driven SCENE-TRANSITION
# machine. The shipping loop (p6_scene_tick armed path) now dispatches on
# sceneInfo.state: when an object sets ENGINESTATE_LOAD (Zone's RSDK.LoadScene
# at the act signpost), the tick runs p6_scene_load_and_arm() -- the engine
# LoadSceneFolder/LoadSceneAssets/InitObjects chain + the Saturn render re-arm --
# instead of ignoring the request and ticking the old scene forever.
#
# RED-FIRST: the prior shipping/diag build has NO p6_w_transitions symbol and no
# state dispatch, so this gate is RED on the previous link map (T1 missing).
#
# HOW THE TRANSITION IS EXERCISED (diag-only, -DP6_TRANSITION_TEST via
# build_diag.sh's P6_XTEST=1): a ONE-SHOT injected trigger sets
# sceneInfo.state = ENGINESTATE_LOAD at cont_frames >= 100 with listPos
# unchanged (a GHZ1 -> GHZ1 self-reload -- the already-mounted band store stays
# valid; a different-layout transition is F.2). This proves the MACHINE without
# a multi-minute playthrough to the signpost. The real trigger is Zone.
#
# CHECKS
#   T1 witness symbols present in game.map (RED while F unimplemented).
#   T2 p6_w_transitions >= 1: the ENGINESTATE_LOAD dispatch handled a transition
#      (the injected self-reload was detected + the load/arm chain ran).
#   T3 the loop SURVIVED the transition: p6_w_cont_frames is large (> 120, well
#      past the trigger at 100) and the Player is live in the re-initialised GHZ
#      scene (pos in GHZ bounds, valid anim id) -- i.e. the reload did not hang
#      or corrupt the scene.
#
# Usage: python tools/_portspike/qa_p6_transition.py [savestate.mcs] [map]
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

SYMS = ["_p6_w_transitions", "_p6_w_cont_frames", "_p6_w_cont_plr_x",
        "_p6_w_cont_plr_y", "_p6_w_cont_animid", "_p6_w_lay_bytes"]

CONT_FRAMES_MIN = 120  # the injected trigger fires at 100; a capture past it
                       # proves the loop kept ticking THROUGH the transition.
# F.2: the GHZ2 act advance lands the Player at its canonical spawn (256,1358)px,
# GROUNDED. GHZ2's playfield is 96 tiles = 1536px tall. A player that FELL THROUGH
# (the pre-scratch-fix bug) sits at ~1770px, BELOW the playfield -> the y-bound at
# the playfield floor (1536px) is the grounded-vs-fell-through discriminator.
# F.2 cross-zone discriminator: after the GHZ1 -> GHZ2 act advance the windowed
# band store mounted at P6_LW_LAYOUTBANDS must be GHZ2's (43,347 B at bandRows=12),
# NOT the boot GHZ1 store (51,094 B). p6_w_lay_bytes carries the mounted size.
GHZ2_LAY_BYTES = 43347
PLR_X_MIN = 0
PLR_X_MAX = 16384 << 16
PLR_Y_MIN = 0
PLR_Y_MAX = 1536 << 16  # GHZ2 playfield floor (96 tiles); grounded < this, fell-through above


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.8 STEP F.1 GATE: engine-driven scene-transition machine")
    print("=" * 72)

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
        print("  [ RED ] T1 transition-witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected while Step F is unimplemented.)")
        print("-" * 72)
        print("RESULT: RED -- Step F not proven (T1).")
        return 1
    print("  [GREEN] T1 transition-witness symbols present in the link map")

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

    tr = v["_p6_w_transitions"]
    cont = v["_p6_w_cont_frames"]
    px = v["_p6_w_cont_plr_x"]
    py = v["_p6_w_cont_plr_y"]
    aid = v["_p6_w_cont_animid"]
    lay = v["_p6_w_lay_bytes"]

    t2 = tr is not None and tr >= 1
    t3 = (cont is not None and cont > CONT_FRAMES_MIN
          and px is not None and py is not None and aid is not None
          and PLR_X_MIN <= px <= PLR_X_MAX
          and PLR_Y_MIN <= py <= PLR_Y_MAX
          and aid >= 0)
    t4 = lay is not None and lay == GHZ2_LAY_BYTES

    checks = [
        ("T2 ENGINESTATE_LOAD dispatch handled a transition "
         "(p6_w_transitions >= 1)", t2,
         "p6_w_transitions=%s" % tr),
        ("T3 loop SURVIVED the transition (cont_frames > %d, Player live "
         "in re-initialised scene)" % CONT_FRAMES_MIN, t3,
         "cont_frames=%s plr=(%d,%d)px animid=%s"
         % (cont, (px >> 16) if px is not None else 0,
            (py >> 16) if py is not None else 0, aid)),
        ("T4 CROSS-ZONE band store swapped to GHZ2 "
         "(p6_w_lay_bytes == %d, not GHZ1's 51094)" % GHZ2_LAY_BYTES, t4,
         "p6_w_lay_bytes=%s" % lay),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine handled a scene transition "
              "(ENGINESTATE_LOAD)")
        print("        and the loop kept running the re-initialised scene.")
        return 0
    print("RESULT: RED -- Step F not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
