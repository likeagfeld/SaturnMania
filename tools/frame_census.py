#!/usr/bin/env python3
# =============================================================================
# frame_census.py -- sprite-pipeline rework increment 1 (offline, read-only).
#
# MEASURES (no guessing) for the GHZ+chain working-set sheets:
#   1. Which frames the anim .bins actually reference (the TSONIC selective-
#      frame precedent): every SPR\0 .bin under extracted/Data/Sprites is
#      parsed with the EXACT build_anim_pack.py parser and its frames grouped
#      by sheet name.
#   2. Per-frame distinct-color counts (excluding transparent index 0) --
#      the 4bpp eligibility census. A frame with <= 15 distinct non-zero
#      indices is losslessly representable in VDP1 16-color LUT mode
#      (ST-013-R3 sec 6.3 color mode 1: 4 bits/pixel + 16-entry lookup
#      table; entries may be color-bank codes -> the ORIGINAL 8-bit CRAM
#      indices are preserved bit-exact, zero quantization).
#   3. Byte sizes: raw whole sheet (8bpp), pre-cut 8bpp (sum of padded
#      frame patterns), pre-cut mixed 4bpp/8bpp. Width padding rule per
#      ST-013-R3 sec 5.1: character size 8..504 horizontal in 8-PIXEL
#      units -> pw = ceil(w/8)*8; 4bpp row = pw/2 bytes, 8bpp row = pw.
#
# Output: tools/frame_census.json + a printed table.
# Reads assets from the MAIN tree (D:\sonicmaniasaturn) READ-ONLY.
# =============================================================================
import json
import os
import struct
import sys

from PIL import Image

MAIN = r"D:\sonicmaniasaturn"
SPRITES = os.path.join(MAIN, "extracted", "Data", "Sprites")
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "frame_census.json")

# The GHZ+chain working set (mission list; .SHT names from build_sheet_bands.py
# + build_heavy_atlas.py). HBH is a COMPOSED atlas -- its five SOURCE sheets are
# censused individually (whatever the 5 Heavy .bins reference).
WORKING_SET = [
    "Players/Sonic1.gif", "Players/Sonic2.gif", "Players/Sonic3.gif",
    "Players/Tails1.gif",
    "Global/Items.gif", "Global/Display.gif", "Global/Shields.gif",
    "Global/Objects.gif", "Global/PhantomRuby.gif",
    "GHZ/Objects.gif",
    "AIZ/Objects.gif",
    "GHZCutscene/Objects.gif",
]

# The 5 Heavies bins composed into HBHOBJ.SHT (build_heavy_atlas.py HEAVIES
# table, lines 52-63) -- their SOURCE sheets are censused individually.
HBH_BINS = ["SPZ1/Boss.bin", "PSZ2/Shinobi.bin", "MSZ/HeavyMystic.bin",
            "LRZ3/HeavyRider.bin", "LRZ3/HeavyKing.bin"]


def parse_bin(path):
    """Verbatim structure of build_anim_pack.parse_bin (same offsets)."""
    d = open(path, "rb").read()
    if d[:4] != b"SPR\0":
        return None
    off = 4
    total, = struct.unpack_from("<I", d, off); off += 4
    sheet_count = d[off]; off += 1
    sheets = []
    for _ in range(sheet_count):
        ln = d[off]; off += 1
        sheets.append(d[off:off + ln].rstrip(b"\0").decode("latin1"))
        off += ln
    hitbox_count = d[off]; off += 1
    for _ in range(hitbox_count):
        ln = d[off]; off += 1 + ln
    anim_count, = struct.unpack_from("<H", d, off); off += 2
    frames = []
    for _ in range(anim_count):
        ln = d[off]; off += 1 + ln
        fc, = struct.unpack_from("<H", d, off); off += 2
        off += 2  # speed
        off += 2  # loop, rot
        for _ in range(fc):
            sheet_ord = d[off]; off += 1
            off += 4  # dur, uni
            sx, sy, w, h = struct.unpack_from("<hhhh", d, off); off += 4 * 2
            off += 4  # px, py
            off += 8 * hitbox_count
            frames.append((sheet_ord, sx, sy, w, h))
    if len(frames) != total:
        raise AssertionError("frame count mismatch %s" % path)
    return sheets, frames


def pad8(w):
    return (w + 7) & ~7


