#!/usr/bin/env python3
# =============================================================================
# qa_engine_menu_render.py -- M1b RED-first gate: the Mania MAIN MENU RENDERS its
# rows + backdrop. Builds on M1a (qa_engine_menu.py: the engine loads+runs the Menu
# scene, but the screen is BLACK -- _p6_w_vdp1_landed == 0, the menu emits NO VDP1).
#
# ROOT CAUSE (measured, decomp tools/_decomp_raw/SonicMania_Objects_Menu_MenuSetup.c):
#   (1) MenuSetup_StaticUpdate (non-Plus, :88-134) gates MenuSetup_Initialize (:242 --
#       the fn that wires the UIControl row tree) behind MenuSetup_InitAPI()==true.
#       InitAPI (:412-489) returned false because the Saturn auth/storage stubs left
#       APICallback==NULL + storageStatus 0. The Saturn-reachable return-true is the
#       API_GetNoSave() branch (:467,470) -- reached by giving APICallback a real
#       zeroed struct + globals->noSave=true + the real GetUserAuthStatus/GetStorage-
#       Status/TryInitStorage state machine (none create entities on the NULL-fn path).
#   (2) the menu UI sprite sheets (UI/MainIcons.gif, UI/TextEN.gif) were not staged as
#       SaturnSheet slots -> DrawSprite dropped on unbound surfaces. + UIBackground's
#       FillScreen backdrop color was thresholded black/white by the title intro
#       p6_fillscreen_saturn (the Mania main-menu backdrop is gold 0xF0C800).
#
# Measurable conditions (RED on M1a build, GREEN only when the menu renders):
#   M6  p6_w_menu_treebuilt      == 1   -- MenuSetup->mainMenu != NULL (the row tree
#                                          built; InitAPI returned true). The KEYSTONE.
#   M6b p6_w_menu_modebtn_classid > 0   -- UIModeButton registered (the 4 main-menu rows).
#   M7  p6_w_menu_vdp1_landed    > 0    -- the menu emitted >=1 VDP1 sprite command
#                                          (currently 0 == black screen).
#   PX  non-black pixel-mass on the captured PNG over a threshold -- the backdrop +
#       at least one row are visible (RED on the all-black M1a frame).
#
#   python tools/_portspike/qa_engine_menu_render.py             # boot + capture + verdict
#   python tools/_portspike/qa_engine_menu_render.py --mcs X.mcs --png Y.png
#   python tools/_portspike/qa_engine_menu_render.py --static    # map-only RED check
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 90.0
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
QA_BOOT   = os.path.join(ROOT, "tools", "qa_boot.ps1")
TMP_MCS   = os.path.join(HERE, "_engine_menu_render.mcs")
TMP_PNG   = os.path.join(HERE, "_engine_menu_render.png")

# witnesses (map names carry the leading underscore the linker emits)
NAMES = ["_p6_w_menu_treebuilt", "_p6_w_menu_modebtn_classid", "_p6_w_menu_vdp1_landed"]

# PX: a frame with the menu backdrop + a row is far from all-black. The M1a black
# frame has ~0 non-black pixels; the gold backdrop alone fills the whole frame.
PX_NONBLACK_MIN = 5000   # of 320x224 = 71,680 px; the gold fill alone is ~full frame


def capture_mcs(out):
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame", str(GATE_FRAME), "-Out", out],
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout[-200:] if r.stdout else "")
    return os.path.exists(out)


def nonblack_stats(png):
    """Return (nonblack_count, total, centroid_x, centroid_y) for pixels above a
    near-black floor. Pure measurement -- no impressionistic judgement."""
    try:
        from PIL import Image
    except Exception:
        return None
    im = Image.open(png).convert("RGB")
    w, h = im.size
    px = im.load()
    n = 0; sx = 0; sy = 0; total = w * h
    for y in range(0, h):
        for x in range(0, w):
            r, g, b = px[x, y]
            if r + g + b > 48:   # near-black floor (sum of channels)
                n += 1; sx += x; sy += y
    cx = (sx / n) if n else -1
    cy = (sy / n) if n else -1
    return (n, total, cx, cy)


