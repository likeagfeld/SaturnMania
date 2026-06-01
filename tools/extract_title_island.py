#!/usr/bin/env python3
"""extract_title_island.py - Phase 1.29c step 1: extract the Mania title
TileLayer 3 (the rotating 'island' layer per
tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103) into a viewable
PNG so we can confirm the decomp-canonical island looks correct before
converting it to Saturn's RBG0 backdrop format.

Scene.bin layer-block layout mirrors src/rsdk/storage.c:227-260 and
tools/parse_title_entities.py.  Layer 3's compressed tile-layout chunk
holds xs*ys u16 entries; each entry packs `tile_index | (h_flip<<10) |
(v_flip<<11)` per RSDKv5 convention.  16x16Tiles.gif is a column of
stacked 16-pixel-tall tiles (1024 tiles total per RSDK 10-bit index).
"""
import struct, sys, zlib
from pathlib import Path
try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow (pip install Pillow)")

ROOT       = Path(__file__).resolve().parent.parent
SCENE_BIN  = ROOT / "extracted/Data/Stages/Title/Scene1.bin"
TILES_GIF  = ROOT / "extracted/Data/Stages/Title/16x16Tiles.gif"
OUT_DIR    = ROOT / "tools/qa_golden"
OUT_DIR.mkdir(parents=True, exist_ok=True)

# --- Scene1.bin layer reader --------------------------------------------
class R:
    def __init__(self, d): self.d, self.p = d, 0
    def u8 (self): v = self.d[self.p];                            self.p += 1; return v
    def u16(self): v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v
    def u32(self): v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v
    def take(self, n): v = self.d[self.p:self.p+n]; self.p += n; return v
    def s(self): n = self.u8(); v = self.d[self.p:self.p+n]; self.p += n; return v
    def compressed(self):
        total  = self.u32()
        _usize = struct.unpack_from(">I", self.d, self.p)[0]; self.p += 4
        clen   = total - 4
        z      = self.d[self.p:self.p + clen]; self.p += clen
        return zlib.decompress(z)

def parse_layers(scene_bytes):
    r = R(scene_bytes)
    assert r.d[:4] == b"SCN\x00", "not an SCN scene"
    r.p = 4
    r.take(0x10)
    nl = r.u8(); r.take(nl + 1)
    layer_count = r.u8()
    layers = []
    for li in range(layer_count):
        r.u8()                              # visibleInEditor
        name = r.s().decode("ascii", "replace")
        type_  = r.u8(); draw_group = r.u8()
        xs   = r.u16();  ys = r.u16()
        para = r.u16();  scr = r.u16()
        sic  = r.u16()
        for _ in range(sic): r.take(6)
        r.compressed()                      # line-scroll table (unused)
        layout_bytes = r.compressed()
        assert len(layout_bytes) == xs * ys * 2, (
            f"layer {li} '{name}' layout {len(layout_bytes)} != {xs}*{ys}*2 = {xs*ys*2}"
        )
        # u16 per cell: bits 0-9 = tile idx, bit 10 = h_flip, bit 11 = v_flip
        cells = [struct.unpack_from("<H", layout_bytes, i*2)[0]
                 for i in range(xs * ys)]
        layers.append({
            "index": li, "name": name, "type": type_, "draw_group": draw_group,
            "xs": xs, "ys": ys, "parallax": para, "scroll": scr,
            "cells": cells,
        })
    return layers


# --- 16x16Tiles.gif reader (column of stacked 16-tall tiles) ------------
def load_tiles(gif_path):
    img = Image.open(gif_path)
    # GIF mode is 'P' (palette). Read the GCT.
    pal = img.getpalette()      # 768-entry list: [r0,g0,b0, r1,g1,b1, ...]
    indexed = img.convert("P")  # ensure indexed mode
    arr_w, arr_h = indexed.size
    assert arr_w == 16, f"16x16Tiles.gif width {arr_w} != 16 (expected stacked column)"
    n_tiles = arr_h // 16
    px = indexed.load()
    tiles = []
    for ti in range(n_tiles):
        # Each tile is 16x16 of palette indices
        tile = bytearray(16 * 16)
        for ty in range(16):
            for tx in range(16):
                tile[ty*16 + tx] = px[tx, ti*16 + ty]
        tiles.append(bytes(tile))
    return tiles, pal, n_tiles


def composite_layer(layer, tiles, pal):
    xs, ys = layer["xs"], layer["ys"]
    out_w  = xs * 16
    out_h  = ys * 16
    canvas = bytearray(out_w * out_h)
    for cy in range(ys):
        for cx in range(xs):
            entry = layer["cells"][cy*xs + cx]
            tile_idx = entry & 0x3FF
            h_flip   = (entry >> 10) & 1
            v_flip   = (entry >> 11) & 1
            if tile_idx >= len(tiles):
                continue
            t = tiles[tile_idx]
            for ty in range(16):
                sy = (15 - ty) if v_flip else ty
                for tx in range(16):
                    sx = (15 - tx) if h_flip else tx
                    canvas[(cy*16+ty) * out_w + (cx*16+tx)] = t[sy*16 + sx]
    return canvas, out_w, out_h, pal


# --- Main ---------------------------------------------------------------
def main():
    scene_bytes = SCENE_BIN.read_bytes()
    layers = parse_layers(scene_bytes)

    print(f"=== Title/Scene1.bin: {len(layers)} layers ===")
    for L in layers:
        print(f"  L{L['index']} {L['name']!r}  type={L['type']} "
              f"drawGroup={L['draw_group']}  {L['xs']}x{L['ys']} tiles "
              f"({L['xs']*16}x{L['ys']*16} px)  parallax={L['parallax']} scroll={L['scroll']}")

    tiles, pal, n_tiles = load_tiles(TILES_GIF)
    print(f"=== 16x16Tiles.gif: {n_tiles} tiles, palette has {len(pal)//3} entries ===")

    # Per TitleBG.c:103-105, TileLayer 3 = island.  Also dump all layers
    # so we have ground truth for cloud + sky too.
    for L in layers:
        canvas, w, h, plt = composite_layer(L, tiles, pal)
        img = Image.frombytes("P", (w, h), bytes(canvas))
        img.putpalette(plt)
        safe_name = L['name'].replace(' ', '_').replace('\x00', '').strip('_')
        out = OUT_DIR / f"title_layer_{L['index']}_{safe_name}.png"
        img.convert("RGB").save(out)
        print(f"  wrote {out.name}  ({w}x{h})")

if __name__ == "__main__":
    main()
