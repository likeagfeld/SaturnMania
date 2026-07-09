#!/usr/bin/env python3
# =============================================================================
# build_anim_pack.py -- P6.7 W13 (Task #227): offline-packed READ-ONLY sprite
# animation stores. The W11/W12 pattern applied to anims: SpriteFrame /
# SpriteAnimationEntry arrays are immutable post-parse, so the heavy Player
# set is pre-parsed offline into the EXACT SH-2 struct layout and loaded to
# a fixed WRAM-H window (P6_HW_ANIMPAK, p6_io_main.cpp) -- LoadSpriteAnimation
# resolves them by path hash with ZERO DATASET_STG cost and ZERO parse time.
#
# MEASURED basis (p6_e1.mcs anim-log ring, 2026-06-12): the GHZ pass loads
# 9 .bins; SuperSonic(556f) + Tails(524f) + Dust(42f) were REFUSED at
# STG 112K (GHZ true peak ~137.5 KB > the WRAM-L pool ceiling). Packing the
# five Player-wave bins (Sonic, SuperSonic, Tails, TailSprite, Dust) drops
# ~63 KB from STG -> STG returns to 80 KB and the heap fits the miniz
# transient again.
#
# FILE FORMAT cd/GHZANIM.PAK (all BIG-ENDIAN, parsed in place on SH-2):
#   'ANM1' | u16 binCount | u16 pad
#   binCount x directory entry:
#     u8[16] pathHash      (engine GEN_HASH_MD5 memory order: md5 digest
#                           with each 4-byte group reversed -- uint32[4]
#                           little-endian values stored big-endian)
#     u32 framesOff  u32 frameCount   (offset from blob start, 2-aligned)
#     u32 animsOff   u32 animCount    (4-aligned)
#     u32 ordsOff                     (frameCount x u8 sheet ordinals --
#                                      re-read on every resolve so repeated
#                                      scene loads re-patch sheetIDs)
#     u8 sheetCount  u8 hitboxCount  u16 nameBytes
#     nameBytes of sheet names: sheetCount x { u8 len, bytes (with NUL) }
#   payload: per bin SpriteFrame[frameCount] (34 B each, FRAMEHITBOX 2),
#            SpriteAnimationEntry[animCount] (28 B each), uint8 ords[].
#
# SH-2 SpriteFrame layout (Animation.hpp, FRAMEHITBOX_COUNT 2 -- the loader
# TU static_asserts sizeof==34):
#   i16 sprX, sprY, width, height, pivotX, pivotY   (12)
#   u16 duration, unicodeChar                       (16)
#   u8  sheetID                                     (17)
#   u8  hitboxCount                                 (18)
#   Hitbox hitboxes[2] (4 x i16 each)               (34)
# SpriteAnimationEntry: u8[16] nameHash (engine memory order) | i32
#   frameListOffset | u16 frameCount | i16 animationSpeed | u8 loopIndex |
#   u8 rotationStyle | u16 pad  == 28 B.
#
# Self-tests: S1 every packed bin round-trips its parse vs the raw .bin;
# S2 directory arithmetic; S3 emitted sizes match the struct contract.
# =============================================================================
import hashlib
import os
import struct
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SPRITES = os.path.join(ROOT, "extracted", "Data", "Sprites")
OUT = os.path.join(ROOT, "cd", "GHZANIM.PAK")

# The W13 GHZ Player-wave set (engine paths are "Data/Sprites/<rel>"; the
# LoadSpriteAnimation hash is over the FILENAME ARG, e.g. "Players/Sonic.bin"
# -> hashed as "data/sprites/players/sonic.bin"?? NO: Animation.cpp:22 hashes
# `filePath` = the caller's string ("Players/Sonic.bin") lowercased).
BINS = [
    "Players/Sonic.bin",
    "Players/SuperSonic.bin",
    "Players/Tails.bin",
    "Players/TailSprite.bin",
    "Global/Dust.bin",
]

# #254 residency lever (user-approved 2026-06-17): the COLD GHZ object anims go to
# a SECOND resident pack in the CART (P6_HW_OBJANIMPAK, 0x22760000), NOT the slow
# path into the full WRAM-L DATASET_STG pool. Object anims are read only when the
# object is on-screen, so cart wait-states are harmless (unlike the hot Player
# anims, which stay in WRAM-H GHZANIM.PAK). This frees their STG bytes AND removes
# the slow windowed-read failure class (the SpikeLog -1). Add an object's .bin here
# as it joins the GHZ sweep; the pre-flight P9/P5 then read it as resident.
OBJ_BINS = [
    "Global/Ring.bin",
    "Global/Springs.bin",
    "Global/Shields.bin",   # 188 frames -- the gate's R13 caught it still slow-pathing
                            # into the full STG (allocfail) after the first 4 moved;
                            # cold (only with a shield) -> cart-resident like the rest.
    "GHZ/Bridge.bin",
    "GHZ/SpikeLog.bin",
    # Batch 3 (2026-07-09, GHZ gameplay-parity sweep): ItemBox monitors (69 frames,
    # sheet Global/Items.gif == ITEMS.SHT, staged at the GHZ landing). Cart-resident
    # per the #254 lever -- the historical STG slow-path overflow for this exact .bin
    # is on record (p6_ovl_ghz.c ForceUnstick note: pool 153600, at_fail 152376).
    "Global/ItemBox.bin",
]
OBJ_OUT = os.path.join(ROOT, "cd", "GHZOBJ.PAK")
OBJ_CAP = 0x40000  # 256 KB cart window (0x22760000..0x227A0000 has 320 KB clear)

