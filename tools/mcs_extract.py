#!/usr/bin/env python3
"""mcs_extract.py - Mednafen Saturn savestate parser.

Parses Mednafen 1.32.1 .mc0/.mcs/.mca savestate files for the Saturn
emulation module (ss/). Exposes the major Saturn memory regions and
registers as inspectable values so QA gates can assert register-level
contracts in addition to pixel-level black-box checks.

FORMAT REFERENCE (citation-backed; see docs/COMPREHENSIVE_PLAN.md
SS11.35 for the full table):

- Outer wrapper: gzip (1f 8b 08 ...).
- Decompressed header (32 bytes): magic "MDFNSVST" (8) + timestamp (8) +
  version (4) + total-size-with-endian-flag (4) + preview width (4) +
  preview height (4). Source: mednafen-git/src/state.cpp lines 614-625.
- After header: preview RGB image at offset 0x20, width * height * 3
  bytes.
- After preview: chunk stream. Each chunk: 32-byte section name (null-
  padded) + 4-byte LE uint32 payload size + payload. Source:
  mednafen-git/src/state.cpp ReadStateChunk lines 227-288.
- Inside payload: variable records. Each: 1-byte name length + name +
  4-byte LE uint32 var size + var data.

SATURN MEMORY MAP (citation: ST-097-R5 SS2.1, mednafen ss/ss.cpp
StateAction lines 2715-2781, ss/vdp1.cpp::1453, ss/vdp2.cpp::1267):

  0x00200000+1MB  = WRAM-L         -> MAIN/WorkRAML       (1048576 B)
  0x05A00000+512K = Sound RAM      -> SCSP/...
  0x05C00000+512K = VDP1 VRAM      -> VDP1/VRAM          (524288 B, uint16)
  0x05C80000+256K = VDP1 FB        -> VDP1/&FB[0][0]     (524288 B, 2 fb)
  0x05D00000+0x20 = VDP1 Regs      -> VDP1/{LOPR,EWDR,EWLR,EWRR,TVMR,
                                            FBCR,PTMR,EDSR} (2 B each)
  0x05E00000+512K = VDP2 VRAM      -> VDP2/VRAM          (524288 B, uint16)
  0x05F00000+4K   = VDP2 CRAM      -> VDP2/CRAM          (4096 B, uint16)
  0x05F80000+512  = VDP2 Regs      -> VDP2/RawRegs       (512 B, host endian)
                                       SPCTL = byte offset 0xE0 (per
                                       ST-058-R2 + jo/sega_saturn.h:357).
  0x06000000+1MB  = WRAM-H         -> MAIN/WorkRAMH       (1048576 B)
  0x00180000+32K  = Backup RAM     -> MAIN/BackupRAM      (32768 B)

CACHE-THROUGH ALIASING: 0x20000000 + base also routes to the same
physical region. We normalise any 0x20xxxxxx address back to its 0x0x
or 0x2x base by masking off bit 29 (0x20000000) before lookup. This
matches Saturn hardware behaviour and the project's existing convention
of using 0x25F800E0 vs 0x05F800E0 interchangeably for VDP2 register
access (see Phase 2.3c findings in docs/COMPREHENSIVE_PLAN.md).

ENDIAN: Mednafen serialises uint16 arrays in host order. On x86_64 the
host is little-endian, but Saturn hardware reports its registers in
big-endian. To match register-level assertions against the Saturn-
visible value, we read uint16 fields with little-endian decoding (the
order Mednafen wrote them) and present them as-is. For a Saturn VDP2
register at address A, the byte at RawRegs[A - 0x05F80000] holds the
high byte of the big-endian register and the next byte holds the low
byte (Mednafen mirrors raw 16-bit big-endian reads into the array
elements then writes them out as uint16 host LE). This is the Mednafen
convention; see ss/vdp2.cpp Read16 implementation for the read path.
For pragmatic CLI use we expose --peek16 returning the value as if the
caller MOV.W'd it from the Saturn address (big-endian uint16).

USAGE
    python tools/mcs_extract.py <mcs> [REGION ARGS] [--json]

REGION ARGS (any combination; --json bundles into a dict):
    --peek      ADDR      32-bit value at Saturn ADDR (big-endian uint32)
    --peek16    ADDR      16-bit value at Saturn ADDR (big-endian uint16)
    --peek8     ADDR      8-bit value at Saturn ADDR
    --vdp1-vram OUT       dump 512 KiB VDP1 VRAM to OUT
    --vdp1-fb   OUT       dump 512 KiB VDP1 framebuffer (both FBs) to OUT
    --vdp2-vram OUT       dump 512 KiB VDP2 VRAM to OUT
    --cram      OUT       dump 4 KiB VDP2 CRAM to OUT
    --vdp1-regs OUT       dump 32 B VDP1 register block to OUT (synthesized)
    --vdp2-regs OUT       dump 512 B VDP2 RawRegs to OUT
    --wram-l    OUT       dump 1 MiB Work RAM-L to OUT
    --wram-h    OUT       dump 1 MiB Work RAM-H to OUT
    --backup    OUT       dump 32 KiB Backup RAM to OUT
    --sh2m-regs           print SH2-Master R0-R15 + PC + SR + VBR
    --sh2s-regs           print SH2-Slave R0-R15 + PC + SR + VBR
    --list-sections       print every chunk name + size
    --list-vars SECT      print every var name + size in SECT

EXIT CODES
    0    success
    2    file/parse error
    3    requested region not present in this state

Author: Phase 1.30 (tools-only infrastructure phase). No src/ or cd/
or Makefile change. Tracked in docs/COMPREHENSIVE_PLAN.md SS11.35.
"""
from __future__ import annotations

