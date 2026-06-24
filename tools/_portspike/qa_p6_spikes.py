#!/usr/bin/env python3
# =============================================================================
# qa_p6_spikes.py -- RED-first gate for the GHZ content-entity port (#247).
#
# Spikes is the FIRST GHZ Act 1 content entity ported into the engine pack (the
# build was a structural skeleton -- Player/Ring/SignPost/HUD only, no level
# hazards/badniks/interactables). This gate asserts the Spikes object REGISTERED
# on the live GHZ engine build -- i.e. LoadGameConfig matched "Spikes" by name
# hash and assigned it a classID, which p6_wave1_reg.c exposes via
# p6_spikes_classid() (read into the WRAM-H witness p6_w_spikes_cid at tick time).
#
#   RED on the pre-port build: _p6_w_spikes_cid absent from the link map (the
#       witness TU was never built) OR cid <= 0 (Spikes never registered).
#   GREEN once Game_Spikes.o is compiled into the pack + RSDK_REGISTER_OBJECT(
#       Spikes) runs + GLOBJ.SHT (Global/Objects.gif) is staged.
#
# Usage: python tools/_portspike/qa_p6_spikes.py [savestate.mcs] [game.map]
# =============================================================================
import importlib.util
import os
import pathlib
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else os.path.join(HERE, "p6_s0.mcs")
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 64)
    print("GHZ CONTENT PORT (#247): Spikes registered on the engine build")
    print("=" * 64)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    mt = _scene.read_text(mp)
    sym = _scene.map_symbol(mt, "_p6_w_spikes_cid")
    if sym is None:
        print("  [ RED ] S0 _p6_w_spikes_cid absent from the link map")
        print("          (Expected on the pre-port build -- Spikes not in the pack.)")
        print("-" * 64)
        print("RESULT: RED -- Spikes witness not built (pre-port).")
        return 1
    print("  [GREEN] S0 _p6_w_spikes_cid present in the link map")

    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1
    mod = _scene.load_harness()
    sec = mod.parse_savestate(pathlib.Path(mcs))
    raw = mod._peek_bytes(sec, _scene.map_symbol(mt, _scene.SYM_MAGIC), 4)
    lab, perm = _scene.calibrate(raw)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1
    cid = _scene.peek_u32(mod, sec, sym, perm, signed=True)
    ok = cid is not None and cid > 0
    print("  [%s] S1 Spikes registered (p6_w_spikes_cid > 0): cid=%s"
          % ("GREEN" if ok else " RED ", cid))
    print("-" * 64)
    if ok:
        print("RESULT: GREEN -- Spikes registered into the engine pack (classID %s);"
              " GLOBJ.SHT staged -> the first GHZ content entity renders." % cid)
        return 0
    print("RESULT: RED -- Spikes not registered (cid=%s)." % cid)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