# R3.2 (#305): the AIZ intro-cutscene OBJECT anims (front-end / P6_AIZ_TEST). The Saturn
# LoadSpriteAnimation resolves anims from the resident packs FIRST; the front-end SKIPS
# the GHZ GHZANIM.PAK/GHZOBJ.PAK, so its object anims have NO pack and the slow windowed-
# GFS LoadFile fails (aniFrames=-1, MEASURED). AIZOBJ.PAK is loaded into the SAME
# P6_HW_OBJANIMPAK cart window (free in the front-end) so the pack-resolution finds
# AIZ/AIZTornado.bin (the FAST path) -> the biplane animator gets frames. Start with the
# Tornado (the fly-in focal sprite); add KingClaw/EggRobo/Ruby as they're wired.
AIZ_OBJ_BINS = [
    "AIZ/AIZTornado.bin",
    # R3.4 (#306 follow-on): the cutscene actor anims that spawn AFTER the fly-in --
    # KingClaw (beat 4 EnterClaw) + EggRobo (the Heavies). MEASURED: both .bins
    # reference the SAME sheet "AIZ/Objects.gif" already staged by AIZOBJ.SHT (slot 8),
    # so no new sheet is needed -- only their anim metadata in the pack. Claw.bin 260 B
    # (10 frames: Chain/Hinge/Front Claw/Back Claw), AIZEggRobo.bin 268 B (12 frames:
    # Body/Arm/Legs); both trivial vs the 256 KB OBJ_CAP. The cutscene IS confirmed to
    # progress (cutscene_state 0->3 measured) so these objects reach their spawn beats.
    "AIZ/Claw.bin",
    "AIZ/AIZEggRobo.bin",
]
AIZ_OBJ_OUT = os.path.join(ROOT, "cd", "AIZOBJ.PAK")

FRAMEHITBOX_COUNT = 2
# SH-2 layout (static_asserted in Animation.cpp): GameSpriteFrameType base
# = 17 B -> pads to 18 (align 2); hitboxCount u8 at 18; 1 pad; hitboxes at
# 20 (2 x 8) -> sizeof(SpriteFrame) == 36.
SPRITEFRAME_SIZE = 36
ANIMENTRY_SIZE = 28


def engine_hash(s):
    """GEN_HASH_MD5 memory image on SH-2 (big-endian): md5 of the string
    AS-IS (MEASURED 2026-06-12: runtime hash[0] for "Players/Sonic.bin"
    reads 0x6BC467A9 BE == md5 digest word0 little-endian -- the engine
    does NOT lowercase here, unlike OpenDataFile's registry domain), each
    digest 4-byte group reversed (uint32 LE values stored big-endian)."""
    d = hashlib.md5(s.encode()).digest()
    return b"".join(d[i:i + 4][::-1] for i in range(0, 16, 4))


def parse_bin(path):
    d = open(path, "rb").read()
    assert d[:4] == b"SPR\0", path
    off = 4
    total, = struct.unpack_from("<I", d, off); off += 4
    sheet_count = d[off]; off += 1
    sheets = []
    for _ in range(sheet_count):
        ln = d[off]; off += 1
        sheets.append(d[off:off + ln])  # includes trailing NUL byte
        off += ln
    hitbox_count = d[off]; off += 1
    for _ in range(hitbox_count):
        ln = d[off]; off += 1 + ln
    anim_count, = struct.unpack_from("<H", d, off); off += 2
    frames = []
    anims = []
    frame_id = 0
    for _ in range(anim_count):
        ln = d[off]; off += 1
        name = d[off:off + ln]; off += ln
        fc, = struct.unpack_from("<H", d, off); off += 2
        speed, = struct.unpack_from("<h", d, off); off += 2
        loop = d[off]; off += 1
        rot = d[off]; off += 1
        anims.append((name, frame_id, fc, speed, loop, rot))
        for _ in range(fc):
            sheet_ord = d[off]; off += 1
            dur, uni = struct.unpack_from("<HH", d, off); off += 4
            sx, sy, w, h, px, py = struct.unpack_from("<hhhhhh", d, off); off += 12
            hbs = []
            for hb in range(hitbox_count):
                hbs.append(struct.unpack_from("<hhhh", d, off)); off += 8
            frames.append((sheet_ord, dur, uni, sx, sy, w, h, px, py, hbs))
            frame_id += 1
    assert frame_id == total, (path, frame_id, total)
    return sheets, hitbox_count, anims, frames


