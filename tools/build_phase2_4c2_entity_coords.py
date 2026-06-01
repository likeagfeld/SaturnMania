#!/usr/bin/env python3
"""
build_phase2_4c2_entity_coords.py - Phase 2.4c.2 Task #147.

Extract SpikeLog / Platform / Newtron entity rows from
extracted/Data/Stages/GHZ/Scene1.bin and emit Saturn-side per-entity
fixed-record BIN tables for the runtime loaders.

Each output BIN format:
  u16 BE count
  count * record

GHZ1LOG.BIN  (SpikeLog, 6 bytes/entity, 61 entities expected)
  u16 BE x, u16 BE y, u8 frame, u8 _pad
    -- decomp SpikeLog_Serialize: frame (VAR_UINT8) per L145.

GHZ1PLAT.BIN (Platform, 14 bytes/entity, 59 entities expected)
  u16 BE x, u16 BE y, u8 type, u8 collision, i16 BE amp_x, i16 BE amp_y,
  i16 BE speed, i16 BE angle, u8 frameID, u8 hasTension
    -- decomp Platform_Serialize L#: type/amplitude/speed/hasTension/
       frameID/collision/childCount/angle (tileOrigin omitted -- not
       used by any active GHZ Act 1 platform).

GHZ1NEWT.BIN (Newtron, 5 bytes/entity, 21 entities expected)
  u16 BE x, u16 BE y, u8 type, u8 direction
    -- decomp Newtron_Serialize L339-343: type (VAR_UINT8) + direction
       (VAR_UINT8).

Source-of-truth references:
  tools/_decomp_raw/SonicMania_Objects_GHZ_SpikeLog.c::SpikeLog_Serialize L145
  tools/_decomp_raw/SonicMania_Objects_Common_Platform.c::Platform_Serialize
  tools/_decomp_raw/SonicMania_Objects_GHZ_Newtron.c::Newtron_Serialize L339-343
  tools/parse_title_entities.py (Scene.bin reader)
"""
from __future__ import annotations
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import parse_title_entities as PTE   # noqa: E402

SCENE_PATH = os.path.join(REPO, "extracted", "Data", "Stages", "GHZ", "Scene1.bin")
CD_DIR     = os.path.join(REPO, "cd")

# Phase 2.4c.2 extends parse_title_entities.KNOWN_NAMES so SpikeLog /
# Platform / Newtron resolve from their MD5 hashes. The base list does
# not include them.
EXTRA_NAMES = [
    "SpikeLog", "Platform", "Newtron",
    # Editable attribute names (per decomp Serialize):
    "frame",       # SpikeLog
    "type", "amplitude", "speed", "hasTension", "frameID",
    "collision", "tileOrigin", "childCount", "angle",  # Platform
    "direction",   # Newtron
]


def main():
    # Inject extra names for hash resolution.
    for n in EXTRA_NAMES:
        if n not in PTE.KNOWN_NAMES:
            PTE.KNOWN_NAMES.append(n)

    objects, consumed, total = PTE.parse_entities(SCENE_PATH)
    print(f"Scene1.bin: parsed {consumed}/{total} bytes, {len(objects)} classes")

    # Bucket the three target classes.
    by_name = {o["name"]: o for o in objects}
    for needed in ("SpikeLog", "Platform", "Newtron"):
        if needed not in by_name:
            print(f"WARN: {needed} not resolved (hash table miss)")

    # === SpikeLog ===
    rows = []
    log = by_name.get("SpikeLog")
    if log:
        for e in log["entities"]:
            x = max(0, min(0xFFFF, (e["x"] + 0x8000) >> 16))
            y = max(0, min(0xFFFF, (e["y"] + 0x8000) >> 16))
            frame = 0
            for an, _t, v in e["attrs"]:
                if an == "frame":
                    frame = int(v) & 0xFF
            rows.append((x, y, frame))
        print(f"  SpikeLog: {len(rows)} entities")
    out = bytearray()
    out += struct.pack(">H", len(rows))
    for x, y, frame in rows:
        out += struct.pack(">HHBB", x, y, frame, 0)
    with open(os.path.join(CD_DIR, "GHZ1LOG.BIN"), "wb") as f:
        f.write(out)
    print(f"  -> cd/GHZ1LOG.BIN  ({len(out)} B)")

    # === Platform ===
    rows = []
    plat = by_name.get("Platform")
    if plat:
        for e in plat["entities"]:
            x = max(0, min(0xFFFF, (e["x"] + 0x8000) >> 16))
            y = max(0, min(0xFFFF, (e["y"] + 0x8000) >> 16))
            type_v = 0
            amp_x = 0
            amp_y = 0
            speed_v = 0
            angle_v = 0
            frame_id = 0
            tension = 0
            collision = 0
            for an, lbl, v in e["attrs"]:
                if an == "type":
                    type_v = int(v) & 0xFF
                elif an == "amplitude":
                    # vector2 = (i32, i32) — clamp to 16-bit pixel amplitudes.
                    ax, ay = v
                    amp_x = max(-32768, min(32767, ax >> 16))
                    amp_y = max(-32768, min(32767, ay >> 16))
                elif an == "speed":
                    speed_v = max(-32768, min(32767, int(v) if int(v) < 32768
                                              else int(v) - 0x100000000))
                elif an == "angle":
                    raw = int(v) if int(v) < 0x80000000 else int(v) - 0x100000000
                    angle_v = max(-32768, min(32767, raw))
                elif an == "frameID":
                    raw = int(v) & 0xFF
                    frame_id = raw if raw < 0x80 else raw - 0x100
                    frame_id &= 0xFF
                elif an == "hasTension":
                    tension = 1 if v else 0
                elif an == "collision":
                    collision = int(v) & 0xFF
            rows.append((x, y, type_v, collision, amp_x, amp_y,
                         speed_v, angle_v, frame_id, tension))
        print(f"  Platform: {len(rows)} entities")
    out = bytearray()
    out += struct.pack(">H", len(rows))
    for r in rows:
        x, y, t, c, ax, ay, sp, ang, fi, ten = r
        out += struct.pack(">HHBBhhhhBB", x, y, t, c, ax, ay, sp, ang, fi, ten)
    with open(os.path.join(CD_DIR, "GHZ1PLAT.BIN"), "wb") as f:
        f.write(out)
    print(f"  -> cd/GHZ1PLAT.BIN  ({len(out)} B)")

    # === Newtron ===
    rows = []
    new = by_name.get("Newtron")
    if new:
        for e in new["entities"]:
            x = max(0, min(0xFFFF, (e["x"] + 0x8000) >> 16))
            y = max(0, min(0xFFFF, (e["y"] + 0x8000) >> 16))
            type_v = 0
            direction = 0
            for an, _lbl, v in e["attrs"]:
                if an == "type":
                    type_v = int(v) & 0xFF
                elif an == "direction":
                    direction = int(v) & 0xFF
            rows.append((x, y, type_v, direction))
        print(f"  Newtron:  {len(rows)} entities")
    out = bytearray()
    out += struct.pack(">H", len(rows))
    for x, y, t, d in rows:
        out += struct.pack(">HHBB", x, y, t, d)
    with open(os.path.join(CD_DIR, "GHZ1NEWT.BIN"), "wb") as f:
        f.write(out)
    print(f"  -> cd/GHZ1NEWT.BIN ({len(out)} B)")


if __name__ == "__main__":
    main()
