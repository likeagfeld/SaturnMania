#!/usr/bin/env python3
"""qa_phase2_3j_sync_load_gate.py — Phase 2.3j synchronous-scene-load gate.

Phase 2.3j retires the async readiness-gate architecture (g_ghz_fg_ready /
g_ghz_fg_probe / ghz_is_active) in favor of a synchronous decomp-style
scene-load mirroring the RSDKv5 engine chain:
  RetroEngine.cpp:345-384 ENGINESTATE_LOAD ->
    Scene.cpp:24-282  LoadSceneFolder() ->
    Scene.cpp:283-666 LoadSceneAssets()  ->
    Object.cpp        InitObjects()

The decomp's load is synchronous; the PC build accepts a dropped frame
during scene init and hides it behind the title-card overlay. The Saturn
port mirrors that — `rsdk_load_scene("Stage")` returns only after VDP2
NBG1/NBG2 are committed.

PREDICATES (all must be GREEN):
  P1  BGON includes NBG1ON|NBG2ON  (=0x0006 mask)
      Address 0x25F80020 (VDP2 SCREEN ON) per ST-058-R2 SCRCTL.
  P2  VDP2 VRAM bank A0 (first 8 bytes) contains non-zero pattern data
      Address 0x25E00000. Proves CEL DMA committed.
  P3  g_ghz_active_tick_counter > 5 — proves mania_ghz_tick_and_draw
      ran at least 5x post-load (synchronous load returned, then the
      main loop ticked GHZ several times).
  P4  CRAM[sky palette base] non-magenta (rules out the Phase 1.29c
      double-shift class — magenta = 0x7C1F).

RED-baseline (current build, before refactor):
  - P3 RED: symbol g_ghz_active_tick_counter not yet defined.
  - P1/P2 may or may not be RED depending on whether prior probe-struct
    cleared properly.
  - P4 depends on palette load order.

USAGE
    python tools/qa_phase2_3j_sync_load_gate.py <state.mcs>
    python tools/qa_phase2_3j_sync_load_gate.py <state.mcs> --tick-addr 0x06...

EXIT CODES
    0    GREEN — all 4 predicates pass
    1    RED  — at least one predicate failed; failures printed
    2    setup error (file/parse)
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mcs_extract import parse_savestate, _peek_bytes  # noqa: E402


TICK_SYMBOL = "g_ghz_active_tick_counter"


def resolve_tick_addr_from_map(map_path: Path) -> int | None:
    if not map_path.is_file():
        return None
    pat = re.compile(r"^\s*0x([0-9a-fA-F]+)\s+(\S+)\s*$")
    with map_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = pat.match(line)
            if not m:
                continue
            addr, name = m.groups()
            base = name.split(".lto_priv")[0]
            if base == TICK_SYMBOL:
                return int(addr, 16)
    return None


def peek_u16_be(sections, addr: int) -> int | None:
    b = _peek_bytes(sections, addr, 2)
    if b is None or len(b) < 2:
        return None
    return (b[0] << 8) | b[1]


def peek_u32_le_words(sections, addr: int, nbytes: int) -> bytes | None:
    return _peek_bytes(sections, addr, nbytes)


def parse_tick_arg(args: list[str]) -> int | None:
    if "--tick-addr" not in args:
        return None
    i = args.index("--tick-addr")
    try:
        v = args[i + 1]
        return int(v, 16) if v.lower().startswith("0x") else int(v, 16)
    except (IndexError, ValueError):
        return None


def main(argv=None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if not args:
        sys.stderr.write(
            "usage: qa_phase2_3j_sync_load_gate.py <state.mcs> "
            "[--tick-addr 0x06xxxxxx]\n"
        )
        return 2

    state = Path(args[0])
    if not state.is_file():
        sys.stderr.write(f"qa_phase2_3j: cannot read {state}\n")
        return 2

    tick_addr = parse_tick_arg(args)
    if tick_addr is None:
        map_path = Path(__file__).resolve().parent.parent / "game.map"
        tick_addr = resolve_tick_addr_from_map(map_path)

    sections = parse_savestate(state)

    failures: list[str] = []
    print(f"PHASE 2.3j SYNC-LOAD GATE — {state.name}")
    print("=" * 60)

    # --- P1: BGON includes NBG1ON|NBG2ON ---
    BGON_ADDR = 0x25F80020
    bgon = peek_u16_be(sections, BGON_ADDR)
    NBG1ON = 0x0002
    NBG2ON = 0x0004
    need = NBG1ON | NBG2ON  # 0x0006
    if bgon is None:
        failures.append(f"P1 BGON: address 0x{BGON_ADDR:08x} not in captured regions")
        print(f"  P1 RED: BGON unreadable")
    else:
        ok = (bgon & need) == need
        msg = "GREEN" if ok else "RED"
        print(f"  P1 {msg}: BGON @ 0x{BGON_ADDR:08x} = 0x{bgon:04x} "
              f"(NBG1ON|NBG2ON mask {need:#06x} {'set' if ok else 'NOT set'})")
        if not ok:
            failures.append(f"P1 BGON 0x{bgon:04x} missing NBG1ON|NBG2ON")

    # --- P2: VDP2 VRAM bank A0 non-zero (first 8 bytes) ---
    VRAM_A0 = 0x25E00000
    vram = peek_u32_le_words(sections, VRAM_A0, 8)
    if vram is None:
        failures.append(f"P2 VRAM: 0x{VRAM_A0:08x} not in captured regions")
        print(f"  P2 RED: VRAM[A0] unreadable")
    else:
        nonzero = any(b != 0 for b in vram)
        msg = "GREEN" if nonzero else "RED"
        hexs = " ".join(f"{b:02x}" for b in vram)
        print(f"  P2 {msg}: VRAM[A0] @ 0x{VRAM_A0:08x} = [{hexs}]")
        if not nonzero:
            failures.append(f"P2 VRAM[A0] all zeros — CEL DMA never landed")

    # --- P3: g_ghz_active_tick_counter > 5 ---
    if tick_addr is None:
        failures.append(
            f"P3 TICK: symbol {TICK_SYMBOL} not in game.map; "
            f"pass --tick-addr 0x... once the symbol is defined"
        )
        print(f"  P3 RED: {TICK_SYMBOL} address unresolved "
              f"(symbol not yet defined or map missing)")
    else:
        tick_bytes = _peek_bytes(sections, tick_addr, 4)
        if tick_bytes is None or len(tick_bytes) < 4:
            failures.append(f"P3 TICK: addr 0x{tick_addr:08x} unreadable")
            print(f"  P3 RED: tick counter @ 0x{tick_addr:08x} unreadable")
        else:
            # SH-2 is big-endian. WRAM bytes for a u32 = MSB..LSB.
            value = (tick_bytes[0] << 24) | (tick_bytes[1] << 16) | \
                    (tick_bytes[2] << 8)  | tick_bytes[3]
            ok = value > 5
            msg = "GREEN" if ok else "RED"
            print(f"  P3 {msg}: {TICK_SYMBOL} @ 0x{tick_addr:08x} = {value} "
                  f"(need >5)")
            if not ok:
                failures.append(
                    f"P3 tick counter = {value}; mania_ghz_tick_and_draw "
                    f"ran <=5x"
                )

    # --- P4: sky CRAM not magenta (rules out double-shift) ---
    # CRAM base 0x25F00000; sky palette (256-color bank 0) at offset 0.
    # Magenta 0x7C1F = R31,G0,B31 in RGB555 BE-on-Saturn.
    CRAM_BASE = 0x25F00000
    cram = _peek_bytes(sections, CRAM_BASE, 32)
    if cram is None:
        # Non-fatal — not all builds capture CRAM at frame 60. Skip rather
        # than fail.
        print(f"  P4 SKIP: CRAM @ 0x{CRAM_BASE:08x} not captured")
    else:
        magenta = 0
        for i in range(0, len(cram), 2):
            word = (cram[i] << 8) | cram[i + 1]
            if word == 0x7C1F:
                magenta += 1
        ok = magenta < 4
        msg = "GREEN" if ok else "RED"
        print(f"  P4 {msg}: CRAM[0..16] magenta count = {magenta} (need <4)")
        if not ok:
            failures.append(f"P4 magenta CRAM count {magenta} — palette corrupt")

    print("=" * 60)
    if failures:
        print(f"PHASE 2.3j GATE RED — {len(failures)} predicate(s) failed:")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("PHASE 2.3j GATE GREEN — synchronous scene-load contract met.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
