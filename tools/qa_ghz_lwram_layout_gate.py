#!/usr/bin/env python3
"""qa_ghz_lwram_layout_gate.py - Task #180 step 4c (LWRAM carve safety).

HOST-VERIFIABLE structural gate proving the GHZ collision LWRAM carve never
overlaps a live neighbour. It parses the carve #defines straight out of the
two C sources that own them and cross-checks them against the ACTUAL shipped
asset byte sizes:

  src/mania/Game.c  -> GHZ_MASK_LWRAM_ADDR / _CAP / GHZ_MASK_FILE_SIZE
  src/rsdk/colwindow.c -> COLWINDOW_LWRAM_WIN_ADDR / _SIZE,
                          COLWINDOW_DECODE_ADDR / _SIZE,
                          COLWINDOW_RESIDENT_ADDR / _SIZE
  src/rsdk/colwindow.h -> COLWINDOW_W (window geometry)
  cd/GHZ1MASK.BIN, cd/GHZ1COL.BIN -> real on-disk sizes

WHY THIS GATE EXISTS (the bug it catches): #180 step 4c made the compacted
collision mask (cd/GHZ1MASK.BIN, GMS2) cover the UNION of BOTH GHZ collision
layers (FG Low 217 + FG High 150 = 260 distinct base tiles) instead of FG Low
alone. That grew the mask 33452 -> 39644 B and overflowed its 0x8400 (33792 B)
LWRAM carve into the colwindow window buffer at 0x00208400 -> silent collision
corruption. A stale GHZ_MASK_FILE_SIZE (33452) ALSO makes the runtime size
check fail -> graceful-degrade -> Sonic falls through (the very #180 bug).

The fixed-neighbour boundaries (FG.TMP, the FR-2 entity pool, the player MRU
pool) are documented constants from src/rsdk/storage.c's LWRAM map; they are
named here with citations so a future carve change that collides with a live
region is caught at build time.

Pure host file parse - no Saturn/emulator dependency; runs in verify_done.
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_C = os.path.join(ROOT, "src", "mania", "Game.c")
COLWIN_C = os.path.join(ROOT, "src", "rsdk", "colwindow.c")
COLWIN_H = os.path.join(ROOT, "src", "rsdk", "colwindow.h")
MASK_BIN = os.path.join(ROOT, "cd", "GHZ1MASK.BIN")
COL_BIN = os.path.join(ROOT, "cd", "GHZ1COL.BIN")

# Fixed LWRAM neighbours (src/rsdk/storage.c LWRAM map; cited, not parsed).
FG_TMP_ADDR = 0x00210000      # FG.TMP tilemap (LIVE every frame) - carve ceiling
FR2_POOL_LO = 0x00260000      # FR-2 entity MRU pool (#189) - DO NOT TOUCH
FR2_POOL_HI = 0x00290000
PLAYER_POOL_ADDR = 0x002D0000  # player MRU pool - resident-region ceiling


def read(path):
    if not os.path.exists(path):
        return None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def find_define(src, name):
    """Parse `#define NAME (... 0xHHHH ...)` or `#define NAME 0xHHHH` /
    decimal. Returns the first integer literal after the name, or None."""
    m = re.search(r"#define\s+" + re.escape(name) + r"\b(.*)", src)
    if not m:
        return None
    tail = m.group(1)
    # strip a trailing line comment so its digits don't get parsed
    tail = re.sub(r"/\*.*", "", tail)
    tail = re.sub(r"//.*", "", tail)
    hexm = re.search(r"0x[0-9A-Fa-f]+", tail)
    if hexm:
        return int(hexm.group(0), 16)
    decm = re.search(r"\b(\d+)\b", tail)
    return int(decm.group(1)) if decm else None


def main():
    print("=== Task #180 step 4c GHZ LWRAM carve-layout gate ===")
    g = read(GAME_C)
    c = read(COLWIN_C)
    h = read(COLWIN_H)
    missing = [p for p, s in ((GAME_C, g), (COLWIN_C, c), (COLWIN_H, h))
               if s is None]
    for m in missing:
        print("  RED: missing source: %s" % m)
    if not os.path.exists(MASK_BIN):
        print("  RED: missing asset: %s" % MASK_BIN); missing.append(MASK_BIN)
    if not os.path.exists(COL_BIN):
        print("  RED: missing asset: %s" % COL_BIN); missing.append(COL_BIN)
    if missing:
        return 1

    mask_addr = find_define(g, "GHZ_MASK_LWRAM_ADDR")
    mask_cap = find_define(g, "GHZ_MASK_LWRAM_CAP")
    mask_def = find_define(g, "GHZ_MASK_FILE_SIZE")
    win_addr = find_define(c, "COLWINDOW_LWRAM_WIN_ADDR")
    win_size = find_define(c, "COLWINDOW_LWRAM_WIN_SIZE")
    dec_addr = find_define(c, "COLWINDOW_DECODE_ADDR")
    dec_size = find_define(c, "COLWINDOW_DECODE_SIZE")
    res_addr = find_define(c, "COLWINDOW_RESIDENT_ADDR")
    res_size = find_define(c, "COLWINDOW_RESIDENT_SIZE")
    win_w = find_define(h, "COLWINDOW_W")

    parsed = {
        "GHZ_MASK_LWRAM_ADDR": mask_addr, "GHZ_MASK_LWRAM_CAP": mask_cap,
        "GHZ_MASK_FILE_SIZE": mask_def, "COLWINDOW_LWRAM_WIN_ADDR": win_addr,
        "COLWINDOW_LWRAM_WIN_SIZE": win_size, "COLWINDOW_DECODE_ADDR": dec_addr,
        "COLWINDOW_DECODE_SIZE": dec_size, "COLWINDOW_RESIDENT_ADDR": res_addr,
        "COLWINDOW_RESIDENT_SIZE": res_size, "COLWINDOW_W": win_w,
    }
    fails = []
    for k, v in parsed.items():
        if v is None:
            fails.append("could not parse #define %s" % k)
    if fails:
        print("=== Gate #180 lwram-layout: RED ===")
        for f in fails:
            print("    - %s" % f)
        return 1

    mask_sz = os.path.getsize(MASK_BIN)
    col_sz = os.path.getsize(COL_BIN)

    # GHZ1COL.BIN geometry (GCO3 header) for the window-buffer capacity check.
    with open(COL_BIN, "rb") as f:
        d = f.read(24)
    nlayers = struct.unpack(">H", d[4:6])[0] if d[:4] == b"GCO3" else 2
    ysize = struct.unpack(">H", d[16:18])[0] if d[:4] == b"GCO3" else 128

    # --- A1 mask define matches real asset size ---------------------------
    if mask_def != mask_sz:
        fails.append("A1 GHZ_MASK_FILE_SIZE=%d != real GHZ1MASK.BIN=%d "
                     "(stale -> runtime size check fails -> fall-through)"
                     % (mask_def, mask_sz))
    else:
        print("  A1 OK  GHZ_MASK_FILE_SIZE=%d == GHZ1MASK.BIN" % mask_def)

    # --- A2 mask fits its carve cap ---------------------------------------
    if mask_sz > mask_cap:
        fails.append("A2 mask %d B > GHZ_MASK_LWRAM_CAP 0x%X (%d B) -> "
                     "overflow into window buffer" % (mask_sz, mask_cap, mask_cap))
    else:
        print("  A2 OK  mask %d B <= cap 0x%X (%d B), slack %d"
              % (mask_sz, mask_cap, mask_cap, mask_cap - mask_sz))

    # --- A3 mask carve does not overlap the window buffer -----------------
    mask_end = mask_addr + mask_cap
    if mask_end > win_addr:
        fails.append("A3 mask carve end 0x%X > window 0x%X (overlap)"
                     % (mask_end, win_addr))
    else:
        print("  A3 OK  mask carve end 0x%X <= window 0x%X" % (mask_end, win_addr))

    # --- A4 window buffer stays below FG.TMP ------------------------------
    win_end = win_addr + win_size
    if win_end > FG_TMP_ADDR:
        fails.append("A4 window end 0x%X > FG.TMP 0x%X (LIVE tilemap clobber)"
                     % (win_end, FG_TMP_ADDR))
    else:
        print("  A4 OK  window end 0x%X <= FG.TMP 0x%X" % (win_end, FG_TMP_ADDR))

    # --- A5 window buffer holds the full W x ysize x nlayers geometry -----
    need = win_w * ysize * nlayers * 2
    if win_size < need:
        fails.append("A5 window 0x%X (%d B) < need %d B (W=%d ysize=%d "
                     "layers=%d)" % (win_size, win_size, need, win_w, ysize,
                                     nlayers))
    else:
        print("  A5 OK  window %d B >= geometry need %d B (W=%d ysize=%d "
              "layers=%d)" % (win_size, need, win_w, ysize, nlayers))

    # --- A6 resident blob fits its cap ------------------------------------
    if col_sz > res_size:
        fails.append("A6 GHZ1COL.BIN %d B > COLWINDOW_RESIDENT_SIZE 0x%X "
                     "(%d B) -> colwindow_open fails -> fall-through"
                     % (col_sz, res_size, res_size))
    else:
        print("  A6 OK  blob %d B <= resident cap 0x%X (%d B), slack %d"
              % (col_sz, res_size, res_size, res_size - col_sz))

    res_end = res_addr + res_size
    dec_end = dec_addr + dec_size

    # --- A7 the four collision regions are mutually disjoint --------------
    regions = (("mask", mask_addr, mask_end),
               ("window", win_addr, win_end),
               ("decode", dec_addr, dec_end),
               ("resident", res_addr, res_end))

    def overlap(a_lo, a_hi, b_lo, b_hi):
        return a_lo < b_hi and b_lo < a_hi

    a7_ok = True
    for i in range(len(regions)):
        for j in range(i + 1, len(regions)):
            n1, l1, h1 = regions[i]
            n2, l2, h2 = regions[j]
            if overlap(l1, h1, l2, h2):
                fails.append("A7 %s [0x%X,0x%X) overlaps %s [0x%X,0x%X)"
                             % (n1, l1, h1, n2, l2, h2))
                a7_ok = False
    if a7_ok:
        print("  A7 OK  mask/window/decode/resident regions mutually disjoint")

    # --- A8 no collision region overlaps a LIVE neighbour -----------------
    # storage.c LWRAM map: FG.TMP, FR-2 entity pool, scene arena, player pool.
    live = (("FG.TMP", FG_TMP_ADDR, FR2_POOL_LO),
            ("FR-2 pool", FR2_POOL_LO, FR2_POOL_HI),
            ("scene arena", 0x00290000, 0x002C0000),
            ("player pool", PLAYER_POOL_ADDR, 0x00300000))
    a8_ok = True
    for rn, rl, rh in regions:
        for ln, ll, lh in live:
            if overlap(rl, rh, ll, lh):
                fails.append("A8 %s [0x%X,0x%X) clobbers LIVE %s [0x%X,0x%X)"
                             % (rn, rl, rh, ln, ll, lh))
                a8_ok = False
        if rl < 0x00200000 or rh > 0x00300000:
            fails.append("A8 %s [0x%X,0x%X) outside LWRAM [0x200000,0x300000)"
                         % (rn, rl, rh))
            a8_ok = False
    if a8_ok:
        print("  A8 OK  no collision region clobbers FG.TMP / FR-2 / arena / "
              "player pool")

    # --- A10 decode buffer holds one worst-case block ---------------------
    need_dec = 16 * ysize * 2  # block_cols * ysize * 2
    if dec_size < need_dec:
        fails.append("A10 decode 0x%X (%d B) < one block %d B"
                     % (dec_size, dec_size, need_dec))
    else:
        print("  A10 OK decode %d B >= one block %d B" % (dec_size, need_dec))

    if fails:
        print("=== Gate #180 lwram-layout: RED ===")
        for f in fails:
            print("    - %s" % f)
        return 1
    print("=== Gate #180 lwram-layout: GREEN (mask + window + decode + "
          "resident carve all fit; no live-region overlap) ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
