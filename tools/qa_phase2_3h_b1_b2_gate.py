#!/usr/bin/env python3
"""qa_phase2_3h_b1_b2_gate.py — Phase 2.3h B1-vs-B2 sub-bisect gate.

Phase 2.3g identified `g_ghz_fg_ready` reads 0x00 at SaveFrame=30 despite
scene_ghz.c:249 setting it true. Phase 2.3h adds a canary + mirror probe
apparatus to distinguish B1 (wild write to specific byte) from B2 (LTO
constant-prop through volatile).

DECISION MATRIX (per Task #128 plan):
  | g_ghz_fg_ready | g_ghz_fg_mirror | canary_pre | canary_post | Verdict |
  | 0x00           | 0x01            | 0x5A       | 0x5A        | B1: wild write at the specific ready byte |
  | 0x00           | 0x00            | 0x5A       | 0x5A        | B2: LTO constant-prop / store elision    |
  | 0x00           | 0x01            | 0x00       | 0x5A        | spatial overwrite low side               |
  | 0x00           | 0x01            | 0x5A       | 0x00        | spatial overwrite high side              |
  | 0x01           | 0x01            | 0x5A       | 0x5A        | GREEN — clobber resolved                 |
  | any canary != 0x5A and != 0x00                              | spatial overflow                        |
  | otherwise                                                    | inconclusive (need more canaries)       |

POST-FIX GREEN:
  ready=0x01, mirror=0x01, both canaries=0x5A.

USAGE
    python tools/qa_phase2_3h_b1_b2_gate.py <state.mcs>
    python tools/qa_phase2_3h_b1_b2_gate.py <state.mcs> --addrs READY=0x... MIRROR=0x... PRE=0x... POST=0x...

If the four BSS addresses are not passed, the gate falls back to map-
parsing `game.map` for the four extern-linkage symbols.

EXIT CODES
    0    GREEN — all probes show fix landed (ready=1, mirror=1, canaries=0x5A)
    1    RED — at least one probe failed; verdict printed
    2    file/parse error / addresses unresolved

Tracked in CLAUDE.md §1 (Phase 2.3h binding scope) and per task #128.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

# Re-use mcs_extract internals for region/peek decoding.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from mcs_extract import parse_savestate, _peek_bytes  # noqa: E402


SYMBOL_NAMES = (
    "g_ghz_fg_canary_pre",
    "g_ghz_fg_ready",
    "g_ghz_fg_canary_post",
    "g_ghz_fg_mirror",
)


def resolve_addrs_from_map(map_path: Path) -> dict[str, int]:
    """Parse game.map for the four canary symbol addresses."""
    addrs: dict[str, int] = {}
    if not map_path.is_file():
        return addrs
    pat = re.compile(r"^\s*0x([0-9a-fA-F]+)\s+(\S+)\s*$")
    with map_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.match(line)
            if not m:
                continue
            addr, name = m.groups()
            # Strip LTO suffix .lto_priv.NNN if present.
            base = name.split(".lto_priv")[0]
            if base in SYMBOL_NAMES and base not in addrs:
                addrs[base] = int(addr, 16)
    return addrs


def parse_arg_addrs(args: list[str]) -> dict[str, int]:
    """Parse CLI --addrs KEY=hex KEY=hex ..."""
    out: dict[str, int] = {}
    if "--addrs" not in args:
        return out
    i = args.index("--addrs")
    rest = args[i + 1:]
    name_map = {
        "READY": "g_ghz_fg_ready",
        "MIRROR": "g_ghz_fg_mirror",
        "PRE": "g_ghz_fg_canary_pre",
        "POST": "g_ghz_fg_canary_post",
    }
    for token in rest:
        if "=" not in token:
            break
        k, v = token.split("=", 1)
        if k not in name_map:
            sys.stderr.write(f"qa_phase2_3h: unknown addr key {k}\n")
            return {}
        try:
            out[name_map[k]] = int(v, 16) if v.lower().startswith("0x") else int(v, 16)
        except ValueError:
            sys.stderr.write(f"qa_phase2_3h: bad hex for {k}={v}\n")
            return {}
    return out


def main(argv=None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if not args:
        sys.stderr.write("usage: qa_phase2_3h_b1_b2_gate.py <state.mcs> [--addrs READY=0x... MIRROR=0x... PRE=0x... POST=0x...]\n")
        return 2

    state = Path(args[0])
    if not state.is_file():
        sys.stderr.write(f"qa_phase2_3h: cannot read {state}\n")
        return 2

    addrs = parse_arg_addrs(args)
    if not addrs:
        # Fallback: parse game.map next to this tool.
        map_path = Path(__file__).resolve().parent.parent / "game.map"
        addrs = resolve_addrs_from_map(map_path)

    missing = [n for n in SYMBOL_NAMES if n not in addrs]
    if missing:
        sys.stderr.write(
            f"qa_phase2_3h: missing addresses for {missing}\n"
            f"  pass --addrs READY=... MIRROR=... PRE=... POST=...\n"
        )
        return 2

    sections = parse_savestate(state)
    peeks: dict[str, int] = {}
    for name in SYMBOL_NAMES:
        b = _peek_bytes(sections, addrs[name], 1)
        if b is None:
            sys.stderr.write(f"qa_phase2_3h: addr 0x{addrs[name]:08x} for {name} not in any captured region\n")
            return 2
        peeks[name] = b[0]

    pre    = peeks["g_ghz_fg_canary_pre"]
    ready  = peeks["g_ghz_fg_ready"]
    post   = peeks["g_ghz_fg_canary_post"]
    mirror = peeks["g_ghz_fg_mirror"]

    print(f"  g_ghz_fg_canary_pre  @ 0x{addrs['g_ghz_fg_canary_pre']:08x} = 0x{pre:02x}")
    print(f"  g_ghz_fg_ready       @ 0x{addrs['g_ghz_fg_ready']:08x} = 0x{ready:02x}")
    print(f"  g_ghz_fg_canary_post @ 0x{addrs['g_ghz_fg_canary_post']:08x} = 0x{post:02x}")
    print(f"  g_ghz_fg_mirror      @ 0x{addrs['g_ghz_fg_mirror']:08x} = 0x{mirror:02x}")
    print()

    # GREEN case first.
    if ready == 0x01 and mirror == 0x01 and pre == 0x5A and post == 0x5A:
        print("PHASE 2.3h GATE GREEN — g_ghz_fg_ready clobber resolved.")
        return 0

    # Spatial-overflow probes first (canaries should be 0x5A post commit).
    if pre not in (0x5A,) or post not in (0x5A,):
        if pre != 0x5A and pre != 0x00:
            print(f"PHASE 2.3h GATE RED — verdict: spatial corruption on LOW canary "
                  f"(pre=0x{pre:02x}; expected 0x5A). A buffer overflow / wide memset "
                  f"is reaching into the ready-flag neighborhood.")
            return 1
        if post != 0x5A and post != 0x00:
            print(f"PHASE 2.3h GATE RED — verdict: spatial corruption on HIGH canary "
                  f"(post=0x{post:02x}; expected 0x5A).")
            return 1
        if pre == 0x00 and post == 0x5A:
            print(f"PHASE 2.3h GATE RED — verdict: LOW-side spatial overwrite reaches "
                  f"canary_pre (=0x00). Inspect symbols immediately below "
                  f"g_ghz_fg_canary_pre in game.map and audit for off-by-one stores.")
            return 1
        if pre == 0x5A and post == 0x00:
            print(f"PHASE 2.3h GATE RED — verdict: HIGH-side spatial overwrite reaches "
                  f"canary_post (=0x00).")
            return 1
        # Both canaries at 0x00 -- the commit code never ran (build/LTO elided
        # the entire commit block).
        if pre == 0x00 and post == 0x00:
            print(f"PHASE 2.3h GATE RED — verdict: BOTH canaries unwritten "
                  f"(pre=0x00, post=0x00). The commit-time block in "
                  f"ghz_setup_foreground was never executed OR was entirely "
                  f"LTO-elided. Either ghz_setup_foreground returned early "
                  f"(check fgpal/cel/pat allocations) or the build is wrong.")
            return 1

    # Canaries OK → ready/mirror disagreement decides B1 vs B2.
    if pre == 0x5A and post == 0x5A:
        if ready == 0x00 and mirror == 0x01:
            print("PHASE 2.3h GATE RED — verdict: B1 CONFIRMED (wild write).")
            print("  The commit block ran (canaries=0x5A, mirror=1) but the ")
            print("  ready byte at 0x{:08x} reads 0x00. A separate write is".format(addrs["g_ghz_fg_ready"]))
            print("  clobbering ONLY the ready byte, not its neighborhood.")
            print("  Investigation: search for single-byte stores to BSS-start")
            print("  or pointer arithmetic targeting 0x06032330 specifically.")
            print("  Audit game.map symbols at +/-256 bytes for buffer-base")
            print("  candidates whose [-N] index could land at the ready byte.")
            return 1
        if ready == 0x00 and mirror == 0x00:
            print("PHASE 2.3h GATE RED — verdict: B2 CONFIRMED (LTO constant-prop).")
            print("  The commit block stamped canaries=0x5A correctly but BOTH")
            print("  ready AND mirror read 0x00. LTO whole-program analysis is")
            print("  either eliding the bool stores OR the peek-time read is")
            print("  being constant-propagated from the static initializer.")
            print("  Fix: -fno-lto on scene_ghz.c (per-file CFLAGS override)")
            print("  OR move ghz_is_active body out of any header inline.")
            return 1

    # Anything else.
    print(f"PHASE 2.3h GATE RED — verdict: inconclusive — extend canary set.")
    print(f"  ready=0x{ready:02x} mirror=0x{mirror:02x} pre=0x{pre:02x} post=0x{post:02x}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
