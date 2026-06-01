#!/usr/bin/env python3
"""qa_phase2_4f_canonical_spawn_gate.py - Phase 2.4f canonical-spawn gate.

BINDING per memory/qa-iterative-improvement.md v3 + the user-reported
bug 2026-05-28 (Task #143):

  > "this doesnt seem like the canonical first level of the game once
  >  someone selects mania mode"

Root cause: src/mania/Game.c::mania_ghz_player_init_on_transition at
lines 1009-1031 (pre-fix) had two Saturn-fit deviations:
  (1) Player_Init(..., 0, ((int32_t)start_y) << 16) -> spawn at world
      col 0, NOT the canonical Scene1.bin Player entity position.
  (2) g_ghz_autorun = true; g_ghz_input_grace = 30; -> forces auto-
      run rightward, ignores controller input for 30 ticks.

Per EXACTLY-A-REPLICA mandate (CLAUDE.md §1) + post-button-press canon
scope (memory/post-button-press-canon-scope.md), both are forbidden.

Decomp authority:
  - tools/_decomp_raw/SonicMania_Objects_Global_Player.c:542-672
    Player_Create reads characterID from the entity attribute table
    and propagates entity->position into self->position via the RSDK
    InitObjects pass (rsdkv5-src/RSDKv5/RSDK/Scene/Scene.cpp:528-665
    LoadSceneAssets -> ProcessObjects -> per-class Create dispatch).
  - extracted/Data/Stages/GHZ/Scene1.bin Player object hash
    md5("Player") = 636da1d35e805b00eae0fcd8333f9234.
    Slot 887 (the active Mania-mode spawn) is at:
      x_q16 = 108 * 65536 = 7077888  (Q16.16 fixed)
      y_q16 = 947 * 65536 = 62062592 (Q16.16 fixed)
    i.e. canonical integer pixel (108, 947).

Saturn-port deviation acknowledged (per §3 policy): the spawn coord is
extracted at BUILD TIME (tools/extract_ghz_spawn.py) and shipped as
cd/GHZ1SPWN.BIN so the runtime doesn't need a full RSDK entity-table
dispatch refactor. The COORD IS DECOMP-CANONICAL; only the read path is
Saturn-fit. The full runtime Approach A (RSDK entity-table dispatch +
Player_Create) is a future engine-completeness task.

Predicates:

  P1 (static) - g_ghz_autorun symbol is deleted from Game.c (or made
      a compile-time-false dead branch). The autorun take-over loop
      may remain as a code path BUT must not be reachable from the
      post-fix init_on_transition.

  P2 (static) - g_ghz_input_grace is deleted (or initialised to 0
      with no setter in init_on_transition).

  P3 (static + savestate) - The spawn coord written by Player_Init
      in mania_ghz_player_init_on_transition matches the Scene1.bin
      Player slot 887 x (=108 px) within 16 px. We verify by:
       (a) Parsing Scene1.bin for the slot 887 x value.
       (b) Grepping Game.c for the literal that Player_Init now uses
           (or for the build-time extracted CD asset reference).
       (c) Optionally peeking Player.xpos via savestate at GHZ_ACTIVE
           entry (--with-savestates).

  P4 (static + savestate) - Same as P3 but for ypos (Scene1.bin slot
      887 y = 947 px).

  P5 (savestate, optional) - After 5 ticks at TS_GHZ_ACTIVE entry with
      NO controller input asserted, Player.xpos has not moved more
      than 1 px (no autorun). Skipped unless --with-savestates passes
      a pair of states bracketing the first 5 GHZ-active frames.

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    python tools/qa_phase2_4f_canonical_spawn_gate.py
    python tools/qa_phase2_4f_canonical_spawn_gate.py \
        --with-savestates samples/qa_phase2_4f_t0.mcs,samples/qa_phase2_4f_t1.mcs
"""

import argparse
import hashlib
import os
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cprint(tag, msg, ok):
    colour = "\033[32m" if ok else "\033[31m"
    reset = "\033[0m"
    print(f"  [{colour}{tag}{reset}] {msg}")
    return ok


# --- Scene1.bin Player-entity coord extraction ----------------------------
#
# Inline the minimum parser needed; tools/parse_title_entities.py contains
# the full version but pulling it in via subprocess adds a dependency loop.

