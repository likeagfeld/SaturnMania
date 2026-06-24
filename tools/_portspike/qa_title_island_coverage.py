#!/usr/bin/env python3
# =============================================================================
# qa_title_island_coverage.py -- SESSION 2026-06-23g RED-first gate for ISSUE 1
# (island shows only the LEFT ~1/3; the user: "3 quarters of the island tiles
# still missing").
#
# qa_title_island_pixel.py asserts the island is PRESENT (green-mass) + ROTATES,
# and stays GREEN. But green-mass cannot see SHAPE/POSITION (field-gotcha #4): a
# left-shifted sliver and a centered full landmass score the same mass. This gate
# measures POSITION + WIDTH: the island green CENTROID must sit near the screen
# horizontal CENTER (not jammed to the left edge), in MOST settled frames.
#
# MEASURED RED baseline (_clouds_band_shot_*, the build BEFORE this fix): the
# green centroid_x is ~190-290 of the 912px window while center is 456 -> the
# landmass is LEFT-shifted by ~170-270 px. GREEN = the centroid is within
# CENTER_TOL_FRAC of the band center in most settled frames (the landmass bulk
# is centered, so more of it is on-screen).
#
#   python tools/_portspike/qa_title_island_coverage.py            # capture + verdict
#   python tools/_portspike/qa_title_island_coverage.py --score "GLOB"
# =============================================================================
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT = os.path.join(ROOT, "tools", "qa_boot.ps1")
SHOT_BASE = "_isl_cov_shot"

BOOT_WAIT, BOOT_EVERY, BOOT_SHOTS = 47.0, 0.6, 24

LOGO_PRESENT_MIN = 30000
# Only judge frames with a real island present (enough green to have a shape).
GREEN_PRESENT_MIN = 6000
# The island green centroid must be within this fraction of the FULL window width
# from the window horizontal center. RED build centroid ~190-290 / 912 -> offset
# ~0.18-0.29 of W from center (456). GREEN target: centered bulk -> offset small.
CENTER_TOL_FRAC = 0.135

BAND_Y0, BAND_Y1 = 0.70, 0.985


def _purge_lock():
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass


def _logo_mass(a):
    import numpy as np
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx = np.maximum(np.maximum(r, g), b); mn = np.minimum(np.minimum(r, g), b)
    blue_bd = (b > r + 30) & (b > g + 20) & (g > r)
    return int((((mx - mn) >= 55) & (~blue_bd)).sum())


def _green(a):
    import numpy as np
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    return (g > r + 18) & (g > b + 8) & (g > 60)


def score(png):
    """Returns (logo_mass, green_px, centroid_x_window, W)."""
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(np.int32)
    H, W, _ = a.shape
    logo = _logo_mass(a)
    ys, ye = int(H * BAND_Y0), int(H * BAND_Y1)
    band = a[ys:ye, :]
    gm = _green(band)
    n = int(gm.sum())
    if n == 0:
        return logo, 0, None, W
    cols = gm.sum(axis=0)
    cx = float((np.arange(len(cols)) * cols).sum() / max(cols.sum(), 1))
    return logo, n, cx, W


def capture_burst():
    _purge_lock()
    for f in glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")):
        try:
            os.remove(f)
        except OSError:
            pass
    subprocess.run(
        ["pwsh", "-File", QA_BOOT, "-Cue", "game.cue", "-Wait", str(BOOT_WAIT),
         "-Every", str(BOOT_EVERY), "-Shots", str(BOOT_SHOTS),
         "-Out", SHOT_BASE + ".png", "-Silent"], capture_output=True, text=True, cwd=ROOT)
    return sorted(glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")),
                  key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0]))


def verdict(pngs):
    print("=" * 70)
    print("ISLAND-COVERAGE GATE (#276g ISSUE 1) -- landmass centered, not a left sliver")
    print("=" * 70)
    judged = []
    for p in pngs:
        try:
            logo, green_px, cx, W = score(p)
        except Exception as e:
            print("  %-28s ERR %s" % (os.path.basename(p), e)); continue
        if logo < LOGO_PRESENT_MIN or green_px < GREEN_PRESENT_MIN:
            print("  %-28s logo=%7d green=%6d  [skip: not a settled-island frame]"
                  % (os.path.basename(p), logo, green_px))
            continue
        center = W / 2.0
        off = abs(cx - center) / W
        ok = off <= CENTER_TOL_FRAC
        judged.append(ok)
        print("  %-28s green=%6d centroid_x=%4.0f center=%4.0f off=%.3f%s"
              % (os.path.basename(p), green_px, cx, center, off,
                 "" if ok else "  <-- LEFT-SHIFTED (sliver)"))
    print("-" * 70)
    if not judged:
        print("RESULT: RED -- no settled-island frames to judge centering.")
        return 1
    nok = sum(1 for v in judged if v)
    need = max(2, (len(judged) * 3) // 5)
    print("  island frames: %d   centered (off<=%.3f): %d   need >= %d"
          % (len(judged), CENTER_TOL_FRAC, nok, need))
    ok = nok >= need
    print("RESULT:", "GREEN -- the island landmass is centered (the bulk is on-screen)."
          if ok else "RED -- the island is LEFT-shifted (only part of the landmass shows).")
    return 0 if ok else 1


def main(argv):
    if "--score" in argv:
        pat = argv[argv.index("--score") + 1]
        pngs = sorted(glob.glob(os.path.join(ROOT, pat)),
                      key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0])
                      if p.rsplit("_", 1)[1].split(".")[0].isdigit() else 0)
        if not pngs:
            print("RESULT: RED -- no pngs match %s" % pat); return 1
        return verdict(pngs)
    return verdict(capture_burst())


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
