#!/usr/bin/env python3
# =============================================================================
# qa_title_sonic.py -- CP5b.2 RED-first VISUAL gate: TitleSonic (Sonic's head +
# finger-wave) renders in the CENTER of the title ring emblem.
#
# WHY (the front-end title, character half): CP5b.1 (qa_title_logo.py) put the full
# SONIC MANIA logo on screen, but the gold emblem RING's interior is still the black
# backdrop -- in the real title screen Sonic's head pops up there (TitleSonic anim 0)
# and waves a finger (anim 1) on the body anim's last frame. CP5b.2 registers the
# verbatim TitleSonic (currently a NULL closure stub in p6_ovl_ghz.c) + stages its
# sprite sheet (Title/Sonic.gif -> the 12th SaturnSheet slot, the TLOGO.SHT mirror)
# so the head binds + blits into the ring center.
#
# THE GATE'S PRIMARY EVIDENCE IS THE SCREENSHOT + A PIXEL MEASURE (field gotcha #4:
# a green witness over a head-shaped hole is still a fail). Sonic's head is distinct
# from the gold/red logo around it -- it adds Sonic-BLUE + tan-muzzle pixels INSIDE
# the ring interior (a region that was ~black backdrop in CP5b.1). So:
#   V1  the SCREENSHOT shows the head: Sonic-blue pixel COUNT inside the ring-interior
#       region exceeds a threshold (the CP5b.1 baseline there is ~0 -- black backdrop).
#   V2  (corroborating, savestate) the bind chain:
#       p6_w_tsonic_handle  >= 0  (Title/Sonic.gif surface BOUND to a VDP1 handle)
#       p6_w_tsonic_shtslot >= 0  (the Sonic sheet staged + hashed)
#       p6_w_tsonic_visible ==  1 (TitleSetup_State_FlashIn flipped it visible)
#       p6_w_vdp1_landed    >   0 (blits reached the framebuffer)
#       (CP5b.1 baseline: tsonic_handle is ABSENT from the map OR <0 -> RED.)
#
# RED on the CURRENT (CP5b.1) Title build: TitleSonic is the NULL stub, its sheet is
# not staged -> the witnesses are ABSENT AND the ring interior is black (head px ~0).
# GREEN once CP5b.2 lands: the head binds (witnesses GREEN) AND the screenshot shows
# Sonic's head in the ring (head px > threshold).
#
# NOTE (impl agent): CALIBRATE RING_BOX + HEAD_PIX_MIN by MEASURING -- capture the
# CP5b.1 build (head absent -> ring interior near-zero blue) AND the CP5b.2 build
# (head present), and set the box to the ring interior + the threshold cleanly between
# the two. The defaults below are seeded from the CP5b.1 _title_logo_render.png ring
# geometry (912x749 window) and MUST be re-measured, not trusted.
#
#   python tools/_portspike/qa_title_sonic.py            # capture burst + savestate + verdict
#   python tools/_portspike/qa_title_sonic.py --static   # map-only RED check (witnesses present?)
#   python tools/_portspike/qa_title_sonic.py --shots-only
# =============================================================================
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT   = os.path.join(ROOT, "tools", "qa_boot.ps1")
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_title_sonic.mcs")
SHOT_BASE = "_title_sonic_shot"
BEST_PNG  = os.path.join(HERE, "_title_sonic_render.png")
GATE_FRAME = 90.0

# Deep real-time burst window. CP5b.2 (MEASURED): the front-end Title flavor now
# stages a 12th sheet (TSONIC.SHT, 1024x1024) -- the longer load pushes the settled
# title to ~70s+ wall-clock (the t=42..78s logo-gate window lands mostly in the blue
# load). Window t=66..126s @ 4s spacing robustly spans the settled full-title state
# (logo + Sonic) across run-to-run boot-time variance.
BOOT_WAIT  = 66.0
BOOT_EVERY = 4.0
BOOT_SHOTS = 16

NAMES = ["_p6_w_tsonic_handle", "_p6_w_tsonic_shtslot", "_p6_w_tsonic_visible",
         "_p6_w_tsonic_onscreen", "_p6_w_tsonic_sheetid", "_p6_w_vdp1_landed",
         "_p6_w_cont_frames", "_p6_w_title_objcount"]

