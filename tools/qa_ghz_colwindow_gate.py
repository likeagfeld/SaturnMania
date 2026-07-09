#!/usr/bin/env python3
"""qa_ghz_colwindow_gate.py - Task #180 step 3d (toroidal-slide streamer).

HOST-VERIFIABLE reference + gate for the column-window collision streamer.

The decomp keeps the entire collision TileLayer resident; Saturn cannot
(GHZ1 = 1024x128 tiles x 2 layers x 2 B = 512 KB, far past the LWRAM carve).
#180 step 3d keeps only a W-column, full-height window resident in LWRAM and
slides it as the camera advances. cd/GHZ1COL.BIN is stored COLUMN-MAJOR
('GCO2') so one world column is a single contiguous ysize*2-byte run -> a
fresh column streams into its wrap slot (worldCol % W) as ONE contiguous copy.

This gate is the executable REFERENCE the C streamer (src/rsdk/colwindow.c)
must mirror line-for-line. It:
  P1  loads cd/GHZ1COL.BIN, asserts column-major 'GCO2' + plausible dims.
  P2  sweeps a monotonic left->right camera over the WHOLE level, advancing
      the toroidal window, and asserts the window fetch returns the SAME
      entry as the full-grid reference for every visible (col,row) - 0
      mismatches required. Also reports the streamed-bytes-per-advance.
  P3  TEETH self-test: a deliberately off-by-one slot map (s=(col+1)%W) MUST
      produce mismatches against the same reference, proving P2 is not
      vacuously green.

No Saturn/C dependency - plain Python over the real shipped asset, so it runs
in the host verify_done pipeline with no emulator.

The window-fetch contract the C mirrors EXACTLY:
    resident band  = [base, base + W)             (base = leftmost world col)
    slot(col)      = col % W                       (toroidal wrap)
    fetch(col,row) = OUT_OF_BAND (0xFFFF) if col < base or col >= base+W
                     else window[slot(col)][row]   (row in [0, ysize))
    advance(base'): for col in [old_base+W, base'+W): stream col -> slot(col)
"""
import argparse
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COLBIN = os.path.join(ROOT, "cd", "GHZ1COL.BIN")

OUT_OF_BAND = 0xFFFF

# Window geometry (mirrored by the C carve in src/rsdk/storage.c / colwindow.c).
#   320 px screen = 20 tiles visible; LEFT_MARGIN columns of look-behind, and
#   W - VIS_TILES - LEFT_MARGIN columns of look-ahead prefetch.
#   48 cols x 128 rows x 2 layers x 2 B = 24576 B resident (fits the 64 KB
#   carve alongside the 33452 B GHZ1MASK.BIN).
WINDOW_W = 48
VIS_TILES = 20
LEFT_MARGIN = 8


def load_colmajor(path):
    """Parse the column-major GHZ1COL.BIN. Accepts 'GCO2' (uncompressed) and
    'GCO3' (#180 step 4c block-DEFLATE). Returns a list of layers, each
    {id,xs,ys,wshift,cols} where cols[tx] is the ysize-long entry list for
    world column tx (the contiguous column run)."""
    with open(path, "rb") as f:
        d = f.read()
    magic = d[:4]
    if magic == b"GCO3":
        return _load_colmajor_gco3(d)
    if magic != b"GCO2":
        raise SystemExit(
            "P1 RED: GHZ1COL.BIN magic %r (expected 'GCO2' or 'GCO3'); "
            "rebuild via tools/build_collayout.py" % magic)
    nlayers, tsz = struct.unpack_from(">HH", d, 4)
    hdr = 8
    layers = []
    data_off = hdr + nlayers * 8
    for i in range(nlayers):
        lid, xs, ys, wshift = struct.unpack_from(">HHHH", d, hdr + i * 8)
        n = xs * ys
        raw = struct.unpack_from(">%dH" % n, d, data_off)
        data_off += n * 2
        cols = [list(raw[tx * ys:tx * ys + ys]) for tx in range(xs)]
        layers.append({"id": lid, "xs": xs, "ys": ys, "wshift": wshift,
                       "cols": cols})
    return layers, tsz


def _load_colmajor_gco3(d):
    """Decode 'GCO3' block-DEFLATE column-major into the same per-column
    structure load_colmajor returns. Inflate every block (raw DEFLATE,
    wbits=-15); the concatenation is the full column-major u16 BE run."""
    import zlib
    nlayers, tsz, block_cols, _res = struct.unpack_from(">HHHH", d, 4)
    off = 12
    descs = []
    for _ in range(nlayers):
        lid, xs, ys, wshift, nblk, _pad = struct.unpack_from(">HHHHHH", d, off)
        descs.append((lid, xs, ys, wshift, nblk))
        off += 12
    tables = []
    for (_lid, _xs, _ys, _w, nblk) in descs:
        offs = struct.unpack_from(">%dI" % (nblk + 1), d, off)
        tables.append(offs)
        off += 4 * (nblk + 1)
    layers = []
    for li, (lid, xs, ys, wshift, nblk) in enumerate(descs):
        cm = bytearray()
        offs = tables[li]
        for b in range(nblk):
            cm += zlib.decompress(d[offs[b]:offs[b + 1]], -15)
        raw = struct.unpack(">%dH" % (xs * ys), bytes(cm))
        cols = [list(raw[tx * ys:tx * ys + ys]) for tx in range(xs)]
        layers.append({"id": lid, "xs": xs, "ys": ys, "wshift": wshift,
                       "cols": cols})
    return layers, tsz


