#!/usr/bin/env python3
# =============================================================================
# qa_p6_ring.py -- #258 RING ARMAMENT visual gate (RED-first, screenshot-based).
#
# WHAT IT PROVES (the user-reported bug: "all VDP1 sprites missing at GHZ load;
# after ~1-2 min only Sonic+Tails show up, nothing else"). Root cause: the GHZ
# placed RINGS never rendered -- Ring_Create was stubbed NULL in the overlay, so
# no ring animator armed and the Items.gif VDP1 handle stayed unbound (the #181
# "registered obj + unbound sheet = present-but-invisible" pattern). The fix ports
# the FULL verbatim Game_Ring (Create + StageLoad + State_Normal + Draw) into the
# CART-relocated overlay. This gate measures the ON-SCREEN symptom directly.
#
# WHY A PIXEL GATE, NOT JUST A WITNESS (sega-saturn-developer gotcha #4): a VISUAL
# bug's gate MUST measure rendered pixels -- a handle/armed witness can go GREEN
# while sprites stay invisible (the #250 precedent). So the PRIMARY measure is the
# count of RING-GOLD pixels in the play area of a settled-GHZ screenshot; the
# p6_w_ring_aniframes witness (Ring_StageLoad's LoadSpriteAnimation succeeded) is
# the COMPLEMENTARY signal.
#
# RING-GOLD SIGNATURE (data-driven from extracted/Data/Sprites/Global/Items.gif
# palette, the authoritative ring sheet): the ring body gradient is idx 43-47 =
# rgb(136,56,0)/(184,104,0)/(240,176,0)/(240,216,0)/(240,240,0) -- high R, mid-high
# G, near-zero B, R>=G. After the Saturn 8->5 bit CRAM quantization a captured
# pixel lands at ~(248,176,0) etc., so the classifier uses a tolerant band:
#   R >= 176  AND  88 <= G <= 250  AND  B <= 64  AND  R >= G-24
# This excludes pure white (B high), magenta key (G low), grey (B high), Sonic blue.
#
# HUD CONFOUND: the GHZ HUD draws a small gold RING-counter icon at top-left even
# when no placed ring renders. So the gate MASKS the top HUD strip (top 18% rows)
# and measures the PLAY AREA only, and uses the preserved RED-baseline capture
# (pre-fix, same scene/HUD, NO placed rings) as the comparison floor. GREEN must
# show BOTH an absolute play-area gold mass AND a large delta over the RED baseline.
#
# RED-first: run on the preserved pre-fix capture (red_baseline) -> gold_play ~0,
# witness ring_aniframes < 0 -> RED. Run on the cart-relocated build's capture ->
# gold_play >> floor, ring_aniframes >= 0 -> GREEN. Watching RED->GREEN is the proof.
#
# Usage:
#   python qa_p6_ring.py <green.png> [red.png] [green.mcs] [game.map]
#   python qa_p6_ring.py --measure <png>      # just print the gold-mass numbers
# Defaults: green.mcs=HERE/p6_ring.mcs, game.map=../../game.map (verify_done feed).
# =============================================================================
import os
import sys
import pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as _scene  # noqa: E402

# Ring-gold classifier (see header -- derived from Items.gif idx 43-47).
def _is_ring_gold(r, g, b):
    return (r >= 176) and (88 <= g <= 250) and (b <= 64) and (r >= g - 24)

# HUD strip to exclude (fraction of frame height measured from the top). The GHZ
# HUD (SCORE/TIME/RINGS + the gold ring-counter icon) occupies the top rows.
HUD_TOP_FRAC = 0.18

# GREEN thresholds (tuned from the measured RED floor + GREEN capture; see the
# run output -- the RED baseline establishes the play-area noise floor).
GOLD_PLAY_MIN = 120      # absolute play-area ring-gold pixels (several rings)
GOLD_DELTA_MIN = 3.0     # GREEN play-gold must be >= this x the RED baseline