# RING INTERIOR box (x0,y0,x1,y1) in the 912x749 Mednafen client area -- the region
# where Sonic's head/body pops out of the gold ring (his blue spikes extend ABOVE the
# ring top, so y starts at 90, ABOVE the emblem). CALIBRATED (CP5b.2, MEASURED on the
# 912x749 window): this box gives the cleanest head-absent vs head-present separation:
#   CP5b.1 baseline (head ABSENT, black ring interior + blue rim bleed) = 6,024 blue px
#   CP5b.2 (head PRESENT)                                               = 23,657 blue px
# HEAD_PIX_MIN = 13,000 sits cleanly between (2.2x over the 6,024 baseline rim floor,
# 1.8x under the 23,657 head). The blue RING RIM is itself Sonic-blue, which is why the
# baseline floor is ~6,000 not 0 -- the threshold clears it.
RING_BOX      = (360, 90, 560, 390)
SONIC_BLUE_MIN = 25     # Sonic's head is a saturated blue (b dominant, b-r and b-g gaps)
HEAD_PIX_MIN   = 13000  # min Sonic-blue px in the ring region for V1 (CALIBRATED: 6024 absent -> 23657 present)
# A blue LOAD screen saturates the ring box with blue (MEASURED 60,000 px) but has NO
# logo (logo_mass ~58). The settled title has the FULL logo (logo_mass >100,000). So a
# frame only counts as a head candidate if the logo is present -- this rejects the blue-
# boot frame (field gotcha #4: the gate must isolate the head, not any blue). MEASURED:
# CP5b.1 baseline logo_mass=128,365 ; CP5b.2 head=139,033 ; blue-boot=58.
LOGO_PRESENT_MIN = 40000  # the logo-gate's settled-logo threshold (full logo ~125-139K)


def _purge_lock():
    try: os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError: pass


def capture_burst():
    _purge_lock()
    for f in glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")):
        try: os.remove(f)
        except OSError: pass
    r = subprocess.run(
        ["pwsh", "-File", QA_BOOT, "-Cue", "game.cue", "-Wait", str(BOOT_WAIT),
         "-Every", str(BOOT_EVERY), "-Shots", str(BOOT_SHOTS),
         "-Out", SHOT_BASE + ".png", "-Silent"], capture_output=True, text=True)
    sys.stdout.write((r.stdout or "")[-200:]); sys.stderr.write((r.stderr or "")[-400:])
    return sorted(glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")),
                  key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0]))


def _logo_mass(a):
    """Saturated, non-blue-backdrop pixels = the SONIC MANIA logo glyphs (gold/red/
    white). The flat blue load screen has ~0 of these; the settled logo has >100K.
    (Identical measure to qa_title_logo.py -- used here only to REJECT blue-load frames
    so the head measure isolates the head, not any blue.)"""
    import numpy as np
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    blue_bd = (b > r + 30) & (b > g + 20) & (g > r)
    return int((((mx - mn) >= 55) & (~blue_bd)).sum())


def head_mass(png):
    """Count Sonic-blue pixels in the ring region (his head/body popping out of the
    ring; the CP5b.1 baseline there is the black hole + blue-rim bleed ~6,024).
    Returns (head_blue, logo_mass) -- logo_mass gates out the blue LOAD screen (which
    saturates the box with blue but has no logo)."""
    from PIL import Image
    import numpy as np
    im = Image.open(png).convert("RGB")
    a = np.asarray(im).astype(np.int32)
    logo = _logo_mass(a)
    x0, y0, x1, y1 = RING_BOX
    s = a[y0:y1, x0:x1]
    r, g, b = s[..., 0], s[..., 1], s[..., 2]
    sonic_blue = (b > r + SONIC_BLUE_MIN) & (b > g + SONIC_BLUE_MIN) & (b > 90)
    return int(sonic_blue.sum()), logo


def pick_best(pngs):
    """Pick the frame with the most ring-region head-blue AMONG frames where the full
    logo is present (logo_mass >= LOGO_PRESENT_MIN). The logo gate rejects the blue
    LOAD screen (which has more ring-box blue than the head, but no logo) -- field
    gotcha #4: the gate must isolate the head, not any blue."""
    best, best_n = None, -1
    for p in pngs:
        try:
            hb, logo = head_mass(p)
        except Exception:
            hb, logo = -1, 0
        if logo < LOGO_PRESENT_MIN:
            continue  # not the settled title (blue load / partial flash-in) -> skip
        if hb > best_n:
            best, best_n = p, hb
    return best, best_n


