#!/usr/bin/env python3
# =============================================================================
# qa_engine_title.py -- CP5a RED-first gate: the engine LoadScene's the TITLE scene
# (folder "Title") through the SAME proven path that runs GHZ + the CP4 Logos scene.
#
# WHY (the front-end chain, link 2 of logos->title->menu->GHZ): CP4 proved a non-GHZ
# UI scene (Logos) loads + its objects instantiate through the generic engine LoadScene
# path (p6_scene_load_and_arm; the GHZ-tilemap arm steps no-op behind p6_isGHZ). CP5a
# extends that to the TITLE scene -- the precursor menu/title the user wants reachable.
# Title has NO tilemap/FG/collision (a sprite-only scene), so the load is light (no GHZ
# ~52s GFS-bound tileset inflate). Booting DIRECTLY into "Title" (P6_FRONTEND_TITLE)
# gives a deterministic capture in TitleSetup_State_Wait (timer 1024 -> -1024 by 16/frame
# = ~128 frames before the first foreach_all(TitleLogo) fires), so the keystone is gated
# without any dependency on Logos-play timing. The Logos->Title AUTO-ADVANCE chain-wire
# (frontend_frame's ENGINESTATE_LOAD handler, currently swallowed at p6_io_main.cpp:4657)
# lifts in a later step once Title is independently proven here.
#
# THE PORT THIS GATES: TitleSetup.c (417 L) + TitleLogo.c (343 L) from tools/_decomp_raw/
# (SonicMania_Objects_Title_{TitleSetup,TitleLogo}.c) into the front-end cart overlay,
# registered via the proven register_object_full ABI; boot-select folder "Title"; the
# FG/collision arm no-ops (p6_isGHZ==0); TitleSetup self-places at slot 0 + drives the
# state machine, TitleLogo carries the Scene1.bin logo-piece placements (the objcount
# evidence). TitleSonic/TitleBG/Title3DSprite + the full logo RENDER are CP5b+.
#
# Measurable conditions (all must hold) in a Title-scene capture:
#   T1  p6_w_frontend_folder_tag == 0x5469  -- 'T'<<8|'i': the TITLE folder is the
#                                              loaded/active scene (not GHZ, not Logos).
#   T2  p6_w_titlesetup_classid   > 0       -- TitleSetup registered + live classID.
#   T3  p6_w_titlelogo_classid    > 0       -- TitleLogo registered + live classID.
#   T4  p6_w_title_objcount       > 0       -- the Title Scene1.bin placements (TitleLogo
#                                              logo pieces) instantiated through the SAME
#                                              generic LoadScene chain GHZ + Logos use.
#   T5  p6_w_cont_frames          > 0       -- engine reached ENGINESTATE_REGULAR + ticks
#                                              (the arm did not fault on the missing FG).
#
# RED on the CURRENT shipping build: these witnesses are ABSENT from game.map (the Title
# port + the "Title" boot-select do not exist yet) -> the gate REDs "witnesses absent".
# GREEN once the port lands and a Title capture satisfies T1-T5. SCREENSHOT the capture too
# (the title render is a VISUAL result the witnesses cannot prove; CP5a proves load+run,
# CP5b proves the logo actually draws -- per the binding visual rule).
#
#   python tools/_portspike/qa_engine_title.py              # boot + capture + verdict
#   python tools/_portspike/qa_engine_title.py --mcs X.mcs  # evaluate an existing capture
#   python tools/_portspike/qa_engine_title.py --static     # map-only RED check (no capture)
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 90.0           # Title is sprite-only (light load); 90 lands inside State_Wait
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_engine_title.mcs")
TITLE_TAG = 0x5469          # 'T'<<8 | 'i'

NAMES = ["_p6_w_frontend_folder_tag", "_p6_w_titlesetup_classid", "_p6_w_titlelogo_classid",
         "_p6_w_title_objcount", "_p6_w_cont_frames"]


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

    # STATIC RED: if the Title witnesses are not even in the map, CP5a is not implemented.
    if "--static" in argv or not all(present.values()):
        print("=" * 64)
        print("CP5a FRONT-END -- engine LoadScene of the TITLE scene")
        print("=" * 64)
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        missing = [n for n in NAMES if not present[n]]
        if missing:
            print("-" * 64)
            print("RESULT: RED -- %d/%d Title witnesses ABSENT from game.map. The Title scene-load "
                  "(TitleSetup+TitleLogo port + boot-select 'Title') is NOT implemented yet. "
                  "This is the expected RED baseline for CP5a." % (len(missing), len(NAMES)))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- all Title witnesses present in the map (run without "
                  "--static to verify the runtime values T1-T5).")
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
        ("T1 the TITLE folder is the active scene (tag==0x5469 'Ti')", v["_p6_w_frontend_folder_tag"], lambda x: x == TITLE_TAG),
        ("T2 TitleSetup registered + classID resolved (>0)",          v["_p6_w_titlesetup_classid"],  lambda x: x and x > 0),
        ("T3 TitleLogo registered + classID resolved (>0)",           v["_p6_w_titlelogo_classid"],   lambda x: x and x > 0),
        ("T4 Title Scene.bin placements instantiated (objcount>0)",   v["_p6_w_title_objcount"],      lambda x: x and x > 0),
        ("T5 engine reached ENGINESTATE_REGULAR + ticks (cont>0)",    v["_p6_w_cont_frames"],         lambda x: x and x > 0),
    ]
    print("=" * 64)
    print("CP5a FRONT-END -- engine LoadScene of the TITLE scene")
    print("=" * 64)
    ok = True
    for label, val, test in checks:
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-56s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))
    print("-" * 64)
    if ok:
        print("RESULT: GREEN -- the TITLE scene loaded + its objects instantiated + ran through the "
              "engine LoadScene path. Front-end link 2 proven: Menu loads by the SAME path; the "
              "Logos->Title auto-advance chain-wire is the next step. (SCREENSHOT the capture -- the "
              "title render is a VISUAL result the witnesses cannot prove.)")
        return 0
    print("RESULT: RED -- see the RED check above. The Title scene did not load/create/run on the "
          "engine path (TitleSetup+TitleLogo port + boot-select 'Title').")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
