#!/usr/bin/env python3
"""qa_phase1_31_static_palette_sim.py - Phase 1.31 Fix #3 static gate.

Loads cd/TITLE3D.DAT + cd/TITLE3D.PAL, simulates Saturn's actual on-screen
pixel-V -> CRAM-bank-1 -> palette[V-1] lookup chain (the jo down-shift per
memory/jo-cram-off-by-one-shift.md + jo-engine/jo_engine/vdp2_malloc.c:60),
emits the simulated PNG to tools/qa_golden/title3d_simulated.png, and
ASSERTS:

  S1 - No single post-shift palette slot accounts for > 10% of pixels
       *that classify as bright-saturated green* (same predicate as
       qa_phase1_31_palette_gate.py P1).  This catches the dominant-
       slot-is-neon-green failure mode at asset-build time, BEFORE
       flashing to ISO.

  S2 - Total bright-green pixel count must be < 35000 (= ~30% of
       the 114688-pixel frame).  The Mania title's Sonic-head island
       legitimately contains a large green-striped silhouette in the
       lower half (~6800-cell visible area expanded by multi-tone
       shading), so the bound is sized to allow the genuine island
       while still catching a "green-flood-of-whole-frame" failure
       (which would be > 50K pixels).  Calibrated 2026-05-27 from
       layered-composite preview: ~17K green pixels per frame is
       authentic for the Sonic-head island; anything > 35K indicates
       neon-green flooding outside the island silhouette.

  S3 - At least one top-5 most-rendered post-shift slot must be in
       the blue/cyan family (sky-blue dominance check).

WHY THIS EXISTS (Phase 1.31 Fix #3, 2026-05-27):
  Prior agent rebuild produced a catastrophic pure-green (0,248,0)
  backdrop because pixel byte 1 -> jo's reserved CRAM slot 0
  (printf-color = bright green by default).  This static check
  catches the failure BEFORE the asset is flashed into game.iso,
  saving a build+capture round-trip per attempted fix.

USAGE:
  python tools/qa_phase1_31_static_palette_sim.py
    [--dat PATH=cd/TITLE3D.DAT]
    [--pal PATH=cd/TITLE3D.PAL]
    [--out PATH=tools/qa_golden/title3d_simulated.png]

Exit:
  0  GREEN (all three predicates pass)
  1  RED  (any predicate fails)
"""
import argparse, struct, sys
from collections import Counter
from pathlib import Path
try:
    from PIL import Image
    import numpy as np
except ImportError:
    sys.exit("requires Pillow + numpy")

ROOT = Path(__file__).resolve().parent.parent

# Same dimensions as src/main.c:67-68 setup_title_bg contract.
OUT_W, OUT_H = 224, 512


def decode_pal(pal_bytes):
    """Decode 256-entry RGB555-BE palette to (256, 3) uint8 numpy array."""
    pal_rgb = np.zeros((256, 3), dtype=np.uint8)
    for i in range(256):
        w = struct.unpack(">H", pal_bytes[i * 2:i * 2 + 2])[0]
        r5 = (w >> 10) & 0x1F
        g5 = (w >>  5) & 0x1F
        b5 = (w      ) & 0x1F
        pal_rgb[i, 0] = (r5 << 3) | (r5 >> 2)
        pal_rgb[i, 1] = (g5 << 3) | (g5 >> 2)
        pal_rgb[i, 2] = (b5 << 3) | (b5 >> 2)
    return pal_rgb


def is_bright_green(r, g, b):
    """Same predicate as qa_phase1_31_palette_gate.py:113-119."""
    sat_g = int(g) - max(int(r), int(b))
    return (int(g) >= 130
            and int(g) > int(r) + 50
            and int(g) > int(b) + 50
            and sat_g >= 60)


