#!/usr/bin/env python3
# =============================================================================
# qa_title_chain.py -- CP5c (Task #270) RED-first FLOW gate: the front-end CHAIN
# boots the Logos (SEGA) splash, plays the logos, then AUTO-ADVANCES to the Title
# screen -- BOTH rendered, in ONE run.
#
# WHY (the FLOW, not one scene): CP4b proved a P6_FRONTEND_LOGOS build renders the
# SEGA splash; CP5b proved a P6_FRONTEND_TITLE build boots DIRECTLY into the rendered
# title. CP5c connects them: P6_FRONTEND_CHAIN boots Logos, and when the decomp
# LogoSetup auto-advance (RSDK.LoadScene) fires, p6_frontend_frame routes it to
# p6_title_reload (EXPLICIT folder-select of "Title" -- robust, NOT scene-list
# adjacency) instead of swallowing it. The proof that the CHAIN works is BOTH the
# Logos frame AND the Title frame coming from the SAME boot.
#
# THE GATE'S PRIMARY EVIDENCE IS THE SCREENSHOTS (the binding visual rule) + a pixel
# measure, corroborated by a savestate witness. A burst spanning the whole
# boot -> logos -> advance -> title arc; from that one burst:
#   V1  an EARLY frame shows the LOGOS/SEGA splash: saturated SEGA-BLUE px in the
#       central splash region exceed a threshold AND the SONIC-MANIA gold/red logo is
#       ABSENT (so a Title frame, which ALSO has blue, is not mistaken for Logos).
#       Ground truth: tools/_portspike/_logos_fixed.png (large blue SEGA on black).
#   V2  a LATER frame shows the TITLE: the SONIC-MANIA gold/red/white logo mass
#       exceeds a threshold AND Sonic's blue head fills the ring interior (the same
#       two measures qa_title_logo.py / qa_title_sonic.py use).
#   V3  the transition FIRED (savestate): p6_w_chain_fired == 1 AND
#       p6_w_frontend_folder_tag == 0x5469 ('Ti' -- Title is the active scene at the
#       deep gate frame) AND p6_w_chain_folder_pre == 0x4C6F ('Lo' -- the advance
#       fired FROM Logos).
#
# RED on the CURRENT (non-CHAIN) build: p6_w_chain_fired is ABSENT from game.map (the
# CHAIN advance does not exist) -> the gate REDs on "witness absent". Also, a
# P6_FRONTEND_TITLE build boots straight to Title (no Logos frame ever) -> V1 fails,
# and a P6_FRONTEND_LOGOS build holds on Logos forever (no Title frame) -> V2 fails.
# Only the CHAIN build can satisfy V1 + V2 + V3 from one run.
#
# CALIBRATION (impl agent -- MEASURE, don't trust the seeds): the burst window + the
# thresholds below are SEEDED from the CP4b/CP5b measures (912x749 window) and MUST be
# re-measured on the actual CHAIN build. Run --shots-only first, eyeball the saved
# _chain_logos.png + _chain_title.png, and tune BOOT_WAIT/EVERY/SHOTS so the burst
# robustly spans BOTH the settled Logos splash AND the settled Title.
#
#   python tools/_portspike/qa_title_chain.py            # capture burst + savestate + verdict
#   python tools/_portspike/qa_title_chain.py --static   # map-only RED check (witness present?)
#   python tools/_portspike/qa_title_chain.py --shots-only
# =============================================================================
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
QA_BOOT   = os.path.join(ROOT, "tools", "qa_boot.ps1")
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_title_chain.mcs")
SHOT_BASE = "_title_chain_shot"
LOGOS_PNG = os.path.join(HERE, "_chain_logos.png")   # the EARLY (Logos) deliverable
TITLE_PNG = os.path.join(HERE, "_chain_title.png")   # the LATE (Title) deliverable
# CP5c FIELD-GOTCHA #4 (the orchestrator caught a wrong-PALETTE Title the gate MISSED):
# the V2 pixel-COUNT measures (Sonic-blue head px + saturated logo mass) BOTH passed on
# the hue-shifted chain Title (head_blue=33163>=13000, logo_mass=153661>=40000) because a
# TEAL head still has b>r&b>g and YELLOW wings + GREEN wordmark still count as saturated
# "logo mass". The wrong palette is a COLOR bug, invisible to a pixel-count. V2b below
# compares the Title frame's REGION COLORS against this golden direct-boot Title render
# (CP5b.2, CORRECT colors: SILVER wings, GOLD "SONIC" wordmark, BLUE Sonic body, GOLD ring).
GOLDEN_TITLE = os.path.join(HERE, "_title_sonic_render.png")
GATE_FRAME = 200.0  # deep savestate frame -- well past the Logos->Title advance + Title settle