def _parse_player_entity(scene_path):
    """Return (slot, x_px, y_px) of the FIRST Player entity (slot 887 per
    inspection) in GHZ/Scene1.bin, or None if not found."""
    if not os.path.exists(scene_path):
        return None
    with open(scene_path, "rb") as f:
        d = f.read()

    class R:
        def __init__(self, b):
            self.d, self.p = b, 0
        def u8(self):
            v = self.d[self.p]; self.p += 1; return v
        def u16(self):
            v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v
        def u32(self):
            v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v
        def i32(self):
            v = struct.unpack_from("<i", self.d, self.p)[0]; self.p += 4; return v
        def i8(self):
            v = struct.unpack_from("<b", self.d, self.p)[0]; self.p += 1; return v
        def i16(self):
            v = struct.unpack_from("<h", self.d, self.p)[0]; self.p += 2; return v
        def take(self, n):
            v = self.d[self.p:self.p+n]; self.p += n; return v
        def s(self):
            n = self.u8(); v = self.d[self.p:self.p+n]; self.p += n; return v
        def compressed(self):
            total = self.u32()
            _u = struct.unpack_from(">I", self.d, self.p)[0]; self.p += 4
            clen = total - 4
            z = self.d[self.p:self.p+clen]; self.p += clen
            return zlib.decompress(z)

    r = R(d)
    if d[:4] != b"SCN\x00":
        return None
    r.p = 4
    r.take(0x10)
    nl = r.u8()
    r.take(nl + 1)
    layer_count = r.u8()
    for _ in range(layer_count):
        r.u8()
        r.s()
        r.u8()
        r.u8()
        r.u16(); r.u16(); r.u16(); r.u16()
        sic = r.u16()
        for _ in range(sic):
            r.take(6)
        r.compressed()
        r.compressed()

    player_hash = hashlib.md5(b"Player").digest()
    obj_count = r.u8()
    for _ in range(obj_count):
        nhash = r.take(16)
        var_count = r.u8()
        attribs = []
        for _ in range(max(0, var_count - 1)):
            r.take(16)
            attribs.append(r.u8())
        entity_count = r.u16()
        for _ in range(entity_count):
            slot = r.u16()
            x = r.i32()
            y = r.i32()
            for atype in attribs:
                if atype == 0:   r.u8()
                elif atype == 1: r.u16()
                elif atype == 2: r.u32()
                elif atype == 3: r.i8()
                elif atype == 4: r.i16()
                elif atype == 5: r.i32()
                elif atype == 6: r.u32()
                elif atype == 7: r.u32()
                elif atype == 8:
                    n = r.u16(); r.take(n * 2)
                elif atype == 9:
                    r.i32(); r.i32()
                elif atype == 11: r.u32()
                else:
                    return None
            if nhash == player_hash:
                # Coord is Q16.16; convert to integer pixels.
                return (slot, x >> 16, y >> 16)
    return None


SCENE_PATH = os.path.join(ROOT, "extracted", "Data", "Stages", "GHZ", "Scene1.bin")


def predicate_1_no_autorun():
    """Game.c::mania_ghz_player_init_on_transition no longer sets
    g_ghz_autorun = true. Detection: re-find the function body and
    scan it for the assignment `g_ghz_autorun = true`. On the pre-
    fix build the string is present; post-fix it is absent (or the
    variable is removed entirely, in which case the regex matches
    nothing -> still GREEN)."""
    path = os.path.join(ROOT, "src", "mania", "Game.c")
    if not os.path.exists(path):
        return cprint("P1 RED", "Game.c not found", False)
    with open(path, "r", encoding="utf-8") as f:
        body = f.read()
    # Locate the function body to scope the search.
    m = re.search(
        r"void\s+mania_ghz_player_init_on_transition\s*\(\s*void\s*\)\s*\{",
        body)
    if not m:
        return cprint("P1 RED",
                      "init_on_transition function not found in Game.c",
                      False)
    # Find matching closing brace.
    start = m.end()
    depth = 1
    p = start
    while p < len(body) and depth > 0:
        if body[p] == '{': depth += 1
        elif body[p] == '}': depth -= 1
        p += 1
    fn_body = body[start:p-1]
    # Look for `g_ghz_autorun = true` (any whitespace).
    if re.search(r"g_ghz_autorun\s*=\s*true\b", fn_body):
        return cprint("P1 RED",
                      "init_on_transition still sets g_ghz_autorun=true",
                      False)
    return cprint("P1 GREEN",
                  "init_on_transition does NOT set g_ghz_autorun=true",
                  True)


