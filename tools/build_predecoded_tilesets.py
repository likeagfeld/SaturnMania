#!/usr/bin/env python3
# build_predecoded_tilesets.py -- offline pre-decode of EVERY scene's 16x16Tiles.gif
# (Task #271 universal load-fix).
#
# WHY THIS EXISTS
# ---------------
# RSDK::LoadStageGIF (Scene.cpp:1147) LZW-decodes every scene's 16x16Tiles.gif into
# the 262,144-byte `tilesetPixels` index plane at scene-load time. MEASURED on the
# 26.8 MHz SH-2 (Task #271, p6_w_p2seeks per-step VBL split): that decode is ~4.4 s
# -- the dominant cost of S8 LoadSceneFolder (4.65 s) and the single largest chunk of
# the front-end's 7.9 s phase-2 load. The LZW prefix/suffix/stack tables (~12 KB)
# blow the SH-2's 4 KB cache, so every one of the 262,144 output pixels costs a
# cache-thrash (~450 cycles/px). The cost is UNIVERSAL: every tileset GIF is
# 16 x 16384 (1024 tiles padded), so every scene pays the same ~4.4 s regardless of
# how few tiles it actually uses (Title GIF = 23 KB, Logos = 1.7 KB -- both decode to
# the same 262,144 px).
#
# THE FIX
# -------
# Pre-decode the GIF OFFLINE (here, with Pillow -- already proven byte-identical to
# the engine's own ReadGifPictureData by tools/_portspike/qa_p6_gif.py gate W2) and
# ship the raw result. At load the Saturn arm in LoadStageGIF memcpy's the raw index
# plane straight into `tilesetPixels` (ms, not seconds) and converts the palette,
# skipping the LZW entirely. No runtime decode for ANY pre-decoded scene.
#
# OUTPUT (per scene folder, into cd/, loose ISO9660 files -- same delivery as the
# existing <TAG>LAYT.BIN band stores, loaded by 8.3 name via GFS):
#   cd/<TAG>TIL.BIN  -- the raw 8-bpp index plane, row-major, TRIMMED to the last
#                       non-zero tile (trailing all-zero/unused tiles are dropped;
#                       the engine memset-zeroes the trimmed suffix, producing a
#                       byte-identical `tilesetPixels` to a full decode). This trim
#                       turns the I/O cost from a flat 262 KB into a few KB for the
#                       mostly-empty front-end tilesets.
#   cd/<TAG>PAL.BIN   -- 256 x big-endian uint32 (0x00RRGGBB), the GIF global colour
#                       table verbatim. 1024 B. The engine runs the SAME active-row
#                       masking + rgb32To16 conversion the decode path runs.
#
# TAG = uppercase(folder)[:5]  (so "<TAG>TIL.BIN" <= 12 chars, the SGL GFS_FNAME_LEN
# 8.3 limit [[sgl-gfs-fname-len-12-limit]]; mirrored verbatim by p6_til_name() in
# Scene.cpp). Verified collision-free across all 42 Stages folders.
#
# DATA-DRIVEN: prints per-scene gif/raw/trimmed/savings so the load-time win is
# measured, not assumed.

import os
import sys
import struct

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("ERROR: Pillow (PIL) is required: pip install Pillow\n")
    sys.exit(2)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAGES = os.path.join(REPO, "extracted", "Data", "Stages")
CD = os.path.join(REPO, "cd")

TILE_SIZE = 16
TILE_DATASIZE = TILE_SIZE * TILE_SIZE          # 256 bytes / tile
TILE_COUNT = 1024
TILESET_SIZE = TILE_COUNT * TILE_DATASIZE      # 262144 -- full index plane


def tag_for(folder):
    """uppercase(folder)[:5] -- MUST match p6_til_name() in Scene.cpp."""
    return folder.upper()[:5]


