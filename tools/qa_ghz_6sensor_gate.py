#!/usr/bin/env python3
"""qa_ghz_6sensor_gate.py - Task #180 step 1 faithful reference gate.

The "Sonic falls through the ground" bug is a DATA + MODEL defect in the old
heightmap collision (cd/GHZ1SURF.BIN + heightmap-snap in collision.c). This
gate proves the REPLACEMENT data + model is faithful, deterministically and
pit-independently, with no dependence on the defective heightmap.

WHY the old model fell through (measured this session, reproduced in the
informational cross-check at the bottom of this gate):
  GHZ1SURF.BIN claims a solid surface in columns where NO collision tile
  exists - 800 columns at surfY=0 (screen top), 192 at surfY=240, and 384
  mid-level phantom surfaces with no backing tile. A heightmap-snap player
  snaps to / fails on those phantom surfaces and drops. The old gate
  tools/qa_ghz_fall_through_gate.py fires RED on exactly this model
  (STUCK_FALSE_WALL @ col 543) and is kept as the broken-model witness.

WHAT this gate asserts (the faithful replacement, the model that gets ported
to collision.c in step 2):
  The collision floor is derived ONLY from the shipped tile data:
    cd/GHZ1MASK.BIN  - per-tile 6-sensor masks (Scene.cpp:753-866 derive)
    cd/GHZ1COL.BIN   - both FG collision layers' 16-bit layout entries
  For every world column an INDEPENDENT oracle computes the topmost
  plane-A solid-floor surface directly from the layout+mask (a plain
  top-down tile scan). The faithful FindFloorPosition descent (the exact
  logic step-2 collision.c runs - 3-tile window, tolerance, both-layer
  scan, plane-keyed solidity, flip transforms) is then dropped down the
  same column at terminal velocity and MUST land on that oracle surface,
  never tunnelling past it. Columns with no plane-A floor tile are real
  GHZ pits (jump-required) and are skipped - this is what makes the gate
  pit-independent, unlike a hold-right auto-walk that inevitably falls in
  the first pit.

Faithful primitives (the exact logic ported to collision.c, cited to decomp):
  Scene.cpp:809-866       regular/yFlip base-mask derive (baked into GHZ1MASK)
  Scene.cpp:869-949       FLIP_X / FLIP_Y / FLIP_XY transforms (applied here)
  Collision.cpp:2162-2229 FindFloorPosition (the descent)
  Collision.cpp:2406      per-layer collisionLayers scan (FG Low + FG High)
  Zone.c:212              collisionLayers = both fg layers
  Collision.cpp:2398      solid = collisionPlane ? (1<<14) : (1<<12) (DOWN)

P1 assets load (magic + size).  P2 every real-floor column is grounded at the
exact oracle surface (0 tunnels, 0 mismatches).  P3 self-test: stripping a
known floor column's solidity makes the descent miss the oracle - proves the
P2 assertion has teeth (not vacuously green).
Exit 0 = GREEN. Non-zero = RED.
"""
import argparse
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TILE_SIZE = 16
PA_FLOOR = 1 << 12   # plane-A floor solidity bit (TILECOLLISION_DOWN, plane 0)
PB_FLOOR = 1 << 14   # plane-B floor solidity bit (plane 1)
AIR_TOL = 16         # find_floor tolerance during a terminal-velocity descent
NO_FLOOR = 0xFFFF


