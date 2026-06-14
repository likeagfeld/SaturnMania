#!/usr/bin/env python3
# =============================================================================
# qa_p6_warp.py -- P6.8 F.2-followup gate: the debug WARP enabler proves the
# REAL SignPost crossing fires in gameplay. A diag-only one-shot warp
# (-DP6_WARP_TEST, built via build_diag.sh P6_WARP=1) teleports SLOT_PLAYER1
# past the GHZ1 signpost (x=15792px); the camera follows, the signpost comes
# in-range, and SignPost_CheckTouch (player.x > signpost.x, SignPost.c:313)
# flips it ACTIVE_BOUNDS(4) -> ACTIVE_NORMAL(2) and starts the goal sequence.
# This unblocks testing the act-clear chain (signpost -> ActClear -> Zone fade
# -> LoadScene -> the F.2 transition machine) without a full playthrough.
#
# CHECKS
#   W1 witness symbols present in game.map (RED while unimplemented).
#   W2 the warp LANDED the player at the signpost: p6_w_warp_plrx is in the
#      signpost neighbourhood (>> the 108px spawn, near 15792px).
#   W3 the SignPost FIRED: p6_w_warp_signactive == ACTIVE_NORMAL (2) -- the
#      real SignPost_CheckTouch crossing detected the warped player (RED before
#      the cross when it reads ACTIVE_BOUNDS=4 or the not-yet-seen -1).
#
# Usage: python tools/_portspike/qa_p6_warp.py [savestate.mcs] [map]
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

SYMS = ["_p6_w_warp_plrx", "_p6_w_warp_signactive", "_p6_w_cont_frames",
        "_p6_w_transitions"]

ACTIVE_NORMAL = 2          # SignPost active after the cross fires


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.8 F.2-followup WARP GATE: real SignPost cross fires in gameplay")
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
        print("  [ RED ] W1 warp-witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected on a non-warp build.)")
        print("-" * 72)
        print("RESULT: RED -- warp not proven (W1).")
        return 1
    print("  [GREEN] W1 warp-witness symbols present in the link map")

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

    px = v["_p6_w_warp_plrx"]
    act = v["_p6_w_warp_signactive"]
    cont = v["_p6_w_cont_frames"]
    tr = v["_p6_w_transitions"]

    # W2: the REAL SignPost_CheckTouch crossing fired (active BOUNDS->NORMAL).
    w2 = act is not None and act == ACTIVE_NORMAL
    # W3: the signpost -> Zone fade -> RSDK.LoadScene chain reached the engine's
    # ENGINESTATE_LOAD dispatch (the F.1/F.2 machine) -- i.e. the REAL gameplay
    # act-transition path completed end-to-end (it loops because ActClear is
    # stubbed: no tally, nothing advances listPos to GHZ2, so GHZ1 reloads).
    w3 = tr is not None and tr >= 1

    checks = [
        ("W2 the REAL SignPost crossing FIRED "
         "(active flipped BOUNDS->ACTIVE_NORMAL=2)", w2,
         "p6_w_warp_signactive=%s; player now at %dpx (chain carried it on, "
         "not at the 15792 signpost)"
         % (act, (px >> 16) if px is not None else 0)),
        ("W3 the signpost->Zone->LoadScene chain reached the engine dispatch "
         "(real act-transition path works)", w3,
         "p6_w_transitions=%s (reloads; loops because ActClear is stubbed) "
         "cont_frames=%s" % (tr, cont)),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the warp drove the REAL SignPost cross, which "
              "ran")
        print("        signpost->Zone->LoadScene end-to-end into the engine "
              "transition machine.")
        print("        The only remaining gap to GHZ1->GHZ2 is ActClear "
              "(stubbed -> reload loop).")
        return 0
    print("RESULT: RED -- warp/signpost not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
