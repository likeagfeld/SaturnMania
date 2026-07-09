#!/usr/bin/env python3
"""qa_ghz_crash_detect.py - deterministic #192 GHZ-crash detector.

WHY REGISTER-ONLY
-----------------
The #192 crash blanks Work-RAM: a human F5 on the already-crashed machine
power-fills every emulated RAM region, so POST-CRASH MEMORY IS FORENSICALLY
EMPTY (WRAM-L/H, VDP1/VDP2 VRAM, CRAM, backup RAM all read as power-on
fill). The ONLY authoritative data in a crashed capture is the SH-2
register file, which Mednafen serialises faithfully regardless of the RAM
blank. This gate therefore keys the crash verdict on the master SH-2
registers alone -- the one thing that survives the capture.

VERDICT
-------
  HEALTHY  <=>  PC  in Work-RAM-H [0x06000000,0x06100000) AND PC even
                AND R15(SP) in Work-RAM-H [0x06000000,0x06100000)
  CRASH    <=>  otherwise

Measured crash signature (tools/_crash192.mcs, #192):
  PC =0x00000003  boot-ROM, ODD -> illegal instruction fetch
  R15=0x13631f90  wild, outside Work-RAM-H -> master stack smashed
  PR =0x060141e0  ghz_fg_vblank+0x24 -> last good return addr, INTACT
Measured healthy signature (tools/_hud_states/gameplay.mcs):
  PC =0x0601aa76  Work-RAM-H .text, even
  R15=0x060ff838  Work-RAM-H master stack

Both invariants (PC-in-WRAMH, SP-in-WRAMH) independently catch the
documented derail, so the verdict is robust to whichever corruption
(PC clobber vs SP smash) the capture happens to freeze.

This is the RED-firing detector for the #192 deterministic-repro + bisect
(task #192). It is a DETECTOR, NOT A FIX: a GREEN verdict on one capture
means that capture did not catch the crash, NOT that the crash is fixed.

Exit codes:
    0 = HEALTHY      (registers sane; no crash signature in this capture)
    1 = CRASH        (PC or SP outside Work-RAM-H -> #192 derail caught)
    2 = HARNESS GAP  (state unreadable / SH2-M registers absent)
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

WRAMH_LO = 0x06000000
WRAMH_HI = 0x06100000
TEXT_LO  = 0x06004040          # .text base                 (game.map:342)
TEXT_HI  = 0x06022A20          # .text base + 0x1e9e0       (game.map:342)
DERAIL_PR = 0x060141E0         # ghz_fg_vblank(0x060141bc)+0x24 (game.map:475)
EPOOL_LO = 0x00260000          # FR-2 entity residency pool (entity_atlas.c:161)
EPOOL_HI = 0x00290000
VDP1_VRAM_LO = 0x05C00000      # VDP1 VRAM
VDP1_VRAM_HI = 0x05C80000


def _in(v, lo, hi):
    return lo <= v < hi


def _norm(v):
    # collapse the cache-through |0x20000000 alias for region tests
    return (v & ~0x20000000) if (v & 0x20000000) else v


def detect(state_path):
    """Return (healthy: bool|None, regs: dict|None). healthy is None on gap."""
    sections = mcs_extract.parse_savestate(Path(state_path))
    regs = mcs_extract._sh2_regs(sections, "master")
    if not regs or "PC" not in regs or "R15" not in regs:
        return None, regs
    pc = regs["PC"]
    sp = regs["R15"]
    pc_ok = _in(pc, WRAMH_LO, WRAMH_HI) and (pc & 1) == 0
    sp_ok = _in(sp, WRAMH_LO, WRAMH_HI)
    return (pc_ok and sp_ok), regs


def main(argv=None):
    ap = argparse.ArgumentParser(
        description="detect the #192 GHZ crash from SH2-M registers")
    ap.add_argument("state", help="GHZ savestate (.mc0/.mcs)")
    ap.add_argument("--json", action="store_true",
                    help="emit verdict + registers as one JSON object")
    a = ap.parse_args(argv)

    if not Path(a.state).exists():
        sys.stderr.write(f"qa_ghz_crash_detect: state not found: {a.state}\n")
        return 2
    try:
        healthy, regs = detect(a.state)
    except SystemExit as exc:
        sys.stderr.write(str(exc) + "\n")
        return 2
    except Exception as exc:  # noqa: BLE001
        sys.stderr.write(f"qa_ghz_crash_detect: cannot parse state: {exc}\n")
        return 2

    if regs is None or "PC" not in regs or "R15" not in regs:
        sys.stderr.write(
            "qa_ghz_crash_detect: SH2-M registers absent from state\n")
        return 2

    pc = regs["PC"]
    sp = regs["R15"]
    pr = regs.get("PR", 0)
    pc_ok = _in(pc, WRAMH_LO, WRAMH_HI) and (pc & 1) == 0
    sp_ok = _in(sp, WRAMH_LO, WRAMH_HI)
    in_text = _in(pc, TEXT_LO, TEXT_HI)
    r11 = regs.get("R11", 0)
    r13 = regs.get("R13", 0)
    r11_epool = _in(_norm(r11), EPOOL_LO, EPOOL_HI)
    r13_vdp1 = _in(_norm(r13), VDP1_VRAM_LO, VDP1_VRAM_HI)

    if a.json:
        print(json.dumps({
            "healthy": bool(healthy),
            "verdict": "HEALTHY" if healthy else "CRASH",
            "pc": pc, "sp": sp, "pr": pr,
            "pc_in_wramh": pc_ok, "sp_in_wramh": sp_ok, "pc_in_text": in_text,
            "pr_is_derail": pr == DERAIL_PR,
            "r11": r11, "r11_in_entity_pool": r11_epool,
            "r13": r13, "r13_in_vdp1_vram": r13_vdp1,
        }))
        return 0 if healthy else 1

    print("GHZ #192 CRASH DETECTOR (SH2-M register signature)")
    print("-" * 62)
    pc_note = ("in WRAM-H .text" if in_text else
               ("in WRAM-H" if pc_ok else "*** OUT OF WRAM-H ***"))
    if pc & 1:
        pc_note += "  *** ODD (illegal fetch) ***"
    print(f"  PC  = 0x{pc:08x}   {pc_note}")
    sp_note = ("in WRAM-H stack" if sp_ok
               else "*** WILD / OUT OF WRAM-H (stack smash) ***")
    print(f"  R15 = 0x{sp:08x}   {sp_note}")
    pr_note = ("== ghz_fg_vblank+0x24 (documented #192 derail)"
               if pr == DERAIL_PR else "")
    print(f"  PR  = 0x{pr:08x}   {pr_note}")
    print(f"  R11 = 0x{r11:08x}   "
          f"{'-> FR-2 entity pool [0x260000,0x290000)' if r11_epool else ''}")
    print(f"  R13 = 0x{r13:08x}   "
          f"{'-> VDP1 VRAM (sprite/texture pointer)' if r13_vdp1 else ''}")
    print("-" * 62)
    if healthy:
        print("  GREEN: SH2-M PC + SP both in Work-RAM-H -> no #192 crash in")
        print("         this capture. DETECTOR ONLY: GREEN means this capture")
        print("         did not catch the crash, NOT that the crash is fixed.")
        return 0
    why = []
    if not pc_ok:
        why.append("PC outside Work-RAM-H" + (" (odd)" if pc & 1 else ""))
    if not sp_ok:
        why.append("SP smashed outside Work-RAM-H")
    print("  RED: #192 crash signature present -- " + "; ".join(why))
    print("       The master SH-2 has derailed out of Work-RAM-H. PR names")
    print("       the last good frame; R11/R13 name what it was touching.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
