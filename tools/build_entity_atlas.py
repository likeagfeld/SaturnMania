#!/usr/bin/env python3
"""build_entity_atlas.py - Phase 2.4e Task #142 generalized entity SPR builder.

Replaces per-class one-off scripts (build_phase2_4c_entities.py, etc.)
with a generalized extractor that:

  1. Reads a decomp `.bin` sprite-animation script.
  2. Walks EVERY animation and EVERY frame (no `sheet_picks` subset).
  3. Renders each frame at NATIVE pixel size (no pivot-canvas padding —
     pivot lives in the .MET sidecar instead, saving 3-5x VDP1 VRAM).
  4. Emits Saturn-side cd/<NAME>.SPR (SPR2 format: per-frame variable
     size) + cd/<NAME>.MET (per-anim cadence + per-frame pivot/duration).

Per `memory/decomp-assets-only-no-synthesis.md` Part 2 + `memory/
qa-iterative-improvement.md` v3 Audit 2:
  - Every shipped frame's pixels come from the decomp atlas GIF
    (no synthesis).
  - All decomp anims listed in the .bin are extracted by default; the
    `--anim-subset` flag enables auto-proposed mitigation (drop named
    anims with brief-report rationale).
  - Per-frame `duration` is preserved in the MET sidecar so the
    Saturn animation walker (`src/rsdk/animation.c`) can match decomp
    cadence within 5%.

Decomp .bin format (cited via tools/convert_ring_sprite.py:14-34).
Saturn SPR2 format (per docs/anim_completeness_audit.md "SPR2 format"):

  4 B  ascii "SPR2"
  u16 BE  frame_count
  u16 BE  reserved (0)
  for each frame:
    u16 BE  width
    u16 BE  height
    width*height * u16 BE  BGR1555 pixels (MSB=opaque, 0x0000=transparent)

Saturn MET sidecar format (MET1):

  4 B  ascii "MET1"
  u16 BE  anim_count
  u16 BE  frame_count_total
  for each anim:
    u16 BE  frame_count_in_anim
    u16 BE  speed
    u16 BE  loop_index
    u16 BE  first_frame_index_in_atlas (lets the consumer find the
            anim's frames in the flat atlas without rewalking)
    char[24]  anim_name (null-padded, latin-1)
  for each frame in the atlas (in extraction order):
    u8   anim_id            (which anim it belongs to)
    u8   frame_id_in_anim   (its index within the anim's frame list)
    i16 BE pivot_x
    i16 BE pivot_y
    u16 BE duration
    u16 BE unicode_char     (per-frame glyph codepoint from the decomp
                             .bin frame.unicodeChar; SetSpriteString /
                             GetStringWidth / DrawString (Animation.cpp:
                             179-231, Drawing.cpp:4312-4391) match string
                             chars against this to map glyph -> frame index.
                             0 for non-font anims. Added Phase 2.4j.1.)

Usage:
    python tools/build_entity_atlas.py \\
        --bin extracted/Data/Sprites/Global/Ring.bin \\
        --spr cd/RING.SPR \\
        --met cd/RING.MET \\
        --drop "Hyper Ring"

Run multiple at once via the build-all entry point at the bottom.
"""
from __future__ import annotations
import argparse, os, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from convert_ring_sprite import parse_spr, load_sheet, to_rgb555_rgb  # noqa: E402

SPRITE_DIR = os.path.join(REPO, "extracted", "Data", "Sprites")
CD_DIR     = os.path.join(REPO, "cd")

SPR2_MAGIC = b"SPR2"
MET1_MAGIC = b"MET1"


def encode_pixel(r, g, b, a):
    """Saturn BGR1555 + MSB opaque flag (matches convert_ring_sprite)."""
    if a == 0:
        return 0x0000
    return 0x8000 | to_rgb555_rgb(r, g, b)


def render_frame_native(atlas_rgba, sx, sy, fw, fh):
    """Return BGR1555 bytes for the frame at native size (no canvas pad)."""
    out = bytearray()
    patch = atlas_rgba[sy:sy + fh, sx:sx + fw]
    if patch.shape[0] != fh or patch.shape[1] != fw:
        # Source GIF is smaller than requested -- pad transparent.
        pad = np.zeros((fh, fw, 4), dtype=np.uint8)
        pad[:patch.shape[0], :patch.shape[1]] = patch
        patch = pad
    for row in range(fh):
        for col in range(fw):
            r, g, b, a = (int(v) for v in patch[row, col])
            out += struct.pack(">H", encode_pixel(r, g, b, a))
    return bytes(out)


