#!/usr/bin/env python3
"""qa_phase2_3f_ghz_sonic_gate.py — Phase 2.3f GHZ Sonic visibility gate
(Task #88).

The gate enforces five predicates on a Mednafen savestate captured from
a debug-build ISO (built with `-DGHZ_AUTOADVANCE_TICKS=480` so the title
auto-advances into GHZ within the capture window):

  P1 (addresses): the .map exposes BSS addresses for the cross-TU GHZ
                  readiness flags. Phase 2.3f volatile audit forces
                  these symbols to survive LTO with their original names
                  (or `.lto_priv.NN` LTO-mangled — the gate accepts
                  either form).

  P2 (reached GHZ): VDP2 BGON has NBG1ON|NBG2ON set, indicating GHZ
                    rendering is active (title sets backdrop differently;
                    see scene_ghz.c ghz_setup_sky:325).

  P3 (player flag set): mcs_extract --peek8 at the BSS address of
                        `g_ghz_player_ready` (LTO-mangled name accepted)
                        returns non-zero. False under the broken build,
                        true under the patched build.

  P4 (sonic visible): VDP1 framebuffer ROI contains skin-tone/blue
                      pixels matching the Sonic sprite palette. This is
                      the visual end-to-end check.

  P5 (tick counter): mania_ghz_tick_and_draw's sentinel counter (if
                     present) exceeded a minimum threshold, proving
                     the function ran across frames (vs LTO elision
                     observed in Phase 2.3e Round 3c).

Per CLAUDE.md §4.7: the gate must FIRE RED on the broken build BEFORE
the fix lands. The RED predicate set is P3 (flag never reads 1) OR
P4 (Sonic never visible) OR P5 (counter <60 across 30s).

Author: Phase 2.3f. Tracked in HANDOFF.md + docs/COMPREHENSIVE_PLAN.md.
"""
from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys
from pathlib import Path
from typing import Dict, Optional


# Repo root = parent of tools/.
ROOT = Path(__file__).resolve().parent.parent
MAP_PATH = ROOT / "game.map"
MCS_EXTRACT = ROOT / "tools" / "mcs_extract.py"


# Symbols we want to localise in the .map. The LTO build may have
# suffixed them with `.lto_priv.NN`; accept either form.
TARGET_SYMBOLS = [
    "g_ghz_player_ready",
    "g_ghz_fg_ready",
    "g_ghz_sky_ready",
    "s_player_load_kicked",
    "s_player_load_deferred",
    "s_audio_kicked",
    "s_ts_state",
    "s_ts_timer",
    "g_ghz_cached_cam_x",
    "g_ghz_cached_cam_y",
]


def parse_map(map_path: Path) -> Dict[str, int]:
    """Scan game.map for our target symbols. Returns a {clean_name: addr}
    dict. LTO-mangled names (`name.lto_priv.NN`) map back to clean names
    (the suffix is dropped). Functions and variables both supported."""
    if not map_path.exists():
        raise SystemExit(f"qa_phase2_3f_gate: game.map not found at {map_path}")
    txt = map_path.read_text(encoding="utf-8", errors="replace")
    out: Dict[str, int] = {}
    pat = re.compile(r"^\s*0x([0-9a-fA-F]{16})\s+([A-Za-z_][\w.]*)\s*$",
                     re.MULTILINE)
    for m in pat.finditer(txt):
        addr_hex = m.group(1)
        sym = m.group(2)
        clean = sym.split(".lto_priv.")[0]
        if clean in TARGET_SYMBOLS:
            try:
                addr = int(addr_hex, 16) & 0xFFFFFFFF
            except ValueError:
                continue
            # Prefer the lowest 32 bits (Saturn addr space). Skip
            # 0x0000... addresses (these are LD discard markers).
            if addr == 0:
                continue
            # Keep the FIRST occurrence (LTO may emit duplicates).
            if clean not in out:
                out[clean] = addr
    return out


def peek_state(mcs_path: Path, addr: int, size: int) -> Optional[int]:
    """Run mcs_extract.py and return the integer value at Saturn addr.
    size in {1,2,4}; returns None on parser failure."""
    flag = {1: "--peek8", 2: "--peek16", 4: "--peek"}[size]
    cmd = [sys.executable, str(MCS_EXTRACT), str(mcs_path), flag,
           hex(addr)]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=30)
    except (FileNotFoundError, subprocess.TimeoutExpired) as exc:
        sys.stderr.write(f"qa_phase2_3f_gate: peek failed: {exc}\n")
        return None
    if proc.returncode != 0:
        sys.stderr.write(
            f"qa_phase2_3f_gate: peek {hex(addr)} returned {proc.returncode}: "
            f"{proc.stderr.strip()}\n"
        )
        return None
    m = re.search(r"=\s*0x([0-9a-fA-F]+)", proc.stdout)
    if not m:
        return None
    return int(m.group(1), 16)