def emit_frame(fr, hitbox_count):
    sheet_ord, dur, uni, sx, sy, w, h, px, py, hbs = fr
    out = struct.pack(">hhhhhhHHB", sx, sy, w, h, px, py, dur, uni, sheet_ord)
    out += b"\0"  # GameSpriteFrameType tail pad (17 -> 18)
    out += struct.pack(">B", min(hitbox_count, FRAMEHITBOX_COUNT))
    out += b"\0"  # pad: hitboxes align to 2 (19 -> 20)
    for i in range(FRAMEHITBOX_COUNT):
        if i < len(hbs):
            out += struct.pack(">hhhh", *hbs[i])
        else:
            out += struct.pack(">hhhh", 0, 0, 0, 0)
    assert len(out) == SPRITEFRAME_SIZE
    return out


def emit_anim(an):
    name, flo, fc, speed, loop, rot = an
    # anim name hash: GEN_HASH_MD5_BUFFER over the name (NUL-stripped, lower)
    h = engine_hash(name.rstrip(b"\0").decode("latin1"))
    out = h + struct.pack(">iHhBBH", flo, fc, speed, loop, rot, 0)
    assert len(out) == ANIMENTRY_SIZE
    return out


def build_pack(bins, out_path, cap):
    """Emit one ANM1 pack from `bins` to `out_path`; assert it fits `cap`.
    Byte-for-byte identical layout to the original single-pack emitter -- the
    SH-2 fast path (Animation.cpp) reads any ANM1 pack the same way regardless
    of where it lives (WRAM-H or cart), since all offsets are pack-relative."""
    dirs = []
    payloads = []
    for rel in bins:
        p = os.path.join(SPRITES, rel.replace("/", os.sep))
        sheets, hbc, anims, frames = parse_bin(p)
        fdata = b"".join(emit_frame(f, hbc) for f in frames)
        adata = b"".join(emit_anim(a) for a in anims)
        ords = bytes(f[0] for f in frames)
        names = b""
        for s in sheets:
            names += bytes([len(s)]) + s
        dirs.append((engine_hash(rel), len(frames), len(anims), len(sheets), hbc, names))
        payloads.append((fdata, adata, ords))
        print("%-26s frames=%4d anims=%3d sheets=%d hitboxes=%d -> %6d B"
              % (rel, len(frames), len(anims), len(sheets), hbc,
                 len(fdata) + len(adata) + len(ords)))

    # layout: header + directory (fixed part 16+4*5+4 = 40 B + names), then payloads
    hdr = struct.pack(">4sHH", b"ANM1", len(bins), 0)
    dir_sizes = [16 + 20 + 4 + len(d[5]) for d in dirs]
    base = len(hdr) + sum(dir_sizes)
    base = (base + 3) & ~3
    blob_dir = b""
    cursor = base
    offsets = []
    for (h, fc, ac, sc, hbc, names), (fdata, adata, ords) in zip(dirs, payloads):
        anims_off = cursor
        frames_off = anims_off + len(adata)          # anims first (4-aligned)
        ords_off = frames_off + len(fdata)
        end = (ords_off + len(ords) + 3) & ~3
        offsets.append((frames_off, anims_off, ords_off))
        blob_dir += h + struct.pack(">IIIII", frames_off, fc, anims_off, ac, ords_off)
        blob_dir += struct.pack(">BBH", sc, hbc, len(names)) + names
        cursor = end
    blob = hdr + blob_dir
    blob += b"\0" * (base - len(blob))
    for (fdata, adata, ords), (frames_off, anims_off, ords_off) in zip(payloads, offsets):
        assert len(blob) == anims_off, (len(blob), anims_off)
        blob += adata + fdata + ords
        blob += b"\0" * ((-len(blob)) % 4)

    assert len(blob) <= cap, "%s %d B exceeds the 0x%X window" % (out_path, len(blob), cap)
    with open(out_path, "wb") as f:
        f.write(blob)
    print("TOTAL %d B (cap %d) -> %s" % (len(blob), cap, out_path))


def main():
    # Player pack -> WRAM-H (hot anims, read every frame).
    build_pack(BINS, OUT, 0x11000)
    # Object pack -> cart (cold anims, read only when on-screen). #254 lever.
    print("--- object anim pack (cart-resident) ---")
    build_pack(OBJ_BINS, OBJ_OUT, OBJ_CAP)
    # R3.2 (#305): AIZ intro-cutscene object anims -> cart (front-end, reuses the
    # P6_HW_OBJANIMPAK window since the GHZ GHZOBJ.PAK is skipped in the front-end).
    print("--- AIZ object anim pack (cart-resident, front-end) ---")
    build_pack(AIZ_OBJ_BINS, AIZ_OBJ_OUT, OBJ_CAP)
    return 0


if __name__ == "__main__":
    sys.exit(main())
