#!/usr/bin/env python3
# =============================================================================
# build_frame_dir.py -- sprite-pipeline rework increment 2: PRE-CUT VDP1
# frame-directory blobs (replaces runtime rect-cutting).
#
# Today (SaturnSheet.cpp:372-436 SaturnSheet_FetchRect + p6_vdp1.c:1636-1739
# p6_title_pool_for): a VDP1 slot-cache MISS cuts the frame rect out of the
# sheet at runtime (banded miniz inflate, or resident cart memcpy with
# row-by-row repack to the padded stride). This tool does ALL of that
# offline: every frame the anim .bins reference is pre-cut into a
# standalone VDP1-ready character pattern.
#
# VDP1 contract (ST-013-R3):
#   sec 5.1  "The character size can be specified from 8 pixels to 504
#            pixels horizontally in 8-PIXEL UNITS and from 1 pixel to 255
#            pixels vertically" -> padded width pw = ceil(w/8)*8; pad
#            pixels = 0 (transparent color code, sec 6.3 SPD table:
#            mode 0/1 transparent = 0H, mode 4 = 00H).
#   sec 6.3  color mode bits 5-3: mode 1 = 16 colors LOOKUP TABLE, 4 bits/
#            pixel; mode 4 = 256 colors color bank, 8 bits/pixel.
#   sec 5.1  4bpp: "the upper 4 bits represent the left pixel and the
#            lower 4 bits the right pixel" (no horizontal inversion).
#   sec 5.2  color lookup table = 32 bytes (16 x u16) in VDP1 VRAM;
#            entries may be COLOR BANK CODE -> we store the ORIGINAL 8-bit
#            palette index per nibble; the runtime builds the LUT entry as
#            (CRAM bank | index), preserving CRAM palette animation and
#            bit-exact parity (zero quantization).
#
# FILE FORMAT cd/<NAME>.FRD (all BIG-ENDIAN, parsed in place on SH-2):
#   +0  'FRD1'
#   +4  u16 frameCount | u16 lutCount
#   +8  u16 sheetW | u16 sheetH             (for defensive rect validation)
#   +12 directory: frameCount x 16 B, SORTED ascending by the u64 key
#       (sy<<48 | sx<<32 | w<<16 | h) for SH-2 binary search:
#         u16 sx, sy       -- rect key (sheet coords, as authored in .bin)
#         u16 w,  h        -- content dims (rect key); h <= 255 (sec 5.1)
#         u32 offset       -- pattern bytes from blob start, 4-aligned
#         u8  mode         -- 0 = 8bpp (VDP1 color mode 4 = CMDPMOD bits
#                             5-3 100B); 1 = 4bpp LUT (mode 1 = 001B)
#         u8  pw8          -- padded width in 8-px units (== CMDSIZE bits
#                             13-8 value, TEXDEF (w&0x1F8)<<5 semantics)
#         u16 lutIdx       -- 4bpp: index into the LUT source table;
#                             0xFFFF for 8bpp frames
#   +12+16*frameCount: LUT source table, lutCount x 16 B = the original
#       8-bit palette index for each nibble value 0..15 (nibble 0 is
#       always index 0 = transparent). DEDUPED across frames.
#   then (4-aligned): pattern data. Per frame, h rows:
#         8bpp: pw8*8 bytes/row (pixels, pad = 0)
#         4bpp: pw8*4 bytes/row (packed nibbles, pad nibble = 0)
#
# A frame is 4bpp iff its distinct non-zero palette indices <= 15
# (measured by frame_census.py; LOSSLESS -- parity binding).
#
# Self-tests every run:
#   S1 every 4bpp frame round-trips: unpack nibbles -> LUT -> original
#      indices == the GIF crop (pad columns == 0).
#   S2 every 8bpp frame round-trips vs the GIF crop.
#   S3 directory is strictly sorted + offsets 4-aligned + arithmetic
#      consumes the whole blob.
# Writes cd/<NAME>.FRD in THIS tree + tools/frame_dir_manifest.json.
# =============================================================================
import json
import os
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
MAIN = ROOT
SPRITES = os.path.join(MAIN, "extracted", "Data", "Sprites")
CD = os.path.join(ROOT, "cd")
MANIFEST = os.path.join(HERE, "frame_dir_manifest.json")

sys.path.insert(0, HERE)
from frame_census import parse_bin, pad8  # noqa: E402