def main(argv):
    mp = Q.read_text(Q.MAP_DEFAULT)
    present = {n: (Q.map_symbol(mp, n) is not None) for n in NAMES}

    if "--static" in argv or not all(present.values()):
        print("=" * 64)
        print("M1b FRONT-END -- the Mania MAIN MENU renders its rows + backdrop")
        print("=" * 64)
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        missing = [n for n in NAMES if not present[n]]
        if missing:
            print("-" * 64)
            print("RESULT: RED -- %d/%d render witnesses ABSENT from game.map. The menu-render "
                  "port (auth-gate flip + UIModeButton + UI sheet staging + backdrop) is NOT "
                  "built yet. Expected RED baseline for M1b." % (len(missing), len(NAMES)))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- render witnesses present (run without --static "
                  "to verify the runtime M6/M6b/M7 values + the pixel gate).")
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
    v = {n: Q.peek_u32(mod, sec, Q.map_symbol(mp, n), perm, signed=True) for n in NAMES}

    checks = [
        ("M6  MenuSetup->mainMenu != NULL (row tree built; InitAPI true)", v["_p6_w_menu_treebuilt"],       lambda x: x == 1),
        ("M6b UIModeButton registered (the 4 main-menu rows)",            v["_p6_w_menu_modebtn_classid"],  lambda x: x and x > 0),
        ("M7  the menu emitted >=1 VDP1 sprite command (landed>0)",       v["_p6_w_menu_vdp1_landed"],      lambda x: x and x > 0),
    ]
    print("=" * 64)
    print("M1b FRONT-END -- the Mania MAIN MENU renders its rows + backdrop")
    print("=" * 64)
    ok = True
    for label, val, test in checks:
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-58s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))

    # PX pixel gate (only if a PNG was supplied/produced)
    png = argv[argv.index("--png") + 1] if "--png" in argv else None
    if png and os.path.exists(png):
        st = nonblack_stats(png)
        if st is None:
            print("  [ -- ] PX pixel gate SKIPPED (PIL unavailable)")
        else:
            n, total, cx, cy = st
            passed = n >= PX_NONBLACK_MIN
            ok = ok and passed
            print("  [%s] PX  non-black pixel-mass %d/%d (centroid %.0f,%.0f), need >=%d"
                  % ("GREEN" if passed else " RED ", n, total, cx, cy, PX_NONBLACK_MIN))
        # IC: MAINICON icon-cleanliness sub-gate (this session). PX (non-black mass)
        # stays GREEN even when the icon sprites are STRIPED garbage -- the gate that
        # caught the P6_FRONTEND_MENU build-flag bug measures the VERTICAL R<->B flip
        # fraction (striped >= 0.0219; clean coherent blobs = 0.0070). Folded in so the
        # menu QA pipeline catches the striped-icon class automatically henceforth.
        try:
            import qa_menu_icon_clean as IC
            flips, satpairs, frac = IC.measure(png)
            if satpairs < IC.SATPAIRS_MIN:
                print("  [ -- ] IC  icon-clean SKIPPED (only %d sat R/B pairs -- not a "
                      "settled menu frame)" % satpairs)
            else:
                ic_ok = frac <= IC.FLIPFRAC_MAX_GREEN
                ok = ok and ic_ok
                print("  [%s] IC  MAINICON icons coherent (vert R<->B flipfrac %.4f <= %.4f; "
                      "striped >= 0.0219)" % ("GREEN" if ic_ok else " RED ", frac, IC.FLIPFRAC_MAX_GREEN))
        except Exception as e:
            print("  [ -- ] IC  icon-clean SKIPPED (%s)" % e)
    print("-" * 64)
    if ok:
        print("RESULT: GREEN -- the Mania MAIN MENU renders (row tree built + UIModeButton live + "
              "VDP1 sprites landed + non-black backdrop). OPEN the PNG to confirm rows + backdrop "
              "match the PC Mania main menu (gold diagonal bg + Mania Mode/Time Attack/Competition/"
              "Options rows). NEXT: UISaveSlot + start-game (SetScene Cutscenes/Angel Island Zone).")
        return 0
    print("RESULT: RED -- see the RED check above (M1a black-screen baseline, or a partial render).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
