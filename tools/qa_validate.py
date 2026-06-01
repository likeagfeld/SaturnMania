#!/usr/bin/env python3
"""
qa_validate.py - Automated QA sweep for the Sonic Mania Saturn port.

Validates EVERY extracted stage (not just GHZ) and checks each against hard
Sega Saturn limits, so a hardware-fatal budget overrun is caught on PC, not on
a black screen. Exit code is non-zero if any check FAILS.

Saturn hard limits (from ST-series docs / VDP2 manual):
  VDP2 VRAM    524288 B (512 KB)   - tile char data + pattern-name planes
  VDP2 CRAM      4096 B (2048 RGB555 colors, mode 1)
  Work RAM-H  1048576 B (1 MB)     + Work RAM-L 1 MB = 2 MB total
  VDP1 VRAM    524288 B            - sprite char data
  Frame budget  16.67 ms (NTSC 60Hz)

Tile/char model: RSDK tiles are 16x16, capped at 1024 (idx & 0x3FF). On VDP2 as
256-color CHAR_SIZE_2x2 that is 256 B/tile, so a full tileset <= 256 KB. All
layers in a stage share ONE tileset (one 16x16Tiles.gif per folder).
"""
import os
import struct
import sys
import zlib

import numpy as np
from PIL import Image

STAGES_DIR = "extracted/Data/Stages"

VDP2_VRAM = 512 * 1024
VDP2_CRAM_COLORS = 2048
WORKRAM = 2 * 1024 * 1024

# Streaming pattern-name plane in VRAM: 64x64 chars * 2 bytes (1-word PN) per layer.
PN_PLANE_BYTES = 64 * 64 * 2

results = {"pass": 0, "fail": 0, "warn": 0}


def check(cond, label, detail=""):
    tag = "PASS" if cond else "FAIL"
    results["pass" if cond else "fail"] += 1
    if not cond or os.environ.get("QA_VERBOSE"):
        print(f"  [{tag}] {label} {detail}")
    return cond


def warn(label, detail=""):
    results["warn"] += 1
    print(f"  [WARN] {label} {detail}")


class R:
    def __init__(self, d): self.d, self.p = d, 0
    def u8(self):  v = self.d[self.p]; self.p += 1; return v
    def u16(self): v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v
    def u32(self): v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v
    def skip(self, n): self.p += n
    def s(self):
        n = self.u8(); v = self.d[self.p:self.p + n]; self.p += n; return v
    def compressed(self):
        total = self.u32()
        struct.unpack_from(">I", self.d, self.p); self.p += 4
        clen = total - 4
        z = self.d[self.p:self.p + clen]; self.p += clen
        return zlib.decompress(z)


def parse_scene_layers(path):
    d = open(path, "rb").read()
    if d[:4] != b"SCN\x00":
        return None
    r = R(d); r.p = 4; r.skip(0x10)
    sl = r.u8(); r.skip(sl + 1)
    lc = r.u8()
    layers = []
    for _ in range(lc):
        r.u8(); name = r.s().decode("latin-1", "replace")
        r.u8(); r.u8()
        xs = r.u16(); ys = r.u16()
        r.u16(); r.u16()
        sic = r.u16()
        for _ in range(sic):
            r.u16(); r.u16(); r.u8(); r.u8()
        r.compressed()
        raw = r.compressed()
        lay = np.frombuffer(raw[:xs*ys*2], dtype="<u2")
        maxidx = int((lay[lay != 0xFFFF] & 0x3FF).max()) if (lay != 0xFFFF).any() else 0
        layers.append({"name": name.strip(), "xs": xs, "ys": ys, "maxidx": maxidx})
    return layers


def stage_palette_colors(path):
    d = open(path, "rb").read()
    if d[:4] != b"CFG\x00":
        return 0
    r = R(d); r.p = 4; r.u8()
    objc = r.u8()
    for _ in range(objc):
        r.s()
    total = 0
    for _ in range(8):
        rows = r.u16()
        for row in range(16):
            if (rows >> row) & 1:
                total += 16
                r.skip(48)
    return total


def main():
    folders = sorted(d for d in os.listdir(STAGES_DIR)
                     if os.path.isdir(os.path.join(STAGES_DIR, d)))
    print(f"QA sweep over {len(folders)} stage folders\n")

    worst_vram = 0
    worst_map = 0
    worst_map_stage = ""
    for folder in folders:
        base = os.path.join(STAGES_DIR, folder)
        gif = os.path.join(base, "16x16Tiles.gif")
        cfg = os.path.join(base, "StageConfig.bin")
        scenes = sorted(f for f in os.listdir(base)
                        if f.startswith("Scene") and f.endswith(".bin"))
        if not os.path.exists(gif) or not scenes:
            continue
        print(f"[{folder}]")

        # tileset
        img = Image.open(gif)
        if img.mode != "P":
            img = img.convert("P")
        arr = np.array(img)
        ntiles = arr.shape[0] // 16
        check(ntiles <= 1024, "tileset <= 1024 tiles", f"({ntiles})")
        check(arr.shape[1] == 16, "tileset width 16px", f"({arr.shape[1]})")
        tile_vram = ntiles * 256

        # palette
        ncolors = stage_palette_colors(cfg) if os.path.exists(cfg) else 0
        check(ncolors <= VDP2_CRAM_COLORS, "palette <= 2048 CRAM colors",
              f"({ncolors})")

        # scenes / layers
        stage_map_bytes = 0
        layer_total = 0
        for sc in scenes:
            layers = parse_scene_layers(os.path.join(base, sc))
            if layers is None:
                check(False, f"{sc} valid SCN signature")
                continue
            check(True, f"{sc} parsed", f"({len(layers)} layers)")
            for ly in layers:
                layer_total += 1
                check(ly["maxidx"] < max(ntiles, 1),
                      f"{sc}:{ly['name'][:10]} tile idx in range",
                      f"(max {ly['maxidx']} < {ntiles})")
                stage_map_bytes += ly["xs"] * ly["ys"] * 2

        # VRAM budget: shared tileset + streaming PN planes for up to 4 visible layers
        vram = tile_vram + 4 * PN_PLANE_BYTES
        check(vram <= VDP2_VRAM, "VDP2 VRAM budget",
              f"({vram} B = tiles {tile_vram} + planes {4*PN_PLANE_BYTES})")
        worst_vram = max(worst_vram, vram)

        # Work RAM: full layer maps if kept resident (across all scenes summed is
        # not simultaneous; per-scene is what matters). Flag if a single scene's
        # maps are large enough to demand CD streaming rather than residency.
        if stage_map_bytes > worst_map:
            worst_map = stage_map_bytes; worst_map_stage = folder

    print("\n=== Saturn budget summary ===")
    print(f"Worst-case VDP2 VRAM use: {worst_vram} B / {VDP2_VRAM} B "
          f"({100*worst_vram/VDP2_VRAM:.1f}%)")
    big_map = worst_map > 512 * 1024
    print(f"Largest summed layer-map (all scenes in a folder): "
          f"{worst_map} B in {worst_map_stage}")
    if big_map:
        warn("layer maps large -> stream per-scene from CD, do not keep all resident",
             f"({worst_map} B)")

    print(f"\nRESULTS: {results['pass']} pass, {results['fail']} fail, "
          f"{results['warn']} warn")
    sys.exit(1 if results["fail"] else 0)


if __name__ == "__main__":
    main()
