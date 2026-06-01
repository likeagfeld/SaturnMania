#!/usr/bin/env python3
"""build_clouds_bg.py - Phase 1.34b: extract Title TileLayer 2 'Clouds'
into Saturn NBG2 cell-mode 8-bpp bitmap (cd/CLOUDS.DAT + cd/CLOUDS.PAL).

DECOMP AUTHORITY:
  tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-136 declares
  TileLayer 2 as the 'Clouds' band animated by Scanline_Clouds. The
  source layout is 16x16 tiles (256x256 px) per Scene1.bin parser
  (tools/extract_title_island.py).

SATURN BUILD CONTRACT:
  Output: 256x256 8-bpp cell-mode bitmap suitable for
  jo_vdp2_set_nbg2_8bits_image (jo-engine/jo_engine/vdp2.c:739).
  Cell-mode requires width + height multiples of 8 -- 256x256 is valid.

  jo's __jo_create_map (vdp2.c:240-266) bakes paloff = palette_id << 12
  into pattern-name words for the NBG2 plane.  The 256-color CRAM bank
  routed to NBG2 must be the palette-id allocated by
  jo_create_palette_from -- the same canonical 'V renders disk[V-1]'
  CRAM-shift convention applies (memory/jo-cram-off-by-one-shift.md).

PALETTE/PIXEL CONVENTION (mirrors Phase 1.34 build_clouds path):
  On-disk PAL byte word i holds RGB555 BE of QUANT color (i-1).  Disk
  slot 0 is zero (jo's reserved transparent slot).  Pixel bytes emitted
  are quant_index + 1 (range 1..255), so cell pixel value V renders
  CRAM[paloff + V] = jo PAL slot V = disk[V] when palette is shifted
  per shift_pal.py (new[i] = old[i+1], i.e. quant_pal[V-1]).

  Concretely: writer emits raw pixel = quant_idx; we then shift +1
  on disk so that on Saturn the cell pixel value V indexes the
  same color quant_idx = V-1 used at composite time.

SOURCE TRANSPARENCY HANDLING:
  Unlike build_title_island_bg.py's Island layer (93% magenta-
  transparent), the Clouds layer is OPAQUE -- every tile is a sky
  region with cloud puffs painted over a solid sky-blue base.  Per
  extract_title_island.py output the 256x256 composite has no gaps;
  most pixels are sky-blue with white/pale cloud highlights.

  Phase 1.34b doesn't try to make sky-blue transparent (that would
  collide with the back-color anyway -- the back-color IS sky-blue).
  Instead the cloud bitmap fully covers the NBG2 plane at priority 1
  (below all VDP1 sprites + RBG0 islands).  Mountain billboards from
  Phase 1.34/1.32 composite on top via VDP1 sprite priority 7.

CITATIONS:
  ST-058-R2-060194.pdf §11 (Color RAM RGB555 layout)
  ST-058-R2-060194.pdf §3.3 (NBG2 cell-mode scroll setup)
  jo-engine/jo_engine/vdp2.c:739 (jo_vdp2_set_nbg2_8bits_image)
  jo-engine/jo_engine/vdp2_malloc.c:60 (CRAM_PALETTE_SIZE + 1)
  tools/shift_pal.py (canonical jo CRAM single-shift)
  memory/jo-cram-off-by-one-shift.md (binding rule)
  tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-136 (Scanline_Clouds)
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow (pip install Pillow)")

ROOT      = Path(__file__).resolve().parent.parent
SCENE_BIN = ROOT / "extracted/Data/Stages/Title/Scene1.bin"
TILES_GIF = ROOT / "extracted/Data/Stages/Title/16x16Tiles.gif"

# Match decomp Clouds layer source dimensions (16x16 tiles = 256x256 px).
# This is also a Saturn-compatible cell-mode plane size (multiples of 8).
OUT_W = 256
OUT_H = 256

LAYER_CLOUDS = 2
TRANSPARENT_INDEX = 0


class R:
    def __init__(self, d): self.d, self.p = d, 0
    def u8 (self): v = self.d[self.p];                              self.p += 1; return v
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


def parse_all_layers(scene_bytes):
    r = R(scene_bytes)
    assert r.d[:4] == b"SCN\x00"
    r.p = 4
    r.take(0x10)
    nl = r.u8(); r.take(nl + 1)
    layer_count = r.u8()
    out = []
    for li in range(layer_count):
        r.u8()
        name = r.s().decode("ascii", "replace").strip("\x00")
        r.u8(); r.u8(); xs = r.u16(); ys = r.u16(); r.u16(); r.u16()
        sic = r.u16()
        for _ in range(sic): r.take(6)
        r.compressed()
        layout_bytes = r.compressed()
        cells = [struct.unpack_from("<H", layout_bytes, i*2)[0]
                 for i in range(xs * ys)]
        out.append({"index": li, "name": name, "xs": xs, "ys": ys,
                    "cells": cells})
    return out


def load_tiles(gif_path):
    img = Image.open(gif_path).convert("P")
    arr_w, arr_h = img.size
    assert arr_w == 16
    n_tiles = arr_h // 16
    px = img.load()
    pal = img.getpalette()
    tiles = []
    for ti in range(n_tiles):
        t = bytearray(16 * 16)
        for ty in range(16):
            for tx in range(16):
                t[ty*16 + tx] = px[tx, ti*16 + ty]
        tiles.append(bytes(t))
    return tiles, pal


def composite_clouds_layer(layer, tiles, src_pal_rgb):
    """Composite the 16x16-tile Clouds layer to a 256x256 RGB canvas.
    The layer source is fully opaque sky-blue with cloud puffs, so we
    treat all pixels as opaque (no transparency skip).  Tiles that hit
    GIF GCT slot 0 are still substituted with sky-blue (the cloud layer's
    base color) so we don't leave magenta speckles in the output.
    """
    xs, ys = layer["xs"], layer["ys"]
    assert xs * 16 == OUT_W and ys * 16 == OUT_H, (
        f"Clouds layer dims {xs*16}x{ys*16} != expected {OUT_W}x{OUT_H}")
    canvas = bytearray(OUT_W * OUT_H * 3)

    # Cloud-layer sky-blue base color is the most common pixel in the
    # source layer; verified via extract_title_island.py output of
    # title_layer_2_Clouds.png to be RGB(0, 96, 224).  We use this for
    # any GIF-transparency pixel so the magenta placeholder doesn't leak.
    SKY_BLUE_FALLBACK = (0, 96, 224)

    for cy in range(ys):
        for cx in range(xs):
            entry = layer["cells"][cy * xs + cx]
            tile_idx = entry & 0x3FF
            h_flip   = (entry >> 10) & 1
            v_flip   = (entry >> 11) & 1
            if tile_idx >= len(tiles):
                # Empty tile -> fill with sky-blue base
                for ty in range(16):
                    dy = cy * 16 + ty
                    for tx in range(16):
                        dx = cx * 16 + tx
                        off = (dy * OUT_W + dx) * 3
                        canvas[off + 0] = SKY_BLUE_FALLBACK[0]
                        canvas[off + 1] = SKY_BLUE_FALLBACK[1]
                        canvas[off + 2] = SKY_BLUE_FALLBACK[2]
                continue
            t = tiles[tile_idx]
            for ty in range(16):
                sy = (15 - ty) if v_flip else ty
                dy = cy * 16 + ty
                for tx in range(16):
                    sx = (15 - tx) if h_flip else tx
                    pi = t[sy * 16 + sx]
                    if pi == TRANSPARENT_INDEX:
                        r = SKY_BLUE_FALLBACK[0]
                        g = SKY_BLUE_FALLBACK[1]
                        b = SKY_BLUE_FALLBACK[2]
                    else:
                        r = src_pal_rgb[pi * 3 + 0]
                        g = src_pal_rgb[pi * 3 + 1]
                        b = src_pal_rgb[pi * 3 + 2]
                    dx = cx * 16 + tx
                    off = (dy * OUT_W + dx) * 3
                    canvas[off + 0] = r
                    canvas[off + 1] = g
                    canvas[off + 2] = b
    return canvas


def quantise_and_emit(rgb_bytes, out_dat, out_pal):
    """Quantise to 255 colors, emit raw 8bpp pixels + 256-entry RGB555 BE
    palette using the canonical shift_pal.py single-shift convention.

    Pixel-side: emit raw quant indices 0..254 (one byte each).  jo's
    CRAM lookup on Saturn reads cell-pixel value V at jo PAL slot V-1
    (per memory/jo-cram-off-by-one-shift.md).

    Palette-side: on-disk PAL writes new[i] = quant_pal[i+1] -- shifted
    DOWN by one slot from quant -- so that jo's CRAM read of slot V-1
    lands on quant_pal[V-1] (the color used at composite for byte V).

    Restating in shift_pal.py terms: shift_pal does new[i] = old[i+1]
    starting from a 256-entry old.  We achieve the same here by writing
    disk[i] for i = 0..253 = RGB555(quant_pal[i+1])... NO -- that's
    WRONG.

    Re-deriving from first principles (per build_title_island_bg.py
    citation chain at vdp2.c:240-266 + vdp2_malloc.c:60):

      1. cell pixel byte V at NBG2 reads CRAM[jo_base + V] where
         jo_base = (palette_id << 8) but the jo allocator offsets the
         entire bank by 1 slot, so the FIRST user palette occupies
         CRAM[257..511]: V reads CRAM[256 + V] = jo PAL entry V.
      2. jo PAL entry V comes from disk PAL entry V (jo_create_palette_
         from copies the source PAL into its CRAM bank without offset).
      3. BUT jo's printf reserves CRAM[256] (= disk[0] for the first
         user palette) for text color, plus another reservation -- the
         off-by-one rule per memory/jo-cram-off-by-one-shift.md is that
         the FIRST 256-color palette is shifted DOWN by one in CRAM,
         so cell pixel V reads disk PAL entry V-1.
      4. Therefore quant_pal[i] -- which we want to render for pixel
         value (i+1) -- must be written to disk[i+1].
         I.e. new[i+1] = quant_pal[i] for i = 0..254, equivalent to
         new[i] = quant_pal[i-1] for i in 1..255, new[0] = 0.

    That is EXACTLY the post-shift_pal.py output if shift_pal.py is
    applied to a hypothetical raw new[i] = quant_pal[i] palette.  We
    skip the intermediate file and apply the shift directly here.

    Pixel emission: raw[i] = quant_idx + 1 for each pixel, so on Saturn
    cell pixel V = (quant_idx + 1) reads CRAM[256 + V] = disk[V] =
    disk[quant_idx + 1] = quant_pal[quant_idx]. CORRECT.

    Constraint: quant must be <= 255 colors (so quant_idx range is
    0..254, pixel byte range is 1..255, never produces byte 0 which
    would index disk[0] = 0 = back-screen-transparent).
    """
    img = Image.frombytes("RGB", (OUT_W, OUT_H), bytes(rgb_bytes))
    quant = img.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    pal_rgb_full = quant.getpalette()
    n_colors = len(pal_rgb_full) // 3
    n_used = min(n_colors, 255)
    pal_rgb = pal_rgb_full[:n_used * 3]

    # Build 256-entry disk PAL.  The full Saturn-side chain is:
    #   - We emit pixel byte = quant_idx + 1  (byte range 1..255).
    #   - Saturn cell-mode reads NBG2 pixel V at jo PAL slot (V-1)
    #     per memory/jo-cram-off-by-one-shift.md.
    #   - So pixel byte (quant_idx + 1) reads disk[quant_idx + 1 - 1]
    #     = disk[quant_idx], which must equal RGB555(quant_pal[quant_idx]).
    #
    # Therefore disk[i] = RGB555(quant_pal[i]) for i = 0..n_used-1,
    # with disk[n_used..255] = 0.
    #
    # This is the same NO-shift convention as the GHZ sky palette pipeline
    # (cd/GHZ1SKY.PAL is post-shift_pal.py output; pixels are emitted with
    # the +1 offset baked into the per-pixel byte at convert time).  We
    # bake the per-pixel +1 directly above so no separate shift_pal pass
    # is needed.
    pal_bytes = bytearray(512)
    for i in range(n_used):
        r8 = pal_rgb[i * 3 + 0]
        g8 = pal_rgb[i * 3 + 1]
        b8 = pal_rgb[i * 3 + 2]
        r5 = r8 >> 3
        g5 = g8 >> 3
        b5 = b8 >> 3
        word = (r5 << 10) | (g5 << 5) | b5
        pal_bytes[i * 2:(i + 1) * 2] = struct.pack(">H", word)
    out_pal.write_bytes(bytes(pal_bytes))

    # Emit pixel bytes = quant_idx + 1, so byte range is 1..255 (never 0).
    raw = bytearray(quant.tobytes())
    out = bytearray(len(raw))
    for i in range(len(raw)):
        v = raw[i] + 1
        if v > 255:
            v = 255
        out[i] = v
    assert len(out) == OUT_W * OUT_H
    out_dat.write_bytes(bytes(out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dat", default=str(ROOT / "cd/CLOUDS.DAT"))
    ap.add_argument("--out-pal", default=str(ROOT / "cd/CLOUDS.PAL"))
    ap.add_argument("--preview",
                    default=str(ROOT / "tools/qa_golden/clouds_bg_preview.png"))
    args = ap.parse_args()

    scene_bytes = SCENE_BIN.read_bytes()
    layers = parse_all_layers(scene_bytes)
    by_idx = {L["index"]: L for L in layers}
    clouds = by_idx[LAYER_CLOUDS]
    print(f"Clouds layer: {clouds['xs']}x{clouds['ys']} tiles "
          f"({clouds['xs']*16}x{clouds['ys']*16} px)")

    tiles, pal_rgb = load_tiles(TILES_GIF)
    print(f"16x16Tiles.gif: {len(tiles)} tiles, {len(pal_rgb)//3}-entry palette")

    composite = composite_clouds_layer(clouds, tiles, pal_rgb)
    print(f"Composited clouds: {OUT_W}x{OUT_H} ({len(composite)}-byte RGB)")

    preview_path = Path(args.preview)
    preview_path.parent.mkdir(parents=True, exist_ok=True)
    Image.frombytes("RGB", (OUT_W, OUT_H), bytes(composite)).save(preview_path)
    print(f"wrote {preview_path} (pre-quantise preview)")

    out_dat = Path(args.out_dat)
    out_pal = Path(args.out_pal)
    out_dat.parent.mkdir(parents=True, exist_ok=True)
    quantise_and_emit(composite, out_dat, out_pal)
    print(f"wrote {out_dat} ({out_dat.stat().st_size} B, {OUT_W}x{OUT_H} 8bpp)")
    print(f"wrote {out_pal} ({out_pal.stat().st_size} B, 256-entry RGB555 BE)")


if __name__ == "__main__":
    main()
