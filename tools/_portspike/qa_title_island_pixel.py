#!/usr/bin/env python3
# =============================================================================
# qa_title_island_pixel.py -- CP5b.6 (Task #276) RED-first PIXEL gate.
#
# The companion qa_title_island_rot.py is a REGISTER/coeff-stream contract gate:
# it proves the RPT matrix + per-line coefficient + RPTA + KTCTL are byte-correct
# for the live angle. But a register-correct RBG0 can still render NOTHING on
# screen (the prior pass had qa_title_island_rot GREEN while the island band was a
# uniform sky-blue blank -- the RBG0 VRAM map/coeff/cells were in the wrong banks
# and/or RBG0ON starved NBG1 of its VRAM access cycle). So this gate measures the
# ON-SCREEN SYMPTOM, per the binding skill rule "for a VISUAL bug the gate must
# measure the on-screen symptom, never a code-level proxy."
#
# It captures a dense settled-title burst (the title needs >=15s of emulated boot
# before content renders -- binding load-floor rule), finds the SETTLED frames
# (the SONIC MANIA logo present), and asserts:
#   (P1) the LOWER BAND (the island region, screen y>168 of 240) contains GREEN
#        ISLAND pixels -- not a uniform sky-blue blank. MEASURED RED baseline
#        (_isl_red_*, current build): lower-band green_px == 0, blue_px ~115k
#        (pure sky). GREEN threshold = LOWER_GREEN_MIN island pixels.
#   (P2) across the settled frames the lower-band green pattern CHANGES (the
#        island ROTATES on screen): the per-frame green-pixel column profile is
#        not identical between the min-green and max-green settled frames. A
#        static (non-rotating) island would render an identical band every frame.
#
# RED now (blank sky-blue lower band -> green_px 0 -> P1 fails). GREEN when the
# island renders in the lower band AND rotates (the column profile varies).
#
# The PRIMARY evidence is the screenshots -- the orchestrator OPENS them. This
# gate quantifies the symptom so it cannot be masked.
#
# Calibration (912x749 window capture, Saturn 320x224 inset; the ~30px title bar
# shifts the frame down -- bands are expressed as window-height fractions, robust
# to the exact inset). MEASURED on _isl_red_* (current flat/blank build):
#   lower band y[0.70:0.97]: green_px=0, blue_px~115745 (uniform sky) -> RED.
#
# Usage:
#   python tools/_portspike/qa_title_island_pixel.py                 # capture + verdict
#   python tools/_portspike/qa_title_island_pixel.py --score "GLOB"  # score existing pngs
# =============================================================================
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT = os.path.join(ROOT, "tools", "qa_boot.ps1")
SHOT_BASE = "_isl_pixel_shot"

# Deep real-time burst: settled title is in a ~76-130s window; angle advances +1
# per front-end tick so frames >=0.6s apart land on different rotation angles.
BOOT_WAIT, BOOT_EVERY, BOOT_SHOTS = 47.0, 0.6, 24

# A settled frame == the SONIC MANIA logo present (saturated non-sky FG mass).
LOGO_PRESENT_MIN = 30000
# Lower-band island floor: green island pixels in the island region. RED build = 0.
LOWER_GREEN_MIN = 3000
# Rotation-liveness: the lower-band green column profile must differ between the
# min- and max-green settled frames by at least this fraction of columns.
ROT_COL_DIFF_FRAC = 0.10

# Window-height fractions of the island lower band (screen y>168/240) + horizontal
# crop (avoid the L/R window border).
BAND_Y0, BAND_Y1 = 0.70, 0.97
CROP_X0, CROP_X1 = 0.06, 0.94


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


def _green_mask(sub):
    """Island grass-green: green channel dominant over both red and blue. The Mania
    title sky-blue back-color is (33,115,216)-ish (b >> g > r); grass greens have
    g > b. So g>r AND g>b cleanly separates island from sky."""
    import numpy as np
    r, g, b = sub[..., 0], sub[..., 1], sub[..., 2]
    return (g > r + 18) & (g > b + 8) & (g > 60)


