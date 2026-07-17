#!/usr/bin/env python3
"""qa_registered_vs_placed.py -- OFFLINE class-coverage gate (no emulator).

THE FAILURE THIS RETIRES (user, 2026-07-16, furious): "ground break is not
occurring... WHY AREN'T YOU CATCHING THIS ON YOUR OWN" -- CollapsingPlatform
(the crumble-underfoot floor) was simply NOT PORTED/LINKED, the gap was even
recorded in project memory, and no gate surfaced it. A missing OBJECT CLASS is
statically knowable from the build artifacts + the decomp manifest -- it must
never depend on someone remembering.

Method (proven 2026-07-16): for each scene the chain ships, diff the decomp
manifest (docs/scene_objects.json stage_config.objects -- what the decomp
REGISTERS for that stage) against the classes actually LINKED into the build
(<Obj>_StageLoad in game.map or tools/_portspike/_p6/ovl_ring.map). A class
in the manifest with no StageLoad in either map CANNOT exist at runtime.

This is a KNOWN-GAP REPORTER, not a hard gate (the port backlog is real work);
it exits 1 so CI surfaces the list every run, and the list itself is the
authoritative "what's still missing" answer. When a port lands, its row
disappears -- no bookkeeping.

Usage: python tools/qa_registered_vs_placed.py [--scenes GHZ,AIZ,GHZCutscene]
Exit 0 = no missing classes in the audited scenes; 1 = missing classes listed.

--baseline mode (verify_done Gate V-CLASS, 2026-07-16 process fix): compares the
missing set against tools/qa_class_coverage_baseline.json (the acknowledged port
backlog). Exit 0 = missing is a SUBSET of the baseline (ports may shrink it; the
report auto-notes shrinkage so the baseline can be refreshed). Exit 1 = a class
missing that is NOT in the baseline == a NEW coverage regression (e.g. a build
flag dropped an object TU). This makes "a class silently stopped linking" a
hard-RED forever without hard-failing on the known backlog.
Refresh after landing ports: python tools/qa_registered_vs_placed.py --write-baseline
"""
from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent

# Setup/manager classes with no runtime entity/behavior gap when absent is
# INTENTIONAL for the Saturn port -- keep this list SHORT and justified.
WAIVED = set()  # none waived: report everything; the reader judges.


def linked_classes():
    # BOTH maps required: with game.map absent (e.g. mid-build after the clean
    # step) engine-side StageLoads (GHZSetup etc.) vanish and the audit lies --
    # a partial map set produced a polluted baseline on 2026-07-16. Hard-fail.
    txt = ""
    for m in (_ROOT / "game.map", _ROOT / "tools/_portspike/_p6/ovl_ring.map"):
        if not m.exists():
            print(f"qa_registered_vs_placed: {m} MISSING -- build first "
                  f"(a partial map set under-counts linked classes)", file=sys.stderr)
            return set()
        txt += m.read_text(errors="replace")
    return set(re.findall(r"\b([A-Za-z0-9_]+)_StageLoad\b", txt))


_BASELINE = _ROOT / "tools/qa_class_coverage_baseline.json"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenes", default="GHZ,AIZ,GHZCutscene",
                    help="comma-separated manifest zones to audit")
    ap.add_argument("--baseline", action="store_true",
                    help="don't-regress mode: RED only on classes missing beyond the baseline")
    ap.add_argument("--write-baseline", action="store_true",
                    help="rewrite the baseline to the current missing set (after ports land)")
    a = ap.parse_args()
    man = json.loads((_ROOT / "docs/scene_objects.json").read_text())
    linked = linked_classes()
    if not linked:
        print("qa_registered_vs_placed: no maps found (build first) -- SKIP", file=sys.stderr)
        return 2
    total_missing = 0
    per_zone = {}
    print("=" * 72)
    print("registered-vs-placed class audit (decomp manifest vs linked StageLoads)")
    print("=" * 72)
    for zone in a.scenes.split(","):
        zone = zone.strip()
        if zone not in man:
            print(f"[{zone}] not in manifest -- skipped")
            continue
        exp = set(man[zone]["stage_config"]["objects"])
        missing = sorted((exp - linked) - WAIVED)
        per_zone[zone] = missing
        print(f"[{zone:12s}] expected={len(exp)} linked={len(exp & linked)} MISSING={len(missing)}")
        for m in missing:
            print(f"    MISSING: {m}  (class cannot exist at runtime -- port needed)")
        total_missing += len(missing)
    print("-" * 72)

    if a.write_baseline:
        _BASELINE.write_text(json.dumps(per_zone, indent=2) + "\n")
        print(f"baseline written: {_BASELINE} ({total_missing} classes)")
        return 0

    if a.baseline:
        if not _BASELINE.exists():
            print("V-CLASS: no baseline yet -- run --write-baseline first", file=sys.stderr)
            return 2
        base = json.loads(_BASELINE.read_text())
        regressions = []
        shrunk = []
        for zone, missing in per_zone.items():
            bset = set(base.get(zone, []))
            for m in missing:
                if m not in bset:
                    regressions.append(f"{zone}/{m}")
            for m in sorted(bset - set(missing)):
                shrunk.append(f"{zone}/{m}")
        if shrunk:
            print(f"V-CLASS note: {len(shrunk)} class(es) NEWLY LINKED vs baseline "
                  f"(refresh with --write-baseline): {', '.join(shrunk)}")
        if regressions:
            print(f"V-CLASS RED: {len(regressions)} class(es) missing BEYOND the baseline "
                  f"(coverage REGRESSION): {', '.join(regressions)}")
            return 1
        print(f"V-CLASS GREEN: missing set ({total_missing}) within the acknowledged baseline")
        return 0

    if total_missing == 0:
        print("qa_registered_vs_placed: GREEN -- every manifest class is linked")
        return 0
    print(f"qa_registered_vs_placed: {total_missing} class(es) missing -- this IS the port backlog")
    return 1


if __name__ == "__main__":
    sys.exit(main())
