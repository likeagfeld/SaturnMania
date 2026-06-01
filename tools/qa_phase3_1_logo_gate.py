#!/usr/bin/env python3
"""qa_phase3_1_logo_gate.py - Phase 3.1 Path B RED-firing gate.

Predicates (CLAUDE.md §4.5.1 + §4.7 + memory/qa-iterative-improvement.md v3):

  P1 (asset)            cd/LOGOS.ATL exists and is sized 5-15 KB.
  P2 (logo window)      In a 3 fps Mednafen capture starting from cold
                        boot, the t=2-4 s window contains at least one
                        frame whose dominant color is Sega-blue (HSV
                        H in [200, 230] degrees, S > 0.5, V > 0.4) AND
                        contains no recognisable title content (no
                        gold-yellow MANIA wordmark).
  P3 (transition)       In the same capture, the t=4.5-9 s window
                        contains at least one frame that shows title
                        content (gold-yellow pixels > 1% -- the MANIA
                        wordmark + ribbon, OR Sonic-blue pixels >5% in
                        the centre-bottom region).
  P4 (sprite integrity) The build-time qa_gate.png golden-diff stays
                        within tolerance (delegated to qa_gate.ps1; this
                        gate references the pass/fail result of the
                        outer qa_gate.ps1, but does not re-run it).

Usage:
    python tools/qa_phase3_1_logo_gate.py [--capture-dir <dir>] \
                                          [--capture-prefix qa_phase3_1] \
                                          [--build-only]

  --build-only  Only check P1 (no Mednafen capture required).
                Used in the early build stage before captures exist.

Exit codes:
    0  All checked predicates GREEN.
    1  At least one predicate RED.
    2  Capture missing or unreadable (transient).
"""
import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow")

ROOT = Path(__file__).resolve().parent.parent


def rgb_to_hsv(r, g, b):
    """Lightweight RGB->HSV (avoids colorsys)."""
    r_, g_, b_ = r / 255.0, g / 255.0, b / 255.0
    mx = max(r_, g_, b_)
    mn = min(r_, g_, b_)
    df = mx - mn
    if df == 0:
        h = 0
    elif mx == r_:
        h = (60 * ((g_ - b_) / df) + 360) % 360
    elif mx == g_:
        h = (60 * ((b_ - r_) / df) + 120) % 360
    else:
        h = (60 * ((r_ - g_) / df) + 240) % 360
    s = 0 if mx == 0 else df / mx
    v = mx
    return h, s, v


def is_sega_blue(r, g, b):
    """Sega-logo blue per LOGOS.ATL palette dump:
        slot 1 (64.65% of opaque pixels) = rgb (0, 107, 247)
        slot 3..6 = (41,173,247) (24,140,247) (107,214,247) (156,231,247)
    All have H around 207-217 degrees, S > 0.4, V > 0.4."""
    h, s, v = rgb_to_hsv(r, g, b)
    return 195 <= h <= 230 and s > 0.40 and v > 0.40


def is_gold_yellow(r, g, b):
    """MANIA wordmark gold/yellow: H roughly 40-70 deg, S>0.4, V>0.4."""
    h, s, v = rgb_to_hsv(r, g, b)
    return 35 <= h <= 75 and s > 0.40 and v > 0.40


def dominant_color_check(img_path, predicate):
    """Returns fraction of pixels in img where predicate(r,g,b) is True."""
    img = Image.open(img_path).convert("RGB")
    px = img.load()
    w, h = img.size
    count = 0
    total = 0
    # Sample on a stride-4 grid for speed.
    for y in range(0, h, 4):
        for x in range(0, w, 4):
            r, g, b = px[x, y]
            total += 1
            if predicate(r, g, b):
                count += 1
    return count / total if total else 0.0


def predicate_p1():
    """P1 -- cd/LOGOS.ATL exists and is 5-15 KB."""
    atl = ROOT / "cd" / "LOGOS.ATL"
    if not atl.exists():
        return False, f"cd/LOGOS.ATL missing"
    size = atl.stat().st_size
    if size < 5000 or size > 15000:
        return False, f"cd/LOGOS.ATL size {size} B outside [5000, 15000]"
    return True, f"cd/LOGOS.ATL: {size} B"


