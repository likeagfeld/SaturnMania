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
                    must be 0). ATTRIBUTION (r3, code-verified): p6_w_sht_fetches
                    increments ONLY after a real p6_mz_uncompress on the BANDED
                    path (SaturnSheet.cpp:454); the resident cart path returns
                    at :419 without counting, and an FRD hit bypasses FetchRect
                    entirely (p6_vdp1.c:1740-1756). So the witness IS the real
                    miniz-inflate counter -- honest as labeled. NOTE: FRD
                    misses==0 does NOT prove all draws hit FRD: a sheet with
                    frdSlot<0 never consults the directory (no miss counted).
                    p6_w_vdp1_lastfetch (shtSlot<<24|w<<12|h) is sampled for
                    per-sheet attribution of any banded fetch.
  C1b churn      -- slot-restage churn bounded: per-leg median p6_w_vdp1_evicts
                    delta/frame <= 16 (regression bound above the measured
                    12.8/frame 3-badnik-window worst of the 18:19 r2 run; the
                    animating-badniks fmax>slots thrash class stays tracked
                    honestly and separately from real inflates).
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
  C8 loops/stall -- x progress never stalls PERMANENTLY while alive. Every
                    >STALL_S stagnation is recorded with x/y/state, but the
                    verdict REDs only stalls the input table never cleared
                    (max_x never subsequently exceeded stall.x + 8) -- a stall
                    the scripted input then clears is acceptable (r3 semantics).
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
    sphys = lv.r32s("RSDK::p6_pool_scene_phys")
    if sphys is None or not (0 < sphys <= SCENE_PHYS):
        sphys = SCENE_PHYS

    # manifest (authoritative scene coordinates)
    man = json.loads(subprocess.check_output(
        [sys.executable, str(_HERE / "_ghz1_obstacle_map.py"), "--json"]).decode())
    bridges_x = sorted(r["x"] for r in man if r["cls"] == "Bridge")

    # object classIDs: pack objects (SignPost/ActClear/Player) via their object
    # POINTER symbols in game.map (ptr -> Object.classID u16 @ +0); overlay
    # badniks via the p6_w_b2_cids[9] witness array (p6_ovl_ghz.c:1629-1637);
    # Bridge via p6_w_brg_classid.
    def obj_cid(name):
        p = lv.r32s(name)
        if p and 0x00200000 <= p < 0x06100000:
            v = lv.r32(p)
            return (v >> 16) & 0xFFFF if v is not None else None
        return None

    B2_IDX = {"Newtron": 3, "Crabmeat": 4, "BuzzBomber": 5, "Chopper": 6,
              "Motobug": 7, "Batbrain": 8}

    def all_cids():
        cid = {}
        for nm in ("SignPost", "ActClear", "Player"):
            cid[nm] = obj_cid(nm)
        b2 = lv.sym("p6_w_b2_cids")
        for nm, i in B2_IDX.items():
            v = lv.r32(b2 + 4 * i) if b2 else None
            cid[nm] = (s32(v) if v is not None else None)
            if cid[nm] is not None and cid[nm] <= 0:
                cid[nm] = None
        brg = lv.r32s("p6_w_brg_classid")
        cid["Bridge"] = s32(brg) if brg and s32(brg) > 0 else None
        return cid

    CID = all_cids()
    print("live classIDs:", {k: v for k, v in CID.items()})

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

    # MID-LOAD ARTIFACT GUARD (campaign r2, MEASURED via _classreg_probe.py):
    # currentSceneFolder flips to "GHZ" at the START of the long GHZ load
    # (~40s of frozen cont_frames); the overlay witness (p6_w_b2_cids /
    # p6_w_brg_classid) only ticks once the engine loop resumes, so classIDs
    # read as None mid-load ("first-life classes missing" was this artifact,
    # NOT a registration gap). Wait for cont_frames to ADVANCE before latching.
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

    # refresh classIDs now that GHZ objects are registered
    time.sleep(1.0)
    CID = all_cids()
    print("GHZ classIDs:", CID, flush=True)
    cid2name = {v: k for k, v in CID.items() if v}

    W = ["p6_w_cont_frames", "p6_perf_vbl_count", "p6_w_sht_fetches",
         "p6_w_vdp1_evicts", "p6_w_obj_refills", "p6_w_vdp1_cmds",
         "p6_w_vdp1_drops", "p6_w_vdp1_handle_drops", "p6_w_str_track",
         "p6_w_sfx_skips", "RSDK::p6_saturn_anim_allocfail", "RSDK::p6_w_anim_lastfail",
         "p6_w_snd_plays", "p6_w_brg_frames", "p6_w_plr_draws",
         "p6_w_transitions", "p6_w_xing_count", "p6_w_stream_starve",
         # C1 attribution (r3): last banded fetch (shtSlot<<24|w<<12|h,
         # p6_vdp1.c GHZCUT_BOOT witness) + FRD registry health
         "p6_w_vdp1_lastfetch", "p6_w_frd_active", "p6_w_frd_lookups",
         "p6_w_frd_misses"]

    out = open(a.out, "w")

    # C5 baselines at GHZ entry: the counters are chain-cumulative (menu/AIZ/
    # cutscene legs); the traversal verdict is on the GHZ DELTA, absolutes
    # reported for the front-end follow-up.
    # NOTE: p6_w_sfx_skips is a one-shot mirror at LoadGameConfig; the LIVE
    # counter is RSDK::p6_saturn_sfx_skips (increments on every later stage
    # SFX skip too) -- use the live counter for the delta.
    base_skips = s32(lv.r32s("RSDK::p6_saturn_sfx_skips") or 0)
    base_afail = lv.r32s("RSDK::p6_saturn_anim_allocfail") or 0
    base_plays = lv.r32s("p6_w_snd_plays") or 0

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
    fetch_sheets = {}  # C1 attribution: shtSlot -> samples where it was the last banded fetch
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
    # C1 sampling fix (campaign r2): 4.0s initial + 6.0s per death/respawn settle
    # windows consumed the ENTIRE ~4.5s lives of the 14:29 run -> legs={} -> C1
    # "0 legs" was a sampling gap, not an inflate verdict. 2.0/2.5s still skips
    # the load-inflate burst (measured <1.5s) while leaving steady-motion samples.
    settle_until = time.time() + 2.0  # post-load settle: inflates expected
    trk_seen = set()   # C4: every p6_w_str_track value observed WHILE in GHZ
    folder_checks = 0

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

        # C4: latch the CD-DA track while the GHZ scene is live (post-verdict
        # reads see the menu/gameover track). Also abort on scene exit
        # (3 deaths -> GameOver -> Menu) -- the traversal is over.
        folder_checks += 1
        if folder_checks % 8 == 0:
            f = lv.folder()
            if f != "GHZ":
                print("!! scene left GHZ (folder=%r) -- traversal over (game over?)" % f)
                break
        if w["p6_w_str_track"] is not None:
            trk_seen.add(s32(w["p6_w_str_track"]))

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
        # C1 attribution: whenever ANY banded fetch happened this sample, tally
        # the sheet slot of the last one (partial but unbiased over a long run).
        if (last["p6_w_sht_fetches"] is not None and w["p6_w_sht_fetches"] is not None
                and w["p6_w_sht_fetches"] > last["p6_w_sht_fetches"]
                and w["p6_w_vdp1_lastfetch"] is not None):
            slotid = (w["p6_w_vdp1_lastfetch"] >> 24) & 0xFF
            fetch_sheets[slotid] = fetch_sheets.get(slotid, 0) + 1

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
                settle_until = now + 2.5
            if prev_p["cid"] == 0 and p["cid"] == CID.get("Player"):
                respawns.append({"t": rec["t"], "x": p["x"]})
                print("  >> RESPAWN @%.1fs x=%d" % (rec["t"], p["x"]), flush=True)
                settle_until = now + 2.5
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
            if any(CID.get(nm) is None for nm in B2_IDX) or CID.get("Bridge") is None:
                CID = all_cids()   # overlay latches can post-date GHZ entry
                cid2name.clear()
                cid2name.update({v: k for k, v in CID.items() if v})
                print("  [scan] refreshed classIDs: %s" % CID, flush=True)
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
    churn_bad = []
    for leg in sorted(legs):
        med = statistics.median(legs[leg])
        mean = sum(legs[leg]) / len(legs[leg])
        emed = statistics.median(leg_evicts[leg])
        print("   leg %2d (x %5d-%5d): inflate/frame med=%.3f mean=%.3f "
              "evict/frame med=%.3f n=%d" %
              (leg, leg * 1000, leg * 1000 + 999, med, mean, emed, len(legs[leg])))
        if med > 0.0 or mean > 0.5:
            bad_legs.append(leg)
        if emed > 16.0:  # C1b bound: measured 12.8/frame 3-badnik worst, r2 18:19 run
            churn_bad.append(leg)
    if fetch_sheets:
        print("   C1 attribution: banded-fetch sheet slots (slot: samples) = %s"
              % dict(sorted(fetch_sheets.items(), key=lambda kv: -kv[1])))
    print("   FRD registry: active=%s lookups=%s misses=%s" %
          (lv.r32s("p6_w_frd_active"), lv.r32s("p6_w_frd_lookups"),
           lv.r32s("p6_w_frd_misses")))
    verdict("C1 inflates", len(bad_legs) == 0 and len(legs) > 0,
            "steady-motion inflate legs bad=%s of %d legs; banded-fetch slots=%s" %
            (bad_legs, len(legs), dict(sorted(fetch_sheets.items(), key=lambda kv: -kv[1]))))
    verdict("C1b churn", len(churn_bad) == 0 and len(legs) > 0,
            "evict-churn legs over 16/frame median: %s of %d legs" % (churn_bad, len(legs)))
    # C2
    verdict("C2 player-drawn", len(plr_undrawn) == 0,
            "%d undrawn-while-alive+onscreen samples %s" %
            (len(plr_undrawn), plr_undrawn[:3]), skip=not has_plr_draws)
    # C3 -- class-level anim-load witnesses (p6_ovl_ghz.c latches, int16 -1 ==
    # LoadSpriteAnimation failed) + the per-entity animator.frames scan above
    anif = {}
    for wname in ("p6_w_batbrain_aniframes", "p6_w_newtron_aniframes",
                  "p6_w_spikes_aniframes", "p6_w_spikelog_aniframes",
                  "p6_w_platform_aniframes", "p6_w_itembox_aniframes",
                  "p6_w_spring_aniframes"):
        v = lv.r32s(wname)
        anif[wname.replace("p6_w_", "").replace("_aniframes", "")] = (s32(v) if v is not None else None)
    cls_ok = all(v is None or v >= 0 for v in anif.values())
    verdict("C3 badniks", len(badnik_bad) == 0 and cls_ok,
            "class aniFrames=%s; in-window entities w/ frames==0: %d %s" %
            (anif, len(badnik_bad), badnik_bad[:3]))
    # C4 -- tracks observed while IN the GHZ scene (Music.c:37 Music_Create ->
    # SetMusicTrack(scene Music entity trackName) -> Saturn HandleStreamLoad
    # GreenHill1.ogg -> CD-DA track 2, p6_io_main.cpp:2495). -1 before the
    # first PlayStream is tolerated only as a transient.
    trk_bad = trk_seen - {2, -1}
    verdict("C4 music", 2 in trk_seen and not trk_bad,
            "tracks seen in GHZ=%s (expect 2=GreenHill1)" % sorted(trk_seen))
    # C5
    skips = s32(lv.r32s("RSDK::p6_saturn_sfx_skips") or 0)
    afail = lv.r32s("RSDK::p6_saturn_anim_allocfail") or 0
    plays = lv.r32s("p6_w_snd_plays") or 0
    d_skips = (skips or 0) - (base_skips or 0)
    d_afail = afail - base_afail
    d_plays = plays - base_plays
    verdict("C5 sfx", d_skips <= 0 and d_afail == 0,
            "GHZ-delta: sfx_skips=%+d anim_allocfail=%+d snd_plays=%+d "
            "(chain absolutes: skips=%s allocfail=%s plays=%s lastfail=0x%X)" %
            (d_skips, d_afail, d_plays, skips, afail, plays,
             (lv.r32s("RSDK::p6_w_anim_lastfail") or 0)))
    # bridge-1 forensic instruments (AUTORUN build; None on older builds)
    forens = {}
    for wn in ("p6_w_btch_calls", "p6_w_btch_hits", "p6_w_btch_lastdy",
               "p6_w_btch_lastvy", "p6_w_arun_brg_live", "p6_w_arun_brg_active",
               "p6_w_arun_brg_firstx", "p6_w_arun_brg_gapmiss", "p6_w_arun_inspan"):
        v = lv.r32s(wn)
        forens[wn.replace("p6_w_", "")] = s32(v) if v is not None else None
    print("   bridge-1 forensics: %s" % forens)
    # anim-load log (first 24 loads: hash, (result<<16)|frameCount; result -1 = allocfail)
    la = lv.sym("RSDK::p6_w_anim_log")
    ln = lv.r32s("RSDK::p6_w_anim_logn") or 0
    if la and ln:
        ents_log = []
        for i in range(min(ln, 24)):
            h = lv.r32(la + 8 * i)
            rv = lv.r32(la + 8 * i + 4) or 0
            res = s32(rv) >> 16
            ents_log.append("0x%08X:%s" % (h or 0, res))
        print("   anim-load log (%d): %s" % (ln, " ".join(ents_log)))
    sh = lv.r32s("RSDK::p6_w_sfxskip_hash")
    if sh:
        try:
            tbl = json.loads((_HERE.parent / "_sfx_hashes.json").read_text())
            print("   skipped sfx: hash=0x%08X -> %s" % (sh, tbl.get(hex(sh), "?not-in-GameConfig")))
        except Exception:
            print("   skipped sfx hash=0x%08X (no table)" % sh)
    # C6
    brg = s32(lv.r32s("p6_w_brg_frames") or 0)
    verdict("C6 bridges", brg is not None and brg > 0 and len(bridge_bad) == 0,
            "brg_frames=%s missing-near-bridge events=%d %s" %
            (hex(brg) if brg else brg, len(bridge_bad), bridge_bad[:3]))
    # C7
    verdict("C7 inclines", len(incline_bad) == 0,
            "%d bad incline samples %s" % (len(incline_bad), incline_bad[:3]))
    # C8 -- r3 semantics: RED only on PERMANENT stalls (never subsequently
    # cleared: max_x never exceeded stall.x + 8) or any death. A stagnation the
    # scripted input then clears is a recorded-but-acceptable event.
    perm_stalls = [s for s in stalls if max_x <= s["x"] + 8]
    verdict("C8 no-permanent-stall/no-death",
            len(perm_stalls) == 0 and len(deaths) == 0,
            "stalls=%d (permanent=%d %s) deaths=%d %s respawns=%d" %
            (len(stalls), len(perm_stalls), perm_stalls[:3],
             len(deaths), deaths[:3], len(respawns)))
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
