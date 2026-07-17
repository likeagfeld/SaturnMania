#!/usr/bin/env python3
# =============================================================================
# qa_menu_rows.py -- CHAIN menu RED-first gate: at chain folder==Menu the
# main-menu button rows are CENTERED ON-SCREEN, not left at raw world coords
# off the 320-wide display.
#
# WHY (MEASURED live 2026-07-17, build 96481d7, chain P6_GHZCUT_BOOT, folder==Menu):
#   memory [[chain-menu-rows-offscreen-scroll-2026-07-17]]. The menu LOGIC is
#   fully alive (p6_w_menu_ctrl_count=27, active_btn_count=5, Mania Mode id=0
#   selected) but the active UIModeButton is at WORLD (756,358) with SCREEN
#   position IDENTICAL (756,358) -> currentScreen->position (p6_w_menu_scrx) is
#   0, i.e. the world->screen scroll origin is NOT centered on the active
#   control. Only 12 VDP1 cmds -> the rows render off the 320-wide screen.
#
#   Decomp contract (UIControl.c:52-53, UIControl_Draw):
#     ScreenInfo->position = FROM_FIXED(activeControl->position) - center
#   so the active control lands at screen center (160,112). The chain
#   (P6_GHZCUT_BOOT) never consumed the overlay-computed p6_w_menu_force_scrx
#   into currentScreen->position (the pack consumer was gated behind the
#   undefined P6_MENU_LAYOUT320). The fix forces currentScreen->position each
#   chain menu frame; this gate proves it RED->GREEN.
#
# Measurable conditions (RED on the un-forced build, GREEN after the force):
#   R1  active row (bid==0, Mania Mode) screen sx in [0,320) and sy in [0,224).
#   R2  scroll origin p6_w_menu_scrx != 0  (currentScreen->position centered).
#   R3  all VALID UIModeButton rows on-screen: 0<=sx<320, 0<=sy<224.
#
#   python tools/_portspike/qa_menu_rows.py             # boot + capture + verdict
#   python tools/_portspike/qa_menu_rows.py --mcs X.mcs # use an existing state
#   python tools/_portspike/qa_menu_rows.py --static    # map-only (witnesses present?)
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 90.0
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_menu_rows.mcs")

SCREEN_W, SCREEN_H = 320, 224

SCALARS = ["_p6_w_menu_scrx", "_p6_w_menu_scry", "_p6_w_menu_ctrl_count",
           "_p6_w_menu_modebtn_classid"]
ARRAYS  = ["_p6_w_menu_modebtn_px", "_p6_w_menu_modebtn_py",
           "_p6_w_menu_modebtn_sx", "_p6_w_menu_modebtn_sy",
           "_p6_w_menu_modebtn_act", "_p6_w_menu_modebtn_bid"]


def capture_mcs(out):
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
    allnames = SCALARS + ARRAYS
    present = {n: (Q.map_symbol(mp, n) is not None) for n in allnames}
    missing = [n for n in allnames if not present[n]]

    print("=" * 70)
    print("CHAIN menu rows -- the main-menu buttons land CENTERED ON-SCREEN")
    print("=" * 70)

    if missing:
        for n in missing:
            print("  [ABSENT] witness %s" % n)
        print("-" * 70)
        print("RESULT: RED -- %d/%d row witnesses ABSENT from game.map."
              % (len(missing), len(allnames)))
        return 1
    if "--static" in argv:
        print("RESULT: GREEN(static) -- all row witnesses present in game.map.")
        return 0

    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else TMP_MCS
    if "--mcs" not in argv and not capture_mcs(mcs):
        print("FAIL: no savestate"); return 1
    mod = Q.load_harness()
    sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    if perm is None:
        print("RED: magic uncalibrated"); return 1

    def rd(name):
        return Q.peek_u32(mod, sec, Q.map_symbol(mp, name), perm, signed=True)

    def rd_arr(name, n=4):
        base = Q.map_symbol(mp, name)
        return [Q.peek_u32(mod, sec, base + 4 * i, perm, signed=True) for i in range(n)]

    scrx = rd("_p6_w_menu_scrx"); scry = rd("_p6_w_menu_scry")
    px = rd_arr("_p6_w_menu_modebtn_px"); py = rd_arr("_p6_w_menu_modebtn_py")
    sx = rd_arr("_p6_w_menu_modebtn_sx"); sy = rd_arr("_p6_w_menu_modebtn_sy")
    act = rd_arr("_p6_w_menu_modebtn_act"); bid = rd_arr("_p6_w_menu_modebtn_bid")

    print("  scroll origin currentScreen->position = (%s, %s)" % (Q._dv(scrx), Q._dv(scry)))
    print("  UIModeButton rows (world px -> screen px):")
    names = {0: "ManiaMode", 1: "TimeAttack", 2: "Competition", 3: "Options"}
    for i in range(4):
        nm = names.get(bid[i], "bid=%s" % Q._dv(bid[i]))
        print("    row[%d] %-11s world(%s,%s) -> screen(%s,%s)  active=%s"
              % (i, nm, Q._dv(px[i]), Q._dv(py[i]), Q._dv(sx[i]), Q._dv(sy[i]), Q._dv(act[i])))

    valid = [i for i in range(4) if sx[i] > -900000 and sy[i] > -900000]
    ok = True

    # R2: the scroll origin is centered (non-zero).
    r2 = (scrx != 0)
    ok = ok and r2
    print("  [%s] R2  scroll origin p6_w_menu_scrx != 0 (currentScreen centered) = %s"
          % ("GREEN" if r2 else " RED ", Q._dv(scrx)))

    # R1: the ACTIVE row (Mania Mode, bid==0) is on-screen.
    active_i = next((i for i in valid if bid[i] == 0), None)
    if active_i is None:
        r1 = False
        print("  [ RED ] R1  Mania Mode row (bid==0) not found among valid rows")
    else:
        ax, ay = sx[active_i], sy[active_i]
        r1 = (0 <= ax < SCREEN_W and 0 <= ay < SCREEN_H)
        print("  [%s] R1  Mania Mode row screen (%s,%s) in [0,%d)x[0,%d)"
              % ("GREEN" if r1 else " RED ", Q._dv(ax), Q._dv(ay), SCREEN_W, SCREEN_H))
    ok = ok and r1

    # R3: all valid rows on-screen.
    offs = [i for i in valid if not (0 <= sx[i] < SCREEN_W and 0 <= sy[i] < SCREEN_H)]
    r3 = (len(valid) > 0 and not offs)
    ok = ok and r3
    print("  [%s] R3  all %d valid rows on-screen (off-screen rows: %s)"
          % ("GREEN" if r3 else " RED ", len(valid), offs if offs else "none"))

    print("-" * 70)
    if ok:
        print("RESULT: GREEN -- the chain main-menu rows are centered on-screen. "
              "OPEN the PNG: the Mania Mode row + neighbors are visible.")
        return 0
    print("RESULT: RED -- the chain menu rows are off-screen (scroll origin not "
          "centered on the active control). Force currentScreen->position from "
          "the active UIControl per decomp UIControl_Draw (UIControl.c:52-53).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
