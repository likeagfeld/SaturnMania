#!/usr/bin/env python3
# =============================================================================
# qa_title_island_match.py -- the RIGHT island gate. Measures the USER's actual
# complaint ("island appears small / only 1 quarter / not the whole island /
# not centered") instead of raw green-mass (the prior qa_title_island_pixel.py
# rewarded the agent's distorted green FLOOD and so optimized the wrong thing).
#
# GROUND TRUTH: sim_island_decomp.py proved the Saturn RBG0 formula == the decomp
# TitleBG_Scanline_Island EXACTLY (band mean|diff| 0.00 at every angle) WHEN the
# params are decomp-exact (CTR_TEXEL=512, My swing=0xA000). Sweeping all 1024
# angles, decomp-exact keeps the island land-mass TIGHT (frac 0.28..0.37) and
# CENTERED (centroid frac 0.44..0.56, range 0.12); the agent params (448,0x5000)
# swing the centroid 0.22..0.74 (range 0.51) and the mass 0.08..0.54 (tiny
# fragment <-> green flood). Those ENVELOPES are the gate.
#
# Per settled frame, in the lower band (island lives screen-y[168,240)):
#   land_frac    = (grass-green | orange-muzzle) pixels / band pixels
#   centroid_frac= mean land x / width
# Across the burst (the rotation sweeps many angles):
#   GREEN: island present every frame (land_frac >= PRESENT) AND never floods
#          (land_frac <= FLOOD) AND stays centered (centroid range <= CENT_RANGE)
#   RED  : any tiny/blank frame, any flood frame, or a wide centroid swing
#          = "only part of the island, off-center" (the reported symptom)
#
#   python tools/_portspike/qa_title_island_match.py            # capture + verdict
#   python tools/_portspike/qa_title_island_match.py --score "GLOB"
#   python tools/_portspike/qa_title_island_match.py --sim      # self-test (no HW)
# =============================================================================
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT = os.path.join(ROOT, "tools", "qa_boot.ps1")
SHOT_BASE = "_island_match_shot"

BOOT_WAIT, BOOT_EVERY, BOOT_SHOTS = 47.0, 0.5, 24
BAND_Y0, BAND_Y1 = 0.66, 1.00      # lower band: 224-line display island y[152,224)/224 = 0.68..1.0

# Envelope thresholds (sim-derived; calibration note appended after first HW run).
PRESENT     = 0.10     # land frac floor: island substantial. HW-CALIBRATED 2026-06-23:
                       # the Saturn CRAM renders the island green/orange darker than the
                       # source palette, so the classifier catches ~0.40x the sim mass. The
                       # FULL repaired island (head shifted into page0) measures 0.11..0.14
                       # on HW; the BROKEN 1-of-4-quadrant builds measured 0.04..0.09. 0.10
                       # cleanly separates fixed-vs-broken (sim self-test floor stays the same
                       # because the sim uses the bright source palette -> 0.28..0.37).
FLOOD       = 0.46     # land frac ceil: not a green flood (agent flood=0.544 fails)
CENT_RANGE  = 0.20     # centroid frac span across burst (decomp 0.12; agent 0.51 fails)
MIN_PRESENT_FRAMES = 0.85   # fraction of frames that must have the island present


def _purge_lock():
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass


def land_metrics(rgb):
    """(land_frac, centroid_frac) in the lower band. rgb = HxWx3 uint8 array."""
    import numpy as np
    H, W, _ = rgb.shape
    band = rgb[int(H * BAND_Y0):int(H * BAND_Y1)].astype(int)
    r, g, b = band[:, :, 0], band[:, :, 1], band[:, :, 2]
    green  = (g > 110) & (g > r + 30) & (g > b + 30)
    orange = (r > 150) & (g > 60) & (g < 200) & (b < 110) & (r > b + 60)
    land = green | orange
    n = int(land.sum())
    bp = band.shape[0] * band.shape[1]
    if n == 0:
        return 0.0, -1.0
    xs = np.where(land)[1]
    return n / bp, float(xs.mean()) / band.shape[1]


