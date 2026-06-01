#!/usr/bin/env python3
"""Phase 1.14 B1/B2/B3 split — back-color sampler with B-channel decoding.

Per docs/COMPREHENSIVE_PLAN.md Sec 11.20, fg_vblank now encodes three
sub-cause flags into the B channel on top of the Phase 1.13 R/G encoding:

  bit15 = 1 (opaque)
  bits14..10 (B field, 5 bits):
    B[4]    = 1 if __callbacks.first == NULL  (B1 head-pointer corruption)
    B[3]    = 1 if first->data.ptr == NULL    (B2 function-pointer clobber)
    B[2..0] = jo_core_run loop_count low 3 bits (proves loop iterates;
              stuck at 0 across captures -> B3 short-circuits before
              the increment)
  bits9..5 (G field) = count & 0x0F (Phase 1.13)
  bits4..0 (R field) = node-bit at R[4] (Phase 1.13)

Border-zone (cols 5/10/906 at row 374) is proven reliable per Phase 1.12.
"""
from PIL import Image
import os, sys

PNG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "..", "qa_probe_phase114.png")
img = Image.open(PNG).convert("RGB")
W, H = img.size
print(f"image: {PNG} -> {W}x{H}")

border_points = [
    ("BORDER: col 5,   row 374",  5,     374),
    ("BORDER: col 10,  row 374",  10,    374),
    ("BORDER: col 906, row 374",  906,   374),
]


def decode_5bit(channel_8bit):
    """8-bit channel -> 5-bit field value 0..31."""
    return max(0, min(31, round(channel_8bit * 31 / 255)))


def decode(r, g, b):
    r_f = decode_5bit(r)
    g_f = decode_5bit(g)
    b_f = decode_5bit(b)
    count  = g_f & 0x0F
    node   = "set" if (r_f & 0x10) else "clear"
    first_null    = bool(b_f & 0x10)
    dataptr_null  = bool(b_f & 0x08)
    loop_lo3      = b_f & 0x07
    return {
        "R_field": r_f, "G_field": g_f, "B_field": b_f,
        "count": count, "node": node,
        "first_null": first_null, "dataptr_null": dataptr_null,
        "loop_lo3": loop_lo3,
    }


print()
print(f"{'sample':40s}  {'RGB':>16s}  decoded")
print("-" * 110)
for label, x, y in border_points:
    x = max(0, min(W - 1, x))
    y = max(0, min(H - 1, y))
    r, g, b = img.getpixel((x, y))
    d = decode(r, g, b)
    print(f"{label:40s}  ({r:3d},{g:3d},{b:3d})  R={d['R_field']:2d} G={d['G_field']:2d} B={d['B_field']:2d}  "
          f"count={d['count']} node={d['node']} first_null={d['first_null']} dataptr_null={d['dataptr_null']} loop_lo3={d['loop_lo3']}")

# Consensus
print()
print("Border-zone consensus:")
samples = []
for label, x, y in border_points:
    r, g, b = img.getpixel((x, y))
    samples.append(decode(r, g, b))

first_nulls = set(s["first_null"] for s in samples)
dataptr_nulls = set(s["dataptr_null"] for s in samples)
loops = [s["loop_lo3"] for s in samples]

# Verdict
print()
if len(first_nulls) == 1 and True in first_nulls:
    print("  OUTCOME B1: __callbacks.first == NULL at vblank time.")
    print("              Head pointer scribbled to NULL post-registration.")
    print("              Phase 1.15: audit memory ordering / heap corruption between")
    print("                          jo_core_add_callback return and steady-state.")
elif len(dataptr_nulls) == 1 and True in dataptr_nulls:
    print("  OUTCOME B2a: first->data.ptr == NULL (function pointer clobbered to NULL).")
    print("               Phase 1.15: audit what overwrites node->data.ptr between")
    print("                           registration and steady-state.")
elif all(L == 0 for L in loops):
    print("  OUTCOME B3: jo_core_run main loop never increments __jo_phase114_loop_count.")
    print("              Loop short-circuits BEFORE the increment statement, or")
    print("              jo_core_run never called.")
    print("              Phase 1.15: audit jo_core_run path from jo_main's invocation.")
else:
    print("  OUTCOME (NONE OF B1/B2/B3 cleanly): main loop iterates, list intact,")
    print("                                       data_ptr non-NULL.")
    print("              Bug must be inside jo_list_foreach macro or __jo_call_event.")
    print("              Phase 1.15: encode (data.ptr ^ &mania_tick) >> 16 to verify")
    print("                          the ptr value actually points at mania_tick.")
