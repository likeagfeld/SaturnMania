#!/usr/bin/env python3
# =============================================================================
# measure_text.py -- P6.7d (Task #210): REAL SH-2 .text census of the VERBATIM
# decomp object set. Every number comes from compiled-object section headers
# (sh-none-elf-gcc-8.2.0 -m2 -O2 -ffunction-sections), not a LOC ratio.
# Feeds the per-zone overlay-window sizing + the collision-residency brief.
#
# Usage: python tools/_portspike/_p67d_sizing/measure_text.py
# =============================================================================
import glob
import os
import struct
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
OBJ = os.path.join(HERE, "obj")


def elf_sections(path):
    """Yield (name, size, flags, type) for each section of a 32-bit BE ELF."""
    with open(path, "rb") as f:
        e = f.read()
    if e[:4] != b"\x7fELF":
        return
    is_be = e[5] == 2
    fmt = ">" if is_be else "<"
    shoff = struct.unpack_from(fmt + "I", e, 0x20)[0]
    shentsize = struct.unpack_from(fmt + "H", e, 0x2E)[0]
    shnum = struct.unpack_from(fmt + "H", e, 0x30)[0]
    shstrndx = struct.unpack_from(fmt + "H", e, 0x32)[0]
    strtab_off = struct.unpack_from(fmt + "I", e, shoff + shstrndx * shentsize + 0x10)[0]
    for i in range(shnum):
        base = shoff + i * shentsize
        name_off, sh_type, flags, _addr, _off, size = struct.unpack_from(
            fmt + "IIIIII", e, base)
        end = e.index(b"\x00", strtab_off + name_off)
        name = e[strtab_off + name_off:end].decode("latin1")
        yield name, size, flags, sh_type


def main():
    cats = defaultdict(lambda: [0, 0, 0, 0])  # text, rodata, data, bss
    files = defaultdict(int)
    for p in sorted(glob.glob(os.path.join(OBJ, "SonicMania_Objects_*.o"))):
        base = os.path.basename(p)[len("SonicMania_Objects_"):-2]
        cat = base.split("_")[0]
        files[cat] += 1
        for name, size, flags, sh_type in elf_sections(p):
            if name.startswith(".text"):
                cats[cat][0] += size
            elif name.startswith(".rodata"):
                cats[cat][1] += size
            elif name.startswith(".data"):
                cats[cat][2] += size
            elif name.startswith(".bss") or name == "COMMON":
                cats[cat][3] += size
    print("%-10s %5s %9s %9s %8s %8s %10s" %
          ("category", "objs", ".text", ".rodata", ".data", ".bss", "text+ro"))
    tot = [0, 0, 0, 0]
    for cat in sorted(cats, key=lambda c: -(cats[c][0] + cats[c][1])):
        t, r, d, b = cats[cat]
        for i, v in enumerate((t, r, d, b)):
            tot[i] += v
        print("%-10s %5d %9d %9d %8d %8d %10d" % (cat, files[cat], t, r, d, b, t + r))
    print("%-10s %5d %9d %9d %8d %8d %10d" %
          ("TOTAL", sum(files.values()), tot[0], tot[1], tot[2], tot[3],
           tot[0] + tot[1]))
    res = sum(cats[c][0] + cats[c][1] for c in ("Global", "Common", "Helpers") if c in cats)
    zones = {c: cats[c][0] + cats[c][1] for c in cats
             if c not in ("Global", "Common", "Helpers")}
    if zones:
        big = max(zones, key=zones.get)
        print("-" * 64)
        print("resident set (Global+Common+Helpers) text+ro : %d B" % res)
        print("largest zone overlay (%s) text+ro            : %d B" % (big, zones[big]))


if __name__ == "__main__":
    main()
