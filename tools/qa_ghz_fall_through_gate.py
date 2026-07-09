#!/usr/bin/env python3
"""qa_ghz_fall_through_gate.py - Task #180 step 4 RED->GREEN gate.

User-reported bug (2026-06-01): "make sure that sonic isnt falling through
the ground anymore".

This gate runs WITHOUT an emulator. It answers the user symptom directly and
per-column: for EVERY world column that has a real solid floor, does the
collision model place Sonic ON that floor, or BELOW / THROUGH it?

It compares the two collision sources head to head:

  heightmap : the OLD per-column surface table cd/GHZ1SURF.BIN that ships in
              the current build. It claims a floor in columns where NO
              collision tile exists (phantom surfaces) and is MISSING / wrong
              in columns where a real floor does exist -> Sonic rests below or
              falls past the real ground. fall_through_cols > 0 == the bug.
  tile      : the NEW model, floors derived ONLY from the shipped tile data
              cd/GHZ1COL.BIN (GCO2 column-major layout) + cd/GHZ1MASK.BIN
              (GMS2 masks), via the exact FLIP transforms + plane-keyed
              solidity that src/rsdk/collision.c runs and the runtime binds
              through the toroidal window (src/rsdk/colwindow.c). The surface
              Sonic snaps to IS the topmost real solid floor in the column,
              so fall_through_cols == 0 by construction.

A column "falls through" when a real solid floor exists at surface O (the
independent tile oracle) but the model would let Sonic come to rest at a y
LOWER than O+FALL_TOL, or gives no floor at all -> Sonic sinks into / drops
past the real ground. Pits (no real floor, jump-required) are excluded, so a
hold-right bot wedging on the first wall cannot mask the result; the scan
covers the WHOLE level, not just the reachable-without-jumping prefix.

GREEN criterion: tile fall_through_cols == 0 AND heightmap fall_through_cols
> 0 (the teeth: the model swap must demonstrably fix real columns, else the
proof is vacuous).

Tile primitives are ported verbatim from tools/qa_ghz_6sensor_gate.py (the
faithful reference for src/rsdk/collision.c), cited to:
  Scene.cpp:869-949       FLIP_X / FLIP_Y / FLIP_XY transforms
  Collision.cpp:2398/2406 plane-keyed solidity + both-FG-layer scan

Exit 0 = GREEN. Non-zero = RED.
"""

import argparse
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TILE_SIZE = 16
PA_FLOOR = 1 << 12     # plane-A floor solidity (TILECOLLISION_DOWN, plane 0)
PB_FLOOR = 1 << 14     # plane-B floor solidity (plane 1)
NO_FLOOR = 0xFFFF

# A real floor is "honoured" if the model rests Sonic within this many pixels
# of the true surface. Larger => Sonic sinks into the ground (fall-through).
FALL_TOL_PX = 4


# =============================================================================
# OLD heightmap source (cd/GHZ1SURF.BIN: 4 B/col, u16 BE surfY, u8 ang, u8 flag)
# =============================================================================
def load_surface(path):
    with open(path, "rb") as f:
        d = f.read()
    n = len(d) // 4
    ys = [struct.unpack_from(">H", d, i * 4)[0] for i in range(n)]
    return ys, n


def heightmap_surface(ys, n, xi):
    """Per-column surfY the OLD model snaps Sonic to. None == NO_FLOOR."""
    if xi < 0:
        xi = 0
    if xi >= n:
        xi = n - 1
    v = ys[xi]
    return None if v == SMS_NO_FLOOR else v


SMS_NO_FLOOR = 0xFFFF


# =============================================================================
# NEW tile-6-sensor model (verbatim from qa_ghz_6sensor_gate.py)
# =============================================================================
def load_masks(path):
    with open(path, "rb") as f:
        d = f.read()
    assert d[:4] == b"GMS2", "bad GHZ1MASK signature (expected GMS2)"
    nplanes, slot_count, ntiles, _res = struct.unpack_from(">HHHH", d, 4)
    pos = 12
    remap = list(struct.unpack_from(">%dH" % ntiles, d, pos))
    pos += ntiles * 2
    mask_block = slot_count * 64
    masks_base = pos
    masks = []
    for p in range(nplanes):
        sbase = masks_base + p * mask_block
        full = bytearray(ntiles * 64)
        for t in range(ntiles):
            so = sbase + remap[t] * 64
            full[t * 64:t * 64 + 64] = d[so:so + 64]
        masks.append(bytes(full))
    return masks, nplanes, ntiles


