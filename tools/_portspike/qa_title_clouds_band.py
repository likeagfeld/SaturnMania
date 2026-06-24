#!/usr/bin/env python3
# =============================================================================
# qa_title_clouds_band.py -- SESSION 2026-06-23g RED-first gate for ISSUE 2
# (clouds DOUBLE-LAYER / full-screen tiling).
#
# The companion qa_title_clouds_pixel.py asserts the clouds are VISIBLE in the
# TOP band (and stays GREEN -- we must NOT lose the upper clouds). This gate
# asserts the COMPLEMENT: the decomp clouds (TitleBG_Scanline_Clouds, clip
# y[0,SCREEN_YSIZE/2]=[0,120]) are an UPPER-HALF band ONLY. The current Saturn
# B1 cloud arm tiles the 16x16 cloud layout across a FULL 32x32 page -> the
# clouds fill the ENTIRE plane = a dense full-screen white-wisp field (the
# "static cloud background" the user sees). That field shows in the LOWER band
# too (the bottom sky corners, beside/below the ribbon).
#
# METRIC: cloud (bright/whitish, non-sky, non-FG) pixels in the LOWER sky
# corners (below the logo, screen y in the bottom band, L/R strips away from the
# island green + the central ribbon). RED now (full-screen tiling -> lower band
# has clouds). GREEN when the cloud field is restricted to the upper band ->
# the lower sky corners are flat sky-blue (cloud px ~0).
#
# This is the SYMPTOM gate for the double-layer: a single upper cloud band has
# NO clouds in the lower corners; a full-screen tiled field does.
#
#   python tools/_portspike/qa_title_clouds_band.py            # capture + verdict
#   python tools/_portspike/qa_title_clouds_band.py --score "GLOB"
# =============================================================================
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT = os.path.join(ROOT, "tools", "qa_boot.ps1")
SHOT_BASE = "_clouds_band_shot"

BOOT_WAIT, BOOT_EVERY, BOOT_SHOTS = 47.0, 0.6, 24

# A settled frame == the SONIC MANIA logo present (saturated non-sky FG mass).
LOGO_PRESENT_MIN = 30000
# LOWER-band cloud ceiling: a single upper cloud band leaves the lower sky
# corners flat sky-blue. RED build (full-screen tiling) has many cloud px here.
# GREEN = lower-band cloud px UNDER this ceiling in MOST settled frames.
LOWER_CLOUD_MAX = 600

# LOWER sky-corner band (below the logo/ribbon) + L/R strips. The island green
# lives center-bottom; the ribbon tails L/R at y~0.66-0.80. We sample the very
# bottom L/R corners (y[0.82:0.97]) where only sky (or full-screen clouds) live,
# and exclude the green-island mask so island grass never counts as a cloud.
BAND_Y0, BAND_Y1 = 0.82, 0.985
LEFT_X0, LEFT_X1 = 0.04, 0.26
RIGHT_X0, RIGHT_X1 = 0.74, 0.96


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


def _cloud_mask(sub):
    """Cloud puffs = bright + NOT blue-dominant (whitish/grey), AND NOT green
    (so island grass in the bottom-center never counts). Mirrors the upper-band
    gate's _cloud_mask + an explicit green exclusion for the bottom band."""
    import numpy as np
    r, g, b = sub[..., 0].astype(np.int32), sub[..., 1].astype(np.int32), sub[..., 2].astype(np.int32)
    luma = (r * 30 + g * 59 + b * 11) // 100
    blue_dom = (b - r) > 45              # flat sky-blue back-color
    green = (g > r + 18) & (g > b + 8) & (g > 60)   # island grass
    bright = luma >= 150                 # cloud whites/light greys
    return bright & (~blue_dom) & (~green)


def _lower_corners(a):
    H, W, _ = a.shape
    ys, ye = int(H * BAND_Y0), int(H * BAND_Y1)
    lxs, lxe = int(W * LEFT_X0), int(W * LEFT_X1)
    rxs, rxe = int(W * RIGHT_X0), int(W * RIGHT_X1)
    import numpy as np
    return np.concatenate([a[ys:ye, lxs:lxe], a[ys:ye, rxs:rxe]], axis=1)


def score(png):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(np.int32)
    logo = _logo_mass(a)
    band = _lower_corners(a)
    cm = _cloud_mask(band)
    return logo, int(cm.sum())


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
    print("CLOUDS-BAND GATE (#276g ISSUE 2) -- lower sky corners must be SKY, not clouds")
    print("=" * 70)
    settled = []
    for p in pngs:
        try:
            logo, lower_cloud = score(p)
        except Exception as e:
            print("  %-30s ERR %s" % (os.path.basename(p), e)); continue
        tag = "settled" if logo >= LOGO_PRESENT_MIN else "intro/transition"
        mark = ""
        if logo >= LOGO_PRESENT_MIN:
            settled.append((p, lower_cloud))
            mark = "  <-- lower-band CLOUDS (full-screen field)" if lower_cloud > LOWER_CLOUD_MAX else "  <-- lower band clean"
        print("  %-30s logo=%7d lower_cloud=%6d  [%s]%s"
              % (os.path.basename(p), logo, lower_cloud, tag, mark))
    print("-" * 70)
    if not settled:
        print("RESULT: RED -- no settled (logo-present) frames captured; cannot judge.")
        return 1
    clean = [t for t in settled if t[1] <= LOWER_CLOUD_MAX]
    lows = [t[1] for t in settled]
    print("  settled frames: %d   lower-band-clean (cloud<=%d): %d   max=%d min=%d"
          % (len(settled), LOWER_CLOUD_MAX, len(clean), max(lows), min(lows)))
    # GREEN: the lower sky corners are clean (no clouds) in MOST settled frames.
    p1 = len(clean) >= max(2, (len(settled) * 3) // 4)
    print("-" * 70)
    print("  P1 lower band CLEAN (no full-screen cloud field): %s"
          % ("PASS" if p1 else "FAIL (clouds tile into the lower band = double-layer field)"))
    print("RESULT:", "GREEN -- clouds are an UPPER band only; the lower sky corners are flat sky."
          if p1 else "RED -- clouds fill the lower band too (full-screen tiled field = the double-layer look).")
    return 0 if p1 else 1


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
