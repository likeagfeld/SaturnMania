#!/usr/bin/env python3
# =============================================================================
# qa_title_backdrop.py -- CP5b.3 RED-first VISUAL gate: the Mania title BACKDROP
# (green floating island + clouds/sky) renders BEHIND Sonic.
#
# WHY: the engine Title (P6_FRONTEND_TITLE, CP5b.2) put the SONIC MANIA logo +
# Sonic's head/finger-wave on screen, but the ENTIRE VDP2 BACKDROP is BLACK --
# the green floating island (the Sonic-head-shaped island in the water), the
# clouds, and the sky horizon are all missing. CP5b.3 ports TitleBG +
# Title3DSprite (the mountains/water/billboard VDP1 sprites) AND lights up the
# VDP2 island/cloud backdrop so the title matches the real Mania title.
#
# THE GATE'S PRIMARY EVIDENCE IS THE SCREENSHOT (the orchestrator OPENS it and
# compares to the real Mania title). The pixel measures below encode the
# backdrop-presence symptom so the gate fires RED on the black-backdrop build
# and GREEN only once backdrop pixels are actually there.
#
# CALIBRATED (MEASURED on the CP5b.2 BLACK-backdrop baseline _bl_title_14.png,
# 912x749 Mednafen client area):
#   BOTTOM-CENTER region y[644:749] x[182:729]: island/water region.
#       black baseline: island_green = 0  (clean -- the logo banner ends above it)
#   TOP-CORNERS region (L x[0:200] + R x[711:912], y[0:134]): sky/cloud region.
#       black baseline: sky_blue = 0/9 (clean -- the logo wings don't reach the corners)
# So a backdrop that renders the green island + sky makes BOTH rise well above 0.
#
# V1 (SCREENSHOT, primary): island_green (BOTTOM-CENTER) >= ISLAND_MIN
#                       AND  sky_blue   (TOP-CORNERS)   >= SKY_MIN
# V2 (corroborating, savestate witnesses): the backdrop arm + TitleBG bind chain.
#
# RED on the CURRENT (CP5b.2) build: black backdrop -> island_green=0, sky_blue=0.
# GREEN once CP5b.3 lands: the island + sky render -> both clear the thresholds.
#
# Usage:
#   python tools/_portspike/qa_title_backdrop.py            # capture burst + verdict
#   python tools/_portspike/qa_title_backdrop.py --shots GLOB  # score existing pngs
# =============================================================================
import os, subprocess, sys, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT = os.path.join(ROOT, "tools", "qa_boot.ps1")
SHOT_BASE = "_title_backdrop_shot"
BEST_PNG = os.path.join(HERE, "_title_backdrop.png")

# Same deep real-time burst window as qa_title_sonic (the Title flavor settles
# ~70s+ wall-clock after the long TSONIC.SHT load).
BOOT_WAIT = 66.0
BOOT_EVERY = 4.0
BOOT_SHOTS = 16

# Regions (912x749 client area). CALIBRATED black-baseline = 0 in both.
BOT = (182, 644, 729, 749)       # island / water (below the logo banner)
TOPL = (0, 0, 200, 134)          # sky / cloud, left corner
TOPR = (711, 0, 912, 134)        # sky / cloud, right corner

# Thresholds (CALIBRATED, MEASURED on _bl_title_14.png black baseline vs the
# CP5b.3 backdrop build):
#   island_green (BOTTOM-CENTER, strict grass-green): black baseline 0, backdrop
#       reveal-state 29,000-34,000. This is the UNAMBIGUOUS backdrop-present signal.
#   sky_cloud (TOP-CORNERS, sky-blue OR bright cloud-white): black baseline 10,139
#       (Sonic-blue wing-fringe bleed into the corners), backdrop 52,799. The 25,000
#       floor sits 2.5x over the baseline-fringe, 2x under the backdrop.
ISLAND_MIN = 1500    # strict grass-green px in BOTTOM-CENTER (baseline 0; backdrop ~32k)
SKY_MIN = 25000      # sky-blue OR cloud-white px in the top corners (baseline 10k; backdrop 53k)

