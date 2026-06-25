#!/usr/bin/env python3
# =============================================================================
# qa_menu_layout.py -- M2a/M2b RED-first gate: the Mania MAIN MENU rows land in
# the correct ON-SCREEN 2x2 column, not scattered with off-screen clipping.
#
# WHY (measured, decomp tools/_decomp_raw/SonicMania_Objects_Menu_UIControl.c +
# UIModeButton.c + the captured _menuok_*.png):
#   The base-game Menu/Scene1.bin places the 4 main-menu rows (UIModeButton:
#   Mania Mode/Time Attack/Competition/Options) as a 2x2 grid at world px
#   (756,358)(948,358)(756,420)(948,420) [docs/scene_census.json _coords]. The
#   decomp world->screen transform is  screen = world - ScreenInfo->position,
#   where UIControl_Draw (UIControl.c:52-53) sets ScreenInfo->position =
#   FROM_FIXED(activeControl->position) - center for the ACTIVE menu control.
#   The Saturn DrawSprite (p6_io_main.cpp DrawSprite:1720-1723) applies that
#   subtraction. So the rows are correctly placed IFF the active main-menu
#   UIControl's Draw ran (set the scroll origin to center the 2x2) AND no
#   inactive UIControl is leaking its content.
#
# SYMPTOM on the current build (the RED baseline): the rows scatter across the
#   whole frame (Mania Mode top-left, Time Attack mid-right clipped, Competition
#   lower-left, Extras lower-center, Controls top-right clipped). MEASURED from
#   the witnesses: either the scroll origin is wrong/stale OR >1 UIControl is
#   visible (inactive menus drawing their content).
#
# Measurable conditions (RED on the scattered build, GREEN when the column matches):
#   L1 visctrls == 1   -- exactly ONE UIControl is visible (the active main menu).
#                         >1 == inactive menus leaking content (the scatter cause).
#   L2 all 4 UIModeButton screen positions ON-SCREEN: 0<=sx<=320, 0<=sy<=224
#                         (the RED build clips >=1 row off the right/bottom edge).
#   L3 the 4 form a COMPACT 2x2: x-spread (max-min of sx) <= 220 AND
#                         y-spread (max-min of sy) <= 140  (the decomp 2x2 is
#                         ~192 wide x ~62 tall after the centering transform; the
#                         RED scatter spreads them >250 px).
#   L4 the active control's tag is the Main Menu (tagh == djb2("Main Menu")) so
#                         the scroll origin is taken from the RIGHT control.
#
#   python tools/_portspike/qa_menu_layout.py             # boot + capture + verdict
#   python tools/_portspike/qa_menu_layout.py --mcs X.mcs # use an existing state
#   python tools/_portspike/qa_menu_layout.py --static    # map-only (witnesses present?)
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 90.0
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_menu_layout.mcs")

SCREEN_W, SCREEN_H = 320, 224

# M2b SATURN-NATIVE 320 FIT (Task #294): a row is drawn as icon+label CENTRED on its
# screen position. The widest element is the 148px TextEN label ("Competition") -> half-
# width 74. The icon is ~44px tall -> half-height 22. For "no clipping" the row BBOX
# (centroid +/- these half-extents) must be FULLY inside [0,320]x[0,224], not just the
# centroid. Measured from parse_spr (UI/MainIcons.bin anim0, UI/TextEN.bin anim1).
ROW_HALF_W = 74
ROW_HALF_H = 22

# scalar witnesses
SCALARS = ["_p6_w_menu_scrx", "_p6_w_menu_scry", "_p6_w_menu_visctrls",
           "_p6_w_menu_ctrl_count", "_p6_w_menu_actctrl_px", "_p6_w_menu_actctrl_py",
           "_p6_w_menu_actctrl_tagh", "_p6_w_menu_modebtn_classid"]
# array witnesses (4 ints each, contiguous)
ARRAYS = ["_p6_w_menu_modebtn_px", "_p6_w_menu_modebtn_py",
          "_p6_w_menu_modebtn_sx", "_p6_w_menu_modebtn_sy",
          "_p6_w_menu_modebtn_act", "_p6_w_menu_modebtn_bid"]

# L3 spreads: the decomp 2x2 (756..948 world x, 358..420 world y) is ~192x62 after
# centering; allow generous margin for the icon/text element offsets within a row.
XSPREAD_MAX = 220
YSPREAD_MAX = 140


def djb2(s):
    h = 5381
    for ch in s:
        h = ((h * 33) + ord(ch)) & 0xFFFFFFFF
    return h


