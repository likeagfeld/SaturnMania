#!/usr/bin/env python3
"""qa_title_dat_pool_gate.py - TITLE.DAT jo-pool-leak gate (#187).

The HUD-present investigation (#186) MEASURED that the jo malloc pool
(256 KB) is exhausted at GHZ gameplay (~8.9 KB free) because a 114688-byte
block (= TITLE_BG_W * TITLE_BG_H = 224 * 512 = the TITLE.DAT backdrop
bitmap) is still resident in the pool during gameplay. jo's allocator
never splits a reused block (malloc.c:127-131) and jo_free either pops the
zone high or marks the block free (malloc.c:189-192), so whether TITLE.DAT
was never freed OR was freed then recycled-whole by a smaller later alloc,
the 114 KB is wasted either way.

Fix: stage TITLE.DAT through the GHZ FG.TMP LWRAM region (0x00210000,
320 KB, provably free during the title phase per storage.c:211-221) via
jo_fs_read_file_ptr (pool-free GFS read). The pixels are copied into VDP2
VRAM by jo_img_to_vdp2_cells during the one-shot bind, so the staging
buffer is dead the instant setup_title_bg returns and is reclaimed by the
GHZ FG.TMP load at the title->GHZ transition. Same LWRAM-bypass mechanism
as memory/ghz-sky-dat-lwram-bypass.md and the #186 HUD fix.

Predicates
----------
STATIC (default; no emulator):

  T1  src/main.c setup_title_bg loads TITLE.DAT via jo_fs_read_file_ptr
      (the pool-free scratch form) and NOT jo_fs_read_file (the pool form).
      The LWRAM staging pointer must NOT be passed to jo_free.

RUNTIME (--runtime; needs tools/_hud_states/gameplay.mcs from
tools/qa_savestate.ps1):

  T2  Peek g_hud_diag_poolok from the gameplay state. The HUD-load probe
      mallocs HUD.SP2's 27536 bytes during gameplay; poolok == 1 means the
      pool had room (>=112 KB recovered), poolok == 0 means still
      exhausted. Require poolok == 1. The capture must be in gameplay
      (s_ts_state == 6 = TS_GHZ_ACTIVE), else rejected (not a false RED).

Exit codes:
    0 = GREEN (TITLE.DAT off the pool; pool recovered)
    1 = RED   (TITLE.DAT still pooled / pool still exhausted)
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

MAIN_C = os.path.join(REPO, "src", "main.c")
GAME_MAP = os.path.join(REPO, "game.map")
STATE = os.path.join(HERE, "_hud_states", "gameplay.mcs")


def check_t1():
    if not os.path.exists(MAIN_C):
        return False, "T1 FAIL: src/main.c not found"
    with open(MAIN_C, "r", errors="ignore") as f:
        txt = f.read()
    # Isolate the setup_title_bg function body.
    m = re.search(r"setup_title_bg\s*\(\s*void\s*\)\s*\{", txt)
    if not m:
        return False, "T1 FAIL: setup_title_bg(void) not found in main.c"
    body = txt[m.end():m.end() + 8000]

    pooled = re.search(r'jo_fs_read_file\s*\(\s*"TITLE\.DAT"', body)
    if pooled:
        return False, ('T1 FAIL: TITLE.DAT still loaded via jo_fs_read_file '
                       '(pool path) -- 114 KB stays resident in the jo pool')

    scratch = re.search(r'jo_fs_read_file_ptr\s*\(\s*"TITLE\.DAT"', body)
    if not scratch:
        return False, ('T1 FAIL: TITLE.DAT not loaded via jo_fs_read_file_ptr '
                       '(pool-free scratch path)')

    # The staging pointer must never be jo_free'd (it is LWRAM, not pool).
    # Guard: the free site must skip jo_free when the dat is the LWRAM stage.
    freed = re.search(r"jo_free\s*\(\s*s_title_bg_dat\s*\)", txt)
    if freed:
        return False, ('T1 FAIL: s_title_bg_dat (LWRAM stage) still passed to '
                       'jo_free -- would corrupt the jo zone header')

    return True, ("T1 OK (TITLE.DAT via jo_fs_read_file_ptr LWRAM scratch; "
                  "not pooled, not jo_free'd)")


def _map_text():
    with open(GAME_MAP, "r", errors="ignore") as f:
        return f.read()


def _sym_addr(txt, sym):
    m = re.search(r"0x([0-9a-fA-F]{8,})\s+_?" + re.escape(sym) +
                  r"(?![A-Za-z0-9_])", txt)
    return int(m.group(1), 16) if m else None


def check_t2():
    if not os.path.exists(STATE):
        sys.stderr.write(
            "T2 HARNESS GAP: gameplay savestate not captured.\n"
            "  Capture it with:\n"
            "    pwsh tools/qa_savestate.ps1 -Cue game.cue -PressStartAt 30 "
            "-SaveFrame 44 -Out tools/_hud_states/gameplay.mcs\n"
            "  then re-run with --runtime.\n")
        return None, "T2 SKIPPED (gameplay state not captured)"

    import mcs_extract  # noqa: E402
    from pathlib import Path

    if not os.path.exists(GAME_MAP):
        return False, "T2 FAIL: game.map not found (build first)"
    txt = _map_text()
    a_pool = _sym_addr(txt, "g_hud_diag_poolok")
    a_ts = _sym_addr(txt, "s_ts_state")
    if a_pool is None:
        return False, "T2 FAIL: g_hud_diag_poolok address not in game.map"

    sections = mcs_extract.parse_savestate(Path(STATE))

    def s32(addr):
        b = mcs_extract._peek_bytes(sections, addr, 4)
        v = (b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]
        return struct.unpack(">i", struct.pack(">I", v))[0]

    def u32(addr):
        b = mcs_extract._peek_bytes(sections, addr, 4)
        return (b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]

    poolok = s32(a_pool)
    ts = u32(a_ts) if a_ts is not None else -1
    pool_name = {1: "pool-OK (malloc succeeded)", 0: "POOL-EXHAUSTED",
                 -1: "skipped (fid<0)"}.get(poolok, "?")
    print(f"      T2 diag: poolok={poolok}({pool_name}) ts={ts}")

    if a_ts is not None and ts != 6:
        return False, (f"T2 REJECT: state not in gameplay (s_ts_state={ts}, "
                       f"need 6=TS_GHZ_ACTIVE); recapture deeper")

    if poolok != 1:
        return False, (f"T2 FAIL: jo pool still exhausted at gameplay "
                       f"(poolok={poolok}); TITLE.DAT 114 KB not recovered")
    return True, ("T2 OK (poolok=1; pool has room for the 27536 HUD probe "
                  "-- 114 KB TITLE.DAT recovered)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runtime", action="store_true",
                    help="also peek the gameplay savestate pool probe")
    args = ap.parse_args()

    print("TITLE.DAT POOL-LEAK GATE (#187)")
    print("-" * 60)
    results = []
    ok, msg = check_t1()
    results.append(ok)
    print(("  PASS " if ok else "  FAIL ") + msg)

    if args.runtime:
        ok, msg = check_t2()
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
        print("GREEN: TITLE.DAT off the jo pool")
        sys.exit(0)
    print("RED: TITLE.DAT still pooled / pool still exhausted")
    sys.exit(1)


if __name__ == "__main__":
    main()
