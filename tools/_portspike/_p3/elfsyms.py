#!/usr/bin/env python3
# Minimal ELF32-BE symbol-table dumper (host-side; the jo Docker toolchain ships
# no nm/objdump/readelf). Resolves the ONE question p3.linker + the witness gate
# + crt0 all depend on: does sh-none-elf prepend a leading underscore to C
# symbols, or is the ELF symbol name identical to the C identifier?
import sys, struct

def dump(path, want_substr=None):
    d = open(path, "rb").read()
    assert d[:4] == b"\x7fELF", "not ELF"
    ei_class = d[4]; ei_data = d[5]
    assert ei_class == 1, "not ELF32"
    end = ">" if ei_data == 2 else "<"   # 2 = big-endian
    # ELF32 header
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags,
     e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx
     ) = struct.unpack(end + "HHIIIIIHHHHHH", d[16:52])
    secs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size,
         sh_link, sh_info, sh_addralign, sh_entsize
         ) = struct.unpack(end + "IIIIIIIIII", d[off:off+40])
        secs.append(dict(name=sh_name, type=sh_type, off=sh_offset, size=sh_size,
                         link=sh_link, entsize=sh_entsize))
    # find .symtab (type 2) + its string table (sh_link)
    sym = next((s for s in secs if s["type"] == 2), None)
    if not sym:
        print("  (no .symtab)"); return
    strtab = secs[sym["link"]]
    strdata = d[strtab["off"]:strtab["off"]+strtab["size"]]
    def name(o):
        e = strdata.find(b"\x00", o)
        return strdata[o:e].decode("latin1")
    n = sym["size"] // 16
    print(f"== {path}  ({n} symbols, {'BE' if end=='>' else 'LE'}) ==")
    for i in range(n):
        o = sym["off"] + i * 16
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack(
            end + "IIIBBH", d[o:o+16])
        nm = name(st_name)
        if not nm:
            continue
        bind = st_info >> 4; typ = st_info & 0xf
        bs = {0:"LOCAL",1:"GLOBAL",2:"WEAK"}.get(bind, str(bind))
        ts = {0:"NOTYPE",1:"OBJECT",2:"FUNC",3:"SECTION",4:"FILE"}.get(typ, str(typ))
        shn = {0:"UND"}.get(st_shndx, str(st_shndx))
        if want_substr and want_substr not in nm:
            continue
        print(f"  {nm:30s} bind={bs:6s} type={ts:7s} shndx={shn:4s} val=0x{st_value:08x} sz={st_size}")

if __name__ == "__main__":
    path = sys.argv[1]
    want = sys.argv[2] if len(sys.argv) > 2 else None
    dump(path, want)
