#!/usr/bin/env python3
# P2 code-size gate -- linked-image CODE fit (engine true-port, #201, step [f]).
#
# The P2 link gate (qa_p2_saturn_link.sh) proved SYMBOL CLOSURE: the 21 true-port
# relocatables + 4 P2 support objects (miniz + libm_saturn + CppRuntime_Saturn +
# main stub) LINK against the real Saturn toolchain runtime (newlib libc +
# libnosys + libgcc) with ZERO unresolved symbols, emitting _p2_link.elf. That
# closes the "does it link" axis. This gate closes the SECOND P2 axis the pivot
# plan named: does the LINKED engine CODE image fit the Saturn code budget?
#
# It is the single-linked-ELF sibling of Probe 7 (qa_core_memfit_gate.py, which
# measured the 16 unlinked _coreobj/*.o). It reuses Probe 7's host-side ELF32-BE
# reader verbatim (the jo LINUX toolchain ships no size/nm/objdump, so we parse
# the section + symbol tables ourselves) and applies the SAME CODE_BUDGET. The
# difference: this runs AFTER the real link, so the measured .text now folds in
# the libc/libgcc soft-float + 64-bit-arith helpers that actually get pulled --
# i.e. it is the true post-link code footprint, not a pre-link estimate.
#
# GATE (RED-capable): FIXED_IMAGE (.text + .rodata + .data) must fit CODE_BUDGET
# (512 KB, the same half-of-one-WRAM-bank budget Probe 7 used). This is the
# genuine P2 code-feasibility axis.
#
# REPORT (NOT gated): the linked .bss (~6.5 MB of stock-PC zero-init data) is
# enumerated as the bounded P4 retarget target, named array-by-array straight
# from the linked symbol table. P2 is symbol-closure + code-fit ONLY; placing
# that .bss into the Saturn's 2 MB (flip x4->x1, Saturn-size the 5 malloc pools +
# ENTITY_COUNT + data[0x100]) is P4's problem, exactly as the link gate's header
# states ("the stock 5.88 MB .bss would overflow the Saturn RAM regions ...
# Section placement + the 2 MB data fit is P4's retarget problem").
#
#   python tools/_portspike/qa_p2_memfit.py
#   python tools/_portspike/qa_p2_memfit.py --selftest   # prove RED fires
#
# Exit 0 iff FIXED_IMAGE <= CODE_BUDGET. Else exit 1.

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

# Reuse Probe 7's reader + budget verbatim (its module body runs no main(), so
# importing it is side-effect-free beyond a couple of path constants).
from qa_core_memfit_gate import (  # noqa: E402
    parse_elf32_be,
    bucket,
    kb,
    mb,
    CODE_BUDGET,
    REPORT_SYM_THRESHOLD,
    SHN_COMMON,
    WRAM_H,
    SATURN_MAIN_RAM,
    ElfError,
)

LINKELF = os.path.join(HERE, "_p2_link.elf")

# ELF e_type (offset 0x10, big-endian u16) -- for the evidence line only.
ET_REL = 1
ET_EXEC = 2
ET_DYN = 3
_ET_NAME = {ET_REL: "ET_REL", ET_EXEC: "ET_EXEC", ET_DYN: "ET_DYN"}


def elf_etype(path):
    """Read e_type so the report can state 'linked executable' vs 'relocatable'."""
    import struct
    with open(path, "rb") as f:
        head = f.read(0x14)
    return struct.unpack_from(">H", head, 0x10)[0]