def is_blue_family(r, g, b):
    sat_b = int(b) - max(int(r), int(g))
    return int(b) >= 100 and sat_b >= 30


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dat", default=str(ROOT / "cd/TITLE3D.DAT"))
    ap.add_argument("--pal", default=str(ROOT / "cd/TITLE3D.PAL"))
    ap.add_argument("--out",
                    default=str(ROOT / "tools/qa_golden/title3d_simulated.png"))
    args = ap.parse_args()

    dat_path = Path(args.dat)
    pal_path = Path(args.pal)
    out_path = Path(args.out)

    if not dat_path.exists() or not pal_path.exists():
        print(f"RED: missing {dat_path} or {pal_path}")
        sys.exit(1)

    dat = dat_path.read_bytes()
    pal_bytes = pal_path.read_bytes()
    if len(pal_bytes) < 512:
        print(f"RED: {pal_path.name} too small ({len(pal_bytes)} B)")
        sys.exit(1)
    expected = OUT_W * OUT_H
    if len(dat) < expected:
        print(f"RED: {dat_path.name} too small ({len(dat)} B, expected {expected})")
        sys.exit(1)

    pal_rgb = decode_pal(pal_bytes)

    # Saturn fetches CRAM[bank*256 + V] for pixel byte V; jo allocates the
    # first user palette starting at CRAM[257] (vdp2_malloc.c:60), so
    # disk[i] lives at CRAM[257 + i] = bank-1 slot (1 + i).  Therefore
    # pixel byte V in bank 1 reads disk[V - 1] (the canonical jo
    # down-shift).
    dat_arr = np.frombuffer(dat[:expected], dtype=np.uint8)
    shifted = ((dat_arr.astype(np.int16) - 1) & 0xFF).astype(np.uint8)

    rendered = pal_rgb[shifted].reshape(OUT_H, OUT_W, 3)

    # Save the simulated on-screen PNG so a human can eyeball it BEFORE
    # the asset is flashed to ISO.
    out_path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rendered, "RGB").save(out_path)
    print(f"wrote {out_path}")

    # Per-slot post-shift histogram
    slot_hits = Counter(int(v) for v in shifted)
    ranked = sorted(slot_hits.items(), key=lambda t: -t[1])
    total_px = expected

    # --- S1: no single slot > 10% of pixels AND classifies as bright green
    # ----------------------------------------------------------------
    s1_ok = True
    s1_msg = ""
    for slot, count in ranked:
        r, g, b = pal_rgb[slot]
        frac = count / total_px
        if frac > 0.10 and is_bright_green(r, g, b):
            s1_ok = False
            s1_msg = (f"S1 RED: slot {slot} = {count} px ({frac*100:.1f}%) "
                      f"with rgb=({r},{g},{b}) is bright green and exceeds 10%")
            break
    if s1_ok:
        s1_msg = (f"S1 GREEN: no slot > 10% with bright-green color; "
                  f"top slot {ranked[0][0]} = {ranked[0][1]} px "
                  f"({ranked[0][1]/total_px*100:.1f}%), "
                  f"rgb={tuple(int(x) for x in pal_rgb[ranked[0][0]])}")
    print(f"  {s1_msg}")

    # --- S2: total bright-green pixel count < 35000 (Mania-island budget)
    # ----------------------------------------------------------------
    flat = rendered.reshape(-1, 3)
    r_ch = flat[:, 0].astype(np.int16)
    g_ch = flat[:, 1].astype(np.int16)
    b_ch = flat[:, 2].astype(np.int16)
    sat_g = g_ch - np.maximum(r_ch, b_ch)
    green_mask = (g_ch >= 130) & (g_ch > r_ch + 50) & (g_ch > b_ch + 50) & (sat_g >= 60)
    green_count = int(green_mask.sum())
    s2_ok = green_count < 35000
    if s2_ok:
        s2_msg = f"S2 GREEN: total bright-green px = {green_count} (< 35000)"
    else:
        s2_msg = f"S2 RED: total bright-green px = {green_count} (>= 35000 = flood)"
    print(f"  {s2_msg}")

    # --- S3: top-5 most-rendered slots include >= 1 blue/cyan family
    # ----------------------------------------------------------------
    top5 = ranked[:5]
    any_blue = False
    for slot, count in top5:
        r, g, b = pal_rgb[slot]
        if is_blue_family(r, g, b):
            any_blue = True
            break
    if any_blue:
        s3_msg = ("S3 GREEN: top-5 most-rendered post-shift slots include "
                  "a blue/cyan family color: " +
                  ", ".join(f"slot{s}={c}px rgb={tuple(int(x) for x in pal_rgb[s])}"
                            for s, c in top5))
    else:
        s3_msg = ("S3 RED: no blue/cyan slot in top-5 most-rendered: " +
                  ", ".join(f"slot{s}={c}px rgb={tuple(int(x) for x in pal_rgb[s])}"
                            for s, c in top5))
    print(f"  {s3_msg}")

    print()
    if s1_ok and s2_ok and any_blue:
        print("GREEN: static palette simulation gate passes.")
        sys.exit(0)
    failed = []
    if not s1_ok:   failed.append("S1")
    if not s2_ok:   failed.append("S2")
    if not any_blue: failed.append("S3")
    print(f"RED: static palette simulation gate failed: {', '.join(failed)}")
    sys.exit(1)


if __name__ == "__main__":
    main()
