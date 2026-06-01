#!/usr/bin/env python3
"""qa_phase2_3g_ghz_sonic_gate.py — Phase 2.3g RED-firing gate.

Bisects the GHZ Sonic-invisibility + NBG1 vertical-stripe regression by
peeking the known volatile flags + BGON + SH-2 PC from a Mednafen
savestate of a debug build configured with GHZ_AUTOADVANCE_TICKS=480
(so the title->GHZ transition fires automatically and SaveFrame=30
captures lands well after t=8s of GHZ-active).

PREDICATES (P1..P5 register/memory, P6 VDP1 cmd table) — gate is RED
when ANY predicate fails. The predicate that fails IS the bisect
result.

P1 BGON  @ 0x25F80020 must include NBG1ON (bit 1) + NBG2ON (bit 2) +
   SPRON (bit 6). Expected 0x0046 (verified from
   qa_phase2_3f_red_state.mcs Phase 2.3f baseline).
P2 g_ghz_fg_ready (volatile bool at 0x06032330 per game.map line 1508)
   must read 0x01.
   *** PHASE 2.3g FINDING (this gate fires RED on it) ***
   Phase 2.3f red baseline reads 0x00, despite ghz_setup_foreground
   setting it to true at scene_ghz.c:249 and never explicitly clearing
   it. ghz_is_active() returns false, so mania_ghz_tick_and_draw +
   ghz_fg_build_page + ghz_fg_vblank early-return every frame.
   This is the ROOT BLOCKER for both the NBG1 stripe garbage (page is
   never rebuilt after the one-shot at setup time) and Sonic
   invisibility (mania_ghz_tick_and_draw never enters its body).
P3 SPCTL @ 0x25F800E0 must equal 0x0023 (Phase 2.3c contract, ST-058-R2
   SPCTL = SPCLMD=1 + SPTYPE=3).
P4 master SH-2 PC must be within 0x06010000..0x06032000 (.text range
   per game.map). Falsifies hypothesis D (CD/GFS deadlock).
P5 nbg1_cell extern symbol (game.map) — its destination VDP2 VRAM byte
   should not be all-zero (if nbg1 wasn't populated, screen would be
   blank not stripes). Inspect first VDP2 VRAM word.

USAGE
    python tools/qa_phase2_3g_ghz_sonic_gate.py <state.mcs>

EXIT CODES
    0    GREEN — all predicates pass; root cause class either A or C
    1    RED — at least one predicate failed; output prints the
         failing predicate(s) for bisect localisation
    2    file/parse error

Tracked in CLAUDE.md §1 (Phase 2.3g binding scope) and per task #127.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

# Re-use mcs_extract internals for region/peek decoding.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from mcs_extract import parse_savestate, _peek_bytes, _sh2_regs  # noqa: E402


# Symbol addresses (from game.map; refresh on every rebuild).
ADDR_G_GHZ_FG_READY = 0x06032330   # game.map line 1508
ADDR_BGON           = 0x25F80020
ADDR_SPCTL          = 0x25F800E0
TEXT_LO             = 0x06010000
TEXT_HI             = 0x06032000   # __bstart per game.map line 1503-1504


def main(argv=None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 1:
        sys.stderr.write("usage: qa_phase2_3g_ghz_sonic_gate.py <state.mcs>\n")
        return 2

    state = Path(args[0])
    if not state.is_file():
        sys.stderr.write(f"qa_phase2_3g: cannot read {state}\n")
        return 2

    sections = parse_savestate(state)
    fails: list[str] = []

    # P1 BGON
    bgon_bytes = _peek_bytes(sections, ADDR_BGON, 2)
    if bgon_bytes is None:
        fails.append("P1 BGON: address not in any captured region")
    else:
        bgon = struct.unpack(">H", bgon_bytes)[0]
        # NBG1ON=bit1, NBG2ON=bit2, SPRON=bit6
        required = (1 << 1) | (1 << 2) | (1 << 6)
        if (bgon & required) != required:
            fails.append(
                f"P1 BGON @ 0x{ADDR_BGON:08x} = 0x{bgon:04x}; "
                f"missing bits from required 0x{required:04x} "
                f"(NBG1ON|NBG2ON|SPRON). GHZ not actually rendered."
            )
        else:
            print(f"  P1 BGON          = 0x{bgon:04x}  PASS")

    # P2 g_ghz_fg_ready (1-byte volatile bool)
    fg_bytes = _peek_bytes(sections, ADDR_G_GHZ_FG_READY, 1)
    if fg_bytes is None:
        fails.append("P2 g_ghz_fg_ready: address not in WRAM-H region")
    else:
        fg = fg_bytes[0]
        if fg != 0x01:
            fails.append(
                f"P2 g_ghz_fg_ready @ 0x{ADDR_G_GHZ_FG_READY:08x} = "
                f"0x{fg:02x} (expected 0x01). ROOT CAUSE: scene_ghz.c "
                f"sets the flag at line 249 and never explicitly clears "
                f"it; the flag reads false in a GHZ-active savestate so "
                f"ghz_is_active() returns false and mania_ghz_tick_and_"
                f"draw / ghz_fg_build_page / ghz_fg_vblank all early-"
                f"return. Investigate: (a) the value reads true initially "
                f"then a wild write clobbers byte 0x{ADDR_G_GHZ_FG_READY:08x} "
                f"(unlikely — adjacent g_rsdk_tilelayer_count=0x01 is "
                f"correct, so a wide overwrite is ruled out); OR (b) LTO "
                f"is propagating the static initializer (= false) as a "
                f"compile-time constant into ghz_is_active() callers "
                f"despite the `volatile` qualifier. Check ltrans0.ltrans.o "
                f"disasm for ghz_is_active inline body."
            )
        else:
            print(f"  P2 g_ghz_fg_ready = 0x{fg:02x}    PASS")

    # P3 SPCTL
    spctl_bytes = _peek_bytes(sections, ADDR_SPCTL, 2)
    if spctl_bytes is None:
        fails.append("P3 SPCTL: address not in VDP2/RawRegs")
    else:
        spctl = struct.unpack(">H", spctl_bytes)[0]
        if spctl != 0x0023:
            fails.append(
                f"P3 SPCTL @ 0x{ADDR_SPCTL:08x} = 0x{spctl:04x}; "
                f"expected 0x0023 (Phase 2.3c contract)."
            )
        else:
            print(f"  P3 SPCTL         = 0x{spctl:04x}  PASS")

    # P4 SH-2 PC in .text
    regs = _sh2_regs(sections, "master")
    if regs is None or "PC" not in regs:
        fails.append("P4 SH-2 PC: master regs absent")
    else:
        pc = regs["PC"]
        if not (TEXT_LO <= pc < TEXT_HI):
            fails.append(
                f"P4 SH-2 PC = 0x{pc:08x} outside .text "
                f"[0x{TEXT_LO:08x}, 0x{TEXT_HI:08x}). Possible CD/GFS "
                f"deadlock (hypothesis D)."
            )
        else:
            print(f"  P4 SH-2 PC       = 0x{pc:08x}  PASS")

    # P5 VDP2 VRAM first word at NBG1 cell base (rough sanity — first
    # 16-bit word of VDP2 VRAM should hold real cell data, not all-
    # zero, after ghz_setup_foreground has run slDMACopy of the cell
    # bank. All-zero would indicate the cell DMA never happened.
    v_bytes = _peek_bytes(sections, 0x05E00000, 4)
    if v_bytes is None:
        fails.append("P5 VDP2 VRAM start: not captured")
    else:
        v = struct.unpack(">I", v_bytes)[0]
        if v == 0x00000000:
            fails.append(
                "P5 VDP2 VRAM[0..3] is all-zero — cell-bank DMA never "
                "landed; NBG1 garbage symptom EXPLAINED purely by "
                "missing cell data."
            )
        else:
            print(f"  P5 VDP2 VRAM[0]  = 0x{v:08x}  PASS")

    if fails:
        print()
        print("PHASE 2.3g GATE RED — the following predicates failed:")
        for f in fails:
            print(f"  - {f}")
        return 1

    print()
    print("PHASE 2.3g GATE GREEN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
