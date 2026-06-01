#!/usr/bin/env python3
"""extract_ghz_spawn.py - Emit cd/GHZ<act>SPWN.BIN with the canonical
Mania-Mode Player spawn coord from extracted/Data/Stages/GHZ/Scene<act>.bin.

Per Phase 2.4f (Task #143) + EXACTLY-A-REPLICA mandate (CLAUDE.md §1) +
memory/post-button-press-canon-scope.md. The pre-fix Saturn build
spawned at world col 0 (a Saturn-fit deviation). The fix: read the
canonical (x,y) from the Scene<act>.bin Player entity slot.

Decomp authority:
  - tools/_decomp_raw/SonicMania_Objects_Global_Player.c:542-672
    Player_Create reads entity->position from the entity-table dispatch
    (rsdkv5-src/RSDKv5/RSDK/Scene/Scene.cpp:528-665 LoadSceneAssets ->
    ProcessObjects).
  - extracted/Data/Stages/GHZ/Scene1.bin Player object hash =
    md5('Player') = 636da1d35e805b00eae0fcd8333f9234. Slot 887 holds
    the canonical Sonic spawn at integer pixel (108, 947).

Saturn-port deviation (acknowledged per docs/MANIA_MODE_PARITY_PLAN.md
§3 policy): the FULL decomp Approach A would (1) hash-resolve the
Player object during rsdk_load_scene, (2) invoke Player_Create with
the entity attribute table, (3) propagate entity->position to the
runtime player_t. That requires substantial new engine plumbing
(scene_ghz currently bypasses rsdk_load_scene). Approach B (this
script) extracts the coord at build time so the runtime CD-DA chain
loads a 12-byte blob with the canonical (x,y) and an attribute flag.
The COORD IS DECOMP-CANONICAL; only the read path is Saturn-fit.

Output format (big-endian, fits in a 12-byte file):
  +0  i32  xpos_px         (integer pixels from Scene1.bin slot 887)
  +4  i32  ypos_px
  +8  u32  characterID     (the Scene1.bin attribute, enum 0..4)

Big-endian to match every other cd/*.BIN asset and the SH-2 native
order (no byte-swap needed on read).
"""
import argparse
import hashlib
import os
import struct
import sys
import zlib


def parse_player_entity(scene_path):
    """Return list of (slot, x_px, y_px, characterID) for every Player
    entity in the scene. The first entity is the active Sonic spawn."""
    with open(scene_path, "rb") as f:
        d = f.read()

    class R:
        def __init__(self, b):
            self.d, self.p = b, 0
        def u8(self):
            v = self.d[self.p]; self.p += 1; return v
        def u16(self):
            v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v
        def u32(self):
            v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v
        def i32(self):
            v = struct.unpack_from("<i", self.d, self.p)[0]; self.p += 4; return v
        def i8(self):
            v = struct.unpack_from("<b", self.d, self.p)[0]; self.p += 1; return v
        def i16(self):
            v = struct.unpack_from("<h", self.d, self.p)[0]; self.p += 2; return v
        def take(self, n):
            v = self.d[self.p:self.p+n]; self.p += n; return v
        def s(self):
            n = self.u8(); v = self.d[self.p:self.p+n]; self.p += n; return v
        def compressed(self):
            total = self.u32()
            _u = struct.unpack_from(">I", self.d, self.p)[0]; self.p += 4
            clen = total - 4
            z = self.d[self.p:self.p+clen]; self.p += clen
            return zlib.decompress(z)

    r = R(d)
    if d[:4] != b"SCN\x00":
        raise RuntimeError(f"{scene_path}: not an SCN scene")
    r.p = 4
    r.take(0x10)
    nl = r.u8()
    r.take(nl + 1)
    layer_count = r.u8()
    for _ in range(layer_count):
        r.u8(); r.s(); r.u8(); r.u8()
        r.u16(); r.u16(); r.u16(); r.u16()
        sic = r.u16()
        for _ in range(sic):
            r.take(6)
        r.compressed(); r.compressed()

    player_hash = hashlib.md5(b"Player").digest()
    characterID_hash = hashlib.md5(b"characterID").digest()

    found = []
    obj_count = r.u8()
    for _ in range(obj_count):
        nhash = r.take(16)
        var_count = r.u8()
        attribs = []
        for _ in range(max(0, var_count - 1)):
            ah = r.take(16)
            at = r.u8()
            attribs.append((ah, at))
        entity_count = r.u16()
        for _ in range(entity_count):
            slot = r.u16()
            x = r.i32()
            y = r.i32()
            attr_vals = []
            for ah, atype in attribs:
                if atype == 0:   v = r.u8()
                elif atype == 1: v = r.u16()
                elif atype == 2: v = r.u32()
                elif atype == 3: v = r.i8()
                elif atype == 4: v = r.i16()
                elif atype == 5: v = r.i32()
                elif atype == 6: v = r.u32()
                elif atype == 7: v = r.u32()
                elif atype == 8:
                    n = r.u16(); r.take(n * 2); v = None
                elif atype == 9:
                    v = (r.i32(), r.i32())
                elif atype == 11: v = r.u32()
                else:
                    raise RuntimeError(f"unsupported attr type {atype}")
                attr_vals.append((ah, v))
            if nhash == player_hash:
                cid = 0
                for ah, v in attr_vals:
                    if ah == characterID_hash:
                        cid = v if isinstance(v, int) else 0
                        break
                # Coords are Q16.16 in the scene-bin; convert to integer px.
                found.append((slot, x >> 16, y >> 16, cid))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--act", type=int, default=1,
                    help="GHZ act number (1 or 2)")
    ap.add_argument("--scene", default=None,
                    help="explicit scene-bin path (default Scene<act>.bin)")
    ap.add_argument("--out", default=None,
                    help="output cd/GHZ<act>SPWN.BIN")
    args = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    scene = args.scene
    if scene is None:
        scene = os.path.join(root, "extracted", "Data", "Stages", "GHZ",
                             f"Scene{args.act}.bin")
    out = args.out
    if out is None:
        out = os.path.join(root, "cd", f"GHZ{args.act}SPWN.BIN")

    if not os.path.exists(scene):
        print(f"ERROR: scene not found: {scene}", file=sys.stderr)
        return 1
    players = parse_player_entity(scene)
    if not players:
        print(f"ERROR: no Player entity in {scene}", file=sys.stderr)
        return 1
    # First entity = active Mania-mode Sonic spawn (the lower-slot one
    # if multiple). Sort by slot ascending; first wins.
    players.sort(key=lambda p: p[0])
    slot, x_px, y_px, cid = players[0]

    blob = struct.pack(">iiI", x_px, y_px, cid)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as f:
        f.write(blob)

    print(f"Scene1 Player entity slot={slot} x={x_px} y={y_px} characterID={cid}")
    print(f"Wrote {len(blob)} B -> {out}")
    if len(players) > 1:
        print(f"  (also saw {len(players) - 1} additional Player slots; "
              f"using lowest-slot as the active spawn)")
        for p in players[1:]:
            print(f"  alt slot={p[0]} x={p[1]} y={p[2]} characterID={p[3]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