def main():
    selftest = "--selftest" in sys.argv[1:]

    if not os.path.isfile(LINKELF):
        print("RESULT: RED -- %s missing." % LINKELF)
        print("        Run the P2 link gate first (emits _p2_link.elf):")
        print("        MSYS_NO_PATHCONV=1 docker run --rm -v \"D:/sonicmaniasaturn\":/work \\")
        print("          -w /work joengine-saturn:latest \\")
        print("          bash /work/tools/_portspike/qa_p2_saturn_link.sh")
        return 1

    print("==============================================================")
    print("P2 gate: linked-image CODE-size fit (qa_p2_memfit on _p2_link.elf)")
    print("==============================================================")

    etype = elf_etype(LINKELF)
    disk = os.path.getsize(LINKELF)
    print("[1/3] Parsing linked ELF32-BE (EM_SH) _p2_link.elf ...")
    try:
        sections, symbols = parse_elf32_be(LINKELF)
    except ElfError as e:
        print("RESULT: RED -- %s" % e)
        return 1
    print("        parsed OK: %s, %d sections, %d bytes on disk"
          % (_ET_NAME.get(etype, "e_type=%d" % etype), len(sections), disk))
    print("        (on-disk size includes .debug_* DWARF; the gate measures")
    print("         loadable section sizes, NOT the file size).")

    totals = {"text": 0, "rodata": 0, "data": 0, "bss": 0}
    for sec in sections:
        b = bucket(sec["name"])
        if b is not None:
            totals[b] += sec["size"]

    common_total = 0
    big_syms = []  # (size, name, secname)
    for sym in symbols:
        if sym["shndx"] == SHN_COMMON:
            common_total += sym["size"]
        if sym["size"] >= REPORT_SYM_THRESHOLD:
            big_syms.append((sym["size"], sym["name"], sym["secname"]))
    totals["bss"] += common_total  # 0 after a real link, but be exact.

    fixed_image = totals["text"] + totals["rodata"] + totals["data"]
    zero_bss = totals["bss"]

    if selftest:
        # Inject a synthetic +1 MB of code to PROVE the RED path fires (mirrors
        # Probe 7's selftest: a real over-budget link must report FAIL, not crash).
        print("        [SELFTEST] injected +1.00 MB synthetic .text.")
        fixed_image += 1024 * 1024
        totals["text"] += 1024 * 1024

    print("[2/3] Measured footprint of the LINKED P2 image:")
    print("--------------------------------------------------------------")
    print("  .text   (SH-2 code + libc/libgcc) : %10d  (%s)" % (totals["text"], kb(totals["text"])))
    print("  .rodata (const tables)            : %10d  (%s)" % (totals["rodata"], kb(totals["rodata"])))
    print("  .data   (init globals)            : %10d  (%s)" % (totals["data"], kb(totals["data"])))
    print("  ----------------------------------------------")
    print("  FIXED_IMAGE (.text+.rodata+.data) : %10d  (%s)" % (fixed_image, kb(fixed_image)))
    print("  .bss + COMMON (zero-init)         : %10d  (%s)" % (zero_bss, mb(zero_bss)))
    print("--------------------------------------------------------------")

    # Name the dominant zero-init arrays straight from the linked symbol table --
    # these ARE the P4 retarget #defines, measured from the binary not guessed.
    big_syms.sort(reverse=True)
    print("Dominant static allocations (>= %s) -- the P4 retarget targets:" % kb(REPORT_SYM_THRESHOLD))
    if not big_syms:
        print("    (none -- symbol table stripped or no array >= threshold)")
    seen = set()
    shown = 0
    for size, name, secname in big_syms:
        if name in seen:
            continue
        seen.add(name)
        print("    %10d  (%9s)  %-30s [%s]"
              % (size, kb(size), name or "(anon)", secname or "?"))
        shown += 1
        if shown >= 20:
            print("    ... (%d more >= threshold elided)" % (len(seen) - shown))
            break
    print("--------------------------------------------------------------")

    # Budget verdict.
    text_ok = totals["text"] <= CODE_BUDGET
    code_ok = fixed_image <= CODE_BUDGET
    print("[3/3] Saturn budget (DTS96 map: WRAM-H 1 MB + WRAM-L 1 MB = 2 MB):")
    print("--------------------------------------------------------------")
    print("  FEASIBILITY AXIS (gated): linked core+support CODE image")
    print("    .text            %9s  vs  CODE_BUDGET %s   -> %s"
          % (kb(totals["text"]), kb(CODE_BUDGET), "PASS" if text_ok else "FAIL"))
    print("    FIXED_IMAGE      %9s  vs  CODE_BUDGET %s   -> %s"
          % (kb(fixed_image), kb(CODE_BUDGET), "PASS" if code_ok else "FAIL"))
    print("    (linked code occupies %.1f%% of one 1 MB WRAM bank)"
          % (100.0 * fixed_image / WRAM_H))
    print()
    print("  P4 RETARGET AXIS (reported, tunable -- NOT a P2 blocker):")
    print("    linked static .bss  %s  vs  %s main RAM" % (mb(zero_bss), mb(SATURN_MAIN_RAM)))
    if zero_bss > SATURN_MAIN_RAM:
        print("    => overflows 2 MB by %s; this is the MEASURED P4 data-fit"
              % mb(zero_bss - SATURN_MAIN_RAM))
    else:
        print("    => fits 2 MB as-is.")
    print("       problem (flip x4->x1, Saturn-size the 5 malloc pools +")
    print("       ENTITY_COUNT + data[0x100]). P2 links FLAT for symbol")
    print("       closure + code-fit ONLY; placing this .bss is P4's retarget.")
    print("--------------------------------------------------------------")

    if code_ok:
        print("RESULT: GREEN -- the LINKED true-port image's CODE fits the Saturn")
        print("        code budget (%s of %s). Even after pulling the real"
              % (kb(fixed_image), kb(CODE_BUDGET)))
        print("        libc + libgcc soft-float/64-bit helpers into the link, the")
        print("        engine code is a non-issue. P2 is GREEN on BOTH axes:")
        print("        symbol closure (0 unresolved, qa_p2_saturn_link.sh) AND")
        print("        code-fit (this gate). The remaining %s .bss is the" % mb(zero_bss))
        print("        bounded P4 data-retarget task, named array-by-array above.")
        return 0
    else:
        print("RESULT: RED -- linked CODE image %s exceeds budget %s."
              % (kb(fixed_image), kb(CODE_BUDGET)))
        return 1


if __name__ == "__main__":
    sys.exit(main())
