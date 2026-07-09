#!/usr/bin/env python3
"""qa_aiz_pan_target.py -- GATE: the AIZ intro beat-4 (EnterClaw) camera pan target.

Decomp contract (AIZSetup.c:457): Camera_SetupLerp(..., AIZSetup->platform->position.x
- 0x400000, ...) where AIZSetup->platform = the first scene Platform with frameID==0
(AIZSetup.c:279-286). Scene1.bin authored Platforms sit at x=11120 (slots 14..24,
parsed offline) -> the ONLY decomp-correct lerp target x is 11120-64 = 11056 px.

RED (measured 2026-07-09, chain build 3e70b2c): lerpEndX = 8128 -- Platform is
UNREGISTERED so AIZSetup->platform is NULL and the target is a garbage read; the
camera pans 2843 px LEFT into empty jungle away from the claw/EggRobos/Ruby.

Usage: boot the chain live (tools/_gl_boot.ps1 or qa_live.ps1 -NoMonitor), then
    python tools/qa_aiz_pan_target.py [max_wait_aiz_s]
Waits for the AIZ scene, then for beat>=4, samples camera lerpEndX + settled camx.
Exit 0 = GREEN (target 11056 +-4 AND settled camx >= 10900); 1 = RED; 2 = harness.
"""
import importlib.util, sys, time
from pathlib import Path

H = Path(__file__).resolve().parent
def load(m, f):
    s = importlib.util.spec_from_file_location(m, H / f)
    x = importlib.util.module_from_spec(s); s.loader.exec_module(x); return x
qt = load("qa_trace", "qa_trace.py")

POOL = 0x00243000; WIDE = 556
EXPECT = 11056; TOL = 4
mt = Path("game.map").read_text(errors="replace")
rd = qt.Reader(True, None, "127.0.0.1", 55355)
csf = rd.sym(mt, "RSDK::currentSceneFolder")
def folder():
    try: return rd.mem.read_saturn(csf, 16).split(b"\0")[0].decode(errors="replace")
    except Exception: return None
BEAT_A = rd.sym(mt, "p6_w_aiz_cutscene_state") or rd.sym(mt, "RSDK::p6_w_aiz_cutscene_state")
def s32(v): return v - 0x100000000 if v is not None and v >= 0x80000000 else v
def beat():
    v = rd.r32(BEAT_A)
    return s32(v)

WAIT = float(sys.argv[1]) if len(sys.argv) > 1 else 480.0
t0 = time.time()
while time.time() - t0 < WAIT:
    if folder() == "AIZ": break
    time.sleep(0.4)
else:
    print("GATE ERROR: never reached AIZ"); sys.exit(2)
print("AIZ reached @%.0fs; waiting for beat 4..." % (time.time() - t0))

camslot = None
def cam():
    global camslot
    if camslot is None or ((rd.r32(POOL + camslot * WIDE + 52) or 0) & 0xFFFF) != 6:
        camslot = None
        for sl in range(64):
            if ((rd.r32(POOL + sl * WIDE + 52) or 0) & 0xFFFF) == 6:
                camslot = sl; break
    if camslot is None: return None, None
    b = POOL + camslot * WIDE
    return s32(rd.r32(b + 0)), s32(rd.r32(b + 172))  # position.x, endLerpPos.x

t1 = time.time(); targets = []; settled = []
while time.time() - t1 < 240.0:
    b = beat()
    f = folder()
    if f != "AIZ":
        break
    if b is not None and 4 <= b <= 8:
        cx, ex = cam()
        if ex is not None and ex != 0:
            targets.append(ex >> 16)
        if b >= 5 and cx is not None:
            settled.append(cx >> 16)
    if b is not None and b >= 9:
        break
    time.sleep(0.25)

if not targets:
    print("GATE ERROR: never sampled a lerp target in beats 4-8"); sys.exit(2)
tgt = max(set(targets), key=targets.count)
stl = settled[-1] if settled else None
ok_t = abs(tgt - EXPECT) <= TOL
ok_s = (stl is not None and stl >= 10900)
print("lerp target x = %d (expect %d +-%d) -> %s" % (tgt, EXPECT, TOL, "OK" if ok_t else "RED"))
print("settled camx  = %s (expect >= 10900) -> %s" % (stl, "OK" if ok_s else "RED"))
if ok_t and ok_s:
    print("GATE GREEN: camera pans to the dig site (claw/EggRobos/Ruby framed)")
    sys.exit(0)
print("GATE RED: camera pans to a garbage target (empty jungle)")
sys.exit(1)
