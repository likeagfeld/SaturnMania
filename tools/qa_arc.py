#!/usr/bin/env python3
"""qa_arc.py -- full-arc LIVE monitor over the headless RetroArch harness.

Watches the shipping chain progress from boot through every stage
  boot -> Logos -> Title -> Menu -> AIZ -> GHZCutscene -> GHZ (-> signpost)
and measures, per stage, LIVE (no Mednafen, no single snapshot):
  * reached?             currentSceneFolder + entity signature
  * progression          p6_w_cont_frames increasing == the loop runs
  * universal invariants qa_invariants: players / camera / visible / sane
  * REAL Saturn fps      60 * d(p6_w_cont_frames) / d(p6_perf_vbl_count) -- the
                         rendered-frames-per-true-60Hz-vblank ratio, which is the
                         real-hardware fps and is INVARIANT to headless run speed
                         (both counters are globals that advance in EVERY stage).

Emits the arc timeline (stage transitions) + a per-stage GREEN/RED + fps table =
the data-driven punch list for the parity/perf campaign. Requires a RUNNING
headless RA (video_driver=null); the qa_trace health guard refuses garbage.

Usage: python tools/qa_arc.py [--secs 180] [--slots 700]
Exit 0 = reached GHZ, all hard invariants GREEN; 1 = a stage RED / no GHZ; 2 = err.
"""
from __future__ import annotations

import argparse
import importlib.util
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


qa_trace = _load("qa_trace", "qa_trace.py")
qa_invariants = _load("qa_invariants", "qa_invariants.py")

CLASS_PLAYER = 8
CLASS_CAMERA = 6
CUTSCENE_FOLDERS = ("AIZ", "")  # AIZ intro; GHZCutscene runs with folder cleared


def classify(folder, players, cams):
    f = (folder or "").strip()
    if f in ("Logos", "Title", "Menu", "GHZ"):
        return f
    if f == "AIZ":
        return "AIZ-intro"
    if f == "" and players and cams:
        return "GHZCutscene"
    if not players and not cams:
        return "boot/transition"
    return f or "unknown"


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--secs", type=float, default=180.0)
    p.add_argument("--slots", type=int, default=700)
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=55355)
    p.add_argument("--period", type=float, default=0.7)
    a = p.parse_args(argv)

    map_text = Path(a.map).read_text(errors="replace")
    try:
        rd = qa_trace.Reader(True, None, a.host, a.port)
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"qa_arc: {e}\n"); return 2

    cont_sym = rd.sym(map_text, "p6_w_cont_frames")
    vbl_sym = rd.sym(map_text, "p6_perf_vbl_count") or rd.sym(map_text, "_p6_perf_vbl_count")

    def rdu(sym):
        return rd.r32(sym) if sym else None

    print("=" * 74)
    print("qa_arc -- full-arc LIVE monitor (headless RA). boot -> GHZ; fps=60*dframes/dvbl")
    print(f"  cont_frames sym={'ok' if cont_sym else 'MISSING'}  vbl_count sym={'ok' if vbl_sym else 'MISSING'}")
    print("=" * 74)

    stages = {}   # name -> dict
    order = []
    last_stage = None
    t0 = time.time()
    while time.time() - t0 < a.secs:
        try:
            s = qa_trace.sample(rd, map_text, a.slots)
        except Exception:
            if last_stage != "booting":
                print(f"[{time.time()-t0:6.1f}s] booting / mid-transition (harness not healthy yet)")
                last_stage = "booting"
            time.sleep(a.period); continue
        players = [e for e in s["entities"] if e["classID"] == CLASS_PLAYER]
        cams = [e for e in s["entities"] if e["classID"] == CLASS_CAMERA]
        stage = classify(s["folder"], players, cams)
        cont = rdu(cont_sym); vbl = rdu(vbl_sym)

        st = stages.get(stage)
        if st is None:
            st = {"folder": s["folder"], "inv": None, "n": s["n_entities"],
                  "cont0": cont, "contL": cont, "vbl0": vbl, "vblL": vbl}
            stages[stage] = st
            order.append(stage)
            print(f"[{time.time()-t0:6.1f}s] --> STAGE {stage:14s} folder={s['folder']!r} "
                  f"n={s['n_entities']} cont={cont}")
            last_stage = stage
        if cont is not None:
            st["contL"] = cont
            if st["cont0"] is None:
                st["cont0"] = cont
        if vbl is not None:
            st["vblL"] = vbl
            if st["vbl0"] is None:
                st["vbl0"] = vbl
        st["n"] = s["n_entities"]
        st["inv"] = qa_invariants.invariants(s, cutscene=(s["folder"] or "") in CUTSCENE_FOLDERS)
        time.sleep(a.period)

    def stage_fps(st):
        if None in (st["cont0"], st["contL"], st["vbl0"], st["vblL"]):
            return None
        dc = st["contL"] - st["cont0"]; dv = st["vblL"] - st["vbl0"]
        if dv < 15 or dc < 0:
            return None    # too short a sample / wrap
        return round(60.0 * dc / dv, 1)

    print("-" * 74)
    print("ARC SUMMARY:  stage : verdict : fps : entities : progressed")
    reached_ghz = False
    total_hard = 0
    for name in order:
        st = stages[name]
        hard = [f"{n}:{d}" for n, ok, d, adv in (st["inv"] or []) if not ok and not adv]
        f = stage_fps(st)
        prog = (st["contL"] - st["cont0"]) if None not in (st["cont0"], st["contL"]) else "?"
        verdict = "GREEN" if not hard else f"RED({len(hard)})"
        if name == "GHZ":
            reached_ghz = True
        total_hard += len(hard)
        print(f"  {name:14s} {verdict:9s} fps={str(f):7s} n={st['n']:<4} +{prog} frames")
        for h in hard[:3]:
            print(f"       hard: {h}")
    print("-" * 74)
    ok = reached_ghz and total_hard == 0
    print(f"qa_arc: reached_GHZ={reached_ghz} hard_violations={total_hard} -> {'GREEN' if ok else 'RED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
