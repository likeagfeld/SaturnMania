#!/usr/bin/env python3
# Throwaway: list ELF32-BE (SH-2) symbol table of an .o/.a-member. Used to
# assess "boot via SGL SYS_INIT.O" feasibility for P6.2 (what it defines vs
# what it leaves undefined). Cleaned at exit.
import struct, sys

def u16(b, o): return struct.unpack(">H", b[o:o+2])[0]
def u32(b, o): return struct.unpack(">I", b[o:o+4])[0]

def parse(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"\x7fELF":
        print("%s: not ELF (first4=%r)" % (path, d[:4])); return
    ei_class = d[4]; ei_data = d[5]
    print("%s: ELF%s %s" % (path, "32" if ei_class == 1 else "64",
                            "BE" if ei_data == 2 else "LE"))
    e_machine = u16(d, 18)
    e_shoff = u32(d, 0x20); e_shentsize = u16(d, 0x2E)
    e_shnum = u16(d, 0x30); e_shstrndx = u16(d, 0x32)
    print("  e_machine=%d (SH=42) shnum=%d" % (e_machine, e_shnum))
    secs = []
    for i in range(e_shnum):
        base = e_shoff + i * e_shentsize
        secs.append({
            "name": u32(d, base + 0), "type": u32(d, base + 4),
            "offset": u32(d, base + 0x10), "size": u32(d, base + 0x14),
            "link": u32(d, base + 0x18), "entsize": u32(d, base + 0x24),
        })
    shstr = secs[e_shstrndx]
    def secname(s):
        o = shstr["offset"] + s["name"]
        e = d.index(b"\0", o); return d[o:e].decode("latin1")
    print("  -- SECTIONS (%d):" % len(secs))
    for i, s in enumerate(secs):
        print("      [%2d] %-20s type=%d size=0x%X" % (i, secname(s), s["type"], s["size"]))
    symtab = None
    for s in secs:
        if s["type"] == 2:  # SHT_SYMTAB
            symtab = s; break
    if not symtab:
        print("  no .symtab"); return
    strtab = secs[symtab["link"]]
    def symname(off):
        o = strtab["offset"] + off
        e = d.index(b"\0", o); return d[o:e].decode("latin1")
    n = symtab["size"] // symtab["entsize"]
    undef, defined, allsyms = [], [], []
    BIND = {0: "LOCAL", 1: "GLOBAL", 2: "WEAK"}
    TYP = {0: "NOTYPE", 1: "OBJECT", 2: "FUNC", 3: "SECTION", 4: "FILE"}
    for i in range(n):
        base = symtab["offset"] + i * symtab["entsize"]
        st_name = u32(d, base + 0); st_value = u32(d, base + 4)
        st_info = d[base + 12]; st_shndx = u16(d, base + 14)
        bind = st_info >> 4; typ = st_info & 0xF
        nm = symname(st_name)
        sec = secname(secs[st_shndx]) if 0 < st_shndx < len(secs) else (
            "UNDEF" if st_shndx == 0 else "abs/0x%X" % st_shndx)
        allsyms.append((nm, st_value, BIND.get(bind, str(bind)),
                        TYP.get(typ, str(typ)), sec))
        if not nm:
            continue
        if st_shndx == 0:  # SHN_UNDEF
            undef.append(nm)
        elif bind in (1, 2):  # GLOBAL/WEAK
            defined.append((nm, st_value, sec))
    print("  -- ALL symbols (%d):" % len(allsyms))
    for nm, val, bnd, typ, sec in allsyms:
        print("      %-24s val=0x%08X %-6s %-7s in %s" %
              (nm if nm else "<noname>", val, bnd, typ, sec))
    print("  -- DEFINED globals (%d):" % len(defined))
    for nm, val, sec in sorted(defined):
        print("      %-28s val=0x%08X  in %s" % (nm, val, sec))
    print("  -- UNDEFINED refs (%d):" % len(undef))
    for nm in sorted(set(undef)):
        print("      %s" % nm)

if __name__ == "__main__":
    for p in sys.argv[1:]:
        parse(p); print()
