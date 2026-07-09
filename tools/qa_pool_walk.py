#!/usr/bin/env python3
"""qa_pool_walk.py - walk the jo malloc pool from a Mednafen savestate.

Reads memory_zones[] (jo-engine/jo_engine/malloc.c:60) out of a captured
Saturn WRAM-H image and walks each zone's 8-byte block headers
(jo_memory_block {short zone@0; uint size@4}) from .begin to .high to
report, per zone and total: bytes in use, bytes free (trailing tail
end-high PLUS interior free blocks), and the LARGEST single contiguous
free block available to a fresh jo_malloc (= max(end-high, largest
interior free block) -- jo never coalesces, malloc.c:127-133).

This answers, with NO rebuild, whether a large alloc (e.g. FG.CEL ~90 KB)
fails from genuine pool shortage vs fragmentation.

WRAM-H byte order: Mednafen serialises the MAIN/WorkRAMH chunk pair-
swapped (each 16-bit halfword's two bytes swapped relative to the SH-2
big-endian image). We unswap per the project gate convention
(qa_title_dat_pool_gate.py s32/u32).
"""
from __future__ import annotations
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mcs_extract  # noqa: E402

WRAMH_BASE = 0x06000000
WRAMH_SIZE = 0x00100000
MEMORY_ZONES_ADDR = 0x06054120
MAX_ZONES = 9
JO_BLOCK_FREE = 0xFFFF


def load_wramh(state_path: Path) -> bytes:
    sections = mcs_extract.parse_savestate(state_path)
    data = mcs_extract._read_region(sections, "MAIN", "WorkRAMH")
    if data is None:
        raise SystemExit("WorkRAMH not present in state")
    return data


def u32(buf: bytes, addr: int) -> int:
    o = addr - WRAMH_BASE
    b = buf[o:o + 4]
    # unswap 16-bit pairs: stored [b0,b1,b2,b3] -> BE value b1 b0 b3 b2
    return (b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]


def u16(buf: bytes, addr: int) -> int:
    o = addr - WRAMH_BASE
    b = buf[o:o + 2]
    return (b[1] << 8) | b[0]


def in_wramh(addr: int) -> bool:
    return WRAMH_BASE <= addr < WRAMH_BASE + WRAMH_SIZE


def walk():
    if len(sys.argv) < 2:
        raise SystemExit("usage: qa_pool_walk.py <state.mcs>")
    buf = load_wramh(Path(sys.argv[1]))

    print(f"memory_zones @ 0x{MEMORY_ZONES_ADDR:08x}")
    print("=" * 70)
    grand_free = 0
    grand_used = 0
    grand_largest = 0
    for z in range(MAX_ZONES):
        za = MEMORY_ZONES_ADDR + z * 12
        begin = u32(buf, za)
        high = u32(buf, za + 4)
        end = u32(buf, za + 8)
        if begin == 0 and high == 0 and end == 0:
            continue
        if not (in_wramh(begin) and in_wramh(end)) or end <= begin:
            print(f"zone {z}: begin=0x{begin:08x} high=0x{high:08x} "
                  f"end=0x{end:08x}  (out of WRAM-H range; skipped)")
            continue
        capacity = end - begin
        tail_free = end - high
        used = 0
        interior_free = 0
        largest_interior_free = 0
        nblocks = 0
        nfree = 0
        ptr = begin
        corrupt = False
        while ptr + 8 <= high:
            zone = u16(buf, ptr)
            size = u32(buf, ptr + 4)
            if size == 0 or (size & 3) != 0 or ptr + size > high + 8:
                corrupt = True
                break
            nblocks += 1
            if zone == JO_BLOCK_FREE:
                nfree += 1
                interior_free += size
                if size > largest_interior_free:
                    largest_interior_free = size
            else:
                used += size
            ptr += size
        zone_free = tail_free + interior_free
        zone_largest = max(tail_free, largest_interior_free)
        grand_free += zone_free
        grand_used += used
        grand_largest = max(grand_largest, zone_largest)
        print(f"zone {z}: cap={capacity:>8} ({capacity//1024} KB)  "
              f"used={used:>8}  free={zone_free:>8} ({zone_free//1024} KB)")
        print(f"         blocks={nblocks} free_blocks={nfree}  "
              f"tail_free(end-high)={tail_free} ({tail_free//1024} KB)  "
              f"interior_free={interior_free}  "
              f"largest_contig_free={zone_largest} ({zone_largest//1024} KB)"
              + ("  [WALK HIT CORRUPT HEADER]" if corrupt else ""))
    print("=" * 70)
    print(f"TOTAL used={grand_used} ({grand_used//1024} KB)  "
          f"free={grand_free} ({grand_free//1024} KB)")
    print(f"LARGEST single contiguous free block (max fresh alloc) = "
          f"{grand_largest} ({grand_largest//1024} KB)")
    # FG.CEL probe: need >= 90 KB contiguous (g_ghz_num_cells*64 + 8 hdr).
    fg_cel_need = 90 * 1024
    print("-" * 70)
    print(f"FG.CEL needs ~{fg_cel_need} B (~90 KB) single contiguous; "
          f"{'AVAILABLE' if grand_largest >= fg_cel_need else 'NOT AVAILABLE'}"
          f" (largest={grand_largest})")


if __name__ == "__main__":
    walk()
