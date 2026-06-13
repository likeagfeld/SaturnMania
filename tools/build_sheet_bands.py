#!/usr/bin/env python3
# =============================================================================
# build_sheet_bands.py -- P6.7 W12 (Task #227): offline ROW-BAND stores for
# sprite sheets too large to be RAM-resident on the Saturn.
#
# WHY (W12 declaration, SaturnMemoryMap.h): LoadSpriteSheet decodes whole
# sheets into the 64 KB DATASET_STG pool; the Player set alone is 786,432 B
# decoded (Sonic1+2+3, MEASURED). The working set lives in VDP1 via the
# P6.5b3 rect-keyed slot cache -- the cache-miss path fetches a frame's rows
# from these band stores instead of a resident surface.
#
# Codec = the proven W11 layout-band codec (build_layout_bands.py): 16-row
# bands, zlib -9, parsed in place on SH-2 (big-endian directory). MEASURED
# (2026-06-11): Sonic1 59,703 / Sonic2 61,153 / Sonic3 33,295 = 154,151 B
# for the Player trio (vs 786,432 raw).
#
# FILE FORMAT cd/<NAME>.SHT (all BIG-ENDIAN, parsed in place):
#   'SHB1' | u16 width | u16 height | u16 bandRows | u16 bandCount
#   bandCount x { u32 offset, u32 zsize, u32 rawsize }
#   zlib streams (offsets from blob start)
#
# Emits per sheet: cd/<NAME>.SHT + appends to tools/_portspike/_p6/
# p6_sheet_model.json (offline ground truth for qa_p6_sheet.py: file hash,
# per-sheet dims, probe rects with djb2 over the rect bytes).
#
# Self-tests S1-S3 every run: band round-trip byte-exact, directory
# arithmetic, probe-rect extraction matches the raw decode.
# =============================================================================
import json
import os
import struct
import sys
import zlib

from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BAND_ROWS = 16

# The W12 Player-wave set. 8.3 names (SGL GFS_FNAME_LEN = 12 rule).
# Items.gif joined for the STG-sizing iteration (Task #227): banding it
# drops the 32,768 B resident decode from DATASET_STG so the GHZ anim
# working set (1,624 frames at FRAMEHITBOX_COUNT 2) fits the 80 KB pool.
# W19 (Task #227): DISPLAY.SHT (GHZ HUD digits) + SHIELDS.SHT (shield FX) join
# the staged set so their entity blits LAND (MEASURED 960 + 59 of the 1,139
# silent VDP1 handle<0 drops/run). Tails1.gif is DELIBERATELY ABSENT: its
# 58,643 B band store overflows the 245,760 B VDP2 band-store window by
# 19,105 B (6-sheet total 206,222 B fits; 7-sheet total 264,865 B does not).
# Adding Tails1 needs a funded plan (reclaim NBG1 cell tiles / relocate window).
SHEETS = [
    ("Players/Sonic1.gif", "SONIC1.SHT"),
    ("Players/Sonic2.gif", "SONIC2.SHT"),
    ("Players/Sonic3.gif", "SONIC3.SHT"),
    ("Global/Items.gif", "ITEMS.SHT"),
    ("Global/Display.gif", "DISPLAY.SHT"),
    ("Global/Shields.gif", "SHIELDS.SHT"),
]


def djb2(data):
    h = 5381
    for b in data:
        h = ((h << 5) + h) ^ b
        h &= 0xFFFFFFFF
    return h