import argparse
import gzip
import json
import struct
import sys
from pathlib import Path
from typing import Dict, Optional, Tuple


MAGIC = b"MDFNSVST"

# Saturn memory map -> (section, var-name, base, size).
# Cache-through mirror (0x20000000+) is normalised by ADDR & ~0x20000000.
REGION_MAP = [
    ("MAIN",  "WorkRAML",      0x00200000, 0x00100000),
    ("MAIN",  "WorkRAMH",      0x06000000, 0x00100000),
    ("MAIN",  "BackupRAM",     0x00180000, 0x00008000),
    ("VDP1",  "VRAM",          0x05C00000, 0x00080000),
    ("VDP1",  "&FB[0][0]",     0x05C80000, 0x00080000),
    ("VDP2",  "VRAM",          0x05E00000, 0x00080000),
    ("VDP2",  "CRAM",          0x05F00000, 0x00001000),
    ("VDP2",  "RawRegs",       0x05F80000, 0x00000200),
]

# VDP1 individual register addresses (per ST-013-R3 SSregister map).
VDP1_REG_MAP = {
    0x05D00000: ("VDP1", "TVMR", 2),
    0x05D00002: ("VDP1", "FBCR", 2),
    0x05D00004: ("VDP1", "PTMR", 2),
    0x05D00006: ("VDP1", "EWDR", 2),
    0x05D00008: ("VDP1", "EWLR", 2),
    0x05D0000A: ("VDP1", "EWRR", 2),
    # ENDR is write-only; not in state.
    0x05D00010: ("VDP1", "EDSR", 2),
    0x05D00012: ("VDP1", "LOPR", 2),
}


def _normalise_addr(addr: int) -> int:
    """Strip cache-through bit. 0x25F800E0 -> 0x05F800E0."""
    return addr & ~0x20000000 & 0xFFFFFFFF


def parse_savestate(path: Path) -> Dict[str, Dict[str, Tuple[int, int]]]:
    """Read .mc0/.mcs file. Returns {section: {var: (offset, size)}}.

    Offsets are byte offsets into the decompressed buffer (which is
    stored in the returned object's '__buf__' key).
    """
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise SystemExit(f"mcs_extract: cannot open {path}: {exc}")
    if len(raw) < 2:
        raise SystemExit(f"mcs_extract: file too small ({len(raw)} bytes)")
    # Both .mc0 (gzipped) and uncompressed test files are tolerated.
    if raw[:2] == b"\x1f\x8b":
        try:
            dec = gzip.decompress(raw)
        except OSError as exc:
            raise SystemExit(f"mcs_extract: gzip decompression failed: {exc}")
    else:
        dec = raw

    if len(dec) < 32 or dec[:8] != MAGIC:
        raise SystemExit(
            f"mcs_extract: missing MDFNSVST magic "
            f"(got {dec[:8]!r})"
        )

    # Header: 8 magic + 8 ts + 4 ver + 4 size+flags + 4 prev_w + 4 prev_h
    prev_w = struct.unpack("<I", dec[24:28])[0]
    prev_h = struct.unpack("<I", dec[28:32])[0]
    off = 32 + prev_w * prev_h * 3

    sections: Dict[str, Dict[str, Tuple[int, int]]] = {}
    while off + 36 <= len(dec):
        name = dec[off:off + 32].rstrip(b"\x00").decode("ascii", "replace")
        sz = struct.unpack("<I", dec[off + 32:off + 36])[0]
        payload_off = off + 36
        payload_end = payload_off + sz
        if payload_end > len(dec):
            sys.stderr.write(
                f"mcs_extract: chunk {name!r} declares size {sz} which "
                f"exceeds file ({payload_end} > {len(dec)})\n"
            )
            break
        vars_in_section: Dict[str, Tuple[int, int]] = {}
        vo = payload_off
        while vo < payload_end:
            if vo + 1 > len(dec):
                break
            nlen = dec[vo]
            vo += 1
            if vo + nlen + 4 > payload_end:
                # malformed; bail out of this section
                break
            vname = dec[vo:vo + nlen].rstrip(b"\x00").decode("ascii", "replace")
            vo += nlen
            vsz = struct.unpack("<I", dec[vo:vo + 4])[0]
            vo += 4
            if vo + vsz > payload_end:
                break
            vars_in_section[vname] = (vo, vsz)
            vo += vsz
        sections[name] = vars_in_section
        off = payload_end

    # Stash the decompressed buffer under a reserved key so callers
    # can byte-slice without re-decompressing.
    sections["__buf__"] = {"__data__": (0, len(dec))}  # type: ignore[assignment]
    sections["__buf_bytes__"] = dec  # type: ignore[assignment]
    return sections


