#!/usr/bin/env python3
# qa_drawcost_watch.py -- #324 DrawLists sub-cost profiler over the live chain.
# Samples the SHIPPING witnesses (P6_PERF_OBJPROF FRT brackets + the #317
# P6_FRONTEND_MENU vblank sub-brackets + the VDP1 pool counters) each period and
# emits JSONL. Run against a headless RA (qa_live.ps1 -NoMonitor). Capture
# harness for qa_drawcost_gate.py (the #324 front-end DrawLists perf gate).
import importlib.util, json, sys, time
from pathlib import Path

H = Path(__file__).resolve().parent
def load(m, f):
    s = importlib.util.spec_from_file_location(m, H / f)
    x = importlib.util.module_from_spec(s); s.loader.exec_module(x); return x
qt = load("qa_trace", "qa_trace.py")
mt = Path("game.map").read_text(errors="replace")
rd = qt.Reader(True, None, "127.0.0.1", 55355)
csf = rd.sym(mt, "RSDK::currentSceneFolder")
SYMS = {}
def U(nm):
    a = SYMS.get(nm)
    if a is None:
        a = rd.sym(mt, nm) or rd.sym(mt, "RSDK::" + nm) or 0
        SYMS[nm] = a
    return rd.r32(a) if a else None
def folder():
    try: return rd.mem.read_saturn(csf, 16).split(b"\0")[0].decode(errors="replace")
    except Exception: return None
def s32(v): return v - 0x100000000 if v is not None and v >= 0x80000000 else v

NAMES = ["p6_w_cont_frames", "p6_perf_vbl_count", "p6_w_perf_cks",
         "p6_w_perf_cyc_input", "p6_w_perf_cyc_obj", "p6_w_perf_cyc_draw",
         "p6_w_perf_cyc_present", "p6_w_perf_cyc_fgbg",
         "p6_w_draw_sort", "p6_w_draw_cb", "p6_w_draw_nents", "p6_w_draw_maxgrp",
         "p6_w_draw_hook_v", "p6_w_draw_cb_v", "p6_w_draw_tile_v",
         "p6_w_draw_blit_v", "p6_w_draw_dma_v",
         "p6_w_vdp1_cmds", "p6_w_vdp1_evicts", "p6_w_vdp1_landed",
         "p6_w_dl_cmds_max", "p6_w_sht_fetches", "p6_w_draw_calls",
         "p6_w_rst_wait_v", "p6_w_rst_work_v", "p6_w_rst_calls",
         "p6_w_draw_tail", "p6_w_rst_cyc",
         "p6_w_buck0_fmax", "p6_w_buck1_fmax", "p6_w_buck2_fmax",
         "p6_w_buck3_fmax", "p6_w_buck4_fmax",
         "p6_w_sht_resmask", "p6_w_sht_resfill", "p6_w_mkres_reason",
         "p6_w_ghcobj_slot", "p6_w_rubyobj_slot", "p6_w_itemsht_slot",
         "p6_w_aiz_cutscene_state"]

OUT = Path(sys.argv[1] if len(sys.argv) > 1 else "_drawprof.jsonl")
DUR = float(sys.argv[2]) if len(sys.argv) > 2 else 480.0
fout = OUT.open("w"); ts = time.time(); lastf = None
while time.time() - ts < DUR:
    t = time.time() - ts
    r = {"t": round(t, 2), "folder": folder()}
    for nm in NAMES:
        v = U(nm)
        r[nm.replace("p6_w_", "").replace("p6_perf_", "")] = s32(v)
    fout.write(json.dumps(r) + "\n"); fout.flush()
    if r["folder"] != lastf:
        print("[%6.1f] folder=%r cont=%s" % (t, r["folder"], r.get("cont_frames")), flush=True)
        lastf = r["folder"]
    if r["folder"] == "GHZ" and (r.get("cont_frames") or 0) > 0:
        # sample the landing for ~40s then stop
        if not hasattr(sys, "_ghz_t0"):
            sys._ghz_t0 = t
        elif t - sys._ghz_t0 > 45:
            break
    time.sleep(0.55)
print("done ->", OUT)
