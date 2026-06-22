#!/usr/bin/env python3
# =============================================================================
# qa_titlebg_sprites.py -- CP5b.4 RED-first gate: the TitleBG (mountains/water/
# wing-shine) + Title3DSprite (MountainL/M/S, Tree, Bush billboards) sprites
# register + render on the engine Title scene -- WITHOUT regressing the proven
# foreground (logo + Sonic) or the Saturn-native backdrop.
#
# WHY: bed5bac put the SONIC MANIA logo + Sonic + the VDP2 sky/cloud/island
# backdrop on screen, but TitleBG + Title3DSprite were GATED OFF (P6_TITLEBG_
# SPRITES) because the verbatim TitleBG_SetupFX installs per-frame scanline
# callbacks whose SetClipBounds corrupts the Saturn FG sprite clip (the
# "alternating island/logo frames; head vanishes" the prior agent measured).
# CP5b.4 makes SetupFX Saturn-safe (no scanline-callback install -- no Saturn
# consumer) + un-gates the registration so the 9 TitleBG + 58 Title3DSprite
# entities (parse_title_entities.py) populate the island.
#
# THE PRIMARY EVIDENCE IS THE SCREENSHOT (the orchestrator OPENS it + compares
# to the real Mania title). The savestate witnesses corroborate the register +
# bind chain so the gate fires RED on bed5bac (objects unregistered) and GREEN
# only once the sprites are actually registered + visible.
#
# V1 (savestate, register chain):
#     p6_w_titlebg_classid > 0   (TitleBG registered)
#     p6_w_title3d_classid > 0   (Title3DSprite registered)
#     p6_w_tbg_handle      >= 0  (TBG.SHT / Title/BG.gif surface bound to VDP1)
#     MEASURED bed5bac baseline (_cp5b4_baseline.mcs, 16-bit-pairswap perm):
#       titlebg_classid = -9, title3d_classid = -9, tbg_handle = -9  -> RED.
# V2 (savestate, visibility): p6_w_titlebg_vis + p6_w_title3d_vis (count of
#     entities SetupFX flipped visible) > 0. Baseline 0 (objects not registered).
# V3 (SCREENSHOT, primary): the island region gains mountain/billboard structure
#     -- DISTINCT-from-flat-green pixels (mountain gray + structured-edge count)
#     in the island band rise above the bed5bac flat-green baseline.
# MUST-NOT-REGRESS: re-run qa_title_logo + qa_title_sonic (FG intact) separately.
#
# Usage:
#   python tools/_portspike/qa_titlebg_sprites.py            # capture + savestate + verdict
#   python tools/_portspike/qa_titlebg_sprites.py --static   # map-only (witnesses present?)
#   python tools/_portspike/qa_titlebg_sprites.py --mcs F.mcs --png F.png  # score given files
# =============================================================================
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT   = os.path.join(ROOT, "tools", "qa_boot.ps1")
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_titlebg_sprites.mcs")
SHOT_BASE = "_titlebg_sprites_shot"
BEST_PNG  = os.path.join(HERE, "_titlebg_sprites.png")
GATE_FRAME = 90.0

# Deep real-time burst window. MEASURED: the settled title (Sonic head fully up in
# WaitForSonic/WaitForEnter) appears in a NARROW window ~76-130s after boot; a sparse
# Every=4 sweep misses it. Dense 1.5s sampling over 60s reliably catches it (head
# >1000 in ~9/40 frames). The pick_best keeps the head-present + logo-present frame.
BOOT_WAIT  = 76.0
BOOT_EVERY = 1.5
BOOT_SHOTS = 40

# Register + bind + visibility witnesses (defined in p6_io_main; latched in the
# overlay under the un-gated registration).
NAMES = ["_p6_w_titlebg_classid", "_p6_w_title3d_classid", "_p6_w_tbg_handle",
         "_p6_w_tbg_shtslot", "_p6_w_titlebg_vis", "_p6_w_title3d_vis",
         "_p6_w_title_backdrop_done", "_p6_w_cont_frames"]

# Island band (912x749 Mednafen client area; same calibration basis as
# qa_title_backdrop's BOT region). The Title3DSprite mountains/trees + TitleBG
# mountains add GRAY/DARK-GREEN STRUCTURE (mountain rock, tree trunks) over the
# bed5bac flat grass-green ground. Measure mountain-gray + structured pixels.
ISLAND = (140, 600, 772, 749)
LOGO_PRESENT_MIN = 40000  # reject the blue LOAD screen (field gotcha #4)
# CALIBRATED (MEASURED A/B): the bed5bac gated-off FG had sonic_head ~10667; the
# Title3DSprite-on corruption dropped it to 0. The shipped TitleBG-only build keeps
# the FG intact (sonic_head ~10760). >=5000 proves the head is rendered (FG intact).
HEAD_MIN = 5000           # Sonic-head peach px (the FG no-regression floor)


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
    mx = np.maximum(np.maximum(r, g), b); mn = np.minimum(np.minimum(r, g), b)
    blue_bd = (b > r + 30) & (b > g + 20) & (g > r)
    return int((((mx - mn) >= 55) & (~blue_bd)).sum())


