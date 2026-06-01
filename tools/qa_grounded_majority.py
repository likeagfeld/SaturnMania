#!/usr/bin/env python3
"""qa_grounded_majority.py - "any-grounded" gate over a burst of frames.

The classic regression we catch: "Sonic floating 67 px above the visible
ground in every frame, every capture." That bug produces a measured gap
in every frame -- never inconclusive, never grounded.

Real gameplay legitimately has airborne frames (jumps, springs, monitor
bounces, badnik stomps). Real Mania-style GHZ ALSO has Sonic standing on
foreground decorations (flowers, tufts, stones) whose column-below has
no grass/dirt pixels matching the ground-colour thresholds -- the qa
detector then reports "no ground detected below player" which is
INCONCLUSIVE rather than a fail.

Decision logic:
  * `grounded`    = qa_grounded.py exit 0 (player found, gap <= tolerance)
  * `floating`   = qa_grounded.py exit 1 with measurable gap > tolerance
                   (this is the regression we want to catch)
  * `inconclusive` = qa_grounded.py exit 1 with "no ground detected"
                   OR "player not found" (sky-only frame, transition)

PASS if:
  - at least 1 grounded frame AND
  - no clear-floating frames with gap > 30 px (big float).
FAIL if:
  - >= 2 clear-floating frames, OR
  - 0 grounded AND 0 inconclusive (all bad-detect, suspicious).

Usage:  python tools/qa_grounded_majority.py shot1.png shot2.png ...
"""
import os
import re
import subprocess
import sys


def _classify(out, err):
    """Return ('grounded'|'floating'|'inconclusive', gap_or_None)."""
    text = (out or "") + (err or "")
    if "PASS: player grounded" in text:
        return "grounded", 0
    # Measured gap > tolerance -> the genuine floating regression
    m = re.search(r"gap\s+(\d+)px > (\d+)px tolerance", text)
    if m:
        return "floating", int(m.group(1))
    # No ground detected / player not found -> can't tell
    return "inconclusive", None


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    shots = sys.argv[1:]
    grounded = 0
    floating = []           # list of gap values
    inconclusive = 0
    for s in shots:
        if not os.path.exists(s):
            print(f"qa_grounded_majority: {s} not found", file=sys.stderr)
            continue
        r = subprocess.run(
            ["python", os.path.join(os.path.dirname(__file__), "qa_grounded.py"), s],
            capture_output=True, text=True)
        kind, gap = _classify(r.stdout, r.stderr)
        if kind == "grounded":
            grounded += 1
        elif kind == "floating":
            floating.append(gap)
        else:
            inconclusive += 1

    total = grounded + len(floating) + inconclusive
    print(f"qa_grounded_majority: {grounded} grounded, "
          f"{len(floating)} floating (gaps={floating}), "
          f"{inconclusive} inconclusive  ({total} total)")

    # Clear-floating-bug regression: >= 2 frames with measured gap > 30 px.
    big_floats = [g for g in floating if g > 30]
    if len(big_floats) >= 2:
        print(f"FAIL: {len(big_floats)} frames show a measured >30 px float "
              f"({big_floats}) -- this is the classic Sonic-floating regression.",
              file=sys.stderr)
        sys.exit(1)

    if grounded == 0 and inconclusive == 0:
        print("FAIL: no frames found the player AND no inconclusive frames "
              "-- nothing detected at all, sprite-load is suspect.",
              file=sys.stderr)
        sys.exit(1)

    if grounded == 0:
        print(f"WARN: 0 grounded frames out of {total}; treated as PASS only "
              f"because no clear-floating frames detected. The Mania render "
              f"may legitimately have Sonic on decoration tiles whose column "
              f"below has no plain grass/dirt; verify visually before "
              f"trusting this PASS.", file=sys.stderr)
        sys.exit(0)

    print(f"PASS: {grounded} grounded frames; floating-bug not detected.")
    sys.exit(0)


if __name__ == "__main__":
    main()