# --- asset loaders -------------------------------------------------------
def load_masks(path):
    """Read the GMS2 compacted, slot-indexed mask table (#180 step 3) and
    EXPAND it back to a full tile-indexed view so this oracle's physics
    primitives (which index masks[p][tile*64 + ...] / info[p][tile*8 + ...])
    stay byte-for-byte unchanged.

    GMS2 layout: magic 'GMS2', u16 planes/slot_count/tile_count/reserved,
    then tile_count u16 remap (tileID -> slot; 0 = blank), then per-plane
    slot_count*64B masks (slot 0 = blank 0xFF sentinel), then per-plane
    slot_count*8B info. Unreferenced tiles expand to the blank slot."""
    with open(path, "rb") as f:
        d = f.read()
    assert d[:4] == b"GMS2", "bad GHZ1MASK signature (expected GMS2)"
    nplanes, slot_count, ntiles, _res = struct.unpack_from(">HHHH", d, 4)
    pos = 12
    remap = list(struct.unpack_from(">%dH" % ntiles, d, pos))
    pos += ntiles * 2
    mask_block = slot_count * 64
    info_block = slot_count * 8
    masks_base = pos
    info_base = masks_base + nplanes * mask_block
    masks, info = [], []
    for p in range(nplanes):
        sbase = masks_base + p * mask_block
        full = bytearray(ntiles * 64)
        for t in range(ntiles):
            so = sbase + remap[t] * 64
            full[t * 64:t * 64 + 64] = d[so:so + 64]
        masks.append(bytes(full))
    for p in range(nplanes):
        sbase = info_base + p * info_block
        full = bytearray(ntiles * 8)
        for t in range(ntiles):
            so = sbase + remap[t] * 8
            full[t * 8:t * 8 + 8] = d[so:so + 8]
        info.append(bytes(full))
    return masks, info, nplanes, ntiles


def load_layers(path):
    """Read GHZ1COL.BIN. Accepts 'GCOL' (legacy row-major), 'GCO2'
    (#180 step 3d column-major) and 'GCO3' (#180 step 4c RAM-resident
    block-DEFLATE column-major) and always materialises a ROW-MAJOR
    in-memory grid (entry(tx,ty) at grid[tx + (ty<<wshift)]) so this
    oracle's column-scan primitives stay byte-for-byte unchanged."""
    with open(path, "rb") as f:
        d = f.read()
    magic = d[:4]
    if magic == b"GCO3":
        return _load_layers_gco3(d)
    assert magic in (b"GCOL", b"GCO2"), "bad GHZ1COL signature %r" % magic
    colmajor = (magic == b"GCO2")
    nlayers, tsz = struct.unpack_from(">HH", d, 4)
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
    """Decode 'GCO3' (#180 step 4c): block-DEFLATE column-major. Inflate
    every block (raw DEFLATE, zlib wbits=-15) into the full column-major
    layer, then transpose to the same ROW-MAJOR grid the oracle expects."""
    import zlib
    nlayers, tsz, block_cols, _res = struct.unpack_from(">HHHH", d, 4)
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
        colmajor_bytes = bytearray()
        offs = tables[li]
        for b in range(nblk):
            colmajor_bytes += zlib.decompress(d[offs[b]:offs[b + 1]], -15)
        n = xs * ys
        raw = list(struct.unpack(">%dH" % n, bytes(colmajor_bytes)))
        grid = [0] * n
        for tx in range(xs):
            base = tx * ys
            for ty in range(ys):
                grid[tx + (ty << wshift)] = raw[base + ty]
        layers.append({"id": lid, "xs": xs, "ys": ys, "wshift": wshift,
                       "grid": grid})
    return layers


# --- faithful 6-sensor floor primitives (ported verbatim to collision.c) -
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


def floor_angle(info, plane, tile, flipx, flipy):
    fa = info[plane][tile * 8 + 0]
    ra = info[plane][tile * 8 + 3]
    if not flipx and not flipy:
        return fa
    if flipx and not flipy:
        return (-fa) & 0xFF
    if not flipx and flipy:
        return (-0x80 - ra) & 0xFF
    return (-(((-0x80 - ra)) & 0xFF)) & 0xFF