TAG_MAINMENU = djb2("Main Menu")


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
    print("M2a/M2b -- the Mania MAIN MENU rows land in the correct on-screen 2x2")
    print("=" * 70)

    if missing:
        for n in missing:
            print("  [ABSENT] witness %s" % n)
        print("-" * 70)
        print("RESULT: RED -- %d/%d layout witnesses ABSENT from game.map. The M2a "
              "witness build is not linked yet." % (len(missing), len(allnames)))
        return 1
    if "--static" in argv:
        print("RESULT: GREEN(static) -- all layout witnesses present in game.map.")
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

    s = {n: rd(n) for n in SCALARS}
    a = {n: rd_arr(n) for n in ARRAYS}

    scrx, scry = s["_p6_w_menu_scrx"], s["_p6_w_menu_scry"]
    px, py = a["_p6_w_menu_modebtn_px"], a["_p6_w_menu_modebtn_py"]
    sx, sy = a["_p6_w_menu_modebtn_sx"], a["_p6_w_menu_modebtn_sy"]
    act    = a["_p6_w_menu_modebtn_act"]
    bid    = a["_p6_w_menu_modebtn_bid"]

    print("  scroll origin currentScreen->position = (%s, %s)" % (Q._dv(scrx), Q._dv(scry)))
    print("  active UIControl pos = (%s, %s)  tagh=0x%08X (MainMenu=0x%08X)  "
          "visible UIControls=%s of %s"
          % (Q._dv(s["_p6_w_menu_actctrl_px"]), Q._dv(s["_p6_w_menu_actctrl_py"]),
             (s["_p6_w_menu_actctrl_tagh"] & 0xFFFFFFFF), TAG_MAINMENU,
             Q._dv(s["_p6_w_menu_visctrls"]), Q._dv(s["_p6_w_menu_ctrl_count"])))
    print("  UIModeButton rows (world px -> screen px):")
    names = {0: "ManiaMode", 1: "TimeAttack", 2: "Competition", 3: "Options"}
    for i in range(4):
        nm = names.get(bid[i], "bid=%s" % Q._dv(bid[i]))
        print("    row[%d] %-11s world(%s,%s) -> screen(%s,%s)  active=%s"
              % (i, nm, Q._dv(px[i]), Q._dv(py[i]), Q._dv(sx[i]), Q._dv(sy[i]), Q._dv(act[i])))

    # --- the gates ---------------------------------------------------------
    valid = [i for i in range(4) if sx[i] > -900000 and sy[i] > -900000]
    ok = True

    # L1: exactly one visible UIControl
    l1 = (s["_p6_w_menu_visctrls"] == 1)
    ok = ok and l1
    print("  [%s] L1  exactly ONE visible UIControl (active main menu)  = %s"
          % ("GREEN" if l1 else " RED ", Q._dv(s["_p6_w_menu_visctrls"])))

    # L4: active control is the Main Menu
    l4 = ((s["_p6_w_menu_actctrl_tagh"] & 0xFFFFFFFF) == TAG_MAINMENU)
    ok = ok and l4
    print("  [%s] L4  active control tag == \"Main Menu\"" % ("GREEN" if l4 else " RED "))

    # L2: all 4 rows on-screen WITHOUT CLIPPING -- the row BBOX (centroid +/- the label
    # half-width / icon half-height), not just the centroid, must be inside [0,320]x[0,224].
    # This is the Saturn-native-320 acceptance: a 148px label centred at x80 spans [6,154]
    # and at x240 spans [166,314] -- both inside; the raw decomp 64/256 cols would clip.
    def row_inside(i):
        return (sx[i] - ROW_HALF_W >= 0 and sx[i] + ROW_HALF_W <= SCREEN_W
                and sy[i] - ROW_HALF_H >= 0 and sy[i] + ROW_HALF_H <= SCREEN_H)
    onscreen = all(row_inside(i) for i in valid) and len(valid) == 4
    ok = ok and onscreen
    offs = [i for i in valid if not row_inside(i)]
    print("  [%s] L2  all 4 row BBOXes IN [0..%d]x[0..%d] (label hw=%d, icon hh=%d; clipped rows: %s)"
          % ("GREEN" if onscreen else " RED ", SCREEN_W, SCREEN_H, ROW_HALF_W, ROW_HALF_H,
             offs if offs else "none"))

    # L3: compact 2x2
    if len(valid) == 4:
        xs = [sx[i] for i in valid]; ys = [sy[i] for i in valid]
        xspread = max(xs) - min(xs); yspread = max(ys) - min(ys)
        l3 = (xspread <= XSPREAD_MAX and yspread <= YSPREAD_MAX)
    else:
        xspread = yspread = -1; l3 = False
    ok = ok and l3
    print("  [%s] L3  compact 2x2: x-spread %s<=%d, y-spread %s<=%d"
          % ("GREEN" if l3 else " RED ", xspread, XSPREAD_MAX, yspread, YSPREAD_MAX))

    # L5: rows in the expected 2x2 ORDER by buttonID (decomp UIMODEBUTTON enum):
    #   bid0 Mania = top-left, bid1 TimeAttack = top-right, bid2 Competition = bottom-left,
    #   bid3 Options = bottom-right. Verify each present row lands in its quadrant relative
    #   to the grid centre (so the Saturn-native layout still reads as the Mania mode grid).
    if len(valid) == 4:
        cx = sum(sx[i] for i in valid) / 4.0
        cy = sum(sy[i] for i in valid) / 4.0
        want = {0: ("L", "T"), 1: ("R", "T"), 2: ("L", "B"), 3: ("R", "B")}
        l5 = True
        bad = []
        for i in valid:
            b = bid[i]
            if b not in want:
                l5 = False; bad.append((i, b, "unknown bid")); continue
            hx = "L" if sx[i] < cx else "R"
            hy = "T" if sy[i] < cy else "B"
            if (hx, hy) != want[b]:
                l5 = False; bad.append((i, b, "%s%s want %s%s" % (hx, hy, *want[b])))
    else:
        l5 = False; bad = ["<4 rows]"]
    ok = ok and l5
    print("  [%s] L5  rows in expected 2x2 order (Mania TL/TimeAttack TR/Competition BL/"
          "Options BR)%s" % ("GREEN" if l5 else " RED ", "" if l5 else "  bad=%s" % bad))

    print("-" * 70)
    if ok:
        print("RESULT: GREEN -- the 4 main-menu rows land in a compact on-screen 2x2, "
              "exactly one UIControl visible, scroll origin from the Main Menu control. "
              "OPEN the PNG + diff vs the PC base-Mania 2x2 mode grid.")
        return 0
    print("RESULT: RED -- the menu rows are scattered/clipped (see the per-row screen "
          "positions above). The world->screen scroll origin and/or the visible-control "
          "set is wrong. Fix per the decomp UIControl_Draw/SetActiveMenu transform.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