def _read_region(
    sections: Dict, section: str, var: str
) -> Optional[bytes]:
    buf = sections["__buf_bytes__"]  # type: ignore[index]
    if section not in sections:
        return None
    if var not in sections[section]:
        return None
    o, s = sections[section][var]
    return buf[o:o + s]


def _peek_bytes(sections: Dict, addr: int, nbytes: int) -> Optional[bytes]:
    """Look up a Saturn address. Returns nbytes raw bytes (big-endian
    Saturn order, as the SH-2 would see them) or None if the address
    isn't in any captured region."""
    norm = _normalise_addr(addr)
    # VDP1 individual register table first.
    if norm in VDP1_REG_MAP and nbytes <= 2:
        sect, var, sz = VDP1_REG_MAP[norm]
        if sect not in sections or var not in sections[sect]:
            return None
        o, _ = sections[sect][var]
        buf = sections["__buf_bytes__"]
        # Stored as host LE uint16; Saturn is big-endian.
        raw_le = buf[o:o + sz]
        be = raw_le[::-1] if sz == 2 else raw_le
        return be[:nbytes]
    # Region table.
    for section, var, base, size in REGION_MAP:
        if base <= norm < base + size:
            off_in_region = norm - base
            data = _read_region(sections, section, var)
            if data is None:
                return None
            if off_in_region + nbytes > len(data):
                return None
            # For VDP1/VDP2 uint16-arrays, swap each 16-bit pair to get
            # big-endian (Saturn-visible) order. Plain RAM (WorkRAM,
            # BackupRAM) is stored as raw bytes — no swap needed; the
            # SH-2 already wrote them in big-endian.
            if section in ("VDP1", "VDP2") and var in (
                "VRAM", "CRAM", "RawRegs", "&FB[0][0]"
            ):
                # Swap pairs around the requested range.
                start_pair = off_in_region & ~1
                # extract pair-aligned window large enough to cover request
                end_pair = (off_in_region + nbytes + 1) & ~1
                window = bytearray(data[start_pair:end_pair])
                for i in range(0, len(window), 2):
                    window[i], window[i + 1] = window[i + 1], window[i]
                local = off_in_region - start_pair
                return bytes(window[local:local + nbytes])
            return data[off_in_region:off_in_region + nbytes]
    return None