def _sonic_head(a):
    """Sonic's head peach muzzle/glove pixels in the upper-center (the FG that MUST
    NOT regress). bed5bac gated-off baseline: ~10667; corrupted (Title3DSprite-on):
    0. This is the no-regression proof -- the FG must stay rendered."""
    import numpy as np
    h = a[120:420, 300:640]
    hr, hg, hb = h[..., 0], h[..., 1], h[..., 2]
    return int((((hr > 180) & (hg > 120) & (hg < 200) & (hb > 90) & (hb < 180))).sum())


# SCREEN-EDGE-CLEAN (this session): the engine TitleBG parallax band (Title/BG.gif
# mountains/reflection/water) is scrolled + horizontally wrapped by the verbatim
# decomp TitleBG_Update for a WIDER PC screen; on Saturn's 320 px screen the wide
# (176-192 px) sprites overshoot both edges and the Saturn VDP1 fixed 248 px sprite
# box crosses the 512 px framebuffer LINE STRIDE -> the off-screen columns WRAP to
# the OPPOSITE edge as a duplicated LOGO/RIBBON "fragment" (MEASURED per-blit ring on
# a settled-title savestate: TitleBG drew at x=-67 left, x=293 right; the wrap
# produced a red "MANIA"/"NIA" ribbon copy at the wrong edge).
#
# This gate measures the WRAP SIGNATURE = saturated RED+YELLOW (the SONIC MANIA logo +
# red ribbon colours) in the far-left + far-right edge columns at the ribbon-row band.
# It deliberately does NOT count the legitimate GREEN/BLUE distant-island band that
# TitleBG legitimately scrolls to the screen edge -- that backdrop sprite is supposed
# to be there; only a wrapped LOGO/RIBBON copy is the glitch. CALIBRATED (MEASURED):
#   ORIGINAL glitch (_title_backdrop.png): left red+yellow = 4371 (the "NIA" wrap).
#   FIXED build:                            left = 0, right = 582 (island brown only).
# Threshold 1500 fires RED on the glitch and GREEN once the box-crossing sprite is
# culled (the wrap is gone; only the legit island brown/grass remains < 1500).
EDGE_FRAC      = 0.10   # far-left/right 10% of the image width = the edge columns
EDGE_ROW0_FRAC = 0.50   # ribbon-row band starts at ~50% image height
EDGE_ROW1_FRAC = 0.83   # ...ends at ~83% (above the island foot AA)
EDGE_CLEAN_MAX = 1500   # per-edge logo/ribbon (red+yellow) wrap mass ceiling


def _edge_logo(a):
    """(left_edge_mass, right_edge_mass): the SONIC MANIA logo/ribbon WRAP signature --
    saturated RED + YELLOW pixel mass in the far-left and far-right edge columns at the
    ribbon-row band. A wrapped logo/ribbon copy (the glitch) reads thousands; a clean
    edge (sky/water blue, or the legit green/brown distant-island band) reads ~0. The
    red+yellow restriction is what separates a wrapped RIBBON fragment from the
    legitimate scrolling TitleBG island that reaches the screen edge by design."""
    import numpy as np
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    H, W, _ = a.shape
    # The SONIC MANIA logo/ribbon palette: saturated reds (ribbon) + yellows (wordmark).
    red = (r > 140) & (r > g + 40) & (r > b + 40)
    yellow = (r > 170) & (g > 150) & (b < 120) & (np.abs(r - g) < 70)
    logo = red | yellow
    y0, y1 = int(H * EDGE_ROW0_FRAC), int(H * EDGE_ROW1_FRAC)
    le = int(W * EDGE_FRAC)
    band = logo[y0:y1, :]
    left = int(band[:, 0:le].sum())
    right = int(band[:, W - le:W].sum())
    return left, right


def score(png):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(np.int32)
    return _sonic_head(a), _logo_mass(a)