# working-set sheet -> 8.3 output name (SGL GFS_FNAME_LEN rule, matches the
# .SHT naming in build_sheet_bands.py). HBH is composed offline into its own
# atlas (build_heavy_atlas.py) -- when the parent adopts FRD for HBH, run this
# converter over the COMPOSED atlas instead of the 5 source sheets.
SHEETS = [
    ("Players/Sonic1.gif", "SONIC1.FRD"),
    ("Players/Sonic2.gif", "SONIC2.FRD"),
    ("Players/Sonic3.gif", "SONIC3.FRD"),
    ("Players/Tails1.gif", "TAILS1.FRD"),
    ("Global/Items.gif", "ITEMS.FRD"),
    ("Global/Display.gif", "DISPLAY.FRD"),
    ("Global/Shields.gif", "SHIELDS.FRD"),
    ("Global/Objects.gif", "GLOBJ.FRD"),
    ("Global/PhantomRuby.gif", "RUBYOBJ.FRD"),
    ("GHZ/Objects.gif", "GHZOBJ.FRD"),
    ("AIZ/Objects.gif", "AIZOBJ.FRD"),
    ("GHZCutscene/Objects.gif", "GHCOBJ.FRD"),
    ("Global/Water.gif", "WATER.FRD"),   # Water M1b (GHZ WATER_WATERLEVEL surface strip)
]

DIR_ENTRY = 16


def collect_rects():
    """All rects referenced by any anim .bin, grouped by sheet name (lower)."""
    by_sheet = {}
    for root, _d, files in os.walk(SPRITES):
        for fn in files:
            if not fn.lower().endswith(".bin"):
                continue
            r = parse_bin(os.path.join(root, fn))
            if r is None:
                continue
            sheets, frames = r
            for (so, sx, sy, w, h) in frames:
                if w <= 0 or h <= 0 or so >= len(sheets):
                    continue
                by_sheet.setdefault(sheets[so].lower(), set()).add((sx, sy, w, h))
    return by_sheet


# DEFAULT-ON (task #328, 2026-07-13): the runtime FRD dispatch serves mode 0 (8bpp)
# ONLY -- a 4bpp (mode 1) frame is FOUND by SaturnFrameDir_Lookup (so it does NOT count
# as an frd_miss) but then REJECTED by the `fi.mode == 0` guard and silently falls back
# to the slow banded FetchRect inflate. That was the chain-GHZ draw-wall's hidden half:
# Tails1/Items/Display had 4bpp frames -> never served from FRD -> banded every frame.
# Emitting every frame 8bpp makes ALL frames FRD-serveable. Pass --4bpp to opt back in
# ONLY once the P6_FRAMEDIR_4BPP draw path is coded. Was: `"--all8" in sys.argv`.
ALL8 = "--4bpp" not in sys.argv


