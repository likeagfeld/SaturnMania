#!/usr/bin/env python3
# transient: sum ELF32 .text/.rodata/.data (overlay window cost) + .bss per .o
import sys, struct, os
def secsizes(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF":
        return None
    en = ">" if d[5] == 2 else "<"
    e_shoff   = struct.unpack(en + "I", d[0x20:0x24])[0]
    e_shentsz = struct.unpack(en + "H", d[0x2e:0x30])[0]
    e_shnum   = struct.unpack(en + "H", d[0x30:0x32])[0]
    e_shstrnd = struct.unpack(en + "H", d[0x32:0x34])[0]
    hdrs = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsz
        nm, ty, fl, ad, of, sz = struct.unpack(en + "IIIIII", d[o:o + 24])
        hdrs.append((nm, ty, sz, of))
    stroff = hdrs[e_shstrnd][3]
    def name(nm):
        e = d.index(b"\0", stroff + nm)
        return d[stroff + nm:e].decode("ascii", "replace")
    prog = bss = 0
    for nm, ty, sz, of in hdrs:
        n = name(nm)
        if ty == 8:                      # SHT_NOBITS = .bss/COMMON
            bss += sz
        elif n.startswith((".text", ".rodata", ".data")):
            prog += sz                   # contributes to the overlay binary
    return prog, bss

P6 = os.path.dirname(os.path.abspath(__file__))
total_prog = total_bss = 0
for o in sys.argv[1:]:
    p = os.path.join(P6, o)
    r = secsizes(p) if os.path.exists(p) else None
    if r is None:
        print("  %-22s MISSING/notELF" % o); continue
    prog, bss = r
    total_prog += prog; total_bss += bss
    print("  %-22s window(text+rodata+data)=%6d  bss=%6d" % (o, prog, bss))
print("  " + "-" * 58)
print("  TOTAL window cost = %d B (%.1f KB) | bss = %d B" % (total_prog, total_prog / 1024.0, total_bss))
