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
     "p6_w_arun_brg_gapmiss", "p6_w_btch_calls", "p6_w_cont_frames",
     "p6_w_stream_mat", "p6_w_stream_dorm", "p6_w_stream_free",
     "p6_w_stream_resident", "p6_w_stream_starve", "p6_scan_loadbuild_seq",
     "p6_w_brg_classid", "p6_w_pool_inv_bad"]
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
def rb(a, n):
    out = bytearray(); left, cur = n, a
    while left > 0:
        take = min(2000, left)
        out += mem.read_saturn(cur, take)
        cur += take; left -= take
    return bytes(out)

def bridge_scan(brg_cid):
    """List (physslot, x, y, active) of every live Bridge in the narrow scene region."""
    NARROW, RES, WIDE = 344, 64, 556
    sphys = r32(sym("RSDK::p6_pool_scene_phys")) or 640
    raw = rb(pool + RES * WIDE, sphys * NARROW)
    outp = []
    for k in range(sphys):
        off = k * NARROW
        cid = (raw[off + 54] << 8) | raw[off + 55]
        if cid == brg_cid:
            x = int.from_bytes(raw[off:off+4], "big", signed=True) >> 16
            y = int.from_bytes(raw[off+4:off+8], "big", signed=True) >> 16
            outp.append((k, x, y, raw[off + 76]))
    return outp

ts = time.time()
tick_i = 0
while time.time() - ts < 120:
    tick_i += 1
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
    if tick_i % 8 == 1:
        bcid = s32(r32(sym("p6_w_brg_classid"))) or 0
        if bcid > 0:
            print("   bridges live: %s" % bridge_scan(bcid), flush=True)
    if folder() != "GHZ":
        print("left GHZ"); break
    time.sleep(0.3)