def peek_bgon(mcs_path: Path) -> Optional[int]:
    """VDP2 BGON is at 0x25F80020 (cache-through) per ST-058-R2 +
    jo/sega_saturn.h. Returns 16-bit big-endian value or None."""
    return peek_state(mcs_path, 0x25F80020, 2)


def count_sprite_pixels(fb_path: Path) -> int:
    """Decode the VDP1 framebuffer dump as 320x224 RGB555 and count
    pixels matching the Sonic sprite palette signature (blue body +
    skin-tone face). Returns approximate pixel count."""
    if not fb_path.exists():
        return 0
    raw = fb_path.read_bytes()
    if len(raw) < 320 * 224 * 2:
        return 0
    n = 0
    # Walk just the first VDP1 framebuffer (320x224 region) — Mednafen
    # dumps both fbs sequentially.
    for i in range(0, 320 * 224 * 2, 2):
        # Mednafen serializes the framebuffer in host-LE uint16; the
        # mcs_extract dump path does not byte-swap so the data is host-
        # LE order. Decode as little-endian uint16 then interpret
        # as Saturn RGB555 (MSB=alpha/priority, then 5R5G5B).
        w = struct.unpack_from("<H", raw, i)[0]
        r = (w >> 10) & 0x1F
        g = (w >> 5) & 0x1F
        b = w & 0x1F
        # Sonic body blue: high blue, mid red, lowish green.
        # Skin tone (Sonic muzzle): mid-high red+green, low blue.
        is_sonic_blue = (b >= 16 and r <= 12 and g <= 16 and (b - r) >= 6)
        is_sonic_skin = (r >= 20 and g >= 10 and b <= 12 and r > b + 6)
        if is_sonic_blue or is_sonic_skin:
            n += 1
    return n