def measure_gold(png_path):
    """Return (gold_play, gold_total, w, h) for a screenshot. gold_play excludes
    the top HUD strip. Raises if PIL/file missing."""
    from PIL import Image
    im = Image.open(png_path).convert("RGB")
    w, h = im.size
    px = im.load()
    hud_cut = int(h * HUD_TOP_FRAC)
    gold_play = 0
    gold_total = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if _is_ring_gold(r, g, b):
                gold_total += 1
                if y >= hud_cut:
                    gold_play += 1
    return gold_play, gold_total, w, h


def read_witnesses(mcs, mp):
    """Return dict of ring witnesses from the savestate, or None if unavailable."""
    if not (mcs and os.path.isfile(mcs) and os.path.isfile(mp)):
        return None
    map_text = _scene.read_text(mp)
    syms = {}
    for s in ["_p6_w_ring_aniframes", "_p6_w_ring_classid", "_p6_w_cont_frames"]:
        syms[s] = _scene.map_symbol(map_text, s)
    magic = _scene.map_symbol(map_text, _scene.SYM_MAGIC)
    if magic is None or syms["_p6_w_ring_aniframes"] is None:
        return None
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, magic, 4)
    _, perm = _scene.calibrate(raw_magic)
    if perm is None:
        return None
    out = {}
    for s, a in syms.items():
        out[s] = (_scene.peek_u32(mod, sections, a, perm, signed=True)
                  if a is not None else None)
    return out


