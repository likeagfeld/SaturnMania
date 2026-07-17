#!/usr/bin/env python3
"""qa_rot_draw.py -- RED/GREEN gate for the rotated-sprite draw path (Fix 1,
user-symptom-map-v2-2026-07-17: "Sonic invisible on slopes/ramps").

ROOT (code-confirmed): p6_io_main.cpp DrawSprite dispatch handles only FX_NONE
and FX_FLIP; a resolved rotation != 0 (ROTSTYLE_FULL on slopes) falls to the
`default:` arm = NO DRAW (the in-code FIXME admits it).

FIX CONTRACT (offline, source-only -- the primary verifies live):
  R1  p6_io_main.cpp: the DrawSprite `default:` arm calls p6_vdp1_blit_rot
      (under P6_DIRECT_VDP1) instead of dropping the draw.
  R2  p6_vdp1.c: p6_dl_sprite_rot exists and emits a VDP1 DISTORTED SPRITE
      command -- CMDCTRL Comm=0010B (0x0002), vertices A..D at p[6..13]
      (ST-013-R3 sec 7.6, pp.124-125: A=upper-left, B=upper-right,
      C=lower-right, D=lower-left).
  R3  p6_vdp1.c: p6_vdp1_blit_rot exists (the slot-resolving wrapper the
      dispatch calls; same p6_slot_for plumbing as p6_vdp1_blit_flipped).
  M1  Vertex-math reference model (this file) self-checks the intended
      integer math on 3 known angles (0x00, 0x40=45deg, 0x80=90deg) against
      a float reference, using the decomp trig-table formulas
      (gen_trig_tables.py host_reference / RSDK Math.cpp:144-152):
        sin512[i] = trunc(sin((i/256)*RSDK_PI)*512), cardinal overrides.
      Rotation forward transform (derived from the decomp inverse map,
      Drawing.cpp:3541-3588 -- angle = 0x200 - rotation, so forward is):
        dx' = (dx*cos - dy*sin) >> 9
        dy' = (dx*sin + dy*cos) >> 9
      FLIP_X mirrors the pivot-relative X extents BEFORE rotation
      (Drawing.cpp:3575-3583: A-corner x offset = -(pivotX), extents
      [-pivotX-width, -pivotX]).

Exit 0 = all GREEN. Exit 1 = RED (lists which check failed).
Live criteria for the primary (NOT checked here): rotated Sonic visible on
the x~700 GHZ slope (screenshot) + nonzero VDP1 distorted cmd for the player
while entity->rotation != 0.
"""
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
P6 = ROOT / "tools" / "_portspike" / "_p6"
IO = (P6 / "p6_io_main.cpp").read_text(errors="replace")
V1 = (P6 / "p6_vdp1.c").read_text(errors="replace")

RSDK_PI = 3.1415927410125732  # decomp RSDK_PI (Math.hpp)

fails = []


def check(name, ok, detail=""):
    print(f"  [{'GREEN' if ok else 'RED  '}] {name}" + (f" -- {detail}" if detail else ""))
    if not ok:
        fails.append(name)


# ---- R1: dispatch wiring -----------------------------------------------------
# The default: arm of the DrawSprite drawFX switch must call p6_vdp1_blit_rot.
m = re.search(r"case FX_FLIP:.*?default:(.*?)break;", IO, re.S)
arm = m.group(1) if m else ""
check("R1 dispatch default: arm calls p6_vdp1_blit_rot",
      "p6_vdp1_blit_rot" in arm,
      "default: arm is a drop (FIXME)" if "p6_vdp1_blit_rot" not in arm else "wired")

# ---- R2: distorted-sprite emit ----------------------------------------------
m = re.search(r"static void p6_dl_sprite_rot\b(.{0,4000}?)\n}", V1, re.S)
body = m.group(1) if m else ""
check("R2a p6_dl_sprite_rot exists in p6_vdp1.c", bool(m))
check("R2b emits Comm=0x0002 (distorted sprite, ST-013-R3 sec 7.6)",
      "0x0002" in body)
check("R2c rotates via (dx*cs - dy*sn) >> 9 fixed-point (Sin512/Cos512 <<9)",
      re.search(r">>\s*9", body) is not None)

