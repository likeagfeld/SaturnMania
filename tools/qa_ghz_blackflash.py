"""GHZ black-flash gate. Analog of qa_ghz_pink.py but detects the sky PLANE DROP
exposing BLACK (back-screen) instead of magenta. The 4bpp pink fix removed the
magenta transparent-key, but the user reports a flashing transparent-black in the
background under motion -- i.e. the sky NBG0 plane still intermittently drops its
VDP2 fetch and reveals black.

DETECTION: the GHZ sky occupies the TOP band of the frame. On a healthy frame that
band is sky-blue (high B, mid G, low-mid R). On a dropped frame the band goes near-
black. We measure the mean luminance of the top 1/3 of the frame and count frames
where it collapses below BLACK_LUMA while a healthy reference frame is bright.

USAGE:
  python tools/qa_ghz_blackflash.py            # wait for GHZ, burst 45, scan
  python tools/qa_ghz_blackflash.py --analyze  # re-scan existing _shots
"""
import importlib.util, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HERE = Path(__file__).resolve().parent
SHOTS = ROOT / "_shots"
NFRAMES = 45
BLACK_LUMA = 30       # top-band mean luma below this = sky dropped to black
THRESH = 1


def topband_stats(f):
    from PIL import Image
    try:
        im = Image.open(f).convert("RGB")
    except Exception:
        return None
    W, H = im.size
    px = im.load()
    tot = 0
    n = 0
    blackpx = 0
    for y in range(0, H // 3, 2):
        for x in range(0, W, 2):
            r, g, b = px[x, y]
            luma = (r * 30 + g * 59 + b * 11) // 100
            tot += luma
            n += 1
            if r < 24 and g < 24 and b < 24:
                blackpx += 1
    if n == 0:
        return None
    return tot / n, blackpx, n


def scan(files):
    black = 0
    for f in files:
        st = topband_stats(f)
        if st is None:
            continue
        mean, blackpx, n = st
        frac = blackpx / n
        # A dropped-sky frame: top band mean luma collapses AND a large fraction
        # of the band is near-black.
        if mean < BLACK_LUMA and frac > 0.5:
            black += 1
            print("  BLACK %s  topband_luma=%.1f black_frac=%.2f" % (f.name, mean, frac))
    return black


def main():
    if "--analyze" in sys.argv:
        shots = sorted(SHOTS.glob("*.png"), key=lambda p: p.stat().st_mtime)[-NFRAMES:]
        black = scan(shots); n = len(shots)
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
        time.sleep(6)
        t_lo = time.time()
        for _ in range(NFRAMES):
            rw.sock.sendto(b"SCREENSHOT\n", rw.addr)
            time.sleep(1.05)
        shots = [p for p in SHOTS.glob("*.png") if p.stat().st_mtime >= t_lo - 1]
        shots = sorted(shots, key=lambda p: p.stat().st_mtime)
        black = scan(shots); n = len(shots)

    verdict = "GREEN" if black <= THRESH else "RED"
    print("=== GHZ BLACKFLASH: %d / %d frames sky-dropped-to-black -> %s (thresh %d) ==="
          % (black, n, verdict, THRESH))
    return 0 if verdict == "GREEN" else 1


if __name__ == "__main__":
    sys.exit(main())