# DEEP burst window. The chain runs Logos FIRST (plays the SEGA/RSDK logos), THEN
# loads Title -- strictly longer than the CP5b.2 Title-direct boot (which settled
# ~70s). Span the whole arc: the early Logos splash AND the late settled Title.
# MEASURED (CP5c fixed build): the blue LOAD screen sits ~40-44s; the SEGA splash is a
# BRIEF window ~46-54s; the Title settles ~68s and holds to ~148s before auto-advancing
# (blank) toward the unported Menu. The SEGA splash is short, so at 4s spacing it fell
# BETWEEN shots on a fast boot (V1 flaked RED with sega_blue=0). Sample DENSER (2s) from
# t=42 so the ~6-8s splash window is reliably hit, while still spanning the settled Title
# (the Title-pick ranks settled color-correct frames, so the wider late tail is fine).
BOOT_WAIT  = 42.0
BOOT_EVERY = 2.0
BOOT_SHOTS = 54

NAMES = ["_p6_w_chain_fired", "_p6_w_chain_folder_pre", "_p6_w_frontend_folder_tag",
         "_p6_w_chain_listpos_adv", "_p6_w_chain_listpos_title",
         "_p6_w_transitions", "_p6_w_tlogo_handle", "_p6_w_tsonic_handle",
         "_p6_w_tsonic_visible", "_p6_w_vdp1_landed", "_p6_w_cont_frames",
         "_p6_w_title_objcount"]

# --- V1 (Logos / SEGA splash) ------------------------------------------------
# The SEGA logo is saturated BLUE glyphs on a BLACK field, centered. (Ground truth:
# _logos_fixed.png + the CHAIN burst _title_chain_shot_2.png on the 912x749 window --
# the SEGA wordmark spans roughly x=200..720, y=300..490.) Box the central splash
# region; count saturated-blue px AND require a high frame-wide BLACK fraction.
#
# MEASURED (CP5c burst): the SEGA splash frame (shot_2) = ~63,000 box-blue px AND
# blackfrac 0.87 (SEGA on black). The BLUE LOAD screen (shot_1) = 128,800 box-blue px
# but blackfrac 0.01 (uniform periwinkle, NOT black-surround). So box-blue ALONE picks
# the load screen; the black-fraction gate (BLACK_FRAC_MIN) is the discriminator that
# isolates the real SEGA-on-black splash. The gold/red SONIC-MANIA logo must ALSO be
# absent (logo_mass < TITLE_LOGO_MIN) so a Title frame can't false-positive here.
LOGOS_BOX      = (180, 280, 740, 510)
LOGOS_BLUE_MIN = 40      # SEGA blue is saturated (b dominant over r AND g)
LOGOS_PIX_MIN  = 25000   # min SEGA-blue px for V1 (MEASURED splash ~63K; load-screen 128K rejected by black-frac)
BLACK_FRAC_MIN = 0.55    # the splash is mostly black (MEASURED 0.87); the blue load is 0.01 -> rejected

# --- V2 (Title) --------------------------------------------------------------
# Reuse the qa_title_logo / qa_title_sonic measures verbatim (same 912x749 window).
TITLE_RING_BOX   = (360, 90, 560, 390)   # ring interior (Sonic's head pops out here)
SONIC_BLUE_MIN   = 25
TITLE_HEAD_MIN   = 13000                  # min Sonic-blue head px (qa_title_sonic CALIBRATED)
TITLE_LOGO_MIN   = 40000                  # min SONIC-MANIA gold/red/white logo mass (qa_title_logo)


