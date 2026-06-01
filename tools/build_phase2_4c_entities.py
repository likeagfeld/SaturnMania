#!/usr/bin/env python3
"""build_phase2_4c_entities.py - Phase 2.4c Task #140 asset builder.

Builds the four CD-side asset files required by the Phase 2.4c entity gate:
  cd/SPIKES.SPR     - multi-frame Saturn BGR1555 sprite (vertical + horizontal).
  cd/GHZ1SPIKE.BIN  - GHZ Act 1 Spikes entity table (u16 BE count + per-entity
                      (u16 BE x, u16 BE y, u8 type, u8 count)).
  cd/BUZZ.SPR       - multi-frame Saturn BGR1555 sprite (body idle / charge /
                      wings / thrust / projectile).
  cd/GHZ1BUZZ.BIN   - GHZ Act 1 BuzzBomber entity table (u16 BE count + per-entity
                      (u16 BE x, u16 BE y, u8 direction)).

Source data:
  - extracted/Data/Stages/GHZ/Scene1.bin     -- entity positions + attribs.
  - extracted/Data/Sprites/Global/Spikes.bin -- decomp Spikes animation script.
  - extracted/Data/Sprites/Global/Items.gif  -- Spikes atlas sheet.
  - extracted/Data/Sprites/GHZ/BuzzBomber.bin -- decomp BuzzBomber anim script.
  - extracted/Data/Sprites/GHZ/Objects.gif    -- BuzzBomber atlas sheet.

Run: python tools/build_phase2_4c_entities.py
"""
from __future__ import annotations
import os
import struct
import sys

# Path setup so the existing tool modules import cleanly.
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import parse_title_entities as pte  # noqa: E402
from convert_ring_sprite import parse_spr, load_sheet, to_rgb555_rgb  # noqa: E402

import numpy as np  # noqa: E402


SPRITE_DIR = os.path.join(REPO, "extracted", "Data", "Sprites")
SCENE_BIN = os.path.join(REPO, "extracted", "Data", "Stages", "GHZ", "Scene1.bin")
CD_DIR = os.path.join(REPO, "cd")


# --- Scene1.bin -> entity table dump (Spikes + BuzzBomber) ------------------

# Names we need pte.parse_entities to resolve. Adding these to KNOWN_NAMES
# patches the hash -> name lookup the parser uses.
pte.KNOWN_NAMES.extend([
    "Spikes", "BuzzBomber", "type", "count", "direction", "moving",
    "stagger", "timer", "planeFilter", "shotRange",
])


def find_class(objects, name):
    for o in objects:
        if o["name"] == name:
            return o
    return None


def get_attr(entity, name, default=0):
    for aname, _label, v in entity["attrs"]:
        if aname == name:
            return v
    return default


def write_spikes_bin(objects, out_path):
    cls = find_class(objects, "Spikes")
    if cls is None:
        print("WARN: Spikes class not present in Scene1.bin")
        return False
    rows = []
    for e in cls["entities"]:
        # Decomp Spikes_Create L333: dir = self->type & 1; type = (type >> 1) & 1.
        # Our Saturn-side encoding (Spikes.c SPIKE_TYPE_*):
        #   0=UP, 1=DOWN, 2=LEFT, 3=RIGHT
        # The decomp's self->type after Create is the SpikeTypes enum directly.
        # In the Scene file the raw "type" attr is the editor-input enum 0..3.
        raw_type = get_attr(e, "type", 0) & 0xFF
        cnt = get_attr(e, "count", 2) & 0xFF
        if cnt < 2:
            cnt = 2
        x_px = int(round(e["x"] / 65536.0))
        y_px = int(round(e["y"] / 65536.0))
        x_px = max(0, min(0xFFFF, x_px))
        y_px = max(0, min(0xFFFF, y_px))
        rows.append((x_px, y_px, raw_type & 0xFF, cnt))

    buf = bytearray()
    buf += struct.pack(">H", len(rows))
    for (x, y, t, c) in rows:
        buf += struct.pack(">HHBB", x, y, t, c)
    with open(out_path, "wb") as f:
        f.write(buf)
    print(f"{out_path}: {len(rows)} spikes ({len(buf)} B)")
    return True


def write_buzz_bin(objects, out_path):
    cls = find_class(objects, "BuzzBomber")
    if cls is None:
        print("WARN: BuzzBomber class not present in Scene1.bin")
        return False
    rows = []
    for e in cls["entities"]:
        # direction attr lives in the editable-var list (decomp Serialize
        # L317-318). Default 0 = FLIP_NONE (face left).
        direction = get_attr(e, "direction", 0) & 1
        x_px = int(round(e["x"] / 65536.0))
        y_px = int(round(e["y"] / 65536.0))
        x_px = max(0, min(0xFFFF, x_px))
        y_px = max(0, min(0xFFFF, y_px))
        rows.append((x_px, y_px, direction))

    buf = bytearray()
    buf += struct.pack(">H", len(rows))
    for (x, y, d) in rows:
        buf += struct.pack(">HHB", x, y, d)
    with open(out_path, "wb") as f:
        f.write(buf)
    print(f"{out_path}: {len(rows)} buzzbombers ({len(buf)} B)")
    return True


# --- .bin animation -> Saturn multi-frame SPR --------------------------------

