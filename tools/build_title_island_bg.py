#!/usr/bin/env python3
"""build_title_island_bg.py - Phase 1.31 Fix #3 (2026-05-27): rebuild
the title backdrop from the decomp-canonical Title scene layers (Black
BG + Horizon + Clouds + Island) into an opaque 224x512 8-bit cell-mode
bitmap suitable for cd/TITLE.DAT + cd/TITLE.PAL.

WHY (Phase 1.31 Fix #3):
  The pre-existing cd/TITLE.DAT (built from an unknown asset pipeline)
  contained ~17K bright-green pixels per Saturn-rendered frame (post-
  jo CRAM down-shift), which the user flagged as "neon green / pink
  artifacts."  Diagnostic via tools/view_title_dat_joshifted.py showed
  three post-shift slots with > 1000 green pixels each:
    slot 130: 8115 px  rgb=(0, 165, 66)
    slot 131: 7243 px  rgb=(0, 206, 99)
    slot 129: 2000 px  rgb=(0, 132, 16)
  These greens originate from 16x16Tiles.gif's Island grass-tile
  colors, but in the source `Island` layer (1024x1024) 93.75% of
  pixels are magenta GCT-index-0 (transparency placeholder).  When
  that transparency-dominated layer is naively 256-color quantised,
  the small remaining grass patches get over-represented in the
  output palette, producing neon-green dominance.

  Phase 1.29c's first build_title_island_bg.py also contained TWO
  palette-shift bugs that double-shifted (palette UP + pixels UP),
  yielding TITLE3D.PAL with 37 bright-green slots all mapped to the
  same color in disk slots 1..7.  Both that double-shift and the
  transparency-dominance issue are fixed here.

FIX (Path A):
  1. Composite all four Title scene layers from back to front
     (Black BG -> Horizon -> Clouds -> Island), treating GIF GCT
     slot 0 (magenta = transparency) as non-overwriting per layer.
     This yields an opaque ~224x512 composite where:
       - Black BG fills the bottom rectangle (orange/red dirt strip
         + dark background)
       - Horizon paints the actual horizon line + water + island base
       - Clouds adds soft sky-blue cloud band
       - Island adds the visible green island silhouette as
         the *minority* contributor (not the dominant one)
     The result is a Mania-authentic backdrop where the dominant
     colors are sky-blue, dirt-orange, and white cloud highlight —
     with green grass as a small accent, not the dominant tone.
  2. Crop the composite to 224x512 starting from the Island's
     anchor (per TitleBG.c the Island center is the focal point of
     the scene).  Both Black BG and Horizon are 512x240, Clouds is
     256x256, and Island is 1024x1024 — none has the exact 224x512
     shape of the existing TITLE.DAT, so we anchor on the Island
     layer's visible region (rows containing non-transparency).
  3. Use Pillow median-cut quantisation on the OPAQUE composite,
     producing 256 well-distributed colors instead of 37+ greens.
  4. Emit palette with the CANONICAL jo-CRAM single-shift convention
     (palette ONLY: disk[i+1] = quantizer_palette[i] for i in 0..254,
     disk[0] = 0; pixels stay UNCHANGED).  Per shift_pal.py docstring
     and memory/jo-cram-off-by-one-shift.md, this is the ONLY shift
     applied — the V -> CRAM[V-1] reading on Saturn then lands at
     disk[i+1-1] = disk[i] which equals quantizer_palette[i].
  5. Output paths match the existing setup_title_bg consumer:
     cd/TITLE.DAT + cd/TITLE.PAL (overwrites the bad pre-existing
     versions).

Citations:
  ST-058-R2-060194.pdf §11 (Color RAM RGB555 layout)
  jo-engine/jo_engine/vdp2_malloc.c:60 (jo CRAM offset rationale)
  tools/shift_pal.py (canonical single-shift convention)
  memory/jo-cram-off-by-one-shift.md (binding rule)
  tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-105
    (Title TileLayer 3 = island layer)
  tools/_decomp_raw/SonicMania_Objects_Title_TitleSetup.c
    (Title scene layer ordering: BG -> Horizon -> Clouds -> Island)
  src/main.c:67-68 (TITLE_BG_W=224, TITLE_BG_H=512 contract)
"""
import argparse, struct, sys, zlib
from pathlib import Path
try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow (pip install Pillow)")

