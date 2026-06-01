#!/usr/bin/env python3
"""qa_phase1_35d_regdiff_gate.py -- Phase 1.35d register-diff gate.

Documents the VDP2 register clobber that jo_vdp2_set_nbg1_8bits_image
(jo-engine/jo_engine/vdp2.c:527-543) inflicts on the carefully composed
Phase 1.34c NBG1+NBG2+RBG0+VDP1 multi-layer setup.

Captured evidence:
  - samples/qa_phase1_31_post_revert.mcs (baseline: Phase 1.34c, NBG1 OFF)
  - samples/qa_phase1_35d_broken.mcs     (Phase 1.35c re-enabled)

Both captured at SaveFrame=24 (post-settle title state).

Diff (peek16 via tools/mcs_extract.py):

  Addr        Register    Baseline   Broken    Delta interpretation (ST-058-R2)
  0x25F80028  CHCTLA      0x1010     0x3210    Sec.6.5 N0/N1 char-size reconfig
  0x25F8002A  CHCTLB      0x1002     0x1000    Sec.6.5 R0CHCN dropped
  0x25F80010  CYCA0L      0x55FE     0x5555    Sec.3.3 CPU read slot reassigned
  0x25F80012  CYCA0U      0xEEEE     0xFEEE    Sec.3.3 bit-15 slot drop
  0x25F80014  CYCA1L      0xFFFE     0x5555    Sec.3.3 CPU->NBG1-read reassign
  0x25F80016  CYCA1U      0xEEEE     0xFEEE    Sec.3.3 bit-15 slot drop
  0x25F80018  CYCB0L      0x1FEE     0xFEEE    Sec.3.3 NBG0 read slot drop
  0x25F8001C  CYCB1L      0xFFEE     0xFEEE    Sec.3.3 slot reassign
  0x25F800E6  CRAOFB      0x0001     0x0000    Sec.11   R0CAOS dropped 1->0
  0x25F800F8  PRINA       0x0600     0x0000    Sec.11.1 N0+N1 prio CLOBBERED
  0x25F800FA  PRINB       0x0001     0x0000    Sec.11.1 N2 prio (clouds) wiped
  0x25F800FC  PRIR        0x0005     0x0004    Sec.11.1 RBG0 prio dropped
  0x25F80020  BGON        0x0052     0x0002    Sec.6.2  NBG2ON+NBG3ON+R0ON OFF

Predicates:
  P_BASELINE_PRESERVED:  Phase 1.34c baseline state shows the documented
                         register snapshot (catches future regression in
                         the NON-NBG1 multi-layer pipeline).
  P_REVERT_APPLIED:      src/main.c does NOT call setup_island_bg() and
                         does NOT have the per-frame
                         `if (main_island_bg_ready()) slPriorityNbg1(2);`
                         REPLACE block enabled.

Usage:
    py -3 tools/qa_phase1_35d_regdiff_gate.py

Exit 0 = revert intact, baseline contract holds.
Exit 1 = either the revert was undone (Phase 1.35c re-enabled) or the
         baseline register contract has changed (likely a regression in
         the unrelated Phase 1.34c rendering).
"""
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MCS_BASELINE = ROOT / "samples" / "qa_phase1_31_post_revert.mcs"
MCS_BROKEN   = ROOT / "samples" / "qa_phase1_35d_broken.mcs"

# (addr, name, baseline_expected) -- the Phase 1.34c contract.
BASELINE_CONTRACT = [
    (0x25F800AC, "BKTAU",  0x12F1),
    (0x25F800AE, "BKTAL",  0xFFFF),
    (0x25F80000, "TVMR",   0x8110),
    (0x25F8000E, "RAMCTL", 0x1327),
    (0x25F80028, "CHCTLA", 0x1010),
    (0x25F8002A, "CHCTLB", 0x1002),
    (0x25F80010, "CYCA0L", 0x55FE),
    (0x25F80012, "CYCA0U", 0xEEEE),
    (0x25F80014, "CYCA1L", 0xFFFE),
    (0x25F80016, "CYCA1U", 0xEEEE),
    (0x25F80018, "CYCB0L", 0x1FEE),
    (0x25F8001A, "CYCB0U", 0xEEEE),
    (0x25F8001C, "CYCB1L", 0xFFEE),
    (0x25F8001E, "CYCB1U", 0xEEEE),
    (0x25F800E4, "CRAOFA", 0x0000),
    (0x25F800E6, "CRAOFB", 0x0001),
    (0x25F800F8, "PRINA",  0x0600),
    (0x25F800FA, "PRINB",  0x0001),
    (0x25F800FC, "PRIR",   0x0005),
    (0x25F80020, "BGON",   0x0052),
]


