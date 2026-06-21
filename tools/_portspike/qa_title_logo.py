#!/usr/bin/env python3
# =============================================================================
# qa_title_logo.py -- CP5b.1 RED-first VISUAL gate: the SONIC MANIA logo (TitleLogo
# emblem/ribbon/gametitle pieces) actually RENDERS on screen.
#
# WHY (the front-end, render half): CP5a (qa_engine_title.py) proved the engine
# LoadScene + RUN the Title scene -- TitleSetup/TitleLogo register + tick (the title
# state machine reaches FlashIn, the logo pieces flip visible=true). BUT nothing
# visible draws: the Title/Logo.gif sprite sheet was never STAGED, so its gfxSurface
# had saturnSheetSlot==-1, the p6_ghz_arm_env bind loop skipped it, the VDP1 handle
# was <0, and every TitleLogo blit DROPPED -> uniform-blue title (the CP4b Logos
# blue-screen class). CP5b.1 stages TLOGO.SHT (Title/Logo.gif, 512x512 banded) into
# the 11th SaturnSheet slot + grows the front-end VDP1 box to 192x160 (the emblem is
# 144 tall, > the CP4b 96 box) so the logo binds + blits.
#
# THE GATE'S PRIMARY EVIDENCE IS THE SCREENSHOT + A PIXEL MEASURE -- a witness alone
# is NOT enough for a visual bug (field gotcha #4: a green proxy witness while the
# screen is blank/garbage). So this gate captures a burst of DEEP screenshots
# (qa_boot path), picks the frame with the most logo-region pixel mass, and requires:
#   V1  the SCREENSHOT shows logo pixels: the colorful (saturated, non-blue-backdrop,
#       non-black) pixel COUNT in the logo region exceeds a threshold. The SONIC MANIA
#       logo is gold (emblem/SONIC text) + red/white/blue (ribbon) -- highly saturated
#       vs the flat blue backdrop, so a saturation+hue measure isolates it.
#   V2  (corroborating, from a savestate) the render witnesses confirm the bind chain:
#       p6_w_tlogo_handle  >= 0  (Title/Logo.gif surface BOUND to a VDP1 handle)
#       p6_w_tlogo_shtslot >= 0  (TLOGO.SHT staged + hashed)
#       p6_w_vdp1_landed   >  0  (blits reached the framebuffer)
#       (CP5a baseline: tlogo_handle is ABSENT from the map OR <0 -> RED.)
#
# RED on the CURRENT (CP5a) Title build: TLOGO.SHT not staged -> the witnesses are
# ABSENT (or tlogo_handle<0) AND the screenshot is uniform blue (logo pixel mass ~0).
# GREEN once CP5b.1 lands: the logo binds (witnesses GREEN) AND the screenshot shows
# the recognizable SONIC MANIA logo (pixel mass > threshold).
#
#   python tools/_portspike/qa_title_logo.py            # capture burst + savestate + verdict
#   python tools/_portspike/qa_title_logo.py --static   # map-only RED check (witnesses present?)
#   python tools/_portspike/qa_title_logo.py --shots-only  # just (re)capture the PNG burst
# =============================================================================
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT   = os.path.join(ROOT, "tools", "qa_boot.ps1")
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_title_logo.mcs")
SHOT_BASE = "_title_logo_shot"                 # repo-root burst PNGs
BEST_PNG  = os.path.join(HERE, "_title_logo_render.png")  # the chosen deliverable PNG
GATE_FRAME = 90.0                              # savestate frame (CP5a proved title is deep by 90)

# Burst capture window: the title runs deep (CP5a: cont_frames~2923 by frame 90), but
# the WALL-CLOCK content render lags the lean-boot load. Sweep a wide arc and pick the
# frame with the most logo pixel mass (deterministic by content, not by wall-clock).
BOOT_WAIT  = 42.0    # s before the first shot. The lean engine boot + the title state
                     # machine (Wait fade -> AnimateUntilFlash electricity -> FlashIn) take
                     # ~40s+ WALL-CLOCK (real-time burst, NOT the FpsScale savestate) before
                     # the full logo settles in WaitForSonic/WaitForEnter. Earlier windows
                     # sample the flash-in (PARTIAL logo) -- MEASURED: a t=30-48s burst's best
                     # frame was a 5,962 px partial ribbon, not the 125K-px full logo.
