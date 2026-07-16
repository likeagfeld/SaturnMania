#!/usr/bin/env python3
"""qa_parity_oracle.py -- COMPREHENSIVE, decomp-derived whole-arc parity oracle.

BINDING PURPOSE (user, 2026-07-16): the oracle must NOT be scoped to the handful
of symptoms the user happened to list -- that list was EXAMPLES, not the spec.
The oracle derives "what MUST be true" from the DECOMP GROUND TRUTH for EVERY
object class and EVERY subsystem, then reports whatever diverges live -- so it
surfaces bugs neither the user nor the agent has named. NO pixels, NO visual
inspection ("you shouldn't need visual inspection to fix this with your memory
tools" -- correct). The agent owns bug discovery; the user must never enumerate.

Ground truth (all decomp-derived, not a symptom list):
  * docs/scene_objects.json  -> per-scene stage_config.objects (EVERY class the
    decomp registers for that stage) + sfx list. The manifest of what must exist.
  * the LIVE object-class REGISTRATION table (sceneInfo.classCount +
    stageObjectIDs @0x002FEF80 + objectClassList backing @0x060D8000, 72B/entry,
    name resolved by md5(objectName) -- the SAME idiom as _classreg_probe.py).
  * the LIVE entity pool (qa_trace generic walk) -> per-class instance counts,
    animID, visible, position, state.
  * per-class frame-count witnesses (p6_w_<class>_aniframes) -> valid animID range.
  * the all-ordinal edge-audit array p6_w_edge_hits[P6_EDGE_MAX] -> ANY ported
    object that forwarded to its own STUB (silently-broken port, every ordinal).
  * subsystem witnesses (str/sfx audio, tick/cont speed, pal_hash palette).

Generic detectors (each catches a WHOLE CLASS of bug across ALL objects, so an
unlisted bug is still caught):
  D1 REGISTER   a manifest class that is NOT in the live registration table
                (whole object class never registered -- ANY class).
  D2 CLASSREG   a registered class whose *staticVars is NULL / classID mismatches
                its slot (broken registration / link-time stub bind).
  D3 SPAWN      a registered gameplay class with ZERO live instances where the
                manifest expects placements (placed-but-not-spawned -- ANY class).
  D4 ANIM       any live entity whose animID is out of range for its class's
                animation set (wrong/garbage pose -- ANY entity, not just named).
  D5 VISIBLE    a gameplay scene with no visible player, or an entity with a
                sane position but never drawn (invisible-sprite class).
  D6 EDGE       any nonzero p6_w_edge_hits[ordinal] (broken-port class, ALL ports).
  D7 SPEED      game-speed d(tick)/d(wall) < 55Hz (logic-slow / #243 pacing).
  D8 AUDIO      a scene the decomp starts a track in, with the BGM stream idle.
  D9 PALETTE    pal_hash temporal oscillation (flash -- ANY bank, not just sky).
  D10 TEMPORAL  entity-count collapse or a player frozen in place across the
                window (streaming/pool loss, stuck-state -- generic).
  D11 SANE      universal invariants (qa_invariants: sane pos, players, camera).

Usage (boots nothing -- run tools/_gl_boot.ps1 first so RA is live on game.cue):
  pwsh tools/_gl_boot.ps1
  python tools/qa_parity_oracle.py [SECONDS] [PERIOD]
Exit 0 = no divergence seen, 1 = >=1 divergence (the work queue), 2 = the live
harness is unhealthy (loud self-verify -- garbage is never trusted).
"""
from __future__ import annotations

import glob
import hashlib
import importlib.util
import json
import os
import re
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_ROOT = _HERE.parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


qa_trace = _load("qa_trace", "qa_trace.py")
qa_invariants = _load("qa_invariants", "qa_invariants.py")
qa_netmem = _load("qa_netmem", "qa_netmem.py")

