#!/usr/bin/env python3
# =============================================================================
# build_layout_bands.py -- P6.7 W11a (Task #210): build the per-zone layout
# BAND STORE asset + the gate model/probes.
#
# WHY (W11, SaturnMemoryMap.h): tile-layer layouts cannot be RAM-resident at
# scale (GHZ1 551,168 B raw; FBZ2 3,006,976 B). The Saturn engine serves
# layout reads from CAMERA-LOCAL sliding windows refilled from this band
# store: each layer is split into 16-row bands, each band zlib-deflated at
# BUILD time (runtime re-encoding is infeasible: tdefl needs ~320 KB state;
# cheap codecs measured insufficient -- RLE16 49%, row-delta+RLE 41% on GHZ
# FG Low vs zlib's 13%). The decoder is the SAME miniz inflate already
# proven on SH-2 since P6.3. Every byte derives 1:1 from the Data.rsdk
# extraction (decomp-assets-only rule; the established build_*.py class).
#
# MEASURED (2026-06-11): GHZ1 bands total 50,782 B (budget 0x14000); worst
# single band 5,354 B deflated / 32,768 B raw. FBZ2 = 177,283 B -- per-zone
# budget trade against its overlay slack, re-derived at that zone's diag.
#
# OUTPUT cd/<TAG>LAYT.BIN, all BIG-ENDIAN (SH-2 reads in place):
#   u32 'LYT1'  u16 layerCount  u16 bandRows(16)
#   per layer: u16 xsize  u16 ysize  u16 bandCount  u16 pad
#   per layer: per band: u32 offset (from file start)  u32 zsize  u32 rawsize
#   ...zlib streams...
#
# MODEL tools/_portspike/_p6/p6_layout_model.json + probes .inc:
#   file djb2, per-layer dims, and probe tuples {layer, x, y, expect}
#   chosen to span MULTIPLE window positions (forcing refills on SH-2).
# =============================================================================
import argparse
import importlib.util
import json
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# P6.8 F.2: PER-ZONE band height (was a fixed 16). One raw band = maxWidth *
# bandRows * 2 must fit the inflate scratch (P6_LW_LAYSCRATCH = 0x8000 = 32,768),
# AND the deflated store must fit the LAYOUTBANDS window (0xC800 = 51,200). At
# 16 rows GHZ2's 1280-wide FG band = 40,960 B OVERFLOWED the scratch -> Refill
# aborted -> empty windows -> the player fell through GHZ2; but smaller fixed
# rows blow GHZ1's store past the window (more dir + worse compression). So pick
# the LARGEST bandRows<=16 whose widest band still fits the scratch, per zone:
# GHZ1 (1024w)->16, GHZ2 (1280w)->12. bandRows is written in the file header
# (u16 @ off 6) and read at mount -- SaturnLayout.cpp no longer hardcodes it.
MAX_BAND_ROWS = 16
SCRATCH_CAP = 32768  # P6_LW_LAYSCRATCH bytes; widest raw band must fit

_spec = importlib.util.spec_from_file_location(
    "pte", os.path.join(ROOT, "tools", "parse_title_entities.py"))
pte = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pte)


def djb2(data):
    h = 5381
    for b in data:
        h = ((h << 5) + h) ^ b
        h &= 0xFFFFFFFF
    return h