BOOT_EVERY = 1.4     # wide spacing across the ~28s full-logo-visible window (FlashIn -> the
                     # WaitForEnter 800-frame timeout -> FadeToVideo); pick-best ignores the
                     # pre-flash + post-fade black edges.
BOOT_SHOTS = 26      # t=42 .. 42+36.4 = ~78s -- robustly spans the settled full-logo state
                     # across run-to-run boot-time variance.

# Render witnesses (savestate corroboration).
NAMES = ["_p6_w_tlogo_handle", "_p6_w_tlogo_shtslot", "_p6_w_tlogo_surfidx",
         "_p6_w_tlogo_surfslot", "_p6_w_tlogo_visible", "_p6_w_tlogo_onscreen",
         "_p6_w_tlogo_sheetid", "_p6_w_vdp1_landed", "_p6_w_cont_frames",
         "_p6_w_title_objcount"]

# Logo pixel-mass thresholds. The Saturn frame is letterboxed inside the Mednafen
# window; measure the WHOLE client area (logo spans the upper-center). "Colorful" =
# saturated AND not the flat blue backdrop. Empirically the blue backdrop is
# (~100,130,222): high blue, low-ish red/green, LOW saturation-vs-that-hue. The logo
# gold/red are high-saturation with red/green dominant -> isolate by (max-min)>S and
# NOT blue-dominant.
SAT_MIN       = 55     # (max-min) over RGB: saturated
LOGO_PIX_MIN  = 40000  # min logo pixels for V1 GREEN. MEASURED: the FULL settled SONIC MANIA
                       # logo (emblem ring + wings + ribbon + SONIC + MANIA) is ~125,000 px; a
                       # PARTIAL flash-in frame (ribbon only) is ~5,962 px. 40,000 cleanly
                       # requires "most of the logo present", rejecting the flash-in/fade frames
                       # the old 1,500 threshold falsely passed (field-gotcha #4: a green gate
                       # over a not-fully-rendered screen).


def _purge_lock():
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass


def capture_burst():
    """qa_boot burst -> repo-root _title_logo_shot_1..N.png. Returns sorted paths."""
    _purge_lock()
    for f in glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")):
        try: os.remove(f)
        except OSError: pass
    r = subprocess.run(
        ["pwsh", "-File", QA_BOOT, "-Cue", "game.cue", "-Wait", str(BOOT_WAIT),
         "-Every", str(BOOT_EVERY), "-Shots", str(BOOT_SHOTS),
         "-Out", SHOT_BASE + ".png", "-Silent"],
        capture_output=True, text=True)
    sys.stdout.write((r.stdout or "")[-200:])
    sys.stderr.write((r.stderr or "")[-400:])
    return sorted(glob.glob(os.path.join(ROOT, SHOT_BASE + "_*.png")),
                  key=lambda p: int(p.rsplit("_", 1)[1].split(".")[0]))


def logo_pixmass(png):
    """Count saturated, non-blue-backdrop pixels (the logo gold/red/white/blue glyphs).
    Returns (count, total) over the client area. The flat blue backdrop and black fade
    are excluded; only the high-saturation logo glyphs survive."""
    from PIL import Image
    import numpy as np
    im = Image.open(png).convert("RGB")
    a = np.asarray(im).astype(np.int32)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    sat = (mx - mn)
    # logo: saturated AND NOT pure-blue-backdrop (backdrop is blue-dominant with
    # green > red; the logo gold/red has red >= green and the ribbon white is bright).
    blue_backdrop = (b > r + 30) & (b > g + 20) & (g > r)
    colorful = (sat >= SAT_MIN) & (~blue_backdrop)
    return int(colorful.sum()), int(a.shape[0] * a.shape[1])


def pick_best(pngs):
    best, best_n = None, -1
    for p in pngs:
        try:
            n, _ = logo_pixmass(p)
        except Exception:
            n = -1
        if n > best_n:
            best, best_n = p, n
    return best, best_n


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