def build_atlas(bin_path: str, spr_path: str, met_path: str,
                drop_anims: list[str] | None = None,
                sprite_dir: str = SPRITE_DIR,
                frame_caps: dict[str, int] | None = None):
    """Extract every anim+frame from `bin_path`, write SPR2 to `spr_path`
    and MET1 sidecar to `met_path`. If `drop_anims` lists anim names
    (latin-1), they are excluded (mitigation path).

    `frame_caps` maps anim-name -> max leading frame count to keep. This
    is NOT a synthesis path: it keeps the FIRST N authentic decomp frames
    of a named anim and drops the trailing ones. Used only for the HUD
    Numbers anim, where the decomp ships 16 frames (digits 0-9 + hex A-F
    for the base-16 debug-camera readout, HUD.c:529-540). The GHZ Act 1
    HUD only ever calls HUD_DrawNumbersBase10 (decimal), so frames 10-15
    (hex A-F) are never referenced. Keeping 0-9 preserves frameID parity
    for every decimal draw while fitting ENTITY_ATLAS_MAX_FRAMES=34."""
    drop_set = set(s.strip() for s in (drop_anims or []))
    caps = dict(frame_caps or {})

    sheets, anims = parse_spr(bin_path)
    sheet_imgs = {i: load_sheet(sprite_dir, name) for i, name in enumerate(sheets)}

    # Flat atlas frame list with per-frame metadata.
    flat_frames = []   # list of (pixel_bytes, w, h)
    per_frame_meta = [] # list of (anim_id_in_kept_list, frame_id, px, py, dur, uc)
    kept_anim_table = []  # list of dicts mirroring anims, with first_frame_index

    next_anim_id = 0
    next_atlas_idx = 0
    for orig_anim_i, a in enumerate(anims):
        name = a["name"].strip()
        if not a["frames"]:
            # Empty decomp anim slot; skip but do NOT log as drop.
            continue
        if name in drop_set:
            print(f"  DROP {orig_anim_i} {name!r} ({len(a['frames'])} frames) "
                  f"-- mitigation")
            continue

        frames = a["frames"]
        cap = caps.get(name)
        if cap is not None and cap < len(frames):
            print(f"  CAP {orig_anim_i} {name!r}: keep first {cap} of "
                  f"{len(frames)} authentic frames (trailing dropped)")
            frames = frames[:cap]

        first = next_atlas_idx
        for frame_i, (sheet_id, sx, sy, fw, fh, px, py, dur, uc) in enumerate(frames):
            atlas = sheet_imgs.get(sheet_id)
            if atlas is None:
                # Sheet missing; ship a 1x1 transparent placeholder so the
                # atlas frame count still matches the MET.
                flat_frames.append((b"\x00\x00", 1, 1))
                per_frame_meta.append((next_anim_id, frame_i, 0, 0, dur, uc))
                next_atlas_idx += 1
                continue
            px_bytes = render_frame_native(atlas, sx, sy, fw, fh)
            flat_frames.append((px_bytes, fw, fh))
            per_frame_meta.append((next_anim_id, frame_i, px, py, dur, uc))
            next_atlas_idx += 1

        kept_anim_table.append({
            "name": name,
            "frame_count": len(frames),
            "speed": a["speed"],
            "loop": a["loop"],
            "first": first,
        })
        next_anim_id += 1

    # --- Pad frame widths to a multiple of 8 (jo/SGL shear fix) --------
    # jo-engine/jo_engine/sprites.c:212 sets the VDP1/SGL texture
    # character-size width to `width & 0x1f8` -- the actual width TRUNCATED
    # DOWN to a multiple of 8 -- while sprites.c:220 DMA-copies the pixel
    # data packed at the ACTUAL width. A non-multiple-of-8 width >= 8 thus
    # makes the hardware read N*8-wide rows from a wider-packed buffer, so
    # each row drifts -> cumulative diagonal shear (the garbled TitleCard
    # ZONE letters, 2026-05-29). Padding the stored width up to a multiple
    # of 8 with transparent (0x0000) right-columns makes char-size == data
    # stride. Glyphs stay left-aligned (origin/pivot unchanged) so both the
    # pivot draw path and the width-accumulation text path keep their
    # layout. Verified by tools/qa_phase2_4j2_gate.py P4.
    padded_frames = []
    for px_bytes, fw, fh in flat_frames:
        pw = (fw + 7) & ~7
        if pw != fw and fh > 0:
            row_src = fw * 2
            row_pad = (pw - fw) * 2
            buf = bytearray()
            for r in range(fh):
                buf += px_bytes[r * row_src:(r + 1) * row_src]
                buf += b"\x00" * row_pad
            px_bytes = bytes(buf)
            fw = pw
        elif pw != fw:
            # Zero-height frame (e.g. the space glyph): no rows to pad,
            # just advertise the padded width so the char-size is mult-8.
            fw = pw
        padded_frames.append((px_bytes, fw, fh))
    flat_frames = padded_frames

    # --- Write SPR2 ----------------------------------------------------
    spr_buf = bytearray()
    spr_buf += SPR2_MAGIC
    spr_buf += struct.pack(">HH", len(flat_frames), 0)
    for px_bytes, fw, fh in flat_frames:
        spr_buf += struct.pack(">HH", fw, fh)
        spr_buf += px_bytes
    os.makedirs(os.path.dirname(spr_path) or ".", exist_ok=True)
    with open(spr_path, "wb") as f:
        f.write(spr_buf)
    print(f"  {spr_path}: {len(flat_frames)} frames, {len(spr_buf)} B")

    # --- Write MET1 ----------------------------------------------------
    met_buf = bytearray()
    met_buf += MET1_MAGIC
    met_buf += struct.pack(">HH", len(kept_anim_table), len(flat_frames))
    for a in kept_anim_table:
        met_buf += struct.pack(">HHHH",
                               a["frame_count"], a["speed"],
                               a["loop"], a["first"])
        # 24-byte null-padded name (latin-1).
        name_bytes = a["name"].encode("latin-1", "ignore")[:24]
        name_bytes = name_bytes + b"\x00" * (24 - len(name_bytes))
        met_buf += name_bytes
    for (anim_id, frame_id, px, py, dur, uc) in per_frame_meta:
        met_buf += struct.pack(">BBhhHH",
                               anim_id & 0xFF, frame_id & 0xFF,
                               int(px), int(py),
                               dur & 0xFFFF, uc & 0xFFFF)
    os.makedirs(os.path.dirname(met_path) or ".", exist_ok=True)
    with open(met_path, "wb") as f:
        f.write(met_buf)
    print(f"  {met_path}: {len(kept_anim_table)} anims, {len(per_frame_meta)} frames, {len(met_buf)} B")

    return len(flat_frames), len(kept_anim_table)