class ColWindow:
    """Toroidal-slide column window over ONE layer. Mirrors the C streamer."""

    def __init__(self, cols, xs, ys, w):
        self.cols = cols          # full-grid reference (column-major)
        self.xs = xs
        self.ys = ys
        self.w = w
        self.win = [[OUT_OF_BAND] * ys for _ in range(w)]  # slot -> column data
        self.loaded = [-1] * w    # slot -> resident world col (-1 = empty)
        self.base = 0
        self.streamed_cols = 0    # total columns copied (CD/LWRAM transfers)

    def stream_column(self, c):
        if c < 0 or c >= self.xs:
            return
        s = c % self.w
        self.win[s] = self.cols[c][:]   # one contiguous ysize*2-byte copy
        self.loaded[s] = c
        self.streamed_cols += 1

    def ensure_band(self, base):
        """Advance the resident band to [base, base+W), streaming only the
        newly-entered columns (initial fill streams the whole band once)."""
        if base < 0:
            base = 0
        if base > self.xs - self.w:
            base = self.xs - self.w
        for c in range(base, base + self.w):
            if self.loaded[c % self.w] != c:
                self.stream_column(c)
        self.base = base

    def fetch(self, c, row, slot_bias=0):
        """Return the resident entry for (c,row), or OUT_OF_BAND if c is
        outside the band. slot_bias != 0 injects the P3 teeth bug."""
        if c < self.base or c >= self.base + self.w:
            return OUT_OF_BAND
        s = (c + slot_bias) % self.w
        return self.win[s][row]


def sweep(layers, w, vis, left_margin, slot_bias=0):
    """Monotonic left->right camera sweep over the whole level. Returns
    (mismatches, total_streamed_cols, advances)."""
    xs = layers[0]["xs"]
    ys = layers[0]["ys"]
    wins = [ColWindow(l["cols"], xs, ys, w) for l in layers]
    mismatches = 0
    advances = 0
    last_base = -1
    cam_hi = xs - vis
    for cam in range(0, cam_hi + 1):
        desired_base = cam - left_margin
        if desired_base < 0:
            desired_base = 0
        if desired_base > xs - w:
            desired_base = xs - w
        for win in wins:
            win.ensure_band(desired_base)
        if desired_base != last_base:
            advances += 1
            last_base = desired_base
        # verify every visible (col,row) against the full-grid reference
        for li, win in enumerate(wins):
            ref = layers[li]["cols"]
            for c in range(cam, cam + vis):
                got = win.fetch(c, 0, slot_bias)  # cheap band guard probe
                if got is OUT_OF_BAND:
                    mismatches += 1
                    continue
                col_ref = ref[c]
                s = (c + slot_bias) % w
                col_got = win.win[s]
                if col_got != col_ref:
                    mismatches += 1
    total = sum(win.streamed_cols for win in wins)
    return mismatches, total, advances


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--col", default=COLBIN)
    ap.add_argument("--selftest", action="store_true",
                    help="run only the P3 teeth self-test and exit")
    args = ap.parse_args()

    print("=== Task #180 step 3d toroidal column-window gate ===")
    if not os.path.exists(args.col):
        print("  P1 RED: asset not found: %s" % args.col)
        return 1
    layers, tsz = load_colmajor(args.col)
    xs = layers[0]["xs"]
    ys = layers[0]["ys"]
    print("  P1 OK  colmajor layers=%d  %s  tile=%d  window_W=%d (%d B resident)"
          % (len(layers),
             ", ".join("%d(%dx%d)" % (l["id"], l["xs"], l["ys"]) for l in layers),
             tsz, WINDOW_W, WINDOW_W * ys * 2 * len(layers)))
    if not (xs > WINDOW_W and ys > 0 and (xs & (xs - 1)) == 0):
        print("  P1 RED: implausible dims xs=%d ys=%d" % (xs, ys))
        return 1

    # P3 teeth - run first so a vacuous P2 can never mask a dead comparison.
    tb, _t, _a = sweep(layers, WINDOW_W, VIS_TILES, LEFT_MARGIN, slot_bias=1)
    p3_ok = tb > 0
    print("  P3 %s teeth: off-by-one slot map -> %d mismatches (expect >0)"
          % ("OK " if p3_ok else "RED", tb))
    if args.selftest:
        return 0 if p3_ok else 1

    # P2 - the real sweep: window fetch == full-grid reference, 0 mismatches.
    mm, total, advances = sweep(layers, WINDOW_W, VIS_TILES, LEFT_MARGIN)
    bytes_per_adv = ys * 2 * len(layers)
    print("  P2 sweep: cameras=%d  advances=%d  streamed_cols=%d  "
          "bytes/advance=%d" % (xs - VIS_TILES + 1, advances, total,
                                bytes_per_adv))
    print("  P2 mismatches (window fetch != full-grid reference): %d" % mm)
    p2_ok = (mm == 0)

    if p2_ok and p3_ok:
        print("=== Gate #180 colwindow: GREEN (toroidal window == full grid; "
              "teeth proven) ===")
        return 0
    print("=== Gate #180 colwindow: RED (P2=%s P3=%s) ===" %
          ("ok" if p2_ok else "FAIL", "ok" if p3_ok else "FAIL"))
    return 1


if __name__ == "__main__":
    sys.exit(main())
