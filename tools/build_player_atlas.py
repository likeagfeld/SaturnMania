#!/usr/bin/env python3
# =============================================================================
# build_player_atlas.py -- Task #309 caveat #2a: Sonic + Tails RENDER in the
# GHZCutscene arrival cutscene (instead of BLACK SQUARES).
#
# The front-end build SKIPS the GHZ player anim pack (GHZANIM.PAK), so
# Player_StageLoad's LoadSpriteAnimation("Players/Sonic.bin"/"Tails.bin")
# resolves to nothing -> aniFrames unresolved -> no sheet bound -> the engine's
# Player_Draw blits an unbound surface (black). This tool builds the Saturn-
# native staging assets that make both players render, MIRRORING the Tier-B.2
# Heavies (tools/build_heavy_atlas.py):
#
#   cd/PLROBJ.SHT  -- ONE selective atlas (SHB1 banded codec) of just the frames
#                     the cutscene displays: the ANI_FAN (anim 25, the fan-twirl,
#                     GHZCutsceneST.c:173/198/219) + Idle (anim 0) frames of Sonic
#                     and Tails, packed into a <=512-wide 8bpp atlas. Staged like
#                     HBHOBJ.SHT into a free SaturnSheet slot.
#   cd/PLRPAL.BIN  -- ONE merged 256-color palette (RGB555, jo CRAM convention)
#                     for CRAM block 7 (CRAM[1792..2047]). MEASURED: the ANI_FAN
#                     frames of Sonic and Tails use DISJOINT palette indices
#                     except {0,1,16,18} where the colors are IDENTICAL, so a
#                     single merged palette (Sonic's color at Sonic indices,
#                     Tails' color at Tails indices) renders BOTH faithfully.
#   cd/HBHOBJ.PAK  -- REBUILT to contain the 5 Heavy bins (BYTE-IDENTICAL to
#                     build_heavy_atlas.py's output for them) PLUS Sonic.bin +
#                     Tails.bin rewritten (sheet -> "Cutscene/Players.gif", FAN/
#                     Idle rects -> atlas coords, every other frame clamped to a
#                     0x0 transparent rect). Player_StageLoad resolves Sonic.bin/
#                     Tails.bin from this CART pack (paks[1]=P6_HW_OBJANIMPAK,
#                     Animation.cpp:85) by name hash. [The Heavy SHT/PAL/atlas are
#                     NOT touched -> qa_ghzcut_heavies stays GREEN; only the PAK
#                     grows by the 2 player bins. paks[0]=P6_HW_ANIMPAK is WRAM-H
#                     and the front-end .bss OVERLAPS it, so the cart pack is the
#                     only resident window -> combine.]
#
# CRAM/CMDCOLR scheme (DOC-CITED): live SPCTL=0x23 (Sprite Type 3 = full 11-bit
# DC, ST-058-R2 Fig 9.1 p201), SPCAOS=0 (CRAOFB, ST-058-R2 sec 10.1), CRAM mode 1.
# A VDP1 8bpp sprite's CRAM address = jo colno (CMDCOLR high byte, ST-013-R3 sec
# 6.4) + char-pixel. The engine draws the players via Player_Draw with block 1;
# the Object.cpp draw-dispatch wrap (#if P6_GHZCUT_BOOT) sets p6_heavy_palblock=7
# around the player draw -> colno=7*256=1792 -> CRAM[1792+pixel]. Block 7 is the
# ONLY free 256-aligned block above the 5 Heavy blocks (CRAM[512..1663]); it is
# disjoint from FG bank0 [0..255], sprite bank1 [256..511], and the Heavies ->
# R3.3-collision-proof.
#
# Self-tests S1-S6 each run (band round-trip, atlas-rect == source-rect bytes,
# pack round-trip, merged-palette no-conflict, palette length, Heavy bytes
# unchanged).
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
import build_anim_pack as bap  # noqa: E402
from build_anim_pack import parse_bin, build_pack  # noqa: E402

BAND_ROWS = 16
ATLAS_W = 512            # <=512 keeps the SHB1 raw band <=0x4000 (MakeResident-safe)
COMBINED_SHEET = b"Cutscene/Players.gif\0"   # the single sheet name both .bin point at
MAGENTA = (255, 0, 255)

