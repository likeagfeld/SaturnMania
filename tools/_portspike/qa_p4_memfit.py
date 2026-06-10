#!/usr/bin/env python3
# P4 data-fit gate -- linked-image RAM fit (engine true-port, #203, step [g]).
#
# P2 closed the CODE axis on two fronts: symbol closure (qa_p2_saturn_link.sh, 0
# unresolved) + code-size (qa_p2_memfit.py, FIXED_IMAGE 226.6 KB <= 512 KB). Both
# GREEN. The axis P2 EXPLICITLY deferred -- and that its own headers name as "P4's
# retarget problem" -- is DATA: the linked image carries ~6.5 MB of stock-PC
# zero-init .bss PLUS a 74 MB runtime malloc heap (Storage.cpp:32-36). The Saturn
# has 2 MB of main RAM (WRAM-H 1 MB @0x06000000 + WRAM-L 1 MB @0x00200000). The
# stock data footprint overflows that by ~40x. P4 retargets the data so the
# linked engine actually FITS, and THIS gate is the RED-first witness for it.
#
# It is the DATA-axis sibling of qa_p2_memfit.py (the CODE-axis gate). Same
# single linked ELF (_p2_link.elf), same host-side ELF32-BE reader (imported from
# Probe 7), same dominant-array enumeration. The difference is what it gates:
#   qa_p2_memfit.py : FIXED_IMAGE (.text+.rodata+.data) <= CODE_BUDGET (512 KB)
#   qa_p4_memfit.py : FIXED_IMAGE + static .bss + heap pools <= DATA_BUDGET
#
# The measured DATA footprint has three parts, two from the ELF, one from source:
#   FIXED_IMAGE  = .text + .rodata + .data   (code+const; in the ELF)
#   STATIC_BSS   = .bss + COMMON             (zero-init arrays; in the ELF)
#   HEAP_POOLS   = the 5 DataStorage storageLimit literals (Storage.cpp:32-36),
#                  parsed from source because malloc heap is NOT in the ELF. The
#                  parser is RETRO_SATURN-branch aware, so once the retarget wraps
#                  the limits in `#if RETRO_SATURN`, this gate reads the Saturn
#                  sizes, not the stock 74 MB.
#
# BUDGET (reframed at Lever 5, Task #203 -- user directive "Gated surgery"):
# the PASS/FAIL line is the PHYSICAL 2 MB Saturn ceiling (WRAM-H 1 MB @0x06000000
# + WRAM-L 1 MB @0x00200000), NOT the 1.5 MB DATA_BUDGET. Two reasons:
#   (1) P3's linker splits .bss across BOTH banks, so the engine's data has the
#       full 2 MB to live in -- the 1.5 MB figure assumed a single-bank top-of-
#       WRAM-H layout that the two-bank script supersedes.
#   (2) This true-port image is the platform-INDEPENDENT logic core ONLY. The
#       renderer (SGL/VDP) lives in the backend and is measured separately, so
#       the 256 KB SGL_RESERVE does not apply to THIS image's footprint.
# DATA_BUDGET = SATURN_MAIN_RAM - SGL_RESERVE(256 KB) - SYS_RESERVE(256 KB) =
# 1.5 MB is STILL computed and printed as a CONSERVATIVE informational watermark
# (the SGL+system headroom P5+ integration must respect once the backend is
# linked in), but it no longer gates. The gate asserts
#   FIXED_IMAGE + STATIC_BSS + HEAP_POOLS <= PHYSICAL (2 MB) with a comfortable
#   margin (>= 64 KB, mirroring qa_p4_screen_layer_file_gate.py L7).
# P6 RESTORATION: when the data-retarget levers are reverted to stock, restore
# DATA_BUDGET as the gating line (data_ok) -- exactly like SCENEENTITY_COUNT.
#
# REPORT (not gated): every static array >= REPORT_SYM_THRESHOLD is named from the
# linked symbol table -- these ARE the retarget levers, measured array-by-array.
# Each re-link reprints them so the effect of each retarget step is visible.
#
#   python tools/_portspike/qa_p4_memfit.py
#   python tools/_portspike/qa_p4_memfit.py --selftest   # prove RED fires post-GREEN
#
# Exit 0 iff FIXED_IMAGE + STATIC_BSS + HEAP_POOLS <= PHYSICAL (2 MB) with a
# >= 64 KB margin. DATA_BUDGET (1.5 MB) is reported but no longer gates. Else exit 1.

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

