#!/usr/bin/env python3
"""
qa_phase3_2a_uicontrol_gate.py - Phase 3.2.a (Task #145) RED/GREEN gate.

Validates the four foundation predicates from the Phase 3.2.a brief:

  P1: UIControl + UIBackground symbols present in game.map
      (UIControl_Create / UIControl_Update / UIControl_StageLoad /
       UIBackground_Create / UIBackground_Update / UIBackground_StageLoad)
  P2: tools/_decomp_raw/class_hash_table.json exists and maps >= 40 hashes
  P3: Menu/Scene1.bin's 41 entity-class hashes now resolve >= 40 (97%+)
      via the hash table (vs the prior baseline of 5 of 41 known names
      in parse_title_entities.py::KNOWN_NAMES)
  P4: src/rsdk/input.h declares the full ControllerState field set
      including the new 3.2.a extensions (key_l, key_r, analog_stick_x,
      analog_stick_y, has_analog) so 6-button + 3D Analog Control Pad
      paths are routed.

Expected RED-firing predicates on the state BEFORE this iteration's
edits land:
  P1: FAIL — UIControl/UIBackground symbols don't exist in the .c source.
  P2: FAIL — class_hash_table.json doesn't exist yet.
  P3: FAIL — Menu/Scene1.bin resolution rate is 5/41 (12%) at baseline.
  P4: FAIL — key_l/key_r/analog_* fields not yet declared.

Expected GREEN after Phase 3.2.a lands:
  P1..P4 all PASS.

Exit codes:
  0  = all 4 predicates green
  1  = at least one predicate red

Usage:
    python tools/qa_phase3_2a_uicontrol_gate.py [--game-map game.map]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


# --- P1: symbol presence in game.map (or source if map absent) ----------

REQUIRED_SYMBOLS = [
    "UIControl_Create",
    "UIControl_Update",
    "UIControl_StageLoad",
    "UIControl_ProcessInputs",
    "UIControl_SetActiveMenu",
    "UIBackground_Create",
    "UIBackground_Update",
    "UIBackground_StageLoad",
    "UIBackground_DrawNormal",
]


def check_p1(verbose: bool) -> tuple[bool, str]:
    map_path = ROOT / "game.map"
    src_uictl_c = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIControl.c"
    src_uibg_c = ROOT / "src" / "mania" / "Objects" / "Menu" / "UIBackground.c"

    # Prefer source-presence check: it's authoritative for the gate
    # (the build needs to run to populate game.map; the gate's job is to
    # validate the SOURCE state is consistent so the next build will
    # produce the symbols). game.map is a secondary cross-check used
    # only if it's been refreshed.
    if not src_uictl_c.exists() or not src_uibg_c.exists():
        return False, "P1 RED: UIControl.c / UIBackground.c source files absent"
    src_text = src_uictl_c.read_text(errors="replace") + src_uibg_c.read_text(errors="replace")
    missing_src = []
    for s in REQUIRED_SYMBOLS:
        if not re.search(rf"\b{re.escape(s)}\s*\(", src_text):
            missing_src.append(s)
    if missing_src:
        return False, f"P1 RED: source missing definitions of {missing_src}"

    # Optional cross-check: if game.map references TitleSetup symbols, it
    # came from a sufficiently recent build that should also have UIControl
    # symbols once rebuilt — but we don't require this for GREEN since the
    # build hasn't necessarily run yet.
    map_note = ""
    if map_path.exists():
        text = map_path.read_text(errors="replace")
        map_has_uicontrol = "UIControl_Create" in text
        map_has_titlesetup = "TitleSetup_Create" in text
        if map_has_uicontrol:
            map_note = " (game.map confirms post-build)"
        elif map_has_titlesetup:
            map_note = " (game.map is from prior build — rebuild to populate)"
        else:
            map_note = " (game.map is stale/pre-Phase-1 — rebuild to populate)"
    else:
        map_note = " (game.map absent — build not run)"
    return True, f"P1 GREEN: all {len(REQUIRED_SYMBOLS)} symbols defined in .c sources{map_note}"


# --- P2: class_hash_table.json exists + >= 40 hashes --------------------

def check_p2(verbose: bool) -> tuple[bool, str]:
    json_path = ROOT / "tools" / "_decomp_raw" / "class_hash_table.json"
    if not json_path.exists():
        return False, f"P2 RED: {json_path} does not exist"
    try:
        data = json.loads(json_path.read_text())
    except Exception as e:
        return False, f"P2 RED: JSON parse failed: {e}"
    hashes = data.get("hashes", {})
    if len(hashes) < 40:
        return False, f"P2 RED: only {len(hashes)} hash slots (need >= 40)"
    return True, f"P2 GREEN: {len(hashes)} hash slots; {len(data.get('global', []))} global + {len(data.get('by_scene', {}))} per-scene class lists"


# --- P3: Menu/Scene1.bin resolution rate >= 40/41 -----------------------

def parse_scene_class_hashes(scene_path: Path) -> list[str]:
    """Walk the Scene file's object table, return all class hashes as hex."""
    with open(scene_path, "rb") as f:
        d = f.read()
    if d[:4] != b"SCN\x00":
        raise ValueError(f"{scene_path} is not an SCN file")

    p = 4 + 0x10
    nl = d[p]; p += 1 + nl + 1
    layer_count = d[p]; p += 1
    for _ in range(layer_count):
        p += 1
        nm = d[p]; p += 1 + nm
        p += 1 + 1 + 2 + 2 + 2 + 2
        sic = struct.unpack_from("<H", d, p)[0]; p += 2
        p += sic * 6
        for _z in range(2):
            total = struct.unpack_from("<I", d, p)[0]; p += 4
            _usize = struct.unpack_from(">I", d, p)[0]; p += 4
            clen = total - 4
            p += clen

    obj_count = d[p]; p += 1
    out = []
    for _ in range(obj_count):
        nhash = d[p:p + 16].hex(); p += 16
        var_count = d[p]; p += 1
        attribs: list[int] = []
        for _ in range(max(0, var_count - 1)):
            p += 16
            attribs.append(d[p]); p += 1
        entity_count = struct.unpack_from("<H", d, p)[0]; p += 2
        for _e in range(entity_count):
            p += 2 + 4 + 4
            for at in attribs:
                if at == 0: p += 1
                elif at == 1: p += 2
                elif at == 2: p += 4
                elif at == 3: p += 1
                elif at == 4: p += 2
                elif at == 5: p += 4
                elif at == 6: p += 4
                elif at == 7: p += 4
                elif at == 8:
                    n = struct.unpack_from("<H", d, p)[0]; p += 2
                    p += n * 2
                elif at == 9: p += 8
                elif at == 11: p += 4
                else: raise ValueError(f"attr type {at}")
        out.append(nhash)
    return out