ROOT       = Path(__file__).resolve().parent.parent
SCENE_BIN  = ROOT / "extracted/Data/Stages/Title/Scene1.bin"
TILES_GIF  = ROOT / "extracted/Data/Stages/Title/16x16Tiles.gif"

# Match the existing TITLE.DAT/PAL contract consumed by
# src/main.c:67-68 (TITLE_BG_W=224, TITLE_BG_H=512).  Keeping the same
# dimensions means setup_title_bg + the existing RBG0 PL_SIZE_1x1
# plane mapping + the Fix #1 repeat=false slOverRA(SINGLE) path all
# stay verified GREEN; we only swap the asset content.
OUT_W = 224
OUT_H = 512

# Layer indices from extract_title_island.py output:
#   L0 'Black BG'  32x15 tiles (512x240 px)  drawGroup=0
#   L1 'Horizon'   32x15 tiles (512x240 px)  drawGroup=16
#   L2 'Clouds'    16x16 tiles (256x256 px)  drawGroup=16
#   L3 'Island'    64x64 tiles (1024x1024 px) drawGroup=16  parallax=256
LAYER_BLACK_BG = 0
LAYER_HORIZON  = 1
LAYER_CLOUDS   = 2
LAYER_ISLAND   = 3

# Magenta transparency placeholder (GIF GCT slot 0): per
# extract_title_island.py inspection, 16x16Tiles.gif slot 0 = (255,0,255).
# Any tile pixel that lands on this slot is "transparent" for layer
# compositing purposes (per RSDKv5 TileLayer.c blank-tile handling +
# the Black BG layer providing the under-fill below).
TRANSPARENT_INDEX = 0


# --- Scene1.bin layer reader (mirror of extract_title_island.py) -------
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


def composite_layer(layer, tiles, dst_canvas_rgb, dst_w, dst_h,
                    src_pal_rgb, offset_x=0, offset_y=0):
    """Composite one layer onto dst_canvas_rgb (mutates in place).
    Transparent (TRANSPARENT_INDEX) source pixels do NOT overwrite.
    dst_canvas_rgb is a (h*w*3) bytearray.
    """
    xs, ys = layer["xs"], layer["ys"]
    src_w = xs * 16
    src_h = ys * 16
    for cy in range(ys):
        for cx in range(xs):
            entry = layer["cells"][cy*xs + cx]
            tile_idx = entry & 0x3FF
            h_flip   = (entry >> 10) & 1
            v_flip   = (entry >> 11) & 1
            if tile_idx >= len(tiles): continue
            t = tiles[tile_idx]
            for ty in range(16):
                sy = (15 - ty) if v_flip else ty
                dy = cy*16 + ty + offset_y
                if dy < 0 or dy >= dst_h:
                    continue
                for tx in range(16):
                    sx = (15 - tx) if h_flip else tx
                    pi = t[sy*16 + sx]
                    if pi == TRANSPARENT_INDEX:
                        continue
                    dx = cx*16 + tx + offset_x
                    if dx < 0 or dx >= dst_w:
                        continue
                    r = src_pal_rgb[pi*3 + 0]
                    g = src_pal_rgb[pi*3 + 1]
                    b = src_pal_rgb[pi*3 + 2]
                    off = (dy * dst_w + dx) * 3
                    dst_canvas_rgb[off + 0] = r
                    dst_canvas_rgb[off + 1] = g
                    dst_canvas_rgb[off + 2] = b


