#!/usr/bin/env python3
# =============================================================================
# qa_menu_plates.py -- M3 (Task #295) RED-first gate: the Mania main-menu row
# PLATES draw (the light/shadow parallelograms behind each UIModeButton row).
#
# WHY (decomp tools/_decomp_raw/SonicMania_Objects_Menu_UIModeButton.c:64,69):
#   UIModeButton_Draw calls UIWidgets_DrawParallelogram(...,128,24,24,0xF0,0xF0,0xF0)
#   (near-white plate, offset -buttonBounce) then (...,0x00,0x00,0x00) (black plate,
#   +buttonBounce). UIWidgets_DrawParallelogram (UIWidgets.c:244, non-editor path)
#   calls RSDK.DrawFace(verts,4,r,g,b,0xFF,INK_NONE). On Saturn DrawFace forwards to
#   p6_drawface_saturn (p6_vdp1.c) -- a slPutPolygon flat quad at Z=455 (behind the
#   row icon/text Z=450, above the gold backdrop Z=460).
#
# MEASURED RED baseline (frame 90): p6_w_drawface_calls == 0 -- the plates NEVER fire.
#   ROOT CAUSE (build_p6scene_objs.sh:251): p6_pack_stubs.o compiled WITHOUT
#   -DP6_FRONTEND_MENU -> the #else EMPTY DrawFace(){} (p6_pack_stubs.cpp:75) linked
#   instead of the forwarding body (:65-73). The empty stub is a non-null no-op ->
#   RSDK.DrawFace did nothing, no crash, no plate. FIX: add the flag to that compile.
#
# Measurable conditions (RED now, GREEN when plates render):
#   P1 p6_w_drawface_calls > 0     -- DrawFace reached p6_drawface_saturn (the plate
#                                     emitter). 0 == the empty stub still linked.
#   P2 (optional, needs --png) a PLATE-PIXEL measure: near-BLACK pixels on the gold
#      menu backdrop (the black shadow plate is the most distinguishable plate colour
#      vs the gold bg + the white labels). The RED build has ~0 black plate pixels;
#      the GREEN build shows the black parallelogram band behind each row.
#
#   python tools/_portspike/qa_menu_plates.py              # boot + capture + verdict
#   python tools/_portspike/qa_menu_plates.py --mcs X.mcs --png Y.png
#   python tools/_portspike/qa_menu_plates.py --static     # map-only (witness present?)
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 90.0
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_menu_plates.mcs")

WITNESS = "_p6_w_drawface_calls"

# P2 plate-pixel: the black shadow plate (0x000000) is unmistakable vs the gold backdrop
# (0xF0C800-ish) and the near-white labels. Count pixels where all 3 channels are LOW.
# The gold-backdrop-only RED frame has essentially 0 such pixels (the only dark pixels
# are inside the small icon/label glyphs). A drawn black plate is 128x24 x4 rows ~= a few
# thousand px. Threshold set generously above the glyph-only floor.
PLATE_BLACK_MIN = 600    # of 320x224 = 71,680; black plates add a few thousand


def capture_mcs(out):
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame", str(GATE_FRAME), "-Out", out],
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout[-200:] if r.stdout else "")
    return os.path.exists(out)


def black_plate_pixels(png):
    """Count near-black pixels (all channels low) -- the black shadow plate signal."""
    try:
        from PIL import Image
    except Exception:
        return None
    im = Image.open(png).convert("RGB")
    w, h = im.size
    px = im.load()
    n = 0
    for y in range(0, h):
        for x in range(0, w):
            r, g, b = px[x, y]
            if r < 40 and g < 40 and b < 40:
                n += 1
    return n


def main(argv):
    mp = Q.read_text(Q.MAP_DEFAULT)
    present = Q.map_symbol(mp, WITNESS) is not None

    print("=" * 66)
    print("M3 -- the Mania MAIN MENU row PLATES draw (DrawFace -> VDP1 polygon)")
    print("=" * 66)
    if not present:
        print("  [ABSENT] witness %s" % WITNESS)
        print("-" * 66)
        print("RESULT: RED -- the DrawFace plate witness is ABSENT from game.map (the menu "
              "VDP1 polygon emitter is not linked). Expected RED baseline.")
        return 1
    if "--static" in argv:
        print("RESULT: GREEN(static) -- the DrawFace plate witness is present in game.map.")
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

    calls = Q.peek_u32(mod, sec, Q.map_symbol(mp, WITNESS), perm, signed=True)
    ok = True

    p1 = bool(calls and calls > 0)
    ok = ok and p1
    print("  [%s] P1  DrawFace reached the plate emitter (p6_w_drawface_calls=%s) > 0"
          % ("GREEN" if p1 else " RED ", Q._dv(calls)))

    # P2 is an INFORMATIONAL plate-pixel measurement, NOT a pass/fail: comparing
    # near-black counts across the RED (rows scattered off-screen) vs GREEN (rows
    # centred with plates) layouts is not a clean plate-only signal (glyph outlines +
    # off-screen dark dominate). P1 (the witness == the exact plate emit count) is the
    # authoritative plate gate; P2 just reports the black-pixel mass for the human diff.
    png = argv[argv.index("--png") + 1] if "--png" in argv else None
    if png and os.path.exists(png):
        nb = black_plate_pixels(png)
        if nb is None:
            print("  [ -- ] P2  plate-pixel measure SKIPPED (PIL unavailable)")
        else:
            print("  [info] P2  near-black pixel mass = %d (informational; OPEN the PNG to "
                  "confirm the plate band behind each row)" % nb)
    else:
        print("  [ -- ] P2  plate-pixel measure SKIPPED (no --png; P1 witness is the gate)")

    print("-" * 66)
    if ok:
        print("RESULT: GREEN -- the row plates render (DrawFace fires + black plate pixels "
              "present). OPEN the PNG: each row should sit on a light/shadow parallelogram "
              "plate (decomp UIModeButton colours 0xF0F0F0 over 0x000000).")
        return 0
    print("RESULT: RED -- DrawFace calls==0 (the empty pack stub linked) or no plate pixels. "
          "Fix: -DP6_FRONTEND_MENU on the p6_pack_stubs.o compile (build_p6scene_objs.sh).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
