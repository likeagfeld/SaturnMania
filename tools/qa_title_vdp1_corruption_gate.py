#!/usr/bin/env python3
"""qa_title_vdp1_corruption_gate.py — Title VDP1 command-list / VRAM
corruption gate (Phase QA-VDP1, 2026-05-28).

User report: "title menu is all broken now."

Measured RED baseline (qa_arc_100.png, current build at 2026-05-28 19:02):
  - Bright-green full-height bars at BOTH viewport edges:
      ~20,000 green px concentrated in the left+right edge columns
      (col 3 and col 908 each carry a full 720px-tall bar).
  - ALL title VDP1 sprites absent: bright-red SONIC-banner pixels = 0
      (the MANIA logo / SONIC wordmark / Sonic body / ribbons do not
       render at all; only the VDP2 GHZ-scene backdrop is visible).

Root-cause (CONFIRMED by measurement 2026-05-28, savestate
qa_greenbar_probe.mcs + capture column analysis):
  entities_load_assets() runs at engine_init and copies ~232 KB of GHZ
  gameplay-entity sprites into the 512 KB VDP1 VRAM BEFORE the title
  displays. setup_title_assets() loads the title sprites first; the
  subsequent entity loads overflow VDP1 VRAM and clobber the title
  sprite char-data region -> every title sprite reads garbage / null
  char data -> NOTHING DRAWS (P2 RED baseline = 0 red-banner px).
  Entities are GHZ objects with no business loading during the title;
  the decomp loads them in the GHZ StageLoad pass. FIX: relocate
  entities_load_assets() off mania_engine_init onto the synchronous
  mania_load_ghz_scene() path (Game.c:1594). Measured P2 0 -> 26052.

  H4-GREENBAR HYPOTHESIS *FALSIFIED* (2026-05-28): the original H4
  guessed the same VDP1 overflow ALSO paints the green edge bars. The
  data disproves this:
    - The green full-height edge bars (capture cols 3-10 + 901-908,
      720 px tall) are BYTE-IDENTICAL in the broken baseline
      (qa_arc_82, 32364 green px) AND the fixed build (qa_title_vdp1_82,
      27927 green px) -> untouched by the entity fix.
    - VDP1 framebuffer dump shows ZERO full-height green columns at any
      stride (320/352/512/704); max per-column is 60-108 words. The bars
      are NOT in VDP1 at all.
    - VDP2 priority peek: PRIR=0x0000 (RBG0 hidden), back-screen=0x0000
      (black). The remaining enabled green VDP2 layer is NBG1
      (PRINA N1PRIN=6) -> the title island/foliage layer shows a green
      tile column at the extreme ~5 px L/R display margins (CRT
      overscan region). This is a SEPARATE pre-existing artifact, tracked
      independently (see Task: NBG1 green-edge artifact). P1 below is
      therefore INFORMATIONAL, not a blocker for the missing-sprite
      regression this gate enforces (P2 + P3).

Predicates:
  P1 — INFORMATIONAL ONLY (not a pass/fail blocker; see falsified-
       hypothesis note above). Capture-side green-edge-bar signature.
       Bright-green pixels (G>180, R<120, B<120) summed over the left +
       right EDGE bands (outer 1/30 of width, each side). Reported as a
       measurement so the separate NBG1 island-edge artifact stays
       visible/tracked; does NOT fail this gate because it is proven
       independent of the missing-sprite regression P2+P3 enforce.
  P2 — Capture-side title-sprite presence.
       Bright-red SONIC-banner pixels (R>180, G<80, B<80) anywhere in the
       window must be >= P2_RED_BANNER_MIN. RED baseline = 0 (sprites
       fully absent); a rendered title shows the red SONIC wordmark on the
       ribbon banner. (The GHZ backdrop contains NO bright-red, so this is
       a clean discriminator.)
  P3 — Source-level: entity assets NOT loaded on the synchronous title
       boot path. entities_load_assets() must NOT be called (uncommented)
       from mania_engine_init in src/mania/Game.c. After the fix it is
       deferred to the GHZ scene-load path.

Capture frames: scans qa_title_vdp1_*.png (verify_done step) in repo root,
falling back to qa_arc_*.png (the RED baseline arc) so the gate fires RED
out of the box on the broken build.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

P1_GREEN_EDGE_MAX = 3000   # RED baseline ~20,367 px in edge bands; a clean
                           # title (no corruption bars) measures ~0.
P2_RED_BANNER_MIN = 200    # RED baseline = 0 px; the SONIC red banner spans
                           # a 176x52 sprite -> hundreds of saturated-red px
                           # once the title VDP1 sprites render.


def _edge_band_width(W):
    return max(4, W // 30)


def count_green_edges(arr):
    R = arr[:, :, 0].astype('int32')
    G = arr[:, :, 1].astype('int32')
    B = arr[:, :, 2].astype('int32')
    green = (G > 180) & (R < 120) & (B < 120)
    W = arr.shape[1]
    ew = _edge_band_width(W)
    left = int(green[:, :ew].sum())
    right = int(green[:, W - ew:].sum())
    return left + right


def count_red_banner(arr):
    R = arr[:, :, 0].astype('int32')
    G = arr[:, :, 1].astype('int32')
    B = arr[:, :, 2].astype('int32')
    red = (R > 180) & (G < 80) & (B < 80)
    return int(red.sum())


def gate_captures(frames):
    try:
        import numpy as np  # noqa: F401
        from PIL import Image
    except Exception as exc:
        print(f"  INFO: P1+P2 skipped (no numpy/PIL): {exc}")
        return None, None
    if not frames:
        print("  INFO: P1+P2 skipped (no capture frames found)")
        return None, None
    import numpy as np
    green_edge_max = 0
    red_banner_max = 0
    for f in frames:
        try:
            im = Image.open(f).convert('RGB')
            arr = np.array(im)
            ge = count_green_edges(arr)
            rb = count_red_banner(arr)
            green_edge_max = max(green_edge_max, ge)
            red_banner_max = max(red_banner_max, rb)
            print(f"    {os.path.basename(f)}: green_edge={ge} red_banner={rb}")
        except Exception as exc:
            print(f"    {f}: scan error {exc}")
    return green_edge_max, red_banner_max


def gate_p3_source():
    """entities_load_assets() must NOT be called from mania_engine_init."""
    path = os.path.join(ROOT, 'src', 'mania', 'Game.c')
    if not os.path.exists(path):
        print(f"  FAIL: P3 — {path} missing")
        return False
    with open(path, 'r', encoding='utf-8', errors='replace') as fh:
        lines = fh.readlines()
    # Find the mania_engine_init function body.
    start = None
    for i, line in enumerate(lines):
        if re.search(r'\bmania_engine_init\s*\(', line) and '{' in ''.join(lines[i:i + 3]):
            # crude: the definition (not a prototype ending in ';')
            if not lines[i].rstrip().endswith(';'):
                start = i
                break
    if start is None:
        print("  WARN: P3 — mania_engine_init definition not located; "
              "scanning whole file for an active call")
        scan = list(enumerate(lines))
    else:
        # walk to matching close brace
        depth = 0
        end = len(lines)
        seen = False
        for j in range(start, len(lines)):
            depth += lines[j].count('{')
            depth -= lines[j].count('}')
            if '{' in lines[j]:
                seen = True
            if seen and depth <= 0:
                end = j + 1
                break
        scan = list(enumerate(lines[start:end], start=start))
    active = 0
    for i, line in scan:
        stripped = line.lstrip()
        is_comment = (stripped.startswith('//') or stripped.startswith('*')
                      or stripped.startswith('/*'))
        if is_comment:
            continue
        if re.search(r'\bentities_load_assets\s*\(\s*\)\s*;', line):
            active += 1
            print(f"    Game.c:{i + 1}: ACTIVE entities_load_assets() on boot path")
    print(f"    Active boot-path entities_load_assets() calls: {active}")
    return active == 0


def main():
    print("Gate QA-VDP1: title VDP1 corruption (missing sprites + green bars)")
    print("=================================================================")

    fail = []

    frames = sorted(glob.glob(os.path.join(ROOT, 'qa_title_vdp1_*.png')))
    if not frames:
        frames = sorted(glob.glob(os.path.join(ROOT, 'qa_arc_*.png')))
        if frames:
            print(f"  NOTE: using qa_arc_*.png RED-baseline arc ({len(frames)} "
                  f"frames) — gate should fire RED on these")
    # qa_gate.png is the persisted settled-title frame synthesized by
    # verify_done Gate 3.5 (content-aware red-banner-max picker). The arc
    # frames (qa_arc_*/qa_title_vdp1_*) get swept by Gate 4 cleanup BEFORE
    # this gate runs, so qa_gate.png is the only guaranteed-present settled
    # frame at this point. Always include it so P2 is measured, never skipped.
    qa_gate = os.path.join(ROOT, 'qa_gate.png')
    if os.path.exists(qa_gate) and qa_gate not in frames:
        frames.append(qa_gate)
        print(f"  NOTE: including persisted qa_gate.png settled-title frame")

    print("  P1 + P2 — capture-side green-edge-bar + title-sprite-presence")
    ge_max, rb_max = gate_captures(frames)
    if ge_max is not None:
        # P1 is INFORMATIONAL only: the green edge bars were proven to be a
        # separate, pre-existing NBG1 island-edge artifact (byte-identical in
        # broken + fixed builds; absent from the VDP1 framebuffer; RBG0
        # priority 0). It is NOT caused by the missing-sprite regression this
        # gate enforces, so it never fails the gate. Tracked independently.
        if ge_max <= P1_GREEN_EDGE_MAX:
            print(f"  INFO P1: green-edge-bar max {ge_max} <= {P1_GREEN_EDGE_MAX}")
        else:
            print(f"  INFO P1: green-edge-bar max {ge_max} > {P1_GREEN_EDGE_MAX} "
                  f"(NBG1 island-edge artifact, separate tracked issue; "
                  f"NOT a gate failure)")
    if rb_max is None:
        # No settled-title frame could be measured (no frames / no numpy-PIL).
        # P2 is the measured sprite-presence check that enforces the
        # missing-sprite regression class — an unmeasurable P2 must FAIL, never
        # silently pass, or the regression could ship undetected.
        print("  FAIL P2: no settled-title frame measurable "
              "(P2 sprite-presence check could not run)")
        fail.append('P2')
    elif rb_max >= P2_RED_BANNER_MIN:
        print(f"  PASS P2: red-banner max {rb_max} >= {P2_RED_BANNER_MIN}")
    else:
        print(f"  FAIL P2: red-banner max {rb_max} < {P2_RED_BANNER_MIN} "
              f"(title VDP1 sprites absent)")
        fail.append('P2')

    print("")
    print("  P3 — entity asset load not on the title boot path")
    if gate_p3_source():
        print("  PASS P3: entities_load_assets() deferred off boot path")
    else:
        print("  FAIL P3: entities_load_assets() still loads at engine_init "
              "(VDP1 VRAM overflow risk)")
        fail.append('P3')

    print("")
    if fail:
        print(f"GATE QA-VDP1 FAIL: {','.join(fail)}")
        return 1
    print("GATE QA-VDP1 PASS (title VDP1 sprites render [P2]; entity load off "
          "boot path [P3]. P1 green-edge bars are a separate tracked NBG1 issue.)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
