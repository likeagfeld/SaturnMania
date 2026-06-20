#!/usr/bin/env python3
# =============================================================================
# qa_engine_logos.py -- CP4 (FRONT-END KEYSTONE) RED-first gate: the engine LoadScene's a
# NON-GHZ scene (Logos) through the SAME proven path that runs GHZ.
#
# WHY (the keystone): the P6.8 shipping build runs GHZ via the engine scene-load + ENGINESTATE_LOAD
# transition machine (p6_scene_load_and_arm). The scene SELECT is generic (by folder name, p6_io_main.cpp
# :3325) and the LOAD is generic (:4166) -- only the ARM (FG band-store + collision windows, :4113) is
# GHZ-tilemap-specific. CP4 proves a non-gameplay UI scene (Logos = the Sega/RSDK splash) loads + its
# objects instantiate through that path. Once GREEN, Title and Menu load by the SAME LoadScene path
# (just a different folder), unblocking the whole front-end (title/menus/intro/transitions).
#
# THE PORT THIS GATES: LogoSetup.c (158 L) + UIPicture.c (114 L) from tools/_decomp_raw/
# (SonicMania_Objects_Menu_{LogoSetup,UIPicture}.c) into a front-end cart overlay, registered via the
# proven register_object_full ABI; boot-select folder "Logos"; the FG/collision arm no-ops for the
# non-tilemap scene; UIPicture renders via DrawSprite->VDP1 + LogoSetup fades via FillScreen.
#
# Measurable conditions (all must hold) in a non-GHZ-scene capture:
#   E1  p6_w_frontend_folder_tag != 0   -- a NON-GHZ scene folder is the loaded/active scene (not "GHZ").
#                                          The impl sets this to a tag of sceneInfo.listData[listPos].folder
#                                          (e.g. 'L'<<8|'o' for Logos) the same place the GHZ select runs.
#   E2  p6_w_logosetup_classid  > 0     -- LogoSetup registered + resolved a live classID (the port linked).
#   E3  p6_w_uipicture_classid  > 0     -- UIPicture registered + resolved (the logo-picture object).
#   E4  p6_w_logos_objcount     > 0     -- the Logos Scene.bin placements instantiated (entities created
#                                          through the generic LoadScene chain for a non-GHZ scene).
#   E5  p6_w_cont_frames        > 0     -- the engine reached ENGINESTATE_REGULAR + ticks (didn't hang on
#                                          the non-GHZ load / the arm didn't fault on the missing FG layer).
#
# RED on the CURRENT shipping build: these witnesses are ABSENT from game.map (the front-end port + the
# non-GHZ select do not exist yet) -> the gate REDs on "witnesses absent". GREEN once the port lands and a
# Logos capture satisfies E1-E5. SCREENSHOT the capture too (the UI render is a VISUAL result -- a gate
# witness proves load+create, the screenshot proves the splash actually draws; see the binding visual rule).
#
#   python tools/_portspike/qa_engine_logos.py              # boot + capture + verdict
#   python tools/_portspike/qa_engine_logos.py --mcs X.mcs  # evaluate an existing capture
#   python tools/_portspike/qa_engine_logos.py --static     # map-only RED check (no capture; CI-fast)
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 60.0           # the Logos splash is early-boot; a non-GHZ scene needs no deep GHZ load
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_engine_logos.mcs")

NAMES = ["_p6_w_frontend_folder_tag", "_p6_w_logosetup_classid", "_p6_w_uipicture_classid",
         "_p6_w_logos_objcount", "_p6_w_cont_frames"]


def capture(out):
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame", str(GATE_FRAME), "-Out", out],
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout[-200:] if r.stdout else "")
    return os.path.exists(out)


def main(argv):
    mp = Q.read_text(Q.MAP_DEFAULT)
    present = {n: (Q.map_symbol(mp, n) is not None) for n in NAMES}

    # STATIC RED: if the front-end witnesses are not even in the map, CP4 is not implemented -> RED fast.
    if "--static" in argv or not all(present.values()):
        print("=" * 64)
        print("CP4 FRONT-END KEYSTONE -- engine LoadScene of a NON-GHZ (Logos) scene")
        print("=" * 64)
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        missing = [n for n in NAMES if not present[n]]
        if missing:
            print("-" * 64)
            print("RESULT: RED -- %d/%d front-end witnesses ABSENT from game.map. The non-GHZ scene-load "
                  "(LogoSetup+UIPicture port + boot-select 'Logos' + arm-graceful) is NOT implemented yet. "
                  "This is the expected RED baseline for CP4." % (len(missing), len(NAMES)))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- all front-end witnesses present in the map (run without "
                  "--static to verify the runtime values E1-E5).")
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
        ("E1 a NON-GHZ scene is the active scene (folder_tag != 0)", v["_p6_w_frontend_folder_tag"], lambda x: x not in (None, 0)),
        ("E2 LogoSetup registered + classID resolved (>0)",          v["_p6_w_logosetup_classid"],   lambda x: x and x > 0),
        ("E3 UIPicture registered + classID resolved (>0)",          v["_p6_w_uipicture_classid"],   lambda x: x and x > 0),
        ("E4 Logos Scene.bin placements instantiated (objcount>0)",  v["_p6_w_logos_objcount"],      lambda x: x and x > 0),
        ("E5 engine reached ENGINESTATE_REGULAR + ticks (cont>0)",   v["_p6_w_cont_frames"],         lambda x: x and x > 0),
    ]
    print("=" * 64)
    print("CP4 FRONT-END KEYSTONE -- engine LoadScene of a NON-GHZ (Logos) scene")
    print("=" * 64)
    ok = True
    for label, val, test in checks:
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-54s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))
    print("-" * 64)
    if ok:
        print("RESULT: GREEN -- a NON-GHZ scene loaded + its objects instantiated through the engine "
              "LoadScene path. The front-end is now reachable: Title/Menu load by the SAME path. "
              "(SCREENSHOT the capture -- the splash render is a VISUAL result the witnesses cannot prove.)")
        return 0
    print("RESULT: RED -- see the RED check above. The non-GHZ scene did not load/create/run on the "
          "engine path (LogoSetup+UIPicture port + boot-select 'Logos' + arm-graceful).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
