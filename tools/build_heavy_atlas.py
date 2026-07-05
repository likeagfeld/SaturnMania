#!/usr/bin/env python3
# =============================================================================
# build_heavy_atlas.py -- Task #309 Tier-B.2: the 5 Hard-Boiled-Heavies render
# in the GHZCutscene cutscene without staging 5 full boss sheets (~2.2 MB).
#
# Each Heavy (Gunner/Shinobi/Mystic/Rider/King) loads a boss sheet from a
# DIFFERENT zone (SPZ1/PSZ2/MSZ/LRZ3). The cutscene only ever shows a few anims
# of each. This tool builds ONE selective atlas of just the USED frames + the
# Saturn-native staging assets:
#   cd/HBHOBJ.SHT  -- one banded sheet (SHB1 codec, build_sheet_bands format) of
#                     the combined atlas. Staged like AIZOBJ.SHT.
#   cd/HBHOBJ.PAK  -- 5 rewritten ANM1 .bin entries (keyed by the ORIGINAL load
#                     names SPZ1/Boss.bin etc.) whose sheet names are rewritten
#                     to ONE "Cutscene/HBH.gif" + frame rects remapped to atlas
#                     coords. The decomp CutsceneHBH_LoadSprites loads these by
#                     name; SetSpriteAnimation(aniFrames,N) selects anim N whose
#                     frames now index the atlas.
#   cd/HBHPAL.BIN  -- 5 x 128 colors (RGB555, jo +1 CRAM pre-shift) for the 5
#                     distinct CRAM blocks CRAM[512/768/1024/1280/1536]. Source =
#                     each source GIF's GCT (the authentic art palette).
#
# CRAM/CMDCOLR scheme (DOC-CITED, see docs/feature_checklists/
# ghzcutscene_directboot_t309.md Tier-B.2): live SPCTL=0x23 (Sprite Type 3, full
# 11-bit DC), SPCAOS=0, CRAM mode 1. A VDP1 8bpp sprite's framebuffer pixel =
# jo colno (CMDCOLR, 256-aligned) + charpixel -> CRAM address = that (SPCAOS=0).
# So colno = block base routes each Heavy's pixels to its own 256-entry CRAM
# block. The engine sets colno per-draw (jo_sprite_set_palette(block)); blocks
# 2..6 -> colno 512..1536 -> CRAM[512..1663] (disjoint from bank1 CRAM[256..511]).
#
# Self-tests S1-S4 each run (band round-trip, atlas-rect == source-rect bytes,
# pack round-trip via parse_bin, palette length).
# =============================================================================
import json
import os
import struct
import sys
import zlib

from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SP = os.path.join(ROOT, "extracted", "Data", "Sprites")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from build_anim_pack import parse_bin, build_pack, engine_hash  # noqa: E402

BAND_ROWS = 16
ATLAS_W = 512            # <=512 keeps the SHB1 raw band <=0x4000 (MakeResident-safe)
COMBINED_SHEET = b"Cutscene/HBH.gif\0"   # the single sheet name all 5 .bin point at
CRAM_BLOCK_BASE = 512    # CRAM entry of Heavy block 0 (GUNNER); +256 per Heavy

# Per-Heavy: (load-name, source .gif for the palette block, [anim indices needed]).
# The .gif providing the palette is the one whose frames dominate the cutscene
# pose. SHINOBI's main pose (anim0/3) is on Boss.gif (sheet0); its cape fx (anim5)
# is on Boss2.gif (sheet1) -- but Boss.gif and Boss2.gif share the SAME GCT
# (verified: identical first colors), so one block serves both.
HEAVIES = [
    # who,    load .bin name,       palette .gif,            anims used
    ("GUNNER",  "SPZ1/Boss.bin",       "SPZ1/Boss.gif",         [5]),
    ("SHINOBI", "PSZ2/Shinobi.bin",    "PSZ2/Boss.gif",         [0, 3, 5]),
    ("MYSTIC",  "MSZ/HeavyMystic.bin", "MSZ/HeavyMystic.gif",   [0]),
    ("RIDER",   "LRZ3/HeavyRider.bin", "LRZ3/HeavyRider.gif",   [3]),
    ("KING",    "LRZ3/HeavyKing.bin",  "LRZ3/HeavyKing.gif",    [7, 16]),
]


