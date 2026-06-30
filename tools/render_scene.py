#!/usr/bin/env python3
"""
render_scene.py - Reconstruct an RSDKv5 stage to PNG from extracted assets, to
verify the tile/layout/palette parse before generating Saturn-native data.

Inputs (already decrypted by rsdk_extract.py), e.g. extracted/Data/Stages/GHZ/:
    16x16Tiles.gif   - tileset, 16px wide, 8bpp palette indices, N tiles tall
    Scene1.bin       - SCN: tile layers (zlib u16 layout, idx&0x3FF, flipX 0x400,
                       flipY 0x800, blank 0xFFFF)
    StageConfig.bin  - CFG: 8 palette banks (per-row RGB), authoritative palette

Formats ported verbatim from RSDKv5-Decompilation Scene.cpp / RetroEngine.cpp /
Reader.hpp (ReadCompressed = [u32 LE total][u32 BE usize][zlib total-4 bytes]).

Usage:
    python render_scene.py extracted/Data/Stages/GHZ --scene Scene1.bin --out ghz
"""
import argparse
import os
import struct
import zlib

import numpy as np
from PIL import Image

PALETTE_BANK_COUNT = 8


class R:
    def __init__(self, d): self.d, self.p = d, 0
    def u8(self):  v = self.d[self.p];                       self.p += 1; return v
    def u16(self): v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v
    def u32(self): v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v
    def skip(self, n): self.p += n
    def s(self):
        n = self.u8(); v = self.d[self.p:self.p + n]; self.p += n; return v

    def compressed(self):
        total = self.u32()                 # bytes following this field
        _usize = struct.unpack_from(">I", self.d, self.p)[0]; self.p += 4
        clen = total - 4
        z = self.d[self.p:self.p + clen]; self.p += clen
        return zlib.decompress(z)


def parse_palette(path):
    """Return a (256,3) uint8 RGB array from a CFG file's palette banks (bank 0),
    plus the per-bank/row data so callers can merge global+stage."""
    with open(path, "rb") as f:
        d = f.read()
    r = R(d)
    assert d[:4] == b"CFG\x00", "not a CFG file"
    r.p = 4
    r.u8()                                  # useGlobalObjects (StageConfig) ...
    # NOTE: GameConfig and StageConfig diverge before the palette; this function
    # is only used via the dedicated readers below.
    return d


def read_stage_palette(path):
    """StageConfig.bin -> (banks, masks): 8 banks each (256,3) uint8 (0 where
    unset) and the per-bank 16-bit active-row masks."""
    with open(path, "rb") as f:
        d = f.read()
    r = R(d); assert d[:4] == b"CFG\x00"; r.p = 4
    r.u8()                                  # useGlobalObjects
    objc = r.u8()
    for _ in range(objc):
        r.s()
    banks, masks = [], []
    for _ in range(PALETTE_BANK_COUNT):
        bank = np.zeros((256, 3), np.uint8)
        rows = r.u16()
        for row in range(16):
            if (rows >> row) & 1:
                for col in range(16):
                    rr, gg, bb = r.u8(), r.u8(), r.u8()
                    bank[(row << 4) + col] = (rr, gg, bb)
        banks.append(bank); masks.append(rows)
    return banks, masks


def read_global_palette(gameconfig_path):
    """GameConfig.bin global palette -> (banks, masks). Mania's shared colors
    (rings, HUD, common terrain) live in global bank 0."""
    d = open(gameconfig_path, "rb").read()
    r = R(d)
    assert d[:4] == b"CFG\x00", "not GameConfig"
    r.p = 4
    r.s(); r.s(); r.s()                     # title, subtitle, version
    r.u8(); r.u16()                         # activeCategory, startScene
    objc = r.u8()
    for _ in range(objc):
        r.s()
    banks, masks = [], []
    for _ in range(PALETTE_BANK_COUNT):
        bank = np.zeros((256, 3), np.uint8)
        rows = r.u16()
        for row in range(16):
            if (rows >> row) & 1:
                for col in range(16):
                    rr, gg, bb = r.u8(), r.u8(), r.u8()
                    bank[(row << 4) + col] = (rr, gg, bb)
        banks.append(bank); masks.append(rows)
    return banks, masks