def verdict(frames_rgb, label="capture"):
    import numpy as np
    print("=" * 72)
    print("ISLAND-MATCH GATE -- %s (decomp-exact envelope)" % label)
    print("=" * 72)
    if len(frames_rgb) < 4:
        print("RESULT: RED -- need >=4 settled frames (%d)." % len(frames_rgb)); return 1
    fr_frac, cents = [], []
    for i, rgb in enumerate(frames_rgb):
        lf, cf = land_metrics(rgb)
        fr_frac.append(lf); cents.append(cf)
    present = [i for i, lf in enumerate(fr_frac) if lf >= PRESENT]
    flooded = [i for i, lf in enumerate(fr_frac) if lf > FLOOD]
    cent_present = [c for c in cents if c >= 0]
    crange = (max(cent_present) - min(cent_present)) if cent_present else 1.0
    for i, (lf, cf) in enumerate(zip(fr_frac, cents)):
        tag = ""
        if lf < PRESENT: tag += " TINY/BLANK"
        if lf > FLOOD:   tag += " FLOOD"
        print("  f%02d  land=%.3f  centroid=%s%s"
              % (i, lf, ("%.3f" % cf) if cf >= 0 else " none", tag))
    pres_frac = len(present) / len(fr_frac)
    print("-" * 72)
    print("  frames=%d  present=%.0f%%  flood=%d  centroid_range=%.3f (max %.3f)"
          % (len(fr_frac), pres_frac * 100, len(flooded), crange, CENT_RANGE))
    print("  land_frac min/med/max=%.3f/%.3f/%.3f  (PRESENT>=%.2f FLOOD<=%.2f)"
          % (min(fr_frac), float(np.median(fr_frac)), max(fr_frac), PRESENT, FLOOD))
    ok = (pres_frac >= MIN_PRESENT_FRAMES) and (len(flooded) == 0) and (crange <= CENT_RANGE)
    if ok:
        print("RESULT: GREEN -- full island present + centered across the rotation."); return 0
    why = []
    if pres_frac < MIN_PRESENT_FRAMES: why.append("tiny/blank frames (island only partly shown)")
    if len(flooded): why.append("flood frames (distorted oversample)")
    if crange > CENT_RANGE: why.append("centroid swings %.3f>%.3f (not centered)" % (crange, CENT_RANGE))
    print("RESULT: RED -- " + "; ".join(why)); return 1


def _load(pngs):
    from PIL import Image
    import numpy as np
    out = []
    for p in pngs:
        try:
            out.append(np.asarray(Image.open(p).convert("RGB")))
        except Exception as e:
            print("  skip %s (%s)" % (os.path.basename(p), e))
    return out


def capture_burst():
    _purge_lock()
    for f in glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")):
        try: os.remove(f)
        except OSError: pass
    subprocess.run(["pwsh", "-File", QA_BOOT, "-Cue", "game.cue", "-Wait", str(BOOT_WAIT),
                    "-Every", str(BOOT_EVERY), "-Shots", str(BOOT_SHOTS),
                    "-Out", SHOT_BASE + ".png", "-Silent"],
                   capture_output=True, text=True, cwd=ROOT)
    return sorted(glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")),
                  key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0]))


def sim_selftest():
    """Prove the gate separates decomp-exact (GREEN) from agent (RED), no HW."""
    sys.path.insert(0, os.path.join(ROOT, "tools", "_portspike"))
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import sim_island_decomp as S
    tex = S.build_island_rgba()
    angles = list(range(0, 1024, 1024 // 24))      # 24 angles spanning the cycle
    exact = [S.render_saturn(tex, a, 512, 0xA000) for a in angles]
    agent = [S.render_saturn(tex, a, 448, 0x5000) for a in angles]
    re = verdict(exact, "SIM decomp-exact(512,0xA000) -- expect GREEN")
    ra = verdict(agent, "SIM agent(448,0x5000) -- expect RED")
    ok = (re == 0 and ra == 1)
    print("\nSELF-TEST: %s (exact=%s agent=%s)"
          % ("PASS" if ok else "FAIL", "GREEN" if re == 0 else "RED",
             "RED" if ra == 1 else "GREEN"))
    return 0 if ok else 1


def main(argv):
    if "--sim" in argv:
        return sim_selftest()
    if "--score" in argv:
        pat = argv[argv.index("--score") + 1]
        pngs = sorted(glob.glob(os.path.join(ROOT, pat)),
                      key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0])
                      if p.rsplit("_", 1)[1].split(".")[0].isdigit() else 0)
        return verdict(_load(pngs))
    return verdict(_load(capture_burst()))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
