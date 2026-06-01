#!/usr/bin/env python3
"""qa_phase1_31_fix4a_clip_gate.py - Phase 1.31 Fix #4a RED-firing gate.

User-mandated 2026-05-27:
    "the pivot of the spinning background should only take up about the
    bottom 2/3rds of the screen with top 1/3rd for oncoming sky cloud
    animation... the island should be stretched so that edges are
    outside visible view and it looks like the island is spinning like
    a record pivoted in the center of the screen with depth perception"

Decomp reference (tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:140):
    RSDK.SetClipBounds(0, 0, 168, ScreenInfo->size.x, SCREEN_YSIZE);
The decomp clips the island to Y=168..240 (bottom 72 lines = 30%).
The user's "bottom 2/3" is a looser, more screen-area-rich target;
we target Y=80..240 (bottom 160 lines = 66.7% = 2/3).

Saturn implementation: VDP2 W0 window (a normal rectangular window
per ST-058-R2 §8.1, p.180) with RBG0 routed through it via WCTLC
bits 1 (R0W0E enable) + 0 (R0W0A area select 0=inside).

Register address citations (ST-058-R2 §8.1 p.181 + §8.2 p.193):
  WPSX0   0x05F800C0  bit 9..1 store X (LSB dropped) — for 320 mode store X>>1
  WPSY0   0x05F800C2  bit 8..0 store Y verbatim
  WPEX0   0x05F800C4  bit 9..1 store X (LSB dropped)
  WPEY0   0x05F800C6  bit 8..0 store Y verbatim
  WCTLC   0x05F800D4  bit 1 = R0W0E, bit 0 = R0W0A

Per Table 8.1, X is W0SX9..W0SX1 — the LSB W0SX0 doesn't exist in
Normal graphics mode (the stored register value equals pixel_X<<1).
Y stores V8..V0 verbatim. Pixel-decoded targets:
  WPSX0 -> 0      WPEX0 -> 319
  WPSY0 -> 80     WPEY0 -> 239

WCTLC.R0W0A semantics — per ST-058-R2 §8.1 p.195 the W0 area is the
"Transparent Process Window" (i.e. the W0 region is TRANSPARENT and
the rest is rendered). So:
  R0W0A=0 -> inside W0 transparent -> RBG0 renders OUTSIDE W0
  R0W0A=1 -> outside W0 transparent -> RBG0 renders INSIDE W0
To restrict the rotating island to the bottom-2/3 area, we want
RBG0 rendered INSIDE W0 = R0W0A = 1.
SGL's win0_IN constant (SL_DEF.H:678 = 0x03) maps to WCTLC bits 1..0
= 0b11 (R0W0E=1 + R0W0A=1) which is the correct combination.

Predicates:
  P1 pixel WPSY0 == 80           (top of clip = 80)
  P2 pixel WPEY0 == 239          (bottom of clip = 239)
  P3 pixel WPSX0 == 0            (left of clip = 0)
  P4 pixel WPEX0 == 319          (right of clip = 319)
  P5 WCTLC.R0W0E == 1            (W0 enable for RBG0 set)
  P6 WCTLC.R0W0A == 1            (RBG0 renders inside W0 = bottom 2/3)

Usage:
    py -3 tools/qa_phase1_31_fix4a_clip_gate.py [path/to.mcs]

Exit codes:
    0 = GREEN (all P1-P6 pass)
    1 = RED   (any predicate fails or input missing)
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MCS_DEFAULT = ROOT / "samples" / "qa_phase1_31_fix4a_post.mcs"


def peek16(mcs_path: Path, addr: int) -> int:
    out = subprocess.check_output(
        ["py", "-3", str(ROOT / "tools" / "mcs_extract.py"),
         str(mcs_path), "--peek16", hex(addr)],
        text=True,
    ).strip()
    return int(out.split("= ")[-1], 16)


def main():
    mcs_path = Path(sys.argv[1]) if len(sys.argv) > 1 else MCS_DEFAULT
    if not mcs_path.exists():
        print(f"RED: mcs file not found: {mcs_path}")
        sys.exit(1)

    print(f"qa_phase1_31_fix4a_clip_gate: inspecting {mcs_path}")
    print()

    wpsx0 = peek16(mcs_path, 0x05F800C0)
    wpsy0 = peek16(mcs_path, 0x05F800C2)
    wpex0 = peek16(mcs_path, 0x05F800C4)
    wpey0 = peek16(mcs_path, 0x05F800C6)
    wctlc = peek16(mcs_path, 0x05F800D4)

    # Decode register values to screen pixel coordinates.
    # Per ST-058-R2 §8.1 Table 8.1: in Normal graphics mode the
    # X register field maps register-bit-9..1 to H counter bits 8..0,
    # i.e. the LSB of the register is invalid/dropped. Equivalently:
    # the stored register value equals (pixel_X << 1) for the
    # H-counter range 0..511 in 320-px mode. To recover pixel X:
    #   pixel_X = (register & 0x3FE) >> 1
    # Per Table 8.2: Y register stores V counter bits 8..0 verbatim:
    #   pixel_Y = register & 0x1FF
    wpsx0_pixel = (wpsx0 & 0x03FE) >> 1
    wpex0_pixel = (wpex0 & 0x03FE) >> 1
    wpsy0_pixel = wpsy0 & 0x01FF
    wpey0_pixel = wpey0 & 0x01FF
    r0w0e = (wctlc >> 1) & 1
    r0w0a = wctlc & 1

    print("Window position register snapshot:")
    print(f"  WPSX0 @ 0x05F800C0 = 0x{wpsx0:04x}  -> pixel X = {wpsx0_pixel}")
    print(f"  WPSY0 @ 0x05F800C2 = 0x{wpsy0:04x}  -> pixel Y = {wpsy0_pixel}")
    print(f"  WPEX0 @ 0x05F800C4 = 0x{wpex0:04x}  -> pixel X = {wpex0_pixel}")
    print(f"  WPEY0 @ 0x05F800C6 = 0x{wpey0:04x}  -> pixel Y = {wpey0_pixel}")
    print(f"  WCTLC @ 0x05F800D4 = 0x{wctlc:04x}  R0W0E={r0w0e} R0W0A={r0w0a}")
    print()

    # Target clip rectangle: pixel X = [0..319] (full width), pixel Y =
    # [80..239] (bottom 2/3 of 240-line screen).
    # Register storage (per Table 8.1 LSB-invalid for Normal graphics):
    #   WPSX0 = 0 << 1 = 0
    #   WPEX0 = 319 << 1 = 638 = 0x027E
    #   WPSY0 = 80
    #   WPEY0 = 239
    expected = {
        "WPSY0": 80,
        "WPEY0": 239,
        "WPSX0": 0,
        "WPEX0": 319,
    }

    failures = []

    if wpsy0_pixel == expected["WPSY0"]:
        print(f"  P1 GREEN: pixel WPSY0 = {wpsy0_pixel} (top of clip = bottom 2/3 boundary)")
    else:
        failures.append(f"P1 RED: pixel WPSY0 = {wpsy0_pixel}, expected {expected['WPSY0']}")

    if wpey0_pixel == expected["WPEY0"]:
        print(f"  P2 GREEN: pixel WPEY0 = {wpey0_pixel} (bottom of clip = screen bottom)")
    else:
        failures.append(f"P2 RED: pixel WPEY0 = {wpey0_pixel}, expected {expected['WPEY0']}")

    if wpsx0_pixel == expected["WPSX0"]:
        print(f"  P3 GREEN: pixel WPSX0 = {wpsx0_pixel} (left of clip = screen left)")
    else:
        failures.append(f"P3 RED: pixel WPSX0 = {wpsx0_pixel}, expected {expected['WPSX0']}")

    if wpex0_pixel == expected["WPEX0"]:
        print(f"  P4 GREEN: pixel WPEX0 = {wpex0_pixel} (right of clip = screen right)")
    else:
        failures.append(f"P4 RED: pixel WPEX0 = {wpex0_pixel}, expected {expected['WPEX0']}")

    if r0w0e == 1:
        print(f"  P5 GREEN: WCTLC.R0W0E = 1 (W0 window enabled for RBG0)")
    else:
        failures.append(f"P5 RED: WCTLC.R0W0E = {r0w0e}, expected 1 (W0 not enabled for RBG0)")

    if r0w0a == 1:
        print(f"  P6 GREEN: WCTLC.R0W0A = 1 (RBG0 renders INSIDE W0 area; outside is transparent per ST-058-R2 §8.1 p.195)")
    else:
        failures.append(f"P6 RED: WCTLC.R0W0A = {r0w0a}, expected 1 (RBG0 not rendering inside-W0 per W0A 'transparent process window' semantics)")

    print()
    if failures:
        print("=" * 64)
        print("RED: gate failed")
        print("=" * 64)
        for f in failures:
            print(f)
        print()
        print("Fix: ensure src/main.c mania_title_3d_backdrop_draw() writes:")
        print("  *(volatile Uint16*)0x25F800C0 = 0          # WPSX0 = 0   (pixel X=0)")
        print("  *(volatile Uint16*)0x25F800C2 = 80         # WPSY0 = 80  (pixel Y=80)")
        print("  *(volatile Uint16*)0x25F800C4 = 638        # WPEX0 = 638 (= 319<<1, pixel X=319)")
        print("  *(volatile Uint16*)0x25F800C6 = 239        # WPEY0 = 239 (pixel Y=239)")
        print("  WCTLC bits 1..0 := 0b11 (R0W0E=1, R0W0A=1)  [renders INSIDE W0]")
        print("Equivalent SGL helpers:")
        print("  slScrWindow0(0, 80, 319, 239); slScrWindowMode(scnRBG0, win0_IN);")
        print("(these survive slSynch; direct register writes do NOT)")
        print("Per ST-058-R2 §8.1 p.195: R0W0A is a TRANSPARENT-process bit.")
        sys.exit(1)
    else:
        print("GREEN: all predicates pass")
        sys.exit(0)


if __name__ == "__main__":
    main()