def main(argv):
    mp = Q.read_text(Q.MAP_DEFAULT)
    present = {n: (Q.map_symbol(mp, n) is not None) for n in NAMES}
    print("=" * 70)
    print("CP5b.1 VISUAL GATE -- the SONIC MANIA logo renders on the Title scene")
    print("=" * 70)

    # STATIC RED: render-diag witnesses absent -> the Title-sheet bind/render path is
    # not implemented (the CP5a baseline). tlogo_handle is the load-bearing witness.
    key_absent = [n for n in ("_p6_w_tlogo_handle", "_p6_w_tlogo_shtslot") if not present[n]]
    if "--static" in argv or key_absent:
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        if key_absent:
            print("-" * 70)
            print("RESULT: RED -- %s ABSENT from game.map. The Title-sheet stage + bind "
                  "(TLOGO.SHT + the render-diag) is NOT implemented (CP5a baseline). "
                  "Expected RED before the CP5b.1 fix." % ", ".join(key_absent))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- render witnesses present (run without --static "
                  "for the capture + pixel measure).")
            return 0

    # 1) Burst-capture deep screenshots + pick the frame with the most logo pixel mass.
    if "--png" in argv:
        pngs = [argv[argv.index("--png") + 1]]
    else:
        pngs = capture_burst()
    if not pngs:
        print("RESULT: RED -- no screenshots captured (desktop locked? mednafen failed?).")
        return 1
    best, best_n = pick_best(pngs)
    total = logo_pixmass(best)[1] if best else 0
    # copy the chosen frame to the stable deliverable path (orchestrator opens this).
    if best:
        import shutil
        shutil.copyfile(best, BEST_PNG)
    print("  burst: %d shots; BEST logo-pixel-mass = %d px in %s (-> %s)"
          % (len(pngs), best_n, os.path.basename(best) if best else "-",
             os.path.relpath(BEST_PNG, ROOT)))
    if "--shots-only" in argv:
        return 0

    # 2) Savestate corroboration (the bind chain).
    _purge_lock()
    if "--mcs" in argv:
        mcs = argv[argv.index("--mcs") + 1]
    else:
        mcs = TMP_MCS
        r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame",
                            str(GATE_FRAME), "-Out", mcs], capture_output=True, text=True)
        sys.stdout.write((r.stdout or "")[-160:])
    _, v = read_witnesses(mcs)

    # Verdict.
    v1 = best_n >= LOGO_PIX_MIN
    print("-" * 70)
    print("  [%s] V1 SCREENSHOT shows the logo: %d logo px >= %d (region %d px)"
          % ("GREEN" if v1 else " RED ", best_n, LOGO_PIX_MIN, total))
    ok = v1
    if v is not None:
        h   = v.get("_p6_w_tlogo_handle")
        sht = v.get("_p6_w_tlogo_shtslot")
        lnd = v.get("_p6_w_vdp1_landed")
        c2 = [
            ("V2a Title/Logo.gif surface BOUND (tlogo_handle>=0)", h,   lambda x: x is not None and x >= 0),
            ("V2b TLOGO.SHT staged+hashed (tlogo_shtslot>=0)",     sht, lambda x: x is not None and x >= 0),
            ("V2c blits reached the framebuffer (vdp1_landed>0)",  lnd, lambda x: x is not None and x > 0),
        ]
        for label, val, test in c2:
            try: p = bool(test(val))
            except Exception: p = False
            ok = ok and p
            print("  [%s] %-52s = %s" % ("GREEN" if p else " RED ", label, Q._dv(val)))
        # informational
        for n in ("_p6_w_tlogo_visible", "_p6_w_tlogo_onscreen", "_p6_w_tlogo_sheetid",
                  "_p6_w_tlogo_surfidx", "_p6_w_tlogo_surfslot", "_p6_w_cont_frames",
                  "_p6_w_title_objcount"):
            print("        %-26s = %s" % (n, Q._dv(v.get(n))))
    else:
        print("  [ RED ] V2 witnesses uncalibrated (no savestate) -- V1 pixel measure stands")
        ok = False
    print("-" * 70)
    if ok:
        print("RESULT: GREEN -- the SONIC MANIA logo RENDERS: the screenshot shows %d logo "
              "pixels AND the bind chain is GREEN (TLOGO.SHT staged -> Title/Logo.gif "
              "surface bound -> blits landed). Deliverable PNG: %s"
              % (best_n, os.path.relpath(BEST_PNG, ROOT)))
        return 0
    print("RESULT: RED -- the logo does not render. See the RED check(s) above. The PNG "
          "(%s) is the visual evidence." % os.path.relpath(BEST_PNG, ROOT))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
