#!/usr/bin/env python3
# =============================================================================
# qa_engine_menu_start.py -- M2 RED-first gate: the START-GAME path. Navigate the
# Mania main menu, select Mania Mode -> a new save slot -> RSDK.SetScene("Cutscenes",
# "Angel Island Zone") fires (the biplane intro). The AIZ scene RENDER is the NEXT
# checkpoint (M3); this gate's GREEN is the SetScene FIRING for AIZ, proven by a
# savestate witness.
#
# AUTHORITATIVE SPINE (non-Plus MANIA_USE_PLUS=0):
#   UIModeButton_SelectedCB (UIModeButton.c:189-218) -> UITransition_StartTransition(
#     self->actionCB=MenuSetup_MenuButton_ActionCB, 14); the transition runs the actionCB
#     in UITransition_State_TransitionOut -> MenuSetup_MenuButton_ActionCB case 0 (Mania
#     Mode, MenuSetup.c:926-934) -> UIControl_MatchMenuTag("Save Select").
#   UISaveSlot confirm (UISaveSlot_ProcessButtonCB:916 anyConfirmPress -> SelectedCB ->
#     State_Selected grows fxRadius -> at >0x200 runs actionCB=MenuSetup_SaveSlot_ActionCB)
#     -> RSDK.SetScene("Cutscenes","Angel Island Zone") (MenuSetup.c:1121).
#   RSDK::SetScene (Scene.cpp:1530-1553) sets sceneInfo.activeCategory + listPos by md5;
#     the AIZ scene's folder is "AIZ" (GameConfig verified) -> folder tag 'A'<<8|'I'=0x4149.
#
# Measurable conditions (all must hold) in a Menu-scene capture AFTER an injected
# confirm sequence:
#   S1 p6_w_menu_saveslot_classid > 0     -- UISaveSlot registered + instantiated
#                                            (the start-game slot widget is ported).
#   S2 p6_w_menu_input_seen      != 0     -- a Saturn-pad press reached the live UIControl
#                                            (sticky OR of UIControl any*Press flags).
#   S3 p6_w_menu_startscene_tag  == 0x4149 ('AI') -- the start-game SetScene fired for the
#                                            AIZ scene (folder "AIZ"). (start_cat/listpos
#                                            are diagnostic context for the verdict line.)
#
# RED on the CURRENT build: the witnesses are ABSENT from game.map (no UISaveSlot, no
# start-game path) -> "witnesses absent". GREEN once the port lands AND the injected
# confirm burst reaches the AIZ SetScene. SCREENSHOT the save-select screen too.
#
#   python tools/_portspike/qa_engine_menu_start.py            # boot + inject + capture + verdict
#   python tools/_portspike/qa_engine_menu_start.py --mcs X.mcs  # evaluate an existing capture
#   python tools/_portspike/qa_engine_menu_start.py --static   # map-only RED check (no capture)
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

# Menu is sprite-only (light load). The front-end load settles by ~Wait 20; the menu
# auth-gate flips + the row tree builds within a few frames after that. The select
# sequence needs: ~confirm Mania Mode -> the UITransition wipe (~30f) -> Save Select ->
# confirm a slot -> UISaveSlot_State_Selected fxRadius ramp (~40f after timer>32). Drive
# a START burst from t~22s and capture DEEP (Wait 36) so the AIZ SetScene has fired.
GATE_FRAME    = 36.0       # seconds of emulated boot before the F5 capture
PRESS_START_AT = 22.0      # begin the confirm burst once the menu is up + settled
PRESS_COUNT    = 10        # enough confirms to walk Mania Mode -> slot -> start
PRESS_EVERY    = 1.2       # spacing (each press = one menu-tick sample)
AIZ_TAG  = 0x4149          # 'A'<<8 | 'I' -- the AIZ scene folder "AIZ"

ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
QABOOT    = os.path.join(ROOT, "tools", "qa_boot.ps1")
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_engine_menu_start.mcs")
TMP_PNG   = os.path.join(HERE, "_engine_menu_start.png")