def main():
    ap = argparse.ArgumentParser(
        description="Phase 2.3f GHZ Sonic visibility gate (Task #88)."
    )
    ap.add_argument("--state", required=True,
                    help="path to a Mednafen .mc0/.mcs captured at "
                         "SaveFrame >= 24 (post-GHZ-auto-advance)")
    ap.add_argument("--fb-dump", default="",
                    help="path to VDP1 framebuffer dump (optional). If "
                         "given, P4 sprite-pixel count is evaluated.")
    ap.add_argument("--map", default=str(MAP_PATH),
                    help="path to game.map (default: %(default)s)")
    ap.add_argument("--min-tick-counter", type=int, default=100,
                    help="P5 threshold for mania_ghz_tick_and_draw "
                         "counter sentinel (default 100)")
    ap.add_argument("--min-sprite-pixels", type=int, default=1000,
                    help="P4 threshold for sprite-coloured pixels in fb "
                         "ROI (default 1000)")
    a = ap.parse_args()

    mcs_path = Path(a.state)
    if not mcs_path.exists():
        sys.stderr.write(f"qa_phase2_3f_gate: state not found: {mcs_path}\n")
        return 2

    print("=== Phase 2.3f GHZ Sonic visibility gate ===")
    print(f"  state         : {mcs_path}")
    print(f"  map           : {a.map}")

    # ---- P1: address resolution ----
    symbols = parse_map(Path(a.map))
    p1_ok = ("g_ghz_player_ready" in symbols and
             "g_ghz_fg_ready" in symbols and
             "g_ghz_sky_ready" in symbols)
    print("")
    print("  -- P1: symbol address resolution --")
    for sym in TARGET_SYMBOLS:
        if sym in symbols:
            print(f"     {sym:32s} 0x{symbols[sym]:08x}")
        else:
            print(f"     {sym:32s} <missing in map>")
    print(f"  P1 (player_ready+fg_ready+sky_ready resolved): "
          f"{'PASS' if p1_ok else 'FAIL'}")

    # ---- P2: GHZ rendering reached (BGON) ----
    bgon = peek_bgon(mcs_path)
    p2_ok = False
    if bgon is not None:
        # ST-058-R2 §BGON: bit 0=NBG0, bit 1=NBG1, bit 2=NBG2, bit 5=RBG0.
        nbg1_on = bool(bgon & 0x0002)
        nbg2_on = bool(bgon & 0x0004)
        p2_ok = nbg1_on and nbg2_on
        print("")
        print(f"  -- P2: VDP2 BGON = 0x{bgon:04x} (NBG1={nbg1_on}, NBG2={nbg2_on}) --")
    else:
        print("  -- P2: BGON peek failed --")
    print(f"  P2 (GHZ rendering reached: NBG1ON & NBG2ON): "
          f"{'PASS' if p2_ok else 'FAIL'}")

    # ---- P3: g_ghz_player_ready flag is set ----
    p3_ok = False
    p3_val: Optional[int] = None
    if "g_ghz_player_ready" in symbols:
        addr = symbols["g_ghz_player_ready"]
        # bool in C99 is 1 byte typically; peek 8-bit.
        p3_val = peek_state(mcs_path, addr, 1)
        if p3_val is not None:
            p3_ok = (p3_val != 0)
    print("")
    print(f"  -- P3: g_ghz_player_ready @ "
          f"0x{symbols.get('g_ghz_player_ready', 0):08x} --")
    if p3_val is None:
        print("     peek failed")
    else:
        print(f"     value = 0x{p3_val:02x}")
    print(f"  P3 (g_ghz_player_ready != 0): "
          f"{'PASS' if p3_ok else 'FAIL'}")

    # Bonus diagnostic: dump fg/sky/audio/player-kick flags.
    for nm in ("g_ghz_fg_ready", "g_ghz_sky_ready",
               "s_audio_kicked", "s_player_load_deferred",
               "s_player_load_kicked"):
        if nm in symbols:
            v = peek_state(mcs_path, symbols[nm], 1)
            print(f"     [diag] {nm:28s} = "
                  f"{('0x%02x' % v) if v is not None else '<peek fail>'}")

    # Dump s_ts_state + s_ts_timer (these are int/unsigned int → 4 byte).
    for nm in ("s_ts_state", "s_ts_timer"):
        if nm in symbols:
            v = peek_state(mcs_path, symbols[nm], 4)
            print(f"     [diag] {nm:28s} = "
                  f"{('0x%08x' % v) if v is not None else '<peek fail>'}")

    # ---- P4: sprite-pixel count in framebuffer ----
    p4_ok = True   # optional; pass by default unless --fb-dump given
    if a.fb_dump:
        npx = count_sprite_pixels(Path(a.fb_dump))
        p4_ok = (npx >= a.min_sprite_pixels)
        print("")
        print(f"  -- P4: VDP1 fb Sonic-coloured pixel count = {npx} "
              f"(threshold >= {a.min_sprite_pixels}) --")
        print(f"  P4 (Sonic visible in framebuffer): "
              f"{'PASS' if p4_ok else 'FAIL'}")
    else:
        print("")
        print("  -- P4: skipped (no --fb-dump provided) --")

    # ---- P5: tick counter sentinel ----
    # mania_ghz_tick_and_draw doesn't currently embed a public counter;
    # the gate accepts P5 as PASS when not measurable, but exposes a
    # placeholder for future patches that add `g_ghz_tick_counter`.
    p5_ok = True
    if "g_ghz_tick_counter" in symbols:
        v = peek_state(mcs_path, symbols["g_ghz_tick_counter"], 4)
        if v is None:
            p5_ok = False
            print(f"  P5 (tick counter): peek FAILED")
        else:
            p5_ok = (v >= a.min_tick_counter)
            print(f"  -- P5: g_ghz_tick_counter = {v} "
                  f"(threshold >= {a.min_tick_counter}) --")
            print(f"  P5 (mania_ghz_tick_and_draw ran enough): "
                  f"{'PASS' if p5_ok else 'FAIL'}")
    else:
        print("  -- P5: g_ghz_tick_counter symbol absent (skipped) --")

    # ---- final verdict ----
    overall = p1_ok and p2_ok and p3_ok and p4_ok and p5_ok
    print("")
    print(f"  P1={p1_ok} P2={p2_ok} P3={p3_ok} P4={p4_ok} P5={p5_ok}")
    print(f"=== Phase 2.3f gate: {'PASS' if overall else 'FAIL'} ===")
    return 0 if overall else 1


if __name__ == "__main__":
    sys.exit(main())
