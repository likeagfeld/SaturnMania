#!/usr/bin/env python3
# =============================================================================
# qa_p6_sheet.py -- P6.7 W12 gate (Task #227): sprite-sheet band stores
# STAGED into VDP2 VRAM + the rect fetch byte-exact on SH-2.
#
# WHY: the Player wave's sheets cannot be RAM-resident (W12 declaration,
# SaturnMemoryMap.h -- 786,432 B decoded vs the 64 KB STG pool). The VDP1
# slot-cache miss path fetches frame rects from VDP2-resident band stores
# (platform/Saturn/SaturnSheet.cpp). This gate proves that machinery with
# the W11a evidence shape: staging counts, then byte-exact probe rects
# replayed through the REAL SaturnSheet_FetchRect against the offline
# model (tools/build_sheet_bands.py, self-tests S1-S3).
#
# CHECKS
#   S1 witnesses present in game.map (RED while W12 is unwired).
#   S2 all 3 Player sheets staged into VDP2 (p6_w_sht_staged == 3).
#   S3 probe rects byte-exact: p6_w_sht_probes == model count (15) --
#      every rect spans band boundaries by construction; first mismatch
#      index reported via p6_w_sht_firstbad.
#   S4 fetch machinery exercised: p6_w_sht_fetches > 0 (band inflates ran
#      on SH-2, WRAM-only -- the 16-bit VDP2 access contract held or the
#      hashes would diverge).
#
# Usage: python tools/_portspike/qa_p6_sheet.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
MODEL = os.path.join(HERE, "_p6", "p6_sheet_model.json")

SYMS = ["_p6_w_sht_staged", "_p6_w_sht_fetches", "_p6_w_sht_probes",
        "_p6_w_sht_firstbad"]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7 W12 SHEET GATE: VDP2 band stores + byte-exact rect fetch")
    print("=" * 72)

    model = json.load(open(MODEL))
    nsheets = len(model["sheets"])
    nprobes = sum(len(s["probes"]) for s in model["sheets"])
    total = sum(s["bytes"] for s in model["sheets"])
    print("  model: %d sheets, %d B banded, %d probe rects"
          % (nsheets, total, nprobes))

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the map:")
        for s in missing:
            print("        %s" % s)
        print("        (Expected while W12 is unwired.)")
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=True)
         for s in SYMS}

    # Task #241: the staged count is FLAVOR-DEPENDENT. The cart shipping build
    # relocates the band store to the 4MB cart (384 KB) and stages all `nsheets`
    # (7); the non-cart fallback keeps the 245,760 B VDP2 window which fits only
    # the first 6 (the 7th, TAILS1, overflows). Both are correct -- assert that
    # EVERY staged sheet probes byte-exact (probes == staged * per-sheet-probes),
    # not a hardcoded total, and that the staged count is at least the 6 that fit
    # VDP2. The first probe failure (if any) must be exactly the first UNstaged
    # slot, never a staged one.
    per_sheet = nprobes // nsheets if nsheets else 5
    staged = v["_p6_w_sht_staged"]
    probes_expected = staged * per_sheet
    s3_clean = (v["_p6_w_sht_probes"] == probes_expected
                and (v["_p6_w_sht_firstbad"] < 0
                     or v["_p6_w_sht_firstbad"] == probes_expected))
    checks = [
        ("S2 staged set resident (>=6 VDP2-fit; %d when cart-relocated)" % nsheets,
         staged >= 6 and staged <= nsheets, "staged=%d (model %d)" % (staged, nsheets)),
        ("S3 every staged sheet probes byte-exact (%d = staged*%d) through "
         "SaturnSheet_FetchRect" % (probes_expected, per_sheet), s3_clean,
         "matched=%d expected=%d firstbad=%d"
         % (v["_p6_w_sht_probes"], probes_expected, v["_p6_w_sht_firstbad"])),
        ("S4 band inflates ran on SH-2 (WRAM-only path)",
         v["_p6_w_sht_fetches"] > 0, "fetches=%d" % v["_p6_w_sht_fetches"]),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the W12 sheet band stores are live: staged in")
        print("        VDP2, fetched byte-exact through the slot-cache miss")
        print("        machinery. LoadSpriteSheet can now go non-resident.")
        return 0
    print("RESULT: RED -- W12 sheet machinery not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