_POS = [a for a in sys.argv[1:] if not a.startswith("--")]
SECS = float(_POS[0]) if len(_POS) > 0 else 180.0
PERIOD = float(_POS[1]) if len(_POS) > 1 else 1.0
MAP = (_ROOT / "game.map").read_text(errors="replace")

# --- live registration-table geometry (from _classreg_probe.py, decomp-cited) ---
OBJCLASS_BASE = 0x060D8000
OBJCLASS_SIZE = 72
STAGEIDS = 0x002FEF80
EDGE_MAX = 96             # p6_w_edge_hits[P6_EDGE_MAX]; P6_EDGE_MAX=96 (p6_closure_edge.c:44)
                          # -- reading past 96 over-reads adjacent memory (garbage ordinals).

# scenes where the decomp starts a BGM track on entry (Music_PlayTrack)
AUDIO_EXPECT = {"Title", "Menu", "AIZ", "GHZCutscene", "GHZ"}
UI_SCENES = {"", "?", None, "Logos", "Title", "Menu"}


def build_name_dict():
    """Every Mania object name from the decomp file set + the scene manifest ->
    md5(name) digest (both byte orders, per _classreg_probe wswap idiom). Built at
    runtime so it never drifts from the cached decomp / manifest."""
    names = set()
    for f in glob.glob(str(_ROOT / "tools/_decomp_raw/SonicMania_Objects_*")):
        m = re.match(r"SonicMania_Objects_[^_]+_(.+)\.(c|h)$", os.path.basename(f))
        if m:
            names.add(m.group(1))
    try:
        man = json.loads((_ROOT / "docs/scene_objects.json").read_text())
        for zd in man.values():
            for o in (zd.get("stage_config", {}).get("objects", []) or []):
                names.add(o)
    except Exception:
        man = {}
    h2n = {}
    for n in names:
        d = hashlib.md5(n.encode()).digest()
        h2n[d] = n
        h2n[b"".join(d[i:i + 4][::-1] for i in range(0, 16, 4))] = n
    return h2n, man


H2N, MANIFEST = build_name_dict()

# EVERY witness the oracle reads. Listed centrally so --selftest can prove they
# ALL resolve in the CURRENT game.map BEFORE a live run -- a renamed/dropped
# symbol would otherwise read None and make a detector silently skip (false
# GREEN = the exact lying-gate this project forbids). This is the anti-regression
# guard for the ORACLE itself.
CORE_SYMS = ["p6_w_edge_hits", "p6_w_tick_frames", "p6_w_cont_frames",
             "p6_w_str_state", "p6_w_str_track", "p6_w_sfx_inited", "p6_w_pal_hash"]
REG_SYMS = ["RSDK::sceneInfo", "RSDK::objectEntityList", "RSDK::currentSceneFolder"]

# per-class aniframes witness -> class name (drives the animID-range check)
ANIFRAMES = {
    "Ring": "p6_w_ring_aniframes", "Spring": "p6_w_spring_aniframes",
    "SpikeLog": "p6_w_spikelog_aniframes", "Spikes": "p6_w_spikes_aniframes",
    "ItemBox": "p6_w_itembox_aniframes", "Platform": "p6_w_platform_aniframes",
    "Shield": "p6_w_shield_aniframes", "Explosion": "p6_w_explosion_aniframes",
    "Animals": "p6_w_animals_aniframes", "Dust": "p6_w_dust_aniframes",
    "ScoreBonus": "p6_w_scorebonus_aniframes", "Motobug": "p6_w_batbrain_aniframes",
    "Batbrain": "p6_w_batbrain_aniframes", "Newtron": "p6_w_newtron_aniframes",
    "Bridge": "p6_w_brg_aniframes",
}


def s32(v):
    return v - 0x100000000 if v is not None and v >= 0x80000000 else v