def _sh2_regs(sections: Dict, which: str) -> Optional[Dict[str, int]]:
    """Extract SH2-M or SH2-S register state.

    Mednafen ss/sh7095.cpp stores R0..R15 as a 64-byte 'R' array, PC as
    its own 4-byte var, and SR/GBR/VBR as a 12-byte 'CtrlRegs' array.
    MACL/MACH/PR live in 'SysRegs' (12 bytes). Layout verified by
    cross-referencing the Mednafen SH-2 source (each SH-2 emits these
    three blocks via SFARRAY32 / SFVAR macros).
    """
    sect_name = "SH2-M" if which == "master" else "SH2-S"
    if sect_name not in sections:
        return None
    buf = sections["__buf_bytes__"]
    out: Dict[str, int] = {}
    if "R" in sections[sect_name]:
        ro, rs = sections[sect_name]["R"]
        if rs >= 64:
            for i in range(16):
                out[f"R{i}"] = struct.unpack(
                    "<I", buf[ro + i * 4:ro + (i + 1) * 4]
                )[0]
    if "PC" in sections[sect_name]:
        po, ps = sections[sect_name]["PC"]
        if ps == 4:
            out["PC"] = struct.unpack("<I", buf[po:po + 4])[0]
    if "CtrlRegs" in sections[sect_name]:
        co, cs = sections[sect_name]["CtrlRegs"]
        if cs >= 12:
            # CtrlRegs layout in Mednafen SH-2: SR, GBR, VBR (3 x uint32)
            out["SR"] = struct.unpack("<I", buf[co:co + 4])[0]
            out["GBR"] = struct.unpack("<I", buf[co + 4:co + 8])[0]
            out["VBR"] = struct.unpack("<I", buf[co + 8:co + 12])[0]
    if "SysRegs" in sections[sect_name]:
        so, ss = sections[sect_name]["SysRegs"]
        if ss >= 12:
            # SysRegs layout: MACL, MACH, PR (3 x uint32)
            out["MACL"] = struct.unpack("<I", buf[so:so + 4])[0]
            out["MACH"] = struct.unpack("<I", buf[so + 4:so + 8])[0]
            out["PR"] = struct.unpack("<I", buf[so + 8:so + 12])[0]
    return out if out else None


def _dump_region(
    sections: Dict, section: str, var: str, out_path: Path
) -> int:
    """Write a region to disk. Bytes are written in the host-LE order
    Mednafen serialised them in (NOT byte-swapped). Use --peek for
    Saturn-endian word reads."""
    data = _read_region(sections, section, var)
    if data is None:
        sys.stderr.write(
            f"mcs_extract: region {section}/{var} not in state\n"
        )
        return 3
    out_path.write_bytes(data)
    print(f"wrote {section}/{var} -> {out_path} ({len(data)} bytes)")
    return 0