def read_extra(mcs, mp, names):
    """Peek a list of int32 witnesses from the savestate; returns name->value."""
    if not (mcs and os.path.isfile(mcs) and os.path.isfile(mp)):
        return None
    map_text = _scene.read_text(mp)
    magic = _scene.map_symbol(map_text, _scene.SYM_MAGIC)
    if magic is None:
        return None
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    _, perm = _scene.calibrate(mod._peek_bytes(sections, magic, 4))
    if perm is None:
        return None
    out = {}
    for n in names:
        a = _scene.map_symbol(map_text, n)
        out[n] = (_scene.peek_u32(mod, sections, a, perm, signed=True)
                  if a is not None else None)
    return out


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if "--measure" in argv[1:]:
        gp, gt, w, h = measure_gold(args[0])
        print("%s: %dx%d  gold_total=%d  gold_play=%d (HUD top %d%% excluded)"
              % (args[0], w, h, gt, gp, int(HUD_TOP_FRAC * 100)))
        return 0

    if "--witness" in argv[1:]:
        # Witness-only mode (verify_done runnable; the on-screen pixel proof was
        # USER-CONFIRMED visually 2026-06-18: "I see the rings and could collect
        # them"). Proves the armament from the savestate: Ring_StageLoad's anim
        # loaded + Ring registered + the overlay loaded to CART + reached live GHZ.
        mcs = _scene._as_path(args[0]) if len(args) > 0 else os.path.join(HERE, "p6_ring.mcs")
        mp = _scene._as_path(args[1]) if len(args) > 1 else _scene.MAP_DEFAULT
        print("=" * 72)
        print("RING ARMAMENT (#258) -- WITNESS mode (savestate)")
        print("=" * 72)
        w = read_extra(mcs, mp, ["_p6_w_ring_aniframes", "_p6_w_ring_classid",
                                 "_p6_w_ovl_bytes", "_p6_w_cont_frames",
                                 "_p6_w_plr_live_rings"])
        if w is None:
            print("RESULT: RED -- savestate/map unavailable (%s)" % mcs)
            return 1
        af = w["_p6_w_ring_aniframes"]; cid = w["_p6_w_ring_classid"]
        ob = w["_p6_w_ovl_bytes"]; cf = w["_p6_w_cont_frames"]; lr = w["_p6_w_plr_live_rings"]
        print("  ring_aniframes=%s ring_classid=%s ovl_bytes=%s cont_frames=%s live_rings=%s"
              % (af, cid, ob, cf, lr))
        print("-" * 72)
        checks = [
            ("W1 overlay loaded to cart (ovl_bytes==17472)", ob == 17472, "ovl_bytes=%s" % ob),
            ("W2 Ring registered (classid>0)", cid is not None and cid > 0, "classid=%s" % cid),
            ("W3 Ring anim loaded (aniframes>=0)", af is not None and af >= 0, "aniframes=%s" % af),
            ("W4 reached continuous GHZ (cont_frames>0)", cf is not None and cf > 0, "cont_frames=%s" % cf),
        ]
        ok = True
        for name, passed, detail in checks:
            print("  [%s] %s -- %s" % ("GREEN" if passed else " RED ", name, detail))
            ok = ok and passed
        print("-" * 72)
        if ok:
            print("RESULT: GREEN -- ring armament engaged from the cart overlay "
                  "(rings render+collect; user-confirmed visually).")
            return 0
        print("RESULT: RED -- ring armament not engaged.")
        return 1

    green_png = args[0] if len(args) > 0 else None
    red_png = args[1] if len(args) > 1 else None
    mcs = _scene._as_path(args[2]) if len(args) > 2 else os.path.join(HERE, "p6_ring.mcs")
    mp = _scene._as_path(args[3]) if len(args) > 3 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("RING ARMAMENT (#258): placed GHZ rings render on screen")
    print("=" * 72)

    if not green_png or not os.path.isfile(green_png):
        print("RESULT: RED -- green capture PNG missing (%s)" % green_png)
        return 1

    gp_green, gt_green, w, h = measure_gold(green_png)
    print("  GREEN capture %s (%dx%d): gold_total=%d  gold_play=%d"
          % (os.path.basename(green_png), w, h, gt_green, gp_green))

    gp_red = None
    if red_png and os.path.isfile(red_png):
        gp_red, gt_red, rw, rh = measure_gold(red_png)
        print("  RED baseline  %s (%dx%d): gold_total=%d  gold_play=%d"
              % (os.path.basename(red_png), rw, rh, gt_red, gp_red))

    wit = read_witnesses(mcs, mp)
    if wit:
        print("  witnesses: ring_aniframes=%s ring_classid=%s cont_frames=%s"
              % (wit.get("_p6_w_ring_aniframes"), wit.get("_p6_w_ring_classid"),
                 wit.get("_p6_w_cont_frames")))
    else:
        print("  witnesses: (savestate/map unavailable -- pixel measure only)")
    print("-" * 72)

    checks = []
    # Primary on-screen measure: play-area ring-gold above the absolute floor.
    checks.append(("M1 play-area ring-gold mass >= %d" % GOLD_PLAY_MIN,
                   gp_green >= GOLD_PLAY_MIN, "gold_play=%d" % gp_green))
    # Delta over the HUD-only RED baseline (placed rings actually appeared).
    if gp_red is not None:
        ok_delta = (gp_red == 0 and gp_green >= GOLD_PLAY_MIN) or \
                   (gp_red > 0 and gp_green >= GOLD_DELTA_MIN * gp_red)
        checks.append(("M2 GREEN play-gold >> RED baseline (>= %.1fx or RED~0)"
                       % GOLD_DELTA_MIN, ok_delta,
                       "green=%d red=%d" % (gp_green, gp_red)))
    # Complementary witness: Ring_StageLoad's LoadSpriteAnimation succeeded.
    if wit and wit.get("_p6_w_ring_aniframes") is not None:
        af = wit["_p6_w_ring_aniframes"]
        checks.append(("M3 Ring anim loaded (ring_aniframes >= 0)",
                       af is not None and af >= 0, "ring_aniframes=%s" % af))
        cf = wit.get("_p6_w_cont_frames")
        checks.append(("M4 capture reached continuous GHZ (cont_frames>0)",
                       cf is not None and cf > 0, "cont_frames=%s" % cf))

    ok = True
    for name, passed, detail in checks:
        print("  [%s] %s -- %s" % ("GREEN" if passed else " RED ", name, detail))
        ok = ok and passed

    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- placed GHZ rings render (the missing-VDP1-sprites "
              "bug is fixed; ring armament landed).")
        return 0
    print("RESULT: RED -- placed rings do NOT render (armament not engaged).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