def _logo_mass(a):
    """Saturated, NON-blue-backdrop px = the SONIC MANIA logo glyphs (gold/red/white).
    Identical to qa_title_logo/qa_title_sonic. A blue load screen AND the blue SEGA
    splash have ~0 of these (their saturated color is BLUE, which is excluded); the
    settled SONIC-MANIA logo has >100K. This is what SEPARATES a Title frame from a
    Logos frame in the same burst."""
    import numpy as np
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    blue_bd = (b > r + 30) & (b > g + 20) & (g > r)
    return int((((mx - mn) >= 55) & (~blue_bd)).sum())


def logos_measure(png):
    """(sega_blue_px in the splash box, logo_mass, black_frac). A LOGOS/SEGA frame =
    high sega_blue AND high black_frac (SEGA glyphs on a black field) AND LOW logo_mass
    (no gold/red SONIC-MANIA logo). black_frac is the discriminator vs the uniform blue
    LOAD screen (which has MORE box-blue but ~0 black)."""
    from PIL import Image
    import numpy as np
    im = Image.open(png).convert("RGB")
    a = np.asarray(im).astype(np.int32)
    logo = _logo_mass(a)
    black_frac = float((a.reshape(-1, 3).max(1) < 40).mean())
    x0, y0, x1, y1 = LOGOS_BOX
    s = a[y0:y1, x0:x1]
    r, g, b = s[..., 0], s[..., 1], s[..., 2]
    sega_blue = (b > r + LOGOS_BLUE_MIN) & (b > g + LOGOS_BLUE_MIN) & (b > 110)
    return int(sega_blue.sum()), logo, black_frac


def title_measure(png):
    """(sonic_blue_head_px in the ring box, logo_mass). A TITLE frame = high logo_mass
    AND high ring-head blue. (qa_title_sonic head measure + qa_title_logo logo mass.)"""
    from PIL import Image
    import numpy as np
    im = Image.open(png).convert("RGB")
    a = np.asarray(im).astype(np.int32)
    logo = _logo_mass(a)
    x0, y0, x1, y1 = TITLE_RING_BOX
    s = a[y0:y1, x0:x1]
    r, g, b = s[..., 0], s[..., 1], s[..., 2]
    sonic_blue = (b > r + SONIC_BLUE_MIN) & (b > g + SONIC_BLUE_MIN) & (b > 90)
    return int(sonic_blue.sum()), logo


# --- V2b (COLOR CORRECTNESS vs golden) ---------------------------------------
# CP5c CRAM-palette gate. The chain Title geometry is IDENTICAL to the direct boot
# (sprites in the same places); only the colors differ when CRAM bank 1 holds the
# stale Logos palette. So compare per-region COLORS against the golden Title render.
# Two MEASURED, complementary signals (a wrong palette must pass BOTH to be GREEN):
#   (1) summed per-region mean-RGB Euclidean distance vs golden: MEASURED 435.3 on the
#       wrong-palette _chain_title.png, 0.0 golden-vs-golden. Threshold 200 REDs the
#       bug, GREENs a correct Title with capture-noise headroom.
#   (2) characteristic hue SIGN relations the GOLDEN palette satisfies (capture-light
#       invariant): the "SONIC" wordmark is GOLD (R>G>B; the chain's GREEN has R<G and
#       B>R), the wings are SILVER (B>R; the chain's YELLOW has B<R), the ring is GOLD
#       (R>G; the chain's GREEN has G>R). MEASURED to flip exactly on the bug.
# Region boxes are on the 912x749 capture window the chain burst uses (same as V2).
COLOR_REGIONS = {
    "LEFT_WING":      (95, 160, 250, 360),
    "RIGHT_WING":     (640, 160, 820, 360),
    "WORDMARK_SONIC": (330, 430, 600, 520),
    "SONIC_BODY":     (380, 110, 540, 260),
    "RING_GOLD":      (300, 210, 360, 360),
}
# Threshold MEASURED to separate the WRONG-palette Title from the CORRECT one across
# the whole settle, animation-phase included:
#   wrong palette (stale Logos CRAM):  summed dist 435.3, 5/5 golden hue-signs VIOLATED.
#   correct palette (the fix), settled: summed dist ~163 (shots 16-31), 0 hue-sign
#       violations; the wordmark + wings regions are EXACT (dist 0.0) -- the body/ring
#       regions vary ~50-140 by the electric-ring + spark ANIMATION PHASE (not palette).
# 300 REDs the bug (435) with margin and GREENs the settled correct Title (<=~210). The
# hue-sign checks (0 fails correct, 5 fails wrong) are the animation-INVARIANT primary
# discriminator; the distance gate is the corroborating secondary.
COLOR_DIST_MAX = 300.0   # summed mean-RGB dist vs golden (MEASURED bug 435.3; correct settled ~163)


