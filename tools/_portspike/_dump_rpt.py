#!/usr/bin/env python3
# Dump the ACTUAL VDP2 RBG0 rotation-parameter table + coefficient band from a
# savestate's VRAM (the bytes the HARDWARE reads), decode each field, and compare
# to what p6_vdp2.c intends at the live angle. The C witness shares the sim's
# formula so it can't reveal a HW fixed-point/format mismatch; the raw VRAM can.
import os, sys, struct, math
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import qa_p6_scene as Q

MCS = sys.argv[1]
RPT = 0x05E3FF00       # P6_RBG0_RPT (physical VDP2 VRAM)
KTBL = 0x05E20000      # P6_RBG0_KTBL coeff table
LINE0 = 152

mod = Q.load_harness()
sec = mod.parse_savestate(Q._as_path(MCS))
mp = Q.read_text(Q.MAP_DEFAULT)
ma = Q.map_symbol(mp, "_p6_w_magic")
_, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
ang = Q.peek_u32(mod, sec, Q.map_symbol(mp, "_p6_w_title_island_angle"), perm, signed=True) if perm else None
print("live angle =", ang)

def r32(off):
    b = mod._peek_bytes(sec, RPT + off, 4)
    return struct.unpack(">I", bytes(b[:4]))[0] if b and len(b) >= 4 else None

def s32(v):  return v - 0x100000000 if v is not None and v & 0x80000000 else v
def fx(v):   return None if v is None else s32(v) / 65536.0

flds = [("Xst",0x00),("Yst",0x04),("Zst",0x08),("dXst",0x0C),("dYst",0x10),
        ("DX",0x14),("DY",0x18),("A",0x1C),("B",0x20),("C",0x24),("D",0x28),
        ("E",0x2C),("F",0x30),("Mx",0x44),("My",0x48),("kx",0x4C),("ky",0x50),
        ("KAst",0x54),("dKAst",0x58),("dKAx",0x5C)]
print("=== RPT in VRAM (raw hex / signed 16.16) ===")
for name, off in flds:
    v = r32(off)
    print("  +0x%02X %-6s = 0x%08X  (%.4f)" % (off, name, v if v is not None else 0, fx(v) or 0))

# coeff band: first/mid/last few entries (2-word = 4 bytes each, 1/line)
print("=== coeff band (KTBL[line], 2-word; want 2*id, far=0x28000 down to 0x75B4) ===")
for ln in [LINE0, LINE0+1, LINE0+26, LINE0+56, LINE0+71]:
    b = mod._peek_bytes(sec, KTBL + ln*4, 4)
    v = struct.unpack(">I", bytes(b[:4]))[0] if b and len(b) >= 4 else None
    # decode 2-word sign-magnitude: bit31 transp, bit23 sign, b22-16 int, b15-0 frac
    if v is not None:
        transp = (v >> 31) & 1; sign = (v >> 23) & 1
        integer = (v >> 16) & 0x7F; frac = v & 0xFFFF
        val = (integer + frac/65536.0) * (-1 if sign else 1)
        print("  line %3d KTBL = 0x%08X  transp=%d sign=%d int=%d frac=0x%04X -> %.4f"
              % (ln, v, transp, sign, integer, frac, val))
    else:
        print("  line %3d KTBL = None" % ln)
