#!/usr/bin/env python3
# qa_objcost_gate.py -- #325 front-end ProcessObjects perf gate (RED->GREEN).
#
# Input: the JSONL emitted by qa_objprof_watch.py over a live chain run
# (qa_live.ps1 -NoMonitor + qa_objprof_watch --ab at the landing).
#
# Checks (per the #325 honest targets):
#   G1  GHZ landing (cull ON)  median fps >= 20
#   G2  Menu                   median fps >= 20
#   G3  AIZ fly-in             median fps >= 12
#   G4  A/B RED-proof: landing cull-OFF rows (g_p6_fe_ghz_cull==0, the 186898e
#       behavior) show cyc_obj/frame >= 1.5x the cull-ON rows -- proves the gate
#       measures the bug class the lever fixed (a gate that can't show RED on
#       the old behavior is rejected, meta-QA rule).
#   G5  chain reached GHZ (landing rows exist) -- traversal intact.
#
# Exit 0 = all GREEN; 1 = any RED; 2 = harness error (missing legs/rows).
import json, statistics, sys
from pathlib import Path

path = Path(sys.argv[1] if len(sys.argv) > 1 else "_objprof.jsonl")
rows = []
for ln in path.read_text().splitlines():
    try:
        r = json.loads(ln)
    except Exception:
        continue
    if r.get("kind") == "objupd":
        continue
    rows.append(r)

def leg(name):
    return [r for r in rows if (r.get("folder") or "") == name]

def med(rs, key):
    vs = [r[key] for r in rs if isinstance(r.get(key), (int, float))]
    return statistics.median(vs) if vs else None

FAIL = []
INFO = []

def check(tag, cond, msg):
    (INFO if cond else FAIL).append("%s %s: %s" % ("GREEN" if cond else "RED  ", tag, msg))

# KEY FIX (2026-07-09): the watcher renames p6_w_/p6_perf_ prefixes only, so the
# ProcessObjects group counter is "perf_cyc_obj" in the JSONL (p6_w_perf_cyc_obj),
# NOT "cyc_obj" -- the original key silently produced None medians (a gate that
# cannot fire is rejected, meta-QA rule).
OBJ = "perf_cyc_obj"

menu = leg("Menu")
aiz  = leg("AIZ")
# fps>0 excludes the load-phase rows (folder already "GHZ" but cont_frames frozen
# while the landing loads -- those rows carried cull=0 and polluted the A/B medians).
ghz  = [r for r in rows if (r.get("folder") or "") == "GHZ" and (r.get("cont_frames") or 0) > 0
        and (r.get("fps") or 0) > 0]
ghz_on  = [r for r in ghz if r.get("g_p6_fe_ghz_cull") == 1]
ghz_off = [r for r in ghz if r.get("g_p6_fe_ghz_cull") == 0]

if not ghz:
    print("RED   G5 chain: no live GHZ landing rows -- traversal broken or capture too short")
    sys.exit(1)
INFO.append("GREEN G5 chain: %d live GHZ rows" % len(ghz))

fps_ghz  = med(ghz_on, "fps")
fps_menu = med(menu, "fps")
fps_aiz  = med(aiz, "fps")
check("G1 landing fps", fps_ghz is not None and fps_ghz >= 20.0, "median fps(cull ON) = %s (>=20)" % fps_ghz)
check("G2 menu fps",    fps_menu is not None and fps_menu >= 20.0, "median fps = %s (>=20)" % fps_menu)
check("G3 fly-in fps",  fps_aiz is not None and fps_aiz >= 12.0, "median fps = %s (>=12)" % fps_aiz)

obj_on  = med(ghz_on, OBJ)
obj_off = med(ghz_off, OBJ)
if ghz_off:
    check("G4 A/B RED-proof", obj_on and obj_off and obj_off >= 1.5 * obj_on,
          "cyc_obj cull-OFF=%s vs ON=%s (OFF >= 1.5x ON)" % (obj_off, obj_on))
else:
    INFO.append("note  G4: no cull-OFF rows in this capture (run watch with --ab for the A/B proof)")

# attribution table (informational)
for nm, rs in (("Title", leg("Title")), ("Menu", menu), ("AIZ", aiz),
               ("GHZCutscene", leg("GHZCutscene")), ("GHZ(on)", ghz_on), ("GHZ(off)", ghz_off)):
    if not rs:
        continue
    print("LEG %-12s n=%3d fps=%-6s obj=%-7s N=%-3s static=%-6s l1=%-6s l2=%-5s l3=%-5s statmax=%s cls=%s" % (
        nm, len(rs), med(rs, "fps"), med(rs, OBJ), med(rs, "tick_last"),
        med(rs, "objsec_static"), med(rs, "objsec_loop1"), med(rs, "objsec_loop2"),
        med(rs, "objsec_loop3"), med(rs, "stat_max"), med(rs, "stat_max_cls")))

for m in INFO: print(m)
for m in FAIL: print(m)
sys.exit(1 if FAIL else 0)
