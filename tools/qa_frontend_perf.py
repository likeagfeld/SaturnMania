#!/usr/bin/env python3
"""qa_frontend_perf.py -- #317 front-end perf verification over the live headless RA.

Two levers (user-approved 2026-07-05):
  1. BAND CACHE (draw hog): the front-end re-inflated ~7 sprite bands/frame (miniz).
     Verify inflates/frame DOWN + bandhits/frame UP + draw_vbl DOWN + fps UP.
  2. OBJ + OVERHEAD (Title/Menu/AIZ, ~4 vbl/frame): report obj vblanks + catch-up
     tick count so the second lever can be localized.

Per draw-heavy sample (GHZCutscene/GHZ) it prints the band-cache deltas; per stage it
prints fps + the section vblanks. Requires a RUNNING headless RA (qa_live.ps1).

Usage: python tools/qa_frontend_perf.py [--secs 100]
"""
from __future__ import annotations
import argparse, importlib.util, sys, time
from pathlib import Path

_HERE = Path(__file__).resolve().parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m


qa_trace = _load("qa_trace", "qa_trace.py")
CLASS_PLAYER, CLASS_CAMERA = 8, 6


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
    p.add_argument("--secs", type=float, default=100.0)
    p.add_argument("--period", type=float, default=0.7)
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    a = p.parse_args(argv)
    mt = Path(a.map).read_text(errors="replace")
    try:
        rd = qa_trace.Reader(True, None, "127.0.0.1", 55355)
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"qa_frontend_perf: {e}\n"); return 2

    NAMES = ["p6_w_cont_frames", "p6_perf_vbl_count", "p6_w_sht_fetches",
             "p6_w_sht_bandhits", "p6_w_sht_resident", "p6_w_perf_vbl_draw",
             "p6_w_perf_vbl_obj", "p6_w_tick_last", "p6_w_draw_blit_v"]
    S = {n: rd.sym(mt, n) for n in NAMES}
    miss = [n for n in NAMES if not S[n]]
    if miss:
        print("MISSING witnesses (need instrumented build):", miss)

    def g(n):
        return rd.r32(S[n]) if S.get(n) else None

    print("=" * 78)
    print("qa_frontend_perf -- band-cache win + obj lever (headless RA)")
    print(f"  sheets resident = {g('p6_w_sht_resident')}")
    print("=" * 78)

    stages = {}
    order = []
    pf = pfetch = phit = pvbl = None
    t0 = time.time()
    while time.time() - t0 < a.secs:
        try:
            s = qa_trace.sample(rd, mt, 700)
        except Exception:
            time.sleep(a.period); continue
        players = [e for e in s["entities"] if e["classID"] == CLASS_PLAYER]
        cams = [e for e in s["entities"] if e["classID"] == CLASS_CAMERA]
        stage = classify(s["folder"], players, cams)
        f = g("p6_w_cont_frames"); vbl = g("p6_perf_vbl_count")
        fetch = g("p6_w_sht_fetches"); hit = g("p6_w_sht_bandhits")
        draw = g("p6_w_perf_vbl_draw"); obj = g("p6_w_perf_vbl_obj"); tick = g("p6_w_tick_last")
        st = stages.get(stage)
        if st is None:
            st = {"f0": f, "fL": f, "v0": vbl, "vL": vbl, "fe0": fetch, "feL": fetch,
                  "hi0": hit, "hiL": hit, "draw": [], "obj": [], "tick": []}
            stages[stage] = st; order.append(stage)
        st["fL"] = f; st["vL"] = vbl; st["feL"] = fetch; st["hiL"] = hit
        if draw is not None: st["draw"].append(draw)
        if obj is not None: st["obj"].append(obj)
        if tick is not None: st["tick"].append(tick)
        # live band-cache delta on draw-heavy frames
        if pf is not None and f > pf and (draw or 0) >= 4:
            df = f - pf
            print(f"  {stage:12s} draw_vbl={draw} obj={obj} tick={tick} | "
                  f"inflates/frame={ (fetch-pfetch)/df:.2f} bandhits/frame={ (hit-phit)/df:.2f}")
        pf, pfetch, phit, pvbl = f, fetch, hit, vbl
        time.sleep(a.period)

    def avg(x):
        return sum(x) / len(x) if x else 0.0

    print("-" * 78)
    print("PER-STAGE SUMMARY:")
    for name in order:
        st = stages[name]
        dc = st["fL"] - st["f0"]; dv = st["vL"] - st["v0"]
        fps = round(60.0 * dc / dv, 1) if dv >= 15 and dc >= 0 else None
        infl = (st["feL"] - st["fe0"]) / dc if dc > 0 else 0
        hits = (st["hiL"] - st["hi0"]) / dc if dc > 0 else 0
        print(f"  {name:13s} fps={str(fps):6s} draw~{avg(st['draw']):.1f}vbl "
              f"obj~{avg(st['obj']):.1f}vbl tick~{avg(st['tick']):.1f} | "
              f"inflates/frame~{infl:.2f} bandhits/frame~{hits:.2f}")
    print("-" * 78)
    return 0


if __name__ == "__main__":
    sys.exit(main())
