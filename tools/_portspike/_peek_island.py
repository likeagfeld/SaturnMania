#!/usr/bin/env python3
# Peek the live island witnesses from a savestate (PRIMARY diagnostic, CLAUDE.md 8.5):
# angle, isl_tx/isl_ty (HW-sampled mid-band screen-center texel), rpta (RPT valid?),
# armed, cont_frames. Then I can render the sim at THIS angle and compare to the HW frame.
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

MCS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "_isl_diag.mcs")
NAMES = ["_p6_w_title_island_armed", "_p6_w_title_island_angle",
         "_p6_w_title_island_rpta", "_p6_w_isl_tx", "_p6_w_isl_ty",
         "_p6_w_title_island_kast", "_p6_w_title_island_coeff0", "_p6_w_cont_frames"]

mp = Q.read_text(Q.MAP_DEFAULT)
mod = Q.load_harness()
sec = mod.parse_savestate(Q._as_path(MCS))
ma = Q.map_symbol(mp, "_p6_w_magic")
_, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
print("perm_ok =", perm is not None)
if perm is None:
    print("RED: magic not calibrated (capture too early / wrong build)"); sys.exit(1)
for n in NAMES:
    s = Q.map_symbol(mp, n)
    v = Q.peek_u32(mod, sec, s, perm, signed=True) if s else None
    print("  %-28s = %s" % (n, Q._dv(v)))