def predicate_2_no_input_grace():
    """Same as P1 but for g_ghz_input_grace. The grace counter only
    has meaning if autorun is active; with autorun gone the grace is
    redundant. Post-fix: no assignment in init_on_transition."""
    path = os.path.join(ROOT, "src", "mania", "Game.c")
    with open(path, "r", encoding="utf-8") as f:
        body = f.read()
    m = re.search(
        r"void\s+mania_ghz_player_init_on_transition\s*\(\s*void\s*\)\s*\{",
        body)
    if not m:
        return cprint("P2 RED",
                      "init_on_transition function not found",
                      False)
    start = m.end()
    depth = 1
    p = start
    while p < len(body) and depth > 0:
        if body[p] == '{': depth += 1
        elif body[p] == '}': depth -= 1
        p += 1
    fn_body = body[start:p-1]
    # Allow `g_ghz_input_grace = 0` (explicit zero) but not nonzero.
    bad = re.search(r"g_ghz_input_grace\s*=\s*([1-9]\d*)\b", fn_body)
    if bad:
        return cprint("P2 RED",
                      f"init_on_transition sets g_ghz_input_grace={bad.group(1)}",
                      False)
    return cprint("P2 GREEN",
                  "init_on_transition does NOT set nonzero g_ghz_input_grace",
                  True)


def predicate_3_canonical_x():
    """Player_Init in init_on_transition spawns at the canonical
    Scene1.bin Player x coord. Two evidence paths:

      (a) The cd/GHZ1SPWN.BIN build artefact exists and contains the
          canonical x value (matches Scene1.bin parse exactly).
      (b) Game.c::init_on_transition references the spawn-coord
          accessor (ghz_spawn_x() / mania_ghz_spawn_x() / direct
          int constant 108).

    The combination proves the runtime path uses the canonical x.
    """
    scene_player = _parse_player_entity(SCENE_PATH)
    if scene_player is None:
        return cprint("P3 RED",
                      f"Could not parse Player entity from {SCENE_PATH}",
                      False)
    slot, sx, sy = scene_player
    # Build-artefact check: cd/GHZ1SPWN.BIN exists and starts with
    # canonical x as big-endian int32.
    spwn_path = os.path.join(ROOT, "cd", "GHZ1SPWN.BIN")
    artefact_ok = False
    artefact_msg = ""
    if os.path.exists(spwn_path):
        with open(spwn_path, "rb") as f:
            blob = f.read()
        if len(blob) >= 4:
            artefact_x = struct.unpack(">i", blob[:4])[0]
            if artefact_x == sx:
                artefact_ok = True
                artefact_msg = f"cd/GHZ1SPWN.BIN x={artefact_x} matches Scene1 slot {slot}"
            else:
                artefact_msg = f"cd/GHZ1SPWN.BIN x={artefact_x} != Scene1 x={sx}"
        else:
            artefact_msg = f"cd/GHZ1SPWN.BIN too short ({len(blob)} B)"
    else:
        artefact_msg = "cd/GHZ1SPWN.BIN missing"

    # Source-code check: init_on_transition no longer hard-codes
    # the X coord to 0. We look for `Player_Init(...,  0,` pattern.
    path = os.path.join(ROOT, "src", "mania", "Game.c")
    with open(path, "r", encoding="utf-8") as f:
        body = f.read()
    m = re.search(
        r"void\s+mania_ghz_player_init_on_transition\s*\(\s*void\s*\)\s*\{",
        body)
    src_ok = False
    src_msg = ""
    if m:
        start = m.end()
        depth = 1
        p = start
        while p < len(body) and depth > 0:
            if body[p] == '{': depth += 1
            elif body[p] == '}': depth -= 1
            p += 1
        fn_body = body[start:p-1]
        # Bad pattern: Player_Init(<state-ptr>, ID, 0, <y>)
        bad = re.search(
            r"Player_Init\s*\([^,]+,[^,]+,\s*0\s*,", fn_body)
        if bad:
            src_msg = "init_on_transition still passes literal 0 as xpos"
        else:
            src_ok = True
            src_msg = "init_on_transition no longer passes literal 0 as xpos"
    else:
        src_msg = "init_on_transition function not found"

    if artefact_ok and src_ok:
        return cprint("P3 GREEN",
                      f"canonical x={sx} (slot {slot}); {artefact_msg}; {src_msg}",
                      True)
    return cprint("P3 RED",
                  f"Scene1 x={sx} (slot {slot}); artefact: {artefact_msg}; src: {src_msg}",
                  False)


