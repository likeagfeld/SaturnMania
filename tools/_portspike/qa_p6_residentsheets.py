#!/usr/bin/env python3
# =============================================================================
# qa_p6_residentsheets.py -- Task #243 Lever 1 don't-regress gate.
#
# WHAT IT PROVES
#   The SaturnSheet band store is made RESIDENT (fully inflated once into the
#   free 4MB-cart region 0x22400000..0x227A0000) at stage time, so the per-frame
#   SaturnSheet_FetchRect serves sprite rects with a direct memcpy and NEVER runs
#   the per-frame miniz inflate. That inflate was the dominant GHZ render cost.
#
# MEASURED A/B (in-motion, 12 in-range entities, same spot, 2026-06-15):
#   RED  (P6_SHT_NO_RESIDENT): sht_resident absent, sht_fetches=3941 (~3.26/frm),
#                              ProcessObjectDrawLists 83.3 ms (5 vbl), fps 9.72.
#   GREEN(resident, ship):     sht_resident=7,      sht_fetches=0,
#                              ProcessObjectDrawLists  6.2 ms (1 vbl), fps 29.81.
#   -> 3.07x fps (eliminating ~77 ms/frame of band-inflate).
#
# THE INVARIANT (don't-regress)
#   sht_fetches == 0   : no per-frame band inflate happens for ANY sheet (the
#                        whole point -- one stray non-resident sheet would inflate
#                        every frame and this would fire).
#   sht_resident >= 7  : the 7 staged Player/Display/Shields/HUD/Tails sheets all
#                        fit the resident cart region (mechanism fully engaged).
#
# RED-first: this gate is RED on the P6_SHT_NO_RESIDENT build (the old inflate
# path) and GREEN on the shipping resident build -- the binding QA pattern.
#
# Usage:
#   python qa_p6_residentsheets.py [state.mcs] [game.map]
# Defaults mirror qa_p6_perf.py (HERE/p6_s0.mcs + ../../game.map) so the
# verify_done fresh capture feeds both gates.
# =============================================================================
import os
import sys
import pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as _scene  # noqa: E402

MCS_DEFAULT = os.path.join(HERE, "p6_s0.mcs")

# The staged sheet set (p6_io_main.cpp shtFiles[7]).
EXPECTED_RESIDENT = 7

SYMS = ["_p6_w_sht_resident", "_p6_w_sht_fetches", "_p6_w_cont_frames"]


def main(argv):
    # --static: map-only linkage check (no savestate). This is the half
    # verify_done.ps1 runs on every build -- it asserts the resident-sheet
    # path is LINKED into the image (SaturnSheet_MakeResident driven, witnesses
    # present). The full runtime half (sht_fetches==0 over a live in-motion
    # capture) is the manual A/B, mirroring the other p6 gates' --with-savestate
    # split (the in-motion continuous-GHZ capture is ~3 min, not per-build).
    args = [a for a in argv[1:] if not a.startswith("--")]
    static = "--static" in argv[1:]
    mcs = _scene._as_path(args[0]) if len(args) > 0 else MCS_DEFAULT
    mp = _scene._as_path(args[1]) if len(args) > 1 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("RESIDENT SHEETS (Task #243 Lever 1): no per-frame band inflate%s"
          % ("  [STATIC linkage]" if static else ""))
    print("=" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    # Static linkage also wants the MakeResident driver symbol present (proves
    # the call site survived gc -- if someone removes it, the symbol + its
    # sht_resident witness both vanish and this fires).
    extra = ["_SaturnSheet_MakeResident"] if static else []
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS + extra:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if static:
        link_missing = [s for s in (SYMS + extra) if s in missing
                        and s != "_p6_w_cont_frames"]
        if link_missing:
            print("  [ RED ] resident-sheet path linked into the image")
            for s in link_missing:
                print("          MISSING %s" % s)
            print("-" * 72)
            print("RESULT: RED -- resident-sheet path not linked (regression).")
            return 1
        print("  [GREEN] resident-sheet path linked "
              "(MakeResident + witnesses present)")
        print("          (runtime sht_fetches==0 proof: "
              "qa_p6_residentsheets.py <in-motion.mcs>)")
        print("-" * 72)
        print("RESULT: GREEN -- resident sheet path linked into the ship image.")
        return 0
    if "_p6_w_sht_resident" in missing or "_p6_w_sht_fetches" in missing:
        # RED-first signature: the no-resident build links SaturnSheet without
        # the resident path being driven (sht_resident never incremented) and
        # the inflate counter is live -- exactly the build this gate must catch.
        print("  [ RED ] M1 resident-sheet witnesses present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("-" * 72)
        print("RESULT: RED -- resident-sheet path not present (M1).")
        return 1
    print("  [GREEN] M1 resident-sheet witnesses present in the link map")

    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1

    resident = _scene.peek_u32(mod, sections, syms["_p6_w_sht_resident"], perm, signed=True)
    fetches = _scene.peek_u32(mod, sections, syms["_p6_w_sht_fetches"], perm, signed=True)
    frames = _scene.peek_u32(mod, sections, syms["_p6_w_cont_frames"], perm, signed=True)

    print("          calib=%s" % label)
    print("          cont_frames=%d  sht_resident=%d  sht_fetches=%d"
          % (frames, resident, fetches))
    print("-" * 72)

    checks = []
    # M2: the capture actually reached continuous GHZ (else the witnesses are
    # the pre-load 0 and the gate would false-GREEN on sht_fetches==0).
    checks.append(("M2 capture in continuous GHZ (cont_frames>0)",
                   frames > 0, "cont_frames=%d" % frames))
    # M3: the mechanism -- every staged sheet resident, zero per-frame inflate.
    checks.append(("M3 all staged sheets resident (>= %d)" % EXPECTED_RESIDENT,
                   resident >= EXPECTED_RESIDENT, "sht_resident=%d" % resident))
    checks.append(("M4 ZERO per-frame band inflate (sht_fetches==0)",
                   fetches == 0, "sht_fetches=%d" % fetches))

    ok = True
    for name, passed, detail in checks:
        print("  [%s] %s -- %s" % ("GREEN" if passed else " RED ", name, detail))
        ok = ok and passed

    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- resident sheets serve all rects; no inflate "
              "(the ~77 ms/frame GHZ render cost stays eliminated).")
        return 0
    print("RESULT: RED -- per-frame band inflate is back (render-perf regression).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