# Reuse Probe 7's ELF reader + helpers verbatim (its module body runs no main(),
# so importing it is side-effect-free beyond a couple of path constants).
from qa_core_memfit_gate import (  # noqa: E402
    parse_elf32_be,
    bucket,
    kb,
    mb,
    REPORT_SYM_THRESHOLD,
    SHN_COMMON,
    WRAM_H,
    WRAM_L,
    SATURN_MAIN_RAM,
    ElfError,
)

LINKELF = os.path.join(HERE, "_p2_link.elf")
STORAGE_CPP = os.path.normpath(
    os.path.join(HERE, "..", "..", "rsdkv5-src", "RSDKv5", "RSDK", "Storage", "Storage.cpp")
)

# --- Saturn DATA budget ----------------------------------------------------
# Reserve the SGL work area (top of WRAM-H, measured 0x060C0000 floor) + a
# starting allowance for the jo runtime / Saturn backend / stack / malloc
# bookkeeping. Tunable as P5 integration measures the real platform cost.
SGL_RESERVE = 256 * 1024
SYS_RESERVE = 256 * 1024
DATA_BUDGET = SATURN_MAIN_RAM - SGL_RESERVE - SYS_RESERVE  # 1.5 MB

# The 5 DataStorage pools, by enum name, in Storage.cpp declaration order.
DATASET_NAMES = ["DATASET_STG", "DATASET_MUS", "DATASET_SFX", "DATASET_STR", "DATASET_TMP"]

ET_REL = 1
ET_EXEC = 2
ET_DYN = 3
_ET_NAME = {ET_REL: "ET_REL", ET_EXEC: "ET_EXEC", ET_DYN: "ET_DYN"}


def elf_etype(path):
    """Read e_type (offset 0x10, big-endian u16) for the evidence line."""
    with open(path, "rb") as f:
        head = f.read(0x14)
    return struct.unpack_from(">H", head, 0x10)[0]


def _eval_size_expr(expr):
    """Safely evaluate a C integer size expression (e.g. '24 * 1024 * 1024',
    '256 * 1024', '0x40000'). Whitelist chars, then eval with no builtins."""
    expr = expr.strip()
    if not re.fullmatch(r"[0-9xXa-fA-F*+\-<>() \t]+", expr):
        raise ValueError("unparseable size expr: %r" % expr)
    return int(eval(expr, {"__builtins__": {}}, {}))  # noqa: S307 (host dev gate)


