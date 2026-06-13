#!/usr/bin/env python3
# =============================================================================
# qa_p6_perf.py -- Perf Phase 1 gate: TRUE frame-time BASELINE + per-section
# cost attribution for the continuous engine GHZ loop (p6_ghz_frame).
#
# This is a MEASUREMENT gate, NOT a pass/fail-on-fps gate. We EXPECT sub-60 fps
# (the user flagged "painfully slow") -- the deliverable is the NUMBER: the real
# target fps + which loop section dominates, so Phase 2 can pick the right
# optimization. GREEN means "the instrumentation is live and measuring sanely";
# the printed baseline is what matters. A later phase adds the don't-regress
# threshold once an optimization lands.
#
# MECHANISM (measured, doc-cited):
#  * TRUE fps (overflow-immune): a vblank counter (p6_perf_vbl_count) ++'d by a
#    jo_core_add_vblank_callback -> SGL slIntFunction VBLANK-IN interrupt (true
#    60 Hz, NOT frame-locked). p6_ghz_frame snapshots vblanks-per-rendered-frame.
#    fps = 60 * frames / vblanks. Emulated faithfully (SH-2 clock vs 60 Hz vblank)
#    so it is the REAL-HARDWARE fps and is FpsScale-invariant.
#  * Per-section us (attribution): SBL TIM_FRT_GET_16 read READ-ONLY around each
#    of the 4 sections (ProcessInput / ProcessObjects / ProcessObjectDrawLists /
#    p6_vdp2_present_ghz_camera); wrap-handled FRC tick deltas accumulated into
#    WRAM-H witnesses. The FRT is on-chip + NOT serialized into the savestate, so
#    accumulation is mandatory. Python converts ticks->us using the recorded
#    divider (CKS): us = ticks * (8 << (2*cks)) / clock_MHz.
#
# CHECKS
#   M1 perf witness symbols present in game.map (RED while uninstrumented).
#   M2 measuring: p6_w_perf_frames > 0 AND p6_w_perf_vblanks > 0 AND
#      p6_w_perf_cyc_total > 0 (the loop ran + the FRT + vblank counters moved).
#   M3 sane: vblanks >= frames (cannot render more frames than vblanks) AND
#      every per-section cyc >= 0 AND cks in 0..3.
#
# Usage: python tools/_portspike/qa_p6_perf.py [savestate.mcs] [map]
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

# NTSC 320-mode SH-2 system clock (MHz). The FRT counts at sysclk / divider;
# the divider = (8 << (2*CKS)) cycles per FRC tick (TCR CKS bits, SEGA_TIM.H).
CLOCK_MHZ = 26.8742

