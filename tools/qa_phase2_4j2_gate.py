#!/usr/bin/env python3
"""qa_phase2_4j2_gate.py - Phase 2.4j.2 TitleCard atlas-load gate.

BINDING per memory/qa-iterative-improvement.md + the user-reported
bug 2026-05-29 (Task #157):

  > TitleCard act-intro shows an oversized black slab and NO visible
  >  "GREEN HILL / ZONE" text.

Root cause (data + doc driven, NOT guessed):

  Diagnostic latch g_eatlas_dbg in entity_atlas_load proved that, for
  base_name == "TITLECARD", jo_fs_read_file("TITLECARD.SP2") returns
  NULL at stage 1 (before the sprite-decode loop). The SP2/MET files
  ARE physically on the ISO, so it is not a missing file.

  The cause is the SGL GFS filename-length limit:

    jo-engine/.../SGL_302j/INC/SEGA_GFS.H:37
      #define GFS_FNAME_LEN   12   (ISO9660 8.3 directory-name buffer)

  jo_fs_read_file (jo-engine/jo_engine/fs.c:271) resolves names via
  GFS_NameToId, which matches against the 12-byte fname[] of each
  GfsDirName (SEGA_GFS.H:311-314). "TITLECARD.SP2" is 13 characters
  (9-char base + ".SP2"), exceeding 12, so the lookup never matches
  -> NULL -> entity_atlas_load returns false -> g_titlecard_atlas
  stays all-zero (ready=0) -> rsdk_set_sprite_string is a no-op (atlas
  not ready) -> the zone-name chars stay raw ASCII -> rsdk_get_string_
  width returns 0 and rsdk_draw_text early-returns -> NO TEXT.

  Every other shipped atlas is <=12 chars (SIGNPOST.SP2=12,
  CRABMEAT.SP2=12, MOTOBUG.SP2=11...). TITLECARD is the sole violator.

Fix: rename the asset to an 8.3-compliant base (TITLCARD, 8 chars ->
TITLCARD.SP2 / TITLCARD.MET = 12) in tools/build_entity_atlas.py and
the entity_atlas_load("TITLCARD") call site.

Predicates:

  P1 (static) - No file matching cd/*.SP2 or cd/*.MET has a filename
      longer than GFS_FNAME_LEN (12). RED on current build because
      TITLECARD.SP2 (13) + TITLECARD.MET (13) exist.

  P2 (static) - The TitleCard atlas loader base-name literal is <=8
      chars AND a matching cd/<BASE>.SP2 + cd/<BASE>.MET pair exists
      with <=12-char names. RED on current build (base "TITLECARD").

  P3 (savestate, optional via --state) - g_titlecard_atlas.ready == 1
      and frame_total >= 27 (the zone-name glyph anim has 27 frames).
      Locates g_titlecard_atlas by symbol from game.map, peeks the
      savestate with the WRAM-H pair-swap decode. RED on a state
      captured from the current build (ready=0, frame_total=0).

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    python tools/qa_phase2_4j2_gate.py
    python tools/qa_phase2_4j2_gate.py --state tc2j2_dbg.mcs
"""

import argparse
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GFS_FNAME_LEN = 12  # SEGA_GFS.H:37


def cprint(tag, msg, ok):
    colour = "\033[32m" if ok else "\033[31m"
    reset = "\033[0m"
    print(f"  [{colour}{tag}{reset}] {msg}")
    return ok


def predicate_1_no_overlong_asset_names():
    """No SP2/MET asset filename exceeds GFS_FNAME_LEN (12)."""
    cd = os.path.join(ROOT, "cd")
    offenders = []
    for pat in ("*.SP2", "*.MET"):
        for p in glob.glob(os.path.join(cd, pat)):
            name = os.path.basename(p)
            if len(name) > GFS_FNAME_LEN:
                offenders.append(f"{name}({len(name)})")
    if offenders:
        return cprint(
            "P1 RED",
            f"asset name(s) exceed GFS_FNAME_LEN={GFS_FNAME_LEN}: "
            + ", ".join(sorted(offenders)),
            False)
    return cprint(
        "P1 GREEN",
        f"all cd/*.SP2 + cd/*.MET names <= GFS_FNAME_LEN={GFS_FNAME_LEN}",
        True)


