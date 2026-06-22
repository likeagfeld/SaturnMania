#!/usr/bin/env python3
# =============================================================================
# qa_title_fg_stable.py -- CP5b.5 RED-first gate: the title FOREGROUND (Sonic +
# logo) must stay intact in EVERY settled frame, not just the best one.
#
# WHY (MEASURED 2026-06-22): the CP5b.4 build (5131bed) un-gated the TitleBG
# parallax VDP1 sprites (mountains/reflection/water). With ~9 FG rects already
# resident in the P6_VDP1_NSLOTS=10 Title slot cache, the TitleBG sprites push the
# distinct-rect working set past 10 -> LRU THRASH (savestate witness
# p6_w_vdp1_evicts=273, slots=10 full). When the TitleBG sprites scroll on-screen
# the FG (Sonic head/body) slots get evicted and the FOREGROUND BREAKS UP:
#   shot_1/5/40 (TitleBG scrolled off): FG clean, sonic_head ~10778.
#   shot_15/30  (TitleBG on-screen):    Sonic head DISPLACED / body GONE, a
#                                       detached glove + a brown mountain
#                                       fragment + a water-line. head ~0.
# The CP5b.4 gate (qa_titlebg_sprites.py) PICKED THE BEST FRAME (pick_best ->
# shot_1) and passed -- the classic field-gotcha #4 (it measured the best frame,
# not the worst). This gate is the worst-frame counterpart: among the SETTLED
# frames (the "SONIC MANIA" logo present), the MINIMUM Sonic-head pixel mass must
# stay above the FG-intact floor. It fires RED on CP5b.4 (>=1 settled frame with
# head ~0) and GREEN once the thrash source (TitleBG VDP1 sprites) is removed so
# the FG keeps its 10 slots in every frame.
#
# THE PRIMARY EVIDENCE IS THE SCREENSHOT(S) -- the orchestrator OPENS them. This
# gate quantifies the worst-frame regression so it cannot be masked by a single
# clean frame.
#
# Usage:
#   python tools/_portspike/qa_title_fg_stable.py                  # capture + verdict
#   python tools/_portspike/qa_title_fg_stable.py --score "GLOB"   # score existing pngs
# =============================================================================
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT = os.path.join(ROOT, "tools", "qa_boot.ps1")
SHOT_BASE = "_title_fg_stable_shot"

# Same deep real-time burst window as qa_titlebg_sprites (the settled title is in a
# narrow ~76-130s window after boot; dense 1.5s sampling catches both the clean and
# the thrash phases of the TitleBG scroll cycle).
BOOT_WAIT, BOOT_EVERY, BOOT_SHOTS = 76.0, 1.5, 40

LOGO_PRESENT_MIN = 40000  # a frame with the SONIC MANIA logo present == a settled frame
HEAD_MIN = 5000           # the FG-intact floor (CP5b.4 calib: clean ~10778, broken ~0)


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


def _sonic_head(a):
    """Sonic's head peach muzzle/glove pixels in the iconic upper-center pose region.
    Clean settled frame ~10778; a thrash-broken frame (head evicted/displaced) ~0."""
    import numpy as np
    h = a[120:420, 300:640]
    hr, hg, hb = h[..., 0], h[..., 1], h[..., 2]
    return int((((hr > 180) & (hg > 120) & (hg < 200) & (hb > 90) & (hb < 180))).sum())


def score(png):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(np.int32)
    return _sonic_head(a), _logo_mass(a)


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
    print("CP5b.5 GATE -- title FOREGROUND stable across EVERY settled frame")
    print("=" * 70)
    settled = []
    for p in pngs:
        try:
            head, logo = score(p)
        except Exception as e:
            print("  %-34s ERR %s" % (os.path.basename(p), e)); continue
        tag = "settled" if logo >= LOGO_PRESENT_MIN else "intro/transition"
        mark = ""
        if logo >= LOGO_PRESENT_MIN:
            settled.append((p, head, logo))
            mark = "  <-- FG BROKEN" if head < HEAD_MIN else ""
        print("  %-34s head=%6d logo=%7d  [%s]%s" % (os.path.basename(p), head, logo, tag, mark))
    print("-" * 70)
    if not settled:
        print("RESULT: RED -- no settled (logo-present) frames captured; cannot judge.")
        return 1
    worst = min(settled, key=lambda t: t[1])
    broken = [t for t in settled if t[1] < HEAD_MIN]
    print("  settled frames: %d   FG-broken (head<%d): %d" % (len(settled), HEAD_MIN, len(broken)))
    print("  WORST settled frame: %s  head=%d" % (os.path.basename(worst[0]), worst[1]))
    ok = len(broken) == 0
    print("-" * 70)
    print("RESULT:", "GREEN -- FG intact in ALL %d settled frames (min head=%d >= %d)."
          % (len(settled), worst[1], HEAD_MIN) if ok else
          "RED -- %d/%d settled frames have a BROKEN foreground (head<%d). The title FG "
          "is not stable." % (len(broken), len(settled), HEAD_MIN))
    return 0 if ok else 1


def main(argv):
    if "--score" in argv:
        pat = argv[argv.index("--score") + 1]
        pngs = sorted(glob.glob(os.path.join(ROOT, pat)),
                      key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0]) if p.rsplit("_", 1)[1].split(".")[0].isdigit() else 0)
        if not pngs:
            print("RESULT: RED -- no pngs match %s" % pat); return 1
        return verdict(pngs)
    return verdict(capture_burst())


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
