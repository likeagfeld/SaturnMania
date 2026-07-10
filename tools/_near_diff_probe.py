#!/usr/bin/env python3
"""_near_diff_probe.py -- dump p6_scan_near (logical-slot bitfield, WRAM-H) while
the player is in the bridge-1 span; diff against the authored in-window slot set
(Scene1.bin). Identifies exactly WHICH in-window slots have no near bit."""
import importlib.util, re, struct, sys, time
from pathlib import Path

def load(m, f):
    s = importlib.util.spec_from_file_location(m, Path("tools") / f)
    x = importlib.util.module_from_spec(s); s.loader.exec_module(x); return x

qn = load("qn", "qa_netmem.py")
mem = qn.RetroMem("127.0.0.1", 55355, 2.0)
MAP = Path("game.map").read_text(errors="replace")
def sym(n):
    m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(n) + r"\s*$", MAP, re.M)
    return int(m.group(1), 16) if m else None
def r32(a):
    try: return mem.read32_saturn(a)
    except Exception: return None

# authored entities (slot,x,y,objname)
rs = load("rs", "render_scene.py")
d = open("extracted/Data/Stages/GHZ/Scene1.bin", "rb").read()
r = rs.R(d); r.p = 4; r.skip(0x10)
sl = r.u8(); r.skip(sl + 1)
for _ in range(r.u8()):
    r.u8(); r.s(); r.u8(); r.u8()
    r.u16(); r.u16(); r.u16(); r.u16()
    for _ in range(r.u16()): r.u16(); r.u16(); r.u8(); r.u8()
    r.compressed(); r.compressed()
VARSIZE = {0:1,1:2,2:4,3:1,4:2,5:4,6:4,7:4,9:8,10:4,11:4}
import hashlib
NAMES = """Player Ring ItemBox Spikes Spring Platform CollapsingPlatform Bridge
BreakableWall CheckerBall Decoration Motobug BuzzBomber Crabmeat Newtron Splats
Chopper Batbrain ForceSpin ForceUnstick SpinBooster CorkscrewPath ZipLine SpikeLog
BurningLog Water BGSwitch PlaneSwitch BoundsMarker InvisibleBlock Camera Music HUD
SignPost GHZSetup GHZ2Outro CutsceneSeq DDWrecker TimeAttackGate TitleCard StarPost""".split()
hmap = {hashlib.md5(n.encode()).digest(): n for n in NAMES}
objcount = r.u8()
ents = {}
for i in range(objcount):
    h = bytes(d[r.p:r.p+16]); r.p += 16
    nm = hmap.get(h, "?")
    varcount = d[r.p]; r.p += 1
    types = [None]
    for e in range(1, varcount):
        r.p += 16; types.append(d[r.p]); r.p += 1
    ecount = struct.unpack_from("<H", d, r.p)[0]; r.p += 2
    for e in range(ecount):
        slot = struct.unpack_from("<H", d, r.p)[0]; r.p += 2
        px = struct.unpack_from("<i", d, r.p)[0] >> 16; r.p += 4
        py = struct.unpack_from("<i", d, r.p)[0] >> 16; r.p += 4
        for v in range(1, varcount):
            t = types[v]
            if t == 8:
                ln = struct.unpack_from("<H", d, r.p)[0]; r.p += 2 + ln*2
            else: r.p += VARSIZE.get(t, 4)
        ents[slot] = (nm, px, py)

near_a = sym("p6_scan_near")
csf = sym("RSDK::currentSceneFolder")
def folder():
    try: return mem.read_saturn(csf, 16).split(b"\0")[0].decode(errors="replace")
    except Exception: return None

t0 = time.time()
while time.time() - t0 < 480:
    if folder() == "GHZ": break
    time.sleep(0.5)
print("GHZ @%.1fs" % (time.time() - t0), flush=True)
pool = 0x243000
done = 0
ts = time.time()
while time.time() - ts < 150 and done < 3:
    pxr = r32(pool)
    px = (pxr - 0x100000000 if pxr and pxr >= 0x80000000 else pxr)
    px = px >> 16 if px is not None else None
    cid = (r32(pool + 52) or 0) & 0xFFFF
    idx_n = r32(sym("p6_w_scancull_n"))
    if cid == 8 and px is not None and 1150 <= px <= 1400 and idx_n and idx_n > 900:
        nb = mem.read_saturn(near_a, 152)
        cam = px  # camera tracks player here
        exp, got, missing, extra = [], [], [], []
        for slot, (nm, x, y) in sorted(ents.items()):
            L = slot + 64
            bit = (nb[L >> 3] >> (L & 7)) & 1
            inwin = abs(x - cam) <= 1024
            if inwin: exp.append(L)
            if bit: got.append(L)
            if inwin and not bit: missing.append((L, slot, nm, x, y))
        print("cam~%d idx_n=%s expected=%d gotbits=%d" % (cam, idx_n, len(exp), len(got)), flush=True)
        print("MISSING near bits (in-window, authored):")
        for L, slot, nm, x, y in missing:
            print("   L=%4d scene=%4d %-14s (%5d,%5d)" % (L, slot, nm, x, y))
        done += 1
        time.sleep(1.0)
        continue
    if folder() != "GHZ":
        print("left GHZ"); break
    time.sleep(0.2)
print("done")
