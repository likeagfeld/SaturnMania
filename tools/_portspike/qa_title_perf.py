#!/usr/bin/env python3
# =============================================================================
# qa_title_perf.py -- CP5b.7 RED-first gate: the title is VDP1-draw-bound and
# the draw is mostly TRANSPARENT PADDING. Fire RED on the current build, GREEN
# after the content-size fix.
#
# MEASURED ROOT CAUSE (this session, savestate-attributed -- see the perf
# instrumentation in p6_io_main.cpp p6_frontend_frame + the EDSR/duty-cycle
# witnesses):
#   * CPU compute = 8.1 ms/frame (NOT the bottleneck).
#   * A/B (P6_TITLE_NODRAW): cutting the VDP1 sprite emit moved fps 8.6 -> 26.5
#     -> the ~90 ms/frame slSynch stall IS the VDP1 sprite draw.
#   * The draw is 6.3 sprites/frame, each PADDED to a fixed 248x160 box; 77% of
#     those box pixels are transparent padding (content = 0.79 screens vs the
#     3.47 screens actually drawn). VDP1 redraws 3.47 full screens of mostly-
#     transparent scaled-sprite fill every frame.
#
# THE FIX (CP5b.7): route each title sprite to the SMALLEST fixed box that holds
# it (a 64x64 small pool + the existing 248x160 large pool) so the ~5 small
# sprites/frame stop paying the 248x160 box. Cuts box fill ~4x -> ~0.86 screens.
#
# CHECKS (both RED on the current build, GREEN after the fix):
#   T1 fps        >= FPS_FLOOR    (RED 8.6 -> GREEN ~17)
#   T2 boxpx/frame <= BOX_CEIL    (RED 3.47 screens -> GREEN ~0.86)
#   T3 sanity: the title is actually ticking + drawing (cont_frames>50, cmds>0)
#      so a load-phase / no-draw capture can't pass T1/T2 trivially.
#
# PRIMARY EVIDENCE for the user-felt symptom is the fps number; the box-fill is
# the mechanism. The orchestrator ALSO opens a settled-title screenshot to
# confirm the fix did not break the visuals (CP5b.5 stability).
#
# Usage: python tools/_portspike/qa_title_perf.py [savestate.mcs] [map]
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

MCS_DEFAULT = os.path.join(HERE, "p6_s0.mcs")
SCREEN_PX = 320 * 224                 # one NTSC 320x224 screen of VDP1 fill

FPS_FLOOR = 15.0                      # RED 8.6 -> GREEN ~17 (the user symptom)
BOX_CEIL_SCREENS = 1.5               # RED 3.47 -> GREEN ~0.86 (the mechanism)
MIN_FRAMES = 50                      # the title must be ticking (not load phase)


def _peek(mod, sec, mp, perm, name):
    a = _scene.map_symbol(mp, name)
    return _scene.peek_u32(mod, sec, a, perm, signed=True) if a else None


def main(argv):
    import pathlib
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp_path = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("CP5b.7 TITLE PERF GATE -- fps + VDP1 box-fill (content-size fix)")
    print("=" * 72)
    if not os.path.isfile(mp_path):
        print("RESULT: RED -- link map missing (%s)" % mp_path); return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs); return 1
    mp = _scene.read_text(mp_path)
    mod = _scene.load_harness()
    sec = mod.parse_savestate(pathlib.Path(mcs))
    ma = _scene.map_symbol(mp, _scene.SYM_MAGIC)
    if ma is None:
        print("RESULT: RED -- magic symbol absent from map"); return 1
    label, perm = _scene.calibrate(mod._peek_bytes(sec, ma, 4))
    if perm is None:
        print("RESULT: RED -- magic mis-decode"); return 1

    cf = _peek(mod, sec, mp, perm, "_p6_w_cont_frames")
    vbl = _peek(mod, sec, mp, perm, "_p6_w_perf_vblanks")
    fr = _peek(mod, sec, mp, perm, "_p6_w_perf_frames")
    box = _peek(mod, sec, mp, perm, "_p6_w_vdp1_boxpx")
    cont = _peek(mod, sec, mp, perm, "_p6_w_vdp1_contentpx")
    cmds = _peek(mod, sec, mp, perm, "_p6_w_vdp1_cmds")

    fps = (60.0 * fr / vbl) if (vbl and fr) else 0.0
    box_per = (box / cf) if (box and cf) else 0.0
    cont_per = (cont / cf) if (cont and cf) else 0.0
    box_scr = box_per / SCREEN_PX
    cont_scr = cont_per / SCREEN_PX
    cmds_per = (cmds / cf) if (cmds and cf) else 0.0

    print("  cont_frames=%s  fps=%.2f  (%.2f vbl/frame)"
          % (cf, fps, (vbl / fr) if (vbl and fr) else 0.0))
    print("  VDP1 sprites/frame = %.1f" % cmds_per)
    print("  box  fill/frame    = %d px  (%.2f screens)  <- what VDP1 draws"
          % (box_per, box_scr))
    print("  content/frame      = %d px  (%.2f screens)  <- ideal (no padding)"
          % (cont_per, cont_scr))
    if box_per:
        print("  padding waste      = %.0f%% of drawn pixels are transparent"
              % (100.0 * (box_per - cont_per) / box_per))
    print("-" * 72)

    t3 = (cf is not None and cf > MIN_FRAMES and cmds_per > 0.0)
    t1 = (fps >= FPS_FLOOR)
    t2 = (box_per > 0.0 and box_scr <= BOX_CEIL_SCREENS)
    checks = [
        ("T1 fps >= %.0f (the user symptom)" % FPS_FLOOR, t1,
         "fps=%.2f" % fps),
        ("T2 box fill/frame <= %.1f screens (kill the padding)" % BOX_CEIL_SCREENS, t2,
         "box=%.2f screens" % box_scr),
        ("T3 title ticking + drawing (cont_frames>%d, sprites>0)" % MIN_FRAMES, t3,
         "cont_frames=%s sprites/frame=%.1f" % (cf, cmds_per)),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- title fps >= %.0f and box-fill cut to %.2f screens."
              % (FPS_FLOOR, box_scr))
        return 0
    print("RESULT: RED -- title still slow / over-drawing (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