def selftest():
    """OFFLINE guard (no emulator): prove the oracle's whole ground-truth surface
    is intact so it can NEVER rot into a false-GREEN. Fails LOUD (exit 2) if any
    witness symbol, ground-truth file, or the name dictionary is missing/degraded.
    Run this in verify_done.ps1 on EVERY build so a symbol rename / manifest drop
    is caught the moment it lands, not when a live sweep silently under-reports."""
    problems = []
    syms = list(CORE_SYMS) + list(REG_SYMS) + list(ANIFRAMES.values())
    for n in syms:
        if not re.search(r"0x[0-9a-fA-F]{16}\s+" + re.escape(n) + r"\s*$", MAP, re.M):
            problems.append(f"symbol MISSING from game.map: {n}")
    if not (_ROOT / "docs/scene_objects.json").exists():
        problems.append("ground-truth MISSING: docs/scene_objects.json")
    if len(H2N) < 200:
        problems.append(f"name dictionary degraded ({len(H2N)} entries; expect >=200 "
                        f"decomp+manifest object names) -- REGISTER/SPAWN/ANIM would be blind")
    if not glob.glob(str(_ROOT / "tools/_decomp_raw/SonicMania_Objects_*")):
        problems.append("decomp object cache MISSING: tools/_decomp_raw/SonicMania_Objects_*")
    print("=" * 72)
    print("qa_parity_oracle --selftest  (anti-lying-gate: prove the approach can't false-GREEN)")
    print("=" * 72)
    print(f"  witness symbols checked : {len(syms)}")
    print(f"  name-dict entries       : {len(H2N)}")
    print(f"  manifest scenes         : {len(MANIFEST)}")
    if problems:
        for p in problems:
            print(f"  FAIL {p}")
        print(f"qa_parity_oracle: SELFTEST RED -- {len(problems)} broken dependency(ies); "
              f"the oracle would UNDER-REPORT. Fix before trusting any GREEN.")
        return 2
    print("qa_parity_oracle: SELFTEST GREEN -- every ground-truth dependency resolves; "
          "no detector is blind.")
    return 0


class Oracle:
    def __init__(self):
        self.rd = qa_trace.Reader(True, None, "127.0.0.1", 55355)  # loud self-verify
        self.mem = self.rd.mem

    def sym(self, n):
        return self.rd.sym(MAP, n)

    def w(self, n):
        a = self.sym(n)
        return self.rd.r32(a) if a is not None else None

    def rb(self, a, n):
        out = bytearray()
        left, cur = n, a
        while left > 0:
            take = min(2000, left)
            out += self.mem.read_saturn(cur, take)
            cur += take
            left -= take
        return bytes(out)

    def edge_hits(self):
        """Return {ordinal: count} for every nonzero p6_w_edge_hits slot."""
        a = self.sym("p6_w_edge_hits")
        if a is None:
            return {}
        raw = self.rb(a, 4 * EDGE_MAX)
        out = {}
        for i in range(EDGE_MAX):
            v = int.from_bytes(raw[4 * i:4 * i + 4], "big")
            # WRAM-H pair-swap already undone by read_saturn; value is direct
            if v:
                out[i] = v
        return out

    def registration(self):
        """Walk the LIVE object-class registration table -> list of
        {listIdx, classID, name, sv_ok}. Empty if classCount implausible."""
        si_sym = self.sym("RSDK::sceneInfo")
        if si_sym is None:
            return []
        si = self.rb(si_sym, 40)
        class_count = (si[30] << 8) | si[31]
        if class_count == 0 or class_count > 0x100:
            return []
        ids_raw = self.rb(STAGEIDS, 4 * class_count)
        ids = [int.from_bytes(ids_raw[4 * i:4 * i + 4], "big") for i in range(class_count)]
        maxid = max(ids) if ids else 0
        cls_raw = self.rb(OBJCLASS_BASE, OBJCLASS_SIZE * (maxid + 1))
        rows = []
        for listIdx, cid_idx in enumerate(ids):
            off = cid_idx * OBJCLASS_SIZE
            nm = H2N.get(cls_raw[off:off + 16], "?")
            sv_ptr = int.from_bytes(cls_raw[off + 56:off + 60], "big")  # staticVars @ +16+40
            sv_ok = 0x00200000 <= sv_ptr < 0x00300000 or 0x06000000 <= sv_ptr < 0x06100000
            rows.append({"listIdx": listIdx, "classID": cid_idx, "name": nm, "sv_ok": sv_ok})
        return rows

    def measure_speed_light(self, dt=3.0):
        """Game-speed via a DEDICATED minimal-read burst (only tick/cont), so the
        heavy structural sample's UDP read-load can't stall the emulator during
        the measurement (observer effect). Matches qa_chain_speed.py's method --
        which reads 60.3 tick/s at GHZ where the coupled heavy-sample read only
        23 (a pure artifact). Returns (tick_per_s, render_per_s)."""
        t0 = self.w("p6_w_tick_frames"); c0 = self.w("p6_w_cont_frames"); w0 = time.time()
        time.sleep(dt)
        t1 = self.w("p6_w_tick_frames"); c1 = self.w("p6_w_cont_frames"); dw = time.time() - w0
        if None in (t0, t1, c0, c1) or dw <= 0:
            return None, None
        return (t1 - t0) / dw, (c1 - c0) / dw

    def sample(self):
        s = qa_trace.sample(self.rd, MAP, 700)   # raises loud if unhealthy
        s["reg"] = self.registration()
        s["edge"] = self.edge_hits()
        s["tick"] = self.w("p6_w_tick_frames")
        s["cont"] = self.w("p6_w_cont_frames")
        s["str_state"] = self.w("p6_w_str_state")
        s["str_track"] = self.w("p6_w_str_track")
        s["sfx_inited"] = self.w("p6_w_sfx_inited")
        s["pal_hash"] = self.w("p6_w_pal_hash")
        s["aniframes"] = {c: self.w(sym) for c, sym in ANIFRAMES.items()}
        return s


