#!/usr/bin/env python3
"""
parse_title_entities.py - Parse RSDKv5 Title/Scene1.bin entity list.

Scene file format (RSDKv5), per RSDK-Reverse + RSDKv5-Decompilation Scene.cpp:
  Header: "SCN\0", 16 bytes editor metadata, u8 nameLen + name + u8 null.
  Layers: u8 count; each layer:
    u8 visibleInEditor, u8 nameLen + name, u8 type, u8 drawGroup,
    u16 xs, u16 ys, u16 parallax, u16 scroll,
    u16 scrollInfoCount + scrollInfoCount*6 bytes,
    compressed line-scroll table, compressed layout (xs*ys u16).
  Objects: u8 count; each object:
    16 bytes nameHash, u8 attribCount,
    attribCount * (16 bytes attribHash + u8 attribType),
    u16 entityCount, entityCount * (u16 slot, i32 x, i32 y, attrib values).

Attribute type codes (from RSDKv5 RetroEngine.hpp ATTRIBUTE_TYPE_*):
  0=uint8, 1=uint16, 2=uint32, 3=int8, 4=int16, 5=int32,
  6=enum (uint32), 7=bool (uint32), 8=string (u16 length + utf16-LE),
  9=vector2 (i32 x + i32 y), 11=color (uint32).
  (Type 10 'variable' isn't used in shipped Mania data.)

RSDK hashes: standard MD5 of ASCII bytes of the name, stored in the file as
four little-endian u32 words. To match by hashing a known string we compute
md5(name) as raw bytes and then byte-swap each 4-byte group to little-endian.
"""
import argparse
import hashlib
import os
import struct
import sys
import zlib


# Known RSDK object/attribute names whose MD5s we want to recognise. Adding
# names is cheap: the parser hashes on demand and looks up in this table.
KNOWN_NAMES = [
    # Title-scene object classes (visible/active)
    "TitleSetup", "TitleLogo", "TitleSonic", "TitleBG", "Title3DSprite",
    # Engine/globals declared in Title/Scene1.bin but with zero entities --
    # they appear so the slot tables are pre-allocated.
    "Music", "SaveGame", "Localization", "APICallback", "DemoMenu", "Options",
    # Common attribute names
    "type", "frame", "track", "trackFile", "channel", "isLooping", "desc",
    "filter", "tag",
]


def rsdk_hash(name: str) -> bytes:
    """RSDK stores name MD5s as raw bytes on disk (verified empirically against
    Title/Scene1.bin: file bytes match hashlib.md5(name).digest() exactly).
    The Scene.cpp reader treats them as four u32 hash[0..3] read via
    ReadInt32, but the four u32 are stored in the same byte order as MD5's
    standard digest output -- so the network/raw form IS the on-disk form."""
    return hashlib.md5(name.encode("ascii")).digest()


def build_hash_table():
    """name -> on-disk hash (16 bytes). We also store the inverse for lookup."""
    fwd, rev = {}, {}
    for n in KNOWN_NAMES:
        h = rsdk_hash(n)
        fwd[n] = h
        rev[h] = n
    return fwd, rev


