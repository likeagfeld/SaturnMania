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
        "_p6_w_objupd_topus", "_p6_w_objupd_us", "_p6_w_objupd_n",
        "_p6_w_hog_cid", "_p6_w_hog_x", "_p6_w_hog_y", "_p6_w_obj_refills",
        # Phase 1b (#243): VDP1 draw-completion at compute-done -- the 2-vblank
        # discriminator. EDSR.CEF (bit1) sampled at end of p6_ghz_frame (just
        # before slSynch): CEF=0 => VDP1 still rasterizing the prior sprite list
        # => DRAW-BOUND; CEF=1 => VDP1 kept up => the 2nd vbl is swap cadence.
        "_p6_w_perf_v1_done", "_p6_w_perf_v1_busy",
        "_p6_w_perf_v1_copr", "_p6_w_perf_v1_lopr", "_p6_w_perf_v1_edsr",
        # Phase 2c (#246): the DIRECT compute-full bracket (entry->exit), the
        # jo-body/slSynch cross-frame measure, and the sub-attribution of the
        # previously-unbracketed ~8ms gap (head/kick/tail). These REPLACE deriving
        # compute-full (= frame - synch) with a measurement. The 5 new full/head/
        # kick/tail symbols are absent on the pre-2c map -> M1 fires RED there.
        "_p6_w_perf_synch_frt", "_p6_w_perf_synch_max",
        "_p6_w_perf_full_frt", "_p6_w_perf_full_max",
        "_p6_w_perf_head_frt", "_p6_w_perf_kick_frt", "_p6_w_perf_tail_frt",
        # LOCKED-60 (#243): DrawLists sub-attribution -- bubble-sort vs draw() callbacks.
        "_p6_w_draw_sort", "_p6_w_draw_cb", "_p6_w_draw_maxgrp", "_p6_w_draw_nents",
        # LOCKED-60 (#243): loop1 scan occupancy -- sizes the trim + explains the growth.
        "_p6_w_scan_pop", "_p6_w_scan_maxslot", "_p6_w_scan_bounds"]

# OPTIONAL witnesses -- present only in specific build flavors, peeked with .get so
# M1 stays GREEN on the normal/shipping build that omits them:
#   scan_divergence/divmax -- the P6_SHADOW scan-split parity proof.
#   scan_slave_n/pop        -- the P6_SPLIT dual-SH2 scan-split (slave classify).
OPT_SYMS = ["_p6_w_scan_divergence", "_p6_w_scan_divmax",
            "_p6_w_scan_slave_n", "_p6_w_scan_slave_pop"]


