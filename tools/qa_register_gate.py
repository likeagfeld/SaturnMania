#!/usr/bin/env python3
"""qa_register_gate.py - Register-contract verification for verify_done.

Reads a baseline JSON (the "what should be true at this checkpoint"
contract) and asserts it against a captured Mednafen .mc0/.mcs state.
Used by tools/verify_done.ps1 Gate V-REG to enforce that prior phases'
hardware-register fixes don't silently regress.

Baseline JSON schema:
{
  "<checkpoint-name>": {
    "<assertion-name>": {
      "addr": "0x25F800E0",           # Saturn address (cache-through OK)
      "width": 16,                    # 8 / 16 / 32 bits
      "expect": "0x0023",             # expected value, or
      "expect_mask": "0xFF",          # ... value & mask
      "expect_in_range": [0, 0x100]   # ... or in [lo, hi]
      "expect_not": "0x0000"          # ... or not equal to
    },
    "<mem-assertion>": {
      "section": "MAIN",
      "var": "WorkRAMH",
      "kind": "byte-not-equal",
      "offset": 0,                    # offset within region (bytes)
      "expect_not": 0x00              # byte must not be 0x00
    }
  }
}

Multiple assertions per checkpoint are evaluated independently; each
gets a PASS/FAIL line on stdout. The script returns 0 only if every
assertion passes.

Usage:
    python tools/qa_register_gate.py <state.mcs> <baseline.json> \
        [--checkpoint title_settled] [--verbose]

Exit codes:
    0  all assertions PASS
    1  one or more assertions FAIL
    2  could not parse state / baseline
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

# Reuse the parser from mcs_extract; load via importlib because both
# files live in tools/ and we don't depend on sys.path tweaking.
import importlib.util
_HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location(
    "mcs_extract", _HERE / "mcs_extract.py"
)
mcs_extract = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mcs_extract)  # type: ignore[union-attr]


def _coerce_int(v):
    if isinstance(v, int):
        return v
    if isinstance(v, str):
        s = v.strip()
        return int(s, 16) if s.lower().startswith("0x") else int(s, 0)
    raise ValueError(f"cannot coerce to int: {v!r}")


def _peek_value(sections, addr: int, width: int) -> int | None:
    nbytes = width // 8
    raw = mcs_extract._peek_bytes(sections, addr, nbytes)
    if raw is None:
        return None
    if width == 8:
        return raw[0]
    if width == 16:
        return struct.unpack(">H", raw)[0]
    if width == 32:
        return struct.unpack(">I", raw)[0]
    raise ValueError(f"unsupported width {width}")


def _eval_assertion(sections, name: str, spec: dict, verbose: bool) -> bool:
    """Return True if PASS, False if FAIL. Prints status line."""
    # Memory-region assertions (section/var/offset/kind).
    if "section" in spec and "var" in spec:
        sect = spec["section"]
        var = spec["var"]
        if sect not in sections or var not in sections[sect]:
            print(f"  FAIL {name}: section/var {sect}/{var} absent from state")
            return False
        off, sz = sections[sect][var]
        buf = sections["__buf_bytes__"]
        kind = spec.get("kind", "byte-equal")
        rel_off = _coerce_int(spec.get("offset", 0))
        if rel_off >= sz:
            print(f"  FAIL {name}: offset {rel_off} >= region size {sz}")
            return False
        actual = buf[off + rel_off]
        if kind == "byte-equal":
            exp = _coerce_int(spec["expect"])
            ok = actual == exp
            print(
                f"  {'PASS' if ok else 'FAIL'} {name}: "
                f"{sect}/{var}[+{rel_off}] = 0x{actual:02x}, "
                f"expected 0x{exp:02x}"
            )
            return ok
        if kind == "byte-not-equal":
            exp = _coerce_int(spec["expect_not"])
            ok = actual != exp
            print(
                f"  {'PASS' if ok else 'FAIL'} {name}: "
                f"{sect}/{var}[+{rel_off}] = 0x{actual:02x}, "
                f"expected NOT 0x{exp:02x}"
            )
            return ok
        if kind == "u32-not-zero":
            val = struct.unpack(
                "<I", buf[off + rel_off:off + rel_off + 4]
            )[0]
            ok = val != 0
            print(
                f"  {'PASS' if ok else 'FAIL'} {name}: "
                f"{sect}/{var}[+{rel_off}] LE-u32 = 0x{val:08x}, "
                f"expected != 0"
            )
            return ok
        print(f"  FAIL {name}: unknown kind {kind!r}")
        return False

    # Address-based assertions.
    if "addr" not in spec:
        print(f"  FAIL {name}: assertion lacks 'addr' or 'section'+'var'")
        return False
    addr = _coerce_int(spec["addr"])
    width = int(spec.get("width", 16))
    actual = _peek_value(sections, addr, width)
    if actual is None:
        print(f"  FAIL {name}: peek {hex(addr)} (w={width}) not in any region")
        return False

    def _fmt(x: int) -> str:
        if width == 8:
            return f"0x{x:02x}"
        if width == 16:
            return f"0x{x:04x}"
        return f"0x{x:08x}"

    if "expect" in spec:
        exp = _coerce_int(spec["expect"])
        mask = _coerce_int(spec.get("expect_mask", (1 << width) - 1))
        ok = (actual & mask) == (exp & mask)
        print(
            f"  {'PASS' if ok else 'FAIL'} {name}: "
            f"@{hex(addr)} = {_fmt(actual)} & {_fmt(mask)} -> "
            f"{_fmt(actual & mask)}, expected {_fmt(exp & mask)}"
        )
        return ok
    if "expect_not" in spec:
        exp = _coerce_int(spec["expect_not"])
        ok = actual != exp
        print(
            f"  {'PASS' if ok else 'FAIL'} {name}: "
            f"@{hex(addr)} = {_fmt(actual)}, expected != {_fmt(exp)}"
        )
        return ok
    if "expect_in_range" in spec:
        lo, hi = spec["expect_in_range"]
        lo = _coerce_int(lo)
        hi = _coerce_int(hi)
        ok = lo <= actual <= hi
        print(
            f"  {'PASS' if ok else 'FAIL'} {name}: "
            f"@{hex(addr)} = {_fmt(actual)}, expected in "
            f"[{_fmt(lo)},{_fmt(hi)}]"
        )
        return ok
    if "expect_bits_set" in spec:
        bits = _coerce_int(spec["expect_bits_set"])
        ok = (actual & bits) == bits
        print(
            f"  {'PASS' if ok else 'FAIL'} {name}: "
            f"@{hex(addr)} = {_fmt(actual)} & {_fmt(bits)} = "
            f"{_fmt(actual & bits)}, expected = {_fmt(bits)}"
        )
        return ok

    print(f"  FAIL {name}: assertion has no recognised predicate")
    return False


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="verify Saturn register/memory contract in a .mc0 state"
    )
    p.add_argument("mcs")
    p.add_argument("baseline")
    p.add_argument("--checkpoint", default=None,
                   help="select a specific top-level key in baseline; "
                        "default = first key")
    p.add_argument("--verbose", action="store_true")
    a = p.parse_args(argv)

    try:
        sections = mcs_extract.parse_savestate(Path(a.mcs))
    except SystemExit as exc:
        sys.stderr.write(str(exc) + "\n")
        return 2

    try:
        baseline = json.loads(Path(a.baseline).read_text(encoding="utf-8"))
    except Exception as exc:
        sys.stderr.write(f"qa_register_gate: cannot read baseline: {exc}\n")
        return 2

    if a.checkpoint:
        if a.checkpoint not in baseline:
            sys.stderr.write(
                f"qa_register_gate: checkpoint {a.checkpoint!r} not in baseline "
                f"(have: {list(baseline.keys())})\n"
            )
            return 2
        ckp = a.checkpoint
    else:
        # Skip underscore-prefixed keys (reserved for _comment etc.).
        ckps = [k for k in baseline.keys() if not k.startswith("_")]
        if not ckps:
            sys.stderr.write("qa_register_gate: no non-_ keys in baseline\n")
            return 2
        ckp = ckps[0]

    print(f"qa_register_gate: checkpoint {ckp!r} ({len(baseline[ckp])} assertions)")
    all_ok = True
    for name, spec in baseline[ckp].items():
        ok = _eval_assertion(sections, name, spec, a.verbose)
        all_ok = all_ok and ok

    if all_ok:
        print(f"qa_register_gate: PASS ({ckp})")
        return 0
    print(f"qa_register_gate: FAIL ({ckp})")
    return 1


if __name__ == "__main__":
    sys.exit(main())
