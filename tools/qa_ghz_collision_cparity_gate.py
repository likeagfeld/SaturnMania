#!/usr/bin/env python3
"""qa_ghz_collision_cparity_gate.py - Task #180 step 2.

C-parity gate for the decomp-faithful 6-sensor collision flip transform.

WHY THIS GATE LOOKS THE WAY IT DOES (measured environment constraint):
  There is NO host-runnable C compiler in this environment. Probed both the
  Windows host (no gcc/cc/clang/cl) and the docker build image (only the SH-2
  cross-compiler sh-none-elf-gcc-8.2.0, which emits non-host-runnable code,
  plus Python 3.10). So the harness CANNOT compile src/rsdk/collision.c and
  execute it on the host to diff against an oracle column-by-column.

  The thing collision.c uniquely adds over the build_tilemasks.py oracle is the
  SATURN DEVIATION: instead of pre-baking 4 flip variants per tile (the decomp
  Scene.cpp:869-949 path, 512 KB of masks), Saturn ships ONLY the 1024 base
  masks/info per plane (cd/GHZ1MASK.BIN) and applies the flip transform at
  LOOKUP time via the m_floor/m_lwall/m_rwall/m_roof + a_floor/a_lwall/a_rwall/
  a_roof helpers (collision.c:49-113). The correctness question is exactly:
  "does flipping a base mask at lookup produce the SAME byte the decomp would
  have pre-baked into the variant table?"

  This gate answers that question with EXECUTABLE evidence:
    P1 - Python mirror of collision.c's m_*/a_* helpers (literal transcription)
         is run against the real shipped base masks from cd/GHZ1MASK.BIN, and
         compared byte-for-byte to an INDEPENDENT Python bake of the 4 variants
         done the decomp's way (Scene.cpp:869-949). Across all planes x 1024
         tiles x 4 variants x 4 mask-kinds x 16 columns + all 4 angle fields.
    P2 - Structural pass: parse the ACTUAL m_*/a_* bodies out of collision.c
         and assert each contains the exact transform expression the P1 mirror
         uses. This ties the P1 executable proof to the real C source: if
         anyone edits a transform in collision.c, P2 fails until the mirror
         (and thus the proof) is updated to match.

  Combined with qa_ghz_6sensor_gate.py (validates the sensor MODEL on real GHZ
  data) and qa_ghz_fall_through_gate.py (end-to-end runtime proof after the
  Player.c wiring), this is the full RED->GREEN evidence chain for #180.
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MASK_BIN = os.path.join(ROOT, "cd", "GHZ1MASK.BIN")
COLLISION_C = os.path.join(ROOT, "src", "rsdk", "collision.c")

TILE_COUNT = 0x400
TILE_SIZE = 0x10
CPATH_COUNT = 2

# Decomp flip-variant indices (Scene.hpp / Scene.cpp:869-949).
FLIP_NONE, FLIP_X, FLIP_Y, FLIP_XY = 0, 1, 2, 3


def inv4(h):
    """collision.c inv4(): 0xFF passes through, else 0xF - h."""
    return 0xFF if h == 0xFF else (0xF - h) & 0xFF


def to_u8(v):
    return v & 0xFF


# --------------------------------------------------------------------------
# P1a - Python mirror of collision.c:49-113 (the lookup-time flip transform).
#       Literal transcription. Each returns the looked-up byte/angle for the
#       (fx,fy) variant from a BASE tile record.
# --------------------------------------------------------------------------

def m_floor(fl, lw, rw, rf, fx, fy, c):
    if not fx and not fy:
        return fl[c]
    if fx and not fy:
        return fl[0xF - c]
    if not fx and fy:
        return inv4(rf[c])
    return inv4(rf[0xF - c])


def m_lwall(fl, lw, rw, rf, fx, fy, c):
    if not fx and not fy:
        return lw[c]
    if fx and not fy:
        return inv4(rw[c])
    if not fx and fy:
        return lw[0xF - c]
    return inv4(rw[0xF - c])


def m_rwall(fl, lw, rw, rf, fx, fy, c):
    if not fx and not fy:
        return rw[c]
    if fx and not fy:
        return inv4(lw[c])
    if not fx and fy:
        return rw[0xF - c]
    return inv4(lw[0xF - c])


def m_roof(fl, lw, rw, rf, fx, fy, c):
    if not fx and not fy:
        return rf[c]
    if fx and not fy:
        return rf[0xF - c]
    if not fx and fy:
        return inv4(fl[c])
    return inv4(fl[0xF - c])


def a_floor(fa, la, ra, roa, fx, fy):
    if not fx and not fy:
        v = fa
    elif fx and not fy:
        v = -fa
    elif not fx and fy:
        v = -0x80 - roa
    else:
        v = 0x80 + roa
    return to_u8(v)


def a_lwall(fa, la, ra, roa, fx, fy):
    if not fx and not fy:
        v = la
    elif fx and not fy:
        v = -ra
    elif not fx and fy:
        v = -0x80 - la
    else:
        v = 0x80 + ra
    return to_u8(v)


def a_rwall(fa, la, ra, roa, fx, fy):
    if not fx and not fy:
        v = ra
    elif fx and not fy:
        v = -la
    elif not fx and fy:
        v = -0x80 - ra
    else:
        v = 0x80 + la
    return to_u8(v)


def a_roof(fa, la, ra, roa, fx, fy):
    if not fx and not fy:
        v = roa
    elif fx and not fy:
        v = -roa
    elif not fx and fy:
        v = -0x80 - fa
    else:
        v = 0x80 + fa
    return to_u8(v)


# --------------------------------------------------------------------------
# P1b - Independent decomp bake of the 4 variants (Scene.cpp:869-949).
#       Takes a base record, returns the full variant record the decomp
#       would have stored in collisionMasks[p][t + variant*TILE_COUNT].
# --------------------------------------------------------------------------

def bake_variants(fl, lw, rw, rf, fa, la, ra, roa):
    """Returns dict variant -> (floor,lwall,rwall,roof, fAng,lAng,rAng,roAng)
    exactly as Scene.cpp:869-949 fills them."""
    out = {FLIP_NONE: (fl[:], lw[:], rw[:], rf[:], fa, la, ra, roa)}

    # FlipX (Scene.cpp:869-894)
    xf = [0] * TILE_SIZE
    xl = [0] * TILE_SIZE
    xr = [0] * TILE_SIZE
    xrf = [0] * TILE_SIZE
    for c in range(TILE_SIZE):
        xr[c] = 0xFF if lw[c] == 0xFF else 0xF - lw[c]
        xl[c] = 0xFF if rw[c] == 0xFF else 0xF - rw[c]
        xf[c] = fl[0xF - c]
        xrf[c] = rf[0xF - c]
    out[FLIP_X] = (xf, xl, xr, xrf,
                   to_u8(-fa), to_u8(-ra), to_u8(-la), to_u8(-roa))

    # FlipY (Scene.cpp:896-921)
    yf = [0] * TILE_SIZE
    yl = [0] * TILE_SIZE
    yr = [0] * TILE_SIZE
    yrf = [0] * TILE_SIZE
    for c in range(TILE_SIZE):
        yf[c] = 0xFF if rf[c] == 0xFF else 0xF - rf[c]
        yrf[c] = 0xFF if fl[c] == 0xFF else 0xF - fl[c]
        yl[c] = lw[0xF - c]
        yr[c] = rw[0xF - c]
    out[FLIP_Y] = (yf, yl, yr, yrf,
                   to_u8(-0x80 - roa), to_u8(-0x80 - la),
                   to_u8(-0x80 - ra), to_u8(-0x80 - fa))

    # FlipXY (Scene.cpp:923-949) - built FROM the FlipY variant
    yf2, yl2, yr2, yrf2, yfa, yla, yra, yroa = out[FLIP_Y]
    zf = [0] * TILE_SIZE
    zl = [0] * TILE_SIZE
    zr = [0] * TILE_SIZE
    zrf = [0] * TILE_SIZE
    for c in range(TILE_SIZE):
        zr[c] = 0xFF if yl2[c] == 0xFF else 0xF - yl2[c]
        zl[c] = 0xFF if yr2[c] == 0xFF else 0xF - yr2[c]
        zf[c] = yf2[0xF - c]
        zrf[c] = yrf2[0xF - c]
    out[FLIP_XY] = (zf, zl, zr, zrf,
                    to_u8(-yfa), to_u8(-yra), to_u8(-yla), to_u8(-yroa))
    return out


def parse_mask_bin(path):
    """Parse the GMS2 compacted, slot-indexed mask table (#180 step 3).

    Layout: magic 'GMS2', u16 planes/slot_count/tile_count/reserved, then a
    tile_count u16 remap table, then per-plane slot_count*64B masks, then
    per-plane slot_count*8B info. Returns (masks, info, remap, slot_count)
    where masks[p]/info[p] are SLOT-indexed (slot 0 = blank sentinel)."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"GMS2":
        raise SystemExit("bad GHZ1MASK.BIN magic %r (expected GMS2)" % d[:4])
    planes, slot_count, tiles, _res = struct.unpack(">HHHH", d[4:12])
    if planes != CPATH_COUNT or tiles != TILE_COUNT:
        raise SystemExit("unexpected header planes=%d tiles=%d" % (planes, tiles))
    pos = 12
    remap = list(struct.unpack(">%dH" % TILE_COUNT, d[pos:pos + TILE_COUNT * 2]))
    pos += TILE_COUNT * 2
    masks = [[None] * slot_count for _ in range(planes)]
    for p in range(planes):
        for s in range(slot_count):
            fl = list(d[pos:pos + 16]); pos += 16
            lw = list(d[pos:pos + 16]); pos += 16
            rw = list(d[pos:pos + 16]); pos += 16
            rf = list(d[pos:pos + 16]); pos += 16
            masks[p][s] = (fl, lw, rw, rf)
    info = [[None] * slot_count for _ in range(planes)]
    for p in range(planes):
        for s in range(slot_count):
            fa, la, ra, roa, flag = struct.unpack("BBBBB", d[pos:pos + 5])
            pos += 8  # 8-byte stride
            info[p][s] = (fa, la, ra, roa, flag)
    return masks, info, remap, slot_count


def run_p1(masks, info, slot_count):
    """Execute the lookup mirror vs the decomp bake over every shipped slot."""
    mismatches = 0
    first = None
    checked = 0
    variants = ((FLIP_NONE, 0, 0), (FLIP_X, 1, 0),
                (FLIP_Y, 0, 1), (FLIP_XY, 1, 1))
    for p in range(CPATH_COUNT):
        for t in range(slot_count):
            fl, lw, rw, rf = masks[p][t]
            fa, la, ra, roa, _flag = info[p][t]
            baked = bake_variants(fl, lw, rw, rf, fa, la, ra, roa)
            for var, fx, fy in variants:
                bfl, blw, brw, brf, bfa, bla, bra, broa = baked[var]
                for c in range(TILE_SIZE):
                    checked += 4
                    pairs = (
                        ("floor", m_floor(fl, lw, rw, rf, fx, fy, c), bfl[c]),
                        ("lwall", m_lwall(fl, lw, rw, rf, fx, fy, c), blw[c]),
                        ("rwall", m_rwall(fl, lw, rw, rf, fx, fy, c), brw[c]),
                        ("roof",  m_roof(fl, lw, rw, rf, fx, fy, c), brf[c]),
                    )
                    for kind, got, want in pairs:
                        if got != want:
                            mismatches += 1
                            if first is None:
                                first = ("mask", kind, p, t, var, c, got, want)
                # angle fields (one per variant)
                checked += 4
                ap = (
                    ("fAng",  a_floor(fa, la, ra, roa, fx, fy), bfa),
                    ("lAng",  a_lwall(fa, la, ra, roa, fx, fy), bla),
                    ("rAng",  a_rwall(fa, la, ra, roa, fx, fy), bra),
                    ("roAng", a_roof(fa, la, ra, roa, fx, fy), broa),
                )
                for kind, got, want in ap:
                    if got != want:
                        mismatches += 1
                        if first is None:
                            first = ("angle", kind, p, t, var, -1, got, want)
    return checked, mismatches, first


# --------------------------------------------------------------------------
# P2 - Structural pass: the m_*/a_* bodies in collision.c must contain the
#      exact transform expressions the P1 mirror relies on. Keeps the
#      executable proof honest about the REAL source.
# --------------------------------------------------------------------------

# (function name, list of substrings that MUST appear in its body)
P2_EXPECT = {
    "m_floor": ["m->floorMasks[c]", "m->floorMasks[0xF - c]",
                "inv4(m->roofMasks[c])", "inv4(m->roofMasks[0xF - c])"],
    "m_lwall": ["m->lWallMasks[c]", "inv4(m->rWallMasks[c])",
                "m->lWallMasks[0xF - c]", "inv4(m->rWallMasks[0xF - c])"],
    "m_rwall": ["m->rWallMasks[c]", "inv4(m->lWallMasks[c])",
                "m->rWallMasks[0xF - c]", "inv4(m->lWallMasks[0xF - c])"],
    "m_roof":  ["m->roofMasks[c]", "m->roofMasks[0xF - c]",
                "inv4(m->floorMasks[c])", "inv4(m->floorMasks[0xF - c])"],
    "a_floor": ["v = fa", "v = -fa", "v = -0x80 - roa", "v = 0x80 + roa"],
    "a_lwall": ["v = la", "v = -rwa", "v = -0x80 - la", "v = 0x80 + rwa"],
    "a_rwall": ["v = rwa", "v = -la", "v = -0x80 - rwa", "v = 0x80 + la"],
    "a_roof":  ["v = roa", "v = -roa", "v = -0x80 - fa", "v = 0x80 + fa"],
}


def extract_body(src, fname):
    """Return the brace-balanced body of `static ... fname(...) { ... }`."""
    m = re.search(r"\b" + re.escape(fname) + r"\s*\([^)]*\)\s*\{", src)
    if not m:
        return None
    i = m.end() - 1  # at the '{'
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i:j + 1]
    return None


