#!/usr/bin/env python3
# =============================================================================
# qa_frontend_loadtime.py -- Task #271: MEASURE where the ~70 s front-end (Title)
# load goes, per LOAD SUB-STEP, from a Mednafen savestate. The DELIVERABLE is the
# per-sub-step millisecond breakdown + the named dominant cost + the settled-title
# runtime fps.
#
# METHOD (see docs/feature_checklists/frontend_title_load_timing.md):
#   p6_io_main.cpp brackets the load into 10 sub-steps. For each it records
#   (vbl, fills, kb, frt). Two regimes:
#     MASKED core S1..S7 (p6_scene_run, interrupts off + vblank ISR not yet
#       registered): vbl=0 (frozen). I/O sized by FILLS x ms-per-fill; compute
#       sized by FRT ticks / (CKS clock). The /32 FRT (CKS=1) wraps at 78 ms, so
#       a multi-wrap I/O sub-step's FRT undercounts -> use its fills instead.
#     PHASE-2 S8..S10 (p6_scene_load_and_arm, UNMASKED): vbl is EXACT ms AND
#       yields the ground-truth ms-per-fill = ph2_vbl*16.67/ph2_fills.
#
#   ms-per-fill: derived from phase-2 (ph2_vbl/ph2_fills). Each emulated GFS_Fread
#   is CD-latency-dominated (#251 ~83 ms/fill) regardless of size, so the masked
#   core's CD I/O ms = fills * ms_per_fill. (Cross-checked vs the FRT ticks, which
#   are exact for the compute-only sub-steps.)
#
#   python tools/_portspike/qa_frontend_loadtime.py [savestate.mcs] [game.map]
# =============================================================================
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
MCS_DEFAULT = os.path.join(HERE, "_loadtime.mcs")
MAP_DEFAULT = Q.MAP_DEFAULT

VBL_MS = 1000.0 / 60.0  # one vblank = 16.667 ms (NTSC 320 non-interlace)

# Sub-step labels (index 1..10). Regime: 'M' masked core (fills+frt), 'P' phase-2.
STEPS = [
    (1,  "M", "boot pre-load (memsets + InitStorage)"),
    (2,  "M", "chain loads (OVLRING/DORM/LAYT/ANIMPACK/GHZ Player sheets)"),
    (3,  "M", "512x512 sheets (LOGOS.SHT + TLOGO.SHT load+stage+resident)"),
    (4,  "M", "TSONIC.SHT (1024x1024, ~121 KB) load+stage  <-- CP5b.2 suspect"),
    (5,  "M", "LoadDataPack (DATA.RSDK 182 MB windowed walk)  <-- #251 suspect"),
    (6,  "M", "AudioDevice::Init + ScoreAdd + MenuBleep SFX"),
    (7,  "M", "LoadGameConfig (GameConfig.bin parse)"),
    (8,  "P", "LoadSceneFolder (Title scene + TileConfig)"),
    (9,  "P", "LoadSceneAssets (sprite-sheet decode/stage)"),
    (10, "P", "InitObjects (entity Create/StageLoad) + arm_env VDP1 binds"),
]

# CKS divider -> FRT tick frequency. Saturn 320-mode master clock = 26.8 MHz.
# ticks/sec = 26.8e6 / (8 << (2*cks)) :  cks0=/8 cks1=/32 cks2=/128.
def frt_khz(cks):
    if cks is None or cks < 0:
        cks = 1  # default observed = /32
    return 26800.0 / (8 << (2 * cks))  # kHz


def _peek_arr(mod, sec, base, perm, n):
    """Read n consecutive int32 starting at `base` (an array symbol)."""
    out = []
    for i in range(n):
        v = Q.peek_u32(mod, sec, (base + 4 * i) & 0xFFFFFFFF, perm, signed=True) if base else None
        out.append(v)
    return out