SYMS = ["_p6_w_perf_vblanks", "_p6_w_perf_frames", "_p6_w_perf_vbl_max",
        "_p6_w_perf_cyc_input", "_p6_w_perf_cyc_obj", "_p6_w_perf_cyc_draw",
        "_p6_w_perf_cyc_present", "_p6_w_perf_cyc_total", "_p6_w_perf_cks",
        "_p6_w_perf_vbl_frame", "_p6_w_perf_vbl_jo", "_p6_w_perf_vbl_jo_max",
        "_p6_w_perf_vbl_input", "_p6_w_perf_vbl_obj", "_p6_w_perf_vbl_draw",
        "_p6_w_perf_vbl_present",
        "_p6_w_present_vbl_walk", "_p6_w_present_vbl_map",
        "_p6_w_present_vbl_hash", "_p6_w_present_refills",
        "_p6_w_obj_inrange", "_p6_w_obj_topclass", "_p6_w_obj_topcount",
        "_p6_w_obj_classcnt",
        "_p6_w_objupd_topclass", "_p6_w_objupd_topvbl", "_p6_w_objupd_topn",
        "_p6_w_hog_cid", "_p6_w_hog_x", "_p6_w_hog_y", "_p6_w_obj_refills"]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("PERF PHASE 1 BASELINE: true fps + per-section cost attribution")
    print("=" * 72)

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
        print("  [ RED ] M1 perf-witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected while Perf Phase 1 is uninstrumented.)")
        print("-" * 72)
        print("RESULT: RED -- perf instrumentation not present (M1).")
        return 1
    print("  [GREEN] M1 perf-witness symbols present in the link map")

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

    vbl = v["_p6_w_perf_vblanks"]
    frames = v["_p6_w_perf_frames"]
    vbl_max = v["_p6_w_perf_vbl_max"]
    ci = v["_p6_w_perf_cyc_input"]
    co = v["_p6_w_perf_cyc_obj"]
    cd = v["_p6_w_perf_cyc_draw"]
    cp = v["_p6_w_perf_cyc_present"]
    ct = v["_p6_w_perf_cyc_total"]
    cks = v["_p6_w_perf_cks"]

    m2 = (frames is not None and frames > 0 and vbl is not None and vbl > 0
          and ct is not None and ct > 0)
    m3 = (vbl is not None and frames is not None and vbl >= frames
          and all(x is not None and x >= 0 for x in (ci, co, cd, cp, ct))
          and cks is not None and 0 <= cks <= 3)
    # M4 (Phase 2c don't-regress): the present's static-map build is CACHED --
    # the last (settled) frame must do ZERO SaturnLayout inflates and a near-zero
    # present. RED on the pre-2c build (6 inflates + 51 vbl/frame); GREEN cached.
    prf = v.get("_p6_w_present_refills")
    pvp = v.get("_p6_w_perf_vbl_present")
    m4 = (prf is not None and prf == 0 and pvp is not None and pvp <= 3)
    # M5 (Phase 2g don't-regress): the COLLISION path must do ZERO SaturnLayout
    # inflates on the settled frame. RED on the pre-2g build (4 refills/frame --
    # the Player's two stable sensor bands ping-ponged a single window); GREEN
    # once the N-way window cache lets the two positions co-reside and SWAP.
    objr = v.get("_p6_w_obj_refills")
    m5 = (objr is not None and objr == 0)

    checks = [
        ("M2 instrumentation measuring (frames>0, vblanks>0, cyc_total>0)", m2,
         "frames=%s vblanks=%s cyc_total=%s" % (frames, vbl, ct)),
        ("M3 measurement sane (vblanks>=frames, cyc>=0, cks 0..3)", m3,
         "vbl_max=%s cks=%s" % (vbl_max, cks)),
        ("M4 present static-map CACHED (0 inflates, present<=3 vbl/frame)", m4,
         "present_refills=%s present=%s vbl/frame" % (prf, pvp)),
        ("M5 collision window CACHED (0 ProcessObjects inflates/frame)", m5,
         "obj_refills=%s" % (objr,)),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)

    # ---- THE BASELINE (the deliverable) ----------------------------------
    print("-" * 72)
    print("BASELINE (the Phase-1 deliverable):")
    if m2:
        c = cks if (cks is not None and 0 <= cks <= 3) else 0
        tick_us = (8 << (2 * c)) / CLOCK_MHZ
        def us(t):
            return (t * (8 << (2 * c))) / CLOCK_MHZ
        VBL_MS = 1000.0 / 60.0   # one true vblank period (NTSC) in ms
        frt_range_ms = (65536 * (8 << (2 * c))) / CLOCK_MHZ / 1000.0  # FRT wrap window

        # RAW average over the whole capture (polluted by the one-time GHZ load
        # on the first lean tick -- that frame's slip is vbl_max).
        fps_raw = 60.0 * frames / vbl
        # STEADY-STATE: drop the single worst frame (the boot/scene-load spike)
        # so the number reflects the running loop, not the one-time load.
        steady_vbl = vbl - (vbl_max if vbl_max and vbl_max > 0 else 0)
        steady_frames = frames - 1
        fps_steady = (60.0 * steady_frames / steady_vbl) if steady_vbl > 0 and steady_frames > 0 else fps_raw
        vpf_steady = (float(steady_vbl) / steady_frames) if steady_frames > 0 else 0.0
        frame_ms_steady = vpf_steady * VBL_MS

        print("  TRUE fps (steady)  : %.2f   (~%.0f ms/frame; %d vbl/frame)"
              % (fps_steady, frame_ms_steady, round(vpf_steady)))
        print("  TRUE fps (raw avg) : %.2f   (incl. one-time scene-load frame, "
              "slip=%s vbl)" % (fps_raw, vbl_max))
        print("    [%d frames over %d true 60Hz vblanks; FRT /%d -> %.4f us/tick]"
              % (frames, vbl, (8 << (2 * c)), tick_us))
        # Per-section: VBLANK is the TRUTH (overflow-immune); FRT us is only
        # valid for sub-78ms sections (it multi-wraps + undercounts bigger ones).
        vi = v.get("_p6_w_perf_vbl_input"); vo = v.get("_p6_w_perf_vbl_obj")
        vd = v.get("_p6_w_perf_vbl_draw"); vpr = v.get("_p6_w_perf_vbl_present")
        print("  --- per-section cost (last frame) -- VBLANK is authoritative ---")
        sect = [("ProcessInput", ci, vi), ("ProcessObjects", co, vo),
                ("ProcessObjectDrawLists", cd, vd),
                ("p6_vdp2_present_ghz_camera", cp, vpr)]
        dom = max(sect, key=lambda kv: (kv[2] if kv[2] is not None else -1))
        for name, t, vbl in sect:
            mark = "  <== DOMINANT" if name == dom[0] else ""
            wrap = " (FRT WRAPPED -- us unreliable)" if (vbl is not None and vbl * VBL_MS > frt_range_ms) else ""
            print("    %-28s %4s vbl %8.1f ms  [frt %.1f ms%s]%s"
                  % (name, vbl, (vbl or 0) * VBL_MS, us(t) / 1000.0, wrap, mark))
        print("    %-28s %4s vbl %8.1f ms  [frt-sum %.1f ms]"
              % ("p6_ghz_frame TOTAL", v.get("_p6_w_perf_vbl_frame"),
                 (v.get("_p6_w_perf_vbl_frame") or 0) * VBL_MS, us(ct) / 1000.0))
        # WHERE THE FRAME GOES (vblank-authoritative). The FRT per-section us
        # above UNDERCOUNTS any section > the FRT range (it multi-wraps); the
        # vblank counter is overflow-immune and is the truth.
        vbl_frame = v.get("_p6_w_perf_vbl_frame")
        vbl_jo = v.get("_p6_w_perf_vbl_jo")
        vbl_jo_max = v.get("_p6_w_perf_vbl_jo_max")
        print("  --- WHERE THE FRAME GOES (vblank-MEASURED, the truth) --------")
        if vbl_frame is not None and vbl_jo is not None:
            print("    inside p6_ghz_frame (engine) : %4s vbl  %8.1f ms  (the cost)"
                  % (vbl_frame, vbl_frame * VBL_MS))
            print("    jo loop body (slSynch etc.)  : %4s vbl  %8.1f ms  (normal --"
                  " NOT the bottleneck)" % (vbl_jo, vbl_jo * VBL_MS))
            print("    -> the cost is CPU work INSIDE p6_ghz_frame: the present +"
                  " ProcessObjects (see per-section above), NOT slSynch/VDP wait.")
        # Phase 2b: sub-attribution INSIDE the present (850ms #1 target).
        pw = v.get("_p6_w_present_vbl_walk"); pm = v.get("_p6_w_present_vbl_map")
        ph_ = v.get("_p6_w_present_vbl_hash"); prf = v.get("_p6_w_present_refills")
        if pw is not None:
            print("  --- INSIDE the present (850ms #1 target; vblank-measured) ----")
            print("    sec1 blank-char GetTile walk : %4s vbl %8.1f ms  "
                  "[%s SaturnLayout inflate(s)/frame]" % (pw, pw * VBL_MS, prf))
            print("    sec2 map build (GetTile+VDP2): %4s vbl %8.1f ms"
                  % (pm, pm * VBL_MS))
            print("    sec4 witness hash+count      : %4s vbl %8.1f ms  "
                  "(DIAGNOSTIC-only -- droppable in shipping)" % (ph_, ph_ * VBL_MS))
        # Phase 2d: ProcessObjects sub-attribution by in-range entity population.
        inr = v.get("_p6_w_obj_inrange"); topc = v.get("_p6_w_obj_topclass")
        topn = v.get("_p6_w_obj_topcount"); dcnt = v.get("_p6_w_obj_classcnt")
        if inr is not None:
            # vblank-authoritative ProcessObjects ms (vo = vblanks, NOT FRT ticks)
            obj_ms = (vo or 0) * VBL_MS
            per = (obj_ms / inr) if inr else 0.0
            print("  --- ProcessObjects population (%.0f ms; #2 target) ----------"
                  % obj_ms)
            print("    in-range entities : %s  (%.2f ms each over %s distinct "
                  "classes)" % (inr, per, dcnt))
            print("    dominant class    : classID&0xFF=%s  x%s in-range" % (topc, topn))
            if inr and inr <= 60:
                print("    -> FEW entities, high per-entity cost: a heavy class "
                      "(Player collision?) -- targeted-win territory.")
            elif inr:
                print("    -> MANY entities: count-dominated -> culling / dual-SH2.")
            # Phase 2d: the per-Update TIMING hog (P6_PERF_OBJPROF; the actual
            # dominant by TIME, not just count -- this NAMES the target).
            hc = v.get("_p6_w_objupd_topclass"); hv = v.get("_p6_w_objupd_topvbl")
            hn = v.get("_p6_w_objupd_topn")
            if hc is not None and hc >= 0:
                hms = (hv or 0) * VBL_MS
                print("    UPDATE-TIME HOG  : classID&0x3F=%s  %.0f ms over %s "
                      "in-range (%.1f ms each) <== the fix target"
                      % (hc, hms, hn, (hms / hn) if hn else 0.0))
                hcid = v.get("_p6_w_hog_cid"); hx = v.get("_p6_w_hog_x")
                hy = v.get("_p6_w_hog_y")
                if hcid is not None and hcid >= 0:
                    print("    hog identity     : full classID=%s  @ world "
                          "(%d, %d) px  (match to GHZ Act1 layout)"
                          % (hcid, (hx >> 16) if hx else 0, (hy >> 16) if hy else 0))
                objr = v.get("_p6_w_obj_refills")
                if objr is not None:
                    verdict = ("ROOT CAUSE -- collision re-inflates the layout window"
                               if objr > 0 else "NOT inflates (objr=0) -> soft-float / loop")
                    print("    ProcessObjects SaturnLayout inflates/frame = %s  [%s]"
                          % (objr, verdict))
        print("  60fps budget = %.2f ms/frame; steady frame is %.0fx over budget."
              % (VBL_MS, frame_ms_steady / VBL_MS if VBL_MS > 0 else 0))
    else:
        print("  (not measuring -- see M2)")
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- frame-time instrumentation live; baseline above.")
        print("        (Measurement gate: sub-60 is EXPECTED + documented, not a")
        print("         failure. Phase 2 targets the DOMINANT vblank-measured")
        print("         section -- the VDP2 FG present, then ProcessObjects.)")
        return 0
    print("RESULT: RED -- perf instrumentation not measuring sanely (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
