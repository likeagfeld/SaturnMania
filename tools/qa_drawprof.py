#!/usr/bin/env python3
"""qa_drawprof.py -- LIVE per-stage DrawLists sub-attribution over headless RA.

#317 front-end perf. The front-end (p6_frontend_frame) runs at 2-11 fps; live
per-section VBLANK brackets localised the cost to the DRAW section
(ProcessObjectDrawLists), ~200ms/frame, while VDP1 fill is idle (0-12% busy) --
so the cost is CPU-side draw-list construction, not fill. The FRT tick witnesses
(p6_w_draw_sort/cb) WRAP at ~78ms (cks=1) so they cannot measure a 200ms section.

This reads the VBLANK-based sub-brackets added to Object.cpp::ProcessObjectDrawLists
(gated on P6_FRONTEND_MENU so plain GHZ stays byte-identical):
  * p6_w_draw_hook_v  -- vblanks in the drawgroup hookCB()   (tile-layer draw hook)
  * p6_w_draw_cb_v    -- vblanks in the entity draw() loops
  * p6_w_draw_tile_v  -- vblanks in the tile-layer ProcessParallax/scanline loop
each 32-bit (wrap-free), latched per-frame; 1 unit = 16.67 ms. Plus the existing
per-section vbl (input/obj/present/draw) + counts (nents/maxgrp) for context.

Reports, per live sample: the current stage (folder), windowed fps
(60*d(cont)/d(vbl)), the 4 top-level section vblanks, and the draw breakdown
[hook / cb / tile] -- naming the dominant draw sub-part so the fix targets it.

Usage: python tools/qa_drawprof.py [--secs 30] [--period 0.7]
Requires a RUNNING headless RA (qa_live.ps1). The qa_trace health guard refuses
garbage (unhealthy anchor -> loud raise, never a lying read).
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

CLASS_PLAYER = 8
CLASS_CAMERA = 6

SECTION_SYMS = [
    "p6_w_perf_vbl_frame", "p6_w_perf_vbl_input", "p6_w_perf_vbl_obj",
    "p6_w_perf_vbl_present", "p6_w_perf_vbl_draw",
]
DRAW_SYMS = ["p6_w_draw_hook_v", "p6_w_draw_cb_v", "p6_w_draw_tile_v",
             "p6_w_draw_blit_v", "p6_w_draw_dma_v"]
CTX_SYMS = ["p6_w_draw_nents", "p6_w_draw_maxgrp", "p6_w_draw_cb"]


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
    p.add_argument("--secs", type=float, default=30.0)
    p.add_argument("--period", type=float, default=0.7)
    p.add_argument("--slots", type=int, default=700)
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=55355)
    a = p.parse_args(argv)

    map_text = Path(a.map).read_text(errors="replace")
    try:
        rd = qa_trace.Reader(True, None, a.host, a.port)
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"qa_drawprof: {e}\n"); return 2

    syms = {n: rd.sym(map_text, n) for n in SECTION_SYMS + DRAW_SYMS + CTX_SYMS}
    cont_sym = rd.sym(map_text, "p6_w_cont_frames")
    vbl_sym = rd.sym(map_text, "p6_perf_vbl_count")
    missing = [n for n, s in syms.items() if not s]
    print("=" * 78)
    print("qa_drawprof -- LIVE per-stage DrawLists sub-attribution (vblank brackets)")
    if missing:
        print(f"  MISSING SYMS (not in this build): {missing}")
        print("  -> the draw sub-brackets need the P6_FRONTEND_MENU instrumented build.")
    print("=" * 78)

    def r(sym):
        return rd.r32(sym) if sym else None

    # per-stage accumulation of dominant-sub observations
    stage_hist = {}
    last_cont = last_vbl = None
    t0 = time.time()
    while time.time() - t0 < a.secs:
        try:
            s = qa_trace.sample(rd, map_text, a.slots)
        except Exception:
            print(f"[{time.time()-t0:5.1f}s] booting / not healthy yet")
            time.sleep(a.period); continue
        players = [e for e in s["entities"] if e["classID"] == CLASS_PLAYER]
        cams = [e for e in s["entities"] if e["classID"] == CLASS_CAMERA]
        stage = classify(s["folder"], players, cams)
        vals = {n: r(sy) for n, sy in syms.items()}
        cont = r(cont_sym); vbl = r(vbl_sym)
        fps = None
        if None not in (cont, vbl, last_cont, last_vbl):
            dc = cont - last_cont; dv = vbl - last_vbl
            if dv >= 8 and dc >= 0:
                fps = round(60.0 * dc / dv, 1)
        last_cont, last_vbl = cont, vbl

        hook = vals["p6_w_draw_hook_v"]; cbv = vals["p6_w_draw_cb_v"]; tile = vals["p6_w_draw_tile_v"]
        blit = vals["p6_w_draw_blit_v"]; dma = vals["p6_w_draw_dma_v"]
        drawv = vals["p6_w_perf_vbl_draw"]
        parts = {"hook": hook or 0, "cb": cbv or 0, "tile": tile or 0}
        dom = max(parts, key=parts.get) if any(v is not None for v in (hook, cbv, tile)) else "?"
        # within cb: blit vs object-logic (cb - blit); within blit: dma vs rest
        objlogic = (cbv or 0) - (blit or 0)
        print(f"[{time.time()-t0:5.1f}s] {stage:13s} fps={str(fps):5s} "
              f"frame={vals['p6_w_perf_vbl_frame']} draw={drawv} | hook={hook} cb={cbv} tile={tile} "
              f"-> {dom.upper()} || cb-split: blit={blit} (dma={dma}) objlogic={objlogic} "
              f"(nents={vals['p6_w_draw_nents']})")
        h = stage_hist.setdefault(stage, {"hook": 0, "cb": 0, "tile": 0, "blit": 0, "dma": 0, "n": 0, "draw": 0})
        h["hook"] += parts["hook"]; h["cb"] += parts["cb"]; h["tile"] += parts["tile"]
        h["blit"] += blit or 0; h["dma"] += dma or 0
        h["draw"] += drawv or 0; h["n"] += 1
        time.sleep(a.period)

    print("-" * 78)
    print("PER-STAGE DRAW BREAKDOWN (summed vblanks over the window; the hog is the max):")
    for stage, h in stage_hist.items():
        if h["n"] == 0:
            continue
        tot = h["hook"] + h["cb"] + h["tile"]
        dom = max(("hook", h["hook"]), ("cb", h["cb"]), ("tile", h["tile"]), key=lambda x: x[1])
        def pct(v):
            return f"{100.0*v/tot:.0f}%" if tot else "-"
        objlogic = h["cb"] - h["blit"]
        cbdom = "DMA" if h["dma"] >= h["blit"] - h["dma"] and h["blit"] >= objlogic else ("BLIT" if h["blit"] > objlogic else "OBJLOGIC")
        print(f"  {stage:13s} n={h['n']:2}  draw_total={h['draw']:5}  "
              f"hook={h['hook']}({pct(h['hook'])}) cb={h['cb']}({pct(h['cb'])}) "
              f"tile={h['tile']}({pct(h['tile'])})  -> HOG={dom[0].upper()}"
              f"  || cb={h['cb']}: blit={h['blit']} (dma={h['dma']}) objlogic={objlogic} -> {cbdom}")
    print("-" * 78)
    return 0


if __name__ == "__main__":
    sys.exit(main())