def predecode_one(folder):
    gif = os.path.join(STAGES, folder, "16x16Tiles.gif")
    if not os.path.isfile(gif):
        return None  # UI / non-tilemap scene -- no tileset

    im = Image.open(gif)
    if im.mode != "P":
        raise SystemExit("%s/16x16Tiles.gif is not an indexed (mode P) GIF: %s"
                         % (folder, im.mode))
    if im.info.get("interlace"):
        # The engine's ReadGifPictureData honours the interlace bit, but Pillow
        # de-interlaces transparently -- the index order would diverge. All 42
        # tilesets are confirmed non-interlaced; fail loudly if that ever changes.
        raise SystemExit("%s/16x16Tiles.gif is INTERLACED -- index order would "
                         "diverge from the engine decode; handle explicitly" % folder)

    w, h = im.size
    raw = im.tobytes()                          # row-major 8-bpp indices, w*h bytes
    if len(raw) != w * h:
        raise SystemExit("%s: tobytes() len %d != w*h %d" % (folder, len(raw), w * h))
    if w != TILE_SIZE or (w * h) > TILESET_SIZE:
        raise SystemExit("%s: unexpected tileset dims %dx%d (raw %d > %d)"
                         % (folder, w, h, w * h, TILESET_SIZE))

    # --- trim trailing all-zero tiles ------------------------------------------
    # tilesetPixels byte value 0 == palette index 0; unused high tiles are filled
    # with index 0 by the GIF. Drop the trailing zero run, rounded UP to a tile
    # boundary (256 B) so no partial tile is cut. The engine zero-fills the rest.
    last_nz = -1
    for i in range(len(raw) - 1, -1, -1):
        if raw[i] != 0:
            last_nz = i
            break
    if last_nz < 0:
        trimmed_len = TILE_DATASIZE             # degenerate all-zero -> keep 1 tile
    else:
        trimmed_len = ((last_nz // TILE_DATASIZE) + 1) * TILE_DATASIZE
    trimmed_len = max(TILE_DATASIZE, min(trimmed_len, len(raw)))
    til = raw[:trimmed_len]

    # --- palette: 256 x big-endian uint32 0x00RRGGBB ---------------------------
    pal = im.getpalette() or []
    pal = (pal + [0] * (256 * 3))[:256 * 3]     # pad/clamp to exactly 256 entries
    pbuf = bytearray()
    for c in range(256):
        r, g, b = pal[c * 3 + 0], pal[c * 3 + 1], pal[c * 3 + 2]
        pbuf += struct.pack(">I", ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF))
    assert len(pbuf) == 1024

    tag = tag_for(folder)
    with open(os.path.join(CD, tag + "TIL.BIN"), "wb") as f:
        f.write(til)
    with open(os.path.join(CD, tag + "PAL.BIN"), "wb") as f:
        f.write(pbuf)

    return {
        "folder": folder, "tag": tag, "w": w, "h": h,
        "gif": os.path.getsize(gif), "raw": len(raw), "trim": trimmed_len,
    }


def main():
    only = set(a for a in sys.argv[1:] if not a.startswith("-"))
    folders = sorted(os.listdir(STAGES))
    seen_tags = {}
    results = []
    for folder in folders:
        if only and folder not in only:
            continue
        if not os.path.isdir(os.path.join(STAGES, folder)):
            continue
        r = predecode_one(folder)
        if r is None:
            continue
        if r["tag"] in seen_tags:
            raise SystemExit("TAG COLLISION: %s and %s both -> %s"
                             % (seen_tags[r["tag"]], r["folder"], r["tag"]))
        seen_tags[r["tag"]] = r["folder"]
        results.append(r)

    if not results:
        raise SystemExit("no tilesets pre-decoded (check --only filter / paths)")

    tot_gif = tot_trim = 0
    print("%-13s %-6s %-9s %9s %9s %9s  %s"
          % ("folder", "tag", "dims", "gif", "raw", "TIL.BIN", "decode->memcpy"))
    for r in results:
        tot_gif += r["gif"]
        tot_trim += r["trim"]
        print("%-13s %-6s %dx%-7d %9d %9d %9d  %5.1f%% of raw shipped"
              % (r["folder"], r["tag"], r["w"], r["h"], r["gif"], r["raw"],
                 r["trim"], 100.0 * r["trim"] / r["raw"]))
    print("-" * 78)
    print("scenes=%d  total TIL.BIN=%d B (%.1f MB)  + %d PAL.BIN x 1024 B"
          % (len(results), tot_trim, tot_trim / 1048576.0, len(results)))
    print("Each scene: ~4.4 s SH-2 LZW decode -> %d B GFS read + memcpy (ms)."
          % (tot_trim // max(1, len(results))))


if __name__ == "__main__":
    main()
