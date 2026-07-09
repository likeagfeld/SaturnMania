#!/usr/bin/env python3
"""qa_drawcost_gate.py -- #324 front-end ProcessObjectDrawLists cost gate.

Judges a qa_drawcost_watch.py JSONL capture of the live chain against the
measured don't-regress thresholds for the three DrawLists checkpoints the
2026-07-09 profile localised (RED baseline in the table below). All FRT
witnesses are cks=1 ticks (1 tick = 32/26.8742 us = 1.1907 us).

RED baseline (current build 1b88d1d, _drawprof.jsonl 2026-07-09):
  AIZ-claw     : draw_cb med 36,394 ticks (43 ms), 11.6 restages/frame, 6.2 fps
  GHZCutscene  : draw_cb med 135,642 ticks (161 ms), 23 banded inflates/frame, 2.1 fps
  AIZ fly-in   : cyc_draw med 13,711 ticks (16.3 ms) with draw_cb 558 (bracket tail)

GREEN thresholds (post-#324 levers: direct VDP1-VRAM restage pack, 64x80 bucket
12->18 re-carve, NOSCAN-stripped GL1 hot-loop diag, cutscene-seam sheet
residency, fade_fn pool-scan cache). MEASURED-ACHIEVED don't-regress levels
(_drawprof_G3.jsonl 2026-07-09): claw cb med 2,841 / fps 10.7; cutscene settled
cb 1,498 with a real ~35k ExitHBH-beat plateau (5 Heavies animating = ~6
content restages/frame -- structural until the Heavies stop being per-frame
new frames) + a 1.3/frame fetch residual; fly cyc_draw 5,812.
  G1 AIZ-claw    draw_cb med    <= 15,000 ticks (17.9 ms)   [RED was 36,394]
  G2 AIZ-claw    fps med        >= 10.0                     [RED was 6.15]
  G3 GHZCutscene sht inflates   <= 2.5 / frame (med)        [RED was 23.0]
  G4 GHZCutscene draw_cb med    <= 45,000 ticks (53.6 ms)   [RED was 135,642]
  G5 AIZ fly-in  cyc_draw med   <= 6,000 ticks (7.1 ms)     [RED was 13,711]

Usage: python tools/qa_drawcost_gate.py [_drawprof.jsonl]
Exit 0 = all GREEN; 1 = any RED; 2 = capture unusable (checkpoint missing).
"""
import json
import sys
from pathlib import Path


def med(xs):
    xs = sorted(x for x in xs if x is not None)
    return xs[len(xs) // 2] if xs else None


def main(argv):
    path = Path(argv[1] if len(argv) > 1 else "_drawprof.jsonl")
    rows = [json.loads(l) for l in path.open() if l.strip()]
    rows = [r for r in rows if r.get("cont_frames")]

    # per-sample deltas for the cumulative counters (fps, evicts, inflates)
    prev = None
    claw, cut, fly = [], [], []
    for r in rows:
        if prev and prev["cont_frames"] and r["cont_frames"] > prev["cont_frames"]:
            df = r["cont_frames"] - prev["cont_frames"]
            dv = r["vbl_count"] - prev["vbl_count"]
            s = dict(r)
            s["fps"] = 60.0 * df / dv if dv > 0 else None
            s["sht_pf"] = (r["sht_fetches"] - prev["sht_fetches"]) / df
            if r["folder"] == "AIZ":
                (fly if (r.get("aiz_cutscene_state") or 0) < 3 else claw).append(s)
            elif r["folder"] == "GHZCutscene":
                cut.append(s)
        prev = r

    if not claw or not cut or not fly:
        print("qa_drawcost_gate: capture missing a checkpoint "
              "(fly=%d claw=%d cut=%d) -- rerun the chain watch" %
              (len(fly), len(claw), len(cut)))
        return 2

    # The GHZCutscene leg is short (~10 s): its first ticking samples carry the
    # scene-arm COLD-FILL burst (every bucket slot restages once -- MEASURED
    # rst/frame=53 on the first armed frame, _drawprof_G3.jsonl). Judge the
    # SETTLED cutscene: drop the first 2 ticking samples.
    if len(cut) > 3:
        cut = cut[2:]

    checks = [
        ("G1 AIZ-claw draw_cb med <= 15000", med(x["draw_cb"] for x in claw), 15000, "le"),
        ("G2 AIZ-claw fps med >= 10.0", med(x["fps"] for x in claw), 10.0, "ge"),
        ("G3 GHZCutscene inflates/frame med <= 2.5", med(x["sht_pf"] for x in cut), 2.5, "le"),
        ("G4 GHZCutscene draw_cb med <= 45000", med(x["draw_cb"] for x in cut), 45000, "le"),
        ("G5 AIZ-fly cyc_draw med <= 6000", med(x["perf_cyc_draw"] for x in fly), 6000, "le"),
    ]
    ok = True
    for name, val, thr, op in checks:
        good = val is not None and (val <= thr if op == "le" else val >= thr)
        ok &= good
        print("%s  %-42s measured=%s" % ("GREEN" if good else "RED  ", name,
                                         round(val, 2) if val is not None else None))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
