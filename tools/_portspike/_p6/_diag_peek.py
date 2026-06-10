#!/usr/bin/env python3
# Throwaway diagnostic: peek P6.2 witnesses + both SH-2 PCs from the io-green
# savestate, reusing qa_p6_io.py's harness + magic calibration. Cleaned at exit.
import importlib.util, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORTSPIKE = os.path.normpath(os.path.join(HERE, ".."))
MCS = os.path.join(HERE, "_p6_io.mcs")
MCS_EXTRACT = os.path.normpath(os.path.join(PORTSPIKE, "..", "mcs_extract.py"))

MAGIC_BE = bytes([0x12, 0x34, 0x56, 0x78])
PERMS = [
    ("big-endian (identity)", (0, 1, 2, 3)),
    ("16-bit pair-swap", (1, 0, 3, 2)),
    ("full little-endian", (3, 2, 1, 0)),
    ("32-bit word-swap", (2, 3, 0, 1)),
]
SYMS = {
    "p6_w_magic":        0x0604E140,
    "p6_w_io_step":      0x06069F90,
    "p6_w_io_fid":       0x06069F94,
    "p6_w_io_gfsinit":   0x06069F98,
    "p6_w_io_firstbytes":0x06069F9C,
    "p6_w_io_filesize":  0x06069FA0,
    "p6_w_io_loaded":    0x06069FA4,
    "p6_w_ring_posy":    0x06069FB0,
    "p6_w_ring_classid": 0x06069FC0,
    "p6_w_last_frameid": 0x06069FC4,
    "p6_w_draw_calls":   0x06069FC8,
    "p6_w_ticks":        0x06069FCC,
}

def load_harness():
    spec = importlib.util.spec_from_file_location("mcs_extract", MCS_EXTRACT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

def decode_u32(raw4, perm):
    inv = [perm.index(j) for j in range(4)]
    true_be = bytes(raw4[inv[j]] for j in range(4))
    return struct.unpack(">I", true_be)[0]

def main():
    mod = load_harness()
    try:
        from pathlib import Path
        sections = mod.parse_savestate(Path(MCS))
    except Exception:
        sections = mod.parse_savestate(MCS)

    raw = mod._peek_bytes(sections, SYMS["p6_w_magic"], 4)
    perm = None; label = None
    for lbl, p in PERMS:
        if bytes(MAGIC_BE[i] for i in p) == bytes(raw[:4]):
            perm, label = p, lbl; break
    print("magic raw=%s perm=%s" % (raw.hex() if raw else None, label))
    if perm is None:
        print("NO CALIBRATION"); return 2

    for name, addr in SYMS.items():
        b = mod._peek_bytes(sections, addr, 4)
        if b is None or len(b) < 4:
            print("  %-20s @0x%08X = <unreadable>" % (name, addr)); continue
        v = decode_u32(bytes(b[:4]), perm)
        sv = v - 0x100000000 if v & 0x80000000 else v
        print("  %-20s @0x%08X = %d (0x%08X)  signed=%d" % (name, addr, v, v, sv))

    for who in ("master", "slave"):
        regs = mod._sh2_regs(sections, who)
        if not regs:
            print("  SH2-%s regs: <none>" % who); continue
        pc = regs.get("PC"); pr = regs.get("PR"); sr = regs.get("SR")
        vbr = regs.get("VBR")
        print("  SH2-%-6s PC=%s PR=%s SR=%s VBR=%s" % (
            who,
            ("0x%08X" % pc) if pc is not None else "?",
            ("0x%08X" % pr) if pr is not None else "?",
            ("0x%08X" % sr) if sr is not None else "?",
            ("0x%08X" % vbr) if vbr is not None else "?"))

    # BIOS interrupt jump table (SEGA_SYS.H:109-116): SYS_SETUINT @0x06000300,
    # GETUINT @0x304, SETSINT @0x310, GETSINT @0x314. If these are not valid
    # BIOS-ROM (0x000xxxxx) pointers, the SGL interrupt-registration path is
    # broken -> slInitSystem's V-blank handler register faults.
    RAW = [
        ("SETUINT_ptr @0x06000300", 0x06000300),
        ("GETUINT_ptr @0x06000304", 0x06000304),
        ("SETSINT_ptr @0x06000310", 0x06000310),
        ("GETSINT_ptr @0x06000314", 0x06000314),
        ("masterPR-region @0x06000928", 0x06000928),
        ("masterPC-region @0x06000950", 0x06000950),
        ("masterPC-region @0x06000954", 0x06000954),
    ]
    for name, addr in RAW:
        b = mod._peek_bytes(sections, addr, 4)
        if b is None or len(b) < 4:
            print("  %-26s = <unreadable>" % name); continue
        v = decode_u32(bytes(b[:4]), perm)
        print("  %-26s = 0x%08X" % (name, v))

    # Recover the FAULTING instruction address from the master stack. The SH-2
    # exception sequence pushes SR then PC at R15 (SP). Dumping words from R15
    # surfaces the original PC inside LIBSGL.A where slInitSystem died.
    mregs = mod._sh2_regs(sections, "master")
    if mregs and mregs.get("R15") is not None:
        sp = mregs["R15"]
        print("  master R0..R15:")
        for i in range(16):
            print("    R%-2d = 0x%08X" % (i, mregs.get("R%d" % i, 0)))
        print("  master stack dump from R15=0x%08X:" % sp)
        for k in range(24):
            a = sp + k * 4
            b = mod._peek_bytes(sections, a, 4)
            if b is None or len(b) < 4:
                print("    @0x%08X = <unreadable>" % a); continue
            v = decode_u32(bytes(b[:4]), perm)
            tag = ""
            if 0x060040C0 <= v < 0x06042A58: tag = " <- in LIBSGL/.text"
            elif 0x06042A60 <= v < 0x0604E140: tag = " <- in SLPROG"
            print("    @0x%08X = 0x%08X%s" % (a, v, tag))
    return 0

if __name__ == "__main__":
    sys.exit(main())
