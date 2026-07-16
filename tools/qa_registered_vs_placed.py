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
    txt = ""
    for m in (_ROOT / "game.map", _ROOT / "tools/_portspike/_p6/ovl_ring.map"):
        if m.exists():
            txt += m.read_text(errors="replace")
    return set(re.findall(r"\b([A-Za-z0-9_]+)_StageLoad\b", txt))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenes", default="GHZ,AIZ,GHZCutscene",
                    help="comma-separated manifest zones to audit")
    a = ap.parse_args()
    man = json.loads((_ROOT / "docs/scene_objects.json").read_text())
    linked = linked_classes()
    if not linked:
        print("qa_registered_vs_placed: no maps found (build first) -- SKIP", file=sys.stderr)
        return 2
    total_missing = 0
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
        print(f"[{zone:12s}] expected={len(exp)} linked={len(exp & linked)} MISSING={len(missing)}")
        for m in missing:
            print(f"    MISSING: {m}  (class cannot exist at runtime -- port needed)")
        total_missing += len(missing)
    print("-" * 72)
    if total_missing == 0:
        print("qa_registered_vs_placed: GREEN -- every manifest class is linked")
        return 0
    print(f"qa_registered_vs_placed: {total_missing} class(es) missing -- this IS the port backlog")
    return 1


if __name__ == "__main__":
    sys.exit(main())
