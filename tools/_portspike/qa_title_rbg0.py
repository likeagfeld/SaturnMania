#!/usr/bin/env python3
# =============================================================================
# qa_title_rbg0.py -- CP5b.6 RED-first gate: the title Green Hill island renders
# on VDP2 RBG0 (Mode-7) WITHOUT the prior-art neon-green flood and WITHOUT
# regressing the foreground.
#
# WHY: the prior hand-port RBG0 island (Phase 1.26-1.31) failed with a ~95%
# NEON-GREEN FLOOD (back-screen / out-of-domain dots) + FG corruption, and was
# hidden (Task #106). CP5b.6 re-attempts it engine-side (raw SGL RBG0 in bank B1,
# island palette in CRAM bank 1 + R0CAOS=1, sky back-screen 0xF180, per-frame
# re-apply). This gate is the SAFETY net: it fires RED on the current build (no
# RBG0 armed) and only goes GREEN when RBG0 is armed AND every settled frame has
# ~zero neon-green AND the FG (Sonic head) is intact.
#
# PRIMARY EVIDENCE IS THE SCREENSHOT (the orchestrator opens the burst). The
# witness p6_w_title_rbg0_armed corroborates that the RBG0 setup actually ran.
#
# Verdict (worst settled frame -- never a best-frame picker, per CP5b.5 lesson):
#   STEP 1 (static RBG0): armed==1 AND for EVERY settled (logo>=40000) frame
#     neon_flood <= NEON_MAX AND sonic_head >= HEAD_MIN.
#   STEP 2 (rotation) adds: the island region content changes across the burst
#     (rotation) -- reported, the orchestrator confirms the spin visually.
#
# Usage:
#   python tools/_portspike/qa_title_rbg0.py                # capture + witness + verdict
#   python tools/_portspike/qa_title_rbg0.py --score "GLOB" # score existing pngs
# =============================================================================
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT = os.path.join(ROOT, "tools", "qa_boot.ps1")
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS = os.path.join(HERE, "_title_rbg0.mcs")
SHOT_BASE = "_title_rbg0_shot"
GATE_FRAME = 90.0
BOOT_WAIT, BOOT_EVERY, BOOT_SHOTS = 76.0, 1.5, 40

LOGO_PRESENT_MIN = 40000
HEAD_MIN = 5000          # FG-intact floor (CP5b.5 calibration)
NEON_MAX = 400           # LOWER-BAND neon-green ceiling (prior flood was ~95% = 600k+)
ISLAND_MIN = 2000        # island-green px required in the lower band -> the RBG0 island is visible

NAMES = ["_p6_w_title_rbg0_armed", "_p6_w_title_island_angle", "_p6_w_cont_frames"]


def _purge_lock():
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass


def _neon(a):
    """Prior-art flood signature (g>=200, r<64, b<64) in the LOWER band only (the island
    band, decomp rows 168..239 = client rows ~524+). CALIBRATED 2026-06-22: the title has
    a ~3468px green wing-outline baseline in the UPPER band (#152, present in CP5b.5 too)
    that is NOT an RBG0 flood -- restrict to the lower band so the gate measures a real
    RBG0 island-flood, not the wing edge."""
    import numpy as np
    H = a.shape[0]
    lo = a[int(H * 168.0 / 240.0):H, :]
    r, g, b = lo[..., 0], lo[..., 1], lo[..., 2]
    return int(((g >= 200) & (r < 64) & (b < 64)).sum())


def _sonic_head(a):
    import numpy as np
    h = a[120:420, 300:640]
    hr, hg, hb = h[..., 0], h[..., 1], h[..., 2]
    return int((((hr > 180) & (hg > 120) & (hg < 200) & (hb > 90) & (hb < 180))).sum())


def _logo_mass(a):
    import numpy as np
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx = np.maximum(np.maximum(r, g), b); mn = np.minimum(np.minimum(r, g), b)
    blue_bd = (b > r + 30) & (b > g + 20) & (g > r)
    return int((((mx - mn) >= 55) & (~blue_bd)).sum())


