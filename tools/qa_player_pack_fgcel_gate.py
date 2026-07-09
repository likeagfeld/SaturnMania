#!/usr/bin/env python3
"""Gate V-192: player resident pack must NOT collide with the live FG.CEL.

Task #192 ("everything crashes visually after the title card loads") root
cause: the #192 resident SPC packs were placed at LWRAM 0x002D0000.., which
is byte-for-byte the LIVE #188 GHZ FG.CEL region [0x2D0000, 0x2E8000). FG.CEL
is retained by jo's NBG1 internal pointers across gameplay (scene_ghz.c
documents it must NOT be freed/overwritten -> Gate V-188). Overwriting it
corrupts the foreground cell map -> the post-title-card visual crash.

User-chosen fix ("Player-only resident"): SONIC.SPC stays resident, but
relocated entirely ABOVE FG.CEL into the free LWRAM tail [0x2E8000,0x300000).
Entities revert to CD streaming (GHZENT.SPC retired), so this gate also
asserts entity_atlas.c no longer references the GHZENT.SPC resident pack.

This gate is SOURCE-STATIC (parses defines from src), so it runs without a
build or emulator. It fires RED on the current source (player pack @0x2D0000
overlaps FG.CEL) and GREEN once the player pack/decode move above 0x2E8000.

Exit 0 = GREEN. Exit 1 = RED (collision / out-of-bounds / GHZENT.SPC ref).
"""
import re
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
PLAYER_ATLAS = ROOT / "src" / "rsdk" / "player_atlas.c"
ENTITY_ATLAS = ROOT / "src" / "rsdk" / "entity_atlas.c"
SCENE_GHZ = ROOT / "src" / "rsdk" / "scene_ghz.c"

LWRAM_LO = 0x00200000
LWRAM_HI = 0x00300000


def _read(p):
    return p.read_text(encoding="utf-8", errors="replace")


def _hex_define(text, name):
    """Find `#define NAME ... 0xHHHH...` and return the int, or None."""
    m = re.search(
        r"#define\s+" + re.escape(name) + r"\b[^\n]*?0x([0-9A-Fa-f]+)", text)
    return int(m.group(1), 16) if m else None


def main():
    fails = []

    ptext = _read(PLAYER_ATLAS)
    stext = _read(SCENE_GHZ)
    etext = _read(ENTITY_ATLAS)

    fg_addr = _hex_define(stext, "GHZ_FG_CEL_LWRAM_ADDR")
    fg_size = _hex_define(stext, "GHZ_FG_CEL_LWRAM_SIZE")
    if fg_addr is None or fg_size is None:
        print("FATAL: could not parse GHZ_FG_CEL_LWRAM_ADDR/_SIZE from scene_ghz.c")
        return 1
    fg_lo, fg_hi = fg_addr, fg_addr + fg_size

    pk_addr = _hex_define(ptext, "SPC_PLAYER_ADDR")
    pk_cap = _hex_define(ptext, "SPC_PLAYER_CAP")
    dc_addr = _hex_define(ptext, "PLAYER_DECODE_ADDR")
    dc_cap = _hex_define(ptext, "PLAYER_DECODE_CAP")
    missing = [n for n, v in (
        ("SPC_PLAYER_ADDR", pk_addr), ("SPC_PLAYER_CAP", pk_cap),
        ("PLAYER_DECODE_ADDR", dc_addr), ("PLAYER_DECODE_CAP", dc_cap),
    ) if v is None]
    if missing:
        print("FATAL: could not parse player_atlas.c defines: " + ", ".join(missing))
        return 1

    pk_lo, pk_hi = pk_addr, pk_addr + pk_cap
    dc_lo, dc_hi = dc_addr, dc_addr + dc_cap

    def overlaps(a_lo, a_hi, b_lo, b_hi):
        return a_lo < b_hi and b_lo < a_hi

    # P1: player pack must not overlap FG.CEL.
    if overlaps(pk_lo, pk_hi, fg_lo, fg_hi):
        fails.append(
            "P1 player pack [0x%06X,0x%06X) OVERLAPS live FG.CEL [0x%06X,0x%06X)"
            % (pk_lo, pk_hi, fg_lo, fg_hi))

    # P2: player decode buffer must not overlap FG.CEL.
    if overlaps(dc_lo, dc_hi, fg_lo, fg_hi):
        fails.append(
            "P2 player decode [0x%06X,0x%06X) OVERLAPS live FG.CEL [0x%06X,0x%06X)"
            % (dc_lo, dc_hi, fg_lo, fg_hi))

    # P3: pack and decode must not overlap each other.
    if overlaps(pk_lo, pk_hi, dc_lo, dc_hi):
        fails.append(
            "P3 player pack [0x%06X,0x%06X) OVERLAPS decode [0x%06X,0x%06X)"
            % (pk_lo, pk_hi, dc_lo, dc_hi))

    # P4: both regions must stay inside LWRAM.
    for nm, lo, hi in (("pack", pk_lo, pk_hi), ("decode", dc_lo, dc_hi)):
        if lo < LWRAM_LO or hi > LWRAM_HI:
            fails.append(
                "P4 player %s [0x%06X,0x%06X) outside LWRAM [0x%06X,0x%06X)"
                % (nm, lo, hi, LWRAM_LO, LWRAM_HI))

    # P5: entities must no longer reference the retired GHZENT.SPC pack.
    if "GHZENT.SPC" in etext:
        fails.append("P5 entity_atlas.c still references GHZENT.SPC "
                     "(entities must revert to CD streaming)")

    print("=== Gate V-192: player pack vs FG.CEL ===")
    print("  FG.CEL  : [0x%06X, 0x%06X)  (%d B, LIVE per V-188)"
          % (fg_lo, fg_hi, fg_size))
    print("  pack    : [0x%06X, 0x%06X)  (%d B)" % (pk_lo, pk_hi, pk_cap))
    print("  decode  : [0x%06X, 0x%06X)  (%d B)" % (dc_lo, dc_hi, dc_cap))
    print("  GHZENT.SPC in entity_atlas.c: %s"
          % ("YES" if "GHZENT.SPC" in etext else "no"))

    if fails:
        print("\nRED (%d):" % len(fails))
        for f in fails:
            print("  - " + f)
        return 1

    print("\nGREEN: player pack + decode are above FG.CEL, in-bounds, "
          "non-overlapping; entities CD-stream.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
