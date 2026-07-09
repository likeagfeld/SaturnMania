#!/usr/bin/env python3
"""qa_ghz_cd_contention_gate.py - Task #180 step 4c RED gate.

The #180 step 4b runtime wiring streamed collision columns from CD every
frame (src/rsdk/colwindow.c colwindow_stream_column: GFS_Seek+GFS_Fread per
column). On the single-head Saturn CD that contends with the GHZ CD-DA music
track (Game.c:2150 jo_audio_play_cd_track(2,2,true)) and stalls the main
loop -> user-reported >10s title-card delay, no level music/SFX, super-slow
gameplay, invisible HUD/title-card/player.

The chosen fix ("Resident compressed layout"): ship the collision layout
block-DEFLATE compressed (cd/GHZ1COL.BIN 'GCO3'), load the whole compressed
file ONCE into free LWRAM (96 KB @ 0x278000) at scene start, then decode
columns RAM->RAM via an embedded puff inflater during play. Zero CD access
during gameplay, full #180 collision parity preserved.

This gate fires RED on the current 'GCO2' + per-frame-CD build and turns
GREEN only when all four invariants hold:

  P1  cd/GHZ1COL.BIN magic == 'GCO3'              (RED now: 'GCO2')
  P2  compressed file size <= 96 KB free region   (RED now: ~524 KB raw)
  P3  src/rsdk/colwindow.c calls GFS_Seek/GFS_Fread ONLY inside
      colwindow_open() - the per-column decode path is pure RAM->RAM
      (RED now: colwindow_stream_column has GFS_Seek+GFS_Fread)
  P4  every GCO3 block round-trips: raw-DEFLATE inflate of the shipped
      blocks byte-matches the column-major layout derived from Scene1.bin
      (RED now: no GCO3 blocks to inflate)

'GCO3' format (all multi-byte fields big-endian):
  [0..3]   magic 'GCO3'
  [4..5]   u16 num_layers
  [6..7]   u16 tile_size
  [8..9]   u16 block_cols          (columns per compressed block, 16)
  [10..11] u16 _reserved
  per-layer descriptor (num_layers x 12 B) @ off 12:
     u16 layer_id, u16 xsize, u16 ysize, u16 width_shift,
     u16 num_blocks (= ceil(xsize/block_cols)), u16 _pad
  block-index tables (per layer, in layer order), each
     (num_blocks+1) x u32 BE absolute file offsets; block i payload is
     [off[i], off[i+1]); the trailing entry is the end-of-block sentinel.
  raw-DEFLATE compressed block payloads.
Each block decompresses to (cols_in_block * ysize) u16 BE entries,
COLUMN-MAJOR (cols_in_block = block_cols, or the remainder for the last
block). Max raw block = 16*128*2 = 4096 B -> 4 KB decode scratch.
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
COLBIN = os.path.join(ROOT, "cd", "GHZ1COL.BIN")
COLWINDOW_C = os.path.join(ROOT, "src", "rsdk", "colwindow.c")

COLLISION_LAYER_NAMES = ("FG Low", "FG High")
# Resident region = the 64 KB freed by shrinking SCENE_LWRAM_ARENA 256->192 KB
# (0x002C0000..0x002D0000). The old 96 KB scratch @0x278000 is no longer free:
# the FR-2 entity MRU pool (#189) now owns 0x260000..0x290000. The top 4 KB of
# the region (0x002CF000..0x002D0000) is the block-decode scratch, so the
# resident GCO3 blob budget is COLWINDOW_RESIDENT_SIZE = 0xF000 (61440 B). The
# full carve-overlap proof is tools/qa_ghz_lwram_layout_gate.py.
FREE_LWRAM_BYTES = 0xF000   # 61440 B (64 KB region minus the 4 KB decode buf)
BLOCK_COLS = 16


def expected_columns():
    """Per-layer column-major raw u16-BE bytes, same as build_collayout."""
    layers = parse_scene(SCENE)
    by_name = {l["name"].rstrip("\x00"): (i, l) for i, l in enumerate(layers)}
    out = []
    for nm in COLLISION_LAYER_NAMES:
        idx, l = by_name[nm]
        xs, ys = l["xs"], l["ys"]
        layout = np.asarray(l["layout"], dtype="<u2").reshape(ys, xs)
        colmajor = np.ascontiguousarray(layout.T)  # (xs, ys)
        out.append((idx, xs, ys, colmajor.astype(">u2").tobytes()))
    return out


def parse_gco3(blob):
    if blob[0:4] != b"GCO3":
        raise ValueError("magic %r not GCO3" % blob[0:4])
    num_layers, tile_size, block_cols, _ = struct.unpack(">HHHH", blob[4:12])
    descs = []
    off = 12
    for _ in range(num_layers):
        lid, xs, ys, wsh, nblk, _pad = struct.unpack(">HHHHHH", blob[off:off + 12])
        descs.append((lid, xs, ys, wsh, nblk))
        off += 12
    tables = []
    for (_lid, _xs, _ys, _wsh, nblk) in descs:
        offs = struct.unpack(">%dI" % (nblk + 1), blob[off:off + 4 * (nblk + 1)])
        tables.append(offs)
        off += 4 * (nblk + 1)
    return num_layers, tile_size, block_cols, descs, tables


def main():
    fails = []

    if not os.path.exists(COLBIN):
        print("P1 FAIL: %s missing" % COLBIN)
        return 1
    blob = open(COLBIN, "rb").read()

    # ---- P1: magic 'GCO3' ------------------------------------------------
    magic = blob[0:4]
    if magic == b"GCO3":
        print("P1 PASS: cd/GHZ1COL.BIN magic == 'GCO3'")
    else:
        fails.append("P1: magic %r != 'GCO3' (still uncompressed CD-streamed)"
                     % magic)
        print("P1 FAIL: magic %r != 'GCO3'" % magic)

    # ---- P2: compressed size fits the free 96 KB LWRAM region ------------
    if len(blob) <= FREE_LWRAM_BYTES:
        print("P2 PASS: %d B <= %d B free LWRAM" % (len(blob), FREE_LWRAM_BYTES))
    else:
        fails.append("P2: file %d B > %d B free LWRAM region @0x278000"
                     % (len(blob), FREE_LWRAM_BYTES))
        print("P2 FAIL: %d B > %d B free LWRAM" % (len(blob), FREE_LWRAM_BYTES))

    # ---- P3: GFS only in colwindow_open ---------------------------------
    import re
    raw_src = open(COLWINDOW_C, "r", encoding="utf-8", errors="replace").read()
    # Strip C block + line comments so GFS_* mentions in prose don't count;
    # the invariant is about real code only. Replace with same-length spaces
    # so byte offsets (and func_at attribution) stay aligned.
    def _blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    src = re.sub(r"/\*.*?\*/|//[^\n]*", _blank, raw_src, flags=re.DOTALL)
    # Build an ordered list of (offset, func_name) for every function
    # definition: a line-anchored "[static] rettype name(args)" header. GFS
    # tokens only appear inside bodies, and headers are sequential, so each
    # token belongs to the nearest header that precedes it.
    headers = [(m.start(), m.group(1)) for m in re.finditer(
        r"(?m)^(?:static\s+)?[A-Za-z_][\w\*\s]*?\b(\w+)\s*\([^;{]*\)\s*$", src)]
    headers.sort()

    def func_at(pos):
        best = None
        for off, name in headers:
            if off < pos:
                best = name
            else:
                break
        return best
    bad = []
    for tok in ("GFS_Seek", "GFS_Fread"):
        start = 0
        while True:
            i = src.find(tok, start)
            if i < 0:
                break
            fn = func_at(i)
            if fn != "colwindow_open":
                bad.append("%s in %s()" % (tok, fn or "?"))
            start = i + 1
    if not bad:
        print("P3 PASS: GFS_Seek/GFS_Fread confined to colwindow_open()")
    else:
        fails.append("P3: CD access outside colwindow_open -> %s" % "; ".join(bad))
        print("P3 FAIL: %s" % "; ".join(bad))

    # ---- P4: round-trip every block vs Scene1.bin column-major bytes -----
    if magic == b"GCO3":
        try:
            exp = expected_columns()
            nl, tsz, bcols, descs, tables = parse_gco3(blob)
            if nl != len(exp):
                raise ValueError("num_layers %d != %d" % (nl, len(exp)))
            if bcols != BLOCK_COLS:
                raise ValueError("block_cols %d != %d" % (bcols, BLOCK_COLS))
            for li in range(nl):
                lid, xs, ys, wsh, nblk = descs[li]
                _eidx, exs, eys, ebytes = exp[li]
                if xs != exs or ys != eys:
                    raise ValueError("layer %d dims (%d,%d)!=(%d,%d)"
                                     % (li, xs, ys, exs, eys))
                colbytes = ys * 2
                rebuilt = bytearray()
                offs = tables[li]
                for b in range(nblk):
                    payload = blob[offs[b]:offs[b + 1]]
                    raw = zlib.decompress(payload, -15)  # raw DEFLATE
                    rebuilt += raw
                if bytes(rebuilt) != ebytes:
                    # locate first mismatch column
                    mm = next((k for k in range(min(len(rebuilt), len(ebytes)))
                               if rebuilt[k] != ebytes[k]), -1)
                    raise ValueError("layer %d round-trip mismatch at byte %d "
                                     "(col %d), len %d vs %d"
                                     % (li, mm, mm // colbytes if colbytes else -1,
                                        len(rebuilt), len(ebytes)))
            print("P4 PASS: all blocks round-trip byte-exact vs Scene1.bin")
        except Exception as e:
            fails.append("P4: %s" % e)
            print("P4 FAIL: %s" % e)
    else:
        fails.append("P4: cannot round-trip - not GCO3")
        print("P4 FAIL: not GCO3, no blocks to inflate")

    print("")
    if fails:
        print("GATE RED (%d failing): " % len(fails))
        for f in fails:
            print("  - %s" % f)
        return 1
    print("GATE GREEN: collision layout is RAM-resident compressed; zero CD "
          "access during gameplay.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