def main(argv):
    # CP5b.7 Phase 1: UNIVERSAL per-scene perf gate. `--scene {ghz,title,ui}` picks the
    # don't-regress fps FLOOR + which scene-specific checks apply. M4 (present) / M5
    # (collision-inflate) are GHZ-only -- their witnesses are 0 on a UI/title scene, so
    # they would false-RED a title capture; SKIPPED for non-ghz. The fps FLOOR is the
    # "solid foundation moving forward" teeth: it RED's if future content drops a scene
    # below its banked floor. Floors rise as each perf phase lands (title 12->17->24->28).
    SCENE_FLOOR = {"ghz": 45.0, "title": 12.0, "ui": 25.0}
    args = list(argv[1:])
    scene = "ghz"
    if "--scene" in args:
        i = args.index("--scene")
        if i + 1 < len(args):
            scene = args[i + 1]
            del args[i:i + 2]
    fps_floor = SCENE_FLOOR.get(scene, 15.0)
    is_ghz = (scene == "ghz")
    mcs = _scene._as_path(args[0]) if len(args) > 0 else MCS_DEFAULT
    mp = _scene._as_path(args[1]) if len(args) > 1 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("PERF PHASE 1 BASELINE: true fps + per-section cost attribution  [scene=%s]" % scene)
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
    for s in OPT_SYMS:
        a = _scene.map_symbol(map_text, s)
        v[s] = _scene.peek_u32(mod, sections, a, perm, signed=True) if a else None

    vbl = v["_p6_w_perf_vblanks"]
    frames = v["_p6_w_perf_frames"]
    vbl_max = v["_p6_w_perf_vbl_max"]
    ci = v["_p6_w_perf_cyc_input"]
    co = v["_p6_w_perf_cyc_obj"]
    cd = v["_p6_w_perf_cyc_draw"]
    cp = v["_p6_w_perf_cyc_present"]
    ct = v["_p6_w_perf_cyc_total"]
    cks = v["_p6_w_perf_cks"]

    # CP5b.7 Phase 1: steady fps (drop the single worst frame = the one-time scene-load
    # spike) -- computed early so the per-scene M-FPS floor can gate it. Same formula
    # the baseline block prints below.
    fps_steady = 0.0
    if (frames is not None and frames > 1 and vbl is not None and vbl > 0):
        _svbl = vbl - (vbl_max if (vbl_max and vbl_max > 0) else 0)
        _sfr = frames - 1
        fps_steady = (60.0 * _sfr / _svbl) if (_svbl > 0 and _sfr > 0) else (60.0 * frames / vbl)
    m_fps = (fps_steady >= fps_floor)

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
    # CP5b.7 Phase 1: M4 (GHZ FG present cache) + M5 (GHZ collision-window cache) are
    # GHZ-only -- their witnesses are 0 on a UI/title scene (no FG present, no collision
    # band), so they false-RED a non-GHZ capture. SKIP them off-GHZ.
    if not is_ghz:
        m4 = True
        m5 = True
    # M6 (Phase 1b #243): VDP1 draw-completion sampled. RED while uninstrumented
    # (witnesses absent); GREEN once done+busy frames were counted. This is the
    # measurement that localizes the 2-vblank lock (draw-bound vs swap cadence).
    v1d = v.get("_p6_w_perf_v1_done")
    v1b = v.get("_p6_w_perf_v1_busy")
    m6 = (v1d is not None and v1b is not None and (v1d + v1b) > 0)
    # M7 (Phase 2c #246): the DIRECT compute-full bracket is measuring. full_frt
    # MUST be > 0 and >= the 4-section sum (the full entry->exit bracket strictly
    # CONTAINS head + the 4 sections + kick + tail), and head/kick/tail >= 0. RED
    # while uninstrumented (the 5 symbols are absent -> caught by M1). This is the
    # measurement that retires the derivation -- master compute is now read, not
    # computed from frame-minus-synch.
    ff = v.get("_p6_w_perf_full_frt")
    hd = v.get("_p6_w_perf_head_frt")
    kk = v.get("_p6_w_perf_kick_frt")
    tl = v.get("_p6_w_perf_tail_frt")
    m7 = (ff is not None and ff > 0 and ct is not None and ff >= ct
          and all(x is not None and x >= 0 for x in (hd, kk, tl)))

    # ---- DON'T-REGRESS TEETH (the "fps won't come back" gate, 2026-06-18) -------
    # The perf gate was measurement-only ("sub-60 is EXPECTED"). It now also FAILS
    # on a framerate regression, because the badnik batch silently HALVED fps
    # (48->20) when a per-frame diagnostic (the BD_SCAN 6x-foreach badnik witness)
    # leaked into the SHIPPING hot loop -- and no gate caught it. Two ceilings,
    # both read from the in-motion shipping capture:
    #   M8 compute-full <= 33.3ms  -- the 2-vblank boundary. Over it the master
    #      compute spills to 3+ vblanks => sub-30fps. Protects "never below 30".
    #   M9 shipping diagnostic tail <= 3ms -- the per-frame witness/EDSR/overlay
    #      block (present-end -> frame-exit). In the NOSCAN shipping build this
    #      MUST be ~0 (the census + the badnik scan are compile-stripped). A
    #      non-trivial tail == a diagnostic leaked into the production frame (the
    #      exact regression). RED on the leak, GREEN once it's stripped.
    # NOTE: run against the SHIPPING build (P6_NOSCAN=1). A diag build (NOSCAN off)
    # legitimately fails M9 (it runs the witnesses by design) -- that is expected.
    COMPUTE_CEIL_MS = 33.3
    TAIL_CEIL_MS = 3.0
    _c = cks if (cks is not None and 0 <= cks <= 3) else 1
    _tick_us = (8 << (2 * _c)) / CLOCK_MHZ
    compute_full_ms = (ff * _tick_us / 1000.0) if ff else None
    tail_ms_now = (tl * _tick_us / 1000.0) if tl is not None else None
    m8 = compute_full_ms is not None and compute_full_ms <= COMPUTE_CEIL_MS
    m9 = tail_ms_now is not None and tail_ms_now <= TAIL_CEIL_MS
    # CP5b.7 Phase 1: M8 (compute-full <= the 2-vblank cliff) is the GHZ compute-
    # overrun don't-regress -- it's the fps determinant ONLY for a compute-bound scene.
    # The title is DRAW-bound (the cost is in slSynch, OUTSIDE the bracket; compute-full
    # is a noisy per-frame sample there), so M8 is GHZ-only; M-FPS is the title's floor.
    if not is_ghz:
        m8 = True

    checks = [
        ("M2 instrumentation measuring (frames>0, vblanks>0, cyc_total>0)", m2,
         "frames=%s vblanks=%s cyc_total=%s" % (frames, vbl, ct)),
        ("M3 measurement sane (vblanks>=frames, cyc>=0, cks 0..3)", m3,
         "vbl_max=%s cks=%s" % (vbl_max, cks)),
        ("M4 present static-map CACHED (0 inflates, present<=3 vbl/frame)", m4,
         "present_refills=%s present=%s vbl/frame" % (prf, pvp)),
        ("M5 collision window CACHED (0 ProcessObjects inflates/frame)", m5,
         "obj_refills=%s" % (objr,)),
        ("M6 VDP1 draw-completion sampled (done+busy>0)", m6,
         "v1_done=%s v1_busy=%s" % (v1d, v1b)),
        ("M7 compute-full MEASURED (full>0, full>=section-sum, head/kick/tail>=0)", m7,
         "full=%s sectsum=%s head=%s kick=%s tail=%s" % (ff, ct, hd, kk, tl)),
        ("M8 DON'T-REGRESS compute-full <= %.1fms (>=30fps floor)" % COMPUTE_CEIL_MS, m8,
         "compute_full=%.1f ms (ceiling %.1f)" % (compute_full_ms if compute_full_ms else -1.0, COMPUTE_CEIL_MS)),
        ("M9 DON'T-REGRESS shipping diag tail <= %.1fms (no diagnostic in hot loop)" % TAIL_CEIL_MS, m9,
         "tail=%.1f ms (ceiling %.1f)" % (tail_ms_now if tail_ms_now else -1.0, TAIL_CEIL_MS)),
        ("M-FPS DON'T-REGRESS steady fps >= %.0f (scene=%s floor)" % (fps_floor, scene), m_fps,
         "fps_steady=%.2f (floor %.1f)" % (fps_steady, fps_floor)),
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
            # Phase 2h: the per-Update TIMING hog ranked by FRT TICKS (the vbl
            # profiler saturates to 0 once the frame fits ~1 vblank). us() maps
            # ticks -> microseconds via the recorded TCR divider.
            hc = v.get("_p6_w_objupd_topclass"); hu = v.get("_p6_w_objupd_topus")
            hn = v.get("_p6_w_objupd_topn")
            if hc is not None and hc >= 0 and hu:
                hms = us(hu) / 1000.0
                print("    UPDATE-TIME HOG  : classID&0x3F=%s  %.2f ms over %s "
                      "in-range (%.2f ms each) <== the fix target"
                      % (hc, hms, hn, (hms / hn) if hn else 0.0))
                hcid = v.get("_p6_w_hog_cid"); hx = v.get("_p6_w_hog_x")
                hy = v.get("_p6_w_hog_y")
                if hcid is not None and hcid >= 0:
                    print("    hog identity     : full classID=%s  @ world "
                          "(%d, %d) px  (match to GHZ Act1 layout)"
                          % (hcid, (hx >> 16) if hx else 0, (hy >> 16) if hy else 0))
                # Per-class FRT-us table (ranked) -- peek the us[64]/n[64] arrays.
                usb = syms.get("_p6_w_objupd_us"); nb = syms.get("_p6_w_objupd_n")
                if usb is not None and nb is not None:
                    rows = []
                    for ci_ in range(64):
                        t = _scene.peek_u32(mod, sections, usb + ci_ * 4, perm, signed=True)
                        n = _scene.peek_u32(mod, sections, nb + ci_ * 4, perm, signed=True)
                        if t and t > 0:
                            rows.append((t, ci_, n))
                    rows.sort(reverse=True)
                    tot = sum(r[0] for r in rows) or 1
                    print("    per-class Update cost (FRT, ranked):")
                    for t, ci_, n in rows[:8]:
                        print("      classID&0x3F=%-2d  %6.2f ms  x%-2s  (%4.1f%% of obj)"
                              % (ci_, us(t) / 1000.0, n, 100.0 * t / tot))
                objr = v.get("_p6_w_obj_refills")
                if objr is not None:
                    verdict = ("ROOT CAUSE -- collision re-inflates the layout window"
                               if objr > 0 else "NOT inflates (objr=0) -> soft-float / loop")
                    print("    ProcessObjects SaturnLayout inflates/frame = %s  [%s]"
                          % (objr, verdict))
        # Phase 2i (Task #245): ProcessObjects internal sub-attribution. The
        # 13ms section splits into loop1 (the full ENTITY_COUNT inRange scan +
        # the ~12 in-range Update()s), loop2 (typeGroup, 2h-list-driven), loop3
        # (the lateUpdate pass -- STILL a full ENTITY_COUNT scan). Object.cpp
        # brackets each under P6_PERF_OBJPROF; optional (absent on a non-profiled
        # build -> this block silently skips, no RED).
        l1s = _scene.map_symbol(map_text, "_p6_w_objsec_loop1")
        l2s = _scene.map_symbol(map_text, "_p6_w_objsec_loop2")
        l3s = _scene.map_symbol(map_text, "_p6_w_objsec_loop3")
        if l1s and l2s and l3s:
            l1 = _scene.peek_u32(mod, sections, l1s, perm, signed=True)
            l2 = _scene.peek_u32(mod, sections, l2s, perm, signed=True)
            l3 = _scene.peek_u32(mod, sections, l3s, perm, signed=True)
            usb2 = syms.get("_p6_w_objupd_us")
            upd = 0
            if usb2 is not None:
                for ci_ in range(64):
                    upd += _scene.peek_u32(mod, sections, usb2 + ci_ * 4, perm, signed=True)
            tot = (l1 + l2 + l3) or 1
            print("  --- INSIDE ProcessObjects (FRT-measured; Lever-2 target) ----")
            print("    loop1 inRange-scan+Update : %6.2f ms" % (us(l1) / 1000.0))
            print("      - game-logic Update     : %6.2f ms  (objupd_us sum)"
                  % (us(upd) / 1000.0))
            print("      - bare full-slot scan   : %6.2f ms  (loop1 - Update)"
                  % (us(l1 - upd) / 1000.0))
            print("    loop2 typeGroup (2h-list) : %6.2f ms" % (us(l2) / 1000.0))
            print("    loop3 lateUpdate full-scan: %6.2f ms" % (us(l3) / 1000.0))
            print("    -> %.0f%% of ProcessObjects = the two full entity-table "
                  "scans, NOT game logic (%.2f ms)."
                  % (100.0 * (l1 - upd + l3) / tot, us(upd) / 1000.0))
        # LOCKED-60 (#243): DrawLists (7.3ms) sub-attribution -- the lever decision.
        # bubble-sort O(n^2) is a CHEAP single-CPU fix; callbacks/emit cost justifies
        # the dual-SH2 render-pipeline. MEASURED, not guessed.
        dsort = v.get("_p6_w_draw_sort"); dcb = v.get("_p6_w_draw_cb")
        dmax = v.get("_p6_w_draw_maxgrp"); dn = v.get("_p6_w_draw_nents")
        if dsort is not None and dcb is not None:
            sort_ms = us(dsort) / 1000.0; cb_ms = us(dcb) / 1000.0
            print("  --- INSIDE DrawLists (#243 LOCKED-60 lever decision) --------")
            print("    zdepth bubble-sort (O(n^2)) : %6.2f ms" % sort_ms)
            print("    draw() callbacks + VDP1 emit: %6.2f ms" % cb_ms)
            print("    drawgroups: max %s entities/group, %s total entries"
                  % (dmax, dn))
            if sort_ms > cb_ms:
                print("    -> SORT DOMINATES: a cheap single-CPU sort fix is the lever"
                      " (possibly 60fps WITHOUT dual-SH2).")
            else:
                print("    -> CALLBACKS/EMIT dominate: the dual-SH2 render-pipeline"
                      " is the lever.")
        # LOCKED-60 (#243): loop1 SCAN occupancy -- sizes the maxOccupiedSlot trim AND
        # explains the 5.82->15.95ms growth. ENTITY_COUNT=1216 (0x40 reserve + 0x440
        # scene + 0x40 temp). More ACTIVE_*BOUNDS = more per-camera distance checks.
        spop = v.get("_p6_w_scan_pop"); smax = v.get("_p6_w_scan_maxslot")
        sbnd = v.get("_p6_w_scan_bounds")
        if spop is not None and smax is not None and spop > 0:
            ENT = 1216
            tail = (ENT - 1 - smax) if smax >= 0 else 0
            print("  --- loop1 SCAN occupancy (the 15.95ms WALL: trim size + growth) -")
            print("    populated slots (classID!=0) : %s / %d" % (spop, ENT))
            print("    highest populated slot       : %s  (empty tail %d = %.1f%% of scan)"
                  % (smax, tail, 100.0 * tail / ENT))
            print("    ACTIVE_*BOUNDS (per-cam check): %s  (%.0f%% of populated = per-slot hog)"
                  % (sbnd, 100.0 * (sbnd or 0) / spop))
            # The trim saving is NOT the tail % of TIME -- empty slots are CHEAP
            # (classID==0 -> instant skip), so skipping the empty tail saves only
            # tail*cheap-cost. The scan TIME is dominated by the populated BOUNDS
            # checks (per-camera distance reads from slow WRAM-L). So when bounds%
            # is high, the maxOccupiedSlot trim is MARGINAL and the real lever is
            # parallelizing the bounds scan (dual-SH2 split) -- MEASURED, not guessed.
            bpct = 100.0 * (sbnd or 0) / spop
            if bpct > 80.0:
                print("    -> SCAN COST = the %s BOUNDS checks (%.0f%% of populated) on slow"
                      % (sbnd, bpct))
                print("       WRAM-L, NOT slot count. Empty slots are cheap -> maxOccupiedSlot")
                print("       saves only ~tail*cheap = MARGINAL. The lever is the dual-SH2")
                print("       scan-split (parallelize the bounds scan), NOT the trim.")
            elif tail >= 0.10 * ENT:
                print("    -> maxOccupiedSlot trim recovers ~%.1f%% (empty tail; worth it)."
                      % (100.0 * tail / ENT))
            else:
                print("    -> maxOccupiedSlot trim MARGINAL (scattered empties).")
        # LOCKED-60 (#243): scan-split PARITY PROOF -- divergence between classify-all-
        # at-frame-start (the split model) and the serial interleaved engine. 0 over
        # real gameplay => the dual-SH2 scan-split is parity-EXACT for GHZ1 (safe to
        # build). >0 => a mid-frame reposition crosses a bound -> the split would
        # differ; investigate those entities before splitting.
        sdiv = v.get("_p6_w_scan_divergence"); sdmax = v.get("_p6_w_scan_divmax")
        if sdiv is not None:
            print("  --- SCAN-SPLIT PARITY PROOF (#243; P6_SHADOW build) -----------")
            print("    inRange divergence (split vs serial): this frame %s, worst %s"
                  % (sdiv, sdmax))
            if (sdmax or 0) == 0:
                print("    -> GREEN: scan-split is PARITY-EXACT for GHZ1 -- safe to build.")
            else:
                print("    -> %s entities diverge: a mid-frame reposition crosses a bound."
                      % sdmax)
                print("       Scan-split is NOT free-parity here -- investigate before building.")
        # Phase 1b (#243): the 2-VBLANK-LOCK discriminator. A 4ms CPU cut moved
        # fps 29.91->29.91 (zero frames flipped to 1 vbl) -> the 30fps is NOT CPU-
        # bound. EDSR.CEF at compute-done (just before slSynch) decides WHY the
        # frame holds 2 vblanks: VDP1 still drawing the prior sprite list (draw-
        # bound -> cull/dirty-rect lever) vs VDP1 idle (swap cadence -> reorder).
        v1d2 = v.get("_p6_w_perf_v1_done"); v1b2 = v.get("_p6_w_perf_v1_busy")
        v1co = v.get("_p6_w_perf_v1_copr"); v1lo = v.get("_p6_w_perf_v1_lopr")
        v1ed = v.get("_p6_w_perf_v1_edsr")
        if v1d2 is not None and v1b2 is not None and (v1d2 + v1b2) > 0:
            tot_s = v1d2 + v1b2
            busy_pct = 100.0 * v1b2 / tot_s
            print("  --- VDP1 DRAW-COMPLETION at compute-done (2-VBLANK-LOCK key) -")
            print("    EDSR.CEF sampled at end of p6_ghz_frame, just before slSynch")
            print("    (CEF=1 VDP1 finished prior sprite list; CEF=0 still drawing):")
            print("      VDP1 DONE  (CEF=1, kept up) : %5d / %d frames (%.0f%%)"
                  % (v1d2, tot_s, 100.0 - busy_pct))
            print("      VDP1 BUSY  (CEF=0, drawing) : %5d / %d frames (%.0f%%)"
                  % (v1b2, tot_s, busy_pct))
            if v1lo:
                print("      last-busy COPR/LOPR        : %d / %d  (~%.0f%% through "
                      "the cmd list when CPU finished)"
                      % ((v1co or 0), v1lo, 100.0 * (v1co or 0) / v1lo if v1lo else 0))
            print("      last raw EDSR              : 0x%04X"
                  % ((v1ed or 0) & 0xFFFF))
            print("    VERDICT (the lever, MEASURED -- not guessed):")
            if busy_pct >= 50.0:
                print("      >> VDP1-DRAW-BOUND. VDP1 has NOT finished the prior")
                print("         frame's sprite draw when the CPU is done; slSynch's")
                print("         swap then waits on VDP1 -> the frame spans 2 vblanks.")
                print("         LEVER = DRAW REDUCTION (sprite cull / dirty-rect /")
                print("         fewer+larger VDP1 cmds), NOT CPU cuts, NOT swap reorder.")
            else:
                # Phase 2c (#246): prefer the DIRECT compute-full bracket over the
                # 4-section FRT-sum (the sum UNDERCOUNTS by the unbracketed head+
                # kick+tail ~8ms gap). MEASURED, not derived.
                ff_ = v.get("_p6_w_perf_full_frt")
                use_full = bool(ff_ and ff_ > 0)
                compute_ms = us(ff_) / 1000.0 if use_full else us(ct) / 1000.0
                overrun = compute_ms - VBL_MS
                print("      >> VDP1 KEEPS UP (idle at compute-done) -- NOT draw-bound.")
                if overrun > 0.0:
                    print("         COMPUTE-OVERRUN CLIFF: CPU compute (%.1f ms %s)"
                          % (compute_ms, "MEASURED full" if use_full else "FRT-sum"))
                    print("         exceeds the %.1f ms vblank by %.1f ms, so slSynch swaps"
                          % (VBL_MS, overrun))
                    print("         a vblank LATE -> 2 vbl/frame = 30fps. This is a CLIFF:")
                    print("         a cut < %.1f ms does NOTHING (frame stays 2 vbl -- why"
                          % overrun)
                    print("         the earlier 4ms cut showed 0 fps change); a cut >= %.1f"
                          % overrun)
                    print("         ms (target ProcessObjectDrawLists %dms + the bare entity"
                          % round(us(cd) / 1000.0))
                    print("         scan ~5.8ms) drops the frame to 1 vbl = 60fps.")
                    print("         LEVER = CPU compute reduction (~%.1f ms), NOT draw cull"
                          % overrun)
                    print("         (VDP1 idle), NOT swap reorder.")
                else:
                    print("         compute is UNDER budget yet the swap lands late ->")
                    print("         genuine SWAP CADENCE/PHASE: reorder so compute finishes")
                    print("         before the target vblank. (Not CPU volume, not draw.)")
        # Phase 2c (#246): the DIRECT, no-deriving master per-frame model. The four
        # section brackets sum to cyc_total but UNDERCOUNT the frame (head + the
        # slave-kick + the census/EDSR/witness tail were unbracketed = the ~8ms gap).
        # compute-FULL (entry->exit) MEASURES the in-frame master cost; head/kick/tail
        # sub-attribute the gap; master_total = full + synch (the jo-body/slSynch
        # cross-frame delta) RECONCILES with the fps-derived steady frame -- an
        # end-to-end cross-check that the model is now complete (nothing derived).
        ff2 = v.get("_p6_w_perf_full_frt"); ffm = v.get("_p6_w_perf_full_max")
        hd2 = v.get("_p6_w_perf_head_frt"); kk2 = v.get("_p6_w_perf_kick_frt")
        tl2 = v.get("_p6_w_perf_tail_frt"); sy2 = v.get("_p6_w_perf_synch_frt")
        if ff2 is not None and ff2 > 0:
            full_ms = us(ff2) / 1000.0
            synch_ms = us(sy2) / 1000.0 if sy2 is not None else 0.0
            head_ms = us(hd2) / 1000.0 if hd2 is not None else 0.0
            kick_ms = us(kk2) / 1000.0 if kk2 is not None else 0.0
            tail_ms = us(tl2) / 1000.0 if tl2 is not None else 0.0
            gap_ms = us(ff2 - ct) / 1000.0
            resid = ff2 - ((hd2 or 0) + ci + co + (kk2 or 0) + cd + cp + (tl2 or 0))
            master_ms = full_ms + synch_ms
            print("  --- MASTER PER-FRAME, DIRECTLY MEASURED (#246; no deriving) ---")
            print("    compute-FULL (entry->exit)   : %7.2f ms   (worst %.2f ms)"
                  % (full_ms, us(ffm) / 1000.0 if ffm else 0.0))
            print("      4 sections (sum)           : %7.2f ms" % (us(ct) / 1000.0))
            print("      + gap (head+kick+tail+res) : %7.2f ms" % gap_ms)
            print("          head (entry->Input)    : %7.2f ms" % head_ms)
            print("          kick (slave fork)      : %7.2f ms" % kick_ms)
            print("          tail (census/EDSR/wit) : %7.2f ms" % tail_ms)
            print("          residual (unbracketed) : %7.2f ms" % (us(resid) / 1000.0))
            print("    jo-body/slSynch (measured)   : %7.2f ms" % synch_ms)
            print("    MASTER TOTAL = full + synch  : %7.2f ms" % master_ms)
            print("                fps-derived frame: %7.2f ms  (cross-check)"
                  % frame_ms_steady)
            print("    VERDICT (#246, MEASURED not derived):")
            if full_ms > VBL_MS:
                over = full_ms - VBL_MS
                print("      >> COMPUTE-bound. The master's IN-FRAME compute alone "
                      "(%.2f ms)" % full_ms)
                print("         exceeds the %.2f ms vblank by %.2f ms -- slSynch can NOT"
                      % (VBL_MS, over))
                print("         start the swap on time no matter what VDP1 does. 60fps")
                print("         REQUIRES cutting compute-full below %.2f ms (need -%.1f ms)."
                      % (VBL_MS, over))
                chunks = [("DrawLists", us(cd) / 1000.0),
                          ("ProcessObjects", us(co) / 1000.0),
                          ("tail", tail_ms), ("present-join", us(cp) / 1000.0),
                          ("head", head_ms), ("kick", kick_ms),
                          ("Input", us(ci) / 1000.0)]
                chunks.sort(key=lambda kv: kv[1], reverse=True)
                print("         biggest in-frame chunks: " + ", ".join(
                    "%s %.1f" % (n, m) for n, m in chunks[:4]) + " ms")
            else:
                print("      >> compute-full (%.2f ms) is UNDER the %.2f ms vblank; the"
                      % (full_ms, VBL_MS))
                print("         remaining over-budget is jo-body/slSynch (%.2f ms) ->"
                      % synch_ms)
                print("         VDP1 draw / swap cadence is the lever (VDP1 verdict above).")
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
