#!/usr/bin/env python3
# =============================================================================
# qa_active_census.py -- D3 input for the unified S1 + 7.3 camera-local pool
# (WHOLE_GAME_MASSPORT_PLAN.md 7.5 / 9.3). Sizes the RESIDENT floor + surfaces
# the MANAGER set (the pool's resident-pin candidates).
#
# The camera-local pool streams the camera-near set, but objects that run
# ACTIVE_ALWAYS / ACTIVE_NORMAL / ACTIVE_PAUSED update EVERY frame regardless of
# camera (Object.cpp sets inRange but those types are forced true) -> they must
# ALWAYS be materialized. ACTIVE_BOUNDS/XBOUNDS/YBOUNDS/RBOUNDS are camera-gated
# -> streamable. ActiveFlags: Object.hpp:185-197.
#
# ADVERSARIAL-QA FIX (plan 9.3 MAJOR #4, verified 2026-06-19): classifying by the
# CREATE body alone UNDER-COUNTS true managers. PlatformControl_Create:158 sets
# ACTIVE_BOUNDS (-> looked STREAMABLE) but PlatformControl_Update:16 sets
# self->active = ACTIVE_NORMAL UNCONDITIONALLY at the top of Update -> it makes
# itself resident every frame while on-screen and walks its child slots (the
# corruption hazard). So a class is a MANAGER iff Create is BOUNDS-only AND Update
# sets active = ALWAYS/NORMAL *before the first conditional* (if/for/while/switch/
# foreach). That UNCONDITIONAL test is what separates a true manager (PlatformControl)
# from the ~117 classes that set NORMAL CONDITIONALLY inside a State_/trigger (those
# stay STREAMABLE -- the over-count the Create-only parse was avoiding).
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
RE_COND = re.compile(r"\b(?:if|for|while|switch|foreach_\w+|foreach)\b\s*\(?")


def fn_body(txt, name, suffix):
    """Brace-matched body of `<name><suffix>(...) { ... }`, or ''."""
    i = txt.find(name + suffix)
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


def update_makes_resident(txt, name):
    """True iff <name>_Update sets active = ALWAYS/NORMAL UNCONDITIONALLY (before
    the first if/for/while/switch/foreach) -> a manager that self-pins resident
    every frame (PlatformControl pattern). A conditional set (inside a State_/
    trigger) does NOT count -- that is the transient-NORMAL the Create-only parse
    rightly ignored."""
    body = fn_body(txt, name, "_Update")
    if not body:
        return False
    mcond = RE_COND.search(body)
    cond_pos = mcond.start() if mcond else len(body)
    for m in RE_ACT.finditer(body):
        if m.start() < cond_pos and m.group(1) in ALWAYS:
            return True
    return False


def class_info():
    """class name -> (set of Create-time ACTIVE_* types, is_manager bool)."""
    out = {}
    for f in glob.glob(os.path.join(DECOMP, "SonicMania_Objects_*.c")):
        txt = open(f, "r", encoding="utf-8", errors="replace").read()
        names = RE_REG.findall(txt)
        if not names:
            base = os.path.basename(f)[:-2]
            names = [base.split("_")[-1]]
        for n in names:
            acts = set(RE_ACT.findall(fn_body(txt, n, "_Create")))
            mgr = update_makes_resident(txt, n)
            prev = out.get(n)
            if prev:
                acts |= prev[0]
                mgr = mgr or prev[1]
            out[n] = (acts, mgr)
    return out


def classify(acts, mgr):
    has_always = bool(acts & ALWAYS)
    has_bounds = bool(acts & BOUNDS)
    if mgr and (has_bounds or not has_always):
        # Create BOUNDS-only (or no Create active) but Update unconditionally
        # flips ALWAYS/NORMAL -> a true manager; must be resident-pin candidate.
        return "MANAGER"
    if has_bounds:
        return "STREAMABLE"
    if has_always:
        return "RESIDENT"
    if not acts:
        return "NONE"
    return "STREAMABLE"


def main():
    cam = class_info()
    census = json.load(open(CENSUS, "r", encoding="utf-8"))
    scenes = census.get("scenes", {})

    cls_kind = {c: classify(a, m) for c, (a, m) in cam.items()}

    rows = []
    placed_classes, known_classes = set(), set()
    for key, s in sorted(scenes.items()):
        objs = s.get("objects")
        if not objs:
            continue
        resident = manager = streamable = none = unknown = 0
        for cls, cnt in objs.items():
            placed_classes.add(cls)
            kind = cls_kind.get(cls)
            if kind is None:
                unknown += cnt
                continue
            known_classes.add(cls)
            if kind == "RESIDENT":
                resident += cnt
            elif kind == "MANAGER":
                manager += cnt
            elif kind == "NONE":
                none += cnt
            else:
                streamable += cnt
        # the resident-pin floor = always-active singletons + managers that
        # self-pin while on-screen (the manager-span gate decides which of these
        # actually need pinning vs stream-as-a-unit -- see qa_manager_span.py).
        rows.append((key, resident, manager, streamable, none, unknown))

    gr = max(rows, key=lambda r: r[1])
    gm = max(rows, key=lambda r: r[1] + r[2])  # peak resident-pin floor (resid + mgr)

    print("=== qa_active_census :: resident-pin floor for the camera-local pool ===")
    print("(Create steady-state + UNCONDITIONAL-Update-NORMAL manager detection -- plan 9.3 #4)")
    print("classes with cached .c=%d ; placed classes=%d ; matched=%d"
          % (len(cam), len(placed_classes), len(placed_classes & known_classes)))
    print("class kinds (cached): RESIDENT=%d MANAGER=%d STREAMABLE=%d NONE=%d"
          % (sum(1 for v in cls_kind.values() if v == "RESIDENT"),
             sum(1 for v in cls_kind.values() if v == "MANAGER"),
             sum(1 for v in cls_kind.values() if v == "STREAMABLE"),
             sum(1 for v in cls_kind.values() if v == "NONE")))
    print("\nPEAK resident-pin floor the pool must hold:")
    print("  RESIDENT (always-active) max = %3d  (%s)" % (gr[1], gr[0]))
    print("  RESIDENT+MANAGER       max = %3d  (%s)" % (gm[1] + gm[2], gm[0]))

    print("\n-- top 14 scenes by RESIDENT+MANAGER --")
    print("  %-22s %6s %4s %8s %6s %6s" % ("scene", "RESID", "MGR", "stream", "none", "unkwn"))
    for key, r, mg, st, no, u in sorted(rows, key=lambda x: -(x[1] + x[2]))[:14]:
        print("  %-22s %6d %4d %8d %6d %6d" % (key, r, mg, st, no, u))

    mgrs = sorted(c for c, v in cls_kind.items() if v == "MANAGER")
    print("\nMANAGER classes (%d -- Update unconditionally self-pins; resident-pin"
          " candidates, span-gated):\n  %s" % (len(mgrs), ", ".join(mgrs) or "(none)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