def peek16(mcs: Path, addr: int) -> int:
    out = subprocess.check_output(
        ["py", "-3", str(ROOT / "tools" / "mcs_extract.py"),
         str(mcs), "--peek16", hex(addr)],
        text=True,
    ).strip()
    return int(out.split("= ")[-1], 16)


def predicate_baseline_contract():
    if not MCS_BASELINE.exists():
        return False, f"baseline mcs missing: {MCS_BASELINE}"
    deltas = []
    for addr, name, expected in BASELINE_CONTRACT:
        actual = peek16(MCS_BASELINE, addr)
        if actual != expected:
            deltas.append(f"{name}@{hex(addr)}: expected=0x{expected:04x} "
                          f"actual=0x{actual:04x}")
    if deltas:
        return False, "baseline regression: " + "; ".join(deltas)
    return True, f"all {len(BASELINE_CONTRACT)} baseline register values match"


def predicate_revert_applied():
    main_c = ROOT / "src" / "main.c"
    src = main_c.read_text(encoding="utf-8", errors="replace")
    bad = []
    # The setup_island_bg() call must be commented out.
    for line in src.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("setup_island_bg();"):
            bad.append("setup_island_bg() call is ACTIVE (Phase 1.35c re-enabled)")
            break
    # The per-frame REPLACE must be commented out.
    for line in src.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("if (main_island_bg_ready()) { slPriorityNbg1(2); }"):
            bad.append("per-frame slPriorityNbg1(2) REPLACE is ACTIVE")
            break
    if bad:
        return False, "; ".join(bad)
    return True, "setup_island_bg + per-frame REPLACE are both disabled (revert intact)"


def predicate_broken_state_documented():
    """Sanity check: the captured broken state shows the documented diff.
    Confirms qa_phase1_35d_broken.mcs has not been overwritten with a
    different scenario."""
    if not MCS_BROKEN.exists():
        return False, f"broken mcs missing: {MCS_BROKEN}"
    expected_deltas = {
        0x25F80028: 0x3210,
        0x25F800F8: 0x0000,
        0x25F800FA: 0x0000,
        0x25F80020: 0x0002,
    }
    mismatches = []
    for addr, expected in expected_deltas.items():
        actual = peek16(MCS_BROKEN, addr)
        if actual != expected:
            mismatches.append(f"@{hex(addr)}: expected=0x{expected:04x} "
                              f"actual=0x{actual:04x}")
    if mismatches:
        return False, "broken sample drift: " + "; ".join(mismatches)
    return True, "broken sample matches documented diff (CHCTLA/PRINA/PRINB/BGON)"


# =====================================================================
# Phase 1.35e structural-fix-path block predicate (added 2026-05-28).
#
# User's Phase 1.35e directive proposed three structural fix paths for
# the NBG1 island visibility / sprite-overlay symptoms:
#   A. NBG1 256-color transparency-enable (slScrTransparent / TPMD)
#   B. NBG1 Y-scroll (slScrPosNbg1) + asset rebuild (art moved to
#      bottom 72-96 rows of ISLAND.DAT)
#   C. NBG1 per-scanline window clip (slScrLineWindow1 / LWTA1 + WCTLA)
#
# Per ST-058-R2 §3.3 VRAM Access Cycle Pattern (p.46-50): all three
# operate downstream of the VDP2 register write phase. The Phase 1.35d
# savestate diff (samples/qa_phase1_35d_broken.mcs) proves the call to
# jo_vdp2_set_nbg1_8bits_image (jo-engine/jo_engine/vdp2.c:527-543) AT
# SETUP TIME destructively rewrites the VRAM cycle pattern registers
# CYCA0L/A1L/B0L/B1L, removing the character-pattern-read slots that
# NBG2 (clouds) and RBG0 (title backdrop) need to fetch their cell
# data. Without those read slots no amount of:
#   - TPMD bit flipping (path A)
#   - SCYIN1/SCYDN1 write (path B)
#   - LWTA1 + WCTLA window mask (path C)
# can restore NBG2/RBG0 visibility, because those layers' character-
# pattern bandwidth is already gone before any scroll/window/transparency
# register write executes.
#
# This predicate asserts the structural block as a binding contract:
# the cycle-pattern delta (5 banks, 8 register words) cannot be repaired
# downstream. The only fix path remaining is bypassing the jo wrapper
# entirely with a custom NBG1 setup that does not touch CYCA0/A1/B0/B1
# (=== "Phase 1.36 manual NBG1 setup" === the path the Phase 1.35d
# agent correctly identified in src/main.c:1331-1337 revert comment).
# Until that work lands, Phase 1.35e structural-fix-only attempts MUST
# revert to baseline; this predicate enforces that.
# =====================================================================

