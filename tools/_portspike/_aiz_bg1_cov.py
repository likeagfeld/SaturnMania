#!/usr/bin/env python3
# R2.5 verification: measure BG1 (distant backdrop) coverage across the EARLY AIZ
# fly-in burst (the 50-105s window where R2.4 measured 0/110 non-black -- BG4 empty,
# no BG1). If BG1 now renders on NBG2, the early frames fill with backdrop content
# (non-black), regardless of hue. green% is secondary (jungle tint).
#   nonblack = max(R,G,B) > 24     -> backdrop present (R2.4 early ~0)
#   green    = G>R+8 and G>B+8 and G>40
# Reports per-frame + early-window mean + top frames. Pure stdlib (PNG via struct/zlib).
import sys, os, glob, struct, zlib

def load_png(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos = 8; w = h = bitd = colt = None; idat = b""
    while pos < len(data):
        ln = struct.unpack(">I", data[pos:pos+4])[0]
        typ = data[pos+4:pos+8]; chunk = data[pos+8:pos+8+ln]
        if typ == b"IHDR":
            w, h, bitd, colt = struct.unpack(">IIBB", chunk[:10])
        elif typ == b"IDAT":
            idat += chunk
        elif typ == b"IEND":
            break
        pos += 12 + ln
    raw = zlib.decompress(idat)
    ch = {0:1,2:3,3:1,4:2,6:4}[colt]
    bpp = ch * (bitd // 8)
    stride = w * bpp
    out = bytearray(); prev = bytearray(stride)
    p = 0
    for _ in range(h):
        ft = raw[p]; p += 1
        line = bytearray(raw[p:p+stride]); p += stride
        if ft == 1:
            for i in range(bpp, stride): line[i] = (line[i] + line[i-bpp]) & 255
        elif ft == 2:
            for i in range(stride): line[i] = (line[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(stride):
                a = line[i-bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                a = line[i-bpp] if i >= bpp else 0
                b = prev[i]; c = prev[i-bpp] if i >= bpp else 0
                pp = a + b - c
                pa = abs(pp-a); pb = abs(pp-b); pc = abs(pp-c)
                pr = a if (pa<=pb and pa<=pc) else (b if pb<=pc else c)
                line[i] = (line[i] + pr) & 255
        out += line; prev = line
    return w, h, ch, bytes(out)

def measure(path):
    w, h, ch, px = load_png(path)
    # sample the central playfield (skip a small border to avoid letterbox/UI)
    x0, x1 = int(w*0.06), int(w*0.94)
    y0, y1 = int(h*0.10), int(h*0.90)
    nb = grn = tot = 0
    sr = sg = sb = 0
    for y in range(y0, y1, 2):
        row = y * w * ch
        for x in range(x0, x1, 2):
            i = row + x*ch
            r, g, b = px[i], px[i+1], px[i+2]
            tot += 1
            if max(r, g, b) > 24:
                nb += 1; sr += r; sg += g; sb += b
            if g > r+8 and g > b+8 and g > 40:
                grn += 1
    nbp = 100.0*nb/tot if tot else 0
    grp = 100.0*grn/tot if tot else 0
    mc = (sr//nb, sg//nb, sb//nb) if nb else (0,0,0)
    return nbp, grp, mc

def main(argv):
    pat = argv[0] if argv else "_aizbg1_*.png"
    files = sorted(glob.glob(pat), key=lambda p: (len(p), p))
    if not files:
        print("no frames match", pat); return 1
    rows = []
    for f in files:
        nb, gr, mc = measure(f)
        idx = "".join(c for c in os.path.basename(f) if c.isdigit())
        rows.append((int(idx or 0), f, nb, gr, mc))
    rows.sort()
    print("idx  file                 nonblack%  green%   mean(R,G,B)")
    for idx, f, nb, gr, mc in rows:
        print("%-4d %-20s %7.1f  %6.1f   %s" % (idx, os.path.basename(f), nb, gr, mc))
    nbs = [r[2] for r in rows]; grs = [r[3] for r in rows]
    print("-"*60)
    print("frames=%d  nonblack%%: mean=%.1f max=%.1f min=%.1f   green%%: mean=%.1f max=%.1f"
          % (len(rows), sum(nbs)/len(nbs), max(nbs), min(nbs), sum(grs)/len(grs), max(grs)))
    top = sorted(rows, key=lambda r: -r[2])[:6]
    print("top nonblack frames:", [(r[0], round(r[2],1)) for r in top])
    # R2.5 PASS: early-window backdrop is no longer black. R2.4 early == ~0 nonblack.
    filled = sum(1 for nb in nbs if nb > 20)
    print("frames with >20%% backdrop: %d/%d" % (filled, len(rows)))
    # FLICKER GATE: the BG planes must not periodically drop to black. A per-frame
    # slChar/slPage/slPlane/slMap rewrite in p6_vdp2_aiz_bg_frame races the SGL vblank
    # register-image DMA -> ~1/4 of frames lose the plane (same class as the title
    # cloud-blink, p6_vdp2.c:556-560). RED at the rewrite-every-frame build (~26%);
    # GREEN once geometry is config-once (manual slScrCycleSet keeps the fetch).
    black = sum(1 for nb in nbs if nb < 5)
    rate = 100.0 * black / len(rows)
    print("BLACK frames (<5%% fill): %d/%d = %.1f%%" % (black, len(rows), rate))
    MAX = float(os.environ.get("AIZ_MAX_BLACK", "8"))
    if rate > MAX:
        print("RESULT: RED -- BG-plane flicker %.1f%% > %.0f%% (per-frame geometry "
              "rewrite races the SGL vblank DMA). Move slChar/slPage/slPlane/slMap "
              "in p6_vdp2_aiz_bg_frame to config-once." % (rate, MAX))
        return 1
    print("RESULT: GREEN -- BG-plane stable (%.1f%% black <= %.0f%%)." % (rate, MAX))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