def _synth_vdp1_regs(sections: Dict, out_path: Path) -> int:
    """VDP1 registers are stored as discrete variables in Mednafen, not
    as a single 32-byte block. Synthesize the block per ST-013-R3 layout
    (offsets 0x00..0x12 covered; 0x14-0x1F are write-only/reserved)."""
    blk = bytearray(0x20)
    layout = [
        (0x00, "TVMR"),
        (0x02, "FBCR"),
        (0x04, "PTMR"),
        (0x06, "EWDR"),
        (0x08, "EWLR"),
        (0x0A, "EWRR"),
        (0x10, "EDSR"),
        (0x12, "LOPR"),
    ]
    if "VDP1" not in sections:
        sys.stderr.write("mcs_extract: VDP1 section absent\n")
        return 3
    buf = sections["__buf_bytes__"]
    for off, var in layout:
        if var in sections["VDP1"]:
            vo, vs = sections["VDP1"][var]
            if vs == 2:
                # Mednafen LE uint16 -> Saturn BE bytes.
                w_le = struct.unpack("<H", buf[vo:vo + vs])[0]
                blk[off] = (w_le >> 8) & 0xFF
                blk[off + 1] = w_le & 0xFF
    out_path.write_bytes(bytes(blk))
    print(f"wrote synthesized VDP1 regs -> {out_path} (32 bytes)")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="Mednafen Saturn savestate (.mc0/.mcs) parser."
    )
    p.add_argument("mcs", help="path to Mednafen savestate file")
    p.add_argument("--peek", action="append", default=[],
                   help="hex/decimal Saturn ADDR; print 32-bit big-endian uint32")
    p.add_argument("--peek16", action="append", default=[],
                   help="hex/decimal Saturn ADDR; print 16-bit big-endian uint16")
    p.add_argument("--peek8", action="append", default=[],
                   help="hex/decimal Saturn ADDR; print 8-bit uint8")
    p.add_argument("--vdp1-vram", metavar="OUT")
    p.add_argument("--vdp1-fb",   metavar="OUT")
    p.add_argument("--vdp2-vram", metavar="OUT")
    p.add_argument("--cram",      metavar="OUT")
    p.add_argument("--vdp1-regs", metavar="OUT")
    p.add_argument("--vdp2-regs", metavar="OUT")
    p.add_argument("--wram-l",    metavar="OUT")
    p.add_argument("--wram-h",    metavar="OUT")
    p.add_argument("--backup",    metavar="OUT")
    p.add_argument("--sh2m-regs", action="store_true")
    p.add_argument("--sh2s-regs", action="store_true")
    p.add_argument("--list-sections", action="store_true")
    p.add_argument("--list-vars", metavar="SECT")
    p.add_argument("--json", action="store_true",
                   help="emit all peeked values + sh2 regs as a single JSON object")
    a = p.parse_args(argv)

    sections = parse_savestate(Path(a.mcs))
    rc = 0
    j: Dict[str, object] = {}

    if a.list_sections:
        keys = [k for k in sections.keys() if not k.startswith("__")]
        for n in keys:
            total = sum(s for _, s in sections[n].values())
            print(f"  {n:32s} {len(sections[n])} vars, payload {total} B")

    if a.list_vars:
        if a.list_vars not in sections:
            sys.stderr.write(f"mcs_extract: section {a.list_vars} absent\n")
            return 3
        for vn, (vo, vs) in sections[a.list_vars].items():
            print(f"  {vn:24s} off=0x{vo:08x} size={vs}")

    def _addr(s: str) -> int:
        return int(s, 16) if s.lower().startswith("0x") else int(s, 0)

    for s in a.peek:
        addr = _addr(s)
        v = _peek_bytes(sections, addr, 4)
        if v is None:
            sys.stderr.write(f"mcs_extract: peek {hex(addr)} not in any region\n")
            rc = 3
            continue
        val = struct.unpack(">I", v)[0]
        if a.json:
            j[f"peek32_{hex(addr)}"] = val
        else:
            print(f"peek32 {hex(addr)} = 0x{val:08x}")

    for s in a.peek16:
        addr = _addr(s)
        v = _peek_bytes(sections, addr, 2)
        if v is None:
            sys.stderr.write(f"mcs_extract: peek16 {hex(addr)} not in any region\n")
            rc = 3
            continue
        val = struct.unpack(">H", v)[0]
        if a.json:
            j[f"peek16_{hex(addr)}"] = val
        else:
            print(f"peek16 {hex(addr)} = 0x{val:04x}")

    for s in a.peek8:
        addr = _addr(s)
        v = _peek_bytes(sections, addr, 1)
        if v is None:
            sys.stderr.write(f"mcs_extract: peek8 {hex(addr)} not in any region\n")
            rc = 3
            continue
        val = v[0]
        if a.json:
            j[f"peek8_{hex(addr)}"] = val
        else:
            print(f"peek8 {hex(addr)} = 0x{val:02x}")

    if a.vdp1_vram:
        rc = max(rc, _dump_region(sections, "VDP1", "VRAM", Path(a.vdp1_vram)))
    if a.vdp1_fb:
        rc = max(rc, _dump_region(sections, "VDP1", "&FB[0][0]", Path(a.vdp1_fb)))
    if a.vdp2_vram:
        rc = max(rc, _dump_region(sections, "VDP2", "VRAM", Path(a.vdp2_vram)))
    if a.cram:
        rc = max(rc, _dump_region(sections, "VDP2", "CRAM", Path(a.cram)))
    if a.vdp1_regs:
        rc = max(rc, _synth_vdp1_regs(sections, Path(a.vdp1_regs)))
    if a.vdp2_regs:
        rc = max(rc, _dump_region(sections, "VDP2", "RawRegs", Path(a.vdp2_regs)))
    if a.wram_l:
        rc = max(rc, _dump_region(sections, "MAIN", "WorkRAML", Path(a.wram_l)))
    if a.wram_h:
        rc = max(rc, _dump_region(sections, "MAIN", "WorkRAMH", Path(a.wram_h)))
    if a.backup:
        rc = max(rc, _dump_region(sections, "MAIN", "BackupRAM", Path(a.backup)))

    if a.sh2m_regs:
        regs = _sh2_regs(sections, "master")
        if regs is None:
            sys.stderr.write("mcs_extract: SH2-M section absent or no regs\n")
            rc = 3
        else:
            if a.json:
                j["sh2m"] = regs
            else:
                print("SH2-M registers:")
                for k in sorted(regs):
                    print(f"  {k:5s} = 0x{regs[k]:08x}")

    if a.sh2s_regs:
        regs = _sh2_regs(sections, "slave")
        if regs is None:
            sys.stderr.write("mcs_extract: SH2-S section absent or no regs\n")
            rc = 3
        else:
            if a.json:
                j["sh2s"] = regs
            else:
                print("SH2-S registers:")
                for k in sorted(regs):
                    print(f"  {k:5s} = 0x{regs[k]:08x}")

    if a.json:
        print(json.dumps(j, indent=2, sort_keys=True))

    return rc


if __name__ == "__main__":
    sys.exit(main())