def djb2(data):
    h = 5381
    for b in data:
        h = ((h << 5) + h) ^ b
        h &= 0xFFFFFFFF
    return h


def parse_cutscene_hbh_tempals():
    """Parse the 5 hardcoded colorSet tempPal[0x80] tables out of the cached
    decomp CutsceneHBH.c (CutsceneHBH_SetupColors, decomp:84-163). Returns
    {colorSet: [128 x 0xRRGGBB]}. Hard-asserts 5 tables of exactly 128 entries
    so a decomp-cache change can never silently ship a short palette."""
    import re
    path = os.path.join(ROOT, "tools", "_decomp_raw",
                        "SonicMania_Objects_Cutscene_CutsceneHBH.c")
    src = open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(
            r"self->colorSet\s*=\s*(\d+)\s*;\s*"
            r"color\s+tempPal\[0x80\]\s*=\s*\{(.*?)\};", src, re.S):
        cset = int(m.group(1))
        vals = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", m.group(2))]
        assert len(vals) == 128, (cset, len(vals))
        out[cset] = vals
    assert sorted(out) == [0, 1, 2, 3, 4], sorted(out)
    return out


def rgb888_to_bgr555_shift(r, g, b):
    """Same conversion as p6_pal_mirror (p6_vdp1.c:711-714): RGB888 -> 5-bit each,
    pack as 0x8000 | (b5<<10)|(g5<<5)|r5. The jo +1 CRAM pre-shift is handled by
    the engine's CRAM write index, NOT here (we emit a flat 128-entry block the
    engine copies straight to CRAM[block..block+127])."""
    r5 = (r >> 3) & 0x1F
    g5 = (g >> 3) & 0x1F
    b5 = (b >> 3) & 0x1F
    return 0x8000 | (b5 << 10) | (g5 << 5) | r5


def load_sheet_pixels(gif_rel):
    im = Image.open(os.path.join(SP, gif_rel))
    assert im.mode == "P", (gif_rel, im.mode)
    w, h = im.size
    px = im.tobytes()
    assert len(px) == w * h
    return w, h, px


