#!/usr/bin/env python3
"""build_rings.py - emit cd/GHZRINGS.BIN from a Mania Scene1.bin's entity list.

Scene1.bin format (after the layer section that render_scene.parse_scene reads):
    u8   objectClassCount
    For each class:
      16B  MD5 hash of class name (read as 4 LE u32 -- bytes are the raw hash)
      u8   varCount  (var 0 is always 'position' so the explicit list begins at 1)
      For each var in [1..varCount-1]:
        16B  MD5 hash of var name
        u8   var type (RSDK VAR_*: 0=u8 1=i8 2=u16 3=i16 4=u32 5=i32 6=enum
                       7=bool 8=string 9=vector2 10=float 11=color)
      u16  entityCount
      For each entity:
        u2   slotID
        i32 (BE) position.x in RSDK Q24.8 subpixels (>> 8 -> world px)
        i32 (BE) position.y in same
        For var in [1..varCount-1]: read per type (string is u16 len + bytes)

We compute MD5(b"Ring") to identify the Ring class. For each ring entity we
emit (u16 worldX, u16 worldY) big-endian. Output also starts with a u16 ring
count for the Saturn loader.

Usage:
    python tools/build_rings.py extracted/Data/Stages/GHZ \\
        --scene Scene1.bin --out cd/GHZRINGS.BIN
"""
import argparse, hashlib, os, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
from render_scene import R

# RSDK VAR_* enum order (RSDKv5/RSDK/Scene/Object.hpp):
#   0 UINT8(1B)  1 UINT16(2B)  2 UINT32(4B)
#   3 INT8 (1B)  4 INT16 (2B)  5 INT32 (4B)
#   6 ENUM (4B)  7 BOOL  (4B)
#   8 STRING (variable: u16 len + bytes)
#   9 VECTOR2(8B) 10 FLOAT(4B) 11 COLOR(4B)
VAR_SIZE = {0: 1, 1: 2, 2: 4, 3: 1, 4: 2, 5: 4, 6: 4, 7: 4,
            9: 8, 10: 4, 11: 4}
VAR_STRING = 8


def md5_bytes(name):
    return hashlib.md5(name.encode("utf-8")).digest()


def skip_layer_section(r):
    """Advance reader past the SCN header + layer list. Mirrors parse_scene
    in render_scene.py but consumes only -- no need to actually decode."""
    assert r.d[:4] == b"SCN\x00", "not a Scene .bin"
    r.p = 4
    r.skip(0x10)
    sl = r.u8()
    r.skip(sl + 1)
    layer_count = r.u8()
    for _ in range(layer_count):
        r.u8()                                     # visibleInEditor
        nl = r.u8(); r.skip(nl)                    # layer name (Pascal string)
        r.u8(); r.u8()                             # type, draw
        r.u16(); r.u16()                           # xs, ys
        r.u16(); r.u16()                           # parallax, scroll
        sic = r.u16()
        r.skip(sic * 6)                            # scrollInfo
        r.compressed()                             # line-scroll table
        r.compressed()                             # tile layout


def parse_entities(r, target_class_name):
    """Walk the object-class table; return list of (worldX_px, worldY_px) for
    every entity instance whose class hash equals MD5(target_class_name)."""
    target_hash = md5_bytes(target_class_name)
    out = []

    object_count = r.u8()
    for _ in range(object_count):
        cls_hash = r.d[r.p:r.p + 16]; r.p += 16

        var_count = r.u8()
        var_types = []
        for _ in range(var_count - 1):             # var 0 is implicit "position"
            r.skip(16)                             # var hash
            var_types.append(r.u8())

        entity_count = r.u16()
        is_target = (cls_hash == target_hash)

        for _ in range(entity_count):
            r.skip(2)                              # slot ID (LE u16, unused)
            # position.x, position.y are LITTLE-endian i32 in RSDK Q16.16
            # fixed-point world pixels (RSDK Mania uses Q16.16 throughout;
            # >> 16 -> integer world pixel).
            x_q16 = struct.unpack_from("<i", r.d, r.p)[0]; r.p += 4
            y_q16 = struct.unpack_from("<i", r.d, r.p)[0]; r.p += 4

            # Skip the per-var payload for this entity.
            for vt in var_types:
                if vt == VAR_STRING:
                    # u16 LE length, then length * 2 bytes of wide chars.
                    n = struct.unpack_from("<H", r.d, r.p)[0]; r.p += 2
                    r.p += n * 2
                else:
                    r.p += VAR_SIZE.get(vt, 0)

            if is_target:
                out.append((x_q16 >> 16, y_q16 >> 16))

    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage_dir")
    ap.add_argument("--scene", default="Scene1.bin")
    ap.add_argument("--class-name", default="Ring",
                    help="object class to extract (default Ring)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    with open(os.path.join(args.stage_dir, args.scene), "rb") as f:
        r = R(f.read())
    skip_layer_section(r)
    rings = parse_entities(r, args.class_name)

    if not rings:
        print(f"WARNING: no {args.class_name!r} entities found", file=sys.stderr)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(struct.pack(">H", len(rings)))
        for (x, y) in rings:
            # clamp to u16 in case any sit just outside the visible level
            x = max(0, min(0xFFFF, x))
            y = max(0, min(0xFFFF, y))
            f.write(struct.pack(">HH", x, y))

    print(f"{args.out}: {len(rings)} {args.class_name} entities  "
          f"({2 + len(rings)*4} B)")
    if rings:
        xs = [r[0] for r in rings]; ys = [r[1] for r in rings]
        print(f"   x range: {min(xs)}..{max(xs)}")
        print(f"   y range: {min(ys)}..{max(ys)}")


if __name__ == "__main__":
    main()
