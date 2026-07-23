"""GHZ pink-flash gate (RED/GREEN). Measures the magenta transparent-key flash on
the GHZ background plane from live RA raw-core-framebuffer screenshots.

MECHANISM (measured 2026-07-22): the GHZ BG plane intermittently mis-banks under
motion (SGL slScrAutoDisp B1-char drop, gotcha #7) and exposes RSDK's magenta
transparent-key (CRAM 0xFC1F -> displayed RGB(224,0,224)). ~13-15% of GHZ frames
DURING MOTION; 0% on a parked camera (the demagenta keeps a static sky clean).

USAGE:
  1. boot the chain in RA windowed (observe_override -> raw screenshots to _shots/)
  2. python tools/qa_ghz_pink.py            # wait for GHZ, burst, scan, verdict
  3. python tools/qa_ghz_pink.py --analyze  # re-scan the existing _shots only

RED  = pink frames > THRESH over the burst (bug present)
GREEN = pink frames <= THRESH (fixed)
"""
import importlib.util, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent
SHOTS = ROOT / "_shots"
NFRAMES = 45          # ~45s of 1/sec capture -- enough to catch a 13% transient
THRESH = 1            # allow 1 borderline frame; >1 = RED


def ispink(r, g, b):
    return r >= 170 and b >= 140 and g <= min(r, b) - 40


def scan(files):
    from PIL import Image
    pink = 0
    for f in files:
        try:
            im = Image.open(f).convert("RGB")
        except Exception:
            continue
        W, H = im.size
        px = im.load()
        cnt = 0
        for y in range(0, H, 2):
            for x in range(0, W, 2):
                r, g, b = px[x, y]
                if ispink(r, g, b):
                    cnt += 1
        if cnt > 15:
            pink += 1
            print("  PINK %s  magenta_px=%d" % (f.name, cnt))
    return pink


def main():
    if "--analyze" in sys.argv:
        shots = sorted(SHOTS.glob("*.png"), key=lambda p: p.stat().st_mtime)[-NFRAMES:]
        pink = scan(shots)
        n = len(shots)
    else:
        spec = importlib.util.spec_from_file_location("qa_netmem", HERE / "qa_netmem.py")
        m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m)
        mt = (ROOT / "game.map").read_text(errors="replace")

        def sym(name):
            for line in mt.splitlines():
                p = line.split()
                if len(p) >= 2 and p[-1] == name and p[0].startswith("0x"):
                    return int(p[0], 16)
            return None

        a_fol = sym("RSDK::currentSceneFolder")
        rw = None
        for _ in range(150):
            try:
                rw = m.RetroMem(); rw.read_saturn(a_fol, 4); break
            except Exception:
                time.sleep(2)
        if not rw:
            print("RA netmem never came up"); return 2

        def folder():
            try:
                return rw.read_saturn(a_fol, 16).split(b"\0")[0].decode("ascii", "replace")
            except Exception:
                return "?"

        t0 = time.time()
        while time.time() - t0 < 300 and folder() != "GHZ":
            time.sleep(2)
        if folder() != "GHZ":
            print("never reached GHZ"); return 2
        print("at GHZ, bursting %d frames..." % NFRAMES, flush=True)
        time.sleep(6)  # let motion establish
        t_lo = time.time()
        for _ in range(NFRAMES):
            rw.sock.sendto(b"SCREENSHOT\n", rw.addr)
            time.sleep(1.05)
        shots = [p for p in SHOTS.glob("*.png") if p.stat().st_mtime >= t_lo - 1]
        shots = sorted(shots, key=lambda p: p.stat().st_mtime)
        pink = scan(shots)
        n = len(shots)

    verdict = "GREEN" if pink <= THRESH else "RED"
    print("=== GHZ PINK: %d / %d frames magenta -> %s (thresh %d) ===" % (pink, n, verdict, THRESH))
    return 0 if verdict == "GREEN" else 1


if __name__ == "__main__":
    sys.exit(main())
