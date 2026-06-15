#!/usr/bin/env python3
# Task #238 step 1 (RED-first): the 4MB Extended RAM Cart probe gate.
#
# The boot probe p6_cart_probe() (p6_io_main.cpp) writes distinct sentinels to
# both 2MB banks (start + last longword) via the cache-through A-Bus alias
# (0x22400000 / 0x22600000) and reads them back, recording p6_w_cart_ok:
#   bit0 = bank0 (0x02400000..0x025FFFFF) read/write correct
#   bit1 = bank1 (0x02600000..0x027FFFFF) read/write correct
# GREEN iff p6_w_cart_ok == 3 -> a contiguous, RW-correct 4MB cart is present and
# addressable (Mednafen ss.cart=extram4 confirmed empirically; the cart's bank
# addresses are MEASURED here, not assumed -- the 1996 DTS docs predate the cart).
#
# RED on the current build: the witness symbols are absent from the map (probe not
# built) OR ss.cart is wrong / cart absent -> p6_w_cart_ok != 3.
import importlib.util
import os
import pathlib
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

SYMS = ["_p6_w_cart_ok", "_p6_w_cart_rb0", "_p6_w_cart_rb1"]
S0 = 0xC0DECAFE
S1 = 0x5A7064A7


def main(argv):
    if len(argv) < 2:
        print("usage: qa_p6_cart.py <state.mcs> [game.map]")
        return 2
    mcs = _scene._as_path(argv[1])
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT
    map_text = _scene.read_text(mp)
    syms = {s: _scene.map_symbol(map_text, s) for s in [_scene.SYM_MAGIC] + SYMS}

    missing = [s for s in SYMS if syms[s] is None]
    if missing:
        print("RED: cart-probe witness(es) absent from map (build not updated):")
        for s in missing:
            print("       MISSING %s" % s)
        return 1

    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    print("calibration: %s" % label)

    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=True) for s in SYMS}
    ok = v["_p6_w_cart_ok"] & 0xFFFFFFFF
    rb0 = v["_p6_w_cart_rb0"] & 0xFFFFFFFF
    rb1 = v["_p6_w_cart_rb1"] & 0xFFFFFFFF
    print("-" * 60)
    print("  _p6_w_cart_ok  = 0x%08x (bit0=bank0 RW, bit1=bank1 RW)" % ok)
    print("  _p6_w_cart_rb0 = 0x%08x  (expect S0=0x%08x)" % (rb0, S0))
    print("  _p6_w_cart_rb1 = 0x%08x  (expect S1=0x%08x)" % (rb1, S1))
    print("-" * 60)
    print("  bank0 0x02400000..0x025FFFFF (2MB): %s"
          % ("RW OK" if ok & 1 else "FAIL"))
    print("  bank1 0x02600000..0x027FFFFF (2MB): %s"
          % ("RW OK" if ok & 2 else "FAIL"))
    print("=" * 60)
    if ok == 3:
        print("RESULT: GREEN -- 4MB Extended RAM Cart present + RW-correct across "
              "both banks. Pool retarget into cart RAM is unblocked.")
        return 0
    print("RESULT: RED -- 4MB cart not fully addressable (cart_ok=0x%x, want 0x3)."
          % ok)
    if rb0 != S0 and rb1 != S1:
        print("        Neither bank echoed its sentinel -> cart absent or "
              "ss.cart != extram4 in mednafen.cfg.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
