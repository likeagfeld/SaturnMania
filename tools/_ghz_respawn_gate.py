#!/usr/bin/env python3
# _ghz_respawn_gate.py -- RED gate for #318 death->respawn (chain build).
#
# HYPOTHESIS (decomp-confirmed): when the leader dies with lives left,
# Player_HandleDeath (Player.c:2089-2091) runs Zone_StartFadeOut_MusicFade +
# blanks the player (classID -> TYPE_BLANK=0); Zone_State_FadeOut (Zone.c:812)
# then RSDK.LoadScene() -> sceneInfo.state = ENGINESTATE_LOAD with folder still
# "GHZ". p6_frontend_frame's ENGINESTATE_LOAD seam only handles the forward
# chain hops (one-shots to NEW folders), so a GHZ->GHZ reload falls through to
# the swallow (p6_io_main.cpp:7311 `state = ENGINESTATE_REGULAR`) -> the blanked
# player never respawns. RED = death occurs, p6_w_transitions increments (Zone
# DID request the reload), yet slot0 classID stays 0 forever. GREEN = after the
# reload, slot0 classID returns to 8 (Sonic re-created at spawn).
#
# Reads live headless RetroArch (55355). Boot first: pwsh tools/qa_live.ps1 -NoMonitor
import importlib.util, time, sys
from pathlib import Path
H = Path("tools")
def load(m, f):
    s = importlib.util.spec_from_file_location(m, H / f); x = importlib.util.module_from_spec(s); s.loader.exec_module(x); return x
qt = load("qa_trace", "qa_trace.py")
STRIDE = 556; POOL = 0x243000
mt = Path("game.map").read_text(errors="replace")
rd = qt.Reader(True, None, "127.0.0.1", 55355)
csf = rd.sym(mt, "RSDK::currentSceneFolder")
def folder():
    try: return rd.mem.read_saturn(csf, 16).split(b"\0")[0].decode(errors="replace")
    except Exception: return None
def U(*names):
    for n in names:
        a = rd.sym(mt, n)
        if a is not None:
            v = rd.r32(a)
            if v is not None: return v
    return None
def s32(v): return v - 0x100000000 if v is not None and v >= 0x80000000 else v
def slot0():
    b = POOL
    cid = (rd.r32(b+52) or 0) & 0xFFFF
    x = s32(rd.r32(b+0) or 0) >> 16; y = s32(rd.r32(b+4) or 0) >> 16
    return cid, x, y

WAIT       = float(sys.argv[1]) if len(sys.argv) > 1 else 300.0  # reach GHZ + see Sonic alive
RESPAWNWIN = float(sys.argv[2]) if len(sys.argv) > 2 else 90.0   # watch for respawn after death

# --- Phase 1: reach GHZ with Sonic alive (classID 8) ---
t0 = time.time(); seen = set(); alive = False
while time.time() - t0 < WAIT:
    f = folder(); c0, x0, y0 = slot0()
    key = (f, c0 == 8)
    if key not in seen:
        seen.add(key)
        print("[%6.1fs] folder=%r slot0.classID=%d pos=(%d,%d) cont=%s trans=%s"
              % (time.time()-t0, f, c0, x0, y0, U("p6_w_cont_frames"), U("p6_w_transitions")), flush=True)
    if f == "GHZ" and c0 == 8:
        alive = True; break
    time.sleep(0.4)
if not alive:
    print("\nINCONCLUSIVE: never saw Sonic ALIVE (classID 8) in GHZ within %.0fs" % WAIT); sys.exit(2)
print("\n=== Sonic ALIVE in GHZ @%.1fs -- watching for death then respawn ===" % (time.time()-t0), flush=True)

# --- Phase 2: watch for the death (classID 8 -> 0) ---
tD = time.time(); died = False; trans_at_alive = U("p6_w_transitions")
while time.time() - tD < WAIT:
    c0, x0, y0 = slot0()
    if c0 == 0:
        died = True; trans_at_death = U("p6_w_transitions"); cont_at_death = U("p6_w_cont_frames")
        print("[death @%.1fs] slot0 classID->0 (TYPE_BLANK) pos=(%d,%d) trans=%s cont=%s"
              % (time.time()-tD, x0, y0, trans_at_death, cont_at_death), flush=True)
        break
    time.sleep(0.3)
if not died:
    print("\nINCONCLUSIVE: Sonic never DIED within %.0fs (no Motobug collision?)" % WAIT); sys.exit(2)

# --- Phase 3: after death, watch for respawn (classID -> 8) + reload evidence ---
tR = time.time(); respawned = False; trans_seen = trans_at_death; cont_moved = False
while time.time() - tR < RESPAWNWIN:
    c0, x0, y0 = slot0()
    tr = U("p6_w_transitions"); cont = U("p6_w_cont_frames")
    if tr is not None and trans_seen is not None and tr > trans_seen: trans_seen = tr
    if cont is not None and cont_at_death is not None and cont > cont_at_death + 3: cont_moved = True
    if c0 == 8:
        respawned = True
        print("[respawn @%.1fs] slot0 classID->8 pos=(%d,%d) trans=%s cont=%s"
              % (time.time()-tR, x0, y0, tr, cont), flush=True)
        break
    time.sleep(0.4)

reload_requested = (trans_seen is not None and trans_at_death is not None and trans_seen > trans_at_death)
print("\n--- VERDICT ---")
print("reload requested by Zone (p6_w_transitions incremented post-death): %s" % reload_requested)
print("engine kept ticking post-death (cont_frames advanced):             %s" % cont_moved)
print("Sonic respawned (slot0 classID returned to 8):                     %s" % respawned)
if respawned:
    print("GREEN: death->respawn works."); sys.exit(0)
else:
    print("RED: Sonic died, never respawned within %.0fs." % RESPAWNWIN)
    if reload_requested:
        print("  -> Zone DID request the reload but it was SWALLOWED (p6_frontend_frame:7311).")
    else:
        print("  -> Zone never requested the reload (fade/LoadScene path, not the swallow).")
    sys.exit(1)