def render_frame_pivot_centered(atlas_rgba, sx, sy, fw, fh, px, py,
                                canvas_w, canvas_h, origin_x, origin_y):
    """Render one frame into a canvas centered on the animation pivot point."""
    canvas = np.zeros((canvas_h, canvas_w, 4), dtype=np.uint8)
    # Frame top-left in canvas coords:
    dst_x = origin_x + px
    dst_y = origin_y + py
    if dst_x < 0 or dst_y < 0:
        return canvas
    if dst_x + fw > canvas_w or dst_y + fh > canvas_h:
        return canvas
    canvas[dst_y:dst_y + fh, dst_x:dst_x + fw] = atlas_rgba[sy:sy + fh, sx:sx + fw]
    return canvas


def write_multiframe_spr(out_path, canvas_w, canvas_h, frames_rgba):
    """Emit the Saturn SPR format the runtime parses (RING.SPR / BADNIK.SPR)."""
    buf = bytearray()
    buf += struct.pack(">HHH", len(frames_rgba), canvas_w, canvas_h)
    for canvas in frames_rgba:
        for row in range(canvas_h):
            for col in range(canvas_w):
                r, g, b, a = (int(v) for v in canvas[row, col])
                if a == 0:
                    px = 0x0000
                else:
                    px = 0x8000 | to_rgb555_rgb(r, g, b)
                buf += struct.pack(">H", px)
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(buf)
    print(f"{out_path}: {len(frames_rgba)} frames x {canvas_w}x{canvas_h} "
          f"({len(buf)} B)")


def build_anim_spr_pick_frames(bin_path, sheet_picks, out_path,
                                canvas_w=64, canvas_h=64):
    """Pick specific frames from specific animations and assemble a SPR.

    sheet_picks: list of (anim_index, frame_index). Per pick we extract
    that frame and render it pivot-centered into a (canvas_w, canvas_h)
    canvas. Order of output frames matches order of sheet_picks.
    """
    sheets, anims = parse_spr(bin_path)
    sheet_imgs = {i: load_sheet(SPRITE_DIR, name) for i, name in enumerate(sheets)}

    origin_x = canvas_w // 2
    origin_y = canvas_h // 2

    frames_rgba = []
    for (anim_i, frame_i) in sheet_picks:
        if anim_i >= len(anims) or not anims[anim_i]["frames"]:
            # missing anim - emit blank
            frames_rgba.append(np.zeros((canvas_h, canvas_w, 4), dtype=np.uint8))
            continue
        anim = anims[anim_i]
        if frame_i >= len(anim["frames"]):
            frame_i = 0
        (sheet_id, sx, sy, fw, fh, px, py, _dur) = anim["frames"][frame_i]
        atlas = sheet_imgs.get(sheet_id)
        if atlas is None:
            frames_rgba.append(np.zeros((canvas_h, canvas_w, 4), dtype=np.uint8))
            continue
        frames_rgba.append(render_frame_pivot_centered(
            atlas, sx, sy, fw, fh, px, py,
            canvas_w, canvas_h, origin_x, origin_y))

    write_multiframe_spr(out_path, canvas_w, canvas_h, frames_rgba)


def main():
    os.makedirs(CD_DIR, exist_ok=True)

    # 1. Parse Scene1.bin once.
    objects, consumed, total = pte.parse_entities(SCENE_BIN)
    print(f"Scene1.bin: {len(objects)} object classes, {consumed}/{total} bytes")

    # 2. Spikes entity table.
    write_spikes_bin(objects, os.path.join(CD_DIR, "GHZ1SPIKE.BIN"))

    # 3. BuzzBomber entity table.
    write_buzz_bin(objects, os.path.join(CD_DIR, "GHZ1BUZZ.BIN"))

    # 4. Spikes SPR - decomp Spikes.bin has anim 0 = vertical, anim 1 = horizontal
    #    (Spikes_StageLoad L403-404). We pick frame 0 of each, plus frame 2/3
    #    (the glint variants for PSZ2-style — not used by GHZ runtime but
    #    reserved in the SPR slot).
    spikes_bin = os.path.join(SPRITE_DIR, "Global", "Spikes.bin")
    if os.path.exists(spikes_bin):
        build_anim_spr_pick_frames(
            spikes_bin,
            sheet_picks=[(0, 0), (1, 0), (0, 0), (1, 0)],
            out_path=os.path.join(CD_DIR, "SPIKES.SPR"),
            canvas_w=64, canvas_h=64,
        )
    else:
        print(f"WARN: {spikes_bin} not found, SPIKES.SPR not built")

    # 5. BuzzBomber SPR - decomp uses anims 0=body idle, 1=body charge,
    #    2=wings, 3=thrust, 4=projectile (Create L67-78 + StageLoad).
    buzz_bin = os.path.join(SPRITE_DIR, "GHZ", "BuzzBomber.bin")
    if os.path.exists(buzz_bin):
        build_anim_spr_pick_frames(
            buzz_bin,
            sheet_picks=[(0, 0), (1, 0), (2, 0), (3, 0), (4, 0)],
            out_path=os.path.join(CD_DIR, "BUZZ.SPR"),
            canvas_w=64, canvas_h=48,
        )
    else:
        print(f"WARN: {buzz_bin} not found, BUZZ.SPR not built")


if __name__ == "__main__":
    main()