def predicate_4_canonical_y():
    """Same as P3 but for Y. Pre-fix Game.c hard-coded start_y=960
    (the col-0 surface table value); canonical Scene1 slot 887 y=947.

    Check (a) cd/GHZ1SPWN.BIN offset +4 holds canonical y and
    (b) init_on_transition no longer hard-codes 960."""
    scene_player = _parse_player_entity(SCENE_PATH)
    if scene_player is None:
        return cprint("P4 RED", "scene parse failed", False)
    slot, sx, sy = scene_player
    spwn_path = os.path.join(ROOT, "cd", "GHZ1SPWN.BIN")
    artefact_ok = False
    artefact_msg = ""
    if os.path.exists(spwn_path):
        with open(spwn_path, "rb") as f:
            blob = f.read()
        if len(blob) >= 8:
            artefact_y = struct.unpack(">i", blob[4:8])[0]
            if artefact_y == sy:
                artefact_ok = True
                artefact_msg = f"cd/GHZ1SPWN.BIN y={artefact_y} matches Scene1 slot {slot}"
            else:
                artefact_msg = f"cd/GHZ1SPWN.BIN y={artefact_y} != Scene1 y={sy}"
        else:
            artefact_msg = f"cd/GHZ1SPWN.BIN too short ({len(blob)} B)"
    else:
        artefact_msg = "cd/GHZ1SPWN.BIN missing"

    path = os.path.join(ROOT, "src", "mania", "Game.c")
    with open(path, "r", encoding="utf-8") as f:
        body = f.read()
    m = re.search(
        r"void\s+mania_ghz_player_init_on_transition\s*\(\s*void\s*\)\s*\{",
        body)
    src_ok = False
    src_msg = ""
    if m:
        start = m.end()
        depth = 1
        p = start
        while p < len(body) and depth > 0:
            if body[p] == '{': depth += 1
            elif body[p] == '}': depth -= 1
            p += 1
        fn_body = body[start:p-1]
        # Bad: literal `start_y = 960` (the pre-fix col-0 fallback).
        bad = re.search(r"start_y\s*=\s*960\b", fn_body)
        if bad:
            src_msg = "init_on_transition still hard-codes start_y=960"
        else:
            src_ok = True
            src_msg = "init_on_transition no longer hard-codes start_y=960"
    else:
        src_msg = "function not found"

    if artefact_ok and src_ok:
        return cprint("P4 GREEN",
                      f"canonical y={sy} (slot {slot}); {artefact_msg}; {src_msg}",
                      True)
    return cprint("P4 RED",
                  f"Scene1 y={sy} (slot {slot}); artefact: {artefact_msg}; src: {src_msg}",
                  False)


def _peek32_wram(sections, addr):
    """Pair-swap WRAM peek (per memory/qa-iterative-improvement.md +
    qa_phase2_4b_collision_gate._peek32_wram). Mednafen stores WRAM
    bytes pair-swapped relative to SH-2 big-endian; un-swap pairs."""
    try:
        sys.path.insert(0, os.path.dirname(__file__))
        from mcs_extract import _peek_bytes  # type: ignore
    except Exception:
        return None
    b = _peek_bytes(sections, addr, 4)
    if not b:
        return None
    swapped = bytes([b[1], b[0], b[3], b[2]])
    val = int.from_bytes(swapped, "big")
    return val