def main(argv):
    pos = [a for a in argv if not a.startswith("--")]
    mcs = pos[0] if len(pos) >= 1 else MCS_DEFAULT
    mp  = pos[1] if len(pos) >= 2 else MAP_DEFAULT

    print("=" * 78)
    print("FRONT-END (Title) LOAD-TIME BREAKDOWN  (Task #271)")
    print("=" * 78)
    print("  savestate : %s" % mcs)
    print("  link map  : %s" % mp)
    if not os.path.isfile(mp):
        print("RESULT: ERROR -- map missing (%s). Build P6_FRONTEND_TITLE first." % mp); return 2
    if not os.path.isfile(mcs):
        print("RESULT: ERROR -- savestate missing (%s). Capture it first." % mcs); return 2

    map_text = Q.read_text(mp)
    mod = Q.load_harness()
    sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(map_text, "_p6_w_magic")
    label, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    if perm is None:
        print("RESULT: ERROR -- magic anchor did not calibrate (image not loaded?)."); return 2

    def sym(n): return Q.map_symbol(map_text, n)
    def scal(n):
        a = sym(n)
        return Q.peek_u32(mod, sec, a, perm, signed=True) if a else None

    cks = scal("_p6_w_lt_cks")
    khz = frt_khz(cks)
    masked_vbl = scal("_p6_w_lt_masked_vbl")
    ph2_fills  = scal("_p6_w_lt_ph2_fills")
    ph2_vbl    = scal("_p6_w_lt_ph2_vbl")

    base_vbl   = sym("_p6_w_lt_vbl")
    base_fills = sym("_p6_w_lt_fills")
    base_kb    = sym("_p6_w_lt_kb")
    base_frt   = sym("_p6_w_lt_frt")
    if not (base_vbl and base_fills and base_kb and base_frt):
        print("RESULT: ERROR -- load-timing witnesses absent from the map.")
        print("        (Built the P6_FRONTEND_TITLE flavor? The -u roots are LOGOS-gated.)")
        return 2

    vbl   = _peek_arr(mod, sec, base_vbl,   perm, 11)
    fills = _peek_arr(mod, sec, base_fills, perm, 11)
    kb    = _peek_arr(mod, sec, base_kb,    perm, 11)
    frt   = _peek_arr(mod, sec, base_frt,   perm, 11)

    # Ground-truth ms-per-fill from phase-2 (the unmasked, vblank-timed regime).
    ms_per_fill = None
    if ph2_fills and ph2_fills > 0 and ph2_vbl is not None:
        ms_per_fill = (ph2_vbl * VBL_MS) / ph2_fills

    # ms-per-fill: the cleanest wrap-immune source is io_vbl = vblanks spent
    # INSIDE the windowed GFS_Seek+GFS_Fread during the UNMASKED phase only
    # (p6_gfs.c; the masked core + the single-shot loader add 0 to it). Divide it
    # by the windowed fills that ran unmasked. We don't separate windowed vs
    # single-shot per sub-step, but phase-2 (S8..S10) is ALL windowed reads and
    # is the unmasked region io_vbl covers, so io_vbl / ph2_fills is the true
    # per-(windowed-)fill cost. ph2_vbl (whole-phase-2 wall) OVERCOUNTS (it
    # includes InitObjects compute), so io_vbl is preferred.
    io_vbl = scal("_p6_w_gfs_io_vbl")
    seeks  = scal("_p6_w_gfs_seeks_real")
    total_fills = scal("_p6_w_gfs_fills")
    ms_per_fill_iovbl = None
    if io_vbl is not None and ph2_fills and ph2_fills > 0:
        ms_per_fill_iovbl = (io_vbl * VBL_MS) / ph2_fills
    print("  FRT divider: CKS=%s -> %.1f kHz (%.3f ms/wrap)   masked-core vblanks=%s (expect ~0)"
          % (cks, khz, 65536.0 / khz, masked_vbl))
    print("  GFS: total_fills=%s  seeks_real=%s (%.0f%% of fills need a CD seek)  total_KB=%s"
          % (Q._dv(total_fills), Q._dv(seeks),
             (100.0 * seeks / total_fills) if (seeks and total_fills) else 0,
             (scal("_p6_w_gfs_bytes") or 0) // 1024))
    print("  phase-2 windowed I/O: io_vbl=%s (%.2f s) over %s fills -> %s ms/(windowed)fill"
          % (Q._dv(io_vbl), (io_vbl or 0) * VBL_MS / 1000.0, ph2_fills,
             ("%.0f" % ms_per_fill_iovbl) if ms_per_fill_iovbl else "n/a"))
    # Per-fill cost used to size the MASKED I/O sub-steps. io_vbl-derived is the
    # cleanest; fall back to the whole-phase-2 aggregate, then #251's ~83ms.
    if ms_per_fill_iovbl and ms_per_fill_iovbl > 0:
        ms_per_fill = ms_per_fill_iovbl
    elif ms_per_fill is None:
        ms_per_fill = 83.0
    print("  -> sizing masked I/O sub-steps at %.0f ms/fill" % ms_per_fill)
    print("-" * 78)
    print("  %-4s %-3s %-7s %-7s %-7s  %-9s  %-7s %s" %
          ("step", "reg", "vbl", "fills", "kb", "ms", "frt_ms", "what"))
    print("  " + "-" * 80)

    rows = []
    total_ms = 0.0
    for idx, reg, lbl in STEPS:
        v, f, k, t = vbl[idx], fills[idx], kb[idx], frt[idx]
        frt_ms = (t or 0) / khz  # lower bound for I/O (undercounts per 78ms wrap)
        # ms estimate:
        #   phase-2: vbl is exact.
        #   masked: I/O sub-step (fills>0) -> fills*ms_per_fill (wrap-immune).
        #           compute sub-step (fills==0) -> frt ticks / khz (exact <78ms).
        if reg == "P":
            ms = (v or 0) * VBL_MS
            how = "vbl"
        else:
            if (f or 0) > 0:
                ms = (f or 0) * ms_per_fill
                how = "fill"
            else:
                ms = frt_ms
                how = "frt"
        total_ms += ms
        rows.append((idx, reg, lbl, v, f, k, t, ms, how))
        print("  S%-3d %-3s %-7s %-7s %-7s  %8.0f%s %7.0f %s"
              % (idx, reg, Q._dv(v), Q._dv(f), Q._dv(k), ms,
                 {"vbl":"v", "fill":"f", "frt":"t"}[how], frt_ms, lbl))

    print("  " + "-" * 80)
    print("  %-46s TOTAL load  %8.0f ms  (%.1f s)" % ("", total_ms, total_ms / 1000.0))
    print("  (ms: v=exact vblank | f=fills x %.0fms/fill | t=FRT/%.1fkHz; frt_ms col = raw"
          % (ms_per_fill, khz))
    print("   FRT ticks/khz, a LOWER bound for I/O rows that wrap the 78ms /32 FRT.)")

    # Dominant sub-step.
    dom = max(rows, key=lambda r: r[7])
    print("-" * 78)
    print("  DOMINANT: S%d (%s) = %.0f ms (%.0f%% of the measured load), %s fills"
          % (dom[0], dom[2], dom[7], 100.0 * dom[7] / total_ms if total_ms else 0,
             Q._dv(dom[4])))

    # Settled-title runtime fps (the per-frame perf witnesses).
    pv = scal("_p6_w_perf_vblanks"); pf = scal("_p6_w_perf_frames")
    full = scal("_p6_w_perf_full_frt"); synch = scal("_p6_w_perf_synch_frt")
    cont = scal("_p6_w_cont_frames")
    fps = (60.0 * pf / pv) if (pv and pf and pv > 0) else None
    print("-" * 78)
    print("  SETTLED-TITLE RUNTIME (per-frame perf witnesses):")
    print("    cont_frames=%s  perf_frames=%s  perf_vblanks=%s -> fps=%s"
          % (Q._dv(cont), Q._dv(pf), Q._dv(pv), ("%.2f" % fps) if fps else "n/a"))
    if full is not None and full >= 0:
        print("    full_frt=%s ticks (%.1f ms compute/frame)  synch_frt=%s"
              % (Q._dv(full), full / khz, Q._dv(synch)))

    # Task #271 FIX verdict: the SFX early-out removes the wasted LoadGameConfig
    # global-SFX file-opens (the MEASURED dominant cost). saved_open counts them.
    saved = scal("_p6_w_lt_sfx_savedopen")
    skips = scal("_p6_w_sfx_skips")
    s7_fills = fills[7]
    print("-" * 78)
    print("  TASK #271 FIX (SFX-early-out) VERDICT:")
    print("    sfx_savedopen=%s (file-opens SKIPPED)  sfx_skips=%s (alloc-rejected after open)"
          % (Q._dv(saved), Q._dv(skips)))
    print("    S7 LoadGameConfig fills=%s" % Q._dv(s7_fills))
    # GREEN once the early-out is active: it saves the bulk of the ~52 SFX opens, so
    # S7 fills collapse from ~54 (every SFX opened) to a handful (only the ones that
    # actually fit + the 1 that latches). RED baseline: saved_open==0, S7 fills~54.
    fixed = (saved is not None and saved >= 30 and s7_fills is not None and s7_fills <= 10)
    if fixed:
        print("  [GREEN] the early-out is ACTIVE: %s opens saved, S7 collapsed to %s fills."
              % (Q._dv(saved), Q._dv(s7_fills)))
    elif saved == 0:
        print("  [ RED ] early-out INERT (saved_open=0): the ~51 wasted SFX opens still run.")
    else:
        print("  [INFO ] partial: saved_open=%s, S7 fills=%s." % (Q._dv(saved), Q._dv(s7_fills)))
    print("=" * 78)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
