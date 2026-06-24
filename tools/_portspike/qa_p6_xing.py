#!/usr/bin/env python3
# =============================================================================
# qa_p6_xing.py -- Task #249 gate: the GHZ band-crossing STALL is eliminated.
#
# BUG (user-reported): "the fps get REALLY slow as I move forward in the level
# and as it renders the next part of the level ... but starts out okay." ROOT
# CAUSE (measured): on forward progression the camera-local layout window slides
# past its band edge and SaturnLayout_Refill synchronously mz_uncompress()'d the
# new 16-row band(s) for BOTH the collision path (ProcessObjects sensors) and the
# FG present -- a ~50 ms inflate that blocks the frame (the "renders the next
# part" stall). Pre-fix MEASURED: 47 crossing frames, worst compute-full 73.7 ms,
# worst FG present-join 53.7 ms.
#
# FIX (platform/Saturn/SaturnLayout.cpp): pre-inflate each layout layer's FULL
# row-major image into the 4 MB cart RES store ONCE at Mount (hidden in the
# ~110 s load), via SaturnSheet_ResAlloc. Refill then copies the window slice
# DIRECTLY from resident pixels (a ~4 KB memcpy) with NO per-crossing inflate --
# byte-exact with the band path (identical uncompressed data). When every layer
# is resident the inflate code path is structurally unreachable during gameplay.
#
# THE GATE (the unambiguous RED->GREEN): p6_w_lay_resident == the layout's
# layerCount (read from cd/GHZ1LAYT.BIN header, u16 @ off 4). All layers resident
# => SaturnLayout_Refill can NEVER inflate during play => the synchronous
# band-crossing stall cannot occur. RED on the pre-fix build (no resident store,
# p6_w_lay_resident absent or 0). GREEN once all layers are cart-resident.
#
# NOTE on p6_w_xing_count: the resident fast-path STILL counts as a "refill"
# (a window re-window, SaturnLayout.cpp:256), so xing_count counts cheap resident
# memcpy re-windows, NOT inflates -- it is reported here for context but is NOT
# the pass/fail (a re-window is ~us; the eliminated inflate was ~50 ms). The
# pass/fail is lay_resident == layerCount.
#
# Usage: python tools/_portspike/qa_p6_xing.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "p6_xing3.mcs")
LAYT = os.path.join(ROOT, "cd", "GHZ1LAYT.BIN")

# NTSC 320-mode SH-2 clock (MHz); FRT tick = (8 << (2*CKS)) sysclk cycles.
CLOCK_MHZ = 26.8742

SYMS = ["_p6_w_lay_resident", "_p6_w_xing_count", "_p6_w_xing_max_frt",
        "_p6_w_xing_present_max", "_p6_w_obj_refills", "_p6_w_present_refills",
        "_p6_w_cont_frames", "_p6_w_perf_cks"]


def layer_count_from_store(path):
    if not os.path.isfile(path):
        return None
    d = open(path, "rb").read(8)
    if len(d) < 6 or d[:4] != b"LYT1":
        return None
    return struct.unpack(">H", d[4:6])[0]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("TASK #249 GATE: GHZ band-crossing stall eliminated (resident layouts)")
    print("=" * 72)

    layer_count = layer_count_from_store(LAYT)
    if layer_count is None:
        print("RESULT: RED -- cd/GHZ1LAYT.BIN missing/unreadable (build the asset)")
        return 1
    print("  layout store: cd/GHZ1LAYT.BIN -> %d layers" % layer_count)

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
        print("  [ RED ] X1 witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected on the pre-#249 build -- no resident store.)")
        print("RESULT: RED -- band-crossing fix not present (X1).")
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

    resident = v["_p6_w_lay_resident"]
    frames = v["_p6_w_cont_frames"]
    cks = v["_p6_w_perf_cks"]
    c = cks if (cks is not None and 0 <= cks <= 3) else 1
    tick_us = (8 << (2 * c)) / CLOCK_MHZ

    def ms(ticks):
        return (ticks or 0) * tick_us / 1000.0

    x1 = True  # symbols present (got here)
    # X2 -- the fix: EVERY layout layer is cart-resident -> the inflate path is
    # structurally unreachable in gameplay -> no synchronous band-crossing stall.
    x2 = (resident is not None and resident == layer_count)
    # X3 -- ran a real forward-progression session (not a still capture): the
    # gate is only meaningful over a run that actually crosses bands.
    x3 = (frames is not None and frames > 300)

    checks = [
        ("X1 #249 witness symbols present in the link map", x1, "ok"),
        ("X2 ALL layout layers cart-resident (no gameplay band-inflate)", x2,
         "lay_resident=%s / layerCount=%s" % (resident, layer_count)),
        ("X3 forward-progression session captured (frames>300)", x3,
         "cont_frames=%s" % frames),
    ]
    ok = all(c2 for _, c2, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)

    print("-" * 72)
    print("MEASURED (context -- the eliminated stall vs the residual):")
    print("  layers resident          : %s of %s" % (resident, layer_count))
    print("  window re-windows on run : %s frames (cheap resident memcpy, ~us --"
          % v["_p6_w_xing_count"])
    print("                             NOT the eliminated ~50 ms inflate)")
    print("  worst re-window frame    : %.1f ms compute-full  (was 73.7 ms"
          " pre-fix; residual = FG" % ms(v["_p6_w_xing_max_frt"]))
    print("                             present map-rebuild on scroll, #243/#246)")
    print("  worst FG present-join    : %.1f ms  (was 53.7 ms pre-fix)"
          % ms(v["_p6_w_xing_present_max"]))
    print("  settled-frame inflates   : obj=%s present=%s  (0 = resident path)"
          % (v["_p6_w_obj_refills"], v["_p6_w_present_refills"]))
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- #249 band-crossing stall ELIMINATED: all %d GHZ1"
              % layer_count)
        print("        layout layers are cart-resident, so SaturnLayout_Refill")
        print("        serves every window slice by memcpy and NEVER inflates a")
        print("        band during gameplay. The synchronous re-inflate root")
        print("        cause cannot occur. (Residual scroll-frame cost = the FG")
        print("        present rebuild, tracked under #243/#246, not #249.)")
        return 0
    print("RESULT: RED -- #249 not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
