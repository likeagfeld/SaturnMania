#!/usr/bin/env python3
# =============================================================================
# qa_ghzcut_liverender.py -- RED-first gate for Task #309 gate-2 FOLLOW-UP:
# the LIVE AIZ->GHZCutscene arrival RENDER must MATCH the clean DIRECT-BOOT.
#
# WHY THIS GATE EXISTS: the prior gate-2 closeout used `currentSceneFolder ==
# "GHZCutscene"` as the "render is right" proxy (qa_ghzcut_load.py). That proxy
# is SATISFIED while the render is WRONG -- the live seam comes FROM the AIZ
# scene, which armed the AIZ 4-bpp VDP2 BG planes (NBG0/NBG2/NBG3) + their BGON
# + scroll. The GHZCutscene present arms slScrAutoDisp(NBG1ON|SPRON) but stale
# AIZ BG-plane state shows through where the direct-boot (which NEVER ran AIZ)
# has clean zeroed BG planes: the checkerboard FG bleeds into the upper-screen
# black-sky region (direct-boot _ghzcut_heav_GREEN.png = black sky above; live
# _ghzcut_live_render.png = checkerboard there).
#
# The DIRECT-BOOT is the GROUND-TRUTH target. This gate compares LIVE vs DIRECT:
#
#   (a) REGISTER/CAMERA match (savestate peek, ST-058-R2-cited addresses):
#       - BGON   0x25F80020  Screen Display Enable (ST-058-R2 p48, txt:2509-2551):
#                bit0=N0ON,bit1=N1ON,bit2=N2ON,bit3=N3ON,bit4=R0ON,bit5=R1ON.
#                A clean GHZCutscene = N1ON(+SPR) only; NBG0/2/3 OFF.
#       - SCXIN1/SCYIN1 0x25F80080/4  NBG1 (FG) scroll (ST-058-R2 p122-123,
#                txt:5405-5423). NOTE: the task hint's @0x70/74 are NBG0, not NBG1.
#       - NBG0/2/3 scroll 0x25F80070/4, 0x25F80090/2, 0x25F80094/6 -- the AIZ BG
#                planes; in a clean GHZCutscene these are 0 / don't-care (planes off).
#       - screens[0].position via the p6_w_aiz_scrx/scry witnesses (latched every
#                present frame for BOTH flavors, under #if P6_AIZ_TEST).
#     The LIVE state's (BGON & visible-plane mask) + NBG1 scroll + camera must
#     EQUAL the DIRECT-BOOT state's.
#
#   (b) PIXEL check of the captured LIVE frame (gotcha #4 -- a render bug's gate
#       must measure the on-screen symptom): the upper-screen band (where the
#       direct-boot shows black sky) must be DARK, not bright checkerboard FG.
#       The GHZ FG checkerboard is mid-brightness brown; black sky is near-zero
#       luma. Measure the fraction of upper-band pixels that are "dark".
#
# USAGE:
#   # register/camera compare (both states from the SAME build's map):
#   python qa_ghzcut_liverender.py --live LIVE.mcs --direct DIRECT.mcs
#   # add the live-frame pixel check:
#   python qa_ghzcut_liverender.py --live LIVE.mcs --direct DIRECT.mcs \
#          --live-png LIVE.png [--direct-png DIRECT.png]
#   # pixel-only (no register compare):
#   python qa_ghzcut_liverender.py --live-png LIVE.png
#
# VERDICT: RED on the current live build (BGON/scroll/camera differ from direct,
# and/or the live upper band is checkerboard). GREEN after the seam-branch fix
# (registers+camera match + upper band is black/sky).
# =============================================================================
import argparse
import importlib.util
import os
import pathlib
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

_spec = importlib.util.spec_from_file_location(
    "mcs_extract", os.path.join(ROOT, "tools", "mcs_extract.py"))
_mcs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mcs)

sys.path.insert(0, HERE)
import qa_p6_scene as Q  # noqa: E402  (map_symbol/calibrate/peek_u32 helpers)

# VDP2 registers (cache-through 0x25F8xxxx). ST-058-R2 addresses.
VDP2_BGON = 0x25F80020   # Screen Display Enable     (ST-058-R2 p48)
VDP2_SCXIN0 = 0x25F80070  # NBG0 H scroll integer    (ST-058-R2 p122)
VDP2_SCYIN0 = 0x25F80074  # NBG0 V scroll integer
VDP2_SCXIN1 = 0x25F80080  # NBG1 H scroll integer    (ST-058-R2 p123)
VDP2_SCYIN1 = 0x25F80084  # NBG1 V scroll integer
VDP2_SCXN2 = 0x25F80090   # NBG2 H scroll            (ST-058-R2 p123)
VDP2_SCYN2 = 0x25F80092
VDP2_SCXN3 = 0x25F80094   # NBG3 H scroll
VDP2_SCYN3 = 0x25F80096