def main():
    # -- 1. scan every .bin, group referenced rects by sheet ----------------
    by_sheet = {}   # sheet name (as authored, case-preserved) -> set of rects
    bins_per_sheet = {}
    nbins = 0
    for root, _dirs, files in os.walk(SPRITES):
        for fn in files:
            if not fn.lower().endswith(".bin"):
                continue
            p = os.path.join(root, fn)
            try:
                r = parse_bin(p)
            except Exception as e:
                print("SKIP %s (%s)" % (p, e))
                continue
            if r is None:
                continue
            nbins += 1
            sheets, frames = r
            rel = os.path.relpath(p, SPRITES).replace(os.sep, "/")
            for (so, sx, sy, w, h) in frames:
                if w <= 0 or h <= 0 or so >= len(sheets):
                    continue
                sname = sheets[so]
                by_sheet.setdefault(sname, set()).add((sx, sy, w, h))
                bins_per_sheet.setdefault(sname, set()).add(rel)

    # sheet-name keys in bins are e.g. "Players/Sonic1.gif" -- map case-insensitively
    key_by_lower = {}
    for k in by_sheet:
        key_by_lower.setdefault(k.lower(), k)

    # add the HBH source sheets to the working set
    ws = list(WORKING_SET)
    for hb in HBH_BINS:
        p = os.path.join(SPRITES, hb.replace("/", os.sep))
        if os.path.exists(p):
            sheets, _f = parse_bin(p)
            for s in sheets:
                if s not in ws:
                    ws.append(s)

    # -- 2. census each working-set sheet -----------------------------------
    report = []
    print("%-28s %5s %8s %7s %7s | %9s %9s %9s | colorcensus (frames)" %
          ("sheet", "frms", "rawB", "shtCol", "maxFrm",
           "precut8", "precut4mx", "save%"))
    for rel in ws:
        gp = os.path.join(SPRITES, rel.replace("/", os.sep))
        if not os.path.exists(gp):
            print("%-28s MISSING GIF" % rel)
            continue
        key = key_by_lower.get(rel.lower())
        rects = sorted(by_sheet.get(key, set())) if key else []
        im = Image.open(gp)
        w, h = im.size
        px = im.tobytes()
        assert len(px) == w * h, "not 8bpp indexed: %s" % rel
        sheet_cols = set(px)

        hist = {}          # distinct nonzero colors -> frame count
        pre8 = 0
        pre4 = 0
        n4 = 0
        maxcols = 0
        oversize = 0
        frames_meta = []
        for (sx, sy, fw, fh) in rects:
            # clamp any authored rect that exceeds the sheet (defensive)
            if sx < 0 or sy < 0 or sx + fw > w or sy + fh > h:
                oversize += 1
                continue
            cols = set()
            for r in range(fh):
                o = (sy + r) * w + sx
                cols.update(px[o:o + fw])
            nz = len(cols - {0})
            maxcols = max(maxcols, nz)
            hist[nz] = hist.get(nz, 0) + 1
            pw = pad8(fw)
            b8 = pw * fh
            pre8 += b8
            if nz <= 15:
                pre4 += (pw // 2) * fh
                n4 += 1
            else:
                pre4 += b8
            frames_meta.append({"sx": sx, "sy": sy, "w": fw, "h": fh,
                                "colors": nz})
        raw = w * h
        row = {
            "sheet": rel, "width": w, "height": h, "raw_bytes": raw,
            "frames_referenced": len(rects), "frames_oob": oversize,
            "sheet_distinct_colors": len(sheet_cols),
            "sheet_distinct_nonzero": len(sheet_cols - {0}),
            "max_frame_colors": maxcols,
            "frames_4bpp_eligible": n4,
            "precut_8bpp_bytes": pre8,
            "precut_mixed4_bytes": pre4,
            "color_histogram": {str(k): v for k, v in sorted(hist.items())},
            "bins": sorted(bins_per_sheet.get(key, set())) if key else [],
        }
        report.append(row)
        sv = (100.0 * (raw - pre4) / raw) if raw else 0.0
        print("%-28s %5d %8d %7d %7d | %9d %9d %8.1f%% | 4bpp-ok %d/%d" %
              (rel, len(rects), raw, len(sheet_cols), maxcols,
               pre8, pre4, sv, n4, len(rects)))

    with open(OUT, "w") as f:
        json.dump({"_comment": "GENERATED by frame_census.py",
                   "bins_scanned": nbins, "sheets": report}, f, indent=1)
    print("\n%d .bins scanned; report -> %s" % (nbins, OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
