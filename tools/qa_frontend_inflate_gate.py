#!/usr/bin/env python3
"""qa_frontend_inflate_gate.py -- RED/GREEN gate for the front-end draw/inflate hog (#317).

The chain front-end draws many BANDED sprite sheets; SaturnSheet_FetchRect re-runs a
miniz inflate per band on EVERY draw of a banded sheet (SaturnSheet.cpp:385-387,
++p6_w_sht_fetches). A RESIDENT sheet is a zero-inflate direct memcpy (line 345). The
hog: at the GHZCutscene / landed Green Hill Zone the RES store is pre-filled by the
resident TITLE sheets (TSONIC 1 MB), so the late-scene sheets stay banded -> heavy
per-frame re-inflate -> low fps.

This gate boots-agnostic: it reads a RUNNING headless RA (start it with qa_live.ps1),
buckets samples by scene, and reports per-scene:
  inflations/frame = d(p6_w_sht_fetches) / d(p6_w_cont_frames)
  fps              = 60 * d(cont_frames) / d(p6_perf_vbl_count)
plus the residency state (p6_w_sht_resident / _staged / _resbytes). It asserts
inflations/frame <= THRESHOLD on the "hot" scenes (GHZCutscene, GHZ). RED on the
current pre-fix build; GREEN after the hot sheets are promoted resident.

NO dependency on the reverted band-cache witness (p6_w_sht_bandhits).

Usage: python tools/qa_frontend_inflate_gate.py [--secs 120] [--threshold 3.0]
Exit 0 = all hot scenes GREEN; 1 = a hot scene over threshold (RED); 2 = setup error.
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
# #323: "AIZ-intro" joined the hot set -- the AIZ leg ran 3.5-8 fps with 680-1340
# inflates per claw-beat window (banded AIZOBJ/SONIC/TAILS re-inflated per draw ->
# sprite blink/fragments) until the Menu->AIZ seam resident-promote landed.
HOT_STAGES = {"AIZ-intro", "GHZCutscene", "GHZ"}


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
    p.add_argument("--secs", type=float, default=120.0)
    p.add_argument("--period", type=float, default=0.6)
    p.add_argument("--threshold", type=float, default=3.0)
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    a = p.parse_args(argv)
    mt = Path(a.map).read_text(errors="replace")
    try:
        rd = qa_trace.Reader(True, None, "127.0.0.1", 55355)
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"qa_frontend_inflate_gate: {e}\n"); return 2

    NAMES = ["p6_w_cont_frames", "p6_perf_vbl_count", "p6_w_sht_fetches",
             "p6_w_sht_resident", "p6_w_sht_staged", "p6_w_sht_resbytes",
             "p6_w_sht_resfill", "p6_w_sht_resmask", "p6_w_sht_rescap"]
    S = {n: rd.sym(mt, n) for n in NAMES}
    miss = [n for n in NAMES if not S[n]]
    if miss:
        print("MISSING witnesses:", miss)

    def g(n):
        return rd.r32(S[n]) if S.get(n) else None

    print("=" * 78)
    print("qa_frontend_inflate_gate -- draw/inflate hog RED/GREEN (headless RA)")
    print(f"  threshold = {a.threshold} inflations/frame on hot scenes {sorted(HOT_STAGES)}")
    print("=" * 78)

    stages = {}
    order = []
    pf = pfetch = pvbl = None
    last_res = last_stg = last_rb = 0
    t0 = time.time()
    while time.time() - t0 < a.secs:
        try:
            s = qa_trace.sample(rd, mt, 700)
        except Exception:
            time.sleep(a.period); continue
        players = [e for e in s["entities"] if e["classID"] == CLASS_PLAYER]
        cams = [e for e in s["entities"] if e["classID"] == CLASS_CAMERA]
        stage = classify(s["folder"], players, cams)
        f = g("p6_w_cont_frames"); vbl = g("p6_perf_vbl_count"); fetch = g("p6_w_sht_fetches")
        res = g("p6_w_sht_resident"); stg = g("p6_w_sht_staged"); rb = g("p6_w_sht_resbytes")
        if res is not None: last_res = res
        if stg is not None: last_stg = stg
        if rb is not None: last_rb = rb
        rf = g("p6_w_sht_resfill"); rm = g("p6_w_sht_resmask")
        st = stages.get(stage)
        if st is None:
            st = {"df": 0, "dfetch": 0, "dvbl": 0, "res": res, "stg": stg, "rb": rb,
                  "rf": rf, "rm": rm}
            stages[stage] = st; order.append(stage)
        if rf is not None: st["rf"] = rf
        if rm is not None: st["rm"] = rm
        # accumulate deltas only within the same stage across consecutive samples
        if pf is not None and f is not None and fetch is not None and vbl is not None \
           and stage == getattr(main, "_pstage", None) and f >= pf:
            st["df"] += (f - pf); st["dfetch"] += (fetch - pfetch); st["dvbl"] += (vbl - pvbl)
        st["res"] = last_res; st["stg"] = last_stg; st["rb"] = last_rb
        main._pstage = stage
        pf, pfetch, pvbl = f, fetch, vbl
        time.sleep(a.period)

    print("-" * 78)
    print("PER-STAGE (accumulated within-stage deltas):")
    red = []
    for name in order:
        st = stages[name]
        infl = st["dfetch"] / st["df"] if st["df"] > 0 else 0.0
        fps = 60.0 * st["df"] / st["dvbl"] if st["dvbl"] > 0 else None
        hot = name in HOT_STAGES
        flag = ""
        if hot and st["df"] >= 30:  # enough frames to trust the rate
            if infl > a.threshold:
                flag = "  <-- RED (hot scene over threshold)"; red.append(name)
            else:
                flag = "  <-- GREEN"
        rf = st.get("rf"); rm = st.get("rm")
        rfkb = f"{rf/1024:.0f}KB" if rf else "?"
        rmn = bin(rm).count("1") if rm else 0
        print(f"  {name:15s} frames={st['df']:5d} inflations/frame={infl:6.2f} "
              f"fps={('%.1f' % fps) if fps else 'n/a':>6s}  resfill={rfkb:>7s} "
              f"resident_slots={rmn}{flag}")
    print("-" * 78)
    print(f"RESIDENCY (last seen): resident_sheets={last_res}  staged_sheets={last_stg}  "
          f"res_store_bytes_used={last_rb} ({last_rb/1024:.0f} KB of 1664 KB)")
    print("-" * 78)
    if red:
        print(f"RED: hot scenes over {a.threshold} inflations/frame: {red}")
        return 1
    # Only GREEN if we actually observed a hot scene with enough frames
    saw_hot = any(n in HOT_STAGES and stages[n]["df"] >= 30 for n in order)
    if not saw_hot:
        print("INCONCLUSIVE: no hot scene (GHZCutscene/GHZ) sampled with >=30 frames.")
        return 2
    print("GREEN: all hot scenes within threshold.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