def _region_means(png):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(png).convert("RGB")).astype(np.float64)
    return {k: a[y0:y1, x0:x1].reshape(-1, 3).mean(0)
            for k, (x0, y0, x1, y1) in COLOR_REGIONS.items()}, a.shape


def title_color_check(png, golden):
    """Returns (ok, dist, sign_fail_list, detail). ok == colors MATCH the golden.
    dist = summed per-region mean-RGB distance vs golden. sign_fail_list names the
    characteristic GOLD/SILVER hue relations the frame violates (a wrong palette
    flips these). A frame is color-correct iff dist <= COLOR_DIST_MAX AND no sign
    relation is violated."""
    import numpy as np
    if not (os.path.exists(png) and os.path.exists(golden)):
        return False, 9999.0, ["png-or-golden-missing"], {}
    cm, cs = _region_means(png)
    gm, _ = _region_means(golden)
    dist = 0.0
    detail = {}
    for k in COLOR_REGIONS:
        d = float(np.sqrt(((cm[k] - gm[k]) ** 2).sum()))
        dist += d
        detail[k] = (tuple(round(v) for v in cm[k]), tuple(round(v) for v in gm[k]), round(d, 1))
    # Characteristic golden hue-sign relations (R,G,B per region):
    fails = []
    def R(k): return cm[k][0]
    def G(k): return cm[k][1]
    def B(k): return cm[k][2]
    if not (R("WORDMARK_SONIC") > G("WORDMARK_SONIC")):
        fails.append("SONIC wordmark not GOLD (need R>G; chain GREEN has R<G)")
    if not (B("WORDMARK_SONIC") < R("WORDMARK_SONIC")):
        fails.append("SONIC wordmark too BLUE (need B<R; chain GREEN has B>R)")
    if not (B("LEFT_WING") > R("LEFT_WING")):
        fails.append("LEFT wing not SILVER (need B>R; chain YELLOW has B<R)")
    if not (B("RIGHT_WING") > R("RIGHT_WING")):
        fails.append("RIGHT wing not SILVER (need B>R; chain YELLOW has B<R)")
    if not (R("RING_GOLD") > G("RING_GOLD")):
        fails.append("RING not GOLD (need R>G; chain GREEN has G>R)")
    ok = (dist <= COLOR_DIST_MAX) and (len(fails) == 0)
    return ok, dist, fails, detail


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


def pick_logos(pngs):
    """The best LOGOS/SEGA frame: max SEGA-blue AMONG frames that are mostly BLACK
    (black_frac >= BLACK_FRAC_MIN -- SEGA on black, NOT the uniform blue load screen)
    AND where the gold/red SONIC-MANIA logo is ABSENT (logo_mass < TITLE_LOGO_MIN).
    Returns (png, sega_blue, logo_mass, black_frac)."""
    best, best_n, best_logo, best_blk = None, -1, 0, 0.0
    for p in pngs:
        try: sb, logo, blk = logos_measure(p)
        except Exception: sb, logo, blk = -1, 0, 0.0
        if logo >= TITLE_LOGO_MIN:
            continue  # gold/red logo present -> a Title frame, not Logos
        if blk < BLACK_FRAC_MIN:
            continue  # not black-surround -> the blue LOAD screen, not the SEGA splash
        if sb > best_n:
            best, best_n, best_logo, best_blk = p, sb, logo, blk
    return best, best_n, best_logo, best_blk