def parse_storage_pools(path):
    """Parse the 5 DataStorage storageLimit values from Storage.cpp.

    RETRO_SATURN-branch aware: tracks a stack of (kind, active) so that once the
    retarget wraps the limits in `#if RETRO_PLATFORM == RETRO_SATURN ... #else ...
    #endif` we read the Saturn branch, not the stock #else. Any other `#if*` is
    treated as transparent (active follows its parent) -- the gate only needs to
    disambiguate the RETRO_SATURN split it controls.

    Returns dict {DATASET_NAME: bytes}. A line is honoured only when every guard
    on the stack is active-for-Saturn; the LAST active assignment per set wins.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    assign_re = re.compile(
        r"dataStorage\[\s*(DATASET_[A-Z]+)\s*\]\.storageLimit\s*=\s*(.+?)\s*;"
    )
    # Stack entries: dict(kind=<'saturn'|'not_saturn'|'other'>, active=bool).
    stack = []

    def active_now():
        return all(e["active"] for e in stack)

    pools = {}
    for raw in lines:
        line = raw.strip()

        m_if = re.match(r"#\s*if(n?def)?\s+(.*)$", line)
        m_elif = re.match(r"#\s*elif\s+(.*)$", line)
        m_else = re.match(r"#\s*else\b", line)
        m_endif = re.match(r"#\s*endif\b", line)

        if m_if:
            ifkind = m_if.group(1)  # None, 'def', or 'ndef'
            cond = m_if.group(2).strip()
            parent = active_now()
            # The codebase gates Saturn with `#if RETRO_PLATFORM == RETRO_SATURN`
            # (RetroEngine.hpp:430), NOT a boolean `#if RETRO_SATURN` -- RETRO_SATURN
            # is the platform CONSTANT (9), so `#if RETRO_SATURN` would be `#if 9`
            # (always true) and would corrupt the PC build. Recognize the real
            # convention plus the bare/def/ndef forms for robustness.
            cond_norm = re.sub(r"\s+", " ", cond)
            is_saturn_eq = cond_norm in (
                "RETRO_PLATFORM == RETRO_SATURN",
                "RETRO_SATURN == RETRO_PLATFORM",
            )
            is_not_saturn_eq = cond_norm in (
                "RETRO_PLATFORM != RETRO_SATURN",
                "RETRO_SATURN != RETRO_PLATFORM",
            )
            if (cond == "RETRO_SATURN" and ifkind in (None, "def")) or is_saturn_eq:
                stack.append({"kind": "saturn", "active": parent and True})
            elif (cond == "RETRO_SATURN" and ifkind == "ndef") or is_not_saturn_eq \
                    or cond in ("!RETRO_SATURN", "! RETRO_SATURN"):
                stack.append({"kind": "not_saturn", "active": parent and False})
            else:
                # Unrelated guard -> transparent: follow parent active-ness.
                stack.append({"kind": "other", "active": parent})
            continue
        if m_elif:
            if stack:
                # Treat #elif as the start of a non-Saturn alternative.
                top = stack[-1]
                top["active"] = False if top["kind"] == "saturn" else top["active"]
            continue
        if m_else:
            if stack:
                top = stack[-1]
                if top["kind"] == "saturn":
                    top["active"] = False  # #else of `#if RETRO_SATURN`
                elif top["kind"] == "not_saturn":
                    # #else of `#ifndef RETRO_SATURN` / `#if !RETRO_SATURN`
                    parent = all(e["active"] for e in stack[:-1]) if len(stack) > 1 else True
                    top["active"] = parent
                # 'other' #else stays transparent (no flip needed for our use).
            continue
        if m_endif:
            if stack:
                stack.pop()
            continue

        m = assign_re.search(line)
        if m and active_now():
            try:
                pools[m.group(1)] = _eval_size_expr(m.group(2))
            except ValueError:
                pass  # leave unset; reported as missing below

    return pools


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
    print("P4 gate: linked-image DATA-fit (qa_p4_memfit on _p2_link.elf)")
    print("==============================================================")

    etype = elf_etype(LINKELF)
    disk = os.path.getsize(LINKELF)
    print("[1/4] Parsing linked ELF32-BE (EM_SH) _p2_link.elf ...")
    try:
        sections, symbols = parse_elf32_be(LINKELF)
    except ElfError as e:
        print("RESULT: RED -- %s" % e)
        return 1
    print("        parsed OK: %s, %d sections, %d bytes on disk"
          % (_ET_NAME.get(etype, "e_type=%d" % etype), len(sections), disk))

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
    static_bss = totals["bss"]

    # [2/4] Parse the runtime malloc heap (not in the ELF).
    print("[2/4] Parsing the 5 DataStorage pools (Storage.cpp:32-36) ...")
    pools = parse_storage_pools(STORAGE_CPP)
    heap_total = 0
    pool_lines = []
    for nm in DATASET_NAMES:
        sz = pools.get(nm)
        if sz is None:
            pool_lines.append("    %-12s : (NOT FOUND in source)" % nm)
        else:
            heap_total += sz
            pool_lines.append("    %-12s : %12d  (%s)" % (nm, sz, mb(sz)))
    for pl in pool_lines:
        print(pl)
    print("    %-12s : %12d  (%s)" % ("HEAP TOTAL", heap_total, mb(heap_total)))

    if selftest:
        # Inject DATA_BUDGET worth of synthetic .bss to PROVE RED fires even on a
        # GREEN (retargeted) build -- the regression catch once this goes GREEN.
        print("        [SELFTEST] injected +%s synthetic .bss." % mb(DATA_BUDGET))
        static_bss += DATA_BUDGET

    total = fixed_image + static_bss + heap_total

    print("[3/4] Measured RAM footprint of the LINKED P2 image + heap:")
    print("--------------------------------------------------------------")
    print("  FIXED_IMAGE (.text+.rodata+.data) : %12d  (%s)" % (fixed_image, kb(fixed_image)))
    print("  STATIC_BSS  (.bss + COMMON)       : %12d  (%s)" % (static_bss, mb(static_bss)))
    print("  HEAP_POOLS  (5 DataStorage pools) : %12d  (%s)" % (heap_total, mb(heap_total)))
    print("  ----------------------------------------------")
    print("  TOTAL RAM (image + heap)          : %12d  (%s)" % (total, mb(total)))
    print("--------------------------------------------------------------")

    # Name the dominant zero-init arrays -- the retarget levers, measured.
    big_syms.sort(reverse=True)
    print("Dominant static .bss arrays (>= %s) -- the P4 retarget levers:"
          % kb(REPORT_SYM_THRESHOLD))
    if not big_syms:
        print("    (none -- symbol table stripped or no array >= threshold)")
    seen = set()
    shown = 0
    for size, name, secname in big_syms:
        if name in seen:
            continue
        seen.add(name)
        print("    %12d  (%9s)  %-30s [%s]"
              % (size, kb(size), name or "(anon)", secname or "?"))
        shown += 1
        if shown >= 20:
            print("    ... (%d more >= threshold elided)" % (len(seen) - shown))
            break
    print("--------------------------------------------------------------")

    # Budget verdict. P4 (Lever 5 reframe) gates on the PHYSICAL 2 MB line with a
    # comfortable margin; DATA_BUDGET (1.5 MB) is printed but informational only.
    MARGIN_MIN = 64 * 1024
    margin = SATURN_MAIN_RAM - total
    phys_ok = total <= SATURN_MAIN_RAM and margin >= MARGIN_MIN
    data_ok = total <= DATA_BUDGET  # informational watermark -- does NOT gate
    print("[4/4] Saturn budget (DTS96 map: WRAM-H 1 MB + WRAM-L 1 MB = 2 MB):")
    print("--------------------------------------------------------------")
    print("  GATE: PHYSICAL = %s, require >= %s margin (Lever 5 reframe)"
          % (mb(SATURN_MAIN_RAM), kb(MARGIN_MIN)))
    print("    TOTAL RAM        %12s  vs  PHYSICAL   %s   -> %s  (margin %s)"
          % (mb(total), mb(SATURN_MAIN_RAM), "PASS" if phys_ok else "FAIL", kb(margin)))
    print("  INFORMATIONAL (not gated): DATA_BUDGET = 2 MB - SGL_RESERVE(%s) - SYS_RESERVE(%s) = %s"
          % (kb(SGL_RESERVE), kb(SYS_RESERVE), mb(DATA_BUDGET)))
    print("    TOTAL RAM        %12s  vs  DATA_BUDGET %s   -> %s"
          % (mb(total), mb(DATA_BUDGET), "PASS" if data_ok else "FAIL"))
    if total > SATURN_MAIN_RAM:
        print("    => overflows PHYSICAL 2 MB by %s" % mb(total - SATURN_MAIN_RAM))
    elif not phys_ok:
        print("    => fits 2 MB but margin %s < required %s" % (kb(margin), kb(MARGIN_MIN)))
    elif not data_ok:
        print("    => fits 2 MB physical; consumes %s of the informational SGL/system reserve"
              % mb(total - DATA_BUDGET))
    print("--------------------------------------------------------------")

    if phys_ok:
        print("RESULT: GREEN -- the LINKED true-port image's DATA (static .bss +")
        print("        the 5 retargeted heap pools) fits the PHYSICAL 2 MB Saturn")
        print("        main RAM (%s of %s, %s margin)."
              % (mb(total), mb(SATURN_MAIN_RAM), kb(margin)))
        print("        P4's data-retarget axis is GREEN. P3's two-bank linker")
        print("        places this .bss across WRAM-H + WRAM-L. DATA_BUDGET (%s,"
              % mb(DATA_BUDGET))
        print("        informational) is %s -- the SGL/system headroom P5+"
              % ("intact" if data_ok else "partly consumed"))
        print("        integration must respect once the backend renderer links in.")
        return 0
    else:
        print("RESULT: RED -- linked DATA footprint %s breaches the PHYSICAL 2 MB"
              % mb(total))
        print("        Saturn line (need <= %s with >= %s margin)."
              % (mb(SATURN_MAIN_RAM), kb(MARGIN_MIN)))
        print("        Retarget the levers named above (frameBuffer stub, DATAFILE_COUNT")
        print("        + LAYER_COUNT caps, data[0x40], collisionMasks/tileInfo x1) under")
        print("        `#if RETRO_PLATFORM == RETRO_SATURN`, re-link, re-run gate.")
        return 1


if __name__ == "__main__":
    sys.exit(main())
