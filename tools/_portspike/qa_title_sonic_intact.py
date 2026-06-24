#!/usr/bin/env python3
# =============================================================================
# qa_title_sonic_intact.py -- catches the INTERMITTENT "Sonic derezzes" garble
# (RBG0 RPT-clobber renders horizontal-stripe garbage over Sonic on SOME frames).
#
# v2 metric = FRAME-TO-FRAME DIFFERENCE from the median, in Sonic's head/torso
# region. When the title is settled, Sonic's head+body are STATIC, so a clean
# burst's frames are all ~identical there (the median IS the clean Sonic). A
# derezzed frame (stripe garbage over Sonic) differs HUGELY from the median.
# (v1 measured peach-mass, which only saw the static gold ring -> useless.)
#
# Region excludes the lower island band (y>0.55) and most of the waving finger.
#   GREEN = every frame is close to the median (no garble; max diff < FLOOR)
#   RED   = >=1 frame spikes (Sonic derezzed that frame)
#
#   python tools/_portspike/qa_title_sonic_intact.py            # capture + verdict
#   python tools/_portspike/qa_title_sonic_intact.py --score "GLOB"
# =============================================================================
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT = os.path.join(ROOT, "tools", "qa_boot.ps1")
SHOT_BASE = "_sonic_intact_shot"

BOOT_WAIT, BOOT_EVERY, BOOT_SHOTS = 47.0, 0.5, 24
RX0, RX1 = 0.36, 0.66      # Sonic head + torso (centre)
RY0, RY1 = 0.10, 0.50
DIFF_FLOOR = 22.0          # mean abs per-channel diff from median; clean ~ <12, derez >> 30


def _purge_lock():
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass


def _region(png):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(np.float32)
    H, W, _ = a.shape
    return a[int(H * RY0):int(H * RY1), int(W * RX0):int(W * RX1)]


def capture_burst():
    _purge_lock()
    for f in glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")):
        try:
            os.remove(f)
        except OSError:
            pass
    subprocess.run(["pwsh", "-File", QA_BOOT, "-Cue", "game.cue", "-Wait", str(BOOT_WAIT),
                    "-Every", str(BOOT_EVERY), "-Shots", str(BOOT_SHOTS),
                    "-Out", SHOT_BASE + ".png", "-Silent"],
                   capture_output=True, text=True, cwd=ROOT)
    return sorted(glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")),
                  key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0]))


def verdict(pngs):
    import numpy as np
    print("=" * 70)
    print("SONIC-INTACT GATE v2 -- frame-vs-median diff in Sonic's region")
    print("=" * 70)
    if len(pngs) < 3:
        print("RESULT: RED -- need >=3 frames to form a median (%d)." % len(pngs)); return 1
    regs, ok = [], []
    for p in pngs:
        try:
            regs.append(_region(p)); ok.append(p)
        except Exception as e:
            print("  %-30s ERR %s" % (os.path.basename(p), e))
    shp = min(r.shape for r in regs)
    regs = [r[:shp[0], :shp[1]] for r in regs]
    stack = np.stack(regs, axis=0)
    median = np.median(stack, axis=0)
    diffs = [(ok[i], float(np.mean(np.abs(regs[i] - median)))) for i in range(len(regs))]
    derez = [t for t in diffs if t[1] >= DIFF_FLOOR]
    for p, d in diffs:
        flag = "  <-- DEREZ (Sonic garbled)" if d >= DIFF_FLOOR else ""
        print("  %-30s diff=%6.1f%s" % (os.path.basename(p), d, flag))
    mx = max(d for _, d in diffs)
    print("-" * 70)
    print("  frames=%d  max_diff=%.1f  floor=%.1f  derez_frames=%d" % (len(diffs), mx, DIFF_FLOOR, len(derez)))
    if not derez:
        print("RESULT: GREEN -- Sonic intact in ALL %d frames (max diff %.1f < %.1f)."
              % (len(diffs), mx, DIFF_FLOOR)); return 0
    print("RESULT: RED -- Sonic DEREZZES in %d/%d frames (max diff %.1f >= %.1f)."
          % (len(derez), len(diffs), mx, DIFF_FLOOR)); return 1


def main(argv):
    if "--score" in argv:
        pat = argv[argv.index("--score") + 1]
        pngs = sorted(glob.glob(os.path.join(ROOT, pat)),
                      key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0])
                      if p.rsplit("_", 1)[1].split(".")[0].isdigit() else 0)
        return verdict(pngs)
    return verdict(capture_burst())


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