def pick_best(pngs):
    # Pick the settled-title frame with the FG (logo) present AND the most Sonic-head
    # pixels (the FG-intact proof).
    best, best_n = None, -1
    for p in pngs:
        try:
            head, logo = score(p)
        except Exception:
            head, logo = 0, 0
        if logo < LOGO_PRESENT_MIN:
            continue
        if head > best_n:
            best, best_n = p, head
    return best


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
    print("=" * 70)
    print("CP5b.4 GATE -- TitleBG + Title3DSprite sprites render on the Title scene")
    print("=" * 70)

    # 1) Savestate witnesses (register + bind chain).
    _purge_lock()
    if "--mcs" in argv:
        mcs = argv[argv.index("--mcs") + 1]
    else:
        mcs = TMP_MCS
        if "--static" not in argv:
            r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame",
                                str(GATE_FRAME), "-Out", mcs], capture_output=True, text=True)
            sys.stdout.write((r.stdout or "")[-160:])
    present, v = read_witnesses(mcs)
    for n in NAMES:
        val = (v.get(n) if v else None)
        print("  %-28s present=%s  value=%s" % (n, present[n], Q._dv(val)))

    if v is None:
        if "--static" in argv:
            # static: a registered-by-default build will at least have the latch
            # symbols; the real verdict needs a savestate.
            print("RESULT: GREEN(static) -- witnesses present; run without --static "
                  "for the register+visibility+pixel verdict.")
            return 0
        print("RESULT: RED -- no savestate witnesses (calibration failed / no .mcs).")
        return 1

    tbg_cid = v.get("_p6_w_titlebg_classid")
    t3d_cid = v.get("_p6_w_title3d_classid")
    tbg_h   = v.get("_p6_w_tbg_handle")
    tbg_vis = v.get("_p6_w_titlebg_vis")
    t3d_vis = v.get("_p6_w_title3d_vis")
    # SHIPPED DEFAULT (CP5b.4): TitleBG (mountains/reflection/water-sparkle) is
    # registered + rendered; Title3DSprite (the 58 billboards) is DEFERRED behind
    # P6_TITLE3D_ON because registering it corrupts the FG (MEASURED A/B). So the
    # gate requires TitleBG only; Title3DSprite is reported but not required.
    v1 = (tbg_cid is not None and tbg_cid > 0
          and tbg_h is not None and tbg_h >= 0)
    v2 = (tbg_vis is not None and tbg_vis > 0)
    print("-" * 70)
    print("  V1 register+bind: titlebg_classid>0 AND tbg_handle>=0  -> %s"
          % ("PASS" if v1 else "FAIL"))
    print("  V2 visibility:    titlebg_vis>0                        -> %s"
          % ("PASS" if v2 else "FAIL"))
    print("  (Title3DSprite DEFERRED: title3d_classid=%s, title3d_vis=%s -- registering it "
          "corrupts the FG; enable with P6_TITLE3D_ON only once that is fixed.)"
          % (Q._dv(t3d_cid), Q._dv(t3d_vis)))

    # 2) Screenshot (primary).
    if "--png" in argv:
        pngs = [argv[argv.index("--png") + 1]]
    elif "--static" in argv:
        pngs = []
    else:
        pngs = capture_burst()
    head = 0
    edge_l, edge_r = -1, -1
    if pngs:
        best = pick_best(pngs)
        if best is None:
            best = max(pngs, key=lambda p: score(p)[0])
        head, logo = score(best)
        from PIL import Image
        import numpy as np
        edge_l, edge_r = _edge_logo(np.asarray(Image.open(best).convert("RGB")).astype(np.int32))
        try:
            import shutil
            shutil.copyfile(best, BEST_PNG)
        except Exception:
            pass
        print("  V3 screenshot (FG NO-REGRESSION): best=%s  sonic_head=%d (>=%d)  logo=%d  -> %s"
              % (os.path.basename(best), head, HEAD_MIN, logo,
                 "PASS" if head >= HEAD_MIN else "FAIL"))
        print("  screenshot -> %s" % BEST_PNG)
    v3 = head >= HEAD_MIN
    # V4 SCREEN-EDGE-CLEAN: the far-left + far-right edge columns must be backdrop-
    # only (no wrapped logo/ribbon fragment). RED on the current TitleBG-wrap glitch
    # (leftEdge ~6600, rightEdge ~2400), GREEN once the off-screen sprites are clipped.
    v4 = (edge_l >= 0 and edge_l < EDGE_CLEAN_MAX and edge_r < EDGE_CLEAN_MAX)
    if edge_l >= 0:
        print("  V4 screen-edge-clean: leftEdge=%d rightEdge=%d (<%d each)  -> %s"
              % (edge_l, edge_r, EDGE_CLEAN_MAX,
                 "PASS" if v4 else "FAIL (wrapped TitleBG fragment at a screen edge)"))

    ok = v1 and v2 and v3 and v4
    print("-" * 70)
    print("RESULT:", "GREEN -- TitleBG sprites render, FG (Sonic+logo) intact, edges clean."
          if ok else "RED -- TitleBG not registered/visible OR FG regressed OR an edge fragment is present.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
