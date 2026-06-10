#!/usr/bin/env python3
"""gen_trig_tables.py -- bake RSDK trig lookup tables from an on-target savestate.

P6.1-fast (Task #212). The real RSDK::CalculateTrigAngles (Core/Math.cpp:49-107)
fills 16 trig lookup tables at boot. Its final loop (Math.cpp:98-106) runs
65,536 iterations (0x100 x 0x100) of DOUBLE-precision atan2((float)y,x); the
FPU-less SH-2 emulates every one in newlib/libgcc soft-float -> a multi-minute
one-time boot stall (measured: GREEN dispatch only appears at SaveFrame ~120).

The tables are deterministic, so we bake them: extract the 16 tables from a
Mednafen savestate captured AFTER the on-target fill completed, and emit a C
include file of const arrays. Math.cpp memcpy's them into the live tables behind
RETRO_PLATFORM==RETRO_SATURN (the RNG seed is preserved; only the FILL is
replaced). The baked bytes are this build's OWN newlib output -> bit-identical
to what CalculateTrigAngles would compute on-target, no host-libm ULP drift.

BYTE ORDER. Mednafen serialises MAIN RAM (WRAM-L/H) from its internal uint16 bus
array in host little-endian; the SH-2 wrote big-endian, so each aligned halfword
is byte-swapped on disk (the "16-bit pair-swap" the dispatch gate auto-calibrates
from the magic anchor; mcs_extract.py's no-swap WRAM assumption is wrong for these
states, tracked as Task #136). We auto-detect the order by testing two anchors and
cross-checking every int32 entry against an independent host recompute, so a wrong
order fails loudly rather than baking swapped garbage.

  ANCHOR A (arcTan256, transposed layout arc[y+256*x]): index 0 == 0
    (atan2(0,0)=0); indices 1..255 == a single constant PI/2 plateau
    (atan2(y>0,0)=PI/2); index 256 == 0 (atan2(0,x=1)=0, the y=0 row resumes).
    The plateau byte is libm-specific: host libm rounds 63.998 -> 0x3F, on-target
    newlib yields 0x40 -- so we assert the STRUCTURE (one repeated nonzero value
    bracketed by zeros), which is exactly the ULP drift that makes on-target
    extraction the faithful choice over host recompute.
  ANCHOR B (cross-check): each int32 table entry within +/-2 of host libm
    recompute of the same decomp formula (tan singularities at +-PI/2 skipped:
    both libms emit platform-specific garbage there, which is exactly why we
    extract on-target instead of host-computing).

USAGE
    python tools/_portspike/_p6/gen_trig_tables.py \
        tools/_portspike/_p6/_p6_dispatch_green.mcs \
        tools/_portspike/_p6/_p6.map \
        platform/Saturn/TrigTables_Saturn.inc

EXIT 0 = emitted + both anchors pass. 1 = anchor/cross-check failed (no emit).
"""
from __future__ import annotations

import math
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]          # _p6 -> _portspike -> tools -> repo root
sys.path.insert(0, str(ROOT / "tools"))
from mcs_extract import parse_savestate, _read_region  # noqa: E402

WRAM_L_BASE = 0x00200000
RSDK_PI = 3.1415927410125732  # decomp RSDK_PI (float) -- Math/Math.hpp

# The 16 trig tables: name -> (element count, element size bytes).
# Addresses are read from the link map (they live in WRAM-L .bss_l and shift
# build-to-build). int32 = 4, uint8 = 1.
TABLE_SHAPE = {
    "arcTan256LookupTable": (0x100 * 0x100, 1),
    "acos256LookupTable":   (0x100, 4),
    "asin256LookupTable":   (0x100, 4),
    "tan256LookupTable":    (0x100, 4),
    "cos256LookupTable":    (0x100, 4),
    "sin256LookupTable":    (0x100, 4),
    "acos512LookupTable":   (0x200, 4),
    "asin512LookupTable":   (0x200, 4),
    "tan512LookupTable":    (0x200, 4),
    "cos512LookupTable":    (0x200, 4),
    "sin512LookupTable":    (0x200, 4),
    "acos1024LookupTable":  (0x400, 4),
    "asin1024LookupTable":  (0x400, 4),
    "tan1024LookupTable":   (0x400, 4),
    "cos1024LookupTable":   (0x400, 4),
    "sin1024LookupTable":   (0x400, 4),
}

# Emit order (matches Math.cpp:6-24 declaration order for readability).
EMIT_ORDER = [
    "sin1024LookupTable", "cos1024LookupTable", "tan1024LookupTable",
    "asin1024LookupTable", "acos1024LookupTable",
    "sin512LookupTable", "cos512LookupTable", "tan512LookupTable",
    "asin512LookupTable", "acos512LookupTable",
    "sin256LookupTable", "cos256LookupTable", "tan256LookupTable",
    "asin256LookupTable", "acos256LookupTable",
    "arcTan256LookupTable",
]

TAN_SINGULAR = 100000  # |value| above this = tan blow-up near +-PI/2; skip in cross-check


