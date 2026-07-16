#!/usr/bin/env python3
"""qa_ghz_fetch.py -- #243 chain-GHZ per-frame banded-fetch gate + attribution
(Mednafen two-capture, emulated-time -- the BINDING measurement method).

WHY NOT LIVE RA: RetroArch wall-clock speed numbers lie by the throttle factor
(fast-forward inflated a prior chain-GHZ reading to a FALSE "60.2 GREEN" --
memory aiz-cd-fix-ghz-regression-bisect.md, 2026-07-16). This gate uses two
Mednafen savestates and computes every rate over EMULATED time
(d(p6_perf_vbl_count)/60 seconds); ratio counters (fetches per rendered frame)
are throttle-independent.

WHAT IT MEASURES (GHZ play phase; SaveFrame defaults 380/395 are past the
~40 s GHZ TitleCard -- never measure during the card):
  fetches/frame = d(p6_w_sht_fetches) / d(p6_w_cont_frames)   GREEN <= floor
  tick/s        = d(p6_w_tick_frames) / (d(p6_perf_vbl_count)/60)
  render fps    = d(p6_w_cont_frames) / (d(p6_perf_vbl_count)/60)

ATTRIBUTION (#243 instrumentation, chain flavor only): p6_w_fetch_hist[32]
(per-STORE-slot banded inflates, SaturnSheet.cpp FetchRect) is delta'd between
the captures and each slot named via p6_w_sht_slothash0[] (engine GEN_HASH_MD5
word0 == md5(path).digest()[:4] little-endian -- tools/build_anim_pack.py
engine_hash convention, MEASURED).

Usage:
  py -3 tools/qa_ghz_fetch.py --capture              # fresh Mednafen captures
  py -3 tools/qa_ghz_fetch.py --a a.mcs --b b.mcs    # pre-captured states
Exit 0 = GREEN, 1 = RED, 2 = harness error.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_ROOT = _HERE.parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


qa_trace = _load("qa_trace", "qa_trace.py")
mcs_extract = _load("mcs_extract", "mcs_extract.py")

# Every engine sheet path the chain ever stages (p6_io_main.cpp staging sites).
SHEET_PATHS = [
    "Logos/Logos.gif", "Title/Logo.gif", "Title/Sonic.gif", "Title/BG.gif",
    "Title/Electricity1.gif", "Title/Electricity2.gif",
    "UI/MainIcons.gif", "UI/TextEN.gif",
    "AIZ/Objects.gif", "Cutscene/HBH.gif", "Cutscene/Players.gif",
    "GHZCutscene/Objects.gif", "Global/PhantomRuby.gif",
    "Players/Sonic1.gif", "Players/Sonic2.gif", "Players/Sonic3.gif",
    "Players/Tails1.gif",
    "Global/Items.gif", "Global/Display.gif", "Global/Shields.gif",
    "Global/Objects.gif", "GHZ/Objects.gif",
    "Global/Explosions.gif", "Global/Animals.gif",
]


def hash_word0(path: str) -> int:
    """Engine GEN_HASH_MD5 in-memory uint32 word0 (build_anim_pack.engine_hash)."""
    return int.from_bytes(hashlib.md5(path.encode()).digest()[:4], "little")


HASH0_TO_NAME = {hash_word0(p): p for p in SHEET_PATHS}


def folder_str(rd, addr) -> str | None:
    """currentSceneFolder from a savestate: WRAM-H bytes are PAIR-SWAPPED in
    the .mcs (skill gotcha #10) -- unswap before decoding."""
    raw = mcs_extract._peek_bytes(rd.sections, addr, 16)
    if raw is None:
        return None
    fixed = bytes(raw[i ^ 1] for i in range(16))
    return fixed.split(b"\0")[0].decode(errors="replace")


def capture(cue: str, save_frame: float, out: Path) -> None:
    cmd = ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass",
           "-File", str(_HERE / "qa_savestate.ps1"),
           "-Cue", cue, "-SaveFrame", str(save_frame), "-Out", str(out)]
    print("[qa_ghz_fetch] capturing %s at t=%ss ..." % (out.name, save_frame))
    r = subprocess.run(cmd, cwd=str(_ROOT))
    if r.returncode != 0 or not out.exists():
        raise RuntimeError("capture failed for %s (rc=%d)" % (out, r.returncode))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--capture", action="store_true",
                    help="run two fresh Mednafen captures (serial, host quiet)")
    ap.add_argument("--cue", default="game.cue")
    ap.add_argument("--frame-a", type=float, default=380.0)
    ap.add_argument("--frame-b", type=float, default=395.0)
    ap.add_argument("--a", default=str(_ROOT / "_qa_fetch_a.mcs"))
    ap.add_argument("--b", default=str(_ROOT / "_qa_fetch_b.mcs"))
    ap.add_argument("--floor", type=float, default=0.5,
                    help="max banded inflates per rendered frame for GREEN")
    ap.add_argument("--map", default=str(_ROOT / "game.map"))
    args = ap.parse_args()

    pa, pb = Path(args.a), Path(args.b)
    if args.capture:
        capture(args.cue, args.frame_a, pa)
        capture(args.cue, args.frame_b, pb)
    if not pa.exists() or not pb.exists():
        print("[HARNESS] state files missing (use --capture)", file=sys.stderr)
        return 2

    map_text = Path(args.map).read_text(errors="replace")
    ra = qa_trace.Reader(False, str(pa), None, None)
    rb = qa_trace.Reader(False, str(pb), None, None)

    def sym(n):
        return ra.sym(map_text, n)

    a_csf = sym("RSDK::currentSceneFolder")
    a_cont = sym("p6_w_cont_frames")
    a_tick = sym("p6_w_tick_frames")
    a_vbl = sym("p6_perf_vbl_count")
    a_fetch = sym("p6_w_sht_fetches")
    a_hist = sym("p6_w_fetch_hist")
    a_h0 = sym("p6_w_sht_slothash0")
    a_frda = sym("p6_w_frd_active")
    a_frdm = sym("p6_w_frd_misses")
    for nm, av in [("currentSceneFolder", a_csf), ("p6_w_cont_frames", a_cont),
                   ("p6_w_tick_frames", a_tick), ("p6_perf_vbl_count", a_vbl),
                   ("p6_w_sht_fetches", a_fetch)]:
        if av is None:
            print("[HARNESS] missing symbol:", nm, file=sys.stderr)
            return 2

    fa, fb = folder_str(ra, a_csf), folder_str(rb, a_csf)
    conts = (ra.r32(a_cont), rb.r32(a_cont))
    ticks = (ra.r32(a_tick), rb.r32(a_tick))
    vbls = (ra.r32(a_vbl), rb.r32(a_vbl))
    fetches = (ra.r32(a_fetch), rb.r32(a_fetch))
    if fa != "GHZ" or fb != "GHZ":
        print("[HARNESS] not at GHZ play in both captures (A=%r B=%r) -- "
              "adjust --frame-a/--frame-b" % (fa, fb), file=sys.stderr)
        return 2
    if None in conts or None in ticks or None in vbls or None in fetches:
        print("[HARNESS] counter read failed", file=sys.stderr)
        return 2
    d_cont = conts[1] - conts[0]
    d_tick = ticks[1] - ticks[0]
    d_vbl = vbls[1] - vbls[0]
    d_fetch = fetches[1] - fetches[0]
    if d_cont <= 0 or d_vbl <= 0:
        print("[HARNESS] window did not advance (d_cont=%s d_vbl=%s) -- "
              "captures too close / frozen scene" % (d_cont, d_vbl),
              file=sys.stderr)
        return 2

    emu_s = d_vbl / 60.0
    fetch_pf = d_fetch / d_cont
    tick_ps = d_tick / emu_s
    fps = d_cont / emu_s

    print("=" * 72)
    print("#243 CHAIN-GHZ BANDED-FETCH GATE (Mednafen two-capture, %.1f emu-s)"
          % emu_s)
    print("  folder A/B            = %s / %s" % (fa, fb))
    print("  fetches / frame       = %.2f   (floor %.2f)  [d_fetch=%d d_cont=%d]"
          % (fetch_pf, args.floor, d_fetch, d_cont))
    print("  game speed            = %.1f tick/s (%.0f%% realtime)"
          % (tick_ps, tick_ps / 60.0 * 100.0))
    print("  render fps            = %.1f" % fps)
    if a_frda:
        print("  frd_active A/B        = %s / %s" % (ra.r32(a_frda), rb.r32(a_frda)))
    if a_frdm:
        print("  frd_misses A/B        = %s / %s" % (ra.r32(a_frdm), rb.r32(a_frdm)))
    if a_hist and a_h0:
        print("  -- attribution: per-store-slot banded inflates over the window --")
        any_row = False
        for i in range(32):
            ha = ra.r32(a_hist + 4 * i)
            hb = rb.r32(a_hist + 4 * i)
            if ha is None or hb is None:
                continue
            d = hb - ha
            if d == 0 and (hb or 0) == 0:
                continue
            h0 = rb.r32(a_h0 + 4 * i) or 0
            name = HASH0_TO_NAME.get(h0 & 0xFFFFFFFF, "?hash0=0x%08X" % (h0 & 0xFFFFFFFF))
            print("    slot %2d  %-26s  +%d over window (%.2f/frame; total %d)"
                  % (i, name, d, d / d_cont, hb))
            any_row = True
        if not any_row:
            print("    (no slot fetched in the window)")
    else:
        print("  [attribution symbols absent -- pre-instrumentation build]")
    print("=" * 72)

    if fetch_pf > args.floor:
        print("RED: %.2f banded inflates/frame > floor %.2f" % (fetch_pf, args.floor))
        return 1
    print("GREEN: %.2f banded inflates/frame <= %.2f (tick/s=%.1f fps=%.1f "
          "reported honestly -- render wall is a separate axis)"
          % (fetch_pf, args.floor, tick_ps, fps))
    return 0


if __name__ == "__main__":
    sys.exit(main())
