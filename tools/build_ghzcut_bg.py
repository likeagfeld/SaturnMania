#!/usr/bin/env python3
"""
build_ghzcut_bg.py - Task #309 #2b: build the GHZCutscene 'BG Outside' sky
(scene TileLayer 0) as a Saturn VDP2 4-bpp NBG asset, so it can render BEHIND
the transparent FG and kill the black warp-in sky.

MEASURED (tools/_bgo_probe2.py, tools/render_scene.py):
  - GHZCutscene Scene1.bin layer 0 'BG Outside' = 512x24 tiles, 100% populated
    (12288/12288 cells), 86 unique 16x16 tiles.
  - Its palette is STAGE BANK 1 rows 8-11 (source indices 128..191): blue sky
    gradient + white clouds + green/brown hills + blue water. NOT bank 0.

Mirrors the PROVEN AIZ BG pipeline (tools/build_aiz_4bpp.py) -- 4-bpp custom
16-color banks (greedy set-cover, slot 0 transparent), COMPACT char (charno =
compactIdx*4), 2-word PND map, PAL_BASE=32 -> CRAM[512..] (clear of FG bank0 +
VDP1 sprite bank1). DIFFERENCE: the sky palette is BAKED to final Saturn BGR555
here (from stage bank 1, the palette that produced the verified render), so the
engine upload copies colors straight to CRAM with NO runtime-palette dependency.

Char/PND formulas are byte-identical to p6_vdp2.c p6_vdp2_aiz_bg_upload so the
same B1 char base (0x25E60000) / charno 0x3000 / PL_SIZE_2x2 apply.

Self-verifying: reconstructs every BG Outside pixel from the emitted CHR+PAL and
compares to the stage-bank-1 render. EXITS NONZERO on any mismatch (RED gate).

Outputs (big-endian, into cd/ by default):
    GHCBG.CHR   K*128 B compact 4-bpp char (K used tiles, ascending order).
    GHCBG.MAP   16384 B precomputed 64x64 NBG0 PND window (2-word, page-tiled).
    GHCBG.PAL   n*16 u16 BGR555 CRAM banks (slot0=0x0000 transparent), baked
                from stage bank 1 -> copied to CRAM[PAL_BASE*16 + b*16 + s].
Usage:
    python tools/build_ghzcut_bg.py                 # -> cd/GHCBG.{CHR,MAP,PAL}
    python tools/build_ghzcut_bg.py --census        # measure only, no emit
"""
import argparse, os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_aiz_4bpp as aiz
from render_scene import load_tiles, parse_scene, read_stage_palette, build_palette, render_layer

STAGE_DEFAULT = "extracted/Data/Stages/GHZCutscene"
SKY_LAYER = 0            # 'BG Outside'
SKY_BANK  = 1           # stage palette bank carrying the sky colors (MEASURED)
# CRAM banks 4-7 (CRAM[64..127]). THIRD relocation, each prior slot MEASURED
# occupied (savestate CRAM peeks, 2026-07-01):
#   PAL_BASE=32  -> CRAM[512..575] stomped the Gunner HBHPAL block (colno=(2+cid)
#                   *256 puts the 5 Heavies at CRAM[512..1663], and their sprite
#                   pixels index up to 255 -> the FULL [512..1791] is Heavy-read).
#   PAL_BASE=112 -> CRAM[1792..1855] stomped the LIVE merged Sonic+Tails cutscene
#                   player palette (block 7; CRAM[1856..1871] shows its blues/
#                   oranges tail, so the loader IS live -- the "reverted #2a
#                   vacated it" note below was wrong).
# CRAM[64..127] is verified free three ways: (1) tile histogram over ALL 5 scene
# layers uses only slot 0 + rows 8-12 (banks 4-7 = 0 px); (2) the scene's decomp
# palette writers touch 181-184/197-200 (GHZSetup RotatePalette), 128-207
# (CopyPalette), 128-255 (CutsceneHBH SetPaletteEntry) -- none reach [64..127];
# (3) every VDP1 sprite colno is >=256 so no sprite CRAM window reaches bank 0.
# MUST equal p6_vdp2.c P6_GHCBG_PAL_BASE.
PAL_BASE  = 4
CHARNO_BASE = 0x3000    # (0x25E60000-0x25E00000)/0x20
WIN_X = 0               # window anchor (BG is 100% populated -> any window is sky)