# ANI_FAN = enum index 25 (Player.h:46, NON-PLUS). The cutscene sets ONLY ANI_FAN
# on the players (GHZCutsceneST.c:173/198/219). We ALSO carry Idle (0) as a static-
# pose safety net (the player entry before the trigger / any non-FAN held frame).
PLAYERS = [
    # who,     load .bin name,       palette .gif,        anims used
    ("SONIC", "Players/Sonic.bin", "Players/Sonic1.gif", [0, 25]),
    ("TAILS", "Players/Tails.bin", "Players/Tails1.gif", [0, 25]),
]

# CRAM block 7 base (the merged player palette). colno = 7*256 = 1792.
CRAM_PLAYER_BLOCK = 7
CRAM_PLAYER_BASE = CRAM_PLAYER_BLOCK * 256   # 1792


def djb2(data):
    h = 5381
    for b in data:
        h = ((h << 5) + h) ^ b
        h &= 0xFFFFFFFF
    return h


def rgb888_to_bgr555_shift(r, g, b):
    """Same conversion as p6_pal_mirror / build_heavy_atlas: RGB888 -> 5-bit each,
    packed 0x8000 | (b5<<10)|(g5<<5)|r5. The engine copies this block straight to
    CRAM[block..block+255] (no jo +1 pre-shift on the raw-CRAM sprite-bank path)."""
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
    # ---- 0. Build the Heavy assets first (UNTOUCHED) so the Heavy temp bins exist
    #      in tools/_portspike/_hbh_bins and HBHOBJ.SHT/HBHPAL.BIN are written. We
    #      then REBUILD HBHOBJ.PAK to ALSO include the player bins. ----
    import build_heavy_atlas
    rc = build_heavy_atlas.main()
    if rc != 0:
        print("build_player_atlas: build_heavy_atlas FAILED (%d)" % rc)
        return rc
    heavy_tmpdir = os.path.join(ROOT, "tools", "_portspike", "_hbh_bins")
    heavy_bins = [b for (_w, b, _p, _a) in build_heavy_atlas.HEAVIES]
    # snapshot the Heavy SHT/PAL bytes (S6: must be byte-identical after we run)
    sht_before = open(os.path.join(ROOT, "cd", "HBHOBJ.SHT"), "rb").read()
    pal_before = open(os.path.join(ROOT, "cd", "HBHPAL.BIN"), "rb").read()

    # ---- 1. Gather the unique used rects per player, with their source pixels ----
    player_rects = []   # list of dicts: who, src_gif, sx, sy, w, h
    seen = {}
    bin_info = {}       # load_name -> parsed source
    for who, binname, palgif, anims_needed in PLAYERS:
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
                    seen[key] = len(player_rects)
                    player_rects.append({"who": who, "src_gif": src_gif,
                                         "sx": sx, "sy": sy, "w": w, "h": h})

    # ---- 2. Shelf-pack the rects into the ATLAS_W-wide 8bpp atlas (tallest-first) -
    order = sorted(range(len(player_rects)), key=lambda i: -player_rects[i]["h"])
    GUT = 1
    shelf_x = shelf_y = shelf_h = 0
    place = {}
    for i in order:
        r = player_rects[i]
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
    srccache = {}
    for i, r in enumerate(player_rects):
        gif = r["src_gif"]
        if gif not in srccache:
            srccache[gif] = load_sheet_pixels(gif)
        sw, sh, spx = srccache[gif]
        ax, ay = place[i]
        for row in range(r["h"]):
            s0 = (r["sy"] + row) * sw + r["sx"]
            d0 = (ay + row) * ATLAS_W + ax
            atlas[d0:d0 + r["w"]] = spx[s0:s0 + r["w"]]

    # S2: every placed rect byte-matches its source rect
    for i, r in enumerate(player_rects):
        sw, sh, spx = srccache[r["src_gif"]]
        ax, ay = place[i]
        for row in range(r["h"]):
            srow = spx[(r["sy"] + row) * sw + r["sx"]:(r["sy"] + row) * sw + r["sx"] + r["w"]]
            arow = bytes(atlas[(ay + row) * ATLAS_W + ax:(ay + row) * ATLAS_W + ax + r["w"]])
            if srow != arow:
                print("S2 FAIL: atlas rect %d row %d mismatch" % (i, row))
                return 1

    # ---- 3. Emit PLROBJ.SHT (SHB1 banded codec, identical to build_sheet_bands) --
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
    open(os.path.join(ROOT, "cd", "PLROBJ.SHT"), "wb").write(sht)
    raw_band = BAND_ROWS * w

    # ---- 4. Rewrite the 2 player .bin to the Heavy temp dir, then rebuild the PAK -
    atlas_lookup = {}
    for i, r in enumerate(player_rects):
        atlas_lookup[(r["who"], r["src_gif"], r["sx"], r["sy"], r["w"], r["h"])] = place[i]

    # FAN-ONLY size reduction (ROOT-CAUSE FIX for the #2a regression): the FULL
    # 547/524-frame rewrite made HBHOBJ.PAK 72,772 B; loaded at P6_HW_OBJANIMPAK
    # (0x22760000) it collided (the 42 KB tail past the 30 KB Heavy pak) with the
    # front-end SaturnSheet band store / GHZCutscene resident region 0x22720000..
    # 0x22800000 -> corrupted the banded fetch of the Heavy/claw sheets -> p6_slot_for
    # dropped EVERY cutscene VDP1 sprite (MEASURED: Build 3 vdp1_landed=0 vs Build 4
    # 30 KB pak vdp1_landed=4093). FIX: emit ONLY the frames of the anims the cutscene
    # displays (Idle=0 + FAN=25). Every OTHER anim keeps its HEADER (so anim index 25
    # stays valid for SetSpriteAnimation) but fc=0 -> zero frames emitted. build_pack
    # lays frames out anim-by-anim cumulatively, so anim 25's frameListOffset follows
    # anim 0's (few) frames. Sonic 547 frames -> ~15; the combined pak drops from 72 KB
    # back under the safe 30 KB Heavy footprint (no collision). Default ON (the full
    # rewrite is the proven-broken path); set PLR_FULLFRAMES=1 to reproduce the RED.
    fan_only = os.environ.get("PLR_FULLFRAMES", "") == ""

    player_bins = []
    for who, binname, palgif, anims_needed in PLAYERS:
        sheets, snames, hbc, anims, frames = bin_info[binname]
        needed_frame = set()
        for a_idx in anims_needed:
            if a_idx >= len(anims):
                continue
            _n, fstart, fc, _s, _l, _r = anims[a_idx]
            needed_frame.update(range(fstart, fstart + fc))
        out = bytearray()
        out += b"SPR\0"
        # frame count = the frames actually EMITTED (only the needed anims' frames in
        # fan_only mode; every frame in the full mode). build_pack reads its own count.
        emitted_count = (sum(anims[a][2] for a in anims_needed if a < len(anims))
                         if fan_only else len(frames))
        out += struct.pack("<I", emitted_count)
        out += bytes([1])                       # sheet_count = 1
        out += bytes([len(COMBINED_SHEET)]) + COMBINED_SHEET
        out += bytes([hbc])
        for _hb in range(hbc):
            nm = b"H\0"
            out += bytes([len(nm)]) + nm
        out += struct.pack("<H", len(anims))
        for (aname, fstart, fc, speed, loop, rot) in anims:
            nb = aname if isinstance(aname, (bytes, bytearray)) else aname.encode("latin1")
            out += bytes([len(nb)]) + nb
            # In fan_only mode an un-needed anim emits fc=0 (its header stays so the
            # index is preserved, but no frames follow -> the frame array shrinks).
            emit_frames = (not fan_only) or (fstart in [anims[a][1] for a in anims_needed if a < len(anims)])
            eff_fc = fc if emit_frames else 0
            out += struct.pack("<H", eff_fc)
            out += struct.pack("<h", speed)
            out += bytes([loop])
            out += bytes([rot])
            for k in range(eff_fc):
                so, dur, uni, sx, sy, w0, h0, px, py, hbs = frames[fstart + k]
                src_gif = snames[so]
                key = (who, src_gif, sx, sy, w0, h0)
                if (fstart + k) in needed_frame and key in atlas_lookup:
                    ax, ay = atlas_lookup[key]
                else:
                    ax, ay, w0, h0 = 0, 0, 0, 0   # unused -> 0x0 (nothing drawn)
                out += bytes([0])                  # sheet_ord = 0 (Cutscene/Players.gif)
                out += struct.pack("<HH", dur, uni)
                out += struct.pack("<hhhhhh", ax, ay, w0, h0, px, py)
                for hb in range(hbc):
                    if hb < len(hbs):
                        out += struct.pack("<hhhh", *hbs[hb])
                    else:
                        out += struct.pack("<hhhh", 0, 0, 0, 0)
        binpath = os.path.join(heavy_tmpdir, binname.replace("/", os.sep))
        os.makedirs(os.path.dirname(binpath), exist_ok=True)
        open(binpath, "wb").write(out)
        player_bins.append(binname)
        # S3: re-parse the rewritten .bin -- anim index 25 (FAN) must still exist and
        # carry its frames; the FAN frames must be the first frames of anim 25's range.
        s2, hbc2, an2, fr2 = parse_bin(binpath)
        assert s2[0].rstrip(b"\x00") == b"Cutscene/Players.gif", (binname, s2)
        assert len(an2) == len(anims), (binname, "anim count changed", len(an2), len(anims))
        fan_a = an2[25]
        assert fan_a[2] == anims[25][2], (binname, "FAN fc mismatch", fan_a[2], anims[25][2])
        print("  %-18s emitted_frames=%d (was %d)  FAN@anim25 fc=%d"
              % (who, len(fr2), len(frames), fan_a[2]))

    # rebuild HBHOBJ.PAK = 5 Heavy bins + 2 player bins (all in heavy_tmpdir).
    # OBJ window cap = 0x40000 (256 KB); the pack-internal cap is the asserted limit.
    saved_sprites = bap.SPRITES
    bap.SPRITES = heavy_tmpdir
    pak_path = os.path.join(ROOT, "cd", "HBHOBJ.PAK")
    # Batch 3 step 2 (full Platform port, 2026-07-09): the GHZCutscene scene authors
    # ONE Platform entity; the front-end slow windowed-GFS anim path FAILS by
    # construction (the R3.2 finding), so GHZCutscene/Platform.bin must ride the pack
    # resident during that leg == HBHOBJ.PAK (loaded into P6_HW_OBJANIMPAK at the
    # cutscene seam). Copied VERBATIM into the temp dir (85 B, 1 frame, sheet
    # GHZCutscene/Objects.gif -- staged at the seam per dccf167); no rewrite needed.
    ghzcut_plat = "GHZCutscene/Platform.bin"
    # #302 AIZ->GHZCutscene seam CD-storm elimination (2026-07-16, measured via
    # tools/_aiz_cdprobe.py fill attribution): these StageLoad anims slow-path
    # into DATA.RSDK at the GHZCutscene seam = one scattered CD seek each while
    # the handoff is frozen (21 fills / io_vbl +248). HBHOBJ.PAK is the OBJ pack
    # mounted at that seam (P6_HW_OBJANIMPAK), so carry them VERBATIM here (the
    # Platform.bin pattern above). Caller strings verified in _decomp_raw:
    # Player.c:795 (SuperSonic -- Sonic/Tails already ride as the REWRITTEN
    # atlas bins above), HUD.c:485 (SuperButtons), UIWidgets.c:76 (TextEN),
    # UIWaitSpinner.c:73 (WaitSpinner), AIZKingClaw.c:89 (GHZCutscene/Claw.bin).
    extra_verbatim = [
        ghzcut_plat,
        "Players/SuperSonic.bin",
        "Global/SuperButtons.bin",
        "UI/TextEN.bin",
        "UI/WaitSpinner.bin",
        "GHZCutscene/Claw.bin",
    ]
    for _rel in extra_verbatim:
        _src = os.path.join(SP, _rel.replace("/", os.sep))
        _dst = os.path.join(heavy_tmpdir, _rel.replace("/", os.sep))
        if not os.path.isdir(os.path.dirname(_dst)):
            os.makedirs(os.path.dirname(_dst))
        with open(_src, "rb") as _f:
            _pb = _f.read()
        with open(_dst, "wb") as _g:
            _g.write(_pb)
    try:
        build_pack(heavy_bins + player_bins + extra_verbatim, pak_path, 0x20000)
    finally:
        bap.SPRITES = saved_sprites

    # ---- 5. Emit PLRPAL.BIN: ONE merged 256-color block. For each index choose the
    #      color: Sonic's if Sonic uses it (non-magenta in Sonic1.gif), else Tails'
    #      if Tails uses it, else 0. The merged block goes to CRAM[1792..2047]. ----
    s_gct = Image.open(os.path.join(SP, "Players/Sonic1.gif")).getpalette()
    t_gct = Image.open(os.path.join(SP, "Players/Tails1.gif")).getpalette()

    # S4: verify NO real conflict over the FAN+Idle frame indices (the only indices
    # the cutscene draws). Real conflict = an index BOTH players draw with DIFFERENT
    # colors. (Established offline = 0; re-assert so a future asset change is caught.)
    def used_indices(who, gif, binname):
        sheets, snames, hbc, anims, frames = bin_info[binname]
        used = set()
        sw, sh, spx = srccache[gif]
        for r in player_rects:
            if r["who"] != who:
                continue
            for row in range(r["h"]):
                base = (r["sy"] + row) * sw + r["sx"]
                for col in range(r["w"]):
                    used.add(spx[base + col])
        return used
    su = used_indices("SONIC", "Players/Sonic1.gif", "Players/Sonic.bin")
    tu = used_indices("TAILS", "Players/Tails1.gif", "Players/Tails.bin")
    conflicts = []
    for i in sorted(su & tu):
        if i == 0:
            continue   # index 0 is the transparent key (VDP1 skips pixel 0) -- never drawn
        sc = tuple(s_gct[i * 3:i * 3 + 3])
        tc = tuple(t_gct[i * 3:i * 3 + 3])
        if sc != tc and sc != MAGENTA and tc != MAGENTA:
            conflicts.append((i, sc, tc))
    if conflicts:
        print("S4 FAIL: merged palette has REAL conflicts (both players draw these "
              "indices with different colors): %s" % conflicts)
        print("  -> a single merged CRAM block cannot render both faithfully; "
              "a 2-block scheme is required.")
        return 1

    merged = []
    for i in range(256):
        sc = tuple(s_gct[i * 3:i * 3 + 3])
        tc = tuple(t_gct[i * 3:i * 3 + 3])
        if i in su and sc != MAGENTA:
            r, g, b = sc
        elif i in tu and tc != MAGENTA:
            r, g, b = tc
        elif sc != MAGENTA:
            r, g, b = sc
        else:
            r, g, b = tc
        merged.append(rgb888_to_bgr555_shift(r, g, b))
    palout = b"".join(struct.pack(">H", c) for c in merged)
    # S5: 256 colors * 2 bytes
    assert len(palout) == 256 * 2, len(palout)
    open(os.path.join(ROOT, "cd", "PLRPAL.BIN"), "wb").write(palout)

    # S6: the Heavy SHT/PAL bytes are UNCHANGED (we only grew the PAK).
    sht_after = open(os.path.join(ROOT, "cd", "HBHOBJ.SHT"), "rb").read()
    pal_after = open(os.path.join(ROOT, "cd", "HBHPAL.BIN"), "rb").read()
    if sht_after != sht_before or pal_after != pal_before:
        print("S6 FAIL: HBHOBJ.SHT or HBHPAL.BIN changed (Heavy regression)")
        return 1

    # ---- report ----
    print("PLROBJ.SHT  %6d B  atlas %dx%d  %d bands  rawband=0x%X  djb2=0x%08X"
          % (len(sht), w, h, nbands, raw_band, djb2(bytes(sht))))
    print("HBHOBJ.PAK  rebuilt (5 Heavy + Sonic.bin + Tails.bin -> Cutscene/Players.gif)"
          "  size=%d B" % os.path.getsize(pak_path))
    print("PLRPAL.BIN  %6d B  (1 merged 256-color block @ CRAM[%d], block %d)"
          % (len(palout), CRAM_PLAYER_BASE, CRAM_PLAYER_BLOCK))
    print("player atlas rects=%d  (Sonic+Tails FAN+Idle)" % len(player_rects))
    print("merged-palette REAL conflicts (must be 0): %d" % len(conflicts))

    model = {"_comment": "GENERATED by build_player_atlas.py",
             "atlas_w": w, "atlas_h": h, "bands": nbands,
             "sht_bytes": len(sht), "sht_hash": "0x%08X" % djb2(bytes(sht)),
             "raw_band": raw_band,
             "cram_player_block": CRAM_PLAYER_BLOCK,
             "cram_player_base": CRAM_PLAYER_BASE,
             "rects": len(player_rects),
             "pak_bytes": os.path.getsize(pak_path),
             "conflicts": len(conflicts)}
    open(os.path.join(ROOT, "tools", "_portspike", "_p6", "p6_plr_model.json"), "w").write(
        json.dumps(model, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