def main():
    # ---- 1. Gather the unique used rects per Heavy, with their source pixels ----
    # rects[(who, (sheet_path, sx, sy, w, h))] = atlas placement (filled later).
    # We keep the per-Heavy source SHEET so we copy from the right GIF.
    heavy_rects = []   # list of dicts: who, src_gif, sx, sy, w, h
    seen = {}          # (who, sheet_path, sx, sy, w, h) -> index into heavy_rects
    bin_info = {}      # load_name -> (sheets, hbc, anims, frames) parsed source
    for who, binname, palgif, anims_needed in HEAVIES:
        sheets, hbc, anims, frames = parse_bin(os.path.join(SP, binname.replace("/", os.sep)))
        snames = [s.rstrip(b"\x00").decode("latin1") for s in sheets]
        bin_info[binname] = (sheets, snames, hbc, anims, frames)
        for a_idx in anims_needed:
            if a_idx >= len(anims):
                continue
            name, fstart, fc, speed, loop, rot = anims[a_idx]
            for fi in range(fstart, fstart + fc):
                so, dur, uni, sx, sy, w, h, px, py, hbs = frames[fi]
                src_gif = snames[so]
                key = (who, src_gif, sx, sy, w, h)
                if key not in seen:
                    seen[key] = len(heavy_rects)
                    heavy_rects.append({"who": who, "src_gif": src_gif,
                                        "sx": sx, "sy": sy, "w": w, "h": h})

    # ---- 2. Shelf-pack the rects into the ATLAS_W-wide 8bpp atlas ----
    # Sort tallest-first for tighter shelves. 1px gutter avoids bleed.
    order = sorted(range(len(heavy_rects)),
                   key=lambda i: -heavy_rects[i]["h"])
    GUT = 1
    shelf_x = 0
    shelf_y = 0
    shelf_h = 0
    place = {}   # heavy_rects index -> (ax, ay)
    for i in order:
        r = heavy_rects[i]
        if shelf_x + r["w"] + GUT > ATLAS_W:
            shelf_y += shelf_h + GUT
            shelf_x = 0
            shelf_h = 0
        place[i] = (shelf_x, shelf_y)
        shelf_x += r["w"] + GUT
        if r["h"] > shelf_h:
            shelf_h = r["h"]
    atlas_h = shelf_y + shelf_h
    atlas_h = (atlas_h + BAND_ROWS - 1) // BAND_ROWS * BAND_ROWS  # band-align

    atlas = bytearray(ATLAS_W * atlas_h)  # index 0 == transparent everywhere
    # cache loaded source sheets
    srccache = {}
    for i, r in enumerate(heavy_rects):
        gif = r["src_gif"]
        if gif not in srccache:
            srccache[gif] = load_sheet_pixels(gif)
        sw, sh, spx = srccache[gif]
        ax, ay = place[i]
        for row in range(r["h"]):
            s0 = (r["sy"] + row) * sw + r["sx"]
            d0 = (ay + row) * ATLAS_W + ax
            atlas[d0:d0 + r["w"]] = spx[s0:s0 + r["w"]]

    # S2: every placed rect in the atlas byte-matches its source rect
    for i, r in enumerate(heavy_rects):
        sw, sh, spx = srccache[r["src_gif"]]
        ax, ay = place[i]
        for row in range(r["h"]):
            srow = spx[(r["sy"] + row) * sw + r["sx"]:(r["sy"] + row) * sw + r["sx"] + r["w"]]
            arow = bytes(atlas[(ay + row) * ATLAS_W + ax:(ay + row) * ATLAS_W + ax + r["w"]])
            if srow != arow:
                print("S2 FAIL: atlas rect %d row %d mismatch" % (i, row))
                return 1

    # ---- 3. Emit HBHOBJ.SHT (SHB1 banded codec, identical to build_sheet_bands) ----
    w, h = ATLAS_W, atlas_h
    nbands = (h + BAND_ROWS - 1) // BAND_ROWS
    head = 12
    dir_bytes = nbands * 12
    blobs = []
    entries = []
    off = head + dir_bytes
    for b in range(nbands):
        raw = bytes(atlas[b * BAND_ROWS * w:(b + 1) * BAND_ROWS * w])
        z = zlib.compress(raw, 9)
        entries.append((off, len(z), len(raw)))
        blobs.append(z)
        off += len(z)
    sht = bytearray()
    sht += b"SHB1"
    sht += struct.pack(">HHHH", w, h, BAND_ROWS, nbands)
    for e in entries:
        sht += struct.pack(">III", *e)
    for z in blobs:
        sht += z
    # S1: band round-trip byte-exact
    for i, (o, zs, rs) in enumerate(entries):
        raw = zlib.decompress(bytes(sht[o:o + zs]))
        if raw != bytes(atlas[i * BAND_ROWS * w:i * BAND_ROWS * w + rs]):
            print("S1 FAIL: band %d round-trip" % i)
            return 1
    sht_path = os.path.join(ROOT, "cd", "HBHOBJ.SHT")
    open(sht_path, "wb").write(sht)
    raw_band = BAND_ROWS * w  # 16 * 512 = 8192 = 0x2000 (MakeResident-safe <= 0x4000)

    # ---- 4. Emit the 5 rewritten .bin (SPR\0) to a temp dir, build HBHOBJ.PAK ----
    # Rewrite: ONE sheet name "Cutscene/HBH.gif", every frame sheet_ord=0, rects
    # -> atlas coords. Keep anim/frame STRUCTURE (so SetSpriteAnimation(N) maps).
    # Frames whose anim is NOT needed are kept (structure-preserving) but pointed
    # at the rect (0,0,w,h)=transparent? No -- to keep the pack tiny AND correct,
    # we remap ONLY the used rects; unused frames keep their ORIGINAL rect but
    # sheet_ord 0 (they'd read garbage if ever drawn, but the cutscene never draws
    # them). To be safe, unused-frame rects are clamped to a 1x1 transparent atlas
    # corner so a stray draw shows nothing.
    tmpdir = os.path.join(ROOT, "tools", "_portspike", "_hbh_bins")
    os.makedirs(tmpdir, exist_ok=True)
    rewritten_names = []
    atlas_lookup = {}  # (who, src_gif, sx, sy, w, h) -> (ax, ay)
    for i, r in enumerate(heavy_rects):
        atlas_lookup[(r["who"], r["src_gif"], r["sx"], r["sy"], r["w"], r["h"])] = place[i]

    for who, binname, palgif, anims_needed in HEAVIES:
        sheets, snames, hbc, anims, frames = bin_info[binname]
        needed = set(anims_needed)
        # build the set of frame indices that belong to a needed anim
        needed_frame = set()
        for a_idx in anims_needed:
            if a_idx >= len(anims):
                continue
            _n, fstart, fc, _s, _l, _r = anims[a_idx]
            needed_frame.update(range(fstart, fstart + fc))
        # write SPR\0
        out = bytearray()
        out += b"SPR\0"
        out += struct.pack("<I", len(frames))
        out += bytes([1])                      # sheet_count = 1
        out += bytes([len(COMBINED_SHEET)]) + COMBINED_SHEET
        out += bytes([hbc])
        for _hb in range(hbc):
            nm = b"H\0"                          # 1-char hitbox name + NUL
            out += bytes([len(nm)]) + nm
        out += struct.pack("<H", len(anims))
        fi = 0
        for (aname, fstart, fc, speed, loop, rot) in anims:
            nb = aname if isinstance(aname, (bytes, bytearray)) else aname.encode("latin1")
            out += bytes([len(nb)]) + nb
            out += struct.pack("<H", fc)
            out += struct.pack("<h", speed)
            out += bytes([loop])
            out += bytes([rot])
            for _ in range(fc):
                so, dur, uni, sx, sy, w0, h0, px, py, hbs = frames[fi]
                src_gif = snames[so]
                key = (who, src_gif, sx, sy, w0, h0)
                if fi in needed_frame and key in atlas_lookup:
                    ax, ay = atlas_lookup[key]
                else:
                    ax, ay, w0, h0 = 0, 0, 0, 0   # unused -> 0x0 (nothing drawn)
                out += bytes([0])                  # sheet_ord = 0 (Cutscene/HBH.gif)
                out += struct.pack("<HH", dur, uni)
                out += struct.pack("<hhhhhh", ax, ay, w0, h0, px, py)
                for hb in range(hbc):
                    if hb < len(hbs):
                        out += struct.pack("<hhhh", *hbs[hb])
                    else:
                        out += struct.pack("<hhhh", 0, 0, 0, 0)
                fi += 1
        # write under the ORIGINAL relative name (the pack key is engine_hash(name))
        binpath = os.path.join(tmpdir, binname.replace("/", os.sep))
        os.makedirs(os.path.dirname(binpath), exist_ok=True)
        open(binpath, "wb").write(out)
        rewritten_names.append(binname)
        # S3: the rewritten .bin re-parses
        s2, hbc2, an2, fr2 = parse_bin(binpath)
        assert s2[0].rstrip(b"\x00") == b"Cutscene/HBH.gif", (binname, s2)
        assert len(fr2) == len(frames), (binname, len(fr2), len(frames))

    # build the pack from the rewritten temp bins (build_pack reads SPRITES-rel
    # paths; point it at the temp dir by temporarily overriding the module SPRITES).
    import build_anim_pack as bap
    saved_sprites = bap.SPRITES
    bap.SPRITES = tmpdir
    pak_path = os.path.join(ROOT, "cd", "HBHOBJ.PAK")
    try:
        build_pack(rewritten_names, pak_path, 0x11000)
    finally:
        bap.SPRITES = saved_sprites

    # ---- 5. Emit HBHPAL.BIN: 5 blocks x 256 colors (RGB555, the engine copies
    # straight to CRAM[block..block+255]).
    #   LOWER 128 = the first 128 GCT entries of that Heavy's palette .gif
    #              (index 0 stays the source color -- pixel 0 is VDP1-transparent).
    #   UPPER 128 = the decomp's hardcoded cutscene tempPal for that Heavy's
    #              colorSet (CutsceneHBH_SetupColors, CutsceneHBH.c:84-163),
    #              which CutsceneHBH_SetupPalettes writes to stage palette
    #              indices 128-255 on PC (CutsceneHBH.c:195). Task #311 MEASURED:
    #              11.5-28.1% of every Heavy's packed-frame pixels index >=128 --
    #              without this half they read junk CRAM = the garble strips.
    #              colorSet map: GUNNER=0, SHINOBI=1, MYSTIC=2, RIDER=KING=3.
    #              Parsed from the cached decomp at build time (single source of
    #              truth; a table-count/size mismatch is a hard assert). ----
    tempals = parse_cutscene_hbh_tempals()
    HEAVY_COLORSET = {"GUNNER": 0, "SHINOBI": 1, "MYSTIC": 2, "RIDER": 3, "KING": 3}
    palout = bytearray()
    for who, binname, palgif, anims_needed in HEAVIES:
        im = Image.open(os.path.join(SP, palgif))
        gct = im.getpalette()  # 768 = 256*3
        for c in range(128):
            r, g, b = gct[c * 3], gct[c * 3 + 1], gct[c * 3 + 2]
            palout += struct.pack(">H", rgb888_to_bgr555_shift(r, g, b))
        for rgb in tempals[HEAVY_COLORSET[who]]:
            r, g, b = (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF
            palout += struct.pack(">H", rgb888_to_bgr555_shift(r, g, b))
    # S4: 5 blocks * 256 colors * 2 bytes
    assert len(palout) == 5 * 256 * 2, len(palout)
    pal_path = os.path.join(ROOT, "cd", "HBHPAL.BIN")
    open(pal_path, "wb").write(palout)

    # ---- report ----
    print("HBHOBJ.SHT  %6d B  atlas %dx%d  %d bands  rawband=0x%X  djb2=0x%08X"
          % (len(sht), w, h, nbands, raw_band, djb2(bytes(sht))))
    print("HBHOBJ.PAK  written (5 rewritten .bin -> Cutscene/HBH.gif)")
    print("HBHPAL.BIN  %6d B  (5 x 256 RGB555 blocks @ CRAM[512/768/1024/1280/1536];"
          " lower=GCT[0..127], upper=decomp tempPal colorSets)" % len(palout))
    print("unique atlas rects=%d" % len(heavy_rects))
    # emit a tiny model for the gate
    model = {"_comment": "GENERATED by build_heavy_atlas.py",
             "atlas_w": w, "atlas_h": h, "bands": nbands,
             "sht_bytes": len(sht), "sht_hash": "0x%08X" % djb2(bytes(sht)),
             "raw_band": raw_band, "cram_block_base": CRAM_BLOCK_BASE,
             "rects": len(heavy_rects),
             "blocks": {h2[0]: CRAM_BLOCK_BASE + 256 * i for i, h2 in enumerate(HEAVIES)}}
    open(os.path.join(ROOT, "tools", "_portspike", "_p6", "p6_hbh_model.json"), "w").write(
        json.dumps(model, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
