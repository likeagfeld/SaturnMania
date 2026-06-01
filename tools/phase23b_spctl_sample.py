#!/usr/bin/env python3
"""Phase 2.3b SPCTL bisect sample.

Decodes the VDP2 SPCTL snapshots (pre / post jo_vdp2_set_nbg1_8bits_image,
and post ghz_setup_sky) encoded into the back-color by main.c::fg_vblank.

Encoding (BGR1555 back-color):
  bit 15      = 1 (opaque)
  bits 14..10 = SPCTL_pre_nbg1[4:0]   (R channel)
  bits  9.. 5 = SPCTL_post_nbg1[4:0]  (G channel)
  bits  4.. 0 = SPCTL_post_sky[4:0]   (B channel)

Border-zone sample positions (cols 5/10/906 at row 374) inherited from
Phase 1.12-1.14 work — proven reliable.

Per docs/COMPREHENSIVE_PLAN.md Sec 12.3b.

Usage:
  python tools/phase23b_spctl_sample.py qa_phase2_3b_spctl_NNN.png
  python tools/phase23b_spctl_sample.py qa_phase2_3b_spctl_*.png    (multi)
"""
from PIL import Image
import glob, os, sys


def decode_5bit(channel_8bit):
    """8-bit channel -> 5-bit field value 0..31."""
    return max(0, min(31, round(channel_8bit * 31 / 255)))


BORDER_POINTS = [(5, 374), (10, 374), (906, 374)]


def median(values):
    s = sorted(values)
    return s[len(s) // 2]


def sample_one(path):
    img = Image.open(path).convert("RGB")
    W, H = img.size
    pre_vals, post_vals, sky_vals = [], [], []
    rgbs = []
    for x, y in BORDER_POINTS:
        x = max(0, min(W - 1, x))
        y = max(0, min(H - 1, y))
        r, g, b = img.getpixel((x, y))
        rgbs.append((r, g, b))
        pre_vals.append(decode_5bit(r))
        post_vals.append(decode_5bit(g))
        sky_vals.append(decode_5bit(b))
    return {
        "path": path,
        "pre": median(pre_vals),
        "post": median(post_vals),
        "sky": median(sky_vals),
        "rgb_samples": rgbs,
    }


def spctl_decode(field5):
    """Decode the low 5 bits of SPCTL (SPTYPE [3:0] + SPCLMD [4])."""
    sptype = field5 & 0x0F
    spclmd = (field5 >> 4) & 1  # SPCLMD bit 5 is bit 5 of full register; in low-5 view it's bit 4
    return f"SPTYPE={sptype:X} SPCLMD={spclmd}"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    args = []
    for arg in sys.argv[1:]:
        if "*" in arg or "?" in arg:
            args.extend(sorted(glob.glob(arg)))
        else:
            args.append(arg)
    print(f"{'frame':40s}  {'pre':>4s} {'post':>4s} {'sky':>4s}  pre/post/sky decoded")
    print("-" * 110)
    any_active = False
    for path in args:
        r = sample_one(path)
        # Skip frames where back-color is the boot default (light blue 96/128/224
        # = jo_core_init's color) — those frames are pre-GHZ-active.
        pre, post, sky = r["pre"], r["post"], r["sky"]
        rgb0 = r["rgb_samples"][0]
        # Detect pre-GHZ frames: jo_core_init back-color RGB(96,128,224) ->
        # 5-bit: R=12 G=16 B=28
        if rgb0 == (96, 128, 224) or (pre == 12 and post == 16 and sky == 28):
            label = "[pre-GHZ jo_core_init default]"
            print(f"{os.path.basename(path):40s}  {pre:4d} {post:4d} {sky:4d}  {label}")
            continue
        any_active = True
        pre_dec  = spctl_decode(pre)
        post_dec = spctl_decode(post)
        sky_dec  = spctl_decode(sky)
        print(f"{os.path.basename(path):40s}  {pre:4d} {post:4d} {sky:4d}  pre[{pre_dec}] post[{post_dec}] sky[{sky_dec}]")

    if any_active:
        print()
        print("Note: bit 5 of full SPCTL (SPCLMD) maps to bit 4 of the low-5 view.")
        print("Expected sprite-friendly SPCTL low 5 bits: 0x03 (SPCLMD=0+SPTYPE=3)")
        print("or 0x13 (SPCLMD=1+SPTYPE=3 — jo's documented default 0x23 -> low 5 = 0x03 since SPCLMD is bit 5 of full reg).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
