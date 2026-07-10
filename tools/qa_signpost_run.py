#!/usr/bin/env python3
"""qa_signpost_run.py -- LIVE-MEMORY traversal invariant gate for the GHZ1
boot->signpost campaign (2026-07-10). NO pixels, NO screenshots: every check is
proven from live Saturn memory reads (RetroArch Beetle Saturn UDP 55355, the
qa_live.ps1 harness) + shipping witnesses + the scene manifest.

Boot first:  pwsh tools/qa_live.ps1 -NoCdda -NoMonitor      (autorun chain build)
Then:        python tools/qa_signpost_run.py [--wait 420] [--watch 900]

The 9 checks (mission classes):
  C1 inflates    -- steady-motion per-frame miniz inflates == 0 on every 1000px
                    leg (p6_w_sht_fetches delta / logic frame; median per leg
                    must be 0); deflate/evict churn bounded (p6_w_vdp1_evicts).
  C2 player draw -- player never undrawn while alive+on-screen
                    (p6_w_plr_draws delta vs p6_w_cont_frames delta; AUTORUN
                    witness). SKIP if the symbol is absent (older build).
  C3 badniks     -- every badnik-class entity inside the camera window has its
                    animator.frames bound (per-class entity offsets from
                    tools/_offsets_probe.c) + class aniFrames != 0xFFFF.
  C4 music       -- p6_w_str_track == 2 (GreenHill1.ogg -> CD-DA track 2 per
                    AudioDevice::HandleStreamLoad, p6_io_main.cpp:2495; decomp
                    Music.c GHZ track table / GHZSetup.c StageLoad).
  C5 sfx         -- p6_w_sfx_skips <= 0 (no LoadSfx alloc-guard skips),
                    p6_saturn_anim_allocfail == 0, p6_w_snd_plays > 0.
  C6 bridges     -- p6_w_brg_frames > 0 (Bridge.bin staged) + a live Bridge
                    entity present whenever the player is within 400px of an
                    authored bridge x (manifest: Scene1.bin).
  C7 inclines    -- while onGround && angle != 0 && alive: animator.frames
                    != 0, visible == 1, and (if C2 witness present) the player
                    draw delta > 0 across the incline samples.
  C8 loops/stall -- x progress never stalls > STALL_S seconds while alive
                    (loop clip-through / PlaneSwitch failure shows as a stall
                    or a death; every stall is reported with x/y/state).
  C9 signpost    -- player reaches signpost x (manifest 15792), the RUNPAST/
                    DROP SignPost entity's active flips ACTIVE_BOUNDS(4) ->
                    ACTIVE_NORMAL(2) (SignPost_CheckTouch, SignPost.c:326),
                    its state hits SignPost_State_Spin (SignPost.c:416), and
                    ActClear appears at SLOT_ACTCLEAR=16 (SignPost.c:452).

Pool geometry (Object.hpp, verified against the live map):
  base = *RSDK::objectEntityList (0x00243000)
  reserve  slots [0,64)    stride 556
  scene    phys  [0,1088)  stride 344 at base + 64*556
  temp     slots [1152,1216) stride 556 after the scene region
Entity field offsets: classID @52(dword,lo16 after BE swap)=struct+54; pos @0/4;
angle @32; groundVel @44; onGround @72; visible @85; onScreen @86; state @88;
player animator @104. Per-class animator offsets compiled from the census
Game.h by tools/_offsets_probe.c (2026-07-10):
  Motobug 116  Crabmeat 112  Newtron 108  Chopper 112  Batbrain 108
  BuzzBomber 120  Bridge 120  SignPost: state 88 type 92 spinCount 128
Object-struct aniFrames offsets: Motobug/Batbrain 12, Crabmeat/BuzzBomber 20,
Newtron/Chopper 36, Bridge 4.

Exit: 0 = all checks PASS/SKIP, 1 = any RED, 2 = harness failure.
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

# ---- pool geometry (Object.hpp) --------------------------------------------
RESERVE = 64
SCENE_PHYS = 1088
WIDE = 556
NARROW = 344
SLOT_ACTCLEAR = 16

# per-class entity animator offsets (tools/_offsets_probe.c output, 2026-07-10)
BADNIK_ANIM_OFF = {
    "Motobug": 116, "Crabmeat": 112, "Newtron": 108,
    "Chopper": 112, "Batbrain": 108, "BuzzBomber": 120,
}
BRIDGE_ANIM_OFF = 120
SIGN_STATE_OFF, SIGN_TYPE_OFF, SIGN_SPIN_OFF = 88, 92, 128
# object-struct aniFrames offsets (same probe)
BADNIK_OBJ_ANIF = {
    "Motobug": 12, "Crabmeat": 20, "Newtron": 36,
    "Chopper": 36, "Batbrain": 12, "BuzzBomber": 20,
}

SIGNPOST_X = 15792  # manifest (tools/_ghz1_obstacle_map.py, Scene1.bin)


def s32(v):
    return v - 0x100000000 if v is not None and v >= 0x80000000 else v


class Live:
    def __init__(self, host, port, map_path):
        self.mem = qa_netmem.RetroMem(host, port, 2.0)
        self.map_text = Path(map_path).read_text(errors="replace")
        self._symcache = {}

    def sym(self, name):
        if name in self._symcache:
            return self._symcache[name]
        m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$",
                      self.map_text, re.M)
        a = int(m.group(1), 16) if m else None
        self._symcache[name] = a
        return a

    def r32(self, addr):
        try:
            return self.mem.read32_saturn(addr)
        except Exception:
            return None

    def r32s(self, name):
        a = self.sym(name)
        return self.r32(a) if a else None

    def r16(self, addr):
        try:
            return self.mem.read16_saturn(addr)
        except Exception:
            return None

    def rblock(self, addr, n, chunk=2000):
        out = bytearray()
        a = addr
        left = n
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
    ap.add_argument("--wait", type=float, default=420.0, help="s to reach GHZ")
    ap.add_argument("--watch", type=float, default=900.0, help="s of traversal watch")
    ap.add_argument("--tick", type=float, default=0.35)
    ap.add_argument("--scan-period", type=float, default=10.0)
    ap.add_argument("--stall-s", type=float, default=12.0)
    ap.add_argument("--out", default="_signpost_run.jsonl")
    a = ap.parse_args(argv)

    lv = Live(a.host, a.port, a.map)

    # ---- harness health (binding: fail LOUD, never trust garbage) ----------
    pool = lv.r32s("RSDK::objectEntityList")
    if pool is None or not (0x00200000 <= pool < 0x00300000):
        print("HARNESS UNHEALTHY: objectEntityList=%s not a WRAM-L pool ptr; "
              "core stuck at boot or mapping wrong. Reboot qa_live.ps1 and retry."
              % (hex(pool) if pool else pool))
        return 2
    scene_base = pool + RESERVE * WIDE
    sphys = lv.r32s("RSDK::p6_pool_scene_phys") or lv.r32s("p6_pool_scene_phys")
    if sphys is None or not (0 < sphys <= SCENE_PHYS):
        sphys = SCENE_PHYS

    # manifest (authoritative scene coordinates)
    man = json.loads(subprocess.check_output(
        [sys.executable, str(_HERE / "_ghz1_obstacle_map.py"), "--json"]).decode())
    bridges_x = sorted(r["x"] for r in man if r["cls"] == "Bridge")

    # object classIDs from the live object pointers (map symbol -> ptr -> u16)
    def obj_cid(name):
        p = lv.r32s(name)
        if p and 0x00200000 <= p < 0x06100000:
            v = lv.r32(p)
            return (v >> 16) & 0xFFFF if v is not None else None
        return None

    CID = {}
    for nm in ("Motobug", "Crabmeat", "Newtron", "Chopper", "Batbrain",
               "BuzzBomber", "Bridge", "SignPost", "ActClear", "Player"):
        CID[nm] = obj_cid(nm)
    print("live classIDs:", {k: v for k, v in CID.items()})

    def obj_aniframes(name):
        p = lv.r32s(name)
        off = BADNIK_OBJ_ANIF.get(name, None)
        if p and off is not None:
            return lv.r16(p + off)
        return None

    spin_addr = lv.sym("SignPost_State_Spin")
    has_plr_draws = lv.sym("p6_w_plr_draws") is not None

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
        return 2
    print("=== GHZ reached @%.1fs; watching traversal ===" % (time.time() - t0), flush=True)

    # refresh classIDs now that GHZ objects are registered
    for nm in list(CID):
        CID[nm] = obj_cid(nm)
    print("GHZ classIDs:", CID, flush=True)
    cid2name = {v: k for k, v in CID.items() if v}

    W = ["p6_w_cont_frames", "p6_perf_vbl_count", "p6_w_sht_fetches",
         "p6_w_vdp1_evicts", "p6_w_obj_refills", "p6_w_vdp1_cmds",
         "p6_w_vdp1_drops", "p6_w_vdp1_handle_drops", "p6_w_str_track",
         "p6_w_sfx_skips", "p6_saturn_anim_allocfail", "p6_w_anim_lastfail",
         "p6_w_snd_plays", "p6_w_brg_frames", "p6_w_plr_draws",
         "p6_w_transitions", "p6_w_xing_count", "p6_w_stream_starve"]

    out = open(a.out, "w")

    def player():
        b = pool
        raw = lv.rblock(b, 120)
        if raw is None or len(raw) < 120:
            return None
        return {
            "cid": be16(raw, 54), "x": s32(be32(raw, 0)) >> 16,
            "y": s32(be32(raw, 4)) >> 16, "angle": s32(be32(raw, 32)),
            "gvel": s32(be32(raw, 44)), "onG": be32(raw, 72),
            "vis": raw[85], "onScr": raw[86], "state": be32(raw, 88),
            "aframes": be32(raw, 104), "animID": be16(raw, 112),
        }

    def camera_x():
        # camera entity classID==6 in the reserve region (measured convention)
        for sl in range(RESERVE):
            b = pool + sl * WIDE
            v = lv.r32(b + 52)
            if v is not None and (v & 0xFFFF) == 6:
                px = lv.r32(b + 0)
                return s32(px) >> 16 if px is not None else None
        return None

    signpost_rec = {}

    def full_scan():
        """Bulk-read the narrow scene region; return per-class entity lists."""
        ents = []
        raw = lv.rblock(scene_base, sphys * NARROW)
        for k in range(sphys):
            off = k * NARROW
            cid = be16(raw, off + 54)
            if cid == 0 or cid > 0x400:
                continue
            x = s32(be32(raw, off + 0)) >> 16
            y = s32(be32(raw, off + 4)) >> 16
            nm = cid2name.get(cid)
            # Entity.active = uint8 @ +76 (after onGround bool32 @72..75;
            # then filter 77, direction 78, drawGroup 79 -- Object.hpp REV02)
            e = {"k": k, "cid": cid, "cls": nm, "x": x, "y": y,
                 "active": raw[off + 76], "vis": raw[off + 85]}
            if nm in BADNIK_ANIM_OFF:
                e["aframes"] = be32(raw, off + BADNIK_ANIM_OFF[nm])
            if nm == "Bridge":
                e["aframes"] = be32(raw, off + BRIDGE_ANIM_OFF)
            if nm == "SignPost":
                e["type"] = raw[off + SIGN_TYPE_OFF]
                e["state"] = be32(raw, off + SIGN_STATE_OFF)
                e["spin"] = be32(raw, off + SIGN_SPIN_OFF)
            ents.append(e)
        return ents

    # ---- traversal watch ----------------------------------------------------
    ts = time.time()
    last = {k: None for k in W}
    prev_p = None
    legs = {}          # leg_index -> list of (d_fetch per d_cont)
    leg_evicts = {}
    incline_bad = []
    plr_undrawn = []
    badnik_bad = []
    bridge_bad = []
    stalls = []
    deaths = []
    respawns = []
    max_x = -1
    stall_t0 = time.time()
    stall_x = None
    sign_seen = {"crossed": False, "spin": False, "actclear": False, "reached": False}
    last_scan = 0.0
    settle_until = time.time() + 4.0  # post-load settle: inflates expected

    while time.time() - ts < a.watch:
        now = time.time()
        p = player()
        w = {k: lv.r32s(k) for k in W}
        cam = None
        if p is None or w["p6_w_cont_frames"] is None:
            print("!! live read failed (RA flake?) -- aborting sample loop")
            break
        rec = {"t": round(now - ts, 2), "p": p,
               "w": {k: (s32(w[k]) if w[k] is not None else None) for k in W}}
        out.write(json.dumps(rec) + "\n")
        out.flush()

        alive = p["cid"] == CID.get("Player")
        dcont = None
        if last["p6_w_cont_frames"] is not None and w["p6_w_cont_frames"] is not None:
            dcont = w["p6_w_cont_frames"] - last["p6_w_cont_frames"]

        # C1 per-leg inflate rate (only in steady motion: alive, moving, settled)
        if (alive and dcont and dcont > 0 and now > settle_until
                and prev_p and p["x"] > prev_p["x"]):
            df = (w["p6_w_sht_fetches"] or 0) - (last["p6_w_sht_fetches"] or 0)
            de = (w["p6_w_vdp1_evicts"] or 0) - (last["p6_w_vdp1_evicts"] or 0)
            leg = p["x"] // 1000
            legs.setdefault(leg, []).append(df / dcont)
            leg_evicts.setdefault(leg, []).append(de / dcont)

        # C2/C7 player drawn
        if (alive and dcont and dcont > 0 and p["vis"] == 1 and p["onScr"] == 1
                and has_plr_draws and w["p6_w_plr_draws"] is not None
                and last["p6_w_plr_draws"] is not None):
            dpd = w["p6_w_plr_draws"] - last["p6_w_plr_draws"]
            if dpd == 0:
                ev = {"t": rec["t"], "x": p["x"], "y": p["y"], "angle": p["angle"],
                      "animID": p["animID"], "state": p["state"], "dcont": dcont}
                plr_undrawn.append(ev)
                if p["angle"] != 0:
                    incline_bad.append(ev)
        if alive and p["onG"] and p["angle"] != 0:
            if p["aframes"] == 0 or p["vis"] == 0:
                incline_bad.append({"t": rec["t"], "x": p["x"], "angle": p["angle"],
                                    "aframes": p["aframes"], "vis": p["vis"]})

        # C8 stall detection
        if alive:
            if stall_x is None or p["x"] > stall_x + 4:
                stall_x = p["x"]
                stall_t0 = now
            elif now - stall_t0 > a.stall_s:
                stalls.append({"t": rec["t"], "x": p["x"], "y": p["y"],
                               "state": p["state"], "animID": p["animID"],
                               "gvel": p["gvel"], "onG": p["onG"]})
                print("  >> STALL @%.1fs x=%d y=%d state=0x%X anim=%d" %
                      (rec["t"], p["x"], p["y"], p["state"], p["animID"]), flush=True)
                stall_t0 = now
        # death / respawn edges
        if prev_p is not None:
            if prev_p["cid"] == CID.get("Player") and p["cid"] == 0:
                deaths.append({"t": rec["t"], "x": prev_p["x"], "y": prev_p["y"]})
                print("  >> DEATH @%.1fs (%d,%d)" % (rec["t"], prev_p["x"], prev_p["y"]), flush=True)
                settle_until = now + 6.0
            if prev_p["cid"] == 0 and p["cid"] == CID.get("Player"):
                respawns.append({"t": rec["t"], "x": p["x"]})
                print("  >> RESPAWN @%.1fs x=%d" % (rec["t"], p["x"]), flush=True)
                settle_until = now + 6.0
                stall_x = None

        if alive and p["x"] > max_x:
            max_x = p["x"]

        # C9 signpost live probe once close
        if alive and p["x"] > SIGNPOST_X - 600:
            sign_seen["reached"] = sign_seen["reached"] or p["x"] >= SIGNPOST_X - 8
            for e in full_scan():
                if e["cls"] == "SignPost" and e.get("type", 9) <= 1:
                    signpost_rec.update(e)
                    if e["active"] == 2:
                        sign_seen["crossed"] = True
                    if spin_addr and e["state"] == spin_addr:
                        sign_seen["spin"] = True
            ac = lv.r32(pool + SLOT_ACTCLEAR * WIDE + 52)
            if ac is not None and (ac & 0xFFFF) != 0 and (ac & 0xFFFF) == CID.get("ActClear"):
                sign_seen["actclear"] = True
            if sign_seen["actclear"] and sign_seen["crossed"]:
                print("  >> SIGNPOST CHAIN COMPLETE @%.1fs" % rec["t"], flush=True)
                break

        # periodic structural scan (C3 badniks, C6 bridges)
        if now - last_scan > a.scan_period and alive:
            last_scan = now
            cam = camera_x()
            ents = full_scan()
            win_lo = (cam if cam is not None else p["x"]) - 260
            win_hi = (cam if cam is not None else p["x"]) + 260
            nbad = nbrg = 0
            for e in ents:
                if e["cls"] in BADNIK_ANIM_OFF and win_lo <= e["x"] <= win_hi:
                    nbad += 1
                    if e.get("aframes", 0) == 0:
                        badnik_bad.append({"t": rec["t"], **e})
                if e["cls"] == "Bridge":
                    nbrg += 1
            near_bridge = any(abs(p["x"] - bx) < 400 for bx in bridges_x)
            if near_bridge and nbrg == 0:
                bridge_bad.append({"t": rec["t"], "x": p["x"],
                                   "reason": "no live Bridge entity near authored bridge"})
            print("  [scan @%.1fs] x=%d cam=%s ents=%d badniks_in_win=%d bridges=%d" %
                  (rec["t"], p["x"], cam, len(ents), nbad, nbrg), flush=True)

        if int(now - ts) % 5 < a.tick and alive:
            print("t%6.1f x=%6d y=%5d cid=%s vis=%d onG=%d ang=%4d anim=%2d "
                  "fetch=%s evict=%s trk=%s" %
                  (rec["t"], p["x"], p["y"], p["cid"], p["vis"],
                   1 if p["onG"] else 0, p["angle"], p["animID"],
                   w["p6_w_sht_fetches"], w["p6_w_vdp1_evicts"], w["p6_w_str_track"]),
                  flush=True)

        last = w
        prev_p = p
        time.sleep(a.tick)

    out.close()

    # ---- verdict -------------------------------------------------------------
    print("\n================ SIGNPOST-RUN VERDICT ================")
    red = 0

    def verdict(tag, ok, detail, skip=False):
        nonlocal red
        s = "SKIP" if skip else ("PASS" if ok else "RED ")
        if not ok and not skip:
            red += 1
        print("[%s] %s -- %s" % (s, tag, detail))

    # C1
    import statistics
    bad_legs = []
    for leg in sorted(legs):
        med = statistics.median(legs[leg])
        mean = sum(legs[leg]) / len(legs[leg])
        emed = statistics.median(leg_evicts[leg])
        print("   leg %2d (x %5d-%5d): inflate/frame med=%.3f mean=%.3f "
              "evict/frame med=%.3f n=%d" %
              (leg, leg * 1000, leg * 1000 + 999, med, mean, emed, len(legs[leg])))
        if med > 0.0 or mean > 0.5:
            bad_legs.append(leg)
    verdict("C1 inflates", len(bad_legs) == 0 and len(legs) > 0,
            "steady-motion inflate legs bad=%s of %d legs" % (bad_legs, len(legs)))
    # C2
    verdict("C2 player-drawn", len(plr_undrawn) == 0,
            "%d undrawn-while-alive+onscreen samples %s" %
            (len(plr_undrawn), plr_undrawn[:3]), skip=not has_plr_draws)
    # C3
    anif = {nm: obj_aniframes(nm) for nm in BADNIK_OBJ_ANIF}
    cls_ok = all(v is None or v != 0xFFFF for v in anif.values())
    verdict("C3 badniks", len(badnik_bad) == 0 and cls_ok,
            "class aniFrames=%s; in-window entities w/ frames==0: %d %s" %
            (anif, len(badnik_bad), badnik_bad[:3]))
    # C4
    trk = lv.r32s("p6_w_str_track")
    verdict("C4 music", trk == 2, "p6_w_str_track=%s (expect 2=GreenHill1)" % trk)
    # C5
    skips = s32(lv.r32s("p6_w_sfx_skips") or 0)
    afail = lv.r32s("p6_saturn_anim_allocfail")
    plays = lv.r32s("p6_w_snd_plays")
    verdict("C5 sfx", (skips is None or skips <= 0) and (afail in (None, 0))
            and (plays or 0) > 0,
            "sfx_skips=%s anim_allocfail=%s snd_plays=%s" % (skips, afail, plays))
    # C6
    brg = s32(lv.r32s("p6_w_brg_frames") or 0)
    verdict("C6 bridges", brg is not None and brg > 0 and len(bridge_bad) == 0,
            "brg_frames=%s missing-near-bridge events=%d %s" %
            (hex(brg) if brg else brg, len(bridge_bad), bridge_bad[:3]))
    # C7
    verdict("C7 inclines", len(incline_bad) == 0,
            "%d bad incline samples %s" % (len(incline_bad), incline_bad[:3]))
    # C8
    verdict("C8 no-stall", len(stalls) == 0,
            "stalls=%d %s deaths=%d %s respawns=%d" %
            (len(stalls), stalls[:3], len(deaths), deaths[:3], len(respawns)))
    # C9
    verdict("C9 signpost", sign_seen["crossed"] and (sign_seen["spin"] or sign_seen["actclear"]),
            "max_x=%d reached=%s crossed=%s spin=%s actclear=%s rec=%s" %
            (max_x, sign_seen["reached"], sign_seen["crossed"], sign_seen["spin"],
             sign_seen["actclear"], signpost_rec))

    print("======================================================")
    print("RESULT: %s (%d RED)" % ("GREEN" if red == 0 else "RED", red))
    return 0 if red == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
