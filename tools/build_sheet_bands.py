#!/usr/bin/env python3
# =============================================================================
# build_sheet_bands.py -- P6.7 W12 (Task #227): offline ROW-BAND stores for
# sprite sheets too large to be RAM-resident on the Saturn.
#
# WHY (W12 declaration, SaturnMemoryMap.h): LoadSpriteSheet decodes whole
# sheets into the 64 KB DATASET_STG pool; the Player set alone is 786,432 B
# decoded (Sonic1+2+3, MEASURED). The working set lives in VDP1 via the
# P6.5b3 rect-keyed slot cache -- the cache-miss path fetches a frame's rows
# from these band stores instead of a resident surface.
#
# Codec = the proven W11 layout-band codec (build_layout_bands.py): 16-row
# bands, zlib -9, parsed in place on SH-2 (big-endian directory). MEASURED
# (2026-06-11): Sonic1 59,703 / Sonic2 61,153 / Sonic3 33,295 = 154,151 B
# for the Player trio (vs 786,432 raw).
#
# FILE FORMAT cd/<NAME>.SHT (all BIG-ENDIAN, parsed in place):
#   'SHB1' | u16 width | u16 height | u16 bandRows | u16 bandCount
#   bandCount x { u32 offset, u32 zsize, u32 rawsize }
#   zlib streams (offsets from blob start)
#
# Emits per sheet: cd/<NAME>.SHT + appends to tools/_portspike/_p6/
# p6_sheet_model.json (offline ground truth for qa_p6_sheet.py: file hash,
# per-sheet dims, probe rects with djb2 over the rect bytes).
#
# Self-tests S1-S3 every run: band round-trip byte-exact, directory
# arithmetic, probe-rect extraction matches the raw decode.
# =============================================================================
import json
import os
import struct
import sys
import zlib

from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BAND_ROWS = 16