def pick_title(pngs):
    """The best SETTLED TITLE frame among frames where the SONIC-MANIA gold/red logo IS
    present (logo_mass >= TITLE_LOGO_MIN). CP5c calibration fix: the OLD pick (max
    Sonic-blue head) selected a TRANSIENT electric-ring FLASH frame (a momentary blue
    spike) whose body/ring color regions are off animation-phase -- not representative of
    the settled Title. Rank instead by COLOR CORRECTNESS vs the golden: fewest golden
    hue-sign violations FIRST (the palette-diagnostic, animation-invariant signal), then
    lowest summed color-dist. This delivers a representative settled frame and keeps the
    V2/V2b verdict honest (a wrong-palette frame has 5 sign-fails and ~435 dist -> it can
    never win, so the gate still REDs the bug). Returns (png, head_blue, logo_mass)."""
    cands = []
    for p in pngs:
        try: hb, logo = title_measure(p)
        except Exception: hb, logo = -1, 0
        if logo < TITLE_LOGO_MIN:
            continue
        try:
            _, cdist, cfails, _ = title_color_check(p, GOLDEN_TITLE)
        except Exception:
            cdist, cfails = 9999.0, ["err"] * 9
        # rank key: (#sign-fails asc, color-dist asc); tie-break head-blue desc.
        cands.append((len(cfails), cdist, -hb, p, hb, logo))
    if not cands:
        return None, -1, 0
    cands.sort()
    _, _, _, best, best_hb, best_logo = cands[0]
    return best, best_hb, best_logo


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
    print("=" * 72)
    print("CP5c FLOW GATE -- boot Logos -> play -> auto-advance to Title (one run)")
    print("=" * 72)
    key_absent = [n for n in ("_p6_w_chain_fired", "_p6_w_chain_folder_pre") if not present[n]]
    if "--static" in argv or key_absent:
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        if key_absent:
            print("-" * 72)
            print("RESULT: RED -- %s ABSENT. The CHAIN advance (P6_FRONTEND_CHAIN) is "
                  "NOT built -- this is not a CHAIN flavor." % ", ".join(key_absent))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- chain witnesses present (run without --static for capture).")
            return 0

    if "--png-logos" in argv and "--png-title" in argv:
        pngs = [argv[argv.index("--png-logos") + 1], argv[argv.index("--png-title") + 1]]
    else:
        pngs = capture_burst()
    if not pngs:
        print("RESULT: RED -- no screenshots (desktop locked? mednafen failed?)."); return 1

    lp, l_blue, l_logo, l_blk = pick_logos(pngs)
    tp, t_head, t_logo = pick_title(pngs)
    import shutil
    if lp: shutil.copyfile(lp, LOGOS_PNG)
    if tp: shutil.copyfile(tp, TITLE_PNG)
    print("  burst: %d shots" % len(pngs))
    print("  LOGOS pick: %s  (SEGA-blue=%d px, logo_mass=%d, black_frac=%.2f) -> %s"
          % (os.path.basename(lp) if lp else "-", l_blue, l_logo, l_blk,
             os.path.relpath(LOGOS_PNG, ROOT)))
    print("  TITLE pick: %s  (Sonic-head-blue=%d px, logo_mass=%d) -> %s"
          % (os.path.basename(tp) if tp else "-", t_head, t_logo,
             os.path.relpath(TITLE_PNG, ROOT)))
    if "--shots-only" in argv: return 0

    _purge_lock()
    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else TMP_MCS
    if "--mcs" not in argv:
        r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame",
                            str(GATE_FRAME), "-Out", mcs], capture_output=True, text=True)
        sys.stdout.write((r.stdout or "")[-160:])
    v = read_witnesses(mcs)

    print("-" * 72)
    v1 = (l_blue >= LOGOS_PIX_MIN) and (l_logo < TITLE_LOGO_MIN) and (l_blk >= BLACK_FRAC_MIN)
    print("  [%s] V1 EARLY frame shows the LOGOS/SEGA splash: %d SEGA-blue px >= %d "
          "AND gold/red logo absent (logo_mass %d < %d) AND black-surround (%.2f >= %.2f)"
          % ("GREEN" if v1 else " RED ", l_blue, LOGOS_PIX_MIN, l_logo, TITLE_LOGO_MIN,
             l_blk, BLACK_FRAC_MIN))
    v2 = (t_head >= TITLE_HEAD_MIN) and (t_logo >= TITLE_LOGO_MIN)
    print("  [%s] V2 LATER frame shows the TITLE: %d Sonic-head-blue px >= %d "
          "AND SONIC-MANIA logo present (logo_mass %d >= %d)"
          % ("GREEN" if v2 else " RED ", t_head, TITLE_HEAD_MIN, t_logo, TITLE_LOGO_MIN))

    # V2b (CP5c CRAM-PALETTE): the Title frame's COLORS match the golden direct-boot
    # Title (NOT a hue-shifted Logos-palette Title). This is the check the orchestrator
    # had to make BY EYE -- field-gotcha #4: V2's pixel COUNTS pass on the wrong palette.
    v2b, cdist, cfails, cdetail = title_color_check(TITLE_PNG, GOLDEN_TITLE)
    print("  [%s] V2b TITLE COLORS match golden (%s): summed mean-RGB dist %.1f <= %.1f "
          "AND golden hue-signs hold"
          % ("GREEN" if v2b else " RED ", os.path.relpath(GOLDEN_TITLE, ROOT),
             cdist, COLOR_DIST_MAX))
    for k in COLOR_REGIONS:
        cc, gg, dd = cdetail.get(k, ((0, 0, 0), (0, 0, 0), 0))
        print("        %-15s chain RGB%s  golden RGB%s  dist=%s" % (k, cc, gg, dd))
    for f in cfails:
        print("        HUE-VIOLATION: %s" % f)

    ok = v1 and v2 and v2b
    if v is not None:
        c3 = [
            ("V3a chain advance fired (chain_fired==1)",          v.get("_p6_w_chain_fired"),      lambda x: x == 1),
            ("V3b advance fired FROM Logos (chain_folder_pre==0x4C6F)", v.get("_p6_w_chain_folder_pre"), lambda x: x == 0x4C6F),
            ("V3c Title is the active scene (folder_tag==0x5469)", v.get("_p6_w_frontend_folder_tag"), lambda x: x == 0x5469),
        ]
        for label, val, test in c3:
            try: p = bool(test(val))
            except Exception: p = False
            ok = ok and p
            print("  [%s] %-52s = %s" % ("GREEN" if p else " RED ", label, Q._dv(val)))
        print("        --- measured scene-list order (risk #1) ---")
        print("        chain_listpos_adv (engine ++ dest, Logos+1) = %s" % Q._dv(v.get("_p6_w_chain_listpos_adv")))
        print("        chain_listpos_title (by-name 'Title' scan)  = %s" % Q._dv(v.get("_p6_w_chain_listpos_title")))
        print("        --- residency corroboration (the chain-transition re-bind) ---")
        for n in ("_p6_w_tlogo_handle", "_p6_w_tsonic_handle", "_p6_w_tsonic_visible",
                  "_p6_w_vdp1_landed", "_p6_w_transitions", "_p6_w_cont_frames",
                  "_p6_w_title_objcount"):
            print("        %-26s = %s" % (n, Q._dv(v.get(n))))
    else:
        print("  [ RED ] V3 witnesses uncalibrated (no savestate)"); ok = False

    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the FLOW chains: the SEGA splash renders early (V1), the "
              "Title renders later (V2), and the advance fired Logos->Title (V3) IN ONE RUN. "
              "Deliverables: %s + %s"
              % (os.path.relpath(LOGOS_PNG, ROOT), os.path.relpath(TITLE_PNG, ROOT)))
        return 0
    print("RESULT: RED -- the chain does not complete. See the RED check(s). PNGs: %s + %s"
          % (os.path.relpath(LOGOS_PNG, ROOT), os.path.relpath(TITLE_PNG, ROOT)))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