# A frame only counts if the settled logo is present (rejects the blue LOAD
# screen, which floods blue everywhere but has no logo) -- field gotcha #4.
LOGO_PRESENT_MIN = 40000


def _purge_lock():
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass


def capture_burst():
    _purge_lock()
    for f in glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")):
        try:
            os.remove(f)
        except OSError:
            pass
    r = subprocess.run(
        ["pwsh", "-File", QA_BOOT, "-Cue", "game.cue", "-Wait", str(BOOT_WAIT),
         "-Every", str(BOOT_EVERY), "-Shots", str(BOOT_SHOTS),
         "-Out", SHOT_BASE + ".png", "-Silent"], capture_output=True, text=True,
        cwd=ROOT)
    sys.stdout.write((r.stdout or "")[-200:])
    sys.stderr.write((r.stderr or "")[-400:])
    return sorted(glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")),
                  key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0]))


def _logo_mass(a):
    import numpy as np
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    blue_bd = (b > r + 30) & (b > g + 20) & (g > r)
    return int((((mx - mn) >= 55) & (~blue_bd)).sum())


def _island_green(a, box):
    import numpy as np
    x0, y0, x1, y1 = box
    s = a[y0:y1, x0:x1]
    r, g, b = s[..., 0], s[..., 1], s[..., 2]
    # Mania island = saturated grass green (g dominant over BOTH r and b).
    m = (g > r + 40) & (g > b + 30) & (g > 80)
    return int(m.sum())


def _sky_blue(a, box):
    import numpy as np
    x0, y0, x1, y1 = box
    s = a[y0:y1, x0:x1]
    r, g, b = s[..., 0], s[..., 1], s[..., 2]
    # Mania sky-region backdrop = sky-blue OR bright cloud-white. (Sky-blue: b high,
    # bluer than red, g>r. Cloud: bright white-blue.) Distinct from the black
    # baseline where the only blue is the Sonic-spike wing-fringe in the corners.
    skyblue = (b > 120) & (b > r + 20) & (g > r)
    cloud = (r > 170) & (g > 190) & (b > 200)
    return int((skyblue | cloud).sum())


def score(png):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(np.int32)
    logo = _logo_mass(a)
    isl = _island_green(a, BOT)
    sky = _sky_blue(a, TOPL) + _sky_blue(a, TOPR)
    return isl, sky, logo


def pick_best(pngs):
    """Pick the settled-title frame (logo present) with the most backdrop
    evidence (island_green + sky_blue)."""
    best, best_n = None, -1
    for p in pngs:
        try:
            isl, sky, logo = score(p)
        except Exception:
            isl, sky, logo = 0, 0, 0
        if logo < LOGO_PRESENT_MIN:
            continue
        n = isl + sky
        if n > best_n:
            best, best_n = p, n
    return best


def main():
    import shutil
    args = sys.argv[1:]
    if "--shots" in args:
        pngs = sorted(glob.glob(args[args.index("--shots") + 1]))
    else:
        pngs = capture_burst()
    if not pngs:
        print("RESULT: RED -- no screenshots (desktop locked? mednafen failed?).")
        return 1
    best = pick_best(pngs)
    if best is None:
        # No settled-title frame -> report the best-looking by raw backdrop.
        best = max(pngs, key=lambda p: sum(score(p)[:2]))
    isl, sky, logo = score(best)
    try:
        shutil.copyfile(best, BEST_PNG)
    except Exception:
        pass
    print(f"best={os.path.basename(best)}  island_green={isl}  sky_blue={sky}  logo_mass={logo}")
    print(f"thresholds: island>={ISLAND_MIN}  sky>={SKY_MIN}  logo>={LOGO_PRESENT_MIN}")
    print(f"screenshot -> {BEST_PNG}")
    ok = (isl >= ISLAND_MIN) and (sky >= SKY_MIN) and (logo >= LOGO_PRESENT_MIN)
    print("RESULT:", "GREEN -- backdrop present (island + sky behind Sonic)." if ok
          else "RED -- backdrop missing/insufficient.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
