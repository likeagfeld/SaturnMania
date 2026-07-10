#!/usr/bin/env python3
# _brg_index_probe.py -- signpost campaign: is bridge-1 (logical slot 257 =
# Scene1.bin slotID 193 + RESERVE 64) in the near-index/near-set while the
# player approaches? Reads p6_w_scancull_n (index size), the p6_scan_near bit
# for slot 257, and the arun bridge forensics, every 0.3s once folder==GHZ.
import importlib.util, re, sys, time
from pathlib import Path

spec = importlib.util.spec_from_file_location("qn", "tools/qa_netmem.py")
qn = importlib.util.module_from_spec(spec); spec.loader.exec_module(qn)
mem = qn.RetroMem("127.0.0.1", 55355, 2.0)
mt = Path("game.map").read_text(errors="replace")
def sym(n):
    m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(n) + r"\s*$", mt, re.M)
    return int(m.group(1), 16) if m else None
def r32(a):
    try: return mem.read32_saturn(a)
    except Exception: return None
def s32(v): return v - 0x100000000 if v is not None and v >= 0x80000000 else v

L_BRG1 = 257
csf = sym("RSDK::currentSceneFolder")
near = sym("p6_scan_near")
scn = sym("p6_w_scancull_n")
scnear = sym("p6_w_scancull_near")
pool = 0x243000
W = ["p6_w_arun_brg_live", "p6_w_arun_brg_firstx", "p6_w_arun_inspan",
     "p6_w_arun_brg_gapmiss", "p6_w_btch_calls", "p6_w_cont_frames"]
WA = {w: sym(w) for w in W}
print("near addr", hex(near), "scancull_n addr", hex(scn))

def folder():
    try: return mem.read_saturn(csf, 16).split(b"\0")[0].decode(errors="replace")
    except Exception: return None

t0 = time.time()
while time.time() - t0 < 420:
    if folder() == "GHZ":
        break
    time.sleep(1)
print("GHZ @%.1fs" % (time.time() - t0), flush=True)
ts = time.time()
while time.time() - ts < 120:
    b = r32(near + (L_BRG1 >> 3) & ~3)  # dword containing byte 257>>3=32
    # byte 32 of p6_scan_near: read 4 bytes at near+32
    try:
        by = mem.read_saturn(near + (L_BRG1 >> 3), 1)[0]
    except Exception:
        by = None
    bit = (by >> (L_BRG1 & 7)) & 1 if by is not None else None
    px = s32(r32(pool + 0))
    px = px >> 16 if px is not None else None
    cid = (r32(pool + 52) or 0) & 0xFFFF
    vals = {w: s32(r32(WA[w])) for w in W}
    print("t%5.1f plr(%s)x=%s idx_n=%s near_cnt=%s brg1nearbit=%s %s" %
          (time.time() - ts, cid, px, s32(r32(scn)), s32(r32(scnear)), bit, vals), flush=True)
    if folder() != "GHZ":
        print("left GHZ"); break
    time.sleep(0.3)
