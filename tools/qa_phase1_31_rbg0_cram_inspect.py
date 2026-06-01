#!/usr/bin/env python3
"""qa_phase1_31_rbg0_cram_inspect.py

Decodes the dumped CRAM bin from mcs_extract --cram and prints critical
slot contents for the Phase 1.31 Fix #3 RBG0 green-flood diagnostic.

The dump byte order: mcs_extract _dump_region writes the raw Mednafen
serialised host-LE uint16 bytes. Since the SH-2 MOV.W of a value V
(big-endian) produces the same host-LE storage as Mednafen's internal
uint16=V representation, reading the bytes back as <H gives back the
Saturn-visible uint16 value the SH-2 wrote. CRAM RGB555 layout per
ST-058-R2 §11 (BGR555 — bit 14-10 = B, 9-5 = G, 4-0 = R; bit 15 ignored
in Mode 0).
"""
import struct
import sys
from pathlib import Path

CRAM_PATH = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("samples/qa_phase1_31_fix3_cram.bin")

data = CRAM_PATH.read_bytes()
slots = [struct.unpack("<H", data[i:i+2])[0] for i in range(0, len(data), 2)]


def rgb555(w):
    r5 = w & 0x1F
    g5 = (w >> 5) & 0x1F
    b5 = (w >> 10) & 0x1F
    return ((r5 << 3) | (r5 >> 2),
            (g5 << 3) | (g5 >> 2),
            (b5 << 3) | (b5 >> 2))


def flag(i, w):
    rgb = rgb555(w)
    s = ""
    if rgb == (0, 248, 0):
        s += " <-- NEON-GREEN FLOOD"
    if i == 0:
        s += " <jo slot 0 reserved>"
    if i == 256:
        s += " <slot between reserved + first user>"
    if i == 257:
        s += " <jo first user palette slot 0>"
    return rgb, s


print("--- Critical CRAM slots (RGB555 BGR layout per ST-058-R2 sec 11) ---")
check = [0, 1, 2, 3, 4, 5, 6, 7, 100, 130, 131, 132, 140, 141, 142, 143,
         255, 256, 257, 258, 259, 260, 270, 300, 400, 500, 511, 512, 513]
for i in check:
    if i < len(slots):
        w = slots[i]
        rgb, fl = flag(i, w)
        print(f"  CRAM[{i:4d}] = 0x{w:04x} -> RGB={rgb}{fl}")

print()
print("--- RBG0 actual-read region: CRAM[0..255] since CRAOFB.R0CAOS=0 ---")
neon_b0 = nonzero_b0 = 0
neon_b0_idx = []
for i in range(256):
    w = slots[i]
    if w != 0:
        nonzero_b0 += 1
    if rgb555(w) == (0, 248, 0):
        neon_b0 += 1
        neon_b0_idx.append(i)
print(f"  Bank-0 (0..255) nonzero slots: {nonzero_b0}/256")
print(f"  Bank-0 (0..255) (0,248,0) NEON-GREEN slots: {neon_b0}/256")
if neon_b0_idx:
    head = neon_b0_idx[:16]
    print(f"  First 16 neon-green slot indices in bank 0: {head}")

print()
print("--- jo first user-palette region CRAM[257..512] ---")
nz1 = neon1 = 0
sample = []
for i in range(257, 513):
    w = slots[i]
    if w != 0:
        nz1 += 1
    if rgb555(w) == (0, 248, 0):
        neon1 += 1
print(f"  CRAM[257..512] nonzero slots: {nz1}/256")
print(f"  CRAM[257..512] (0,248,0) NEON slots: {neon1}")

print()
print("--- Sample CRAM[1..20] (jo printf-font reserved palette) ---")
for i in range(1, 21):
    w = slots[i]
    rgb, _ = flag(i, w)
    print(f"  CRAM[{i:4d}] = 0x{w:04x} -> RGB={rgb}")

print()
print("--- Sample CRAM[257..276] (jo first user palette first 20) ---")
for i in range(257, 277):
    w = slots[i]
    rgb, _ = flag(i, w)
    print(f"  CRAM[{i:4d}] = 0x{w:04x} -> RGB={rgb}")
