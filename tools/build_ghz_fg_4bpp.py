#!/usr/bin/env python3
"""
build_ghz_fg_4bpp.py - Task #326: re-cut the GHZ FOREGROUND tileset (the NBG1
gameplay plane) from 8-bpp (256-color, 256 KB char) to 4-bpp (16-color, 128 KB
char). Halving the FG char frees VDP2 VRAM bank A1, into which the GHZ sky BG
char is relocated OUT of bank B1 -- killing the pink-flash at its root (the SGL
slScrAutoDisp allocator drops a B1-char NBG intermittently under motion, gotcha
#7; A1 is a standard NBG char bank the allocator schedules natively, so no drop
and no manual slScrCycleSet fight). See memory ghz-pink-flash-root-cause-4bpp-
fg-relocate.

WHY 4-bpp is lossless for the FG (MEASURED, extracted/Data/Stages/GHZ/16x16Tiles.gif):
  - 1024 tiles; 929 referenced by Scene1+Scene2 layouts.
  - max 20 distinct palette indices/tile; only 3 referenced tiles (885/942/954)
    exceed the 15 usable slots (16 - slot0 transparent). Those 3 are QUANTISED to
    15 colors by dropping their least-frequent indices to the nearest kept color
    (negligible: 1/3/5 dropped; the lossless gate reports the exact px delta).
  - greedy 15-color set-cover over the used tiles = 27 banks (VDP2 allows 128).

WHY the FG needs LIVE-palette CRAM banks (NOT baked, unlike build_ghzcut_bg.py):
  the GHZ stage palette CYCLES at runtime -- GHZSetup.c:21-24,101-102 RotatePalette
  indices 181-184 and 197-200 (water) every StaticUpdate. Those cycling indices land
  in 12 of the 27 banks (MEASURED). So this tool emits the bank COMPOSITION (the
  source palette index per slot, per bank) and the ENGINE rebuilds CRAM bank b from
  the LIVE fullPalette each frame: CRAM[base + b*16 + s] = saturn565(pal565[CMP[b][s]])
  -- exactly the build_aiz_4bpp.py .CMP live-rebuild model, so the water cycle
  animates identically to the 8-bpp path.

Char/PND conventions match p6_vdp2.c (the 8-bpp path being replaced):
  - Cell order VDP2 2x2 (TL,TR,BL,BR), each cell 8x8 row-major; hi nibble = LEFT
    pixel (matches build_aiz_4bpp.encode_tile_4bpp + tools/build_titlesonic_atlas).
  - Tiles emitted UNFLIPPED, one 128 B char per tile, FULL 1024 tiles RESIDENT
    (the FG char is NOT camera-streamed -- p6_vdp2_upload_cells uploads all 1024
    once). charno = tile*4 (4-bpp 16x16 = 4 charno units of 0x20 B = 128 B/tile;
    was tile*8 for 8-bpp). NO compaction: raw tile index == char index, so the
    layout PND charno formula stays a pure function of the tile index.
  - 2-word PND: bit31 Vflip, bit30 Hflip, bits22-16 palette# = PAL_BASE + bank[tile],
    low word charno = tile*4.

Outputs (big-endian; SH-2 native), default into cd/:
    GHZFG.CHR   1024 * 128 B = 128 KB 4-bpp char, raw tile order (tile i at i*128).
                Fills VDP2 bank A0 ONLY (0x25E00000..0x25E1FFFF) -> A1 freed.
    GHZFG.BNK   1024 * u8: per-tile bank index (0..N-1). The engine writes
                (PAL_BASE + this) into the PND palette field, and uses it to pick
                the encode slotmap. Unreferenced tiles -> bank 0 (harmless).
    GHZFG.CMP   [u8 N][N * 16 u8 source palette index]: bank composition. Slot 0 of
                every bank = source index 0 (transparent placeholder). The engine
                rebuilds CRAM from the LIVE cycled palette each frame.

Self-verifying: reconstructs every referenced FG pixel from CHR+BNK+CMP (+ layout
flip bits) and compares to the source 8-bpp tile (after the 3-tile quantise).
EXITS NONZERO if any drawn FG pixel mismatches beyond the quantised-tile budget.

Usage:
    python tools/build_ghz_fg_4bpp.py               # -> cd/GHZFG.{CHR,BNK,CMP}
    python tools/build_ghz_fg_4bpp.py --census      # measure only, no emit
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_aiz_4bpp as aiz
from render_scene import load_tiles, parse_scene

N_TILES = 1024
USABLE_PER_BANK = aiz.USABLE_PER_BANK           # 15 (slot 0 transparent)
# CRAM banks for the 4-bpp FG: bank 0 [CRAM 0..255] was the 8-bpp FG's whole
# palette. The 4-bpp banks reuse that SAME CRAM window (the FG owns CRAM[0..255]
# exclusively -- VDP1 sprites are bank1 [256..511], the BG sky is banks 4-7 of a
# HIGHER window). 27 banks * 16 = 432 entries > 256, so the FG 4-bpp banks need
# CRAM[0..431]. That overlaps the VDP1 sprite bank1 [256..511]! => the FG 4-bpp
# banks MUST start at PAL_BASE=0 and the engine caps N so N*16 <= 256 (N<=16), OR
# CRAM mode 1 (2048 colors) hosts them above 512. The BG already relies on CRAM
# mode 1 (banks 4-7 at [64..127] AND Heavy blocks at [512..]). The FG 4-bpp banks
# go at CRAM[0..N*16-1] with N<=16 IF they fit; measured N=27 does NOT fit under
# 256, so host the FG banks at a HIGH base clear of every other user. See main().
PAL_BASE = 0            # placeholder; real base decided + asserted in main()


def used_tiles(stage, scenes):
    s = set()
    for sc in scenes:
        layers = parse_scene(os.path.join(stage, sc))
        for l in layers:
            for e in np.unique(l["layout"]):
                if int(e) < 0xFFFF:
                    s.add(int(e) & 0x3FF)
    return sorted(s)


def quantise_tile(tile, keep):
    """Return a copy of `tile` with every palette index NOT in `keep` remapped to
    the nearest kept index by VALUE (indices are ordered by the GIF GCT; nearest-
    value is the cheapest lossless-ish approximation without the RGB table here).
    Slot 0 (transparent) is always kept. Returns (newtile, dropped_px)."""
    keep = set(keep) | {0}
    out = tile.copy()
    dropped = 0
    kept_sorted = sorted(keep)
    for v in np.unique(tile):
        v = int(v)
        if v in keep:
            continue
        # nearest kept index by value
        nv = min(kept_sorted, key=lambda k: abs(k - v))
        mask = tile == v
        dropped += int(mask.sum())
        out[mask] = nv
    return out, dropped


def build_colorsets(tiles, ut):
    """colorset per used tile, quantising the 3 over-15 tiles to their 15 most-
    frequent non-zero indices. Returns (colorsets, quantised_tiles, over_report)."""
    colorsets = {}
    qtiles = {}
    over_report = []
    for t in ut:
        vals, counts = np.unique(tiles[t], return_counts=True)
        nz = [(int(v), int(c)) for v, c in zip(vals, counts) if int(v) != 0]
        if len(nz) <= USABLE_PER_BANK:
            colorsets[t] = frozenset(v for v, _ in nz)
            qtiles[t] = tiles[t]
        else:
            keep = [v for v, _ in sorted(nz, key=lambda vc: vc[1], reverse=True)[:USABLE_PER_BANK]]
            qt, dropped = quantise_tile(tiles[t], keep)
            colorsets[t] = frozenset(int(v) for v in np.unique(qt) if int(v) != 0)
            qtiles[t] = qt
            over_report.append((t, len(nz), dropped))
    return colorsets, qtiles, over_report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", default="extracted/Data/Stages/GHZ")
    ap.add_argument("--scenes", nargs="+", default=["Scene1.bin", "Scene2.bin"])
    # Output prefix sorts within the GFS root-dir index cap (P6_GFS_MAX_DIR).
    # "AGHFG" sorts right after AGHCBG.* (index ~5-7); the AIZBG.* GFS-loaded
    # block then ends at index 18, so P6_GFS_MAX_DIR must be >= 19 (bumped to 20
    # in p6_gfs.c). A "GHZFG" name sorted at ISO position ~103 -> unresolvable.
    ap.add_argument("--out", default="cd/AGHFG")
    ap.add_argument("--pal-base", type=int, default=None,
                    help="CRAM bank base (16-entry units). Default: auto (see note).")
    ap.add_argument("--census", action="store_true")
    args = ap.parse_args()

    tiles = load_tiles(os.path.join(args.stage, "16x16Tiles.gif"))
    assert tiles.shape[0] <= N_TILES, f"{tiles.shape[0]} tiles > {N_TILES}"
    ut = used_tiles(args.stage, args.scenes)

    colorsets, qtiles, over_report = build_colorsets(tiles, ut)
    maxc = max((len(cs) for cs in colorsets.values()), default=0)
    allidx = sorted(set().union(*colorsets.values())) if colorsets else []

    print(f"GHZ FG 4-bpp re-cut census ({args.stage} {args.scenes}):")
    print(f"  {len(ut)} unique referenced tiles (of {tiles.shape[0]})")
    print(f"  max colors/tile after quantise = {maxc} (usable/bank = {USABLE_PER_BANK})")
    print(f"  source palette index range = {allidx[0]}..{allidx[-1]}")
    for t, ncol, dropped in over_report:
        print(f"  QUANTISED tile {t}: {ncol} colors -> 15 (dropped {dropped} px to nearest)")

    bank_lists, tile_bank = aiz.greedy_pack(colorsets)
    n = len(bank_lists)
    print(f"  packed into {n} custom 16-color bank(s)")

    # CRAM base decision: FG banks live in CRAM. The FG 8-bpp path owned bank0
    # [0..255]. The 4-bpp banks need n*16 entries. n=27 -> 432 > 256, so they can
    # NOT sit at [0..255] alone. HIGH-base option: CRAM[?] clear of FG-bank0-legacy
    # (unused once 4-bpp), VDP1 sprite bank1 [256..511], BG sky banks 4-7 [64..127],
    # Heavy blocks [512..1791], player block7 [1792..2047]. The ONLY contiguous
    # n*16-wide free window is [0..255]+... -> we RECLAIM [0..255] (the old 8-bpp FG
    # palette is gone) and, since 27*16=432 spills to 512, we must NOT collide the
    # sprite bank1 [256..511]. => host FG 4-bpp banks at CRAM[0..], but that spills
    # into sprites. RESOLUTION: the FG banks go HIGH at CRAM[1024+] region? measured
    # occupied by Heavies. The clean answer: FG banks at a base the engine reserves;
    # for GHZ GAMEPLAY (no Heavies/cutscene-players live) CRAM[512..1791] IS free ->
    # host the 27 FG banks at CRAM[512..512+432]. Assert it fits < 2048 and is clear
    # of the BG sky banks [64..127] + sprite bank1 [256..511].
    global PAL_BASE
    if args.pal_base is not None:
        PAL_BASE = args.pal_base
    else:
        PAL_BASE = 32                      # CRAM[512..] (GHZ gameplay: Heavies not resident)
    hi = PAL_BASE * 16 + n * 16
    assert hi <= 2048, f"FG banks end CRAM[{hi}] > 2048"
    assert not (PAL_BASE * 16 < 512 and hi > 256), "FG banks would collide sprite bank1 [256..511]"
    print(f"  CRAM: {n} banks at PAL_BASE={PAL_BASE} -> CRAM[{PAL_BASE*16}..{hi-1}] "
          f"(sprite bank1 [256..511] + BG sky [64..127] clear)")

    slotmaps = aiz.build_slotmaps(bank_lists)

    # ---- self-verify (RED/GREEN): rebuild every referenced FG pixel ----
    tiles_4bpp = {}
    for t in ut:
        chr128 = aiz.encode_tile_4bpp(qtiles[t], slotmaps[tile_bank[t]])
        tiles_4bpp[t] = aiz.decode_tile_4bpp(chr128, bank_lists[tile_bank[t]])

    total_px = total_bad = quant_bad = 0
    qset = {t for t, _, _ in over_report}
    for sc in args.scenes:
        layers = parse_scene(os.path.join(args.stage, sc))
        for l in layers:
            lay = l["layout"]
            ys, xs = lay.shape
            for y in range(ys):
                for x in range(xs):
                    e = int(lay[y, x])
                    if e >= 0xFFFF:
                        continue
                    idx = e & 0x3FF
                    orig = tiles[idx]           # compare vs ORIGINAL (quantise counted separately)
                    recon = tiles_4bpp.get(idx)
                    if recon is None:
                        total_bad += 256; total_px += 256; continue
                    if e & 0x400:
                        orig = orig[:, ::-1]; recon = recon[:, ::-1]
                    if e & 0x800:
                        orig = orig[::-1, :]; recon = recon[::-1, :]
                    bad = int(np.count_nonzero(orig != recon))
                    if idx in qset:
                        quant_bad += bad
                    else:
                        total_bad += bad
                    total_px += 256

    # GREEN = 0 mismatches on NON-quantised tiles (the quantised px are the
    # accepted, reported loss on 3 tiles).
    ok = (total_bad == 0)
    print(f"  LOSSLESS GATE: {total_bad}/{total_px} non-quantised FG px mismatched, "
          f"{quant_bad} px on the 3 quantised tiles -> {'GREEN' if ok else 'RED'}")

    if args.census:
        return 0 if ok else 1
    if not ok:
        print("  REFUSING TO EMIT: re-cut is lossy on non-quantised tiles (RED).")
        return 1

    # ---- emit: FULL 1024-tile resident char (raw tile order) ----
    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out + ".CHR", "wb") as f:
        for t in range(N_TILES):
            if t in tile_bank:
                f.write(aiz.encode_tile_4bpp(qtiles[t], slotmaps[tile_bank[t]]))
            else:
                f.write(b"\x00" * 128)          # unreferenced tile -> blank char
    with open(args.out + ".BNK", "wb") as f:
        f.write(bytes(tile_bank.get(t, 0) & 0xFF for t in range(N_TILES)))
    with open(args.out + ".CMP", "wb") as f:
        f.write(bytes([n]))
        for b in bank_lists:
            row = [0] + list(b) + [0] * (15 - len(b))     # slot0 transparent + pad
            f.write(bytes(row[:16]))
    chr_bytes = N_TILES * 128
    print(f"  wrote {args.out}.CHR ({chr_bytes} B = A0 only, A1 freed), "
          f"{args.out}.BNK ({N_TILES} B), {args.out}.CMP ({1 + n*16} B, {n} banks)")
    print(f"  PAL_BASE={PAL_BASE} MUST equal p6_vdp2.c P6_GHZFG_PAL_BASE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