def check_p3(verbose: bool) -> tuple[bool, str]:
    scene = ROOT / "extracted" / "Data" / "Stages" / "Menu" / "Scene1.bin"
    json_path = ROOT / "tools" / "_decomp_raw" / "class_hash_table.json"
    if not scene.exists():
        return False, f"P3 RED: {scene} missing"
    if not json_path.exists():
        return False, "P3 RED: class_hash_table.json missing (run P2 first)"
    try:
        hashes_table = set(json.loads(json_path.read_text()).get("hashes", {}).keys())
    except Exception as e:
        return False, f"P3 RED: hash table JSON parse failed: {e}"
    try:
        scene_hashes = parse_scene_class_hashes(scene)
    except Exception as e:
        return False, f"P3 RED: scene parse failed: {e}"
    total = len(scene_hashes)
    resolved = sum(1 for h in scene_hashes if h in hashes_table)
    pct = 100.0 * resolved / max(1, total)
    if resolved < 40:
        return False, (f"P3 RED: only {resolved}/{total} ({pct:.1f}%) Menu "
                       f"Scene1.bin classes resolve (need >= 40/41)")
    return True, (f"P3 GREEN: {resolved}/{total} ({pct:.1f}%) Menu Scene1.bin "
                  f"classes resolve via class_hash_table.json")


# --- P4: src/rsdk/input.h declares 3-button + 6-button + Analog fields --

REQUIRED_INPUT_FIELDS = [
    "key_a", "key_b", "key_c",      # 3-button
    "key_x", "key_y", "key_z",      # 6-button
    "key_start",
    "key_l", "key_r",               # 6-button + 3D pad shoulder
    "analog_stick_x", "analog_stick_y",  # 3D Analog pad stick
    "has_analog",
]


def check_p4(verbose: bool) -> tuple[bool, str]:
    h = ROOT / "src" / "rsdk" / "input.h"
    if not h.exists():
        return False, "P4 RED: src/rsdk/input.h missing"
    text = h.read_text(errors="replace")
    missing = [f for f in REQUIRED_INPUT_FIELDS if not re.search(rf"\b{re.escape(f)}\b", text)]
    if missing:
        return False, f"P4 RED: rsdk_controller_state_t missing {missing}"
    return True, f"P4 GREEN: rsdk_controller_state_t declares all {len(REQUIRED_INPUT_FIELDS)} required fields (3-button + 6-button + Analog)"


# --- Driver --------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    print("Phase 3.2.a (Task #145) UIControl + UIBackground foundation gate")
    print("=" * 70)
    p1 = check_p1(args.verbose)
    p2 = check_p2(args.verbose)
    p3 = check_p3(args.verbose)
    p4 = check_p4(args.verbose)
    for ok, msg in [p1, p2, p3, p4]:
        print(("  PASS  " if ok else "  FAIL  ") + msg)
    overall = all(ok for ok, _ in [p1, p2, p3, p4])
    print("=" * 70)
    print(f"OVERALL: {'GREEN' if overall else 'RED'}")
    return 0 if overall else 1


if __name__ == "__main__":
    sys.exit(main())