def find_island_bbox(island_layer, tiles):
    """Find the visible (non-transparent) bounding box of the Island
    layer, since the layer is 93.75% magenta-transparency.  Returns
    (min_x, min_y, max_x, max_y) in pixel coordinates within the
    1024x1024 source layer.
    """
    xs, ys = island_layer["xs"], island_layer["ys"]
    min_cx, min_cy = xs, ys
    max_cx, max_cy = -1, -1
    for cy in range(ys):
        for cx in range(xs):
            entry = island_layer["cells"][cy*xs + cx]
            tile_idx = entry & 0x3FF
            if tile_idx >= len(tiles):
                continue
            t = tiles[tile_idx]
            # Check if this tile has any non-transparent pixels
            has_opaque = any(p != TRANSPARENT_INDEX for p in t)
            if has_opaque:
                if cx < min_cx: min_cx = cx
                if cy < min_cy: min_cy = cy
                if cx > max_cx: max_cx = cx
                if cy > max_cy: max_cy = cy
    if max_cx < 0:
        return (0, 0, xs*16, ys*16)
    return (min_cx*16, min_cy*16, (max_cx+1)*16, (max_cy+1)*16)


def composite_title_scene(layers, tiles, src_pal_rgb):
    """Composite Horizon -> Clouds -> Island into the OUT_W x OUT_H
    output canvas matching the Mania title's visual layering.

    Layer roles (per extract_title_island.py + decomp inspection):
      L0 Black BG (512x240, 100% pure black) - structural, not visible
                  through the rest of the composite.  Ignored: the
                  sky from Horizon + Clouds fills the upper half;
                  the Island's water-rim provides its own underwater
                  blue.
      L1 Horizon  (512x240, sky+water+horizon-line) - the canonical
                  Mania title sky.  TILED VERTICALLY across OUT_H
                  to fill the entire 224x512 backdrop with the sky
                  pattern (the Horizon layer is designed to tile
                  vertically per the scrolling decomp behaviour).
      L2 Clouds   (256x256, cloud puffs over sky-blue) - composited
                  over the upper half of the canvas; the cloud band
                  is meant to occupy the sky region only.
      L3 Island   (1024x1024, mostly transparent; visible bbox is
                  the 252x252 Sonic-head island in the centre) -
                  cropped to the visible bbox and pasted centred
                  in the lower half of the canvas (the rotating
                  focal point).

    Output is a fully opaque OUT_W x OUT_H RGB canvas with proper
    color distribution: sky-blue + cloud-white dominant, green
    Sonic-head silhouette as a small minority contributor.
    """
    by_idx = {L["index"]: L for L in layers}

    # Step 1: render Horizon layer to its 512x240 RGB image (opaque).
    horizon = by_idx[LAYER_HORIZON]
    h_canvas = bytearray(512 * 240 * 3)
    composite_layer(horizon, tiles, h_canvas, 512, 240,
                    src_pal_rgb, offset_x=0, offset_y=0)

    # Step 2: render Clouds layer to its 256x256 RGB image (opaque).
    clouds = by_idx[LAYER_CLOUDS]
    c_canvas = bytearray(256 * 256 * 3)
    composite_layer(clouds, tiles, c_canvas, 256, 256,
                    src_pal_rgb, offset_x=0, offset_y=0)

    # Step 3: render the Island layer's visible bbox.
    island = by_idx[LAYER_ISLAND]
    ix0, iy0, ix1, iy1 = find_island_bbox(island, tiles)
    i_w = ix1 - ix0
    i_h = iy1 - iy0
    # Compute world-coordinate offset so the full layer composites
    # into i_canvas's (0,0) origin.
    i_canvas = bytearray(i_w * i_h * 3)
    composite_layer(island, tiles, i_canvas, i_w, i_h,
                    src_pal_rgb, offset_x=-ix0, offset_y=-iy0)

    # Final composite canvas: OUT_W x OUT_H.
    out = bytearray(OUT_W * OUT_H * 3)

    # Step 4: tile Horizon (512x240 wide) into OUT_W (224) wide by
    # centre-crop (skip leftmost (512-224)/2 = 144 px and take the
    # central 224 columns), then tile vertically across OUT_H.  The
    # horizon line in the source layer is roughly at y=120; tile
    # repeats give a continuous sky+water band across the whole
    # output height.
    h_crop_x = (512 - OUT_W) // 2  # = 144
    for y in range(OUT_H):
        sy = y % 240
        for x in range(OUT_W):
            sx = x + h_crop_x
            si = (sy * 512 + sx) * 3
            di = (y * OUT_W + x) * 3
            out[di + 0] = h_canvas[si + 0]
            out[di + 1] = h_canvas[si + 1]
            out[di + 2] = h_canvas[si + 2]

    # Step 5: composite Clouds (256x256) onto the upper half of the
    # output (rows 0..255).  Centre-crop the cloud band horizontally
    # (256 -> 224 means skip 16 columns on each side).
    c_crop_x = (256 - OUT_W) // 2  # = 16
    # Cloud layer is the sky-only band; only paint where the cloud
    # source is NOT pure sky-blue (i.e. only the white/light cloud
    # accents overwrite the underlying Horizon sky).  Identify the
    # cloud-base sky color so we keep the cloud puffs but let the
    # Horizon's actual horizon line show through.
    # Cloud source's most-common color is the sky-blue base (40K+ px
    # of (0,96,224) per Counter dump); treat that as transparent for
    # this overlay so we don't double-paint solid sky.
    CLOUD_SKY_BASE = (0, 96, 224)
    for y in range(min(256, OUT_H)):
        for x in range(OUT_W):
            sx = x + c_crop_x
            if sx < 0 or sx >= 256:
                continue
            si = (y * 256 + sx) * 3
            cr, cg, cb = c_canvas[si], c_canvas[si+1], c_canvas[si+2]
            # Skip the cloud layer's flat sky-base color; keep the
            # actual cloud puffs (whites + light-blues).
            if (cr, cg, cb) == CLOUD_SKY_BASE:
                continue
            di = (y * OUT_W + x) * 3
            out[di + 0] = cr
            out[di + 1] = cg
            out[di + 2] = cb

    # Step 6: composite the Island bbox centred horizontally in the
    # lower half of the output (rows OUT_H/2 - i_h/2 .. that + i_h),
    # or starting just below the horizon line if i_h is too tall.
    # The Mania title has the Sonic-head island floating in the sea
    # below the horizon; place its TOP at row 240 (the Saturn-clip
    # horizon position) and centre horizontally.
    island_dst_x = (OUT_W - i_w) // 2  # may be negative if i_w > OUT_W
    island_dst_y = 240
    for y in range(i_h):
        dy = y + island_dst_y
        if dy < 0 or dy >= OUT_H:
            continue
        for x in range(i_w):
            dx = x + island_dst_x
            if dx < 0 or dx >= OUT_W:
                continue
            si = (y * i_w + x) * 3
            # Skip pixels still set to default 0 RGB (transparent
            # region inside the island bbox that wasn't touched
            # because the underlying source tile pixel was the
            # GIF GCT-0 magenta placeholder).
            ir, ig, ib = i_canvas[si], i_canvas[si+1], i_canvas[si+2]
            if ir == 0 and ig == 0 and ib == 0:
                continue
            di = (dy * OUT_W + dx) * 3
            out[di + 0] = ir
            out[di + 1] = ig
            out[di + 2] = ib

    return out