class R:
    def __init__(self, d):
        self.d, self.p = d, 0

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]
        self.p += 2
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.p)[0]
        self.p += 4
        return v

    def i8(self):
        v = struct.unpack_from("<b", self.d, self.p)[0]
        self.p += 1
        return v

    def i16(self):
        v = struct.unpack_from("<h", self.d, self.p)[0]
        self.p += 2
        return v

    def take(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def s(self):
        n = self.u8()
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def compressed(self):
        total = self.u32()                                            # bytes following this field
        _usize = struct.unpack_from(">I", self.d, self.p)[0]
        self.p += 4
        clen = total - 4
        z = self.d[self.p:self.p + clen]
        self.p += clen
        return zlib.decompress(z)


def skip_layers(r: R):
    """Walk past the SCN header + all tile layers, leaving r.p at the start
    of the object table. Mirrors the layer block in render_scene.parse_scene."""
    assert r.d[:4] == b"SCN\x00", "not an SCN scene"
    r.p = 4
    r.take(0x10)                              # editor metadata
    nl = r.u8()
    r.take(nl + 1)                            # scene name + trailing null
    layer_count = r.u8()
    for _ in range(layer_count):
        r.u8()                                # visibleInEditor
        r.s()                                 # layer name
        r.u8()                                # type
        r.u8()                                # drawGroup
        r.u16()                               # xs
        r.u16()                               # ys
        r.u16()                               # parallax factor
        r.u16()                               # scroll speed
        sic = r.u16()
        for _ in range(sic):
            r.take(6)                         # scrollInfo: u16,u16,u8,u8
        r.compressed()                        # line-scroll table (unused here)
        r.compressed()                        # tile layout (unused here)


# Attribute readers. Each returns a Python value plus a human label.
def read_attr(r: R, atype: int):
    if atype == 0:
        return ("uint8", r.u8())
    if atype == 1:
        return ("uint16", r.u16())
    if atype == 2:
        return ("uint32", r.u32())
    if atype == 3:
        return ("int8", r.i8())
    if atype == 4:
        return ("int16", r.i16())
    if atype == 5:
        return ("int32", r.i32())
    if atype == 6:
        return ("enum", r.u32())              # stored as 32-bit in shipped data
    if atype == 7:
        return ("bool", bool(r.u32()))        # stored as 32-bit in shipped data
    if atype == 8:
        n = r.u16()
        b = r.take(n * 2)
        return ("string", b.decode("utf-16-le"))
    if atype == 9:
        return ("vector2", (r.i32(), r.i32()))
    if atype == 11:
        return ("color", r.u32())
    raise ValueError(f"unsupported attribute type {atype}")


def parse_entities(path):
    with open(path, "rb") as f:
        d = f.read()
    r = R(d)
    skip_layers(r)
    fwd_hashes, rev_hashes = build_hash_table()

    obj_count = r.u8()
    objects = []
    for _ in range(obj_count):
        nhash = r.take(16)
        name = rev_hashes.get(nhash, f"<unknown {nhash.hex()}>")
        # varCount on disk INCLUDES the implicit slot 0 ("filter"), which is
        # not stored as a hash/type pair and not present in the entity blob.
        # Hence only (varCount - 1) (hash + type) records follow.
        var_count = r.u8()
        attribs = []
        for _ in range(max(0, var_count - 1)):
            ahash = r.take(16)
            atype = r.u8()
            aname = rev_hashes.get(ahash, f"<unknown {ahash.hex()}>")
            attribs.append((aname, atype))
        entity_count = r.u16()
        entities = []
        for _ in range(entity_count):
            slot = r.u16()
            x = r.i32()
            y = r.i32()
            vals = []
            for aname, atype in attribs:
                label, v = read_attr(r, atype)
                vals.append((aname, label, v))
            entities.append({"slot": slot, "x": x, "y": y, "attrs": vals})
        objects.append({"name": name, "hash": nhash, "attribs": attribs,
                        "entities": entities})

    return objects, r.p, len(d)


TITLELOGO_TYPE = {
    0: "EMBLEM", 1: "RIBBON", 2: "GAMETITLE", 3: "POWERLED",
    4: "COPYRIGHT", 5: "RINGBOTTOM", 6: "PRESSSTART", 7: "PLUS",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "scene",
        nargs="?",
        default=r"D:\sonicmaniasaturn\extracted\Data\Stages\Title\Scene1.bin",
    )
    args = ap.parse_args()

    objects, consumed, total = parse_entities(args.scene)

    print("==== Object MD5 hashes (RSDK on-disk form: byte-swapped u32 words) ====")
    for n in ["TitleSetup", "TitleLogo", "TitleSonic", "TitleBG",
              "Title3DSprite", "TitleEggman"]:
        h = rsdk_hash(n)
        raw = hashlib.md5(n.encode()).digest()
        print(f"  {n:14s} raw_md5={raw.hex()}  on_disk={h.hex()}")
    print()

    print(f"==== Entities (consumed {consumed}/{total} bytes) ====")
    for o in objects:
        print(f"\nObject: {o['name']}   ({len(o['entities'])} entities)")
        print(f"  hash: {o['hash'].hex()}")
        print(f"  attribs: {[(a, t) for a, t in o['attribs']]}")
        for e in o["entities"]:
            xp = e["x"] / 65536.0
            yp = e["y"] / 65536.0
            attr_str = ""
            for aname, label, v in e["attrs"]:
                extra = ""
                if (o["name"] == "TitleLogo" and aname == "type"
                        and isinstance(v, int) and v in TITLELOGO_TYPE):
                    extra = f" [{TITLELOGO_TYPE[v]}]"
                attr_str += f"  {aname}({label})={v}{extra}"
            print(f"  slot={e['slot']:3d}  x={xp:8.2f}  y={yp:8.2f}{attr_str}")

    print("\n==== Summary table ====")
    print(f"{'Class':<14}{'Slot':>5}{'Type':>6}{'TypeName':>12}"
          f"{'X(px)':>10}{'Y(px)':>10}")
    for o in objects:
        for e in o["entities"]:
            typ_val = ""
            typ_name = ""
            for aname, label, v in e["attrs"]:
                if aname == "type":
                    typ_val = str(v)
                    if o["name"] == "TitleLogo" and isinstance(v, int):
                        typ_name = TITLELOGO_TYPE.get(v, "")
                    break
            xp = e["x"] / 65536.0
            yp = e["y"] / 65536.0
            print(f"{o['name']:<14}{e['slot']:>5}{typ_val:>6}"
                  f"{typ_name:>12}{xp:>10.2f}{yp:>10.2f}")


if __name__ == "__main__":
    main()
