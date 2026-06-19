#!/usr/bin/env python3
# =============================================================================
# qa_manager_span.py -- RED gate for the camera-local pool's manager-corruption
# hazard (WHOLE_GAME_MASSPORT_PLAN.md 9.2 CRITICAL #3, gate added 2026-06-19).
#
# The adversarial QA proved a camera-local pool that dormant-izes far entities
# CORRUPTS a contiguous-manager object if it walks child slots that lie outside
# the camera-near window while the manager is live (PlatformControl walks its
# PlatformNode run with deep EntityBase read/writes; platform->speed is an
# ABSOLUTE slot index). The DESIGN FIX is to PIN any level-wide manager circuit
# RESIDENT (don't stream it) -- which also stabilizes its absolute-slot refs.
# That only works if the pin set is BOUNDED. This gate measures, offline from
# scene_census._coords (slot+pixel-pos per placed entity), the resident-pin cost
# of every level-wide circuit and asserts it fits the pool's resident-pin budget.
#
# Manager circuit model (verified on-disk):
#   PlatformNode chains via RSDK_GET_ENTITY(entitySlot+1) -> consecutive slots
#   (PlatformNode.c:31). A circuit = a consecutive-slot run of PlatformNodes; its
#   world-span (max bbox dim) > the near window => the controller can be live with
#   a child off-screen => must be pinned. (Standalone Platforms carry children via
#   collisionOffset = co-located, safe; ZipLine = 2-entity local.)
#
# RED if ANY scene's total level-wide-circuit entities exceed PIN_BUDGET (the
# resident sub-region the ~256-slot pool reserves for pinned managers). GREEN =
# every dense circuit fits the pin budget -> the pool's 318 KB saving holds.
# Measured 2026-06-19: worst = MSZ2 17, TMZ3 8 -> well under a 32 budget.
#
# Usage: python tools/qa_manager_span.py [--win W H] [--budget N]
# =============================================================================
import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CENSUS = os.path.join(ROOT, "docs", "scene_census.json")

NEAR_W, NEAR_H = 680, 496   # RSDKv5 camera-active box proxy (qa_camera_local_pool)
PIN_BUDGET = 32             # resident-pin slots the pool reserves for managers


def consecutive_runs(slots):
    slots = sorted(slots)
    runs, cur = [], []
    for s in slots:
        if cur and s != cur[-1] + 1:
            runs.append(cur)
            cur = []
        cur.append(s)
    if cur:
        runs.append(cur)
    return runs


def main(argv):
    win_w, win_h, budget = NEAR_W, NEAR_H, PIN_BUDGET
    if "--win" in argv:
        i = argv.index("--win"); win_w, win_h = int(argv[i + 1]), int(argv[i + 2])
    if "--budget" in argv:
        i = argv.index("--budget"); budget = int(argv[i + 1])

    census = json.load(open(CENSUS, "r", encoding="utf-8"))["scenes"]
    worst_span = 0
    worst_span_scene = ""
    worst_pin = 0
    worst_pin_scene = ""
    offenders = []
    n_scenes_with_circuits = 0

    for key, s in census.items():
        co = s.get("_coords")
        if not co:
            continue
        nodes = co.get("PlatformNode")
        if not nodes:
            continue
        n_scenes_with_circuits += 1
        pos = {e[0]: (e[1], e[2]) for cls, lst in co.items() for e in lst}
        pinned = 0
        scene_span = 0
        for run in consecutive_runs([e[0] for e in nodes]):
            xs = [pos[sl][0] for sl in run]
            ys = [pos[sl][1] for sl in run]
            span_x, span_y = max(xs) - min(xs), max(ys) - min(ys)
            scene_span = max(scene_span, span_x, span_y)
            # PER-AXIS hazard: the near window is win_w x win_h, so a child is
            # out-of-window (-> dormant while the controller is live) iff the
            # circuit exceeds the window in EITHER axis. (A max(span) collapse
            # over-counts wide-but-short circuits.)
            if span_x > win_w or span_y > win_h:
                pinned += len(run)
        if scene_span > worst_span:
            worst_span, worst_span_scene = scene_span, key
        if pinned > worst_pin:
            worst_pin, worst_pin_scene = pinned, key
        if pinned > budget:
            offenders.append((key, pinned, scene_span))

    print("=" * 72)
    print("MANAGER-SPAN gate :: camera-local pool resident-pin budget (plan 9.2 #3)")
    print("=" * 72)
    print("near window %dx%d px ; pin budget %d ; %d scenes with PlatformNode circuits"
          % (win_w, win_h, budget, n_scenes_with_circuits))
    print("  worst single-circuit span : %d px  (%s)" % (worst_span, worst_span_scene))
    print("  worst resident-pin cost   : %d entities  (%s)" % (worst_pin, worst_pin_scene))
    print("-" * 72)
    if offenders:
        print("  [ RED ] every scene's level-wide-circuit pin cost <= %d" % budget)
        for k, p, sp in sorted(offenders, key=lambda r: -r[1]):
            print("          OVER  %-22s pin=%d span=%dpx" % (k, p, sp))
        print("RESULT: RED -- a manager circuit needs more resident-pins than the pool")
        print("        budgets. Grow the pin sub-region OR special-case that circuit.")
        return 1
    print("  [GREEN] worst pin cost %d <= budget %d -> every level-wide circuit fits"
          % (worst_pin, budget))
    print("RESULT: GREEN -- the pool can pin all level-wide manager circuits resident;")
    print("        the camera-local pool keystone + its 318 KB WRAM-L saving hold.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