# === Per-entity build manifests ===========================================
#
# Phase 2.4e in-scope entities. Each row is (bin_path, spr_path, met_path,
# drop_anims, comment). drop_anims is the auto-proposed mitigation list per
# docs/anim_completeness_audit.md "Auto-proposed mitigation" section.

# Output paths use the .SP2 extension to differentiate the new SPR2
# format from the legacy .SPR files that the current Saturn consumers
# read. The Phase 2.4e v2 follow-up will migrate consumers to read
# .SP2 + .MET. Until then both files ship side by side: the existing
# runtime keeps using <name>.SPR (legacy SPR1), and the new build
# pipeline emits <name>.SP2 + <name>.MET. The Phase 2.4e v1 gate
# verifies the SP2/MET pair against the decomp; the legacy SPR is
# left untouched so the current runtime keeps booting.
MANIFEST = [
    {
        "name":  "Ring",
        "bin":   "extracted/Data/Sprites/Global/Ring.bin",
        "spr":   "cd/RING.SP2",
        "met":   "cd/RING.MET",
        "drop":  ["Hyper Ring"],   # Plus-mode only; GHZ Act 1 spawns RING_TYPE_NORMAL
    },
    {
        "name":  "ItemBox",
        "bin":   "extracted/Data/Sprites/Global/ItemBox.bin",
        "spr":   "cd/ITEMBOX.SP2",
        "met":   "cd/ITEMBOX.MET",
        "drop":  ["Snow"],          # IIZ Ice overlay; not used in GHZ
    },
    {
        "name":  "Spring",
        "bin":   "extracted/Data/Sprites/Global/Springs.bin",
        "spr":   "cd/SPRING.SP2",
        "met":   "cd/SPRING.MET",
        "drop":  [],                # all 6 Yellow/Red x V/H/D variants spawn in Scene1
    },
    {
        "name":  "SignPost",
        "bin":   "extracted/Data/Sprites/Global/SignPost.bin",
        "spr":   "cd/SIGNPOST.SP2",
        "met":   "cd/SIGNPOST.MET",
        "drop":  ["Tails", "Knuckles"],  # GHZ Sonic-only; decomp L506-511 playerID switch
    },
    {
        "name":  "Spikes",
        "bin":   "extracted/Data/Sprites/Global/Spikes.bin",
        "spr":   "cd/SPIKES.SP2",
        "met":   "cd/SPIKES.MET",
        "drop":  [],                # both anims single-frame, keep both
    },
    {
        "name":  "Motobug",
        "bin":   "extracted/Data/Sprites/GHZ/Motobug.bin",
        "spr":   "cd/MOTOBUG.SP2",
        "met":   "cd/MOTOBUG.MET",
        "drop":  [],                # all 4 anims needed (Move/Idle/Turn/Puff)
    },
    {
        "name":  "BuzzBomber",
        "bin":   "extracted/Data/Sprites/GHZ/BuzzBomber.bin",
        "spr":   "cd/BUZZ.SP2",
        "met":   "cd/BUZZ.MET",
        "drop":  [],                # all 5 anims needed (Fly/Shoot/Wings/Thrust/Projectile)
    },
    # === Phase 2.4c.2 Task #147 ===========================================
    {
        "name":  "SpikeLog",
        "bin":   "extracted/Data/Sprites/GHZ/SpikeLog.bin",
        "spr":   "cd/SPIKELOG.SP2",
        "met":   "cd/SPIKELOG.MET",
        "drop":  [],
    },
    {
        "name":  "Platform",
        "bin":   "extracted/Data/Sprites/GHZ/Platform.bin",
        "spr":   "cd/PLATFORM.SP2",
        "met":   "cd/PLATFORM.MET",
        "drop":  [],
    },
    {
        "name":  "Newtron",
        "bin":   "extracted/Data/Sprites/GHZ/Newtron.bin",
        "spr":   "cd/NEWTRON.SP2",
        "met":   "cd/NEWTRON.MET",
        "drop":  [],
    },
    # === Phase 2.4h Task: GHZ Act 1 badniks ================================
    {
        "name":  "Chopper",
        "bin":   "extracted/Data/Sprites/GHZ/Chopper.bin",
        "spr":   "cd/CHOPPER.SP2",
        "met":   "cd/CHOPPER.MET",
        "drop":  [],   # Jump/Swim/Charge all driven per decomp Chopper.c states
    },
    {
        "name":  "Crabmeat",
        "bin":   "extracted/Data/Sprites/GHZ/Crabmeat.bin",
        "spr":   "cd/CRABMEAT.SP2",
        "met":   "cd/CRABMEAT.MET",
        "drop":  [],   # Stand/Walk/Shoot/Projectile all used per Crabmeat.c
    },
    {
        "name":  "Batbrain",
        "bin":   "extracted/Data/Sprites/GHZ/Batbrain.bin",
        "spr":   "cd/BATBRAIN.SP2",
        "met":   "cd/BATBRAIN.MET",
        "drop":  [],   # Hang/Fall/Fly all used per Batbrain.c states
    },
    # === Phase 2.4-PLAT Task #155: GHZ platforming entity (Bridge) ========
    #
    # Bridge is the ONLY in-game-visible class of the five 2.4-PLAT ports
    # (CollapsingPlatform/ForceSpin/BreakableWall/SpinBooster are invisible
    # collision triggers / FG-tilemap surfaces, per their decomp .c bodies).
    # GHZ/Bridge.bin ships a single 1-frame "plank" anim; Bridge_Draw
    # (decomp Bridge.c) repeats that plank along the span with a sine-curve
    # depression. Keep the lone anim (no drop).
    {
        "name":  "Bridge",
        "bin":   "extracted/Data/Sprites/GHZ/Bridge.bin",
        "spr":   "cd/BRIDGE.SP2",
        "met":   "cd/BRIDGE.MET",
        "drop":  [],
    },
    # === Phase 2.4i Task #154: authentic HUD (replaces fabricated DIGITS.SPR) =
    #
    # extracted/Data/Sprites/Global/HUD.bin ships 10 anims (75 frames). The
    # GHZ Act 1 HUD (decomp HUD.c:81-348) only ever draws 3 of them:
    #   anim 0  "HUD Elements" : SCORE/TIME/RINGS labels (f0/f1/f3), colon
    #                            (f12), life "x" (f14).
    #   anim 1  "Numbers"      : decimal digits drawn by HUD_DrawNumbersBase10.
    #   anim 2  "Life Icons"   : f0 = Sonic head (non-Plus path, HUD.c:294).
    # The other 7 anims (Player Name / Got Through / Act / Game Over /
    # Time Over / Competition / Hyper Numbers) belong to ActClear / GameOver
    # / Plus-mode and are not part of the in-play HUD, so they are dropped.
    # "Numbers" is capped to its first 10 (decimal) frames -- the trailing
    # A-F hex frames (HUD.c:529 base-16 debug readout) are never referenced
    # in GHZ Act 1. Result: 17 + 10 + 3 = 30 frames < MAX_FRAMES(34).
    {
        "name":  "HUD",
        "bin":   "extracted/Data/Sprites/Global/HUD.bin",
        "spr":   "cd/HUD.SP2",
        "met":   "cd/HUD.MET",
        "drop":  ["Player Name", "Got Through", "Act", "Game Over",
                  "Time Over", "Competition", "Hyper Numbers"],
        "caps":  {"Numbers": 10},
    },
    # === Phase 2.4j.1 Task: TitleCard (GHZ act-intro card) ================
    #
    # extracted/Data/Sprites/Global/TitleCard.bin ships the act-intro
    # glyph font + "ZONE"/act-number sprites. The decomp TitleCard.c
    # composes the zone name from the "Name Letters" font anim via
    # SetSpriteString (Animation.cpp:211-231), which matches each string
    # char against the per-frame frame.unicodeChar -- so EVERY frame of
    # EVERY anim is shipped (drop=[]) and the per-frame unicode is carried
    # into the .MET sidecar (the u16 unicode_char field added above).
    {
        "name":  "TitleCard",
        "bin":   "extracted/Data/Sprites/Global/TitleCard.bin",
        # 8.3-compliant base name. The SGL GFS directory-name buffer is
        # GFS_FNAME_LEN=12 (SEGA_GFS.H:37); a 9-char base ("TITLECARD")
        # yields a 13-char filename that GFS_NameToId can't match, so
        # jo_fs_read_file returns NULL and the atlas never loads. 8-char
        # "TITLCARD" -> "TITLCARD.SP2" = 12 chars fits. (Phase 2.4j.2)
        "spr":   "cd/TITLCARD.SP2",
        "met":   "cd/TITLCARD.MET",
        "drop":  [],
    },
]


