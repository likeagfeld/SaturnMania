#!/usr/bin/env python3
"""qa_watch.py - Cross-state memory delta watcher.

Compares two (or more) Mednafen savestates and reports byte addresses
where a watched region changed. Use to:

  * Confirm that a static-state region really is static (e.g. SortList
    head pointer should not change frame-to-frame while the title is
    settled; if it does, something is racing).
  * Localise the bytes that change between two suspicious states
    (e.g. just before vs. just after a register clobber).

Usage:
    python tools/qa_watch.py STATE_A STATE_B [STATE_C ...] \
        --region {wram-h|wram-l|vdp2-vram|vdp1-vram|cram|vdp2-regs} \
        [--start 0x0000] [--len 0x1000] [--max-diffs 64] [--json]

If --region maps to a Saturn address space (e.g. vdp2-regs at
0x05F80000), --start may be either a relative offset into the region
or an absolute Saturn address — both are accepted; absolute addresses
in the region's range are converted automatically.

Exit codes:
    0  no diffs found (region is invariant across all input states)
    1  one or more diffs found
    2  cannot parse one of the states / bad arg
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location(
    "mcs_extract", _HERE / "mcs_extract.py"
)
mcs_extract = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mcs_extract)  # type: ignore[union-attr]


REGION_ALIAS = {
    # alias        -> (section, var, base_addr)
    "wram-l":      ("MAIN", "WorkRAML",   0x00200000),
    "wram-h":      ("MAIN", "WorkRAMH",   0x06000000),
    "backup":     ("MAIN", "BackupRAM",   0x00180000),
    "vdp1-vram":   ("VDP1", "VRAM",       0x05C00000),
    "vdp1-fb":     ("VDP1", "&FB[0][0]",  0x05C80000),
    "vdp2-vram":   ("VDP2", "VRAM",       0x05E00000),
    "cram":        ("VDP2", "CRAM",       0x05F00000),
    "vdp2-regs":   ("VDP2", "RawRegs",    0x05F80000),
}


def _coerce_int(s) -> int:
    if isinstance(s, int):
        return s
    s = str(s).strip()
    return int(s, 16) if s.lower().startswith("0x") else int(s, 0)


def _load_region(state_path: Path, region: str) -> tuple[bytes, int]:
    """Load (region_bytes, region_base_addr)."""
    if region not in REGION_ALIAS:
        raise SystemExit(
            f"qa_watch: unknown region {region!r}. "
            f"Valid: {sorted(REGION_ALIAS)}"
        )
    section, var, base = REGION_ALIAS[region]
    sections = mcs_extract.parse_savestate(state_path)
    if section not in sections or var not in sections[section]:
        raise SystemExit(
            f"qa_watch: {section}/{var} not in state {state_path}"
        )
    off, sz = sections[section][var]
    buf = sections["__buf_bytes__"]  # type: ignore[index]
    return buf[off:off + sz], base


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="cross-state memory delta watcher"
    )
    p.add_argument("states", nargs="+",
                   help="two or more .mc0/.mcs files")
    p.add_argument("--region", required=True,
                   help=f"region alias: {sorted(REGION_ALIAS)}")
    p.add_argument("--start", default="0",
                   help="start offset (relative or absolute Saturn addr)")
    p.add_argument("--len", default="0x40",
                   help="byte length to watch (default 64)")
    p.add_argument("--max-diffs", type=int, default=64,
                   help="cap number of diff bytes reported")
    p.add_argument("--json", action="store_true")
    a = p.parse_args(argv)

    if len(a.states) < 2:
        sys.stderr.write("qa_watch: need at least 2 state files\n")
        return 2

    region_bytes_list: list[bytes] = []
    region_base = 0
    for st in a.states:
        try:
            rb, region_base = _load_region(Path(st), a.region)
        except SystemExit as exc:
            sys.stderr.write(str(exc) + "\n")
            return 2
        region_bytes_list.append(rb)

    start = _coerce_int(a.start)
    # Normalise cache-through alias (0x2xxxxxxx -> 0x0xxxxxxx).
    if start & 0x20000000:
        start &= ~0x20000000
    if region_base <= start < region_base + len(region_bytes_list[0]):
        rel_start = start - region_base
    else:
        rel_start = start
    nbytes = _coerce_int(a.len)
    end = rel_start + nbytes
    if rel_start < 0 or end > len(region_bytes_list[0]):
        sys.stderr.write(
            f"qa_watch: window [{hex(rel_start)},{hex(end)}) outside "
            f"region {a.region} of size {hex(len(region_bytes_list[0]))}\n"
        )
        return 2

    diffs: list[dict] = []
    ref = region_bytes_list[0][rel_start:end]
    for i in range(rel_start, end):
        col = [region_bytes_list[s][i] for s in range(len(a.states))]
        if len(set(col)) > 1:
            sat_addr = region_base + i
            diffs.append({
                "saturn_addr": f"0x{sat_addr:08X}",
                "rel_offset": i - rel_start,
                "values": [f"0x{b:02x}" for b in col],
            })
            if len(diffs) >= a.max_diffs:
                break

    if a.json:
        print(json.dumps({
            "region": a.region,
            "window_start_rel": rel_start,
            "window_start_addr": f"0x{region_base + rel_start:08X}",
            "window_len": nbytes,
            "states": list(a.states),
            "diff_count": len(diffs),
            "diffs": diffs,
        }, indent=2))
    else:
        print(
            f"qa_watch: region={a.region}  base=0x{region_base:08X}  "
            f"window=[0x{rel_start:X}..0x{end:X})  states={len(a.states)}"
        )
        if not diffs:
            print("qa_watch: no diffs (region invariant across states)")
        else:
            print(f"qa_watch: {len(diffs)} diff byte(s):")
            for d in diffs:
                vals = " ".join(d["values"])
                print(
                    f"  {d['saturn_addr']}  rel+{d['rel_offset']:5d}  "
                    f"vals=[{vals}]"
                )
            if len(diffs) == a.max_diffs:
                print(f"  (truncated at --max-diffs={a.max_diffs})")

    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())
