#!/usr/bin/env python3
# =============================================================================
# qa_p6_overlay.py -- P6.7d.3 gate (Task #210): PER-ZONE CODE OVERLAY
# MECHANISM. The verbatim decomp Ring moves OUT of the resident image into a
# fixed-base overlay binary (cd/OVLRING.BIN, linked at OVL_BASE against the
# main image's symbols via ld -R) chain-loaded from the ISO at boot -- the
# Phase 1.21 two-binary precedent completed. Its entry function registers the
# class via the engine's own RegisterObject, and the engine's function-table
# dispatch then runs Ring_Update/Ring_Draw FROM THE OVERLAY WINDOW every
# tick. This is the mechanism every zone pack uses at the P6.8 flip
# (SaturnMemoryMap.h: the SPZ-sized window inside P68_HWRAM_CODE_BYTES).
#
#   V1 the overlay loaded: p6_w_ovl_bytes == the byte size of cd/OVLRING.BIN
#      and p6_w_ovl_hash == djb2 over the file (computed on SH-2 over the
#      loaded window -- proves the CD load is byte-exact).
#   V2 the entry ran and registered: p6_w_ovl_classes == 3 (DefaultObject +
#      DevOutput + the overlay's Ring).
#   V3 the registered code IS overlay-resident: p6_w_ovl_updatefn (the
#      Ring_Update pointer the entry handed back) lies inside
#      [OVL_BASE, OVL_BASE + OVL_WINDOW).
#   V4 (the load-bearing proof, run separately): qa_p6_obj O1-O5 GREEN --
#      the verbatim LostFX physics/cadence model holds with every dispatched
#      instruction now executing from the chain-loaded window.
#
# ORDERING (binding, P6.5b2 GFS trap): the overlay loads BEFORE LoadDataPack
# -- a second concurrent GFS_Open fails while the pack handle is open, so the
# overlay's GFS open/close must complete before the pack mounts.
#
# Usage: python tools/_portspike/qa_p6_overlay.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
OVL_BIN = os.path.join(ROOT, "cd", "OVLRING.BIN")
OVL_BASE = 0x060C0000
OVL_WINDOW = 0x8000  # proof window; the P6.8 zone window is 124 KB

SYMS = ["_p6_w_ovl_bytes", "_p6_w_ovl_hash", "_p6_w_ovl_classes",
        "_p6_w_ovl_updatefn"]


def djb2(data):
    h = 5381
    for b in data:
        h = ((h << 5) + h) ^ b
        h &= 0xFFFFFFFF
    return h


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7d.3 OVERLAY GATE: fixed-base zone-code chain-load mechanism")
    print("=" * 72)
    print("  savestate: %s" % mcs)

    if not os.path.isfile(OVL_BIN):
        print("RESULT: RED -- overlay binary missing (%s)" % OVL_BIN)
        return 1
    blob = open(OVL_BIN, "rb").read()
    exp_bytes = len(blob)
    exp_hash = djb2(blob)
    print("  overlay: %d bytes, djb2 0x%08X" % (exp_bytes, exp_hash))
    print("-" * 72)

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
        print("        (Expected while the P6.7d.3 loader is unwritten.)")
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
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=False)
         for s in SYMS}
    print("  peeked: bytes=%d hash=0x%08X classes=%d updatefn=0x%08X"
          % (v["_p6_w_ovl_bytes"], v["_p6_w_ovl_hash"],
             v["_p6_w_ovl_classes"], v["_p6_w_ovl_updatefn"]))

    checks = [
        ("V1 overlay chain-loaded byte-exact (%d B, djb2 0x%08X)"
         % (exp_bytes, exp_hash),
         v["_p6_w_ovl_bytes"] == exp_bytes
         and v["_p6_w_ovl_hash"] == exp_hash,
         "got %d B / 0x%08X" % (v["_p6_w_ovl_bytes"], v["_p6_w_ovl_hash"])),
        ("V2 overlay entry registered (classCount == 3)",
         v["_p6_w_ovl_classes"] == 3, "got %d" % v["_p6_w_ovl_classes"]),
        ("V3 dispatched code is OVERLAY-resident: Ring_Update in "
         "[0x%08X, 0x%08X)" % (OVL_BASE, OVL_BASE + OVL_WINDOW),
         OVL_BASE <= v["_p6_w_ovl_updatefn"] < OVL_BASE + OVL_WINDOW,
         "fn=0x%08X" % v["_p6_w_ovl_updatefn"]),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the zone-overlay mechanism is live: a fixed-")
        print("        base binary chain-loads from CD, registers through the")
        print("        engine, and the engine dispatches INTO the window")
        print("        (V4 = qa_p6_obj physics GREEN on overlay-resident code).")
        return 0
    print("RESULT: RED -- overlay mechanism not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