def find_floor(layers, masks, info, plane, px, py, tol):
    """Port of FindFloorPosition (Collision.cpp:2162-2229) over both layers.
    Returns (collided, ty, angle)."""
    solid = PB_FLOOR if plane else PA_FLOOR
    collided = False
    out_ty = py
    out_ang = 0
    start_y = py
    for layer in layers:
        xs, ys, wshift, grid = (layer["xs"], layer["ys"],
                                layer["wshift"], layer["grid"])
        colX, colY = px, py
        cy = (colY & -TILE_SIZE) - TILE_SIZE
        if 0 <= colX < TILE_SIZE * xs:
            for _ in range(3):
                if 0 <= cy < TILE_SIZE * ys:
                    entry = grid[(colX // TILE_SIZE) + ((cy // TILE_SIZE) << wshift)]
                    if entry < NO_FLOOR and (entry & solid):
                        tile = entry & 0x3FF
                        flipx = bool(entry & 0x400)
                        flipy = bool(entry & 0x800)
                        mask = floor_mask(masks, plane, tile, flipx, flipy,
                                          colX & 0xF)
                        ty = cy + mask
                        if mask < 0xFF:
                            if (not collided) or start_y >= ty:
                                if abs(colY - ty) <= tol:
                                    collided = True
                                    out_ty = ty
                                    out_ang = floor_angle(info, plane, tile,
                                                          flipx, flipy)
                                    start_y = ty
                                    break
                cy += TILE_SIZE
    return collided, out_ty, out_ang


# --- independent per-column oracle (plain top-down tile scan, NOT the 3-tile
#     windowed descent) so a descent bug cannot hide behind the oracle -----
def oracle_floor(layers, masks, plane, px):
    """Topmost plane-solid-floor surface in column px, scanning every tile
    row of both layers directly. None if no solid-floor tile exists (pit)."""
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


def descent_floor(layers, masks, info, plane, px, clamp):
    """Drop a sensor from the top at terminal velocity (16px/tick); return
    the floor ty it lands on, or None if it tunnels to the clamp."""
    y = 0
    while y < clamp:
        coll, ty, _ = find_floor(layers, masks, info, plane, px, y, AIR_TOL)
        if coll:
            return ty
        y += TILE_SIZE
    return None


def level_floor_clamp(layers):
    maxy = 0
    for layer in layers:
        xs, ys, wshift, grid = (layer["xs"], layer["ys"],
                                layer["wshift"], layer["grid"])
        for ty in range(ys):
            row = ty << wshift
            for tx in range(xs):
                entry = grid[tx + row]
                if entry < NO_FLOOR and (entry & 0xF000):
                    if ty > maxy:
                        maxy = ty
    return (maxy + 1) * TILE_SIZE + 64


# --- per-column scan -----------------------------------------------------
def scan_columns(layers, masks, info, plane, clamp):
    width = layers[0]["xs"] * TILE_SIZE
    floor_cols = 0
    pit_cols = 0
    tunnels = []     # column had an oracle floor but descent tunnelled
    mismatch = []    # descent landed off the oracle surface
    for x in range(width):
        o = oracle_floor(layers, masks, plane, x)
        if o is None:
            pit_cols += 1
            continue
        floor_cols += 1
        d = descent_floor(layers, masks, info, plane, x, clamp)
        if d is None:
            tunnels.append((x, o))
        elif d != o:
            mismatch.append((x, o, d))
    return width, floor_cols, pit_cols, tunnels, mismatch


# --- informational: why the OLD heightmap was defective ------------------
def heightmap_phantom_count(layers, masks, surf_path):
    """Count columns where the OLD heightmap claims a solid surface but NO
    plane-A floor tile exists there (+-48px). Evidence the old asset, not the
    new model, is the fall-through root cause. Returns None if no heightmap."""
    if not os.path.exists(surf_path):
        return None
    with open(surf_path, "rb") as f:
        d = f.read()
    n = len(d) // 4
    phantom = 0
    for x in range(n):
        hs = struct.unpack_from(">H", d, x * 4)[0]
        if hs == NO_FLOOR:
            continue
        o = oracle_floor(layers, masks, 0, x)
        if o is None or abs(o - hs) > 48:
            phantom += 1
    return n, phantom


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mask", default=os.path.join(ROOT, "cd", "GHZ1MASK.BIN"))
    ap.add_argument("--col", default=os.path.join(ROOT, "cd", "GHZ1COL.BIN"))
    ap.add_argument("--surf", default=os.path.join(ROOT, "cd", "GHZ1SURF.BIN"))
    ap.add_argument("--selftest", action="store_true",
                    help="run only the teeth self-test and exit")
    args = ap.parse_args()

    print("=== Task #180 GHZ 6-sensor faithful gate (mask-model oracle) ===")

    # P1 - assets load
    for pth in (args.mask, args.col):
        if not os.path.exists(pth):
            print("  P1 RED: asset not found: %s" % pth)
            return 1
    masks, info, nplanes, ntiles = load_masks(args.mask)
    layers = load_layers(args.col)
    clamp = level_floor_clamp(layers)
    print("  P1 OK  masks: planes=%d tiles=%d   layers: %s   clamp=%d"
          % (nplanes, ntiles,
             ", ".join("%d(%dx%d)" % (l["id"], l["xs"], l["ys"])
                       for l in layers), clamp))

    # P3 - teeth self-test: strip plane-A floor solidity from the topmost
    # floor tile of a known solid column; the descent must then MISS the
    # oracle's (original) surface. Proves P2 is not vacuously green.
    def run_selftest():
        import copy
        x0 = None
        for x in range(layers[0]["xs"] * TILE_SIZE):
            if oracle_floor(layers, masks, 0, x) is not None:
                x0 = x
                break
        if x0 is None:
            print("  P3 RED: no solid column found for self-test")
            return False
        o0 = oracle_floor(layers, masks, 0, x0)
        mut = copy.deepcopy(layers)
        tx = x0 // TILE_SIZE
        for layer in mut:
            wshift = layer["wshift"]
            for ty in range(layer["ys"]):
                idx = tx + (ty << wshift)
                if layer["grid"][idx] & PA_FLOOR:
                    layer["grid"][idx] &= ~PA_FLOOR   # strip floor solidity
        d = descent_floor(mut, masks, info, 0, x0, clamp)
        ok = (d != o0)   # descent must no longer land on the original surface
        print("  P3 %s  self-test col=%d oracle=%d  post-strip descent=%s "
              "(expect != %d)"
              % ("OK " if ok else "RED", x0, o0,
                 "tunnel" if d is None else d, o0))
        return ok

    if args.selftest:
        return 0 if run_selftest() else 1

    # P2 - every real-floor column grounded at the exact oracle surface
    width, floor_cols, pit_cols, tunnels, mismatch = scan_columns(
        layers, masks, info, 0, clamp)
    print("  P2 scan: width=%d  floor_cols=%d  pit_cols=%d" %
          (width, floor_cols, pit_cols))
    print("  P2 tunnels (floor exists, descent fell through): %d" %
          len(tunnels))
    print("  P2 mismatches (descent != oracle surface): %d" % len(mismatch))
    if tunnels:
        print("     first tunnels:", tunnels[:8])
    if mismatch:
        print("     first mismatches:", mismatch[:8])

    p2_ok = (len(tunnels) == 0 and len(mismatch) == 0)
    p3_ok = run_selftest()

    # informational heightmap cross-check (NOT a pass criterion)
    hp = heightmap_phantom_count(layers, masks, args.surf)
    if hp is not None:
        n, phantom = hp
        print("  [info] OLD heightmap GHZ1SURF.BIN: %d/%d columns claim a "
              "floor with NO backing tile (phantom) -> old-model "
              "fall-through root cause" % (phantom, n))

    if p2_ok and p3_ok:
        print("=== Gate #180 6-sensor: GREEN "
              "(every real-floor column grounded; self-test has teeth) ===")
        return 0
    print("=== Gate #180 6-sensor: RED (P2=%s P3=%s) ===" %
          ("ok" if p2_ok else "FAIL", "ok" if p3_ok else "FAIL"))
    return 1


if __name__ == "__main__":
    sys.exit(main())