# The W12 Player-wave set. 8.3 names (SGL GFS_FNAME_LEN = 12 rule).
# Items.gif joined for the STG-sizing iteration (Task #227): banding it
# drops the 32,768 B resident decode from DATASET_STG so the GHZ anim
# working set (1,624 frames at FRAMEHITBOX_COUNT 2) fits the 80 KB pool.
# W19 (Task #227): DISPLAY.SHT (GHZ HUD digits) + SHIELDS.SHT (shield FX) join
# the staged set so their entity blits LAND (MEASURED 960 + 59 of the 1,139
# silent VDP1 handle<0 drops/run). Tails1.gif is DELIBERATELY ABSENT: its
# 58,643 B band store overflows the 245,760 B VDP2 band-store window by
# 19,105 B (6-sheet total 206,222 B fits; 7-sheet total 264,865 B does not).
# Adding Tails1 needs a funded plan (reclaim NBG1 cell tiles / relocate window).
SHEETS = [
    ("Players/Sonic1.gif", "SONIC1.SHT"),
    ("Players/Sonic2.gif", "SONIC2.SHT"),
    ("Players/Sonic3.gif", "SONIC3.SHT"),
    ("Global/Items.gif", "ITEMS.SHT"),
    ("Global/Display.gif", "DISPLAY.SHT"),
    ("Global/Shields.gif", "SHIELDS.SHT"),
    # Task #241 main: the SaturnSheet band store is RELOCATED from the 245,760 B
    # VDP2 VRAM window to a 384 KB region in the 4MB cart (0x227A0000..0x22800000,
    # funded by shrinking DATASET_TMP 768->640KB). That dissolves the VDP2 window
    # wall: TAILS1 (sidekick body) stages ALONGSIDE the full 6-sheet set -- no
    # SHIELDS trade. 7 sheets = 264,865 B inside the 384 KB cart store.
    ("Players/Tails1.gif", "TAILS1.SHT"),
    # Phase P6.7 GHZ content entities (#247): Global/Objects.gif is the shared
    # sheet for Spikes (2 frames) + Spring + other Global objects -- stage it once
    # to unlock the global-object batch. 256x256 = 65,536 B decoded; banded .SHT
    # fits the 384 KB cart store + a free SaturnSheet slot (#248 budget).
    ("Global/Objects.gif", "GLOBJ.SHT"),
    # GHZ1 parity P2 (#247): GHZ/Objects.gif is the SHARED GHZ content-objects
    # sheet (the bridge planks + GHZ springs/etc. all index it). 512x256 = 131,072 B
    # decoded; banded .SHT ~25 KB fits the ~107 KB free in the 384 KB cart store.
    # Staging it (slot 8) makes the registered GHZ Bridge actually blit (#181) and
    # pre-binds the sheet for the rest of the GHZ object sweep.
    ("GHZ/Objects.gif", "GHZOBJ.SHT"),
    # BADNIK-VIS (2026-06-18): Explosions/Animals were trialled as banded effect sheets
    # but staging them grew SATURNSHEET_SLOTS, which (with the P6_VDP1_NSHEETS bump)
    # tripped the #228 orphan-.bss overlap (GFS GfsMng ptr corruption -> boot trap
    # 0x06000956). REVERTED to the 9-sheet staged set; the badnik fix is solely
    # P6_VDP1_NSHEETS 9->12 (binds GHZ/Objects.gif = surf 16). Explosions/Animals keep
    # their stock resident-pixel decode (Sprite.cpp:994) -- which already renders.
    # CP4 (#266): the FRONT-END Logos splash sheet (UIPicture's Logos.gif, 512x256,
    # ~6.4 KB banded). Staged only by the P6_FRONTEND_LOGOS boot into its 10th
    # SaturnSheet slot (build_p6scene_objs.sh -DP6_FRONTEND_LOGOS -> SATURNSHEET_SLOTS
    # 10); the GHZ shipping build never loads it. Emitted here so the front-end asset
    # is part of the normal offline asset build (cd/ is .gitignored = regenerated).
    ("Logos/Logos.gif", "LOGOS.SHT"),
    # CP5b.1 (Task #268): the TITLE logo sheet (TitleLogo's Title/Logo.gif, 512x512,
    # 8bpp). LoadSpriteAnimation("Title/Logo.bin") -> this sheet; its 9 anims hold the
    # emblem (anim 0, 144x144), ribbon (anim 1), gametitle/copyright/ringbottom
    # (anims 4/6/7), press-start (anim 8). Staged ONLY by the P6_FRONTEND_TITLE boot
    # into its 11th SaturnSheet slot (slot 10; slot 9 = LOGOS.SHT, also staged because
    # TITLE implies LOGOS). The GHZ + Logos-only shipping builds never load it. Once
    # staged + hashed, the p6_ghz_arm_env bind loop binds Title/Logo.gif's gfxSurface
    # to a VDP1 handle -> the TitleLogo pieces actually blit (CP5a had it UNBOUND ->
    # handle<0 -> every TitleLogo blit dropped -> uniform-blue title). MIRRORS the
    # CP4b LOGOS.SHT path exactly.
    ("Title/Logo.gif", "TLOGO.SHT"),
]

# CP4c _end-leak FIX (Task #266): sheets that ONLY a FRONT-END boot stages. Their
# probe rows MUST be flag-gated in p6_sheet_probes.inc so the DEFAULT (GHZ) shipping
# build does NOT compile them into the unconditional const p6SheetProbes[] (each row
# = 24 B of .rodata; 3 Logos rows = 72 B pushed _end from 0x060B6BA0 to 0x060B6BF0,
# 16 B under the P6_HW_ANIMPAK 0x060B6C00 ceiling = a #228 boot-trap landmine). The
# GHZ build never stages these (their probes would also FetchRect-fail at runtime),
# so gating is correct on both counts. Keyed by the .SHT output name so the
# model/.SHT asset emission is unchanged.
#
# CP5b.1 (Task #268): each front-end-only sheet maps to the EXACT preprocessor guard
# of the flavor that stages it -- LOGOS.SHT under P6_FRONTEND_LOGOS (staged by both
# the Logos and Title flavors), TLOGO.SHT under P6_FRONTEND_TITLE (staged ONLY by the
# Title flavor, which is slot 10; a Logos-only build stages 10 sheets [0..9] so a
# slot-10 probe there would FetchRect-fail -- gating it under P6_FRONTEND_TITLE keeps
# the Logos-only probe table clean too). The generator emits one #if/#endif block per
# distinct guard so the row counts + the const initializer stay regeneration-stable.
FRONTEND_ONLY_SHEETS = {"LOGOS.SHT": "P6_FRONTEND_LOGOS",
                        "TLOGO.SHT": "P6_FRONTEND_TITLE"}


