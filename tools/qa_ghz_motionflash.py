"""GHZ black-flash MOTION correlator (RED gate + attribution, 2026-07-23).

WHY THIS EXISTS: qa_ghz_blackflash.py samples the parked landing-settle window and
went GREEN while the user still saw black flashes DURING GAMEPLAY MOTION -- the same
parked-vs-motion trap the pink gate had (0/80 parked vs 13% moving). This tool
captures UNDER USER-DRIVEN MOTION: it bursts raw core-framebuffer screenshots at
max rate while sampling the live witnesses (camera x, FG vblank-DMA count, frame
counter, compute FRT, VDP1 cmds/drops/evicts) on every shot, then attributes each
black/dark frame to the witness deltas in its sample window.

USAGE (user drives!):
    python tools/qa_ghz_motionflash.py --secs 45
  Start it, then the USER plays forward through Green Hill (crossing FG bands).
  Output: per-shot log _motionflash_log.txt + verdict. RED = any near-black frame
  while cam_x is MOVING. The correlation column shows which counters jumped in the
  same window (fg_dma burst = band re-inflate DMA; cyc spike = compute stall; VDP1
  drops = sprite-list starvation).

Addresses are read fresh from game.map at startup (never hardcode across builds).
"""
import argparse
import importlib.util
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent
SHOTS = ROOT / "_shots"

WITNESSES = [  # (label, symbol)
    ("cam_x",   "p6_w_cam_x"),
    ("frames",  "p6_w_cont_frames"),
    ("fg_dma",  "p6_w_fg_dma"),
    ("cyc_tot", "p6_w_perf_cyc_total"),
    ("v1_cmds", "p6_w_vdp1_cmds"),
    ("v1_drop", "p6_w_vdp1_drops"),
    ("v1_evic", "p6_w_vdp1_evicts"),
    ("fg4bpp",  "p6_ghz_fg_4bpp"),
    ("ghzchr",  "p6_w_ghzfg_chr"),
]


def load_netmem():
    spec = importlib.util.spec_from_file_location("qa_netmem", HERE / "qa_netmem.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def sym_table(names):
    mt = (ROOT / "game.map").read_text(errors="replace")
    out = {}
    for line in mt.splitlines():
        p = line.split()
        if len(p) >= 2 and p[0].startswith("0x") and p[-1] in names:
            out[p[-1]] = int(p[0], 16)
    return out


def frame_darkness(png):
    """(black_fraction, mean_luma) downsampled; robust cheap dark metric."""
    from PIL import Image
    im = Image.open(png).convert("RGB")
    W, H = im.size
    px = im.load()
    n = 0
    dark = 0
    luma = 0
    for y in range(0, H, 4):
        for x in range(0, W, 4):
            r, g, b = px[x, y]
            l = (r * 3 + g * 6 + b) // 10
            luma += l
            n += 1
            if l < 24:
                dark += 1
    return dark / max(1, n), luma / max(1, n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--secs", type=float, default=45.0)
    ap.add_argument("--analyze", action="store_true",
                    help="re-analyze existing _motionflash_log.txt + shots only")
    args = ap.parse_args()

    if not args.analyze:
        m = load_netmem()
        rw = m.RetroMem()
        syms = sym_table({s for _, s in WITNESSES} | {"RSDK::currentSceneFolder"})
        missing = [s for _, s in WITNESSES if s not in syms]
        if missing:
            print("MISSING SYMBOLS (stale map?):", missing)
            return 2
        a_fol = syms["RSDK::currentSceneFolder"]

        def folder():
            try:
                return rw.read_saturn(a_fol, 16).split(b"\0")[0].decode("ascii", "replace")
            except Exception:
                return "?"

        def i32(a):
            return int.from_bytes(rw.read_saturn(a, 4), "big")

        if folder() != "GHZ":
            print("Not at GHZ (folder=%r). Boot/land first." % folder())
            return 2

        log = open(ROOT / "_motionflash_log.txt", "w")
        log.write("ts " + " ".join(l for l, _ in WITNESSES) + "\n")
        print("CAPTURING %.0fs -- DRIVE FORWARD NOW (cross bands)..." % args.secs,
              flush=True)
        t0 = time.time()
        while time.time() - t0 < args.secs:
            rw.sock.sendto(b"SCREENSHOT\n", rw.addr)
            vals = []
            for _, s in WITNESSES:
                try:
                    vals.append(i32(syms[s]))
                except Exception:
                    vals.append(-1)
            log.write(time.strftime("%H%M%S")
                      + " " + " ".join(str(v) for v in vals) + "\n")
            log.flush()
            time.sleep(0.30)   # ~3 samples/s; RA writes at most 1 shot/s (ts filename)
        log.close()
        print("capture done")

    # ---- analysis: join shots (by HHMMSS) with the witness log ----
    rows = []
    for line in (ROOT / "_motionflash_log.txt").read_text().splitlines()[1:]:
        p = line.split()
        if len(p) == 1 + len(WITNESSES):
            rows.append((p[0], [int(v) for v in p[1:]]))
    bylab = {ts: v for ts, v in rows}

    shots = sorted(SHOTS.glob("*.png"), key=lambda p: p.stat().st_mtime)
    t_first = rows[0][0] if rows else None
    dark_frames = []
    prev = None
    print("ts        blackfrac luma  d_cam d_dma d_cyc d_drop d_evic")
    for p in shots:
        hh = p.stem.split("-")[-1]
        if hh not in bylab:
            continue
        bf, lu = frame_darkness(p)
        v = bylab[hh]
        if prev is not None:
            d = [v[i] - prev[i] for i in range(len(v))]
        else:
            d = [0] * len(v)
        prev = v
        moving = abs(d[0]) > 2   # cam_x delta
        mark = ""
        if bf > 0.60:
            mark = " <== BLACK" + (" (MOVING)" if moving else " (parked)")
            dark_frames.append((p.name, bf, moving, d))
        print("%s  %.2f     %3d   %5d %4d %8d %4d %5d%s"
              % (hh, bf, lu, d[0], d[2], d[3], d[5], d[6], mark))

    moving_black = [f for f in dark_frames if f[2]]
    verdict = "RED" if moving_black else "GREEN"
    print("=== MOTION BLACK FRAMES: %d (of %d dark total) -> %s ==="
          % (len(moving_black), len(dark_frames), verdict))
    for name, bf, mv, d in dark_frames:
        print("  %s black=%.2f moving=%s d_cam=%d d_fgdma=%d d_cyc=%d d_drop=%d d_evic=%d"
              % (name, bf, mv, d[0], d[2], d[3], d[5], d[6]))
    return 0 if verdict == "GREEN" else 1


if __name__ == "__main__":
    sys.exit(main())