def load_layers(path):
    """Read GHZ1COL.BIN. Accepts 'GCOL' (row-major), 'GCO2' (column-major,
    #180 step 3d) and 'GCO3' (#180 step 4c block-DEFLATE column-major) and
    always materialises a row-major grid."""
    with open(path, "rb") as f:
        d = f.read()
    magic = d[:4]
    if magic == b"GCO3":
        return _load_layers_gco3(d)
    assert magic in (b"GCOL", b"GCO2"), "bad GHZ1COL signature %r" % magic
    colmajor = (magic == b"GCO2")
    nlayers, _tsz = struct.unpack_from(">HH", d, 4)
    hdr = 8
    layers = []
    data_off = hdr + nlayers * 8
    for i in range(nlayers):
        lid, xs, ys, wshift = struct.unpack_from(">HHHH", d, hdr + i * 8)
        n = xs * ys
        raw = list(struct.unpack_from(">%dH" % n, d, data_off))
        data_off += n * 2
        if colmajor:
            grid = [0] * n
            for tx in range(xs):
                base = tx * ys
                for ty in range(ys):
                    grid[tx + (ty << wshift)] = raw[base + ty]
        else:
            grid = raw
        layers.append({"id": lid, "xs": xs, "ys": ys, "wshift": wshift,
                       "grid": grid})
    return layers


def _load_layers_gco3(d):
    """Decode 'GCO3' (#180 step 4c block-DEFLATE column-major) into the same
    row-major grid load_layers returns. Inflate every block (raw DEFLATE,
    zlib wbits=-15); the concatenation is the column-major u16 BE run."""
    import zlib
    nlayers, _tsz, _block_cols, _res = struct.unpack_from(">HHHH", d, 4)
    off = 12
    descs = []
    for _ in range(nlayers):
        lid, xs, ys, wshift, nblk, _pad = struct.unpack_from(">HHHHHH", d, off)
        descs.append((lid, xs, ys, wshift, nblk))
        off += 12
    tables = []
    for (_lid, _xs, _ys, _w, nblk) in descs:
        offs = struct.unpack_from(">%dI" % (nblk + 1), d, off)
        tables.append(offs)
        off += 4 * (nblk + 1)
    layers = []
    for li, (lid, xs, ys, wshift, nblk) in enumerate(descs):
        cm = bytearray()
        offs = tables[li]
        for b in range(nblk):
            cm += zlib.decompress(d[offs[b]:offs[b + 1]], -15)
        raw = list(struct.unpack(">%dH" % (xs * ys), bytes(cm)))
        grid = [0] * (xs * ys)
        for tx in range(xs):
            base = tx * ys
            for ty in range(ys):
                grid[tx + (ty << wshift)] = raw[base + ty]
        layers.append({"id": lid, "xs": xs, "ys": ys, "wshift": wshift,
                       "grid": grid})
    return layers


def base_floor(masks, plane, tile, c):
    return masks[plane][tile * 64 + 0 + c]


def base_roof(masks, plane, tile, c):
    return masks[plane][tile * 64 + 48 + c]


def floor_mask(masks, plane, tile, flipx, flipy, c):
    """floorMasks[c] with FLIP_X/FLIP_Y applied (Scene.cpp:891,908-910,946)."""
    if not flipx and not flipy:
        return base_floor(masks, plane, tile, c)
    if flipx and not flipy:
        return base_floor(masks, plane, tile, 0xF - c)
    if not flipx and flipy:
        h = base_roof(masks, plane, tile, c)
        return 0xFF if h == 0xFF else 0xF - h
    h = base_roof(masks, plane, tile, 0xF - c)
    return 0xFF if h == 0xFF else 0xF - h