def parse_layers(scene_path):
    d = open(scene_path, "rb").read()
    r = pte.R(d)
    assert r.d[:4] == b"SCN\x00"
    r.p = 4
    r.take(0x10)
    nl = r.u8()
    r.take(nl + 1)
    layers = []
    for _ in range(r.u8()):
        r.u8()
        n = r.u8()
        name = r.take(n).rstrip(b"\x00").decode("latin1")
        r.u8(); r.u8()
        xs = r.u16(); ys = r.u16()
        r.u16(); r.u16()
        sic = r.u16()
        for _ in range(sic):
            r.take(6)
        r.compressed()
        layout = r.compressed()
        layers.append((name, xs, ys, layout))
    return layers


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", default="extracted/Data/Stages/GHZ/Scene1.bin")
    ap.add_argument("--tag", default="GHZ1")
    ap.add_argument("--probes", type=int, default=96)
    args = ap.parse_args()

    layers = parse_layers(os.path.join(ROOT, args.scene))

    # PER-ZONE band height: largest <= MAX_BAND_ROWS whose widest raw band
    # (maxWidth * bandRows * 2) fits the SCRATCH_CAP inflate buffer.
    max_w = max((xs for _, xs, _, _ in layers), default=1)
    BAND_ROWS = max(1, min(MAX_BAND_ROWS, SCRATCH_CAP // (max_w * 2)))

    # directory sizing
    n_layers = len(layers)
    head = 8 + n_layers * 8
    band_counts = [(ys + BAND_ROWS - 1) // BAND_ROWS for _, _, ys, _ in layers]
    dir_bytes = sum(bc * 12 for bc in band_counts)
    blobs = []
    offset = head + dir_bytes
    dirs = []
    for (name, xs, ys, layout), bc in zip(layers, band_counts):
        entries = []
        for b in range(bc):
            raw = layout[b * BAND_ROWS * xs * 2:(b + 1) * BAND_ROWS * xs * 2]
            z = zlib.compress(raw, 9)
            entries.append((offset, len(z), len(raw)))
            blobs.append(z)
            offset += len(z)
        dirs.append(entries)

    out = bytearray()
    out += b"LYT1"
    out += struct.pack(">HH", n_layers, BAND_ROWS)
    for (name, xs, ys, layout), bc in zip(layers, band_counts):
        out += struct.pack(">HHHH", xs, ys, bc, 0)
    for entries in dirs:
        for off, zsz, rsz in entries:
            out += struct.pack(">III", off, zsz, rsz)
    for z in blobs:
        out += z
    out_path = os.path.join(ROOT, "cd", args.tag + "LAYT.BIN")
    open(out_path, "wb").write(out)

    # probes: deterministic spread across multiple window positions per
    # collidable-class layer (the two largest), plus a few BG probes
    import random
    rng = random.Random(0x57313161)  # "W11a"
    probes = []
    sizes = sorted(range(n_layers), key=lambda i: -(layers[i][1] * layers[i][2]))
    fg = sizes[:2]
    for li in fg:
        name, xs, ys, layout = layers[li]
        for _ in range(args.probes // 2 // len(fg)):
            x = rng.randrange(xs)
            y = rng.randrange(ys)
            w = layout[(x + y * xs) * 2] | (layout[(x + y * xs) * 2 + 1] << 8)
            probes.append({"layer": li, "x": x, "y": y, "expect": w})
        # clustered walk forcing horizontal window crossings
        y = rng.randrange(ys)
        for x in range(0, xs, max(1, xs // (args.probes // 4))):
            w = layout[(x + y * xs) * 2] | (layout[(x + y * xs) * 2 + 1] << 8)
            probes.append({"layer": li, "x": x, "y": y, "expect": w})
    for li in range(n_layers):
        if li in fg:
            continue
        name, xs, ys, layout = layers[li]
        for _ in range(4):
            x = rng.randrange(xs)
            y = rng.randrange(ys)
            w = layout[(x + y * xs) * 2] | (layout[(x + y * xs) * 2 + 1] << 8)
            probes.append({"layer": li, "x": x, "y": y, "expect": w})

    model = {
        "_comment": "GENERATED by build_layout_bands.py -- qa_p6_layout model",
        "tag": args.tag,
        "file_bytes": len(out), "file_hash": "0x%08X" % djb2(out),
        "layers": [{"name": nm, "xs": xs, "ys": ys, "bands": bc}
                   for (nm, xs, ys, _), bc in zip(layers, band_counts)],
        "probe_count": len(probes),
    }
    mdl_path = os.path.join(ROOT, "tools", "_portspike", "_p6",
                            "p6_layout_model.json")
    open(mdl_path, "w").write(json.dumps(model, indent=1))

    inc = ["// p6_layout_probes.inc -- GENERATED by build_layout_bands.py. "
           "DO NOT EDIT.",
           "// %d probes over %s layouts; expectations = raw layout words."
           % (len(probes), args.tag),
           "static const struct { uint8 layer; uint16 x; uint16 y; "
           "uint16 expect; }",
           "p6LayoutProbes[%d] = {" % len(probes)]
    for p in probes:
        inc.append("    { %d, %4d, %3d, 0x%04X }," %
                   (p["layer"], p["x"], p["y"], p["expect"]))
    inc.append("};")
    inc_path = os.path.join(ROOT, "tools", "_portspike", "_p6",
                            "p6_layout_probes.inc")
    open(inc_path, "w").write("\n".join(inc) + "\n")

    print("OK: %s = %d B (djb2 %s), %d layers, %d probes" %
          (out_path, len(out), model["file_hash"], n_layers, len(probes)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
