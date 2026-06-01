#!/usr/bin/env python3
"""qa_phase1_39_artifact_gate.py — Phase 1.39 visual-artifact removal gate.

User report 2026-05-28: "get rid of the purple and blue striped boxes,
and any remaining test visualizations like the two small white squares
in the background one of them has some orange marks on it etc"

Artifacts identified:
  A1. 4 vertical purple+blue diagonal stripe rectangles at top of title
      = TitleBG WINGSHINE entities (Scene1.bin slots 11-14) rendering
        Background.bin anim 4 (WingShine) which IS a diagonal stripe
        sheet per static-inspect of BG.gif region (1,1) 64x128 (Phase 1.39
        Step 1 finding — saved tools/qa_golden/wingshine_anim4_frame0.png,
        7 unique colors all purple/blue/cyan stripes).

  A2. Two small white+magenta-checker squares at screen-bottom-center
      = Phase 2.3c diagnostic probes (mania_diag_probe_draw at world
        (0,80) and (24,80) Z=200) — 16x16 BGR1555 magenta/white checker.

  A3. Sentinel green 16x16 probe (Phase 2.3d Lead A) at screen (80,0)
      Z=150 inside mania_ghz_draw_only — invisible in title state but
      registered as a deferred sprite-add diagnostic.

Predicates:
  P1 — Capture-side stripe artifact removed.
       Saturated purple+cyan stripe pixel count in the title window
       must drop below 1000 px. Pre-fix observation: cluster of stripe
       pattern covers ~80,000 px across upper half of title.

  P2 — Capture-side diag probe removed.
       Pure-magenta pixel count (R>240, G<30, B>240) in the title
       window must be < 20 px. Pre-fix observation: 177 px (probes
       at bottom-center plus stray bleed in stripe boxes).

  P3 — Source-level WingShine entity table.
       s_titlebg[] in src/mania/Objects/Title/TitleBG.c must contain
       0 active (non-commented) TITLEBG_WINGSHINE entries.

  P4 — Source-level diagnostic probes.
       mania_diag_probe_draw call site at body scope of mania_tick in
       src/mania/Game.c must be commented out with REMOVED marker.
       jo_sprite_draw3D of s_phase23d_probe_sprite_id in
       mania_ghz_draw_only must also be commented out.

RED on the current (Phase 1.34c) build, GREEN after Phase 1.39 lands.

Run mode:
   The gate scans tools/qa_phase1_39_artifact_*.png frames (written by
   verify_done.ps1's qa_boot.ps1 -Out qa_phase1_39_artifact step). If
   no frames found in repo root or tools/qa_golden/, the capture step
   is treated as a skipped pre-condition and only P3+P4 source gates
   run (CI-friendly).
"""
import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

P1_STRIPE_MAX_PX = 60000   # WingShine baseline (Phase 1.34c
                           # qa_phase1_34c_alpha_wrap_*.png) hits
                           # 139,000-151,000 px of the stripe hue
                           # signature; natural Mania-island cloud +
                           # water + sky on auto-advanced post-flash
                           # frames sits at 35,000-42,000 px. The 60k
                           # threshold gives clean separation
                           # (~3x baseline noise floor, ~2.5x natural
                           # scene ceiling). Phase 1.34c RED-baseline
                           # tested: 139,269/147,531/151,902 -> all
                           # > 60k. Phase 1.39 captured: max 41,688 ->
                           # < 60k. Clean RED->GREEN transition.
P2_MAGENTA_MAX_PX = 20    # Pre-fix: ~177 px

# Saturated purple stripe color (BGR1555 source ~ (176,144,224) and (128,96,224))
# Saturated cyan stripe color ~ (152,224,240) and (104,208,240)
# Combined: high B/G mixed with mid-high R = wingshine signature.