def saturn_bgr555(rgb):
    r, g, b = int(rgb[0]) >> 3, int(rgb[1]) >> 3, int(rgb[2]) >> 3
    return 0x8000 | (b << 10) | (g << 5) | r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", default=STAGE_DEFAULT)
    ap.add_argument("--scene", default="Scene1.bin")
    # Output prefix sorts BEFORE "AIZBG" so it falls within the GFS root-dir index
    # cap (P6_GFS_MAX_DIR=16); a "GHCBG" name sorted at ISO position ~37 and
    # GFS_NameToId could not resolve it (MEASURED: p6_w_ghcbg_loaded=0). "AGHCBG"
    # sorts just before the proven-loadable AIZBG.* range.
    ap.add_argument("--out", default="cd/AGHCBG")
    ap.add_argument("--census", action="store_true")
    args = ap.parse_args()

    gif = os.path.join(args.stage, "16x16Tiles.gif")
    tiles = load_tiles(gif)
    banks, masks = read_stage_palette(os.path.join(args.stage, "StageConfig.bin"))
    pal = build_palette(gif, banks, masks, SKY_BANK)          # (256,3) RGB, sky bank
    layers = parse_scene(os.path.join(args.stage, args.scene))
    layer = layers[SKY_LAYER]
    lay = layer["layout"]
    ut = sorted({int(e) & 0x3FF for e in np.unique(lay) if int(e) < 0xFFFF})

    colorsets = {t: aiz.tile_colorset(tiles, t) for t in ut}
    over = [t for t, cs in colorsets.items() if len(cs) > aiz.USABLE_PER_BANK]
    maxc = max((len(cs) for cs in colorsets.values()), default=0)
    allidx = sorted(set().union(*colorsets.values())) if colorsets else []
    print(f"GHZCutscene BG Outside ({args.stage}/{args.scene} layer {SKY_LAYER}):")
    print(f"  {layer['xs']}x{layer['ys']} tiles, {len(ut)} unique; max colors/tile={maxc}"
          f" (usable/bank={aiz.USABLE_PER_BANK}); src palette idx {allidx[0]}..{allidx[-1]}")
    if over:
        print(f"  WARNING: {len(over)} tiles exceed {aiz.USABLE_PER_BANK} colors: {over[:8]}")

    bank_lists, tile_bank = aiz.greedy_pack(colorsets)
    slotmaps = aiz.build_slotmaps(bank_lists)
    n = len(bank_lists)
    print(f"  packed into {n} custom 16-color bank(s)")

    # ---- self-verify: 4-bpp re-palettize is lossless over BG Outside ----
    tiles_4bpp = {t: aiz.decode_tile_4bpp(aiz.encode_tile_4bpp(tiles[t], slotmaps[tile_bank[t]]),
                                          bank_lists[tile_bank[t]]) for t in ut}
    total_px = total_bad = 0
    ys, xs = lay.shape
    for y in range(ys):
        for x in range(xs):
            e = int(lay[y, x])
            if e >= 0xFFFF:
                continue
            idx = e & 0x3FF
            orig, recon = tiles[idx], tiles_4bpp.get(idx)
            if recon is None:
                total_bad += 256; total_px += 256; continue
            if e & 0x400: orig, recon = orig[:, ::-1], recon[:, ::-1]
            if e & 0x800: orig, recon = orig[::-1, :], recon[::-1, :]
            total_bad += int(np.count_nonzero(orig != recon)); total_px += 256
    ok = (total_bad == 0 and not over)
    print(f"  LOSSLESS GATE: {total_bad}/{total_px} px mismatched -> {'GREEN' if ok else 'RED'}")
    if args.census:
        return 0 if ok else 1
    if not ok:
        print("  REFUSING TO EMIT (lossy).")
        return 1

    # ---- emit ----
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    compact = {t: i for i, t in enumerate(ut)}
    # tell the shared PND builder our compact/blank/charno layout (PAL_BASE identical)
    aiz.PAL_BASE = PAL_BASE
    aiz.CHARNO_BASE = CHARNO_BASE
    aiz.BLANK_COMP = len(ut)                      # blank char sits one past the used set
    with open(args.out + ".CHR", "wb") as f:
        for t in ut:
            f.write(aiz.encode_tile_4bpp(tiles[t], slotmaps[tile_bank[t]]))
    nbg0_map = aiz.build_nbg0_map(layer, compact, tile_bank, WIN_X)
    with open(args.out + ".MAP", "wb") as f:
        f.write(nbg0_map)
    with open(args.out + ".PAL", "wb") as f:
        for b in bank_lists:
            words = [0x0000] + [saturn_bgr555(pal[src]) for src in b]     # slot0 transparent
            words += [0x0000] * (16 - len(words))
            for w in words[:16]:
                f.write(int(w).to_bytes(2, "big"))
    chr_bytes = len(ut) * 128
    print(f"  emit: {args.out}.CHR ({chr_bytes} B, {len(ut)} tiles), .MAP (16384 B), "
          f".PAL ({n}x16 u16, baked BGR555)")

    # ---- preview: reconstruct the emitted window as Saturn would show it ----
    prev = np.zeros((64 * 16, 64 * 16, 3), np.uint8)
    for y in range(64):
        for x in range(64):
            sx, sy = WIN_X + x, y
            e = int(lay[sy, sx]) if (sy < ys and sx < xs) else 0xFFFF
            if e >= 0xFFFF:
                continue
            idx = e & 0x3FF
            recon = tiles_4bpp.get(idx)
            if recon is None:
                continue
            if e & 0x400: recon = recon[:, ::-1]
            if e & 0x800: recon = recon[::-1, :]
            prev[y*16:y*16+16, x*16:x*16+16] = pal[recon]
    Image.fromarray(prev[:384, :1024]).save("_ghcbg_preview.png")   # top 24 rows x 64 cols
    print("  wrote _ghcbg_preview.png (Saturn-encoded sky, top 384px x 1024px)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