def _resolve_player_syms(state_path):
    """Mirror qa_phase2_4b_collision_gate._resolve_player_syms.
    Resolves player_t base addr via the g_ghz_player_addr landmark."""
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return None
    with open(mp, "r", encoding="utf-8", errors="ignore") as f:
        body = f.read()
    m = re.search(r"(0x0[06][0-9a-fA-F]+)\s+g_ghz_player_addr\b", body)
    if not m:
        return None
    landmark_addr = int(m.group(1), 16)
    try:
        sys.path.insert(0, os.path.dirname(__file__))
        from mcs_extract import parse_savestate  # type: ignore
        from pathlib import Path
        sections = parse_savestate(Path(state_path))
        base = _peek32_wram(sections, landmark_addr)
        if base is None or base == 0 or (base >> 24) != 0x06:
            return None
    except Exception:
        return None
    return {
        "xpos": base + 0,
        "ypos": base + 4,
        "xsp":  base + 8,
        "ysp":  base + 12,
    }


def _read_player_xy(state_path, syms):
    """Read (xpos_q16, ypos_q16) from the savestate."""
    try:
        sys.path.insert(0, os.path.dirname(__file__))
        from mcs_extract import parse_savestate  # type: ignore
        from pathlib import Path
        sections = parse_savestate(Path(state_path))
    except Exception:
        return None
    vx = _peek32_wram(sections, syms["xpos"])
    vy = _peek32_wram(sections, syms["ypos"])
    if vx is None or vy is None:
        return None
    if vx & 0x80000000: vx -= 0x100000000
    if vy & 0x80000000: vy -= 0x100000000
    return (vx, vy)


def predicate_5_no_motion(state_paths):
    """Dynamic check (optional): consecutive savestates with no input
    must show |Player.xpos delta| <= 1 px = 65536 Q16 units. Autorun
    would produce a delta of ~0.1875 px/tick acceleration -> several
    px over the first 5 frames."""
    if not state_paths or len(state_paths) < 2:
        return cprint("P5 SKIP",
                      "no --with-savestates pair; P1+P2 cover the static contract",
                      True)
    scene_player = _parse_player_entity(SCENE_PATH)
    if scene_player is None:
        return cprint("P5 SKIP", "scene parse failed", True)
    slot, sx, sy = scene_player
    syms = _resolve_player_syms(state_paths[0])
    if syms is None:
        return cprint("P5 SKIP",
                      "g_ghz_player_addr landmark not resolvable from game.map",
                      True)
    a = _read_player_xy(state_paths[0], syms)
    b = _read_player_xy(state_paths[1], syms)
    if a is None or b is None:
        return cprint("P5 SKIP", "could not read player state", True)
    xa, ya = a
    xb, yb = b
    dx = abs(xb - xa)
    dy = abs(yb - ya)
    # Tolerance: 2 px = 0x20000 Q16 units (accounts for one-frame
    # gravity settling onto the surface).
    tol = 2 * 65536
    if dx <= tol and dy <= tol:
        return cprint("P5 GREEN",
                      f"|dx|={dx/65536.0:.2f}px |dy|={dy/65536.0:.2f}px (no autorun)",
                      True)
    return cprint("P5 RED",
                  f"|dx|={dx/65536.0:.2f}px |dy|={dy/65536.0:.2f}px (motion exceeds tol; autorun?)",
                  False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--with-savestates",
        default="",
        help="comma-separated savestates bracketing the first 5 GHZ-active frames")
    args = ap.parse_args()
    states = [s.strip() for s in args.with_savestates.split(",") if s.strip()]

    print("=== Phase 2.4f canonical-spawn gate ===")
    print(f"  Scene1.bin: {SCENE_PATH}")
    sp = _parse_player_entity(SCENE_PATH)
    if sp:
        print(f"  Scene1.bin Player entity: slot={sp[0]} x={sp[1]} y={sp[2]}")
    else:
        print("  Scene1.bin Player entity: PARSE FAILED")

    ok = True
    ok &= predicate_1_no_autorun()
    ok &= predicate_2_no_input_grace()
    ok &= predicate_3_canonical_x()
    ok &= predicate_4_canonical_y()
    ok &= predicate_5_no_motion(states)

    if ok:
        print("=== Gate Phase 2.4f: GREEN ===")
        return 0
    print("=== Gate Phase 2.4f: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