def read_witnesses(mcs):
    mp = Q.read_text(Q.MAP_DEFAULT)
    if not os.path.exists(mcs): return None
    mod = Q.load_harness(); sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    if perm is None: return None
    return {n: (Q.peek_u32(mod, sec, Q.map_symbol(mp, n), perm, signed=True)
                if Q.map_symbol(mp, n) else None) for n in NAMES}


def main(argv):
    mp = Q.read_text(Q.MAP_DEFAULT)
    present = {n: (Q.map_symbol(mp, n) is not None) for n in NAMES}
    print("=" * 70)
    print("CP5b.2 VISUAL GATE -- TitleSonic head + finger-wave in the ring center")
    print("=" * 70)
    key_absent = [n for n in ("_p6_w_tsonic_handle", "_p6_w_tsonic_shtslot") if not present[n]]
    if "--static" in argv or key_absent:
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        if key_absent:
            print("-" * 70)
            print("RESULT: RED -- %s ABSENT. TitleSonic register + sheet-stage (the NULL "
                  "closure stub is still in place) is NOT implemented (CP5b.1 baseline)."
                  % ", ".join(key_absent))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- witnesses present (run without --static for capture)."); return 0

    if "--png" in argv:
        pngs = [argv[argv.index("--png") + 1]]
    else:
        pngs = capture_burst()
    if not pngs:
        print("RESULT: RED -- no screenshots (desktop locked? mednafen failed?)."); return 1
    best, best_n = pick_best(pngs)
    if best:
        import shutil; shutil.copyfile(best, BEST_PNG)
    print("  burst: %d shots; BEST ring-interior head-mass = %d px in %s (-> %s)"
          % (len(pngs), best_n, os.path.basename(best) if best else "-",
             os.path.relpath(BEST_PNG, ROOT)))
    if "--shots-only" in argv: return 0

    _purge_lock()
    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else TMP_MCS
    if "--mcs" not in argv:
        r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame",
                            str(GATE_FRAME), "-Out", mcs], capture_output=True, text=True)
        sys.stdout.write((r.stdout or "")[-160:])
    v = read_witnesses(mcs)

    v1 = best_n >= HEAD_PIX_MIN
    print("-" * 70)
    print("  [%s] V1 SCREENSHOT shows the head: %d Sonic-blue px in ring >= %d"
          % ("GREEN" if v1 else " RED ", best_n, HEAD_PIX_MIN))
    ok = v1
    if v is not None:
        c2 = [
            ("V2a Title/Sonic.gif surface BOUND (tsonic_handle>=0)", v.get("_p6_w_tsonic_handle"), lambda x: x is not None and x >= 0),
            ("V2b Sonic sheet staged+hashed (tsonic_shtslot>=0)",    v.get("_p6_w_tsonic_shtslot"), lambda x: x is not None and x >= 0),
            ("V2c TitleSonic flipped visible (tsonic_visible==1)",   v.get("_p6_w_tsonic_visible"), lambda x: x == 1),
            ("V2d blits reached the framebuffer (vdp1_landed>0)",    v.get("_p6_w_vdp1_landed"),    lambda x: x is not None and x > 0),
        ]
        for label, val, test in c2:
            try: p = bool(test(val))
            except Exception: p = False
            ok = ok and p
            print("  [%s] %-52s = %s" % ("GREEN" if p else " RED ", label, Q._dv(val)))
        for n in ("_p6_w_tsonic_onscreen", "_p6_w_tsonic_sheetid", "_p6_w_cont_frames", "_p6_w_title_objcount"):
            print("        %-24s = %s" % (n, Q._dv(v.get(n))))
    else:
        print("  [ RED ] V2 witnesses uncalibrated (no savestate) -- V1 stands"); ok = False
    print("-" * 70)
    if ok:
        print("RESULT: GREEN -- TitleSonic's head RENDERS in the ring center: %d blue px AND the "
              "bind chain is GREEN. Deliverable PNG: %s" % (best_n, os.path.relpath(BEST_PNG, ROOT)))
        return 0
    print("RESULT: RED -- the head does not render. See the RED check(s). PNG: %s"
          % os.path.relpath(BEST_PNG, ROOT))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
