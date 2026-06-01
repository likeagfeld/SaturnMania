#!/usr/bin/env python3
"""build_island_bg.py - Phase 1.35: extract decomp Title TileLayer 3
'Island' into a Saturn NBG1 cell-mode 8-bpp bitmap (cd/ISLAND.DAT +
cd/ISLAND.PAL).

DECOMP AUTHORITY
================
  tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-105 declares
  TileLayer 3 = 'Island' at drawGroup 1 (behind the Title3DSprite
  billboards at drawGroup 2; in front of the cloud layer at
  drawGroup 0 in foreach-order).

  TitleBG_Scanline_Island (TitleBG.c:138-140):
      RSDK.SetClipBounds(0, 0, 168, ScreenInfo->size.x, SCREEN_YSIZE);
  The decomp clips the island to screen Y >= 168 (i.e. the bottom
  ~56 rows of a 224-row screen).  The TileLayer 3 source bitmap is
  1024x1024 (64x64 tiles of 16x16) per Scene1.bin layer dims, but
  ONLY pixels that land in Y=[168..224] are ever displayed.

SATURN BUILD CONTRACT (Phase 1.35c — VRAM-bank-A0-fit revision)
==============================================================
  Output: 256x256 8-bpp cell-mode bitmap suitable for
  jo_vdp2_set_nbg1_8bits_image (jo-engine/jo_engine/vdp2.c:527).
  Cell-mode requires width + height multiples of 8 -- 256x256 valid.

  WHY 256x256 (Phase 1.35c, NOT 512x256 like Phase 1.35 v1):
    Phase 1.35 v1 shipped 512x256 = 131072 B (= 128KB).  jo's NBG1
    cell-mode allocation (jo_vdp2_set_nbg1_8bits_image -> jo_vdp2_malloc
    with usage JO_VDP2_RAM_CELL_NBG1) routes the request to VRAM bank
    A0 (jo_engine/vdp2_malloc.c:196-220).  Bank A0 = 128KB total
    (VDP2_VRAM_A0 .. VDP2_VRAM_A0+0x1FFFF).  A 128 KB allocation
    FILLS A0 entirely; downstream A0 allocations (e.g. nbg0_cell font
    bank, or any subsequent jo_vdp2_malloc for BITMAP_NBG1 / CELL_RBG0)
    return JO_NULL → garbage pointers → SGL state corruption observable
    as Sonic head / MANIA wordmark / WingShine visual scramble.
    Diagnosed by samples/qa_phase1_35b_diag.mcs (Phase 1.35b).
    256x256 = 65536 B = 64 KB leaves 64 KB headroom in A0.

  Crop strategy: the decomp Island layer composite is 1024x1024 with
  >90% magenta-transparent border surrounding a central region that
  holds the palm-tree island silhouette (per
  tools/qa_golden/title_layer_3_Island.png).  We crop a 256x256 rect
  CENTERED on the visible island bbox at native pixel scale (no
  downsample) -- losing some lateral palm-fronds but keeping the
  silhouette/water-frame readable on Saturn output.

  Top-transparent shaping: per the decomp clip Y=168..224 (only
  bottom 56 of 224 rows displayed), the Saturn island plane should
  paint only its lower portion.  We paint rows Y=0..96 as palette
  byte 0 (jo's transparent slot) and rows Y=96..256 as the island
  silhouette + sky-blue.  When NBG1 scrolls to align Y=96 of the
  plane with screen Y=168, the top 96 rows are invisible (mapped
  off-screen above) and the bottom 160 rows display the island.

PALETTE / PIXEL CONVENTION (mirrors Phase 1.34b proven GREEN code)
==================================================================
  Following tools/build_clouds_bg.py:241-281 (the Phase 1.34b code
  proven GREEN on Saturn capture - cf qa_phase1_34b_clouds_*.png):

    1. Quantise to 255 colors (Image.Quantize.MEDIANCUT).
    2. Disk palette word i (i=0..254) holds RGB555 BE of
       quant_pal[i]; disk[255] = 0.
    3. Pixel byte emitted = quant_idx + 1, so byte range is 1..255
       (never 0 = jo's transparent slot).
    4. Top transparent rows: pixel byte = 0 directly (slot 0 = jo
       reserved transparent).

  Saturn-side chain (per memory/jo-cram-off-by-one-shift.md +
  Phase 1.34b citation): cell pixel V reads CRAM[base + V] where
  the bank applies jo's reserved-slot offset; effectively pixel
  byte (quant_idx + 1) renders quant_pal[quant_idx] correctly.

  NOTE on +2 vs +1 byte offset: the Phase 1.35 task spec mentioned
  "+2 byte offset" but the Phase 1.34b GREEN reference code in
  build_clouds_bg.py uses +1 (255 colors, byte range 1..255).
  Since the cloud build is the PROVEN working Saturn-Green reference,
  we follow its convention rather than the speculative +2.  Cite:
  tools/build_clouds_bg.py:241-281 + Phase 1.34b GREEN capture in
  tools/qa_golden/qa_phase1_34b_clouds_*.png.

CITATIONS
=========
  ST-058-R2-060194.pdf §3.3 (NBG1 cell-mode scroll setup) + §11
      (Color RAM RGB555 BE layout, p.217)
  ST-238-R1-051795.pdf §slCharNbg1, §slPlaneNbg1, §slPriorityNbg1
  jo-engine/jo_engine/vdp2.c:527-543 (jo_vdp2_set_nbg1_8bits_image)
  jo-engine/jo_engine/vdp2_malloc.c:60 (CRAM reserved slot)
  tools/shift_pal.py (canonical jo CRAM single-shift)
  memory/jo-cram-off-by-one-shift.md (binding rule)
  tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-105 +
      :138-140 (Island layer + Scanline clip)
  tools/build_clouds_bg.py (Phase 1.34b GREEN reference)
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

# Phase 1.35c: Saturn output plane = 256x256 = 65536 B = 64 KB.
# Fits in VDP2 VRAM bank A0 (128 KB total) leaving 64 KB headroom for
# downstream A0 residents (nbg0_cell font / BITMAP_NBG1 / CELL_RBG0).
# Phase 1.35 v1's 512x256 = 131072 B FILLED A0, exhausting it for any
# subsequent jo_vdp2_malloc(JO_VDP2_RAM_CELL_NBG1/BITMAP_NBG1/CELL_RBG0)
# call and triggering SGL state corruption (visible as Sonic head /
# MANIA wordmark / WingShine scramble). Cell-mode requires width+height
# multiples of 8 -- 256x256 is valid.
OUT_W = 256
OUT_H = 256

# Top-half transparent band: rows 0..TRANSPARENT_TOP_ROWS hold pixel
# byte 0 (jo's transparent slot).  Rows TRANSPARENT_TOP_ROWS..OUT_H
# hold the actual island art.  96 rows transparent + 160 rows art
# is the Saturn-side approximation of the decomp Y=168..224 clip
# (the island plane scrolls vertically so that pixel-row 96 maps
# onto screen-row 168 = top of decomp-visible band).
TRANSPARENT_TOP_ROWS = 96

LAYER_ISLAND = 3
TRANSPARENT_INDEX = 0  # GIF palette slot used as transparent placeholder

# Sky-blue background color matching back-color (RGB(96,128,224)).
# Source-side decomp islandLayer composite uses magenta as the visible
# transparent placeholder; we substitute sky-blue so any leaked
# placeholder pixels match the surrounding back-color instead of
# fluorescent magenta.
SKY_BLUE_FALLBACK = (96, 128, 224)


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


def composite_island_full(layer, tiles, src_pal_rgb):
    """Composite the full Island layer (1024x1024) so we can later
    crop the central visible 512x256 rect.  Pixels coming from GIF
    palette slot 0 are treated as transparent placeholders (set to
    magenta sentinel for later detection)."""
    xs, ys = layer["xs"], layer["ys"]
    full_w = xs * 16
    full_h = ys * 16
    canvas = bytearray(full_w * full_h * 4)  # RGBA: alpha 0 = transparent
    for cy in range(ys):
        for cx in range(xs):
            entry = layer["cells"][cy * xs + cx]
            tile_idx = entry & 0x3FF
            h_flip   = (entry >> 10) & 1
            v_flip   = (entry >> 11) & 1
            if tile_idx == 0 or tile_idx >= len(tiles):
                # Empty cell -- leave canvas alpha 0 (transparent).
                continue
            t = tiles[tile_idx]
            for ty in range(16):
                sy = (15 - ty) if v_flip else ty
                dy = cy * 16 + ty
                for tx in range(16):
                    sx = (15 - tx) if h_flip else tx
                    pi = t[sy * 16 + sx]
                    dx = cx * 16 + tx
                    off = (dy * full_w + dx) * 4
                    if pi == TRANSPARENT_INDEX:
                        # GIF transparent slot -- keep alpha 0.
                        continue
                    r = src_pal_rgb[pi * 3 + 0]
                    g = src_pal_rgb[pi * 3 + 1]
                    b = src_pal_rgb[pi * 3 + 2]
                    canvas[off + 0] = r
                    canvas[off + 1] = g
                    canvas[off + 2] = b
                    canvas[off + 3] = 0xFF
    return canvas, full_w, full_h


def find_island_bbox(rgba, full_w, full_h):
    """Find the bounding box of non-transparent pixels (the visible
    island region) in the full layer composite."""
    min_x = full_w
    min_y = full_h
    max_x = -1
    max_y = -1
    for y in range(full_h):
        row_off = y * full_w * 4
        for x in range(full_w):
            if rgba[row_off + x * 4 + 3] == 0xFF:
                if x < min_x: min_x = x
                if y < min_y: min_y = y
                if x > max_x: max_x = x
                if y > max_y: max_y = y
    return min_x, min_y, max_x, max_y


def crop_to_saturn_plane(rgba, full_w, full_h, bbox):
    """Crop the visible island into a 256x256 Saturn-fit plane (Phase 1.35c).
    Top TRANSPARENT_TOP_ROWS rows of the plane stay transparent
    (mapped to off-screen above the decomp clip line on Saturn).
    The visible island art is placed in rows [TRANSPARENT_TOP_ROWS..OUT_H]
    of the plane and may be scaled down (preserving aspect) to fit the
    256-wide / 160-tall art area."""
    min_x, min_y, max_x, max_y = bbox
    isl_w = max_x - min_x + 1
    isl_h = max_y - min_y + 1
    print(f"  island bbox: ({min_x},{min_y})..({max_x},{max_y}) "
          f"= {isl_w}x{isl_h}")

    # Build 256x256 RGB plane.  Initialise to sky-blue fallback so any
    # gaps composite cleanly against the back-color.  We'll mark which
    # pixels are "real" island pixels via a parallel mask so the
    # quantiser sees only island colors (the sky-blue fallback row 0..95
    # gets overwritten with pixel byte 0 = transparent at emit time).
    plane_rgb = bytearray(OUT_W * OUT_H * 3)
    for i in range(0, len(plane_rgb), 3):
        plane_rgb[i + 0] = SKY_BLUE_FALLBACK[0]
        plane_rgb[i + 1] = SKY_BLUE_FALLBACK[1]
        plane_rgb[i + 2] = SKY_BLUE_FALLBACK[2]
    plane_mask = bytearray(OUT_W * OUT_H)  # 1 = island pixel

    # Available art area in plane: rows [TRANSPARENT_TOP_ROWS..OUT_H]
    # = 256x(256-96)=256x160 art region for Phase 1.35c (was 512x160
    # in Phase 1.35 v1).
    avail_h = OUT_H - TRANSPARENT_TOP_ROWS
    avail_w = OUT_W

    # Scale source island bbox to fit (preserve aspect, no upscale beyond
    # native size to avoid blur).  Phase 1.35c: typical bbox ~640x300
    # exceeds avail_w=256 + avail_h=160 -> scale ~0.4 down-sample.
    # nearest-neighbour preserves silhouette outlines acceptably for the
    # NBG1 cell-mode bitmap which only needs to show a recognisable
    # island shape behind the Mania logo.
    scale = 1.0
    if isl_w > avail_w or isl_h > avail_h:
        scale = min(avail_w / isl_w, avail_h / isl_h)
    dst_w = int(isl_w * scale)
    dst_h = int(isl_h * scale)

    # Center horizontally; align top of art to TRANSPARENT_TOP_ROWS.
    dst_x0 = (OUT_W - dst_w) // 2
    dst_y0 = TRANSPARENT_TOP_ROWS

    print(f"  scale={scale:.3f}  dst rect: ({dst_x0},{dst_y0}) "
          f"{dst_w}x{dst_h}")

    # Nearest-neighbour copy (preserves pixel-art crispness).
    for dy in range(dst_h):
        sy = min_y + int(dy / scale)
        if sy > max_y: sy = max_y
        for dx in range(dst_w):
            sx = min_x + int(dx / scale)
            if sx > max_x: sx = max_x
            soff = (sy * full_w + sx) * 4
            if rgba[soff + 3] != 0xFF:
                continue  # transparent source -> leave sky-blue
            px = dst_x0 + dx
            py = dst_y0 + dy
            doff = (py * OUT_W + px) * 3
            plane_rgb[doff + 0] = rgba[soff + 0]
            plane_rgb[doff + 1] = rgba[soff + 1]
            plane_rgb[doff + 2] = rgba[soff + 2]
            plane_mask[py * OUT_W + px] = 1
    return plane_rgb, plane_mask


def quantise_and_emit(plane_rgb, plane_mask, out_dat, out_pal):
    """Quantise to 255 colors, emit raw 8bpp pixels + 256-entry
    RGB555 BE palette using the Phase 1.34b proven GREEN convention:
       - Palette: disk[i] = RGB555(quant_pal[i]) for i = 0..n_used-1.
       - Pixel: byte = quant_idx + 1 for art pixels (range 1..255).
       - Pixel: byte = 0 for rows 0..TRANSPARENT_TOP_ROWS (transparent).

    Citation: tools/build_clouds_bg.py:241-281 (Phase 1.34b GREEN
    capture qa_phase1_34b_clouds_*.png).
    """
    img = Image.frombytes("RGB", (OUT_W, OUT_H), bytes(plane_rgb))
    quant = img.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    pal_rgb_full = quant.getpalette()
    n_colors = len(pal_rgb_full) // 3
    n_used = min(n_colors, 255)
    pal_rgb = pal_rgb_full[:n_used * 3]

    # 256-entry disk PAL: disk[i] = RGB555(quant_pal[i]); rest = 0.
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

    # Emit pixel bytes.  For transparent top rows (0..TRANSPARENT_TOP_ROWS)
    # emit byte 0 directly (jo's transparent slot).  For art rows emit
    # quant_idx + 1 (range 1..255).
    raw = quant.tobytes()
    out = bytearray(len(raw))
    for y in range(OUT_H):
        for x in range(OUT_W):
            i = y * OUT_W + x
            if y < TRANSPARENT_TOP_ROWS:
                out[i] = 0  # transparent
            else:
                v = raw[i] + 1
                if v > 255:
                    v = 255
                out[i] = v
    assert len(out) == OUT_W * OUT_H
    out_dat.write_bytes(bytes(out))

    return n_used


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dat", default=str(ROOT / "cd/ISLAND.DAT"))
    ap.add_argument("--out-pal", default=str(ROOT / "cd/ISLAND.PAL"))
    ap.add_argument("--preview",
                    default=str(ROOT / "tools/qa_golden/island_bg_preview.png"))
    args = ap.parse_args()

    scene_bytes = SCENE_BIN.read_bytes()
    layers = parse_all_layers(scene_bytes)
    by_idx = {L["index"]: L for L in layers}
    island = by_idx[LAYER_ISLAND]
    print(f"Island layer: {island['xs']}x{island['ys']} tiles "
          f"({island['xs']*16}x{island['ys']*16} px)")

    tiles, pal_rgb = load_tiles(TILES_GIF)
    print(f"16x16Tiles.gif: {len(tiles)} tiles, "
          f"{len(pal_rgb)//3}-entry palette")

    rgba, full_w, full_h = composite_island_full(island, tiles, pal_rgb)
    print(f"Composited full island layer: {full_w}x{full_h} RGBA")

    bbox = find_island_bbox(rgba, full_w, full_h)
    if bbox[2] < 0:
        sys.exit("ERROR: no non-transparent pixels in Island layer.")

    plane_rgb, plane_mask = crop_to_saturn_plane(rgba, full_w, full_h, bbox)
    n_island_px = sum(plane_mask)
    print(f"Saturn plane: {OUT_W}x{OUT_H}  island pixels: {n_island_px}")

    preview_path = Path(args.preview)
    preview_path.parent.mkdir(parents=True, exist_ok=True)
    Image.frombytes("RGB", (OUT_W, OUT_H), bytes(plane_rgb)).save(preview_path)
    print(f"wrote {preview_path} (pre-quantise preview)")

    out_dat = Path(args.out_dat)
    out_pal = Path(args.out_pal)
    out_dat.parent.mkdir(parents=True, exist_ok=True)
    n_used = quantise_and_emit(plane_rgb, plane_mask, out_dat, out_pal)
    print(f"wrote {out_dat} ({out_dat.stat().st_size} B, "
          f"{OUT_W}x{OUT_H} 8bpp)")
    print(f"wrote {out_pal} ({out_pal.stat().st_size} B, "
          f"256-entry RGB555 BE; {n_used} used)")


if __name__ == "__main__":
    main()
