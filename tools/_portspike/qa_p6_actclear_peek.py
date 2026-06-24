#!/usr/bin/env python3
# One-off diagnostic peek for Task #234: did ActClear (SLOT_ACTCLEAR=16) spawn
# in the warp build, and which ActClear_State_* is its `state` fn pointer stuck
# on? Reads the WRAM-H witnesses via the qa_p6_scene calibrated peek, then maps
# the raw state pointer to the nearest preceding game.map symbol.
import importlib.util
import os
import pathlib
import re
import sys

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

SYMS = ["_p6_w_ac_classid", "_p6_w_ac_state", "_p6_w_ac_timer",
        "_p6_w_ac_frames", "_p6_w_ac_objcid", "_p6_w_sign_state",
        "_p6_w_ring_cid", "_p6_w_ac_laststate", "_p6_w_listpos_max",
        "_p6_w_mount_tag", "_p6_w_mount_listpos",
        "_p6_w_warp_plrx", "_p6_w_warp_signactive", "_p6_w_cont_frames",
        "_p6_w_transitions", "_p6_w_lay_bytes"]


def nearest_symbol(map_text, addr):
    """Return (symbol, addr) of the highest map symbol <= addr."""
    best = None
    for m in re.finditer(r"0x0*([0-9a-fA-F]{6,8})\s+([A-Za-z_]\w+)", map_text):
        a = int(m.group(1), 16)
        if a <= addr and (best is None or a > best[1]):
            best = (m.group(2), a)
    return best


def main(argv):
    mcs = _scene._as_path(argv[1])
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT
    map_text = _scene.read_text(mp)
    syms = {s: _scene.map_symbol(map_text, s) for s in [_scene.SYM_MAGIC] + SYMS}
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    print("calibration: %s" % label)
    v = {}
    for s in SYMS:
        if syms[s] is None:
            print("  MISSING %s" % s)
            continue
        v[s] = _scene.peek_u32(mod, sections, syms[s], perm, signed=True)
    print("-" * 60)
    for s in SYMS:
        if s in v:
            print("  %-24s = %d (0x%08x)" % (s, v[s], v[s] & 0xFFFFFFFF))
    print("-" * 60)
    st = v.get("_p6_w_ac_state")
    if st:
        sym = nearest_symbol(map_text, st & 0xFFFFFFFF)
        if sym:
            print("  ac_state 0x%08x -> %s (+0x%x)"
                  % (st & 0xFFFFFFFF, sym[0], (st & 0xFFFFFFFF) - sym[1]))
        else:
            print("  ac_state 0x%08x -> no preceding symbol" % (st & 0xFFFFFFFF))
    ss = v.get("_p6_w_sign_state")
    if ss:
        sym = nearest_symbol(map_text, ss & 0xFFFFFFFF)
        if sym:
            print("  sign_state 0x%08x -> %s (+0x%x)"
                  % (ss & 0xFFFFFFFF, sym[0], (ss & 0xFFFFFFFF) - sym[1]))
    # F.3: sign_state is now the REAL SignPost state (state-ptr-range matched);
    # ac_state repurposed = ever-entered-Spin flag; ac_timer = min spinCount seen.
    rs = v.get("_p6_w_sign_state")
    SIGN_LO, SIGN_HI = 0x06032780, 0x06033900
    if rs and SIGN_LO <= (rs & 0xFFFFFFFF) < SIGN_HI:
        sym = nearest_symbol(map_text, rs & 0xFFFFFFFF)
        lbl = ("%s (+0x%x)" % (sym[0], (rs & 0xFFFFFFFF) - sym[1])) if sym else "?"
        print("  REAL SignPost: state 0x%08x -> %s" % (rs & 0xFFFFFFFF, lbl))
        print("                 active=%s  everEnteredSpin=%s  minSpinCount=%s"
              % (v.get("_p6_w_warp_signactive"), v.get("_p6_w_ac_state"),
                 v.get("_p6_w_ac_timer")))
    else:
        print("  REAL SignPost: NOT matched (sign_state=0x%08x not in text range)"
              % ((rs or 0) & 0xFFFFFFFF))
    ls = v.get("_p6_w_ac_laststate")
    if ls:
        sym = nearest_symbol(map_text, ls & 0xFFFFFFFF)
        lbl = ("%s (+0x%x)" % (sym[0], (ls & 0xFFFFFFFF) - sym[1])) if sym else "?"
        print("  ActClear last live state 0x%08x -> %s" % (ls & 0xFFFFFFFF, lbl))
    print("  listPos MAX seen: %s (GHZ2 = GHZ1+1; did ++listPos fire?)"
          % v.get("_p6_w_listpos_max"))
    mt = v.get("_p6_w_mount_tag") or 0
    tagbytes = bytes([(mt >> (8 * i)) & 0xFF for i in range(4)])
    tagstr = tagbytes.split(b"\0")[0].decode("latin1", "replace")
    print("  band-mount resolved tag '%s' at listPos=%s (GHZ2 expected -> GHZ2/43347)"
          % (tagstr, v.get("_p6_w_mount_listpos")))
    frames = v.get("_p6_w_ac_frames")
    objcid = v.get("_p6_w_ac_objcid")
    print("  ActClear EVER alive (sticky frames): %s" % frames)
    print("  ActClear Object registered classID: %s%s"
          % (objcid, "  (ZERO -> ResetEntitySlot spawns nothing!)"
             if objcid == 0 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
