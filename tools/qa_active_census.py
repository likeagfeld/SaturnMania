#!/usr/bin/env python3
# =============================================================================
# qa_active_census.py -- D3 input for the unified S1 + 7.3 camera-local pool
# (WHOLE_GAME_MASSPORT_PLAN.md 7.5). Sizes the RESIDENT floor.
#
# The camera-local pool streams the camera-near set, but objects that run
# ACTIVE_ALWAYS / ACTIVE_NORMAL / ACTIVE_PAUSED update EVERY frame regardless of
# camera (Object.cpp:499-555 sets inRange but those types are forced true) -> they
# must ALWAYS be materialized. ACTIVE_BOUNDS/XBOUNDS/YBOUNDS/RBOUNDS are
# camera-gated -> streamable. ActiveFlags: Object.hpp:185-197.
#
# This census parses each cached decomp object's `active = ACTIVE_*` assignments
# (the class name from its RSDK_REGISTER_OBJECT), classifies the class, then
# cross-refs docs/scene_census.json placement to report, per scene, how many
# placed entities are RESIDENT (always-active) vs MANAGER (sets ALWAYS/NORMAL at
# setup AND a BOUNDS type -> streamable after setup, e.g. PlatformControl) vs
# STREAMABLE vs UNKNOWN (class .c not cached). The peak RESIDENT count is the
# pool's always-resident floor; MANAGER instances need their setup handled (D3
# hybrid) but are not a steady-state floor.
# =============================================================================
import json
import os
import re
import glob
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DECOMP = os.path.join(ROOT, "tools", "_decomp_raw")
CENSUS = os.path.join(ROOT, "docs", "scene_census.json")

ALWAYS = {"ACTIVE_ALWAYS", "ACTIVE_NORMAL", "ACTIVE_PAUSED"}
BOUNDS = {"ACTIVE_BOUNDS", "ACTIVE_XBOUNDS", "ACTIVE_YBOUNDS", "ACTIVE_RBOUNDS"}

RE_REG = re.compile(r"RSDK_REGISTER_OBJECT(?:_STATIC)?\s*\(\s*(\w+)")
RE_ACT = re.compile(r"\bactive\s*=\s*(ACTIVE_\w+)")


def create_body(txt, name):
    """Return the text of `<name>_Create(...) { ... }` (brace-matched), or ''.
    The CREATE-time active is the steady state -- the transient ACTIVE_NORMAL many
    objects set when triggered lives in their State_* functions, not Create, so
    parsing Create alone avoids the 'everything is a MANAGER' artifact."""
    i = txt.find(name + "_Create")
    if i < 0:
        return ""
    b = txt.find("{", i)
    if b < 0:
        return ""
    depth, j = 0, b
    while j < len(txt):
        c = txt[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return txt[b:j + 1]
        j += 1
    return txt[b:]


def class_active_map():
    """class name -> set of ACTIVE_* types assigned IN ITS Create (steady state)."""
    out = {}
    for f in glob.glob(os.path.join(DECOMP, "SonicMania_Objects_*.c")):
        txt = open(f, "r", encoding="utf-8", errors="replace").read()
        names = RE_REG.findall(txt)
        if not names:
            base = os.path.basename(f)[:-2]
            names = [base.split("_")[-1]]
        for n in names:
            body = create_body(txt, n)
            out.setdefault(n, set()).update(RE_ACT.findall(body))
    return out


def classify(acts):
    has_always = bool(acts & ALWAYS)
    has_bounds = bool(acts & BOUNDS)
    if has_bounds:
        return "STREAMABLE"        # Create places it camera-gated (transient NORMAL is in State_*)
    if has_always:
        return "RESIDENT"          # Create steady state is always-update -> must stay resident
    if not acts:
        return "NONE"              # no active set in Create (inherits default / set elsewhere)
    return "STREAMABLE"


def main():
    cam = class_active_map()
    census = json.load(open(CENSUS, "r", encoding="utf-8"))
    scenes = census.get("scenes", {})

    cls_kind = {c: classify(a) for c, a in cam.items()}

    rows = []
    placed_classes, known_classes = set(), set()
    for key, s in sorted(scenes.items()):
        objs = s.get("objects")
        if not objs:
            continue
        resident = streamable = none = unknown = 0
        for cls, cnt in objs.items():
            placed_classes.add(cls)
            kind = cls_kind.get(cls)
            if kind is None:
                unknown += cnt
                continue
            known_classes.add(cls)
            if kind == "RESIDENT":
                resident += cnt
            elif kind == "NONE":
                none += cnt
            else:
                streamable += cnt
        rows.append((key, resident, streamable, none, unknown))

    gr = max(rows, key=lambda r: r[1])

    print("=== qa_active_census :: resident-floor sizing for the camera-local pool ===")
    print("(classified by CREATE-time active -- steady state -- not transient State_* changes)")
    print("classes with cached .c=%d ; placed classes across scenes=%d ; matched=%d"
          % (len(cam), len(placed_classes), len(placed_classes & known_classes)))
    print("class kinds (cached): RESIDENT=%d STREAMABLE=%d NONE(no Create active)=%d"
          % (sum(1 for v in cls_kind.values() if v == "RESIDENT"),
             sum(1 for v in cls_kind.values() if v == "STREAMABLE"),
             sum(1 for v in cls_kind.values() if v == "NONE")))
    print("\nPEAK RESIDENT per scene = the always-resident floor the pool must hold:")
    print("  RESIDENT max = %3d  (%s)" % (gr[1], gr[0]))

    print("\n-- top 14 scenes by RESIDENT (always-active) --")
    print("  %-22s %6s %8s %6s %6s" % ("scene", "RESID", "stream", "none", "unkwn"))
    for key, r, st, no, u in sorted(rows, key=lambda x: -x[1])[:14]:
        print("  %-22s %6d %8d %6d %6d" % (key, r, st, no, u))

    res = sorted(c for c, v in cls_kind.items() if v == "RESIDENT")
    print("\nRESIDENT classes (%d, Create active=ALWAYS/NORMAL/PAUSED):\n  %s"
          % (len(res), ", ".join(res)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
