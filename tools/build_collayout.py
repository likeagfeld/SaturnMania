#!/usr/bin/env python3
"""build_collayout.py - Task #180 step 4c (RAM-resident compressed layout).

Build cd/GHZ1COL.BIN: the collision-layer tile-entry grids the windowed
streamer pages into LWRAM. As of #180 step 4c the file is shipped block-
DEFLATE compressed ('GCO3') so the WHOLE file loads once into a small
resident LWRAM buffer at scene start and columns are decoded RAM->RAM during
play - ZERO CD access during gameplay. (The prior 'GCO2' was an uncompressed
~524 KB grid streamed column-by-column from CD every frame, which contended
with the GHZ CD-DA music track on the single Saturn CD head and stalled the
main loop - the >10s-title-card / no-music / slow-gameplay regression.)

The decomp Player scans BOTH foreground collision layers:
  Zone.c:212  Zone->collisionLayers = (1 << fgLayer[0]) | (1 << fgLayer[1]);
and FloorCollision/LWallCollision/etc (Collision.cpp:2406) iterate EVERY
layer in collisionLayers independently. For GHZ that is layer 3 "FG Low" +
layer 4 "FG High". GHZ1COL.BIN preserves BOTH as independent grids and the
runtime scans both exactly like the decomp.

Each cell is the FULL 16-bit layout entry, retained verbatim:
    bits 0-9  : tile-ID (& 0x3FF), index into the 1024 base masks
    bit 10    : FLIP_X (0x400)
    bit 11    : FLIP_Y (0x800)
    bits 12-15: solidity (planeA floor 0x1000, planeA roof/wall 0x2000,
                planeB floor 0x4000, planeB roof/wall 0x8000)
    0xFFFF    : blank
(Collision.cpp:2424 `tile < 0xFFFF && tile & solid`; mask index `tile & 0xFFF`.)

'GCO3' format (all multi-byte fields big-endian for SH-2):
    [0..3]    magic 'GCO3'
    [4..5]    u16 num_layers   (2)
    [6..7]    u16 tile_size    (16)
    [8..9]    u16 block_cols   (BLOCK_COLS, 16)
    [10..11]  u16 _reserved    (0)
    per-layer descriptor (num_layers x 12 B):
        u16 layer_id, u16 xsize, u16 ysize, u16 width_shift,
        u16 num_blocks (= ceil(xsize/block_cols)), u16 _pad
    per-layer block-index table (in layer order), each
        (num_blocks+1) x u32 BE absolute file offset; block i payload is
        [off[i], off[i+1]); trailing entry = end-of-block sentinel.
    raw-DEFLATE (zlib wbits=-15) compressed block payloads.

Each block decompresses to (cols_in_block * ysize) u16 BE entries,
COLUMN-MAJOR (cols_in_block = block_cols, or the remainder for the last
block). Column c of layer L lives in block c//block_cols; within the decoded
block it is the (c % block_cols)-th ysize*2-byte run. Max raw block =
block_cols*MAX_YSIZE*2 = 16*128*2 = 4096 B -> a 4 KB RAM decode buffer.

The C consumer is src/rsdk/colwindow.c (raw-DEFLATE inflate via the embedded
puff.c). The host gate tools/qa_ghz_cd_contention_gate.py round-trips every
block back to the Scene1.bin column-major bytes (lossless proof) and asserts
the file fits the 64 KB resident LWRAM region.
"""
import os
import struct
import sys
import zlib

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render_scene import parse_scene  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE = os.path.join(ROOT, "extracted", "Data", "Stages", "GHZ", "Scene1.bin")
OUT = os.path.join(ROOT, "cd", "GHZ1COL.BIN")

COLLISION_LAYER_NAMES = ("FG Low", "FG High")  # Zone fgLayer[0], fgLayer[1]
BLOCK_COLS = 16
TILE_SIZE = 16


def deflate(raw):
    """Raw DEFLATE (no zlib header/adler) at max ratio -> puff-decodable."""
    co = zlib.compressobj(9, zlib.DEFLATED, -15)
    return co.compress(raw) + co.flush()


