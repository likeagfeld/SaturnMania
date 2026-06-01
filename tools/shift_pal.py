#!/usr/bin/env python3
"""shift_pal.py - shift a 256-entry RGB555 .PAL file UP by 1 so it works with
jo_create_palette_from() without the off-by-one CRAM corruption.

WHY: jo's vdp2_malloc.c allocates user palettes starting at CRAM[257] (= 256+1;
"the first palette is reserved" + 1 entry per bank for jo's printf text color),
but VDP2 256-color mode reads bank N from CRAM[N*256..N*256+255]. Therefore
cell-pixel value V reads CRAM[bank+V] = jo's PAL[V-1], i.e. the palette is
read one slot LOW. This off-by-one turns the white frond-highlight pixels in
GHZ palms into red/yellow speckles on screen, and corrupts every 256-color
NBG layer loaded via jo (FG, sky, anything else).

The fix: shift the PAL file UP by 1 entry on disk:
  new[i] = old[i+1]  for i = 0..254;   new[255] = 0
Then cell-pixel V reads jo's new PAL[V-1] = old[V] -- correct.
The lost old[0] is the GIF-transparent placeholder (never rendered), so
nothing visual is lost.

Usage:  python tools/shift_pal.py <in.pal> [<out.pal>]  (in-place if out omitted)
"""
import struct, sys, os

def shift_pal(pal_bytes):
    assert len(pal_bytes) == 512, f"expected 256 u16 (512B) PAL, got {len(pal_bytes)}"
    old = list(struct.unpack(">256H", pal_bytes))
    new = old[1:] + [0]                    # shift up; tail becomes 0 (unused)
    return struct.pack(">256H", *new)

def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(2)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src
    with open(src, "rb") as f:
        data = f.read()
    out = shift_pal(data)
    with open(dst, "wb") as f:
        f.write(out)
    print(f"shifted PAL: {src} -> {dst} ({len(out)}B)")

if __name__ == "__main__":
    main()