def build_all():
    print("Phase 2.4e Task #142 -- generalized entity-atlas build")
    print("=" * 60)
    total_frames = 0
    total_anims = 0
    for entry in MANIFEST:
        bin_full = os.path.join(REPO, entry["bin"])
        spr_full = os.path.join(REPO, entry["spr"])
        met_full = os.path.join(REPO, entry["met"])
        if not os.path.exists(bin_full):
            print(f"SKIP {entry['name']}: {entry['bin']} not found")
            continue
        print(f"== {entry['name']} ==")
        fc, ac = build_atlas(bin_full, spr_full, met_full,
                             drop_anims=entry["drop"],
                             frame_caps=entry.get("caps"))
        total_frames += fc
        total_anims += ac
    print("=" * 60)
    print(f"Total: {total_anims} anims, {total_frames} frames across "
          f"{len(MANIFEST)} entity atlases")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", help="Path to decomp .bin script")
    ap.add_argument("--spr", help="Output SPR2 path")
    ap.add_argument("--met", help="Output MET1 path")
    ap.add_argument("--drop", action="append", default=[],
                    help="Anim name(s) to drop (repeatable)")
    ap.add_argument("--all", action="store_true",
                    help="Build every entity in MANIFEST")
    args = ap.parse_args()

    if args.all or (not args.bin and not args.spr):
        build_all()
        return
    if not (args.bin and args.spr and args.met):
        ap.error("--bin, --spr, --met all required (or pass --all)")
    build_atlas(args.bin, args.spr, args.met, drop_anims=args.drop)


if __name__ == "__main__":
    main()