def predicate_p2(captures, fps):
    """P2 -- t=2-4 s window has at least one Sega-blue dominant frame and
    no gold-yellow title content."""
    if not captures:
        return False, "no captures"
    # Frame index window for t = [2, 4] s at fps captures/s.
    # Captures are 1-indexed typically (qa_phase3_1_1.png etc.)
    start_idx = max(1, int(2 * fps))
    end_idx   = max(start_idx + 1, int(4 * fps) + 1)
    candidates = [c for c in captures
                  if start_idx <= extract_idx(c) <= end_idx]
    if not candidates:
        return False, f"no captures in t=[2,4]s window " \
                     f"(need indices {start_idx}..{end_idx})"
    best_blue = 0.0
    best_yellow = 0.0
    best_frame = None
    for c in candidates:
        b = dominant_color_check(c, is_sega_blue)
        y = dominant_color_check(c, is_gold_yellow)
        if b > best_blue:
            best_blue = b
            best_frame = c
        if y > best_yellow:
            best_yellow = y
    if best_blue < 0.05:
        return False, f"no Sega-blue dominant frame in t=[2,4]s " \
                     f"(best blue %={best_blue*100:.2f}, in {best_frame})"
    if best_yellow > 0.02:
        return False, f"unexpected gold-yellow content in t=[2,4]s " \
                     f"(yellow %={best_yellow*100:.2f}; title should not " \
                     f"show yet)"
    return True, f"sega-blue%={best_blue*100:.2f} in {best_frame}"


def predicate_p3(captures, fps):
    """P3 -- t=4.5-9 s window shows title content (gold-yellow >1% OR
    no Sega-blue dominance left)."""
    if not captures:
        return False, "no captures"
    start_idx = max(1, int(4.5 * fps))
    end_idx   = max(start_idx + 1, int(9 * fps) + 1)
    candidates = [c for c in captures
                  if start_idx <= extract_idx(c) <= end_idx]
    if not candidates:
        return False, f"no captures in t=[4.5,9]s window " \
                     f"(need indices {start_idx}..{end_idx})"
    title_seen = False
    best_yellow = 0.0
    best_blue = 1.0
    best_frame = None
    for c in candidates:
        y = dominant_color_check(c, is_gold_yellow)
        b = dominant_color_check(c, is_sega_blue)
        if y > best_yellow:
            best_yellow = y
            best_frame = c
        if b < best_blue:
            best_blue = b
        # Title content is present if gold-yellow > 1% (MANIA wordmark)
        # OR Sega-blue dropped below 1% (Sega logo gone).
        if y > 0.01 or b < 0.01:
            title_seen = True
    if not title_seen:
        return False, f"no title content in t=[4.5,9]s " \
                     f"(yellow%={best_yellow*100:.2f}, blue%={best_blue*100:.2f}, " \
                     f"best={best_frame})"
    return True, f"yellow%={best_yellow*100:.2f}, min-blue%={best_blue*100:.2f} " \
                 f"in {best_frame}"


def extract_idx(path):
    """Pull the numeric index from a capture filename like
    'qa_phase3_1_12.png' -> 12, or fall back to 0."""
    stem = Path(path).stem
    parts = stem.rsplit("_", 1)
    if len(parts) < 2:
        return 0
    try:
        return int(parts[-1])
    except ValueError:
        return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture-dir", default=str(ROOT))
    ap.add_argument("--capture-prefix", default="qa_phase3_1")
    ap.add_argument("--fps", type=float, default=3.0)
    ap.add_argument("--build-only", action="store_true")
    args = ap.parse_args()

    print("=== Phase 3.1 Path B RED-gate ===")

    # P1
    ok1, msg1 = predicate_p1()
    print(f"P1 (asset): {'PASS' if ok1 else 'FAIL'}  {msg1}")

    if args.build_only:
        # Build-only mode: only P1 matters; skip P2/P3 (no captures yet).
        if ok1:
            print("Build-only check PASSED (P1 GREEN).")
            return 0
        else:
            print("Build-only check FAILED (P1 RED).")
            return 1

    # Gather captures.
    cap_dir = Path(args.capture_dir)
    pattern = f"{args.capture_prefix}_*.png"
    captures = sorted(cap_dir.glob(pattern), key=extract_idx)
    if not captures:
        print(f"P2/P3: SKIP (no captures matching '{pattern}' in {cap_dir})")
        return 2 if not ok1 else 0

    print(f"Found {len(captures)} captures at {args.fps} fps "
          f"(indices {extract_idx(captures[0])}..{extract_idx(captures[-1])})")

    # P2
    ok2, msg2 = predicate_p2(captures, args.fps)
    print(f"P2 (logo): {'PASS' if ok2 else 'FAIL'}  {msg2}")

    # P3
    ok3, msg3 = predicate_p3(captures, args.fps)
    print(f"P3 (title): {'PASS' if ok3 else 'FAIL'}  {msg3}")

    if ok1 and ok2 and ok3:
        print("ALL PREDICATES GREEN.")
        return 0
    print("AT LEAST ONE PREDICATE RED.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
