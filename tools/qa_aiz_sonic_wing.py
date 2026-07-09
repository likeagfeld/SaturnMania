#!/usr/bin/env python3
# qa_aiz_sonic_wing.py -- RED->GREEN gate for "Sonic missing on the AIZ Tornado top wing".
#
# ROOT CAUSE (this session, live-trace + GL screenshot): during the AIZ intro fly-in the
# decomp AIZTornado_HandlePlayerCollisions rides SLOT_PLAYER1 as a platform (Player_State_
# Static + ANI_RIDE; MEASURED Sonic.x tracks 92->6274 with the plane, visible=1) -- the ride
# logic is CORRECT. But the chain build (P6_FRONTEND_TITLE) skips the 9 GHZ gameplay sheets at
# boot and only stages Players/Sonic1.gif at the GHZ-handoff seam (STEP-3), which runs AFTER
# the AIZ intro. So the AIZ Sonic's Players/Sonic1.gif surface has saturnSheetSlot==-1 ->
# Player_Draw's DrawSprite drops (no VDP1 handle) -> the wing is EMPTY.
#
# FIX: p6_aiz_reload stages SONIC1/2/3.SHT (hash Players/SonicN.gif), banded, so the arm-env
# bind loop binds Sonic's surface + p6_pal_mirror provides the palette (bank1, like the Tornado).
#
# This gate measures the LIVE witness p6_w_aiz_sonicsht_slot (written by p6_aiz_reload):
#   RED  (current committed build): symbol ABSENT from game.map, or reads -9 (never staged)
#                                   -> Sonic's sheet is NOT staged during AIZ -> wing empty.
#   GREEN(fixed build):             p6_w_aiz_sonicsht_slot >= 0 during folder=='AIZ'
#                                   -> the Players/Sonic1.gif surface has a staged slot ->
#                                      the arm bind loop binds it -> Player_Draw's blit lands
#                                      -> blue Sonic renders on the wing (confirm by screenshot).
#
# The SECONDARY (authoritative) proof is the SCREENSHOT: the fly-in frame must show blue Sonic
# standing on the Tornado's TOP WING (above the "SONIC" fuselage). See _gl_boot.ps1 +
# SCREENSHOT UDP; RED baseline = tools/refs (biplane with EMPTY top wing).
#
# Usage (parent runs after integrating + building):
#   pwsh tools/qa_live.ps1 -NoMonitor -NoCdda        # boot headless RA (chain autorun)
#   python tools/qa_aiz_sonic_wing.py                # polls to AIZ, asserts the witness >=0
#
# Requires: headless RetroArch already running (qa_live.ps1 -NoMonitor), game.map present.
import importlib.util, time, sys
from pathlib import Path

H = Path("tools")


def load(m, f):
    s = importlib.util.spec_from_file_location(m, H / f)
    x = importlib.util.module_from_spec(s)
    s.loader.exec_module(x)
    return x


qt = load("qa_trace", "qa_trace.py")
mt = Path("game.map").read_text(errors="replace")
rd = qt.Reader(True, None, "127.0.0.1", 55355)
csf = rd.sym(mt, "RSDK::currentSceneFolder")


def folder():
    try:
        return rd.mem.read_saturn(csf, 16).split(b"\0")[0].decode(errors="replace")
    except Exception:
        return None


def s32(v):
    return v - 0x100000000 if (v is not None and v >= 0x80000000) else v


def sym_present(name):
    return rd.sym(mt, name) is not None


WAIT = float(sys.argv[1]) if len(sys.argv) > 1 else 120.0

# 1) Structural: the witness must exist in the built image (proves the fix compiled in).
if not sym_present("p6_w_aiz_sonicsht_slot"):
    print("[RED] p6_w_aiz_sonicsht_slot ABSENT from game.map -- the AIZ-SONIC-VIS fix is NOT "
          "in this build (this is the RED baseline; Sonic's sheet was never staged for AIZ).")
    sys.exit(1)

# 2) Live: drive the chain to AIZ, read the witness (best value seen while folder=='AIZ').
t0 = time.time()
best = -99
saw_aiz = False
last = None
while time.time() - t0 < WAIT:
    f = folder()
    if f != last:
        print("[%6.1fs] folder=%r" % (time.time() - t0, f), flush=True)
        last = f
    if f == "AIZ":
        saw_aiz = True
        a = rd.sym(mt, "p6_w_aiz_sonicsht_slot")
        v = s32(rd.r32(a)) if a is not None else None
        if v is not None and v > best:
            best = v
        # Once it resolves >=0 we can stop early (the staging ran).
        if best >= 0:
            break
    # Past AIZ (into GHZCutscene/GHZ) with a valid reading already captured -> stop.
    if saw_aiz and f not in ("AIZ", None) and best > -99:
        break
    time.sleep(0.2)

if not saw_aiz:
    print("[RED] never reached folder=='AIZ' within %.0fs -- cannot evaluate (chain stall?)." % WAIT)
    sys.exit(2)

print("p6_w_aiz_sonicsht_slot (best during AIZ) = %s" % str(best))
if best >= 0:
    print("[GREEN] Players/Sonic1.gif is staged (slot %d) during the AIZ fly-in -> the arm "
          "bind loop binds Sonic's surface -> Player_Draw's blit lands. Confirm the wing "
          "visually: _gl_boot.ps1 + SCREENSHOT while folder=='AIZ' must show blue Sonic on "
          "the Tornado TOP WING (above 'SONIC')." % best)
    sys.exit(0)
else:
    print("[RED] p6_w_aiz_sonicsht_slot=%d during AIZ -> Sonic's Players/Sonic1.gif sheet is "
          "NOT staged -> Player_Draw's blit drops -> the wing is EMPTY (the reported bug)." % best)
    sys.exit(1)
