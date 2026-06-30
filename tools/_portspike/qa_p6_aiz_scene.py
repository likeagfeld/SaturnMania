#!/usr/bin/env python3
# =============================================================================
# qa_p6_aiz_scene.py -- M3.0 RED-first gate: the engine LoadScene + RENDER of the
# AIZ intro-cutscene scene (folder "AIZ", the decomp Cutscenes/"Angel Island Zone"
# target -- MenuSetup.c:1121). M3.0 proves the engine can LOAD a NON-GHZ gameplay
# scene folder and render its FG tilemap; the cutscene MOTION (Tornado fly-in,
# claw, ruby) is the LATER M3.2/M3.3 work.
#
# DETERMINISTIC, decoupled from the 20fps menu-confirm input problem: M3.0 forces
# the AIZ load via the P6_AIZ_TEST debug-inject (env P6_AIZ_TEST=1 -> p6_aiz_reload()
# at boot), the SAME pattern F.1/F.2 used (P6_TRANSITION_TEST) to prove the GHZ1->
# GHZ2 load+arm before wiring the real signpost trigger. So this gate measures the
# LOAD+RENDER path in isolation; the menu->AIZ confirm wiring is gated separately by
# qa_engine_menu_start.py (S3 startscene_tag==0x4149).
#
# AUTHORITATIVE SPINE:
#   p6_aiz_reload (p6_io_main.cpp) -- mirror of p6_title_reload: scan every category
#     range for folder "AIZ", set activeCategory+listPos, arm scanlines, call the
#     generic p6_scene_load_and_arm (the gameplay-tilemap arm runs because p6_isAIZ).
#   p6_scene_load_and_arm p6_isAIZ branch (G3) -- mirror of the p6_isTitle block:
#     upload the AIZ 16x16 cells to NBG1 (the FG render) on a real folder change.
#   ANIMPAK INVARIANT (#228): the front-end flavor compiles OUT the GHZANIM.PAK-at-
#     0x060B6C00 load (#if !defined(P6_FRONTEND_TITLE), p6_io_main.cpp:3665), so AIZ
#     anims are cart-resident -- the menu/AIZ _end (0x060B95E0, ABOVE ANIMPAK) is
#     harmless as long as nothing loads a pack there. This gate does NOT regress that.
#
# Measurable conditions (all must hold) in a P6_AIZ_TEST capture:
#   A1 p6_w_aiz_loaded     == 1       -- p6_scene_load_and_arm finished with the
#                                        CURRENTLY-LOADED folder == "AIZ" (engine
#                                        LoadScene of the non-GHZ folder succeeded).
#   A2 p6_w_aiz_fg_hash    != 0       -- the AIZ NBG1 FG cells were uploaded with
#                                        non-empty content (djb2 of the upload != 0)
#                                        -> the FG tilemap RENDERS (not a blank scene).
#   A3 p6_w_aiz_objcount   >  0       -- the AIZ scene's entities were created (the
#                                        14-class overlay registration resolved; the
#                                        LoadScene did not choke on unregistered types).
#
# RED on the CURRENT build: these witnesses are ABSENT from game.map (no AIZ load
# path, no AIZ overlay registration) -> "witnesses absent". GREEN once M3.0 lands the
# p6_aiz_reload + p6_isAIZ arm + the AIZ overlay registrations + the P6_AIZ_TEST inject.
# SCREENSHOT the AIZ background too (the user verifies pixels).
#
#   python tools/_portspike/qa_p6_aiz_scene.py             # boot (P6_AIZ_TEST) + capture + verdict
#   python tools/_portspike/qa_p6_aiz_scene.py --mcs X.mcs # evaluate an existing capture
#   python tools/_portspike/qa_p6_aiz_scene.py --static    # map-only RED check (no capture)
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

# The AIZ scene is a gameplay-tilemap load (cells + band store) like GHZ, and the GHZ
# shipping load is emulated-CD-IO-bound at ~50s (#251 ghz-load-time). MEASURED: at
# SaveFrame 30 the load is STILL IN PROGRESS (cont_frames=0, PC in engine load code --
# captured too early, NOT a freeze, per shipping-ghz-load-timing); the post-arm witnesses
# latch by ~SaveFrame 75. Capture DEEP so the synchronous p6_aiz_reload has returned.
GATE_FRAME = 75.0          # seconds of emulated boot before the F5 capture (past the ~50s load)

ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6_aiz_scene.mcs")

