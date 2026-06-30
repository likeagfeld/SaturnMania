#!/usr/bin/env python3
# =============================================================================
# qa_ghzcut_load.py -- RED-first gate for Task #309 gate-1: the AIZ -> GHZCutscene
# destination scene LOADS (and, in --phase handoff, the cutscene reaches its
# SetupGHZ1 beat -> playable Green Hill Zone).
#
# Mechanism mirrors tools/_portspike/_aiz_handoff.py: parse a Mednafen savestate
# via the qa_p6_scene harness and read the engine global `currentSceneFolder`
# (Scene.cpp) -- the authoritative "what scene is live" string. WRAM-H is read
# with the #136 16-bit-pair byteswap.
#
# Verdicts:
#   --phase load     PASS iff currentSceneFolder == "GHZCutscene"  (scene loaded)
#   --phase handoff  PASS iff currentSceneFolder == "GHZ"          (reached playable GHZ)
#   (default)        PASS iff folder in {GHZCutscene, GHZ}
# Plus a not-captured-too-early sanity check on cont_frames (> 0).
#
# RED on the CURRENT build: no GHZCutscene path exists yet, so any capture reads
# folder "AIZ"/"Menu"/"" -> exit 1. GREEN once P6_GHZCUT_BOOT (or the live AIZ
# handoff) lands the scene.
# =============================================================================
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__))))
import qa_p6_scene as Q  # noqa: E402

# currentSceneFolder address fallback (hardcoded in _aiz_handoff.py for the
# front-end flavor). Prefer the map symbol so an _end shift can't desync it.
CURFOLDER_FALLBACK = 0x06094614


def read_curfolder(mod, sec, mp):
    sym = None
    for n in ("_currentSceneFolder", "currentSceneFolder"):
        sym = Q.map_symbol(mp, n)
        if sym:
            break
    addr = sym if sym else CURFOLDER_FALLBACK
    b = mod._peek_bytes(sec, addr, 16)
    if not b:
        return None
    sw = bytes(bytes(b)[i ^ 1] for i in range(len(b)))  # #136 pair-swap
    return sw.split(b"\x00")[0].decode("latin1", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("state")
    ap.add_argument("--phase", choices=["load", "handoff", "any"], default="any")
    args = ap.parse_args()

    mp = Q.read_text(Q.MAP_DEFAULT)
    mod = Q.load_harness()
    sec = mod.parse_savestate(Q._as_path(args.state))

    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    # ROBUSTNESS (Task #309 follow-up): if the magic witness does not match any
    # known WRAM byte-permutation, the savestate was captured against a build
    # whose game.map differs from the current root game.map -> the cont_frames
    # decode_u32 would crash on perm=None (AttributeError perm.index). Treat that
    # as a hard, explicit error: a mismatched-map capture is NOISE (skill gotcha
    # #3), never silently "GREEN". Recapture from the freshly-built binary.
    if perm is None:
        print("=== qa_ghzcut_load (phase=%s) ===" % args.phase)
        print("  RED: magic-calibration FAILED -- the savestate's WRAM does not")
        print("       match the current game.map (p6_w_magic mismatch). The state")
        print("       was captured against a STALE build. Rebuild + recapture so")
        print("       map<->state correspond, then re-run this gate.")
        return 2

    folder = read_curfolder(mod, sec, mp)
    cf_sym = Q.map_symbol(mp, "_p6_w_cont_frames")
    cont = Q.peek_u32(mod, sec, cf_sym, perm, signed=True) if cf_sym else None

    print("=== qa_ghzcut_load (phase=%s) ===" % args.phase)
    print("  currentSceneFolder = %r" % folder)
    print("  cont_frames        = %s" % cont)

    want = {"load": {"GHZCutscene"}, "handoff": {"GHZ"},
            "any": {"GHZCutscene", "GHZ"}}[args.phase]

    fails = []
    if folder not in want:
        fails.append("folder %r not in %s" % (folder, sorted(want)))
    if cont is not None and cont <= 0:
        fails.append("cont_frames %s <= 0 (captured too early / frozen)" % cont)

    print("\n=== VERDICT ===")
    if fails:
        for f in fails:
            print("  RED: " + f)
        return 1
    print("  GREEN: GHZCutscene handoff scene live (folder=%r)" % folder)
    return 0


if __name__ == "__main__":
    sys.exit(main())
