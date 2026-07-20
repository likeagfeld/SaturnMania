#!/usr/bin/env python3
"""qa_p6_sfx_residency.py -- RED-first gate for the dead-gameplay-SFX defect.

MEASURED ROOT CAUSE (live, 2026-07-20, memory/dead-sfx-rootcause-f32-pool-
exhaustion): Mania decodes SFX to F32 in the ~32 KB Saturn DATASET_SFX WRAM pool
(4 B/sample -> a 1 s 44.1 kHz SFX ~176 KB). AllocateStorage fails after 1-2 SFX,
p6_saturn_sfx_pool_full latches, and every remaining LoadSfx is skipped -> only
3/256 sfxList entries ever load -> every gameplay GetSfx returns -1 -> PlaySfx
early-returns -> ZERO channel arming -> silent gameplay. The fix is the P6.8 item:
decode WAV -> S16 directly into SCSP sound RAM (skip the dead F32-in-WRAM buffer),
key-on an SCSP voice at PlaySfx.

This gate proves the defect from LIVE memory (RA must be up; run tools/_gl_boot.ps1
first and let the chain reach Green Hill Zone). It asserts, at a settled gameplay
scene:
  (1) LOADED  -- sfxList has >= SFX_MIN entries with scope!=0 (gameplay SFX loaded)
  (2) ARMED   -- p6_w_sfx_arm_events grows across the window (PlaySfx actually
                 arms channels as the player acts)

BOTH fail RED on the current build (3 loaded, 0 arming). GREEN requires the fix.
Exit 0 = GREEN, 1 = RED (defect present), 2 = live harness unhealthy.

Usage:  python tools/qa_p6_sfx_residency.py [WATCH_SECONDS]   (default 12)
"""
from __future__ import annotations

import importlib.util
import re
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_ROOT = _HERE.parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


qa_netmem = _load("qa_netmem", "qa_netmem.py")
MAP = (_ROOT / "game.map").read_text(errors="replace")

SFX_MIN = 8          # a settled gameplay scene must have loaded at least this many
SFXINFO_STRIDE = 32  # RETRO_HASH_MD5(16)+buffer(4)+length(4)+playCount(4)+2 -> 32
SCOPE_OFF = 29
WATCH = float(sys.argv[1]) if len(sys.argv) > 1 else 12.0


def sym(name):
    m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$", MAP, re.M)
    return int(m.group(1), 16) & 0xFFFFFFFF if m else None


def rb(mem, addr, n):
    out = bytearray()
    left, cur = n, addr
    while left > 0:
        take = min(1024, left)
        out += mem.read_saturn(cur, take)
        cur += take
        left -= take
    return bytes(out)


def main():
    sfxlist = sym("RSDK::sfxList")
    arm_sym = sym("p6_w_sfx_arm_events")
    if sfxlist is None or arm_sym is None:
        sys.stderr.write("qa_p6_sfx_residency: symbols missing from game.map "
                         f"(sfxList={sfxlist}, p6_w_sfx_arm_events={arm_sym}) -- "
                         "rebuild with the audio witnesses.\n")
        return 2
    try:
        mem = qa_netmem.RetroMem()
        raw = rb(mem, sfxlist, 256 * SFXINFO_STRIDE)
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"qa_p6_sfx_residency: LIVE HARNESS UNHEALTHY -- {e}\n")
        return 2

    loaded = [i for i in range(256) if raw[i * SFXINFO_STRIDE + SCOPE_OFF] != 0]

    # arm events across the window (Nyquist-proof 60Hz monotonic accumulator)
    def r32(a):
        b = mem.read_saturn(a, 4)
        return int.from_bytes(b, "big")

    a0 = r32(arm_sym)
    t0 = time.time()
    while time.time() - t0 < WATCH:
        time.sleep(1.0)
    a1 = r32(arm_sym)
    arm_delta = a1 - a0

    print("=" * 70)
    print("qa_p6_sfx_residency  (dead-gameplay-SFX gate; RED until the SCSP-S16 fix)")
    print("=" * 70)
    print(f"  sfxList entries loaded (scope!=0) : {len(loaded)}  (need >= {SFX_MIN})")
    print(f"      loaded slots                  : {loaded[:20]}")
    print(f"  sfx_arm_events delta over {WATCH:.0f}s     : {arm_delta}  (need > 0)")

    fail = []
    if len(loaded) < SFX_MIN:
        fail.append(f"only {len(loaded)} SFX loaded (< {SFX_MIN}) -- gameplay SFX not "
                    f"resident (F32-in-WRAM pool exhaustion)")
    if arm_delta <= 0:
        fail.append("zero PlaySfx arming across the window -- gameplay produced no SFX "
                    "(GetSfx returns -1 for unloaded SFX)")
    if fail:
        for f in fail:
            print(f"  RED  {f}")
        print("qa_p6_sfx_residency: RED -- dead gameplay SFX (this IS the defect the "
              "fix must clear).")
        return 1
    print("qa_p6_sfx_residency: GREEN -- gameplay SFX resident AND arming.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
