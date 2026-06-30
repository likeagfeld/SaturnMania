#!/usr/bin/env python3
# =============================================================================
# qa_p6_aiz_cutscene.py -- M3.1 RED-first gate: the AIZ intro-cutscene DRIVER is
# registered + running, so the AIZ scene positions its OWN camera (cutscene-driven)
# instead of the leftover screens[0].position.x=10676 repeating-fill (the M3.0c
# MEASURED bug). M3.0 proved the engine LOADS folder "AIZ" + renders its FG tilemap;
# M3.1 makes AIZSetup/CutsceneSeq/AIZTornado/AIZTornadoPath run so the camera frames
# the biplane intro. The later beats (claw/ruby/warp) are M3.2/M3.3.
#
# DETERMINISTIC, decoupled from the menu-confirm input: M3.1 forces the AIZ load via
# the P6_AIZ_TEST debug-inject (env P6_AIZ_TEST=1 -> p6_aiz_reload() at boot), the
# SAME pattern F.1/F.2 used (P6_TRANSITION_TEST). So this gate measures the cutscene-
# driver path in isolation.
#
# AUTHORITATIVE SPINE (decomp):
#   AIZTornadoPath_Create START node (AIZTornadoPath.c:29-47) -- grabs SLOT_CAMERA1,
#     sets camera->state=StateMachine_None + camera->position = node->position (:35-36),
#     ScreenInfo->position.y (:43). THE first camera reposition off the leftover x.
#   AIZTornadoPath_HandleMoveSpeed (AIZTornadoPath.c:97-137) -- writes camera->position
#     along the path each frame.
#   AIZSetup_StaticUpdate (AIZSetup.c:16) -> SetupObjects + GetCutsceneSetupPtr ->
#     CutsceneSeq_StartSequence (AIZSetup.c:336); cutscene EnterAIZ (AIZSetup.c:360-361)
#     clamps camera->position.x to ScreenInfo->size.x<<16.
#   CutsceneSeq_LateUpdate (CutsceneSeq.c:21) -- runs the state machine; the active
#     state index is exported as p6_w_aiz_cutscene_state (set in the overlay witness).
#
# Measurable conditions (all must hold) in a P6_AIZ_TEST capture:
#   C1 p6_w_aiz_cutscene_state >= 0  -- CutsceneSeq was instantiated + its LateUpdate
#                                        ran (the AIZSetup cutscene started). The witness
#                                        is -1 before instantiation, >=0 once the seq's
#                                        stateID is read (state 0 = EnterAIZ is running).
#   C2 p6_w_aiz_scrx != 10676        -- the camera became CUTSCENE-DRIVEN. The M3.0c
#                                        leftover was exactly 10676 (an un-positioned
#                                        camera). After AIZTornadoPath_Create grabs +
#                                        positions SLOT_CAMERA1, screens[0].position.x is
#                                        the path-driven value (small intro-region x or
#                                        the EnterAIZ clamp), NOT 10676.
#   C3 p6_w_aiz_setup_classid > 0    -- AIZSetup registered + its static instance live
#      AND p6_w_aiz_seq_classid > 0     (its StaticUpdate ran SetupObjects/StartSequence)
#                                        AND CutsceneSeq registered + instantiated.
#
# RED on the CURRENT build: these witnesses are ABSENT from game.map (no AIZ driver
# registration, no cutscene-state witness) -> "witnesses absent". GREEN once M3.1 lands
# the AIZSetup/CutsceneSeq/AIZTornado/AIZTornadoPath overlay registrations + the
# cutscene-state witness. SCREENSHOT the AIZ background too -- a GREEN gate with a
# black/wrong screen is the proxy trap (the user verifies the biplane-intro framing).
#
#   python tools/_portspike/qa_p6_aiz_cutscene.py             # boot (P6_AIZ_TEST) + capture + verdict
#   python tools/_portspike/qa_p6_aiz_cutscene.py --mcs X.mcs # evaluate an existing capture
#   python tools/_portspike/qa_p6_aiz_cutscene.py --static    # map-only RED check (no capture)
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

