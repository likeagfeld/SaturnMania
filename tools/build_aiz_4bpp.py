#!/usr/bin/env python3
"""
build_aiz_4bpp.py - R2.0: re-palettize the AIZ background tileset to VDP2 4-bpp
(16-color) so the 4 AIZ Background layers can each occupy a VDP2 NBG/RBG cell
plane (the faithful AIZ intro fly-in render).

WHY 4-bpp: doc-verified VDP2 VRAM cycle wall (ST-058-R2 VRAM Cycle Patterns) -
two 256-color (8-bpp) NBG planes already saturate the character-read cycle
budget, so 8-bpp cannot host the 4 background planes alongside the 8-bpp FG on
NBG1. 16-color (4-bpp) cells need half the character-read accesses per dot, so
the 4 BG planes fit. This tool produces the 4-bpp character data + the custom
16-color palette banks that make every BG tile representable WITHOUT color loss.

MEASURED (the data that makes this lossless, not a guess):
  - The 4 AIZ Background layers (Scene1.bin layers 0-3) reference 490 unique
    16x16 tiles.
  - At CHAR_SIZE_2x2 (the engine's mode - one pattern-name palette number per
    16x16 = all 4 cells share one bank) EVERY used BG tile has <=16 distinct
    palette indices (max 11). They span source palette indices 128..170 only.
  - The cyclable water colors (RotatePalette(0,171,174) in AIZSetup) are used
    ONLY by FG Low, NEVER by the backgrounds -> the BG re-palettize carries no
    palette-cycle constraint, and the working 8-bpp FG on NBG1 stays untouched.
  - Reserving slot 0 of each bank for transparency (VDP2 palette entry 0 is
    transparent) leaves 15 usable colors >= the 11 max -> a bijective, lossless
    index->slot remap. A greedy 15-color set-cover packs all 490 tiles into a
    handful of custom banks.

Char data convention matches the repo's established 4-bpp packing
(tools/build_titlesonic_atlas.py:250): byte = (leftPixelSlot<<4)|rightPixelSlot,
HIGH nibble = leftmost pixel. Cell order is VDP2 2x2 (TL,TR,BL,BR), each cell
8x8 row-major, matching the 8-bpp p6_vdp2.c upload. Tiles are emitted UNFLIPPED;
H/V flips are applied by the engine via the pattern-name flip bits (same as the
8-bpp present), so the per-tile color set is flip-invariant.

Outputs (big-endian; SH-2 native), default into cd/:
    AIZBG.CHR   COMPACT 4-bpp char data: only the K tiles used by the BG layers,
                in ascending tile order (compact char index = position), K*128 B
                (~61 KB << the 128 KB free VRAM bank B1, leaving room for the BG
                maps). Raw (unflipped) tiles, VDP2 2x2-cell order, encoded through
                each tile's assigned bank. charno = compactIndex*4 (32-byte units,
                4-bpp 2x2 -> 128 B/tile).
    AIZBG.RMP   1024 * u16 (big-endian): raw tile index -> compact char index, or
                0xFFFF if that tile is never used by a BG layer (engine -> blank).
    AIZBG.BNK   1024 * u8: the custom-bank index (0..N-1) per raw tile. The engine
                writes (PAL_BASE + this) into the 2-word pattern-name palette field
                (bits 22-16). PAL_BASE=32 keeps the 4-bpp banks at CRAM[512..] --
                clear of BOTH the 8-bpp FG's CRAM[0..255] (bank 0) AND the VDP1
                sprite palette CRAM[256..511] (bank 1, p6_vdp1.c p6_pal_mirror).
                R3.3 #306: at PAL_BASE=16 the banks stomped the sprite palette ->
                magenta/green AIZ Tornado. MUST MATCH p6_vdp2.c P6_AIZBG_PAL_BASE.
    AIZBG.CMP   bank composition: [u8 N][N * 16 u8 source palette index]. The
                engine builds CRAM bank b from the LIVE runtime palette:
                CRAM[base + b*16 + s] = saturn565(fullPalette[CMP[b][s]]).
                Slot 0 of every bank = source index 0 (transparent placeholder).

Self-verifying: reconstructs every BG layer's palette-index image from
CHR+BNK+CMP (+ the layout's flip bits) and compares to the source 8-bpp render.
EXITS NONZERO if any drawn BG pixel mismatches (the built-in RED/GREEN gate).

Usage:
    python tools/build_aiz_4bpp.py                 # -> cd/AIZBG.{CHR,BNK,CMP}
    python tools/build_aiz_4bpp.py --census        # measure + report, no emit
    python tools/build_aiz_4bpp.py --out cd/AIZBG
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render_scene import load_tiles, parse_scene

BG_LAYERS = (0, 1, 2, 3)       # Background 1..4 in AIZ Scene1.bin
USABLE_PER_BANK = 15           # slot 0 reserved transparent -> 15 real colors
N_TILES = 1024
CELL_ORDER = ((0, 0), (0, 8), (8, 0), (8, 8))   # TL, TR, BL, BR

# R2.1 NBG0 PND map (must MATCH p6_vdp2.c p6_vdp2_aiz_bg_upload byte-for-byte):
# 2-word PND, charno_base 0x3000, palette base 32, blank compact char 490, a
# 64x64-cell PL_SIZE_2x2 plane (4 pages * 32x32 * 2 u16). The BG layers are
# WINDOWED on Saturn (>8192 B -> tileLayers[].layout is NULL), so the engine
# CANNOT read the layout at runtime -- it copies this precomputed B0 map image.
# NOTE: this baked map is only the NBG0 FRAME-0 SEED; p6_vdp2_aiz_bg_stream
# re-windows all 3 BG plane maps every frame using the RUNTIME P6_AIZBG_PAL_BASE,
# so the runtime value is authoritative -- but keep this in lockstep (R3.3 #306).
MAP_LAYER = 3                  # BG4 (the jungle, drawGroup 3) -- first BG plane
CHARNO_BASE = 0x3000
PAL_BASE = 32                  # R3.3 #306: CRAM[512..], clear of VDP1 sprite bank 1
BLANK_COMP = 490
PLANE_TILES = 64               # PL_SIZE_2x2 -> 64x64 cells


def tile_colorset(tiles, idx):
    """Flip-invariant set of non-transparent palette indices in a 16x16 tile."""
    return frozenset(int(v) for v in np.unique(tiles[idx]) if v != 0)


def used_bg_tiles(layers):
    s = set()
    for li in BG_LAYERS:
        for e in np.unique(layers[li]["layout"]):
            if int(e) < 0xFFFF:
                s.add(int(e) & 0x3FF)
    return sorted(s)


def greedy_pack(colorsets):
    """Pack {tile: colorset} into banks of <=USABLE_PER_BANK colors.
    Returns (banks, tile_bank): banks = list[sorted list of source indices];
    tile_bank = {tile: bank_index}. First-fit-decreasing by set size."""
    order = sorted(colorsets.items(), key=lambda kv: len(kv[1]), reverse=True)
    banks = []          # list[set]
    tile_bank = {}
    for tile, cs in order:
        placed = False
        for bi, b in enumerate(banks):
            if len(b | cs) <= USABLE_PER_BANK:
                b |= cs
                tile_bank[tile] = bi
                placed = True
                break
        if not placed:
            banks.append(set(cs))
            tile_bank[tile] = len(banks) - 1
    bank_lists = [sorted(b) for b in banks]
    return bank_lists, tile_bank


def build_slotmaps(bank_lists):
    """bank -> {source_index: slot}. Slot 0 = transparent (source 0)."""
    maps = []
    for b in bank_lists:
        m = {0: 0}
        for s, src in enumerate(b, start=1):     # slots 1..15
            m[src] = s
        maps.append(m)
    return maps


def densest_window(layer, win_w=PLANE_TILES):
    """The x-offset of the win_w-tile-wide window of `layer` with the most
    non-empty tiles (so the static proof shows populated jungle, not sky)."""
    lay = layer["layout"]
    ys, xs = lay.shape
    nonempty = (lay != 0xFFFF).sum(axis=0)            # per-column non-empty count
    best_x, best_n = 0, -1
    for x0 in range(0, max(1, xs - win_w + 1)):
        n = int(nonempty[x0:x0 + win_w].sum())
        if n > best_n:
            best_n, best_x = n, x0
    return best_x, best_n


def build_nbg0_map(layer, compact, tile_bank, win_x):
    """Precompute the 64x64 NBG0 PND map for a win_x-anchored window of `layer`.
    Byte-for-byte the image p6_vdp2.c would build: 2-word PND, page-tiled, BE.
    Returns 16,384 bytes (4 pages * 32x32 * 2 u16 * 2 B)."""
    lay = layer["layout"]
    ys, xs = lay.shape
    out = bytearray(PLANE_TILES * PLANE_TILES * 4)    # 64*64 PNDs * 4 B
    blank_pnd = (PAL_BASE << 16) | (CHARNO_BASE + BLANK_COMP * 4)
    for y in range(PLANE_TILES):
        for x in range(PLANE_TILES):
            sx, sy = win_x + x, y
            e = int(lay[sy, sx]) if (sy < ys and sx < xs) else 0xFFFF
            tile = e & 0x3FF
            comp = compact.get(tile) if e != 0xFFFF else None
            if comp is None:
                pnd = blank_pnd
            else:
                palnum = PAL_BASE + tile_bank[tile]
                charno = CHARNO_BASE + comp * 4
                pnd = (((e & 0x800) << 20) | ((e & 0x400) << 20)
                       | (palnum << 16) | charno)
            page = ((y >> 5) << 1) + (x >> 5)
            off = (page * 2048 + (((y & 31) << 5) + (x & 31)) * 2) * 2   # bytes
            out[off:off + 2] = ((pnd >> 16) & 0xFFFF).to_bytes(2, "big")
            out[off + 2:off + 4] = (pnd & 0xFFFF).to_bytes(2, "big")
    return bytes(out)


def encode_tile_4bpp(tile16, slotmap):
    """16x16 index tile -> 128 B 4-bpp, VDP2 2x2-cell order, hi nibble = left."""
    out = bytearray(128)
    p = 0
    for (cy, cx) in CELL_ORDER:
        for row in range(8):
            for col in range(0, 8, 2):
                left = int(tile16[cy + row, cx + col])
                right = int(tile16[cy + row, cx + col + 1])
                ls = slotmap.get(left, 0)
                rs = slotmap.get(right, 0)
                out[p] = ((ls & 0x0F) << 4) | (rs & 0x0F)
                p += 1
    return bytes(out)


def decode_tile_4bpp(chr128, bank_list):
    """Inverse: 128 B 4-bpp + bank source-index list -> 16x16 source-index tile.
    bank_list[s-1] is the source index for slot s (slot 0 -> source index 0)."""
    tile = np.zeros((16, 16), np.uint8)
    p = 0
    for (cy, cx) in CELL_ORDER:
        for row in range(8):
            for col in range(0, 8, 2):
                byte = chr128[p]
                p += 1
                for half, dx in ((byte >> 4, 0), (byte & 0x0F, 1)):
                    src = 0 if half == 0 else bank_list[half - 1]
                    tile[cy + row, cx + col + dx] = src
    return tile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", default="extracted/Data/Stages/AIZ")
    ap.add_argument("--scene", default="Scene1.bin")
    ap.add_argument("--out", default="cd/AIZBG")
    ap.add_argument("--census", action="store_true",
                    help="measure + report packing only; do not emit files")
    args = ap.parse_args()

    tiles = load_tiles(os.path.join(args.stage, "16x16Tiles.gif"))
    assert tiles.shape[0] <= N_TILES, f"{tiles.shape[0]} tiles > {N_TILES}"
    layers = parse_scene(os.path.join(args.stage, args.scene))

    ut = used_bg_tiles(layers)
    colorsets = {t: tile_colorset(tiles, t) for t in ut}
    over = [t for t, cs in colorsets.items() if len(cs) > USABLE_PER_BANK]
    maxc = max((len(cs) for cs in colorsets.values()), default=0)
    allidx = sorted(set().union(*colorsets.values())) if colorsets else []

    print(f"AIZ BG re-palettize census ({args.stage}/{args.scene}):")
    print(f"  BG layers {BG_LAYERS} use {len(ut)} unique 16x16 tiles")
    print(f"  max colors/tile = {maxc} (usable/bank = {USABLE_PER_BANK})")
    print(f"  source palette index range = {allidx[0]}..{allidx[-1]}")
    if over:
        print(f"  WARNING: {len(over)} tiles exceed {USABLE_PER_BANK} colors "
              f"-> NOT losslessly representable: {over[:8]}")

    bank_lists, tile_bank = greedy_pack(colorsets)
    n = len(bank_lists)
    print(f"  packed into {n} custom 16-color bank(s):")
    for bi, b in enumerate(bank_lists):
        print(f"    bank {bi}: {len(b)} colors -> src idx {b}")

    # ---- self-verify (the RED/GREEN gate): rebuild every BG layer ----
    # tiles_4bpp[T] reconstructed purely from what we will emit.
    slotmaps = build_slotmaps(bank_lists)
    tiles_4bpp = {}
    for t in ut:
        chr128 = encode_tile_4bpp(tiles[t], slotmaps[tile_bank[t]])
        tiles_4bpp[t] = decode_tile_4bpp(chr128, bank_lists[tile_bank[t]])

    total_px = 0
    total_bad = 0
    for li in BG_LAYERS:
        lay = layers[li]["layout"]
        ys, xs = lay.shape
        bad = 0
        px = 0
        for y in range(ys):
            for x in range(xs):
                e = int(lay[y, x])
                if e >= 0xFFFF:
                    continue
                idx = e & 0x3FF
                orig = tiles[idx]
                recon = tiles_4bpp.get(idx)
                if recon is None:               # tile never collected (shouldn't happen)
                    bad += 256
                    px += 256
                    continue
                if e & 0x400:
                    orig = orig[:, ::-1]; recon = recon[:, ::-1]
                if e & 0x800:
                    orig = orig[::-1, :]; recon = recon[::-1, :]
                bad += int(np.count_nonzero(orig != recon))
                px += 256
        total_px += px
        total_bad += bad
        print(f"  L{li} {layers[li]['name'].strip()!r}: {px} px, {bad} mismatched")

    ok = (total_bad == 0 and not over)
    print(f"  LOSSLESS GATE: {total_bad}/{total_px} BG pixels mismatched -> "
          f"{'GREEN' if ok else 'RED'}")

    if args.census:
        return 0 if ok else 1

    if not ok:
        print("  REFUSING TO EMIT: re-palettize is lossy (see RED above).")
        return 1

    # ---- emit (COMPACT: only the K used BG tiles, ascending tile order) ----
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    compact = {t: i for i, t in enumerate(ut)}      # raw tile -> compact char idx
    chr_bytes = len(ut) * 128
    B1_BYTES = 0x20000                              # one VDP2 VRAM bank = 128 KB
    assert chr_bytes <= B1_BYTES * 3 // 4, \
        f"compact CHR {chr_bytes} B leaves too little of bank B1 for the maps"

    with open(args.out + ".CHR", "wb") as f:
        for t in ut:
            f.write(encode_tile_4bpp(tiles[t], slotmaps[tile_bank[t]]))
    with open(args.out + ".RMP", "wb") as f:
        f.write(b"".join(
            (compact[t] if t in compact else 0xFFFF).to_bytes(2, "big")
            for t in range(N_TILES)))
    with open(args.out + ".BNK", "wb") as f:
        f.write(bytes(tile_bank.get(t, 0) & 0xFF for t in range(N_TILES)))
    with open(args.out + ".CMP", "wb") as f:
        f.write(bytes([n]))
        for b in bank_lists:
            row = [0] + list(b) + [0] * (15 - len(b))   # slot0 transparent + pad
            f.write(bytes(row[:16]))
    # AIZBG.MAP: precomputed NBG0 PND map for BG4 (the engine copies it to B0 --
    # the BG layers are windowed, so the engine can't read the layout at runtime).
    win_x, win_n = densest_window(layers[MAP_LAYER])
    nbg0_map = build_nbg0_map(layers[MAP_LAYER], compact, tile_bank, win_x)
    with open(args.out + ".MAP", "wb") as f:
        f.write(nbg0_map)
    # AIZBG.L3: the FULL BG4 layout (raw entries incl. flip bits, big-endian
    # u16, row-major L3[y*xsize + x]) so the engine can CAMERA-STREAM the NBG0
    # window (BG4 = 768 tiles wide >> the 64-tile plane; parallax 0.75x scrolls
    # ~7,900 px over the fly-in -> must re-window, not wrap). Mirrors how the FG
    # streams its SaturnLayout band, but BG4 is small enough to keep resident.
    l3 = layers[MAP_LAYER]["layout"]
    l3ys, l3xs = l3.shape
    with open(args.out + ".L3", "wb") as f:
        f.write(l3.astype(">u2").tobytes())
    # AIZBG.L0: the FULL BG1 layout (the distant backdrop -- full content cols 0-79,
    # parallax ~0.25, visible THROUGHOUT the fly-in incl. the region where BG4 is
    # empty). BG1's tiles (idx 161-170) are ALREADY in the packed 490-tile set, so it
    # REUSES AIZBG.CHR/RMP/BNK/CMP -- only its 80x16 layout is new. Streamed on NBG2.
    l0 = layers[0]["layout"]
    l0ys, l0xs = l0.shape
    with open(args.out + ".L0", "wb") as f:
        f.write(l0.astype(">u2").tobytes())
    # AIZBG.L2: the FULL BG3 layout (the distant mountains/island layer, visible
    # mid-fly-in; decomp Background 3 = Scene1.bin layer 2). Like BG1, BG3's tiles
    # are ALREADY in the packed 490-tile set (the lossless gate covers all 4 BG
    # layers) -> REUSES AIZBG.CHR/RMP/BNK/CMP; only its layout is new. Rendered on
    # the 4th plane NBG3 (R2.6, single-scroll approximation of its dominant band).
    l2 = layers[2]["layout"]
    l2ys, l2xs = l2.shape
    with open(args.out + ".L2", "wb") as f:
        f.write(l2.astype(">u2").tobytes())
    print(f"  wrote {args.out}.L0 (BG1 {l0xs}x{l0ys}), {args.out}.L2 (BG3 {l2xs}x{l2ys}), "
          f"{args.out}.L3 (BG4 {l3xs}x{l3ys})")
    # AIZBG.SI2: BG2 (layer 1) line-scroll SOURCE for the NBG0 hardware line-scroll table
    # (R2.7). The engine builds the per-screen-line VDP2 scroll table each frame:
    #   scroll[L] = camera_x * parallax[ lineScroll[(bg2_yrow + L) % nrows] ] >> 8  (+ sine
    #   deform per AIZSetup deformationDataW). parallax 128..256 = the 0.5x..1.0x depth ramp.
    # Format (big-endian): [u16 nbands][nbands * i16 parallaxFactor][u16 nrows][nrows * u8
    # row->band]. 129 bands + 512 rows -> 2 + 258 + 2 + 512 = 774 B.
    b2 = layers[1]
    si2 = b2["scrollInfo"]; ls2 = b2["lineScroll"]
    with open(args.out + ".SI2", "wb") as f:
        f.write(len(si2).to_bytes(2, "big"))
        for pf, ss, df in si2:
            f.write((pf & 0xFFFF).to_bytes(2, "big"))
        f.write(len(ls2).to_bytes(2, "big"))
        f.write(bytes(int(b) & 0xFF for b in ls2))
    print(f"  wrote {args.out}.SI2 (BG2 line-scroll: {len(si2)} bands "
          f"parallax {min(b[0] for b in si2)}..{max(b[0] for b in si2)}, {len(ls2)} rows)")

    print(f"  wrote {args.out}.CHR ({chr_bytes} B, {len(ut)} compact tiles), "
          f"{args.out}.RMP ({N_TILES*2} B), {args.out}.BNK ({N_TILES} B), "
          f"{args.out}.CMP ({1 + n*16} B, {n} banks), "
          f"{args.out}.MAP ({len(nbg0_map)} B, BG4 win_x={win_x} "
          f"({win_n} non-empty tiles))")
    print(f"  VRAM: CHR {chr_bytes} B fits bank B1 (0x{B1_BYTES:X}) with "
          f"{B1_BYTES - chr_bytes} B left for BG maps")
    return 0


if __name__ == "__main__":
    sys.exit(main())