def parse_map(map_path: Path) -> dict:
    """Return {table_name: saturn_address} for the 16 trig tables."""
    pat = re.compile(r"^\s*0x0*([0-9a-fA-F]+)\s+RSDK::(\w+LookupTable)\s*$")
    out = {}
    for line in map_path.read_text(errors="replace").splitlines():
        m = pat.match(line)
        if m and m.group(2) in TABLE_SHAPE:
            out[m.group(2)] = int(m.group(1), 16)
    missing = set(TABLE_SHAPE) - set(out)
    if missing:
        raise SystemExit(f"gen_trig: map missing tables: {sorted(missing)}")
    return out


def unswap16(buf: bytes) -> bytes:
    """Swap the two bytes of every aligned 16-bit halfword."""
    b = bytearray(buf)
    if len(b) & 1:
        b.append(0)
    b[0::2], b[1::2] = bytes(b[1::2]), bytes(b[0::2])
    return bytes(b)


def read_table(region: bytes, addr: int, count: int, esize: int):
    off = addr - WRAM_L_BASE
    raw = region[off:off + count * esize]
    if len(raw) < count * esize:
        raise SystemExit(f"gen_trig: region too short for table @ {hex(addr)}")
    if esize == 1:
        return list(raw)
    return list(struct.unpack(f">{count}i", raw))  # big-endian int32


# --- independent host recompute of the decomp formulas (Math.cpp:54-106) ------
def host_reference() -> dict:
    ref = {}
    f = lambda c: math.floor(c) if c >= 0 else -math.floor(-c)   # C (int) trunc-toward-0

    sin1024 = [f(math.sin((i / 512.0) * RSDK_PI) * 1024.0) for i in range(0x400)]
    cos1024 = [f(math.cos((i / 512.0) * RSDK_PI) * 1024.0) for i in range(0x400)]
    tan1024 = [f(math.tan((i / 512.0) * RSDK_PI) * 1024.0) for i in range(0x400)]
    asin1024 = [f((math.asin(i / 1023.0) * 512.0) / RSDK_PI) for i in range(0x400)]
    acos1024 = [f((math.acos(i / 1023.0) * 512.0) / RSDK_PI) for i in range(0x400)]
    for idx, v in ((0x000, 0x400), (0x100, 0), (0x200, -0x400), (0x300, 0)):
        cos1024[idx] = v
    for idx, v in ((0x000, 0), (0x100, 0x400), (0x200, 0), (0x300, -0x400)):
        sin1024[idx] = v

    sin512 = [f(math.sin((i / 256.0) * RSDK_PI) * 512.0) for i in range(0x200)]
    cos512 = [f(math.cos((i / 256.0) * RSDK_PI) * 512.0) for i in range(0x200)]
    tan512 = [f(math.tan((i / 256.0) * RSDK_PI) * 512.0) for i in range(0x200)]
    asin512 = [f((math.asin(i / 511.0) * 256.0) / RSDK_PI) for i in range(0x200)]
    acos512 = [f((math.acos(i / 511.0) * 256.0) / RSDK_PI) for i in range(0x200)]
    for idx, v in ((0x00, 0x200), (0x80, 0), (0x100, -0x200), (0x180, 0)):
        cos512[idx] = v
    for idx, v in ((0x00, 0), (0x80, 0x200), (0x100, 0), (0x180, -0x200)):
        sin512[idx] = v

    sin256 = [sin512[i * 2] >> 1 for i in range(0x100)]
    cos256 = [cos512[i * 2] >> 1 for i in range(0x100)]
    tan256 = [tan512[i * 2] >> 1 for i in range(0x100)]
    asin256 = [f((math.asin(i / 255.0) * 128.0) / RSDK_PI) for i in range(0x100)]
    acos256 = [f((math.acos(i / 255.0) * 128.0) / RSDK_PI) for i in range(0x100)]

    arc = [0] * (0x100 * 0x100)
    for y in range(0x100):
        for x in range(0x100):
            arc[y + x * 0x100] = f(math.atan2(y, x) * 40.743664) & 0xFF

    ref.update(sin1024LookupTable=sin1024, cos1024LookupTable=cos1024,
               tan1024LookupTable=tan1024, asin1024LookupTable=asin1024,
               acos1024LookupTable=acos1024, sin512LookupTable=sin512,
               cos512LookupTable=cos512, tan512LookupTable=tan512,
               asin512LookupTable=asin512, acos512LookupTable=acos512,
               sin256LookupTable=sin256, cos256LookupTable=cos256,
               tan256LookupTable=tan256, asin256LookupTable=asin256,
               acos256LookupTable=acos256, arcTan256LookupTable=arc)
    return ref


def cross_check(tables: dict, ref: dict):
    """Return (max_abs_diff_excluding_tan_singular, total_entries_differing)."""
    worst = 0
    differ = 0
    for name in TABLE_SHAPE:
        got, exp = tables[name], ref[name]
        is_tan = name.startswith("tan")
        for g, e in zip(got, exp):
            if is_tan and (abs(g) > TAN_SINGULAR or abs(e) > TAN_SINGULAR):
                continue
            d = abs(g - e)
            if d:
                differ += 1
                worst = max(worst, d)
    return worst, differ