def run_p2():
    with open(COLLISION_C, "r", encoding="utf-8") as f:
        src = f.read()
    missing = []
    for fname, needles in P2_EXPECT.items():
        body = extract_body(src, fname)
        if body is None:
            missing.append("%s: function not found in collision.c" % fname)
            continue
        for needle in needles:
            if needle not in body:
                missing.append("%s: missing transform expr %r" % (fname, needle))
    # inv4 must pass 0xFF through and otherwise compute 0xF - h
    inv4_body = extract_body(src, "inv4")
    if inv4_body is None or "0xFF" not in inv4_body or "0xF" not in inv4_body:
        missing.append("inv4: pass-through/0xF-h body not found")
    return missing


def main():
    if not os.path.exists(MASK_BIN):
        print("FAIL: cd/GHZ1MASK.BIN not found (run tools/build_tilemasks.py)")
        return 1
    if not os.path.exists(COLLISION_C):
        print("FAIL: src/rsdk/collision.c not found")
        return 1

    masks, info, remap, slot_count = parse_mask_bin(MASK_BIN)
    checked, mismatches, first = run_p1(masks, info, slot_count)
    p2_missing = run_p2()

    # P1c - remap sanity: slot 0 is the blank sentinel (all 0xFF masks); every
    # remap entry indexes a valid slot; at least one tile is referenced.
    p1c = []
    blank_fl = masks[0][0][0]
    if any(v != 0xFF for v in blank_fl):
        p1c.append("slot 0 floorMasks not all-0xFF (blank sentinel broken)")
    if max(remap) >= slot_count:
        p1c.append("remap entry %d >= slot_count %d" % (max(remap), slot_count))
    if sum(1 for v in remap if v) == 0:
        p1c.append("remap references zero tiles")

    print("qa_ghz_collision_cparity_gate (Task #180 step 2/3)")
    print("  P1 lookup-vs-decomp-bake: checked=%d comparisons, mismatches=%d"
          % (checked, mismatches))
    if mismatches:
        kind, fld, p, t, var, c, got, want = first
        print("     first mismatch: %s %s plane=%d slot=%d variant=%d col=%d "
              "got=0x%02X want=0x%02X" % (kind, fld, p, t, var, c, got, want))
    print("  P1c GMS2 remap sanity: slot_count=%d referenced=%d  %s"
          % (slot_count, sum(1 for v in remap if v),
             "OK" if not p1c else "BAD"))
    for line in p1c:
        print("     - " + line)
    print("  P2 collision.c structural transform match: %s"
          % ("OK" if not p2_missing else "MISSING %d" % len(p2_missing)))
    for line in p2_missing:
        print("     - " + line)

    if mismatches == 0 and not p2_missing and not p1c:
        print("PASS")
        return 0
    print("FAIL")
    return 1


if __name__ == "__main__":
    sys.exit(main())