# ---- R3: slot-resolving wrapper ---------------------------------------------
check("R3 p6_vdp1_blit_rot exists (p6_slot_for plumbing)",
      re.search(r"void p6_vdp1_blit_rot\b.*?p6_slot_for", V1, re.S) is not None)

# ---- M1: vertex-math reference model ----------------------------------------
def trig512():
    f = lambda c: math.floor(c) if c >= 0 else -math.floor(-c)
    sin = [f(math.sin((i / 256.0) * RSDK_PI) * 512.0) for i in range(0x200)]
    cos = [f(math.cos((i / 256.0) * RSDK_PI) * 512.0) for i in range(0x200)]
    for idx, v in ((0x00, 0x200), (0x80, 0), (0x100, -0x200), (0x180, 0)):
        cos[idx] = v
    for idx, v in ((0x00, 0), (0x80, 0x200), (0x100, 0), (0x180, -0x200)):
        sin[idx] = v
    return sin, cos


SIN, COS = trig512()


def sar9(v):
    """SH-2 arithmetic >>9 on a C int (round toward -inf), as the C code does."""
    return v >> 9


def quad(x, y, pw, ph, pivotX, pivotY, rot, flipX):
    """The intended C math: pivot-relative corners rotated by the RSDK angle."""
    sn, cs = SIN[rot & 0x1FF], COS[rot & 0x1FF]
    dxl = (-pivotX - pw) if flipX else pivotX
    dxr = dxl + pw
    dyt, dyb = pivotY, pivotY + ph
    def rx(dx, dy): return x + sar9(dx * cs - dy * sn)
    def ry(dx, dy): return y + sar9(dx * sn + dy * cs)
    return [(rx(dxl, dyt), ry(dxl, dyt)), (rx(dxr, dyt), ry(dxr, dyt)),
            (rx(dxr, dyb), ry(dxr, dyb)), (rx(dxl, dyb), ry(dxl, dyb))]


# Sample: a 48x48 player-ish frame, pivot (-24,-24), pos (100,100).
X, Y, PW, PH, PX, PY = 100, 100, 48, 48, -24, -24

# angle 0: must equal the axis-aligned FX_NONE rect (pos+pivot .. +pw/ph).
q0 = quad(X, Y, PW, PH, PX, PY, 0, 0)
check("M1a rot=0 == unrotated rect",
      q0 == [(76, 76), (124, 76), (124, 124), (76, 124)], str(q0))

# angle 0x80 (90 deg CW, sin=512 cos=0): exact (dx,dy)->(-dy,dx).
q90 = quad(X, Y, PW, PH, PX, PY, 0x80, 0)
exp90 = [(X - dy, Y + dx) for (dx, dy) in
         [(PX, PY), (PX + PW, PY), (PX + PW, PY + PH), (PX, PY + PH)]]
check("M1b rot=0x80 (90deg) exact", q90 == exp90, f"{q90} vs {exp90}")

# angle 0x40 (45 deg): within 1.5 px of the float rotation.
q45 = quad(X, Y, PW, PH, PX, PY, 0x40, 0)
ok45 = True
for (gx, gy), (dx, dy) in zip(q45, [(PX, PY), (PX + PW, PY),
                                    (PX + PW, PY + PH), (PX, PY + PH)]):
    a = (0x40 / 256.0) * RSDK_PI
    fx = X + dx * math.cos(a) - dy * math.sin(a)
    fy = Y + dx * math.sin(a) + dy * math.cos(a)
    if abs(gx - fx) > 1.5 or abs(gy - fy) > 1.5:
        ok45 = False
check("M1c rot=0x40 (45deg) within 1.5px of float reference", ok45, str(q45))

# FLIP_X at rot=0: content right edge at x - pivotX (RSDK FLIP_X world formula
# x - width - pivotX for the left edge; pw==width here so exact).
qf = quad(X, Y, PW, PH, PX, PY, 0, 1)
check("M1d FLIP_X mirrors X extents pre-rotation",
      qf[0] == (X - PX - PW, Y + PY) and qf[1] == (X - PX, Y + PY), str(qf))

print()
if fails:
    print(f"RED: {len(fails)} failing: {', '.join(fails)}")
    sys.exit(1)
print("GREEN: rotated-draw contract satisfied (offline). "
      "Primary verifies live: rotated Sonic on the x~700 slope screenshot.")
sys.exit(0)
