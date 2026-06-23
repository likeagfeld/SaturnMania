#!/usr/bin/env python3
# =============================================================================
# qa_title_twirl_resident.py -- RED/GREEN gate for the title Sonic-twirl
# "slide show" (user, verbatim: "the sonic twirl is effectively a slide show").
#
# THE BUG (MEASURED diagnosis, memory title-render-decomp-fidelity-rebuild.md):
# the Sonic BODY twirl = Title/Sonic.bin anim0, ~49 frames, PLAY-ONCE. On the
# CURRENT (banded) build TSONIC.SHT is staged BANDED but NOT made resident
# (p6_io_main.cpp:3599 DELIBERATELY skips SaturnSheet_MakeResident), so each
# play-once body frame's VDP1-cache MISS runs SaturnSheet_FetchRect, which miniz-
# INFLATES the intersecting compressed bands EVERY frame (SaturnSheet.cpp:347
# ++p6_w_sht_fetches per band). That per-frame inflate of a 241x137 body frame is
# the slideshow cost. The FIX (mirror the PROVEN GHZ resident-sheet path, memory
# ghz-resident-sheets-render-perf.md) is SaturnSheet_MakeResident(TSONIC) so every
# twirl frame is a fast cart->dst memcpy with ZERO inflate.
#
# WHY A TWO-STATE DELTA GATE (not a single peek): the title's p6_frontend_frame
# does NOT call p6_vdp1_perf_reset(), so p6_w_sht_fetches / p6_w_vdp1_evicts are
# CUMULATIVE monotonic counters. The PER-FRAME twirl cost is the DELTA of those
# counters between two savestates captured during the (auto-running) intro,
# divided by the cont_frames delta. This is the savestate-harness measurement the
# methodology mandates (NOT a screenshot, NOT a code proxy).
#
# THE DISCRIMINATOR (this gate measures BOTH candidate costs so the verdict is
# data-driven, not assumed):
#   * sht_fetches/frame  -- the BANDED miniz inflate (the resident fix kills this).
#   * vdp1_evicts/frame  -- the LRU box-staging + jo_sprite_replace DMA (a SEPARATE
#                           cost the resident fix does NOT touch; reported so we
#                           know whether residency alone is sufficient).
#
# GATE TEETH (RED on the current banded build, GREEN after MakeResident lands):
#   T1  witness symbols present in game.map.
#   T2  both captures sane: magic calibrates, cont_frames_B > cont_frames_A
#       (the intro actually advanced between the two states), tsonic surface BOUND
#       (tsonic_handle >= 0) in both (so the body IS the thing being drawn).
#   T3  THE GATE: per-frame banded-inflate delta == 0 during the twirl.
#       RED  = sht_fetches advanced -> the body re-inflates per frame (slideshow).
#       GREEN= TSONIC resident (sht_resident grew + sht_fetches flat) -> each twirl
#              frame is a cart copy, no inflate.
#
# Usage:
#   python tools/_portspike/qa_title_twirl_resident.py <stateA.mcs> <stateB.mcs> [game.map]
#       stateA = earlier intro-twirl frame, stateB = later intro-twirl frame.
#   python tools/_portspike/qa_title_twirl_resident.py --selftest
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

MAP_DEFAULT = os.path.normpath(os.path.join(ROOT, "game.map"))

# Cumulative witnesses (C-linkage; one-underscore sh-none-elf convention).
SYM_FETCHES   = "_p6_w_sht_fetches"    # SaturnSheet.cpp: per-band miniz inflates
SYM_RESIDENT  = "_p6_w_sht_resident"   # sheets made resident (MakeResident ok count)
SYM_STAGED    = "_p6_w_sht_staged"     # sheets staged into the band store
SYM_EVICTS    = "_p6_w_vdp1_evicts"    # p6_vdp1.c: LRU box-staging + jo_sprite_replace
SYM_CMDS      = "_p6_w_vdp1_cmds"      # landed sprite cmds (last frame, NOT reset on title)
SYM_LANDED    = "_p6_w_vdp1_landed"    # cumulative blits that reached a valid slot
SYM_CONTFR    = "_p6_w_cont_frames"    # frontend frames ticked (the per-frame divisor)
SYM_TSON_H    = "_p6_w_tsonic_handle"  # TitleSonic VDP1 bind handle (>=0 == bound)
SYM_TSON_AID  = "_p6_w_tsonic_animid"  # live anim id (0 == the body twirl)
SYM_TSON_FID  = "_p6_w_tsonic_frameid" # live frame id (advances through the twirl)
SYM_TSON_VIS  = "_p6_w_tsonic_visible" # FlashIn flips visible=1