def build_one(rel, outname):
    im = Image.open(os.path.join(ROOT, "extracted", "Data", "Sprites", rel))
    w, h = im.size
    px = im.tobytes()
    assert len(px) == w * h, "expected 8bpp indexed"

    nbands = (h + BAND_ROWS - 1) // BAND_ROWS
    head = 12
    dir_bytes = nbands * 12
    blobs = []
    entries = []
    off = head + dir_bytes
    for b in range(nbands):
        raw = px[b * BAND_ROWS * w:(b + 1) * BAND_ROWS * w]
        z = zlib.compress(raw, 9)
        entries.append((off, len(z), len(raw)))
        blobs.append(z)
        off += len(z)

    out = bytearray()
    out += b"SHB1"
    out += struct.pack(">HHHH", w, h, BAND_ROWS, nbands)
    for e in entries:
        out += struct.pack(">III", *e)
    for z in blobs:
        out += z

    # S1: every band round-trips byte-exact through the directory
    for i, (o, zs, rs) in enumerate(entries):
        raw = zlib.decompress(bytes(out[o:o + zs]))
        if len(raw) != rs or raw != px[i * BAND_ROWS * w:i * BAND_ROWS * w + rs]:
            print("S1 FAIL: band %d round-trip (%s)" % (i, rel))
            return None
    # S2: directory arithmetic consumed the whole blob
    if len(out) != off:
        print("S2 FAIL: %d != %d (%s)" % (len(out), off, rel))
        return None

    path = os.path.join(ROOT, "cd", outname)
    open(path, "wb").write(out)

    # Probe rects: spread across the sheet (typical frame sizes), expected =
    # djb2 over the rect bytes from the RAW decode; the SH-2 gate replays the
    # SAME rects through SaturnSheet_FetchRect.
    probes = []
    for (sx, sy, pw, ph) in [(0, 0, 48, 48), (w // 2, 8, 64, 48),
                             (w - 64, h // 2 - 8, 64, 64), (16, h - 40, 48, 40),
                             (w // 4, (h // 2) - (h // 4), 32, 56)]:
        rect = bytearray()
        for r in range(ph):
            rect += px[(sy + r) * w + sx:(sy + r) * w + sx + pw]
        # S3: extraction sanity -- rect length
        if len(rect) != pw * ph:
            print("S3 FAIL: probe rect (%s)" % rel)
            return None
        probes.append({"sx": sx, "sy": sy, "w": pw, "h": ph,
                       "hash": "0x%08X" % djb2(bytes(rect))})

    return {"sheet": rel, "file": outname, "width": w, "height": h,
            "band_rows": BAND_ROWS, "bands": nbands, "bytes": len(out),
            "file_hash": "0x%08X" % djb2(bytes(out)), "probes": probes}


def main():
    model = {"_comment": "GENERATED by build_sheet_bands.py -- qa_p6_sheet model",
             "sheets": []}
    total = 0
    for rel, outname in SHEETS:
        m = build_one(rel, outname)
        if not m:
            return 1
        model["sheets"].append(m)
        total += m["bytes"]
        print("%-22s -> %-12s %6d B (%d bands)" % (rel, outname, m["bytes"], m["bands"]))
    mpath = os.path.join(ROOT, "tools", "_portspike", "_p6", "p6_sheet_model.json")
    with open(mpath, "w") as f:
        json.dump(model, f, indent=2)

    # Diag probe table: the SH-2 replays these via SaturnSheet_FetchRect
    # (slot index == staging order == SHEETS order) and djb2s each rect.
    ipath = os.path.join(ROOT, "tools", "_portspike", "_p6", "p6_sheet_probes.inc")
    with open(ipath, "w") as f:
        f.write("// generated by build_sheet_bands.py -- DO NOT EDIT\n")
        f.write("// {slot, sx, sy, w, h, expected djb2 over the rect bytes}\n")
        rows = []
        for si, m in enumerate(model["sheets"]):
            for p in m["probes"]:
                rows.append("    { %d, %d, %d, %d, %d, 0x%08Xu }"
                            % (si, p["sx"], p["sy"], p["w"], p["h"],
                               int(p["hash"], 16)))
        f.write("#define P6_SHEET_PROBE_COUNT %d\n" % len(rows))
        f.write("static const struct { int32 slot, sx, sy, w, h; uint32 expect; }\n")
        f.write("p6SheetProbes[P6_SHEET_PROBE_COUNT] = {\n")
        f.write(",\n".join(rows))
        f.write("\n};\n")

    print("TOTAL %d B; model -> %s; probes -> %s"
          % (total, os.path.normpath(mpath), os.path.normpath(ipath)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