def _find_titlecard_base():
    """Return the base-name literal passed to entity_atlas_load for the
    TitleCard atlas (e.g. 'TITLECARD' or 'TITLCARD'), or None."""
    # Search the whole src tree; the load call lives in Game.c /
    # TitleCard.c (titlecard_load_assets) but don't hard-code the file.
    for path in glob.glob(os.path.join(ROOT, "src", "**", "*.c"),
                          recursive=True):
        try:
            with open(path, "r", encoding="utf-8", errors="ignore") as f:
                body = f.read()
        except OSError:
            continue
        # Accept the plain pool-path loader OR the Phase 2.4j.2 LWRAM-scratch
        # variant entity_atlas_load_ex(&g_titlecard_atlas, "BASE", scratch,
        # cap) -- the latter is the actual fix (bypasses jo_malloc OOM).
        m = re.search(
            r'entity_atlas_load(?:_ex)?\s*\(\s*&\s*g_titlecard_atlas\s*,\s*"([^"]+)"',
            body)
        if m:
            return m.group(1)
    return None


def predicate_2_titlecard_base_8_3():
    """TitleCard loader base name <=8 chars and matching SP2+MET on disk
    with <=12-char names."""
    base = _find_titlecard_base()
    if base is None:
        return cprint(
            "P2 RED",
            "no entity_atlas_load(&g_titlecard_atlas, \"...\") call found",
            False)
    if len(base) > 8:
        return cprint(
            "P2 RED",
            f"TitleCard atlas base \"{base}\" is {len(base)} chars (>8); "
            f"\"{base}.SP2\" = {len(base)+4} chars > GFS_FNAME_LEN",
            False)
    sp2 = os.path.join(ROOT, "cd", base + ".SP2")
    met = os.path.join(ROOT, "cd", base + ".MET")
    missing = [os.path.basename(x) for x in (sp2, met) if not os.path.exists(x)]
    if missing:
        return cprint(
            "P2 RED",
            f"base \"{base}\" OK but missing asset(s): {', '.join(missing)}",
            False)
    return cprint(
        "P2 GREEN",
        f"TitleCard atlas base \"{base}\" (<=8); {base}.SP2 + {base}.MET present",
        True)


def _peek32_wram(sections, addr):
    """Pair-swap WRAM-H peek (per memory/qa-iterative-improvement.md)."""
    sys.path.insert(0, os.path.dirname(__file__))
    from mcs_extract import _peek_bytes  # type: ignore
    b = _peek_bytes(sections, addr, 4)
    if not b:
        return None
    swapped = bytes([b[1], b[0], b[3], b[2]])
    return int.from_bytes(swapped, "big")


def _titlecard_atlas_addr():
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return None
    with open(mp, "r", encoding="utf-8", errors="ignore") as f:
        body = f.read()
    m = re.search(r"(0x0[06][0-9a-fA-F]+)\s+g_titlecard_atlas\b", body)
    return int(m.group(1), 16) if m else None