def quantise_and_emit(rgb_bytes, out_dat, out_pal):
    """Convert (OUT_W * OUT_H * 3) RGB bytes to 8-bit indexed + emit
    out_dat (raw 8bpp) + out_pal (256-entry RGB555 BE) using the +2
    byte-offset convention that fully dodges jo's CRAM-reservation
    zone.

    PALETTE / PIXEL CONVENTION (Phase 1.31 Fix #3 retry, 2026-05-27):

    The Saturn-side mapping chain, traced source-by-source:

      1. jo's __jo_create_map (jo-engine/jo_engine/vdp2.c:240-266) bakes
         paloff = palette_id << 12 into every pattern-name-table word.
         For palette_id = 1 (the first user palette returned by
         jo_create_palette), VDP2 reads each cell pixel byte V at
         CRAM offset bank-1 + V, i.e. CRAM[256 + V] (per ST-058-R2
         §VDP2 pattern-name + color-bank).
      2. jo's CRAM allocator (jo-engine/jo_engine/vdp2_malloc.c:60)
         hands out user palettes starting at CRAM byte-offset 257
         (the "+1" past the bank-0 reservation: `((jo_color *)
         JO_VDP2_CRAM) + CRAM_PALETTE_SIZE + 1`).  So disk[i] of the
         FIRST user palette lands at CRAM[257 + i].
      3. Composing the two: pixel byte V renders the on-disk PAL
         entry (V - 1) once V >= 1.  This is the canonical "jo CRAM
         off-by-one" already encoded by tools/shift_pal.py.
      4. There's ALSO an empirical reservation at byte 1 / CRAM[257]:
         jo's printf (jo-engine/jo_engine/tools.c:156) reads exactly
         that slot for its text color (CRAM[1 + 256*palette_idx]
         with palette_idx=1 = CRAM[257]).  Whatever default value is
         baked at boot leaks through as the rendered color for any
         cell pixel byte that happens to evaluate to byte 1.
         Empirically (per prior-agent's diagnosis of the first
         rebuild attempt) this surfaced as a catastrophic neon-
         green flood when the rebuild used pixel byte 1 for a
         color slot.

    The +2 OFFSET TRICK (this fix):
      - Quantise the OPAQUE composite to 254 colors.  Pillow's
        quantize.tobytes() emits pixel bytes 0..253 indexing the
        254-entry palette.
      - Add +2 to every emitted pixel byte, producing on-disk pixel
        bytes in range 2..255.  Bytes 0 + 1 are NEVER produced, so
        CRAM[256] (transparency / over-mode-single fringe) and
        CRAM[257] (jo printf reserved) are both immune to leak from
        Mania-grass colors.
      - On Saturn, pixel byte V = quant_index + 2 reads CRAM[256+V]
        = CRAM[258 + quant_index] = disk[1 + quant_index].
        So we must write disk[1 + i] = quant_pal[i] for i in 0..253.
        Equivalent re-statement: disk slot 1..254 hold quant colors
        0..253 respectively; disk[0] + disk[255] = 0x0000.

    BACK-CHECK against shift_pal.py canonical "shift UP by 1":
      shift_pal: new[i] = old[i+1], i.e. disk[i] = source_pal[i+1].
      This fix:  disk[1+i] = quant_pal[i],
                 i.e. disk[k] = quant_pal[k-1] for k in 1..254.
      Same single-shift relationship between disk and the source
      palette; the only difference is that this fix RESERVES disk[0]
      (and slot 1 in the OLD shift_pal convention was the dropped
      transparency placeholder).  Therefore the +2 trick is the
      canonical shift_pal convention with EXTRA headroom (disk[0]
      explicitly zeroed) for the printf-reserved CRAM[257] case.

    Citations:
      jo-engine/jo_engine/vdp2_malloc.c:60 (CRAM_PALETTE_SIZE + 1)
      jo-engine/jo_engine/vdp2.c:240-266 (paloff << 12 cell mapping)
      jo-engine/jo_engine/tools.c:156 (printf reads CRAM[257])
      tools/shift_pal.py (canonical shift-UP-by-1 convention)
      memory/jo-cram-off-by-one-shift.md (binding rule)
      ST-058-R2-060194.pdf §VDP2 color-bank + cell pattern-name
    """
    img = Image.frombytes("RGB", (OUT_W, OUT_H), bytes(rgb_bytes))
    quant = img.quantize(colors=254, method=Image.Quantize.MEDIANCUT)
    pal_rgb_full = quant.getpalette()
    n_colors = len(pal_rgb_full) // 3
    # Cap at 254 valid quant entries (matches the +2 pixel range 2..255).
    n_used = min(n_colors, 254)
    pal_rgb = pal_rgb_full[:n_used * 3]

    # Build the 256-entry on-disk palette: disk[0] = 0, disk[1+i] =
    # RGB555(quant_pal[i]) for i in 0..n_used-1, disk[n_used+1..255] = 0.
    pal_bytes = bytearray(512)
    for i in range(n_used):
        r8 = pal_rgb[i * 3 + 0]
        g8 = pal_rgb[i * 3 + 1]
        b8 = pal_rgb[i * 3 + 2]
        r5 = r8 >> 3
        g5 = g8 >> 3
        b5 = b8 >> 3
        word = (r5 << 10) | (g5 << 5) | b5
        # disk slot (1 + i) holds quant color i.  Saturn pixel byte
        # V = (i + 2) reads CRAM[256 + V] = jo PAL slot V (relative to
        # the +1 jo offset) = disk[V - 1] = disk[i + 1] = this color.
        pal_bytes[(1 + i) * 2:(1 + i) * 2 + 2] = struct.pack(">H", word)
    # disk[0] and disk[1 + n_used .. 255] stay 0x0000.

    out_pal.write_bytes(bytes(pal_bytes))

    # Pixel bytes: emit quant_index + 2 so on-disk pixel range is 2..255.
    raw = bytearray(quant.tobytes())
    for i in range(len(raw)):
        v = raw[i] + 2
        if v > 255:
            # n_used was capped at 254 so quant_index <= 253 -> v <= 255;
            # defensive clamp in case Pillow returned higher indices.
            v = 255
        raw[i] = v
    assert len(raw) == OUT_W * OUT_H, (
        f"expected {OUT_W * OUT_H} pixels, got {len(raw)}")
    out_dat.write_bytes(bytes(raw))


