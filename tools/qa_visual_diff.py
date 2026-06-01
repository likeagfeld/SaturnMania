#!/usr/bin/env python3
"""qa_visual_diff.py — SSIM-based visual fidelity gate.

Compares a Saturn capture against a PC Steam Mania reference image and
returns an SSIM score in [-1, 1]. The Saturn output is cropped to the
viewport, resampled to match the reference resolution, then compared
with a per-channel structural similarity metric.

Usage:
    python tools/qa_visual_diff.py <saturn_capture> <reference_image> \
        [--threshold 0.45] [--crop-top 30] [--crop-side 3]

Exit codes:
    0  - score >= threshold (PASS)
    1  - score < threshold  (FAIL)
    2  - error reading inputs

The default threshold (0.45) is intentionally lenient because:
  * Saturn renders at 320x224 RGB555; the PC reference is typically
    higher resolution and full RGB888.
  * Saturn uses a different style for some title elements (large
    drawn Sonic atlas vs. PC-only HD assets).
  * Background composition differs (Saturn shows the pre-composed
    TITLE.DAT bitmap; PC shows multi-layer parallax + INK_BLEND/ADD).
A score >= 0.45 indicates "recognisably the same title screen".
"""
import sys
import argparse
import numpy as np

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("PIL/Pillow not installed; cannot run qa_visual_diff\n")
    sys.exit(2)


def ssim_channel(a, b, k1=0.01, k2=0.03, dyn_range=255.0):
    """SSIM for a single 2D channel (no Gaussian window; uniform mean+std).

    This is the global-mean SSIM variant — adequate for whole-image
    structural comparison. Returns a scalar in [-1, 1].
    """
    a = a.astype(np.float64)
    b = b.astype(np.float64)
    mu_a = a.mean()
    mu_b = b.mean()
    sig_a = a.std()
    sig_b = b.std()
    cov   = ((a - mu_a) * (b - mu_b)).mean()
    c1 = (k1 * dyn_range) ** 2
    c2 = (k2 * dyn_range) ** 2
    num = (2 * mu_a * mu_b + c1) * (2 * cov + c2)
    den = (mu_a * mu_a + mu_b * mu_b + c1) * (sig_a * sig_a + sig_b * sig_b + c2)
    if den == 0:
        return 0.0
    return float(num / den)


def ssim_rgb(a_img, b_img):
    """SSIM over RGB channels averaged."""
    a = np.asarray(a_img.convert("RGB"))
    b = np.asarray(b_img.convert("RGB"))
    if a.shape != b.shape:
        b_resized = b_img.convert("RGB").resize(
            (a.shape[1], a.shape[0]), Image.BILINEAR
        )
        b = np.asarray(b_resized)
    score = 0.0
    for c in range(3):
        score += ssim_channel(a[..., c], b[..., c])
    return score / 3.0


def main():
    p = argparse.ArgumentParser()
    p.add_argument("saturn")
    p.add_argument("reference")
    p.add_argument("--threshold", type=float, default=0.45)
    p.add_argument("--crop-top", type=int, default=30,
                   help="Pixels to crop from top (Mednafen window chrome).")
    p.add_argument("--crop-side", type=int, default=3,
                   help="Pixels to crop from L/R (Mednafen window borders).")
    p.add_argument("--target-w", type=int, default=320)
    p.add_argument("--target-h", type=int, default=224)
    a = p.parse_args()

    try:
        sat = Image.open(a.saturn).convert("RGB")
        ref = Image.open(a.reference).convert("RGB")
    except Exception as e:
        sys.stderr.write(f"qa_visual_diff: cannot open inputs: {e}\n")
        sys.exit(2)

    # Crop Mednafen chrome from the Saturn capture.
    W, H = sat.size
    if a.crop_top >= H or 2 * a.crop_side >= W:
        sys.stderr.write("qa_visual_diff: crop exceeds image dimensions\n")
        sys.exit(2)
    sat = sat.crop((a.crop_side, a.crop_top, W - a.crop_side, H))

    # Resize both to a canonical comparison resolution. We pick the Saturn
    # native (320x224) as the target so reference detail is downsampled
    # rather than Saturn detail upsampled.
    sat = sat.resize((a.target_w, a.target_h), Image.BILINEAR)
    ref = ref.resize((a.target_w, a.target_h), Image.BILINEAR)

    score = ssim_rgb(sat, ref)
    print(f"qa_visual_diff: SSIM={score:.3f} (threshold={a.threshold:.3f})")
    if score < a.threshold:
        print(f"qa_visual_diff: FAIL score below threshold")
        sys.exit(1)
    print("qa_visual_diff: PASS")
    sys.exit(0)


if __name__ == "__main__":
    main()