NAMES = ["_p6_w_aiz_loaded", "_p6_w_aiz_fg_hash", "_p6_w_aiz_objcount",
         "_p6_w_frontend_folder_tag"]


def capture(out):
    # P6_AIZ_TEST=1 forces p6_aiz_reload at boot (the deterministic load inject).
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    env = dict(os.environ, P6_AIZ_TEST="1")
    r = subprocess.run(
        ["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame", str(GATE_FRAME),
         "-Out", out],
        capture_output=True, text=True, env=env)
    sys.stdout.write(r.stdout[-400:] if r.stdout else "")
    sys.stdout.write(r.stderr[-400:] if r.stderr else "")
    return os.path.exists(out)


def main(argv):
    mp = Q.read_text(Q.MAP_DEFAULT)
    present = {n: (Q.map_symbol(mp, n) is not None) for n in NAMES}

    if "--static" in argv or not all(present.values()):
        print("=" * 70)
        print("M3.0 AIZ SCENE -- engine LoadScene + render the AIZ intro-cutscene FG")
        print("=" * 70)
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        missing = [n for n in NAMES if not present[n]]
        if missing:
            print("-" * 70)
            print("RESULT: RED -- %d/%d M3.0 witnesses ABSENT from game.map. The AIZ scene "
                  "load+render path (p6_aiz_reload + p6_isAIZ arm + the AIZ overlay "
                  "registrations + the P6_AIZ_TEST inject) is NOT implemented yet. This is "
                  "the expected RED baseline for M3.0." % (len(missing), len(NAMES)))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- all M3.0 witnesses present in the map (run without "
                  "--static to force the AIZ load + verify A1-A3).")
            return 0

    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else TMP_MCS
    if "--mcs" not in argv and not capture(mcs):
        print("FAIL: no savestate"); return 1
    mod = Q.load_harness()
    sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    if perm is None:
        print("RED: magic uncalibrated"); return 1
    v = {n: Q.peek_u32(mod, sec, Q.map_symbol(mp, n), perm, signed=True) for n in NAMES}

    checks = [
        ("A1 engine LoadScene of folder AIZ succeeded (aiz_loaded==1)",   v["_p6_w_aiz_loaded"],   lambda x: x == 1),
        ("A2 AIZ FG cells uploaded with content (fg_hash!=0)",            v["_p6_w_aiz_fg_hash"],   lambda x: x not in (0, None)),
        ("A3 AIZ scene entities created (objcount>0)",                    v["_p6_w_aiz_objcount"],  lambda x: x and x > 0),
    ]
    print("=" * 70)
    print("M3.0 AIZ SCENE -- engine LoadScene + render the AIZ intro-cutscene FG")
    print("=" * 70)
    print("  context: frontend_folder_tag=%s (expect 0x4149 'AI' after the AIZ load)"
          % (hex(v["_p6_w_frontend_folder_tag"] & 0xFFFF)
             if isinstance(v["_p6_w_frontend_folder_tag"], int) else v["_p6_w_frontend_folder_tag"]))
    ok = True
    for label, val, test in checks:
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-58s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))
    print("-" * 70)
    if ok:
        print("RESULT: GREEN -- the engine LOADED the AIZ folder + rendered its FG tilemap. "
              "M3.0 done. NEXT = M3.1 (BG parallax + AIZ palette cycle), then M3.2 (cutscene "
              "motion). Open the AIZ-background screenshot to pixel-verify.")
        return 0
    print("RESULT: RED -- see the RED check above. A1 RED = the load did not finish on folder "
          "AIZ (p6_aiz_reload scan or p6_scene_load_and_arm failed). A2 RED = loaded but FG "
          "cells empty (cell-upload path / AIZ tileset). A3 RED = entities not created "
          "(overlay registration closure).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
