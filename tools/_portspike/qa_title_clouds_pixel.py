#!/usr/bin/env python3
# =============================================================================
# qa_title_clouds_pixel.py -- SUB2 (#276) RED-first PIXEL gate for the TITLE
# CLOUDS (VDP2 NBG1 on bank B1), modeled on qa_title_island_pixel.py.
#
# The companion witnesses (_p6_w_title_clouds_armed==1, _p6_w_title_clouds_ntiles
# ==211) are GREEN -- the remap + map build ran. But a register/witness-correct
# NBG1 can render NOTHING (the prior pass had clouds_armed==1 while the top of the
# settled title was a uniform sky-blue blank -- the cloud CELL pixels in B1 were
# all zero because the B1 cell-copy read tilesetPixels AFTER LoadSceneAssets
# reclaimed it). So this gate measures the ON-SCREEN SYMPTOM, per the binding
# skill rule "for a VISUAL bug the gate must measure the on-screen symptom, never
# a code-level proxy."
#
# It captures a dense settled-title burst (the title needs >=15s of emulated boot
# before content renders -- binding load-floor rule), finds the SETTLED frames
# (the SONIC MANIA logo present), and asserts:
#   (P1) the TOP BAND (the sky/cloud region, screen y < ~0.42 of the window) of a
#        settled frame contains CLOUD pixels (whitish/bright, distinct from the
#        flat sky-blue back-color and from the FG logo wings, which sit lower) --
#        NOT a uniform sky-blue blank. MEASURED RED baseline (current build):
#        top-band cloud_px == 0 (pure sky). GREEN threshold = TOP_CLOUD_MIN.
#
# RED now (blank sky-blue top band -> cloud_px ~0 -> P1 fails). GREEN when the
# cloud band renders white puffs across the top.
#
# The PRIMARY evidence is the screenshots -- the orchestrator OPENS them. This
# gate quantifies the symptom so it cannot be masked.
#
# Calibration (912x749 window capture, Saturn 320x224 inset; the ~30px title bar
# shifts the frame down -- bands are window-height fractions, robust to the inset).
# The TOP band sits ABOVE the logo wings (the wings' top is ~y 0.20-0.55 of the
# window in the settled frame); the cloud band the decomp parks is the very top
# sky rows. We crop the top band to y[0.04:0.20] (above the wings) and the L/R
# thirds (x outside the central logo column) so the logo/Sonic never contaminate
# the cloud measure.
#
# Usage:
#   python tools/_portspike/qa_title_clouds_pixel.py                 # capture + verdict
#   python tools/_portspike/qa_title_clouds_pixel.py --score "GLOB"  # score existing pngs
# =============================================================================
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT = os.path.join(ROOT, "tools", "qa_boot.ps1")
SHOT_BASE = "_clouds_pixel_shot"

BOOT_WAIT, BOOT_EVERY, BOOT_SHOTS = 47.0, 0.6, 24

# A settled frame == the SONIC MANIA logo present (saturated non-sky FG mass).
LOGO_PRESENT_MIN = 30000
# Top-band cloud floor: whitish/bright cloud pixels in the upper sky region.
# RED build = ~0 (pure flat sky-blue). The decomp clouds are white puffs.
TOP_CLOUD_MIN = 1500

# Window-height fractions of the upper sky/cloud band (ABOVE the logo wings) +
# horizontal crops avoiding the central logo column AND the window borders.
BAND_Y0, BAND_Y1 = 0.045, 0.21
# two side strips (L and R of the central logo); the logo wings span the center.
LEFT_X0, LEFT_X1 = 0.05, 0.30
RIGHT_X0, RIGHT_X1 = 0.70, 0.95


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
    """Cloud puffs = bright + low saturation (whitish/grey), clearly separated from
    the saturated sky-blue back-color. The Mania title sky-blue is (33,115,216)-ish
    (strongly blue-dominant, b >> r,g). Clouds are near-white / light grey: high
    luma AND NOT blue-dominant. So: luma high AND (b - r) small."""
    import numpy as np
    r, g, b = sub[..., 0].astype(np.int32), sub[..., 1].astype(np.int32), sub[..., 2].astype(np.int32)
    luma = (r * 30 + g * 59 + b * 11) // 100
    blue_dom = (b - r) > 45          # the flat sky-blue back-color
    bright = luma >= 150             # cloud whites/light greys
    return bright & (~blue_dom)


def _top_band(a):
    """Return the (L,R side-strip) top-band sub-images for the cloud region."""
    H, W, _ = a.shape
    ys, ye = int(H * BAND_Y0), int(H * BAND_Y1)
    lxs, lxe = int(W * LEFT_X0), int(W * LEFT_X1)
    rxs, rxe = int(W * RIGHT_X0), int(W * RIGHT_X1)
    import numpy as np
    return np.concatenate([a[ys:ye, lxs:lxe], a[ys:ye, rxs:rxe]], axis=1)


def score(png):
    """Returns (logo_mass, top_cloud_px)."""
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(np.int32)
    logo = _logo_mass(a)
    band = _top_band(a)
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
    print("SUB2 #276 CLOUD PIXEL GATE -- title clouds VISIBLE in the top sky band")
    print("=" * 70)
    settled = []
    for p in pngs:
        try:
            logo, cloud_px = score(p)
        except Exception as e:
            print("  %-30s ERR %s" % (os.path.basename(p), e)); continue
        tag = "settled" if logo >= LOGO_PRESENT_MIN else "intro/transition"
        mark = ""
        if logo >= LOGO_PRESENT_MIN:
            settled.append((p, cloud_px))
            mark = "  <-- clouds BLANK" if cloud_px < TOP_CLOUD_MIN else "  <-- clouds visible"
        print("  %-30s logo=%7d top_cloud=%6d  [%s]%s"
              % (os.path.basename(p), logo, cloud_px, tag, mark))
    print("-" * 70)
    if not settled:
        print("RESULT: RED -- no settled (logo-present) frames captured; cannot judge.")
        return 1
    clouds = [t[1] for t in settled]
    visible = [t for t in settled if t[1] >= TOP_CLOUD_MIN]
    print("  settled frames: %d   clouds-visible (cloud>=%d): %d   max=%d min=%d"
          % (len(settled), TOP_CLOUD_MIN, len(visible), max(clouds), min(clouds)))
    # P1: the clouds must be VISIBLE in the top band of most settled frames.
    p1 = len(visible) >= max(2, len(settled) // 2)
    print("-" * 70)
    print("  P1 clouds VISIBLE in top band : %s" % ("PASS" if p1 else "FAIL (uniform sky-blue blank)"))
    ok = p1
    print("RESULT:", "GREEN -- the title clouds RENDER (white puffs in the top sky band)."
          if ok else "RED -- the title clouds are a uniform sky-blue blank in the top band.")
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