def main():
    if "--selftest" in sys.argv:
        return selftest()
    baseline_path = None
    for i, a in enumerate(sys.argv):
        if a == "--baseline" and i + 1 < len(sys.argv):
            baseline_path = Path(sys.argv[i + 1])
    # A live run MUST NOT proceed on a rotted approach -- run the offline selftest
    # first; if the ground-truth surface is broken, a live GREEN would be a lie.
    if selftest() != 0:
        sys.stderr.write("qa_parity_oracle: refusing live run -- selftest RED (see above)\n")
        return 2
    try:
        o = Oracle()
    except Exception as e:
        sys.stderr.write(f"qa_parity_oracle: LIVE HARNESS UNHEALTHY -- {e}\n")
        return 2

    samples = []
    t0 = time.time()
    prev = None
    scene_speed = {}   # folder -> (tick_per_s, render_per_s) via a LIGHT burst (no observer effect)
    print("t     folder        n   reg  tick     cont  edge palhash")
    while time.time() - t0 < SECS:
        try:
            s = o.sample()
        except Exception as e:
            sys.stderr.write(f"qa_parity_oracle: sample failed -- {e}\n")
            return 2
        s["wall"] = time.time() - t0
        # game-SPEED must be measured with MINIMAL reads (the heavy structural
        # sample's UDP load stalls the emulator -> a false low reading). Take one
        # dedicated light burst per distinct scene the first time it's seen.
        fol = s["folder"]
        if fol not in scene_speed and fol not in UI_SCENES and s["n_entities"] and s["n_entities"] > 3:
            scene_speed[fol] = o.measure_speed_light(3.0)
        print("%4.0f  %-12s %3s  %3d  %7s %7s  %3d %8s" % (
            s["wall"], (s["folder"] or "?")[:12], s["n_entities"], len(s["reg"]),
            s["tick"], s["cont"], len(s["edge"]),
            s["pal_hash"] if s["pal_hash"] is not None else "?"))
        samples.append(s)
        time.sleep(PERIOD)

    # ---------- per-scene DECOMP-vs-LIVE divergence analysis ----------
    print("\n" + "=" * 80)
    print("PARITY DIVERGENCE LIST  (decomp ground-truth vs live -- comprehensive, per class)")
    print("=" * 80)
    scenes = {}
    for s in samples:
        scenes.setdefault(s["folder"], []).append(s)

    div = []

    def D(scene, code, msg):
        div.append((scene, code, msg))
        print(f"  [{(scene or '?'):12s}] {code:8s} {msg}")

    for folder, ss in scenes.items():
        last = ss[-1]
        man = None
        # manifest lookup: scene_objects.json is keyed by zone folder (GHZ, AIZ, ...)
        for k, zd in MANIFEST.items():
            if k == folder or zd.get("folder") == folder:
                man = zd
                break
        expected = set((man or {}).get("stage_config", {}).get("objects", []) or [])

        # registration set seen across the window (union -- a class may register late)
        reg_rows = {}
        for s in ss:
            for r in s["reg"]:
                reg_rows[r["name"]] = r
        reg_names = set(n for n in reg_rows if n != "?")

        # D1 REGISTER: a decomp class not registered live. HONEST-ONLY: asserting a
        # class is ABSENT requires resolving the WHOLE registered set by name. The
        # objectClassList name-hash (md5, halfword-swapped) resolves cleanly for
        # well-formed entries but many stage indices are empty/pointer slots, so
        # resolution is partial. If we did NOT resolve the full set, we CANNOT
        # conclude absence (that produced a false-positive flood in run 1) -> emit a
        # coverage NOTE, not a divergence. Per-class absence is asserted ONLY when
        # every registered class resolved.
        classcount = len(last["reg"])
        resolved_count = len(reg_names)
        if expected:
            if classcount > 0 and resolved_count >= classcount:
                for nm in sorted(expected - reg_names):
                    D(folder, "REGISTER", f"decomp class '{nm}' NOT registered "
                                          f"({resolved_count}/{classcount} classes resolved) -- absent")
            else:
                print(f"  [{folder or '?':12s}] NOTE     REGISTER coverage-limited: "
                      f"{resolved_count}/{classcount} class names resolved, manifest expects "
                      f"{len(expected)} stage classes -- per-class absence NOT asserted "
                      f"(name-resolution calibration TODO; not a false GREEN, a KNOWN blind spot)")

        # D2 CLASSREG: a RESOLVED class whose *staticVars is NULL. Restricted to
        # resolved names -- an unresolved ('?') entry is often an empty/unused class
        # slot whose all-zero staticVars is expected, not a broken port.
        for nm, r in reg_rows.items():
            if nm == "?":
                continue
            if not r["sv_ok"]:
                D(folder, "CLASSREG", f"class '{nm}' (id {r['classID']}) *staticVars unreadable/NULL "
                                      f"-- broken registration / stub bind")

        # per-class live instance counts (by classID -> name via registration)
        id2name = {r["classID"]: r["name"] for r in last["reg"]}
        live_counts = {}
        for e in last["entities"]:
            nm = id2name.get(e["classID"], f"cid{e['classID']}")
            live_counts[nm] = live_counts.get(nm, 0) + 1

        # D3 SPAWN: a registered gameplay class with 0 live instances (in a gameplay
        # scene). Reported for the classes the manifest lists as stage content --
        # excludes pure setup/API/global-manager classes that never instantiate.
        if folder not in UI_SCENES and expected:
            NONSPAWN = {"GHZSetup", "APICallback", "BadnikHelpers", "Music", "Zone",
                        "COverlay", "AIZSetup", "CPZSetup", "CutsceneSeq"}
            for nm in sorted(expected & reg_names):
                if nm in NONSPAWN:
                    continue
                if live_counts.get(nm, 0) == 0:
                    D(folder, "SPAWN", f"registered class '{nm}' has 0 live instances "
                                       f"(placed-but-not-spawned? verify vs Scene{folder} placements)")

        # D4 ANIM: any live entity with an out-of-range animID for its class
        for e in last["entities"]:
            nm = id2name.get(e["classID"], None)
            aid = e["animID"]
            maxf = last["aniframes"].get(nm) if nm else None
            if aid is None:
                continue
            if aid >= 0xFF00 or aid > 200:
                D(folder, "ANIM", f"entity '{nm or ('cid'+str(e['classID']))}' slot{e['slot']} "
                                  f"animID={aid} is garbage (out of any valid range)")
            elif maxf and maxf > 0 and aid >= maxf:
                D(folder, "ANIM", f"entity '{nm}' slot{e['slot']} animID={aid} >= class frame "
                                  f"count {maxf} -- wrong/undefined pose")

        # D5 VISIBLE: gameplay scene, no visible player
        if folder not in UI_SCENES:
            for s in ss:
                players = [e for e in s["entities"] if e["classID"] == 8]
                if players and not any((p.get("visible") or 0) for p in players):
                    D(folder, "VISIBLE", f"no VISIBLE player at t={s['wall']:.0f}s "
                                         f"(characters missing / not drawn)")
                    break

        # D6 EDGE: any edge-audit ordinal that fired DURING THIS SCENE's window.
        # p6_w_edge_hits[] is CUMULATIVE SINCE BOOT (p6_closure_edge.c) -- attributing
        # the running total to whichever scene was being sampled mis-blamed AIZ for
        # hits accumulated in Menu (calibration finding, 2026-07-16). Use the
        # first-vs-last delta within the scene window; a count that grew here fired here.
        first_e, last_e = ss[0]["edge"], ss[-1]["edge"]
        edge_delta = {}
        for k, v in last_e.items():
            d = v - first_e.get(k, 0)
            if d > 0:
                edge_delta[k] = d
        if edge_delta:
            D(folder, "EDGE", f"edge-audit ordinals fired IN this scene {dict(sorted(edge_delta.items()))} "
                              f"(delta within window; map ordinal->fn via p6_closure_edge.c) "
                              f"-- a boundary stub was crossed (classify dead-cosmetic vs broken-gameplay)")

        # D7 SPEED: game-speed vs 60Hz, from the DEDICATED light burst (not the
        # heavy-sample cadence, which is observer-contaminated). game-time < wall =
        # slow-motion; game-time == 60 but low render = choppy (render-bound, a
        # DIFFERENT problem -- reported as info, not a speed divergence).
        if folder in scene_speed and scene_speed[folder][0] is not None:
            tps, rps = scene_speed[folder]
            if tps < 55.0:
                D(folder, "SPEED", f"game-speed {tps:.1f} tick/s vs 60 = {tps/60*100:.0f}% "
                                   f"realtime (SLOW-MOTION / logic pacing #243)")
            elif rps < 20.0:
                print(f"  [{folder or '?':12s}] NOTE     game-speed OK ({tps:.0f} tick/s = "
                      f"{tps/60*100:.0f}% realtime) but render {rps:.0f} fps -- CHOPPY not slow "
                      f"(render-bound, #243 catch-up; separate from parity)")

        # D8 AUDIO: BGM idle where the decomp starts a track
        if folder in AUDIO_EXPECT:
            if not any((s["str_state"] or 0) for s in ss):
                D(folder, "AUDIO", "BGM stream idle (p6_w_str_state=0) -- decomp Music_PlayTrack "
                                   "should be active (silent-intro class)")
            if not any((s["sfx_inited"] or 0) for s in ss):
                D(folder, "AUDIO", "p6_w_sfx_inited=0 -- SCSP SFX path uninitialised")

        # D9 PALETTE: temporal pal_hash oscillation = a flash (ANY bank)
        ph = [s["pal_hash"] for s in ss if s["pal_hash"] is not None]
        if len(ph) >= 4 and len(set(ph)) >= max(3, len(ph) // 2):
            D(folder, "PALETTE", f"pal_hash oscillates ({len(set(ph))} distinct / {len(ph)} samples) "
                                 f"-- TEMPORAL palette instability (flash; confirm colour via savestate CRAM)")

        # D10 TEMPORAL: entity-count collapse or a frozen player across the window
        ns = [s["n_entities"] for s in ss]
        if len(ns) >= 3 and max(ns) >= 6 and min(ns) <= 2:
            D(folder, "TEMPORAL", f"entity count collapsed {max(ns)}->{min(ns)} across the window "
                                  f"-- streaming/pool loss")
        if folder not in UI_SCENES and len(ss) >= 4:
            pxs = []
            for s in ss:
                pl = [e for e in s["entities"] if e["classID"] == 8]
                pxs.append(pl[0]["x"] if pl and pl[0]["x"] is not None else None)
            good = [x for x in pxs if x is not None]
            if len(good) >= 4 and len(set(good)) == 1:
                D(folder, "TEMPORAL", f"player X frozen at {good[0]} across {len(good)} samples "
                                      f"-- stuck-state (death/no-respawn/no-input class)")

        # D11 SANE: universal invariants
        cutscene = folder in ("AIZ", "GHZCutscene")
        for name, ok, detail, advisory in qa_invariants.invariants(last, cutscene=cutscene):
            if not ok and not advisory:
                D(folder, "SANE", f"{name}: {detail}")

    print("-" * 80)
    from collections import Counter
    byscene = {}
    for scene, code, _ in div:
        byscene.setdefault(scene, []).append(code)
    visited = sorted(scenes.keys(), key=lambda x: (x is None, x))
    if div:
        print("summary (this IS the work queue -- decomp-derived, not a symptom list):")
        for scene, codes in byscene.items():
            print(f"   [{scene or '?':12s}] {dict(Counter(codes))}")
    else:
        print("qa_parity_oracle: NO divergence from decomp ground-truth across the arc")

    # ---- REGRESSION GATE: compare to a recorded baseline so a fix in one scene
    # can't silently reintroduce/add a divergence in another (whole-arc, per the
    # binding rule). Keyset = {(scene, code)} for scenes visited THIS run. ----
    cur = sorted(set((sc, cd) for sc, cd, _ in div))
    rc = 1 if div else 0
    if baseline_path is not None:
        if not baseline_path.exists():
            baseline_path.write_text(json.dumps(
                {"visited": visited, "divergences": [list(k) for k in cur]}, indent=1))
            print(f"\n[baseline] wrote initial baseline -> {baseline_path} "
                  f"({len(cur)} divergence keys, {len(visited)} scenes). "
                  f"Future runs FAIL on any NEW key in a visited scene.")
        else:
            base = json.loads(baseline_path.read_text())
            base_keys = set(tuple(k) for k in base.get("divergences", []))
            # only judge scenes covered by BOTH runs (don't false-regress an
            # unreached scene). NEW key in a shared scene = REGRESSION.
            shared = set(visited) & set(base.get("visited", []))
            new = sorted(k for k in cur if k not in base_keys and k[0] in shared)
            cleared = sorted(k for k in base_keys if k not in set(cur) and k[0] in shared)
            print("\n[regression gate vs baseline]")
            if cleared:
                print(f"  CLEARED ({len(cleared)}): " +
                      ", ".join(f"{s}:{c}" for s, c in cleared) + "  (improvement)")
            if new:
                print(f"  NEW REGRESSIONS ({len(new)}): " +
                      ", ".join(f"{s}:{c}" for s, c in new))
                print("  -> RED: a change reintroduced/added a divergence in a covered scene.")
                rc = 1
            else:
                print("  no new divergence in any covered scene -> approach did NOT regress.")
                # a clean run that only cleared items should still exit 0
                if not div:
                    rc = 0
    if div:
        print(f"qa_parity_oracle: {len(div)} divergence(s) present.")
    return rc


if __name__ == "__main__":
    sys.exit(main())