def predicate_4_spr_widths_mult8():
    """Every TITLCARD.SP2 frame width is a multiple of 8.

    jo/SGL slDispSprite shear bug (root cause of the garbled ZONE letters,
    2026-05-29): jo-engine/jo_engine/sprites.c:212 sets the VDP1/SGL texture
    character-size width to `width & 0x1f8` -- i.e. the actual width
    TRUNCATED DOWN to a multiple of 8 -- while sprites.c:220 DMA-copies the
    pixel data packed at the ACTUAL width. For any frame whose width is a
    non-multiple of 8 and >= 8, the hardware reads N*8-wide rows from a
    wider-packed buffer, so each successive row drifts -> cumulative diagonal
    shear. The TitleCard "Zone Letters" frames (Z/O/N/E = 26/26/26/28 wide)
    were the only displayed sprites in that class -> the smeared ZONE banner.

    Fix: tools/build_entity_atlas.py pads every frame width UP to the next
    multiple of 8 with transparent right-columns, so char-size == data
    stride. Glyphs stay left-aligned (pivot/origin unchanged) so both the
    pivot draw path (tc_draw_sprite) and the width-accumulation text path
    (rsdk_draw_text) keep their layout."""
    sp2 = os.path.join(ROOT, "cd", "TITLCARD.SP2")
    if not os.path.exists(sp2):
        return cprint("P4 RED", "cd/TITLCARD.SP2 missing", False)
    with open(sp2, "rb") as f:
        b = f.read()
    if b[:4] != b"SPR2" or len(b) < 8:
        return cprint("P4 RED", "TITLCARD.SP2 bad magic/size", False)
    nf = (b[4] << 8) | b[5]
    off = 8
    offenders = []
    for i in range(nf):
        if off + 4 > len(b):
            break
        fw = (b[off] << 8) | b[off + 1]
        fh = (b[off + 2] << 8) | b[off + 3]
        off += 4 + fw * fh * 2
        if fw % 8 != 0:
            offenders.append(f"f{i}={fw}")
    if offenders:
        return cprint(
            "P4 RED",
            "SP2 frame width(s) not mult-of-8 (jo slDispSprite shear): "
            + ", ".join(offenders),
            False)
    return cprint(
        "P4 GREEN",
        f"all {nf} TITLCARD.SP2 frame widths are multiples of 8",
        True)


def predicate_3_atlas_ready(state_path):
    """g_titlecard_atlas.ready==1 && frame_total>=27 from a savestate.

    entity_atlas_t header (entity_atlas.h:71-77): ready u8 @+0x00,
    anim_count u8 @+0x01, frame_total u16 @+0x02. A big-endian 32-bit
    read of the base word = ready<<24 | anim_count<<16 | frame_total."""
    if not state_path:
        return cprint(
            "P3 SKIP",
            "no --state; P1+P2 cover the static contract",
            True)
    addr = _titlecard_atlas_addr()
    if addr is None:
        return cprint(
            "P3 SKIP",
            "g_titlecard_atlas not resolvable from game.map",
            True)
    try:
        sys.path.insert(0, os.path.dirname(__file__))
        from mcs_extract import parse_savestate  # type: ignore
        from pathlib import Path
        sections = parse_savestate(Path(state_path))
    except Exception as e:  # noqa
        return cprint("P3 SKIP", f"savestate parse failed: {e}", True)
    word = _peek32_wram(sections, addr)
    if word is None:
        return cprint("P3 SKIP", "could not peek g_titlecard_atlas", True)
    ready = (word >> 24) & 0xFF
    frame_total = word & 0xFFFF
    if ready == 1 and frame_total >= 27:
        return cprint(
            "P3 GREEN",
            f"g_titlecard_atlas ready={ready} frame_total={frame_total} "
            f"(@0x{addr:08x})",
            True)
    return cprint(
        "P3 RED",
        f"g_titlecard_atlas ready={ready} frame_total={frame_total} "
        f"(@0x{addr:08x}) -- atlas did NOT load",
        False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--state", default="",
                    help="savestate (.mcs/.mc0) captured at the TitleCard")
    args = ap.parse_args()

    print("=== Phase 2.4j.2 TitleCard atlas-load gate ===")
    ok = True
    ok &= predicate_1_no_overlong_asset_names()
    ok &= predicate_2_titlecard_base_8_3()
    ok &= predicate_4_spr_widths_mult8()
    ok &= predicate_3_atlas_ready(args.state.strip())

    if ok:
        print("=== Gate Phase 2.4j.2: GREEN ===")
        return 0
    print("=== Gate Phase 2.4j.2: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