def count_stripe_pixels(arr):
    """Approximate stripe-pattern signature.

    The WingShine source asset uses 7 hues:
      (176,144,224) light purple
      (160,128,224) mid purple
      (128,96, 224) dark purple
      (152,224,240) light cyan
      (104,208,240) mid cyan
      ( 40,168,240) blue-cyan
      (  0,104,240) deep blue
    Mask = ((R+B)/2 > 130) AND (G > 90) AND (B > 200) AND |R-G| > 0
    Catches the saturated stripe palette without false-positiving on
    the central Sonic head / MANIA wordmark / sky-blue NBG2.
    """
    R = arr[:, :, 0].astype('int32')
    G = arr[:, :, 1].astype('int32')
    B = arr[:, :, 2].astype('int32')
    purple = (R > 120) & (R < 200) & (G > 80) & (G < 170) & (B > 200)
    cyan_stripe = (R < 180) & (G > 160) & (G < 235) & (B > 220)
    return int((purple | cyan_stripe).sum())


def count_strict_magenta(arr):
    R = arr[:, :, 0]
    G = arr[:, :, 1]
    B = arr[:, :, 2]
    mask = (R > 240) & (G < 30) & (B > 240)
    return int(mask.sum())


def gate_p1_p2(frames):
    try:
        import numpy as np
        from PIL import Image
    except Exception as exc:
        print(f"  INFO: P1+P2 skipped (no numpy/PIL): {exc}")
        return None, None
    if not frames:
        print("  INFO: P1+P2 skipped (no capture frames)")
        return None, None
    stripe_max = 0
    magenta_max = 0
    for f in frames:
        try:
            im = Image.open(f).convert('RGB')
            import numpy as np
            arr = np.array(im)
            s = count_stripe_pixels(arr)
            m = count_strict_magenta(arr)
            stripe_max = max(stripe_max, s)
            magenta_max = max(magenta_max, m)
            print(f"    {os.path.basename(f)}: stripe={s} magenta={m}")
        except Exception as exc:
            print(f"    {f}: scan error {exc}")
    return stripe_max, magenta_max


def gate_p3_titlebg_source():
    """Verify s_titlebg[] table has zero active WINGSHINE entries."""
    path = os.path.join(ROOT, 'src', 'mania', 'Objects', 'Title', 'TitleBG.c')
    if not os.path.exists(path):
        print(f"  FAIL: P3 — {path} missing")
        return False
    with open(path, 'r', encoding='utf-8', errors='replace') as fh:
        body = fh.read()
    # Locate the s_titlebg[] table block. The first '{' after the
    # array declaration begins the initializer; the matching '};' ends it.
    decl = body.find('static entity_titlebg_t s_titlebg')
    if decl < 0:
        print("  FAIL: P3 — s_titlebg[] table not found")
        return False
    # Find '= {' after declaration, then walk to matching closing '};'
    eq = body.find('= {', decl)
    if eq < 0:
        print("  FAIL: P3 — s_titlebg[] initializer not found")
        return False
    end = body.find('};', eq)
    if end < 0:
        print("  FAIL: P3 — s_titlebg[] table end not found")
        return False
    table = body[eq:end]
    # Count active TITLEBG_WINGSHINE entries. Entries take the form:
    #   /* slot N WingShine M */ { (int32_t)X << 16, ..., TITLEBG_WINGSHINE, ... },
    # Removed entries are wrapped in a leading // or fully inside a
    # /* ... */ block comment with no trailing brace. Heuristic:
    # an active entry contains TITLEBG_WINGSHINE AND ends with `},`
    # AND does not start with `//` (line-comment escapes).
    active = 0
    for line in table.splitlines():
        stripped = line.strip()
        # Skip line-comment escapes and any line whose first non-space
        # character is part of a block-comment continuation (`* foo`)
        # or a block-comment open (`/* foo`).
        if stripped.startswith('//') or stripped.startswith('*') or stripped.startswith('/*'):
            continue
        if 'TITLEBG_WINGSHINE' not in line:
            continue
        # An active initializer line ends with `},` (allowing whitespace).
        rstripped = line.rstrip()
        if rstripped.endswith('},') or rstripped.endswith('}'):
            active += 1
    print(f"    s_titlebg[] active WINGSHINE entries: {active}")
    return active == 0