def build_palette(gif_path, banks, masks, bank):
    """Faithful active palette = GameConfig global bank 0 (shared colors,
    indices 0-95) overlaid with the StageConfig stage `bank` (zone-specific).
    This mirrors the engine's fullPalette build. The tileset GIF's color table
    is a magenta placeholder in Mania and is NOT used. GameConfig is located
    relative to the tileset path (.../Data/Stages/<F>/.. -> .../Data/Game)."""
    data_dir = os.path.dirname(os.path.dirname(os.path.dirname(gif_path)))
    gc = os.path.join(data_dir, "Game", "GameConfig.bin")
    pal = np.zeros((256, 3), np.uint8)
    if os.path.exists(gc):
        gbanks, _ = read_global_palette(gc)
        pal = gbanks[0].copy()              # shared base (rings/HUD/common)
    for row in range(16):                   # overlay zone-specific stage bank
        if (masks[bank] >> row) & 1:
            lo = row << 4
            pal[lo:lo + 16] = banks[bank][lo:lo + 16]
    return pal


def load_tiles(gif_path):
    """Decode 16x16Tiles.gif -> (N,16,16) uint8 palette indices."""
    img = Image.open(gif_path)
    if img.mode != "P":
        img = img.convert("P")
    arr = np.array(img, dtype=np.uint8)         # (N*16, 16)
    h, w = arr.shape
    assert w == 16, f"expected 16px-wide tileset, got {w}"
    n = h // 16
    return arr[:n * 16].reshape(n, 16, 16)


def parse_scene(path):
    with open(path, "rb") as f:
        d = f.read()
    r = R(d)
    assert d[:4] == b"SCN\x00", "not an SCN scene"
    r.p = 4
    r.skip(0x10)                                # editor metadata
    sl = r.u8(); r.skip(sl + 1)                 # scene name + null
    layer_count = r.u8()
    layers = []
    for _ in range(layer_count):
        r.u8()                                  # visibleInEditor
        name = r.s().decode("latin-1")
        ltype = r.u8(); draw = r.u8()
        xs = r.u16(); ys = r.u16()
        parallax = r.u16(); scroll = r.u16()
        sic = r.u16()
        sinfo = []                              # per-band: (parallaxFactor, scrollSpeed, deform)
        for _ in range(sic):
            pf = r.u16(); ss = r.u16(); df = r.u8(); r.u8()
            if pf >= 0x8000: pf -= 0x10000       # i16
            if ss >= 0x8000: ss -= 0x10000
            sinfo.append((pf, ss, df))
        lstab = r.compressed()                  # line-scroll indexes: per pixel-row u8 -> band
        raw = r.compressed()                    # tile layout: xs*ys u16 LE
        layout = np.frombuffer(raw[:xs * ys * 2], dtype="<u2").reshape(ys, xs)
        layers.append({"name": name, "type": ltype, "xs": xs, "ys": ys,
                       "parallax": parallax, "scrollInfo": sinfo,
                       "lineScroll": lstab, "layout": layout})
    return layers


def render_layer(layer, tiles, palette):
    xs, ys, layout = layer["xs"], layer["ys"], layer["layout"]
    out = np.zeros((ys * 16, xs * 16, 3), np.uint8)
    for ty in range(ys):
        for tx in range(xs):
            e = int(layout[ty, tx])
            if e >= 0xFFFF:
                continue
            idx = e & 0x3FF
            if idx >= tiles.shape[0]:
                continue
            t = tiles[idx]
            if e & 0x400:
                t = t[:, ::-1]
            if e & 0x800:
                t = t[::-1, :]
            out[ty * 16:ty * 16 + 16, tx * 16:tx * 16 + 16] = palette[t]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage_dir")
    ap.add_argument("--scene", default="Scene1.bin")
    ap.add_argument("--out", default="render")
    ap.add_argument("--bank", type=int, default=0,
                    help="active palette bank (per-stage; 0 default)")
    args = ap.parse_args()

    gif = os.path.join(args.stage_dir, "16x16Tiles.gif")
    tiles = load_tiles(gif)
    banks, masks = read_stage_palette(os.path.join(args.stage_dir, "StageConfig.bin"))
    palette = build_palette(gif, banks, masks, getattr(args, "bank", 0))
    layers = parse_scene(os.path.join(args.stage_dir, args.scene))

    print(f"Tiles: {tiles.shape[0]}   Layers: {len(layers)}")
    os.makedirs(args.out, exist_ok=True)
    for i, ly in enumerate(layers):
        print(f"  layer {i}: '{ly['name']}' type={ly['type']} "
              f"{ly['xs']}x{ly['ys']} tiles  parallax={ly['parallax']}")
        img = render_layer(ly, tiles, palette)
        safe = ly["name"].strip() or f"layer{i}"
        safe = "".join(c if c.isalnum() else "_" for c in safe)
        Image.fromarray(img).save(os.path.join(args.out, f"{i}_{safe}.png"))
    print(f"Wrote PNGs to {args.out}/")


if __name__ == "__main__":
    main()