def extract_all(region_raw: bytes, addrs: dict):
    region = unswap16(region_raw)
    out = {}
    for name, (count, esize) in TABLE_SHAPE.items():
        out[name] = read_table(region, addrs[name], count, esize)
    return out, region


def anchor_a_ok(tables: dict) -> bool:
    # Transposed layout arc[y + 256*x] (Math.cpp:98-105, stride 0x100):
    #   index 0      = atan2(0,0)           = 0
    #   indices 1..255 = atan2(y>0, 0) = PI/2 -- a CONSTANT plateau. Its byte is
    #                  libm-specific: host libm rounds 63.998 -> 0x3F, but the
    #                  on-target newlib build yields 0x40 (the very ULP drift that
    #                  makes on-target extraction, not host recompute, the faithful
    #                  choice). So assert the STRUCTURE (a single nonzero value
    #                  repeated across the whole plateau), not a hard-coded byte.
    #   index 256    = atan2(0, x=1)         = 0   (the y=0 row resumes)
    arc = tables["arcTan256LookupTable"]
    plateau = arc[1]
    return (arc[0] == 0 and plateau in (0x3F, 0x40)
            and all(arc[i] == plateau for i in range(1, 256))
            and arc[256] == 0)


def emit_inc(tables: dict, out_path: Path, src_mcs: str, src_map: str):
    lines = [
        "/* AUTO-GENERATED by tools/_portspike/_p6/gen_trig_tables.py -- DO NOT EDIT. */",
        f"/* Source savestate : {src_mcs} (on-target post-CalculateTrigAngles fill). */",
        f"/* Source link map  : {src_map}. */",
        "/* Baked for RETRO_PLATFORM==RETRO_SATURN: replaces the 65,536 double-atan2  */",
        "/* soft-float boot fill (Math.cpp:54-106) with this build's own newlib output. */",
        "",
    ]
    for name in EMIT_ORDER:
        count, esize = TABLE_SHAPE[name]
        vals = tables[name]
        if esize == 1:
            ctype, per = "unsigned char", 24
            body = [f"0x{v:02X}u," for v in vals]
        else:
            ctype, per = "signed int", 8
            body = [f"{v}," for v in vals]
        lines.append(f"static const {ctype} saturn_{name}[{count}] = {{")
        for i in range(0, len(body), per):
            lines.append("    " + "".join(s.ljust(0) for s in
                          (x + " " for x in body[i:i + per])).rstrip())
        lines.append("};")
        lines.append("")
    out_path.write_text("\n".join(lines))


def main(argv) -> int:
    if len(argv) != 4:
        sys.stderr.write(
            "usage: gen_trig_tables.py <state.mcs> <link.map> <out.inc>\n")
        return 2
    mcs, mapf, outf = (Path(argv[1]), Path(argv[2]), Path(argv[3]))

    addrs = parse_map(mapf)
    print("[1/4] table addresses from map:")
    for name in EMIT_ORDER:
        print(f"        {name:24s} {hex(addrs[name])}  "
              f"({TABLE_SHAPE[name][0]} x {TABLE_SHAPE[name][1]}B)")

    sections = parse_savestate(mcs)
    region_raw = _read_region(sections, "MAIN", "WorkRAML")
    if region_raw is None:
        sys.stderr.write("gen_trig: WorkRAML region absent in savestate\n")
        return 1
    print(f"[2/4] WorkRAML region: {len(region_raw)} bytes")

    print("[3/4] byte-order auto-detect + cross-check vs host libm recompute:")
    ref = host_reference()
    chosen = None
    for label, transform in (("16-bit pair-swap", unswap16), ("raw", lambda b: b)):
        region = transform(region_raw)
        tables = {n: read_table(region, addrs[n], c, e)
                  for n, (c, e) in TABLE_SHAPE.items()}
        a_ok = anchor_a_ok(tables)
        worst, differ = cross_check(tables, ref)
        b_ok = worst <= 2
        status = "PASS" if (a_ok and b_ok) else "fail"
        print(f"        [{status}] order={label:16s} anchorA(arcTan)={a_ok} "
              f"crosscheck max|d|={worst} (<=2) entries_differing={differ}")
        if a_ok and b_ok and chosen is None:
            chosen = (label, tables, differ, worst)
    if chosen is None:
        sys.stderr.write(
            "gen_trig: NO byte order satisfies both anchors -- refusing to emit\n")
        return 1
    label, tables, differ, worst = chosen
    print(f"        -> chosen order: {label}; {differ} of 101376 entries differ "
          f"from host libm by <= {worst} (newlib vs host ULP -- why we bake on-target)")

    emit_inc(tables, outf, str(mcs), str(mapf))
    nbytes = outf.stat().st_size
    print(f"[4/4] wrote {outf} ({nbytes} bytes, 16 const tables)")
    print("GREEN: tables baked + both anchors pass.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
