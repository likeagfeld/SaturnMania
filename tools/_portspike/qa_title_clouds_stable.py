#!/usr/bin/env python3
# =============================================================================
# qa_title_clouds_stable.py -- the title CLOUD-FLICKER gate (user 2026-06-23:
# "cloud background are still constantly flickering.... need to stabilize them").
#
# The NBG1 cloud wisps must be STEADY frame-to-frame (only the slow horizontal
# scroll changes the top band). MEASURED flicker signature: the cloud-light-pixel
# mass in the top band OSCILLATES -- ~40k on good frames, dipping to ~32-35k on
# flicker frames (a per-frame chunk of wisps blinks). A stable layer varies only
# a few % (wisps entering/leaving at the scroll edges).
#
# Metric: top-band "cloud-light" mass per settled frame (pixels brighter than the
# blue sky). DIP = a frame whose mass is < FLOOR_FRAC * median (a wisp-blink).
#   GREEN = clouds steady: <= MAX_DIPS frames dip below FLOOR_FRAC of the median.
#   RED   = > MAX_DIPS dip frames (the flicker), OR clouds absent (median ~ 0).
#
#   python tools/_portspike/qa_title_clouds_stable.py            # capture + verdict
#   python tools/_portspike/qa_title_clouds_stable.py --score "GLOB"
# =============================================================================
import glob, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT = os.path.join(ROOT, "tools", "qa_boot.ps1")
SHOT_BASE = "_clouds_stable_shot"

BOOT_WAIT, BOOT_EVERY, BOOT_SHOTS = 47.0, 0.5, 24
BAND_Y0, BAND_Y1 = 0.05, 0.32      # cloud band = top of screen (decomp y[0,120])
FLOOR_FRAC = 0.88                  # a frame < 0.88*median cloud-mass = a wisp-blink dip
MAX_DIPS   = 2                     # allow <=2 (scroll-edge); more = flicker
PRESENT_MIN = 4000                 # median mass below this = clouds absent/blank


def _purge_lock():
    try: os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError: pass


def cloud_mass(png):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(int)
    H, W, _ = a.shape
    t = a[int(H * BAND_Y0):int(H * BAND_Y1)]
    r, g, b = t[:, :, 0], t[:, :, 1], t[:, :, 2]
    # cloud wisps = lighter/whiter than the sky back-color (~0,96,224)
    light = ((r > 60) & (g > 150) & (b > 210)) | ((r > 150) & (g > 180) & (b > 210))
    return int(light.sum())


def verdict(pngs):
    import numpy as np
    print("=" * 70)
    print("CLOUD-STABLE GATE -- top-band wisp-mass steadiness (no flicker)")
    print("=" * 70)
    if len(pngs) < 5:
        print("RESULT: RED -- need >=5 settled frames (%d)." % len(pngs)); return 1
    masses = [cloud_mass(p) for p in pngs]
    med = float(np.median(masses))
    floor = FLOOR_FRAC * med
    dips = [i for i, m in enumerate(masses) if m < floor]
    for i, m in enumerate(masses):
        tag = "  <-- DIP (wisp-blink)" if m < floor else ""
        print("  f%02d  cloud_mass=%6d  (%.2f x med)%s" % (i, m, m / max(med, 1), tag))
    print("-" * 70)
    print("  frames=%d  median=%.0f  floor(%.2f x med)=%.0f  dips=%d (max %d)"
          % (len(masses), med, FLOOR_FRAC, floor, len(dips), MAX_DIPS))
    if med < PRESENT_MIN:
        print("RESULT: RED -- clouds ABSENT (median mass %.0f < %d)." % (med, PRESENT_MIN)); return 1
    if len(dips) <= MAX_DIPS:
        print("RESULT: GREEN -- clouds steady (%d dip frames <= %d; flicker gone)."
              % (len(dips), MAX_DIPS)); return 0
    print("RESULT: RED -- clouds FLICKER (%d/%d frames dip below %.2f x median)."
          % (len(dips), len(masses), FLOOR_FRAC)); return 1


def _load_sorted(pat):
    return sorted(glob.glob(os.path.join(ROOT, pat)),
                  key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0])
                  if p.rsplit("_", 1)[1].split(".")[0].isdigit() else 0)


def capture():
    _purge_lock()
    for f in glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")):
        try: os.remove(f)
        except OSError: pass
    subprocess.run(["pwsh", "-File", QA_BOOT, "-Cue", "game.cue", "-Wait", str(BOOT_WAIT),
                    "-Every", str(BOOT_EVERY), "-Shots", str(BOOT_SHOTS),
                    "-Out", SHOT_BASE + ".png", "-Silent"],
                   capture_output=True, text=True, cwd=ROOT)
    return _load_sorted(SHOT_BASE + "_*.png")


def main(argv):
    if "--score" in argv:
        return verdict(_load_sorted(argv[argv.index("--score") + 1]))
    return verdict(capture())


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