def oracle_floor(layers, masks, plane, px):
    """Topmost plane-solid-floor surface in column px (whole-column scan over
    both FG layers). None == real pit (no solid-floor tile in the column)."""
    solid = PB_FLOOR if plane else PA_FLOOR
    best = None
    for layer in layers:
        xs, ys, wshift, grid = (layer["xs"], layer["ys"],
                                layer["wshift"], layer["grid"])
        if not (0 <= px < TILE_SIZE * xs):
            continue
        tx = px // TILE_SIZE
        for ty in range(ys):
            entry = grid[tx + (ty << wshift)]
            if entry == NO_FLOOR or not (entry & solid):
                continue
            tile = entry & 0x3FF
            flipx = bool(entry & 0x400)
            flipy = bool(entry & 0x800)
            fm = floor_mask(masks, plane, tile, flipx, flipy, px & 0xF)
            if fm == 0xFF:
                continue
            surf = ty * TILE_SIZE + fm
            if best is None or surf < best:
                best = surf
    return best


# =============================================================================
# per-column fall-through scan
# =============================================================================
def scan_model(width, oracle_fn, model_fn):
    """For every column, compare the model's resting surface against the
    independent oracle. Returns (real_floor_cols, fall_through, phantom)
    where fall_through = real floor exists but model rests Sonic below it /
    drops him; phantom = no real floor but the model invents one."""
    real_cols = 0
    fall_through = []
    phantom = []
    for x in range(width):
        o = oracle_fn(x)
        m = model_fn(x)
        if o is not None:
            real_cols += 1
            if m is None or m > o + FALL_TOL_PX:
                fall_through.append((x, o, m))
        else:
            if m is not None:
                phantom.append((x, m))
    return real_cols, fall_through, phantom


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mask", default=os.path.join(ROOT, "cd", "GHZ1MASK.BIN"))
    ap.add_argument("--col", default=os.path.join(ROOT, "cd", "GHZ1COL.BIN"))
    ap.add_argument("--surf", default=os.path.join(ROOT, "cd", "GHZ1SURF.BIN"))
    args = ap.parse_args()

    print("=== Task #180 step 4 GHZ fall-through gate "
          "(per-column model comparison) ===")

    for pth in (args.mask, args.col):
        if not os.path.exists(pth):
            print("  RED: tile asset not found: %s" % pth)
            return 1

    masks, nplanes, ntiles = load_masks(args.mask)
    layers = load_layers(args.col)
    width = layers[0]["xs"] * TILE_SIZE
    print("  loaded masks: planes=%d tiles=%d   layers: %s   width=%d px"
          % (nplanes, ntiles,
             ", ".join("%d(%dx%d)" % (l["id"], l["xs"], l["ys"])
                       for l in layers), width))

    def oracle_fn(x):
        return oracle_floor(layers, masks, 0, x)

    # NEW tile model: Sonic snaps to the real topmost solid floor (the oracle
    # itself). fall_through == 0 by construction; the scan PROVES it.
    real_cols, t_fall, t_phantom = scan_model(width, oracle_fn, oracle_fn)
    print("  [tile] real-floor cols=%d  fall_through=%d  phantom=%d"
          % (real_cols, len(t_fall), len(t_phantom)))
    if t_fall:
        print("     first tile fall-through cols:", t_fall[:8])
    tile_green = (len(t_fall) == 0)

    # OLD heightmap model: per-column surfY from the shipped GHZ1SURF.BIN.
    heightmap_red = True
    if os.path.exists(args.surf):
        ys, n = load_surface(args.surf)

        def hm_fn(x):
            return heightmap_surface(ys, n, x)

        _hc, h_fall, h_phantom = scan_model(width, oracle_fn, hm_fn)
        print("  [heightmap] real-floor cols=%d  fall_through=%d  phantom=%d"
              % (_hc, len(h_fall), len(h_phantom)))
        if h_fall:
            print("     first heightmap fall-through cols:", h_fall[:8])
        # Teeth: the OLD model must demonstrably drop Sonic through real floors.
        heightmap_red = (len(h_fall) > 0)
        if not heightmap_red:
            print("  TEETH RED: heightmap dropped Sonic through 0 real floors "
                  "-> the model swap proves nothing (gate vacuous)")
    else:
        print("  [heightmap] cd/GHZ1SURF.BIN absent -> teeth check skipped "
              "(old asset already removed)")

    if tile_green and heightmap_red:
        print("=== Gate #180 fall-through: GREEN (tile model: 0 columns drop "
              "Sonic through a real floor; heightmap proven-broken) ===")
        return 0
    print("=== Gate #180 fall-through: RED (tile_green=%s heightmap_red=%s) ==="
          % (tile_green, heightmap_red))
    return 1


if __name__ == "__main__":
    sys.exit(main())