def main():
    if not os.path.exists(SCENE):
        print("ERROR: Scene1.bin not found: %s" % SCENE)
        return 1
    layers = parse_scene(SCENE)
    by_name = {l["name"].rstrip("\x00"): (i, l) for i, l in enumerate(layers)}

    chosen = []
    for nm in COLLISION_LAYER_NAMES:
        if nm not in by_name:
            print("ERROR: collision layer %r not in scene (have %s)" %
                  (nm, [l["name"] for l in layers]))
            return 1
        chosen.append(by_name[nm])

    # Per-layer: column-major raw bytes, dims, block split.
    descs = []     # (layer_id, xs, ys, width_shift, num_blocks)
    blocks = []    # per-layer list of compressed block payloads
    for idx, l in chosen:
        xs, ys = l["xs"], l["ys"]
        width_shift = int(xs).bit_length() - 1
        if (1 << width_shift) != xs:
            print("ERROR: layer %r xsize %d not a power of two" %
                  (l["name"], xs))
            return 1
        # parse_scene yields ROW-MAJOR (entry(tx,ty) at ty*xs+tx). Transpose to
        # COLUMN-MAJOR so each world column is one contiguous run.
        layout = np.asarray(l["layout"], dtype="<u2").reshape(ys, xs)
        colmajor = np.ascontiguousarray(layout.T).astype(">u2").tobytes()
        colbytes = ys * 2
        num_blocks = (xs + BLOCK_COLS - 1) // BLOCK_COLS
        lb = []
        for b in range(num_blocks):
            c0 = b * BLOCK_COLS
            c1 = min(c0 + BLOCK_COLS, xs)
            raw = colmajor[c0 * colbytes: c1 * colbytes]
            lb.append(deflate(raw))
        descs.append((idx, xs, ys, width_shift, num_blocks))
        blocks.append(lb)

    num_layers = len(chosen)

    # --- compute layout offsets -----------------------------------------
    header_sz = 12 + num_layers * 12
    index_sz = sum((d[4] + 1) * 4 for d in descs)
    data_off0 = header_sz + index_sz

    out = bytearray()
    out += b"GCO3"
    out += struct.pack(">HHHH", num_layers, TILE_SIZE, BLOCK_COLS, 0)
    for (lid, xs, ys, wsh, nblk) in descs:
        out += struct.pack(">HHHHHH", lid, xs, ys, wsh, nblk, 0)

    # block-index tables, then payloads. Walk a running offset cursor.
    cursor = data_off0
    index_blob = bytearray()
    payload_blob = bytearray()
    for li in range(num_layers):
        offs = []
        for comp in blocks[li]:
            offs.append(cursor)
            payload_blob += comp
            cursor += len(comp)
        offs.append(cursor)  # end sentinel
        for o in offs:
            index_blob += struct.pack(">I", o)
    out += index_blob
    assert len(out) == data_off0, (len(out), data_off0)
    out += payload_blob

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(out)

    raw_total = sum((d[1] * d[2] * 2) for d in descs)
    max_block_comp = max(len(c) for lb in blocks for c in lb)
    max_block_raw = BLOCK_COLS * max(d[2] for d in descs) * 2
    print("build_collayout: wrote %s (%d bytes, 'GCO3' block-%d DEFLATE)" %
          (OUT, len(out), BLOCK_COLS))
    print("  uncompressed grid would be %d B -> %.1f%% (%.1fx smaller)" %
          (raw_total, 100.0 * len(out) / raw_total, raw_total / max(1, len(out))))
    print("  max compressed block = %d B; raw decode buffer needed = %d B" %
          (max_block_comp, max_block_raw))
    for (idx, xs, ys, wsh, nblk) in descs:
        print("  layer %d  %dx%d  width_shift=%d  blocks=%d" %
              (idx, xs, ys, wsh, nblk))
    return 0


if __name__ == "__main__":
    sys.exit(main())
