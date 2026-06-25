#!/usr/bin/env python3
# =============================================================================
# qa_engine_menu.py -- M1 RED-first gate: the engine LoadScene's the MENU scene
# (category "Presentation", folder "Menu") through the SAME proven path that runs
# GHZ + the CP4 Logos scene + the CP5a Title scene.
#
# WHY (front-end chain, link 3 of logos->title->MENU->intro->GHZ): CP5a proved the
# TITLE scene loads+runs on the engine (qa_engine_title.py T1-T5). M1 extends that
# to the MENU scene -- the precursor the user wants reachable so Mania-Mode -> new
# save -> the biplane intro (Cutscenes/Angel Island Zone, AIZTornado -- an ENTITY
# cutscene, NOT an FMV) -> GHZ1 becomes reachable (see memory menu-biplane-ghz-flow
# -verified). Authoritative spine: TitleSetup -> SetScene("Presentation","Menu")
# (TitleSetup.c:325,331); MenuSetup is the non-Plus director (MANIA_USE_PLUS=0);
# MenuSetup_SaveSlot_ActionCB:1121 fires SetScene("Cutscenes","Angel Island Zone").
# Menu is a sprite-only UI scene (no tilemap/FG/collision) -> light load, the GHZ
# arm steps no-op behind p6_isGHZ==0.
#
# Measurable conditions (all must hold) in a Menu-scene capture:
#   M1  p6_w_frontend_folder_tag == 0x4D65  -- 'M'<<8|'e': the MENU folder is the
#                                              loaded/active scene (not GHZ/Logos/Title).
#   M2  p6_w_menusetup_classid    > 0       -- MenuSetup registered + live classID.
#   M3  p6_w_uicontrol_classid    > 0       -- UIControl registered + live classID.
#   M4  p6_w_menu_objcount        > 0       -- the Menu Scene.bin placements (UIControl
#                                              /UIBackground/UIButton) instantiated through
#                                              the SAME generic LoadScene chain.
#   M5  p6_w_cont_frames          > 0       -- engine reached ENGINESTATE_REGULAR + ticks.
#
# RED on the CURRENT build: these witnesses are ABSENT from game.map (the Menu port +
# "Menu" boot-select do not exist yet) -> "witnesses absent". GREEN once the port lands
# and a Menu capture satisfies M1-M5. SCREENSHOT too (the menu render -- UIBackground +
# UIControl rows drawing -- is a VISUAL result the witnesses cannot prove; M1a proves
# load+run, M1b proves it draws, per the binding visual rule).
#
#   python tools/_portspike/qa_engine_menu.py              # boot + capture + verdict
#   python tools/_portspike/qa_engine_menu.py --mcs X.mcs  # evaluate an existing capture
#   python tools/_portspike/qa_engine_menu.py --static     # map-only RED check (no capture)
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 90.0           # Menu is sprite-only (light load); 90 lands after settle
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_engine_menu.mcs")
MENU_TAG  = 0x4D65          # 'M'<<8 | 'e'

NAMES = ["_p6_w_frontend_folder_tag", "_p6_w_menusetup_classid", "_p6_w_uicontrol_classid",
         "_p6_w_menu_objcount", "_p6_w_cont_frames"]


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

    if "--static" in argv or not all(present.values()):
        print("=" * 64)
        print("M1 FRONT-END -- engine LoadScene of the MENU scene")
        print("=" * 64)
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        missing = [n for n in NAMES if not present[n]]
        if missing:
            print("-" * 64)
            print("RESULT: RED -- %d/%d Menu witnesses ABSENT from game.map. The Menu scene-load "
                  "(MenuSetup+UIControl port + boot-select 'Menu') is NOT implemented yet. "
                  "This is the expected RED baseline for M1." % (len(missing), len(NAMES)))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- all Menu witnesses present in the map (run without "
                  "--static to verify the runtime values M1-M5).")
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
        ("M1 the MENU folder is the active scene (tag==0x4D65 'Me')", v["_p6_w_frontend_folder_tag"], lambda x: x == MENU_TAG),
        ("M2 MenuSetup registered + classID resolved (>0)",          v["_p6_w_menusetup_classid"],   lambda x: x and x > 0),
        ("M3 UIControl registered + classID resolved (>0)",          v["_p6_w_uicontrol_classid"],   lambda x: x and x > 0),
        ("M4 Menu Scene.bin placements instantiated (objcount>0)",   v["_p6_w_menu_objcount"],       lambda x: x and x > 0),
        ("M5 engine reached ENGINESTATE_REGULAR + ticks (cont>0)",   v["_p6_w_cont_frames"],         lambda x: x and x > 0),
    ]
    print("=" * 64)
    print("M1 FRONT-END -- engine LoadScene of the MENU scene")
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
        print("RESULT: GREEN -- the MENU scene loaded + its objects instantiated + ran through the "
              "engine LoadScene path. Front-end link 3 proven; M2 = navigate + start-game "
              "(UISaveSlot -> SetScene Cutscenes/Angel Island Zone). SCREENSHOT the capture -- the "
              "menu render (UIBackground + rows) is a VISUAL result the witnesses cannot prove.")
        return 0
    print("RESULT: RED -- see the RED check above. The Menu scene did not load/create/run on the "
          "engine path (MenuSetup+UIControl port + boot-select 'Menu').")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