# AIZ is a gameplay-tilemap load like GHZ; the shipping load is emulated-CD-IO-bound at
# ~50 s (#251). At SaveFrame 30 the load is STILL in progress (cont_frames=0); the post-
# arm witnesses + the first cutscene ticks latch by ~SaveFrame 80. Capture DEEP so the
# synchronous p6_aiz_reload has returned AND several frontend frames have run the
# cutscene state machine (the camera needs a few frames of AIZTornadoPath to move).
GATE_FRAME = 85.0

ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6_aiz_cutscene.mcs")

# The M3.1 witnesses (DEFINED in p6_io_main.cpp under P6_AIZ_TEST, written by the overlay
# witness / the present block; -u-rooted in build_p6scene_objs.sh).
NAMES = ["_p6_w_aiz_cutscene_state", "_p6_w_aiz_scrx",
         "_p6_w_aiz_setup_classid", "_p6_w_aiz_seq_classid",
         "_p6_w_aiz_tornado_classid", "_p6_w_aiz_path_classid",
         "_p6_w_aiz_cam_x", "_p6_w_frontend_folder_tag"]

LEFTOVER_SCRX = 10676  # the M3.0c un-positioned-camera leftover


def capture(out):
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
        print("M3.1 AIZ CUTSCENE DRIVER -- camera is cutscene-driven (not the 10676 leftover)")
        print("=" * 70)
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        missing = [n for n in NAMES if not present[n]]
        if missing:
            print("-" * 70)
            print("RESULT: RED -- %d/%d M3.1 witnesses ABSENT from game.map. The AIZ "
                  "cutscene-driver registration (AIZSetup/CutsceneSeq/AIZTornado/"
                  "AIZTornadoPath overlay registration + the cutscene-state witness) is "
                  "NOT implemented yet. This is the expected RED baseline for M3.1."
                  % (len(missing), len(NAMES)))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- all M3.1 witnesses present in the map (run "
                  "without --static to force the AIZ load + verify C1-C3).")
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
        ("C1 cutscene state machine ran (cutscene_state>=0)",
         v["_p6_w_aiz_cutscene_state"], lambda x: x is not None and x >= 0),
        ("C2 camera is cutscene-driven (scrx != 10676 leftover)",
         v["_p6_w_aiz_scrx"], lambda x: x is not None and x != LEFTOVER_SCRX),
        ("C3a AIZSetup registered + instantiated (setup_classid>0)",
         v["_p6_w_aiz_setup_classid"], lambda x: x and x > 0),
        ("C3b CutsceneSeq registered + instantiated (seq_classid>0)",
         v["_p6_w_aiz_seq_classid"], lambda x: x and x > 0),
    ]
    print("=" * 70)
    print("M3.1 AIZ CUTSCENE DRIVER -- camera is cutscene-driven (not the 10676 leftover)")
    print("=" * 70)
    print("  context: folder_tag=%s (expect 0x4149 'AI'); cam_x=%s scrx=%s "
          "tornado_cid=%s path_cid=%s"
          % (hex(v["_p6_w_frontend_folder_tag"] & 0xFFFF)
             if isinstance(v["_p6_w_frontend_folder_tag"], int) else v["_p6_w_frontend_folder_tag"],
             Q._dv(v["_p6_w_aiz_cam_x"]), Q._dv(v["_p6_w_aiz_scrx"]),
             Q._dv(v["_p6_w_aiz_tornado_classid"]), Q._dv(v["_p6_w_aiz_path_classid"])))
    ok = True
    for label, val, test in checks:
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-55s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))
    print("-" * 70)
    if ok:
        print("RESULT: GREEN -- the AIZ cutscene driver runs + the camera is cutscene-"
              "positioned (no longer the 10676 leftover). M3.1 done. SCREENSHOT the AIZ "
              "background to pixel-verify the biplane-intro framing (jungle BG, not the "
              "repeating fill). NEXT = M3.2 (claw grab + PhantomRuby), M3.3 (FXRuby warp).")
        return 0
    print("RESULT: RED -- see the RED check. C1 RED = CutsceneSeq never ran (registration "
          "/ closure / StaticUpdate gate). C2 RED = camera still un-positioned (AIZTornado"
          "Path START node did not grab SLOT_CAMERA1 -- StarPost NULL deref? path not "
          "registered?). C3 RED = AIZSetup/CutsceneSeq not registered+instantiated.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