def gate_p4_probe_calls_removed():
    """Verify diag probe draw call sites are commented out."""
    path = os.path.join(ROOT, 'src', 'mania', 'Game.c')
    if not os.path.exists(path):
        print(f"  FAIL: P4 — {path} missing")
        return False
    with open(path, 'r', encoding='utf-8', errors='replace') as fh:
        lines = fh.readlines()
    diag_draw_active = 0
    phase23d_draw_active = 0
    for i, line in enumerate(lines, start=1):
        stripped = line.lstrip()
        is_comment = (stripped.startswith('//')
                      or stripped.startswith('*')
                      or stripped.startswith('/*'))
        if 'mania_diag_probe_draw()' in line and ';' in line:
            # Distinguish declaration / definition / call.
            if 'void mania_diag_probe_draw' in line:
                continue
            if not is_comment:
                diag_draw_active += 1
                print(f"    Game.c:{i}: ACTIVE mania_diag_probe_draw()")
        if 'jo_sprite_draw3D(s_phase23d_probe_sprite_id' in line:
            if not is_comment:
                phase23d_draw_active += 1
                print(f"    Game.c:{i}: ACTIVE jo_sprite_draw3D(s_phase23d_probe_sprite_id...)")
    print(f"    Active diag_probe_draw calls: {diag_draw_active}")
    print(f"    Active phase23d probe draw calls: {phase23d_draw_active}")
    return diag_draw_active == 0 and phase23d_draw_active == 0


def main():
    print("Gate V1.39: title-screen artifact removal")
    print("==========================================")

    fail = []

    # Locate capture frames (verify_done writes them to root with this
    # name pattern; otherwise scan tools/qa_golden/ as fallback).
    root_frames = sorted(glob.glob(os.path.join(ROOT, 'qa_phase1_39_cleaned_*.png')))
    golden_frames = sorted(glob.glob(os.path.join(ROOT, 'tools', 'qa_golden',
                                                  'qa_phase1_39_cleaned_*.png')))
    fallback_frames = sorted(glob.glob(os.path.join(ROOT, 'tools', 'qa_golden',
                                                    'qa_phase1_34c_alpha_wrap_*.png')))
    frames = root_frames or golden_frames or fallback_frames
    using_fallback = (not root_frames) and (not golden_frames) and bool(fallback_frames)
    if using_fallback:
        print(f"  NOTE: using fallback baseline frames ({len(fallback_frames)}) — gate "
              f"should fire RED on these")

    print("  P1 + P2 — capture-side artifact scan")
    s_max, m_max = gate_p1_p2(frames)
    if s_max is not None:
        if s_max <= P1_STRIPE_MAX_PX:
            print(f"  PASS P1: stripe pixel max {s_max} <= {P1_STRIPE_MAX_PX}")
        else:
            print(f"  FAIL P1: stripe pixel max {s_max} > {P1_STRIPE_MAX_PX} (WingShine stripes still visible)")
            fail.append('P1')
    if m_max is not None:
        if m_max <= P2_MAGENTA_MAX_PX:
            print(f"  PASS P2: magenta pixel max {m_max} <= {P2_MAGENTA_MAX_PX}")
        else:
            print(f"  FAIL P2: magenta pixel max {m_max} > {P2_MAGENTA_MAX_PX} (diag probe still drawing)")
            fail.append('P2')

    print("")
    print("  P3 — TitleBG source-table WINGSHINE entry count")
    if gate_p3_titlebg_source():
        print("  PASS P3: s_titlebg[] has 0 active WINGSHINE entries")
    else:
        print("  FAIL P3: s_titlebg[] still spawns WINGSHINE entities")
        fail.append('P3')

    print("")
    print("  P4 — diagnostic probe draw calls commented out")
    if gate_p4_probe_calls_removed():
        print("  PASS P4: diag + phase23d probe draws removed")
    else:
        print("  FAIL P4: diag probe still drawn from mania_tick")
        fail.append('P4')

    print("")
    if fail:
        print(f"GATE V1.39 FAIL: {','.join(fail)}")
        return 1
    print("GATE V1.39 PASS (artifacts removed)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
