#!/usr/bin/env python3
"""qa_ghz_fgcel_lwram_gate.py - GHZ FG.CEL pool-exhaustion gate (#188).

MEASURED root cause (tools/qa_pool_walk.py against a gameplay-attempt
savestate): the jo malloc pool is a single 256 KB zone holding 207 KB of
resident allocations at the title->GHZ transition, leaving a 48 KB free
tail with ZERO interior free blocks (no fragmentation). cd/GHZ1FG.CEL is
89984 bytes (88 KB) and was loaded via jo_fs_read_file (pool path); 88 KB
does not fit in the 48 KB tail, so jo_fs_read_file returns NULL,
ghz_setup_foreground sets g_ghz_load_error_code |= 0x04 (bit 2 = FG.CEL)
and returns false, and the TS_TRANSITION_TO_GHZ state retries forever
(s_ts_state stuck at 5, never reaching 6 = TS_GHZ_ACTIVE).

#187 (TITLE.DAT off the pool) EXPOSED this: the pre-#187 build only
reached gameplay because freeing TITLE.DAT (114 KB) left an interior free
hole that FG.CEL (88 KB < 114 KB) reused whole (jo never splits/coalesces,
malloc.c:127-133). Removing TITLE.DAT removed the accidental crutch.

Fix (binding memory/ghz-sky-dat-lwram-bypass.md, 5th application after
FG.TMP/SKY.DAT/HUD/TITLE.DAT): load FG.CEL into a dedicated LWRAM region
(0x002D0000, 192 KB free per the storage.c LWRAM map) via
rsdk_storage_load_to_lwram, bypassing the 256 KB pool entirely. The cel
buffer is consumed by jo_img_to_vdp2_cells (SKY.DAT proves the LWRAM read)
+ slDMACopy(cel->VRAM) (archived main.c proves the cel->VRAM CPU DMA), then
dead -- so the LWRAM staging is never jo_free'd.

Predicates
----------
STATIC (default; no emulator):

  T1  src/rsdk/scene_ghz.c ghz_setup_foreground loads FG.CEL via
      rsdk_storage_load_to_lwram (the pool-free LWRAM path) and NOT
      jo_fs_read_file (the pool path). The cel staging pointer must NOT be
      passed to jo_free.

RUNTIME (--runtime; needs a gameplay-attempt savestate, default
tools/_hud_states/gameplay.mcs):

  T2  Peek g_ghz_load_error_code and s_ts_state. GREEN requires
      error_code == 0 (no GHZ asset-load failure) AND s_ts_state == 6
      (TS_GHZ_ACTIVE -- the transition completed, no hang). RED on the
      current build: error_code == 4 (bit 2 FG.CEL) and s_ts_state == 5.

Exit codes:
    0 = GREEN (FG.CEL off the pool; transition reaches gameplay)
    1 = RED   (FG.CEL still pooled / transition still hangs)
    2 = harness error / runtime state not captured
"""
from __future__ import annotations
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

SCENE_GHZ_C = os.path.join(REPO, "src", "rsdk", "scene_ghz.c")
GAME_MAP = os.path.join(REPO, "game.map")
STATE = os.path.join(HERE, "_hud_states", "gameplay.mcs")


def check_t1():
    if not os.path.exists(SCENE_GHZ_C):
        return False, "T1 FAIL: src/rsdk/scene_ghz.c not found"
    with open(SCENE_GHZ_C, "r", errors="ignore") as f:
        txt = f.read()
    m = re.search(r"bool\s+ghz_setup_foreground\s*\(\s*int\s+act\s*\)\s*\{", txt)
    if not m:
        return False, "T1 FAIL: ghz_setup_foreground(int act) not found"
    # Body up to the next top-level function (ghz_setup_sky).
    end = txt.find("bool ghz_setup_sky", m.end())
    body = txt[m.end():end if end > 0 else m.end() + 6000]

    pooled = re.search(r'jo_fs_read_file\s*\(\s*\(char\s*\*\)\s*ghz_path\(act,\s*"FG\.CEL"', body)
    if pooled:
        return False, ('T1 FAIL: FG.CEL still loaded via jo_fs_read_file '
                       '(pool path) -- 88 KB cannot fit the 48 KB free tail')

    # FG.CEL must reach a buffer via the LWRAM helper. Accept either a
    # direct rsdk_storage_load_to_lwram("FG.CEL") call or a cel pointer set
    # to a GHZ_FG_CEL_LWRAM_ADDR-style staging address.
    lwram = (re.search(r'rsdk_storage_load_to_lwram\s*\(\s*\(char\s*\*\)\s*ghz_path\(act,\s*"FG\.CEL"', body)
             or re.search(r'GHZ_FG_CEL_LWRAM_ADDR', body))
    if not lwram:
        return False, ('T1 FAIL: FG.CEL not loaded via rsdk_storage_load_to_lwram '
                       '/ GHZ_FG_CEL_LWRAM_ADDR (pool-free LWRAM path)')

    # The cel/LWRAM staging must never be jo_free'd.
    if re.search(r"jo_free\s*\(\s*cel\s*\)", body):
        return False, ('T1 FAIL: cel (LWRAM stage) still passed to jo_free '
                       '-- LWRAM is not pool memory')

    return True, ("T1 OK (FG.CEL via LWRAM bypass; not pooled, not jo_free'd)")