def _island_lower(a):
    """Green island pixels in the lower band (decomp island rows 168..239; in the
    912x749 client that is rows ~524..749). Distinguishes 'island present' from a
    blank/flooded lower screen -- island green = g>r, g>b, g moderate-high."""
    import numpy as np
    H = a.shape[0]
    y0 = int(H * 168.0 / 240.0)
    lo = a[y0:H, :]
    r, g, b = lo[..., 0], lo[..., 1], lo[..., 2]
    return int(((g > r + 20) & (g > b + 10) & (g > 80) & (g < 230)).sum())


def score(png):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(np.int32)
    return _neon(a), _sonic_head(a), _logo_mass(a), _island_lower(a)


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


def read_witnesses(mcs):
    mp = Q.read_text(Q.MAP_DEFAULT)
    present = {n: (Q.map_symbol(mp, n) is not None) for n in NAMES}
    if not os.path.exists(mcs):
        return present, None
    mod = Q.load_harness()
    sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    if perm is None:
        return present, None
    v = {}
    for n in NAMES:
        s = Q.map_symbol(mp, n)
        v[n] = Q.peek_u32(mod, sec, s, perm, signed=True) if s else None
    return present, v


def verdict(pngs, armed):
    print("=" * 70)
    print("CP5b.6 GATE -- title island on VDP2 RBG0, no neon flood, FG intact")
    print("=" * 70)
    settled = []
    for p in pngs:
        try:
            neon, head, logo, isl = score(p)
        except Exception as e:
            print("  %-30s ERR %s" % (os.path.basename(p), e)); continue
        if logo >= LOGO_PRESENT_MIN:
            settled.append((p, neon, head, isl))
            bad = []
            if neon > NEON_MAX: bad.append("NEON-FLOOD")
            if head < HEAD_MIN: bad.append("FG-BROKEN")
            print("  %-30s neon=%6d head=%6d island=%6d %s"
                  % (os.path.basename(p), neon, head, isl, ("<-- " + "+".join(bad)) if bad else ""))
    print("-" * 70)
    print("  witness p6_w_title_rbg0_armed = %s" % Q._dv(armed))
    if not settled:
        print("RESULT: RED -- no settled frames captured."); return 1
    worst_neon = max(settled, key=lambda t: t[1])
    worst_head = min(settled, key=lambda t: t[2])
    worst_isl  = min(settled, key=lambda t: t[3])
    flooded = [t for t in settled if t[1] > NEON_MAX]
    broken = [t for t in settled if t[2] < HEAD_MIN]
    no_island = [t for t in settled if t[3] < ISLAND_MIN]
    armed_ok = (armed == 1)
    print("  settled=%d  flooded=%d  FG-broken=%d  no-island=%d  worst_neon=%d  worst_head=%d  worst_island=%d"
          % (len(settled), len(flooded), len(broken), len(no_island), worst_neon[1], worst_head[2], worst_isl[3]))
    ok = armed_ok and not flooded and not broken and not no_island
    print("-" * 70)
    if ok:
        print("RESULT: GREEN -- RBG0 island visible (island>=%d), 0 lower-band flood, FG intact in all %d frames."
              % (ISLAND_MIN, len(settled)))
    else:
        why = []
        if not armed_ok: why.append("RBG0 NOT armed (witness!=1)")
        if flooded: why.append("%d lower-band neon-flood frame(s)" % len(flooded))
        if broken: why.append("%d FG-broken frame(s)" % len(broken))
        if no_island: why.append("%d frame(s) with NO island (island<%d)" % (len(no_island), ISLAND_MIN))
        print("RESULT: RED -- " + "; ".join(why))
    return 0 if ok else 1


def main(argv):
    if "--score" in argv:
        pat = argv[argv.index("--score") + 1]
        pngs = sorted(glob.glob(os.path.join(ROOT, pat)),
                      key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0]) if p.rsplit("_", 1)[1].split(".")[0].isdigit() else 0)
        return verdict(pngs, None)
    _purge_lock()
    r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame",
                        str(GATE_FRAME), "-Out", TMP_MCS], capture_output=True, text=True)
    sys.stdout.write((r.stdout or "")[-120:])
    present, v = read_witnesses(TMP_MCS)
    armed = (v.get("_p6_w_title_rbg0_armed") if v else None)
    return verdict(capture_burst(), armed)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