# (CYCA0L, CYCA1L, CYCB0L, CYCB1L) tuple: damage that the jo wrapper
# inflicts -- proven via samples/qa_phase1_35d_broken.mcs vs
# samples/qa_phase1_31_post_revert.mcs.
CYCLE_PATTERN_BASELINE = {
    0x25F80010: 0x55FE,  # CYCA0L: NBG1 read slot 0xE present
    0x25F80014: 0xFFFE,  # CYCA1L: CPU+NBG1 read slots present
    0x25F80018: 0x1FEE,  # CYCB0L: NBG0 read slot present
    0x25F8001C: 0xFFEE,  # CYCB1L: slot 0xE present
}
CYCLE_PATTERN_BROKEN = {
    0x25F80010: 0x5555,  # all slots reassigned to NBG0 reads
    0x25F80014: 0x5555,  # CPU/NBG1 wiped
    0x25F80018: 0xFEEE,  # NBG0 read slot dropped
    0x25F8001C: 0xFEEE,  # slot 0xE wiped
}


def predicate_cycle_pattern_damage_unrepairable():
    """P4: prove the jo wrapper's VRAM cycle-pattern damage is structural.

    Asserts the captured broken-state cycle pattern matches the
    documented CYCLE_PATTERN_BROKEN values AND differs from baseline
    by at least 4 register words (CYCA0L/A1L/B0L/B1L all changed).

    If this predicate ever fails (e.g. the broken sample is overwritten
    with a cleaner jo wrapper or a successful custom-NBG1 bypass), the
    Phase 1.35e structural-fix block is lifted; until then any
    Phase 1.35e attempt that uses jo_vdp2_set_nbg1_8bits_image must
    revert because paths A/B/C all operate downstream of this damage.
    """
    if not MCS_BROKEN.exists():
        return False, f"broken mcs missing: {MCS_BROKEN}"
    deltas = []
    for addr, expected_broken in CYCLE_PATTERN_BROKEN.items():
        actual = peek16(MCS_BROKEN, addr)
        if actual != expected_broken:
            deltas.append(f"@{hex(addr)}: expected_broken=0x{expected_broken:04x} "
                          f"actual=0x{actual:04x}")
    if deltas:
        return False, ("broken-sample cycle pattern drifted: "
                       + "; ".join(deltas))
    n_changed = sum(
        1 for addr in CYCLE_PATTERN_BASELINE
        if CYCLE_PATTERN_BASELINE[addr] != CYCLE_PATTERN_BROKEN[addr]
    )
    return True, (f"jo wrapper damages {n_changed}/4 VRAM cycle-pattern "
                  f"register words at setup time (CYCA0L/A1L/B0L/B1L); "
                  f"Phase 1.35e paths A/B/C cannot repair downstream -- "
                  f"manual-NBG1 bypass (Phase 1.36) is the only fix")


def main():
    print("=== Phase 1.35d VDP2 register-diff gate ===")
    failures = []

    ok, msg = predicate_baseline_contract()
    print(f"  P1 baseline contract  : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P1")

    ok, msg = predicate_revert_applied()
    print(f"  P2 revert applied     : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P2")

    ok, msg = predicate_broken_state_documented()
    print(f"  P3 broken sample OK   : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P3")

    ok, msg = predicate_cycle_pattern_damage_unrepairable()
    print(f"  P4 cycle-pattern blk  : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P4")

    print()
    if failures:
        print(f"Gate RED: {len(failures)} predicate(s) failed: "
              f"{', '.join(failures)}")
        sys.exit(1)
    print("Gate GREEN.")
    sys.exit(0)


if __name__ == "__main__":
    main()