NAMES = ["_p6_w_menu_saveslot_classid", "_p6_w_menu_input_seen",
         "_p6_w_menu_startscene_tag", "_p6_w_menu_start_cat", "_p6_w_menu_start_listpos",
         "_p6_w_menusetup_classid", "_p6_w_uicontrol_classid"]


def capture(out):
    # qa_savestate captures a savestate at -SaveFrame; it injects START via the same
    # -PressStartAt/-PressCount/-PressEvery flags qa_boot uses (the scancode-injection
    # harness). The confirm burst walks Mania Mode -> a save slot -> the AIZ SetScene.
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    r = subprocess.run(
        ["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame", str(GATE_FRAME),
         "-Out", out, "-PressStartAt", str(PRESS_START_AT),
         "-PressCount", str(PRESS_COUNT), "-PressEvery", str(PRESS_EVERY)],
        capture_output=True, text=True)
    sys.stdout.write(r.stdout[-400:] if r.stdout else "")
    sys.stdout.write(r.stderr[-400:] if r.stderr else "")
    return os.path.exists(out)


def main(argv):
    mp = Q.read_text(Q.MAP_DEFAULT)
    present = {n: (Q.map_symbol(mp, n) is not None) for n in NAMES}

    if "--static" in argv or not all(present.values()):
        print("=" * 70)
        print("M2 START-GAME -- menu navigate + select -> SetScene(Cutscenes/Angel Island Zone)")
        print("=" * 70)
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        missing = [n for n in NAMES if not present[n]]
        if missing:
            print("-" * 70)
            print("RESULT: RED -- %d/%d M2 witnesses ABSENT from game.map. The start-game path "
                  "(UISaveSlot + UITransition port + the SetScene latch) is NOT implemented yet. "
                  "This is the expected RED baseline for M2." % (len(missing), len(NAMES)))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- all M2 witnesses present in the map (run without "
                  "--static to drive the select sequence + verify S1-S3).")
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
        ("S1 UISaveSlot registered + instantiated (classID>0)",          v["_p6_w_menu_saveslot_classid"], lambda x: x and x > 0),
        ("S2 a Saturn-pad press reached the live UIControl (seen!=0)",    v["_p6_w_menu_input_seen"],       lambda x: x == 1),
        ("S3 start-game SetScene fired for AIZ (folder tag==0x4149 'AI')", v["_p6_w_menu_startscene_tag"],  lambda x: x == AIZ_TAG),
    ]
    print("=" * 70)
    print("M2 START-GAME -- menu navigate + select -> SetScene(Cutscenes/Angel Island Zone)")
    print("=" * 70)
    print("  context: MenuSetup classID=%s UIControl classID=%s start_cat=%s start_listpos=%s "
          "startscene_tag=%s"
          % (Q._dv(v["_p6_w_menusetup_classid"]), Q._dv(v["_p6_w_uicontrol_classid"]),
             Q._dv(v["_p6_w_menu_start_cat"]), Q._dv(v["_p6_w_menu_start_listpos"]),
             hex(v["_p6_w_menu_startscene_tag"] & 0xFFFF) if isinstance(v["_p6_w_menu_startscene_tag"], int) else v["_p6_w_menu_startscene_tag"]))
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
        print("RESULT: GREEN -- the select sequence (Mania Mode -> save slot) reached "
              "RSDK.SetScene(\"Cutscenes\",\"Angel Island Zone\"). M2 done. NEXT = M3 (port the AIZ "
              "biplane cutscene scene so that SetScene actually LOADS + RENDERS).")
        return 0
    print("RESULT: RED -- see the RED check above. If S1/S2 GREEN but S3 RED, the navigation "
          "reached the menu but the confirm sequence did not fire the AIZ SetScene -- MEASURE "
          "which ActionCB ran (start_cat/listpos/startscene_tag context above) before adjusting "
          "the injected sequence.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
