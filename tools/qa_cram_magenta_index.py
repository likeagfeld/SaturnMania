#!/usr/bin/env python3
"""qa_cram_magenta_index.py -- RED gate for the D9 residual pink/magenta CRAM flash.

Parity-audit workflow INV[0] (2026-07-21): the D9 detector counts EVERY CRAM entry
with r>=24 & b>=24 & g<=8 as "magenta", including block-local index 0 -- which RSDK
Mania palettes set to the magenta TRANSPARENT KEY and which the Saturn keys transparent
(pixel value 0 never rasterized). So most residual magenta is INVISIBLE and a detector
over-count. This gate splits magenta into KEY (i%256==0, invisible) vs NON-KEY
(i%256!=0, a DRAWN slot referencing magenta = a REAL visible bug).

Input: a 4096-byte CRAM dump (2048 BGR555 u16, big-endian) from
  python tools/mcs_extract.py <state>.mcs --cram _cram.bin
Exit 0 (GREEN) = all residual magenta is index-0 transparent keys (invisible).
Exit 1 (RED)   = NON-KEY magenta exists (prints exact (block,localidx)) -> real bug.
"""
import sys, struct

def main(path):
    raw = open(path, "rb").read()
    if len(raw) < 4096:
        sys.stderr.write("cram dump too small (%d B, need 4096)\n" % len(raw)); return 2
    words = struct.unpack(">2048H", raw[:4096])   # big-endian u16, matches mcs_extract --cram
    key, nonkey = [], []
    for i, c in enumerate(words):
        r = c & 0x1F; g = (c >> 5) & 0x1F; b = (c >> 10) & 0x1F
        if r >= 24 and b >= 24 and g <= 8:
            (key if i % 256 == 0 else nonkey).append(i)
    print("magenta=%d  key(idx0,invisible)=%d  NONKEY(drawn)=%d"
          % (len(key) + len(nonkey), len(key), len(nonkey)))
    if nonkey:
        print("RED -- NON-KEY magenta at (block,localidx):",
              [(i >> 8, i & 0xFF) for i in nonkey[:40]])
        print("  a drawn tile/sprite references a magenta palette slot -> real visible bug.")
        return 1
    print("GREEN -- all residual magenta is index-0 transparent keys (invisible; D9 over-count).")
    return 0

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.stderr.write("usage: qa_cram_magenta_index.py <cram_dump.bin>\n"); sys.exit(2)
    sys.exit(main(sys.argv[1]))