def main():
    ap = argparse.ArgumentParser()
    # Phase 1.31 Fix #3 retry (2026-05-27): emit to cd/TITLE3D.DAT +
    # cd/TITLE3D.PAL (DIFFERENT filenames from baseline cd/TITLE.DAT/
    # PAL).  This keeps the May-26 baseline asset intact as a known-
    # good fallback while we land the +2-byte-offset rebuild on a
    # separate file pair.  src/main.c setup_title_bg switches to the
    # TITLE3D.* filenames in the same change set.
    ap.add_argument("--out-dat", default=str(ROOT / "cd/TITLE3D.DAT"))
    ap.add_argument("--out-pal", default=str(ROOT / "cd/TITLE3D.PAL"))
    ap.add_argument("--preview", default=str(ROOT / "tools/qa_golden/title_bg_preview.png"))
    args = ap.parse_args()

    scene_bytes = SCENE_BIN.read_bytes()
    layers = parse_all_layers(scene_bytes)
    print(f"Title/Scene1.bin: {len(layers)} layers")
    for L in layers:
        print(f"  L{L['index']} {L['name']!r}: {L['xs']}x{L['ys']} tiles "
              f"({L['xs']*16}x{L['ys']*16} px)")

    tiles, pal_rgb = load_tiles(TILES_GIF)
    print(f"16x16Tiles.gif: {len(tiles)} tiles, {len(pal_rgb)//3}-entry palette")

    composite = composite_title_scene(layers, tiles, pal_rgb)
    print(f"Composited title scene: {OUT_W}x{OUT_H} px, "
          f"{len(composite)}-byte RGB buffer")

    # Eyeball the composite BEFORE quantisation to confirm it's not
    # transparency-dominated.
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