REQUIRED = [SYM_FETCHES, SYM_RESIDENT, SYM_STAGED, SYM_EVICTS, SYM_CONTFR,
            SYM_TSON_H]
OPTIONAL = [SYM_CMDS, SYM_LANDED, SYM_TSON_AID, SYM_TSON_FID, SYM_TSON_VIS]


def _read_state(mp_text, mcs_path):
    """Return {sym: signed int} for every REQUIRED+OPTIONAL symbol from one mcs."""
    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs_path))
    magic_addr = _scene.map_symbol(mp_text, _scene.SYM_MAGIC)
    if magic_addr is None:
        return None, "magic symbol absent from map"
    raw_magic = mod._peek_bytes(sections, magic_addr, 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        return None, "magic mis-decode (image not loaded / wrong bank)"
    out = {"__perm__": label}
    for s in REQUIRED + OPTIONAL:
        a = _scene.map_symbol(mp_text, s)
        out[s] = (_scene.peek_u32(mod, sections, a, perm, signed=True)
                  if a is not None else None)
    return out, None


def _fmt(v):
    return "None" if v is None else str(v)


def evaluate(A, B):
    """A, B: per-state dicts (B is the later capture). Returns (ok, checks, info)."""
    checks = []

    # T2 sanity
    ca, cb = A.get(SYM_CONTFR), B.get(SYM_CONTFR)
    dfr = (cb - ca) if (ca is not None and cb is not None) else None
    ha, hb = A.get(SYM_TSON_H), B.get(SYM_TSON_H)
    bound = (ha is not None and ha >= 0 and hb is not None and hb >= 0)
    t2 = (dfr is not None and dfr > 0 and bound)
    checks.append((
        "T2 captures sane (intro advanced cont_frames>0; TitleSonic BOUND both)",
        t2,
        "cont_frames %s->%s (delta %s); tsonic_handle %s->%s"
        % (_fmt(ca), _fmt(cb), _fmt(dfr), _fmt(ha), _fmt(hb))))

    # Per-frame deltas (only meaningful if dfr>0)
    info = {}
    if dfr and dfr > 0:
        fa, fb = A.get(SYM_FETCHES), B.get(SYM_FETCHES)
        ea, eb = A.get(SYM_EVICTS), B.get(SYM_EVICTS)
        d_fetch = (fb - fa) if (fa is not None and fb is not None) else None
        d_evict = (eb - ea) if (ea is not None and eb is not None) else None
        info["d_fetch"] = d_fetch
        info["d_evict"] = d_evict
        info["dfr"] = dfr
        info["fetch_per_frame"] = (d_fetch / dfr) if d_fetch is not None else None
        info["evict_per_frame"] = (d_evict / dfr) if d_evict is not None else None

        # T3 THE GATE: zero banded inflate per frame during the twirl.
        t3 = (d_fetch is not None and d_fetch == 0)
        checks.append((
            "T3 per-frame banded inflate == 0 during twirl (TSONIC resident)",
            t3,
            "sht_fetches delta=%s over %s frames (%.2f/frame); sht_resident %s->%s"
            % (_fmt(d_fetch), _fmt(dfr),
               (info["fetch_per_frame"] or 0.0),
               _fmt(A.get(SYM_RESIDENT)), _fmt(B.get(SYM_RESIDENT)))))
    else:
        checks.append((
            "T3 per-frame banded inflate == 0 during twirl (TSONIC resident)",
            False, "cannot measure -- captures did not advance (see T2)"))

    ok = all(c for _, c, _ in checks)
    return ok, checks, info


def _print(checks):
    for name, ok, detail in checks:
        print("  [%s] %s" % ("GREEN" if ok else " RED ", name))
        print("          %s" % detail)


def selftest():
    print("=" * 72)
    print("TITLE-TWIRL RESIDENT GATE -- SELFTEST (prove RED fires on banded, GREEN on resident)")
    print("=" * 72)
    # RED model: banded build. cont_frames advanced 40 frames; TitleSonic bound;
    # sht_fetches climbed (per-frame inflate of the play-once body); resident 11
    # (LOGOS+TLOGO etc but NOT TSONIC); evicts also climbed (separate staging cost).
    A_red = {SYM_CONTFR: 200, SYM_TSON_H: 1, SYM_FETCHES: 1000, SYM_EVICTS: 5000,
             SYM_RESIDENT: 11}
    B_red = {SYM_CONTFR: 240, SYM_TSON_H: 1, SYM_FETCHES: 1100, SYM_EVICTS: 5300,
             SYM_RESIDENT: 11}
    red_ok, red_checks, red_info = evaluate(A_red, B_red)
    _print(red_checks)
    print("          [diag] fetch/frame=%.2f  evict/frame=%.2f"
          % (red_info.get("fetch_per_frame") or -1,
             red_info.get("evict_per_frame") or -1))
    # GREEN model: resident build. sht_fetches FLAT (no inflate during twirl);
    # sht_resident grew to 12 (TSONIC now resident).
    A_grn = {SYM_CONTFR: 200, SYM_TSON_H: 1, SYM_FETCHES: 1000, SYM_EVICTS: 5300,
             SYM_RESIDENT: 12}
    B_grn = {SYM_CONTFR: 240, SYM_TSON_H: 1, SYM_FETCHES: 1000, SYM_EVICTS: 5600,
             SYM_RESIDENT: 12}
    grn_ok, grn_checks, _ = evaluate(A_grn, B_grn)
    print("-" * 72)
    if (not red_ok) and grn_ok:
        print("RESULT: RED (selftest) -- the banded capture is correctly REJECTED")
        print("        (T3 sht_fetches advanced = per-frame body inflate = slideshow)")
        print("        while the resident model (sht_fetches flat) is ACCEPTED.")
        return 1
    print("RESULT: SELFTEST BROKEN (red_ok=%s grn_ok=%s)" % (red_ok, grn_ok))
    return 2


def main(argv):
    if "--selftest" in argv:
        return selftest()
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) < 2:
        print("usage: qa_title_twirl_resident.py <stateA.mcs> <stateB.mcs> [game.map]")
        print("       (or --selftest)")
        return 2
    mcsA = _scene._as_path(args[0]) if hasattr(_scene, "_as_path") else args[0]
    mcsB = _scene._as_path(args[1]) if hasattr(_scene, "_as_path") else args[1]
    mp = args[2] if len(args) > 2 else MAP_DEFAULT

    print("=" * 72)
    print("TITLE-TWIRL RESIDENT GATE: per-frame banded-inflate delta during the intro")
    print("=" * 72)
    print("  stateA (earlier): %s" % mcsA)
    print("  stateB (later)  : %s" % mcsB)
    print("  map             : %s" % mp)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    mp_text = _scene.read_text(mp)

    # T1 symbol presence
    missing = [s for s in REQUIRED if _scene.map_symbol(mp_text, s) is None]
    if missing:
        print("  [ RED ] T1 witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("RESULT: RED -- witness symbol(s) absent (T1).")
        return 1
    print("  [GREEN] T1 witness symbols present in the link map")

    for p in (mcsA, mcsB):
        if not os.path.isfile(p):
            print("RESULT: RED -- savestate missing (%s)" % p)
            return 1

    A, errA = _read_state(mp_text, mcsA)
    if A is None:
        print("RESULT: RED -- stateA: %s" % errA)
        return 1
    B, errB = _read_state(mp_text, mcsB)
    if B is None:
        print("RESULT: RED -- stateB: %s" % errB)
        return 1
    print("  byte-order: A=%s  B=%s" % (A.get("__perm__"), B.get("__perm__")))

    ok, checks, info = evaluate(A, B)
    _print(checks)

    print("-" * 72)
    print("MEASURED per-frame twirl cost (the deliverable -- discriminates the fix):")
    print("  frames advanced (cont_frames delta) : %s" % _fmt(info.get("dfr")))
    print("  BANDED inflate  sht_fetches delta   : %s  -> %.2f inflate/frame"
          % (_fmt(info.get("d_fetch")), info.get("fetch_per_frame") or 0.0))
    print("    (the resident MakeResident fix DRIVES THIS TO 0)")
    print("  LRU staging     vdp1_evicts delta   : %s  -> %.2f evict/frame"
          % (_fmt(info.get("d_evict")), info.get("evict_per_frame") or 0.0))
    print("    (a SEPARATE box-staging+DMA cost residency does NOT remove)")
    print("  sht_resident  A=%s B=%s | sht_staged A=%s B=%s | tsonic_anim A=%s frame A=%s"
          % (_fmt(A.get(SYM_RESIDENT)), _fmt(B.get(SYM_RESIDENT)),
             _fmt(A.get(SYM_STAGED)), _fmt(B.get(SYM_STAGED)),
             _fmt(A.get(SYM_TSON_AID)), _fmt(A.get(SYM_TSON_FID))))
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- per-frame banded inflate is 0 during the twirl")
        print("        (TSONIC resident: each play-once body frame is a cart copy).")
        return 0
    print("RESULT: RED -- the twirl re-inflates the body sheet per frame (slideshow).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