def _lower_band(a):
    """Return the (cropped) lower-band sub-image for the island region."""
    H, W, _ = a.shape
    ys, ye = int(H * BAND_Y0), int(H * BAND_Y1)
    xs, xe = int(W * CROP_X0), int(W * CROP_X1)
    return a[ys:ye, xs:xe]


def score(png):
    """Returns (logo_mass, lower_green_px, green_col_profile)."""
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(np.int32)
    logo = _logo_mass(a)
    band = _lower_band(a)
    gm = _green_mask(band)
    green_px = int(gm.sum())
    col = gm.sum(axis=0)  # per-column green count -> the island silhouette profile
    return logo, green_px, col


def _profiles_differ(col_a, col_b):
    """Fraction of columns whose green-count differs materially between two frames
    (rotation moves the island silhouette -> the column profile shifts)."""
    import numpy as np
    n = min(len(col_a), len(col_b))
    if n == 0:
        return 0.0
    a = col_a[:n].astype(np.int32); b = col_b[:n].astype(np.int32)
    denom = np.maximum(np.maximum(a, b), 1)
    rel = np.abs(a - b) / denom
    return float((rel > 0.25).mean())


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
    print("CP5b.6 PIXEL GATE -- title island VISIBLE + ROTATING in the lower band")
    print("=" * 70)
    settled = []
    for p in pngs:
        try:
            logo, green_px, col = score(p)
        except Exception as e:
            print("  %-30s ERR %s" % (os.path.basename(p), e)); continue
        tag = "settled" if logo >= LOGO_PRESENT_MIN else "intro/transition"
        mark = ""
        if logo >= LOGO_PRESENT_MIN:
            settled.append((p, green_px, col))
            mark = "  <-- island BLANK" if green_px < LOWER_GREEN_MIN else "  <-- island visible"
        print("  %-30s logo=%7d lower_green=%6d  [%s]%s"
              % (os.path.basename(p), logo, green_px, tag, mark))
    print("-" * 70)
    if not settled:
        print("RESULT: RED -- no settled (logo-present) frames captured; cannot judge.")
        return 1
    greens = [t[1] for t in settled]
    visible = [t for t in settled if t[1] >= LOWER_GREEN_MIN]
    print("  settled frames: %d   island-visible (green>=%d): %d   max_green=%d min_green=%d"
          % (len(settled), LOWER_GREEN_MIN, len(visible), max(greens), min(greens)))
    # P1: the island must be VISIBLE in the lower band (most settled frames).
    p1 = len(visible) >= max(2, len(settled) // 2)
    # P2: rotation liveness -- the band's green column profile must change between
    # the highest- and lowest-green visible frames.
    p2 = False
    col_diff = 0.0
    if len(visible) >= 2:
        lo = min(visible, key=lambda t: t[1])
        hi = max(visible, key=lambda t: t[1])
        col_diff = _profiles_differ(lo[2], hi[2])
        p2 = col_diff >= ROT_COL_DIFF_FRAC
        print("  rotation: lower-band green column profile differs by %.1f%% of columns "
              "between %s and %s (need >=%.0f%%)"
              % (col_diff * 100, os.path.basename(lo[0]), os.path.basename(hi[0]),
                 ROT_COL_DIFF_FRAC * 100))
    print("-" * 70)
    print("  P1 island VISIBLE in lower band : %s" % ("PASS" if p1 else "FAIL (uniform sky-blue blank)"))
    print("  P2 island ROTATES (band changes): %s" % ("PASS" if p2 else "FAIL (static / blank)"))
    ok = p1 and p2
    print("RESULT:", "GREEN -- the title island RENDERS (green in the lower band) and "
          "ROTATES (band profile changes %.0f%%)." % (col_diff * 100) if ok else
          "RED -- the title island is %s in the lower band."
          % ("not rotating (static)" if p1 and not p2 else "a uniform sky-blue blank"))
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
