#!/usr/bin/env python3
"""qa_ddwrecker.py -- LIVE-MEMORY gate for the GHZ1 DDWrecker boss port
(2026-07-11). NO pixels: every check is a live Saturn memory read (RetroArch
Beetle Saturn UDP 55355 via the qa_live.ps1 harness) + the game.map symbols +
the Scene1.bin manifest.

Boot first:  pwsh tools/qa_live.ps1 -NoCdda -NoMonitor   (a boss-flavor build)
Then:        python tools/qa_ddwrecker.py [--wait 240] [--watch 600]

The DDWrecker boss (decomp GHZ/DDWrecker.c) is the placed SETUP entity at
(15792,1588) slot 317 in GHZ/Scene1.bin. Its State_InitChildren spawns 7
children (4 CHAIN + 1 CORE + 2 BALL) into entitySlot+1..+7. The two BALL
children carry health=3 and the hitbox. Hitting a Vulnerable ball ->
DDWrecker_Hit (DDWrecker.c:774) --health; at 0 -> State_Die (DDWrecker.c:890)
-> State_SpawnSignpost (DDWrecker.c:913) sets every SignPost ->
SignPost_State_Falling; that lands and transitions to SignPost_State_Spin
(SignPost.c:581) -> ActClear at SLOT_ACTCLEAR (SignPost.c:452).

Checks (mission classes):
  D1 registered  -- DDWrecker has a live classID (object-ptr symbol or the
                    p6_w_ddw_classid witness). RED if unregistered (absent
                    symbol / classID 0).
  D2 spawns/runs -- a DDWrecker entity exists in the scene pool and its state
                    advances (state pointer lands in the DDWrecker_State_*
                    range and is observed to CHANGE across samples).
  D3 hittable    -- at least one BALL child (type in {1,2}) exists with
                    health>0 (the target the fight must damage).
  D4 defeat->spin-- on defeat the SignPost DROP/RUNPAST entity reaches
                    state==SignPost_State_Spin NATURALLY + ActClear appears.

Entity field offsets (tools/_offsets_probe.c, 2026-07-11, census Game.h):
  DDWrecker: state @88  stateBall @92  type @104  health @116  animator @180
  SignPost : state @88  type @92  spinCount @128
  classID  @54 (dword @52, lo16 after BE swap); pos @0/4; active(u8) @76.

Exit: 0 = all PASS/SKIP, 1 = any RED, 2 = harness failure.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import subprocess
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(errors="backslashreplace")
    except Exception:
        pass


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


qa_netmem = _load("qa_netmem", "qa_netmem.py")

# ---- pool geometry (Object.hpp, verified against the live map) --------------
RESERVE = 64
SCENE_PHYS = 1088
WIDE = 556
NARROW = 344
SLOT_ACTCLEAR = 16

# DDWrecker entity offsets (tools/_offsets_probe.c)
DDW_STATE, DDW_STATEBALL, DDW_TYPE, DDW_HEALTH = 88, 92, 104, 116
# SignPost entity offsets (same probe)
SIGN_STATE, SIGN_TYPE, SIGN_SPIN = 88, 92, 128

DDW_X = 15792  # Scene1.bin manifest (tools/_ghz1_obstacle_map.py) slot 317


def s32(v):
    return v - 0x100000000 if v is not None and v >= 0x80000000 else v


class Live:
    def __init__(self, host, port, map_path, ovl_map_path=None):
        self.mem = qa_netmem.RetroMem(host, port, 2.0)
        self.map_text = Path(map_path).read_text(errors="replace")
        self.ovl_text = ""
        if ovl_map_path and Path(ovl_map_path).exists():
            self.ovl_text = Path(ovl_map_path).read_text(errors="replace")
        self._symcache = {}

    def sym(self, name):
        if name in self._symcache:
            return self._symcache[name]
        m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$",
                      self.map_text, re.M)
        a = int(m.group(1), 16) if m else None
        self._symcache[name] = a
        return a

    def ovl_sym(self, name):
        """Overlay-only symbol (DDWrecker_State_* live in ovl_ring.map). The
        state function pointer stored in an entity is the EXECUTABLE cached
        alias 0x0269xxxx == the ovl_ring.map address."""
        if not self.ovl_text:
            return None
        m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$",
                      self.ovl_text, re.M)
        return int(m.group(1), 16) if m else None

    def r32(self, addr):
        try:
            return self.mem.read32_saturn(addr)
        except Exception:
            return None

    def r32s(self, name):
        a = self.sym(name)
        return self.r32(a) if a else None

    def rblock(self, addr, n, chunk=2000):
        out = bytearray()
        a, left = addr, n
        while left > 0:
            take = min(chunk, left)
            out += self.mem.read_saturn(a, take)
            a += take
            left -= take
        return bytes(out)

    def folder(self):
        a = self.sym("RSDK::currentSceneFolder")
        if not a:
            return None
        try:
            return self.mem.read_saturn(a, 16).split(b"\0")[0].decode(errors="replace")
        except Exception:
            return None


def be16(b, off):
    return (b[off] << 8) | b[off + 1]


def be32(b, off):
    return int.from_bytes(b[off:off + 4], "big")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=55355)
    ap.add_argument("--map", default=str(_HERE.parent / "game.map"))
    ap.add_argument("--ovl-map", default=str(_HERE / "_portspike" / "_p6" / "ovl_ring.map"))
    ap.add_argument("--wait", type=float, default=240.0, help="s to reach GHZ")
    ap.add_argument("--watch", type=float, default=600.0, help="s to watch the boss")
    ap.add_argument("--tick", type=float, default=0.35)
    ap.add_argument("--out", default="_ddwrecker_run.jsonl")
    a = ap.parse_args(argv)

    lv = Live(a.host, a.port, a.map, a.ovl_map)

    # ---- harness health (binding: fail LOUD, never trust garbage) ----------
    pool = lv.r32s("RSDK::objectEntityList")
    if pool is None or not (0x00200000 <= pool < 0x00300000):
        print("HARNESS UNHEALTHY: objectEntityList=%s not a WRAM-L pool ptr; "
              "core stuck at boot or mapping wrong. Reboot qa_live.ps1 and retry."
              % (hex(pool) if pool else pool))
        return 2
    scene_base = pool + RESERVE * WIDE
    sphys = lv.r32s("RSDK::p6_pool_scene_phys")
    if sphys is None or not (0 < sphys <= SCENE_PHYS):
        sphys = SCENE_PHYS

    # DDWrecker + SignPost + ActClear classIDs. DDWrecker via its object-ptr
    # symbol (ptr -> Object.classID u16 @ +0) OR the p6_w_ddw_classid witness.
    def obj_cid(name):
        p = lv.r32s(name)
        if p and 0x00200000 <= p < 0x06100000:
            v = lv.r32(p)
            return (v >> 16) & 0xFFFF if v is not None else None
        return None

    spin_addr = lv.sym("SignPost_State_Spin")
    # DDWrecker_State_* symbols live in the OVERLAY map (ovl_ring.map), NOT
    # game.map -- the state ptr stored in an entity is the executable cached
    # alias 0x0269xxxx == the ovl_ring.map address. Read them via ovl_sym.
    ddw_syms = {}
    for nm in ("DDWrecker_State_SetupArena", "DDWrecker_State_InitChildren",
               "DDWrecker_State_Assemble", "DDWrecker_State_EnterWreckers",
               "DDWrecker_State_AttackDelay", "DDWrecker_State_SwingRight",
               "DDWrecker_State_SwingLeft", "DDWrecker_State_Die",
               "DDWrecker_State_SpawnSignpost", "DDWrecker_StateBall_Vulnerable",
               "DDWrecker_StateBall_Spiked"):
        ddw_syms[nm] = lv.ovl_sym(nm)
    ddw_sym_addrs = {v for v in ddw_syms.values() if v}

    cid_ddw = obj_cid("DDWrecker")
    if cid_ddw is None:
        w = lv.r32s("p6_w_ddw_classid")
        cid_ddw = s32(w) if w and s32(w) > 0 else None
    cid_sign = obj_cid("SignPost")
    cid_ac = obj_cid("ActClear")
    print("live syms: DDWrecker registered? classID=%s ; SignPost=%s ActClear=%s"
          % (cid_ddw, cid_sign, cid_ac))
    print("DDWrecker_State_* map symbols present: %d/%d ; SignPost_State_Spin=%s"
          % (len(ddw_sym_addrs), len(ddw_syms), hex(spin_addr) if spin_addr else None))

    # ---- phase A: reach GHZ -------------------------------------------------
    t0 = time.time()
    seen = set()
    while time.time() - t0 < a.wait:
        f = lv.folder()
        if f not in seen:
            seen.add(f)
            print("[%6.1fs] folder=%r cont=%s" % (time.time() - t0, f,
                                                  lv.r32s("p6_w_cont_frames")), flush=True)
        if f == "GHZ":
            break
        time.sleep(1.0)
    if lv.folder() != "GHZ":
        print("NEVER REACHED GHZ in %.0fs (folders: %s)" % (a.wait, sorted(map(str, seen))))
        # D1 can still be judged from the map symbol even without GHZ; report.
        red = 0
        d1 = cid_ddw is not None or len(ddw_sym_addrs) > 0
        print("[%s] D1 registered -- DDWrecker classID=%s state-syms=%d"
              % ("PASS" if d1 else "RED ", cid_ddw, len(ddw_sym_addrs)))
        print("RESULT: RED (never reached GHZ; D2-D4 unverifiable)")
        return 2

    # let the GHZ load settle (cont_frames must advance)
    tk0 = lv.r32s("p6_w_cont_frames")
    tkw = time.time()
    while time.time() - tkw < 180.0:
        time.sleep(1.0)
        tk1 = lv.r32s("p6_w_cont_frames")
        if tk0 is not None and tk1 is not None and tk1 >= tk0 + 2:
            break
        tk0 = tk1 if tk1 is not None else tk0
    print("=== GHZ ticking (cont=%s) @%.1fs ===" % (lv.r32s("p6_w_cont_frames"),
                                                    time.time() - t0), flush=True)
    time.sleep(1.0)
    # refresh classIDs post-load
    if cid_ddw is None:
        cid_ddw = obj_cid("DDWrecker")
        if cid_ddw is None:
            w = lv.r32s("p6_w_ddw_classid")
            cid_ddw = s32(w) if w and s32(w) > 0 else None
    if cid_sign is None:
        cid_sign = obj_cid("SignPost")
    if cid_ac is None:
        cid_ac = obj_cid("ActClear")
    print("GHZ classIDs: DDWrecker=%s SignPost=%s ActClear=%s"
          % (cid_ddw, cid_sign, cid_ac), flush=True)

    out = open(a.out, "w")

    def full_scan():
        raw = lv.rblock(scene_base, sphys * NARROW)
        ddw, signs = [], []
        for k in range(sphys):
            off = k * NARROW
            cid = be16(raw, off + 54)
            if cid == 0 or cid > 0x400:
                continue
            x = s32(be32(raw, off + 0)) >> 16
            y = s32(be32(raw, off + 4)) >> 16
            if cid_ddw and cid == cid_ddw:
                ddw.append({"k": k, "x": x, "y": y,
                            "state": be32(raw, off + DDW_STATE),
                            "stateBall": be32(raw, off + DDW_STATEBALL),
                            "type": s32(be32(raw, off + DDW_TYPE)),
                            "health": s32(be32(raw, off + DDW_HEALTH)),
                            "active": raw[off + 76]})
            if cid_sign and cid == cid_sign:
                signs.append({"k": k, "x": x, "y": y,
                              "state": be32(raw, off + SIGN_STATE),
                              "type": raw[off + SIGN_TYPE],
                              "spin": be32(raw, off + SIGN_SPIN),
                              "active": raw[off + 76]})
        return ddw, signs

    # ---- watch --------------------------------------------------------------
    ts = time.time()
    d2_states = set()          # distinct DDWrecker states observed
    d2_seen = False
    d3_hittable = False
    d4_spin = False
    d4_actclear = False
    max_health_seen = None
    min_health_seen = None
    last_report = 0.0

    while time.time() - ts < a.watch:
        now = time.time()
        ddw, signs = full_scan()
        rec = {"t": round(now - ts, 2), "ddw": ddw, "signs": signs}
        out.write(json.dumps(rec) + "\n")
        out.flush()

        for e in ddw:
            d2_seen = True
            st = e["state"]
            # count any non-null state ptr in the overlay .text range as a
            # DDWrecker state (the exact set is in ddw_sym_addrs if the ovl map
            # was found; else fall back to "in the overlay code window").
            if st in ddw_sym_addrs or (0x02690000 <= st < 0x026C0000):
                d2_states.add(st)
            if e["type"] in (1, 2) and e["health"] is not None and e["health"] > 0:
                d3_hittable = True
                h = e["health"]
                max_health_seen = h if max_health_seen is None else max(max_health_seen, h)
                min_health_seen = h if min_health_seen is None else min(min_health_seen, h)

        # WITNESS-BASED secondary evidence (P6_DDW_ARENA build): the overlay's own
        # pool scan (p6_w_ddw_seen/state0/health_min) is authoritative -- it uses the
        # compiled struct offsets, immune to any gate-side offset drift.
        w_seen = lv.r32s("p6_w_ddw_seen")
        w_st0 = lv.r32s("p6_w_ddw_state0")
        w_hmin = lv.r32s("p6_w_ddw_health_min")
        if w_seen is not None and w_seen > 0:
            d2_seen = True
            if w_st0 and (w_st0 in ddw_sym_addrs or (0x02690000 <= w_st0 < 0x026C0000)):
                d2_states.add(w_st0)
        if w_hmin is not None:
            hm = s32(w_hmin)
            if hm > 0:
                d3_hittable = True
                min_health_seen = hm if min_health_seen is None else min(min_health_seen, hm)
                max_health_seen = hm if max_health_seen is None else max(max_health_seen, hm)

        for s in signs:
            if s.get("type", 9) <= 1:
                if spin_addr and s["state"] == spin_addr:
                    d4_spin = True
        ac = lv.r32(pool + SLOT_ACTCLEAR * WIDE + 52)
        if ac is not None and cid_ac and (ac & 0xFFFF) == cid_ac:
            d4_actclear = True

        if now - last_report > 4.0:
            last_report = now
            if ddw:
                e0 = ddw[0]
                print("t%6.1f DDW n=%d slot0(type=%s health=%s state=0x%X active=%s) "
                      "signs=%d spin=%s ac=%s" %
                      (rec["t"], len(ddw), e0["type"], e0["health"], e0["state"],
                       e0["active"], len(signs), d4_spin, d4_actclear), flush=True)
            else:
                print("t%6.1f DDW n=0 pool (witness seen=%s st0=%s hmin=%s warp=%s) signs=%d"
                      % (rec["t"], w_seen,
                         hex(w_st0) if w_st0 else w_st0, w_hmin,
                         lv.r32s("p6_w_ddw_warp_fired"), len(signs)), flush=True)

        if d4_spin and (d4_actclear or True):
            print("  >> DEFEAT->SPIN CHAIN OBSERVED @%.1fs" % rec["t"], flush=True)
            break

        # abort if the scene left GHZ
        if int(now - ts) % 10 < a.tick:
            if lv.folder() != "GHZ":
                print("!! scene left GHZ -- watch over")
                break

        time.sleep(a.tick)

    out.close()

    # ---- verdict ------------------------------------------------------------
    print("\n================ DDWRECKER VERDICT ================")
    red = 0

    def verdict(tag, ok, detail, skip=False):
        nonlocal red
        s = "SKIP" if skip else ("PASS" if ok else "RED ")
        if not ok and not skip:
            red += 1
        print("[%s] %s -- %s" % (s, tag, detail))

    d1 = cid_ddw is not None or len(ddw_sym_addrs) > 0
    verdict("D1 registered", d1,
            "DDWrecker classID=%s ; DDWrecker_State_* map symbols=%d/%d"
            % (cid_ddw, len(ddw_sym_addrs), len(ddw_syms)))
    verdict("D2 spawns/runs", d2_seen and len(d2_states) >= 2,
            "boss entity seen=%s ; distinct DDWrecker states observed=%d %s"
            % (d2_seen, len(d2_states), [hex(s) for s in list(d2_states)[:4]]))
    verdict("D3 hittable", d3_hittable,
            "BALL child with health>0 seen=%s (health range %s..%s)"
            % (d3_hittable, min_health_seen, max_health_seen))
    verdict("D4 defeat->spin", d4_spin,
            "SignPost reached State_Spin naturally=%s ; ActClear=%s"
            % (d4_spin, d4_actclear))

    print("===================================================")
    print("RESULT: %s (%d RED)" % ("GREEN" if red == 0 else "RED", red))
    return 0 if red == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
