#!/usr/bin/env python3
"""
qa_phase2_4c_entities_gate.py - Phase 2.4c Task #140 RED-firing gate.

Verifies that the priority GHZ Act 1 entity ports have:
  1. Their Saturn-side .c port file present under src/mania/Objects/.
  2. The Saturn-side .SPR + .BIN assets present under cd/.
  3. The decomp authoritative-counterpart file cached under
     tools/_decomp_raw/.

Priority ports (per docs/ghz_act1_entity_gap.md §"Priority list"):
  - Spikes        (Global; 41 GHZ1 instances)
  - BuzzBomber    (GHZ;    18 GHZ1 instances)
  - Motobug       (GHZ;    9 GHZ1 instances; UPGRADE pass)

The gate fires RED whenever any priority item lacks its file/asset, and
GREEN once every item is wired. Designed so that:
  - On a stock pre-Phase-2.4c build, the gate fires RED on every
    priority item.
  - On a Phase-2.4c-shipped build, the gate fires GREEN.

Usage:
    python tools/qa_phase2_4c_entities_gate.py

Exit code: 0 = GREEN, 1 = RED.
"""
from __future__ import annotations
import os
import sys


REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# Priority entities: each (class, port_path_rel, decomp_rel, cd_assets_rel)
PRIORITY = [
    {
        "name": "Spikes",
        "port": [
            os.path.join("src", "mania", "Objects", "Global", "Spikes.c"),
            os.path.join("src", "mania", "Objects", "Global", "Spikes.h"),
        ],
        "decomp": os.path.join("tools", "_decomp_raw",
                               "SonicMania_Objects_Global_Spikes.c"),
        "cd_assets": [
            os.path.join("cd", "SPIKES.SPR"),
            os.path.join("cd", "GHZ1SPIKE.BIN"),
        ],
    },
    {
        "name": "BuzzBomber",
        "port": [
            os.path.join("src", "mania", "Objects", "GHZ", "BuzzBomber.c"),
            os.path.join("src", "mania", "Objects", "GHZ", "BuzzBomber.h"),
        ],
        "decomp": os.path.join("tools", "_decomp_raw",
                               "SonicMania_Objects_GHZ_BuzzBomber.c"),
        "cd_assets": [
            os.path.join("cd", "BUZZ.SPR"),
            os.path.join("cd", "GHZ1BUZZ.BIN"),
        ],
    },
    {
        "name": "Motobug-upgrade",
        "port": [
            # The upgrade adds a cadence audit + a decomp-exact hitbox.
            # Sentinel: the upgraded source contains the literal
            # "Motobug->hitboxBadnik" struct citation per decomp L96-99.
            os.path.join("src", "mania", "Objects", "Common", "Entities.c"),
        ],
        "port_must_contain": [
            "MOTOBUG_HITBOX_LEFT",    # decomp Motobug.c:96 -14
            "MOTOBUG_HITBOX_RIGHT",   # decomp Motobug.c:98 +14
            "MOTOBUG_HITBOX_TOP",     # decomp Motobug.c:97 -14
            "MOTOBUG_HITBOX_BOTTOM",  # decomp Motobug.c:99 +14
        ],
        "decomp": os.path.join("tools", "_decomp_raw",
                               "SonicMania_Objects_GHZ_Motobug.c"),
        "cd_assets": [],   # uses existing BADNIK.SPR
    },
]


def check(item):
    errs = []
    for p in item["port"]:
        full = os.path.join(REPO, p)
        if not os.path.exists(full):
            errs.append(f"missing Saturn port file: {p}")
    for p in item.get("cd_assets", []):
        full = os.path.join(REPO, p)
        if not os.path.exists(full):
            errs.append(f"missing CD asset: {p}")
    full_decomp = os.path.join(REPO, item["decomp"])
    if not os.path.exists(full_decomp):
        errs.append(f"missing cached decomp: {item['decomp']}")
    for needle in item.get("port_must_contain", []):
        # Check at least one of the port files contains the literal.
        found = False
        for p in item["port"]:
            full = os.path.join(REPO, p)
            if not os.path.exists(full):
                continue
            try:
                with open(full, "r", encoding="utf-8", errors="replace") as f:
                    if needle in f.read():
                        found = True
                        break
            except OSError:
                pass
        if not found:
            errs.append(
                f"port file(s) missing required token (decomp citation): "
                f"{needle}")
    return errs


def main():
    failures = []
    for item in PRIORITY:
        errs = check(item)
        if errs:
            failures.append((item["name"], errs))
            print(f"RED  {item['name']}:")
            for e in errs:
                print(f"     {e}")
        else:
            print(f"GREEN {item['name']}: port + assets + decomp citation OK")
    print()
    if failures:
        print(f"Gate Phase 2.4c entity-parity: RED "
              f"({len(failures)} priority item(s) not yet wired)")
        return 1
    print("Gate Phase 2.4c entity-parity: GREEN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
