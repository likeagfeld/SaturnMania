#!/usr/bin/env python3
"""Phase 1.13 __callbacks.count border-zone back-color sampler.

Per docs/COMPREHENSIVE_PLAN.md Sec 11.19 fg_vblank encodes the
captured `__callbacks.count` AND captured `jo_core_add_callback`
return value into the back-color every frame:

  bit15 = 1 (opaque)
  bits9-5  (G channel)  = count & 0x0F
  bit4     (R LSB-ish)  = 1 if returned node ptr != 0 else 0

Border-zone (cols 5/10/906 at row 374) is proven reliable per
Phase 1.12 (3/3 red samples landed on the static 0x801F). This turn
replaces that with a dynamic encoding.

Captured RGB classifications:
  count=0, node=0   -> 0x8000 -> R=0   G=0   B=0   pure BLACK
  count=1, node=0   -> 0x8020 -> R=0   G=~8  B=0   very-dark green
  count=1, node!=0  -> 0x8030 -> R=~8  G=~8  B=0   dark-olive
  count=2, node!=0  -> 0x8050 -> R=~8  G=~16 B=0
  count=N, node!=0  -> green ramps up

If the probe never ran (jo_main never reached the call site), the
static `s_callbacks_count_at_register=-1` would encode as
count&0x0F=15, G=31 channel -> bright green (~248) + R.
"""
from PIL import Image
import os, sys

PNG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "..", "qa_probe_phase113.png")
img = Image.open(PNG).convert("RGB")
W, H = img.size
print(f"image: {PNG} -> {W}x{H}")

# Phase 1.12 proved cols 5, 10, 906 at row 374 reliably honor live
# back-color writes (3/3 red samples on the static 0x801F).
border_points = [
    ("BORDER: col 5,   row 374",  5,     374),
    ("BORDER: col 10,  row 374",  10,    374),
    ("BORDER: col 906, row 374",  906,   374),
]
active_points = [
    ("ACTIVE: col 20,  row 400",  20,    400),
    ("ACTIVE: col 450, row 40 ",  450,   40 ),
    ("ACTIVE: col 882, row 200",  W-30,  200),
]
control_points = [
    ("CONTROL: col 0,   row 374 (Mednafen chrome)", 0, 374),
    ("CONTROL: col 911, row 374 (Mednafen chrome)", W-1, 374),
    ("CONTROL: col 450, row 374 (NBG2 art)",         450, 374),
]


def classify_count(r, g, b):
    """Decode the count from the GREEN channel.
    BGR1555 G field is 5 bits (0..31); captured 8-bit G ~= G*8 + dither.
    count = G_captured // 8  (best estimate, rounded)."""
    # Detect "almost pure black" -> count=0 case
    if r < 20 and g < 20 and b < 20:
        return ("count=0, node=0", 0, 0)
    # Detect node-bit on (R LSB present, G low nibble holding count).
    # R&0x1F=0x10 -> R_captured ~= 16*8 = 128? No -- R channel is 5 bits
    # so bit4 set means R_captured ~ (0x10 / 0x1F) * 255 = 132.
    # But we wrote bit4=1, R[4]=1 only -> raw R[4:0] = 0b10000 = 16
    # -> R_captured = 16 * (255/31) ~ 131. Close enough.
    # Practical: R > 100 with G > 0 -> node bit set.
    g_field = max(0, min(31, round(g * 31 / 255)))
    r_field = max(0, min(31, round(r * 31 / 255)))
    count = g_field & 0x0F
    node_bit = "set" if r_field & 0x10 else "clear"
    return (f"count={count}, node-bit={node_bit}", count, r_field & 0x10)


print()
print(f"{'sample':50s}  {'RGB':>16s}  decoded")
print("-" * 110)
for label, x, y in border_points + active_points + control_points:
    x = max(0, min(W - 1, x))
    y = max(0, min(H - 1, y))
    r, g, b = img.getpixel((x, y))
    decoded, _, _ = classify_count(r, g, b)
    print(f"{label:50s}  ({r:3d},{g:3d},{b:3d})       {decoded}")

# Consensus on border zone
print()
print("Border-zone consensus (Phase 1.12 proved reliable):")
counts = []
node_bits = []
for label, x, y in border_points:
    r, g, b = img.getpixel((x, y))
    _, c, n = classify_count(r, g, b)
    counts.append(c)
    node_bits.append(n)
print(f"  counts seen:   {counts}")
print(f"  node-bits set: {node_bits}")
if all(c == counts[0] for c in counts):
    print(f"  CONSENSUS COUNT = {counts[0]}")
    if counts[0] == 0:
        print("  OUTCOME A: jo_core_add_callback failed to append (Cause A).")
        print("             jo_list_add_ptr returned JO_NULL -- likely jo heap OOM.")
        print("             Phase 1.14: audit heap pressure at jo_main entry, expand pool")
        print("                         OR move callback registration earlier.")
    elif counts[0] == 1:
        print("  OUTCOME B: List populated with 1 node (Cause B).")
        print("             jo_list_foreach or __jo_call_event is the bug.")
        print("             Phase 1.14: audit __callbacks.first head pointer,")
        print("                         and compare first->data.ptr to &mania_tick.")
    elif counts[0] >= 2:
        print(f"  OUTCOME C: {counts[0]} registrations -- audit for duplicates.")
    elif counts[0] == 15:
        print("  OUTCOME D: count==-1 raw -> probe never ran. Check jo_main reached the call site.")
else:
    print("  INCONSISTENT -- border zone samples disagree; investigate per-scanline VRAM access.")