# BGON display-enable bits (ST-058-R2 p48). The VISIBLE-LAYER mask we compare:
# a clean GHZCutscene must have IDENTICAL display-enable on NBG0..R1 (bits 0-5).
BGON_DISP_MASK = 0x003F  # N0ON|N1ON|N2ON|N3ON|R0ON|R1ON


def _be16(b):
    return ((b[0] << 8) | b[1]) if (b is not None and len(b) >= 2) else 0


def peek16(sec, addr):
    return _be16(_mcs._peek_bytes(sec, addr, 2))


def read_regs(sec):
    """All the VDP2 registers + camera witnesses this gate compares."""
    mp = Q.read_text(Q.MAP_DEFAULT)
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(_mcs._peek_bytes(sec, ma, 4) if ma else None)
    out = {
        "BGON": peek16(sec, VDP2_BGON),
        "SCXIN0": peek16(sec, VDP2_SCXIN0),
        "SCYIN0": peek16(sec, VDP2_SCYIN0),
        "SCXIN1": peek16(sec, VDP2_SCXIN1),
        "SCYIN1": peek16(sec, VDP2_SCYIN1),
        "SCXN2": peek16(sec, VDP2_SCXN2),
        "SCYN2": peek16(sec, VDP2_SCYN2),
        "SCXN3": peek16(sec, VDP2_SCXN3),
        "SCYN3": peek16(sec, VDP2_SCYN3),
        "_perm_ok": perm is not None,
    }
    # screens[0].position via the per-present-frame witnesses (both flavors).
    sx = Q.map_symbol(mp, "_p6_w_aiz_scrx")
    sy = Q.map_symbol(mp, "_p6_w_aiz_scry")
    out["cam_x"] = Q.peek_u32(_mcs, sec, sx, perm, signed=True) if (sx and perm) else None
    out["cam_y"] = Q.peek_u32(_mcs, sec, sy, perm, signed=True) if (sy and perm) else None
    # folder + cont_frames sanity.
    cfo = Q.map_symbol(mp, "_currentSceneFolder") or Q.map_symbol(mp, "_RSDK::currentSceneFolder")
    if cfo:
        fb = _mcs._peek_bytes(sec, cfo, 16)
        if fb:
            sw = bytes(bytes(fb)[i ^ 1] for i in range(len(fb)))  # #136 pair-swap
            out["folder"] = sw.split(b"\x00")[0].decode("latin1", "replace")
    cf = Q.map_symbol(mp, "_p6_w_cont_frames")
    out["cont"] = Q.peek_u32(_mcs, sec, cf, perm, signed=True) if (cf and perm) else None
    return out


def fmt_regs(tag, r):
    print("  [%s] folder=%r cont=%s" % (tag, r.get("folder"), r.get("cont")))
    print("      BGON=0x%04X (disp-mask 0x%04X)  SCXIN1=0x%04X SCYIN1=0x%04X"
          % (r["BGON"], r["BGON"] & BGON_DISP_MASK, r["SCXIN1"], r["SCYIN1"]))
    print("      NBG0 scroll=0x%04X/0x%04X  NBG2=0x%04X/0x%04X  NBG3=0x%04X/0x%04X"
          % (r["SCXIN0"], r["SCYIN0"], r["SCXN2"], r["SCYN2"], r["SCXN3"], r["SCYN3"]))
    print("      camera screens[0].position = (%s, %s)" % (r.get("cam_x"), r.get("cam_y")))


# ----- (b) PIXEL check: the upper-screen band must be dark (black sky) ---------
# The Mednafen window capture is ~912x740 incl. the title bar; the GHZCutscene
# game area starts a few px down. We measure a band across the TOP THIRD of the
# game area (clearly above the grass line in both reference frames) and report
# the fraction of "dark" pixels. Black sky -> high dark-fraction; checkerboard
# FG bleed -> low dark-fraction (mid-bright brown tiles).
DARK_LUMA = 60          # luma <= this counts as "dark/sky"
UPPER_BAND_FRAC = 0.33  # measure the top 33% of the game area


def luma(px):
    r, g, b = px[0], px[1], px[2]
    return (r * 299 + g * 587 + b * 114) // 1000