def build_one(rel, outname, rects):
    im = Image.open(os.path.join(SPRITES, rel.replace("/", os.sep)))
    W, H = im.size
    px = im.tobytes()
    assert len(px) == W * H, "not 8bpp indexed: %s" % rel

    entries = []   # (sx, sy, w, h, mode, pw8, lutIdx, pattern-bytes)
    luts = []      # list of 16-byte LUT source tables
    lut_index = {} # bytes -> idx
    stats = {"n4": 0, "n8": 0, "pat4": 0, "pat8": 0, "oob": 0}

    # sort by the SH-2 binary-search key (sy, sx, w, h)
    for (sx, sy, w, h) in sorted(rects, key=lambda r: (r[1], r[0], r[2], r[3])):
        if sx < 0 or sy < 0 or sx + w > W or sy + h > H:
            stats["oob"] += 1
            continue
        assert 1 <= h <= 255, "h out of ST-013 sec 5.1 range: %s %r" % (rel, (sx, sy, w, h))
        pw = pad8(w)
        assert 8 <= pw <= 504, "w out of ST-013 sec 5.1 range: %s %r" % (rel, (sx, sy, w, h))
        rows = [px[(sy + r) * W + sx:(sy + r) * W + sx + w] for r in range(h)]
        cols = set()
        for row in rows:
            cols.update(row)
        nz = sorted(cols - {0})
        if len(nz) <= 15 and not ALL8:
            # ---- 4bpp LUT frame: nibble 0 = transparent index 0 ----
            lut = bytes([0] + nz + [0] * (15 - len(nz)))
            remap = {0: 0}
            for i, c in enumerate(nz):
                remap[c] = i + 1
            pat = bytearray()
            for row in rows:
                padded = row + b"\0" * (pw - w)
                for i in range(0, pw, 2):
                    pat.append((remap[padded[i]] << 4) | remap[padded[i + 1]])
            li = lut_index.get(lut)
            if li is None:
                li = len(luts)
                lut_index[lut] = li
                luts.append(lut)
            entries.append((sx, sy, w, h, 1, pw // 8, li, bytes(pat)))
            stats["n4"] += 1
            stats["pat4"] += len(pat)
        else:
            pat = b"".join(row + b"\0" * (pw - w) for row in rows)
            entries.append((sx, sy, w, h, 0, pw // 8, 0xFFFF, pat))
            stats["n8"] += 1
            stats["pat8"] += len(pat)

    # ---- layout ----
    n = len(entries)
    hdr = struct.pack(">4sHHHH", b"FRD1", n, len(luts), W, H)
    lut_off = len(hdr) + n * DIR_ENTRY
    pat_base = (lut_off + 16 * len(luts) + 3) & ~3
    dirblob = b""
    cursor = pat_base
    offsets = []
    for (sx, sy, w, h, mode, pw8, li, pat) in entries:
        offsets.append(cursor)
        dirblob += struct.pack(">HHHHIBBH", sx, sy, w, h, cursor, mode, pw8, li)
        cursor = (cursor + len(pat) + 3) & ~3
    blob = bytearray(hdr + dirblob + b"".join(luts))
    blob += b"\0" * (pat_base - len(blob))
    for (e, off) in zip(entries, offsets):
        assert len(blob) == off
        blob += e[7]
        blob += b"\0" * ((-len(blob)) % 4)

    # ---- S3: directory sorted, aligned, consumed ----
    keys = [(e[1] << 48) | (e[0] << 32) | (e[2] << 16) | e[3] for e in entries]
    assert keys == sorted(keys) and len(set(keys)) == len(keys), "S3 sort " + rel
    assert all(o % 4 == 0 for o in offsets), "S3 align " + rel
    assert len(blob) == (cursor if entries else pat_base), "S3 size " + rel

    # ---- S1/S2: byte-exact round-trip of EVERY frame ----
    for (sx, sy, w, h, mode, pw8, li, _pat), off in zip(entries, offsets):
        pw = pw8 * 8
        want = [px[(sy + r) * W + sx:(sy + r) * W + sx + w] + b"\0" * (pw - w)
                for r in range(h)]
        if mode == 1:
            lut = blob[lut_off + 16 * li:lut_off + 16 * li + 16]
            got = []
            for r in range(h):
                ro = off + r * (pw // 2)
                row = bytearray()
                for b in blob[ro:ro + pw // 2]:
                    row.append(lut[b >> 4])
                    row.append(lut[b & 15])
                got.append(bytes(row))
        else:
            got = [bytes(blob[off + r * pw:off + (r + 1) * pw]) for r in range(h)]
        if got != want:
            print("S1/S2 FAIL: %s rect %r" % (rel, (sx, sy, w, h)))
            return None

    path = os.path.join(CD, outname)
    with open(path, "wb") as f:
        f.write(blob)
    return {"sheet": rel, "file": outname, "bytes": len(blob),
            "frames": n, "frames_4bpp": stats["n4"], "frames_8bpp": stats["n8"],
            "luts": len(luts), "pattern4_bytes": stats["pat4"],
            "pattern8_bytes": stats["pat8"], "frames_oob": stats["oob"],
            "raw_sheet_bytes": W * H}


def main():
    os.makedirs(CD, exist_ok=True)
    by_sheet = collect_rects()
    manifest = {"_comment": "GENERATED by build_frame_dir.py", "sheets": []}
    tot_frd = tot_raw = 0
    print("%-26s %-12s %6s %5s %5s %4s %9s %9s" %
          ("sheet", "file", "frames", "4bpp", "8bpp", "luts", "FRD B", "raw B"))
    for rel, outname in SHEETS:
        rects = by_sheet.get(rel.lower(), set())
        m = build_one(rel, outname, rects)
        if m is None:
            return 1
        manifest["sheets"].append(m)
        tot_frd += m["bytes"]
        tot_raw += m["raw_sheet_bytes"]
        print("%-26s %-12s %6d %5d %5d %4d %9d %9d" %
              (rel, outname, m["frames"], m["frames_4bpp"], m["frames_8bpp"],
               m["luts"], m["bytes"], m["raw_sheet_bytes"]))
    print("TOTAL FRD %d B vs raw sheets %d B (%.1f%%)" %
          (tot_frd, tot_raw, 100.0 * tot_frd / tot_raw))
    with open(MANIFEST, "w") as f:
        json.dump(manifest, f, indent=1)
    print("manifest -> %s" % MANIFEST)
    return 0


if __name__ == "__main__":
    sys.exit(main())