def _map_text():
    with open(GAME_MAP, "r", errors="ignore") as f:
        return f.read()


def _sym_addr(txt, sym):
    m = re.search(r"0x([0-9a-fA-F]{8,})\s+_?" + re.escape(sym) +
                  r"(?![A-Za-z0-9_])", txt)
    return int(m.group(1), 16) if m else None


def check_t2(state_path):
    if not os.path.exists(state_path):
        sys.stderr.write(
            "T2 HARNESS GAP: gameplay-attempt savestate not captured.\n"
            f"  Expected: {state_path}\n"
            "  Capture with tools/qa_savestate.ps1 (press START, deep SaveFrame),\n"
            "  then re-run with --runtime.\n")
        return None, "T2 SKIPPED (state not captured)"

    import mcs_extract
    from pathlib import Path

    if not os.path.exists(GAME_MAP):
        return False, "T2 FAIL: game.map not found (build first)"
    txt = _map_text()
    a_err = _sym_addr(txt, "g_ghz_load_error_code")
    a_ts = _sym_addr(txt, "s_ts_state")
    if a_err is None:
        return False, "T2 FAIL: g_ghz_load_error_code not in game.map"
    if a_ts is None:
        return False, "T2 FAIL: s_ts_state not in game.map"

    sections = mcs_extract.parse_savestate(Path(state_path))

    def u32(addr):
        b = mcs_extract._peek_bytes(sections, addr, 4)
        # WRAM-H pair-swap (qa_title_dat_pool_gate.py convention).
        return (b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]

    err = u32(a_err)
    ts = u32(a_ts)
    bits = []
    for bit, name in ((0, "FG.TMP"), (1, "FG.PAL"), (2, "FG.CEL"),
                      (3, "FG.PAT"), (4, "SKY.PAL"), (5, "SKY.DAT")):
        if err & (1 << bit):
            bits.append(name)
    ts_name = {0: "FADE_IN", 1: "FLASH", 2: "WAIT_SONIC", 3: "PRESS_REVEAL",
               4: "WAIT_ENTER", 5: "TRANSITION_TO_GHZ", 6: "GHZ_ACTIVE"}.get(ts, "?")
    print(f"      T2 diag: g_ghz_load_error_code=0x{err:x} "
          f"({','.join(bits) if bits else 'none'})  "
          f"s_ts_state={ts}({ts_name})")

    if ts != 6:
        return False, (f"T2 FAIL: transition did not complete "
                       f"(s_ts_state={ts}={ts_name}, need 6=GHZ_ACTIVE); "
                       f"load error bits=[{','.join(bits) if bits else 'none'}]")
    if err != 0:
        return False, (f"T2 FAIL: GHZ load error still set "
                       f"(0x{err:x}=[{','.join(bits)}])")
    return True, "T2 OK (error_code=0, s_ts_state=6 TS_GHZ_ACTIVE -- transition completed)"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runtime", action="store_true",
                    help="also peek the gameplay savestate load-error + state")
    ap.add_argument("--state", default=STATE,
                    help="savestate path for the runtime check")
    args = ap.parse_args()

    print("GHZ FG.CEL POOL-EXHAUSTION GATE (#188)")
    print("-" * 60)
    results = []
    ok, msg = check_t1()
    results.append(ok)
    print(("  PASS " if ok else "  FAIL ") + msg)

    if args.runtime:
        ok, msg = check_t2(args.state)
        if ok is None:
            print("  SKIP " + msg)
            print("-" * 60)
            print("HARNESS GAP: runtime state not captured; static verdict "
                  "stands but T2 unverified.")
            sys.exit(2)
        results.append(ok)
        print(("  PASS " if ok else "  FAIL ") + msg)

    print("-" * 60)
    if all(results):
        print("GREEN: FG.CEL off the jo pool; transition reaches gameplay")
        sys.exit(0)
    print("RED: FG.CEL still pooled / transition still hangs")
    sys.exit(1)


if __name__ == "__main__":
    main()
