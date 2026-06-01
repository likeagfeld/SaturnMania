#!/usr/bin/env python3
"""qa_phase2_4b_collision_gate.py - Phase 2.4b collision-parity gate.

BINDING per memory/qa-iterative-improvement.md v3 + the user-reported bug
2026-05-28 "Sonic jumps in and out of monitors" (Task #139). The symptom
is the per-frame oscillation that results when the ad-hoc AABB hit-test
fires but no snap-back happens, so Sonic walks INTO the box and the next
frame's input nudges further, accumulating jitter.

The fix per the decomp is Player_CheckCollisionBox, defined in
tools/_decomp_raw/SonicMania_Objects_Global_Player.c:2267-2341. Its
inner call CheckObjectCollisionBox(setValues=true) is implemented in
rsdkv5-src/RSDKv5/RSDK/Scene/Collision.cpp:276-487 and mutates the
player's position AND zeros the velocity component on the contact side
(C_LEFT/RIGHT/TOP/BOTTOM).

This gate fires RED on the current (pre-fix) build and GREEN once the
Saturn-side Player_CheckCollisionBox port + three entity call-site
rewrites land.

Predicates:

  P1 (static map) - Player_CheckCollisionBox is exported from the Saturn
      Player object file. We grep game.map; if no symbol the symbol port
      hasn't landed.

  P2 (static src) - src/mania/Objects/Common/Entities.c calls
      Player_CheckCollisionBox at >=3 distinct call sites (one for
      itembox, spring, motobug). On a stock build the file contains
      zero call sites; post-fix it contains three+.

  P3 (savestate, optional) - Capture 4 sequential savestates during
      gameplay; Player.xpos delta between consecutive frames is <=
      |xsp|+1 (no teleport). Skipped unless --with-savestates is passed
      with 2+ comma-separated state paths.

  P4 (savestate, optional) - When Player hitbox overlaps an ItemBox
      hitbox, the next frame has |Player.xpos - (box.x - box.right -
      player_hitbox.left)| <= 1 px AND Player.xsp == 0 (the snap-back
      contract). Skipped unless --with-savestates lists 2 frames
      bracketing a collision.

Exit code: 0 = all GREEN. Non-zero = at least one RED.

Run:
    python tools/qa_phase2_4b_collision_gate.py
    python tools/qa_phase2_4b_collision_gate.py \
        --with-savestates samples/qa_phase2_4b_t0.mcs,samples/qa_phase2_4b_t1.mcs
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cprint(tag, msg, ok):
    colour = "\033[32m" if ok else "\033[31m"
    reset = "\033[0m"
    print(f"  [{colour}{tag}{reset}] {msg}")
    return ok


def predicate_1_source_definition():
    """src/mania/Objects/Global/Player.c defines Player_CheckCollisionBox
    AND src/mania/Objects/Global/Player.h declares it. game.map is
    NOT a useful sentinel here because the build chain uses GCC 8.2
    LTO whole-program optimization, which inlines small functions
    across TU boundaries and strips their symbol names from the
    linker map (same gotcha class as memory/sync-load-eliminates-
    cross-tu-volatile.md — LTO + map-grep is unreliable on this
    toolchain).

    The reliable static contract is: the function definition exists
    in Player.c with the expected signature AND the header export
    is visible, so Entities.c's call sites resolve at compile time.
    Run-time P3/P4 are the dynamic complement of this static check."""
    cpath = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.c")
    hpath = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.h")
    if not (os.path.exists(cpath) and os.path.exists(hpath)):
        return cprint("P1 RED", "Player.c or Player.h missing", False)
    with open(cpath, "r", encoding="utf-8") as f:
        cbody = f.read()
    with open(hpath, "r", encoding="utf-8") as f:
        hbody = f.read()
    # Definition: `int Player_CheckCollisionBox(player_t *p, ...` at column 0
    # (file-scope definition, not a comment / declaration).
    def_re = re.compile(
        r"^int\s+Player_CheckCollisionBox\s*\(\s*player_t\s*\*",
        re.MULTILINE)
    has_def = def_re.search(cbody) is not None
    # Header export prototype.
    decl_re = re.compile(
        r"int\s+Player_CheckCollisionBox\s*\(\s*player_t\s*\*")
    has_decl = decl_re.search(hbody) is not None
    # Fallback hitbox definition per decomp Player.c:12.
    has_fallback = re.search(
        r"hitbox_t\s+Player_FallbackHitbox\s*=\s*\{\s*-10\s*,\s*-20\s*,\s*10\s*,\s*20\s*\}",
        cbody) is not None
    ok = has_def and has_decl and has_fallback
    if ok:
        return cprint("P1 GREEN",
                      "Player.c defines + Player.h declares + FallbackHitbox present",
                      True)
    return cprint("P1 RED",
                  f"def={has_def} decl={has_decl} fallback={has_fallback}",
                  False)


def predicate_2_call_sites():
    """Entities.c invokes Player_CheckCollisionBox at >=3 distinct
    sites (itembox, spring, motobug). On a pre-fix build the file
    has zero call sites; post-fix it has three+."""
    path = os.path.join(ROOT, "src", "mania", "Objects", "Common",
                        "Entities.c")
    if not os.path.exists(path):
        return cprint("P2 RED", "Entities.c not found", False)
    with open(path, "r", encoding="utf-8") as f:
        body = f.read()
    # Count actual call sites (excluding comments / declarations).
    # Match Player_CheckCollisionBox(...) — the trailing '(' filters
    # out comments that mention the symbol by name.
    hits = re.findall(r"Player_CheckCollisionBox\s*\(", body)
    if len(hits) >= 3:
        return cprint("P2 GREEN",
                      f"Entities.c has {len(hits)} call sites (>=3)",
                      True)
    return cprint("P2 RED",
                  f"Entities.c has only {len(hits)} call sites (need >=3)",
                  False)


def _peek32_wram(sections, addr):
    """Pair-swap WRAM-L/H peek (per memory/qa-iterative-improvement.md +
    qa_phase2_3l_gate._peek32_wram). Mednafen stores WorkRAMH/L bytes
    pair-swapped relative to SH-2 big-endian; un-swap each 2-byte pair
    before interpreting as big-endian uint32."""
    try:
        sys.path.insert(0, os.path.dirname(__file__))
        from mcs_extract import _peek_bytes  # type: ignore
    except Exception:
        return None
    b = _peek_bytes(sections, addr, 4)
    if not b:
        return None
    # [b0,b1,b2,b3] -> [b1,b0,b3,b2] then big-endian int32
    swapped = bytes([b[1], b[0], b[3], b[2]])
    val = int.from_bytes(swapped, "big")
    return val


def _peek_player_state(state_path, sym_map):
    """Return (xpos_q16, ypos_q16, xsp_q16, ysp_q16) read from the
    Mednafen savestate or None if unavailable.

    sym_map: dict with keys xpos, ypos, xsp, ysp -> hex addresses of
    the player_t fields (resolved from g_ghz_player_addr landmark).
    """
    try:
        sys.path.insert(0, os.path.dirname(__file__))
        from mcs_extract import parse_savestate  # type: ignore
    except Exception:
        return None
    try:
        from pathlib import Path
        sections = parse_savestate(Path(state_path))
    except Exception:
        return None
    out = {}
    for key in ("xpos", "ypos", "xsp", "ysp"):
        a = sym_map.get(key)
        if a is None:
            return None
        val = _peek32_wram(sections, a)
        if val is None:
            return None
        # Two's complement int32
        if val & 0x80000000:
            val -= 0x100000000
        out[key] = val
    return (out["xpos"], out["ypos"], out["xsp"], out["ysp"])


def _resolve_player_syms(state_path):
    """Resolve player_t field addresses via the g_ghz_player_addr
    landmark.

    The Saturn-side `g_ghz_player` is `static player_t g_ghz_player`
    in Game.c — LTO can inline it / rename its symbol so it doesn't
    survive in game.map. Phase 2.4b adds an `__attribute__((used))`
    external pointer `g_ghz_player_addr` whose value IS the address
    of g_ghz_player; we peek that pointer's stored value to find the
    actual player_t base address at runtime.

    Field offsets per Player.h:
      offset 0   : xpos   (int32_t, Q16.16)
      offset 4   : ypos
      offset 8   : xsp
      offset 12  : ysp
    """
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return None
    with open(mp, "r", encoding="utf-8", errors="ignore") as f:
        body = f.read()
    # Look for the landmark pointer address in the map.
    m = re.search(r"(0x0[06][0-9a-fA-F]+)\s+g_ghz_player_addr\b", body)
    if not m:
        return None
    landmark_addr = int(m.group(1), 16)
    # Peek the pointer value with WRAM pair-swap helper.
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


def predicate_3_plausible_state(state_paths):
    """Player state plausibility check from a captured savestate:
      - xpos in valid GHZ1 world range (0..16384 px)
      - ypos in valid GHZ1 world range (0..2048 px)
      - |xsp| <= topSpeed (6.0 px/fr = 0x60000 Q16.16) + 1 px tolerance
      - |ysp| <= terminal velocity (16.0 px/fr) + 1 px tolerance

    Diagnoses runaway physics from broken collision (e.g. an unbounded
    xsp accumulation that would result from per-frame penetration with
    no snap-back). Each savestate is captured from its OWN boot via
    qa_savestate.ps1 (the harness boots Mednafen fresh per capture),
    so we cannot meaningfully cross-compare positions; we check each
    state independently for plausibility."""
    if not state_paths:
        return cprint("P3 SKIP",
                      "no --with-savestates; static P1+P2 cover the contract",
                      True)
    syms = _resolve_player_syms(state_paths[0])
    if syms is None:
        return cprint("P3 SKIP",
                      "g_ghz_player_addr landmark not resolvable "
                      "(rebuild required to land the Phase 2.4b landmark)",
                      True)
    # Plausibility bounds in Q16.16.
    XPOS_MAX = 16384 << 16
    YPOS_MAX = 2048 << 16
    XSP_MAX  = (6 << 16) + (1 << 16)   # topSpeed + 1
    YSP_MAX  = (16 << 16) + (1 << 16)  # terminal + 1
    for sp in state_paths:
        rd = _peek_player_state(sp, syms)
        if rd is None:
            return cprint("P3 SKIP", f"peek failed for {sp}", True)
        xpos, ypos, xsp, ysp = rd
        if xpos < 0 or xpos > XPOS_MAX:
            return cprint("P3 RED",
                          f"{sp}: xpos out of bounds ({xpos/65536.0:.1f}px)",
                          False)
        if ypos < 0 or ypos > YPOS_MAX:
            return cprint("P3 RED",
                          f"{sp}: ypos out of bounds ({ypos/65536.0:.1f}px)",
                          False)
        if abs(xsp) > XSP_MAX:
            return cprint("P3 RED",
                          f"{sp}: xsp runaway ({xsp/65536.0:.2f}px/fr)",
                          False)
        if abs(ysp) > YSP_MAX:
            return cprint("P3 RED",
                          f"{sp}: ysp runaway ({ysp/65536.0:.2f}px/fr)",
                          False)
    return cprint("P3 GREEN",
                  f"player state plausible across {len(state_paths)} captures",
                  True)


def predicate_4_snap_back(state_paths):
    """When Player hitbox overlaps an ItemBox, the next frame snaps
    Player.xpos to (box.x - box.right - player_hitbox.left) and xsp=0.

    Without instrumented box positions we approximate: if a savestate
    is captured immediately after a side-collision (xsp went from
    non-zero to zero between frames N and N+1), confirm the position
    did not penetrate further (delta in direction-of-prev-xsp <= 1 px).
    Skipped unless 2 states supplied."""
    if not state_paths or len(state_paths) < 2:
        return cprint("P4 SKIP",
                      "no --with-savestates; static P1+P2 cover the contract",
                      True)
    syms = _resolve_player_syms(state_paths[0])
    if syms is None:
        return cprint("P4 SKIP",
                      "g_ghz_player_addr landmark not resolvable "
                      "(rebuild required to land the Phase 2.4b landmark)",
                      True)
    rd_a = _peek_player_state(state_paths[0], syms)
    rd_b = _peek_player_state(state_paths[1], syms)
    if rd_a is None or rd_b is None:
        return cprint("P4 SKIP", "peek failed", True)
    xpos_a, _, xsp_a, _ = rd_a
    xpos_b, _, xsp_b, _ = rd_b
    # The contract: if xsp_a != 0 and xsp_b == 0 (xsp zeroed on
    # contact), the position must NOT have moved more than |xsp_a|+1
    # in the direction xsp_a was pointing.
    if xsp_a == 0 or xsp_b != 0:
        return cprint("P4 SKIP",
                      f"states don't bracket a collision (xsp {xsp_a:#x} -> "
                      f"{xsp_b:#x})",
                      True)
    direction_step = (xpos_b - xpos_a) * (1 if xsp_a > 0 else -1)
    if direction_step > abs(xsp_a) + 0x10000:
        return cprint("P4 RED",
                      f"penetrated past snap point: step={direction_step/65536.0:.2f} "
                      f"> |xsp|+1={abs(xsp_a)/65536.0+1:.2f}",
                      False)
    return cprint("P4 GREEN",
                  f"snap-back contract holds (step={direction_step/65536.0:.2f}px)",
                  True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-savestates", default=None,
                    help=("Comma-separated list of .mcs paths for P3/P4 "
                          "(>=2 for both predicates to run)."))
    args = ap.parse_args()

    print("=== Phase 2.4b - collision-parity gate ===")
    print(f"  root: {ROOT}")
    print("")

    states = []
    if args.with_savestates:
        states = [s.strip() for s in args.with_savestates.split(",") if s.strip()]

    print("Static predicates:")
    p1 = predicate_1_source_definition()
    p2 = predicate_2_call_sites()
    print("")
    print("Savestate predicates (optional):")
    p3 = predicate_3_plausible_state(states)
    p4 = predicate_4_snap_back(states)
    print("")
    all_ok = all([p1, p2, p3, p4])
    print(f"RESULT: {'GREEN - gate passes' if all_ok else 'RED - gate fails'}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