def djb2(data):
    h = 5381
    for b in data:
        h = ((h << 5) + h) ^ b
        h &= 0xFFFFFFFF
    return h


def build_one(rel, outname):
    im = Image.open(os.path.join(ROOT, "extracted", "Data", "Sprites", rel))
    w, h = im.size
    px = im.tobytes()
    assert len(px) == w * h, "expected 8bpp indexed"

    nbands = (h + BAND_ROWS - 1) // BAND_ROWS
    head = 12
    dir_bytes = nbands * 12
    blobs = []
    entries = []
    off = head + dir_bytes
    for b in range(nbands):
        raw = px[b * BAND_ROWS * w:(b + 1) * BAND_ROWS * w]
        z = zlib.compress(raw, 9)
        entries.append((off, len(z), len(raw)))
        blobs.append(z)
        off += len(z)

    out = bytearray()
    out += b"SHB1"
    out += struct.pack(">HHHH", w, h, BAND_ROWS, nbands)
    for e in entries:
        out += struct.pack(">III", *e)
    for z in blobs:
        out += z

    # S1: every band round-trips byte-exact through the directory
    for i, (o, zs, rs) in enumerate(entries):
        raw = zlib.decompress(bytes(out[o:o + zs]))
        if len(raw) != rs or raw != px[i * BAND_ROWS * w:i * BAND_ROWS * w + rs]:
            print("S1 FAIL: band %d round-trip (%s)" % (i, rel))
            return None
    # S2: directory arithmetic consumed the whole blob
    if len(out) != off:
        print("S2 FAIL: %d != %d (%s)" % (len(out), off, rel))
        return None

    path = os.path.join(ROOT, "cd", outname)
    open(path, "wb").write(out)

    # Probe rects: spread across the sheet (typical frame sizes), expected =
    # djb2 over the rect bytes from the RAW decode; the SH-2 gate replays the
    # SAME rects through SaturnSheet_FetchRect.
    # Task #241: 3 probe rects/sheet (was 5). The 7th sheet (cart relocation)
    # grew p6SheetProbes by 5 entries x 24 B = 120 B, pushing the shipping _end
    # over the W17 ANIMPAK floor; 3 spread rects still exercise top/middle/bottom
    # bands byte-exact. 7 sheets x 3 = 21 entries < the prior 6 x 5 = 30.
    probes = []
    for (sx, sy, pw, ph) in [(0, 0, 48, 48),
                             (w - 64, h // 2 - 8, 64, 64),
                             (16, h - 40, 48, 40)]:
        rect = bytearray()
        for r in range(ph):
            rect += px[(sy + r) * w + sx:(sy + r) * w + sx + pw]
        # S3: extraction sanity -- rect length
        if len(rect) != pw * ph:
            print("S3 FAIL: probe rect (%s)" % rel)
            return None
        probes.append({"sx": sx, "sy": sy, "w": pw, "h": ph,
                       "hash": "0x%08X" % djb2(bytes(rect))})

    return {"sheet": rel, "file": outname, "width": w, "height": h,
            "band_rows": BAND_ROWS, "bands": nbands, "bytes": len(out),
            "file_hash": "0x%08X" % djb2(bytes(out)), "probes": probes}


def main():
    model = {"_comment": "GENERATED by build_sheet_bands.py -- qa_p6_sheet model",
             "sheets": []}
    total = 0
    for rel, outname in SHEETS:
        m = build_one(rel, outname)
        if not m:
            return 1
        model["sheets"].append(m)
        total += m["bytes"]
        print("%-22s -> %-12s %6d B (%d bands)" % (rel, outname, m["bytes"], m["bands"]))
    mpath = os.path.join(ROOT, "tools", "_portspike", "_p6", "p6_sheet_model.json")
    with open(mpath, "w") as f:
        json.dump(model, f, indent=2)

    # Diag probe table: the SH-2 replays these via SaturnSheet_FetchRect
    # (slot index == staging order == SHEETS order) and djb2s each rect.
    ipath = os.path.join(ROOT, "tools", "_portspike", "_p6", "p6_sheet_probes.inc")
    with open(ipath, "w") as f:
        f.write("// generated by build_sheet_bands.py -- DO NOT EDIT\n")
        f.write("// {slot, sx, sy, w, h, expected djb2 over the rect bytes}\n")
        # CP4c (#266) + CP5b.1 (#268): split the rows into DEFAULT (every GHZ shipping
        # sheet) and FRONT-END-ONLY groups keyed by the EXACT preprocessor guard of the
        # flavor that stages each sheet (FRONTEND_ONLY_SHEETS maps .SHT -> guard). Each
        # guard group is wrapped in its own #if so the DEFAULT (GHZ) build's const table
        # is byte-identical (no front-end rows) -- _end stays at 0x060B6BA0, clear of the
        # 0x060B6C00 ANIMPAK ceiling. Groups are emitted in a STABLE order (LOGOS before
        # TITLE) so the slot indices (staging order) + the array initializer stay
        # regeneration-stable. P6_FRONTEND_TITLE implies P6_FRONTEND_LOGOS, so in the
        # Title build BOTH groups compile (count = base + LOGOS + TITLE); in a Logos-only
        # build only the LOGOS group does (count = base + LOGOS); in GHZ neither (base).
        GUARD_ORDER = ["P6_FRONTEND_LOGOS", "P6_FRONTEND_TITLE"]
        base_rows = []
        fe_groups = {}  # guard -> [rows]
        for si, m in enumerate(model["sheets"]):
            guard = FRONTEND_ONLY_SHEETS.get(m["file"])
            for p in m["probes"]:
                row = ("    { %d, %d, %d, %d, %d, 0x%08Xu }"
                       % (si, p["sx"], p["sy"], p["w"], p["h"],
                          int(p["hash"], 16)))
                if guard:
                    fe_groups.setdefault(guard, []).append(row)
                else:
                    base_rows.append(row)
        guards = [g for g in GUARD_ORDER if fe_groups.get(g)]
        # extra guards not in the explicit order (none expected; keep stable)
        guards += [g for g in fe_groups if g not in guards]

        # P6_SHEET_PROBE_COUNT must expand to a PLAIN integer literal: it is used as a C
        # array bound (p6SheetProbes[P6_SHEET_PROBE_COUNT]), where the `defined()`
        # operator is NOT valid (defined() works only inside #if/#elif). So emit the
        # count via preprocessor #if directives that #define it to a literal per flavor.
        # Each guard group adds its rows when defined; TITLE => LOGOS so the Title build
        # gets base+LOGOS+TITLE. Order the branches most-specific-first.
        if guards:
            n_base = len(base_rows)
            # Build the per-flavor totals. The only flavors that exist: GHZ (none),
            # LOGOS-only (LOGOS), TITLE (LOGOS+TITLE). Guard the literals accordingly.
            n_logos = len(fe_groups.get("P6_FRONTEND_LOGOS", []))
            n_title = len(fe_groups.get("P6_FRONTEND_TITLE", []))
            # Any extra guards beyond LOGOS/TITLE -> add unconditionally to the relevant
            # branch (none expected today; assert to fail loudly if a 3rd guard appears).
            assert set(guards) <= {"P6_FRONTEND_LOGOS", "P6_FRONTEND_TITLE"}, \
                "build_sheet_bands: add a count branch for new guard(s): %s" % guards
            f.write("#if defined(P6_FRONTEND_TITLE)\n")
            f.write("#define P6_SHEET_PROBE_COUNT %d\n" % (n_base + n_logos + n_title))
            f.write("#elif defined(P6_FRONTEND_LOGOS)\n")
            f.write("#define P6_SHEET_PROBE_COUNT %d\n" % (n_base + n_logos))
            f.write("#else\n")
            f.write("#define P6_SHEET_PROBE_COUNT %d\n" % n_base)
            f.write("#endif\n")
        else:
            f.write("#define P6_SHEET_PROBE_COUNT %d\n" % len(base_rows))
        f.write("static const struct { int32 slot, sx, sy, w, h; uint32 expect; }\n")
        f.write("p6SheetProbes[P6_SHEET_PROBE_COUNT] = {\n")
        f.write(",\n".join(base_rows))
        for g in guards:
            # The leading comma chains this guard group's rows onto the list only when
            # its guard is defined. Groups are independent #ifs so any subset compiles.
            f.write("\n#if defined(%s)\n" % g)
            f.write("    ,\n")
            f.write(",\n".join(fe_groups[g]))
            f.write("\n#endif")
        f.write("\n};\n")

    print("TOTAL %d B; model -> %s; probes -> %s"
          % (total, os.path.normpath(mpath), os.path.normpath(ipath)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