def upper_band_dark_fraction(png_path):
    from PIL import Image
    im = Image.open(png_path).convert("RGB")
    w, h = im.size
    # Skip the OS title bar (~26 px) -> game area top.
    top = 30
    band_h = int((h - top) * UPPER_BAND_FRAC)
    y0, y1 = top, top + band_h
    # Sample a centered horizontal strip (avoid window borders).
    x0, x1 = int(w * 0.05), int(w * 0.95)
    px = im.load()
    dark = 0
    total = 0
    step = 2  # subsample for speed
    for y in range(y0, y1, step):
        for x in range(x0, x1, step):
            total += 1
            if luma(px[x, y]) <= DARK_LUMA:
                dark += 1
    return (dark / total) if total else 0.0, (w, h, y0, y1)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--live", help="LIVE GHZCutscene savestate (.mcs)")
    ap.add_argument("--direct", help="DIRECT-BOOT GHZCutscene savestate (.mcs)")
    ap.add_argument("--live-png", help="captured LIVE GHZCutscene frame (.png)")
    ap.add_argument("--direct-png", help="captured DIRECT-BOOT frame (.png, optional ref)")
    ap.add_argument("--min-dark", type=float, default=0.70,
                    help="min upper-band dark fraction for a clean black sky (default 0.70; "
                         "direct-boot reference measures ~0.76, checkerboard-bleed ~0.63)")
    ap.add_argument("--dark-margin", type=float, default=0.08,
                    help="if --direct-png given, live dark-fraction must be within this "
                         "margin BELOW the direct reference (default 0.08)")
    args = ap.parse_args(argv)

    fails = []
    print("=== qa_ghzcut_liverender (LIVE must MATCH direct-boot) ===")

    # ---- (a) register/camera compare -----------------------------------------
    if args.live and args.direct:
        if not os.path.exists(args.live):
            fails.append("live state missing: %s" % args.live)
        if not os.path.exists(args.direct):
            fails.append("direct state missing: %s" % args.direct)
        if not fails:
            live = read_regs(_mcs.parse_savestate(pathlib.Path(args.live)))
            direct = read_regs(_mcs.parse_savestate(pathlib.Path(args.direct)))
            print("-- REGISTER/CAMERA --")
            fmt_regs("DIRECT", direct)
            fmt_regs("LIVE  ", live)
            if not live.get("_perm_ok"):
                fails.append("live state magic-calibration FAILED (map<->state "
                             "mismatch); recapture from the freshly-built binary")
            if not direct.get("_perm_ok"):
                fails.append("direct state magic-calibration FAILED (map<->state "
                             "mismatch); recapture from the freshly-built binary")
            # BGON visible-layer enables must match.
            if (live["BGON"] & BGON_DISP_MASK) != (direct["BGON"] & BGON_DISP_MASK):
                fails.append("BGON display-enable differs: live 0x%04X vs direct 0x%04X "
                             "(AIZ BG plane(s) still enabled in the live build)"
                             % (live["BGON"] & BGON_DISP_MASK,
                                direct["BGON"] & BGON_DISP_MASK))
            # NBG1 (FG) scroll must match.
            for k in ("SCXIN1", "SCYIN1"):
                if live[k] != direct[k]:
                    fails.append("%s differs: live 0x%04X vs direct 0x%04X"
                                 % (k, live[k], direct[k]))
            # Camera screens[0].position must match (if both readable).
            if (live.get("cam_x") is not None and direct.get("cam_x") is not None
                    and (live["cam_x"], live["cam_y"]) != (direct["cam_x"], direct["cam_y"])):
                fails.append("camera screens[0].position differs: live (%s,%s) vs "
                             "direct (%s,%s)" % (live["cam_x"], live["cam_y"],
                                                 direct["cam_x"], direct["cam_y"]))
            # cont sanity.
            if live.get("cont") is not None and live["cont"] <= 0:
                fails.append("live cont_frames %s <= 0 (captured too early)" % live["cont"])
    elif args.live or args.direct:
        print("  (register compare needs BOTH --live and --direct; skipping reg half)")

    # ---- (b) live-frame pixel check ------------------------------------------
    if args.live_png:
        if not os.path.exists(args.live_png):
            fails.append("live PNG missing: %s" % args.live_png)
        else:
            frac, geom = upper_band_dark_fraction(args.live_png)
            print("-- PIXEL (upper-band black-sky check) --")
            print("  live  %s : upper-band dark-fraction = %.3f  (band y=%d..%d of %dx%d)"
                  % (os.path.basename(args.live_png), frac, geom[2], geom[3],
                     geom[0], geom[1]))
            dfrac = None
            if args.direct_png and os.path.exists(args.direct_png):
                dfrac, _ = upper_band_dark_fraction(args.direct_png)
                print("  direct %s : upper-band dark-fraction = %.3f (reference)"
                      % (os.path.basename(args.direct_png), dfrac))
            if frac < args.min_dark:
                fails.append("live upper band dark-fraction %.3f < %.2f -- the FG "
                             "checkerboard is bleeding into the black-sky region"
                             % (frac, args.min_dark))
            if dfrac is not None and frac < dfrac - args.dark_margin:
                fails.append("live upper-band dark-fraction %.3f is %.3f BELOW the "
                             "direct-boot reference %.3f (margin %.2f) -- the live "
                             "render is NOT as clean as the direct-boot"
                             % (frac, dfrac - frac, dfrac, args.dark_margin))

    if not (args.live or args.direct or args.live_png):
        print("  RED-ready: pass --live/--direct (.mcs) and/or --live-png (.png).")
        return 1

    print("\n=== VERDICT ===")
    if fails:
        for f in fails:
            print("  RED: " + f)
        return 1
    print("  GREEN: live GHZCutscene render matches the direct-boot "
          "(BGON+NBG1-scroll+camera equal; upper band is black sky).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
